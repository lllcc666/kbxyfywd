#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <vector>

#include "packet_types.h"

namespace ShuangTai {
constexpr int COPY_SCENE_ID = 4;
constexpr int MAP_ID = 14000;
constexpr int FIRST_LAYER = 1;
constexpr int MAX_PET_COUNT = 6;
constexpr int DELAY_BETWEEN_PACKETS = 300;
constexpr int DELAY_SWITCH_PET = 1000;
constexpr int DELAY_BATTLE_ROUND = 500;

constexpr uint32_t INIT_SEND = 1186818;
constexpr uint32_t JUMP_LAYER_SEND = 1186180;
constexpr uint32_t ENTER_BATTLE_SEND = 1186184;
constexpr uint32_t BATTLE_OP1_SEND = 1186049;
constexpr uint32_t BATTLE_OP2_SEND = 1186056;
constexpr uint32_t USER_OP_SEND = 1186050;
constexpr uint32_t BATTLE_END_SEND = 1186182;
constexpr uint32_t BATTLE_START_OP_SEND = 1184769;
constexpr uint32_t BATTLE_END_OP_SEND = 1184886;

constexpr uint32_t GATECRASH_BACK = 1317252;
constexpr uint32_t BATTLE_START_BACK = 1317120;
constexpr uint32_t BATTLE_ROUND_START_BACK = 1317121;
constexpr uint32_t BATTLE_ROUND_RESULT_BACK = 1317122;
constexpr uint32_t BATTLE_END_BACK = 1317125;
}  // namespace ShuangTai

BOOL QueryShuangTaiMonsters();
void UpdateShuangTaiUIFromMonsterData();
BOOL StartOneKeyShuangTaiPacket(bool blockBattle = false);
void StopShuangTai();

void ProcessShuangTaiBattleStartResponse(const GamePacket& packet);
void ProcessShuangTaiBattleRoundStartResponse(const GamePacket& packet);
void ProcessShuangTaiBattleRoundResultResponse(const GamePacket& packet);
void ProcessShuangTaiBattleEndResponse(const GamePacket& packet);

