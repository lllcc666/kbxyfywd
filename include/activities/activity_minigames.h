#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "packet_types.h"

// 福瑞宝箱（活动 900）的协议定义。
// 权威来源：HeavenFuruiBox.swf / HeavenFurui.swf 反编译源码
// swf_cache/probe/furui/dec_HeavenFuruiBox/scripts/HeavenFuruiBox/heavenfuruibox/model/ActiveModel.as
namespace HeavenFurui {
// 活动 900 挂在旧活动总线 OP_CLIENT_ACTIVITY_QINGYANG（send 1184833 / back 1324097）上，
// 与新总线 ACTIVITY_ACTION_* 无关。见 MainModel.as:737 进图后自动补发的查询。
constexpr int ACTIVITY_ID = 900;

// body[0] 是操作码，参数从 body[1] 开始。
constexpr int OP_QUERY = 1;          // [1, 地图id]          查询当前场景宝箱
constexpr int OP_OPEN_BOX = 2;       // [2, 地图id, 宝箱id]  花铜钱开箱
constexpr int OP_PICK_REWARD = 4;    // [4, 0] 抽奖 / [4, 1] 放弃
constexpr int OP_BOX_BROADCAST = 5;  // 服务端推送：他人开箱（客户端同号用于设置跳过抽奖动画）
constexpr int OP_STATE_PUSH = 6;     // 服务端推送：0=宝箱清空，5=要求重查当前场景

constexpr int MAX_BOX_PER_MAP = 5;      // AS3 boxNum > 0 && boxNum <= 5 才读 boxId
constexpr int DAILY_OPEN_LIMIT = 30;    // AS3 openCount >= 30 直接拒绝
constexpr int REWARD_OPTION_COUNT = 8;  // 开箱成功回包跟 8 个候选奖励 id
constexpr int OPEN_INTERVAL_MS = 300;  // 开箱请求之间的节流间隔（AS3 的 3 秒是给手点防误触用的）
}  // namespace HeavenFurui

// 福瑞宝箱运行状态（hook 核心的进图回包要置位 entered）。
extern std::atomic<bool> g_heavenFuruiRunning;
extern std::atomic<bool> g_heavenFuruiEnteredMap;
extern std::atomic<int> g_heavenFuruiMaxBoxes;

BOOL SendHeavenFuruiPacket(int opType, const std::vector<int32_t>& bodyValues = {});
BOOL SendHeavenFuruiQueryPacket(int mapId);
BOOL SendHeavenFuruiOpenBoxPacket(int mapId, int boxId);
BOOL SendHeavenFuruiPickRewardPacket();
BOOL StartOneKeyHeavenFuruiPacket(int maxBoxes = HeavenFurui::DAILY_OPEN_LIMIT);
void StopHeavenFurui();
void SetHeavenFuruiMaxBoxes(int maxBoxes);
void ProcessHeavenFuruiResponse(const GamePacket& packet);
