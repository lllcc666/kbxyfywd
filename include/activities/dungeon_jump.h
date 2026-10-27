#pragma once

#include <atomic>

#include <windows.h>

#include <vector>

#include "packet_protocol.h"
#include "packet_types.h"

// 这里只保留本模块私有的常量。
// 凡是跨模块通用的 opcode（活动总线、进场景…）一律引用 Opcode 命名空间，
// 不再在本文件里用同一个数字另起一个名字——那是重复定义的温床。
namespace DungeonJump {
    // 这几个 opcode 在全局协议表里已有同名/同义定义，这里保留模块内的叫法，
    // 但值只引用一次，不再各写一遍数字。
    constexpr uint32_t JUMP_LAYER_SEND = Opcode::JUMP_LAYER_SEND;
    constexpr uint32_t JUMP_LAYER_BACK = Opcode::JUMP_LAYER_BACK;
    constexpr uint32_t QUERY_DUNGEON_INFO = Opcode::PLAYER_LIST_SEND;
    constexpr uint32_t QUERY_DUNGEON_BACK = Opcode::PLAYER_LIST_BACK;
    constexpr uint32_t PREPARE_BATTLE = Opcode::NPC_LIST_SEND;
    constexpr uint32_t PREPARE_BATTLE_BACK = Opcode::NPC_LIST_BACK;
    constexpr uint32_t COMPLETE_JUMP = Opcode::NPC_STATUS_SEND;
    constexpr uint32_t COMPLETE_JUMP_BACK = Opcode::NPC_STATUS_BACK;
    constexpr uint32_t PUT_SPIRIT_STORE = 1187333;
    constexpr uint32_t SET_FIRST_SPIRIT = 1187344;

    constexpr uint32_t DUNGEON_ACTIVITY_SEND = Opcode::ACTIVITY_QINGYANG_SEND;
    constexpr uint32_t ACTIVITY_BACK = Opcode::ACTIVITY_QINGYANG_BACK;

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
    std::atomic<bool> monsterDataReceived{false};  ///< 收包线程写、跳层线程等，必须原子
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
BOOL SendSetFirstSpiritPacket(int monsterUniqueId);
BOOL StartOneKeyDungeonJumpPacket(int targetLayer);
void StopDungeonJump();
