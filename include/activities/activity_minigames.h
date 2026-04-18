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

namespace Act793 {
constexpr int ACTIVITY_ID = 793;
constexpr int BLOOD_MAX = 5;
constexpr int LEVEL_MAX = 5;
constexpr int TARGET_MEDALS = 75;
}  // namespace Act793

BOOL SendAct793Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});
BOOL SendAct793GameInfoPacket();
BOOL SendAct793StartGamePacket();
BOOL SendAct793GameHitPacket(int hitCount);
BOOL SendAct793EndGamePacket(int medalCount);
BOOL SendAct793SweepInfoPacket();
BOOL SendAct793SweepPacket();
BOOL StartOneKeyAct793Packet(bool useSweep = false, int targetMedals = Act793::TARGET_MEDALS);
void ProcessAct793Response(const GamePacket& packet);

namespace Act791 {
constexpr int ACTIVITY_ID = 791;
constexpr int GAME_DURATION = 60;
constexpr int TARGET_SCORE = 250;
constexpr uint32_t EXTRA_OPCODE = 1184812;
constexpr int EXTRA_PARAMS = 3;
}  // namespace Act791

BOOL SendAct791Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});
BOOL SendAct791GameInfoPacket();
BOOL SendAct791StartGamePacket();
BOOL SendAct791EndGamePacket(int score);
BOOL SendAct791SweepInfoPacket();
BOOL SendAct791SweepPacket();
BOOL StartOneKeyAct791Packet(bool useSweep = false, int targetScore = Act791::TARGET_SCORE);
void ProcessAct791Response(const GamePacket& packet);

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

namespace Act803 {
constexpr int ACTIVITY_ID = 803;
constexpr int MAX_NUM = 25;
}  // namespace Act803

BOOL SendAct803Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});
BOOL SendAct803GameInfoPacket();
BOOL SendAct803StartGamePacket(int startFlag = 1);
BOOL SendAct803EndGamePacket(int score, bool isWin = true);
BOOL SendAct803SweepInfoPacket();
BOOL SendAct803SweepPacket();
BOOL StartOneKeyAct803Packet(bool useSweep = false, int targetScore = Act803::MAX_NUM);
void ProcessAct803Response(const GamePacket& packet);

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

namespace Act624 {
constexpr int ACTIVITY_ID = 624;
}  // namespace Act624

BOOL SendAct624Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});
BOOL SendAct624GameInfoPacket();
BOOL SendAct624StartGamePacket(int promptFlag = 1);
BOOL SendAct624EndRoundPacket(int round, int gameTime, int mushroomNum);
BOOL SendAct624NextRoundPacket();
BOOL SendAct624SweepInfoPacket();
BOOL SendAct624SweepPacket();
BOOL StartOneKeyAct624Packet(bool useSweep = false);
void ProcessAct624Response(const GamePacket& packet);

namespace SeaBattle {
constexpr int ACTIVITY_ID = 653;
constexpr int PASS_SCORE = 300;
constexpr int TARGET_SCORE = 1000;
constexpr uint32_t EXTRA_OPCODE = 1184812;
constexpr int EXTRA_PARAMS = 3;
constexpr int EXTRA_TASK_ID = 4039001;
}  // namespace SeaBattle

BOOL SendSeaBattlePacket(const std::string& operation, const std::vector<int32_t>& bodyValues = {});
BOOL SendSeaBattleUIInfoPacket();
BOOL SendSeaBattleStartGamePacket(int promptFlag = 0);
BOOL SendSeaBattleEndGamePacket(int score);
BOOL SendSeaBattleSweepInfoPacket();
BOOL SendSeaBattleSweepPacket();
BOOL StartOneKeySeaBattlePacket(bool useSweep = false);
void ProcessSeaBattleResponse(const GamePacket& packet);

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
