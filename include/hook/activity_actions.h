#pragma once
#include <windows.h>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "packet_types.h"

BOOL InitializeWpeHook();
VOID CleanupWpeHook();
BOOL SendQueryLingyuPacket();
BOOL SendQueryBagPacket();
BOOL SendDecomposeLingyuPacket(const std::wstring& jsonArray);
BOOL SendQueryMonsterPacket();
BOOL SendQueryDeepDigCountPacket();
void ProcessDeepDigResponse(const GamePacket& packet);
void ProcessDeepDigQueryResponse(const GamePacket& packet);
BOOL SendDailyCardPacket();
BOOL SendDailyGiftPacket();
BOOL SendWeeklyGiftPacket();
BOOL SendFamilyCheckinPacket();
BOOL SendFamilyReportPacket();
BOOL SendFamilyDefendPacket();
BOOL SendShopSurprisePacket();
BOOL SendBeibeiHealPacket();
BOOL SendMD5CheckReplyPacket(int index);
BOOL SendEnterScenePacket(int mapId, uint32_t expectedOpcode = 0, DWORD timeoutMs = 0);
BOOL SendDanceActivityPacketEx(int params, const std::vector<int32_t>& bodyValues);
BOOL SendDanceStagePacketEx(int params, const std::vector<int32_t>& bodyValues);
BOOL SendDanceEnterPacket();
BOOL SendDanceStartPacket();
BOOL SendDanceProcessPacketEx(int serverTime, int danceCounter);
BOOL SendDanceEndPacketEx();
BOOL SendDanceExitPacketEx();
BOOL SendDanceSubmitScorePacket(int serverScore);
void ProcessDanceActivityResponse(const GamePacket& packet);
void ProcessDanceStageResponse(const GamePacket& packet);
BOOL SendDanceContestPacket();
void ProcessTrialResponse(const GamePacket& packet);
BOOL SendFireWindTrialPacket();
BOOL SendFireTrialPacket();
BOOL SendStormTrialPacket();
BOOL SendTowerCheckInfoPacket();
BOOL SendBuyDicePacket();
BOOL SendThrowBonesPacket();
BOOL SendExitScenePacket(uint32_t userId);
BOOL SendReenterTowerMapPacket();
BOOL StartOneKeyTowerPacket();
void ProcessTowerActivityResponse(const GamePacket& packet);
void StartDailyTasksAsync(DWORD flags);
BOOL StartEightTrigramsTaskAsync();
VOID StopEightTrigramsTask();
BOOL QueryTaskZoneUserTaskListProgress(uint32_t taskId);
BOOL QueryTaskZoneUserTaskListProgressAsync(uint32_t taskId);
BOOL StartHuangchengWeijiTaskAsync(const std::vector<uint32_t>& subtaskIds);
VOID StopHuangchengWeijiTask();
void PushStoryTaskDefinitionToUi();
BOOL StartOneKeyCollectPacket(DWORD flags);
void ProcessCollectResponse(const GamePacket& packet);

struct PackItemInfo { uint32_t position; uint32_t id; uint32_t count; uint32_t packcode; };
extern std::map<uint32_t, uint32_t> g_itemPositionMap;
extern std::vector<PackItemInfo> g_packItems;
BOOL SendReqPackageDataPacket(uint32_t packType = 0xFFFFFFFF);
void ProcessPackageDataResponse(const GamePacket& packet);
BOOL SendBuyGoodsPacket(uint32_t itemId, uint32_t count);
BOOL SendUseItemInBattlePacket(uint32_t itemId, uint32_t position = 0);
std::wstring GetItemName(uint32_t itemId);
void SetLoginKey(const std::wstring& key);
std::wstring GetLoginKey();
BOOL ExtractLoginKeyFromPacket(const BYTE* pData, DWORD len);
std::wstring BuildLoginUrl(const std::wstring& hexPacket);
void ProcessMD5CheckAndReply(const std::vector<BYTE>& body, uint32_t params);
void QueryActivityCountsAsync();
