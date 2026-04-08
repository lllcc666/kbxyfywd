#pragma once

#include <windows.h>

#include <vector>

#include "packet_types.h"

namespace DungeonJump {
    constexpr uint32_t JUMP_LAYER_SEND = 1186180;
    constexpr uint32_t QUERY_DUNGEON_INFO = 1184317;
    constexpr uint32_t PREPARE_BATTLE = 1184782;
    constexpr uint32_t DUNGEON_ACTIVITY_SEND = 1184833;
    constexpr uint32_t COMPLETE_JUMP = 1184771;
    constexpr uint32_t PUT_SPIRIT_STORE = 1187333;
    constexpr uint32_t SET_FIRST_SPIRIT = 1187344;

    constexpr uint32_t JUMP_LAYER_BACK = 1317252;
    constexpr uint32_t QUERY_DUNGEON_BACK = 1315086;
    constexpr uint32_t PREPARE_BATTLE_BACK = 1315854;
    constexpr uint32_t ACTIVITY_BACK = 1324097;
    constexpr uint32_t COMPLETE_JUMP_BACK = 1315843;

    constexpr int DUNGEON_MAP_ID = 2000;
    constexpr int ACTIVITY_ID_900 = 900;
    constexpr int ACTIVITY_ID_902 = 902;
    constexpr int ACTIVITY_ID_325 = 325;
}

struct DungeonJumpState {
    bool isRunning = false;
    int targetLayer = 1;
    std::vector<MonsterItem> highLevelMonsters;
    int originalFirstSpiritId = 0;
    bool monsterDataReceived = false;
    int storedMonsterCount = 0;
    int retrievedMonsterCount = 0;

    void Reset() {
        isRunning = false;
        targetLayer = 1;
        highLevelMonsters.clear();
        originalFirstSpiritId = 0;
        monsterDataReceived = false;
        storedMonsterCount = 0;
        retrievedMonsterCount = 0;
    }
};

// 地宫跳层状态唯一 owner。
extern DungeonJumpState g_dungeonJumpState;

BOOL SendDungeonJumpLayerPacket(int layer);
BOOL SendQueryDungeonInfoPacket();
BOOL SendPrepareBattlePacket();
BOOL SendDungeonActivityPacket(int activityId);
BOOL SendCompleteJumpPacket();
BOOL SendPutSpiritStorePacket(int monsterUniqueId);
BOOL SendSetFirstSpiritPacket(int monsterUniqueId);
BOOL StartOneKeyDungeonJumpPacket(int targetLayer);
void StopDungeonJump();
