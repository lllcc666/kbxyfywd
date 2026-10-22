#pragma once

#include <windows.h>

#include <vector>

#include "packet_types.h"

// 新活动总线（AS3 MsgDoc.as:1580 OP_CLIENT_ACTIVITY_QINGYANG_NEW）。
// send=1185429 / back=1316501，params=活动ID，body="u16 长度 + ASCII 动作名"。
// 回包 body 同样是 u16 长度 + 动作名开头（Act810 RunModel.as:96 body.readUTF()）。
//
// 动作名不是通用枚举，每个活动 SWF 自己定义一套，出处是各活动包里的 Define 类。
// 走这条总线的活动类：Act720/770/774/775/779/780/799/804/806/808/810/827Entrance。

// 发一个「活动ID + 字符串动作名」请求。
BOOL SendActivityAction(int activityId, const char* action);

// 「活动ID + 字符串动作名 + 若干 int32」，对应 AS3 的 sendCmd(["end_game", a, b, ...])。
BOOL SendActivityActionWithInts(int activityId,
                                const char* action,
                                const std::vector<int32_t>& bodyValues);

// 旧活动总线（AS3 MsgDoc.as:1464 OP_CLIENT_ACTIVITY_QINGYANG）。
// send=1184833 / back=1324097，params 是活动子命令码，body 是参数列表。
BOOL SendActivityQingyangPacket(int commandCode, const std::vector<int32_t>& bodyValues);

// 障碍赛跑（Act810）：AS3 里扫荡有前置条件，必须先 start_game 打完一局，
// 否则 sweep_info 返回非 0 并弹「需要成功进行一局游戏才可以扫荡哦！」
// （Act810RunMain model/RunModel.as:144）。所以顺序是
// open_ui → start_game → 等 checkCode → end_game → sweep_info → sweep → reward。
void StartRunCompetitionAsync(int sweepTimes);

// 障碍赛跑回包（1316501 / params=810）：读 protocolID 与 start_game 下发的 checkCode。
void ProcessRunCompetitionResponse(const GamePacket& packet);

// 金平府祈愿（activityId 688，场景 22004）：start → 等待 → finish，循环 roundCount 轮。
void StartJinpingfuWishAsync(int roundCount, int holdMs);

// 场景 1010 / NPC 18004 的活动入口流程（进场景 → 拉列表 → 旧总线登记 → 点 NPC）。
void StartGomokuAsync(int roundCount);
