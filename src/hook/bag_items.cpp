#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
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

// 背包映射由收包线程在 ProcessPackageDataResponse 里整体重建，
// 活动线程会在发包前查 position，两边必须用同一把锁。
namespace {
std::mutex g_packItemsMutex;
}


extern bool PostScriptToUI(const std::wstring& jsCode);

// ============ 灵玉相关功能实现 ============

// 解析 JSON 数组字符串，如 ["37","52"] 或 ['37','52']，返回 "37_52" 格式
static std::string ParseIndicesArray(const std::wstring& jsonArray) {
    std::string result;

    // 直接转换为 ANSI 字符串
    int len = WideCharToMultiByte(CP_ACP, 0, jsonArray.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return result;
    }

    std::string jsonA(len, 0);
    WideCharToMultiByte(CP_ACP, 0, jsonArray.c_str(), -1, &jsonA[0], len, nullptr, nullptr);

    // 移除空白，并将单引号替换为双引号
    std::string clean;
    for (char c : jsonA) {
        if (!isspace((unsigned char)c)) {
            if (c == '\'') clean += '"';  // 单引号转双引号
            else clean += c;
        }
    }

    // 查找 [ 和 ]
    size_t start = clean.find('[');
    size_t end = clean.find(']');
    if (start == std::string::npos || end == std::string::npos || end <= start) {
        return result;
    }

    std::string content = clean.substr(start + 1, end - start - 1);

    // 用逗号分割
    size_t pos = 0;
    bool first = true;
    while (pos < content.length()) {
        size_t nextComma = content.find(',', pos);
        std::string item;
        if (nextComma == std::string::npos) {
            item = content.substr(pos);
            pos = content.length();
        } else {
            item = content.substr(pos, nextComma - pos);
            pos = nextComma + 1;
        }

        // 移除引号
        size_t q1 = item.find('"');
        size_t q2 = item.find_last_of('"');
        if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1) {
            item = item.substr(q1 + 1, q2 - q1 - 1);
        }

        if (!item.empty()) {
            if (!first) result += "_";
            result += item;
            first = false;
        }
    }

    return result;
}

// 发送查询灵玉封包（2个封包）
BOOL SendQueryLingyuPacket() {
    // 查询灵玉背包列表（背包中的灵玉）
    // sendMessage(MsgDoc.OP_CLIENT_SPIRIT_EQUIP_ALL.send, userId, [1])
    // 封包格式: Magic(2) + Length(2) + Opcode(4) + Params(4) + Body(8)
    // Body: 数组长度(4字节) + 元素值(4字节)
    
    // 检查是否有卡布号
    if (g_userId == 0) {
        return FALSE;
    }
    
    // 查询封包1: 查询背包中的灵玉 (params=userId, body=[1])
    // Opcode: 1185809 (0x00121811)
    BYTE packet1[20] = {
        0x44, 0x53,  // Magic: "SD"
        0x08, 0x00,  // Length: 8 bytes (数组长度4 + 元素值4)
        0x11, 0x18, 0x12, 0x00,  // Opcode: 0x00121811 (1185809)
        0x00, 0x00, 0x00, 0x00,  // Params: userId (动态填充)
        0x01, 0x00, 0x00, 0x00,  // Body: 数组长度 = 1
        0x01, 0x00, 0x00, 0x00   // Body: 元素值 = 1 (背包灵玉)
    };
    // 填充卡布号到Params字段 (小端序)
    packet1[8] = static_cast<BYTE>(g_userId & 0xFF);
    packet1[9] = static_cast<BYTE>((g_userId >> 8) & 0xFF);
    packet1[10] = static_cast<BYTE>((g_userId >> 16) & 0xFF);
    packet1[11] = static_cast<BYTE>((g_userId >> 24) & 0xFF);

    // 查询封包2: 查询已装备的灵玉 (params=userId, body=[2])
    // Opcode: 1185809 (0x00121811)
    BYTE packet2[20] = {
        0x44, 0x53,  // Magic: "SD"
        0x08, 0x00,  // Length: 8 bytes
        0x11, 0x18, 0x12, 0x00,  // Opcode: 0x00121811 (1185809)
        0x00, 0x00, 0x00, 0x00,  // Params: userId (动态填充)
        0x01, 0x00, 0x00, 0x00,  // Body: 数组长度 = 1
        0x02, 0x00, 0x00, 0x00   // Body: 元素值 = 2 (已装备灵玉)
    };
    // 填充卡布号到Params字段 (小端序)
    packet2[8] = static_cast<BYTE>(g_userId & 0xFF);
    packet2[9] = static_cast<BYTE>((g_userId >> 8) & 0xFF);
    packet2[10] = static_cast<BYTE>((g_userId >> 16) & 0xFF);
    packet2[11] = static_cast<BYTE>((g_userId >> 24) & 0xFF);

    BOOL result1 = SendPacket(0, packet1, sizeof(packet1));

    // 延迟300ms发送第二个封包
    Sleep(300);

    BOOL result2 = SendPacket(0, packet2, sizeof(packet2));

    return result1 && result2;
}

// 发送查询背包封包（2个包）
BOOL SendQueryBagPacket() {
    // 查询封包1: 44 53 00 00 18 18 12 00 00 00 00 00
    BYTE packet1[] = {
        0x44, 0x53,  // Magic: "SD"
        0x00, 0x00,  // Length: 0x0000
        0x18, 0x18, 0x12, 0x00,  // Opcode: 0x00121818
        0x00, 0x00, 0x00, 0x00   // Params: 0
    };

    // 查询封包2: 44 53 00 00 11 10 12 00 FF FF FF FF
    BYTE packet2[] = {
        0x44, 0x53,  // Magic: "SD"
        0x00, 0x00,  // Length: 0x0000
        0x11, 0x10, 0x12, 0x00,  // Opcode: 0x00121011
        0xFF, 0xFF, 0xFF, 0xFF   // Params: 0xFFFFFFFF
    };

    BOOL result1 = SendPacket(0, packet1, sizeof(packet1));

    // 延迟50ms发送第二个封包
    Sleep(50);

    BOOL result2 = SendPacket(0, packet2, sizeof(packet2));

    return result1 && result2;
}

// 发送一键分解封包 - 异步版本
// jsonArray: JSON数组字符串，如 ["37","52"]
struct DecomposeTaskData {
    std::string indices;
    HWND hwnd;  // 窗口句柄用于发送消息
};

// 分解任务完成消息

DWORD WINAPI DecomposeThreadProc(LPVOID lpParam) {
    DecomposeTaskData* taskData = static_cast<DecomposeTaskData*>(lpParam);

    // 立即启用阻止发送功能，阻止 1185792 和 1185809 的发送
    g_blockOpcodeSend = true;

    // 解析索引数组（例如 "62_61" -> ["62", "61"]）
    std::vector<std::string> indexList;
    std::string current;
    for (size_t i = 0; i < taskData->indices.length(); i++) {
        if (taskData->indices[i] == '_') {
            if (!current.empty()) {
                indexList.push_back(current);
                current.clear();
            }
        } else {
            current += taskData->indices[i];
        }
    }
    if (!current.empty()) {
        indexList.push_back(current);
    }

    // 构建分解封包: 44 53 00 00 16 18 12 00 + 索引(4字节LE)
    // 例如: 分解索引 1234 (0x04D2) -> 44 53 00 00 16 18 12 00 D2 04 00 00

    // 将索引数组合并为一个字符串，用下划线连接
    std::string indicesStr;
    for (size_t i = 0; i < indexList.size(); i++) {
        if (i > 0) indicesStr += "_";
        indicesStr += indexList[i];
    }

    BOOL allSuccess = TRUE;

    // 逐个分解每个灵玉
    for (size_t i = 0; i < indexList.size(); i++) {
        const auto& indexStr = indexList[i];

        // 将字符串索引转换为整数
        int indexValue = 0;
        if (!TryParseInt32Decimal(indexStr, indexValue)) {
            allSuccess = FALSE;
            continue;
        }

        // 使用 PacketBuilder 构建分解封包
        // Opcode: 1185814, Params: symmIndex
        auto packet = PacketBuilder()
            .SetOpcode(1185814)
            .SetParams(static_cast<uint32_t>(indexValue))
            .Build();

        // 发送分解封包
        BOOL result = SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
        
        // 生成十六进制封包字符串并发送到UI
        std::string hexPacket = HexToString(packet.data(), static_cast<DWORD>(packet.size())) + " ";

        // 向UI线程发送十六进制封包信息
        if (g_hWnd) {
            HexPacketData* hexData = new HexPacketData();
            hexData->len = hexPacket.length() + 1;
            hexData->data = new char[hexData->len];
            strcpy_s(hexData->data, hexData->len, hexPacket.c_str());
    
            PostMessage(g_hWnd, AppMessage::kDecomposeHexPacket, (WPARAM)hexData, 0);
        }
    
            // 延迟200ms再分解下一个，避免过快发送
            if (&indexStr != &indexList.back()) {
                Sleep(200);
            }
    
            // 更新整体成功状态
            allSuccess = allSuccess && result;
        }

    // 禁用阻止发送功能
    g_blockOpcodeSend = false;

    // 发送完成消息到UI线程
    if (g_hWnd) {
        PostMessage(g_hWnd, AppMessage::kDecomposeComplete, (WPARAM)allSuccess, 0);
    }
    
    // 清理内存
    delete taskData;
    
    return allSuccess;
}

BOOL SendDecomposeLingyuPacket(const std::wstring& jsonArray) {
    if (jsonArray.empty()) {
        return FALSE;
    }

    // 解析 JSON 数组，构造 indices 字符串
    std::string indices = ParseIndicesArray(jsonArray);
    if (indices.empty()) {
        return FALSE;
    }

    // 创建任务数据结构
    DecomposeTaskData* taskData = new DecomposeTaskData();
    taskData->indices = indices;
    taskData->hwnd = g_hWnd;

    // 创建新线程处理分解任务，避免阻塞主线程
    HANDLE hThread = CreateManagedThread(NULL, 0, DecomposeThreadProc, taskData, 0, NULL);
    if (hThread) {
        CloseHandle(hThread);
        return TRUE;
    } else {
        delete taskData;
        return FALSE;
    }
}

// 发送查询妖怪背包封包
// Opcode: 1187329, Params: 0
BOOL SendQueryMonsterPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1187329)
        .SetParams(0)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

BOOL SendReqPackageDataPacket(uint32_t packType) {
    // 封包格式: Opcode=1183761, Params=packType, Body=空
    std::vector<uint8_t> packet = PacketBuilder()
        .SetOpcode(Opcode::REQ_PACKAGE_DATA_SEND)
        .SetParams(packType)
        .Build();
    
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

/**
 * @brief 处理背包数据响应
 * @param packet 封包数据
 */
void ProcessPackageDataResponse(const GamePacket& packet) {
    if (packet.body.size() < 8) {
        UIBridge::Instance().UpdateHelperText(L"背包数据响应格式错误");
        return;
    }
    
    const BYTE* body = packet.body.data();
    size_t offset = 0;
    size_t bodySize = packet.body.size();
    
    // 读取 isocode（占位字段，仅需推进 offset）
    (void)ReadInt32LE(body, offset);
    
    // 读取 packcount（包数量）
    int packcount = ReadInt32LE(body, offset);
    
    // 重建整张映射表：先持锁清空再填充，JS 调用留到锁外。
    std::wstring itemCountsJson = L"{";
    size_t itemTotal = 0;
    {
        std::lock_guard<std::mutex> packLock(g_packItemsMutex);
        g_itemPositionMap.clear();
        g_packItems.clear();
    
    // 遍历每个包
    for (int i = 0; i < packcount; i++) {
        // 检查是否有足够的空间读取 packcode, packid, count (共12字节)
        if (offset + 12 > bodySize) break;
        
        int packcode = ReadInt32LE(body, offset);
        (void)ReadInt32LE(body, offset);  // packid：占位字段，仅需推进 offset
        int count = ReadInt32LE(body, offset);
        
        // 遍历包内物品
        for (int j = 0; j < count; j++) {
            // 检查是否有足够的空间读取 position 和 id (共8字节)
            if (offset + 8 > bodySize) break;
            
            PackItemInfo item;
            item.packcode = packcode;
            item.position = ReadInt32LE(body, offset);
            item.id = ReadInt32LE(body, offset);
            
            if (item.id > 0) {
                // 检查是否有足够的空间读取 count (4字节)
                if (offset + 4 > bodySize) break;
                
                item.count = ReadInt32LE(body, offset);
                
                // 保存到列表
                g_packItems.push_back(item);
                
                // 建立ID -> position映射
                g_itemPositionMap[item.id] = item.position;
            }
        }
    }
    
        // 构建道具数量JSON
        bool first = true;
        for (const auto& item : g_packItems) {
            if (!first) itemCountsJson += L",";
            first = false;
            itemCountsJson += L"\"" + std::to_wstring(item.id) + L"\":" + std::to_wstring(item.count);
        }
        itemCountsJson += L"}";
        itemTotal = g_packItems.size();
    }
    
    std::wstring script = L"if(window.updateItemCounts) { window.updateItemCounts(" + itemCountsJson + L"); }";
    UIBridge::Instance().ExecuteJS(script);
    
    // 更新UI
    wchar_t msg[128];
    swprintf_s(msg, L"背包数据已更新，共 %zu 个物品", itemTotal);
    UIBridge::Instance().UpdateHelperText(msg);
}

/**
 * @brief 购买道具
 * @param itemId 道具ID
 * @param count 购买数量
 * @return 发送是否成功
 * @note 封包格式: Opcode=1183760, Params=itemId, Body=[count]
 */
BOOL SendBuyGoodsPacket(uint32_t itemId, uint32_t count) {
    // 封包格式: Opcode=1183760, Params=itemId, Body=[count]
    std::vector<uint8_t> packet = PacketBuilder()
        .SetOpcode(Opcode::BUY_GOODS_SEND)
        .SetParams(itemId)
        .WriteUInt32(count)
        .Build();
    
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

/**
 * @brief 使用道具（战斗中）
 * @param itemId 道具ID
 * @param position 物品位置索引（如果为0则从映射表查找）
 * @return 发送是否成功
 * @note 封包格式: Opcode=1186050, Params=2, Body=[packcode=1, position, sid=1]
 */
BOOL SendUseItemInBattlePacket(uint32_t itemId, uint32_t position) {
    // 如果未指定位置，从映射表中查找
    if (position == 0) {
        std::lock_guard<std::mutex> packLock(g_packItemsMutex);
        auto it = g_itemPositionMap.find(itemId);
        if (it == g_itemPositionMap.end()) {
            return FALSE;
        }
        position = it->second;
    }
    
    // 封包格式: Opcode=1186050, Params=2, Body=[packcode, position, sid]
    // packcode = 1 (道具背包), sid = 1 (玩家角色)
    std::vector<uint8_t> packet = PacketBuilder()
        .SetOpcode(Opcode::USER_OP_SEND)
        .SetParams(2)
        .WriteUInt32(1)        // packcode = 1
        .WriteUInt32(position) // position
        .WriteUInt32(1)        // sid = 1
        .Build();
    
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// ============================================================================
// 封包回调
// ============================================================================

