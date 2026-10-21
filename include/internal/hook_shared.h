#pragma once

#include <windows.h>

#include <string>
#include <utility>
#include <vector>

#include "packet_types.h"
#include "wpe_hook.h"

// ============================================================================
// 活动模块共享原语
// ============================================================================
//
// wpe_hook.cpp 拆分后，各活动模块（battle_six / dungeon_jump / shuangtai /
// spirit_collect / story_task / activity_minigames）需要的底层设施都从这里取。
// 这里只放"跨模块复用、且没有业务语义"的东西：
//   * 回包字符串读取（uint16 长度前缀）
//   * 活动协议发包 + 每日任务上报
//   * 活动模块专用的 UI 提示
//   * 奖励表构造
// 业务状态一律留在各自模块内，不出现在这个头里。

// uint16 长度前缀字符串读取；越界返回 false，不推进 offset。
bool ReadPacketString(const BYTE* body, size_t bodySize, size_t& offset, std::string& out);

// 活动通用发包：opcode=ACTIVITY_QINGYANG_NEW_SEND，body 由 activityId + operation +
// bodyValues 组成。activityId 同时作为 params 参与回包匹配。
BOOL SendActivityPacket(uint32_t activityId, const std::string& operation,
                        const std::vector<int32_t>& bodyValues = {});

// 活动结算后上报每日任务（opcode 1184812，与 TASK_DAILY_SEND 不是同一个协议）。
BOOL SendActivityDailyTaskReport();

// 精魄系统的三条 UI 提示（window.handleSpiritCollectData）。
void NotifySpiritAlert(const std::wstring& message);
void NotifySpiritSuccess(const std::wstring& message);
void NotifySpiritConfirm(const std::wstring& playerName);

// 奖励表构造：medal(0) / exp(202) / coin(201) + 其它道具。
void BuildAct757AwardList(int medal, int exp, int coin,
                          const std::vector<std::pair<int, int>>& rewards,
                          std::vector<std::pair<int, int>>& awardList);
void BuildAct822AwardList(int medal, int exp, int coin,
                          const std::vector<std::pair<int, int>>& rewards,
                          std::vector<std::pair<int, int>>& awardList);
int FindAwardAmount(const std::vector<std::pair<int, int>>& awardList, int itemId);

// hook 侧共享全局：最近的游戏套接字（SendPacket 传 0 时用它兜底）。
// 定义在 wpe_hook.cpp，拆分后的模块只读不写。
extern std::atomic<SOCKET> g_LastGameSocket;
