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

namespace Act685 {
constexpr int ACTIVITY_ID = 685;
constexpr int PASS_SCORE = 40;
constexpr int MAX_SCORE = 50;
}  // namespace Act685

BOOL SendAct685Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});
BOOL SendAct685OpenUIPacket();
BOOL SendAct685StartGamePacket(int ruleFlag = 0);
BOOL SendAct685EndGamePacket(int score);
BOOL SendAct685SweepInfoPacket();
BOOL SendAct685SweepPacket();


BOOL SendAct666Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});
BOOL SendAct666OpenUIPacket();
BOOL SendAct666StartGamePacket(int isPropt = 0, int type = 0);
BOOL SendAct666EndGamePacket(int medalCount, bool isPass = true);
BOOL SendAct666SweepInfoPacket();
BOOL SendAct666SweepPacket();

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

namespace Act684 {
constexpr int ACTIVITY_ID = 684;
constexpr int PASS_SCORE = 2000;
constexpr int MAX_SCORE = 3000;
constexpr int TARGET_SCORE = PASS_SCORE;
}  // namespace Act684

BOOL SendAct684Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});
BOOL SendAct684OpenUIPacket();
BOOL SendAct684StartGamePacket(int ruleFlag = 0);
BOOL SendAct684EndGamePacket(int score);
BOOL SendAct684SweepInfoPacket();
BOOL SendAct684SweepPacket();

namespace Act717 {
constexpr int ACTIVITY_ID = 717;
constexpr int TARGET_SCORE = 200;
}  // namespace Act717

BOOL SendAct717Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});
BOOL SendAct717OpenUIPacket();
BOOL SendAct717StartGamePacket(int promptFlag = 0);
BOOL SendAct717EndGamePacket(int score);
BOOL SendAct717SweepInfoPacket();
BOOL SendAct717SweepPacket();


BOOL SendAct805Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});
BOOL SendAct805GameInfoPacket();
BOOL SendAct805StartGamePacket();
BOOL SendAct805EndGamePacket(int score);
BOOL SendAct805SweepInfoPacket();
BOOL SendAct805SweepPacket();


BOOL SendAct757Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});
BOOL SendAct757OpenUIPacket();
BOOL SendAct757StartGamePacket(int promptFlag = 0);
BOOL SendAct757EndGamePacket(int score);
BOOL SendAct757SweepInfoPacket();
BOOL SendAct757SweepPacket();

namespace Act631 {
constexpr int ACTIVITY_ID = 631;
constexpr int PASS_SCORE = 200;
constexpr int DAILY_TASK_ID = 4039001;
}  // namespace Act631

BOOL SendAct631Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});
BOOL SendAct631OpenUIPacket();
BOOL SendAct631StartGamePacket(int ruleFlag = 0);
BOOL SendAct631EndGamePacket(int isPass, int clientCheckCode, int score);
BOOL SendAct631SweepInfoPacket();
BOOL SendAct631SweepPacket();

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
BOOL SendAct810OpenUIPacket();
BOOL SendAct810StartGamePacket(int ruleFlag = 1);
BOOL SendAct810EndGamePacket(int medalCount);
BOOL SendAct810SweepInfoPacket();
BOOL SendAct810SweepPacket();

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

namespace Act822 {
constexpr int ACTIVITY_ID = 822;
constexpr int PASS_SCORE = 35;
constexpr int MAX_SCORE = 35;
constexpr int PASS_EXP = 10000;
constexpr int PASS_XIUWEI = 30;
constexpr int DAILY_TASK_ID = 4039001;
}  // namespace Act822

BOOL SendAct822Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});
BOOL SendAct822OpenUIPacket();
BOOL SendAct822StartGamePacket(int ruleFlag = 0);
BOOL SendAct822EndGamePacket(int score, bool isPass = true);
BOOL SendAct822SweepInfoPacket();
BOOL SendAct822SweepPacket();

namespace Act826 {
constexpr int ACTIVITY_ID = 826;
constexpr int PASS_SCORE = 600;
}  // namespace Act826

BOOL SendAct826Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});
BOOL SendAct826OpenUIPacket();
BOOL SendAct826StartGamePacket(int ruleFlag = 0);
BOOL SendAct826EndGamePacket(int checkCode, int score);
BOOL SendAct826SweepInfoPacket();
BOOL SendAct826SweepPacket();

namespace Act827 {
constexpr int ACTIVITY_ID = 827;
constexpr int TARGET_SCORE = 1000;
}  // namespace Act827

BOOL SendAct827Packet(const std::string& operation, const std::vector<int32_t>& bodyValues = {});
BOOL SendAct827OpenUIPacket();
BOOL SendAct827StartGamePacket(int promptFlag = 0);
BOOL SendAct827EndGamePacket(int checkCode, int score);
BOOL SendAct827SweepInfoPacket();
BOOL SendAct827SweepPacket();

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
