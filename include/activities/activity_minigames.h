#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "packet_types.h"


// 剩余次数：< 0 表示尚未解析到。

namespace HeavenFurui {
constexpr int ACTIVITY_ID = 900;
constexpr int OP_QUERY = 1;
constexpr int OP_PICKUP = 2;
constexpr int OP_CONFIRM = 4;
}  // namespace HeavenFurui

// 福瑞宝箱运行状态（hook 核心的进图回包要置位 entered）。
extern std::atomic<bool> g_heavenFuruiRunning;
extern std::atomic<bool> g_heavenFuruiQuerySuccess;
extern std::atomic<int> g_heavenFuruiBoxCount;
extern std::atomic<bool> g_heavenFuruiEnteredMap;
extern std::atomic<int> g_heavenFuruiMaxBoxes;
extern std::atomic<int> g_heavenFuruiOpenedBoxes;

BOOL SendHeavenFuruiPacket(int opType, const std::vector<int32_t>& bodyValues = {});
BOOL SendHeavenFuruiQueryPacket(int mapId);
BOOL SendHeavenFuruiPickupPacket(int mapId, int boxId);
BOOL SendHeavenFuruiConfirmPacket();
BOOL StartOneKeyHeavenFuruiPacket(int maxBoxes = 30);
void StopHeavenFurui();
void SetHeavenFuruiMaxBoxes(int maxBoxes);
void ProcessHeavenFuruiResponse(const GamePacket& packet);
