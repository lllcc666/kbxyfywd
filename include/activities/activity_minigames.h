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
BOOL StartOneKeyAct778Packet(bool useSweep = false);
void ProcessAct778Response(const GamePacket& packet);

namespace Act666 {
constexpr int ACTIVITY_ID = 666;
}  // namespace Act666

BOOL SendAct666Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});
BOOL SendAct666OpenUIPacket();
BOOL SendAct666StartGamePacket(int isPropt = 0, int type = 0);
BOOL SendAct666EndGamePacket(int medalCount, bool isPass = true);
BOOL SendAct666SweepInfoPacket();
BOOL SendAct666SweepPacket();
BOOL StartOneKeyAct666Packet(bool useSweep = false);
void ProcessAct666Response(const GamePacket& packet);

namespace Act641 {
constexpr int ACTIVITY_ID = 641;
constexpr int TARGET_SCORE = 1000;
constexpr int MAX_SCORE = 1800;
}  // namespace Act641

BOOL SendAct641Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});
BOOL SendAct641OpenUIPacket();
BOOL SendAct641StartGamePacket();
BOOL SendAct641EndGamePacket(int score);
BOOL SendAct641SweepInfoPacket();
BOOL SendAct641SweepPacket();
BOOL StartOneKeyAct641Packet(bool useSweep = false, int targetScore = Act641::TARGET_SCORE);
void ProcessAct641Response(const GamePacket& packet);

namespace Act805 {
constexpr int ACTIVITY_ID = 805;
constexpr int TARGET_SCORE = 350;
constexpr int DAILY_TASK_ID = 4039001;
}  // namespace Act805

BOOL SendAct805Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});
BOOL SendAct805GameInfoPacket();
BOOL SendAct805StartGamePacket();
BOOL SendAct805EndGamePacket(int score);
BOOL SendAct805SweepInfoPacket();
BOOL SendAct805SweepPacket();
BOOL StartOneKeyAct805Packet(bool useSweep = false, int targetScore = Act805::TARGET_SCORE);
void ProcessAct805Response(const GamePacket& packet);

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
BOOL StartOneKeyAct782Packet(bool useSweep = false, int targetScore = Act782::TARGET_SCORE);
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
BOOL StartOneKeyAct804Packet(bool useSweep = false, int targetScore = Act804::TARGET_SCORE);
void ProcessAct804Response(const GamePacket& packet);

namespace Act810 {
constexpr int ACTIVITY_ID = 810;
}  // namespace Act810

BOOL SendAct810Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});
BOOL SendAct810OpenUIPacket();
BOOL SendAct810StartGamePacket(int ruleFlag = 1);
BOOL SendAct810EndGamePacket(int medalCount);
BOOL SendAct810SweepInfoPacket();
BOOL SendAct810SweepPacket();
BOOL StartOneKeyAct810Packet(bool useSweep = false);
void ProcessAct810Response(const GamePacket& packet);

namespace Act811 {
constexpr int ACTIVITY_ID = 811;
}  // namespace Act811

BOOL SendAct811Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});
BOOL SendAct811OpenUIPacket();
BOOL SendAct811StartGamePacket(int hintFlag = 0);
BOOL SendAct811SweepInfoPacket();
BOOL SendAct811SweepPacket();
BOOL SendAct811EndGamePacket();
BOOL StartOneKeyAct811Packet(bool useSweep = false);
void ProcessAct811Response(const GamePacket& packet);

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
BOOL StartOneKeyAct808Packet(bool useSweep = false);
void ProcessAct808Response(const GamePacket& packet);

namespace HeavenFurui {
constexpr int ACTIVITY_ID = 900;
constexpr int OP_QUERY = 1;
constexpr int OP_PICKUP = 2;
constexpr int OP_CONFIRM = 4;
constexpr int MAX_BOX_COUNT = 10;
}  // namespace HeavenFurui

BOOL SendHeavenFuruiPacket(int opType, const std::vector<int32_t>& bodyValues = {});
BOOL SendHeavenFuruiQueryPacket(int mapId);
BOOL SendHeavenFuruiPickupPacket(int mapId, int boxId);
BOOL SendHeavenFuruiConfirmPacket();
BOOL StartOneKeyHeavenFuruiPacket(int maxBoxes = 30);
void StopHeavenFurui();
void SetHeavenFuruiMaxBoxes(int maxBoxes);
void ProcessHeavenFuruiResponse(const GamePacket& packet);
