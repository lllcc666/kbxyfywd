#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "activity_minigames.h"
#include "activity_states_internal.h"
#include "app_host.h"
#include "battle_six.h"
#include "dungeon_jump.h"
#include "hook_shared.h"
#include "latest_activities.h"
#include "managed_thread.h"
#include "packet_builder.h"
#include "packet_parser.h"
#include "packet_protocol.h"
#include "shuangtai_internal.h"
#include "story_task.h"
#include "story_task_internal.h"
#include "task_zone_internal.h"
#include "ui_bridge.h"
#include "utils.h"
#include "wpe_hook.h"
#include "wpe_hook_internal.h"

// 封包标签化功能
// ============================================================================

/**
 * @brief Opcode 到标签的映射
 */
// ============================================================================
// 劫持功能实现
// ============================================================================

BOOL AddHijackRule(HijackType type, bool forSend, bool forRecv, 
                   const std::string& searchHex, const std::string& replaceHex) {
    if (type == HIJACK_NONE || searchHex.empty()) {
        return FALSE;
    }
    
    HijackRule rule;
    rule.type = type;
    rule.forSend = forSend;
    rule.forRecv = forRecv;
    rule.searchHex = searchHex;
    rule.replaceHex = replaceHex;

    CriticalSectionLock lock(g_hijackRulesCS.Get());
    // UI 每敲一个字符就会 post 一次 add_hijack_rule，规则必须是"当前这一条"，
    // 否则清空前缀会越攒越多：先输入 "53"，再输入 "53 44"，
    // 第一条 "53" 会命中几乎所有封包，后面填什么都被它抢先匹配。
    g_HijackRules.clear();
    g_HijackRules.push_back(rule);
    // 有规则就说明用户要劫持。以前有个 g_bHijackEnabled 总开关，
    // 但界面上根本没有对应的勾选框，永远没人把它打开——整个功能形同不存在。
    g_bHijackEnabled = true;

    return TRUE;
}

VOID ClearHijackRules() {
    CriticalSectionLock lock(g_hijackRulesCS.Get());
    g_HijackRules.clear();
}

VOID SetHijackEnabled(bool enable) {
    g_bHijackEnabled = enable;
}

namespace {

// 在 [p, p+n) 里从 from 开始找 pat，命中返回 true 并给出偏移
bool FindBytesInRange(const BYTE* p, size_t n, const std::vector<BYTE>& pat,
                      size_t from, size_t* at) {
    if (pat.empty() || n < pat.size() || from > n - pat.size()) {
        return false;
    }
    for (size_t i = from; i + pat.size() <= n; ++i) {
        if (memcmp(p + i, pat.data(), pat.size()) == 0) {
            *at = i;
            return true;
        }
    }
    return false;
}

// 判断当前位置是不是一个完整封包的开头，是则给出封包总长度
bool ProbePacketHeader(const BYTE* p, size_t remaining, size_t* packetSize) {
    if (remaining < PacketProtocol::HEADER_SIZE) {
        return false;
    }
    const uint16_t magic = static_cast<uint16_t>(p[0] | (p[1] << 8));
    if (magic != PacketProtocol::MAGIC_NORMAL && magic != PacketProtocol::MAGIC_COMPRESSED) {
        return false;
    }
    const size_t bodyLength = static_cast<size_t>(p[2] | (p[3] << 8));
    const size_t size = PacketProtocol::HEADER_SIZE + bodyLength;
    if (size > remaining) {
        return false;
    }
    *packetSize = size;
    return true;
}

bool IsPacketMagic(const BYTE* p) {
    const uint16_t magic = static_cast<uint16_t>(p[0] | (p[1] << 8));
    return magic == PacketProtocol::MAGIC_NORMAL || magic == PacketProtocol::MAGIC_COMPRESSED;
}

}  // namespace

// 劫持：按"封包"为单位逐条匹配规则，而不是整段缓冲区里瞎找。
//
// 旧实现有三个致命问题：
//   1. 匹配到就把**整包**换成用户填的替换字节（"简单实现：替换整个封包"），
//      长度、头都没了，发出去/收下来都是废字节；
//   2. 接收方向压根没有调用点（老代码里只是一句 TODO），勾了"劫持接收"什么都不会发生；
//   3. 拦截（BLOCK）在接收方向如果按"长度清零"实现，recv 返回 0 就是 EOF，客户端直接断线。
//
// 现在的语义：
//   - 只替换命中的片段，其余字节保持原样，替换后重算包头长度字段；
//   - 发送方向拦截 = 整个封包不发出（调用方返回假成功）；
//   - 接收方向拦截 = 把该封包 opcode 清 0（客户端没有对应监听器，等于没收到），
//     长度不变，绝不会让 recv 返回 0。
bool ProcessHijack(bool bSend, const BYTE* pData, DWORD* pdwSize, std::vector<BYTE>* pModifiedData) {
    if (!g_bHijackEnabled || !pData || !pdwSize || !pModifiedData || *pdwSize == 0) {
        return false;
    }

    CriticalSectionLock lock(g_hijackRulesCS.Get());
    if (g_HijackRules.empty()) {
        return false;
    }

    // 只挂在发送方向的规则，别让接收线程白搬一遍缓冲区
    bool anyRuleForDirection = false;
    for (const auto& rule : g_HijackRules) {
        if (bSend ? rule.forSend : rule.forRecv) {
            anyRuleForDirection = true;
            break;
        }
    }
    if (!anyRuleForDirection) {
        return false;
    }

    const size_t total = static_cast<size_t>(*pdwSize);
    std::vector<BYTE> out;
    out.reserve(total + 16);

    size_t offset = 0;
    bool changed = false;

    while (offset < total) {
        size_t packetSize = 0;
        if (!ProbePacketHeader(pData + offset, total - offset, &packetSize)) {
            break;  // 未对齐 / 半包：剩下的原样带走
        }

        std::vector<BYTE> pkt(pData + offset, pData + offset + packetSize);
        bool dropped = false;
        bool rewritten = false;

        for (const auto& rule : g_HijackRules) {
            if (bSend && !rule.forSend) continue;
            if (!bSend && !rule.forRecv) continue;

            const std::vector<BYTE> search = StringToHex(rule.searchHex);
            size_t at = 0;
            if (!FindBytesInRange(pkt.data(), pkt.size(), search, 0, &at)) {
                continue;
            }

            if (rule.type == HIJACK_BLOCK) {
                if (bSend) {
                    dropped = true;
                } else {
                    memset(pkt.data() + 4, 0, 4);  // recv 不能删包，改成中和 opcode
                    rewritten = true;
                }
                break;
            }

            // 替换：把本封包里所有命中片段换掉，其余字节不动
            const std::vector<BYTE> rep = StringToHex(rule.replaceHex);
            std::vector<BYTE> np;
            np.reserve(pkt.size() + rep.size());
            size_t cursor = 0;
            size_t hit = at;
            while (true) {
                np.insert(np.end(), pkt.begin() + cursor, pkt.begin() + hit);
                np.insert(np.end(), rep.begin(), rep.end());
                cursor = hit + search.size();
                if (!FindBytesInRange(pkt.data(), pkt.size(), search, cursor, &hit)) {
                    break;
                }
            }
            np.insert(np.end(), pkt.begin() + cursor, pkt.end());

            // 替换后长度可能变了：只要头还是合法 magic，就重算长度字段
            if (np.size() >= PacketProtocol::HEADER_SIZE && IsPacketMagic(np.data())) {
                const size_t newBody = np.size() - PacketProtocol::HEADER_SIZE;
                if (newBody > 0xFFFF) {
                    continue;  // 撑爆 16 位长度字段，这条规则不生效
                }
                np[2] = static_cast<BYTE>(newBody & 0xFF);
                np[3] = static_cast<BYTE>((newBody >> 8) & 0xFF);
            }
            pkt.swap(np);
            rewritten = true;
            break;  // 一个封包只套一条规则（WPE 同款行为，避免规则互相打架）
        }

        if (dropped) {
            changed = true;
            offset += packetSize;
            continue;
        }
        if (rewritten) {
            changed = true;
        }
        out.insert(out.end(), pkt.begin(), pkt.end());
        offset += packetSize;
    }

    if (offset < total) {
        out.insert(out.end(), pData + offset, pData + total);
    }

    if (!changed) {
        return false;
    }

    *pModifiedData = std::move(out);
    *pdwSize = static_cast<DWORD>(pModifiedData->size());
    return true;
}

// ============================================================================
// 自动发送功能
// ============================================================================

DWORD SendAllPackets(DWORD intervalMs, DWORD loopCount, 
                     PacketProgressCallback progressCallback) {
    if (loopCount < 1) loopCount = 1;
    
    DWORD totalSent = 0;
    DWORD currentLoop = 0;
    
    // 复制发送包列表（避免锁住太长时间）
    std::vector<std::vector<BYTE>> sendPackets;
    std::vector<std::string> packetLabels;
    
    {
        CriticalSectionLock lock(g_packetListCS.Get());
        
        for (const auto& packet : g_PacketList) {
            if (packet.bSend && packet.dwSize > 0) {
                std::vector<BYTE> data(packet.pData, packet.pData + packet.dwSize);
                sendPackets.push_back(data);
                
                // 获取封包标签
                std::string label = "";
                if (packet.dwSize >= PacketProtocol::HEADER_SIZE) {
                    size_t offset = 0;
                    uint16_t magic = ReadUInt16LE(packet.pData, offset);  // Magic: offset 0-1
                    (void)ReadUInt16LE(packet.pData, offset);             // Length: offset 2-3
                    if (magic == PacketProtocol::MAGIC_NORMAL || magic == PacketProtocol::MAGIC_COMPRESSED) {
                        uint32_t opcode = ReadUInt32LE(packet.pData, offset);  // Opcode: offset 4-7
                        label = GetPacketLabel(opcode, true);
                    }
                }
                packetLabels.push_back(label);
            }
        }
    }
    
    // 循环发送
    g_bStopAutoSend = false;
    for (DWORD loop = 0; loop < loopCount && !g_bStopAutoSend; ++loop) {
        currentLoop = loop + 1;
        
        for (size_t i = 0; i < sendPackets.size() && !g_bStopAutoSend; ++i) {
            // 调用进度回调
            if (progressCallback) {
                progressCallback(currentLoop, (DWORD)i + 1, packetLabels[i]);
            }
            
            // 复制封包数据（用于可能的修改）
            std::vector<BYTE> packetData = sendPackets[i];
            
            // 检查是否为战斗封包 (OP_CLIENT_CLICK_NPC = 1186048)
            // 封包结构: 12字节头部 + 4字节Body (counter)
            if (packetData.size() >= 16) {
                size_t offset = 0;
                uint16_t magic = ReadUInt16LE(packetData.data(), offset);  // Magic: offset 0-1
                (void)ReadUInt16LE(packetData.data(), offset);             // Length: offset 2-3
                
                if (magic == PacketProtocol::MAGIC_NORMAL || magic == PacketProtocol::MAGIC_COMPRESSED) {
                    uint32_t opcode = ReadUInt32LE(packetData.data(), offset);  // Opcode: offset 4-7
                    
                    if (opcode == Opcode::CLICK_NPC) {  // 1186048 - 发起战斗
                        // 获取当前 counter 值（从全局变量）
                        uint32_t currentCounter = g_battleCounter.load();
                        if (currentCounter == 0) {
                            currentCounter = 1;
                        }
                        
                        // 更新 Body 中的 counter（偏移12-15，小端序）
                        packetData[12] = static_cast<BYTE>(currentCounter & 0xFF);
                        packetData[13] = static_cast<BYTE>((currentCounter >> 8) & 0xFF);
                        packetData[14] = static_cast<BYTE>((currentCounter >> 16) & 0xFF);
                        packetData[15] = static_cast<BYTE>((currentCounter >> 24) & 0xFF);
                        
                        // 可选：更新进度回调显示counter已更新
                        if (progressCallback) {
                            char msg[256];
                            sprintf_s(msg, "[Counter已更新: %u] %s", currentCounter, packetLabels[i].c_str());
                            progressCallback(currentLoop, (DWORD)i + 1, msg);
                        }
                    }
                }
            }
            
            if (SendPacket(0, packetData.data(), (DWORD)packetData.size())) {
                totalSent++;
            }
            
            if (intervalMs > 0) {
                Sleep(intervalMs);
            }
        }
    }
    
    return totalSent;
}

VOID StopAutoSend() {
    g_bStopAutoSend = true;
}
