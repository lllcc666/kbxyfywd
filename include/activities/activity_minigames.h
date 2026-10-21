#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "packet_types.h"

namespace Act778 {
constexpr int ACTIVITY_ID = 778;
constexpr int MAX_SCORE = 1500;
}  // namespace Act778

BOOL SendAct778Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});
BOOL SendAct778GameInfoPacket();
BOOL SendAct778StartGamePacket();
BOOL SendAct778GameHitPacket(int below);
BOOL SendAct778EndGamePacket(int monsterCount, int endType);
BOOL SendAct778SweepInfoPacket();
BOOL SendAct778SweepPacket();
void ProcessAct778Response(const GamePacket& packet);


BOOL SendAct685Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});


BOOL SendAct666Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});


BOOL SendAct641Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});


BOOL SendAct684Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});


BOOL SendAct717Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});


BOOL SendAct805Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});


BOOL SendAct757Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});


BOOL SendAct631Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});

namespace Act782 {
constexpr int ACTIVITY_ID = 782;
constexpr int PASS_SCORE = 200;
constexpr int TARGET_SCORE = 400;
}  // namespace Act782

BOOL SendAct782Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});
BOOL SendAct782OpenUIPacket();
BOOL SendAct782StartGamePacket(int ruleFlag);
BOOL SendAct782EndGamePacket(int score);
BOOL SendAct782SweepInfoPacket();
BOOL SendAct782SweepPacket();
void ProcessAct782Response(const GamePacket& packet);

namespace Act804 {
constexpr int ACTIVITY_ID = 804;
constexpr int TARGET_SCORE = 450;
}  // namespace Act804

BOOL SendAct804Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});
BOOL SendAct804GameInfoPacket();
BOOL SendAct804StartGamePacket(int promptFlag = 1);
BOOL SendAct804EndGamePacket(const std::string& jsonStr);
BOOL SendAct804SweepInfoPacket();
BOOL SendAct804SweepPacket();
void ProcessAct804Response(const GamePacket& packet);


BOOL SendAct810Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});

namespace Act811 {
constexpr int ACTIVITY_ID = 811;
}  // namespace Act811

BOOL SendAct811Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});
BOOL SendAct811OpenUIPacket();
BOOL SendAct811StartGamePacket(int hintFlag = 0);
BOOL SendAct811SweepInfoPacket();
BOOL SendAct811SweepPacket();
BOOL SendAct811EndGamePacket();
void ProcessAct811Response(const GamePacket& packet);


BOOL SendAct822Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});


BOOL SendAct826Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});


BOOL SendAct827Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});

namespace Act808 {
constexpr int ACTIVITY_ID = 808;
constexpr int DAILY_TASK_ID = 4039001;
constexpr int MAX_SCORE = 55;
}  // namespace Act808

BOOL SendAct808Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});
BOOL SendAct808OpenUIPacket();
BOOL SendAct808StartGamePacket(int popRule = 0);
BOOL SendAct808GameingPacket(int index);
BOOL SendAct808EndGamePacket(int score);
BOOL SendAct808SweepInfoPacket();
BOOL SendAct808SweepPacket();
void ProcessAct808Response(const GamePacket& packet);
// 剩余次数：< 0 表示尚未解析到。
int GetAct808PlayCount();

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
