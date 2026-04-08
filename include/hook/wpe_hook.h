#pragma once

#include <windows.h>

#include <string>

#include <vector>

#include <cstdint>

#include <atomic>

#include <map>

#include "packet_types.h"

typedef struct _PACKET {

    DWORD dwSize;

    BOOL  bSend;

    BYTE* pData;

    DWORD dwTime;

} PACKET, *PPACKET;

typedef void (*PACKET_CALLBACK)(BOOL bSend, const BYTE* pData, DWORD dwSize);

typedef HRESULT (*EXECUTE_SCRIPT_FUNC)(const WCHAR* script);

namespace WpeHook {

constexpr int DANCE_MAP_ID = 1028;

constexpr int DANCE_MAX_DAILY_COUNT = 3;

constexpr int DEEP_DIG_DEFAULT_COUNT = 2;

constexpr int TRIAL_CHECK_CODE_COEFF_NORMAL = 56;

constexpr int TRIAL_CHECK_CODE_COEFF_STORM = 72;

constexpr DWORD TIMEOUT_SHORT = 100;

constexpr DWORD TIMEOUT_NORMAL = 300;

constexpr DWORD TIMEOUT_RESPONSE = 3000;

constexpr DWORD TIMEOUT_SEND = 5000;

constexpr DWORD TIMEOUT_RETRY_INTERVAL = 500;

}

#define WM_EXECUTE_JS           (WM_USER + 101)

#define WM_DECOMPOSE_COMPLETE   (WM_USER + 102)

#define WM_DECOMPOSE_HEX_PACKET (WM_USER + 103)

#define WM_DAILY_TASK_COMPLETE  (WM_USER + 104)

BOOL InitializeHooks();

VOID UninitializeHooks();

VOID StartIntercept();

VOID StopIntercept();

VOID SetInterceptType(BOOL bSend, BOOL bRecv);

void SetPacketCallback(PACKET_CALLBACK callback);

void SetExecuteScriptFunction(EXECUTE_SCRIPT_FUNC func);

using PacketProgressCallback = void (*)(DWORD, DWORD, const std::string&);

BOOL SendPacket(
    SOCKET s,
    const BYTE* pData,
    DWORD dwSize,
    uint32_t expectedOpcode = 0,
    DWORD timeoutMs = 0,
    uint32_t expectedParams = 0,
    bool matchExpectedParams = false
);

VOID ClearPacketList();

VOID DeleteSelectedPackets(const std::vector<DWORD>& indices);

DWORD GetPacketCount();

BOOL GetPacket(DWORD index, PPACKET pPacket);

std::string HexToString(const BYTE* pData, DWORD dwSize);

std::vector<BYTE> StringToHex(const std::string& str);

void AddPacketToUI(BOOL bSend, const BYTE* pData, DWORD dwSize, DWORD dwTime);

void SyncPacketsToUI();

BOOL InitializeWpeHook();

VOID CleanupWpeHook();

BOOL SendQueryLingyuPacket();

BOOL SendQueryBagPacket();

BOOL SendDecomposeLingyuPacket(const std::wstring& jsonArray);

BOOL SendQueryMonsterPacket();

BOOL SendDeepDigPacketNTimes(int count = WpeHook::DEEP_DIG_DEFAULT_COUNT);

BOOL SendDeepDigPacket();

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

BOOL SendFireWindEndPacket(int result, int awardCount, int checkCode);

BOOL SendFireTrialPacket();

BOOL SendFireEndPacket(int result, int awardCount, int checkCode);

BOOL SendStormTrialPacket();

BOOL SendStormEndPacket(int exitStatus, int brand, int checkCode);

namespace TowerOpcode {

    constexpr uint32_t BUY_DICE_SEND = 1185569;

    constexpr uint32_t CLAIM_DICE = 393216;

    constexpr uint32_t ENTER_SCENE_SEND = 1184313;

    constexpr uint32_t START_BATTLE = 1184788;

    constexpr uint32_t EXIT_SCENE = 4370;

    constexpr uint32_t ACTIVITY_QINGYANG = 1184833;

}

constexpr int TOWER_MAP_ID = 16006;

BOOL SendPutToSpiritStorePacket(int param1, int param2);

BOOL SendFabaoPacket(int type, uint32_t userId);

BOOL SendClaimFreeDicePacket();

BOOL SendTowerCheckInfoPacket();

BOOL SendBuyDicePacket();

BOOL SendBuyDice5Packet();

BOOL SendEnterTowerMapPacket();

BOOL SendThrowBonesPacket();

BOOL SendExitScenePacket(uint32_t userId);

BOOL SendReenterTowerMapPacket();

BOOL StartOneKeyTowerPacket();

void ProcessTowerActivityResponse(const GamePacket& packet);

void StartDailyTasksAsync(DWORD flags);

BOOL StartEightTrigramsTaskAsync();

VOID StopEightTrigramsTask();

BOOL StartOneKeyCollectPacket(DWORD flags);

void ProcessCollectResponse(const GamePacket& packet);

extern std::atomic<uint32_t> g_userId;

extern HWND g_hWnd;

extern std::atomic<bool> g_autoHeal;

extern std::atomic<bool> g_blockBattle;

extern std::atomic<bool> g_autoGoHome;

extern std::atomic<int32_t> g_battleCounter;

extern std::atomic<bool> g_battleStarted;

extern std::atomic<int> g_md5CheckIndex;

extern std::atomic<bool> g_collectFinished;

extern std::atomic<bool> g_collectAutoMode;

extern std::atomic<int> g_collectStatus;

struct HexPacketData {

    char* data;

    size_t len;

};

enum HijackType {

    HIJACK_NONE = 0,

    HIJACK_BLOCK = 1,

    HIJACK_REPLACE = 2

};

struct HijackRule {

    HijackType type;

    bool forSend;

    bool forRecv;

    std::string searchHex;

    std::string replaceHex;

};

std::string GetPacketLabel(uint32_t opcode, bool bSend);

BOOL AddHijackRule(HijackType type, bool forSend, bool forRecv,

                   const std::string& searchHex, const std::string& replaceHex = "");

VOID ClearHijackRules();

VOID SetHijackEnabled(bool enable);

bool ProcessHijack(bool bSend, const BYTE* pData, DWORD* pdwSize, std::vector<BYTE>* pModifiedData);

BOOL SavePacketsToFile(const std::string& filePath);

int LoadPacketsFromFile(const std::string& filePath);

DWORD SendAllPackets(DWORD intervalMs = 100, DWORD loopCount = 1,

                     PacketProgressCallback progressCallback = nullptr);

VOID StopAutoSend();

BOOL SendBattlePacket(uint32_t spiritId, uint32_t useId = 0, uint8_t extraParam = 0, uint32_t forcedCounter = 0);

struct PackItemInfo {

    uint32_t position;

    uint32_t id;

    uint32_t count;

    uint32_t packcode;

};

struct ItemInfo {

    uint32_t id;

    std::wstring name;

    uint32_t price;

    std::wstring desc;

};

extern std::map<uint32_t, uint32_t> g_itemPositionMap;

extern std::vector<PackItemInfo> g_packItems;

BOOL SendReqPackageDataPacket(uint32_t packType = 0xFFFFFFFF);

void ProcessPackageDataResponse(const GamePacket& packet);

BOOL SendBuyGoodsPacket(uint32_t itemId, uint32_t count);

BOOL SendUseItemInBattlePacket(uint32_t itemId, uint32_t position = 0);

BOOL SendUsePropsPacket(uint32_t itemId, uint32_t spiritId = 0,

                        uint32_t param1 = 1, uint32_t param2 = 0, uint32_t param3 = 0);

void ProcessBuyGoodsResponse(const GamePacket& packet);

void ProcessUsePropsResponse(const GamePacket& packet);

std::wstring GetItemName(uint32_t itemId);

uint32_t GetItemPrice(uint32_t itemId);

uint32_t GetItemPosition(uint32_t itemId);

extern std::wstring g_loginKey;

extern std::atomic<bool> g_loginKeyCaptured;

BOOL ExtractLoginKeyFromPacket(const BYTE* pData, DWORD len);

std::wstring BuildLoginUrl(const std::wstring& hexPacket);

void ProcessMD5CheckAndReply(const std::vector<BYTE>& body, uint32_t params);

