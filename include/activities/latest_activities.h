#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>

#include "packet_types.h"

// ============================================================================
// 最新活动（周活动小游戏）——按日期码筛选出的未过期小游戏
//
// 这些活动共用协议族 OP_CLIENT_ACTIVITY_QINGYANG_NEW（send 1185429 / back
// 1316501），body 编码为「操作字符串 + int32 参数」，与既有 Act685/Act822 等
// 一致；差异只在各活动的 open_ui 字段偏移、start_game/end_game 参数与
// checkCode 变换上。
//
// 每个活动 ID 均来自对应 SWF 内定义的 ACTIVE_ID 常量（已用 FFDec 反编译核对）：
//   647 突破迷阵        （Active20140911Maze，日期码 26093001）
//   869 摘星之旅        （TouchStarMain，日期码 26092401）
//   749 寒冰洞历险记    （Active20140306Explore，日期码 26092403）
//   870 月饼大胃王      （Activity20140828EatMoonCake，日期码 26092001）
//   704 夺回花束行动    （Active20141218StolenFlower，日期码 26091401）
//   697 美食大作战      （Activity20140605ShareFood，日期码 26091402）
//
// 按 skill 约定，只接主流程：进入活动查次数 -> 扫荡或走一局 -> 回填次数；
// 不展开兑换、礼包、买宠等入口。
//
// 注意：749/704 的"进入活动"操作串是 "game_info"，其余为 "open_ui"。
// ============================================================================

namespace LatestAct {
constexpr int ACT_MAZE = 647;        ///< 突破迷阵
constexpr int ACT_TOUCH_STAR = 869;  ///< 摘星之旅
constexpr int ACT_EXPLORE = 749;     ///< 寒冰洞历险记
constexpr int ACT_MOON_CAKE = 870;   ///< 月饼大胃王
constexpr int ACT_FLOWER = 704;      ///< 夺回花束行动
constexpr int ACT_FOOD = 697;        ///< 美食大作战

constexpr int ACTIVITY_COUNT = 6;
}  // namespace LatestAct

/// 该活动是否由本模块负责（供次数查询/UI 通道判断）。
bool IsLatestActivity(uint32_t activityId);

/// 该活动"进入活动"操作串：多数为 "open_ui"，749/704 为 "game_info"。
/// 非本模块活动返回 "open_ui"。
const char* GetLatestActivityOpenUIOperation(uint32_t activityId);

/// 次数探针占用该活动的响应通道；已被一键流程占用时返回 false。
/// 探针与一键流程共用同一响应处理器并互相清除等待标志，必须互斥。
bool BeginLatestActivityProbe(uint32_t activityId);
void EndLatestActivityProbe(uint32_t activityId);

/// 单个活动的剩余次数；< 0 表示未知/未获取。
int GetLatestActivityPlayCount(uint32_t activityId);

/// 发送 open_ui 查询封包（会等待响应）。
BOOL SendLatestActivityOpenUIPacket(uint32_t activityId);

/// 启动一键执行线程（自动判定扫荡或走一局，只做主流程）。
/// 单活动入口：等价于只传一个 ID 的批量版本。
BOOL StartOneKeyLatestActivityPacket(uint32_t activityId);

/// 批量一键执行：单线程顺序处理，每个活动循环到次数耗尽（或冷却/失败）为止，
/// 每轮结束自动把剩余次数回填前端，无需手动刷新。
BOOL StartOneKeyLatestActivitiesPacket(const std::vector<uint32_t>& activityIds);

/// 响应处理器：由 ACTIVITY_QUERY_BACK + activityId 路由进来。
void ProcessLatestActivityResponse(const GamePacket& packet);
