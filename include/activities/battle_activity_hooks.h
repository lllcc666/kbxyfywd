/**
 * @file battle_activity_hooks.h
 * @brief 协议层与活动层之间的战斗事件窄接口。
 *
 * packet_parser 只依赖本头文件，不再 include battle_six / dungeon_jump /
 * shuangtai 等具体活动头。活动模块在实现文件里订阅这些事件。
 *
 * 这是从“协议解析器直接操作活动全局变量”退到“协议层发事件、活动层收事件”
 * 的第一步；后续若活动数量继续增长，可把函数指针换成
 * std::function 注册表，接口不变。
 */
#pragma once

#include <cstdint>

#include "packet_types.h"

namespace BattleActivity {

/// 战斗快照变化（initialize=true 表示新一场开始）。
void SyncAutoBattle(const BattleData& battle, bool initialize);
/// 回合开始（BATTLE_ROUND_START / BATTLE_CHANGE_SPIRIT_ROUND）。
void NotifyRoundStart();
/// 结算标记（BATTLE_NEWEXP 的 win flags）。
void MarkSettlement(uint32_t winFlags);
/// 妖怪背包数据已更新（副本跳层 / 双台谷等待方）。
void NotifyMonsterDataReceived();

}  // namespace BattleActivity
