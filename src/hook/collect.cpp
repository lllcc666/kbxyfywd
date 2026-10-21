#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include <random>
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

// ============================================================================
// 一键采集实现
// ============================================================================

/** 采集物品数据结构 */

struct CollectItemData {

    const wchar_t* name;   ///< 物品名称

    uint32_t mapId;        ///< 地图ID

    uint32_t stuffId;      ///< 物品ID

    int maxCount;          ///< 最大采集次数

};



/** 采集物品列表（按易语言代码顺序）



 * 



 * 封包编码规则（重要！）：



 * - 封包中所有数值字段都是小端序存储



 * - 例如：封包 A10F0000 小端序解读 = 0x00000FA1 = 4001



 * - 例如：封包 F6930400 小端序解读 = 0x000493F6 = 299254



 * - 构造封包时，数值需要以小端序写入



 */







static const CollectItemData COLLECT_ITEMS[] = {







    // 名称        mapId    stuffId   次数







    // stuffId 从易语言封包解析：小端序读取







    // 例如：FC930400 → 0x000493FC = 300028















    {L"上清宝玉", 4001,  300022,  3},   // F6930400 → 0x000493F6 (易语言代码改为3次)







    {L"天地灵气", 6002,  300028,  2},   // FC930400 → 0x000493FC (易语言代码改为2次)
    {L"火云岩",   8001,  300019,  3},   // F3930400 → 0x000493F3
    {L"千年红木", 2004,  300016,  3},   // F0930400 → 0x000493F0
    {L"远古青松", 4002,  300017,  3},   // F1930400 → 0x000493F1
    {L"精蓝石",   9002,  300021,  5},   // F5930400 → 0x000493F5, mapId 0x232A=9002 (小端序: 2A23)
    {L"松绿石",   5001,  300025,  5},   // F9930400 → 0x000493F9
    {L"冰霜岩",   3004,  300018,  4},   // F2930400 → 0x000493F2, mapId 0x0BBC=3004 (小端序: BC0B)
    {L"幻影石",   2101,  300015,  4},   // EF930400 → 0x000493EF (结束用FF93)
    {L"冰晶砂",   3004,  300023,  5},   // F7930400 → 0x000493F7, mapId 0x0BBC=3004 (小端序: BC0B)
    {L"天罡石",   6002,  300020,  4},   // F4930400 → 0x000493F4
    {L"茅山玉",   7001,  300024,  5},   // F8930400 → 0x000493F8, mapId 0x1B59=7001 (小端序: 591B)
    {L"神明果",   6003,  300030,  5},   // FE930400 → 0x000493FE (改为5次)
    {L"朱砂石",   9002,  300026,  5},   // FA930400 → 0x000493FA, mapId 0x232A=9002
    {L"青金石",   9002,  300027,  5},   // FB930400 → 0x000493FB, mapId 0x232A=9002
    {L"坐骑蛋",   2003,  0,      0},    // 特殊处理

};

static const int COLLECT_ITEM_COUNT = sizeof(COLLECT_ITEMS) / sizeof(COLLECT_ITEMS[0]);

/** 采集单个物品 */
static BOOL CollectSingleItem(const CollectItemData& item) {
    if (item.maxCount == 0) {
        // 坐骑蛋特殊处理
        return TRUE;
    }

    // 1. 跳转地图封包: Opcode 1184313, Params=mapId
    auto mapPacket = PacketBuilder()
        .SetOpcode(Opcode::ENTER_SCENE_SEND)
        .SetParams(static_cast<uint32_t>(item.mapId))
        .Build();

    if (!SendPacket(g_LastGameSocket, mapPacket.data(), static_cast<DWORD>(mapPacket.size()))) {
        return FALSE;
    }
    Sleep(400);

    // 2. 第二个封包: Opcode 1184319
    auto packet2 = PacketBuilder()
        .SetOpcode(1184319)
        .SetParams(0)
        .Build();

    if (!SendPacket(g_LastGameSocket, packet2.data(), static_cast<DWORD>(packet2.size()))) {
        return FALSE;
    }
    Sleep(265);

    // 3. 第三个封包: Opcode 1184798
    auto packet3 = PacketBuilder()
        .SetOpcode(1184798)
        .SetParams(0)
        .Build();

    if (!SendPacket(g_LastGameSocket, packet3.data(), static_cast<DWORD>(packet3.size()))) {
        return FALSE;
    }
    Sleep(265);

    // 循环采集（基于响应包判断完成）
    for (int i = 0; i < item.maxCount; i++) {
        // 重置采集状态
        g_collectStatus = 0;
        g_collectFinished = false;
        
        // 4. 开始采集封包: Opcode 1187106, Params=1, Body=stuffId
        auto startPacket = PacketBuilder()
            .SetOpcode(1187106)
            .SetParams(1)
            .WriteInt32(item.stuffId)
            .Build();

        if (!SendPacket(g_LastGameSocket, startPacket.data(), static_cast<DWORD>(startPacket.size()))) {
            return FALSE;
        }

        // 更新提示（使用宽字符串，显示地图名称）
        std::wstring mapName = GetMapName(item.mapId);
        std::wstring msg = L"正在采集: ";
        msg.append(item.name);
        msg.append(L" (");
        msg.append(mapName.empty() ? L"未知地图" : mapName);
        msg.append(L", ");
        msg.append(std::to_wstring(i + 1));
        msg.append(L"/");
        msg.append(std::to_wstring(item.maxCount));
        msg.append(L")");
        UIBridge::Instance().UpdateHelperText(msg);

        // 等待服务端返回 statusid=1（开始采集动画）
        // 超时设置：最多等待 3 秒
        int waitCount = 0;
        while (g_collectStatus != 1 && waitCount < 30) {
            Sleep(100);
            waitCount++;
            // 检查错误状态
            if (g_collectStatus == -1) {
                // 每日采集次数用完
                UIBridge::Instance().UpdateHelperText(std::wstring(item.name) + L": 每日采集次数已用完");
                return FALSE;
            }
            if (g_collectStatus == -2) {
                // 未装备采集工具
                UIBridge::Instance().UpdateHelperText(std::wstring(item.name) + L": 未装备采集工具");
                return FALSE;
            }
        }

        // 等待采集完成响应（statusid=2 或 statusid=3）
        // 超时设置：最多等待 15 秒（采集动画约 10-13 秒）
        g_collectFinished = false;
        waitCount = 0;
        while (!g_collectFinished && waitCount < 150) {
            Sleep(100);
            waitCount++;
        }

        // 5. 结束采集封包: Opcode 1187106 (小端序: 221D1200)
        BYTE endPacket[16];
        memset(endPacket, 0, sizeof(endPacket));
        endPacket[0] = 0x44;  // Magic: "SD"
        endPacket[1] = 0x53;
        endPacket[2] = 0x04;  // Length: 4 (小端序)
        endPacket[3] = 0x00;
        endPacket[4] = 0x22;  // Opcode: 1187106 小端序 (0x00121D22)
        endPacket[5] = 0x1D;
        endPacket[6] = 0x12;
        endPacket[7] = 0x00;
        endPacket[8] = 0x02;  // Params: 2 (结束采集)
        endPacket[9] = 0x00;
        endPacket[10] = 0x00;
        endPacket[11] = 0x00;
        // Body: stuffId 小端序写入（32位）
        endPacket[12] = (item.stuffId) & 0xFF;
        endPacket[13] = (item.stuffId >> 8) & 0xFF;
        endPacket[14] = (item.stuffId >> 16) & 0xFF;
        endPacket[15] = (item.stuffId >> 24) & 0xFF;

        if (!SendPacket(g_LastGameSocket, endPacket, 16)) {
            return FALSE;
        }

        // 如果收到 statusid=3，表示采集全部结束，跳出循环
        if (g_collectStatus == 3) {
            break;
        }
        
        Sleep(300);
    }

    return TRUE;
}

/** 处理坐骑蛋采集 */
static BOOL CollectMountEgg() {
    // 坐骑蛋需要特殊处理：拾取场景中的坐骑蛋
    // 封包格式：所有数值都是小端序
    
    // 1. 跳转到地图 2003: 封包 4453000039121200D3070000
    // mapId 2003 = 0x000007D3，小端序写入: D3 07 00 00
    BYTE mapPacket[12];
    memset(mapPacket, 0, sizeof(mapPacket));
    mapPacket[0] = 0x44;  // Magic: "SD"
    mapPacket[1] = 0x53;
    mapPacket[2] = 0x00;  // Length: 0 (小端序)
    mapPacket[3] = 0x00;
    mapPacket[4] = 0x39;  // Opcode: 1184313 小端序 (0x00121239)
    mapPacket[5] = 0x12;
    mapPacket[6] = 0x12;
    mapPacket[7] = 0x00;
    mapPacket[8] = 0xD3;  // mapId: 2003 小端序
    mapPacket[9] = 0x07;
    mapPacket[10] = 0x00;
    mapPacket[11] = 0x00;
    SendPacket(g_LastGameSocket, mapPacket, 12);
    Sleep(300);

    // 2. 发送拾取封包: 44530800001912007952000000000000
    // Opcode 1185792 = 0x00121900，小端序: 00 19 12 00
    // Params 21089 = 0x00005279，小端序: 79 52 00 00
    BYTE packet[20];
    memset(packet, 0, sizeof(packet));
    packet[0] = 0x44;  // Magic: "SD"
    packet[1] = 0x53;
    packet[2] = 0x08;  // Length: 8 (小端序)
    packet[3] = 0x00;
    packet[4] = 0x00;  // Opcode: 1185792 小端序 (0x00121900)
    packet[5] = 0x19;
    packet[6] = 0x12;
    packet[7] = 0x00;
    packet[8] = 0x79;  // Params: 21089 小端序
    packet[9] = 0x52;
    packet[10] = 0x00;
    packet[11] = 0x00;
    packet[12] = 0x00;  // Body: 8字节填充
    packet[13] = 0x00;
    packet[14] = 0x00;
    packet[15] = 0x00;
    packet[16] = 0x00;
    packet[17] = 0x00;
    packet[18] = 0x00;
    packet[19] = 0x00;
    SendPacket(g_LastGameSocket, packet, 20);
    Sleep(400);

    // 3. 跳转到地图 10969: 封包 4453000039121200F92A0000
    // mapId 10969 = 0x00002AF9，小端序: F9 2A 00 00
    mapPacket[8] = 0xF9;
    mapPacket[9] = 0x2A;
    mapPacket[10] = 0x00;
    mapPacket[11] = 0x00;
    SendPacket(g_LastGameSocket, mapPacket, 12);
    Sleep(300);

    // 4. 发送拾取封包: Params 41220 = 0x0000A104
    packet[8] = 0x04;  // Params: 41220 小端序
    packet[9] = 0xA1;
    packet[10] = 0x00;
    packet[11] = 0x00;
    SendPacket(g_LastGameSocket, packet, 20);
    Sleep(400);

    // 5. 跳转到地图 4103: 封包 4453000039121200A20F0000
    // mapId 4103 = 0x00001007，小端序: 07 10 00 00
    // 但易语言是 A20F，即 mapId 0x00000FA2 = 4002？让我重新分析
    // A2 0F 小端序 = 0x0FA2 = 4002
    mapPacket[8] = 0xA2;
    mapPacket[9] = 0x0F;
    mapPacket[10] = 0x00;
    mapPacket[11] = 0x00;
    SendPacket(g_LastGameSocket, mapPacket, 12);
    Sleep(300);

    // 6. 发送拾取封包
    SendPacket(g_LastGameSocket, packet, 20);
    Sleep(400);

    // 7. 跳转到地图 10004: 封包 445300003912120014270000
    // mapId 10004 = 0x00002714，小端序: 14 27 00 00
    mapPacket[8] = 0x14;
    mapPacket[9] = 0x27;
    mapPacket[10] = 0x00;
    mapPacket[11] = 0x00;
    SendPacket(g_LastGameSocket, mapPacket, 12);
    Sleep(300);

    // 8. 发送拾取封包
    SendPacket(g_LastGameSocket, packet, 20);
    Sleep(400);

    return TRUE;
}

/** 采集线程函数 */
static DWORD WINAPI CollectThreadProc(LPVOID lpParam) {
    DWORD flags = *(DWORD*)lpParam;
    delete (DWORD*)lpParam;
    
    g_collectAutoMode = true;
    UIBridge::Instance().UpdateHelperText(L"开始一键采集...");
    
    // 按照标志位采集
    for (int i = 0; i < COLLECT_ITEM_COUNT; i++) {
        if (!(flags & (1 << i))) {
            continue;  // 未选中，跳过
        }
        
        const CollectItemData& item = COLLECT_ITEMS[i];
        
        if (item.maxCount == 0) {
            // 坐骑蛋
            CollectMountEgg();
            UIBridge::Instance().UpdateHelperText(L"拾取坐骑蛋完成");
        } else {
            // 普通采集
            if (CollectSingleItem(item)) {
                UIBridge::Instance().UpdateHelperText(std::wstring(item.name) + L"采集完成");
            } else {
                UIBridge::Instance().UpdateHelperText(std::wstring(item.name) + L"采集失败");
            }
        }
        
        Sleep(500);
    }
    
    // 采集完成，返回地图
    // 易语言封包: 445300003912120086030000
    // 地图ID: 86 03 00 00 → 小端序 0x00000386 = 902
    SendEnterScenePacket(902);
    Sleep(500);
    
    UIBridge::Instance().UpdateHelperText(L"一键采集完成！");
    g_collectAutoMode = false;
    
    return 0;
}

/** 一键采集所有选中的材料 */
BOOL StartOneKeyCollectPacket(DWORD flags) {
    if (flags == 0) {
        UIBridge::Instance().UpdateHelperText(L"请先选择要采集的材料");
        return FALSE;
    }
    
    if (g_userId == 0) {
        UIBridge::Instance().UpdateHelperText(L"请先进入游戏");
        return FALSE;
    }
    
    DWORD* pFlags = new DWORD(flags);
    HANDLE hThread = CreateThread(NULL, 0, CollectThreadProc, pFlags, 0, NULL);
    if (hThread) {
        CloseHandle(hThread);
        return TRUE;
    } else {
        delete pFlags;
        UIBridge::Instance().UpdateHelperText(L"启动采集线程失败");
        return FALSE;
    }
}

/** 处理采集响应（在HookedRecv中调用）
 * 
 * 根据AS3代码 StoneControl.as 和 MainModel.as：
 * - statusid 在封包的 Params 字段（mParams），不是 body
 * - statusid = 1: 开始采集（播放采集动画）
 * - statusid = 2: 采集完成，获得物品（body: stuffid, stuffcount, userid）
 * - statusid = 3: 采集全部结束（body: userid）
 * - statusid = 4: 额外获得物品（body: stuffid, stuffcount）
 * - statusid = -1: 每日采集次数用完
 * - statusid = -2: 未装备采集工具
 */
void ProcessCollectResponse(const GamePacket& packet) {
    if (packet.opcode != Opcode::COLLECT_STATUS_BACK) {
        return;
    }
    
        // statusid 在封包的 Params 字段（AS3: event.msg.mParams）
    
        int32_t statusid = static_cast<int32_t>(packet.params);
    
        
    
        g_collectStatus = statusid;
    
        
    
        // 根据AS3代码逻辑：
    
        // statusid = 2 表示单次采集完成，获得物品
    
        // statusid = 3 表示采集全部结束
    
        if (statusid == 2 || statusid == 3) {
    
            g_collectFinished = true;
    
        }
    
        // statusid = 1: 开始采集动画，继续等待
    
        // statusid = -1: 每日采集次数用完
    
        // statusid = -2: 未装备采集工具
    
    }
    
    
    
    // ============ BOSS专区功能实现 ============
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    /** 发送BOSS战斗封包
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
     * 
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
     * 封包格式（根据易语言代码）：
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
     * - Magic: 21316 (0x5344)
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
     * - Length: 4 (Body长度)
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
     * - Opcode: 1186048 (OP_CLIENT_CLICK_NPC)
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
     * - Params: spiritId (BOSS ID，小端序)
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
     * - Body: 00 00 00 00 (最后4字节替换为0)
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
     * 
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
     * @param spiritId BOSS的妖怪ID（大于10000）
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
     * @return 发送是否成功
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
     */
    
    
    
