#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <vector>

#include "packet_protocol.h"
#include "packet_types.h"

namespace ShuangTai {
constexpr int COPY_SCENE_ID = 4;
constexpr int MAP_ID = 14000;
constexpr int FIRST_LAYER = 1;
constexpr int MAX_PET_COUNT = 6;
constexpr int DELAY_BETWEEN_PACKETS = 300;
constexpr int DELAY_SWITCH_PET = 1000;
constexpr int DELAY_BATTLE_ROUND = 500;

// 双台谷的战斗收发包全部属于全局战斗协议，这里只保留别名，
// 不再各写一份数字（同一个 1317120 此前在两处各写了一遍）。
constexpr uint32_t INIT_SEND = 1186818;
constexpr uint32_t JUMP_LAYER_SEND = Opcode::JUMP_LAYER_SEND;
constexpr uint32_t ENTER_BATTLE_SEND = 1186184;
constexpr uint32_t BATTLE_OP1_SEND = Opcode::BATTLE_READY;
constexpr uint32_t BATTLE_OP2_SEND = Opcode::BATTLE_PLAY_OVER;
constexpr uint32_t USER_OP_SEND = Opcode::USER_OP_SEND;
constexpr uint32_t BATTLE_END_SEND = Opcode::BATTLE_END_SEND;
constexpr uint32_t BATTLE_START_OP_SEND = 1184769;
constexpr uint32_t BATTLE_END_OP_SEND = 1184886;

constexpr uint32_t GATECRASH_BACK = Opcode::JUMP_LAYER_BACK;
constexpr uint32_t BATTLE_START_BACK = Opcode::BATTLE_START;
constexpr uint32_t BATTLE_ROUND_START_BACK = Opcode::BATTLE_ROUND_START;
constexpr uint32_t BATTLE_ROUND_RESULT_BACK = Opcode::BATTLE_ROUND;
constexpr uint32_t BATTLE_END_BACK = Opcode::BATTLE_END;
}  // namespace ShuangTai

/// 是否正在等妖怪背包回包：双台谷线程写、收包线程读，必须是原子量
extern std::atomic<bool> g_shuangtaiWaitingForMonsterData;

BOOL QueryShuangTaiMonsters();
void UpdateShuangTaiUIFromMonsterData();
BOOL StartOneKeyShuangTaiPacket(bool blockBattle = false);
void StopShuangTai();

void ProcessShuangTaiBattleStartResponse(const GamePacket& packet);
void ProcessShuangTaiBattleRoundStartResponse(const GamePacket& packet);
void ProcessShuangTaiBattleRoundResultResponse(const GamePacket& packet);
void ProcessShuangTaiBattleEndResponse(const GamePacket& packet);

