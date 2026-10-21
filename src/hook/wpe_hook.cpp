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

// 摘取大力果实状态 (Act782)
static std::atomic<int> g_act782PlayCount{0};
static std::atomic<int> g_act782RestTime{0};
static std::atomic<int> g_act782BubbleNum{0};
static std::atomic<int> g_act782RuleFlag{0};
static std::atomic<int> g_act782CheckCode{0};
static std::atomic<int> g_act782RandomCode{0};
static std::atomic<int> g_act782LastResult{-1};
static std::atomic<int> g_act782LastScore{0};
static std::atomic<int> g_act782LastMedal{0};
static std::atomic<int> g_act782LastExp{0};
static std::atomic<int> g_act782LastCoin{0};
static std::atomic<bool> g_act782WaitingResponse{false};
static std::atomic<bool> g_act782UseSweep{false};
static std::atomic<bool> g_act782SweepAvailable{false};

// 果宝救援状态 (Act757)
static std::atomic<int> g_act757PlayCount{0};
static std::atomic<int> g_act757RestTime{0};
static std::atomic<int> g_act757TotalBadgeNum{0};
static std::atomic<int> g_act757BuyCnt{0};
static std::atomic<int> g_act757PassCount{0};
static std::atomic<int> g_act757IsPop{0};
static std::atomic<int> g_act757HistoryBestScore{0};
static std::atomic<int> g_act757LastScore{0};
static std::atomic<int> g_act757PassFlag{0};
static std::atomic<int> g_act757CatchId{0};
static std::atomic<int> g_act757CheckCode{0};
static std::atomic<int> g_act757StartResult{-1};
static std::atomic<int> g_act757EndResult{-1};
static std::atomic<int> g_act757SweepResult{-1};
static std::atomic<bool> g_act757WaitingResponse{false};
static std::atomic<bool> g_act757UseSweep{false};
static std::atomic<bool> g_act757SweepAvailable{false};
static std::mutex g_act757Mutex;
static std::vector<int> g_act757PassAwards;
static std::vector<std::pair<int, int>> g_act757CatchList;
static std::vector<std::pair<int, int>> g_act757SweepAwards;
static std::vector<std::pair<int, int>> g_act757ResultAwards;

// 光辉穿梭大考验状态 (Act822)
static std::atomic<int> g_act822PlayCount{0};
static std::atomic<int> g_act822RestTime{0};
static std::atomic<int> g_act822TotalBadgeNum{0};
static std::atomic<int> g_act822BuyCnt{0};
static std::atomic<int> g_act822PassCount{0};
static std::atomic<int> g_act822IsPop{0};
static std::atomic<int> g_act822HistoryBestScore{0};
static std::atomic<int> g_act822LastScore{0};
static std::atomic<int> g_act822PassFlag{0};
static std::atomic<int> g_act822CatchId{0};
static std::atomic<int> g_act822CheckCode{0};
static std::atomic<int> g_act822StartResult{-1};
static std::atomic<int> g_act822EndResult{-1};
static std::atomic<int> g_act822SweepResult{-1};
static std::atomic<bool> g_act822WaitingResponse{false};
static std::atomic<bool> g_act822UseSweep{false};
static std::atomic<bool> g_act822SweepAvailable{false};
static std::mutex g_act822Mutex;
static std::vector<int> g_act822PassAwards;
static std::vector<std::pair<int, int>> g_act822CatchList;
static std::vector<std::pair<int, int>> g_act822SweepAwards;
static std::vector<std::pair<int, int>> g_act822ResultAwards;

// 逆流的试炼状态 (Act804)
static std::atomic<int> g_act804PlayCount{0};
static std::atomic<int> g_act804RestTime{0};
static std::atomic<int> g_act804MoonPowerNum{0};
static std::atomic<int> g_act804MooncakeNum{0};
static std::atomic<int> g_act804EvolveFlag1{0};
static std::atomic<int> g_act804PromptFlag{1};
static std::atomic<int> g_act804BuyNum{0};
static std::atomic<int> g_act804LastResult{-1};
static std::atomic<bool> g_act804WaitingResponse{false};
static std::atomic<bool> g_act804UseSweep{false};
static std::atomic<bool> g_act804SweepAvailable{false};
static std::mutex g_act804Mutex;
static std::vector<int> g_act804StartAwardList;

// 我是神射手状态 (Act811)
static std::atomic<int> g_act811PlayCount{0};
static std::atomic<int> g_act811RestTime{0};
static std::atomic<int> g_act811PropNum{0};
static std::atomic<int> g_act811HintFlag{0};
static std::atomic<int> g_act811LastResult{-1};
static std::atomic<bool> g_act811WaitingResponse{false};
static std::atomic<bool> g_act811UseSweep{false};
static std::atomic<bool> g_act811SweepAvailable{false};
static std::vector<int> g_act811AwardList;

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

// 福瑞宝箱 owner：运行态和统计状态统一由这一组维护。
std::atomic<bool> g_heavenFuruiRunning{false};
std::atomic<bool> g_heavenFuruiQuerySuccess{false};
std::atomic<int> g_heavenFuruiBoxCount{0};
std::atomic<bool> g_heavenFuruiEnteredMap{false};
std::atomic<int> g_heavenFuruiMaxBoxes{30};  ///< 最大开启宝箱数量
std::atomic<int> g_heavenFuruiOpenedBoxes{0};  ///< 已开启宝箱数量




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

// ============ 航海大挑战功能实现 (Act685) ============

// ============================================================================
// 扫荡自动判定（所有活动共用）
//
// 扫荡会重放服务端的「历史最好成绩」，游戏流程会提交 flowScore（本工具认定的
// 目标分）。仅当可重放的历史最好成绩 bestScore >= flowScore，即扫荡不比自己
// 打一遍差时才自动扫荡；否则自己走一遍游戏流程分数更高，走游戏流程。
//
// bestScore <= 0 视为没有可重放成绩（服务端无可扫荡记录），不扫荡。
// 是否真的允许扫荡由各活动后续的 sweep_info 响应再次校验，此处无需重复判断，
// 否则像 826 这样 open_ui 不回传可扫荡标记的活动会被误判为永远不可扫荡。
// ============================================================================
bool ShouldAutoSweep(int bestScore, int flowScore) {
    if (bestScore <= 0) {
        return false;
    }
    return bestScore >= flowScore;
}

#define ACT685_STATE ActivityStateManager::Instance().GetAct685State()

BOOL SendAct685Packet(const std::string& operation, const std::vector<int32_t>& bodyValues) {
    return SendActivityPacket(Act685::ACTIVITY_ID, operation, bodyValues);
}

BOOL SendAct685OpenUIPacket() {
    ACT685_STATE.waitingResponse = true;
    return SendAct685Packet("open_ui", {});
}

BOOL SendAct685StartGamePacket(int ruleFlag) {
    ACT685_STATE.waitingResponse = true;
    return SendAct685Packet("start_game", {ruleFlag});
}

BOOL SendAct685EndGamePacket(int score) {
    const int checkCode = ACT685_STATE.checkCode.load();
    if (checkCode == 0) {
        return FALSE;
    }

    const uint32_t userId = g_userId.load();
    const int64_t clientCheckCode =
        static_cast<int64_t>(checkCode) * static_cast<int64_t>(userId % 100u + 1u) +
        static_cast<int64_t>(score);

    ACT685_STATE.waitingResponse = true;
    return SendAct685Packet("end_game", {static_cast<int32_t>(clientCheckCode), score});
}

BOOL SendAct685SweepInfoPacket() {
    ACT685_STATE.waitingResponse = true;
    return SendAct685Packet("sweep_info", {});
}

BOOL SendAct685SweepPacket() {
    ACT685_STATE.waitingResponse = true;
    return SendAct685Packet("sweep", {});
}

DWORD WINAPI Act685ThreadProc(LPVOID lpParam) {
    (void)lpParam;

    const bool useSweep = ACT685_STATE.useSweep.load();
    ACT685_STATE.Reset();
    ACT685_STATE.useSweep = useSweep;

    auto waitForResponse = []() -> bool {
        for (int i = 0; i < 30 && ACT685_STATE.waitingResponse; ++i) {
            Sleep(100);
        }
        const bool received = !ACT685_STATE.waitingResponse.load();
        ACT685_STATE.waitingResponse = false;
        return received;
    };

    Sleep(300);
    UIBridge::Instance().UpdateHelperText(L"航海大挑战：正在获取活动信息...");

    SendAct685OpenUIPacket();
    if (!waitForResponse()) {
        UIBridge::Instance().UpdateHelperText(L"航海大挑战：获取活动信息失败");
        return 0;
    }

    if (ACT685_STATE.playCount.load() <= 0) {
        UIBridge::Instance().UpdateHelperText(L"航海大挑战：今日次数已用完");
        return 0;
    }

    if (ACT685_STATE.restTime.load() > 0) {
        UIBridge::Instance().UpdateHelperText(L"航海大挑战：冷却中，请稍后再试");
        return 0;
    }

    // 自动判定：可扫荡且扫荡成绩 >= 游戏流程成绩时才扫荡
    const bool autoSweep = ShouldAutoSweep(
        ACT685_STATE.maxScore.load(),
        Act685::MAX_SCORE);
    if (autoSweep) {
        Sleep(300);
        UIBridge::Instance().UpdateHelperText(L"航海大挑战：正在获取扫荡信息...");

        SendAct685SweepInfoPacket();
        if (waitForResponse() && ACT685_STATE.sweepAvailable.load()) {
            Sleep(300);
            UIBridge::Instance().UpdateHelperText(L"航海大挑战：执行扫荡...");
            ACT685_STATE.endResult = -1;
            SendAct685SweepPacket();
            if (waitForResponse() && ACT685_STATE.endResult.load() != 3) {
                Sleep(300);
                SendAct685OpenUIPacket();
                waitForResponse();
                UIBridge::Instance().UpdateHelperText(L"航海大挑战：扫荡完成");
                return 0;
            }
        }

        UIBridge::Instance().UpdateHelperText(L"航海大挑战：扫荡失败，改为直接完成");
    }

    Sleep(300);
    UIBridge::Instance().UpdateHelperText(L"航海大挑战：开始游戏...");

    int ruleFlag = ACT685_STATE.ruleFlag.load();
    if (ruleFlag < 1) {
        ruleFlag = 1;
    }

    ACT685_STATE.ruleFlag = ruleFlag;
    SendAct685StartGamePacket(ruleFlag);
    if (!waitForResponse()) {
        UIBridge::Instance().UpdateHelperText(L"航海大挑战：开始游戏失败");
        return 0;
    }

    const int startResult = ACT685_STATE.startResult.load();
    if (startResult != 0) {
        if (startResult == 10) {
            UIBridge::Instance().UpdateHelperText(L"航海大挑战：当前已经在游戏中");
        } else if (startResult == 11) {
            UIBridge::Instance().UpdateHelperText(L"航海大挑战：冷却中，请稍后再试");
        } else if (startResult == 12) {
            UIBridge::Instance().UpdateHelperText(L"航海大挑战：剩余次数不足");
        } else if (startResult == 13) {
            UIBridge::Instance().UpdateHelperText(L"航海大挑战：玩家正在支付中");
        } else {
            UIBridge::Instance().UpdateHelperText(L"航海大挑战：开始游戏失败");
        }
        return 0;
    }

    if (ACT685_STATE.checkCode.load() == 0) {
        UIBridge::Instance().UpdateHelperText(L"航海大挑战：获取校验码失败");
        return 0;
    }

    Sleep(500);
    UIBridge::Instance().UpdateHelperText(L"航海大挑战：直接提交结算...");
    SendAct685EndGamePacket(Act685::MAX_SCORE);
    if (!waitForResponse()) {
        UIBridge::Instance().UpdateHelperText(L"航海大挑战：结算失败");
        return 0;
    }

    if (ACT685_STATE.endResult.load() == 3) {
        UIBridge::Instance().UpdateHelperText(L"航海大挑战：结算失败");
        return 0;
    }

    Sleep(300);
    SendAct685OpenUIPacket();
    waitForResponse();
    UIBridge::Instance().UpdateHelperText(L"航海大挑战：结算完成");
    return 0;
}

BOOL StartOneKeyAct685Packet(bool useSweep) {
    ACT685_STATE.useSweep = useSweep;
    HANDLE hThread = CreateThread(nullptr, 0, Act685ThreadProc, nullptr, 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
        return TRUE;
    }
    return FALSE;
}

void ProcessAct685Response(const GamePacket& packet) {
    size_t offset = 0;
    std::string operation;
    if (!ReadPacketString(packet.body.data(), packet.body.size(), offset, operation)) {
        return;
    }

    const BYTE* body = packet.body.data();
    ACT685_STATE.waitingResponse = false;

    if (operation == "open_ui") {
        if (offset + 60 <= packet.body.size()) {
            ACT685_STATE.playCount = ReadInt32LE(body, offset);
            ACT685_STATE.restTime = ReadInt32LE(body, offset);
            ACT685_STATE.bubbleNum = ReadInt32LE(body, offset);
            ACT685_STATE.ruleFlag = ReadInt32LE(body, offset);
            ACT685_STATE.maxScore = ReadInt32LE(body, offset);
            ACT685_STATE.passCount = ReadInt32LE(body, offset);
            ACT685_STATE.passBonusFlag = ReadInt32LE(body, offset);

            if (ACT685_STATE.catchList.size() < 2) {
                ACT685_STATE.catchList.assign(2, 0);
            }
            ACT685_STATE.catchList[0] = ReadInt32LE(body, offset);
            ACT685_STATE.catchList[1] = ReadInt32LE(body, offset);

            if (ACT685_STATE.passBonusList.size() < 3) {
                ACT685_STATE.passBonusList.assign(3, std::pair<int, int>{0, 0});
            }
            for (size_t i = 0; i < 3; ++i) {
                ACT685_STATE.passBonusList[i].first = ReadInt32LE(body, offset);
                ACT685_STATE.passBonusList[i].second = ReadInt32LE(body, offset);
            }

            ACT685_STATE.sweepAvailable = (ACT685_STATE.passCount.load() > 0);

            wchar_t msg[256];
            swprintf_s(
                msg,
                L"航海大挑战：次数=%d 冷却=%d秒 海魂之力=%d",
                ACT685_STATE.playCount.load(),
                ACT685_STATE.restTime.load(),
                ACT685_STATE.bubbleNum.load());
            UIBridge::Instance().UpdateHelperText(msg);
        }
    } else if (operation == "start_game") {
        if (offset + 4 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            ACT685_STATE.startResult = result;
            if (result == 0 && offset + 8 <= packet.body.size()) {
                ReadInt32LE(body, offset);  // skip
                ACT685_STATE.checkCode = ReadInt32LE(body, offset);
            } else if (result == 10) {
                UIBridge::Instance().UpdateHelperText(L"航海大挑战：当前已经在游戏中");
            } else if (result == 11) {
                UIBridge::Instance().UpdateHelperText(L"航海大挑战：冷却中，请稍后再试");
            } else if (result == 12) {
                UIBridge::Instance().UpdateHelperText(L"航海大挑战：剩余次数不足");
            } else if (result == 13) {
                UIBridge::Instance().UpdateHelperText(L"航海大挑战：玩家正在支付中");
            }
        }
    } else if (operation == "sweep_info") {
        if (offset + 16 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            if (result == 0) {
                std::vector<int> rewardList;
                rewardList.reserve(3);
                for (int i = 0; i < 3; ++i) {
                    rewardList.push_back(ReadInt32LE(body, offset));
                }

                ACT685_STATE.rewardBubbleNum = rewardList[0];
                ACT685_STATE.rewardCoin = rewardList[1];
                ACT685_STATE.rewardExp = rewardList[2];
                ACT685_STATE.sweepAvailable = true;

                wchar_t msg[256];
                swprintf_s(
                    msg,
                    L"航海大挑战：扫荡预览，海魂之力=%d 铜钱=%d 历练=%d",
                    rewardList[0],
                    rewardList[1],
                    rewardList[2]);
                UIBridge::Instance().UpdateHelperText(msg);
            }
        }
    } else if (operation == "end_game" || operation == "sweep") {
        if (offset + 4 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            ACT685_STATE.endResult = result;
            if (offset + 12 <= packet.body.size()) {
                ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
            }

            if (result != 3 && offset + 12 <= packet.body.size()) {
                if (ACT685_STATE.propList.size() < 3) {
                    ACT685_STATE.propList.assign(3, 0);
                }
                for (size_t i = 0; i < 3; ++i) {
                    ACT685_STATE.propList[i] = ReadInt32LE(body, offset);
                }

                ACT685_STATE.rewardBubbleNum = ACT685_STATE.propList[0];
                ACT685_STATE.rewardCoin = ACT685_STATE.propList[1];
                ACT685_STATE.rewardExp = ACT685_STATE.propList[2];

                wchar_t msg[256];
                if (operation == "sweep") {
                    swprintf_s(
                        msg,
                        L"航海大挑战：扫荡完成，海魂之力=%d 铜钱=%d 历练=%d",
                        ACT685_STATE.rewardBubbleNum.load(),
                        ACT685_STATE.rewardCoin.load(),
                        ACT685_STATE.rewardExp.load());
                } else {
                    swprintf_s(
                        msg,
                        L"航海大挑战：结算完成，海魂之力=%d 铜钱=%d 历练=%d",
                        ACT685_STATE.rewardBubbleNum.load(),
                        ACT685_STATE.rewardCoin.load(),
                        ACT685_STATE.rewardExp.load());
                }
                UIBridge::Instance().UpdateHelperText(msg);
            }
        }
    }
}

// ============ 天之骄子的特训功能实现 (Act666) ============

#define ACT666_STATE ActivityStateManager::Instance().GetAct666State()

BOOL SendAct666Packet(const std::string& operation, const std::vector<int32_t>& bodyValues) {
    return SendActivityPacket(Act666::ACTIVITY_ID, operation, bodyValues);
}

BOOL SendAct666OpenUIPacket() {
    ACT666_STATE.waitingResponse = true;
    return SendAct666Packet("open_ui", {});
}

BOOL SendAct666StartGamePacket(int isPropt, int type) {
    ACT666_STATE.waitingResponse = true;
    return SendAct666Packet("start_game", {isPropt, type});
}

BOOL SendAct666EndGamePacket(int medalCount, bool isPass) {
    int clientCheckCode = 0;
    const int checkCode = ACT666_STATE.checkCode.load();
    if (checkCode != 0) {
        clientCheckCode = checkCode & static_cast<int>(g_userId.load() % static_cast<uint32_t>(checkCode));
    }
    ACT666_STATE.waitingResponse = true;
    const BOOL sent = SendAct666Packet("end_game", {medalCount, clientCheckCode, isPass ? 1 : 0});
    if (sent) {
        // 与 AS3 KongKongModel.decodeEndGame 末尾的 sendDayTask() 一致。
        SendActivityDailyTaskReport();
    }
    return sent;
}

BOOL SendAct666SweepInfoPacket() {
    ACT666_STATE.waitingResponse = true;
    return SendAct666Packet("sweep_info", {});
}

BOOL SendAct666SweepPacket() {
    ACT666_STATE.waitingResponse = true;
    return SendAct666Packet("sweep", {});
}

DWORD WINAPI Act666ThreadProc(LPVOID lpParam) {
    (void)lpParam;

    const bool useSweep = ACT666_STATE.useSweep.load();
    ACT666_STATE.Reset();
    ACT666_STATE.useSweep = useSweep;

    auto waitForResponse = []() -> bool {
        for (int i = 0; i < 30 && ACT666_STATE.waitingResponse; ++i) {
            Sleep(100);
        }
        const bool received = !ACT666_STATE.waitingResponse.load();
        ACT666_STATE.waitingResponse = false;
        return received;
    };

    Sleep(300);
    UIBridge::Instance().UpdateHelperText(L"天之骄子的特训：正在获取活动信息...");

    SendAct666OpenUIPacket();
    if (!waitForResponse()) {
        UIBridge::Instance().UpdateHelperText(L"天之骄子的特训：获取活动信息失败");
        return 0;
    }

    if (ACT666_STATE.playCount.load() <= 0) {
        UIBridge::Instance().UpdateHelperText(L"天之骄子的特训：今日次数已用完");
        return 0;
    }

    if (ACT666_STATE.restTime.load() > 0) {
        UIBridge::Instance().UpdateHelperText(L"天之骄子的特训：冷却中，请稍后再试");
        return 0;
    }

    if (useSweep && ACT666_STATE.bestFlag.load() > 0) {
        UIBridge::Instance().UpdateHelperText(L"天之骄子的特训：正在获取扫荡信息...");

        Sleep(300);
        SendAct666SweepInfoPacket();
        if (waitForResponse()) {
            Sleep(300);
            SendAct666SweepPacket();
            if (waitForResponse()) {
                Sleep(300);
                SendAct666OpenUIPacket();
                waitForResponse();
                UIBridge::Instance().UpdateHelperText(L"天之骄子的特训：扫荡完成");
                return 0;
            }
        }

        UIBridge::Instance().UpdateHelperText(L"天之骄子的特训：扫荡失败，改为直接完成");
    }

    Sleep(300);
    UIBridge::Instance().UpdateHelperText(L"天之骄子的特训：开始游戏...");
    SendAct666StartGamePacket(0, 0);
    if (!waitForResponse()) {
        UIBridge::Instance().UpdateHelperText(L"天之骄子的特训：开始游戏失败");
        return 0;
    }

    const int startResult = ACT666_STATE.startResult.load();
    if (startResult != 0) {
        if (startResult != 10 && startResult != 11 && startResult != 12 && startResult != 13) {
            UIBridge::Instance().UpdateHelperText(L"天之骄子的特训：开始游戏失败");
        }
        return 0;
    }

    if (ACT666_STATE.checkCode.load() == 0) {
        UIBridge::Instance().UpdateHelperText(L"天之骄子的特训：获取校验码失败");
        return 0;
    }

    Sleep(500);
    UIBridge::Instance().UpdateHelperText(L"天之骄子的特训：直接提交结算...");
    SendAct666EndGamePacket(1, true);
    if (!waitForResponse()) {
        UIBridge::Instance().UpdateHelperText(L"天之骄子的特训：结算失败");
        return 0;
    }

    Sleep(300);
    SendAct666OpenUIPacket();
    waitForResponse();
    UIBridge::Instance().UpdateHelperText(L"天之骄子的特训：结算完成");
    return 0;
}

BOOL StartOneKeyAct666Packet(bool useSweep) {
    ACT666_STATE.useSweep = useSweep;
    HANDLE hThread = CreateThread(nullptr, 0, Act666ThreadProc, nullptr, 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
        return TRUE;
    }
    return FALSE;
}

void ProcessAct666Response(const GamePacket& packet) {
    size_t offset = 0;
    std::string operation;
    if (!ReadPacketString(packet.body.data(), packet.body.size(), offset, operation)) {
        return;
    }

    const BYTE* body = packet.body.data();
    ACT666_STATE.waitingResponse = false;

    if (operation == "open_ui") {
        if (offset + 52 <= packet.body.size()) {
            ACT666_STATE.playCount = ReadInt32LE(body, offset);
            ACT666_STATE.restTime = ReadInt32LE(body, offset);
            ACT666_STATE.medalCount = ReadInt32LE(body, offset);
            ACT666_STATE.passCount = ReadInt32LE(body, offset);
            ACT666_STATE.getAwardFlag = ReadInt32LE(body, offset);
            ACT666_STATE.bestFlag = ReadInt32LE(body, offset);
            if (ACT666_STATE.rewardList.size() < 4) {
                ACT666_STATE.rewardList.assign(4, 0);
            }
            for (size_t i = 0; i < 4; ++i) {
                ACT666_STATE.rewardList[i] = ReadInt32LE(body, offset);
            }
            ACT666_STATE.catchId = ReadInt32LE(body, offset);
            if (ACT666_STATE.catchList.size() < 2) {
                ACT666_STATE.catchList.assign(2, 0);
            }
            ACT666_STATE.catchList[0] = ReadInt32LE(body, offset);
            ACT666_STATE.catchList[1] = ReadInt32LE(body, offset);
            ACT666_STATE.sweepAvailable = (ACT666_STATE.bestFlag.load() > 0);

            wchar_t msg[256];
            swprintf_s(
                msg,
                L"天之骄子的特训：次数=%d 冷却=%d秒 勋章=%d",
                ACT666_STATE.playCount.load(),
                ACT666_STATE.restTime.load(),
                ACT666_STATE.medalCount.load());
            UIBridge::Instance().UpdateHelperText(msg);
        }
    } else if (operation == "start_game") {
        if (offset + 4 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            ACT666_STATE.startResult = result;
            if (result == 0 && offset + 8 <= packet.body.size()) {
                ACT666_STATE.playCount = ReadInt32LE(body, offset);
                ACT666_STATE.checkCode = ReadInt32LE(body, offset);
            } else if (result == 10) {
                UIBridge::Instance().UpdateHelperText(L"天之骄子的特训：游戏中");
            } else if (result == 11) {
                UIBridge::Instance().UpdateHelperText(L"天之骄子的特训：冷却中，请稍后再试");
            } else if (result == 12) {
                UIBridge::Instance().UpdateHelperText(L"天之骄子的特训：今日次数已用完");
            } else if (result == 13) {
                UIBridge::Instance().UpdateHelperText(L"天之骄子的特训：玩家正在支付中");
            }
        }
    } else if (operation == "end_game") {
        if (offset + 32 <= packet.body.size()) {
            ReadInt32LE(body, offset);  // type / result marker
            ACT666_STATE.playCount = ReadInt32LE(body, offset);
            ACT666_STATE.restTime = ReadInt32LE(body, offset);
            ReadInt32LE(body, offset);  // score
            ACT666_STATE.bestFlag = ReadInt32LE(body, offset);
            ACT666_STATE.passCount = ReadInt32LE(body, offset);
            ACT666_STATE.rewardMedalCount = ReadInt32LE(body, offset);
            ACT666_STATE.rewardExp = ReadInt32LE(body, offset);
            ACT666_STATE.rewardCoin = ReadInt32LE(body, offset);
            ACT666_STATE.sweepAvailable = (ACT666_STATE.bestFlag.load() > 0);

            wchar_t msg[256];
            swprintf_s(
                msg,
                L"天之骄子的特训：结算完成，勋章=%d 经验=%d 铜钱=%d",
                ACT666_STATE.rewardMedalCount.load(),
                ACT666_STATE.rewardExp.load(),
                ACT666_STATE.rewardCoin.load());
            UIBridge::Instance().UpdateHelperText(msg);
        }
    } else if (operation == "sweep_info") {
        if (offset + 12 <= packet.body.size()) {
            ACT666_STATE.rewardMedalCount = ReadInt32LE(body, offset);
            ACT666_STATE.rewardExp = ReadInt32LE(body, offset);
            ACT666_STATE.rewardCoin = ReadInt32LE(body, offset);
            ACT666_STATE.sweepAvailable = true;

            wchar_t msg[256];
            swprintf_s(
                msg,
                L"天之骄子的特训：扫荡预览，勋章=%d 经验=%d 铜钱=%d",
                ACT666_STATE.rewardMedalCount.load(),
                ACT666_STATE.rewardExp.load(),
                ACT666_STATE.rewardCoin.load());
            UIBridge::Instance().UpdateHelperText(msg);
        }
    } else if (operation == "sweep") {
        if (offset + 24 <= packet.body.size()) {
            ReadInt32LE(body, offset);  // type / result marker
            ACT666_STATE.playCount = ReadInt32LE(body, offset);
            ACT666_STATE.restTime = ReadInt32LE(body, offset);
            ACT666_STATE.rewardMedalCount = ReadInt32LE(body, offset);
            ACT666_STATE.rewardExp = ReadInt32LE(body, offset);
            ACT666_STATE.rewardCoin = ReadInt32LE(body, offset);
            ACT666_STATE.sweepAvailable = (ACT666_STATE.bestFlag.load() > 0);

            wchar_t msg[256];
            swprintf_s(
                msg,
                L"天之骄子的特训：扫荡完成，勋章=%d 经验=%d 铜钱=%d",
                ACT666_STATE.rewardMedalCount.load(),
                ACT666_STATE.rewardExp.load(),
                ACT666_STATE.rewardCoin.load());
            UIBridge::Instance().UpdateHelperText(msg);
        }
    }
}

// ============ 光辉穿梭大考验功能实现 (Act822) ============

#define ACT822_STATE ActivityStateManager::Instance().GetAct822State()

static BOOL SendAct822DailyTaskPacket() {
    std::vector<BYTE> packet = PacketBuilder()
                                   .SetOpcode(Opcode::TASK_DAILY_REPORT_SEND)
                                   .SetParams(3)
                                   .WriteInt32(3)
                                   .WriteInt32(Act822::DAILY_TASK_ID)
                                   .WriteInt32(0)
                                   .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

BOOL SendAct822Packet(const std::string& operation, const std::vector<int32_t>& bodyValues) {
    return SendActivityPacket(Act822::ACTIVITY_ID, operation, bodyValues);
}

BOOL SendAct822OpenUIPacket() {
    ACT822_STATE.waitingResponse = true;
    return SendAct822Packet("open_ui", {});
}

BOOL SendAct822StartGamePacket(int ruleFlag) {
    (void)ruleFlag;
    ACT822_STATE.waitingResponse = true;
    return SendAct822Packet("start_game", {});
}

BOOL SendAct822EndGamePacket(int score, bool isPass) {
    const int checkCode = ACT822_STATE.checkCode.load();
    if (checkCode == 0) {
        return FALSE;
    }

    const uint32_t userId = g_userId.load();
    const int clientCheckCode = checkCode & static_cast<int>(userId % static_cast<uint32_t>(checkCode));
    std::vector<int32_t> bodyValues;
    bodyValues.reserve(10);
    bodyValues.push_back(clientCheckCode);
    bodyValues.push_back(score);
    bodyValues.push_back(isPass ? 1 : 0);
    // AS3 会把 scoreArr 的前 7 项附在结算包后面，最后第 8 项是总分。
    // 当前 C++ 侧不复刻完整的逐项统计流程，先按相同长度补 7 个占位值，保持协议结构一致。
    bodyValues.insert(bodyValues.end(), 7, 0);
    ACT822_STATE.waitingResponse = true;
    const BOOL sent = SendAct822Packet("end_game", bodyValues);
    if (sent) {
        SendAct822DailyTaskPacket();
    }
    return sent;
}

BOOL SendAct822SweepInfoPacket() {
    ACT822_STATE.waitingResponse = true;
    return SendAct822Packet("sweep_info", {});
}

BOOL SendAct822SweepPacket() {
    ACT822_STATE.waitingResponse = true;
    return SendAct822Packet("sweep", {});
}

static void ResetAct822State() {
    g_act822PlayCount = 0;
    g_act822RestTime = 0;
    g_act822TotalBadgeNum = 0;
    g_act822BuyCnt = 0;
    g_act822PassCount = 0;
    g_act822IsPop = 0;
    g_act822HistoryBestScore = 0;
    g_act822LastScore = 0;
    g_act822PassFlag = 0;
    g_act822CatchId = 0;
    g_act822CheckCode = 0;
    g_act822StartResult = -1;
    g_act822EndResult = -1;
    g_act822SweepResult = -1;
    g_act822WaitingResponse = false;
    g_act822UseSweep = false;
    g_act822SweepAvailable = false;

    std::lock_guard<std::mutex> lock(g_act822Mutex);
    g_act822PassAwards.assign(3, 0);
    g_act822CatchList.assign(1, {0, 0});
    g_act822SweepAwards.clear();
    g_act822ResultAwards.clear();
}

static bool WaitForAct822Response() {
    // 发包侧置位的是 ACT822_STATE.waitingResponse，响应处理器清除的也是它；
    // 原先此处轮询另一独立全局 g_act822WaitingResponse（从未被置 true），
    // 导致本函数恒返回 true，一键流程每次都读到未填充的状态而立即退出。
    for (int i = 0; i < 30 && ACT822_STATE.waitingResponse.load(); ++i) {
        Sleep(100);
    }
    const bool received = !ACT822_STATE.waitingResponse.load();
    ACT822_STATE.waitingResponse = false;
    g_act822WaitingResponse = false;
    return received;
}

DWORD WINAPI Act822ThreadProc(LPVOID lpParam) {
    const bool useSweep = lpParam ? *static_cast<bool*>(lpParam) : false;
    delete static_cast<bool*>(lpParam);

    ACT822_STATE.Reset();
    ResetAct822State();
    ACT822_STATE.useSweep = useSweep;
    g_act822UseSweep = useSweep;

    Sleep(300);
    UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：正在获取活动信息...");

    SendAct822OpenUIPacket();
    if (!WaitForAct822Response()) {
        UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：获取活动信息失败");
        return 0;
    }

    if (g_act822PlayCount.load() <= 0) {
        UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：今日次数已用完");
        return 0;
    }

    if (g_act822RestTime.load() > 0) {
        UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：冷却中，请稍后再试");
        return 0;
    }

    // 自动判定：可扫荡且扫荡成绩 >= 游戏流程成绩时才扫荡
    if (ShouldAutoSweep(g_act822HistoryBestScore.load(),
                        Act822::PASS_SCORE)) {
        Sleep(300);
        UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：正在获取扫荡信息...");
        SendAct822SweepInfoPacket();
        if (WaitForAct822Response() && g_act822SweepAvailable.load()) {
            Sleep(300);
            UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：执行扫荡...");
            SendAct822SweepPacket();
            // 必须在等待之后读取结果：原先在 WaitForAct822Response() 之前快照，
            // 拿到的是上一次（或初值 -1）的陈旧值。
            const bool sweepWaited = WaitForAct822Response();
            const int sweepResult = g_act822SweepResult.load();
            if (sweepWaited && (sweepResult == 0 || sweepResult == 4)) {
                Sleep(300);
                SendAct822OpenUIPacket();
                WaitForAct822Response();
                std::vector<std::pair<int, int>> resultAwards;
                {
                    std::lock_guard<std::mutex> lock(g_act822Mutex);
                    resultAwards = g_act822ResultAwards;
                }
                const int medal = FindAwardAmount(resultAwards, 0);
                const int exp = FindAwardAmount(resultAwards, 202);
                const int coin = FindAwardAmount(resultAwards, 201);
                wchar_t msg[256];
                swprintf_s(
                    msg,
                    L"光辉穿梭大考验：扫荡完成，勋章=%d 经验=%d 铜钱=%d",
                    medal,
                    exp,
                    coin);
                UIBridge::Instance().UpdateHelperText(msg);
                return 0;
            }
        }

        UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：扫荡失败，改为直接完成");
    }

    Sleep(300);
    UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：开始游戏...");
    SendAct822StartGamePacket(g_act822IsPop.load());
    if (!WaitForAct822Response()) {
        UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：开始游戏失败");
        return 0;
    }

    const int startResult = g_act822StartResult.load();
    if (startResult != 0) {
        if (startResult == 10) {
            UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：游戏中！");
        } else if (startResult == 11) {
            UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：在冷却");
        } else if (startResult == 12) {
            UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：没次数了");
        } else if (startResult == 13) {
            UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：玩家正在支付中");
        } else {
            UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：开始游戏失败");
        }
        return 0;
    }

    if (g_act822CheckCode.load() == 0) {
        UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：获取校验码失败");
        return 0;
    }

    const int finalScore = Act822::PASS_SCORE;
    g_act822LastScore = finalScore;
    ACT822_STATE.lastScore = finalScore;
    Sleep(500);
    UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：直接提交结算...");
    SendAct822EndGamePacket(finalScore, true);
    if (!WaitForAct822Response()) {
        UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：结算失败");
        return 0;
    }

    if (g_act822EndResult.load() == 3) {
        UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：结算失败");
        return 0;
    }

    Sleep(300);
    SendAct822OpenUIPacket();
    WaitForAct822Response();

    std::vector<std::pair<int, int>> resultAwards;
    {
        std::lock_guard<std::mutex> lock(g_act822Mutex);
        resultAwards = g_act822ResultAwards;
    }

    wchar_t msg[256];
    swprintf_s(
        msg,
        L"光辉穿梭大考验：结算完成，分数=%d 勋章=%d 经验=%d 铜钱=%d",
        g_act822LastScore.load(),
        FindAwardAmount(resultAwards, 0),
        FindAwardAmount(resultAwards, 202),
        FindAwardAmount(resultAwards, 201));
    UIBridge::Instance().UpdateHelperText(msg);
    return 0;
}

BOOL StartOneKeyAct822Packet(bool useSweep, int targetScore) {
    (void)targetScore;
    ACT822_STATE.useSweep = useSweep;
    bool* pUseSweep = new bool(useSweep);
    HANDLE hThread = CreateThread(nullptr, 0, Act822ThreadProc, pUseSweep, 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
        return TRUE;
    }
    delete pUseSweep;
    return FALSE;
}

void ProcessAct822Response(const GamePacket& packet) {
    size_t offset = 0;
    std::string operation;
    if (!ReadPacketString(packet.body.data(), packet.body.size(), offset, operation)) {
        return;
    }

    const BYTE* body = packet.body.data();
    g_act822WaitingResponse = false;
    ACT822_STATE.waitingResponse = false;

    if (operation == "open_ui") {
        if (offset + 40 <= packet.body.size()) {
            g_act822PlayCount = ReadInt32LE(body, offset);
            g_act822RestTime = ReadInt32LE(body, offset);
            g_act822BuyCnt = ReadInt32LE(body, offset);
            g_act822TotalBadgeNum = ReadInt32LE(body, offset);
            g_act822PassCount = ReadInt32LE(body, offset);
            g_act822IsPop = ReadInt32LE(body, offset);
            g_act822HistoryBestScore = ReadInt32LE(body, offset);
            g_act822CatchId = ReadInt32LE(body, offset);

            std::vector<std::pair<int, int>> catchList;
            catchList.emplace_back(ReadInt32LE(body, offset), ReadInt32LE(body, offset));

            ACT822_STATE.playCount = g_act822PlayCount.load();
            ACT822_STATE.restTime = g_act822RestTime.load();
            ACT822_STATE.buyCnt = g_act822BuyCnt.load();
            ACT822_STATE.totalBadgeNum = g_act822TotalBadgeNum.load();
            ACT822_STATE.passCount = g_act822PassCount.load();
            ACT822_STATE.isPop = g_act822IsPop.load();
            ACT822_STATE.historyBestScore = g_act822HistoryBestScore.load();
            ACT822_STATE.catchId = g_act822CatchId.load();
            ACT822_STATE.catchList = catchList;
            ACT822_STATE.sweepAvailable = (g_act822HistoryBestScore.load() > 0);
            g_act822SweepAvailable = ACT822_STATE.sweepAvailable.load();

            wchar_t msg[256];
            swprintf_s(
                msg,
                L"光辉穿梭大考验：次数=%d 冷却=%d秒 勋章=%d",
                g_act822PlayCount.load(),
                g_act822RestTime.load(),
                g_act822TotalBadgeNum.load());
            UIBridge::Instance().UpdateHelperText(msg);
        }
    } else if (operation == "start_game") {
        if (offset + 16 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            g_act822StartResult = result;
            ACT822_STATE.startResult = result;
            if (result == 0) {
                g_act822PlayCount = ReadInt32LE(body, offset);
                g_act822CheckCode = ReadInt32LE(body, offset);
                const int awardNum = ReadInt32LE(body, offset);
                for (int i = 0; i < awardNum && offset + 4 <= packet.body.size(); ++i) {
                    ReadInt32LE(body, offset);
                }
                ACT822_STATE.playCount = g_act822PlayCount.load();
                ACT822_STATE.checkCode = g_act822CheckCode.load();
            } else if (result == 10) {
                UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：游戏中！");
            } else if (result == 11) {
                UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：在冷却");
            } else if (result == 12) {
                UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：没次数了");
            } else if (result == 13) {
                UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：玩家正在支付中");
            } else {
                UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：开始游戏失败");
            }
        }
    } else if (operation == "end_game") {
        if (offset + 36 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            const int playCount = ReadInt32LE(body, offset);
            const int coolTimeLeft = ReadInt32LE(body, offset);
            (void)ReadInt32LE(body, offset);  // invite：占位字段，仅需推进 offset
            const int passGmaeAward = ReadInt32LE(body, offset);
            const int medal = ReadInt32LE(body, offset);
            int exp = ReadInt32LE(body, offset);
            const int coin = ReadInt32LE(body, offset);
            const int itemNum = ReadInt32LE(body, offset);

            std::vector<std::pair<int, int>> rewards;
            for (int i = 0; i < itemNum && offset + 8 <= packet.body.size(); ++i) {
                const int id = ReadInt32LE(body, offset);
                const int num = ReadInt32LE(body, offset);
                if (num > 0) {
                    rewards.emplace_back(id, num);
                }
            }

            if (passGmaeAward > 0) {
                exp -= Act822::PASS_EXP;
            }

            std::vector<std::pair<int, int>> awardList;
            BuildAct822AwardList(medal, exp, coin, rewards, awardList);
            {
                std::lock_guard<std::mutex> lock(g_act822Mutex);
                g_act822ResultAwards = awardList;
            }

            g_act822EndResult = result;
            g_act822PlayCount = playCount;
            g_act822RestTime = coolTimeLeft;
            g_act822PassFlag = passGmaeAward;

            ACT822_STATE.endResult = result;
            ACT822_STATE.playCount = g_act822PlayCount.load();
            ACT822_STATE.restTime = g_act822RestTime.load();
            ACT822_STATE.passFlag = g_act822PassFlag.load();
            ACT822_STATE.resultAwards = awardList;
            ACT822_STATE.sweepSuccess = (result == 0);

            if (result == 0) {
                wchar_t msg[256];
                swprintf_s(
                    msg,
                    L"光辉穿梭大考验：结算完成，分数=%d 勋章=%d 经验=%d 铜钱=%d",
                    g_act822LastScore.load(),
                    FindAwardAmount(awardList, 0),
                    FindAwardAmount(awardList, 202),
                    FindAwardAmount(awardList, 201));
                UIBridge::Instance().UpdateHelperText(msg);
            } else {
                UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：结算失败");
            }
        }
    } else if (operation == "sweep_info") {
        if (offset + 28 <= packet.body.size()) {
            const int resultType = ReadInt32LE(body, offset);
            (void)ReadInt32LE(body, offset);  // invite：占位字段，仅需推进 offset
            const int passGmaeAward = ReadInt32LE(body, offset);
            const int medal = ReadInt32LE(body, offset);
            int exp = ReadInt32LE(body, offset);
            const int coin = ReadInt32LE(body, offset);
            const int itemNum = ReadInt32LE(body, offset);

            std::vector<std::pair<int, int>> rewards;
            for (int i = 0; i < itemNum && offset + 8 <= packet.body.size(); ++i) {
                const int id = ReadInt32LE(body, offset);
                const int num = ReadInt32LE(body, offset);
                if (num > 0) {
                    rewards.emplace_back(id, num);
                }
            }

            if (resultType == 1) {
                UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：需要成功进行一局游戏才可以扫荡哦！");
            } else if (resultType == 2) {
                UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：游戏次数不足");
            } else if (resultType == 3) {
                UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：冷却中！");
            } else {
                std::vector<std::pair<int, int>> awardList;
                awardList.emplace_back(0, medal);
                awardList.emplace_back(202, exp);
                awardList.emplace_back(201, coin);
                if (resultType == 4) {
                    awardList.emplace_back(203, Act822::PASS_XIUWEI);
                }
                for (const auto& reward : rewards) {
                    if (reward.second > 0) {
                        awardList.emplace_back(reward.first, reward.second);
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(g_act822Mutex);
                    g_act822SweepAwards = awardList;
                }

                g_act822SweepResult = resultType;
                g_act822PassFlag = passGmaeAward;
                g_act822SweepAvailable = (resultType == 0 || resultType == 4);

                ACT822_STATE.sweepResult = resultType;
                ACT822_STATE.passFlag = g_act822PassFlag.load();
                ACT822_STATE.sweepAwards = awardList;
                ACT822_STATE.sweepAvailable = g_act822SweepAvailable.load();
                UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：已获取扫荡信息");
            }
        }
    } else if (operation == "sweep") {
        if (offset + 36 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            const int buyCnt = ReadInt32LE(body, offset);
            const int restTime = ReadInt32LE(body, offset);
            (void)ReadInt32LE(body, offset);  // invite：占位字段，仅需推进 offset
            const int passGmaeAward = ReadInt32LE(body, offset);
            const int medal = ReadInt32LE(body, offset);
            int exp = ReadInt32LE(body, offset);
            const int coin = ReadInt32LE(body, offset);
            const int itemNum = ReadInt32LE(body, offset);

            std::vector<std::pair<int, int>> rewards;
            for (int i = 0; i < itemNum && offset + 8 <= packet.body.size(); ++i) {
                const int id = ReadInt32LE(body, offset);
                const int num = ReadInt32LE(body, offset);
                if (num > 0) {
                    rewards.emplace_back(id, num);
                }
            }

            if (passGmaeAward > 0) {
                exp -= Act822::PASS_EXP;
            }

            std::vector<std::pair<int, int>> awardList;
            BuildAct822AwardList(medal, exp, coin, rewards, awardList);
            {
                std::lock_guard<std::mutex> lock(g_act822Mutex);
                g_act822ResultAwards = awardList;
            }

            g_act822SweepResult = result;
            g_act822BuyCnt = buyCnt;
            g_act822RestTime = restTime;
            g_act822PassFlag = passGmaeAward;

            ACT822_STATE.sweepResult = result;
            ACT822_STATE.buyCnt = g_act822BuyCnt.load();
            ACT822_STATE.restTime = g_act822RestTime.load();
            ACT822_STATE.passFlag = g_act822PassFlag.load();
            ACT822_STATE.resultAwards = awardList;
            ACT822_STATE.sweepAvailable = (result == 0 || result == 4);
            g_act822SweepAvailable = ACT822_STATE.sweepAvailable.load();

            if (result == 0) {
                wchar_t msg[256];
                swprintf_s(
                    msg,
                    L"光辉穿梭大考验：扫荡完成，勋章=%d 经验=%d 铜钱=%d",
                    FindAwardAmount(awardList, 0),
                    FindAwardAmount(awardList, 202),
                    FindAwardAmount(awardList, 201));
                UIBridge::Instance().UpdateHelperText(msg);
            } else if (result == 1) {
                UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：需要成功进行一局游戏才可以扫荡哦！");
            } else if (result == 2) {
                UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：游戏次数不足");
            } else if (result == 3) {
                UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：冷却中！");
            } else {
                UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：扫荡完成");
            }
        }
    }
}

// ============ 圣石之战功能实现 (Act826) ============

#define ACT826_STATE ActivityStateManager::Instance().GetAct826State()

BOOL SendAct826Packet(const std::string& operation, const std::vector<int32_t>& bodyValues) {
    return SendActivityPacket(Act826::ACTIVITY_ID, operation, bodyValues);
}

BOOL SendAct826OpenUIPacket() {
    ACT826_STATE.waitingResponse = true;
    return SendAct826Packet("open_ui", {});
}

BOOL SendAct826StartGamePacket(int ruleFlag) {
    ACT826_STATE.waitingResponse = true;
    return SendAct826Packet("start_game", {ruleFlag});
}

BOOL SendAct826EndGamePacket(int checkCode, int score) {
    ACT826_STATE.waitingResponse = true;
    return SendAct826Packet("end_game", {checkCode, score});
}

BOOL SendAct826SweepInfoPacket() {
    ACT826_STATE.waitingResponse = true;
    return SendAct826Packet("sweep_info", {});
}

BOOL SendAct826SweepPacket() {
    ACT826_STATE.waitingResponse = true;
    return SendAct826Packet("sweep", {});
}

static bool WaitForAct826Response() {
    for (int i = 0; i < 30 && ACT826_STATE.waitingResponse.load(); ++i) {
        Sleep(100);
    }
    const bool received = !ACT826_STATE.waitingResponse.load();
    ACT826_STATE.waitingResponse = false;
    return received;
}

static int BuildAct826ClientCheckCode() {
    const int checkCode = ACT826_STATE.checkCode.load();
    if (checkCode <= 0) {
        return 0;
    }

    const uint32_t userId = g_userId.load();
    return checkCode & static_cast<int>(userId % static_cast<uint32_t>(checkCode));
}

DWORD WINAPI Act826ThreadProc(LPVOID lpParam) {
    const bool useSweep = lpParam ? *static_cast<bool*>(lpParam) : false;
    delete static_cast<bool*>(lpParam);

    ACT826_STATE.Reset();
    ACT826_STATE.useSweep = useSweep;

    Sleep(300);
    UIBridge::Instance().UpdateHelperText(L"圣石之战：正在获取活动信息...");

    SendAct826OpenUIPacket();
    if (!WaitForAct826Response()) {
        UIBridge::Instance().UpdateHelperText(L"圣石之战：获取活动信息失败");
        return 0;
    }

    if (ACT826_STATE.playCount.load() <= 0) {
        UIBridge::Instance().UpdateHelperText(L"圣石之战：今日次数已用完");
        return 0;
    }

    if (ACT826_STATE.restTime.load() > 0) {
        UIBridge::Instance().UpdateHelperText(L"圣石之战：冷却中，请稍后再试");
        return 0;
    }

    // 自动判定：可扫荡且扫荡成绩 >= 游戏流程成绩时才扫荡
    if (ShouldAutoSweep(ACT826_STATE.maxScore.load(),
                        Act826::PASS_SCORE)) {
        Sleep(300);
        UIBridge::Instance().UpdateHelperText(L"圣石之战：正在获取扫荡信息...");
        SendAct826SweepInfoPacket();
        if (WaitForAct826Response() && ACT826_STATE.sweepAvailable.load()) {
            Sleep(300);
            UIBridge::Instance().UpdateHelperText(L"圣石之战：执行扫荡...");
            ACT826_STATE.sweepResult = -1;
            SendAct826SweepPacket();
            if (WaitForAct826Response() && (ACT826_STATE.sweepResult.load() == 0 || ACT826_STATE.sweepResult.load() == 4)) {
                Sleep(300);
                SendAct826OpenUIPacket();
                WaitForAct826Response();
                wchar_t msg[256];
                swprintf_s(
                    msg,
                    L"圣石之战：扫荡完成，勋章=%d 经验=%d 铜钱=%d",
                    ACT826_STATE.rewardMedal.load(),
                    ACT826_STATE.rewardExp.load(),
                    ACT826_STATE.rewardCoin.load());
                UIBridge::Instance().UpdateHelperText(msg);
                return 0;
            }
        }

        UIBridge::Instance().UpdateHelperText(L"圣石之战：扫荡失败，改为直接完成");
    }

    Sleep(300);
    UIBridge::Instance().UpdateHelperText(L"圣石之战：开始游戏...");
    SendAct826StartGamePacket(ACT826_STATE.ruleFlag.load());
    if (!WaitForAct826Response()) {
        UIBridge::Instance().UpdateHelperText(L"圣石之战：开始游戏失败");
        return 0;
    }

    const int startResult = ACT826_STATE.startResult.load();
    if (startResult != 0) {
        if (startResult == 1) {
            UIBridge::Instance().UpdateHelperText(L"圣石之战：剩余次数不足");
        } else if (startResult == 2) {
            UIBridge::Instance().UpdateHelperText(L"圣石之战：冷却中~~");
        } else if (startResult == 3) {
            UIBridge::Instance().UpdateHelperText(L"圣石之战：在游戏中");
        } else {
            UIBridge::Instance().UpdateHelperText(L"圣石之战：开始游戏失败");
        }
        return 0;
    }

    if (ACT826_STATE.checkCode.load() == 0) {
        UIBridge::Instance().UpdateHelperText(L"圣石之战：获取校验码失败");
        return 0;
    }

    const int finalScore = Act826::PASS_SCORE;
    ACT826_STATE.lastScore = finalScore;
    Sleep(500);
    UIBridge::Instance().UpdateHelperText(L"圣石之战：直接提交结算...");
    const int clientCheckCode = BuildAct826ClientCheckCode();
    if (clientCheckCode == 0) {
        UIBridge::Instance().UpdateHelperText(L"圣石之战：计算校验码失败");
        return 0;
    }

    SendAct826EndGamePacket(clientCheckCode, finalScore);
    if (!WaitForAct826Response()) {
        UIBridge::Instance().UpdateHelperText(L"圣石之战：结算失败");
        return 0;
    }

    if (ACT826_STATE.endResult.load() == 3) {
        UIBridge::Instance().UpdateHelperText(L"圣石之战：结算失败");
    } else {
        wchar_t msg[256];
        swprintf_s(
            msg,
            L"圣石之战：结算完成，分数=%d 勋章=%d 经验=%d 铜钱=%d",
            ACT826_STATE.lastScore.load(),
            ACT826_STATE.rewardMedal.load(),
            ACT826_STATE.rewardExp.load(),
            ACT826_STATE.rewardCoin.load());
        UIBridge::Instance().UpdateHelperText(msg);
    }

    Sleep(300);
    SendAct826OpenUIPacket();
    WaitForAct826Response();
    return 0;
}

BOOL StartOneKeyAct826Packet(bool useSweep) {
    ACT826_STATE.useSweep = useSweep;
    bool* pUseSweep = new bool(useSweep);
    HANDLE hThread = CreateThread(nullptr, 0, Act826ThreadProc, pUseSweep, 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
        return TRUE;
    }
    delete pUseSweep;
    return FALSE;
}

void ProcessAct826Response(const GamePacket& packet) {
    size_t offset = 0;
    std::string operation;
    if (!ReadPacketString(packet.body.data(), packet.body.size(), offset, operation)) {
        return;
    }

    const BYTE* body = packet.body.data();
    ACT826_STATE.waitingResponse = false;

    auto readInt = [&](int& value) -> bool {
        if (offset + 4 > packet.body.size()) {
            return false;
        }
        value = ReadInt32LE(body, offset);
        return true;
    };

    if (operation == "open_ui") {
        if (offset + 64 <= packet.body.size()) {
            int playCount = 0;
            int restTime = 0;
            int ignored = 0;
            int bubbleNum = 0;
            int passCount = 0;
            int ruleFlag = 0;
            int moveControl = 0;
            int maxScore = 0;
            int lastScore = 0;
            int passFlag = 0;
            int petFlag = 0;
            int itemFlag = 0;
            int catchId = 0;
            int catchLimitCur = 0;
            int catchLimitMax = 0;

            readInt(playCount);  // playCount
            readInt(restTime);   // restTime
            readInt(ignored);
            readInt(bubbleNum);
            readInt(passCount);
            readInt(ruleFlag);
            readInt(moveControl);
            readInt(maxScore);
            readInt(lastScore);
            for (int i = 0; i < 3; ++i) {
                readInt(ignored);
            }
            readInt(passFlag);
            readInt(petFlag);
            readInt(itemFlag);
            readInt(catchId);
            readInt(catchLimitCur);
            readInt(catchLimitMax);

            ACT826_STATE.playCount = playCount;
            ACT826_STATE.restTime = restTime;
            ACT826_STATE.bubbleNum = bubbleNum;
            ACT826_STATE.passCount = passCount;
            ACT826_STATE.ruleFlag = ruleFlag;
            ACT826_STATE.isMoveControl = (moveControl == 1);
            ACT826_STATE.maxScore = maxScore;
            ACT826_STATE.lastScore = lastScore;
            ACT826_STATE.passFlag = passFlag;
            ACT826_STATE.petFlag = petFlag;
            ACT826_STATE.itemFlag = itemFlag;
            ACT826_STATE.catchId = catchId;
            ACT826_STATE.catchLimitCur = catchLimitCur;
            ACT826_STATE.catchLimitMax = catchLimitMax;

            wchar_t msg[256];
            swprintf_s(
                msg,
                L"圣石之战：次数=%d 冷却=%d秒 勋章=%d",
                ACT826_STATE.playCount.load(),
                ACT826_STATE.restTime.load(),
                ACT826_STATE.bubbleNum.load());
            UIBridge::Instance().UpdateHelperText(msg);
        }
    } else if (operation == "start_game") {
        if (offset + 12 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            ACT826_STATE.startResult = result;
            if (result == 0) {
                int ignored = 0;
                int checkCode = 0;
                readInt(ignored);
                readInt(checkCode);
                ACT826_STATE.checkCode = checkCode;
            }
        }
    } else if (operation == "end_game" || operation == "sweep") {
        if (offset + 12 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            ReadInt32LE(body, offset);
            ReadInt32LE(body, offset);
            ACT826_STATE.endResult = result;
            ACT826_STATE.sweepResult = result;

            if (result != 3 && offset + 20 <= packet.body.size()) {
                int ignored = ReadInt32LE(body, offset);
                (void)ignored;
                ACT826_STATE.lastScore = ReadInt32LE(body, offset);
                ACT826_STATE.rewardMedal = ReadInt32LE(body, offset);
                ACT826_STATE.rewardExp = ReadInt32LE(body, offset);
                ACT826_STATE.rewardCoin = ReadInt32LE(body, offset);
                ACT826_STATE.sweepSuccess = true;
            } else if (result == 3) {
                ACT826_STATE.sweepSuccess = false;
            }
        }
    } else if (operation == "sweep_info") {
        if (offset + 20 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            ReadInt32LE(body, offset);
            ACT826_STATE.sweepResult = result;
            if (result == 0) {
                ACT826_STATE.rewardMedal = ReadInt32LE(body, offset);
                ACT826_STATE.rewardExp = ReadInt32LE(body, offset);
                ACT826_STATE.rewardCoin = ReadInt32LE(body, offset);
                ACT826_STATE.sweepAvailable = true;
            } else {
                ACT826_STATE.sweepAvailable = false;
            }
        }
    }
}

// ============ 齐心协力大挑战功能实现 (Act827) ============

#define ACT827_STATE ActivityStateManager::Instance().GetAct827State()

BOOL SendAct827Packet(const std::string& operation, const std::vector<int32_t>& bodyValues) {
    return SendActivityPacket(Act827::ACTIVITY_ID, operation, bodyValues);
}

BOOL SendAct827OpenUIPacket() {
    ACT827_STATE.waitingResponse = true;
    return SendAct827Packet("open_ui", {});
}

BOOL SendAct827StartGamePacket(int promptFlag) {
    ACT827_STATE.waitingResponse = true;
    return SendAct827Packet("start_game", {promptFlag});
}

BOOL SendAct827EndGamePacket(int checkCode, int score) {
    ACT827_STATE.waitingResponse = true;
    const BOOL sent = SendAct827Packet("end_game", {checkCode, score});
    if (sent) {
        // 与 AS3 GameControl.onEndGame 一致：结算后顺带上报每日任务 4039001。
        SendActivityDailyTaskReport();
    }
    return sent;
}

BOOL SendAct827SweepInfoPacket() {
    ACT827_STATE.waitingResponse = true;
    return SendAct827Packet("sweep_info", {});
}

BOOL SendAct827SweepPacket() {
    ACT827_STATE.waitingResponse = true;
    return SendAct827Packet("sweep", {});
}

static bool WaitForAct827Response() {
    for (int i = 0; i < 30 && ACT827_STATE.waitingResponse.load(); ++i) {
        Sleep(100);
    }
    const bool received = !ACT827_STATE.waitingResponse.load();
    ACT827_STATE.waitingResponse = false;
    return received;
}

static int BuildAct827ClientCheckCode() {
    const int checkCode = ACT827_STATE.checkCode.load();
    if (checkCode <= 0) {
        return 0;
    }

    const uint32_t userId = g_userId.load();
    return checkCode & static_cast<int>(userId % static_cast<uint32_t>(checkCode));
}

DWORD WINAPI Act827ThreadProc(LPVOID lpParam) {
    const bool useSweep = lpParam ? *static_cast<bool*>(lpParam) : false;
    delete static_cast<bool*>(lpParam);

    ACT827_STATE.Reset();
    ACT827_STATE.useSweep = useSweep;

    Sleep(300);
    UIBridge::Instance().UpdateHelperText(L"齐心协力大挑战：正在获取活动信息...");

    SendAct827OpenUIPacket();
    if (!WaitForAct827Response()) {
        UIBridge::Instance().UpdateHelperText(L"齐心协力大挑战：获取活动信息失败");
        return 0;
    }

    if (ACT827_STATE.playCount.load() <= 0) {
        UIBridge::Instance().UpdateHelperText(L"齐心协力大挑战：今日次数已用完");
        return 0;
    }

    if (ACT827_STATE.restTime.load() > 0) {
        UIBridge::Instance().UpdateHelperText(L"齐心协力大挑战：冷却中，请稍后再试");
        return 0;
    }

    // 自动判定：可扫荡且扫荡成绩 >= 游戏流程成绩时才扫荡
    if (ShouldAutoSweep(ACT827_STATE.bestRecord.load(),
                        Act827::TARGET_SCORE)) {
        Sleep(300);
        UIBridge::Instance().UpdateHelperText(L"齐心协力大挑战：正在获取扫荡信息...");
        SendAct827SweepInfoPacket();
        if (WaitForAct827Response() && ACT827_STATE.sweepAvailable.load()) {
            Sleep(300);
            UIBridge::Instance().UpdateHelperText(L"齐心协力大挑战：执行扫荡...");
            ACT827_STATE.sweepResult = -1;
            SendAct827SweepPacket();
            if (WaitForAct827Response() && (ACT827_STATE.sweepResult.load() == 0 || ACT827_STATE.sweepResult.load() == 4)) {
                Sleep(300);
                SendAct827OpenUIPacket();
                WaitForAct827Response();
                wchar_t msg[256];
                swprintf_s(
                    msg,
                    L"齐心协力大挑战：扫荡完成，分数=%d 勋章=%d 经验=%d 铜钱=%d",
                    ACT827_STATE.rewardScore.load(),
                    ACT827_STATE.rewardMedalNum.load(),
                    ACT827_STATE.rewardExp.load(),
                    ACT827_STATE.rewardCoin.load());
                UIBridge::Instance().UpdateHelperText(msg);
                return 0;
            }
        }

        UIBridge::Instance().UpdateHelperText(L"齐心协力大挑战：扫荡失败，改为直接完成");
    }

    Sleep(300);
    UIBridge::Instance().UpdateHelperText(L"齐心协力大挑战：开始游戏...");
    SendAct827StartGamePacket(ACT827_STATE.promptFlag.load());
    if (!WaitForAct827Response()) {
        UIBridge::Instance().UpdateHelperText(L"齐心协力大挑战：开始游戏失败");
        return 0;
    }

    const int startResult = ACT827_STATE.startResult.load();
    if (startResult != 0) {
        if (startResult == 10) {
            UIBridge::Instance().UpdateHelperText(L"齐心协力大挑战：已经在游戏中了");
        } else if (startResult == 11) {
            UIBridge::Instance().UpdateHelperText(L"齐心协力大挑战：冷却中，请稍后再试");
        } else if (startResult == 12) {
            UIBridge::Instance().UpdateHelperText(L"齐心协力大挑战：剩余次数不足");
        } else {
            UIBridge::Instance().UpdateHelperText(L"齐心协力大挑战：开始游戏失败");
        }
        return 0;
    }

    if (ACT827_STATE.checkCode.load() == 0) {
        UIBridge::Instance().UpdateHelperText(L"齐心协力大挑战：获取校验码失败");
        return 0;
    }

    const int finalScore = (std::max)(ACT827_STATE.bestRecord.load(), Act827::TARGET_SCORE);
    ACT827_STATE.lastScore = finalScore;
    Sleep(500);
    UIBridge::Instance().UpdateHelperText(L"齐心协力大挑战：直接提交结算...");
    const int clientCheckCode = BuildAct827ClientCheckCode();
    if (clientCheckCode == 0) {
        UIBridge::Instance().UpdateHelperText(L"齐心协力大挑战：计算校验码失败");
        return 0;
    }

    SendAct827EndGamePacket(clientCheckCode, finalScore);
    if (!WaitForAct827Response()) {
        UIBridge::Instance().UpdateHelperText(L"齐心协力大挑战：结算失败");
        return 0;
    }

    if (ACT827_STATE.endResult.load() != 0 && ACT827_STATE.endResult.load() != 4) {
        UIBridge::Instance().UpdateHelperText(L"齐心协力大挑战：结算失败");
    } else {
        wchar_t msg[256];
        swprintf_s(
            msg,
            L"齐心协力大挑战：结算完成，分数=%d 勋章=%d 经验=%d 铜钱=%d",
            ACT827_STATE.lastScore.load(),
            ACT827_STATE.rewardMedalNum.load(),
            ACT827_STATE.rewardExp.load(),
            ACT827_STATE.rewardCoin.load());
        UIBridge::Instance().UpdateHelperText(msg);
    }

    Sleep(300);
    SendAct827OpenUIPacket();
    WaitForAct827Response();
    return 0;
}

BOOL StartOneKeyAct827Packet(bool useSweep) {
    ACT827_STATE.useSweep = useSweep;
    bool* pUseSweep = new bool(useSweep);
    HANDLE hThread = CreateThread(nullptr, 0, Act827ThreadProc, pUseSweep, 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
        return TRUE;
    }
    delete pUseSweep;
    return FALSE;
}

void ProcessAct827Response(const GamePacket& packet) {
    size_t offset = 0;
    std::string operation;
    if (!ReadPacketString(packet.body.data(), packet.body.size(), offset, operation)) {
        return;
    }

    const BYTE* body = packet.body.data();
    ACT827_STATE.waitingResponse = false;

    auto readInt = [&](int& value) -> bool {
        if (offset + 4 > packet.body.size()) {
            return false;
        }
        value = ReadInt32LE(body, offset);
        return true;
    };

    if (operation == "open_ui") {
        if (offset + 40 <= packet.body.size()) {
            int playCount = 0;
            int restTime = 0;
            int ignored = 0;
            int totalBadgeNum = 0;
            int promptFlag = 0;
            int flag = 0;
            int bestRecord = 0;
            int monsterId = 0;
            int catch1 = 0;
            int catch2 = 0;

            readInt(playCount);
            readInt(restTime);
            readInt(ignored);
            readInt(totalBadgeNum);
            readInt(promptFlag);
            readInt(flag);
            readInt(bestRecord);
            readInt(monsterId);
            readInt(catch1);
            readInt(catch2);

            ACT827_STATE.playCount = playCount;
            ACT827_STATE.restTime = restTime;
            ACT827_STATE.totalBadgeNum = totalBadgeNum;
            ACT827_STATE.promptFlag = promptFlag;
            ACT827_STATE.flag = flag;
            ACT827_STATE.bestRecord = bestRecord;
            ACT827_STATE.monsterId = monsterId;
            if (ACT827_STATE.catchList.size() < 2) {
                ACT827_STATE.catchList.assign(2, 0);
            }
            ACT827_STATE.catchList[0] = catch1;
            ACT827_STATE.catchList[1] = catch2;
            ACT827_STATE.sweepAvailable = (ACT827_STATE.bestRecord.load() > 0);

            wchar_t msg[256];
            swprintf_s(
                msg,
                L"齐心协力大挑战：次数=%d 冷却=%d秒",
                ACT827_STATE.playCount.load(),
                ACT827_STATE.restTime.load());
            UIBridge::Instance().UpdateHelperText(msg);
        }
    } else if (operation == "start_game") {
        if (offset + 12 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            ACT827_STATE.startResult = result;
            if (result == 0) {
                int restPlayCount = 0;
                int checkCode = 0;
                readInt(restPlayCount);
                readInt(checkCode);
                ACT827_STATE.playCount = restPlayCount;
                ACT827_STATE.checkCode = checkCode;
            }
        }
    } else if (operation == "end_game" || operation == "sweep") {
        if (offset + 28 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            ACT827_STATE.endResult = result;
            ACT827_STATE.sweepResult = result;
            if (result == 0 || result == 4) {
                ACT827_STATE.playCount = ReadInt32LE(body, offset);
                ACT827_STATE.restTime = ReadInt32LE(body, offset);
                ACT827_STATE.rewardScore = ReadInt32LE(body, offset);
                ACT827_STATE.rewardMedalNum = ReadInt32LE(body, offset);
                ACT827_STATE.rewardExp = ReadInt32LE(body, offset);
                ACT827_STATE.rewardCoin = ReadInt32LE(body, offset);
                ACT827_STATE.lastScore = ACT827_STATE.rewardScore.load();
                ACT827_STATE.sweepSuccess = true;
                ACT827_STATE.bestRecord = (std::max)(ACT827_STATE.bestRecord.load(), ACT827_STATE.rewardScore.load());
            } else {
                ACT827_STATE.rewardScore = 0;
                ACT827_STATE.rewardMedalNum = 0;
                ACT827_STATE.rewardExp = 0;
                ACT827_STATE.rewardCoin = 0;
                ACT827_STATE.sweepSuccess = false;
            }
        }
    } else if (operation == "sweep_info") {
        if (offset + 20 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            ACT827_STATE.sweepResult = result;
            if (result == 0) {
                ACT827_STATE.rewardScore = ReadInt32LE(body, offset);
                ACT827_STATE.rewardMedalNum = ReadInt32LE(body, offset);
                ACT827_STATE.rewardExp = ReadInt32LE(body, offset);
                ACT827_STATE.rewardCoin = ReadInt32LE(body, offset);
                ACT827_STATE.sweepAvailable = true;
            } else {
                ACT827_STATE.sweepAvailable = false;
            }
        }
    }
}

// ============ 清除煞气功能实现 (Act641) ============

#define ACT641_STATE ActivityStateManager::Instance().GetAct641State()

BOOL SendAct641Packet(const std::string& operation, const std::vector<int32_t>& bodyValues) {
    return SendActivityPacket(Act641::ACTIVITY_ID, operation, bodyValues);
}

BOOL SendAct641OpenUIPacket() {
    ACT641_STATE.waitingResponse = true;
    return SendAct641Packet("open_ui", {});
}

BOOL SendAct641StartGamePacket() {
    ACT641_STATE.waitingResponse = true;
    return SendAct641Packet("start_game", {});
}

BOOL SendAct641EndGamePacket(int score) {
    const int checkCode = ACT641_STATE.checkCode.load();
    if (checkCode == 0) {
        return FALSE;
    }

    const uint32_t userId = g_userId.load();
    // AS3: uint(userId) % 10000 * 2 + score * checkCode + 7
    const int64_t userIdPart = static_cast<int64_t>(userId % 10000u) * 2;
    const int64_t scorePart = static_cast<int64_t>(score) * static_cast<int64_t>(checkCode);
    const int clientCheckCode = static_cast<int>(userIdPart + scorePart + 7);
    ACT641_STATE.waitingResponse = true;
    return SendAct641Packet("end_game", {clientCheckCode, score});
}

BOOL SendAct641SweepInfoPacket() {
    ACT641_STATE.waitingResponse = true;
    return SendAct641Packet("sweep_info", {});
}

BOOL SendAct641SweepPacket() {
    ACT641_STATE.waitingResponse = true;
    return SendAct641Packet("sweep", {});
}

DWORD WINAPI Act641ThreadProc(LPVOID lpParam) {
    const int targetScore = lpParam ? *static_cast<int*>(lpParam) : Act641::TARGET_SCORE;
    delete static_cast<int*>(lpParam);

    const bool useSweep = ACT641_STATE.useSweep.load();
    ACT641_STATE.Reset();
    ACT641_STATE.useSweep = useSweep;
    ACT641_STATE.targetScore = targetScore;

    auto waitForResponse = []() -> bool {
        for (int i = 0; i < 30 && ACT641_STATE.waitingResponse; ++i) {
            Sleep(100);
        }
        const bool received = !ACT641_STATE.waitingResponse.load();
        ACT641_STATE.waitingResponse = false;
        return received;
    };

    Sleep(300);
    UIBridge::Instance().UpdateHelperText(L"清除煞气：正在获取活动信息...");

    SendAct641OpenUIPacket();
    if (!waitForResponse()) {
        UIBridge::Instance().UpdateHelperText(L"清除煞气：获取活动信息失败");
        return 0;
    }

    if (ACT641_STATE.playCount.load() <= 0) {
        UIBridge::Instance().UpdateHelperText(L"清除煞气：今日次数已用完");
        return 0;
    }

    if (ACT641_STATE.restTime.load() > 0) {
        UIBridge::Instance().UpdateHelperText(L"清除煞气：冷却中，请稍后再试");
        return 0;
    }

    // 自动判定：可扫荡且扫荡成绩 >= 本次游戏流程提交分时才扫荡
    if (ShouldAutoSweep(ACT641_STATE.bestRecord.load(),
                        std::clamp(targetScore, 1, Act641::MAX_SCORE))) {
        UIBridge::Instance().UpdateHelperText(L"清除煞气：正在获取扫荡信息...");

        Sleep(300);
        SendAct641SweepInfoPacket();
        if (waitForResponse() && ACT641_STATE.sweepSuccess.load()) {
            Sleep(300);
            UIBridge::Instance().UpdateHelperText(L"清除煞气：执行扫荡...");
            SendAct641SweepPacket();
            if (waitForResponse() && ACT641_STATE.sweepSuccess.load()) {
                Sleep(300);
                SendAct641OpenUIPacket();
                waitForResponse();
                UIBridge::Instance().UpdateHelperText(L"清除煞气：扫荡完成");
                return 0;
            }
        }

        UIBridge::Instance().UpdateHelperText(L"清除煞气：扫荡失败，改为直接完成");
    }

    Sleep(300);
    UIBridge::Instance().UpdateHelperText(L"清除煞气：开始游戏...");
    SendAct641StartGamePacket();
    if (!waitForResponse()) {
        UIBridge::Instance().UpdateHelperText(L"清除煞气：开始游戏失败");
        return 0;
    }

    const int startResult = ACT641_STATE.startResult.load();
    if (startResult != 0) {
        if (startResult == 10) {
            UIBridge::Instance().UpdateHelperText(L"清除煞气：当前已经在游戏中");
        } else if (startResult == 11) {
            UIBridge::Instance().UpdateHelperText(L"清除煞气：冷却中，请稍后再试");
        } else if (startResult == 12) {
            UIBridge::Instance().UpdateHelperText(L"清除煞气：今日次数已用完");
        } else if (startResult == 13) {
            UIBridge::Instance().UpdateHelperText(L"清除煞气：玩家正在支付中");
        } else {
            UIBridge::Instance().UpdateHelperText(L"清除煞气：开始游戏失败");
        }
        return 0;
    }

    if (ACT641_STATE.checkCode.load() == 0) {
        UIBridge::Instance().UpdateHelperText(L"清除煞气：获取校验码失败");
        return 0;
    }

    Sleep(500);
    UIBridge::Instance().UpdateHelperText(L"清除煞气：直接提交结算...");
    SendAct641EndGamePacket(std::clamp(targetScore, 1, Act641::MAX_SCORE));
    if (!waitForResponse()) {
        UIBridge::Instance().UpdateHelperText(L"清除煞气：结算失败");
        return 0;
    }

    if (ACT641_STATE.endResult.load() == 3) {
        UIBridge::Instance().UpdateHelperText(L"清除煞气：网络错误");
        return 0;
    }

    Sleep(300);
    SendAct641OpenUIPacket();
    waitForResponse();
    UIBridge::Instance().UpdateHelperText(L"清除煞气：结算完成");
    return 0;
}

BOOL StartOneKeyAct641Packet(bool useSweep, int targetScore) {
    ACT641_STATE.useSweep = useSweep;
    int* pTargetScore = new int(targetScore);
    HANDLE hThread = CreateThread(nullptr, 0, Act641ThreadProc, pTargetScore, 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
        return TRUE;
    }
    delete pTargetScore;
    return FALSE;
}

void ProcessAct641Response(const GamePacket& packet) {
    size_t offset = 0;
    std::string operation;
    if (!ReadPacketString(packet.body.data(), packet.body.size(), offset, operation)) {
        return;
    }

    const BYTE* body = packet.body.data();
    ACT641_STATE.waitingResponse = false;

    if (operation == "open_ui") {
        if (offset + 64 <= packet.body.size()) {
            ACT641_STATE.playCount = ReadInt32LE(body, offset);
            ACT641_STATE.restTime = ReadInt32LE(body, offset);
            ACT641_STATE.medalCount = ReadInt32LE(body, offset);
            ACT641_STATE.passCount = ReadInt32LE(body, offset);
            ACT641_STATE.awardFlag = ReadInt32LE(body, offset);
            ACT641_STATE.bestRecord = ReadInt32LE(body, offset);
            ACT641_STATE.promptFlag = ReadInt32LE(body, offset);
            if (ACT641_STATE.bonusList.size() < 6) {
                ACT641_STATE.bonusList.assign(6, 0);
            }
            for (size_t i = 0; i < 6; ++i) {
                ACT641_STATE.bonusList[i] = ReadInt32LE(body, offset);
            }
            ACT641_STATE.catchPetId = ReadInt32LE(body, offset);
            if (ACT641_STATE.catchList.size() < 2) {
                ACT641_STATE.catchList.assign(2, 0);
            }
            ACT641_STATE.catchList[0] = ReadInt32LE(body, offset);
            ACT641_STATE.catchList[1] = ReadInt32LE(body, offset);
            ACT641_STATE.sweepAvailable = (ACT641_STATE.bestRecord.load() > 0);

            wchar_t msg[256];
            swprintf_s(
                msg,
                L"清除煞气：次数=%d 冷却=%d秒 勋章=%d 历史最高=%d",
                ACT641_STATE.playCount.load(),
                ACT641_STATE.restTime.load(),
                ACT641_STATE.medalCount.load(),
                ACT641_STATE.bestRecord.load());
            UIBridge::Instance().UpdateHelperText(msg);
        }
    } else if (operation == "start_game") {
        if (offset + 4 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            ACT641_STATE.startResult = result;
            if (result == 0 && offset + 8 <= packet.body.size()) {
                ACT641_STATE.playCount = ReadInt32LE(body, offset);
                ACT641_STATE.checkCode = ReadInt32LE(body, offset);
            } else if (result == 10) {
                UIBridge::Instance().UpdateHelperText(L"清除煞气：当前已经在游戏中");
            } else if (result == 11) {
                UIBridge::Instance().UpdateHelperText(L"清除煞气：冷却中，请稍后再试");
            } else if (result == 12) {
                UIBridge::Instance().UpdateHelperText(L"清除煞气：今日次数已用完");
            } else if (result == 13) {
                UIBridge::Instance().UpdateHelperText(L"清除煞气：玩家正在支付中");
            }
        }
    } else if (operation == "end_game") {
        if (offset + 4 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            ACT641_STATE.endResult = result;
            // result == 3 means network error; only the status int is guaranteed.
            if (result != 3) {
                if (offset + 32 <= packet.body.size()) {
                    ReadInt32LE(body, offset);
                    ReadInt32LE(body, offset);
                    ACT641_STATE.bestRecord = ReadInt32LE(body, offset);
                    ReadInt32LE(body, offset);  // score
                    ReadInt32LE(body, offset);  // unknown
                    ACT641_STATE.rewardMedalCount = ReadInt32LE(body, offset);
                    ACT641_STATE.rewardExp = ReadInt32LE(body, offset);
                    ACT641_STATE.rewardCoin = ReadInt32LE(body, offset);
                    ACT641_STATE.sweepAvailable = (ACT641_STATE.bestRecord.load() > 0);

                    wchar_t msg[256];
                    swprintf_s(
                        msg,
                        L"清除煞气：结算完成，勋章=%d 经验=%d 铜钱=%d",
                        ACT641_STATE.rewardMedalCount.load(),
                        ACT641_STATE.rewardExp.load(),
                        ACT641_STATE.rewardCoin.load());
                    UIBridge::Instance().UpdateHelperText(msg);
                }
            }
        }
    } else if (operation == "sweep_info") {
        if (offset + 16 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            ACT641_STATE.sweepSuccess = (result == 0);
            if (result == 0) {
                ACT641_STATE.rewardMedalCount = ReadInt32LE(body, offset);
                ACT641_STATE.rewardExp = ReadInt32LE(body, offset);
                ACT641_STATE.rewardCoin = ReadInt32LE(body, offset);
                ACT641_STATE.sweepAvailable = true;

                wchar_t msg[256];
                swprintf_s(
                    msg,
                    L"清除煞气：扫荡预览，勋章=%d 经验=%d 铜钱=%d",
                    ACT641_STATE.rewardMedalCount.load(),
                    ACT641_STATE.rewardExp.load(),
                    ACT641_STATE.rewardCoin.load());
                UIBridge::Instance().UpdateHelperText(msg);
            }
        }
    } else if (operation == "sweep") {
        if (offset + 24 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            ACT641_STATE.sweepSuccess = (result == 0);
            if (result == 0) {
                ACT641_STATE.playCount = ReadInt32LE(body, offset);
                ACT641_STATE.restTime = ReadInt32LE(body, offset);
                ACT641_STATE.rewardMedalCount = ReadInt32LE(body, offset);
                ACT641_STATE.rewardExp = ReadInt32LE(body, offset);
                ACT641_STATE.rewardCoin = ReadInt32LE(body, offset);
                ACT641_STATE.sweepAvailable = (ACT641_STATE.bestRecord.load() > 0);

                wchar_t msg[256];
                swprintf_s(
                    msg,
                    L"清除煞气：扫荡完成，勋章=%d 经验=%d 铜钱=%d",
                    ACT641_STATE.rewardMedalCount.load(),
                    ACT641_STATE.rewardExp.load(),
                    ACT641_STATE.rewardCoin.load());
                UIBridge::Instance().UpdateHelperText(msg);
            }
        }
    }
}

// ============ 疾速特训功能实现 (Act810) ============

static std::atomic<int> g_act810PlayCount{0};
static std::atomic<int> g_act810RestTime{0};
static std::atomic<int> g_act810MedalCount{0};
static std::atomic<int> g_act810RuleFlag{1};
static std::atomic<int> g_act810CheckCode{0};
static std::atomic<int> g_act810StartResult{0};
static std::atomic<int> g_act810RewardMedalCount{0};
static std::atomic<int> g_act810RewardExp{0};
static std::atomic<int> g_act810RewardCoin{0};
static std::atomic<bool> g_act810WaitingResponse{false};
static std::atomic<bool> g_act810UseSweep{false};
static std::atomic<bool> g_act810SweepAvailable{false};
static std::vector<int32_t> g_act810AwardCounts;
static std::mutex g_act810Mutex;

BOOL SendAct810Packet(const std::string& operation, const std::vector<int32_t>& bodyValues) {
    return SendActivityPacket(Act810::ACTIVITY_ID, operation, bodyValues);
}

BOOL SendAct810OpenUIPacket() {
    g_act810WaitingResponse = true;
    return SendAct810Packet("open_ui", {});
}

BOOL SendAct810StartGamePacket(int ruleFlag) {
    g_act810WaitingResponse = true;
    return SendAct810Packet("start_game", {ruleFlag});
}

BOOL SendAct810EndGamePacket(int medalCount) {
    const int checkCode = g_act810CheckCode.load();
    const uint32_t userId = g_userId.load();
    if (checkCode == 0) {
        return FALSE;
    }

    const int clientCheckCode = checkCode & static_cast<int>(userId % static_cast<uint32_t>(checkCode));
    std::vector<int32_t> bodyValues;
    bodyValues.reserve(16);
    bodyValues.push_back(clientCheckCode);
    bodyValues.push_back(medalCount);

    {
        std::lock_guard<std::mutex> lock(g_act810Mutex);
        if (g_act810AwardCounts.size() < 14) {
            bodyValues.insert(bodyValues.end(), 14, 0);
        } else {
            bodyValues.insert(bodyValues.end(), g_act810AwardCounts.begin(), g_act810AwardCounts.begin() + 14);
        }
    }

    g_act810WaitingResponse = true;
    return SendAct810Packet("end_game", bodyValues);
}

BOOL SendAct810SweepInfoPacket() {
    g_act810WaitingResponse = true;
    return SendAct810Packet("sweep_info", {});
}

BOOL SendAct810SweepPacket() {
    g_act810WaitingResponse = true;
    return SendAct810Packet("sweep", {});
}

DWORD WINAPI Act810ThreadProc(LPVOID lpParam) {
    (void)lpParam;

    const bool useSweep = g_act810UseSweep.load();
    g_act810PlayCount = 0;
    g_act810RestTime = 0;
    g_act810MedalCount = 0;
    g_act810RuleFlag = 1;
    g_act810CheckCode = 0;
    g_act810StartResult = 0;
    g_act810RewardMedalCount = 0;
    g_act810RewardExp = 0;
    g_act810RewardCoin = 0;
    g_act810SweepAvailable = false;
    {
        std::lock_guard<std::mutex> lock(g_act810Mutex);
        g_act810AwardCounts.clear();
    }

    auto waitForResponse = []() -> bool {
        for (int i = 0; i < 30 && g_act810WaitingResponse.load(); ++i) {
            Sleep(100);
        }
        const bool received = !g_act810WaitingResponse.load();
        g_act810WaitingResponse = false;
        return received;
    };

    Sleep(300);
    UIBridge::Instance().UpdateHelperText(L"疾速特训：正在获取活动信息...");

    SendAct810OpenUIPacket();
    if (!waitForResponse()) {
        UIBridge::Instance().UpdateHelperText(L"疾速特训：获取活动信息失败");
        return 0;
    }

    if (g_act810PlayCount.load() <= 0) {
        UIBridge::Instance().UpdateHelperText(L"疾速特训：今日次数已用完");
        return 0;
    }

    if (g_act810RestTime.load() > 0) {
        UIBridge::Instance().UpdateHelperText(L"疾速特训：冷却中，请稍后再试");
        return 0;
    }

    if (useSweep) {
        UIBridge::Instance().UpdateHelperText(L"疾速特训：正在获取扫荡信息...");
        Sleep(300);
        SendAct810SweepInfoPacket();
        if (waitForResponse() && g_act810SweepAvailable.load()) {
            Sleep(300);
            UIBridge::Instance().UpdateHelperText(L"疾速特训：执行扫荡...");
            g_act810RewardMedalCount = 0;
            g_act810RewardExp = 0;
            g_act810RewardCoin = 0;
            SendAct810SweepPacket();
            if (waitForResponse()) {
                Sleep(300);
                SendAct810OpenUIPacket();
                waitForResponse();
                UIBridge::Instance().UpdateHelperText(L"疾速特训：扫荡完成");
                return 0;
            }
        }

        UIBridge::Instance().UpdateHelperText(L"疾速特训：扫荡失败，改为直接完成");
    }

    Sleep(300);
    UIBridge::Instance().UpdateHelperText(L"疾速特训：开始游戏...");
    SendAct810StartGamePacket(g_act810RuleFlag.load());
    if (!waitForResponse()) {
        UIBridge::Instance().UpdateHelperText(L"疾速特训：开始游戏失败");
        return 0;
    }

    const int startResult = g_act810StartResult.load();
    if (startResult != 0) {
        if (startResult == 10) {
            UIBridge::Instance().UpdateHelperText(L"疾速特训：当前已经在游戏中");
        } else if (startResult == 11) {
            UIBridge::Instance().UpdateHelperText(L"疾速特训：冷却中，请稍后再试");
        } else if (startResult == 12) {
            UIBridge::Instance().UpdateHelperText(L"疾速特训：今日次数已用完");
        } else if (startResult == 13) {
            UIBridge::Instance().UpdateHelperText(L"疾速特训：玩家正在支付中");
        } else {
            UIBridge::Instance().UpdateHelperText(L"疾速特训：开始游戏失败");
        }
        return 0;
    }

    if (g_act810CheckCode.load() == 0) {
        UIBridge::Instance().UpdateHelperText(L"疾速特训：获取校验码失败");
        return 0;
    }

    int awardTotal = 0;
    {
        std::lock_guard<std::mutex> lock(g_act810Mutex);
        for (int32_t count : g_act810AwardCounts) {
            if (count > 0) {
                awardTotal += count;
            }
        }
    }

    const int progressSteps = std::clamp(awardTotal / 2, 3, 8);
    for (int i = 0; i < progressSteps; ++i) {
        Sleep(250);
        if (i == 0) {
            UIBridge::Instance().UpdateHelperText(L"疾速特训：游戏进行中...");
        } else if (i == progressSteps - 1) {
            UIBridge::Instance().UpdateHelperText(L"疾速特训：准备结算...");
        }
    }

    Sleep(500);
    UIBridge::Instance().UpdateHelperText(L"疾速特训：直接提交结算...");
    SendAct810EndGamePacket(50);
    if (!waitForResponse()) {
        UIBridge::Instance().UpdateHelperText(L"疾速特训：结算失败");
        return 0;
    }

    Sleep(300);
    SendAct810OpenUIPacket();
    waitForResponse();

    if (g_act810RewardMedalCount.load() > 0 || g_act810RewardExp.load() > 0 || g_act810RewardCoin.load() > 0) {
        wchar_t msg[256];
        swprintf_s(
            msg,
            L"疾速特训：结算完成，勋章=%d 经验=%d 铜钱=%d",
            g_act810RewardMedalCount.load(),
            g_act810RewardExp.load(),
            g_act810RewardCoin.load());
        UIBridge::Instance().UpdateHelperText(msg);
    } else {
        UIBridge::Instance().UpdateHelperText(L"疾速特训：结算完成");
    }

    return 0;
}

BOOL StartOneKeyAct810Packet(bool useSweep) {
    g_act810UseSweep = useSweep;
    HANDLE hThread = CreateThread(nullptr, 0, Act810ThreadProc, nullptr, 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
        return TRUE;
    }
    return FALSE;
}

void ProcessAct810Response(const GamePacket& packet) {
    size_t offset = 0;
    std::string operation;
    if (!ReadPacketString(packet.body.data(), packet.body.size(), offset, operation)) {
        return;
    }

    const BYTE* body = packet.body.data();
    g_act810WaitingResponse = false;

    if (operation == "open_ui") {
        if (offset + 24 <= packet.body.size()) {
            g_act810PlayCount = ReadInt32LE(body, offset);
            g_act810RestTime = ReadInt32LE(body, offset);
            ReadInt32LE(body, offset);
            g_act810MedalCount = ReadInt32LE(body, offset);
            ReadInt32LE(body, offset);
            g_act810RuleFlag = ReadInt32LE(body, offset);

            wchar_t msg[256];
            swprintf_s(
                msg,
                L"疾速特训：次数=%d 冷却=%d秒 勋章=%d",
                g_act810PlayCount.load(),
                g_act810RestTime.load(),
                g_act810MedalCount.load());
            UIBridge::Instance().UpdateHelperText(msg);
        }
    } else if (operation == "start_game") {
        if (offset + 4 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            g_act810StartResult = result;
            if (result == 0 && offset + 64 <= packet.body.size()) {
                g_act810PlayCount = ReadInt32LE(body, offset);
                g_act810CheckCode = ReadInt32LE(body, offset);
                std::lock_guard<std::mutex> lock(g_act810Mutex);
                g_act810AwardCounts.clear();
                g_act810AwardCounts.reserve(14);
                for (int i = 0; i < 14 && offset + 4 <= packet.body.size(); ++i) {
                    g_act810AwardCounts.push_back(ReadInt32LE(body, offset));
                }
                while (g_act810AwardCounts.size() < 14) {
                    g_act810AwardCounts.push_back(0);
                }
                UIBridge::Instance().UpdateHelperText(L"疾速特训：已获取开局信息");
            } else if (result == 10) {
                UIBridge::Instance().UpdateHelperText(L"疾速特训：当前已经在游戏中");
            } else if (result == 11) {
                UIBridge::Instance().UpdateHelperText(L"疾速特训：冷却中，请稍后再试");
            } else if (result == 12) {
                UIBridge::Instance().UpdateHelperText(L"疾速特训：今日次数已用完");
            } else if (result == 13) {
                UIBridge::Instance().UpdateHelperText(L"疾速特训：玩家正在支付中");
            }
        }
    } else if (operation == "sweep_info") {
        if (offset + 36 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            if (result == 0) {
                g_act810RewardMedalCount = ReadInt32LE(body, offset);
                g_act810RewardExp = ReadInt32LE(body, offset);
                g_act810RewardCoin = ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                g_act810SweepAvailable = true;

                wchar_t msg[256];
                swprintf_s(
                    msg,
                    L"疾速特训：扫荡预览，勋章=%d 经验=%d 铜钱=%d",
                    g_act810RewardMedalCount.load(),
                    g_act810RewardExp.load(),
                    g_act810RewardCoin.load());
                UIBridge::Instance().UpdateHelperText(msg);
            } else {
                g_act810SweepAvailable = false;
                UIBridge::Instance().UpdateHelperText(L"疾速特训：当前不可扫荡");
            }
        }
    } else if (operation == "end_game" || operation == "sweep") {
        if (offset + 44 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            if (result != 3) {
                ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                g_act810RewardMedalCount = ReadInt32LE(body, offset);
                g_act810RewardExp = ReadInt32LE(body, offset);
                g_act810RewardCoin = ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                if (operation == "sweep") {
                    UIBridge::Instance().UpdateHelperText(L"疾速特训：扫荡完成");
                } else {
                    wchar_t msg[256];
                    swprintf_s(
                        msg,
                        L"疾速特训：结算完成，勋章=%d 经验=%d 铜钱=%d",
                        g_act810RewardMedalCount.load(),
                        g_act810RewardExp.load(),
                        g_act810RewardCoin.load());
                    UIBridge::Instance().UpdateHelperText(msg);
                }
            } else {
                UIBridge::Instance().UpdateHelperText(operation == "sweep" ? L"疾速特训：扫荡失败" : L"疾速特训：结算失败");
            }
        }
    }
}

// ============ 土幻的曙光功能实现 (Act684) ============

#define ACT684_STATE ActivityStateManager::Instance().GetAct684State()

BOOL SendAct684Packet(const std::string& operation, const std::vector<int32_t>& bodyValues) {
    return SendActivityPacket(Act684::ACTIVITY_ID, operation, bodyValues);
}

BOOL SendAct684OpenUIPacket() {
    ACT684_STATE.waitingResponse = true;
    return SendAct684Packet("open_ui", {});
}

BOOL SendAct684StartGamePacket(int ruleFlag) {
    ACT684_STATE.waitingResponse = true;
    return SendAct684Packet("start_game", {ruleFlag});
}

BOOL SendAct684EndGamePacket(int score) {
    const int checkCode = ACT684_STATE.checkCode.load();
    if (checkCode == 0) {
        return FALSE;
    }

    const int finalScore = std::clamp(score, Act684::PASS_SCORE, Act684::MAX_SCORE);
    const uint32_t userId = g_userId.load();
    const int64_t userIdPart = static_cast<int64_t>(userId % 100u) * static_cast<int64_t>(checkCode);
    const int64_t clientCheckCode = static_cast<int64_t>(checkCode) + userIdPart + finalScore;

    ACT684_STATE.waitingResponse = true;
    return SendAct684Packet("end_game", {
        static_cast<int32_t>(clientCheckCode),
        finalScore
    });
}

BOOL SendAct684SweepInfoPacket() {
    ACT684_STATE.waitingResponse = true;
    return SendAct684Packet("sweep_info", {});
}

BOOL SendAct684SweepPacket() {
    ACT684_STATE.waitingResponse = true;
    return SendAct684Packet("sweep", {});
}

DWORD WINAPI Act684ThreadProc(LPVOID lpParam) {
    const int targetScore = lpParam ? *static_cast<int*>(lpParam) : Act684::TARGET_SCORE;
    delete static_cast<int*>(lpParam);

    const bool useSweep = ACT684_STATE.useSweep.load();
    ACT684_STATE.Reset();
    ACT684_STATE.useSweep = useSweep;
    ACT684_STATE.targetScore = targetScore;

    auto waitForResponse = []() -> bool {
        for (int i = 0; i < 30 && ACT684_STATE.waitingResponse; ++i) {
            Sleep(100);
        }
        const bool received = !ACT684_STATE.waitingResponse.load();
        ACT684_STATE.waitingResponse = false;
        return received;
    };

    Sleep(300);
    UIBridge::Instance().UpdateHelperText(L"土幻的曙光：正在获取活动信息...");

    SendAct684OpenUIPacket();
    if (!waitForResponse()) {
        UIBridge::Instance().UpdateHelperText(L"土幻的曙光：获取活动信息失败");
        return 0;
    }

    if (ACT684_STATE.playCount.load() <= 0) {
        UIBridge::Instance().UpdateHelperText(L"土幻的曙光：今日次数已用完");
        return 0;
    }

    if (ACT684_STATE.restTime.load() > 0) {
        UIBridge::Instance().UpdateHelperText(L"土幻的曙光：冷却中，请稍后再试");
        return 0;
    }

    // 自动判定：可扫荡且扫荡成绩 >= 本次游戏流程提交分时才扫荡
    if (ShouldAutoSweep(ACT684_STATE.maxScore.load(),
                        targetScore)) {
        Sleep(300);
        UIBridge::Instance().UpdateHelperText(L"土幻的曙光：正在获取扫荡信息...");
        ACT684_STATE.sweepSuccess = false;

        if (SendAct684SweepInfoPacket() && waitForResponse() && ACT684_STATE.sweepSuccess.load()) {
            Sleep(300);
            UIBridge::Instance().UpdateHelperText(L"土幻的曙光：执行扫荡...");

            if (SendAct684SweepPacket() && waitForResponse() && ACT684_STATE.sweepSuccess.load()) {
                Sleep(300);
                SendAct684OpenUIPacket();
                waitForResponse();
                UIBridge::Instance().UpdateHelperText(L"土幻的曙光：扫荡完成");
                return 0;
            }
        }

        UIBridge::Instance().UpdateHelperText(L"土幻的曙光：扫荡失败，改为直接完成");
    }

    Sleep(300);
    UIBridge::Instance().UpdateHelperText(L"土幻的曙光：开始游戏...");

    int ruleFlag = ACT684_STATE.ruleFlag.load();
    if (ruleFlag < 1) {
        ruleFlag = 1;
    }

    if (!SendAct684StartGamePacket(ruleFlag) || !waitForResponse()) {
        UIBridge::Instance().UpdateHelperText(L"土幻的曙光：开始游戏失败");
        return 0;
    }

    const int startResult = ACT684_STATE.startResult.load();
    if (startResult != 0) {
        if (startResult == 10) {
            UIBridge::Instance().UpdateHelperText(L"土幻的曙光：当前已经在游戏中");
        } else if (startResult == 11) {
            UIBridge::Instance().UpdateHelperText(L"土幻的曙光：冷却中，请稍后再试");
        } else if (startResult == 12) {
            UIBridge::Instance().UpdateHelperText(L"土幻的曙光：今日次数已用完");
        } else if (startResult == 13) {
            UIBridge::Instance().UpdateHelperText(L"土幻的曙光：玩家正在支付中");
        } else {
            UIBridge::Instance().UpdateHelperText(L"土幻的曙光：开始游戏失败");
        }
        return 0;
    }

    if (ACT684_STATE.checkCode.load() == 0) {
        UIBridge::Instance().UpdateHelperText(L"土幻的曙光：获取校验码失败");
        return 0;
    }

    Sleep(500);
    UIBridge::Instance().UpdateHelperText(L"土幻的曙光：直接提交结算...");

    if (!SendAct684EndGamePacket(targetScore) || !waitForResponse()) {
        UIBridge::Instance().UpdateHelperText(L"土幻的曙光：结算失败");
        return 0;
    }

    if (ACT684_STATE.endResult.load() == 3) {
        UIBridge::Instance().UpdateHelperText(L"土幻的曙光：网络错误");
        return 0;
    }

    Sleep(300);
    SendAct684OpenUIPacket();
    waitForResponse();
    UIBridge::Instance().UpdateHelperText(L"土幻的曙光：完成");
    return 0;
}

BOOL StartOneKeyAct684Packet(bool useSweep, int targetScore) {
    ACT684_STATE.useSweep = useSweep;
    int* pTargetScore = new int(targetScore);
    HANDLE hThread = CreateThread(nullptr, 0, Act684ThreadProc, pTargetScore, 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
        return TRUE;
    }

    delete pTargetScore;
    return FALSE;
}

void ProcessAct684Response(const GamePacket& packet) {
    size_t offset = 0;
    std::string operation;
    if (!ReadPacketString(packet.body.data(), packet.body.size(), offset, operation)) {
        return;
    }

    const BYTE* body = packet.body.data();
    ACT684_STATE.waitingResponse = false;

    if (operation == "open_ui") {
        if (offset + 44 <= packet.body.size()) {
            ACT684_STATE.playCount = ReadInt32LE(body, offset);
            ACT684_STATE.restTime = ReadInt32LE(body, offset);
            ACT684_STATE.bubbleNum = ReadInt32LE(body, offset);
            ACT684_STATE.rewardCount = ReadInt32LE(body, offset);
            ACT684_STATE.todayCount = ReadInt32LE(body, offset);
            ACT684_STATE.ruleFlag = ReadInt32LE(body, offset);
            ACT684_STATE.drawFlag = ReadInt32LE(body, offset);
            ACT684_STATE.maxScore = ReadInt32LE(body, offset);
            ReadInt32LE(body, offset);  // skip
            if (ACT684_STATE.catchList.size() < 2) {
                ACT684_STATE.catchList.assign(2, 0);
            }
            ACT684_STATE.catchList[0] = ReadInt32LE(body, offset);
            ACT684_STATE.catchList[1] = ReadInt32LE(body, offset);
            ACT684_STATE.sweepAvailable = (ACT684_STATE.maxScore.load() > 0);

            wchar_t msg[256];
            swprintf_s(
                msg,
                L"土幻的曙光：次数=%d 冷却=%d秒 能量=%d",
                ACT684_STATE.playCount.load(),
                ACT684_STATE.restTime.load(),
                ACT684_STATE.bubbleNum.load());
            UIBridge::Instance().UpdateHelperText(msg);
        }
    } else if (operation == "start_game") {
        if (offset + 12 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            ACT684_STATE.startResult = result;
            if (result == 0) {
                ACT684_STATE.playCount = ReadInt32LE(body, offset);
                ACT684_STATE.checkCode = ReadInt32LE(body, offset);
            }
        }
    } else if (operation == "end_game") {
        if (offset + 36 <= packet.body.size()) {
            const int resultType = ReadInt32LE(body, offset);
            ACT684_STATE.endResult = resultType;
            ACT684_STATE.playCount = ReadInt32LE(body, offset);
            ACT684_STATE.restTime = ReadInt32LE(body, offset);
            ACT684_STATE.todayCount = ReadInt32LE(body, offset);
            ACT684_STATE.rewardMedalCount = ReadInt32LE(body, offset);
            ACT684_STATE.rewardCoin = ReadInt32LE(body, offset);
            ACT684_STATE.rewardExp = ReadInt32LE(body, offset);
            ReadInt32LE(body, offset);  // score
            ACT684_STATE.maxScore = ReadInt32LE(body, offset);
            ACT684_STATE.sweepAvailable = (ACT684_STATE.maxScore.load() > 0);
        }
    } else if (operation == "sweep_info") {
        if (offset + 16 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            ACT684_STATE.sweepSuccess = (result == 0);
            if (result == 0) {
                ACT684_STATE.rewardMedalCount = ReadInt32LE(body, offset);
                ACT684_STATE.rewardCoin = ReadInt32LE(body, offset);
                ACT684_STATE.rewardExp = ReadInt32LE(body, offset);
                ACT684_STATE.sweepAvailable = true;
            }
        }
    } else if (operation == "sweep") {
        if (offset + 36 <= packet.body.size()) {
            const int resultType = ReadInt32LE(body, offset);
            ACT684_STATE.endResult = resultType;
            ACT684_STATE.sweepSuccess = (resultType == 0);
            ACT684_STATE.playCount = ReadInt32LE(body, offset);
            ACT684_STATE.restTime = ReadInt32LE(body, offset);
            ACT684_STATE.todayCount = ReadInt32LE(body, offset);
            ACT684_STATE.rewardMedalCount = ReadInt32LE(body, offset);
            ACT684_STATE.rewardCoin = ReadInt32LE(body, offset);
            ACT684_STATE.rewardExp = ReadInt32LE(body, offset);
            ReadInt32LE(body, offset);  // score
            ACT684_STATE.maxScore = ReadInt32LE(body, offset);
            ACT684_STATE.sweepAvailable = (ACT684_STATE.maxScore.load() > 0);
        }
    }
}

// ============ 驱赶毒蚊功能实现 (Act717) ============

#define ACT717_STATE ActivityStateManager::Instance().GetAct717State()

BOOL SendAct717Packet(const std::string& operation, const std::vector<int32_t>& bodyValues) {
    return SendActivityPacket(Act717::ACTIVITY_ID, operation, bodyValues);
}

BOOL SendAct717OpenUIPacket() {
    ACT717_STATE.waitingResponse = true;
    return SendAct717Packet("open_ui", {});
}

BOOL SendAct717StartGamePacket(int promptFlag) {
    ACT717_STATE.waitingResponse = true;
    return SendAct717Packet("start_game", {promptFlag});
}

BOOL SendAct717EndGamePacket(int score) {
    ACT717_STATE.waitingResponse = true;
    return SendAct717Packet("end_game", {std::clamp(score, 0, Act717::TARGET_SCORE)});
}

BOOL SendAct717SweepInfoPacket() {
    ACT717_STATE.waitingResponse = true;
    return SendAct717Packet("sweep_info", {});
}

BOOL SendAct717SweepPacket() {
    ACT717_STATE.waitingResponse = true;
    return SendAct717Packet("sweep", {});
}

DWORD WINAPI Act717ThreadProc(LPVOID lpParam) {
    const int targetScore = lpParam ? *static_cast<int*>(lpParam) : Act717::TARGET_SCORE;
    delete static_cast<int*>(lpParam);

    const bool useSweep = ACT717_STATE.useSweep.load();
    ACT717_STATE.Reset();
    ACT717_STATE.useSweep = useSweep;

    auto waitForResponse = []() -> bool {
        for (int i = 0; i < 30 && ACT717_STATE.waitingResponse; ++i) {
            Sleep(100);
        }
        const bool received = !ACT717_STATE.waitingResponse.load();
        ACT717_STATE.waitingResponse = false;
        return received;
    };

    Sleep(300);
    UIBridge::Instance().UpdateHelperText(L"驱赶毒蚊：正在获取活动信息...");

    SendAct717OpenUIPacket();
    if (!waitForResponse()) {
        UIBridge::Instance().UpdateHelperText(L"驱赶毒蚊：获取活动信息失败");
        return 0;
    }

    if (ACT717_STATE.playCount.load() <= 0) {
        UIBridge::Instance().UpdateHelperText(L"驱赶毒蚊：今日次数已用完");
        return 0;
    }

    if (ACT717_STATE.restTime.load() > 0) {
        UIBridge::Instance().UpdateHelperText(L"驱赶毒蚊：冷却中，请稍后再试");
        return 0;
    }

    // 717 的 open_ui 只回报 isSweep（可否扫荡），不回报可比的历史成绩，
    // 无法判断扫荡是否优于游戏流程；此处按“可扫荡即扫荡”处理，扫荡由服务端
    // 重放其保存的最好成绩。其余活动均按 ShouldAutoSweep 比较成绩后再决定。
    if (ACT717_STATE.sweepAvailable.load()) {
        Sleep(300);
        UIBridge::Instance().UpdateHelperText(L"驱赶毒蚊：正在获取扫荡信息...");
        ACT717_STATE.sweepSuccess = false;

        if (SendAct717SweepInfoPacket() && waitForResponse() && ACT717_STATE.sweepSuccess.load()) {
            Sleep(300);
            UIBridge::Instance().UpdateHelperText(L"驱赶毒蚊：执行扫荡...");

            if (SendAct717SweepPacket() && waitForResponse() && ACT717_STATE.sweepSuccess.load()) {
                Sleep(300);
                SendAct717OpenUIPacket();
                waitForResponse();
                UIBridge::Instance().UpdateHelperText(L"驱赶毒蚊：扫荡完成");
                return 0;
            }
        }

        UIBridge::Instance().UpdateHelperText(L"驱赶毒蚊：扫荡失败，改为直接完成");
    }

    Sleep(300);
    UIBridge::Instance().UpdateHelperText(L"驱赶毒蚊：开始游戏...");

    if (!SendAct717StartGamePacket(ACT717_STATE.promptFlag.load()) || !waitForResponse()) {
        UIBridge::Instance().UpdateHelperText(L"驱赶毒蚊：开始游戏失败");
        return 0;
    }

    const int startResult = ACT717_STATE.startResult.load();
    if (startResult != 0) {
        if (startResult == 10) {
            UIBridge::Instance().UpdateHelperText(L"驱赶毒蚊：当前已经在游戏中");
        } else if (startResult == 11) {
            UIBridge::Instance().UpdateHelperText(L"驱赶毒蚊：冷却中，请稍后再试");
        } else if (startResult == 12) {
            UIBridge::Instance().UpdateHelperText(L"驱赶毒蚊：今日次数已用完");
        } else if (startResult == 13) {
            UIBridge::Instance().UpdateHelperText(L"驱赶毒蚊：玩家正在支付中");
        } else {
            UIBridge::Instance().UpdateHelperText(L"驱赶毒蚊：开始游戏失败");
        }
        return 0;
    }

    const int finalScore = std::clamp(targetScore, 0, Act717::TARGET_SCORE);
    Sleep(500);
    UIBridge::Instance().UpdateHelperText(L"驱赶毒蚊：直接提交结算...");

    if (!SendAct717EndGamePacket(finalScore) || !waitForResponse()) {
        UIBridge::Instance().UpdateHelperText(L"驱赶毒蚊：结算失败");
        return 0;
    }

    if (ACT717_STATE.endResult.load() == 1) {
        UIBridge::Instance().UpdateHelperText(L"驱赶毒蚊：结算失败");
        return 0;
    }

    Sleep(300);
    SendAct717OpenUIPacket();
    waitForResponse();

    wchar_t msg[256];
    swprintf_s(msg, L"驱赶毒蚊：结算完成，分数=%d", finalScore);
    UIBridge::Instance().UpdateHelperText(msg);
    return 0;
}

BOOL StartOneKeyAct717Packet(bool useSweep, int targetScore) {
    ACT717_STATE.useSweep = useSweep;
    int* pTargetScore = new int(targetScore);
    HANDLE hThread = CreateThread(nullptr, 0, Act717ThreadProc, pTargetScore, 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
        return TRUE;
    }

    delete pTargetScore;
    return FALSE;
}

void ProcessAct717Response(const GamePacket& packet) {
    size_t offset = 0;
    std::string operation;
    if (!ReadPacketString(packet.body.data(), packet.body.size(), offset, operation)) {
        return;
    }

    const BYTE* body = packet.body.data();
    ACT717_STATE.waitingResponse = false;

    auto parseRewardList = [&](size_t& currentOffset) {
        std::vector<std::pair<int, int>> rewardList;
        if (currentOffset + 4 > packet.body.size()) {
            return rewardList;
        }

        const int len = ReadInt32LE(body, currentOffset);
        rewardList.reserve(len);
        for (int i = 0; i < len && currentOffset + 8 <= packet.body.size(); ++i) {
            const int id = ReadInt32LE(body, currentOffset);
            const int num = ReadInt32LE(body, currentOffset);
            rewardList.emplace_back(id, num);
        }
        return rewardList;
    };

    if (operation == "open_ui") {
        if (offset + 32 <= packet.body.size()) {
            ACT717_STATE.playCount = ReadInt32LE(body, offset);
            ACT717_STATE.restTime = ReadInt32LE(body, offset);
            ACT717_STATE.totalBadgeNum = ReadInt32LE(body, offset);
            ACT717_STATE.promptFlag = ReadInt32LE(body, offset);
            const int isSweep = ReadInt32LE(body, offset);
            ReadInt32LE(body, offset);  // skip
            if (ACT717_STATE.catchList.size() < 2) {
                ACT717_STATE.catchList.assign(2, 0);
            }
            ACT717_STATE.catchList[0] = ReadInt32LE(body, offset);
            ACT717_STATE.catchList[1] = ReadInt32LE(body, offset);
            ACT717_STATE.sweepAvailable = (isSweep != 0);

            wchar_t msg[256];
            swprintf_s(
                msg,
                L"驱赶毒蚊：次数=%d 冷却=%d秒 勋章=%d",
                ACT717_STATE.playCount.load(),
                ACT717_STATE.restTime.load(),
                ACT717_STATE.totalBadgeNum.load());
            UIBridge::Instance().UpdateHelperText(msg);
        }
    } else if (operation == "start_game") {
        if (offset + 8 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            ACT717_STATE.startResult = result;
            if (result == 0) {
                ACT717_STATE.playCount = ReadInt32LE(body, offset);
            }
        }
    } else if (operation == "end_game") {
        if (offset + 12 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            ACT717_STATE.endResult = result;
            ACT717_STATE.playCount = ReadInt32LE(body, offset);
            ACT717_STATE.restTime = ReadInt32LE(body, offset);
            if (result != 1) {
                ACT717_STATE.rewardList = parseRewardList(offset);
                ACT717_STATE.sweepSuccess = true;
            }
        }
    } else if (operation == "sweep_info") {
        if (offset + 4 <= packet.body.size()) {
            const int type = ReadInt32LE(body, offset);
            ACT717_STATE.sweepResult = type;
            ACT717_STATE.sweepSuccess = (type != 3);
            if (type != 3) {
                ACT717_STATE.rewardList = parseRewardList(offset);
            }
        }
    } else if (operation == "sweep") {
        if (offset + 12 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            ACT717_STATE.sweepResult = result;
            ACT717_STATE.sweepSuccess = (result != 1);
            ACT717_STATE.playCount = ReadInt32LE(body, offset);
            ACT717_STATE.restTime = ReadInt32LE(body, offset);
            if (result != 1) {
                ACT717_STATE.rewardList = parseRewardList(offset);
            }
        }
    }
}

// ============ 守护梦境功能实现 (Act805) ============

#define ACT805_STATE ActivityStateManager::Instance().GetAct805State()

static BOOL SendAct805DailyTaskPacket() {
    std::vector<BYTE> packet = PacketBuilder()
                                   .SetOpcode(Opcode::TASK_DAILY_REPORT_SEND)
                                   .SetParams(3)
                                   .WriteInt32(3)
                                   .WriteInt32(Act805::DAILY_TASK_ID)
                                   .WriteInt32(0)
                                   .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

BOOL SendAct805Packet(const std::string& operation, const std::vector<int32_t>& bodyValues) {
    return SendActivityPacket(Act805::ACTIVITY_ID, operation, bodyValues);
}

BOOL SendAct805GameInfoPacket() {
    ACT805_STATE.waitingResponse = true;
    return SendAct805Packet("game_info", {});
}

BOOL SendAct805StartGamePacket() {
    ACT805_STATE.waitingResponse = true;
    return SendAct805Packet("start_game", {});
}

BOOL SendAct805EndGamePacket(int score) {
    const int checkCode = ACT805_STATE.checkCode.load();
    const int clientCheckCode = static_cast<int>(g_userId.load() % 1000) + checkCode + score;
    ACT805_STATE.waitingResponse = true;
    const BOOL sent = SendAct805Packet("end_game", {clientCheckCode, score});
    if (sent) {
        SendAct805DailyTaskPacket();
    }
    return sent;
}

BOOL SendAct805SweepInfoPacket() {
    ACT805_STATE.waitingResponse = true;
    return SendAct805Packet("sweep_info", {});
}

BOOL SendAct805SweepPacket() {
    ACT805_STATE.waitingResponse = true;
    return SendAct805Packet("sweep", {});
}

DWORD WINAPI Act805ThreadProc(LPVOID lpParam) {
    const int targetScore = lpParam ? *static_cast<int*>(lpParam) : Act805::TARGET_SCORE;
    delete static_cast<int*>(lpParam);

    const bool useSweep = ACT805_STATE.useSweep.load();
    ACT805_STATE.Reset();
    ACT805_STATE.useSweep = useSweep;
    ACT805_STATE.targetScore = targetScore;

    auto waitForResponse = []() -> bool {
        for (int i = 0; i < 30 && ACT805_STATE.waitingResponse; ++i) {
            Sleep(100);
        }
        const bool received = !ACT805_STATE.waitingResponse.load();
        ACT805_STATE.waitingResponse = false;
        return received;
    };

    Sleep(300);
    UIBridge::Instance().UpdateHelperText(L"守护梦境：正在获取活动信息...");

    SendAct805GameInfoPacket();
    if (!waitForResponse()) {
        UIBridge::Instance().UpdateHelperText(L"守护梦境：获取活动信息失败");
        return 0;
    }

    if (ACT805_STATE.playCount.load() <= 0) {
        UIBridge::Instance().UpdateHelperText(L"守护梦境：今日次数已用完");
        return 0;
    }

    if (ACT805_STATE.restTime.load() > 0) {
        UIBridge::Instance().UpdateHelperText(L"守护梦境：冷却中，请稍后再试");
        return 0;
    }

    if (useSweep && ACT805_STATE.historyBestScore.load() > 0) {
        UIBridge::Instance().UpdateHelperText(L"守护梦境：正在获取扫荡信息...");
        ACT805_STATE.sweepSuccess = false;

        Sleep(300);
        SendAct805SweepInfoPacket();
        if (waitForResponse()) {
            Sleep(300);
            SendAct805SweepPacket();
            if (waitForResponse() && ACT805_STATE.sweepSuccess.load()) {
                Sleep(300);
                SendAct805GameInfoPacket();
                waitForResponse();
                UIBridge::Instance().UpdateHelperText(L"守护梦境：扫荡完成");
                return 0;
            }
        }

        UIBridge::Instance().UpdateHelperText(L"守护梦境：扫荡失败，改为直接结算");
    }

    Sleep(300);
    UIBridge::Instance().UpdateHelperText(L"守护梦境：开始游戏...");
    SendAct805StartGamePacket();
    if (!waitForResponse()) {
        UIBridge::Instance().UpdateHelperText(L"守护梦境：开始游戏失败");
        return 0;
    }

    if (ACT805_STATE.startResult.load() != 0) {
        return 0;
    }

    if (ACT805_STATE.checkCode.load() == 0) {
        UIBridge::Instance().UpdateHelperText(L"守护梦境：获取校验码失败");
        return 0;
    }

    const int finalScore = std::clamp(targetScore, 1, Act805::TARGET_SCORE);
    Sleep(500);
    UIBridge::Instance().UpdateHelperText(L"守护梦境：直接提交结算...");
    SendAct805EndGamePacket(finalScore);
    if (!waitForResponse()) {
        UIBridge::Instance().UpdateHelperText(L"守护梦境：结算失败");
        return 0;
    }

    Sleep(300);
    SendAct805GameInfoPacket();
    waitForResponse();

    wchar_t msg[256];
    swprintf_s(
        msg,
        L"守护梦境：结算完成，分数=%d 勋章=%d 经验=%d 铜钱=%d",
        finalScore,
        ACT805_STATE.rewardMedalCount.load(),
        ACT805_STATE.rewardExp.load(),
        ACT805_STATE.rewardCoin.load());
    UIBridge::Instance().UpdateHelperText(msg);
    return 0;
}

BOOL StartOneKeyAct805Packet(bool useSweep, int targetScore) {
    ACT805_STATE.useSweep = useSweep;
    int* pTargetScore = new int(targetScore);
    HANDLE hThread = CreateThread(nullptr, 0, Act805ThreadProc, pTargetScore, 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
        return TRUE;
    }
    delete pTargetScore;
    return FALSE;
}

void ProcessAct805Response(const GamePacket& packet) {
    size_t offset = 0;
    std::string operation;
    if (!ReadPacketString(packet.body.data(), packet.body.size(), offset, operation)) {
        return;
    }

    const BYTE* body = packet.body.data();
    ACT805_STATE.waitingResponse = false;

    if (operation == "game_info") {
        if (offset + 28 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            if (result == 0) {
                ACT805_STATE.playCount = ReadInt32LE(body, offset);
                ACT805_STATE.restTime = ReadInt32LE(body, offset);
                ACT805_STATE.isPop = ReadInt32LE(body, offset);
                ACT805_STATE.medalNum = ReadInt32LE(body, offset);
                ACT805_STATE.historyBestScore = ReadInt32LE(body, offset);
                ACT805_STATE.lastScore = ReadInt32LE(body, offset);
                ACT805_STATE.sweepAvailable = (ACT805_STATE.historyBestScore.load() > 0);

                wchar_t msg[256];
                swprintf_s(
                    msg,
                    L"守护梦境：次数=%d 冷却=%d秒 勋章=%d 历史最高=%d",
                    ACT805_STATE.playCount.load(),
                    ACT805_STATE.restTime.load(),
                    ACT805_STATE.medalNum.load(),
                    ACT805_STATE.historyBestScore.load());
                UIBridge::Instance().UpdateHelperText(msg);
            }
        }
    } else if (operation == "start_game") {
        if (offset + 4 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            ACT805_STATE.startResult = result;
            if (result == 0 && offset + 8 <= packet.body.size()) {
                ACT805_STATE.playCount = ReadInt32LE(body, offset);
                ACT805_STATE.checkCode = ReadInt32LE(body, offset);
            } else if (result == 10) {
                UIBridge::Instance().UpdateHelperText(L"守护梦境：游戏中");
            } else if (result == 11) {
                UIBridge::Instance().UpdateHelperText(L"守护梦境：冷却中，请稍后再试");
            } else if (result == 12) {
                UIBridge::Instance().UpdateHelperText(L"守护梦境：今日次数已用完");
            } else if (result == 13) {
                UIBridge::Instance().UpdateHelperText(L"守护梦境：玩家正在支付中");
            }
        }
    } else if (operation == "end_game") {
        if (offset + 24 <= packet.body.size()) {
            ReadInt32LE(body, offset);  // result
            ACT805_STATE.playCount = ReadInt32LE(body, offset);
            ACT805_STATE.restTime = ReadInt32LE(body, offset);
            ACT805_STATE.rewardMedalCount = ReadInt32LE(body, offset);
            ACT805_STATE.rewardExp = ReadInt32LE(body, offset);
            ACT805_STATE.rewardCoin = ReadInt32LE(body, offset);
            ACT805_STATE.lastScore = ACT805_STATE.targetScore.load();
            const int historyBestScore = ACT805_STATE.historyBestScore.load();
            const int lastScore = ACT805_STATE.lastScore.load();
            ACT805_STATE.historyBestScore = historyBestScore > lastScore ? historyBestScore : lastScore;
            ACT805_STATE.sweepAvailable = (ACT805_STATE.historyBestScore.load() > 0);

            wchar_t msg[256];
            swprintf_s(
                msg,
                L"守护梦境：结算完成，勋章=%d 经验=%d 铜钱=%d",
                ACT805_STATE.rewardMedalCount.load(),
                ACT805_STATE.rewardExp.load(),
                ACT805_STATE.rewardCoin.load());
            UIBridge::Instance().UpdateHelperText(msg);
        }
    } else if (operation == "sweep_info") {
        if (offset + 20 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            if (result == 0) {
                ReadInt32LE(body, offset);  // endType
                ACT805_STATE.rewardMedalCount = ReadInt32LE(body, offset);
                ACT805_STATE.rewardExp = ReadInt32LE(body, offset);
                ACT805_STATE.rewardCoin = ReadInt32LE(body, offset);
                ACT805_STATE.sweepAvailable = true;

                wchar_t msg[256];
                swprintf_s(
                    msg,
                    L"守护梦境：扫荡预览，勋章=%d 经验=%d 铜钱=%d",
                    ACT805_STATE.rewardMedalCount.load(),
                    ACT805_STATE.rewardExp.load(),
                    ACT805_STATE.rewardCoin.load());
                UIBridge::Instance().UpdateHelperText(msg);
            }
        }
    } else if (operation == "sweep") {
        if (offset + 24 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            ACT805_STATE.sweepSuccess = (result == 0);
            if (result == 0) {
                ACT805_STATE.playCount = ReadInt32LE(body, offset);
                ACT805_STATE.restTime = ReadInt32LE(body, offset);
                ACT805_STATE.rewardMedalCount = ReadInt32LE(body, offset);
                ACT805_STATE.rewardExp = ReadInt32LE(body, offset);
                ACT805_STATE.rewardCoin = ReadInt32LE(body, offset);

                wchar_t msg[256];
                swprintf_s(
                    msg,
                    L"守护梦境：扫荡完成，勋章=%d 经验=%d 铜钱=%d",
                    ACT805_STATE.rewardMedalCount.load(),
                    ACT805_STATE.rewardExp.load(),
                    ACT805_STATE.rewardCoin.load());
                UIBridge::Instance().UpdateHelperText(msg);
            }
        }
    }
}

// ============ 果宝救援功能实现 (Act757) ============

#define ACT757_STATE ActivityStateManager::Instance().GetAct757State()

BOOL SendAct757Packet(const std::string& operation, const std::vector<int32_t>& bodyValues) {
    return SendActivityPacket(Act757::ACTIVITY_ID, operation, bodyValues);
}

BOOL SendAct757OpenUIPacket() {
    g_act757WaitingResponse = true;
    return SendAct757Packet("open_ui", {});
}

BOOL SendAct757StartGamePacket(int promptFlag) {
    g_act757WaitingResponse = true;
    return SendAct757Packet("start_game", {promptFlag});
}

BOOL SendAct757EndGamePacket(int score) {
    const int checkCode = g_act757CheckCode.load();
    int clientCheckCode = 0;
    if (checkCode > 0) {
        clientCheckCode = checkCode & static_cast<int>(g_userId.load() % static_cast<uint32_t>(checkCode));
    }
    g_act757WaitingResponse = true;
    return SendAct757Packet("end_game", {clientCheckCode, score});
}

BOOL SendAct757SweepInfoPacket() {
    g_act757WaitingResponse = true;
    return SendAct757Packet("sweep_info", {});
}

BOOL SendAct757SweepPacket() {
    g_act757WaitingResponse = true;
    return SendAct757Packet("sweep", {});
}

static void ResetAct757State() {
    g_act757PlayCount = 0;
    g_act757RestTime = 0;
    g_act757TotalBadgeNum = 0;
    g_act757BuyCnt = 0;
    g_act757PassCount = 0;
    g_act757IsPop = 0;
    g_act757HistoryBestScore = 0;
    g_act757LastScore = 0;
    g_act757PassFlag = 0;
    g_act757CatchId = 0;
    g_act757CheckCode = 0;
    g_act757StartResult = -1;
    g_act757EndResult = -1;
    g_act757SweepResult = -1;
    g_act757WaitingResponse = false;
    g_act757UseSweep = false;
    g_act757SweepAvailable = false;

    std::lock_guard<std::mutex> lock(g_act757Mutex);
    g_act757PassAwards.assign(3, 0);
    g_act757CatchList.clear();
    g_act757SweepAwards.clear();
    g_act757ResultAwards.clear();
}

static bool WaitForAct757Response() {
    for (int i = 0; i < 30 && g_act757WaitingResponse.load(); ++i) {
        Sleep(100);
    }
    const bool received = !g_act757WaitingResponse.load();
    g_act757WaitingResponse = false;
    return received;
}

DWORD WINAPI Act757ThreadProc(LPVOID lpParam) {
    const bool useSweep = lpParam ? *static_cast<bool*>(lpParam) : false;
    delete static_cast<bool*>(lpParam);

    ACT757_STATE.Reset();
    ResetAct757State();
    g_act757UseSweep = useSweep;
    ACT757_STATE.useSweep = useSweep;

    Sleep(300);
    UIBridge::Instance().UpdateHelperText(L"果宝救援：正在获取活动信息...");

    SendAct757OpenUIPacket();
    if (!WaitForAct757Response()) {
        UIBridge::Instance().UpdateHelperText(L"果宝救援：获取活动信息失败");
        return 0;
    }

    if (g_act757PlayCount.load() <= 0) {
        UIBridge::Instance().UpdateHelperText(L"果宝救援：今日次数已用完");
        return 0;
    }

    if (g_act757RestTime.load() > 0) {
        UIBridge::Instance().UpdateHelperText(L"果宝救援：冷却中，请稍后再试");
        return 0;
    }

    if (useSweep && g_act757HistoryBestScore.load() > 0) {
        Sleep(300);
        UIBridge::Instance().UpdateHelperText(L"果宝救援：正在获取扫荡信息...");
        SendAct757SweepInfoPacket();
        if (WaitForAct757Response() && g_act757SweepAvailable.load()) {
            Sleep(300);
            UIBridge::Instance().UpdateHelperText(L"果宝救援：执行扫荡...");
            SendAct757SweepPacket();
            if (WaitForAct757Response() && g_act757SweepResult.load() == 0) {
                Sleep(300);
                SendAct757OpenUIPacket();
                WaitForAct757Response();
                UIBridge::Instance().UpdateHelperText(L"果宝救援：扫荡完成");
                return 0;
            }
        }

        UIBridge::Instance().UpdateHelperText(L"果宝救援：扫荡失败，改为直接完成");
    } else if (useSweep) {
        UIBridge::Instance().UpdateHelperText(L"果宝救援：需要成功进行一局游戏才可以扫荡哦！");
    }

    Sleep(300);
    UIBridge::Instance().UpdateHelperText(L"果宝救援：开始游戏...");
    SendAct757StartGamePacket(g_act757IsPop.load());
    if (!WaitForAct757Response()) {
        UIBridge::Instance().UpdateHelperText(L"果宝救援：开始游戏失败");
        return 0;
    }

    if (g_act757StartResult.load() != 0) {
        return 0;
    }

    if (g_act757CheckCode.load() == 0) {
        UIBridge::Instance().UpdateHelperText(L"果宝救援：获取校验码失败");
        return 0;
    }

    const int finalScore = Act757::MAX_SCORE;
    Sleep(500);
    UIBridge::Instance().UpdateHelperText(L"果宝救援：直接提交结算...");
    SendAct757EndGamePacket(finalScore);
    if (!WaitForAct757Response()) {
        UIBridge::Instance().UpdateHelperText(L"果宝救援：结算失败");
        return 0;
    }

    Sleep(300);
    SendAct757OpenUIPacket();
    WaitForAct757Response();

    std::vector<std::pair<int, int>> resultAwards;
    {
        std::lock_guard<std::mutex> lock(g_act757Mutex);
        resultAwards = g_act757ResultAwards;
    }

    if (!resultAwards.empty()) {
        const int medal = resultAwards.size() > 0 ? resultAwards[0].second : 0;
        const int coin = resultAwards.size() > 1 ? resultAwards[1].second : 0;
        const int exp = resultAwards.size() > 2 ? resultAwards[2].second : 0;
        wchar_t msg[256];
        swprintf_s(
            msg,
            L"果宝救援：结算完成，分数=%d，疯狂勋章=%d，历练=%d，铜钱=%d",
            finalScore,
            medal,
            exp,
            coin);
        UIBridge::Instance().UpdateHelperText(msg);
    } else {
        UIBridge::Instance().UpdateHelperText(L"果宝救援：结算完成");
    }
    return 0;
}

BOOL StartOneKeyAct757Packet(bool useSweep) {
    bool* pUseSweep = new bool(useSweep);
    HANDLE hThread = CreateThread(nullptr, 0, Act757ThreadProc, pUseSweep, 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
        return TRUE;
    }
    delete pUseSweep;
    return FALSE;
}

void ProcessAct757Response(const GamePacket& packet) {
    size_t offset = 0;
    std::string operation;
    if (!ReadPacketString(packet.body.data(), packet.body.size(), offset, operation)) {
        return;
    }

    const BYTE* body = packet.body.data();
    g_act757WaitingResponse = false;

    if (operation == "open_ui") {
        // open_ui: playCount, frozenTime, buyCnt, totalBadgeNum, passCnt, isPop, historyBestScore, lastScore, passAwards[3], passFlag, catchId, catchList[1][2]
        if (offset + 60 <= packet.body.size()) {
            g_act757PlayCount = ReadInt32LE(body, offset);
            g_act757RestTime = ReadInt32LE(body, offset);
            g_act757BuyCnt = ReadInt32LE(body, offset);
            g_act757TotalBadgeNum = ReadInt32LE(body, offset);
            g_act757PassCount = ReadInt32LE(body, offset);
            g_act757IsPop = ReadInt32LE(body, offset);
            g_act757HistoryBestScore = ReadInt32LE(body, offset);
            g_act757LastScore = ReadInt32LE(body, offset);

            std::vector<int> passAwards;
            passAwards.reserve(3);
            for (int i = 0; i < 3; ++i) {
                passAwards.push_back(ReadInt32LE(body, offset));
            }
            g_act757PassFlag = ReadInt32LE(body, offset);
            g_act757CatchId = ReadInt32LE(body, offset);

            std::vector<std::pair<int, int>> catchList;
            catchList.emplace_back(ReadInt32LE(body, offset), ReadInt32LE(body, offset));

            {
                std::lock_guard<std::mutex> lock(g_act757Mutex);
                g_act757PassAwards = passAwards;
                g_act757CatchList = catchList;
            }

            g_act757SweepAvailable = (g_act757HistoryBestScore.load() > 0);
            ACT757_STATE.playCount = g_act757PlayCount.load();
            ACT757_STATE.restTime = g_act757RestTime.load();
            ACT757_STATE.totalBadgeNum = g_act757TotalBadgeNum.load();
            ACT757_STATE.buyCnt = g_act757BuyCnt.load();
            ACT757_STATE.passCount = g_act757PassCount.load();
            ACT757_STATE.isPop = g_act757IsPop.load();
            ACT757_STATE.historyBestScore = g_act757HistoryBestScore.load();
            ACT757_STATE.lastScore = g_act757LastScore.load();
            ACT757_STATE.passFlag = g_act757PassFlag.load();
            ACT757_STATE.catchId = g_act757CatchId.load();
            ACT757_STATE.checkCode = g_act757CheckCode.load();
            ACT757_STATE.sweepAvailable = g_act757SweepAvailable.load();
            ACT757_STATE.passAwards = passAwards;
            ACT757_STATE.catchList = catchList;

            wchar_t msg[256];
            swprintf_s(
                msg,
                L"果宝救援：次数=%d 冷却=%d秒 勋章=%d",
                g_act757PlayCount.load(),
                g_act757RestTime.load(),
                g_act757TotalBadgeNum.load());
            UIBridge::Instance().UpdateHelperText(msg);
        }
    } else if (operation == "start_game") {
        if (offset + 4 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            g_act757StartResult = result;
            ACT757_STATE.startResult = result;
            if (result == 0 && offset + 8 <= packet.body.size()) {
                g_act757PlayCount = ReadInt32LE(body, offset);
                g_act757CheckCode = ReadInt32LE(body, offset);
                ACT757_STATE.playCount = g_act757PlayCount.load();
                ACT757_STATE.checkCode = g_act757CheckCode.load();
                ACT757_STATE.sweepAvailable = false;
            } else if (result == 10) {
                UIBridge::Instance().UpdateHelperText(L"果宝救援：游戏中！");
            } else if (result == 11) {
                UIBridge::Instance().UpdateHelperText(L"果宝救援：在冷却");
            } else if (result == 12) {
                UIBridge::Instance().UpdateHelperText(L"果宝救援：没次数了");
            } else if (result == 13) {
                UIBridge::Instance().UpdateHelperText(L"果宝救援：玩家正在支付中");
            } else {
                UIBridge::Instance().UpdateHelperText(L"果宝救援：开始游戏失败");
            }
        }
    } else if (operation == "end_game") {
        if (offset + 32 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            g_act757EndResult = result;
            ACT757_STATE.endResult = result;
            g_act757PlayCount = ReadInt32LE(body, offset);
            g_act757RestTime = ReadInt32LE(body, offset);
            g_act757PassCount = ReadInt32LE(body, offset);
            const int score = ReadInt32LE(body, offset);
            const int medal = ReadInt32LE(body, offset);
            const int exp = ReadInt32LE(body, offset);
            const int coin = ReadInt32LE(body, offset);

            ACT757_STATE.playCount = g_act757PlayCount.load();
            ACT757_STATE.restTime = g_act757RestTime.load();
            ACT757_STATE.passCount = g_act757PassCount.load();
            ACT757_STATE.sweepSuccess = (result == 0);

            std::vector<std::pair<int, int>> awards;
            awards.emplace_back(0, medal);
            awards.emplace_back(201, coin);
            awards.emplace_back(202, exp);
            {
                std::lock_guard<std::mutex> lock(g_act757Mutex);
                g_act757ResultAwards = awards;
            }
            ACT757_STATE.resultAwards = awards;

            wchar_t msg[256];
            swprintf_s(msg, L"果宝救援：结算完成，分数=%d", score);
            UIBridge::Instance().UpdateHelperText(msg);
        }
    } else if (operation == "sweep_info") {
        if (offset + 24 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            g_act757SweepResult = result;
            ACT757_STATE.sweepResult = result;
            ACT757_STATE.sweepSuccess = (result == 0);
            if (result == 0 && offset + 20 <= packet.body.size()) {
                const int score = ReadInt32LE(body, offset);
                const int medal = ReadInt32LE(body, offset);
                const int exp = ReadInt32LE(body, offset);
                const int coin = ReadInt32LE(body, offset);
                const int rewardNum = ReadInt32LE(body, offset);
                std::vector<std::pair<int, int>> rewards;
                for (int i = 0; i < rewardNum && offset + 8 <= packet.body.size(); ++i) {
                    const int id = ReadInt32LE(body, offset);
                    const int num = ReadInt32LE(body, offset);
                    if (num <= 0) {
                        break;
                    }
                    rewards.emplace_back(id, num);
                }

                std::vector<std::pair<int, int>> awards;
                BuildAct757AwardList(medal, exp, coin, rewards, awards);
                {
                    std::lock_guard<std::mutex> lock(g_act757Mutex);
                    g_act757SweepAwards = awards;
                }
                ACT757_STATE.sweepAwards = awards;
                g_act757SweepAvailable = true;
                ACT757_STATE.sweepAvailable = true;
                (void)score;
                UIBridge::Instance().UpdateHelperText(L"果宝救援：已获取扫荡信息");
            } else if (result == 1) {
                UIBridge::Instance().UpdateHelperText(L"果宝救援：需要成功进行一局游戏才可以扫荡哦！");
            } else if (result == 2) {
                UIBridge::Instance().UpdateHelperText(L"果宝救援：游戏次数不足");
            } else if (result == 3) {
                UIBridge::Instance().UpdateHelperText(L"果宝救援：冷却中！");
            } else {
                ACT757_STATE.sweepAvailable = false;
                UIBridge::Instance().UpdateHelperText(L"果宝救援：扫荡失败");
            }
        }
    } else if (operation == "sweep") {
        if (offset + 36 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            g_act757SweepResult = result;
            ACT757_STATE.sweepResult = result;
            ACT757_STATE.sweepSuccess = (result == 0);
            g_act757BuyCnt = ReadInt32LE(body, offset);
            g_act757RestTime = ReadInt32LE(body, offset);
            g_act757PassCount = ReadInt32LE(body, offset);
            const int score = ReadInt32LE(body, offset);
            const int medal = ReadInt32LE(body, offset);
            const int exp = ReadInt32LE(body, offset);
            const int coin = ReadInt32LE(body, offset);
            const int rewardNum = ReadInt32LE(body, offset);
            std::vector<std::pair<int, int>> rewards;
            for (int i = 0; i < rewardNum && offset + 8 <= packet.body.size(); ++i) {
                const int id = ReadInt32LE(body, offset);
                const int num = ReadInt32LE(body, offset);
                if (num <= 0) {
                    break;
                }
                rewards.emplace_back(id, num);
            }

            std::vector<std::pair<int, int>> awards;
            BuildAct757AwardList(medal, exp, coin, rewards, awards);
            {
                std::lock_guard<std::mutex> lock(g_act757Mutex);
                g_act757ResultAwards = awards;
            }
            ACT757_STATE.resultAwards = awards;

            g_act757SweepAvailable = true;
            ACT757_STATE.buyCnt = g_act757BuyCnt.load();
            ACT757_STATE.restTime = g_act757RestTime.load();
            ACT757_STATE.passCount = g_act757PassCount.load();
            ACT757_STATE.sweepAvailable = true;
            ACT757_STATE.checkCode = g_act757CheckCode.load();

            wchar_t msg[256];
            swprintf_s(msg, L"果宝救援：扫荡完成，分数=%d", score);
            UIBridge::Instance().UpdateHelperText(msg);
        }
    }
}

// ============ 妖力考验功能实现 (Act631) ============

#define ACT631_STATE ActivityStateManager::Instance().GetAct631State()

static BOOL SendAct631DailyTaskPacket() {
    std::vector<BYTE> packet = PacketBuilder()
                                   .SetOpcode(Opcode::TASK_DAILY_REPORT_SEND)
                                   .SetParams(3)
                                   .WriteInt32(3)
                                   .WriteInt32(Act631::DAILY_TASK_ID)
                                   .WriteInt32(0)
                                   .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

BOOL SendAct631Packet(const std::string& operation, const std::vector<int32_t>& bodyValues) {
    return SendActivityPacket(Act631::ACTIVITY_ID, operation, bodyValues);
}

BOOL SendAct631OpenUIPacket() {
    ACT631_STATE.waitingResponse = true;
    return SendAct631Packet("open_ui", {});
}

BOOL SendAct631StartGamePacket(int ruleFlag) {
    ACT631_STATE.waitingResponse = true;
    return SendAct631Packet("start_game", {ruleFlag});
}

BOOL SendAct631EndGamePacket(int isPass, int clientCheckCode, int score) {
    ACT631_STATE.waitingResponse = true;
    const BOOL sent = SendAct631Packet("end_game", {isPass, clientCheckCode, score});
    if (sent) {
        SendAct631DailyTaskPacket();
    }
    return sent;
}

BOOL SendAct631SweepInfoPacket() {
    ACT631_STATE.waitingResponse = true;
    return SendAct631Packet("sweep_info", {});
}

BOOL SendAct631SweepPacket() {
    ACT631_STATE.waitingResponse = true;
    return SendAct631Packet("sweep", {});
}

DWORD WINAPI Act631ThreadProc(LPVOID lpParam) {
    (void)lpParam;

    const bool useSweep = ACT631_STATE.useSweep.load();
    ACT631_STATE.Reset();
    ACT631_STATE.useSweep = useSweep;

    auto waitForResponse = []() -> bool {
        for (int i = 0; i < 30 && ACT631_STATE.waitingResponse; ++i) {
            Sleep(100);
        }
        const bool received = !ACT631_STATE.waitingResponse.load();
        ACT631_STATE.waitingResponse = false;
        return received;
    };

    auto waitForSettlementResponse = []() -> bool {
        for (int i = 0; i < 60 && ACT631_STATE.waitingResponse; ++i) {
            Sleep(100);
        }
        const bool received = !ACT631_STATE.waitingResponse.load();
        ACT631_STATE.waitingResponse = false;
        return received;
    };

    Sleep(300);
    UIBridge::Instance().UpdateHelperText(L"妖力考验：正在获取活动信息...");

    SendAct631OpenUIPacket();
    if (!waitForResponse()) {
        UIBridge::Instance().UpdateHelperText(L"妖力考验：获取活动信息失败");
        return 0;
    }

    if (ACT631_STATE.playCount.load() <= 0) {
        UIBridge::Instance().UpdateHelperText(L"妖力考验：今日次数已用完");
        return 0;
    }

    if (ACT631_STATE.restTime.load() > 0) {
        UIBridge::Instance().UpdateHelperText(L"妖力考验：冷却中，请稍后再试");
        return 0;
    }

    // 自动判定：可扫荡且扫荡成绩 >= 游戏流程提交分（PASS_SCORE）时才扫荡
    if (ShouldAutoSweep(ACT631_STATE.maxScore.load(),
                        Act631::PASS_SCORE)) {
        Sleep(300);
        UIBridge::Instance().UpdateHelperText(L"妖力考验：正在获取扫荡信息...");
        ACT631_STATE.sweepSuccess = false;

        if (SendAct631SweepInfoPacket() && waitForResponse() && ACT631_STATE.sweepSuccess.load()) {
            Sleep(300);
            UIBridge::Instance().UpdateHelperText(L"妖力考验：执行扫荡...");

            if (SendAct631SweepPacket() && waitForResponse()) {
                Sleep(300);
                SendAct631OpenUIPacket();
                waitForResponse();
                UIBridge::Instance().UpdateHelperText(L"妖力考验：扫荡完成");
                return 0;
            }
        }

        UIBridge::Instance().UpdateHelperText(L"妖力考验：扫荡失败，改为直接完成");
    }

    Sleep(300);
    UIBridge::Instance().UpdateHelperText(L"妖力考验：开始游戏...");

    int ruleFlag = ACT631_STATE.ruleFlag.load();
    if (ruleFlag < 1) {
        ruleFlag = 1;
    }

    if (!SendAct631StartGamePacket(ruleFlag) || !waitForResponse()) {
        UIBridge::Instance().UpdateHelperText(L"妖力考验：开始游戏失败");
        return 0;
    }

    const int startResult = ACT631_STATE.startResult.load();
    if (startResult != 0) {
        if (startResult == 10) {
            UIBridge::Instance().UpdateHelperText(L"妖力考验：当前已经在游戏中");
        } else if (startResult == 11) {
            UIBridge::Instance().UpdateHelperText(L"妖力考验：冷却中，请稍后再试");
        } else if (startResult == 12) {
            UIBridge::Instance().UpdateHelperText(L"妖力考验：今日次数已用完");
        } else if (startResult == 13) {
            UIBridge::Instance().UpdateHelperText(L"妖力考验：玩家正在支付中");
        } else {
            UIBridge::Instance().UpdateHelperText(L"妖力考验：开始游戏失败");
        }
        return 0;
    }

    if (ACT631_STATE.checkCode.load() == 0) {
        UIBridge::Instance().UpdateHelperText(L"妖力考验：获取校验码失败");
        return 0;
    }

    const int score = Act631::PASS_SCORE;
    const int isPass = (score >= Act631::PASS_SCORE) ? 1 : 2;
    const int clientCheckCode = ACT631_STATE.checkCode.load()
        + static_cast<int>(g_userId.load() % 100)
        + isPass
        + score;

    Sleep(500);
    UIBridge::Instance().UpdateHelperText(L"妖力考验：直接提交结算...");
    (void)SendAct631EndGamePacket(isPass, clientCheckCode, score);
    if (!waitForSettlementResponse()) {
        UIBridge::Instance().UpdateHelperText(L"妖力考验：结算失败");
        return 0;
    }

    Sleep(300);
    SendAct631OpenUIPacket();
    waitForResponse();
    UIBridge::Instance().UpdateHelperText(L"妖力考验：结算完成");
    return 0;
}

BOOL StartOneKeyAct631Packet(bool useSweep) {
    ACT631_STATE.useSweep = useSweep;
    HANDLE hThread = CreateThread(nullptr, 0, Act631ThreadProc, nullptr, 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
        return TRUE;
    }
    return FALSE;
}

void ProcessAct631Response(const GamePacket& packet) {
    size_t offset = 0;
    std::string operation;
    if (!ReadPacketString(packet.body.data(), packet.body.size(), offset, operation)) {
        return;
    }

    const BYTE* body = packet.body.data();
    ACT631_STATE.waitingResponse = false;

    if (operation == "open_ui") {
        if (offset + 52 <= packet.body.size()) {
            ACT631_STATE.playCount = ReadInt32LE(body, offset);
            ACT631_STATE.restTime = ReadInt32LE(body, offset);
            ACT631_STATE.bubbleNum = ReadInt32LE(body, offset);
            ACT631_STATE.flag = ReadInt32LE(body, offset);

            if (ACT631_STATE.rewardList.size() < 3) {
                ACT631_STATE.rewardList.assign(3, 0);
            }
            for (size_t i = 0; i < 3; ++i) {
                ACT631_STATE.rewardList[i] = ReadInt32LE(body, offset);
            }

            ACT631_STATE.passCount = ReadInt32LE(body, offset);
            ACT631_STATE.maxScore = ReadInt32LE(body, offset);
            ACT631_STATE.ruleFlag = ReadInt32LE(body, offset);
            ReadInt32LE(body, offset);  // skip

            if (ACT631_STATE.catchList.size() < 2) {
                ACT631_STATE.catchList.assign(2, 0);
            }
            ACT631_STATE.catchList[0] = ReadInt32LE(body, offset);
            ACT631_STATE.catchList[1] = ReadInt32LE(body, offset);

            ACT631_STATE.sweepAvailable = (ACT631_STATE.passCount.load() > 0 || ACT631_STATE.maxScore.load() > 0);

            wchar_t msg[256];
            swprintf_s(
                msg,
                L"妖力考验：次数=%d 冷却=%d秒 勋章=%d",
                ACT631_STATE.playCount.load(),
                ACT631_STATE.restTime.load(),
                ACT631_STATE.bubbleNum.load());
            UIBridge::Instance().UpdateHelperText(msg);
        }
    } else if (operation == "start_game") {
        if (offset + 4 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            ACT631_STATE.startResult = result;
            if (result == 0 && offset + 8 <= packet.body.size()) {
                ACT631_STATE.playCount = ReadInt32LE(body, offset);
                ACT631_STATE.checkCode = ReadInt32LE(body, offset);
            } else if (result == 10) {
                UIBridge::Instance().UpdateHelperText(L"妖力考验：当前已经在游戏中");
            } else if (result == 11) {
                UIBridge::Instance().UpdateHelperText(L"妖力考验：冷却中，请稍后再试");
            } else if (result == 12) {
                UIBridge::Instance().UpdateHelperText(L"妖力考验：今日次数已用完");
            } else if (result == 13) {
                UIBridge::Instance().UpdateHelperText(L"妖力考验：玩家正在支付中");
            } else {
                UIBridge::Instance().UpdateHelperText(L"妖力考验：开始游戏失败");
            }
        }
    } else if (operation == "end_game") {
        if (offset + 44 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            ACT631_STATE.endResult = result;
            ACT631_STATE.playCount = ReadInt32LE(body, offset);
            ACT631_STATE.restTime = ReadInt32LE(body, offset);
            ACT631_STATE.bubbleNum = ReadInt32LE(body, offset);
            ACT631_STATE.rewardExp = ReadInt32LE(body, offset);
            ACT631_STATE.rewardCoin = ReadInt32LE(body, offset);
            ACT631_STATE.isVip = ReadInt32LE(body, offset);
            ACT631_STATE.isFirst = ReadInt32LE(body, offset);
            ACT631_STATE.score = ReadInt32LE(body, offset);
            ACT631_STATE.maxScore = ReadInt32LE(body, offset);
            ACT631_STATE.passCount = ReadInt32LE(body, offset);
            ACT631_STATE.isPass = (ACT631_STATE.score.load() >= Act631::PASS_SCORE) ? 1 : 2;
            ACT631_STATE.sweepAvailable = (ACT631_STATE.passCount.load() > 0 || ACT631_STATE.maxScore.load() > 0);

            wchar_t msg[256];
            swprintf_s(
                msg,
                L"妖力考验：结算完成，分数=%d 最高分=%d",
                ACT631_STATE.score.load(),
                ACT631_STATE.maxScore.load());
            UIBridge::Instance().UpdateHelperText(msg);
        }
    } else if (operation == "sweep_info") {
        if (offset + 20 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            ACT631_STATE.sweepResult = result;
            if (result == 0) {
                std::vector<std::pair<int, int>> awardList;
                awardList.reserve(8);
                awardList.emplace_back(0, ReadInt32LE(body, offset));
                awardList.emplace_back(202, ReadInt32LE(body, offset));
                awardList.emplace_back(201, ReadInt32LE(body, offset));
                const int len = ReadInt32LE(body, offset);
                for (int i = 0; i < len && offset + 8 <= packet.body.size(); ++i) {
                    const int id = ReadInt32LE(body, offset);
                    const int num = ReadInt32LE(body, offset);
                    awardList.emplace_back(id, num);
                }
                ACT631_STATE.sweepSuccess = true;
                ACT631_STATE.sweepAvailable = true;
                (void)awardList;
                UIBridge::Instance().UpdateHelperText(L"妖力考验：已获取扫荡信息");
            } else {
                ACT631_STATE.sweepSuccess = false;
                ACT631_STATE.sweepAvailable = false;
                UIBridge::Instance().UpdateHelperText(L"妖力考验：需要成功进行一局游戏才可以扫荡哦！");
            }
        }
    } else if (operation == "sweep") {
        if (offset + 28 <= packet.body.size()) {
            const int result = ReadInt32LE(body, offset);
            ACT631_STATE.sweepResult = result;
            ACT631_STATE.sweepSuccess = true;
            ACT631_STATE.playCount = ReadInt32LE(body, offset);
            ACT631_STATE.restTime = ReadInt32LE(body, offset);
            ACT631_STATE.bubbleNum = ReadInt32LE(body, offset);
            ACT631_STATE.rewardExp = ReadInt32LE(body, offset);
            ACT631_STATE.rewardCoin = ReadInt32LE(body, offset);
            ACT631_STATE.maxScore = ReadInt32LE(body, offset);
            ACT631_STATE.sweepAvailable = (ACT631_STATE.maxScore.load() > 0 || ACT631_STATE.passCount.load() > 0);

            wchar_t msg[256];
            swprintf_s(
                msg,
                L"妖力考验：扫荡完成，分数=%d",
                ACT631_STATE.maxScore.load());
            UIBridge::Instance().UpdateHelperText(msg);
        }
    }
}

// ============ 欢乐跷跷板功能实现 (Act808) ============

    static constexpr int ACT808_WAIT_NONE = 0;
    static constexpr int ACT808_WAIT_OPEN_UI = 1;
    static constexpr int ACT808_WAIT_START_GAME = 2;
    static constexpr int ACT808_WAIT_END_GAME = 3;
    static constexpr int ACT808_WAIT_SWEEP_INFO = 4;
    static constexpr int ACT808_WAIT_SWEEP = 5;

    static std::atomic<int> g_act808PlayCount{0};
    static std::atomic<int> g_act808RestTime{0};
    static std::atomic<int> g_act808MedalNum{0};
    static std::atomic<int> g_act808HistoryBestScore{0};
    static std::atomic<int> g_act808IsPop{0};
    static std::atomic<int> g_act808RandomCode{0};
    static std::atomic<int> g_act808CheckCode{0};
    static std::atomic<int> g_act808StartResult{0};
    static std::atomic<int> g_act808EndResult{0};
    static std::atomic<int> g_act808SweepResult{0};
    static std::atomic<int> g_act808WaitingOp{ACT808_WAIT_NONE};
    static std::atomic<bool> g_act808WaitingResponse{false};
    static std::atomic<bool> g_act808SweepAvailable{false};
    static std::mutex g_act808Mutex;
    static std::vector<int> g_act808AwardArr;

    static void ResetAct808State() {
        g_act808PlayCount = 0;
        g_act808RestTime = 0;
        g_act808MedalNum = 0;
        g_act808HistoryBestScore = 0;
        g_act808IsPop = 0;
        g_act808RandomCode = 0;
        g_act808CheckCode = 0;
        g_act808StartResult = 0;
        g_act808EndResult = 0;
        g_act808SweepResult = 0;
        g_act808WaitingOp = ACT808_WAIT_NONE;
        g_act808WaitingResponse = false;
        g_act808SweepAvailable = false;

        std::lock_guard<std::mutex> lock(g_act808Mutex);
        g_act808AwardArr.clear();
    }

    static void ClearAct808WaitingState() {
        g_act808WaitingResponse = false;
        g_act808WaitingOp = ACT808_WAIT_NONE;
    }

    static bool WaitForAct808Response() {
        for (int i = 0; i < 30 && g_act808WaitingResponse.load(); ++i) {
            Sleep(100);
        }

        const bool received = !g_act808WaitingResponse.load();
        ClearAct808WaitingState();
        return received;
    }

    BOOL SendAct808Packet(const std::string& operation, const std::vector<int32_t>& bodyValues) {
        std::vector<BYTE> packet = BuildActivityPacket(
            Opcode::ACTIVITY_QINGYANG_NEW_SEND,
            Act808::ACTIVITY_ID,
            operation,
            bodyValues);
        return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
    }

    static BOOL SendAct808DailyTaskPacket() {
        std::vector<BYTE> packet = PacketBuilder()
            .SetOpcode(Opcode::TASK_DAILY_REPORT_SEND)
            .SetParams(3)
            .WriteInt32(3)
            .WriteInt32(Act808::DAILY_TASK_ID)
            .WriteInt32(0)
            .Build();

        return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
    }

    static int32_t BuildAct808ClientCheckCode() {
        const int64_t randomCode = static_cast<int64_t>(g_act808RandomCode.load());
        const int64_t checkCode = static_cast<int64_t>(g_act808CheckCode.load());
        const int64_t userIdMod = static_cast<int64_t>(g_userId.load() % 1000u);
        return static_cast<int32_t>(randomCode * userIdMod * (checkCode + 1));
    }

    BOOL SendAct808OpenUIPacket() {
        g_act808WaitingOp = ACT808_WAIT_OPEN_UI;
        g_act808WaitingResponse = true;
        if (SendAct808Packet("open_ui", {})) {
            return TRUE;
        }
        ClearAct808WaitingState();
        return FALSE;
    }

    BOOL SendAct808StartGamePacket(int popRule) {
        g_act808WaitingOp = ACT808_WAIT_START_GAME;
        g_act808WaitingResponse = true;
        if (SendAct808Packet("start_game", {popRule})) {
            return TRUE;
        }
        ClearAct808WaitingState();
        return FALSE;
    }

    BOOL SendAct808GameingPacket(int index) {
        if (g_act808RandomCode.load() == 0 || g_act808CheckCode.load() == 0) {
            return FALSE;
        }

        std::vector<BYTE> packet = BuildActivityPacket(
            Opcode::ACTIVITY_QINGYANG_NEW_SEND,
            Act808::ACTIVITY_ID,
            "gameing",
            {index, BuildAct808ClientCheckCode()});
        return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
    }

    BOOL SendAct808EndGamePacket(int score) {
        if (g_act808RandomCode.load() == 0 || g_act808CheckCode.load() == 0) {
            return FALSE;
        }

        g_act808WaitingOp = ACT808_WAIT_END_GAME;
        g_act808WaitingResponse = true;
        if (SendAct808Packet("end_game", {0, BuildAct808ClientCheckCode(), score})) {
            SendAct808DailyTaskPacket();
            return TRUE;
        }
        ClearAct808WaitingState();
        return FALSE;
    }

    BOOL SendAct808SweepInfoPacket() {
        g_act808WaitingOp = ACT808_WAIT_SWEEP_INFO;
        g_act808WaitingResponse = true;
        if (SendAct808Packet("sweep_info", {})) {
            return TRUE;
        }
        ClearAct808WaitingState();
        return FALSE;
    }

    BOOL SendAct808SweepPacket() {
        g_act808WaitingOp = ACT808_WAIT_SWEEP;
        g_act808WaitingResponse = true;
        if (SendAct808Packet("sweep", {})) {
            return TRUE;
        }
        ClearAct808WaitingState();
        return FALSE;
    }

    DWORD WINAPI Act808ThreadProc(LPVOID lpParam) {
        delete static_cast<bool*>(lpParam);

        ResetAct808State();

        Sleep(300);
        UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：正在获取活动信息...");

        if (!SendAct808OpenUIPacket() || !WaitForAct808Response()) {
            UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：获取活动信息失败");
            return 0;
        }

        if (g_act808PlayCount.load() <= 0) {
            UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：今日次数已用完");
            return 0;
        }

        if (g_act808RestTime.load() > 0) {
            UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：冷却中，请稍后再试");
            return 0;
        }

        // 自动判定：可扫荡且历史最好成绩 >= 游戏流程提交分（MAX_SCORE）时才扫荡
        if (ShouldAutoSweep(g_act808HistoryBestScore.load(),
                            Act808::MAX_SCORE)) {
            Sleep(300);
            UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：正在获取扫荡信息...");

            if (SendAct808SweepInfoPacket() && WaitForAct808Response() && g_act808SweepAvailable.load()) {
                Sleep(300);
                UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：执行扫荡...");

                if (SendAct808SweepPacket() && WaitForAct808Response() && g_act808SweepResult.load() == 0) {
                    Sleep(300);
                    SendAct808OpenUIPacket();
                    WaitForAct808Response();
                    UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：扫荡完成");
                    return 0;
                }
            }

            UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：扫荡失败，改为直接完成");
        }

        Sleep(300);
        UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：开始游戏...");

        const int startFlag = g_act808IsPop.load();
        if (!SendAct808StartGamePacket(startFlag) || !WaitForAct808Response()) {
            UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：开始游戏失败");
            return 0;
        }

        const int startResult = g_act808StartResult.load();
        if (startResult != 0) {
            if (startResult == 10) {
                UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：当前已经在游戏中");
            } else if (startResult == 11) {
                UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：冷却中，请稍后再试");
            } else if (startResult == 12) {
                UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：今日次数已用光");
            } else if (startResult == 13) {
                UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：玩家正在支付中");
            } else {
                UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：开始游戏失败");
            }
            return 0;
        }

        std::vector<int> awardArr;
        {
            std::lock_guard<std::mutex> lock(g_act808Mutex);
            awardArr = g_act808AwardArr;
        }

        if (awardArr.empty()) {
            UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：未读取到开局数据");
            return 0;
        }

        size_t sentCount = 0;
        for (int itemType : awardArr) {
            if (sentCount > 0 && (sentCount % 12 == 0)) {
                Sleep(12);
            }

            if (itemType <= 0 || itemType == 30 || itemType > 15) {
                continue;
            }

            if (!SendAct808GameingPacket(itemType)) {
                UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：过程上报失败");
                return 0;
            }
            ++sentCount;
        }

        const int score = Act808::MAX_SCORE;
        Sleep(300);
        UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：提交结算...");

        if (!SendAct808EndGamePacket(score) || !WaitForAct808Response()) {
            UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：结算失败");
            return 0;
        }

        if (g_act808EndResult.load() != 0) {
            UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：结算失败");
            return 0;
        }

        if (score > g_act808HistoryBestScore.load()) {
            g_act808HistoryBestScore = score;
        }

        Sleep(300);
        SendAct808OpenUIPacket();
        WaitForAct808Response();

        UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：完成");
        return 0;
    }

    BOOL StartOneKeyAct808Packet(bool useSweep) {
        bool* pUseSweep = new bool(useSweep);
        HANDLE hThread = CreateThread(nullptr, 0, Act808ThreadProc, pUseSweep, 0, nullptr);
        if (hThread) {
            CloseHandle(hThread);
            return TRUE;
        }

        delete pUseSweep;
        return FALSE;
    }

    void ProcessAct808Response(const GamePacket& packet) {
        size_t offset = 0;
        std::string operation;
        if (!ReadPacketString(packet.body.data(), packet.body.size(), offset, operation)) {
            return;
        }

        const BYTE* body = packet.body.data();
        const int waitingOp = g_act808WaitingOp.load();
        const bool shouldClearWaiting =
            g_act808WaitingResponse.load() &&
            ((waitingOp == ACT808_WAIT_OPEN_UI && operation == "open_ui") ||
             (waitingOp == ACT808_WAIT_START_GAME && operation == "start_game") ||
             (waitingOp == ACT808_WAIT_END_GAME && operation == "end_game") ||
             (waitingOp == ACT808_WAIT_SWEEP_INFO && operation == "sweep_info") ||
             (waitingOp == ACT808_WAIT_SWEEP && operation == "sweep"));

        if (shouldClearWaiting) {
            ClearAct808WaitingState();
        }

        if (operation == "open_ui") {
            if (offset + 32 <= packet.body.size()) {
                g_act808PlayCount = ReadInt32LE(body, offset);
                g_act808RestTime = ReadInt32LE(body, offset);
                g_act808MedalNum = ReadInt32LE(body, offset);
                g_act808HistoryBestScore = ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);  // catchNum
                ReadInt32LE(body, offset);  // isBuy
                ReadInt32LE(body, offset);  // maxNum
                g_act808IsPop = ReadInt32LE(body, offset);
                g_act808SweepAvailable = (g_act808HistoryBestScore.load() > 0);

                wchar_t msg[256];
                swprintf_s(
                    msg,
                    L"欢乐跷跷板：次数=%d 冷却=%d秒 棒棒糖=%d",
                    g_act808PlayCount.load(),
                    g_act808RestTime.load(),
                    g_act808MedalNum.load());
                UIBridge::Instance().UpdateHelperText(msg);
            }
        } else if (operation == "start_game") {
            if (offset + 4 <= packet.body.size()) {
                const int32_t result = ReadInt32LE(body, offset);
                g_act808StartResult = result;
                if (result == 0 && offset + 12 <= packet.body.size()) {
                    g_act808PlayCount = ReadInt32LE(body, offset);
                    g_act808RandomCode = ReadInt32LE(body, offset);
                    g_act808CheckCode = ReadInt32LE(body, offset);

                    std::vector<int> awardArr;
                    awardArr.reserve(180);
                    while (offset + 4 <= packet.body.size()) {
                        awardArr.push_back(ReadInt32LE(body, offset));
                    }

                    {
                        std::lock_guard<std::mutex> lock(g_act808Mutex);
                        g_act808AwardArr = awardArr;
                    }
                } else if (result == 10) {
                    UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：当前已经在游戏中");
                } else if (result == 11) {
                    UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：冷却中，请稍后再试");
                } else if (result == 12) {
                    UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：今日次数已用光");
                } else if (result == 13) {
                    UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：玩家正在支付中");
                }
            }
        } else if (operation == "gameing") {
            if (offset + 8 <= packet.body.size()) {
                ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
            }
        } else if (operation == "end_game") {
            if (offset + 28 <= packet.body.size()) {
                const int32_t result = ReadInt32LE(body, offset);
                g_act808EndResult = result;
                g_act808PlayCount = ReadInt32LE(body, offset);
                g_act808RestTime = ReadInt32LE(body, offset);
                g_act808MedalNum = ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);  // totalNum
                ReadInt32LE(body, offset);  // exp
                ReadInt32LE(body, offset);  // coin
                const int rewardNum = ReadInt32LE(body, offset);
                for (int i = 0; i < rewardNum && offset + 8 <= packet.body.size(); ++i) {
                    ReadInt32LE(body, offset);
                    ReadInt32LE(body, offset);
                }

                if (result == 0) {
                    wchar_t msg[256];
                    swprintf_s(
                        msg,
                        L"欢乐跷跷板：结算完成，棒棒糖=%d",
                        g_act808MedalNum.load());
                    UIBridge::Instance().UpdateHelperText(msg);
                }
            }
        } else if (operation == "sweep_info") {
            if (offset + 4 <= packet.body.size()) {
                const int32_t result = ReadInt32LE(body, offset);
                g_act808SweepResult = result;
                if (result == 0 && offset + 16 <= packet.body.size()) {
                    ReadInt32LE(body, offset);  // medal
                    ReadInt32LE(body, offset);  // exp
                    ReadInt32LE(body, offset);  // coin
                    const int rewardNum = ReadInt32LE(body, offset);
                    for (int i = 0; i < rewardNum && offset + 8 <= packet.body.size(); ++i) {
                        ReadInt32LE(body, offset);
                        ReadInt32LE(body, offset);
                    }
                    g_act808SweepAvailable = true;
                    UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：已获取扫荡信息");
                } else {
                    g_act808SweepAvailable = false;
                    if (result == 1) {
                        UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：需要成功进行一局游戏才可以扫荡哦！");
                    } else if (result == 2) {
                        UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：游戏次数不足");
                    } else if (result == 3) {
                        UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：冷却中！");
                    }
                }
            }
        } else if (operation == "sweep") {
            if (offset + 28 <= packet.body.size()) {
                const int32_t result = ReadInt32LE(body, offset);
                // 线程判扫荡成功读的是 g_act808SweepResult；原先此处只写 EndResult，
                // 导致判定一直沿用 sweep_info 的旧值。两个标记同时更新。
                g_act808SweepResult = result;
                g_act808EndResult = result;
                g_act808PlayCount = ReadInt32LE(body, offset);
                g_act808RestTime = ReadInt32LE(body, offset);
                g_act808MedalNum = ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);  // totalNum
                ReadInt32LE(body, offset);  // exp
                ReadInt32LE(body, offset);  // coin
                const int rewardNum = ReadInt32LE(body, offset);
                for (int i = 0; i < rewardNum && offset + 8 <= packet.body.size(); ++i) {
                    ReadInt32LE(body, offset);
                    ReadInt32LE(body, offset);
                }

                if (result == 0) {
                    wchar_t msg[256];
                    swprintf_s(
                        msg,
                        L"欢乐跷跷板：扫荡完成，棒棒糖=%d",
                        g_act808MedalNum.load());
                    UIBridge::Instance().UpdateHelperText(msg);
                } else {
                    UIBridge::Instance().UpdateHelperText(L"欢乐跷跷板：扫荡失败");
                }
            }
        }
    }

    // ============ 摘取大力果实功能实现 (Act782) ============

    BOOL SendAct782Packet(const std::string& operation, const std::vector<int32_t>& bodyValues) {
        return SendActivityPacket(Act782::ACTIVITY_ID, operation, bodyValues);
    }

    BOOL SendAct782OpenUIPacket() {
        g_act782WaitingResponse = true;
        return SendAct782Packet("open_ui", {});
    }

    BOOL SendAct782StartGamePacket(int ruleFlag) {
        g_act782WaitingResponse = true;
        return SendAct782Packet("start_game", {ruleFlag});
    }

    BOOL SendAct782EndGamePacket(int score) {
        const int randomCode = g_act782RandomCode.load();
        const int serverCheckCode = g_act782CheckCode.load();
        if (randomCode == 0 || serverCheckCode == 0) {
            return FALSE;
        }

        const long long clientCheckCode =
            static_cast<long long>(randomCode)
            * static_cast<long long>(g_userId.load() % 1000)
            * static_cast<long long>(serverCheckCode + 1);

        g_act782WaitingResponse = true;
        return SendAct782Packet("end_game", {0, score, static_cast<int32_t>(clientCheckCode)});
    }

    BOOL SendAct782SweepInfoPacket() {
        g_act782WaitingResponse = true;
        return SendAct782Packet("sweep_info", {});
    }

    BOOL SendAct782SweepPacket() {
        g_act782WaitingResponse = true;
        return SendAct782Packet("sweep", {});
    }

    DWORD WINAPI Act782ThreadProc(LPVOID lpParam) {
        const int targetScore = lpParam ? *static_cast<int*>(lpParam) : Act782::TARGET_SCORE;
        if (lpParam) {
            delete static_cast<int*>(lpParam);
        }

        g_act782CheckCode = 0;
        g_act782RandomCode = 0;
        g_act782LastResult = -1;
        g_act782LastScore = 0;
        g_act782LastMedal = 0;
        g_act782LastExp = 0;
        g_act782LastCoin = 0;
        g_act782SweepAvailable = false;

        Sleep(300);
        UIBridge::Instance().UpdateHelperText(L"摘取大力果实：正在获取活动信息...");

        SendAct782OpenUIPacket();
        for (int i = 0; i < 30 && g_act782WaitingResponse.load(); ++i) {
            Sleep(100);
        }

        if (g_act782PlayCount.load() <= 0) {
            UIBridge::Instance().UpdateHelperText(L"摘取大力果实：今日次数已用完");
            return 0;
        }

        if (g_act782RestTime.load() > 0) {
            UIBridge::Instance().UpdateHelperText(L"摘取大力果实：冷却中，请稍后再试");
            return 0;
        }

        if (g_act782UseSweep.load()) {
            UIBridge::Instance().UpdateHelperText(L"摘取大力果实：正在获取扫荡信息...");

            SendAct782SweepInfoPacket();
            for (int i = 0; i < 30 && g_act782WaitingResponse.load(); ++i) {
                Sleep(100);
            }

            if (g_act782SweepAvailable.load()) {
                Sleep(300);
                UIBridge::Instance().UpdateHelperText(L"摘取大力果实：执行扫荡...");
                SendAct782SweepPacket();
                for (int i = 0; i < 30 && g_act782WaitingResponse.load(); ++i) {
                    Sleep(100);
                }
                if (g_act782LastResult.load() != 3) {
                    return 0;
                }
                UIBridge::Instance().UpdateHelperText(L"摘取大力果实：扫荡失败");
                return 0;
            }

            UIBridge::Instance().UpdateHelperText(L"摘取大力果实：当前不可扫荡，改为400分结算");
        }

        Sleep(300);
        UIBridge::Instance().UpdateHelperText(L"摘取大力果实：开始游戏...");
        SendAct782StartGamePacket(g_act782RuleFlag.load());
        for (int i = 0; i < 30 && g_act782WaitingResponse.load(); ++i) {
            Sleep(100);
        }

        if (g_act782CheckCode.load() == 0 || g_act782RandomCode.load() == 0) {
            UIBridge::Instance().UpdateHelperText(L"摘取大力果实：获取校验码失败");
            return 0;
        }

        Sleep(500);
        UIBridge::Instance().UpdateHelperText(L"摘取大力果实：跳过客户端渲染，直接提交400分...");
        SendAct782EndGamePacket(targetScore);
        for (int i = 0; i < 30 && g_act782WaitingResponse.load(); ++i) {
            Sleep(100);
        }

        if (g_act782LastResult.load() == 3) {
            UIBridge::Instance().UpdateHelperText(L"摘取大力果实：结算失败");
        }

        return 0;
    }

    BOOL StartOneKeyAct782Packet(bool useSweep, int targetScore) {
        g_act782UseSweep = useSweep;
        int* pTargetScore = new int(targetScore);
        HANDLE hThread = CreateThread(nullptr, 0, Act782ThreadProc, pTargetScore, 0, nullptr);
        if (hThread) {
            CloseHandle(hThread);
            return TRUE;
        }
        delete pTargetScore;
        return FALSE;
    }

    void ProcessAct782Response(const GamePacket& packet) {
        size_t offset = 0;
        std::string operation;
        if (!ReadPacketString(packet.body.data(), packet.body.size(), offset, operation)) {
            return;
        }

        const BYTE* body = packet.body.data();
        g_act782WaitingResponse = false;

        if (operation == "open_ui") {
            if (offset + 32 <= packet.body.size()) {
                g_act782PlayCount = ReadInt32LE(body, offset);
                g_act782RestTime = ReadInt32LE(body, offset);
                g_act782BubbleNum = ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                g_act782RuleFlag = ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);

                wchar_t msg[256];
                swprintf_s(
                    msg,
                    L"摘取大力果实：次数=%d 冷却=%d秒 果实=%d",
                    g_act782PlayCount.load(),
                    g_act782RestTime.load(),
                    g_act782BubbleNum.load());
                UIBridge::Instance().UpdateHelperText(msg);
            }
        } else if (operation == "start_game") {
            if (offset + 4 <= packet.body.size()) {
                const int result = ReadInt32LE(body, offset);
                g_act782LastResult = result;
                if (result == 0 && offset + 12 <= packet.body.size()) {
                    ReadInt32LE(body, offset);
                    g_act782RandomCode = ReadInt32LE(body, offset);
                    g_act782CheckCode = ReadInt32LE(body, offset);
                } else if (result == 1) {
                    UIBridge::Instance().UpdateHelperText(L"摘取大力果实：今日次数已用完");
                } else if (result == 2) {
                    UIBridge::Instance().UpdateHelperText(L"摘取大力果实：冷却中，请稍后再试");
                } else if (result == 3) {
                    UIBridge::Instance().UpdateHelperText(L"摘取大力果实：当前已经在游戏中");
                }
            }
        } else if (operation == "sweep_info") {
            if (offset + 16 <= packet.body.size()) {
                const int result = ReadInt32LE(body, offset);
                g_act782SweepAvailable = (result == 0);
                if (result == 0) {
                    const int medal = ReadInt32LE(body, offset);
                    const int exp = ReadInt32LE(body, offset);
                    const int coin = ReadInt32LE(body, offset);
                    g_act782LastMedal = medal;
                    g_act782LastExp = exp;
                    g_act782LastCoin = coin;
                    wchar_t msg[256];
                    swprintf_s(msg, L"摘取大力果实：扫荡预览 勋章=%d 经验=%d 铜钱=%d", medal, exp, coin);
                    UIBridge::Instance().UpdateHelperText(msg);
                }
            }
        } else if (operation == "end_game" || operation == "sweep") {
            if (offset + 4 <= packet.body.size()) {
                const int result = ReadInt32LE(body, offset);
                g_act782LastResult = result;
                if (result != 3 && offset + 24 <= packet.body.size()) {
                    ReadInt32LE(body, offset);
                    ReadInt32LE(body, offset);
                    const int medal = ReadInt32LE(body, offset);
                    const int exp = ReadInt32LE(body, offset);
                    const int coin = ReadInt32LE(body, offset);
                    const int score = ReadInt32LE(body, offset);
                    g_act782LastMedal = medal;
                    g_act782LastExp = exp;
                    g_act782LastCoin = coin;
                    g_act782LastScore = score;

                    wchar_t msg[256];
                    if (operation == "sweep") {
                        swprintf_s(msg, L"摘取大力果实：扫荡完成，勋章=%d 经验=%d 铜钱=%d", medal, exp, coin);
                    } else {
                        swprintf_s(msg, L"摘取大力果实：完成，分数=%d 勋章=%d 经验=%d 铜钱=%d", score, medal, exp, coin);
                    }
                    UIBridge::Instance().UpdateHelperText(msg);
                }
            }
        }
    }

    

    // ============ 逆流的试炼功能实现 (Act804) ============

    // 按 AS3 的 GameControl.onEndGame/getObjByAward 语义拼结算 JSON。
    // 这里不做目标分数截断，直接按实际序列生成，避免和客户端字面行为偏离。
    std::string BuildAct804EndGameJson(const std::vector<int>& awardList) {
        std::vector<std::pair<int, int>> awardCount;
        awardCount.reserve(awardList.size());

        int score = 0;
        for (size_t i = 0; i < awardList.size(); ++i) {
            const int awardType = awardList[i];
            if (awardType == 15) {
                ++score;
            } else if (awardType == 14) {
                score = (score > 3) ? (score - 3) : 0;
            }

            if (i == 14 || i == 15 || awardType <= 0) {
                continue;
            }

            auto it = std::find_if(
                awardCount.begin(),
                awardCount.end(),
                [awardType](const std::pair<int, int>& entry) {
                    return entry.first == awardType;
                });
            if (it == awardCount.end()) {
                awardCount.emplace_back(awardType, 1);
            } else {
                ++it->second;
            }
        }

        std::string jsonStr = "{\"score\":" + std::to_string(score) + ",\"awards\":{";
        bool first = true;
        for (const auto& entry : awardCount) {
            if (!first) {
                jsonStr += ",";
            }
            first = false;
            jsonStr += "\"" + std::to_string(entry.first) + "\":" + std::to_string(entry.second);
        }
        jsonStr += "}}";
        return jsonStr;
    }

    BOOL SendAct804Packet(const std::string& operation, const std::vector<int32_t>& bodyValues) {
        return SendActivityPacket(Act804::ACTIVITY_ID, operation, bodyValues);
    }

    BOOL SendAct804GameInfoPacket() {
        g_act804WaitingResponse = true;
        return SendAct804Packet("game_info", {});
    }

    BOOL SendAct804StartGamePacket(int promptFlag) {
        g_act804WaitingResponse = true;
        return SendAct804Packet("start_game", {promptFlag});
    }

    BOOL SendAct804EndGamePacket(const std::string& jsonStr) {
        g_act804WaitingResponse = true;
        std::vector<BYTE> packet = PacketBuilder()
            .SetOpcode(Opcode::ACTIVITY_LUA_V3_SEND)
            .SetParams(Act804::ACTIVITY_ID)
            .WriteString("end_game")
            .WriteString(jsonStr)
            .Build();
        return SendPacket(
            0,
            packet.data(),
            static_cast<DWORD>(packet.size()),
            Opcode::ACTIVITY_QUERY_BACK,
            WpeHook::TIMEOUT_RESPONSE,
            Act804::ACTIVITY_ID,
            true);
    }

    BOOL SendAct804SweepInfoPacket() {
        g_act804WaitingResponse = true;
        return SendAct804Packet("sweep_info", {});
    }

    BOOL SendAct804SweepPacket() {
        g_act804WaitingResponse = true;
        return SendAct804Packet("sweep", {});
    }

    DWORD WINAPI Act804ThreadProc(LPVOID lpParam) {
        if (lpParam) {
            delete static_cast<int*>(lpParam);
        }

        g_act804PlayCount = 0;
        g_act804RestTime = 0;
        g_act804MoonPowerNum = 0;
        g_act804MooncakeNum = 0;
        g_act804EvolveFlag1 = 0;
        g_act804PromptFlag = 1;
        g_act804BuyNum = 0;
        g_act804LastResult = -1;
        g_act804SweepAvailable = false;
        {
            std::lock_guard<std::mutex> lock(g_act804Mutex);
            g_act804StartAwardList.clear();
        }

        Sleep(300);
        UIBridge::Instance().UpdateHelperText(L"逆流的试炼：正在获取活动信息...");

        SendAct804GameInfoPacket();
        for (int i = 0; i < 30 && g_act804WaitingResponse.load(); ++i) {
            Sleep(100);
        }

        if (g_act804PlayCount.load() <= 0) {
            UIBridge::Instance().UpdateHelperText(L"逆流的试炼：今日次数已用完");
            return 0;
        }

        if (g_act804RestTime.load() > 0) {
            UIBridge::Instance().UpdateHelperText(L"逆流的试炼：冷却中，请稍后再试");
            return 0;
        }

        if (g_act804UseSweep.load()) {
            UIBridge::Instance().UpdateHelperText(L"逆流的试炼：正在获取扫荡信息...");
            SendAct804SweepInfoPacket();
            for (int i = 0; i < 30 && g_act804WaitingResponse.load(); ++i) {
                Sleep(100);
            }

            if (g_act804SweepAvailable.load()) {
                Sleep(300);
                UIBridge::Instance().UpdateHelperText(L"逆流的试炼：执行扫荡...");
                g_act804LastResult = -1;
                SendAct804SweepPacket();
                for (int i = 0; i < 30 && g_act804WaitingResponse.load(); ++i) {
                    Sleep(100);
                }
                if (g_act804LastResult.load() == 0) {
                    UIBridge::Instance().UpdateHelperText(L"逆流的试炼：扫荡完成");
                } else {
                    UIBridge::Instance().UpdateHelperText(L"逆流的试炼：扫荡失败");
                }
                return 0;
            }

            UIBridge::Instance().UpdateHelperText(L"逆流的试炼：当前不可扫荡，改为直接结算");
        }

        Sleep(300);
        UIBridge::Instance().UpdateHelperText(L"逆流的试炼：开始游戏...");
        SendAct804StartGamePacket(g_act804PromptFlag.load());
        for (int i = 0; i < 30 && g_act804WaitingResponse.load(); ++i) {
            Sleep(100);
        }

        if (g_act804LastResult.load() != 0) {
            if (g_act804LastResult.load() == -1) {
            UIBridge::Instance().UpdateHelperText(L"逆流的试炼：获取开局信息失败");
            }
            return 0;
        }

        std::vector<int> startAwardList;
        {
            std::lock_guard<std::mutex> lock(g_act804Mutex);
            startAwardList = g_act804StartAwardList;
        }

        const std::string jsonStr = BuildAct804EndGameJson(startAwardList);

        Sleep(500);
        UIBridge::Instance().UpdateHelperText(L"逆流的试炼：跳过客户端渲染，直接提交结算...");
        g_act804LastResult = -1;
        SendAct804EndGamePacket(jsonStr);
        for (int i = 0; i < 30 && g_act804WaitingResponse.load(); ++i) {
            Sleep(100);
        }

        if (g_act804LastResult.load() == 0) {
            UIBridge::Instance().UpdateHelperText(L"逆流的试炼：结算完成");
        } else {
            UIBridge::Instance().UpdateHelperText(L"逆流的试炼：结算失败");
        }

        return 0;
    }

    BOOL StartOneKeyAct804Packet(bool useSweep, int targetScore) {
        g_act804UseSweep = useSweep;
        int* pTargetScore = new int(targetScore);
        HANDLE hThread = CreateThread(nullptr, 0, Act804ThreadProc, pTargetScore, 0, nullptr);
        if (hThread) {
            CloseHandle(hThread);
            return TRUE;
        }
        delete pTargetScore;
        return FALSE;
    }

    void ProcessAct804Response(const GamePacket& packet) {
        size_t offset = 0;
        std::string operation;
        if (!ReadPacketString(packet.body.data(), packet.body.size(), offset, operation)) {
            return;
        }

        const BYTE* body = packet.body.data();
        g_act804WaitingResponse = false;

        if (operation == "game_info") {
            if (offset + 32 <= packet.body.size()) {
                const int code = ReadInt32LE(body, offset);
                g_act804PlayCount = ReadInt32LE(body, offset);
                g_act804RestTime = ReadInt32LE(body, offset);
                g_act804MoonPowerNum = ReadInt32LE(body, offset);
                g_act804MooncakeNum = ReadInt32LE(body, offset);
                g_act804EvolveFlag1 = ReadInt32LE(body, offset);
                g_act804PromptFlag = ReadInt32LE(body, offset);
                g_act804BuyNum = ReadInt32LE(body, offset);
                const int limitNum = ReadInt32LE(body, offset);
                for (int i = 0; i < limitNum && offset + 8 <= packet.body.size(); ++i) {
                    ReadInt32LE(body, offset);
                    ReadInt32LE(body, offset);
                }

                if (code == 0) {
                    wchar_t msg[256];
                    swprintf_s(
                        msg,
                        L"逆流的试炼：次数=%d 冷却=%d秒 勋章=%d",
                        g_act804PlayCount.load(),
                        g_act804RestTime.load(),
                        g_act804MoonPowerNum.load());
                    UIBridge::Instance().UpdateHelperText(msg);
                }
            }
        } else if (operation == "start_game") {
            if (offset + 4 <= packet.body.size()) {
                const int result = ReadInt32LE(body, offset);
                g_act804LastResult = result;
                if (result == 0 && offset + 8 <= packet.body.size()) {
                    g_act804PlayCount = ReadInt32LE(body, offset);
                    const int rewardLength = ReadInt32LE(body, offset);
                    {
                        std::lock_guard<std::mutex> lock(g_act804Mutex);
                        g_act804StartAwardList.clear();
                        for (int i = 0; i < rewardLength && offset + 4 <= packet.body.size(); ++i) {
                            const int rewardIndex = ReadInt32LE(body, offset);
                            if (rewardIndex != 0) {
                                g_act804StartAwardList.push_back(rewardIndex);
                            }
                        }
                    }
                    UIBridge::Instance().UpdateHelperText(L"逆流的试炼：已获取开局奖励列表");
                } else if (result == 1) {
                    UIBridge::Instance().UpdateHelperText(L"逆流的试炼：今日次数已用完");
                } else if (result == 11) {
                    UIBridge::Instance().UpdateHelperText(L"逆流的试炼：冷却中，请稍后再试");
                } else if (result == 10) {
                    UIBridge::Instance().UpdateHelperText(L"逆流的试炼：当前已经在游戏中");
                } else if (result == 12) {
                    UIBridge::Instance().UpdateHelperText(L"逆流的试炼：游戏次数不足");
                }
            }
        } else if (operation == "sweep_info") {
            if (offset + 8 <= packet.body.size()) {
                const int score = ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                g_act804SweepAvailable = (score > 0);
                if (g_act804SweepAvailable.load()) {
                    std::string jsonStr;
                    ReadPacketString(packet.body.data(), packet.body.size(), offset, jsonStr);
                    UIBridge::Instance().UpdateHelperText(L"逆流的试炼：扫荡预览已获取");
                } else {
                    UIBridge::Instance().UpdateHelperText(L"逆流的试炼：当前不可扫荡");
                }
            }
        } else if (operation == "end_game" || operation == "sweep") {
            if (offset + 20 <= packet.body.size()) {
                const int result = ReadInt32LE(body, offset);
                g_act804LastResult = result;
                const int restPlayCount = ReadInt32LE(body, offset);
                const int restCdTime = ReadInt32LE(body, offset);
                const int score = ReadInt32LE(body, offset);
                const int coin = ReadInt32LE(body, offset);
                std::string jsonStr;
                if (ReadPacketString(packet.body.data(), packet.body.size(), offset, jsonStr)) {
                    (void)jsonStr;
                }
                g_act804PlayCount = restPlayCount;
                g_act804RestTime = restCdTime;
                if (result == 0) {
                    wchar_t msg[256];
                    swprintf_s(
                        msg,
                        L"逆流的试炼：%s完成，得分=%d，收益=%d",
                        operation == "sweep" ? L"扫荡" : L"结算",
                        score,
                        coin);
                    UIBridge::Instance().UpdateHelperText(msg);
                } else {
                    UIBridge::Instance().UpdateHelperText(operation == "sweep" ? L"逆流的试炼：扫荡失败" : L"逆流的试炼：结算失败");
                }
            }
        } else if (operation == "buy_play_game_cnt") {
            if (offset + 4 <= packet.body.size()) {
                const int result = ReadInt32LE(body, offset);
                if (result == 0) {
                    UIBridge::Instance().UpdateHelperText(L"逆流的试炼：已购买一次游戏机会");
                } else if (result == 10) {
                    UIBridge::Instance().UpdateHelperText(L"逆流的试炼：游戏中");
                } else if (result == 11) {
                    UIBridge::Instance().UpdateHelperText(L"逆流的试炼：不需要购买");
                } else if (result == 14) {
                    UIBridge::Instance().UpdateHelperText(L"逆流的试炼：卡布币不足");
                } else if (result == 16) {
                    UIBridge::Instance().UpdateHelperText(L"逆流的试炼：购买次数已达上限");
                }
            }
        } else if (operation == "exchange") {
            if (offset + 8 <= packet.body.size()) {
                const int result = ReadInt32LE(body, offset);
                const int itemIndex = ReadInt32LE(body, offset);
                if (result == 0 && offset + 8 <= packet.body.size()) {
                    g_act804MoonPowerNum = ReadInt32LE(body, offset);
                    const int exchNum = ReadInt32LE(body, offset);
                    (void)itemIndex;
                    (void)exchNum;
                    UIBridge::Instance().UpdateHelperText(L"逆流的试炼：兑换成功");
                }
            }
        } else if (operation == "evolution") {
            if (offset + 4 <= packet.body.size()) {
                const int result = ReadInt32LE(body, offset);
                if (result == 0 && offset + 4 <= packet.body.size()) {
                    ReadInt32LE(body, offset);
                    g_act804EvolveFlag1 = 1;
                    UIBridge::Instance().UpdateHelperText(L"逆流的试炼：超进化完成");
                } else if (result == 1) {
                    UIBridge::Instance().UpdateHelperText(L"逆流的试炼：已经超进化了");
                } else if (result == 2) {
                    UIBridge::Instance().UpdateHelperText(L"逆流的试炼：请先将逆空流设为首选");
                } else if (result == 3) {
                    UIBridge::Instance().UpdateHelperText(L"逆流的试炼：等级不足");
                } else if (result == 4) {
                    UIBridge::Instance().UpdateHelperText(L"逆流的试炼：力之月饼不足");
                } else if (result == 5) {
                    UIBridge::Instance().UpdateHelperText(L"逆流的试炼：卡布币不足");
                }
            }
        } else if (operation == "put_monster") {
            if (offset + 4 <= packet.body.size()) {
                const int result = ReadInt32LE(body, offset);
                if (result == 0) {
                    UIBridge::Instance().UpdateHelperText(L"逆流的试炼：已放入鹦鹉");
                }
            }
        } else if (operation == "clear_cd_time") {
            if (offset + 4 <= packet.body.size()) {
                const int result = ReadInt32LE(body, offset);
                if (result == 0 && offset + 8 <= packet.body.size()) {
                    g_act804RestTime = ReadInt32LE(body, offset);
                    ReadInt32LE(body, offset);
                UIBridge::Instance().UpdateHelperText(L"逆流的试炼：冷却已清除");
                }
            }
        }
    }

    // ============ 我是神射手功能实现 (Act811) ============

    BOOL SendAct811Packet(const std::string& operation, const std::vector<int32_t>& bodyValues) {
        return SendActivityPacket(Act811::ACTIVITY_ID, operation, bodyValues);
    }

    static constexpr std::array<int, 28> kAct811PropPerSection = {
        0, 5, 8, 10, 5, 8, 10, 5, 8, 10, 5, 8, 10, 5, 8, 10, 5, 8, 10, 5, 8, 10, 5, 8, 10, 5, 8, 10
    };

    static std::vector<int32_t> BuildAct811GameEventBody(const std::array<int32_t, 23>& awardCounts) {
        std::vector<int32_t> bodyValues;
        bodyValues.reserve(15);

        for (int i = 1; i <= 12; ++i) {
            bodyValues.push_back(awardCounts[i]);
        }

        int specialIds[3] = {0, 0, 0};
        int specialIndex = 0;
        for (int i = 13; i <= 22 && specialIndex < 3; ++i) {
            if (awardCounts[i] > 0) {
                specialIds[specialIndex++] = i;
            }
        }

        bodyValues.push_back(specialIds[0]);
        bodyValues.push_back(specialIds[1]);
        bodyValues.push_back(specialIds[2]);
        return bodyValues;
    }

    BOOL SendAct811GameEventPacket(const std::array<int32_t, 23>& awardCounts) {
        std::vector<BYTE> packet = BuildActivityPacket(
            Opcode::ACTIVITY_QINGYANG_NEW_SEND,
            Act811::ACTIVITY_ID,
            "game_event",
            BuildAct811GameEventBody(awardCounts));
        return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
    }

    BOOL SendAct811OpenUIPacket() {
        g_act811WaitingResponse = true;
        return SendAct811Packet("open_ui", {});
    }

    BOOL SendAct811StartGamePacket(int hintFlag) {
        g_act811WaitingResponse = true;
        if (hintFlag != 0) {
            return SendAct811Packet("start_game", {hintFlag});
        }
        return SendAct811Packet("start_game", {});
    }

    BOOL SendAct811SweepInfoPacket() {
        g_act811WaitingResponse = true;
        return SendAct811Packet("sweep_info", {});
    }

    BOOL SendAct811SweepPacket() {
        g_act811WaitingResponse = true;
        return SendAct811Packet("sweep", {});
    }

    BOOL SendAct811EndGamePacket() {
        g_act811WaitingResponse = true;
        return SendAct811Packet("end_game", {});
    }

    static void SubmitAct811GameEventsFromAwardList(const std::vector<int>& awardList) {
        std::array<int32_t, 23> awardCounts{};
        size_t awardIndex = 0;

        auto consumeBatch = [&]() {
            for (int lane = 0; lane < 5 && awardIndex < awardList.size(); ++lane) {
                const int awardId = awardList[awardIndex++];
                if (awardId >= 1 && awardId <= 22) {
                    ++awardCounts[awardId];
                }
            }
        };

        for (size_t sectionIndex = 1; sectionIndex < kAct811PropPerSection.size(); ++sectionIndex) {
            for (int batch = 0; batch < kAct811PropPerSection[sectionIndex] && awardIndex < awardList.size(); ++batch) {
                consumeBatch();
            }
            SendAct811GameEventPacket(awardCounts);
        }

        while (awardIndex < awardList.size()) {
            consumeBatch();
            SendAct811GameEventPacket(awardCounts);
        }

        // 按 AS3 的 wingame() 语义，最终结算前再补一次累计结果。
        SendAct811GameEventPacket(awardCounts);
    }

    DWORD WINAPI Act811ThreadProc(LPVOID lpParam) {
        (void)lpParam;

        const bool useSweep = g_act811UseSweep.load();
        g_act811PlayCount = 0;
        g_act811RestTime = 0;
        g_act811PropNum = 0;
        g_act811HintFlag = 0;
        g_act811LastResult = -1;
        g_act811SweepAvailable = false;
        g_act811AwardList.clear();

        auto waitForResponse = []() -> bool {
            for (int i = 0; i < 30 && g_act811WaitingResponse.load(); ++i) {
                Sleep(100);
            }
            const bool received = !g_act811WaitingResponse.load();
            g_act811WaitingResponse = false;
            return received;
        };

        Sleep(300);
        UIBridge::Instance().UpdateHelperText(L"我是神射手：正在获取活动信息...");

        SendAct811OpenUIPacket();
        if (!waitForResponse()) {
            UIBridge::Instance().UpdateHelperText(L"我是神射手：获取活动信息失败");
            return 0;
        }

        if (g_act811PlayCount.load() <= 0) {
            UIBridge::Instance().UpdateHelperText(L"我是神射手：今日次数已用完");
            return 0;
        }

        if (g_act811RestTime.load() > 0) {
            UIBridge::Instance().UpdateHelperText(L"我是神射手：冷却中，请稍后再试");
            return 0;
        }

        if (useSweep) {
            UIBridge::Instance().UpdateHelperText(L"我是神射手：正在获取扫荡信息...");
            SendAct811SweepInfoPacket();
            if (waitForResponse() && g_act811SweepAvailable.load()) {
                Sleep(300);
                UIBridge::Instance().UpdateHelperText(L"我是神射手：执行扫荡...");
                g_act811LastResult = -1;
                SendAct811SweepPacket();
                if (waitForResponse() && g_act811LastResult.load() == 0) {
                    UIBridge::Instance().UpdateHelperText(L"我是神射手：扫荡完成");
                } else {
                    UIBridge::Instance().UpdateHelperText(L"我是神射手：扫荡失败");
                }
                return 0;
            }

            UIBridge::Instance().UpdateHelperText(L"我是神射手：当前不可扫荡，改为直接完成");
        }

        Sleep(300);
        UIBridge::Instance().UpdateHelperText(L"我是神射手：开始游戏...");
        SendAct811StartGamePacket(g_act811HintFlag.load());
        if (!waitForResponse()) {
            UIBridge::Instance().UpdateHelperText(L"我是神射手：获取开局信息失败");
            return 0;
        }

        if (g_act811LastResult.load() != 0) {
            if (g_act811LastResult.load() == 10) {
                UIBridge::Instance().UpdateHelperText(L"我是神射手：当前已经在游戏中");
            } else if (g_act811LastResult.load() == 11) {
                UIBridge::Instance().UpdateHelperText(L"我是神射手：冷却中，请稍后再试");
            } else if (g_act811LastResult.load() == 12) {
                UIBridge::Instance().UpdateHelperText(L"我是神射手：今日次数已用完");
            } else if (g_act811LastResult.load() == 13) {
                UIBridge::Instance().UpdateHelperText(L"我是神射手：玩家正在支付中");
            } else {
                UIBridge::Instance().UpdateHelperText(L"我是神射手：开始游戏失败");
            }
            return 0;
        }

        SubmitAct811GameEventsFromAwardList(g_act811AwardList);

        Sleep(500);
        UIBridge::Instance().UpdateHelperText(L"我是神射手：直接提交结算...");
        g_act811LastResult = -1;
        SendAct811EndGamePacket();
        if (!waitForResponse()) {
            UIBridge::Instance().UpdateHelperText(L"我是神射手：结算失败");
            return 0;
        }

        if (g_act811LastResult.load() == 0) {
            UIBridge::Instance().UpdateHelperText(L"我是神射手：结算完成");
        } else {
            UIBridge::Instance().UpdateHelperText(L"我是神射手：结算失败");
        }

        return 0;
    }

    BOOL StartOneKeyAct811Packet(bool useSweep) {
        g_act811UseSweep = useSweep;
        HANDLE hThread = CreateThread(nullptr, 0, Act811ThreadProc, nullptr, 0, nullptr);
        if (hThread) {
            CloseHandle(hThread);
            return TRUE;
        }
        return FALSE;
    }

    void ProcessAct811Response(const GamePacket& packet) {
        size_t offset = 0;
        std::string operation;
        if (!ReadPacketString(packet.body.data(), packet.body.size(), offset, operation)) {
            return;
        }

        const BYTE* body = packet.body.data();
        g_act811WaitingResponse = false;

        auto parseRewardList = [&](size_t& currentOffset) -> std::vector<std::pair<int, int>> {
            std::vector<std::pair<int, int>> rewardList;
            if (currentOffset + 16 > packet.body.size()) {
                return rewardList;
            }

            rewardList.reserve(3);
            rewardList.emplace_back(0, ReadInt32LE(body, currentOffset));
            rewardList.emplace_back(201, ReadInt32LE(body, currentOffset));
            rewardList.emplace_back(202, ReadInt32LE(body, currentOffset));
            const int len = ReadInt32LE(body, currentOffset);
            for (int i = 0; i < len && currentOffset + 8 <= packet.body.size(); ++i) {
                const int id = ReadInt32LE(body, currentOffset);
                const int num = ReadInt32LE(body, currentOffset);
                rewardList.emplace_back(id, num);
            }
            return rewardList;
        };

        if (operation == "open_ui") {
            if (offset + 28 <= packet.body.size()) {
                g_act811PlayCount = ReadInt32LE(body, offset);
                g_act811RestTime = ReadInt32LE(body, offset);
                g_act811PropNum = ReadInt32LE(body, offset);
                g_act811HintFlag = ReadInt32LE(body, offset);
                const int isSweep = ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                g_act811SweepAvailable = (isSweep != 0);

                wchar_t msg[256];
                swprintf_s(
                    msg,
                    L"我是神射手：次数=%d 冷却=%d秒 蛋糕=%d",
                    g_act811PlayCount.load(),
                    g_act811RestTime.load(),
                    g_act811PropNum.load());
                UIBridge::Instance().UpdateHelperText(msg);
            }
        } else if (operation == "start_game") {
            if (offset + 4 <= packet.body.size()) {
                const int result = ReadInt32LE(body, offset);
                g_act811LastResult = result;
                if (result == 0 && offset + 4 <= packet.body.size()) {
                    g_act811PlayCount = ReadInt32LE(body, offset);
                    g_act811AwardList.clear();
                    for (int i = 0; i < 294 && offset + 4 <= packet.body.size(); ++i) {
                        g_act811AwardList.push_back(ReadInt32LE(body, offset));
                    }
                    UIBridge::Instance().UpdateHelperText(L"我是神射手：已获取开局信息");
                } else if (result == 10) {
                    UIBridge::Instance().UpdateHelperText(L"我是神射手：当前已经在游戏中");
                } else if (result == 11) {
                    UIBridge::Instance().UpdateHelperText(L"我是神射手：冷却中，请稍后再试");
                } else if (result == 12) {
                    UIBridge::Instance().UpdateHelperText(L"我是神射手：今日次数已用完");
                } else if (result == 13) {
                    UIBridge::Instance().UpdateHelperText(L"我是神射手：玩家正在支付中");
                }
            }
        } else if (operation == "sweep_info") {
            if (offset + 4 <= packet.body.size()) {
                const int result = ReadInt32LE(body, offset);
                g_act811SweepAvailable = (result == 0);
                if (result == 0) {
                    std::vector<std::pair<int, int>> rewardList = parseRewardList(offset);
                    (void)rewardList;
                    UIBridge::Instance().UpdateHelperText(L"我是神射手：扫荡预览已获取");
                } else {
                    UIBridge::Instance().UpdateHelperText(L"我是神射手：当前不可扫荡");
                }
            }
        } else if (operation == "end_game" || operation == "sweep") {
            if (offset + 4 <= packet.body.size()) {
                const int result = ReadInt32LE(body, offset);
                g_act811LastResult = result;
                if (result != 3) {
                    if (offset + 8 <= packet.body.size()) {
                        ReadInt32LE(body, offset);
                        ReadInt32LE(body, offset);
                    }
                    std::vector<std::pair<int, int>> rewardList = parseRewardList(offset);
                    (void)rewardList;
                    UIBridge::Instance().UpdateHelperText(
                        operation == "sweep" ? L"我是神射手：扫荡完成" : L"我是神射手：结算完成");
                } else {
                    UIBridge::Instance().UpdateHelperText(
                        operation == "sweep" ? L"我是神射手：扫荡失败" : L"我是神射手：结算失败");
                }
            }
        }
    }

    // ============ 驱傩聚福寿功能实现 (Act778) ============
    
    // 使用新的状态管理器
    #define ACT778_STATE ActivityStateManager::Instance().GetAct778State()
    
    // 兼容旧代码的静态变量映射到状态管理器
    static std::vector<std::pair<int, int>> g_act778AwardList;  // [index, type] 列表
    
    BOOL SendAct778Packet(const std::string& operation, const std::vector<int32_t>& bodyValues) {
        return SendActivityPacket(Act778::ACTIVITY_ID, operation, bodyValues);
    }
    
    BOOL SendAct778GameInfoPacket() {
        ACT778_STATE.waitingResponse = true;
        return SendAct778Packet("open_ui", {});
    }
    
    BOOL SendAct778StartGamePacket() {
        ACT778_STATE.waitingResponse = true;
        return SendAct778Packet("start_game", {});
    }
    
    BOOL SendAct778GameHitPacket(int below) {
        // 校验码: (random 1000-1999) * (checkCode + below) + 5115
        int random_part = (rand() % 1000) + 1000;
        int checkCode = random_part * (ACT778_STATE.checkCode + below) + 5115;
        return SendAct778Packet("game_hit", {checkCode, below});
    }
    
    BOOL SendAct778EndGamePacket(int monsterCount, int endType) {
        // 校验码: checkCode & (userId % checkCode)
        // endType: 0=退出, 1=完成
        int clientCheckCode = ACT778_STATE.checkCode & (g_userId % ACT778_STATE.checkCode);
        return SendAct778Packet("end_game", {clientCheckCode, monsterCount, endType});
    }
    
    BOOL SendAct778SweepInfoPacket() {
        ACT778_STATE.waitingResponse = true;
        return SendAct778Packet("sweep_info", {});
    }
    
    BOOL SendAct778SweepPacket() {
        ACT778_STATE.waitingResponse = true;
        return SendAct778Packet("sweep", {});
    }
    
    // 扫荡是否成功的标志
    static bool g_act778SweepSuccess = false;
    
    DWORD WINAPI Act778ThreadProc(LPVOID lpParam) {
        (void)lpParam;
        Sleep(300);
        
        UIBridge::Instance().UpdateHelperText(L"驱傩聚福寿：正在获取游戏信息...");
        
        SendAct778GameInfoPacket();
        for (int i = 0; i < 30 && ACT778_STATE.waitingResponse; i++) Sleep(100);
        
        if (ACT778_STATE.playCount <= 0) {
            UIBridge::Instance().UpdateHelperText(L"驱傩聚福寿：今日次数已用完");
            return 0;
        }
        
        if (ACT778_STATE.restTime > 0) {
            UIBridge::Instance().UpdateHelperText(L"驱傩聚福寿：冷却中，请稍后再试");
            return 0;
        }
        
        // 尝试扫荡（用户勾选扫荡复选框时直接执行，由服务器判断是否可扫荡）
        if (ACT778_STATE.useSweep) {
            UIBridge::Instance().UpdateHelperText(L"驱傩聚福寿：正在获取扫荡信息...");
            
            Sleep(300);
            g_act778SweepSuccess = false;
            SendAct778SweepInfoPacket();
            for (int i = 0; i < 30 && ACT778_STATE.waitingResponse; i++) Sleep(100);
            
            // 检查扫荡信息是否成功（sweep_info的result!=3才成功）
            if (!g_act778SweepSuccess) {
                // 扫荡失败，需要重新游戏
                UIBridge::Instance().UpdateHelperText(L"驱傩聚福寿：扫荡条件不满足，开始游戏...");
            } else {
                // 执行扫荡
                Sleep(300);
                g_act778SweepSuccess = false;
                SendAct778SweepPacket();
                for (int i = 0; i < 30 && ACT778_STATE.waitingResponse; i++) Sleep(100);
                
                // 检查扫荡是否成功（sweep的result!=3才成功）
                if (g_act778SweepSuccess) {
                    UIBridge::Instance().UpdateHelperText(L"驱傩聚福寿：扫荡完成！");
                    return 0;
                }
                // 扫荡失败，继续游戏流程
            }
        }
        
        // 需要完成一局游戏
        UIBridge::Instance().UpdateHelperText(L"驱傩聚福寿：开始游戏...");
        
        Sleep(300);
        SendAct778StartGamePacket();
        for (int i = 0; i < 30 && ACT778_STATE.waitingResponse; i++) Sleep(100);
        
        // 遍历 awardList 发送 game_hit 获取奖励
        // awardList 格式: [[index, below], ...]
        // below 含义: 1=buff, 2=年糕, 其他=道具ID
        int awardCount = static_cast<int>(g_act778AwardList.size());
        
        DWORD lastSendTime = GetTickCount();
        const DWORD minInterval = 100;  // 最小发送间隔100ms
        
        // 发送 game_hit 拾取每个奖励，每20次更新进度
        for (int i = 0; i < awardCount; i++) {
            // 每20次更新一次进度提示
            if (i % 20 == 0 || i == awardCount - 1) {
                wchar_t msg[128];
                swprintf_s(msg, L"驱傩聚福寿：拾取奖励中... 进度 %d/%d", i + 1, awardCount);
                UIBridge::Instance().UpdateHelperText(msg);
            }
            
            int below = g_act778AwardList[i].second;  // below 值
            SendAct778GameHitPacket(below);
            
            // 智能延迟：计算距离上次发送的时间
            DWORD now = GetTickCount();
            DWORD elapsed = now - lastSendTime;
            if (elapsed < minInterval) {
                Sleep(minInterval - elapsed);
            }
            lastSendTime = GetTickCount();
        }
        
        // 游戏限制80只年兽，monsterCount 使用80
        Sleep(300);
        SendAct778EndGamePacket(80, 1);  // monsterCount=80, endType=1(完成)
        
        UIBridge::Instance().UpdateHelperText(L"驱傩聚福寿：游戏完成，击杀80只年兽！");
        return 0;
    }
    
    BOOL StartOneKeyAct778Packet(bool useSweep) {
        ACT778_STATE.useSweep = useSweep;  // 设置扫荡选项
        HANDLE hThread = CreateThread(nullptr, 0, Act778ThreadProc, nullptr, 0, nullptr);
        if (hThread) { CloseHandle(hThread); return TRUE; }
        return FALSE;
    }
    
    void ProcessAct778Response(const GamePacket& packet) {
        size_t offset = 0;
        std::string operation;
        if (!ReadPacketString(packet.body.data(), packet.body.size(), offset, operation)) return;
        const BYTE* body = packet.body.data();
        
        ACT778_STATE.waitingResponse = false;
        
        if (operation == "open_ui") {
            // ActBaseInfoDto.readFromStream 格式:
            // playCount(4B) + frozenTime(4B) + buyGameCnt(4B) + snowPowerCount(4B) + 
            // skip(4B) + skip(4B) + skip(4B) + HistoryMaxScore(4B) + catch_list...
            if (offset + 32 <= packet.body.size()) {
                ACT778_STATE.playCount = ReadInt32LE(body, offset);  // playCount
                ACT778_STATE.restTime = ReadInt32LE(body, offset);   // frozenTime
                offset += 4;  // buyGameCnt (不使用)
                offset += 4;  // snowPowerCount (不使用)
                offset += 4;  // skip
                offset += 4;  // skip
                offset += 4;  // skip
                ACT778_STATE.bestScore = ReadInt32LE(body, offset);  // HistoryMaxScore
                ACT778_STATE.sweepAvailable = (ACT778_STATE.bestScore > 0);
                // 后续还有 catch_list 数据，但不需要解析
            }
        }
        else if (operation == "start_game") {
            // 响应格式 (根据 AS3 decodeBeginGame + ActStartInfoDto.readFromStream):
            // result(4B) + skip(4B) + checkCode(4B) + count(4B) + [index(4B) + type(4B)] * count
            if (offset + 4 <= packet.body.size()) {
                int result = ReadInt32LE(body, offset);  // result
                if (result == 0 && offset + 16 <= packet.body.size()) {
                    offset += 4;  // skip 第一个 int（ActStartInfoDto.readFromStream 中跳过）
                    ACT778_STATE.checkCode = ReadInt32LE(body, offset);
                    int count = ReadInt32LE(body, offset);
                    // 解析 awardList
                    g_act778AwardList.clear();
                    for (int i = 0; i < count && offset + 8 <= packet.body.size(); i++) {
                        int index = ReadInt32LE(body, offset);
                        int type = ReadInt32LE(body, offset);
                        g_act778AwardList.push_back({index, type});
                    }
                }
            }
        }
        else if (operation == "sweep_info") {
            // sweep_info 成功条件：result != 3
            if (offset + 4 <= packet.body.size()) {
                int result = ReadInt32LE(body, offset);
                g_act778SweepSuccess = (result != 3);
            }
        }
        else if (operation == "sweep") {
            // sweep 成功条件：result != 3
            if (offset + 4 <= packet.body.size()) {
                int result = ReadInt32LE(body, offset);
                g_act778SweepSuccess = (result != 3);
                if (result != 3 && offset + 8 <= packet.body.size()) {
                    ACT778_STATE.playCount = ReadInt32LE(body, offset);
                }
            }
        }
        else if (operation == "end_game") {
            if (offset + 4 <= packet.body.size()) {
                int result = ReadInt32LE(body, offset);
                if (result != 3 && offset + 8 <= packet.body.size()) {
                    ACT778_STATE.playCount = ReadInt32LE(body, offset);
                }
            }
        }
    }
    
      // anonymous namespace

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
// 一键采集实现
// ============================================================================

/** 采集物品数据结构 */

struct CollectItemData {

    const wchar_t* name;   ///< 物品名称

    uint32_t mapId;        ///< 地图ID

    uint32_t stuffId;      ///< 物品ID

    int maxCount;          ///< 最大采集次数

};



/** 采集物品列表（按易语言代码顺序）



 * 



 * 封包编码规则（重要！）：



 * - 封包中所有数值字段都是小端序存储



 * - 例如：封包 A10F0000 小端序解读 = 0x00000FA1 = 4001



 * - 例如：封包 F6930400 小端序解读 = 0x000493F6 = 299254



 * - 构造封包时，数值需要以小端序写入



 */







static const CollectItemData COLLECT_ITEMS[] = {







    // 名称        mapId    stuffId   次数







    // stuffId 从易语言封包解析：小端序读取







    // 例如：FC930400 → 0x000493FC = 300028















    {L"上清宝玉", 4001,  300022,  3},   // F6930400 → 0x000493F6 (易语言代码改为3次)







    {L"天地灵气", 6002,  300028,  2},   // FC930400 → 0x000493FC (易语言代码改为2次)
    {L"火云岩",   8001,  300019,  3},   // F3930400 → 0x000493F3
    {L"千年红木", 2004,  300016,  3},   // F0930400 → 0x000493F0
    {L"远古青松", 4002,  300017,  3},   // F1930400 → 0x000493F1
    {L"精蓝石",   9002,  300021,  5},   // F5930400 → 0x000493F5, mapId 0x232A=9002 (小端序: 2A23)
    {L"松绿石",   5001,  300025,  5},   // F9930400 → 0x000493F9
    {L"冰霜岩",   3004,  300018,  4},   // F2930400 → 0x000493F2, mapId 0x0BBC=3004 (小端序: BC0B)
    {L"幻影石",   2101,  300015,  4},   // EF930400 → 0x000493EF (结束用FF93)
    {L"冰晶砂",   3004,  300023,  5},   // F7930400 → 0x000493F7, mapId 0x0BBC=3004 (小端序: BC0B)
    {L"天罡石",   6002,  300020,  4},   // F4930400 → 0x000493F4
    {L"茅山玉",   7001,  300024,  5},   // F8930400 → 0x000493F8, mapId 0x1B59=7001 (小端序: 591B)
    {L"神明果",   6003,  300030,  5},   // FE930400 → 0x000493FE (改为5次)
    {L"朱砂石",   9002,  300026,  5},   // FA930400 → 0x000493FA, mapId 0x232A=9002
    {L"青金石",   9002,  300027,  5},   // FB930400 → 0x000493FB, mapId 0x232A=9002
    {L"坐骑蛋",   2003,  0,      0},    // 特殊处理

};

static const int COLLECT_ITEM_COUNT = sizeof(COLLECT_ITEMS) / sizeof(COLLECT_ITEMS[0]);

/** 采集单个物品 */
static BOOL CollectSingleItem(const CollectItemData& item) {
    if (item.maxCount == 0) {
        // 坐骑蛋特殊处理
        return TRUE;
    }

    // 1. 跳转地图封包: Opcode 1184313, Params=mapId
    auto mapPacket = PacketBuilder()
        .SetOpcode(Opcode::ENTER_SCENE_SEND)
        .SetParams(static_cast<uint32_t>(item.mapId))
        .Build();

    if (!SendPacket(g_LastGameSocket, mapPacket.data(), static_cast<DWORD>(mapPacket.size()))) {
        return FALSE;
    }
    Sleep(400);

    // 2. 第二个封包: Opcode 1184319
    auto packet2 = PacketBuilder()
        .SetOpcode(1184319)
        .SetParams(0)
        .Build();

    if (!SendPacket(g_LastGameSocket, packet2.data(), static_cast<DWORD>(packet2.size()))) {
        return FALSE;
    }
    Sleep(265);

    // 3. 第三个封包: Opcode 1184798
    auto packet3 = PacketBuilder()
        .SetOpcode(1184798)
        .SetParams(0)
        .Build();

    if (!SendPacket(g_LastGameSocket, packet3.data(), static_cast<DWORD>(packet3.size()))) {
        return FALSE;
    }
    Sleep(265);

    // 循环采集（基于响应包判断完成）
    for (int i = 0; i < item.maxCount; i++) {
        // 重置采集状态
        g_collectStatus = 0;
        g_collectFinished = false;
        
        // 4. 开始采集封包: Opcode 1187106, Params=1, Body=stuffId
        auto startPacket = PacketBuilder()
            .SetOpcode(1187106)
            .SetParams(1)
            .WriteInt32(item.stuffId)
            .Build();

        if (!SendPacket(g_LastGameSocket, startPacket.data(), static_cast<DWORD>(startPacket.size()))) {
            return FALSE;
        }

        // 更新提示（使用宽字符串，显示地图名称）
        std::wstring mapName = GetMapName(item.mapId);
        std::wstring msg = L"正在采集: ";
        msg.append(item.name);
        msg.append(L" (");
        msg.append(mapName.empty() ? L"未知地图" : mapName);
        msg.append(L", ");
        msg.append(std::to_wstring(i + 1));
        msg.append(L"/");
        msg.append(std::to_wstring(item.maxCount));
        msg.append(L")");
        UIBridge::Instance().UpdateHelperText(msg);

        // 等待服务端返回 statusid=1（开始采集动画）
        // 超时设置：最多等待 3 秒
        int waitCount = 0;
        while (g_collectStatus != 1 && waitCount < 30) {
            Sleep(100);
            waitCount++;
            // 检查错误状态
            if (g_collectStatus == -1) {
                // 每日采集次数用完
                UIBridge::Instance().UpdateHelperText(std::wstring(item.name) + L": 每日采集次数已用完");
                return FALSE;
            }
            if (g_collectStatus == -2) {
                // 未装备采集工具
                UIBridge::Instance().UpdateHelperText(std::wstring(item.name) + L": 未装备采集工具");
                return FALSE;
            }
        }

        // 等待采集完成响应（statusid=2 或 statusid=3）
        // 超时设置：最多等待 15 秒（采集动画约 10-13 秒）
        g_collectFinished = false;
        waitCount = 0;
        while (!g_collectFinished && waitCount < 150) {
            Sleep(100);
            waitCount++;
        }

        // 5. 结束采集封包: Opcode 1187106 (小端序: 221D1200)
        BYTE endPacket[16];
        memset(endPacket, 0, sizeof(endPacket));
        endPacket[0] = 0x44;  // Magic: "SD"
        endPacket[1] = 0x53;
        endPacket[2] = 0x04;  // Length: 4 (小端序)
        endPacket[3] = 0x00;
        endPacket[4] = 0x22;  // Opcode: 1187106 小端序 (0x00121D22)
        endPacket[5] = 0x1D;
        endPacket[6] = 0x12;
        endPacket[7] = 0x00;
        endPacket[8] = 0x02;  // Params: 2 (结束采集)
        endPacket[9] = 0x00;
        endPacket[10] = 0x00;
        endPacket[11] = 0x00;
        // Body: stuffId 小端序写入（32位）
        endPacket[12] = (item.stuffId) & 0xFF;
        endPacket[13] = (item.stuffId >> 8) & 0xFF;
        endPacket[14] = (item.stuffId >> 16) & 0xFF;
        endPacket[15] = (item.stuffId >> 24) & 0xFF;

        if (!SendPacket(g_LastGameSocket, endPacket, 16)) {
            return FALSE;
        }

        // 如果收到 statusid=3，表示采集全部结束，跳出循环
        if (g_collectStatus == 3) {
            break;
        }
        
        Sleep(300);
    }

    return TRUE;
}

/** 处理坐骑蛋采集 */
static BOOL CollectMountEgg() {
    // 坐骑蛋需要特殊处理：拾取场景中的坐骑蛋
    // 封包格式：所有数值都是小端序
    
    // 1. 跳转到地图 2003: 封包 4453000039121200D3070000
    // mapId 2003 = 0x000007D3，小端序写入: D3 07 00 00
    BYTE mapPacket[12];
    memset(mapPacket, 0, sizeof(mapPacket));
    mapPacket[0] = 0x44;  // Magic: "SD"
    mapPacket[1] = 0x53;
    mapPacket[2] = 0x00;  // Length: 0 (小端序)
    mapPacket[3] = 0x00;
    mapPacket[4] = 0x39;  // Opcode: 1184313 小端序 (0x00121239)
    mapPacket[5] = 0x12;
    mapPacket[6] = 0x12;
    mapPacket[7] = 0x00;
    mapPacket[8] = 0xD3;  // mapId: 2003 小端序
    mapPacket[9] = 0x07;
    mapPacket[10] = 0x00;
    mapPacket[11] = 0x00;
    SendPacket(g_LastGameSocket, mapPacket, 12);
    Sleep(300);

    // 2. 发送拾取封包: 44530800001912007952000000000000
    // Opcode 1185792 = 0x00121900，小端序: 00 19 12 00
    // Params 21089 = 0x00005279，小端序: 79 52 00 00
    BYTE packet[20];
    memset(packet, 0, sizeof(packet));
    packet[0] = 0x44;  // Magic: "SD"
    packet[1] = 0x53;
    packet[2] = 0x08;  // Length: 8 (小端序)
    packet[3] = 0x00;
    packet[4] = 0x00;  // Opcode: 1185792 小端序 (0x00121900)
    packet[5] = 0x19;
    packet[6] = 0x12;
    packet[7] = 0x00;
    packet[8] = 0x79;  // Params: 21089 小端序
    packet[9] = 0x52;
    packet[10] = 0x00;
    packet[11] = 0x00;
    packet[12] = 0x00;  // Body: 8字节填充
    packet[13] = 0x00;
    packet[14] = 0x00;
    packet[15] = 0x00;
    packet[16] = 0x00;
    packet[17] = 0x00;
    packet[18] = 0x00;
    packet[19] = 0x00;
    SendPacket(g_LastGameSocket, packet, 20);
    Sleep(400);

    // 3. 跳转到地图 10969: 封包 4453000039121200F92A0000
    // mapId 10969 = 0x00002AF9，小端序: F9 2A 00 00
    mapPacket[8] = 0xF9;
    mapPacket[9] = 0x2A;
    mapPacket[10] = 0x00;
    mapPacket[11] = 0x00;
    SendPacket(g_LastGameSocket, mapPacket, 12);
    Sleep(300);

    // 4. 发送拾取封包: Params 41220 = 0x0000A104
    packet[8] = 0x04;  // Params: 41220 小端序
    packet[9] = 0xA1;
    packet[10] = 0x00;
    packet[11] = 0x00;
    SendPacket(g_LastGameSocket, packet, 20);
    Sleep(400);

    // 5. 跳转到地图 4103: 封包 4453000039121200A20F0000
    // mapId 4103 = 0x00001007，小端序: 07 10 00 00
    // 但易语言是 A20F，即 mapId 0x00000FA2 = 4002？让我重新分析
    // A2 0F 小端序 = 0x0FA2 = 4002
    mapPacket[8] = 0xA2;
    mapPacket[9] = 0x0F;
    mapPacket[10] = 0x00;
    mapPacket[11] = 0x00;
    SendPacket(g_LastGameSocket, mapPacket, 12);
    Sleep(300);

    // 6. 发送拾取封包
    SendPacket(g_LastGameSocket, packet, 20);
    Sleep(400);

    // 7. 跳转到地图 10004: 封包 445300003912120014270000
    // mapId 10004 = 0x00002714，小端序: 14 27 00 00
    mapPacket[8] = 0x14;
    mapPacket[9] = 0x27;
    mapPacket[10] = 0x00;
    mapPacket[11] = 0x00;
    SendPacket(g_LastGameSocket, mapPacket, 12);
    Sleep(300);

    // 8. 发送拾取封包
    SendPacket(g_LastGameSocket, packet, 20);
    Sleep(400);

    return TRUE;
}

/** 采集线程函数 */
static DWORD WINAPI CollectThreadProc(LPVOID lpParam) {
    DWORD flags = *(DWORD*)lpParam;
    delete (DWORD*)lpParam;
    
    g_collectAutoMode = true;
    UIBridge::Instance().UpdateHelperText(L"开始一键采集...");
    
    // 按照标志位采集
    for (int i = 0; i < COLLECT_ITEM_COUNT; i++) {
        if (!(flags & (1 << i))) {
            continue;  // 未选中，跳过
        }
        
        const CollectItemData& item = COLLECT_ITEMS[i];
        
        if (item.maxCount == 0) {
            // 坐骑蛋
            CollectMountEgg();
            UIBridge::Instance().UpdateHelperText(L"拾取坐骑蛋完成");
        } else {
            // 普通采集
            if (CollectSingleItem(item)) {
                UIBridge::Instance().UpdateHelperText(std::wstring(item.name) + L"采集完成");
            } else {
                UIBridge::Instance().UpdateHelperText(std::wstring(item.name) + L"采集失败");
            }
        }
        
        Sleep(500);
    }
    
    // 采集完成，返回地图
    // 易语言封包: 445300003912120086030000
    // 地图ID: 86 03 00 00 → 小端序 0x00000386 = 902
    SendEnterScenePacket(902);
    Sleep(500);
    
    UIBridge::Instance().UpdateHelperText(L"一键采集完成！");
    g_collectAutoMode = false;
    
    return 0;
}

/** 一键采集所有选中的材料 */
BOOL StartOneKeyCollectPacket(DWORD flags) {
    if (flags == 0) {
        UIBridge::Instance().UpdateHelperText(L"请先选择要采集的材料");
        return FALSE;
    }
    
    if (g_userId == 0) {
        UIBridge::Instance().UpdateHelperText(L"请先进入游戏");
        return FALSE;
    }
    
    DWORD* pFlags = new DWORD(flags);
    HANDLE hThread = CreateThread(NULL, 0, CollectThreadProc, pFlags, 0, NULL);
    if (hThread) {
        CloseHandle(hThread);
        return TRUE;
    } else {
        delete pFlags;
        UIBridge::Instance().UpdateHelperText(L"启动采集线程失败");
        return FALSE;
    }
}

/** 处理采集响应（在HookedRecv中调用）
 * 
 * 根据AS3代码 StoneControl.as 和 MainModel.as：
 * - statusid 在封包的 Params 字段（mParams），不是 body
 * - statusid = 1: 开始采集（播放采集动画）
 * - statusid = 2: 采集完成，获得物品（body: stuffid, stuffcount, userid）
 * - statusid = 3: 采集全部结束（body: userid）
 * - statusid = 4: 额外获得物品（body: stuffid, stuffcount）
 * - statusid = -1: 每日采集次数用完
 * - statusid = -2: 未装备采集工具
 */
void ProcessCollectResponse(const GamePacket& packet) {
    if (packet.opcode != Opcode::COLLECT_STATUS_BACK) {
        return;
    }
    
        // statusid 在封包的 Params 字段（AS3: event.msg.mParams）
    
        int32_t statusid = static_cast<int32_t>(packet.params);
    
        
    
        g_collectStatus = statusid;
    
        
    
        // 根据AS3代码逻辑：
    
        // statusid = 2 表示单次采集完成，获得物品
    
        // statusid = 3 表示采集全部结束
    
        if (statusid == 2 || statusid == 3) {
    
            g_collectFinished = true;
    
        }
    
        // statusid = 1: 开始采集动画，继续等待
    
        // statusid = -1: 每日采集次数用完
    
        // statusid = -2: 未装备采集工具
    
    }
    
    
    
    // ============ BOSS专区功能实现 ============
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    /** 发送BOSS战斗封包
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
     * 
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
     * 封包格式（根据易语言代码）：
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
     * - Magic: 21316 (0x5344)
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
     * - Length: 4 (Body长度)
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
     * - Opcode: 1186048 (OP_CLIENT_CLICK_NPC)
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
     * - Params: spiritId (BOSS ID，小端序)
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
     * - Body: 00 00 00 00 (最后4字节替换为0)
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
     * 
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
     * @param spiritId BOSS的妖怪ID（大于10000）
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
     * @return 发送是否成功
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
     */
    
    
    
BOOL SendBattlePacket(uint32_t spiritId, uint32_t useId, uint8_t extraParam, uint32_t forcedCounter) {
    uint32_t counter = forcedCounter != 0 ? forcedCounter : g_battleCounter.load();
    if (counter == 0) {
        counter = 1;
    }

    PacketBuilder builder;
    builder.SetOpcode(Opcode::CLICK_NPC);

    if (extraParam == 0) {
        // AS3 BattleControl.onClientNpc：Params=sid，Body=[counter]，
        // 带 useid 时追加第二个 Body 字段，而不是把 useid 编进 Params。
        builder.SetParams(spiritId)
               .WriteUInt32(counter);
        if (useId != 0) {
            builder.WriteUInt32(useId);
        }
    } else {
        // 任务区水魂等历史特殊入口的 Params 布局已经由抓包验证，保留其
        // 专用编码，不与普通 NPC/Boss 入口混用。
        const uint32_t params = (spiritId & 0xFF) |
                                ((static_cast<uint32_t>(extraParam) & 0xFF) << 8) |
                                ((useId & 0xFF) << 24);
        builder.SetParams(params)
               .WriteUInt32(counter);
    }

    const std::vector<uint8_t> packet = builder.Build();
    if (packet.empty()) {
        return FALSE;
    }
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

    // ============================================================================
    // 福瑞宝箱功能 (HeavenFurui) owner：局部静态状态统一跟随该功能块维护。
    // ============================================================================
    
    // 福瑞宝箱局部静态变量
    static std::vector<int> g_heavenFuruiBoxIds;
    static int g_heavenFuruiCurrentMapId = 0;
    
    // 福瑞宝箱地图列表（用户指定的地图ID及名称）
    static constexpr int HEAVEN_FURUI_MAPS[] = {
        // 大唐
        2001,   // 长安城
        2002,   // 皇宫
        2003,   // 双叉岭
        2004,   // 五指山
        2005,   // 五指山顶
        2006,   // 炼丹阁
        2008,   // 逍遥仙境
        // 大唐边境
        2101,   // 蛇盘山
        2102,   // 黑风山
        2103,   // 高老庄
        // 乌斯藏国
        3001,   // 乌斯藏国国都
        3002,   // 黄风岭
        3003,   // 流沙河
        3004,   // 白骨山
        3006,   // 白骨洞
        // 宝象国
        4001,   // 宝象国国都
        4002,   // 黑松林
        4003,   // 平顶山
        // 乌鸡国
        5001,   // 枯松涧
        5002,   // 黑水河
        5003,   // 乌鸡国国都
        // 车迟国
        6001,   // 车迟国都
        6002,   // 通天河
        6003,   // 金兜山
        6004,   // 车迟山
        6005,   // 三清殿
        // 女儿国
        7001,   // 子母河
        7002,   // 毒敌山
        7003,   // 女儿国国都
        7004,   // 伏魔殿
        // 火焰山
        8001,   // 火焰山入口
        // 祭赛国
        9001,   // 祭赛国国都
        9002,   // 乱石山
        9003,   // 荆棘岭
        9004,   // 七绝山
        9006,   // 碧波潭
        // 朱紫国
        10001,  // 朱紫国国都
        10002,  // 麒麟山
        10003,  // 盘丝岭
        10004,  // 蜈蚣岭
        // 狮驼国
        11000,  // 狮驼国国都
        11001,  // 狮驼岭
        11002,  // 云雷墟
        11003,  // 狮驼洞
        11004,  // 后洞
        // 封神岭
        16001,  // 封神岭
        16003,  // 锁灵谷
        16004,  // 超能幻境
        16005,  // 星空堡垒
        16006,  // 神木林
        // 花果山
        30005,  // 花果山
        // 傲来国
        40001,  // 傲来海岸
        // 地府
        90001,  // 鬼门关
        90002,  // 黄泉路
        90003,  // 忘川河
        90004,  // 阎王殿
        90005,  // 内殿
        // 活动乐园
        18001,  // 活动乐园
        // 獬豸洞
        10006,  // 獬豸洞
        // 比丘国
        19001,  // 比丘国国都
        19002,  // 金殿
        19003,  // 柳林坡
        19004,  // 陷空山
        19005,  // 圣光神庙
        19006,  // 镇海寺
        19007,  // 清华洞
        19008,  // 无底洞
        // 灭法国
        21001,  // 灭法国国都
        21002,  // 宫殿
        21003,  // 凤仙郡
        21004,  // 蓬莱镇
        21005,  // 隐雾山
        21006,  // 隐雾山崖底
        21007,  // 折岳连环洞
        21008   // 达摩遗址
    };
    
    /**
     * @brief 构建福瑞宝箱封包
     */
    static std::vector<BYTE> BuildHeavenFuruiPacket(int opType, const std::vector<int32_t>& bodyValues) {
        PacketBuilder builder;
        builder.SetOpcode(Opcode::HEAVEN_FURUI_SEND)
               .SetParams(HeavenFurui::ACTIVITY_ID)
               .WriteInt32(opType)
               .WriteInt32Array(bodyValues);
        return builder.Build();
    }
    
    /**
     * @brief 福瑞宝箱 - 发送活动操作封包
     */
    BOOL SendHeavenFuruiPacket(int opType, const std::vector<int32_t>& bodyValues) {
        std::vector<BYTE> packet = BuildHeavenFuruiPacket(opType, bodyValues);
        return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()),
                          Opcode::HEAVEN_FURUI_BACK, WpeHook::TIMEOUT_RESPONSE);
    }
    
    /**
     * @brief 福瑞宝箱 - 查询地图是否有宝箱
     */
    BOOL SendHeavenFuruiQueryPacket(int mapId) {
        g_heavenFuruiQuerySuccess = false;
        g_heavenFuruiBoxCount = 0;
        g_heavenFuruiBoxIds.clear();
        g_heavenFuruiCurrentMapId = mapId;
        return SendHeavenFuruiPacket(HeavenFurui::OP_QUERY, {mapId});
    }
    
    /**
     * @brief 福瑞宝箱 - 拾取宝箱
     */
    BOOL SendHeavenFuruiPickupPacket(int mapId, int boxId) {
        return SendHeavenFuruiPacket(HeavenFurui::OP_PICKUP, {mapId, boxId});
    }
    
    /**
     * @brief 福瑞宝箱 - 确认拾取
     */
    BOOL SendHeavenFuruiConfirmPacket() {
        return SendHeavenFuruiPacket(HeavenFurui::OP_CONFIRM, {0, 0});
    }
    
    /**
     * @brief 停止福瑞宝箱
     */
    void StopHeavenFurui() {
        g_heavenFuruiRunning = false;
    }
    
    /**
     * @brief 设置福瑞宝箱最大开启数量
     * @param maxBoxes 最大数量（1-30）
     */
    void SetHeavenFuruiMaxBoxes(int maxBoxes) {
        if (maxBoxes < 1) maxBoxes = 1;
        if (maxBoxes > 30) maxBoxes = 30;
        g_heavenFuruiMaxBoxes = maxBoxes;
    }
    
    /**
     * @brief 福瑞宝箱一键完成线程
     */
    DWORD WINAPI HeavenFuruiThreadProc(LPVOID lpParam) {
        (void)lpParam;
        Sleep(300);
        
        // 重置已开启数量
        g_heavenFuruiOpenedBoxes = 0;
        
        // 更新辅助提示
        auto updateHelper = [](const std::wstring& text) {
            UIBridge::Instance().UpdateHelperText(text);
        };
        
        int maxBoxes = g_heavenFuruiMaxBoxes.load();
        wchar_t startMsg[256];
        swprintf_s(startMsg, L"福瑞宝箱：开始查找宝箱（目标：%d个）...", maxBoxes);
        updateHelper(startMsg);
        
        int totalBoxesPicked = 0;
        
        // 遍历所有地图
        for (int mapId : HEAVEN_FURUI_MAPS) {
            // 检查是否已达到目标数量或被停止
            if (!g_heavenFuruiRunning || totalBoxesPicked >= maxBoxes) {
                break;
            }
            
            // 获取地图名称
            std::wstring mapName = GetMapName(mapId);
            if (mapName.empty()) {
                mapName = std::to_wstring(mapId);
            }
            
            // 更新提示
            wchar_t msg[256];
            swprintf_s(msg, L"福瑞宝箱：正在查看地图 [%s]...（已拾取：%d/%d）", 
                      mapName.c_str(), totalBoxesPicked, maxBoxes);
            updateHelper(msg);
            
            // 查询地图是否有宝箱
            g_heavenFuruiQuerySuccess = false;
            SendHeavenFuruiQueryPacket(mapId);
            
            // 等待响应
            for (int i = 0; i < 10 && !g_heavenFuruiQuerySuccess && g_heavenFuruiRunning; i++) {
                Sleep(100);
            }
            
            // 如果有宝箱且未达到目标
            if (g_heavenFuruiBoxCount > 0 && totalBoxesPicked < maxBoxes && g_heavenFuruiRunning) {
                swprintf_s(msg, L"福瑞宝箱：在 [%s] 发现 %d 个宝箱，正在进入地图...", 
                          mapName.c_str(), g_heavenFuruiBoxCount.load());
                updateHelper(msg);
                
                // 进入地图
                g_heavenFuruiEnteredMap = false;
                SendEnterScenePacket(mapId);
                
                // 等待进入地图
                for (int i = 0; i < 20 && !g_heavenFuruiEnteredMap && g_heavenFuruiRunning; i++) {
                    Sleep(100);
                }
                
                Sleep(500);  // 等待地图加载
                
                // 拾取宝箱（不超过目标数量）
                for (int boxId : g_heavenFuruiBoxIds) {
                    // 检查是否已达到目标或被停止
                    if (!g_heavenFuruiRunning || totalBoxesPicked >= maxBoxes) {
                        break;
                    }
                    
                    swprintf_s(msg, L"福瑞宝箱：正在拾取宝箱 (ID: %d)...（已拾取：%d/%d）", 
                              boxId, totalBoxesPicked + 1, maxBoxes);
                    updateHelper(msg);
                    
                    // 发送拾取封包
                    SendHeavenFuruiPickupPacket(mapId, boxId);
                    Sleep(200);
                    
                    // 发送确认封包
                    SendHeavenFuruiConfirmPacket();
                    Sleep(200);
                    
                    totalBoxesPicked++;
                    g_heavenFuruiOpenedBoxes = totalBoxesPicked;
                    
                    // 更新UI进度
                    wchar_t progressJs[256];
                    swprintf_s(progressJs, L"if(window.updateHeavenFuruiProgress) window.updateHeavenFuruiProgress(%d, %d);", 
                              totalBoxesPicked, maxBoxes);
                    UIBridge::Instance().ExecuteJS(progressJs);
                }
                
                swprintf_s(msg, L"福瑞宝箱：已在 [%s] 拾取宝箱，当前进度：%d/%d", 
                          mapName.c_str(), totalBoxesPicked, maxBoxes);
                updateHelper(msg);
                
                Sleep(500);
            }
            
            // 清空当前地图的宝箱列表
            g_heavenFuruiBoxIds.clear();
            g_heavenFuruiBoxCount = 0;
        }
        
        // 完成
        g_heavenFuruiRunning = false;
        
        // 通知UI更新按钮状态
        UIBridge::Instance().ExecuteJS(L"if(window.onHeavenFuruiComplete) window.onHeavenFuruiComplete();");
        
        wchar_t finalMsg[256];
        if (totalBoxesPicked >= maxBoxes) {
            swprintf_s(finalMsg, L"福瑞宝箱：已完成！共拾取 %d 个宝箱（达到目标）", totalBoxesPicked);
        } else {
            swprintf_s(finalMsg, L"福瑞宝箱：遍历完成！共拾取 %d 个宝箱", totalBoxesPicked);
        }
        updateHelper(finalMsg);
        
        return 0;
    }
    
    /**
     * @brief 福瑞宝箱 - 一键完成
     */
    BOOL StartOneKeyHeavenFuruiPacket(int maxBoxes) {
        if (g_heavenFuruiRunning) {
            // 如果正在运行，则停止
            g_heavenFuruiRunning = false;
            return TRUE;
        }
        
        // 设置最大宝箱数量
        SetHeavenFuruiMaxBoxes(maxBoxes);
        
        g_heavenFuruiRunning = true;
        
        // 启动线程执行一键完成
        HANDLE hThread = CreateThread(nullptr, 0, HeavenFuruiThreadProc, nullptr, 0, nullptr);
        if (hThread) {
            CloseHandle(hThread);
            return TRUE;
        }
        
        g_heavenFuruiRunning = false;
        return FALSE;
    }
    
    /**
     * @brief 处理福瑞宝箱响应
     * 
     * 响应格式 (protocolId = 1):
     * - isVailTime: 有效时间标志
     * - clickIconFlag: 点击图标标志
     * - boxNum: 宝箱数量
     * - boxId[]: 宝箱ID列表
     * - openBoxNum: 已开宝箱数量
     */
    void ProcessHeavenFuruiResponse(const GamePacket& packet) {
        if (packet.params != HeavenFurui::ACTIVITY_ID) return;
        if (packet.body.size() < 4) return;
        
        const BYTE* body = packet.body.data();
        size_t offset = 0;
        
        // 读取 protocolId
        int protocolId = ReadInt32LE(body, offset);
        
        if (protocolId == 1) {
            // 服务器推送福瑞宝箱状态
            g_heavenFuruiQuerySuccess = true;
            
            if (offset + 12 <= packet.body.size()) {
                (void)ReadInt32LE(body, offset);  // isVailTime：占位字段
                (void)ReadInt32LE(body, offset);  // clickIconFlag：占位字段
                int boxNum = ReadInt32LE(body, offset);
                
                // 如果已有宝箱数据且新响应 boxNum=0，则忽略
                // 这可能是进入地图后服务器的状态推送，不应覆盖查询结果
                if (boxNum == 0 && g_heavenFuruiBoxCount > 0 && !g_heavenFuruiBoxIds.empty()) {
                    // 保持现有数据不变，仅标记查询成功
                    return;
                }
                
                g_heavenFuruiBoxIds.clear();
                
                // 读取宝箱ID列表
                for (int i = 0; i < boxNum && offset + 4 <= packet.body.size(); i++) {
                    int boxId = ReadInt32LE(body, offset);
                    g_heavenFuruiBoxIds.push_back(boxId);
                }
                
                // 实际保存的宝箱数量（以实际读取的ID数量为准）
                g_heavenFuruiBoxCount = static_cast<int>(g_heavenFuruiBoxIds.size());
                
                // 读取已开宝箱数量
                if (offset + 4 <= packet.body.size()) {
                    (void)ReadInt32LE(body, offset);  // openBoxNum：占位字段
                }
                
                // 通知UI - 显示实际保存的宝箱数量
                std::wstring mapName = GetMapName(g_heavenFuruiCurrentMapId);
                if (mapName.empty()) mapName = std::to_wstring(g_heavenFuruiCurrentMapId);
                
                wchar_t msg[256];
                swprintf_s(msg, L"福瑞宝箱：地图 [%s] 发现 %d 个宝箱（响应boxNum=%d, bodySize=%zu）", 
                          mapName.c_str(), g_heavenFuruiBoxCount.load(), boxNum, packet.body.size());
                UIBridge::Instance().UpdateHelperText(msg);
            }
        }
        else if (protocolId == 6) {
            // protocolId = 6: 服务器推送当前宝箱数量状态
            // 根据 AS3 代码，这个响应只用于更新UI（移除按钮效果），
            // 不应该覆盖之前查询得到的宝箱ID列表
            // 只有在非查询状态（即服务器主动推送）时才更新
            if (offset + 4 <= packet.body.size()) {
                int boxNum = ReadInt32LE(body, offset);
                // 如果当前没有保存的宝箱ID，才用这个值更新数量
                // 否则保持查询得到的宝箱信息不变
                if (g_heavenFuruiBoxIds.empty()) {
                    g_heavenFuruiBoxCount = boxNum;
                }
                // 注意：不更新 g_heavenFuruiQuerySuccess，因为这不是查询响应
            }
        }
    }
    





// ============================================================================
// 坐骑大赛实现已迁移到 src/hook/horse_competition.cpp.
// ============================================================================


// ============================================================================
// 最新活动：剩余次数查询与回填
//
// 各活动 open_ui 响应会把剩余次数解析进各自状态的 playCount。这里逐个下发
// open_ui 探针并回填前端：
//   - 有响应且解析出次数：按 playCount 回填（0 表示次数已用完，前端禁用该行）。
//   - 有响应但未解析出次数：按 0（不可用）处理。
//   - 无响应（活动已过期/服务端不再下发）：按 0（不可用）处理并禁用该行。
//   - 未连接游戏：上报 -1，前端显示“--”且保持可勾选，避免误禁用。
//
// 探针使用比常规流程更短的超时：过期活动不会响应，若沿用 3s 超时，9 个活动会
// 让刷新静默挂起近半分钟。刷新过程中回显进度，结束时给出汇总，避免一直停留在
// “正在获取活动剩余次数...”。
// 必须在所有活动实现之后定义，故置于文件末尾。
// ============================================================================
namespace {

// 探针超时：比常规流程的 TIMEOUT_RESPONSE(3s) 更短，过期活动需尽快跳过。
constexpr DWORD kActivityCountProbeTimeoutMs = 1500;

// 防止重复点击“刷新次数”导致多个查询线程相互抢占响应等待。
std::atomic<bool> g_activityCountRefreshing{false};

struct ActivityCountTarget {
    uint32_t activityId;
    const wchar_t* name;
};

const ActivityCountTarget kActivityCountTargets[] = {
    // 最新活动（周活动小游戏，按日期码筛选出的未过期批次）
    {LatestAct::ACT_MAZE, L"突破迷阵"},
    {LatestAct::ACT_TOUCH_STAR, L"摘星之旅"},
    {LatestAct::ACT_EXPLORE, L"寒冰洞历险记"},
    {LatestAct::ACT_MOON_CAKE, L"月饼大胃王"},
    {LatestAct::ACT_FLOWER, L"夺回花束行动"},
    {LatestAct::ACT_FOOD, L"美食大作战"},
};

constexpr int kActivityCountTargetNum =
    static_cast<int>(sizeof(kActivityCountTargets) / sizeof(kActivityCountTargets[0]));

// 返回单个活动剩余次数；< 0 表示未解析出（不含“次数为 0”的合法情况）。
int GetActivityRemainingCount(uint32_t activityId) {
    auto& mgr = ActivityStateManager::Instance();
    switch (activityId) {
        case Act717::ACTIVITY_ID: return mgr.GetAct717State().playCount.load();
        case Act684::ACTIVITY_ID: return mgr.GetAct684State().playCount.load();
        case Act827::ACTIVITY_ID: return mgr.GetAct827State().playCount.load();
        case Act826::ACTIVITY_ID: return mgr.GetAct826State().playCount.load();
        case Act822::ACTIVITY_ID: return mgr.GetAct822State().playCount.load();
        case Act685::ACTIVITY_ID: return mgr.GetAct685State().playCount.load();
        case Act631::ACTIVITY_ID: return mgr.GetAct631State().playCount.load();
        case Act641::ACTIVITY_ID: return mgr.GetAct641State().playCount.load();
        case Act808::ACTIVITY_ID: return g_act808PlayCount.load();
        default:
            // 最新活动（周活动小游戏）由独立模块维护次数。
            return IsLatestActivity(activityId)
                ? GetLatestActivityPlayCount(activityId)
                : -1;
    }
}

void ReportActivityCount(uint32_t activityId, int remainingCount) {
    wchar_t jsCode[256];
    swprintf_s(
        jsCode,
        L"if(window.updateActivityCounts) { window.updateActivityCounts({ %u: %d }); }",
        activityId,
        remainingCount);
    UIBridge::Instance().ExecuteJS(jsCode);
}

// 直接构造进入活动的探针封包，协议字节与各活动 SendActNNNOpenUIPacket 一致，
// 但使用更短超时。各活动响应处理器只读取 operation/playCount，不依赖
// waitingResponse 预置，故此处无需先置位等待标记。
bool ProbeActivityOpenUI(uint32_t activityId) {
    // 749/704 等活动的进入操作是 "game_info"，不能一律用 "open_ui"。
    std::vector<BYTE> packet = BuildActivityPacket(
        Opcode::ACTIVITY_QINGYANG_NEW_SEND, activityId,
        GetLatestActivityOpenUIOperation(activityId), {});
    if (packet.empty()) {
        return false;
    }

    return SendPacket(
               0,
               packet.data(),
               static_cast<DWORD>(packet.size()),
               Opcode::ACTIVITY_QUERY_BACK,
               kActivityCountProbeTimeoutMs,
               activityId,
               true) != FALSE;
}

}  // anonymous namespace

void QueryActivityCountsAsync() {
    bool expected = false;
    if (!g_activityCountRefreshing.compare_exchange_strong(expected, true)) {
        UIBridge::Instance().UpdateHelperText(L"正在刷新活动次数，请稍候...");
        return;
    }

    HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        if (!g_bInitialized) {
            g_activityCountRefreshing = false;
            return 0;
        }

        // 未连接游戏时不应把活动判成“已过期”，只上报未知（-1）并提示。
        if (g_LastGameSocket.load() == 0) {
            for (const auto& target : kActivityCountTargets) {
                ReportActivityCount(target.activityId, -1);
            }
            UIBridge::Instance().UpdateHelperText(L"未连接游戏，无法获取活动次数");
            g_activityCountRefreshing = false;
            return 0;
        }

        int availableCount = 0;
        int unavailableCount = 0;

        for (int i = 0; i < kActivityCountTargetNum; ++i) {
            if (!g_bInitialized) {
                break;
            }

            const uint32_t activityId = kActivityCountTargets[i].activityId;

            wchar_t progress[128];
            swprintf_s(
                progress,
                L"正在获取活动次数 (%d/%d)：%s",
                i + 1,
                kActivityCountTargetNum,
                kActivityCountTargets[i].name);
            UIBridge::Instance().UpdateHelperText(progress);

            // 最新活动的一键流程与探针共用同一响应通道（都会清 waitingResponse），
            // 抢不到占用时上报「未知」，不能误判成「不可用/已过期」。
            const bool needProbeLock = IsLatestActivity(activityId);
            if (needProbeLock && !BeginLatestActivityProbe(activityId)) {
                ReportActivityCount(activityId, -1);
                continue;
            }

            if (!ProbeActivityOpenUI(activityId)) {
                // 无响应：活动已过期或服务端不再下发，按不可用（0）处理。
                ReportActivityCount(activityId, 0);
                ++unavailableCount;
                if (needProbeLock) {
                    EndLatestActivityProbe(activityId);
                }
                Sleep(120);
                continue;
            }

            // 响应等待被唤醒发生在 recv hook，而解析 playCount 的 handler 跑在
            // 派发 worker 线程上，二者存在先后差；等待 handler 落地后再读取。
            Sleep(150);
            const int remaining = GetActivityRemainingCount(activityId);
            if (remaining < 0) {
                // 有响应但未解析出次数，同样视为不可用。
                ReportActivityCount(activityId, 0);
                ++unavailableCount;
            } else {
                ReportActivityCount(activityId, remaining);
                ++availableCount;
            }

            if (needProbeLock) {
                EndLatestActivityProbe(activityId);
            }
            Sleep(120);
        }

        wchar_t summary[160];
        swprintf_s(
            summary,
            L"刷新完成：可用 %d 个，不可用/已过期 %d 个",
            availableCount,
            unavailableCount);
        UIBridge::Instance().UpdateHelperText(summary);

        g_activityCountRefreshing = false;
        return 0;
    }, nullptr, 0, nullptr);

    if (hThread) {
        CloseHandle(hThread);
        return;
    }

    g_activityCountRefreshing = false;
    UIBridge::Instance().UpdateHelperText(L"刷新活动次数失败：无法创建查询线程");
}

SOCKET GetLastGameSocket() {
    return g_LastGameSocket.load();
}
