#include <windows.h>

#include <string>
#include <vector>

#include "dungeon_jump.h"
#include "hook_shared.h"
#include "managed_thread.h"
#include "packet_builder.h"
#include "packet_parser.h"
#include "packet_protocol.h"
#include "ui_bridge.h"
#include "wpe_hook.h"
#include "wpe_hook_internal.h"

extern bool PostScriptToUI(const std::wstring& jsCode);

// ============================================================================
// 副本跳层功能实现 (DungeonJump)
// ============================================================================

// 副本跳层全局状态
DungeonJumpState g_dungeonJumpState;

// 外部变量声明（来自packet_parser.cpp）

BOOL SendDungeonJumpLayerPacket(int layer) {
    if (layer < 1 || layer > 9999) {
        return FALSE;
    }
    
    // 构造跳层封包: Opcode=1186180, Params=0, Body=[layer(4B小端序)]
    auto packet = PacketBuilder()
        .SetOpcode(DungeonJump::JUMP_LAYER_SEND)
        .SetParams(0)
        .WriteInt32(layer)
        .Build();
    
    BOOL result = SendPacket(g_LastGameSocket, packet.data(), static_cast<DWORD>(packet.size()),
                              DungeonJump::JUMP_LAYER_BACK, 3000);
    
    if (result) {
        wchar_t msg[256];
        swprintf_s(msg, L"副本跳层：发送跳层请求到第%d层", layer);
        UIBridge::Instance().UpdateHelperText(msg);
    }
    
    return result;
}

BOOL SendQueryDungeonInfoPacket() {
    // 构造查询副本信息封包: Opcode=1184317, Params=0, Body=[]
    auto packet = PacketBuilder()
        .SetOpcode(DungeonJump::QUERY_DUNGEON_INFO)
        .SetParams(0)
        .Build();
    
    return SendPacket(g_LastGameSocket, packet.data(), static_cast<DWORD>(packet.size()),
                      DungeonJump::QUERY_DUNGEON_BACK, 3000);
}

BOOL SendPrepareBattlePacket() {
    // 构造准备战斗封包: Opcode=1184782, Params=0, Body=[]
    auto packet = PacketBuilder()
        .SetOpcode(DungeonJump::PREPARE_BATTLE)
        .SetParams(0)
        .Build();
    
    return SendPacket(g_LastGameSocket, packet.data(), static_cast<DWORD>(packet.size()),
                      DungeonJump::PREPARE_BATTLE_BACK, 3000);
}

BOOL SendDungeonActivityPacket(int activityId, int opType, int param) {
    // 构造副本活动操作封包: Opcode=1184833, Params=activityId, Body=[opType, param]
    auto packet = PacketBuilder()
        .SetOpcode(DungeonJump::DUNGEON_ACTIVITY_SEND)
        .SetParams(activityId)
        .WriteInt32(opType)
        .WriteInt32(param)
        .Build();
    
    return SendPacket(g_LastGameSocket, packet.data(), static_cast<DWORD>(packet.size()),
                      DungeonJump::ACTIVITY_BACK, 3000, static_cast<uint32_t>(activityId), true);
}

BOOL SendCompleteJumpPacket() {
    // 构造完成跳层封包: Opcode=1184771, Params=19012, Body=[]
    auto packet = PacketBuilder()
        .SetOpcode(DungeonJump::COMPLETE_JUMP)
        .SetParams(19012)
        .Build();
    
    BOOL result = SendPacket(g_LastGameSocket, packet.data(), static_cast<DWORD>(packet.size()),
                             DungeonJump::COMPLETE_JUMP_BACK, 3000);
    
    if (result) {
        UIBridge::Instance().UpdateHelperText(L"副本跳层：完成跳层操作");
    }
    
    return result;
}

BOOL SendPutSpiritToStorePacket(int spiritId) {
    // 构造存入妖怪仓库封包: Opcode=1187333, Params=spiritId, Body=[4, 0]
    auto packet = PacketBuilder()
        .SetOpcode(DungeonJump::PUT_SPIRIT_STORE)
        .SetParams(spiritId)
        .WriteInt32(4)
        .WriteInt32(0)
        .Build();
    
    return SendPacket(g_LastGameSocket, packet.data(), static_cast<DWORD>(packet.size()));
}

BOOL SendGetSpiritFromStorePacket(int spiritId) {
    // 构造取出妖怪仓库封包: Opcode=1187333, Params=spiritId, Body=[0, 4]
    auto packet = PacketBuilder()
        .SetOpcode(DungeonJump::PUT_SPIRIT_STORE)
        .SetParams(spiritId)
        .WriteInt32(0)
        .WriteInt32(4)
        .Build();
    
    return SendPacket(g_LastGameSocket, packet.data(), static_cast<DWORD>(packet.size()));
}

BOOL SendSetFirstSpiritPacket(int spiritId) {
    // 构造设置首发妖怪封包: Opcode=1187344, Params=0, Body=[spiritId]
    auto packet = PacketBuilder()
        .SetOpcode(DungeonJump::SET_FIRST_SPIRIT)
        .SetParams(0)
        .WriteInt32(spiritId)
        .Build();
    
    return SendPacket(g_LastGameSocket, packet.data(), static_cast<DWORD>(packet.size()));
}

std::vector<MonsterItem> FilterHighLevelMonsters(const std::vector<MonsterItem>& monsters) {
    std::vector<MonsterItem> result;
    for (const auto& monster : monsters) {
        if (monster.level > 50) {
            result.push_back(monster);
        }
    }
    return result;
}

int FindFirstSpiritId(const std::vector<MonsterItem>& monsters) {
    for (const auto& monster : monsters) {
        if (monster.isfirst == 1) {
            return monster.id;
        }
    }
    return 0;
}

// 副本跳层异步线程
static DWORD WINAPI DungeonJumpThreadProc(LPVOID lpParam) {
    (void)lpParam;
    int targetLayer = g_dungeonJumpState.targetLayer;
    
    // 跳层开始
    UIBridge::Instance().UpdateHelperText(L"跳层开始");
    
    // 查询妖怪背包
    g_dungeonJumpState.monsterDataReceived = false;
    SendQueryMonsterPacket();
    
    // 等待妖怪背包数据响应（最多等待3秒）
    int waitCount = 0;
    const int maxWaitCount = 30;
    while (!g_dungeonJumpState.monsterDataReceived && waitCount < maxWaitCount) {
        Sleep(100);
        waitCount++;
    }
    
    if (!g_dungeonJumpState.monsterDataReceived) {
        g_dungeonJumpState.Reset();
        return 1;
    }
    
    // 获取妖怪数据（快照，避免与收包线程重建同时读写）
    const MonsterData monsterData = SnapshotMonsterData();
    if (monsterData.monsters.empty()) {
        g_dungeonJumpState.Reset();
        return 1;
    }
    
    // 筛选高等级妖怪并记录首发
    g_dungeonJumpState.highLevelMonsters = FilterHighLevelMonsters(monsterData.monsters);
    g_dungeonJumpState.originalFirstSpiritId = FindFirstSpiritId(monsterData.monsters);
    
    // 存入高等级妖怪（无提示）
    if (!g_dungeonJumpState.highLevelMonsters.empty()) {
        for (const auto& monster : g_dungeonJumpState.highLevelMonsters) {
            if (!g_dungeonJumpState.isRunning) {
                return 1;
            }
            
            if (SendPutSpiritToStorePacket(monster.id)) {
                g_dungeonJumpState.storedMonsterCount++;
                Sleep(200);
            }
        }
    }
    
    // ========== 执行完整跳层流程（使用ResponseWaiter等待响应） ==========
    
    // 1. 发送跳层封包，等待返回 1317252
    if (!SendDungeonJumpLayerPacket(targetLayer)) {
        goto CLEANUP;
    }
    
    if (!g_dungeonJumpState.isRunning) goto CLEANUP;
    
    // 2. 进入地图，等待返回 1315395 (使用项目中已有的 Opcode::ENTER_SCENE_BACK)
    if (!SendEnterScenePacket(DungeonJump::DUNGEON_MAP_ID, Opcode::ENTER_SCENE_BACK, 5000)) {
        goto CLEANUP;
    }
    
    if (!g_dungeonJumpState.isRunning) goto CLEANUP;
    
    // 3. 查询副本信息，等待返回 1315086
    if (!SendQueryDungeonInfoPacket()) {
        goto CLEANUP;
    }
    
    if (!g_dungeonJumpState.isRunning) goto CLEANUP;
    
    // 4. 准备战斗，等待返回 1315854
    if (!SendPrepareBattlePacket()) {
        goto CLEANUP;
    }
    
    if (!g_dungeonJumpState.isRunning) goto CLEANUP;
    
    // 5. 活动操作900，等待返回 1324097
    if (!SendDungeonActivityPacket(DungeonJump::ACTIVITY_ID_900, 1, DungeonJump::DUNGEON_MAP_ID)) {
        goto CLEANUP;
    }
    
    if (!g_dungeonJumpState.isRunning) goto CLEANUP;
    
    // 6. 活动操作902，等待返回 1324097
    if (!SendDungeonActivityPacket(DungeonJump::ACTIVITY_ID_902, 1, DungeonJump::DUNGEON_MAP_ID)) {
        goto CLEANUP;
    }
    
    if (!g_dungeonJumpState.isRunning) goto CLEANUP;
    
    // 7. 活动操作325，等待返回 1324097
    if (!SendDungeonActivityPacket(DungeonJump::ACTIVITY_ID_325, 4, DungeonJump::DUNGEON_MAP_ID)) {
        goto CLEANUP;
    }
    
    if (!g_dungeonJumpState.isRunning) goto CLEANUP;
    
    // 8. 完成跳层，等待返回 1315843
    if (!SendCompleteJumpPacket()) {
        goto CLEANUP;
    }
    
    // 跳层完成
    UIBridge::Instance().UpdateHelperText(L"跳层完成");
    
    // 清理和恢复流程（无提示）
CLEANUP:
    // 取回妖怪（无提示）
    if (g_dungeonJumpState.storedMonsterCount > 0) {
        for (const auto& monster : g_dungeonJumpState.highLevelMonsters) {
            if (!g_dungeonJumpState.isRunning) break;
            
            if (SendGetSpiritFromStorePacket(monster.id)) {
                g_dungeonJumpState.retrievedMonsterCount++;
                Sleep(200);
            }
        }
    }
    
    // 设置首发（无提示）
    if (g_dungeonJumpState.originalFirstSpiritId > 0 && g_dungeonJumpState.isRunning) {
        SendSetFirstSpiritPacket(g_dungeonJumpState.originalFirstSpiritId);
        Sleep(200);
    }
    
    g_dungeonJumpState.Reset();
    return 0;
}

BOOL StartOneKeyDungeonJumpPacket(int targetLayer) {
    if (g_dungeonJumpState.isRunning) {
        return FALSE;
    }
    
    // 设置状态
    g_dungeonJumpState.Reset();
    g_dungeonJumpState.isRunning = true;
    g_dungeonJumpState.targetLayer = targetLayer;
    
    // 创建异步线程执行跳层流程
    HANDLE hThread = CreateThread(nullptr, 0, DungeonJumpThreadProc, nullptr, 0, nullptr);
    if (!hThread) {
        g_dungeonJumpState.Reset();
        return FALSE;
    }
    
    CloseHandle(hThread);
    return TRUE;
}

void StopDungeonJump() {
    g_dungeonJumpState.Reset();
}
