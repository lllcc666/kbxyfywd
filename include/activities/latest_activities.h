#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>

#include "packet_types.h"

// ============================================================================
// 最新活动（周活动小游戏）——按「本周周活动界面实际存在的按钮」筛出的批次
//
// 这些活动共用协议族 OP_CLIENT_ACTIVITY_QINGYANG_NEW（send 1185429 / back
// 1316501），body 编码为「操作字符串 + int32 参数」；差异只在各活动的
// open_ui 字段偏移、start_game/end_game 参数与 checkCode 变换上。
//
// 每个活动 ID 均来自对应 SWF 内定义的 ACTIVE_ID 常量（已用 FFDec 反编译核对），
// 括号内是 weeklyactivity 清单里的日期码：
//   647 突破迷阵        （Active20140911Maze，26093001）
//   869 摘星之旅        （TouchStarMain，26092401）
//   749 寒冰洞历险记    （Active20140306Explore，26092403）
//   873 填字大作战      （A0926WordGame，26092402）
//   742 守护葫芦藤      （Activity20131226Hulu，26093002）
//   721 采菊东篱下      （Active20140306StarPrincess，26101201）
//   877 寻找七色茱萸    （Active20131010FindCornel，26101202）
//   730 智斗恶灵        （Active20131107FightEvils，26101901）
//   880 夺宝行动        （Active20140619Treasure，26101902）
//
// 按 skill 约定，只接主流程：进入活动查次数 -> 扫荡或走一局 -> 回填次数；
// 不展开兑换、礼包、买宠等入口。
//
// 统一标准（与仓库里全部旧周活动 822/826/827/641/684/631/808 同一条线，
// 逐字对应 ad5143f^:src/hook/activity_minigames.cpp:141 的 ShouldAutoSweep）：
//   ① 进入活动查次数、冷却、历史最好成绩
//   ② 次数用完 -> 停；冷却中 -> 等或停
//   ③ ShouldAutoSweep(最好成绩, 上限最高分) 成立 -> 直接扫荡，一次扫完剩余次数
//   ④ 不成立 -> 完整打完一局（过程包一条不少，打到上限最高分）-> 再扫荡剩余次数
//
// 注意：
//   - 749/730 的"进入活动"操作串是 "game_info"，其余为 "open_ui"。
//   - 721 的 end_game 不带任何参数（body 只有操作串），与其他活动差异最大。
//   - 742 的 end_game 走 OP_CLIENT_LUA_ACTIVITY_V3 且 body 是 JSON；
//     它的分数概念是棋盘格，start_game 回包下发 165 格棋盘，结算时按格统计。
//
// 收录范围只限「本地能完整打完一局的周活动小游戏」：Define 里有
// start_game/end_game/game_event，且 start_game 回包下发 checkCode。
// 挑战类（combat/combat_end、无 checkCode，如 867 月色下的血脉觉醒）、
// 兑换/签到/超进化/场景入口一律不进来——它们没有可验证的对局链路。
// ============================================================================

namespace LatestAct {
constexpr int ACT_MAZE = 647;           ///< 突破迷阵
constexpr int ACT_TOUCH_STAR = 869;     ///< 摘星之旅
constexpr int ACT_EXPLORE = 749;        ///< 寒冰洞历险记
constexpr int ACT_WORD_GAME = 873;      ///< 填字大作战
constexpr int ACT_HULU = 742;           ///< 守护葫芦藤
constexpr int ACT_STAR_PRINCESS = 721;  ///< 采菊东篱下
constexpr int ACT_FIND_CORNEL = 877;    ///< 寻找七色茱萸
constexpr int ACT_FIGHT_EVILS = 730;    ///< 智斗恶灵
constexpr int ACT_TREASURE = 880;       ///< 夺宝行动

}  // namespace LatestAct

/// 该活动是否由本模块负责（供次数查询/UI 通道判断）。
bool IsLatestActivity(uint32_t activityId);

/// 该活动"进入活动"操作串：多数为 "open_ui"，749/730 为 "game_info"。
/// 非本模块活动返回 "open_ui"。
const char* GetLatestActivityOpenUIOperation(uint32_t activityId);

/// 次数探针占用该活动的响应通道；已被一键流程占用时返回 false。
/// 探针与一键流程共用同一响应处理器并互相清除等待标志，必须互斥。
bool BeginLatestActivityProbe(uint32_t activityId);
void EndLatestActivityProbe(uint32_t activityId);

/// 单个活动的剩余次数；< 0 表示未知/未获取。
int GetLatestActivityPlayCount(uint32_t activityId);

/// 批量一键执行：单线程顺序处理，每个活动循环到次数耗尽（或冷却/失败）为止，
/// 每轮结束自动把剩余次数回填前端，无需手动刷新。
BOOL StartOneKeyLatestActivitiesPacket(const std::vector<uint32_t>& activityIds);

/// 停止一键执行：中断批量循环与单活动执行。
void StopLatestActivities();

/// 响应处理器：由 ACTIVITY_QUERY_BACK + activityId 路由进来。
void ProcessLatestActivityResponse(const GamePacket& packet);
