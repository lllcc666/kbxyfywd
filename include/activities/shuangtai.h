#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <vector>

#include "packet_protocol.h"
#include "packet_types.h"

namespace ShuangTai {
// 双台谷的战斗收发包全部属于全局战斗协议，这里只保留别名，
// 不再各写一份数字（同一个 1317120 此前在两处各写了一遍）。
constexpr uint32_t INIT_SEND = 1186818;
constexpr uint32_t JUMP_LAYER_SEND = Opcode::JUMP_LAYER_SEND;
constexpr uint32_t ENTER_BATTLE_SEND = 1186184;
constexpr uint32_t BATTLE_OP1_SEND = Opcode::BATTLE_READY;
constexpr uint32_t BATTLE_OP2_SEND = Opcode::BATTLE_PLAY_OVER;
constexpr uint32_t USER_OP_SEND = Opcode::USER_OP_SEND;
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

