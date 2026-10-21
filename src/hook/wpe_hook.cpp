/**
 * @file wpe_hook.cpp
 * @brief WPE网络封包拦截模块实现
 * 
 * 实现 Windows Socket API 的 Hook，拦截游戏网络封包
 */

#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <condition_variable>
#include <mutex>
#include <string>
#include <array>
#include <vector>
#include <memory>
#include <tuple>
#include <algorithm>
#include <deque>
#include <map>
#include <unordered_map>
#include <fstream>
#include <thread>
#include <random>
#include <cctype>
#include <chrono>

// 第三方库
#include <MemoryModule.h>
#include <MinHook.h>

// 项目头文件
#include "wpe_hook.h"
#include "wpe_hook_internal.h"
#include "hook_shared.h"
#include "story_task_internal.h"
#include "task_zone_internal.h"
#include "activity_states_internal.h"
#include "activity_minigames.h"
#include "battle_six.h"
#include "dungeon_jump.h"
#include "horse_competition.h"
#include "latest_activities.h"
#include "shuangtai.h"
#include "shuangtai_internal.h"
#include "spirit_collect.h"
#include "story_task.h"
#include "story_task_recipe.h"
#include "utils.h"
#include "packet_parser.h"
#include "packet_builder.h"
#include "app_host.h"
#include "ui_bridge.h"
#include "window_messages.h"

// 坐骑大赛活动ID（如果在packet_parser.h中未定义）
#ifndef HORSE_COMPETITION_ACT_ID
constexpr int HORSE_COMPETITION_ACT_ID = 665;
#endif

// 精魄系统活动ID（如果在packet_parser.h中未定义）
#ifndef SPIRIT_COLLECT_ACT_ID
constexpr int SPIRIT_COLLECT_ACT_ID = 754;
#endif

// 精魄系统响应处理函数声明
void ProcessSpiritPresuresResponse(const GamePacket& packet);
void ProcessSpiritCollectResponse(const GamePacket& packet);
void ProcessSpiritSendSpiritResponse(const GamePacket& packet);
void ProcessSpiritPlayerInfoResponse(const GamePacket& packet);
void ProcessAct808Response(const GamePacket& packet);
static void ProcessStoryTaskTalkResponse(const GamePacket& packet);
static void ProcessStoryNpcTaskListResponse(const GamePacket& packet);
static void ProcessStoryNpcListResponse(const GamePacket& packet);
static void ProcessStoryMagicIndexResponse(const GamePacket& packet);
static void ProcessStoryTaskFinishedResponse(const GamePacket& packet);

extern bool PostScriptToUI(const std::wstring& jsCode);

// 嵌入资源
#include "embedded/minhook_data.h"

// 外部变量声明（来自 packet_parser.cpp）
extern std::unordered_map<int, std::wstring> g_petNames;
extern std::unordered_map<int, std::wstring> g_skillNames;
extern std::unordered_map<int, std::wstring> g_elemNames;
extern std::unordered_map<int, std::wstring> g_geniusNames;
extern std::unordered_map<int, int> g_skillPowers;
extern std::unordered_map<int, int> g_skillRanges;
extern std::unordered_map<int, int> g_petElems;


// MinHook 宏定义
#ifndef MH_ALL_HOOKS
#define MH_ALL_HOOKS (nullptr)
#endif

// ============================================================================
// 前向声明
// ============================================================================

struct ICoreWebView2;
extern ICoreWebView2* g_webview;

// 前向声明
void ProcessMD5CheckAndReply(const std::vector<BYTE>& body, uint32_t params);

struct Md5FaceEntry {
    const char* md5;
    int face;
};

// ============================================================================
// 内部命名空间 - 封装实现细节
// ============================================================================

namespace {

// -------------------------
// Opcode 常量（内部使用）
// -------------------------

// 需要阻止发送的 Opcode（灵玉分解过程中）
constexpr uint32_t BLOCKED_OPCODE_QUERY_1 = 1185792;
constexpr uint32_t BLOCKED_OPCODE_QUERY_2 = 1185809;

// -------------------------
// 默认屏蔽的封包（无条件屏蔽，无需用户勾选）
// -------------------------

/**
 * @brief 默认屏蔽的封包结构
 * 用于匹配需要无条件屏蔽的封包
 */
struct DefaultBlockedPacket {
    uint32_t opcode;        ///< Opcode（必须匹配）
    uint32_t params;        ///< Params（0xFFFFFFFF 表示匹配任意值）
    const BYTE* bodyPrefix; ///< Body 前缀匹配（nullptr 表示不匹配 Body）
    size_t bodyPrefixLen;   ///< Body 前缀长度
};

// 默认屏蔽的 Opcode 列表（仅匹配 Opcode）
constexpr uint32_t DEFAULT_BLOCKED_OPCODES[] = {
    1311544,  // 38111400 小端序
    1314916,  // 64161400 小端序
    1311499,  // 0B111400 小端序
};

// 默认屏蔽的完整封包（需要匹配 Opcode + Params，可选匹配 Body）
// 封包1: 44531000021314000100000004000000000000000100000000000000
//   Opcode: 1314050, Params: 1, Body: 04 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00
static const BYTE g_blockBodyPrefix1[] = { 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
// 封包2: 44530400001014000500000000000000
//   Opcode: 1312768, Params: 5, Body: 空

// 默认屏蔽封包列表
static const DefaultBlockedPacket g_defaultBlockedPackets[] = {
    { 1314050, 1, g_blockBodyPrefix1, sizeof(g_blockBodyPrefix1) },  // 匹配完整封包1
    { 1312768, 5, nullptr, 0 },  // 匹配完整封包2
};

/**
 * @brief 检查封包是否在默认屏蔽列表中
 * @param opcode 封包 Opcode
 * @param params 封包 Params
 * @param body 封包 Body 数据
 * @param bodyLen Body 长度
 * @return true 表示应该屏蔽
 */
inline bool IsDefaultBlockedPacket(uint32_t opcode, uint32_t params, 
                                    const BYTE* body, size_t bodyLen) {
    // 1. 检查是否在默认屏蔽 Opcode 列表中
    for (uint32_t blockedOp : DEFAULT_BLOCKED_OPCODES) {
        if (opcode == blockedOp) {
            return true;
        }
    }
    
    // 2. 检查是否匹配完整的默认屏蔽封包
    for (const auto& blocked : g_defaultBlockedPackets) {
        // 匹配 Opcode
        if (opcode != blocked.opcode) continue;
        
        // 匹配 Params（0xFFFFFFFF 表示任意值）
        if (blocked.params != 0xFFFFFFFF && params != blocked.params) continue;
        
        // 匹配 Body 前缀（如果有）
        if (blocked.bodyPrefix != nullptr && blocked.bodyPrefixLen > 0) {
            if (bodyLen < blocked.bodyPrefixLen) continue;
            if (memcmp(body, blocked.bodyPrefix, blocked.bodyPrefixLen) != 0) continue;
        }
        
        return true;
    }
    
    return false;
}

// -------------------------
// 临界区管理（RAII）
// -------------------------

CriticalSectionScope g_packetListCS;       ///< 封包列表临界区
CriticalSectionScope g_pendingPacketsCS;   ///< 暂存封包临界区

// -------------------------
// 全局状态
// -------------------------

EXECUTE_SCRIPT_FUNC g_ExecuteScriptFunc = nullptr;  ///< JS执行函数指针
bool g_bInitialized = false;                         ///< 初始化标志

// 封包列表
std::vector<PACKET> g_PacketList;
std::vector<std::tuple<BOOL, std::vector<BYTE>, DWORD, DWORD>> g_PendingPackets;

// 拦截设置
bool g_bInterceptSend = true;     ///< 是否拦截发送包
bool g_bInterceptRecv = false;    ///< 是否拦截接收包
bool g_bInterceptEnabled = false; ///< 是否启用拦截

// 网络状态
bool g_blockOpcodeSend = false;   ///< 是否阻止特定Opcode发送

// 劫持功能
bool g_bHijackEnabled = false;    ///< 是否启用劫持功能
std::vector<HijackRule> g_HijackRules;  ///< 劫持规则列表
CriticalSectionScope g_hijackRulesCS;   ///< 劫持规则临界区

// 自动发送状态
bool g_bAutoSendEnabled = false;  ///< 是否启用自动发送
volatile bool g_bStopAutoSend = false;  ///< 停止自动发送标志


}  // anonymous namespace

// ============================================================================
// 导出全局变量（在头文件中声明为 extern）
// ============================================================================

std::atomic<uint32_t> g_userId{0};  ///< 卡布号（从进入世界封包获取）

// 最近的游戏套接字：SendPacket 传 0 时兜底，同时供拆分后的活动模块读取。
// 声明在 include/internal/hook_shared.h。
std::atomic<SOCKET> g_LastGameSocket{0};

// 注意：g_hWnd 在 demo.cpp 中定义，这里通过 wpe_hook.h 的 extern 声明使用

// 基础战斗开关 owner：回血、屏蔽战斗、自动回家和战斗校验标记统一由这一组维护。
std::atomic<bool> g_autoHeal{false};  ///< 自动回血
std::atomic<bool> g_blockBattle{false};  ///< 屏蔽战斗
std::atomic<bool> g_autoGoHome{false};   ///< 自动回家（玄塔）
std::atomic<int32_t> g_battleCounter{0};  ///< 战斗counter（用于战斗校验）
std::atomic<bool> g_battleStarted{false};  ///< 是否已进入战斗

// 一键采集 owner：完成标志、自动模式和流程状态统一由这一组维护。
std::atomic<bool> g_collectFinished{false};  ///< 采集完成标志
std::atomic<bool> g_collectAutoMode{false};  ///< 一键采集模式
std::atomic<int> g_collectStatus{0};  ///< 采集状态





// 背包缓存 owner：位置映射和物品列表统一由这一组维护。
std::map<uint32_t, uint32_t> g_itemPositionMap;
std::vector<PackItemInfo> g_packItems;

// 万妖盛会状态变量
BattleSixAutoBattle g_battleSixAuto;




// 登录 key 捕获 owner：原始 key 字符串由这里维护。
std::wstring g_loginKey;                     ///< 整个封包的十六进制字符串（大写）

namespace {

// 跳舞大赛 owner：进程、响应状态和进度统一由该状态对象维护。

// 单次跳舞失败原因：用于在重试日志中说明是哪一步出的问题。
enum DanceFailReason {
    DANCE_FAIL_NONE = 0,
    DANCE_FAIL_ENTER_MAP,      ///< 进入地图失败
    DANCE_FAIL_START_SEND,     ///< 开始封包发送失败
    DANCE_FAIL_START_TIMEOUT,  ///< 未在超时内进入游戏（服务端未响应/未开局）
    DANCE_FAIL_POSITION_TAKEN, ///< 位置被其它小卡布占用或网络不稳（gameState 5/3）
    DANCE_FAIL_THREAD_CREATE,  ///< 跳舞线程创建失败
    DANCE_FAIL_ROUND_TIMEOUT,  ///< 跳舞线程未在限定时间内完成
};

const wchar_t* DanceFailReasonText(int reason) {
    switch (reason) {
        case DANCE_FAIL_ENTER_MAP: return L"进入地图失败";
        case DANCE_FAIL_START_SEND: return L"开始封包发送失败";
        case DANCE_FAIL_START_TIMEOUT: return L"开局超时";
        case DANCE_FAIL_POSITION_TAKEN: return L"位置被占用/网络不稳";
        case DANCE_FAIL_THREAD_CREATE: return L"线程创建失败";
        case DANCE_FAIL_ROUND_TIMEOUT: return L"本轮超时";
        default: return L"未知";
    }
}

struct DanceGameState {
    int processId = 0;           ///< 当前跳舞进程ID
    int serverTime = 0;          ///< 服务器时间
    int serverDifficulty = 5;    ///< 服务器难度
    int todayRewardCnt = 0;      ///< 今日已获得奖励次数
    int drawRewardCnt = 0;       ///< 抽奖次数
    int serverScore = 0;         ///< 服务器分数
    int remainCnt = 3;           ///< 剩余次数
    int gameState = 0;           ///< 游戏状态：0=未开始，2=游戏中
    int clothNum = 0;            ///< 跳舞服装加成
    int counter = 0;             ///< 跳舞计数
    bool waitingResponse = false;///< 等待响应
    bool enteredMap = false;     ///< 是否已进入地图
    bool abnormalRound = false;  ///< 本轮被服务端判为异常（gameState 5/3）
    int lastFailReason = DANCE_FAIL_NONE;  ///< 上一次失败原因（DanceFailReason）
};

static DanceGameState g_danceState;

// 深度挖宝 owner：会话、剩余次数和自动循环状态统一由该状态对象维护。

struct DeepDigState {
    int sessionId = 0;           ///< 会话ID
    int remainingCount = 0;      ///< 剩余次数
    int targetCount = 0;         ///< 目标执行次数
    int completedCount = 0;      ///< 已完成次数
    bool waitingResponse = false;///< 等待开始响应
    bool waitingQuery = false;   ///< 等待查询响应
    bool autoMode = false;       ///< 自动循环模式
};

static DeepDigState g_deepDigState;

// -------------------------
// 回调函数
PACKET_CALLBACK g_PacketCallback = nullptr;
static std::mutex g_receiveDispatchMutex;
static std::condition_variable g_receiveDispatchCv;
static std::deque<std::vector<GamePacket>> g_receiveDispatchQueue;
static std::atomic<bool> g_receiveDispatchAccepting{false};
static std::thread g_receiveDispatchWorker;
static bool g_receiveDispatchStarted = false;
static bool g_receiveDispatchStop = false;
static constexpr size_t kMaxReceiveDispatchQueue = 64;
// -------------------------
// 辅助函数
// -------------------------

/**
 * @brief 检查是否为游戏封包
 */
inline bool IsGamePacket(const BYTE* pData, DWORD dwSize) {
    if (!pData || dwSize < 2) return false;
    uint16_t magic = static_cast<uint16_t>(pData[0]) | 
                     (static_cast<uint16_t>(pData[1]) << 8);
    return (magic == PacketProtocol::MAGIC_NORMAL || 
            magic == PacketProtocol::MAGIC_COMPRESSED);
}

/**
 * @brief 检查是否应该阻止该封包的发送
 */
inline bool ShouldBlockPacketSend(const BYTE* pData, DWORD dwSize) {
    if (!g_blockOpcodeSend || dwSize < PacketProtocol::HEADER_SIZE) {
        return false;
    }
    
    // 读取 Opcode（小端序，偏移4）
    uint32_t opcode = static_cast<uint32_t>(pData[4]) | 
                      (static_cast<uint32_t>(pData[5]) << 8) |
                      (static_cast<uint32_t>(pData[6]) << 16) |
                      (static_cast<uint32_t>(pData[7]) << 24);
    
    return (opcode == BLOCKED_OPCODE_QUERY_1 || opcode == BLOCKED_OPCODE_QUERY_2);
}

/**
 * @brief 从封包数据读取 Opcode
 */
inline uint32_t ReadOpcode(const BYTE* pData) {
    return static_cast<uint32_t>(pData[4]) | 
           (static_cast<uint32_t>(pData[5]) << 8) |
           (static_cast<uint32_t>(pData[6]) << 16) |
           (static_cast<uint32_t>(pData[7]) << 24);
}

/**
 * @brief 从封包数据读取 Params
 */
inline uint32_t ReadParams(const BYTE* pData) {
    return static_cast<uint32_t>(pData[8]) | 
           (static_cast<uint32_t>(pData[9]) << 8) |
           (static_cast<uint32_t>(pData[10]) << 16) |
           (static_cast<uint32_t>(pData[11]) << 24);
}

/**
 * @brief 从封包数据读取 Body 长度
 */
inline uint16_t ReadBodyLength(const BYTE* pData) {
    return static_cast<uint16_t>(pData[2]) | 
           (static_cast<uint16_t>(pData[3]) << 8);
}

}  // anonymous namespace

// ============================================================================
// 玄塔活动临界区（在匿名命名空间外）
// ============================================================================

static CRITICAL_SECTION g_towerCS;  // 玄塔临界区

// 玄塔活动全局变量
static bool g_towerAutoMode = false;        // 自动模式
static int g_towerDiceCount = 0;            // 实际骰子数量
static int g_towerRemainingDice = 0;       // 剩余骰子数
static bool g_towerMapEntered = false;     // 是否已进入地图
static bool g_towerBattleStarted = false;  // 是否已开始战斗
static bool g_towerResultReceived = false; // 是否收到查询结果响应
static bool g_towerWaitingResponse = false; // 等待响应
static HANDLE g_towerThread = nullptr;      // 玄塔线程句柄

// 投掷骰子结果追踪
static bool g_towerThrowResponseReceived = false;  // 是否收到投掷响应
static bool g_towerLastThrowSuccess = false;       // 最后一次投掷是否成功
static int g_towerLastThrowStep = 0;               // 最后一次投掷步数
static int g_towerLastThrowNodeType = 0;           // 最后一次投掷节点类型

// CHECK_INFO 响应追踪
static bool g_towerCheckInfoReceived = false;      // 是否收到CHECK_INFO响应
static int g_towerCheckInfoNbones = 0;             // CHECK_INFO返回的普通骰子数
static int g_towerPassFlag = 0;                    // 通关标志（0=未通关，1=已通关）
static bool g_towerIsCompleted = false;            // 玄塔是否已完成（nodetype==8）

// BUY_BONES 响应追踪
static bool g_towerBuyBonesReceived = false;       // 是否收到BUY_BONES响应
static bool g_towerBuyBonesSuccess = false;        // 购买是否成功
static int g_towerBuyBonesNum = 0;                 // 购买后的骰子数量

// ============================================================================
// 初始化与清理
// ============================================================================

void SetExecuteScriptFunction(EXECUTE_SCRIPT_FUNC func) {
    g_ExecuteScriptFunc = func;
}

BOOL InitializeWpeHook() {
    if (g_bInitialized) {
        return TRUE;
    }

    // 初始化玄塔临界区
    InitializeCriticalSection(&g_towerCS);

    // 初始化解析器
    if (!PacketParser::Initialize()) {
        DeleteCriticalSection(&g_towerCS);
        return FALSE;
    }

    // 初始化响应处理器
    ResponseDispatcher::Instance().InitializeDefaultHandlers();

    g_bInitialized = true;
    return TRUE;
}

bool IsWpeHookInitialized() {
    return g_bInitialized;
}

VOID CleanupWpeHook() {
    if (!g_bInitialized) return;

    // 停止拦截并标记为未初始化
    g_bInterceptEnabled = false;
    g_bInitialized = false;
    
    // 停止脚本执行回调
    SetExecuteScriptFunction(nullptr);
    
    // TODO: 任务执行器关闭（待完善）
    // TaskExecutor::Instance().Shutdown();
    
    // 等待可能的 Hook 函数退出
    Sleep(10);
    
    // 清理封包数据
    {
        CriticalSectionLock lock(g_packetListCS.Get());
        for (auto& packet : g_PacketList) {
            delete[] packet.pData;
            packet.pData = nullptr;
        }
        g_PacketList.clear();
    }
    
    {
        CriticalSectionLock lock(g_pendingPacketsCS.Get());
        g_PendingPackets.clear();
    }

    // 清理玄塔临界区
    DeleteCriticalSection(&g_towerCS);

    // 清理解析器
    PacketParser::Cleanup();
}

// ============================================================================
// 原始函数指针
// ============================================================================

typedef int (WINAPI* SEND_FUNC)(SOCKET s, const char* buf, int len, int flags);
typedef int (WINAPI* RECV_FUNC)(SOCKET s, char* buf, int len, int flags);

SEND_FUNC OriginalSend = nullptr;
RECV_FUNC OriginalRecv = nullptr;

// ============================================================================
// MinHook 函数指针
// ============================================================================

typedef MH_STATUS(WINAPI* PFN_MH_INITIALIZE)(void);
typedef MH_STATUS(WINAPI* PFN_MH_UNINITIALIZE)(void);
typedef MH_STATUS(WINAPI* PFN_MH_CREATEHOOK)(LPVOID pTarget, LPVOID pDetour, LPVOID* ppOriginal);
typedef MH_STATUS(WINAPI* PFN_MH_ENABLEHOOK)(LPVOID pTarget);
typedef MH_STATUS(WINAPI* PFN_MH_DISABLEHOOK)(LPVOID pTarget);

PFN_MH_INITIALIZE g_pfnMH_Initialize = nullptr;
PFN_MH_UNINITIALIZE g_pfnMH_Uninitialize = nullptr;
PFN_MH_CREATEHOOK g_pfnMH_CreateHook = nullptr;
PFN_MH_ENABLEHOOK g_pfnMH_EnableHook = nullptr;
PFN_MH_DISABLEHOOK g_pfnMH_DisableHook = nullptr;

// ============================================================================
// MinHook 内存加载
// ============================================================================

namespace {

HMEMORYMODULE g_hMinHookModule = nullptr;

BOOL LoadMinHookFromMemory() {
    g_hMinHookModule = MemoryLoadLibrary(g_minhook_x64Data, g_minhook_x64Size);
    if (!g_hMinHookModule) {
        return FALSE;
    }
    
    // 获取函数指针
    g_pfnMH_Initialize = (PFN_MH_INITIALIZE)MemoryGetProcAddress(g_hMinHookModule, "MH_Initialize");
    g_pfnMH_Uninitialize = (PFN_MH_UNINITIALIZE)MemoryGetProcAddress(g_hMinHookModule, "MH_Uninitialize");
    g_pfnMH_CreateHook = (PFN_MH_CREATEHOOK)MemoryGetProcAddress(g_hMinHookModule, "MH_CreateHook");
    g_pfnMH_EnableHook = (PFN_MH_ENABLEHOOK)MemoryGetProcAddress(g_hMinHookModule, "MH_EnableHook");
    g_pfnMH_DisableHook = (PFN_MH_DISABLEHOOK)MemoryGetProcAddress(g_hMinHookModule, "MH_DisableHook");
    
    // 验证所有函数指针
    if (!g_pfnMH_Initialize || !g_pfnMH_Uninitialize || !g_pfnMH_CreateHook || 
        !g_pfnMH_EnableHook || !g_pfnMH_DisableHook) {
        MemoryFreeLibrary(g_hMinHookModule);
        g_hMinHookModule = nullptr;
        return FALSE;
    }
    
    return TRUE;
}

VOID UnloadMinHookFromMemory() {
    if (g_hMinHookModule) {
        MemoryFreeLibrary(g_hMinHookModule);
        g_hMinHookModule = nullptr;
        
        g_pfnMH_Initialize = nullptr;
        g_pfnMH_Uninitialize = nullptr;
        g_pfnMH_CreateHook = nullptr;
        g_pfnMH_EnableHook = nullptr;
        g_pfnMH_DisableHook = nullptr;
    }
}

}  // namespace

// ============================================================================
// 道具功能实现
// ============================================================================

/**
 * @brief 请求背包数据
 * @param packType 包类型 (0xFFFFFFFF = 全部)
 * @return 发送是否成功
 */
BOOL SendReqPackageDataPacket(uint32_t packType) {
    // 封包格式: Opcode=1183761, Params=packType, Body=空
    std::vector<uint8_t> packet = PacketBuilder()
        .SetOpcode(Opcode::REQ_PACKAGE_DATA_SEND)
        .SetParams(packType)
        .Build();
    
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

/**
 * @brief 处理背包数据响应
 * @param packet 封包数据
 */
void ProcessPackageDataResponse(const GamePacket& packet) {
    if (packet.body.size() < 8) {
        UIBridge::Instance().UpdateHelperText(L"背包数据响应格式错误");
        return;
    }
    
    const BYTE* body = packet.body.data();
    size_t offset = 0;
    size_t bodySize = packet.body.size();
    
    // 读取 isocode（占位字段，仅需推进 offset）
    (void)ReadInt32LE(body, offset);
    
    // 读取 packcount（包数量）
    int packcount = ReadInt32LE(body, offset);
    
    // 清空之前的物品映射
    g_itemPositionMap.clear();
    g_packItems.clear();
    
    // 遍历每个包
    for (int i = 0; i < packcount; i++) {
        // 检查是否有足够的空间读取 packcode, packid, count (共12字节)
        if (offset + 12 > bodySize) break;
        
        int packcode = ReadInt32LE(body, offset);
        (void)ReadInt32LE(body, offset);  // packid：占位字段，仅需推进 offset
        int count = ReadInt32LE(body, offset);
        
        // 遍历包内物品
        for (int j = 0; j < count; j++) {
            // 检查是否有足够的空间读取 position 和 id (共8字节)
            if (offset + 8 > bodySize) break;
            
            PackItemInfo item;
            item.packcode = packcode;
            item.position = ReadInt32LE(body, offset);
            item.id = ReadInt32LE(body, offset);
            
            if (item.id > 0) {
                // 检查是否有足够的空间读取 count (4字节)
                if (offset + 4 > bodySize) break;
                
                item.count = ReadInt32LE(body, offset);
                
                // 保存到列表
                g_packItems.push_back(item);
                
                // 建立ID -> position映射
                g_itemPositionMap[item.id] = item.position;
            }
        }
    }
    
    // 构建道具数量JSON并发送到JS
    std::wstring itemCountsJson = L"{";
    bool first = true;
    for (const auto& item : g_packItems) {
        if (!first) itemCountsJson += L",";
        first = false;
        itemCountsJson += L"\"" + std::to_wstring(item.id) + L"\":" + std::to_wstring(item.count);
    }
    itemCountsJson += L"}";
    
    std::wstring script = L"if(window.updateItemCounts) { window.updateItemCounts(" + itemCountsJson + L"); }";
    UIBridge::Instance().ExecuteJS(script);
    
    // 更新UI
    wchar_t msg[128];
    swprintf_s(msg, L"背包数据已更新，共 %zu 个物品", g_packItems.size());
    UIBridge::Instance().UpdateHelperText(msg);
}

/**
 * @brief 购买道具
 * @param itemId 道具ID
 * @param count 购买数量
 * @return 发送是否成功
 * @note 封包格式: Opcode=1183760, Params=itemId, Body=[count]
 */
BOOL SendBuyGoodsPacket(uint32_t itemId, uint32_t count) {
    // 封包格式: Opcode=1183760, Params=itemId, Body=[count]
    std::vector<uint8_t> packet = PacketBuilder()
        .SetOpcode(Opcode::BUY_GOODS_SEND)
        .SetParams(itemId)
        .WriteUInt32(count)
        .Build();
    
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

/**
 * @brief 使用道具（战斗中）
 * @param itemId 道具ID
 * @param position 物品位置索引（如果为0则从映射表查找）
 * @return 发送是否成功
 * @note 封包格式: Opcode=1186050, Params=2, Body=[packcode=1, position, sid=1]
 */
BOOL SendUseItemInBattlePacket(uint32_t itemId, uint32_t position) {
    // 如果未指定位置，从映射表中查找
    if (position == 0) {
        auto it = g_itemPositionMap.find(itemId);
        if (it == g_itemPositionMap.end()) {
            return FALSE;
        }
        position = it->second;
    }
    
    // 封包格式: Opcode=1186050, Params=2, Body=[packcode, position, sid]
    // packcode = 1 (道具背包), sid = 1 (玩家角色)
    std::vector<uint8_t> packet = PacketBuilder()
        .SetOpcode(Opcode::USER_OP_SEND)
        .SetParams(2)
        .WriteUInt32(1)        // packcode = 1
        .WriteUInt32(position) // position
        .WriteUInt32(1)        // sid = 1
        .Build();
    
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

/**
 * @brief 使用道具（通用，非战斗）
 * @param itemId 道具ID
 * @param spiritId 妖怪ID
 * @param param1 参数1
 * @param param2 参数2
 * @param param3 参数3
 * @return 发送是否成功
 * @note 封包格式: Opcode=1184310, Params=itemId, Body=[spiritId, param1, param2, param3]
 */
BOOL SendUsePropsPacket(uint32_t itemId, uint32_t spiritId, 
                        uint32_t param1, uint32_t param2, uint32_t param3) {
    // 封包格式: Opcode=1184310, Params=itemId, Body=[spiritId, param1, param2, param3]
    std::vector<uint8_t> packet = PacketBuilder()
        .SetOpcode(Opcode::USE_PROPS_SEND)
        .SetParams(itemId)
        .WriteUInt32(spiritId)
        .WriteUInt32(param1)
        .WriteUInt32(param2)
        .WriteUInt32(param3)
        .Build();
    
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

/**
 * @brief 获取道具名称
 * @param itemId 道具ID
 * @return 道具名称
 */
/**
 * @brief 获取物品位置
 * @param itemId 物品ID
 * @return 物品位置索引
 */
uint32_t GetItemPosition(uint32_t itemId) {
    auto it = g_itemPositionMap.find(itemId);
    if (it != g_itemPositionMap.end()) {
        return it->second;
    }
    return 0;
}

// ============================================================================
// 封包回调
// ============================================================================

void SetPacketCallback(PACKET_CALLBACK callback) {
    g_PacketCallback = callback;
}

// ============================================================================
// 十六进制转换工具
// ============================================================================

// ============================================================================
// UI 同步函数
// ============================================================================

void SyncPacketsToUI() {
    // 拷贝数据，减少锁持有时间
    std::vector<PACKET> packetsCopy;
    {
        CriticalSectionLock lock(g_packetListCS.Get());
        if (!g_hWnd || g_PacketList.empty()) {
            return;
        }
        packetsCopy = g_PacketList;
    }
    
    // 检查窗口句柄有效性
    if (!IsWindow(g_hWnd)) {
        return;
    }
    
    // 清空UI列表项
    {
        std::wstring jsClear = 
            L"(function(){"
            L" var pList=document.getElementById('packet-list');"
            L" if(pList){"
            L"   var pListItems=document.getElementById('packet-list-items');"
            L"   if(pListItems){ pListItems.innerHTML=''; }"
            L" }"
            L"})();";
        
        UIBridge::Instance().ExecuteJS(jsClear);
    }
    
    // 批量同步封包
    for (size_t i = 0; i < packetsCopy.size(); ++i) {
        const auto& packet = packetsCopy[i];
        if (!packet.pData) continue;

        std::string hexStr = HexToString(packet.pData, packet.dwSize);
        std::wstring wideHexStr = Utf8ToWide(hexStr);
        std::wstring direction = packet.bSend ? L"发送包" : L"接收包";
        
        // 获取封包标签
        std::string labelUtf8 = "";
        if (packet.dwSize >= PacketProtocol::HEADER_SIZE) {
            size_t offset = 0;
            uint16_t magic = ReadUInt16LE(packet.pData, offset);  // Magic: offset 0-1
            (void)ReadUInt16LE(packet.pData, offset);             // Length: offset 2-3
            if (magic == PacketProtocol::MAGIC_NORMAL || magic == PacketProtocol::MAGIC_COMPRESSED) {
                uint32_t opcode = ReadUInt32LE(packet.pData, offset);  // Opcode: offset 4-7
                labelUtf8 = GetPacketLabel(opcode, packet.bSend);
            }
        }
        std::wstring wideLabel = Utf8ToWide(labelUtf8);
        
        // 创建时间戳
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t timeStr[64];
        swprintf_s(timeStr, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
        
        std::wstring jsCode = 
            L"if(window.addPacketToList) { window.addPacketToList(" 
            + std::to_wstring(i + 1) + L", '" + direction + L"', '" 
            + wideHexStr + L"', '" + timeStr + L"', '" + wideLabel + L"'); }";
        
        UIBridge::Instance().ExecuteJS(jsCode);
    }
    
    // 在最后统一更新一次计数
    std::wstring jsUpdateCount = 
        L"if(window.updatePacketCount) { window.updatePacketCount(" 
        + std::to_wstring(packetsCopy.size()) + L"); }";
    
    UIBridge::Instance().ExecuteJS(jsUpdateCount);
}

void AddPacketToUI(BOOL bSend, const BYTE* pData, DWORD dwSize, DWORD dwTime) {
    (void)bSend;
    (void)dwTime;
    if (!g_hWnd || !pData || dwSize == 0) return;
    if (!g_bInterceptEnabled) return;

    size_t packetIndex;
    {
        CriticalSectionLock lock(g_packetListCS.Get());
        packetIndex = g_PacketList.size();  // 封包已添加，直接使用size()，不加1
    }

    // 如果窗口可见，同步整个列表到UI
    if (AppHost::IsPacketWindowVisible()) {
        SyncPacketsToUI();
    } else {
        // 窗口关闭时，只更新计数
        std::wstring jsCountCode = 
            L"if(window.updatePacketCount) { window.updatePacketCount(" 
            + std::to_wstring(packetIndex) + L"); }";
        
        UIBridge::Instance().ExecuteJS(jsCountCode);
    }
}

// ============================================================================
// Hook 函数
// ============================================================================

namespace {

thread_local bool g_sendPacketRequiresComplete = false;

int SendAllOriginal(SOCKET socket, const char* data, int length, int flags) {
    if (!OriginalSend || !data || length < 0) {
        WSASetLastError(WSAEINVAL);
        return SOCKET_ERROR;
    }

    int sent = 0;
    while (sent < length) {
        const int result = OriginalSend(socket, data + sent, length - sent, flags);
        if (result == SOCKET_ERROR) {
            return sent > 0 ? sent : SOCKET_ERROR;
        }
        if (result == 0) {
            return sent;
        }
        sent += result;
    }
    return sent;
}

struct SendPacketCompleteGuard {
    bool previous = g_sendPacketRequiresComplete;
    SendPacketCompleteGuard() { g_sendPacketRequiresComplete = true; }
    ~SendPacketCompleteGuard() { g_sendPacketRequiresComplete = previous; }
};

}  // namespace

int WINAPI HookedSend(SOCKET s, const char* buf, int len, int flags) {
    // 总是记录游戏套接字
    if (len > 0) {
        const BYTE* pData = reinterpret_cast<const BYTE*>(buf);
        if (IsGamePacket(pData, static_cast<DWORD>(len))) {
            g_LastGameSocket = s;
        }
    }
    
    // 劫持检测和修改
    if (g_bHijackEnabled && len > 0) {
        const BYTE* pData = reinterpret_cast<const BYTE*>(buf);
        DWORD dwSize = static_cast<DWORD>(len);
        std::vector<BYTE> modifiedData;
        
        if (ProcessHijack(true, pData, &dwSize, &modifiedData)) {
            // 劫持成功：拦截或替换封包
            if (dwSize == 0) {
                // 拦截：返回已发送的假象
                return len;
            } else if (!modifiedData.empty()) {
                // 替换：发送修改后的封包
                const char* sendData = reinterpret_cast<const char*>(modifiedData.data());
                const int sendLength = static_cast<int>(dwSize);
                int result = g_sendPacketRequiresComplete
                    ? SendAllOriginal(s, sendData, sendLength, flags)
                    : OriginalSend(s, sendData, sendLength, flags);
                // 记录修改后的封包
                if (g_bInterceptEnabled && g_bInterceptSend && g_bInitialized) {
                    PACKET packet;
                    packet.dwSize = dwSize;
                    packet.bSend = TRUE;
                    packet.pData = new BYTE[dwSize];
                    memcpy(packet.pData, modifiedData.data(), dwSize);
                    packet.dwTime = GetTickCount();

                    {
                        CriticalSectionLock lock(g_packetListCS.Get());
                        g_PacketList.push_back(packet);
                    }
                    // 锁外调用 UI 更新，避免死锁
                    AddPacketToUI(TRUE, packet.pData, packet.dwSize, packet.dwTime);

                    if (g_PacketCallback) {
                        g_PacketCallback(TRUE, modifiedData.data(), dwSize);
                    }
                }
                return result;
            }
        }
    }
    
    // ========================================================================
    // 战斗封包处理
    // Counter 在收到 BATTLE_START 响应后计算并存储在全局变量中
    // ========================================================================
    
    // 检查是否需要阻止发送
    if (len > 0 && ShouldBlockPacketSend(reinterpret_cast<const BYTE*>(buf), static_cast<DWORD>(len))) {
        return len;  // 欺骗调用者认为已发送
    }

    // 进入场景不会重置战斗 counter；AS3 BattleControl.counter 会跨场景保留。
    if (len >= static_cast<int>(PacketProtocol::HEADER_SIZE)) {
        const BYTE* pData = reinterpret_cast<const BYTE*>(buf);
        if (IsGamePacket(pData, static_cast<DWORD>(len))) {
            uint32_t opcode = ReadOpcode(pData);
            // 检测账号验证封包，提取登录 key
            if (opcode == Opcode::CHECK_ACCOUNT_SEND) {
                ExtractLoginKeyFromPacket(pData, static_cast<DWORD>(len));
            }
            if (opcode == Opcode::BATTLE_PLAY_OVER && g_battleSixAuto.IsInBattle()) {
                g_battleSixPlayOverToken.fetch_add(1);
            }
        }
    }
    // 拦截并记录封包
    if (g_bInterceptEnabled && g_bInterceptSend && g_bInitialized && len > 0) {
        const BYTE* pData = reinterpret_cast<const BYTE*>(buf);
        DWORD dwSize = static_cast<DWORD>(len);

        if (IsGamePacket(pData, dwSize)) {
            PACKET packet;
            packet.dwSize = dwSize;
            packet.bSend = TRUE;
            packet.pData = new BYTE[dwSize];
            memcpy(packet.pData, pData, dwSize);
            packet.dwTime = GetTickCount();

            {
                CriticalSectionLock lock(g_packetListCS.Get());
                g_PacketList.push_back(packet);
            }
            // 锁外调用 UI 更新，避免死锁
            AddPacketToUI(TRUE, packet.pData, packet.dwSize, packet.dwTime);

            if (g_PacketCallback) {
                g_PacketCallback(TRUE, pData, dwSize);
            }
        }
    }
    
    return g_sendPacketRequiresComplete
        ? SendAllOriginal(s, buf, len, flags)
        : OriginalSend(s, buf, len, flags);
}

// ============================================================================
// 接收封包处理器（拆分自 HookedRecv）
// ============================================================================



namespace {

/**
 * @brief 处理进入世界封包
 */
void ProcessEnterWorldPacket(const GamePacket& gp) {
    const auto& body = gp.body;
    if (body.size() < 4) return;
    
    size_t offset = 0;
    
    // 跳过 timeleft, isNewHand, coin, x_coin (4 * 4 = 16字节)
    offset += 16;
    
    // 跳过 UTF 字符串
    if (offset + 2 <= body.size()) {
        uint16_t strLen = static_cast<uint16_t>(body[offset]) | 
                          (static_cast<uint16_t>(body[offset + 1]) << 8);
        offset += 2 + strLen;
    }
    
    // 跳过 5 个 int (20字节)
    offset += 20;
    
    // 读取卡布号
    if (offset + 4 > body.size()) return;
    
    uint32_t kabuId = static_cast<uint32_t>(body[offset]) |
                      (static_cast<uint32_t>(body[offset + 1]) << 8) |
                      (static_cast<uint32_t>(body[offset + 2]) << 16) |
                      (static_cast<uint32_t>(body[offset + 3]) << 24);
    offset += 4;
    
    g_userId = kabuId;
    ResetEightTrigramsSessionState();
    
    g_battleCounter = 1;
    // 读取卡布名
    if (offset + 2 > body.size()) return;
    
    uint16_t nameLen = static_cast<uint16_t>(body[offset]) | 
                       (static_cast<uint16_t>(body[offset + 1]) << 8);
    offset += 2;
    
    if (offset + nameLen > body.size()) return;
    
    std::string nameUtf8(reinterpret_cast<const char*>(&body[offset]), nameLen);
    std::wstring kabuName = Utf8ToWide(nameUtf8);
    
    // 更新窗口标题
    std::wstring newTitle = std::wstring(WpeHook::APP_WINDOW_TITLE_W) + L" - " + 
                           std::to_wstring(kabuId) + L" " + kabuName;
    SetWindowTextW(g_hWnd, newTitle.c_str());
}

/**
 * @brief 处理分解响应后的自动查询
 */
void HandleDecomposeResponse() {
    static DWORD g_lastDecomposeTime = 0;
    DWORD currentTime = GetTickCount();
    
    if (currentTime - g_lastDecomposeTime >= 300) {
        g_lastDecomposeTime = currentTime;
        HANDLE hThread = CreateThread(NULL, 0, [](LPVOID) -> DWORD {
            Sleep(300);
            SendQueryBagPacket();
            return 0;
        }, NULL, 0, NULL);
        if (hThread) {
            CloseHandle(hThread); // 关闭线程句柄，避免资源泄漏
        }
    }
}

/**
 * @brief 处理接收到的游戏封包
 */
void ProcessReceivedGamePackets(const BYTE* pData, DWORD dwSize,
                                const std::vector<GamePacket>& gamePackets) {
    (void)pData;
    (void)dwSize;
    auto& dispatcher = ResponseDispatcher::Instance();

    for (const auto& gp : gamePackets) {
        dispatcher.Dispatch(gp);
        Sleep(0);
    }
}

static void ReceiveDispatchWorkerProc() {
    for (;;) {
        std::vector<GamePacket> packets;
        {
            std::unique_lock<std::mutex> lock(g_receiveDispatchMutex);
            g_receiveDispatchCv.wait(lock, [] {
                return g_receiveDispatchStop || !g_receiveDispatchQueue.empty();
            });

            if (g_receiveDispatchStop && g_receiveDispatchQueue.empty()) {
                return;
            }

            packets = std::move(g_receiveDispatchQueue.front());
            g_receiveDispatchQueue.pop_front();
        }

        ProcessReceivedGamePackets(nullptr, 0, packets);
    }
}

static void StartReceiveDispatchWorker() {
    std::lock_guard<std::mutex> lock(g_receiveDispatchMutex);
    if (!g_receiveDispatchAccepting.load() || g_receiveDispatchStarted) {
        return;
    }

    g_receiveDispatchStop = false;
    try {
        g_receiveDispatchWorker = std::thread(ReceiveDispatchWorkerProc);
        g_receiveDispatchStarted = true;
    } catch (...) {
        g_receiveDispatchStarted = false;
    }
}

static void StopReceiveDispatchWorker() {
    {
        std::lock_guard<std::mutex> lock(g_receiveDispatchMutex);
        if (!g_receiveDispatchStarted) {
            return;
        }
        g_receiveDispatchStop = true;
    }
    g_receiveDispatchCv.notify_all();

    if (g_receiveDispatchWorker.joinable()) {
        g_receiveDispatchWorker.join();
    }

    std::lock_guard<std::mutex> lock(g_receiveDispatchMutex);
    g_receiveDispatchQueue.clear();
    g_receiveDispatchStarted = false;
    g_receiveDispatchStop = false;
}

void DispatchReceivedGamePacketsAsync(const std::vector<GamePacket>& gamePackets) {
    if (gamePackets.empty() || !g_receiveDispatchAccepting.load()) {
        return;
    }

    // Notify while still in the recv hook.  The business worker may be busy
    // in another handler, but protocol waiters must not wait behind it.
    for (const auto& gp : gamePackets) {
        ResponseWaiter::NotifyResponse(gp.opcode, gp.params);
    }

    StartReceiveDispatchWorker();
    {
        std::lock_guard<std::mutex> lock(g_receiveDispatchMutex);
        if (!g_receiveDispatchAccepting.load() || !g_receiveDispatchStarted) {
            return;
        }

        // Bound memory even if a handler is slower than the network.  The
        // newest packets are more useful for current state than stale ones.
        if (g_receiveDispatchQueue.size() >= kMaxReceiveDispatchQueue) {
            g_receiveDispatchQueue.pop_front();
        }
        g_receiveDispatchQueue.emplace_back(gamePackets);
    }
    g_receiveDispatchCv.notify_one();
}

std::vector<BYTE> BuildReceivedPacketBytes(const GamePacket& packet) {
    const std::vector<uint8_t>& bodyBytes = packet.rawBody.empty() ? packet.body : packet.rawBody;
    std::vector<BYTE> packetData(PacketProtocol::HEADER_SIZE + bodyBytes.size());

    size_t offset = 0;
    packetData[offset++] = static_cast<BYTE>(packet.magic & 0xFF);
    packetData[offset++] = static_cast<BYTE>((packet.magic >> 8) & 0xFF);

    const uint16_t bodyLen = static_cast<uint16_t>(bodyBytes.size());
    packetData[offset++] = static_cast<BYTE>(bodyLen & 0xFF);
    packetData[offset++] = static_cast<BYTE>((bodyLen >> 8) & 0xFF);

    packetData[offset++] = static_cast<BYTE>(packet.opcode & 0xFF);
    packetData[offset++] = static_cast<BYTE>((packet.opcode >> 8) & 0xFF);
    packetData[offset++] = static_cast<BYTE>((packet.opcode >> 16) & 0xFF);
    packetData[offset++] = static_cast<BYTE>((packet.opcode >> 24) & 0xFF);

    packetData[offset++] = static_cast<BYTE>(packet.params & 0xFF);
    packetData[offset++] = static_cast<BYTE>((packet.params >> 8) & 0xFF);
    packetData[offset++] = static_cast<BYTE>((packet.params >> 16) & 0xFF);
    packetData[offset++] = static_cast<BYTE>((packet.params >> 24) & 0xFF);

    if (!bodyBytes.empty()) {
        memcpy(packetData.data() + offset, bodyBytes.data(), bodyBytes.size());
    }

    return packetData;
}

}  // anonymous namespace

// ============================================================================
// 登录 Key 提取函数实现
// ============================================================================

// ============================================================================
// Hook 函数实现
// ============================================================================

int WINAPI HookedRecv(SOCKET s, char* buf, int len, int flags) {
    int result = OriginalRecv(s, buf, len, flags);

    if (result <= 0 || !g_bInitialized) {
        return result;
    }

    const BYTE* pData = reinterpret_cast<const BYTE*>(buf);
    DWORD dwSize = static_cast<DWORD>(result);

    // ========================================================================
    // 第一步：解析黏包，得到完整的封包列表
    // ========================================================================
    std::vector<GamePacket> gamePackets;
    bool canRewriteRecvData = false;
    bool hasValidPackets = PacketParser::ParsePackets(
        pData, dwSize, FALSE, gamePackets, &canRewriteRecvData);
    
    // ========================================================================
    // 第二步：封包级别过滤（支持黏包）
    // ========================================================================
    // 记录需要过滤的封包索引
    std::vector<size_t> packetsToFilter;
    bool hasBattleStart = false;
    bool shouldFilterBattleStart = false;
    
    for (size_t i = 0; i < gamePackets.size(); i++) {
        const auto& gp = gamePackets[i];
        const auto& packetBody = gp.body;
        
        // 0. 默认屏蔽检查（无条件屏蔽，无需用户勾选）
        if (IsDefaultBlockedPacket(gp.opcode, gp.params,
                                   packetBody.empty() ? nullptr : packetBody.data(),
                                   packetBody.size())) {
            packetsToFilter.push_back(i);
            continue;  // 跳过后续处理
        }
        
        // 1. 战斗开始响应 - 始终计算并存储 counter
        if (gp.opcode == Opcode::BATTLE_START) {
            hasBattleStart = true;
            
            // 读取战斗类型
            // AS3 stores battleType in the packet params; body[0] is the first entity state.
            int32_t battleType = static_cast<int32_t>(gp.params);

            // 计算 counter
            uint32_t currentCounter = g_battleCounter.load();
            if (currentCounter == 0) {
                currentCounter = 1;
            }

            bool isSpecialBattle = (battleType == 3 || battleType == 5 || battleType == 6 ||
                                   battleType == 14 || battleType == 15 ||
                                   battleType == 17 || battleType == 18 ||
                                   battleType == 19 || battleType == 20 ||
                                   battleType == 21 || battleType == 22);

            if (isSpecialBattle) {
                g_battleCounter = 1;
            } else {
                uint32_t newCounter = currentCounter + (g_userId.load() & 65535) + 9;
                g_battleCounter = newCounter & 65535;
            }


            // 屏蔽战斗功能
            if (g_blockBattle.load()) {
                shouldFilterBattleStart = true;
                packetsToFilter.push_back(i);
            }
        }

        // Settlement packets are parsed by PacketParser.  The common
        // BATTLE_END lifecycle is the only place that sends the end-confirmation,
        // matching BattleControl.clearBattleView() in AS3.
        
        // 2. MD5 图片验证自动回复（不屏蔽，只自动回复）
        if (gp.opcode == Opcode::BATTLE_MD5_CHECK) {
            // 复制body和params用于异步处理
            struct MD5ThreadData {
                std::vector<BYTE> body;
                uint32_t params;
            };
            MD5ThreadData* threadData = new MD5ThreadData{ gp.body, gp.params };
            
            // 异步处理验证（需要下载图片，耗时操作）
            CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
                MD5ThreadData* data = (MD5ThreadData*)lpParam;
                Sleep(100);  // 延迟100ms
                ProcessMD5CheckAndReply(data->body, data->params);
                delete data;
                return 0;
            }, threadData, 0, nullptr);
        }

        // 3. 自动回血功能
        if (g_autoHeal && gp.opcode == Opcode::BATTLE_END && gp.params == 1) {
            CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
                (void)lpParam;
                Sleep(100);
                SendBeibeiHealPacket();
                return 0;
            }, nullptr, 0, nullptr);
        }

        // 4. 劫持检测和修改（接收包）
        if (g_bHijackEnabled) {
            std::vector<BYTE> packetData = BuildReceivedPacketBytes(gp);
            
            std::vector<BYTE> modifiedData;
            DWORD modifiedSize = static_cast<DWORD>(packetData.size());

            if (ProcessHijack(false, packetData.data(), &modifiedSize, &modifiedData)) {
                // TODO: 实现封包替换功能
            }
        }
    }

    // ========================================================================
    // 第三步：重构封包缓冲区（过滤掉需要屏蔽的封包）
    // ========================================================================
    if (!packetsToFilter.empty() && hasValidPackets && canRewriteRecvData) {
        // 创建新的封包缓冲区
        std::vector<BYTE> newBuffer;
        newBuffer.reserve(dwSize);
        
        for (size_t i = 0; i < gamePackets.size(); i++) {
            // 检查是否需要过滤此封包
            bool shouldFilter = false;
            for (size_t filterIdx : packetsToFilter) {
                if (filterIdx == i) {
                    shouldFilter = true;
                    break;
                }
            }
            
            if (!shouldFilter) {
                const auto packetData = BuildReceivedPacketBytes(gamePackets[i]);
                newBuffer.insert(newBuffer.end(), packetData.begin(), packetData.end());
            }
        }
        
        // Never return 0 for a successful recv: that is EOF to the game. If every
        // complete packet was selected for filtering, preserve the original bytes
        // and keep the connection semantics intact.
        if (!newBuffer.empty()) {
            // 复制新缓冲区到原始缓冲区
            size_t newSize = newBuffer.size();
            if (newSize <= result) {
                memset(buf, 0, result);
                memcpy(buf, newBuffer.data(), newSize);
                // 更新返回值为新大小
                result = static_cast<int>(newSize);
            }
        }
    }

    // ========================================================================
    // 第四步：处理解析后的封包（通知等待器、战斗处理等）
    // ========================================================================
    
    // 额外的 ENTER_SCENE_BACK 检测（即使 ParsePackets 失败也检测）
    if (dwSize >= PacketProtocol::HEADER_SIZE) {
        uint32_t opcode = pData[4] | (pData[5] << 8) | (pData[6] << 16) | (pData[7] << 24);
        uint16_t magic = pData[0] | (pData[1] << 8);
        
        // 直接检测 ENTER_SCENE_BACK 响应
        if (opcode == Opcode::ENTER_SCENE_BACK && 
            (magic == PacketProtocol::MAGIC_NORMAL || magic == PacketProtocol::MAGIC_COMPRESSED)) {
            CriticalSectionLock lock(g_towerCS);
            g_towerMapEntered = true;
            g_taskZoneMapEntered = true;
        }
    }
    

    if (hasValidPackets) {
        DispatchReceivedGamePacketsAsync(gamePackets);
    }

    // ========================================================================
    // 第五步：显示封包（仅当启用接收拦截时）
    // ========================================================================
    if (g_bInterceptEnabled && g_bInterceptRecv && !gamePackets.empty()) {
        for (const auto& gp : gamePackets) {
            std::vector<BYTE> packetData = BuildReceivedPacketBytes(gp);
            
            // 添加到封包列表
            PACKET packet;
            packet.dwSize = static_cast<DWORD>(packetData.size());
            packet.bSend = FALSE;
            packet.pData = new BYTE[packetData.size()];
            memcpy(packet.pData, packetData.data(), packetData.size());
            packet.dwTime = GetTickCount();

            {
                CriticalSectionLock lock(g_packetListCS.Get());
                g_PacketList.push_back(packet);
            }
            // 锁外调用 UI 更新，避免死锁
            AddPacketToUI(FALSE, packet.pData, packet.dwSize, packet.dwTime);

            // 调用回调函数
            if (g_PacketCallback) {
                g_PacketCallback(FALSE, packetData.data(), static_cast<DWORD>(packetData.size()));
            }
        }
    }

    return result;
}

// ============================================================================
// Hook 初始化
// ============================================================================

BOOL InitializeHooks() {
    // 初始化 ResponseWaiter
    ResponseWaiter::Initialize();

    if (!LoadMinHookFromMemory()) {
        return FALSE;
    }
    
    MH_STATUS status = g_pfnMH_Initialize();
    if (status != MH_OK) {
        UnloadMinHookFromMemory();
        return FALSE;
    }
    
    // Hook ws2_32.dll (socket)
    HMODULE hWs2Module = LoadLibraryA("ws2_32.dll");
    if (!hWs2Module) {
        g_pfnMH_Uninitialize();
        UnloadMinHookFromMemory();
        return FALSE;
    }
    
    LPVOID pSendAddr = (LPVOID)GetProcAddress(hWs2Module, "send");
    LPVOID pRecvAddr = (LPVOID)GetProcAddress(hWs2Module, "recv");
    
    if (!pSendAddr || !pRecvAddr) {
        g_pfnMH_Uninitialize();
        UnloadMinHookFromMemory();
        return FALSE;
    }
    
    if (g_pfnMH_CreateHook(pSendAddr, HookedSend, (LPVOID*)&OriginalSend) != MH_OK ||
        g_pfnMH_CreateHook(pRecvAddr, HookedRecv, (LPVOID*)&OriginalRecv) != MH_OK) {
        g_pfnMH_Uninitialize();
        UnloadMinHookFromMemory();
        return FALSE;
    }

    g_receiveDispatchAccepting.store(true);
    status = g_pfnMH_EnableHook(MH_ALL_HOOKS);
    if (status != MH_OK) {
        g_receiveDispatchAccepting.store(false);
        g_pfnMH_Uninitialize();
        UnloadMinHookFromMemory();
        return FALSE;
    }
    
    return TRUE;
}

VOID UninitializeHooks() {
    g_receiveDispatchAccepting.store(false);

    if (g_hMinHookModule) {
        g_pfnMH_DisableHook(MH_ALL_HOOKS);
    }

    ResponseWaiter::CancelWait();
    StopReceiveDispatchWorker();

    if (g_hMinHookModule) {
        g_pfnMH_Uninitialize();
        UnloadMinHookFromMemory();
    }

    // 清理 ResponseWaiter
    ResponseWaiter::Cleanup();
}

// ============================================================================
// 拦截控制
// ============================================================================

VOID StartIntercept() {
    g_bInterceptEnabled = true;
}

VOID StopIntercept() {
    g_bInterceptEnabled = false;
}

VOID SetInterceptType(BOOL bSend, BOOL bRecv) {
    g_bInterceptSend = bSend;
    g_bInterceptRecv = bRecv;
}

// ============================================================================
// 响应等待器实现
// ============================================================================

namespace {

struct ResponseRecord {
    uint64_t serial;
    uint32_t opcode;
    uint32_t params;
};

std::deque<ResponseRecord> g_responseQueue;

}  // namespace

CRITICAL_SECTION ResponseWaiter::s_cs;
CONDITION_VARIABLE ResponseWaiter::s_cv;
uint64_t ResponseWaiter::s_responseSerial = 0;
std::atomic<long> ResponseWaiter::s_waitingCount{0};  // 等待线程计数（真正的原子操作）
std::atomic<uint64_t> ResponseWaiter::s_cancelSerial{0};

void ResponseWaiter::Initialize() {
    InitializeCriticalSection(&s_cs);
    InitializeConditionVariable(&s_cv);
    s_waitingCount = 0;
    s_responseSerial = 0;
    s_cancelSerial = 0;
    g_responseQueue.clear();
}

void ResponseWaiter::Cleanup() {
    CancelWait();

    const DWORD startTick = GetTickCount();
    while (s_waitingCount.load() > 0) {
        if (GetTickCount() - startTick > (WpeHook::TIMEOUT_SEND + WpeHook::TIMEOUT_RESPONSE + 1000)) {
            return;
        }
        Sleep(10);
    }

    EnterCriticalSection(&s_cs);
    g_responseQueue.clear();
    LeaveCriticalSection(&s_cs);
    DeleteCriticalSection(&s_cs);
}

uint64_t ResponseWaiter::BeginWait() {
    EnterCriticalSection(&s_cs);

    const long previousWaiting = s_waitingCount.fetch_add(1);
    if (previousWaiting == 0) {
        g_responseQueue.clear();
    }

    const uint64_t serial = s_responseSerial;
    LeaveCriticalSection(&s_cs);
    return serial;
}

void ResponseWaiter::EndWait() {
    EnterCriticalSection(&s_cs);

    const long remainingWaiting = s_waitingCount.fetch_sub(1) - 1;
    if (remainingWaiting <= 0) {
        g_responseQueue.clear();
    }

    LeaveCriticalSection(&s_cs);
}

bool ResponseWaiter::WaitForResponse(
    uint32_t expectedOpcode,
    DWORD timeoutMs,
    uint64_t baselineSerial,
    uint32_t expectedParams,
    bool matchExpectedParams,
    bool preRegistered) {
    if (expectedOpcode == 0 || timeoutMs == 0) {
        return true;  // 不等待，直接返回
    }

    bool ownsRegistration = false;
    if (!preRegistered) {
        BeginWait();
        ownsRegistration = true;
    }

    EnterCriticalSection(&s_cs);
    const uint64_t cancelSerial = s_cancelSerial.load();

    const DWORD startTick = GetTickCount();
    bool received = false;

    while (true) {
        if (cancelSerial != s_cancelSerial.load()) {
            break;
        }

        auto matched = std::find_if(
            g_responseQueue.begin(),
            g_responseQueue.end(),
            [&](const ResponseRecord& record) {
                if (record.serial <= baselineSerial) {
                    return false;
                }

                if (record.opcode != expectedOpcode) {
                    return false;
                }

                if (matchExpectedParams && record.params != expectedParams) {
                    return false;
                }

                return true;
            });

        if (matched != g_responseQueue.end()) {
            received = true;
            g_responseQueue.erase(matched);
            break;
        }

        const DWORD elapsed = GetTickCount() - startTick;
        if (elapsed >= timeoutMs) {
            break;
        }

        SleepConditionVariableCS(
            &s_cv,
            &s_cs,
            timeoutMs - elapsed
        );
    }

    LeaveCriticalSection(&s_cs);

    if (ownsRegistration) {
        EndWait();
    }

    return received;
}

void ResponseWaiter::NotifyResponse(uint32_t opcode, uint32_t params) {
    EnterCriticalSection(&s_cs);
    ++s_responseSerial;
    if (s_waitingCount.load() > 0) {
        g_responseQueue.push_back(ResponseRecord{s_responseSerial, opcode, params});
    }
    LeaveCriticalSection(&s_cs);
    WakeAllConditionVariable(&s_cv);
}

void ResponseWaiter::CancelWait() {
    EnterCriticalSection(&s_cs);
    s_cancelSerial.fetch_add(1);
    LeaveCriticalSection(&s_cs);
    WakeAllConditionVariable(&s_cv);
}



// ============================================================================

// ResponseDispatcher 实现

// ============================================================================



ResponseDispatcher& ResponseDispatcher::Instance() {
    static ResponseDispatcher instance;
    return instance;
}

uint64_t ResponseDispatcher::MakeKey(uint32_t opcode, uint32_t params) {
    return (static_cast<uint64_t>(opcode) << 32) | static_cast<uint64_t>(params);
}

BOOL ResponseDispatcher::Register(uint32_t opcode, ResponseHandler handler) {
    if (!handler) return FALSE;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_opcodeOnlyHandlers[opcode] = handler;
    return TRUE;
}

BOOL ResponseDispatcher::Register(uint32_t opcode, uint32_t params, ResponseHandler handler) {
    if (!handler) return FALSE;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_handlers[MakeKey(opcode, params)] = handler;
    return TRUE;
}

void ResponseDispatcher::Unregister(uint32_t opcode, uint32_t params) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (params != 0xFFFFFFFF) {
        m_handlers.erase(MakeKey(opcode, params));
        return;
    }

    m_opcodeOnlyHandlers.erase(opcode);
    for (auto it = m_handlers.begin(); it != m_handlers.end();) {
        if (static_cast<uint32_t>(it->first >> 32) == opcode) {
            it = m_handlers.erase(it);
        } else {
            ++it;
        }
    }
}

BOOL ResponseDispatcher::Dispatch(const GamePacket& packet) {
    // 锁内只取handler指针，锁外再执行：handler可能耗时甚至回调注册接口。
    ResponseHandler handler = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto exact = m_handlers.find(MakeKey(packet.opcode, packet.params));
        if (exact != m_handlers.end()) {
            handler = exact->second;
        } else {
            auto byOpcode = m_opcodeOnlyHandlers.find(packet.opcode);
            if (byOpcode != m_opcodeOnlyHandlers.end()) {
                handler = byOpcode->second;
            }
        }
    }

    if (!handler) {
        return FALSE;
    }

    handler(packet);
    return TRUE;
}

void ResponseDispatcher::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_handlers.clear();
    m_opcodeOnlyHandlers.clear();
}

void ResponseDispatcher::InitializeDefaultHandlers() {
    Clear();

    const ResponseHandler processBattle = [](const GamePacket& gp) {
        // 共享战斗包统一由通用解析维护公共战斗态/UI。
        // BattleSix 仅在这里补自己的私有自动化状态与结算收尾。
        PacketParser::ProcessBattlePacket(gp);

        // 剧情战斗的回合驱动（万妖盛会/双台谷各自处理，这里只接普通战斗）。
        HandleStoryBattlePacket(gp);

        if (gp.opcode == Opcode::BATTLE_START) {
            if (g_battleSixAuto.IsAutoMatching() &&
                g_battleSixFlowStage.load() != BATTLESIX_FLOW_IDLE) {
                ResetBattleSixFlowState();
            }

            if (g_battleSixAuto.IsInBattle() &&
                g_battleSixAuto.IsAutoMatching() &&
                !g_battleSixReadySupplementSent.exchange(true)) {
                const unsigned long long battleSession = g_battleSixBattleSession.load();
                struct ReadyThreadData {
                    unsigned long long battleSession;
                };
                ReadyThreadData* threadData = new ReadyThreadData{battleSession};
                HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
                    std::unique_ptr<ReadyThreadData> data(static_cast<ReadyThreadData*>(lpParam));
                    Sleep(800);
                    if (g_battleSixBattleSession.load() == data->battleSession && g_battleSixAuto.IsInBattle()) {
                        SendBattleReadyPacket();
                    }
                    return 0;
                }, threadData, 0, nullptr);
                if (hThread) {
                    CloseHandle(hThread);
                } else {
                    delete threadData;
                    g_battleSixReadySupplementSent = false;
                }
            }

            if (g_shuangtaiAuto.IsRunning()) {
                ProcessShuangTaiBattleStartResponse(gp);
            }
            return;
        }

        if (gp.opcode == Opcode::BATTLE_ROUND_START ||
            gp.opcode == Opcode::BATTLE_CHANGE_SPIRIT_ROUND) {
            if (g_shuangtaiAuto.IsRunning()) {
                ProcessShuangTaiBattleRoundStartResponse(gp);
            }
            return;
        }

        if (gp.opcode == Opcode::BATTLE_ROUND) {
            if (g_battleSixAuto.IsInBattle()) {
                ProcessBattleSixBattleRoundResultResponse(gp);
            }

            if (g_shuangtaiAuto.IsRunning()) {
                ProcessShuangTaiBattleRoundResultResponse(gp);
            }
            return;
        }

        if (gp.opcode == Opcode::BATTLE_END) {
            if (g_battleSixAuto.IsInBattle()) {
                ProcessBattleSixBattleEndResponse(gp);
            }

            if (g_shuangtaiAuto.IsRunning()) {
                ProcessShuangTaiBattleEndResponse(gp);
            }
        }

        if (gp.opcode == Opcode::BATTLE_BUF ||
            gp.opcode == Opcode::BATTLE_BUF_DIS ||
            gp.opcode == Opcode::BATTLE_BUFS ||
            gp.opcode == Opcode::BATTLE_FSPK ||
            gp.opcode == Opcode::COMBAT_SITE_OPTION ||
            gp.opcode == Opcode::COMBAT_SITE_EFFECT ||
            gp.opcode == Opcode::COMBAT_NET_PROBE_REQ ||
            gp.opcode == Opcode::COMBAT_NET_REPORT) {
            return;
        }
    };
    const ResponseHandler processLingyu = [](const GamePacket& gp) {
        PacketParser::ProcessLingyuPacket(gp);
    };
    const ResponseHandler processMonster = [](const GamePacket& gp) {
        PacketParser::ProcessMonsterPacket(gp);
    };
    const ResponseHandler enterScene = [](const GamePacket&) {
        g_danceState.enteredMap = true;
        {
            CriticalSectionLock lock(g_towerCS);
            g_towerMapEntered = true;
        }
        g_heavenFuruiEnteredMap = true;
        g_taskZoneMapEntered = true;
        g_storyTaskMapEntered = true;
        g_storyTaskMapEnterSession = g_storyTaskSession.load();
    };

    const auto registerOpcode = [this](uint32_t opcode, const ResponseHandler& handler) {
        Register(opcode, handler);
    };
    const auto registerParams = [this](uint32_t opcode, uint32_t params, const ResponseHandler& handler) {
        Register(opcode, params, handler);
    };

    registerOpcode(Opcode::BATTLE_START, processBattle);
    registerOpcode(Opcode::BATTLE_ROUND_START, processBattle);
    registerOpcode(Opcode::BATTLE_ROUND, processBattle);
    registerOpcode(Opcode::BATTLE_END, processBattle);
    registerOpcode(Opcode::BATTLE_CHANGE_SPIRIT_ROUND, processBattle);
    registerOpcode(Opcode::BATTLE_BUF, processBattle);
    registerOpcode(Opcode::BATTLE_BUF_DIS, processBattle);
    registerOpcode(Opcode::BATTLE_BUFS, processBattle);
    registerOpcode(Opcode::BATTLE_FSPK, processBattle);
    registerOpcode(Opcode::BATTLE_NEWEXP, processBattle);
    registerOpcode(Opcode::SPIRIT_LEVEL_UP, processBattle);
    registerOpcode(Opcode::BATTLE_WITH, processBattle);
    registerOpcode(Opcode::BATTLE_REQ_ON_SKILL, processBattle);
    registerOpcode(Opcode::COMBAT_SITE_OPTION, processBattle);
    registerOpcode(Opcode::COMBAT_SITE_EFFECT, processBattle);
    registerOpcode(Opcode::COMBAT_NET_PROBE_REQ, processBattle);
    registerOpcode(Opcode::COMBAT_NET_REPORT, processBattle);

    registerOpcode(Opcode::LINGYU_LIST, processLingyu);
    registerOpcode(Opcode::DECOMPOSE_RESPONSE, [](const GamePacket&) { HandleDecomposeResponse(); });
    registerOpcode(Opcode::MONSTER_LIST, processMonster);
    registerOpcode(Opcode::ENTER_WORLD, ProcessEnterWorldPacket);
    registerOpcode(Opcode::ENTER_SCENE_BACK, enterScene);
    registerOpcode(Opcode::USER_TASK_LIST_BACK, ProcessTaskZoneUserTaskListResponse);
    registerOpcode(Opcode::TASK_TALK_BACK, ProcessStoryTaskTalkResponse);
    registerOpcode(Opcode::NPCTASKLIST_BACK, ProcessStoryNpcTaskListResponse);
    registerOpcode(Opcode::NPC_LIST_BACK, ProcessStoryNpcListResponse);
    registerOpcode(Opcode::GET_MAGIC_INDEX_BACK, ProcessStoryMagicIndexResponse);
    registerOpcode(Opcode::TASK_FINISHED_BACK, ProcessStoryTaskFinishedResponse);

    registerOpcode(Opcode::DEEP_DIG_BACK, ProcessDeepDigResponse);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, 668, ProcessDeepDigQueryResponse);

    registerOpcode(Opcode::DANCE_ACTIVITY_BACK, ProcessDanceActivityResponse);
    registerOpcode(Opcode::DANCE_STAGE_BACK, ProcessDanceStageResponse);

    registerParams(Opcode::TRIAL_BACK, 142, ProcessTrialResponse);
    registerParams(Opcode::TRIAL_BACK, 341, ProcessTowerActivityResponse);

    registerOpcode(Opcode::COLLECT_STATUS_BACK, ProcessCollectResponse);
    registerOpcode(Opcode::REQ_PACKAGE_DATA_BACK, ProcessPackageDataResponse);

    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act778::ACTIVITY_ID, ProcessAct778Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act685::ACTIVITY_ID, ProcessAct685Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act666::ACTIVITY_ID, ProcessAct666Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act757::ACTIVITY_ID, ProcessAct757Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act641::ACTIVITY_ID, ProcessAct641Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act684::ACTIVITY_ID, ProcessAct684Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act717::ACTIVITY_ID, ProcessAct717Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act808::ACTIVITY_ID, ProcessAct808Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act805::ACTIVITY_ID, ProcessAct805Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act631::ACTIVITY_ID, ProcessAct631Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act782::ACTIVITY_ID, ProcessAct782Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act804::ACTIVITY_ID, ProcessAct804Response);
    registerParams(Opcode::ACTIVITY_LUA_V3_BACK, Act804::ACTIVITY_ID, ProcessAct804Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act810::ACTIVITY_ID, ProcessAct810Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act811::ACTIVITY_ID, ProcessAct811Response);
    registerParams(Opcode::ACTIVITY_LUA_V3_BACK, Act811::ACTIVITY_ID, ProcessAct811Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act822::ACTIVITY_ID, ProcessAct822Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act826::ACTIVITY_ID, ProcessAct826Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act827::ACTIVITY_ID, ProcessAct827Response);
    registerParams(Opcode::HORSE_COMPETITION_BACK, HORSE_COMPETITION_ACT_ID, ProcessHorseCompetitionResponse);

    // 最新活动（周活动小游戏）：按活动 ID 路由到统一处理器
    registerParams(Opcode::ACTIVITY_QUERY_BACK, LatestAct::ACT_MAZE, ProcessLatestActivityResponse);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, LatestAct::ACT_TOUCH_STAR, ProcessLatestActivityResponse);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, LatestAct::ACT_EXPLORE, ProcessLatestActivityResponse);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, LatestAct::ACT_MOON_CAKE, ProcessLatestActivityResponse);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, LatestAct::ACT_FLOWER, ProcessLatestActivityResponse);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, LatestAct::ACT_FOOD, ProcessLatestActivityResponse);

    registerParams(Opcode::HEAVEN_FURUI_BACK, HeavenFurui::ACTIVITY_ID, ProcessHeavenFuruiResponse);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, HeavenFurui::ACTIVITY_ID, ProcessHeavenFuruiResponse);

    registerOpcode(Opcode::BATTLESIX_COMBAT_INFO_BACK, ProcessBattleSixCombatInfoResponse);
    registerOpcode(Opcode::BATTLESIX_MATCH_BACK, ProcessBattleSixMatchResponse);
    registerOpcode(Opcode::BATTLESIX_CANCEL_MATCH_BACK, ProcessBattleSixCancelMatchResponse);
    registerOpcode(Opcode::BATTLESIX_PREPARE_COMBAT_BACK, ProcessBattleSixPrepareCombatResponse);
    registerOpcode(Opcode::BATTLESIX_OTHER_ESCAPED_BACK, ProcessBattleSixOtherEscapedResponse);
    registerOpcode(Opcode::BATTLESIX_COMBAT_OVER_BACK, ProcessBattleSixCombatOverResponse);
    registerOpcode(Opcode::BATTLESIX_SELF_BAN_SPIRIT_BACK, ProcessBattleSixSelfBanSpiritResponse);
    registerOpcode(Opcode::BATTLESIX_FINISH_FIRST_SPIRIT_BACK, ProcessBattleSixFinishFirstSpiritResponse);
    registerOpcode(Opcode::BATTLESIX_REQ_START_BACK, ProcessBattleSixReqStartResponse);

    // 精魄系统响应处理
    registerOpcode(Opcode::SPIRIT_PRESURES_BACK, ProcessSpiritPresuresResponse);
    registerOpcode(Opcode::SPIRIT_SEND_SPIRIT_BACK, ProcessSpiritSendSpiritResponse);
    registerOpcode(Opcode::SPIRIT_PLAYER_INFO_BACK, ProcessSpiritPlayerInfoResponse);
    registerParams(Opcode::SPIRIT_COLLECT_BACK, SPIRIT_COLLECT_ACT_ID, ProcessSpiritCollectResponse);
}



    



    // ============================================================================



    // ActivityStateManager 实现






    



    



    ActivityStateManager& ActivityStateManager::Instance() {
        static ActivityStateManager instance;
        return instance;
    }

    Act778State& ActivityStateManager::GetAct778State() {
        return m_act778State;
    }

    Act685State& ActivityStateManager::GetAct685State() {
        return m_act685State;
    }

    Act666State& ActivityStateManager::GetAct666State() {
        return m_act666State;
    }

    Act757State& ActivityStateManager::GetAct757State() {
        return m_act757State;
    }

    Act822State& ActivityStateManager::GetAct822State() {
        return m_act822State;
    }

    Act826State& ActivityStateManager::GetAct826State() {
        return m_act826State;
    }

    Act827State& ActivityStateManager::GetAct827State() {
        return m_act827State;
    }

    Act641State& ActivityStateManager::GetAct641State() {
        return m_act641State;
    }

    Act684State& ActivityStateManager::GetAct684State() {
        return m_act684State;
    }

    Act717State& ActivityStateManager::GetAct717State() {
        return m_act717State;
    }

    Act805State& ActivityStateManager::GetAct805State() {
        return m_act805State;
    }

    Act631State& ActivityStateManager::GetAct631State() {
        return m_act631State;
    }

    HorseCompetitionState& ActivityStateManager::GetHorseCompetitionState() {
        return m_horseCompetitionState;
    }

    

                    // 封包发送（带超时、重试和自动等待响应）



    // ============================================================================


BOOL SendPacket(
    SOCKET s,
    const BYTE* pData,
    DWORD dwSize,
    uint32_t expectedOpcode,
    DWORD timeoutMs,
    uint32_t expectedParams,
    bool matchExpectedParams) {
    SOCKET targetSocket = (s != 0) ? s : g_LastGameSocket;
    if (targetSocket == 0) {
        return FALSE;
    }

    struct PendingResponseGuard {
        bool active = false;
        ~PendingResponseGuard() {
            if (active) {
                ResponseWaiter::EndWait();
            }
        }
    } pendingResponseGuard;

    uint64_t responseSerial = 0;
    const bool shouldWait = expectedOpcode != 0 && timeoutMs > 0;
    if (shouldWait) {
        responseSerial = ResponseWaiter::BeginWait();
        pendingResponseGuard.active = true;
    }

    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0) {
            Sleep(WpeHook::TIMEOUT_RETRY_INTERVAL);
        }

        targetSocket = (s != 0) ? s : g_LastGameSocket;
        if (targetSocket == 0) {
            continue;
        }

        DWORD sendTimeout = WpeHook::TIMEOUT_SEND;
        DWORD originalTimeout = sendTimeout;
        int optLen = sizeof(originalTimeout);
        getsockopt(targetSocket, SOL_SOCKET, SO_SNDTIMEO,
                   reinterpret_cast<char*>(&originalTimeout), &optLen);
        setsockopt(targetSocket, SOL_SOCKET, SO_SNDTIMEO,
                   reinterpret_cast<char*>(&sendTimeout), sizeof(sendTimeout));

        struct SocketTimeoutGuard {
            SOCKET socket = 0;
            DWORD originalTimeout = 0;
            ~SocketTimeoutGuard() {
                if (socket != 0) {
                    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
                               reinterpret_cast<const char*>(&originalTimeout),
                               sizeof(originalTimeout));
                }
            }
        } timeoutGuard{targetSocket, originalTimeout};

        int result = SOCKET_ERROR;
        {
            SendPacketCompleteGuard completeSendGuard;
            result = HookedSend(targetSocket,
                                reinterpret_cast<const char*>(pData),
                                static_cast<int>(dwSize),
                                0);
        }
        if (result == SOCKET_ERROR) {
            const int error = WSAGetLastError();
            if (error == WSAECONNRESET || error == WSAECONNABORTED) {
                return FALSE;
            }
            continue;
        }

        if (result != static_cast<int>(dwSize)) {
            return FALSE;
        }

        if (shouldWait) {
            const bool received = ResponseWaiter::WaitForResponse(
                expectedOpcode,
                timeoutMs,
                responseSerial,
                expectedParams,
                matchExpectedParams,
                true);
            if (!received) {
                return FALSE;
            }
        }

        return TRUE;
    }

    return FALSE;
}
// ============================================================================

VOID ClearPacketList() {
    if (!g_bInitialized) return;
    
    CriticalSectionLock lock(g_packetListCS.Get());
    for (auto& packet : g_PacketList) {
        delete[] packet.pData;
        packet.pData = nullptr;
    }
    g_PacketList.clear();
}

VOID DeleteSelectedPackets(const std::vector<DWORD>& indices) {
    if (!g_bInitialized) return;
    
    CriticalSectionLock lock(g_packetListCS.Get());
    
    // 从大到小排序，避免索引偏移
    std::vector<DWORD> sortedIndices = indices;
    std::sort(sortedIndices.begin(), sortedIndices.end(), std::greater<DWORD>());
    
    for (DWORD index : sortedIndices) {
        if (index < g_PacketList.size()) {
            delete[] g_PacketList[index].pData;
            g_PacketList.erase(g_PacketList.begin() + index);
        }
    }
}

DWORD GetPacketCount() {
    CriticalSectionLock lock(g_packetListCS.Get());
    return static_cast<DWORD>(g_PacketList.size());
}

BOOL GetPacket(DWORD index, PPACKET pPacket) {
    if (!pPacket) return FALSE;
    
    CriticalSectionLock lock(g_packetListCS.Get());
    if (index >= g_PacketList.size()) {
        return FALSE;
    }
    
    *pPacket = g_PacketList[index];
    return TRUE;
}

// ============ 灵玉相关功能实现 ============

// 解析 JSON 数组字符串，如 ["37","52"] 或 ['37','52']，返回 "37_52" 格式
static std::string ParseIndicesArray(const std::wstring& jsonArray) {
    std::string result;

    // 直接转换为 ANSI 字符串
    int len = WideCharToMultiByte(CP_ACP, 0, jsonArray.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return result;
    }

    std::string jsonA(len, 0);
    WideCharToMultiByte(CP_ACP, 0, jsonArray.c_str(), -1, &jsonA[0], len, nullptr, nullptr);

    // 移除空白，并将单引号替换为双引号
    std::string clean;
    for (char c : jsonA) {
        if (!isspace((unsigned char)c)) {
            if (c == '\'') clean += '"';  // 单引号转双引号
            else clean += c;
        }
    }

    // 查找 [ 和 ]
    size_t start = clean.find('[');
    size_t end = clean.find(']');
    if (start == std::string::npos || end == std::string::npos || end <= start) {
        return result;
    }

    std::string content = clean.substr(start + 1, end - start - 1);

    // 用逗号分割
    size_t pos = 0;
    bool first = true;
    while (pos < content.length()) {
        size_t nextComma = content.find(',', pos);
        std::string item;
        if (nextComma == std::string::npos) {
            item = content.substr(pos);
            pos = content.length();
        } else {
            item = content.substr(pos, nextComma - pos);
            pos = nextComma + 1;
        }

        // 移除引号
        size_t q1 = item.find('"');
        size_t q2 = item.find_last_of('"');
        if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1) {
            item = item.substr(q1 + 1, q2 - q1 - 1);
        }

        if (!item.empty()) {
            if (!first) result += "_";
            result += item;
            first = false;
        }
    }

    return result;
}

// 发送查询灵玉封包（2个封包）
BOOL SendQueryLingyuPacket() {
    // 查询灵玉背包列表（背包中的灵玉）
    // sendMessage(MsgDoc.OP_CLIENT_SPIRIT_EQUIP_ALL.send, userId, [1])
    // 封包格式: Magic(2) + Length(2) + Opcode(4) + Params(4) + Body(8)
    // Body: 数组长度(4字节) + 元素值(4字节)
    
    // 检查是否有卡布号
    if (g_userId == 0) {
        return FALSE;
    }
    
    // 查询封包1: 查询背包中的灵玉 (params=userId, body=[1])
    // Opcode: 1185809 (0x00121811)
    BYTE packet1[20] = {
        0x44, 0x53,  // Magic: "SD"
        0x08, 0x00,  // Length: 8 bytes (数组长度4 + 元素值4)
        0x11, 0x18, 0x12, 0x00,  // Opcode: 0x00121811 (1185809)
        0x00, 0x00, 0x00, 0x00,  // Params: userId (动态填充)
        0x01, 0x00, 0x00, 0x00,  // Body: 数组长度 = 1
        0x01, 0x00, 0x00, 0x00   // Body: 元素值 = 1 (背包灵玉)
    };
    // 填充卡布号到Params字段 (小端序)
    packet1[8] = static_cast<BYTE>(g_userId & 0xFF);
    packet1[9] = static_cast<BYTE>((g_userId >> 8) & 0xFF);
    packet1[10] = static_cast<BYTE>((g_userId >> 16) & 0xFF);
    packet1[11] = static_cast<BYTE>((g_userId >> 24) & 0xFF);

    // 查询封包2: 查询已装备的灵玉 (params=userId, body=[2])
    // Opcode: 1185809 (0x00121811)
    BYTE packet2[20] = {
        0x44, 0x53,  // Magic: "SD"
        0x08, 0x00,  // Length: 8 bytes
        0x11, 0x18, 0x12, 0x00,  // Opcode: 0x00121811 (1185809)
        0x00, 0x00, 0x00, 0x00,  // Params: userId (动态填充)
        0x01, 0x00, 0x00, 0x00,  // Body: 数组长度 = 1
        0x02, 0x00, 0x00, 0x00   // Body: 元素值 = 2 (已装备灵玉)
    };
    // 填充卡布号到Params字段 (小端序)
    packet2[8] = static_cast<BYTE>(g_userId & 0xFF);
    packet2[9] = static_cast<BYTE>((g_userId >> 8) & 0xFF);
    packet2[10] = static_cast<BYTE>((g_userId >> 16) & 0xFF);
    packet2[11] = static_cast<BYTE>((g_userId >> 24) & 0xFF);

    BOOL result1 = SendPacket(0, packet1, sizeof(packet1));

    // 延迟300ms发送第二个封包
    Sleep(300);

    BOOL result2 = SendPacket(0, packet2, sizeof(packet2));

    return result1 && result2;
}

// 发送查询背包封包（2个包）
BOOL SendQueryBagPacket() {
    // 查询封包1: 44 53 00 00 18 18 12 00 00 00 00 00
    BYTE packet1[] = {
        0x44, 0x53,  // Magic: "SD"
        0x00, 0x00,  // Length: 0x0000
        0x18, 0x18, 0x12, 0x00,  // Opcode: 0x00121818
        0x00, 0x00, 0x00, 0x00   // Params: 0
    };

    // 查询封包2: 44 53 00 00 11 10 12 00 FF FF FF FF
    BYTE packet2[] = {
        0x44, 0x53,  // Magic: "SD"
        0x00, 0x00,  // Length: 0x0000
        0x11, 0x10, 0x12, 0x00,  // Opcode: 0x00121011
        0xFF, 0xFF, 0xFF, 0xFF   // Params: 0xFFFFFFFF
    };

    BOOL result1 = SendPacket(0, packet1, sizeof(packet1));

    // 延迟50ms发送第二个封包
    Sleep(50);

    BOOL result2 = SendPacket(0, packet2, sizeof(packet2));

    return result1 && result2;
}

// 发送一键分解封包 - 异步版本
// jsonArray: JSON数组字符串，如 ["37","52"]
struct DecomposeTaskData {
    std::string indices;
    HWND hwnd;  // 窗口句柄用于发送消息
};

// 分解任务完成消息

DWORD WINAPI DecomposeThreadProc(LPVOID lpParam) {
    DecomposeTaskData* taskData = static_cast<DecomposeTaskData*>(lpParam);

    // 立即启用阻止发送功能，阻止 1185792 和 1185809 的发送
    g_blockOpcodeSend = true;

    // 解析索引数组（例如 "62_61" -> ["62", "61"]）
    std::vector<std::string> indexList;
    std::string current;
    for (size_t i = 0; i < taskData->indices.length(); i++) {
        if (taskData->indices[i] == '_') {
            if (!current.empty()) {
                indexList.push_back(current);
                current.clear();
            }
        } else {
            current += taskData->indices[i];
        }
    }
    if (!current.empty()) {
        indexList.push_back(current);
    }

    // 构建分解封包: 44 53 00 00 16 18 12 00 + 索引(4字节LE)
    // 例如: 分解索引 1234 (0x04D2) -> 44 53 00 00 16 18 12 00 D2 04 00 00

    // 将索引数组合并为一个字符串，用下划线连接
    std::string indicesStr;
    for (size_t i = 0; i < indexList.size(); i++) {
        if (i > 0) indicesStr += "_";
        indicesStr += indexList[i];
    }

    BOOL allSuccess = TRUE;

    // 逐个分解每个灵玉
    for (size_t i = 0; i < indexList.size(); i++) {
        const auto& indexStr = indexList[i];

        // 将字符串索引转换为整数
        int indexValue = 0;
        if (!TryParseInt32Decimal(indexStr, indexValue)) {
            allSuccess = FALSE;
            continue;
        }

        // 使用 PacketBuilder 构建分解封包
        // Opcode: 1185814, Params: symmIndex
        auto packet = PacketBuilder()
            .SetOpcode(1185814)
            .SetParams(static_cast<uint32_t>(indexValue))
            .Build();

        // 发送分解封包
        BOOL result = SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
        
        // 生成十六进制封包字符串并发送到UI
        std::string hexPacket = HexToString(packet.data(), static_cast<DWORD>(packet.size())) + " ";

        // 向UI线程发送十六进制封包信息
        if (g_hWnd) {
            HexPacketData* hexData = new HexPacketData();
            hexData->len = hexPacket.length() + 1;
            hexData->data = new char[hexData->len];
            strcpy_s(hexData->data, hexData->len, hexPacket.c_str());
    
            PostMessage(g_hWnd, AppMessage::kDecomposeHexPacket, (WPARAM)hexData, 0);
        }
    
            // 延迟200ms再分解下一个，避免过快发送
            if (&indexStr != &indexList.back()) {
                Sleep(200);
            }
    
            // 更新整体成功状态
            allSuccess = allSuccess && result;
        }

    // 禁用阻止发送功能
    g_blockOpcodeSend = false;

    // 发送完成消息到UI线程
    if (g_hWnd) {
        PostMessage(g_hWnd, AppMessage::kDecomposeComplete, (WPARAM)allSuccess, 0);
    }
    
    // 清理内存
    delete taskData;
    
    return allSuccess;
}

BOOL SendDecomposeLingyuPacket(const std::wstring& jsonArray) {
    if (jsonArray.empty()) {
        return FALSE;
    }

    // 解析 JSON 数组，构造 indices 字符串
    std::string indices = ParseIndicesArray(jsonArray);
    if (indices.empty()) {
        return FALSE;
    }

    // 创建任务数据结构
    DecomposeTaskData* taskData = new DecomposeTaskData();
    taskData->indices = indices;
    taskData->hwnd = g_hWnd;

    // 创建新线程处理分解任务，避免阻塞主线程
    HANDLE hThread = CreateThread(NULL, 0, DecomposeThreadProc, taskData, 0, NULL);
    if (hThread) {
        CloseHandle(hThread);
        return TRUE;
    } else {
        delete taskData;
        return FALSE;
    }
}

// 发送查询妖怪背包封包
// Opcode: 1187329, Params: 0
BOOL SendQueryMonsterPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1187329)
        .SetParams(0)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// ============ 日常活动功能实现 ============

// 深度挖宝 - 发送开始游戏封包
// Opcode: 1185525, Params: 12, Body: 6个int32(都是0)
BOOL SendDeepDigPacket() {
    g_deepDigState.waitingResponse = true;
    g_deepDigState.sessionId = 0;
    
    auto packet = PacketBuilder()
        .SetOpcode(1185525)
        .SetParams(12)
        .WriteInt32(0)
        .WriteInt32(0)
        .WriteInt32(0)
        .WriteInt32(0)
        .WriteInt32(0)
        .WriteInt32(0)
        .Build();
    
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 查询深度挖宝剩余次数
// Opcode: 1185685, Params: 668, Body: "open_ui"字符串
BOOL SendQueryDeepDigCountPacket() {
    g_deepDigState.waitingQuery = true;

    auto packet = PacketBuilder()
        .SetOpcode(1185685)
        .SetParams(668)
        .WriteString("open_ui")
        .Build();

    return SendPacket(
        0,
        packet.data(),
        static_cast<DWORD>(packet.size()),
        Opcode::ACTIVITY_QUERY_BACK,
        3000,
        668,
        true);
}

// 处理深度挖宝查询响应
void ProcessDeepDigQueryResponse(const GamePacket& packet) {
    if (!g_deepDigState.waitingQuery) return;
    if (packet.body.size() < 12) return;
    
    const BYTE* body = packet.body.data();
    size_t offset = 0;
    
    // 读取 UTF 字符串
    if (offset + 2 > packet.body.size()) return;
    uint16_t strLen = ReadUInt16LE(body, offset);
    offset += 2 + strLen;
    
    // 读取剩余次数
    if (offset + 4 > packet.body.size()) return;
    g_deepDigState.remainingCount = ReadInt32LE(body, offset);
    
    g_deepDigState.waitingQuery = false;
}

// 深度挖宝线程数据
struct DeepDigThreadData {
    int targetCount;
    HWND hwnd;
};

// 深度挖宝循环执行线程
DWORD WINAPI DeepDigLoopThreadProc(LPVOID lpParam) {
    DeepDigThreadData* data = static_cast<DeepDigThreadData*>(lpParam);
    int targetCount = data->targetCount;
    
    // 先查询剩余次数
    SendQueryDeepDigCountPacket();

    // 确定实际执行次数
    int actualCount = targetCount;
    if (g_deepDigState.remainingCount > 0 &&
        g_deepDigState.remainingCount < targetCount) {
        actualCount = g_deepDigState.remainingCount;
    }

    // 循环执行
    for (int i = 0; i < actualCount; i++) {
        g_deepDigState.waitingResponse = true;
        g_deepDigState.sessionId = 0;

        // 发送开始游戏封包: Opcode=1185525, Params=12, Body=6个int32(0)
        auto startPacket = PacketBuilder()
            .SetOpcode(1185525)
            .SetParams(12)
            .WriteInt32(0)
            .WriteInt32(0)
            .WriteInt32(0)
            .WriteInt32(0)
            .WriteInt32(0)
            .WriteInt32(0)
            .Build();
        SendPacket(0, startPacket.data(), static_cast<DWORD>(startPacket.size()), Opcode::DEEP_DIG_BACK, 5000);
        
        Sleep(500);
        
        // 发送结束游戏封包
        if (g_deepDigState.sessionId > 0) {
            // Opcode=1185525, Params=12, Body=[4, 0, 4, sessionId, 0, 0]
            auto endPacket = PacketBuilder()
                .SetOpcode(1185525)
                .SetParams(12)
                .WriteInt32(4)
                .WriteInt32(0)
                .WriteInt32(4)
                .WriteInt32(g_deepDigState.sessionId)
                .WriteInt32(0)
                .WriteInt32(0)
                .Build();
            SendPacket(0, endPacket.data(), static_cast<DWORD>(endPacket.size()));
        }
        
        g_deepDigState.completedCount++;
        
        if (i < actualCount - 1) {
            Sleep(1000);
        }
    }
    
    // 发送完成消息
    if (g_hWnd) {
        PostMessage(g_hWnd, AppMessage::kDailyTaskComplete, 
                    g_deepDigState.completedCount, actualCount);
    }
    
    g_deepDigState.autoMode = false;
    delete data;
    return 0;
}

// 深度挖宝 - 执行N次
BOOL SendDeepDigPacketNTimes(int count) {
    if (count <= 0) count = WpeHook::DEEP_DIG_DEFAULT_COUNT;
    if (g_deepDigState.autoMode) return FALSE;
    
    g_deepDigState.autoMode = true;
    g_deepDigState.targetCount = count;
    g_deepDigState.completedCount = 0;
    
    DeepDigThreadData* data = new DeepDigThreadData();
    data->targetCount = count;
    data->hwnd = g_hWnd;
    
    HANDLE hThread = CreateThread(NULL, 0, DeepDigLoopThreadProc, data, 0, NULL);
    if (hThread) {
        CloseHandle(hThread);
        return TRUE;
    } else {
        g_deepDigState.autoMode = false;
        delete data;
        return FALSE;
    }
}

// 深度挖宝 - 发送结束游戏封包
// Opcode: 1185525, Params: 12, Body: [4, 0, 4, sessionId, 0, 0]
static BOOL SendDeepDigEndPacket(int sessionId, int score) {
    (void)score;
    auto packet = PacketBuilder()
        .SetOpcode(1185525)
        .SetParams(12)
        .WriteInt32(4)
        .WriteInt32(0)
        .WriteInt32(4)
        .WriteInt32(sessionId)
        .WriteInt32(0)
        .WriteInt32(0)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 处理深度挖宝响应
void ProcessDeepDigResponse(const GamePacket& packet) {
    if (!g_deepDigState.waitingResponse) return;
    if (packet.body.size() < 8) return;
    
    const BYTE* body = packet.body.data();
    size_t offset = 0;
    
    int32_t act = ReadInt32LE(body, offset);
    int32_t result = ReadInt32LE(body, offset);
    
    if (act == 0) {
        g_deepDigState.sessionId = result;
        g_deepDigState.waitingResponse = false;
        
        CreateThread(NULL, 0, [](LPVOID) -> DWORD {
            Sleep(500);
            SendDeepDigEndPacket(g_deepDigState.sessionId, 4);
            return 0;
        }, NULL, 0, NULL);
    }
}

// 每日卡牌
// Opcode: 1184815, Params: 22
BOOL SendDailyCardPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1184815)
        .SetParams(22)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 每日礼包
// Opcode: 1186300 (OP_CLIENT_VIP_DAY_AWARD), Params: 0
BOOL SendDailyGiftPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1186300)
        .SetParams(0)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 每周礼包
// Opcode: 1186290 (OP_CLIENT_VIP_WEEK_AWARD), Params: 0
BOOL SendWeeklyGiftPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1186290)
        .SetParams(0)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 家族考勤
// Opcode: 1184770, Params: 0
BOOL SendFamilyCheckinPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1184770)
        .SetParams(0)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 家族报道
// Opcode: 1185336, Params: 0
BOOL SendFamilyReportPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1185336)
        .SetParams(0)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 家族保卫战
// 发送3个封包
BOOL SendFamilyDefendPacket() {
    // 封包1: Opcode=1185313, Params=0, Body=[0]
    auto packet1 = PacketBuilder()
        .SetOpcode(1185313)
        .SetParams(0)
        .WriteInt32(0)
        .Build();
    SendPacket(0, packet1.data(), static_cast<DWORD>(packet1.size()));
    Sleep(300);
    
    // 封包2: Opcode=1185313, Params=1, Body=[6]
    auto packet2 = PacketBuilder()
        .SetOpcode(1185313)
        .SetParams(1)
        .WriteInt32(6)
        .Build();
    SendPacket(0, packet2.data(), static_cast<DWORD>(packet2.size()));
    Sleep(300);
    
    // 封包3: Opcode=1184801, Params=3
    auto packet3 = PacketBuilder()
        .SetOpcode(1184801)
        .SetParams(3)
        .Build();
    SendPacket(0, packet3.data(), static_cast<DWORD>(packet3.size()));
    
    return TRUE;
}

// 商城惊喜
// 循环发送3次: Opcode=1184833, Params=154, Body=[1]
BOOL SendShopSurprisePacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1184833)
        .SetParams(154)
        .WriteInt32(1)
        .Build();
    
    for (int i = 0; i < 3; i++) {
        SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
        if (i < 2) Sleep(300);
    }
    
    return TRUE;
}

// ============ 自动回血功能实现 ============

// 发送贝贝回血封包
// Opcode: 1186818, Params: 1
BOOL SendBeibeiHealPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1186818)
        .SetParams(1)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// ============ MD5图片验证自动回复 ============

// 图片内容MD5 -> 正反值映射表 (1=正面/面向你, 0=反面/背向你)
static const Md5FaceEntry g_md5FaceEntries[] = {
    {"6817be5af4b0e77f8446e5a007a4cc28", 0},
    {"74b9314abf6f6cfd2238c6a6236eb5f6", 0},
    {"4d622957f07627b2bc3709f70d669042", 0},
    {"e17631cae5f6a80b47eeae71dbc3c33c", 1},
    {"85a15ce5f34ab6603ee4f7eb22af4c0d", 1},
    {"1278f375c7eb202feb67c119cc8e801b", 0},
    {"a088db9ab3699ea8b5c93117b102c6b9", 0},
    {"849fbb8920a2a65766fa5098265b9ac8", 0},
    {"cb10d81505ded8aa0464b154365125bd", 0},
    {"b8b14263e617489c74bd6a7a9dcdeec5", 0},
    {"7b1dee29c9cc5abe3b33b16d84c86558", 1},
    {"d3c48477832015bdf502f59c67af9e78", 1},
    {"194edd9073b6202eae074c98c063cf09", 1},
    {"a8ff055f0b9545a1452124552bb6f99d", 1},
    {"727f5d15b8fc454d41e7821993006a00", 1},
    {"05eff1b5db485099344d95b1b9335495", 1},
    {"d273efe1270692d9f953b1f589939690", 1},
    {"866eca22ca73a65ed0093d93e32d0ab9", 0},
    {"564aac45bea2042e3a03c32de6d4d27a", 0},
    {"e44739c71ec7522e9d2c11f48278f9d6", 0},
    {"95e59bb35ab652f315acf02a90da65cf", 0},
    {"5c923c72ec51f68216e50a7e57ebd390", 1},
    {"822824d609cf40465093dccd880f6071", 1},
    {"f22b2b947e75a434cf4e5d15f62b4058", 0},
    {"e3a1e2e14c88b23513282fa79cf9b1ef", 0},
    {"7d5d876c5a3ae740e3de8e3c9cff16d7", 1},
    {"0c2cccca9f03980f122864b4f724aed2", 0},
    {"dd1bd287a069788cd25c510b06b66513", 1},
    {"5eeb80b7fc6502a260ff0f6026690d53", 0},
    {"6fb5816f27fc166abaae49e9467ba4cb", 0},
    {"fb68bfa2140cc9d15d0a2115c80f6680", 1},
    {"073c262eb6b27fcd23c393ad19c74a6e", 0},
    {"85405a09136a8adb0c8dce3c623cecb0", 1},
    {"3ab7e8982f30bbc13250352df96aa3ee", 1},
    {"930a7013354000a2eb5d3bdcb93b303c", 1},
    {"8893db073a63d2f0af703a640ebeab89", 1},
    {"fb767d00dd3ebb1974801bfe80a56800", 0},
    {"8c0f86fc2e4555b0dbb737624d95be0f", 0},
    {"6fcfa01f4faf1198b683449ccca8b1de", 1},
    {"39e78f92c6e6041536b73287cf778668", 1}
};

// 下载图片并计算MD5 (POST方式)
static std::string DownloadImageAndGetMD5(const std::string& imageMd5) {
    HINTERNET hSession = WinHttpOpen(L"MD5CheckBot/1.0", 
                                      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, 
                                      WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";
    
    HINTERNET hConnect = WinHttpConnect(hSession, L"php.wanwan4399.com", 
                                         INTERNET_DEFAULT_HTTP_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return "";
    }
    
    // POST 方式
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/game_guard/image.php",
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }
    
    // POST 数据: md5=xxx
    std::string postData = "md5=" + imageMd5;
    
    // 发送请求
    std::string result;
    LPCWSTR headers = L"Content-Type: application/x-www-form-urlencoded\r\n";
    if (WinHttpSendRequest(hRequest, headers, static_cast<DWORD>(-1),
                           (LPVOID)postData.data(), (DWORD)postData.size(),
                           (DWORD)postData.size(), 0)) {
        if (WinHttpReceiveResponse(hRequest, NULL)) {
            DWORD dwSize = 0;
            DWORD dwDownloaded = 0;
            do {
                dwSize = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                if (dwSize == 0) break;
                
                std::vector<char> buffer(dwSize);
                if (!WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) break;
                result.append(buffer.data(), dwDownloaded);
            } while (dwSize > 0);
        }
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    if (result.empty()) return "";
    
    // 计算MD5
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    BYTE rgbHash[16];
    DWORD cbHash = 16;
    
    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        return "";
    }
    if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return "";
    }
    if (!CryptHashData(hHash, (BYTE*)result.data(), (DWORD)result.size(), 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return "";
    }
    if (!CryptGetHashParam(hHash, HP_HASHVAL, rgbHash, &cbHash, 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return "";
    }
    
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    
    // 转为十六进制字符串
    char hex[33];
    for (int i = 0; i < 16; i++) {
        sprintf_s(hex + i * 2, 3, "%02x", rgbHash[i]);
    }
    hex[32] = '\0';
    
    return std::string(hex);
}

// 查找图片正反值
static int FindFaceValue(const std::string& imageContentMd5) {
    for (const auto& entry : g_md5FaceEntries) {
        if (imageContentMd5 == entry.md5) {
            return entry.face;
        }
    }
    return -1;  // 未找到
}

// 计算正确的验证索引
static int CalculateCorrectIndex(const std::vector<std::string>& imageMd5s, int verifyType) {
    // verifyType: 1=面向你(找正面), 其他=背向你(找反面)
    // 正面=1, 反面=0
    
    int targetValue = (verifyType == 1) ? 1 : 0;
    
    std::vector<int> faceValues;
    for (const auto& md5 : imageMd5s) {
        std::string contentMd5 = DownloadImageAndGetMD5(md5);
        if (contentMd5.empty()) {
            faceValues.push_back(-1);
            continue;
        }
        int value = FindFaceValue(contentMd5);
        faceValues.push_back(value);
    }
    
    // 统计目标值数量
    int targetCount = 0;
    int firstTargetIndex = -1;
    int firstOtherIndex = -1;
    
    for (int i = 0; i < 4; i++) {
        if (faceValues[i] == targetValue) {
            targetCount++;
            if (firstTargetIndex < 0) firstTargetIndex = i;
        } else if (faceValues[i] >= 0 && firstOtherIndex < 0) {
            firstOtherIndex = i;
        }
    }
    
    // 如果只有1个目标值，返回其索引
    if (targetCount == 1 && firstTargetIndex >= 0) {
        return firstTargetIndex;
    }
    // 否则返回第一个其他值（或第一个目标值）
    if (firstOtherIndex >= 0) return firstOtherIndex;
    if (firstTargetIndex >= 0) return firstTargetIndex;
    
    return 0;  // 默认返回0
}

// MD5验证回复封包
// Opcode: 1186193, Params: index (0-3)
BOOL SendMD5CheckReplyPacket(int index) {
    auto packet = PacketBuilder()
        .SetOpcode(1186193)
        .SetParams(static_cast<uint32_t>(index))
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 处理MD5验证封包并自动回复
void ProcessMD5CheckAndReply(const std::vector<BYTE>& body, uint32_t params) {
    // 先解压 body 数据（Flash ByteArray.uncompress() 格式）
    std::vector<uint8_t> decompressed;
    
    // 获取 zlib uncompress 函数
    auto uncompressFunc = GetZlibUncompress();
    if (uncompressFunc && !body.empty()) {
        // 检查 zlib 头 (0x78 表示 zlib 格式)
        if (body.size() >= 2 && body[0] == 0x78) {
            // 预分配解压缓冲区（通常是压缩前的4-10倍）
            decompressed.resize(body.size() * 10);
            unsigned long destLen = static_cast<unsigned long>(decompressed.size());
            
            int res = uncompressFunc(decompressed.data(), &destLen, body.data(), static_cast<unsigned long>(body.size()));
            if (res == 0) {
                decompressed.resize(destLen);
            } else {
                // 解压失败，使用原始数据
                decompressed.assign(body.begin(), body.end());
            }
        } else {
            // 不是 zlib 格式，直接使用原始数据
            decompressed.assign(body.begin(), body.end());
        }
    } else {
        decompressed.assign(body.begin(), body.end());
    }
    
    // 从解压后的数据中提取4个UTF字符串（图片MD5值）
    std::vector<std::string> imageMd5s;
    size_t offset = 0;
    
    for (int i = 0; i < 4 && offset < decompressed.size(); i++) {
        // UTF字符串格式：长度(2字节小端) + 内容
        if (offset + 2 > decompressed.size()) break;
        
        uint16_t strLen = decompressed[offset] | (decompressed[offset + 1] << 8);
        offset += 2;
        
        if (offset + strLen > decompressed.size()) break;
        
        std::string md5(decompressed.begin() + offset, decompressed.begin() + offset + strLen);
        imageMd5s.push_back(md5);
        offset += strLen;
    }
    
    if (imageMd5s.size() < 4) return;
    
    // 计算正确索引
    int correctIndex = CalculateCorrectIndex(imageMd5s, params);
    
    // 发送回复
    SendMD5CheckReplyPacket(correctIndex);
}

// ============ 试炼活动功能实现（重构版） ============

// 试炼活动响应 Opcode
// 封包字节 "41 2E 14 00" -> 0x00142E41 -> 1324097
#define OPCODE_TRIAL_BACK 1324097

// 试炼活动全局变量
static int g_trialCheckCode = 0;           // 校验码
static int g_trialGameCount = 0;           // 游戏次数
static int g_trialCoolTime = 0;            // 冷却时间
static int g_trialAwardNum = 0;            // 印记数量
static bool g_waitingTrialResponse = false; // 等待响应标志
static bool g_trialComplete = false;        // 试炼完成标志

// 试炼类型枚举
enum class TrialType {
    FireWind,  // 火风试炼
    Fire,      // 火焰试炼
    Storm      // 风暴试炼
};

// 试炼配置结构（从AS3代码分析得出）
struct TrialConfig {
    TrialType type;
    int startOp;       // 开始游戏协议ID
    int endOp;         // 结束游戏协议ID
    int infoOp;        // 基础信息协议ID
    int award;         // 奖励印记数量
    int checkCodeFactor;  // 校验码系数
    const wchar_t* name; // 试炼名称（用于UI显示）
};

// 三种试炼的配置
static const TrialConfig TRIAL_CONFIGS[] = {
    {TrialType::FireWind, 24, 25, 21, 450, 56, L"火风试炼"},
    {TrialType::Fire,      4,  5,  1, 350, 56, L"火焰试炼"},
    {TrialType::Storm,    14, 15, 11, 450, 72, L"风暴试炼"}
};

// 试炼线程函数声明
DWORD WINAPI TrialThreadProc(LPVOID lpParam);

// 通用试炼封包发送函数
// Opcode: 1184833 (0x00121441), Params: 142
// Body格式: [protocolId, ...其他参数]
static BOOL SendTrialPacket(
    const std::vector<int32_t>& bodyValues,
    uint32_t expectedOpcode = 0,
    DWORD timeoutMs = 0,
    uint32_t expectedParams = 0,
    bool matchExpectedParams = false) {
    auto packet = PacketBuilder()
        .SetOpcode(1184833)
        .SetParams(142)
        .WriteInt32Array(bodyValues)
        .Build();

    return SendPacket(
        0,
        packet.data(),
        static_cast<DWORD>(packet.size()),
        expectedOpcode,
        timeoutMs,
        expectedParams,
        matchExpectedParams);
}

// 根据试炼类型获取配置
inline const TrialConfig& GetTrialConfig(TrialType type) {
    return TRIAL_CONFIGS[static_cast<int>(type)];
}

// ============ 通用试炼函数 ============

// 开始试炼游戏（通用）
static BOOL SendStartTrialPacket(TrialType type) {
    const TrialConfig& config = GetTrialConfig(type);
    g_trialComplete = false;
    g_waitingTrialResponse = true;
    
    // 发送封包并等待响应
    BOOL result = SendTrialPacket({config.startOp}, Opcode::TRIAL_BACK, 5000, 142, true);
    if (!result) {
        g_waitingTrialResponse = false;
        return FALSE;
    }
    
    // 等待试炼线程完成（最多5秒）
    for (int i = 0; i < 50 && !g_trialComplete; i++) {
        Sleep(100);
    }
    
    return g_trialComplete;
}

// 结束试炼游戏（通用）
static BOOL SendEndTrialPacket(TrialType type, int result, int award, int checkCode) {
    const TrialConfig& config = GetTrialConfig(type);
    return SendTrialPacket({config.endOp, result, award, checkCode});
}

// ============ 试炼对外接口函数 ============

// 火风试炼
BOOL SendFireWindTrialPacket() { return SendStartTrialPacket(TrialType::FireWind); }
BOOL SendFireWindEndPacket(int result, int awardCount, int checkCode) {
    return SendEndTrialPacket(TrialType::FireWind, result, awardCount, checkCode);
}

// 火焰试炼
BOOL SendFireTrialPacket() { return SendStartTrialPacket(TrialType::Fire); }
BOOL SendFireEndPacket(int result, int awardCount, int checkCode) {
    return SendEndTrialPacket(TrialType::Fire, result, awardCount, checkCode);
}

// 风暴试炼
BOOL SendStormTrialPacket() { return SendStartTrialPacket(TrialType::Storm); }
BOOL SendStormEndPacket(int exitStatus, int brand, int checkCode) {
    return SendEndTrialPacket(TrialType::Storm, exitStatus, brand, checkCode);
}

// 试炼线程参数结构
struct TrialThreadParam {
    int checkCode;
    TrialType type;
};

// 处理试炼活动响应（在HookedRecv中调用）
// Opcode: 1324097 (0x00142E41), Params: 142
// Body格式: [protocolId, ...]
void ProcessTrialResponse(const GamePacket& packet) {
    if (packet.body.size() < 4) return;
    
    const BYTE* body = packet.body.data();
    size_t offset = 0;
    
    // 读取 protocolId
    int32_t protocolId = ReadInt32LE(body, offset);
    
    // 读取 result
    int32_t result = 0;
    if (offset + 4 <= packet.body.size()) {
        result = ReadInt32LE(body, offset);
    }
    
    // 根据protocolId处理不同响应
    switch (protocolId) {
        case 4:   // 火焰试炼开始
        case 24:  // 火风试炼开始
        case 14:  // 风暴试炼开始
            if (result == 0 && offset + 4 <= packet.body.size()) {
                // 根据protocolId确定试炼类型
                TrialType type = (protocolId == 4) ? TrialType::Fire :
                                (protocolId == 24) ? TrialType::FireWind : TrialType::Storm;
                const TrialConfig& config = GetTrialConfig(type);
                
                // 获取校验码并计算真正的校验码
                int32_t checkCode = ReadInt32LE(body, offset);
                g_trialCheckCode = checkCode * config.checkCodeFactor + (g_userId % 100000);
                g_waitingTrialResponse = false;
                
                // 启动通用试炼线程
                TrialThreadParam* param = new TrialThreadParam{g_trialCheckCode, type};
                HANDLE hThread = CreateThread(nullptr, 0, TrialThreadProc, param, 0, nullptr);
                if (hThread) CloseHandle(hThread);
            }
            break;
            
        case 5:   // 火焰试炼结束
        case 25:  // 火风试炼结束
            if (result == 0 && offset + 4 <= packet.body.size()) {
                int32_t flame = ReadInt32LE(body, offset);
                char msg[64];
                sprintf_s(msg, "试炼完成，获得 %d 印记", flame);
            }
            break;
            
        case 15:  // 风暴试炼结束
            if (result == 0 && offset + 4 <= packet.body.size()) {
                (void)ReadInt32LE(body, offset);  // value：占位字段
            }
            break;
            
        case 1:   // 火焰试炼基础信息
        case 21:  // 火风试炼基础信息
        case 11:  // 风暴试炼基础信息
            if (offset + 12 <= packet.body.size()) {
                g_trialGameCount = ReadInt32LE(body, offset);
                g_trialCoolTime = ReadInt32LE(body, offset);
                g_trialAwardNum = ReadInt32LE(body, offset);
            }
            break;
    }
}

// ============ 通用试炼线程实现 ============

DWORD WINAPI TrialThreadProc(LPVOID lpParam) {
    TrialThreadParam* param = static_cast<TrialThreadParam*>(lpParam);
    int checkCode = param->checkCode;
    const TrialConfig& config = GetTrialConfig(param->type);
    
    // 延时300ms（模拟游戏时间）
    Sleep(300);
    
    // 发送结束游戏封包: [endOp, -1, award, checkCode]
    SendEndTrialPacket(param->type, -1, config.award, checkCode);
    
    // 标记试炼完成
    g_trialComplete = true;
    
    // 释放参数内存
    delete param;
    
    return 0;
}

// ============ 玄塔寻宝功能实现 ============

// 存入妖怪仓库
// Opcode: 1187333, Params: 1, Body: [param1, param2]
BOOL SendPutToSpiritStorePacket(int param1, int param2) {
    auto packet = PacketBuilder()
        .SetOpcode(1187333)
        .SetParams(1)
        .WriteInt32(param1)
        .WriteInt32(param2)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 法宝操作（贝贝）
// Opcode: 1186832, Params: type (0=召唤, 2=收回), Body: [userId, 0]
BOOL SendFabaoPacket(int type, uint32_t userId) {
    auto packet = PacketBuilder()
        .SetOpcode(1186832)
        .SetParams(static_cast<uint32_t>(type))
        .WriteUInt32(userId)
        .WriteInt32(0)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 查询玄塔信息
// Opcode: 1184833, Params: 341, Body: [1] (CHECK_INFO)
BOOL SendTowerCheckInfoPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1184833)
        .SetParams(341)
        .WriteInt32(1)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 购买骰子（18个）
// Opcode: 1184833, Params: 341, Body: [6, 18] (BUY_BONES, 数量18)
BOOL SendBuyDicePacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1184833)
        .SetParams(341)
        .WriteInt32(6)
        .WriteInt32(18)
        .Build();
    BOOL result = SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
    if (result) {
        UIBridge::Instance().UpdateHelperText(L"已购买18个骰子");
    }
    return result;
}

// 购买骰子（5个）
// Opcode: 1184833, Params: 341, Body: [6, 5] (BUY_BONES, 数量5)
BOOL SendBuyDice5Packet() {
    auto packet = PacketBuilder()
        .SetOpcode(1184833)
        .SetParams(341)
        .WriteInt32(6)
        .WriteInt32(5)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 跳转到玄塔地图
// Opcode: 1184313, Params: 16006
BOOL SendEnterTowerMapPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::ENTER_SCENE_SEND)
        .SetParams(16006)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 投掷骰子
// Opcode: 1184833, Params: 341, Body: [2] (THROW_BONES)
BOOL SendThrowBonesPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1184833)
        .SetParams(341)
        .WriteInt32(2)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 退出世界（回家第一步）
// Opcode: 1184002, Params: userId
BOOL SendExitScenePacket(uint32_t userId) {
    auto packet = PacketBuilder()
        .SetOpcode(1184002)
        .SetParams(userId)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 查询玄塔结果
// Opcode: 1184833, Params: 341, Body: [4] (REACH_RESULT)
BOOL SendTowerResultPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1184833)
        .SetParams(341)
        .WriteInt32(4)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 重新进入玄塔地图（回家第二步）
// Opcode: 1184313, Params: 1002
BOOL SendReenterTowerMapPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::ENTER_SCENE_SEND)
        .SetParams(1002)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 玄塔自动跑塔线程
// 流程：循环投掷20次（带响应等待）→ 退出回家
DWORD WINAPI TowerAutoThreadProc(LPVOID lpParam) {
    std::wstring* pStatus = reinterpret_cast<std::wstring*>(lpParam);
    bool success = true;
    int successCount = 0;

    // 辅助函数：更新状态到UI
    auto updateStatus = [&](const std::wstring& status) {
        *pStatus = status;
        UIBridge::Instance().UpdateHelperText(status);
    };

    // 辅助函数：投掷单次骰子并等待响应
    // 返回值：true=成功, false=失败
    auto throwOnceAndWait = [&]() -> bool {
        // 重置投掷响应标志
        {
            CriticalSectionLock lock(g_towerCS);
            g_towerThrowResponseReceived = false;
            g_towerLastThrowSuccess = false;
        }

        // 发送投掷骰子封包
        if (!SendThrowBonesPacket()) {
            return false;
        }

        // 等待 THROW_BONES 响应（最多3秒）
        int waitCount = 0;
        bool received = false;
        bool throwSuccess = false;
        
        while (waitCount < 30) {
            {
                CriticalSectionLock lock(g_towerCS);
                received = g_towerThrowResponseReceived;
                if (received) {
                    throwSuccess = g_towerLastThrowSuccess;
                }
            }
            if (received) break;
            Sleep(100);
            waitCount++;
        }

        return received && throwSuccess;
    };

    // 初始化状态
    {
        CriticalSectionLock lock(g_towerCS);
        g_towerDiceCount = 0;
        g_towerRemainingDice = 0;
        g_towerThrowResponseReceived = false;
        g_towerLastThrowSuccess = false;
        g_towerCheckInfoReceived = false;
        g_towerBuyBonesReceived = false;
        g_towerBuyBonesSuccess = false;
        g_towerBuyBonesNum = 0;
        g_towerPassFlag = 0;           // 重置通关标志
        g_towerIsCompleted = false;    // 重置完成标志
    }

    // ==================== 一键跑塔流程开始 ====================
    
    // 步骤1: 循环投掷骰子直到耗尽
    updateStatus(L"开始投掷骰子...");
    int totalAttempts = 0;
    int maxTotalAttempts = 100;  // 防止无限循环的安全限制
    
    while (totalAttempts < maxTotalAttempts) {
        // 1. 查询玄塔信息，获取当前骰子数量
        {
            CriticalSectionLock lock(g_towerCS);
            g_towerCheckInfoReceived = false;
        }
        SendTowerCheckInfoPacket();
        
        // 等待 CHECK_INFO 响应（最多3秒）
        int waitCount = 0;
        bool checkReceived = false;
        while (waitCount < 30) {
            {
                CriticalSectionLock lock(g_towerCS);
                checkReceived = g_towerCheckInfoReceived;
            }
            if (checkReceived) break;
            Sleep(100);
            waitCount++;
        }
        
        if (!checkReceived) {
            updateStatus(L"错误：查询骰子数量超时");
            success = false;
            goto cleanup;
        }
        
        // 2. 获取当前骰子数量和通关状态
        int currentDice = 0;
        int passFlag = 0;
        bool isCompleted = false;
        {
            CriticalSectionLock lock(g_towerCS);
            currentDice = g_towerRemainingDice;
            passFlag = g_towerPassFlag;
            isCompleted = g_towerIsCompleted;
        }
        
        // 3. 检查是否已通关（passflag==1 或之前已标记完成）
        if (passFlag == 1 || isCompleted) {
            updateStatus(L"玄塔已通关，停止投掷");
            break;
        }
        
        // 4. 如果骰子数量为0，退出循环
        if (currentDice <= 0) {
            updateStatus(L"骰子已耗尽，停止投掷");
            break;
        }
        
        // 4. 投掷一次骰子
        totalAttempts++;
        updateStatus(L"投掷骰子（剩余 " + std::to_wstring(currentDice) + L" 个）...");
        
        // 发送 THROW_BONES 并等待响应
        if (throwOnceAndWait()) {
            successCount++;
        }
        Sleep(50);
        
        // 检查是否通关（nodetype == 8）
        {
            CriticalSectionLock lock(g_towerCS);
            if (g_towerIsCompleted || g_towerLastThrowNodeType == 8) {
                updateStatus(L"已到达玄塔终点，通关！");
                // 发送 REACH_RESULT 后退出
                SendTowerResultPacket();
                Sleep(100);
                break;
            }
        }
        
        // 发送 REACH_RESULT
        SendTowerResultPacket();
        Sleep(100);
    }
    
    if (totalAttempts >= maxTotalAttempts) {
        updateStatus(L"警告：达到最大投掷次数限制（100次）");
    }
    
    updateStatus(L"投掷完成，成功 " + std::to_wstring(successCount) + L" 次");

    // 步骤2和3: 自动回家（如果勾选）
    if (g_autoGoHome.load()) {
        Sleep(200);

        // 步骤2: 退出场景回家
        updateStatus(L"步骤2: 退出场景回家...");
        if (!SendExitScenePacket(g_userId)) {
            updateStatus(L"错误：退出场景失败！");
            success = false;
            goto cleanup;
        }
        Sleep(800);

        // 步骤3: 重新进入玄塔地图（刷新活动状态）
        updateStatus(L"步骤3: 重新进入玄塔地图...");
        if (!SendReenterTowerMapPacket()) {
            updateStatus(L"错误：重新进入地图失败！");
            success = false;
            goto cleanup;
        }
        Sleep(800);
    }

cleanup:
    // 重置状态标志变量
    {
        CriticalSectionLock lock(g_towerCS);
        g_towerMapEntered = false;
        g_towerBattleStarted = false;
        g_towerResultReceived = false;
        g_towerThrowResponseReceived = false;
        g_towerCheckInfoReceived = false;
        g_towerBuyBonesReceived = false;
        g_towerAutoMode = false;
        g_towerPassFlag = 0;           // 重置通关标志
        g_towerIsCompleted = false;    // 重置完成标志
    }

    // 根据成功/失败状态更新 UI
    if (success) {
        updateStatus(L"✓ 一键玄塔完成！成功投掷 " + std::to_wstring(successCount) + L" 次");
    } else {
        updateStatus(L"✗ 一键玄塔失败，请查看日志");
    }

    return 0;
}

// 一键玄塔完整流程
BOOL StartOneKeyTowerPacket() {
    bool autoMode = false;
    {
        CriticalSectionLock lock(g_towerCS);
        autoMode = g_towerAutoMode;
    }

    if (autoMode) {
        UIBridge::Instance().UpdateHelperText(L"一键玄塔已在运行中，请勿重复点击");
        return FALSE;  // 已经在运行
    }

    if (g_userId == 0) {
        UIBridge::Instance().UpdateHelperText(L"错误：请先进入游戏获取卡布号");
        return FALSE;
    }

    // 初始化状态
    {
        CriticalSectionLock lock(g_towerCS);
        g_towerAutoMode = true;
        g_towerMapEntered = false;
        g_towerBattleStarted = false;
        g_towerResultReceived = false;
        g_towerDiceCount = 0;
        g_towerRemainingDice = 0;
        g_towerThrowResponseReceived = false;
        g_towerLastThrowSuccess = false;
        g_towerCheckInfoReceived = false;
        g_towerBuyBonesReceived = false;
        g_towerBuyBonesSuccess = false;
        g_towerBuyBonesNum = 0;
        g_towerPassFlag = 0;           // 重置通关标志
        g_towerIsCompleted = false;    // 重置完成标志
    }

    std::wstring* pStatus = new(std::nothrow) std::wstring(L"一键玄塔初始化...");
    if (!pStatus) {
        CriticalSectionLock lock(g_towerCS);
        g_towerAutoMode = false;
        return FALSE;
    }

    // 启动玄塔线程
    HANDLE hThread = CreateThread(nullptr, 0, TowerAutoThreadProc, pStatus, 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
        return TRUE;
    }

    delete pStatus;
    {
        CriticalSectionLock lock(g_towerCS);
        g_towerAutoMode = false;
    }
    return FALSE;
}

// 处理玄塔活动响应（在HookedRecv中调用）
void ProcessTowerActivityResponse(const GamePacket& packet) {
    // 处理地图进入响应（Opcode 1315395 = ENTER_SCENE_BACK）
    if (packet.opcode == Opcode::ENTER_SCENE_BACK) {
        CriticalSectionLock lock(g_towerCS);
        g_towerMapEntered = true;
    }

    // 处理战斗开始响应（Opcode 1317120 = BATTLE_START）
    if (packet.opcode == Opcode::BATTLE_START) {
        CriticalSectionLock lock(g_towerCS);
        g_towerBattleStarted = true;
    }

    // 处理玄塔活动响应（支持两个 Opcode：1185569 和 1324097 (TRIAL_BACK), Params = 341）
    if ((packet.opcode == 1185569 || packet.opcode == Opcode::TRIAL_BACK) && packet.params == 341) {
        if (packet.body.size() >= 4) {
            size_t offset = 0;
            int32_t operation = ReadInt32LE(packet.body.data(), offset);

            // CHECK_INFO 响应 (operation = 1)：查询玄塔信息
            // AS3 代码: mbody.nbones = body.readInt(); mbody.sbones = body.readInt(); ...
            // Body 结构：[operation:4][nbones:4][sbones:4][passflag:4][getMonsterTime:4][nowpos:4][buycount:4][noTip:4][nodeList:90*4]
            // 总大小：4 + 7*4 + 90*4 = 392 字节
            if (operation == 1 && packet.body.size() >= 392) {
                int32_t nbones = ReadInt32LE(packet.body.data(), offset);  // 普通骰子数量
                int32_t sbones = ReadInt32LE(packet.body.data(), offset);  // 特殊骰子数量
                int32_t passflag = ReadInt32LE(packet.body.data(), offset); // 通关标志（0=未通关，1=已通关）
                // 其余字段: getMonsterTime, nowpos, buycount, noTip, nodeList[90]

                CriticalSectionLock lock(g_towerCS);
                // 更新骰子数量（上限20）
                if (nbones > 20) nbones = 20;
                if (sbones > 20) sbones = 20;
                g_towerDiceCount = nbones;
                g_towerRemainingDice = nbones;
                g_towerCheckInfoNbones = nbones;
                g_towerPassFlag = passflag;  // 存储通关标志
                g_towerCheckInfoReceived = true;
            }
            // REACH_RESULT 响应 (operation = 4)：查询结果
            else if (operation == 4) {
                CriticalSectionLock lock(g_towerCS);
                g_towerResultReceived = true;
            }
            // UPDATE_BONES_NUM 响应 (operation = 9)：服务器通知更新骰子数量
            // AS3 代码: mbody.nbones = body.readInt(); mbody.sbones = body.readInt();
            // Body 结构：[operation:4][nbones:4][sbones:4]
            else if (operation == 9 && packet.body.size() >= 12) {
                int32_t nbones = ReadInt32LE(packet.body.data(), offset);
                int32_t sbones = ReadInt32LE(packet.body.data(), offset);

                CriticalSectionLock lock(g_towerCS);
                if (nbones > 20) nbones = 20;
                if (sbones > 20) sbones = 20;
                g_towerDiceCount = nbones;
                g_towerRemainingDice = nbones;
            }
            // BUY_BONES 响应 (operation = 6)：购买骰子结果
            // AS3 代码: mbody.result = body.readInt(); mbody.num = body.readInt(); mbody.buycount = body.readInt();
            // Body 结构：[operation:4][result:4][num:4][buycount:4]
            // 总大小：4 + 3*4 = 16 字节
            // result: 0=成功, -1=金币不足, -3=已达上限
            // num: 购买后的骰子数量
            // buycount: 今日购买次数
            else if (operation == 6 && packet.body.size() >= 16) {
                int32_t result = ReadInt32LE(packet.body.data(), offset);    // 结果 (0=成功)
                int32_t num = ReadInt32LE(packet.body.data(), offset);       // 骰子数量
                (void)ReadInt32LE(packet.body.data(), offset);               // buycount：占位字段

                CriticalSectionLock lock(g_towerCS);
                g_towerBuyBonesSuccess = (result == 0);
                g_towerBuyBonesNum = num;
                g_towerBuyBonesReceived = true;
                
                // 购买成功时更新骰子数量
                if (result == 0) {
                    if (num > 20) num = 20;
                    g_towerDiceCount = num;
                    g_towerRemainingDice = num;
                }
            }
            // THROW_BONES 响应 (operation = 2)：投掷骰子结果
            // AS3 代码: mbody.result = body.readInt(); mbody.step = body.readInt(); mbody.nodetype = body.readInt(); ...
            // Body 结构：[operation:4][result:4][step:4][nodetype:4][param1:4][param2:4][param3:4]
            // 总大小：4 + 6*4 = 28 字节
            // result: 0=成功, -1=失败
            // step: 投掷步数
            // nodetype: 节点类型 (8=通关终点)
            // param3: 随机骰子点数
            else if (operation == 2 && packet.body.size() >= 28) {
                int32_t result = ReadInt32LE(packet.body.data(), offset);    // 结果 (0=成功, -1=失败)
                int32_t step = ReadInt32LE(packet.body.data(), offset);      // 步数
                int32_t nodetype = ReadInt32LE(packet.body.data(), offset);  // 节点类型
                int32_t param1 = ReadInt32LE(packet.body.data(), offset);
                int32_t param2 = ReadInt32LE(packet.body.data(), offset);
                int32_t param3 = ReadInt32LE(packet.body.data(), offset);    // 随机骰子点数
                (void)param1;
                (void)param2;
                (void)param3;

                CriticalSectionLock lock(g_towerCS);
                // 记录投掷结果
                g_towerLastThrowSuccess = (result == 0);
                g_towerLastThrowStep = step;
                g_towerLastThrowNodeType = nodetype;
                g_towerThrowResponseReceived = true;
                
                // 检测是否通关（nodetype == 8 表示到达终点）
                if (nodetype == 8) {
                    g_towerIsCompleted = true;
                }
                
                // 投掷成功时减少剩余骰子数
                if (result == 0 && g_towerRemainingDice > 0) {
                    g_towerRemainingDice--;
                }
                g_towerResultReceived = true;
            }
        }
    }
}

// ============ 跳舞大赛功能实现 ============

// 进入地图（用于跳舞大赛）
// 封包格式: 44 53 00 00 39 12 12 00 [mapId:4字节] (12字节)
// Opcode: 1184313 (0x121239) 小端序 = 39 12 12 00
// Params: mapId 小端序写入
// 响应 Opcode: 1315395 = OP_CLIENT_REQ_ENTER_SCENE.back
BOOL SendEnterScenePacket(int mapId, uint32_t expectedOpcode, DWORD timeoutMs) {
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::ENTER_SCENE_SEND)
        .SetParams(static_cast<uint32_t>(mapId))
        .Build();

    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()), expectedOpcode, timeoutMs);
}

// ============ 跳舞大赛封包发送函数 ============

// 跳舞大赛 - 发送跳舞活动操作封包
// Opcode: 1187375 (0x121E2F)
// 小端序字节: 2F 1E 12 00
// params=3: 开始游戏, Body=[difficulty]
// params=4: 游戏过程, Body=[serverTime, processid, count, ...states]
// params=5: 结束游戏, Body=null
// params=6: 提交分数, Body=[7, serverScore]
BOOL SendDanceActivityPacketEx(int params, const std::vector<int32_t>& bodyValues) {
    auto packet = PacketBuilder()
        .SetOpcode(1187375)
        .SetParams(static_cast<uint32_t>(params))
        .WriteInt32Array(bodyValues)
        .Build();

    // 如果是开始游戏（params=3），不等待响应（让调用者处理等待）
    uint32_t expectedOpcode = 0;
    DWORD timeoutMs = 0;

    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()), expectedOpcode, timeoutMs);
}

// 跳舞大赛 - 发送阶段封包（进入/退出活动）
// Opcode: 1187368 (0x121E28)
// 小端序字节: 28 1E 12 00
// params=1: 进入活动, Body=[2]
// params=2: 退出活动, Body=[0xFFFFFFFF, userId]
BOOL SendDanceStagePacketEx(int params, const std::vector<int32_t>& bodyValues) {
    auto packet = PacketBuilder()
        .SetOpcode(1187368)
        .SetParams(static_cast<uint32_t>(params))
        .WriteInt32Array(bodyValues)
        .Build();
    
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 跳舞大赛 - 进入活动（发送进入封包）
// 封包: 44 53 04 00 28 1E 12 00 01 00 00 00 02 00 00 00
// Opcode: 1184808, Params=1, Body=[2]
BOOL SendDanceEnterPacket() {
    return SendDanceStagePacketEx(1, {2});
}

// 跳舞大赛 - 开始游戏
BOOL SendDanceStartPacket() {
    g_danceState.waitingResponse = true;
    g_danceState.gameState = 0;
    g_danceState.counter = 5;
    return SendDanceActivityPacketEx(3, {g_danceState.serverDifficulty});
}

// 跳舞大赛 - 游戏过程封包（参考易语言代码）
// 封包格式: 44 53 20 00 2F 1E 12 00 04 00 00 00 [时间戳][跳舞计数] 05 00 00 00 02 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 02 00 00 00 02 00 00 00
// Body: [serverTime, danceCounter, 5, 2, 2, 0, 0, 2, 2]
// state=2 表示 Great (150分)
BOOL SendDanceProcessPacketEx(int serverTime, int danceCounter) {
    std::vector<int32_t> body;
    body.push_back(serverTime);        // serverTime (使用当前时间戳)
    body.push_back(danceCounter);      // 跳舞计数
    body.push_back(5);                 // count = 5
    body.push_back(2);                 // state1 = Great
    body.push_back(2);                 // state2 = Great
    body.push_back(0);                 // state3
    body.push_back(0);                 // state4
    body.push_back(2);                 // state5 = Great
    body.push_back(2);                 // state6 = Great

    return SendDanceActivityPacketEx(4, body);
}

// 跳舞大赛 - 结束游戏
// 封包: 44 53 00 00 2F 1E 12 00 05 00 00 00
// Opcode: 1185839, Params=5
BOOL SendDanceEndPacketEx() {
    return SendDanceActivityPacketEx(5, {});
}

// 跳舞大赛 - 退出活动
// 封包: 44 53 08 00 28 1E 12 00 02 00 00 00 FF FF FF FF [userId]
// Opcode: 1184808, Params=2, Body=[0xFFFFFFFF, userId]
BOOL SendDanceExitPacketEx() {
    return SendDanceStagePacketEx(2, {static_cast<int32_t>(0xFFFFFFFF), static_cast<int32_t>(g_userId)});
}

// 跳舞大赛 - 提交分数
// 封包: 44 53 08 00 2F 1E 12 00 06 00 00 00 07 00 00 00 [serverScore]
// Opcode: 1185839, Params=6, Body=[7, serverScore]
BOOL SendDanceSubmitScorePacket(int serverScore) {
    return SendDanceActivityPacketEx(6, {7, serverScore});
}



// 处理跳舞大赛活动响应
void ProcessDanceActivityResponse(const GamePacket& packet) {
    if (packet.body.size() < 4) {
        return;
    }
    
    uint32_t state = packet.params;
    const BYTE* body = packet.body.data();
    size_t length = packet.body.size();
    size_t offset = 0;
    
    int32_t gameState = ReadInt32LE(body, offset);
    
    if (gameState == 0 || gameState == 2 || gameState == 6) {
        switch (state) {
            case 3:  // 开始游戏响应
                if (offset + 16 <= length) {
                    g_danceState.serverDifficulty = ReadInt32LE(body, offset);
                    g_danceState.serverTime = ReadInt32LE(body, offset);
                    g_danceState.processId = ReadInt32LE(body, offset);
                    g_danceState.clothNum = ReadInt32LE(body, offset);
                    
                    g_danceState.gameState = 2;
                    g_danceState.waitingResponse = false;
                }
                break;
                
            case 4:  // 游戏过程响应
                if (offset + 16 <= length) {
                    offset += 4;  // serverTime
                    int32_t respProcessId = ReadInt32LE(body, offset);
                    offset += 8;  // processId, serverCombo
                    g_danceState.serverScore = ReadInt32LE(body, offset);
                    
                    if (respProcessId >= g_danceState.processId) {
                        g_danceState.processId = respProcessId;
                    }
                    g_danceState.waitingResponse = false;
                }
                break;
                
            case 5:  // 结束游戏响应
                // AS3: serverTime, processid, serverCombo, serverScore, serverExp, todayRewardExpCnt, todayRewardExp, drawRewardCnt
                if (offset + 32 <= length) {
                    offset += 12;  // serverTime(4), processid(4), serverCombo(4)
                    g_danceState.serverScore = ReadInt32LE(body, offset);
                    offset += 8;  // serverScore(4), serverExp(4)
                    g_danceState.todayRewardCnt = ReadInt32LE(body, offset);
                    offset += 8;  // todayRewardExpCnt(4), todayRewardExp(4)
                    g_danceState.drawRewardCnt = ReadInt32LE(body, offset);
                    
                    g_danceState.remainCnt = WpeHook::DANCE_MAX_DAILY_COUNT - g_danceState.todayRewardCnt;
                    if (g_danceState.remainCnt < 0) g_danceState.remainCnt = 0;
                    
                    if (g_ExecuteScriptFunc) {
                        wchar_t script[256];
                        swprintf(script, 256, 
                            L"if(window.updateDanceCount) window.updateDanceCount(%d);",
                            g_danceState.todayRewardCnt);
                        g_ExecuteScriptFunc(script);
                    }
                }
                g_danceState.gameState = 0;
                g_danceState.waitingResponse = false;
                break;
        }
    }
    else if (gameState == 5 || gameState == 3) {
        // AS3：5=“位置已被其它小卡布占用”，3=“网络不太稳定,请重新进入游戏吧”。
        // 二者都是本轮可重试的瞬时失败，不应当作整体失败中断后续次数。
        g_danceState.abnormalRound = true;
        g_danceState.lastFailReason = DANCE_FAIL_POSITION_TAKEN;
        g_danceState.gameState = 0;
        g_danceState.waitingResponse = false;
    }
    else {
        // 其它异常 gameState（AS3 走 Alert("异常"+gameState)）同样按本轮失败处理。
        g_danceState.abnormalRound = true;
        if (g_danceState.lastFailReason == DANCE_FAIL_NONE) {
            g_danceState.lastFailReason = DANCE_FAIL_START_TIMEOUT;
        }
        g_danceState.gameState = 0;
        g_danceState.waitingResponse = false;
    }
}

// 处理跳舞大赛阶段响应 (opcode 1318440)
void ProcessDanceStageResponse(const GamePacket& packet) {
    // 读取 params (mParams)
    uint32_t params = packet.params;
    
    // params == 777 时直接返回
    if (params == 777) return;
    
    // 根据params处理不同阶段
    // params == 0: 初始列表
    // params == 1: 选择物品
    // params == 2: 确认
}

// 跳舞大赛线程函数声明
DWORD WINAPI DanceContestThreadProc(LPVOID lpParam);

// 跳舞大赛单次执行（内部函数）
static BOOL SendDanceContestOnce() {
    // 初始化跳舞计数和状态
    g_danceState.counter = 5;
    g_danceState.gameState = 0;
    g_danceState.waitingResponse = false;
    g_danceState.enteredMap = false;
    g_danceState.abnormalRound = false;
    g_danceState.lastFailReason = DANCE_FAIL_NONE;

    // 1. 循环发送进入地图封包（最多10次）
    int enterCount = 0;
    while (!g_danceState.enteredMap && enterCount < 10) {
        if (SendEnterScenePacket(1028)) {
            // 成功发送并等待响应
            if (g_danceState.enteredMap) {
                break;  // 已进入地图
            }
            Sleep(800);
        }
        // 失败或未进入地图，继续重试
        enterCount++;
    }

    if (!g_danceState.enteredMap) {
        g_danceState.lastFailReason = DANCE_FAIL_ENTER_MAP;
        return FALSE;
    }

    Sleep(300);

    // 2. 发送进入活动封包
    SendDanceEnterPacket();
    Sleep(300);

    // 3. 发送开始游戏封包（不等待响应）
    g_danceState.waitingResponse = true;
    g_danceState.gameState = 0;  // 确保状态重置
    if (!SendDanceStartPacket()) {
        // 发送封包失败
        g_danceState.waitingResponse = false;
        g_danceState.lastFailReason = DANCE_FAIL_START_SEND;
        return FALSE;
    }

    // 4. 等待gameState被设置为2（游戏中），最多等待15秒。
    //    若响应已把本轮判为异常（位置被占/网络不稳），立即结束等待转入重试，
    //    避免白等满 15 秒。
    for (int i = 0; i < 150 && g_danceState.gameState != 2 && !g_danceState.abnormalRound; i++) {
        Sleep(100);
    }

    // 5. 如果状态变为2（游戏中），启动跳舞线程并等待完成
    if (g_danceState.gameState == 2) {
        HANDLE hThread = CreateThread(NULL, 0, DanceContestThreadProc, NULL, 0, NULL);
        if (hThread) {
            const DWORD waitResult = WaitForSingleObject(hThread, 30000);
            CloseHandle(hThread);
            if (waitResult == WAIT_TIMEOUT) {
                g_danceState.lastFailReason = DANCE_FAIL_ROUND_TIMEOUT;
                return FALSE;
            }
            return TRUE;
        }
        g_danceState.lastFailReason = DANCE_FAIL_THREAD_CREATE;
        return FALSE;
    }

    // 未进入游戏中：若响应处理器标记了异常（位置被占/网络不稳），沿用其原因，
    // 否则说明服务端压根没回应，按开局超时处理。
    if (g_danceState.lastFailReason == DANCE_FAIL_NONE) {
        g_danceState.lastFailReason = DANCE_FAIL_START_TIMEOUT;
    }
    return FALSE;
}

// 跳舞大赛主流程（自动执行3次，允许瞬时失败重试）
BOOL SendDanceContestPacket() {
    const int maxRounds = WpeHook::DANCE_MAX_DAILY_COUNT;
    // 每次理论上最多重试 3 次，避免活动不可用时无限循环。
    const int maxAttempts = maxRounds + 3;

    int completedCount = 0;

    // 辅助函数：通过 UIBridge 更新UI计数
    auto updateDanceCountUI = [](int count) {
        std::wstring jsCode = L"if(window.updateDanceCount) window.updateDanceCount(" 
            + std::to_wstring(count) + L");";
        UIBridge::Instance().ExecuteJS(jsCode);
    };

    // 辅助函数：回显本轮失败原因，便于用户判断是位置被占还是网络问题。
    auto reportFailure = [](int reason, int attempt) {
        std::wstring msg = L"跳舞大赛：第 " + std::to_wstring(attempt) +
            L" 次尝试失败（" + DanceFailReasonText(reason) + L"），正在重试...";
        UIBridge::Instance().UpdateHelperText(msg);
    };

    // 初始化UI显示为0
    updateDanceCountUI(0);

    int attempts = 0;
    while (completedCount < maxRounds && attempts < maxAttempts) {
        attempts++;

        if (SendDanceContestOnce()) {
            completedCount++;

            // 每次完成后更新UI计数
            updateDanceCountUI(completedCount);

            // 等待一小段时间确保UI更新
            Sleep(300);
            continue;
        }

        // 本轮失败不中断整批，退出活动后重试。仅「本轮线程超时」例外：此时
        // 跳舞线程可能仍在运行并继续发包，重入会与之并发，故直接结束整批。
        if (g_danceState.lastFailReason == DANCE_FAIL_ROUND_TIMEOUT) {
            UIBridge::Instance().UpdateHelperText(
                L"跳舞大赛：本轮超时，线程状态未清干净，已停止以避免冲突");
            break;
        }

        reportFailure(g_danceState.lastFailReason, attempts);
        SendDanceExitPacketEx();
        Sleep(1200);
    }

    // 最终确保UI显示正确
    updateDanceCountUI(completedCount);

    if (completedCount < maxRounds) {
        std::wstring msg = L"跳舞大赛：完成 " + std::to_wstring(completedCount) +
            L"/" + std::to_wstring(maxRounds) + L" 次，部分尝试未成功";
        UIBridge::Instance().UpdateHelperText(msg);
    }

    return completedCount > 0;
}
// 跳舞大赛线程函数
DWORD WINAPI DanceContestThreadProc(LPVOID lpParam) {
    (void)lpParam;
    // 跳舞计数从5开始，每次+5，直到超过100
    while (g_danceState.counter <= 100) {
        Sleep(220);
        
        int serverTime = static_cast<int>(time(nullptr));
        SendDanceProcessPacketEx(serverTime, g_danceState.counter);
        
        g_danceState.counter += 5;
    }
    
    Sleep(220);
    
    // 发送结束封包两次
    SendDanceEndPacketEx();
    Sleep(220);
    SendDanceEndPacketEx();
    Sleep(220);
    
    // 发送退出封包两次
    SendDanceExitPacketEx();
    Sleep(220);
    SendDanceExitPacketEx();
    Sleep(220);
    
    // 发送提交分数封包
    SendDanceSubmitScorePacket(g_danceState.serverScore);
    
    Sleep(500);
    
    if (g_ExecuteScriptFunc) {
        g_ExecuteScriptFunc(L"if(window.onDanceComplete) window.onDanceComplete();");
    }
    
    // 重置状态，为下一次执行做准备
    g_danceState.gameState = 0;
    g_danceState.waitingResponse = false;
    return 0;
}

// 日常活动任务数据结构
struct DailyTaskData {
    DWORD flags;
    HWND hwnd;
};

// 一键完成日常活动的线程函数
DWORD WINAPI DailyTaskThreadProc(LPVOID lpParam) {
    DailyTaskData* taskData = static_cast<DailyTaskData*>(lpParam);
    DWORD flags = taskData->flags;
    
    int completedCount = 0;
    int totalCount = 0;
    
    // 计算总数（支持12个活动）
    for (int i = 0; i < 12; i++) {
        if (flags & (1 << i)) totalCount++;
    }
    
    // 按顺序执行
    if (flags & 0x01) {  // 深度挖宝
        g_deepDigState.autoMode = true;
        g_deepDigState.completedCount = 0;
        SendQueryDeepDigCountPacket();
        
        // 执行剩余次数
        int execCount = g_deepDigState.remainingCount > 0 ?
                        g_deepDigState.remainingCount : WpeHook::DEEP_DIG_DEFAULT_COUNT;
        for (int i = 0; i < execCount; i++) {
            g_deepDigState.waitingResponse = true;
            g_deepDigState.sessionId = 0;

            // 发送开始游戏封包
            BYTE startPacket[36] = {
                0x44, 0x53, 0x18, 0x00,
                0x15, 0x14, 0x12, 0x00,
                0x0C, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00
            };
            SendPacket(0, startPacket, sizeof(startPacket), Opcode::DEEP_DIG_BACK, 5000);
            
            Sleep(500);
            
            // 发送结束游戏封包
            if (g_deepDigState.sessionId > 0) {
                BYTE endPacket[36] = {
                    0x44, 0x53, 0x18, 0x00,
                    0x15, 0x14, 0x12, 0x00,
                    0x0C, 0x00, 0x00, 0x00,
                    0x04, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00,
                    0x04, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00
                };
                endPacket[24] = static_cast<BYTE>(g_deepDigState.sessionId & 0xFF);
                endPacket[25] = static_cast<BYTE>((g_deepDigState.sessionId >> 8) & 0xFF);
                endPacket[26] = static_cast<BYTE>((g_deepDigState.sessionId >> 16) & 0xFF);
                endPacket[27] = static_cast<BYTE>((g_deepDigState.sessionId >> 24) & 0xFF);
                SendPacket(0, endPacket, sizeof(endPacket));
            }
            
            Sleep(800);
        }
        
        g_deepDigState.autoMode = false;
        completedCount++;
        Sleep(300);
    }
    if (flags & 0x02) {  // 每日卡牌
        SendDailyCardPacket();
        completedCount++;
        Sleep(300);
    }
    if (flags & 0x04) {  // 每日礼包
        SendDailyGiftPacket();
        completedCount++;
        Sleep(300);
    }
    if (flags & 0x08) {  // 每周礼包
        SendWeeklyGiftPacket();
        completedCount++;
        Sleep(300);
    }
    if (flags & 0x10) {  // 家族考勤
        SendFamilyCheckinPacket();
        completedCount++;
        Sleep(300);
    }
    if (flags & 0x20) {  // 家族报道
        SendFamilyReportPacket();
        completedCount++;
        Sleep(300);
    }
    if (flags & 0x40) {  // 家族保卫
        SendFamilyDefendPacket();
        completedCount++;
        Sleep(300);
    }
    if (flags & 0x80) {  // 商城惊喜
        SendShopSurprisePacket();
        completedCount++;
    }
    if (flags & 0x100) {  // 跳舞大赛
        SendDanceContestPacket();
        completedCount++;
        Sleep(300);
    }
    if (flags & 0x200) {  // 火风试炼
        SendFireWindTrialPacket();
        completedCount++;
        Sleep(300);
    }
    if (flags & 0x400) {  // 火焰试炼
        SendFireTrialPacket();
        completedCount++;
        Sleep(300);
    }
    if (flags & 0x800) {  // 风暴试炼
        SendStormTrialPacket();
        completedCount++;
    }
    
    // 发送完成消息
    if (g_hWnd) {
        PostMessage(g_hWnd, AppMessage::kDailyTaskComplete, completedCount, totalCount);
    }
    
    delete taskData;
    return 0;
}

// ============================================================================
// 封包标签化功能
// ============================================================================

/**
 * @brief Opcode 到标签的映射
 */
// ============================================================================
// 劫持功能实现
// ============================================================================

BOOL AddHijackRule(HijackType type, bool forSend, bool forRecv, 
                   const std::string& searchHex, const std::string& replaceHex) {
    if (type == HIJACK_NONE || searchHex.empty()) {
        return FALSE;
    }
    
    HijackRule rule;
    rule.type = type;
    rule.forSend = forSend;
    rule.forRecv = forRecv;
    rule.searchHex = searchHex;
    rule.replaceHex = replaceHex;
    
    CriticalSectionLock lock(g_hijackRulesCS.Get());
    g_HijackRules.push_back(rule);
    
    return TRUE;
}

VOID ClearHijackRules() {
    CriticalSectionLock lock(g_hijackRulesCS.Get());
    g_HijackRules.clear();
}

VOID SetHijackEnabled(bool enable) {
    g_bHijackEnabled = enable;
}

bool ProcessHijack(bool bSend, const BYTE* pData, DWORD* pdwSize, std::vector<BYTE>* pModifiedData) {
    if (!g_bHijackEnabled || !pData || !pdwSize) {
        return false;
    }
    
    CriticalSectionLock lock(g_hijackRulesCS.Get());
    
    for (const auto& rule : g_HijackRules) {
        // 检查规则是否适用于当前封包
        if (bSend && !rule.forSend) continue;
        if (!bSend && !rule.forRecv) continue;
        
        // 将搜索字符串转换为字节数组
        std::vector<BYTE> searchBytes = StringToHex(rule.searchHex);
        if (searchBytes.empty()) continue;
        
        // 在封包中搜索匹配
        std::vector<BYTE> dataBytes(pData, pData + *pdwSize);
        bool found = false;
        
        // 简单搜索：检查搜索字符串是否是封包的子串
        for (size_t i = 0; i + searchBytes.size() <= dataBytes.size(); i++) {
            bool match = true;
            for (size_t j = 0; j < searchBytes.size(); j++) {
                if (dataBytes[i + j] != searchBytes[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                found = true;
                break;
            }
        }
        
        if (found) {
            if (rule.type == HIJACK_BLOCK) {
                // 拦截：返回空封包
                *pdwSize = 0;
                return true;
            } else if (rule.type == HIJACK_REPLACE) {
                // 替换：替换整个封包或部分内容
                if (pModifiedData && !rule.replaceHex.empty()) {
                    std::vector<BYTE> replaceBytes = StringToHex(rule.replaceHex);
                    if (!replaceBytes.empty()) {
                        // 简单实现：替换整个封包
                        *pModifiedData = replaceBytes;
                        *pdwSize = (DWORD)replaceBytes.size();
                        return true;
                    }
                }
            }
        }
    }
    
    return false;
}

// ============================================================================
// 自动发送功能
// ============================================================================

DWORD SendAllPackets(DWORD intervalMs, DWORD loopCount, 
                     PacketProgressCallback progressCallback) {
    if (loopCount < 1) loopCount = 1;
    
    DWORD totalSent = 0;
    DWORD currentLoop = 0;
    
    // 复制发送包列表（避免锁住太长时间）
    std::vector<std::vector<BYTE>> sendPackets;
    std::vector<std::string> packetLabels;
    
    {
        CriticalSectionLock lock(g_packetListCS.Get());
        
        for (const auto& packet : g_PacketList) {
            if (packet.bSend && packet.dwSize > 0) {
                std::vector<BYTE> data(packet.pData, packet.pData + packet.dwSize);
                sendPackets.push_back(data);
                
                // 获取封包标签
                std::string label = "";
                if (packet.dwSize >= PacketProtocol::HEADER_SIZE) {
                    size_t offset = 0;
                    uint16_t magic = ReadUInt16LE(packet.pData, offset);  // Magic: offset 0-1
                    (void)ReadUInt16LE(packet.pData, offset);             // Length: offset 2-3
                    if (magic == PacketProtocol::MAGIC_NORMAL || magic == PacketProtocol::MAGIC_COMPRESSED) {
                        uint32_t opcode = ReadUInt32LE(packet.pData, offset);  // Opcode: offset 4-7
                        label = GetPacketLabel(opcode, true);
                    }
                }
                packetLabels.push_back(label);
            }
        }
    }
    
    // 循环发送
    g_bStopAutoSend = false;
    for (DWORD loop = 0; loop < loopCount && !g_bStopAutoSend; ++loop) {
        currentLoop = loop + 1;
        
        for (size_t i = 0; i < sendPackets.size() && !g_bStopAutoSend; ++i) {
            // 调用进度回调
            if (progressCallback) {
                progressCallback(currentLoop, (DWORD)i + 1, packetLabels[i]);
            }
            
            // 复制封包数据（用于可能的修改）
            std::vector<BYTE> packetData = sendPackets[i];
            
            // 检查是否为战斗封包 (OP_CLIENT_CLICK_NPC = 1186048)
            // 封包结构: 12字节头部 + 4字节Body (counter)
            if (packetData.size() >= 16) {
                size_t offset = 0;
                uint16_t magic = ReadUInt16LE(packetData.data(), offset);  // Magic: offset 0-1
                (void)ReadUInt16LE(packetData.data(), offset);             // Length: offset 2-3
                
                if (magic == PacketProtocol::MAGIC_NORMAL || magic == PacketProtocol::MAGIC_COMPRESSED) {
                    uint32_t opcode = ReadUInt32LE(packetData.data(), offset);  // Opcode: offset 4-7
                    
                    if (opcode == Opcode::CLICK_NPC) {  // 1186048 - 发起战斗
                        // 获取当前 counter 值（从全局变量）
                        uint32_t currentCounter = g_battleCounter.load();
                        if (currentCounter == 0) {
                            currentCounter = 1;
                        }
                        
                        // 更新 Body 中的 counter（偏移12-15，小端序）
                        packetData[12] = static_cast<BYTE>(currentCounter & 0xFF);
                        packetData[13] = static_cast<BYTE>((currentCounter >> 8) & 0xFF);
                        packetData[14] = static_cast<BYTE>((currentCounter >> 16) & 0xFF);
                        packetData[15] = static_cast<BYTE>((currentCounter >> 24) & 0xFF);
                        
                        // 可选：更新进度回调显示counter已更新
                        if (progressCallback) {
                            char msg[256];
                            sprintf_s(msg, "[Counter已更新: %u] %s", currentCounter, packetLabels[i].c_str());
                            progressCallback(currentLoop, (DWORD)i + 1, msg);
                        }
                    }
                }
            }
            
            if (SendPacket(0, packetData.data(), (DWORD)packetData.size())) {
                totalSent++;
            }
            
            if (intervalMs > 0) {
                Sleep(intervalMs);
            }
        }
    }
    
    return totalSent;
}

VOID StopAutoSend() {
    g_bStopAutoSend = true;
}

// 封包文本文件格式：每行一个包，带方向标记。
//   [SEND] xx xx xx ...   客户端发出的包
//   [RECV] xx xx xx ...   服务端下发的包
// 以 # 或 // 开头的行是注释；没有标记的裸十六进制行按发送包兼容处理（旧格式）。
static const char* kPacketSendTag = "[SEND] ";
static const char* kPacketRecvTag = "[RECV] ";

namespace {

// 标记比较不区分大小写：手工编辑过的 [Send]/[Recv] 也要能认。
bool PacketTagEquals(const std::string& tag, const char* expected) {
    size_t i = 0;
    for (; i < tag.size() && expected[i] != '\0'; ++i) {
        if (std::toupper(static_cast<unsigned char>(tag[i])) !=
            std::toupper(static_cast<unsigned char>(expected[i]))) {
            return false;
        }
    }
    return i == tag.size() && expected[i] == '\0';
}

}  // namespace

BOOL SavePacketListToFile(const std::wstring& filePath) {
    std::ofstream file(filePath, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        return FALSE;
    }

    std::vector<PACKET> packetsCopy;
    {
        CriticalSectionLock lock(g_packetListCS.Get());
        packetsCopy = g_PacketList;
    }

    file << "# [SEND] = 客户端发出, [RECV] = 服务端下发; 载入时只取 [SEND]" << std::endl;

    // 保存全部被拦截的封包（发送 + 接收），逐行标记方向，便于对着时间线读收发成对关系。
    for (const auto& packet : packetsCopy) {
        if (packet.pData == nullptr || packet.dwSize == 0) {
            continue;
        }
        file << (packet.bSend ? kPacketSendTag : kPacketRecvTag)
             << HexToString(packet.pData, packet.dwSize) << std::endl;
    }

    file.close();
    return TRUE;
}

BOOL LoadPacketListFromFile(const std::wstring& filePath) {
    std::ifstream file(filePath, std::ios::in);
    if (!file.is_open()) {
        return FALSE;
    }

    // 先清空现有封包列表
    ClearPacketList();

    std::string line;
    while (std::getline(file, line)) {
        // 移除空白字符
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        if (!line.empty()) {
            line.erase(line.find_last_not_of(" \t\r\n") + 1);
        }
        if (line.empty()) {
            continue;
        }
        // 注释行（# 或 //）：保存时可能写入的说明不入列表
        if (line[0] == '#' || (line.size() >= 2 && line[0] == '/' && line[1] == '/')) {
            continue;
        }

        // 解析方向标记；没有标记的裸十六进制行按发送包处理（兼容旧文件）。
        bool isSend = true;
        if (line[0] == '[') {
            const size_t close = line.find(']');
            if (close != std::string::npos) {
                const std::string tag = line.substr(1, close - 1);
                isSend = PacketTagEquals(tag, "SEND") || PacketTagEquals(tag, "S");
                line = line.substr(close + 1);
                line.erase(0, line.find_first_not_of(" \t"));
            }
        }

        // 只载入发送包：载入是给「自动发送」用的，接收包没有可发的语义。
        if (!isSend) {
            continue;
        }

        std::vector<BYTE> data = StringToHex(line);
        if (!data.empty()) {
            // 添加到封包列表
            PACKET packet;
            packet.dwSize = static_cast<DWORD>(data.size());
            packet.bSend = TRUE;
            packet.pData = new BYTE[data.size()];
            memcpy(packet.pData, data.data(), data.size());
            packet.dwTime = GetTickCount();

            {
                CriticalSectionLock lock(g_packetListCS.Get());
                g_PacketList.push_back(packet);
            }
        }
    }

    file.close();
    return TRUE;
}

// 一键完成所有选中的日常活动
void StartDailyTasksAsync(DWORD flags) {
    if (flags == 0) return;
    
    DailyTaskData* taskData = new DailyTaskData();
    taskData->flags = flags;
    taskData->hwnd = g_hWnd;
    
    HANDLE hThread = CreateThread(NULL, 0, DailyTaskThreadProc, taskData, 0, NULL);
    if (hThread) {
        CloseHandle(hThread);
    } else {
        delete taskData;
    }
}

// TASK_TALK_BACK 分发链：八卦灵盘与主线任务共用一个 opcode，两者都要收到回包。
static void ProcessStoryTaskTalkResponse(const GamePacket& packet) {
    ProcessEightTrigramsTaskTalkResponse(packet);
    StoryTaskOnDialogResponse(packet);
}

static void ProcessStoryNpcTaskListResponse(const GamePacket& packet) {
    StoryTaskOnNpcTaskListResponse(packet);
}

static void ProcessStoryNpcListResponse(const GamePacket& packet) {
    StoryTaskOnNpcListResponse(packet);
}

static void ProcessStoryMagicIndexResponse(const GamePacket& packet) {
    StoryTaskOnMagicIndexResponse(packet);
}

static void ProcessStoryTaskFinishedResponse(const GamePacket& packet) {
    StoryTaskOnTaskFinishedResponse(packet);
}

// ============================================================================
// 坐骑大赛实现已迁移到 src/hook/horse_competition.cpp.
// ============================================================================


SOCKET GetLastGameSocket() {
    return g_LastGameSocket.load();
}
