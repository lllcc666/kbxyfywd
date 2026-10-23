#pragma once

#include <windows.h>

#include <string>

#include <vector>

#include <cstdint>

#include <atomic>

#include <map>

#include "window_messages.h"
#include "packet_types.h"

typedef struct _PACKET {

    DWORD dwSize;

    BOOL  bSend;

    BYTE* pData;

    DWORD dwTime;

} PACKET, *PPACKET;

typedef HRESULT (*EXECUTE_SCRIPT_FUNC)(const WCHAR* script);

namespace WpeHook {

// 版本与窗口标题：单一事实来源。窗口标题必须与 FindWindowW 的查找串完全一致，
// 否则窗口句柄回退查找会失败，故统一在此定义，避免各处字面量不一致。
constexpr wchar_t APP_VERSION_W[] = L"1.17";
constexpr wchar_t APP_WINDOW_TITLE_W[] = L"卡布西游浮影微端 V1.17";

constexpr int DANCE_MAX_DAILY_COUNT = 3;

constexpr int DEEP_DIG_DEFAULT_COUNT = 2;

constexpr DWORD TIMEOUT_RESPONSE = 3000;

constexpr DWORD TIMEOUT_SEND = 5000;

constexpr DWORD TIMEOUT_RETRY_INTERVAL = 500;

}

BOOL InitializeHooks();

VOID UninitializeHooks();

VOID StartIntercept();

VOID StopIntercept();

VOID SetInterceptType(BOOL bSend, BOOL bRecv);

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

/// 封包列表容量上限：超限后淘汰最旧的包，避免长时间挂机把内存吃满。
constexpr std::size_t kMaxPacketListSize = 20000;

/// 封包列表的深拷贝快照：脱离 g_packetListCS 之后继续使用封包内容，
/// 避免直接持有裸 pData 而在别的线程清列表后读到已释放内存。
struct PacketSnapshot {
    std::size_t index;   ///< 在 g_PacketList 中的下标（0 起始），界面序号用它推
    bool bSend;
    DWORD dwTime;
    std::vector<BYTE> data;
};

/// 把封包时间（GetTickCount 基准）格式化成 hh:mm:ss。
void FormatPacketClock(DWORD tick, wchar_t* out, std::size_t outCount);

/// 取封包列表的深拷贝快照（内部加锁）。
std::vector<PacketSnapshot> SnapshotPacketList();

std::string HexToString(const BYTE* pData, DWORD dwSize);

std::vector<BYTE> StringToHex(const std::string& str);

/// 把封包追加进列表并同步到界面。入表、序号、UI 三件事在同一临界区内定序，
/// 收/发两条 hook 线程并发时不会拿到重复序号；达到容量上限时淘汰最旧的包。
void AppendPacketAndShow(const PACKET& packet);

void SyncPacketsToUI();

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

/// UI 线程安全的进度查询：内部开线程做等待，避免阻塞 UI/socket 线程。
BOOL QueryTaskZoneUserTaskListProgressAsync(uint32_t taskId);

BOOL StartHuangchengWeijiTaskAsync(const std::vector<uint32_t>& subtaskIds);

VOID StopHuangchengWeijiTask();

/// 把主线任务配方（子任务清单）推送给 UI 渲染。
void PushStoryTaskDefinitionToUi();

BOOL StartOneKeyCollectPacket(DWORD flags);

void ProcessCollectResponse(const GamePacket& packet);

extern std::atomic<uint32_t> g_userId;
SOCKET GetLastGameSocket();

extern HWND g_hWnd;

extern std::atomic<bool> g_autoHeal;

extern std::atomic<bool> g_blockBattle;

extern std::atomic<bool> g_autoGoHome;

extern std::atomic<int32_t> g_battleCounter;

extern std::atomic<bool> g_battleStarted;

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

std::wstring GetItemName(uint32_t itemId);

extern std::wstring g_loginKey;

BOOL ExtractLoginKeyFromPacket(const BYTE* pData, DWORD len);

std::wstring BuildLoginUrl(const std::wstring& hexPacket);

void ProcessMD5CheckAndReply(const std::vector<BYTE>& body, uint32_t params);

/**
 * @brief 查询全部「最新活动」的剩余次数并异步回填到前端。
 *
 * 逐个下发活动 open_ui 查询，解析后调用 JS 的 window.updateActivityCounts，
 * 由前端更新次数显示并自动禁用次数为 0 的活动。
 */
void QueryActivityCountsAsync();
