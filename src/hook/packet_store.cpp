#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
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

extern bool PostScriptToUI(const std::wstring& jsCode);

// 封包文本文件格式：每行一个包，带方向标记。
//   [SEND] xx xx xx ...   客户端发出的包
//   [RECV] xx xx xx ...   服务端下发的包
// 以 # 或 // 开头的行是注释；没有标记的裸十六进制行按发送包兼容处理（旧格式）。
static const char* kPacketSendTag = "[SEND] ";
static const char* kPacketRecvTag = "[RECV] ";

// 标记比较不区分大小写：手工编辑过的 [Send]/[Recv] 也要能认。
bool PacketTagEquals(const std::string& tag, const char* expected) {
    size_t i = 0;
    for (; i < tag.size() && expected[i] != '\0'; ++i) {
        if (std::toupper(static_cast<unsigned char>(tag[i])) !=
            std::toupper(static_cast<unsigned char>(expected[i]))) {
            return false;
        }
    }
    return i == tag.size() && expected[i] == '\0';
}


BOOL SavePacketListToFile(const std::wstring& filePath) {
    std::ofstream file(filePath, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        return FALSE;
    }

    std::vector<PACKET> packetsCopy;
    {
        CriticalSectionLock lock(g_packetListCS.Get());
        packetsCopy = g_PacketList;
    }

    file << "# [SEND] = 客户端发出, [RECV] = 服务端下发; 载入时只取 [SEND]" << std::endl;

    // 保存全部被拦截的封包（发送 + 接收），逐行标记方向，便于对着时间线读收发成对关系。
    for (const auto& packet : packetsCopy) {
        if (packet.pData == nullptr || packet.dwSize == 0) {
            continue;
        }
        file << (packet.bSend ? kPacketSendTag : kPacketRecvTag)
             << HexToString(packet.pData, packet.dwSize) << std::endl;
    }

    file.close();
    return TRUE;
}

BOOL LoadPacketListFromFile(const std::wstring& filePath) {
    std::ifstream file(filePath, std::ios::in);
    if (!file.is_open()) {
        return FALSE;
    }

    // 先清空现有封包列表
    ClearPacketList();

    std::string line;
    while (std::getline(file, line)) {
        // 移除空白字符
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        if (!line.empty()) {
            line.erase(line.find_last_not_of(" \t\r\n") + 1);
        }
        if (line.empty()) {
            continue;
        }
        // 注释行（# 或 //）：保存时可能写入的说明不入列表
        if (line[0] == '#' || (line.size() >= 2 && line[0] == '/' && line[1] == '/')) {
            continue;
        }

        // 解析方向标记；没有标记的裸十六进制行按发送包处理（兼容旧文件）。
        bool isSend = true;
        if (line[0] == '[') {
            const size_t close = line.find(']');
            if (close != std::string::npos) {
                const std::string tag = line.substr(1, close - 1);
                isSend = PacketTagEquals(tag, "SEND") || PacketTagEquals(tag, "S");
                line = line.substr(close + 1);
                line.erase(0, line.find_first_not_of(" \t"));
            }
        }

        // 只载入发送包：载入是给「自动发送」用的，接收包没有可发的语义。
        if (!isSend) {
            continue;
        }

        std::vector<BYTE> data = StringToHex(line);
        if (!data.empty()) {
            // 添加到封包列表
            PACKET packet;
            packet.dwSize = static_cast<DWORD>(data.size());
            packet.bSend = TRUE;
            packet.pData = new BYTE[data.size()];
            memcpy(packet.pData, data.data(), data.size());
            packet.dwTime = GetTickCount();

            {
                CriticalSectionLock lock(g_packetListCS.Get());
                g_PacketList.push_back(packet);
            }
        }
    }

    file.close();
    return TRUE;
}

// 一键完成所有选中的日常活动

// GetTickCount 基准 -> 本地时钟字符串，供封包列表显示。
void FormatPacketClock(DWORD tick, wchar_t* out, size_t outCount) {
    const ULONGLONG nowTick = GetTickCount64();
    const ULONGLONG deltaMs = nowTick >= tick ? nowTick - tick : 0;

    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER value;
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    value.QuadPart -= deltaMs * 10000ULL;

    FILETIME shifted;
    shifted.dwLowDateTime = value.LowPart;
    shifted.dwHighDateTime = value.HighPart;
    FILETIME local;
    FileTimeToLocalFileTime(&shifted, &local);

    SYSTEMTIME st;
    FileTimeToSystemTime(&local, &st);
    swprintf_s(out, outCount, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
}

// 追加封包：统一在这里做加锁与容量淘汰，避免各处 push_back 绕过上限。
void AppendPacketToList(const PACKET& packet) {
    if (!packet.pData || packet.dwSize == 0) {
        return;
    }
    CriticalSectionLock lock(g_packetListCS.Get());
    while (g_PacketList.size() >= kMaxPacketListSize) {
        delete[] g_PacketList.front().pData;
        g_PacketList.erase(g_PacketList.begin());
    }
    g_PacketList.push_back(packet);
}

std::vector<PacketSnapshot> SnapshotPacketList() {
    std::vector<PacketSnapshot> snapshot;
    CriticalSectionLock lock(g_packetListCS.Get());
    snapshot.reserve(g_PacketList.size());
    for (const auto& packet : g_PacketList) {
        if (!packet.pData || packet.dwSize == 0) {
            continue;
        }
        PacketSnapshot item;
        item.bSend = packet.bSend != FALSE;
        item.dwTime = packet.dwTime;
        item.data.assign(packet.pData, packet.pData + packet.dwSize);
        snapshot.push_back(std::move(item));
    }
    return snapshot;
}

VOID ClearPacketList() {
    if (!IsWpeHookInitialized()) return;
    
    CriticalSectionLock lock(g_packetListCS.Get());
    for (auto& packet : g_PacketList) {
        delete[] packet.pData;
        packet.pData = nullptr;
    }
    g_PacketList.clear();
}

VOID DeleteSelectedPackets(const std::vector<DWORD>& indices) {
    if (!IsWpeHookInitialized()) return;
    
    CriticalSectionLock lock(g_packetListCS.Get());
    
    // 从大到小排序，避免索引偏移
    std::vector<DWORD> sortedIndices = indices;
    std::sort(sortedIndices.begin(), sortedIndices.end(), std::greater<DWORD>());
    
    for (DWORD index : sortedIndices) {
        if (index < g_PacketList.size()) {
            delete[] g_PacketList[index].pData;
            g_PacketList.erase(g_PacketList.begin() + index);
        }
    }
}

DWORD GetPacketCount() {
    CriticalSectionLock lock(g_packetListCS.Get());
    return static_cast<DWORD>(g_PacketList.size());
}

BOOL GetPacket(DWORD index, PPACKET pPacket) {
    if (!pPacket) return FALSE;
    
    CriticalSectionLock lock(g_packetListCS.Get());
    if (index >= g_PacketList.size()) {
        return FALSE;
    }
    
    *pPacket = g_PacketList[index];
    return TRUE;
}

// 主线任务回包的分发包装：TASK_TALK_BACK 与八卦灵盘共用，两边都要收到。
// TASK_TALK_BACK 分发链：八卦灵盘与主线任务共用一个 opcode，两者都要收到回包。
