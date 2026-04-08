/**
 * @file wpe_hook.cpp
 * @brief WPE网络封包拦截模块实现
 * 
 * 实现 Windows Socket API 的 Hook，拦截游戏网络封包
 */

#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <string>
#include <vector>
#include <memory>
#include <tuple>
#include <algorithm>
#include <deque>
#include <map>
#include <unordered_map>
#include <fstream>
#include <cstdarg>
#include <thread>
#include <queue>
#include <random>
#include <cctype>
#include <chrono>

// 第三方库
#include <MemoryModule.h>
#include <MinHook.h>

// 项目头文件
#include "wpe_hook.h"
#include "wpe_hook_internal.h"
#include "activity_states_internal.h"
#include "activity_minigames.h"
#include "battle_six.h"
#include "dungeon_jump.h"
#include "horse_competition.h"
#include "shuangtai.h"
#include "shuangtai_internal.h"
#include "spirit_collect.h"
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
static void ProcessEightTrigramsUserTaskListResponse(const GamePacket& packet);
static void ProcessEightTrigramsTaskTalkResponse(const GamePacket& packet);

extern bool PostScriptToUI(const std::wstring& jsCode);

namespace {

bool ReadPacketString(const BYTE* body, size_t bodySize, size_t& offset, std::string& out) {
    if (offset + 2 > bodySize) {
        return false;
    }

    uint16_t len = ReadUInt16LE(body, offset);
    if (offset + len > bodySize) {
        return false;
    }

    out.assign(reinterpret_cast<const char*>(body + offset), len);
    offset += len;
    return true;
}

void NotifySpiritAlert(const std::wstring& message) {
    std::wstring jsCode = L"if(window.handleSpiritCollectData) { window.handleSpiritCollectData({type: 'alert', message: '" + UIBridge::EscapeJsonString(message) + L"'}); }";
    PostScriptToUI(jsCode);
}

void NotifySpiritSuccess(const std::wstring& message) {
    std::wstring jsCode = L"if(window.handleSpiritCollectData) { window.handleSpiritCollectData({type: 'sendSuccess', message: '" + UIBridge::EscapeJsonString(message) + L"'}); }";
    PostScriptToUI(jsCode);
}

void NotifySpiritConfirm(const std::wstring& playerName) {
    std::wstring jsCode = L"if(window.handleSpiritCollectData) { window.handleSpiritCollectData({type: 'confirm', playerName: '" + UIBridge::EscapeJsonString(playerName) + L"'}); }";
    PostScriptToUI(jsCode);
}

}

// 嵌入资源
#include "embedded/minhook_data.h"

// 外部变量声明（来自 packet_parser.cpp）
extern std::unordered_map<int, std::wstring> g_petNames;
extern std::unordered_map<int, std::wstring> g_skillNames;
extern std::unordered_map<int, std::wstring> g_elemNames;
extern std::unordered_map<int, std::wstring> g_geniusNames;
extern std::unordered_map<int, int> g_skillPowers;
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

bool TryParseUInt32Decimal(const std::string& text, uint32_t& value) {
    if (text.empty()) {
        return false;
    }

    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }
    if (start >= text.size()) {
        return false;
    }

    char* end = nullptr;
    const char* begin = text.c_str() + start;
    unsigned long parsed = std::strtoul(begin, &end, 10);
    if (end == begin) {
        return false;
    }

    while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end))) {
        ++end;
    }
    if (*end != '\0' && *end != ',' && *end != '}' && *end != ']') {
        return false;
    }

    value = static_cast<uint32_t>(parsed);
    return true;
}

bool TryParseIntDecimal(const std::string& text, int& value) {
    if (text.empty()) {
        return false;
    }

    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }
    if (start >= text.size()) {
        return false;
    }

    char* end = nullptr;
    const char* begin = text.c_str() + start;
    long parsed = std::strtol(begin, &end, 10);
    if (end == begin) {
        return false;
    }

    while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end))) {
        ++end;
    }
    if (*end != '\0' && *end != ',' && *end != '}' && *end != ']') {
        return false;
    }

    value = static_cast<int>(parsed);
    return true;
}

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
SOCKET g_LastGameSocket = 0;      ///< 最近的游戏套接字
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

// 清明赏河景状态 (Act803)
static std::atomic<int> g_act803PlayCount{0};
static std::atomic<int> g_act803RestTime{0};
static std::atomic<int> g_act803RedBerryCount{0};
static std::atomic<int> g_act803IsPop{0};
static std::atomic<int> g_act803IsMoveControl{0};
static std::atomic<int> g_act803CheckCode{0};
static std::atomic<int> g_act803LastResult{-1};
static std::atomic<bool> g_act803WaitingResponse{false};
static std::atomic<bool> g_act803UseSweep{false};
static std::atomic<bool> g_act803SweepAvailable{false};
static std::vector<int> g_act803HasAwardArr;

// 采蘑菇的好伙伴状态 (Act624)
static std::atomic<int> g_act624PlayCount{0};
static std::atomic<int> g_act624RestTime{0};
static std::atomic<int> g_act624CanGetFlag{0};
static std::atomic<int> g_act624GotFlag{0};
static std::atomic<int> g_act624TotalBadgeNum{0};
static std::atomic<int> g_act624FinishedNum{0};
static std::atomic<int> g_act624CheckCode{0};
static std::atomic<int> g_act624LastResult{-1};
static std::atomic<bool> g_act624WaitingResponse{false};
static std::atomic<bool> g_act624UseSweep{false};
static std::atomic<bool> g_act624SweepAvailable{false};

// -------------------------
// 调试日志函数
// -------------------------

static bool ReadLengthPrefixedString(const std::vector<BYTE>& body, size_t& offset, std::string& value) {
    if (offset + 2 > body.size()) return false;

    const BYTE* data = body.data();
    uint16_t len = static_cast<uint16_t>(data[offset] | (data[offset + 1] << 8));
    offset += 2;
    if (offset + len > body.size()) return false;

    value.assign(reinterpret_cast<const char*>(data + offset), len);
    offset += len;
    return true;
}

static std::vector<int32_t> BuildAct803AwardValues() {
    return std::vector<int32_t>(15, 0);
}

static BOOL SendActivityPacket(uint32_t activityId, const std::string& operation, const std::vector<int32_t>& bodyValues = {}) {
    std::vector<BYTE> packet = BuildActivityPacket(Opcode::ACTIVITY_QINGYANG_NEW_SEND, activityId, operation, bodyValues);
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()),
                      Opcode::ACTIVITY_QUERY_BACK, WpeHook::TIMEOUT_RESPONSE,
                      activityId, true);
}

}  // anonymous namespace

// ============================================================================
// 导出全局变量（在头文件中声明为 extern）
// ============================================================================

std::atomic<uint32_t> g_userId{0};  ///< 卡布号（从进入世界封包获取）

// 注意：g_hWnd 在 demo.cpp 中定义，这里通过 wpe_hook.h 的 extern 声明使用

// 基础战斗开关 owner：回血、屏蔽战斗、自动回家和战斗校验标记统一由这一组维护。
std::atomic<bool> g_autoHeal{false};  ///< 自动回血
std::atomic<bool> g_blockBattle{false};  ///< 屏蔽战斗
std::atomic<bool> g_autoGoHome{false};   ///< 自动回家（玄塔）
std::atomic<int32_t> g_battleCounter{0};  ///< 战斗counter（用于战斗校验）
std::atomic<bool> g_battleStarted{false};  ///< 是否已进入战斗

// MD5 验证自动回复 owner：索引仅用于该自动回复流程。
std::atomic<int> g_md5CheckIndex{0};  ///< MD5验证自动回复索引（-1=禁用，0-3=有效，99=测试）

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

// 八卦灵盘任务区 owner：运行标记、会话号和地图进入状态统一由这里维护。
std::atomic<bool> g_taskZoneRunning{false};
std::atomic<unsigned long long> g_taskZoneSession{0};
std::atomic<bool> g_taskZoneMapEntered{false};

// 八卦灵盘任务区进度 owner：用户任务缓存与对话进度统一由这里维护。
struct EightTrigramsProgressState {
    std::mutex mutex;
    bool userTaskListLoaded = false;
    std::vector<uint32_t> acceptedSubtaskIds;
    std::vector<uint32_t> finishedSubtaskIds;
    bool taskTalkResponseReceived = false;
    uint32_t taskTalkResponseType = 0;
    uint32_t taskTalkResponseDialogId = 0;
    uint32_t taskTalkResponseNpcId = 0;
    bool taskTalkResponseMatchNpcId = true;
    uint32_t talkCurrentId = 0;
    uint32_t talkDialogId = 0;
    uint32_t talkTrigram = 0;
    uint32_t talkExp = 0;
    uint32_t talkNpcId = 0;
    uint32_t talkItemId = 0;
};

EightTrigramsProgressState g_eightTrigramsProgress;
std::atomic<int> g_eightTrigramsResumeStepIndex{-1};

// 背包缓存 owner：位置映射和物品列表统一由这一组维护。
std::map<uint32_t, uint32_t> g_itemPositionMap;
std::vector<PackItemInfo> g_packItems;

// 万妖盛会状态变量
BattleSixAutoBattle g_battleSixAuto;
// 万妖盛会流程 owner：阶段、token 和结算标记统一由该流程维护。
std::atomic<bool> g_battleSixMatching{false};  ///< 是否正在匹配
std::atomic<bool> g_battleSixMatchSuccess{false};  ///< 匹配是否成功
std::atomic<int> g_battleSixSwitchTargetId{-1};  ///< 切换目标精灵uniqueId
std::atomic<int> g_battleSixSwitchRetryCount{0};  ///< 切换重试次数
std::atomic<unsigned long long> g_battleSixBattleSession{0};  ///< 当前战斗会话号，避免延迟线程串场

enum BattleSixFlowStage {
    BATTLESIX_FLOW_IDLE = 0,
    BATTLESIX_FLOW_MATCHING = 1,
    BATTLESIX_FLOW_WAITING_BATTLE_START = 2,
    BATTLESIX_FLOW_PREPARING_COMBAT = 3
};

std::atomic<int> g_battleSixFlowStage{BATTLESIX_FLOW_IDLE};
std::atomic<unsigned long long> g_battleSixFlowToken{0};
constexpr uint32_t BATTLE_SIX_SETTLEMENT_TEXT_BACK = 1317130;  // 万妖盛会胜负文本弹窗回包
constexpr uint32_t BATTLE_SIX_SETTLEMENT_PVP_BACK = 1317154;   // 万妖盛会 PVP 结算弹窗回包
std::atomic<bool> g_battleSixPostSettlementEndSent{false};
std::atomic<bool> g_battleSixReadySupplementSent{false};
std::atomic<unsigned long long> g_battleSixRoundToken{0};
std::atomic<unsigned long long> g_battleSixRoundResultToken{0};
std::atomic<unsigned long long> g_battleSixPlayOverToken{0};

unsigned long long AdvanceBattleSixFlowStage(int stage) {
    g_battleSixFlowStage = stage;
    return g_battleSixFlowToken.fetch_add(1) + 1;
}

void ResetBattleSixFlowState() {
    g_battleSixMatching = false;
    g_battleSixMatchSuccess = false;
    g_battleSixFlowStage = BATTLESIX_FLOW_IDLE;
    g_battleSixFlowToken.fetch_add(1);
}

void ScheduleBattleSixRecoveryViaCombatInfo(DWORD delayMs) {
    DWORD* pDelay = new DWORD(delayMs);
    HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID param) -> DWORD {
        std::unique_ptr<DWORD> delay(static_cast<DWORD*>(param));
        Sleep(*delay);
        if (!g_battleSixAuto.IsAutoMatching() || g_battleSixAuto.IsInBattle()) {
            return 0;
        }
        if (g_battleSixFlowStage.load() != BATTLESIX_FLOW_IDLE) {
            return 0;
        }
        UIBridge::Instance().UpdateHelperText(L"万妖盛会：重新查询并继续匹配...");
        SendBattleSixCombatInfoPacket();
        return 0;
    }, pDelay, 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
    } else {
        delete pDelay;
    }
}

static BOOL SendBattleReadyPacket();
static BOOL SendBattlePlayOverPacket();

static void AdjustBattleSixSpiritSkillPP(std::vector<BattleSixSpiritInfo>& spirits,
                                         int spiritSid,
                                         int skillId,
                                         int delta) {
    if (skillId <= 0 || delta == 0) {
        return;
    }

    for (auto& spirit : spirits) {
        if (spirit.sid != spiritSid) {
            continue;
        }

        for (auto& skill : spirit.skills) {
            if (skill.skillId != skillId) {
                continue;
            }

            const int nextPP = skill.currentPP + delta;
            if (skill.maxPP > 0) {
                if (nextPP < 0) {
                    skill.currentPP = 0;
                } else if (nextPP > skill.maxPP) {
                    skill.currentPP = skill.maxPP;
                } else {
                    skill.currentPP = nextPP;
                }
            } else {
                skill.currentPP = (nextPP < 0) ? 0 : nextPP;
            }
            skill.available = (skill.currentPP > 0);
            return;
        }

        return;
    }
}

void ArmBattleSixFlowWatchdog(
    DWORD timeoutMs,
    int stage,
    unsigned long long token,
    bool cancelBeforeRecovery,
    const wchar_t* timeoutText) {
    struct BattleSixWatchdogContext {
        DWORD timeoutMs;
        int stage;
        unsigned long long token;
        bool cancelBeforeRecovery;
        std::wstring timeoutText;
    };

    auto* context = new BattleSixWatchdogContext{timeoutMs, stage, token, cancelBeforeRecovery, timeoutText};
    HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID param) -> DWORD {
        std::unique_ptr<BattleSixWatchdogContext> context(static_cast<BattleSixWatchdogContext*>(param));
        Sleep(context->timeoutMs);

        if (!g_battleSixAuto.IsAutoMatching() || g_battleSixAuto.IsInBattle()) {
            return 0;
        }
        if (g_battleSixFlowToken.load() != context->token ||
            g_battleSixFlowStage.load() != context->stage) {
            return 0;
        }

        UIBridge::Instance().UpdateHelperText(context->timeoutText);
        if (context->cancelBeforeRecovery) {
            SendBattleSixCancelMatchPacket();
        } else {
            ResetBattleSixFlowState();
        }
        ScheduleBattleSixRecoveryViaCombatInfo(1500);
        return 0;
    }, context, 0, nullptr);

    if (hThread) {
        CloseHandle(hThread);
    } else {
        delete context;
    }
}

// 登录 key 捕获 owner：原始 key 字符串和捕获标记统一由这里维护。
std::wstring g_loginKey;                     ///< 整个封包的十六进制字符串（大写）
// 登录 key 捕获 owner：仅记录当前是否已捕获登录 key。
std::atomic<bool> g_loginKeyCaptured{false}; ///< 是否已捕获登录 key

namespace {

// 跳舞大赛 owner：进程、响应状态和进度统一由该状态对象维护。

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
// -------------------------

PACKET_CALLBACK g_PacketCallback = nullptr;

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
    
    // 读取 isocode
    int isocode = ReadInt32LE(body, offset);
    
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
        int packid = ReadInt32LE(body, offset);
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

// ============ 磐石御天火功能实现 (Act793) ============
    
    // 使用新的状态管理器
    #define ACT793_STATE ActivityStateManager::Instance().GetAct793State()
    
    // 兼容旧代码的静态变量映射到状态管理器
    static bool g_act793SweepSuccess = false;  // 仅用于sweep_success标志
    
    BOOL SendAct793Packet(const std::string& operation, const std::vector<int32_t>& bodyValues) {
        return SendActivityPacket(Act793::ACTIVITY_ID, operation, bodyValues);
    }
    
    BOOL SendAct793GameInfoPacket() {
        ACT793_STATE.waitingResponse = true;
        return SendAct793Packet("open_ui", {});
    }
    
    BOOL SendAct793StartGamePacket() {
        ACT793_STATE.waitingResponse = true;
        return SendAct793Packet("start_game", {});
    }
    
    BOOL SendAct793GameHitPacket(int hitCount) {
        // 封包格式: [game_hit, hitCount + 1000]
        // 注意：game_hit 服务器不返回响应，所以直接使用SendPacket不等待响应
        std::vector<BYTE> packet = BuildActivityPacket(Opcode::ACTIVITY_QINGYANG_NEW_SEND, Act793::ACTIVITY_ID, "game_hit", {hitCount + 1000});
        return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
    }
    
    BOOL SendAct793EndGamePacket(int medalCount) {
        // 计算校验码: checkCode & (userId % checkCode)
        int clientCheckCode = 0;
        if (ACT793_STATE.checkCode != 0) {
            clientCheckCode = ACT793_STATE.checkCode & (static_cast<int>(g_userId) % ACT793_STATE.checkCode);
        }
        return SendAct793Packet("end_game", {clientCheckCode, medalCount});
    }
    
    BOOL SendAct793SweepInfoPacket() {
        ACT793_STATE.waitingResponse = true;
        return SendAct793Packet("sweep_info", {});
    }
    
    BOOL SendAct793SweepPacket() {
        ACT793_STATE.waitingResponse = true;
        return SendAct793Packet("sweep", {});
    }
    
    DWORD WINAPI Act793ThreadProc(LPVOID lpParam) {
        int targetMedals = ACT793_STATE.targetMedals;
        
        Sleep(300);
        UIBridge::Instance().UpdateHelperText(L"磐石御天火：正在获取游戏信息...");
        
        SendAct793GameInfoPacket();
        for (int i = 0; i < 30 && ACT793_STATE.waitingResponse; i++) Sleep(100);
        
        if (ACT793_STATE.playCount <= 0) {
            UIBridge::Instance().UpdateHelperText(L"磐石御天火：今日次数已用完");
            return 0;
        }
        
        if (ACT793_STATE.restTime > 0) {
            UIBridge::Instance().UpdateHelperText(L"磐石御天火：冷却中，请稍后再试");
            return 0;
        }
        
        // 尝试扫荡
        if (ACT793_STATE.useSweep) {
            UIBridge::Instance().UpdateHelperText(L"磐石御天火：正在获取扫荡信息...");
            
            Sleep(300);
            g_act793SweepSuccess = false;
            SendAct793SweepInfoPacket();
            for (int i = 0; i < 30 && ACT793_STATE.waitingResponse; i++) Sleep(100);
            
            if (!g_act793SweepSuccess) {
                UIBridge::Instance().UpdateHelperText(L"磐石御天火：扫荡条件不满足，开始游戏...");
            } else {
                Sleep(300);
                g_act793SweepSuccess = false;
                SendAct793SweepPacket();
                for (int i = 0; i < 30 && ACT793_STATE.waitingResponse; i++) Sleep(100);
                
                if (g_act793SweepSuccess) {
                    UIBridge::Instance().UpdateHelperText(L"磐石御天火：扫荡完成！");
                    return 0;
                }
            }
        }
        
        // 开始游戏
        UIBridge::Instance().UpdateHelperText(L"磐石御天火：开始游戏...");
        
        Sleep(300);
        SendAct793StartGamePacket();
        for (int i = 0; i < 30 && ACT793_STATE.waitingResponse; i++) Sleep(100);
        
        if (ACT793_STATE.checkCode == 0) {
            UIBridge::Instance().UpdateHelperText(L"磐石御天火：获取校验码失败");
            return 0;
        }
        
        // 模拟游戏过程：发送多次 game_hit 封包收集勋章
        for (int i = 0; i < targetMedals; i++) {
            if (i % 10 == 0 || i == targetMedals - 1) {
                wchar_t msg[128];
                swprintf_s(msg, L"磐石御天火：游戏进行中... 进度 %d/%d", i + 1, targetMedals);
                UIBridge::Instance().UpdateHelperText(msg);
            }
            SendAct793GameHitPacket(i + 1);
            Sleep(300);  // 固定300ms延迟
        }
        
        Sleep(300);
        SendAct793EndGamePacket(targetMedals);
        UIBridge::Instance().UpdateHelperText(L"磐石御天火：游戏完成！");
        return 0;
    }
    
    BOOL StartOneKeyAct793Packet(bool useSweep, int targetMedals) {
        ACT793_STATE.useSweep = useSweep;
        ACT793_STATE.targetMedals = targetMedals;
        HANDLE hThread = CreateThread(nullptr, 0, Act793ThreadProc, nullptr, 0, nullptr);
        if (hThread) { CloseHandle(hThread); return TRUE; }
        return FALSE;
    }
    
    void ProcessAct793Response(const GamePacket& packet) {
        size_t offset = 0;
        std::string operation;
        if (!ReadLengthPrefixedString(packet.body, offset, operation)) return;
        const BYTE* body = packet.body.data();
        
        ACT793_STATE.waitingResponse = false;
        
        if (operation == "open_ui") {
            // open_ui 响应格式:
            // playTime(4B) + restTime(4B) + skip(4B) + redBerryCount(4B) + skip(4B) + 
            // isPop(4B) + evoFlag(4B) + isMove(4B)
            if (offset + 32 <= packet.body.size()) {
                ACT793_STATE.playCount = ReadInt32LE(body, offset);
                ACT793_STATE.restTime = ReadInt32LE(body, offset);
                offset += 4;  // skip
                ACT793_STATE.medalCount = ReadInt32LE(body, offset);
                offset += 4;  // skip
                offset += 4;  // isPop (不使用)
                offset += 4;  // evoFlag (不使用)
                offset += 4;  // isMove (不使用)
                
                // 通知UI更新
                wchar_t msg[256];
                swprintf_s(msg, L"磐石御天火：次数=%d 冷却=%d秒 勋章=%d", 
                          ACT793_STATE.playCount.load(), ACT793_STATE.restTime.load(), ACT793_STATE.medalCount.load());
                UIBridge::Instance().UpdateHelperText(msg);
            }
        }
        else if (operation == "start_game") {
            if (offset + 4 <= packet.body.size()) {
                int result = ReadInt32LE(body, offset);
                if (result == 0 && offset + 8 <= packet.body.size()) {
                    offset += 4;  // skip
                    ACT793_STATE.checkCode = ReadInt32LE(body, offset);
                }
            }
        }
        else if (operation == "sweep_info") {
            if (offset + 4 <= packet.body.size()) {
                int result = ReadInt32LE(body, offset);
                g_act793SweepSuccess = (result == 0);
            }
        }
        else if (operation == "sweep") {
            if (offset + 4 <= packet.body.size()) {
                int result = ReadInt32LE(body, offset);
                g_act793SweepSuccess = (result == 0);
                if (result == 0 && offset + 8 <= packet.body.size()) {
                    ACT793_STATE.playCount = ReadInt32LE(body, offset);
                }
            }
        }
        else if (operation == "end_game") {
            if (offset + 4 <= packet.body.size()) {
                int result = ReadInt32LE(body, offset);
                if (result == 0 && offset + 8 <= packet.body.size()) {
                    ACT793_STATE.playCount = ReadInt32LE(body, offset);
                }
            }
        }
    }
    
    // ============ 五行镜破封印功能实现 (Act791) ============
    
    #define ACT791_STATE ActivityStateManager::Instance().GetAct791State()
    
    static bool g_act791SweepSuccess = false;
    
    BOOL SendAct791Packet(const std::string& operation, const std::vector<int32_t>& bodyValues) {
        return SendActivityPacket(Act791::ACTIVITY_ID, operation, bodyValues);
    }
    
    BOOL SendAct791GameInfoPacket() {
        ACT791_STATE.waitingResponse = true;
        return SendAct791Packet("game_info", {});
    }
    
    BOOL SendAct791StartGamePacket() {
        ACT791_STATE.waitingResponse = true;
        return SendAct791Packet("start_game", {});
    }
    
    BOOL SendAct791EndGamePacket(int score) {
        // 步骤1：先发送额外封包 (根据AS3代码: sendMessage(1184812, 3, [3, 4039001, 0]))
        std::vector<BYTE> extraPacket = PacketBuilder().SetOpcode(Act791::EXTRA_OPCODE).SetParams(Act791::EXTRA_PARAMS)
            .WriteInt32(3).WriteInt32(4039001).WriteInt32(0).Build();
        SendPacket(0, extraPacket.data(), static_cast<DWORD>(extraPacket.size()));
        Sleep(100);
        
        // 步骤2：计算校验码 (根据AS3代码: userId % 1000 + serverCheckCode + score)
        int clientCheckCode = static_cast<int>(g_userId) % 1000 + ACT791_STATE.checkCode + score;
        
        return SendAct791Packet("end_game", {clientCheckCode, score});
    }
    
    BOOL SendAct791SweepInfoPacket() {
        ACT791_STATE.waitingResponse = true;
        return SendAct791Packet("sweep_info", {});
    }
    
    BOOL SendAct791SweepPacket() {
        ACT791_STATE.waitingResponse = true;
        return SendAct791Packet("sweep", {});
    }
    
    DWORD WINAPI Act791ThreadProc(LPVOID lpParam) {
        int targetScore = ACT791_STATE.targetScore;
        
        Sleep(300);
        UIBridge::Instance().UpdateHelperText(L"五行镜破封印：正在获取游戏信息...");
        
        SendAct791GameInfoPacket();
        for (int i = 0; i < 30 && ACT791_STATE.waitingResponse; i++) Sleep(100);
        
        if (ACT791_STATE.playCount <= 0) {
            UIBridge::Instance().UpdateHelperText(L"五行镜破封印：今日次数已用完");
            return 0;
        }
        
        if (ACT791_STATE.restTime > 0) {
            UIBridge::Instance().UpdateHelperText(L"五行镜破封印：冷却中，请稍后再试");
            return 0;
        }
        
        // 尝试扫荡
        if (ACT791_STATE.useSweep) {
            UIBridge::Instance().UpdateHelperText(L"五行镜破封印：正在获取扫荡信息...");
            
            Sleep(300);
            g_act791SweepSuccess = false;
            SendAct791SweepInfoPacket();
            for (int i = 0; i < 30 && ACT791_STATE.waitingResponse; i++) Sleep(100);
            
            if (!g_act791SweepSuccess) {
                UIBridge::Instance().UpdateHelperText(L"五行镜破封印：扫荡条件不满足，开始游戏...");
            } else {
                Sleep(300);
                g_act791SweepSuccess = false;
                SendAct791SweepPacket();
                for (int i = 0; i < 30 && ACT791_STATE.waitingResponse; i++) Sleep(100);
                
                if (g_act791SweepSuccess) {
                    UIBridge::Instance().UpdateHelperText(L"五行镜破封印：扫荡完成！");
                    return 0;
                }
            }
        }
        
        // 开始游戏
        UIBridge::Instance().UpdateHelperText(L"五行镜破封印：开始游戏...");
        
        Sleep(300);
        SendAct791StartGamePacket();
        for (int i = 0; i < 30 && ACT791_STATE.waitingResponse; i++) Sleep(100);
        
        if (ACT791_STATE.checkCode == 0) {
            UIBridge::Instance().UpdateHelperText(L"五行镜破封印：获取校验码失败");
            return 0;
        }
        
        // 模拟游戏过程（60秒点击收集游戏）
        // 简化处理：直接结束游戏，发送目标分数
        Sleep(500);
        UIBridge::Instance().UpdateHelperText(L"五行镜破封印：游戏进行中...");
        
        // 结束游戏
        Sleep(300);
        SendAct791EndGamePacket(targetScore);
        UIBridge::Instance().UpdateHelperText(L"五行镜破封印：游戏完成！");
        return 0;
    }
    
    BOOL StartOneKeyAct791Packet(bool useSweep, int targetScore) {
        ACT791_STATE.useSweep = useSweep;
        ACT791_STATE.targetScore = targetScore;
        HANDLE hThread = CreateThread(nullptr, 0, Act791ThreadProc, nullptr, 0, nullptr);
        if (hThread) { CloseHandle(hThread); return TRUE; }
        return FALSE;
    }
    
    void ProcessAct791Response(const GamePacket& packet) {
        size_t offset = 0;
        std::string operation;
        if (!ReadLengthPrefixedString(packet.body, offset, operation)) return;
        const BYTE* body = packet.body.data();
        
        ACT791_STATE.waitingResponse = false;
        
        if (operation == "game_info") {
            // game_info 响应格式 (根据AS3代码):
            // result(4B) + restPlayCount(4B) + restCdTime(4B) + pop_win(4B) + 
            // medalNum(4B) + best_score(4B) + last_game_score(4B) + superEvolutionFlag(4B)
            // 总共 32 字节，字符串后需要 32 字节
            if (offset + 32 <= packet.body.size()) {
                int result = ReadInt32LE(body, offset);
                ACT791_STATE.playCount = ReadInt32LE(body, offset);
                ACT791_STATE.restTime = ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);  // pop_win (跳过)
                ACT791_STATE.medalNum = ReadInt32LE(body, offset);
                ACT791_STATE.bestScore = ReadInt32LE(body, offset);
                ACT791_STATE.lastScore = ReadInt32LE(body, offset);
                ACT791_STATE.superEvolutionFlag = ReadInt32LE(body, offset);
                
                wchar_t msg[256];
                swprintf_s(msg, L"五行镜破封印：结果=%d 次数=%d 冷却=%d秒 勋章=%d 最高分=%d", 
                          result, ACT791_STATE.playCount.load(), ACT791_STATE.restTime.load(), 
                          ACT791_STATE.medalNum.load(), ACT791_STATE.bestScore.load());
                UIBridge::Instance().UpdateHelperText(msg);
            }
        }
        else if (operation == "start_game") {
            // start_game 响应格式: result(4B) + unknown(4B) + checkCode(4B)
            if (offset + 4 <= packet.body.size()) {
                int result = ReadInt32LE(body, offset);
                if (result == 0 && offset + 8 <= packet.body.size()) {
                    offset += 4;  // unknown
                    ACT791_STATE.checkCode = ReadInt32LE(body, offset);
                }
            }
        }
        else if (operation == "sweep_info") {
            // sweep_info 响应格式: medal(4B) + exp(4B) + coin(4B)
            if (offset + 4 <= packet.body.size()) {
                int medal = ReadInt32LE(body, offset);
                g_act791SweepSuccess = (medal > 0);  // 有勋章表示可以扫荡
            }
        }
        else if (operation == "sweep") {
            // sweep 响应格式: result(4B) + unknown(4B) + unknown(4B) + medal(4B) + exp(4B) + coin(4B)
            if (offset + 4 <= packet.body.size()) {
                int result = ReadInt32LE(body, offset);
                g_act791SweepSuccess = (result == 0);
                if (result == 0 && offset + 8 <= packet.body.size()) {
                    ACT791_STATE.playCount = ReadInt32LE(body, offset);
                }
            }
        }
        else if (operation == "end_game") {
            // end_game 响应格式: result(4B) + unknown(4B) + unknown(4B) + medal(4B) + exp(4B) + coin(4B)
            if (offset + 4 <= packet.body.size()) {
                int result = ReadInt32LE(body, offset);
                if (result == 0 && offset + 8 <= packet.body.size()) {
                    ACT791_STATE.playCount = ReadInt32LE(body, offset);
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
        if (!ReadLengthPrefixedString(packet.body, offset, operation)) {
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

    // ============ 清明赏河景功能实现 (Act803) ============

    BOOL SendAct803Packet(const std::string& operation, const std::vector<int32_t>& bodyValues) {
        return SendActivityPacket(Act803::ACTIVITY_ID, operation, bodyValues);
    }

    BOOL SendAct803GameInfoPacket() {
        g_act803WaitingResponse = true;
        return SendAct803Packet("open_ui", {});
    }

    BOOL SendAct803StartGamePacket(int startFlag) {
        g_act803WaitingResponse = true;
        return SendAct803Packet("start_game", {startFlag});
    }

    BOOL SendAct803EndGamePacket(int score, bool isWin) {
        const int checkCode = g_act803CheckCode.load();
        const uint32_t userId = g_userId.load();
        if (checkCode <= 0) {
            return FALSE;
        }

        const int clientCheckCode = checkCode & static_cast<int>(userId % static_cast<uint32_t>(checkCode));
        std::vector<int32_t> bodyValues;
        bodyValues.reserve(18);
        bodyValues.push_back(static_cast<int32_t>(clientCheckCode));
        bodyValues.push_back(score);
        bodyValues.push_back(isWin ? 1 : 0);
        const std::vector<int32_t> awards = BuildAct803AwardValues();
        bodyValues.insert(bodyValues.end(), awards.begin(), awards.end());

        g_act803WaitingResponse = true;
        return SendAct803Packet("end_game", bodyValues);
    }

    BOOL SendAct803SweepInfoPacket() {
        g_act803WaitingResponse = true;
        return SendAct803Packet("sweep_info", {});
    }

    BOOL SendAct803SweepPacket() {
        g_act803WaitingResponse = true;
        return SendAct803Packet("sweep", {});
    }

    static void ParseAct803AwardList(const std::vector<BYTE>& body, size_t& offset) {
        if (offset + 16 > body.size()) {
            return;
        }

        ReadInt32LE(body.data(), offset);
        ReadInt32LE(body.data(), offset);
        ReadInt32LE(body.data(), offset);
        const int len = ReadInt32LE(body.data(), offset);
        for (int i = 0; i < len && offset + 8 <= body.size(); ++i) {
            ReadInt32LE(body.data(), offset);
            ReadInt32LE(body.data(), offset);
        }
    }

    DWORD WINAPI Act803ThreadProc(LPVOID lpParam) {
        const int targetScore = lpParam ? *static_cast<int*>(lpParam) : Act803::MAX_NUM;
        if (lpParam) {
            delete static_cast<int*>(lpParam);
        }

        g_act803CheckCode = 0;
        g_act803LastResult = -1;
        g_act803PlayCount = 0;
        g_act803RestTime = 0;
        g_act803RedBerryCount = 0;
        g_act803IsPop = 0;
        g_act803IsMoveControl = 0;
        g_act803SweepAvailable = false;
        g_act803HasAwardArr.clear();

        Sleep(300);
        UIBridge::Instance().UpdateHelperText(L"清明赏河景：正在获取活动信息...");

        SendAct803GameInfoPacket();
        for (int i = 0; i < 30 && g_act803WaitingResponse.load(); ++i) {
            Sleep(100);
        }

        if (g_act803PlayCount.load() <= 0) {
            UIBridge::Instance().UpdateHelperText(L"清明赏河景：今日次数已用完");
            return 0;
        }

        if (g_act803RestTime.load() > 0) {
            UIBridge::Instance().UpdateHelperText(L"清明赏河景：冷却中，请稍后再试");
            return 0;
        }

        if (g_act803UseSweep.load()) {
            UIBridge::Instance().UpdateHelperText(L"清明赏河景：正在获取扫荡信息...");

            SendAct803SweepInfoPacket();
            for (int i = 0; i < 30 && g_act803WaitingResponse.load(); ++i) {
                Sleep(100);
            }

            if (g_act803SweepAvailable.load()) {
                Sleep(300);
                UIBridge::Instance().UpdateHelperText(L"清明赏河景：执行扫荡...");
                SendAct803SweepPacket();
                for (int i = 0; i < 30 && g_act803WaitingResponse.load(); ++i) {
                    Sleep(100);
                }
                if (g_act803LastResult.load() == 3) {
                    UIBridge::Instance().UpdateHelperText(L"清明赏河景：扫荡失败");
                }
                return 0;
            }

            UIBridge::Instance().UpdateHelperText(L"清明赏河景：当前不可扫荡，改为直接结算");
        }

        Sleep(300);
        UIBridge::Instance().UpdateHelperText(L"清明赏河景：开始游戏...");
        SendAct803StartGamePacket(1);
        for (int i = 0; i < 30 && g_act803WaitingResponse.load(); ++i) {
            Sleep(100);
        }

        if (g_act803CheckCode.load() == 0) {
            UIBridge::Instance().UpdateHelperText(L"清明赏河景：获取校验码失败");
            return 0;
        }

        Sleep(500);
        UIBridge::Instance().UpdateHelperText(L"清明赏河景：跳过客户端渲染，直接提交结算...");
        SendAct803EndGamePacket(targetScore);
        for (int i = 0; i < 30 && g_act803WaitingResponse.load(); ++i) {
            Sleep(100);
        }

        if (g_act803LastResult.load() == 3) {
            UIBridge::Instance().UpdateHelperText(L"清明赏河景：结算失败");
        }

        return 0;
    }

    BOOL StartOneKeyAct803Packet(bool useSweep, int targetScore) {
        g_act803UseSweep = useSweep;
        int* pTargetScore = new int(targetScore);
        HANDLE hThread = CreateThread(nullptr, 0, Act803ThreadProc, pTargetScore, 0, nullptr);
        if (hThread) {
            CloseHandle(hThread);
            return TRUE;
        }
        delete pTargetScore;
        return FALSE;
    }

    void ProcessAct803Response(const GamePacket& packet) {
        size_t offset = 0;
        std::string operation;
        if (!ReadLengthPrefixedString(packet.body, offset, operation)) {
            return;
        }

        const BYTE* body = packet.body.data();
        g_act803WaitingResponse = false;

        if (operation == "open_ui") {
            if (offset + 44 <= packet.body.size()) {
                g_act803PlayCount = ReadInt32LE(body, offset);
                g_act803RestTime = ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                g_act803RedBerryCount = ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                g_act803IsPop = ReadInt32LE(body, offset);
                g_act803IsMoveControl = ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);

                wchar_t msg[256];
                swprintf_s(
                    msg,
                    L"清明赏河景：次数=%d 冷却=%d秒 红莓果=%d",
                    g_act803PlayCount.load(),
                    g_act803RestTime.load(),
                    g_act803RedBerryCount.load());
                UIBridge::Instance().UpdateHelperText(msg);
            }
        } else if (operation == "start_game") {
            if (offset + 4 <= packet.body.size()) {
                const int result = ReadInt32LE(body, offset);
                g_act803LastResult = result;
                if (result == 0 && offset + 8 <= packet.body.size()) {
                    ReadInt32LE(body, offset);
                    g_act803CheckCode = ReadInt32LE(body, offset);
                    g_act803HasAwardArr.clear();
                    while (offset + 4 <= packet.body.size()) {
                        g_act803HasAwardArr.push_back(ReadInt32LE(body, offset));
                    }
                } else if (result == 12) {
                    UIBridge::Instance().UpdateHelperText(L"清明赏河景：今日次数已用完");
                } else if (result == 11) {
                    UIBridge::Instance().UpdateHelperText(L"清明赏河景：冷却中，请稍后再试");
                } else if (result == 10) {
                    UIBridge::Instance().UpdateHelperText(L"清明赏河景：当前已经在游戏中");
                }
            }
        } else if (operation == "sweep_info") {
            if (offset + 8 <= packet.body.size()) {
                const int result = ReadInt32LE(body, offset);
                g_act803SweepAvailable = (result == 0 || result == 4);
                ReadInt32LE(body, offset);
                if (g_act803SweepAvailable.load()) {
                    ParseAct803AwardList(packet.body, offset);
                    UIBridge::Instance().UpdateHelperText(L"清明赏河景：扫荡预览已获取");
                } else {
                    UIBridge::Instance().UpdateHelperText(L"清明赏河景：当前不可扫荡");
                }
            }
        } else if (operation == "end_game" || operation == "sweep") {
            if (offset + 12 <= packet.body.size()) {
                const int result = ReadInt32LE(body, offset);
                g_act803LastResult = result;
                ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                if (result != 3 && offset + 4 <= packet.body.size()) {
                    ReadInt32LE(body, offset);
                    ParseAct803AwardList(packet.body, offset);
                    UIBridge::Instance().UpdateHelperText(operation == "sweep" ? L"清明赏河景：扫荡完成" : L"清明赏河景：结算完成");
                } else if (result == 3) {
                    UIBridge::Instance().UpdateHelperText(operation == "sweep" ? L"清明赏河景：扫荡失败" : L"清明赏河景：结算失败");
                }
            }
        }
    }

    // ============ 采蘑菇的好伙伴功能实现 (Act624) ============

    BOOL SendAct624Packet(const std::string& operation, const std::vector<int32_t>& bodyValues) {
        std::vector<BYTE> packet = BuildActivityPacket(Opcode::ACTIVITY_QINGYANG_NEW_SEND, Act624::ACTIVITY_ID, operation, bodyValues);
        return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
    }

    BOOL SendAct624PacketNoWait(const std::string& operation, const std::vector<int32_t>& bodyValues = {}) {
        std::vector<BYTE> packet = BuildActivityPacket(Opcode::ACTIVITY_QINGYANG_NEW_SEND, Act624::ACTIVITY_ID, operation, bodyValues);
        return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
    }

    BOOL SendAct624GameInfoPacket() {
        g_act624WaitingResponse = true;
        return SendAct624Packet("game_info", {});
    }

    BOOL SendAct624StartGamePacket(int promptFlag) {
        g_act624WaitingResponse = true;
        return SendAct624Packet("start_game", {promptFlag});
    }

    BOOL SendAct624EndRoundPacket(int round, int gameTime, int mushroomNum) {
        const int checkCode = g_act624CheckCode.load();
        const uint32_t userId = g_userId.load();
        if (checkCode <= 0) {
            return FALSE;
        }

        const int clientCheckCode = checkCode & static_cast<int>(userId % static_cast<uint32_t>(checkCode));
        g_act624WaitingResponse = true;
        return SendAct624Packet("round", {clientCheckCode, round, gameTime, mushroomNum, 0});
    }

    BOOL SendAct624NextRoundPacket() {
        return SendAct624PacketNoWait("revive", {2});
    }

    BOOL SendAct624SweepInfoPacket() {
        g_act624WaitingResponse = true;
        return SendAct624Packet("sweep_info", {});
    }

    BOOL SendAct624SweepPacket() {
        g_act624WaitingResponse = true;
        return SendAct624Packet("sweep", {});
    }

    DWORD WINAPI Act624ThreadProc(LPVOID lpParam) {
        (void)lpParam;

        g_act624CheckCode = 0;
        g_act624LastResult = -1;
        g_act624PlayCount = 0;
        g_act624RestTime = 0;
        g_act624CanGetFlag = 0;
        g_act624GotFlag = 0;
        g_act624TotalBadgeNum = 0;
        g_act624FinishedNum = 0;
        g_act624SweepAvailable = false;

        Sleep(300);
        UIBridge::Instance().UpdateHelperText(L"采蘑菇的好伙伴：正在获取活动信息...");

        SendAct624GameInfoPacket();
        for (int i = 0; i < 30 && g_act624WaitingResponse.load(); ++i) {
            Sleep(100);
        }

        if (g_act624PlayCount.load() <= 0) {
            UIBridge::Instance().UpdateHelperText(L"采蘑菇的好伙伴：今日次数已用完");
            return 0;
        }

        if (g_act624RestTime.load() > 0) {
            UIBridge::Instance().UpdateHelperText(L"采蘑菇的好伙伴：冷却中，请稍后再试");
            return 0;
        }

        if (g_act624UseSweep.load()) {
            UIBridge::Instance().UpdateHelperText(L"采蘑菇的好伙伴：正在获取扫荡信息...");
            SendAct624SweepInfoPacket();
            for (int i = 0; i < 30 && g_act624WaitingResponse.load(); ++i) {
                Sleep(100);
            }

            if (g_act624SweepAvailable.load()) {
                Sleep(300);
                UIBridge::Instance().UpdateHelperText(L"采蘑菇的好伙伴：执行扫荡...");
                SendAct624SweepPacket();
                for (int i = 0; i < 30 && g_act624WaitingResponse.load(); ++i) {
                    Sleep(100);
                }
                if (g_act624LastResult.load() == 0) {
                    UIBridge::Instance().UpdateHelperText(L"采蘑菇的好伙伴：扫荡完成");
                } else {
                    UIBridge::Instance().UpdateHelperText(L"采蘑菇的好伙伴：扫荡失败");
                }
                return 0;
            }

            UIBridge::Instance().UpdateHelperText(L"采蘑菇的好伙伴：当前不可扫荡，改为三轮游戏");
        }

        Sleep(300);
        UIBridge::Instance().UpdateHelperText(L"采蘑菇的好伙伴：开始游戏...");
        SendAct624StartGamePacket(1);
        for (int i = 0; i < 30 && g_act624WaitingResponse.load(); ++i) {
            Sleep(100);
        }

        if (g_act624CheckCode.load() == 0) {
            UIBridge::Instance().UpdateHelperText(L"采蘑菇的好伙伴：获取校验码失败");
            return 0;
        }

        static const int kTargetNums[3] = {5, 10, 15};
        static const int kTargetTimes[3] = {0, 0, 0};
        for (int round = 1; round <= 3; ++round) {
            Sleep(300);
            wchar_t msg[128];
            swprintf_s(msg, L"采蘑菇的好伙伴：第%d轮结算中...", round);
            UIBridge::Instance().UpdateHelperText(msg);
            SendAct624EndRoundPacket(round, kTargetTimes[round - 1], kTargetNums[round - 1]);
            for (int i = 0; i < 30 && g_act624WaitingResponse.load(); ++i) {
                Sleep(100);
            }

            if (round < 3) {
                // AS3 里的“下一轮”只是本地 UI 切换，不会额外向服务端发送 revive。
                Sleep(600);
            }
        }

        Sleep(300);
        g_act624WaitingResponse = true;
        SendAct624Packet("revive", {2});
        for (int i = 0; i < 30 && g_act624WaitingResponse.load(); ++i) {
            Sleep(100);
        }

        Sleep(300);
        SendAct624GameInfoPacket();
        for (int i = 0; i < 30 && g_act624WaitingResponse.load(); ++i) {
            Sleep(100);
        }

        UIBridge::Instance().UpdateHelperText(L"采蘑菇的好伙伴：三轮结算完成");
        return 0;
    }

    BOOL StartOneKeyAct624Packet(bool useSweep) {
        g_act624UseSweep = useSweep;
        HANDLE hThread = CreateThread(nullptr, 0, Act624ThreadProc, nullptr, 0, nullptr);
        if (hThread) {
            CloseHandle(hThread);
            return TRUE;
        }
        return FALSE;
    }

    void ProcessAct624Response(const GamePacket& packet) {
        size_t offset = 0;
        std::string operation;
        if (!ReadLengthPrefixedString(packet.body, offset, operation)) {
            return;
        }

        const BYTE* body = packet.body.data();
        g_act624WaitingResponse = false;

        if (operation == "game_info") {
            if (offset + 4 <= packet.body.size()) {
                const int result = ReadInt32LE(body, offset);
                g_act624LastResult = result;
                if (result == 0 && offset + 52 <= packet.body.size()) {
                    g_act624PlayCount = ReadInt32LE(body, offset);
                    g_act624RestTime = ReadInt32LE(body, offset);
                    g_act624CanGetFlag = ReadInt32LE(body, offset);
                    g_act624GotFlag = ReadInt32LE(body, offset);
                    ReadInt32LE(body, offset);
                    ReadInt32LE(body, offset);
                    ReadInt32LE(body, offset);
                    ReadInt32LE(body, offset);
                    g_act624TotalBadgeNum = ReadInt32LE(body, offset);
                    g_act624FinishedNum = ReadInt32LE(body, offset);
                    ReadInt32LE(body, offset);
                    ReadInt32LE(body, offset);
                    ReadInt32LE(body, offset);

                    wchar_t msg[256];
                    swprintf_s(
                        msg,
                        L"采蘑菇的好伙伴：次数=%d 冷却=%d秒 勋章=%d",
                        g_act624PlayCount.load(),
                        g_act624RestTime.load(),
                        g_act624TotalBadgeNum.load());
                    UIBridge::Instance().UpdateHelperText(msg);
                } else if (result == 12) {
                    UIBridge::Instance().UpdateHelperText(L"采蘑菇的好伙伴：今日次数已用完");
                } else if (result == 11) {
                    UIBridge::Instance().UpdateHelperText(L"采蘑菇的好伙伴：冷却中，请稍后再试");
                } else if (result == 10) {
                    UIBridge::Instance().UpdateHelperText(L"采蘑菇的好伙伴：当前已经在游戏中");
                } else {
                    UIBridge::Instance().UpdateHelperText(L"采蘑菇的好伙伴：获取活动信息失败");
                }
            }
        } else if (operation == "start_game") {
            if (offset + 4 <= packet.body.size()) {
                const int result = ReadInt32LE(body, offset);
                g_act624LastResult = result;
                if (result == 0 && offset + 8 <= packet.body.size()) {
                    ReadInt32LE(body, offset);
                    g_act624CheckCode = ReadInt32LE(body, offset);
                } else if (result == 12) {
                    UIBridge::Instance().UpdateHelperText(L"采蘑菇的好伙伴：今日次数已用完");
                } else if (result == 11) {
                    UIBridge::Instance().UpdateHelperText(L"采蘑菇的好伙伴：冷却中，请稍后再试");
                } else if (result == 10) {
                    UIBridge::Instance().UpdateHelperText(L"采蘑菇的好伙伴：当前已经在游戏中");
                }
            }
        } else if (operation == "round") {
            if (offset + 12 <= packet.body.size()) {
                g_act624CheckCode = ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                UIBridge::Instance().UpdateHelperText(L"采蘑菇的好伙伴：结算完成");
            }
        } else if (operation == "revive") {
            g_act624LastResult = 0;
        } else if (operation == "sweep_info") {
            if (offset + 4 <= packet.body.size()) {
                const int result = ReadInt32LE(body, offset);
                g_act624SweepAvailable = (result == 0);
                if (g_act624SweepAvailable.load()) {
                    std::string jsonStr;
                    if (ReadLengthPrefixedString(packet.body, offset, jsonStr)) {
                        UIBridge::Instance().UpdateHelperText(L"采蘑菇的好伙伴：扫荡预览已获取");
                    }
                } else {
                    UIBridge::Instance().UpdateHelperText(L"采蘑菇的好伙伴：当前不可扫荡");
                }
            }
        } else if (operation == "sweep") {
            if (offset + 4 <= packet.body.size()) {
                const int result = ReadInt32LE(body, offset);
                g_act624LastResult = result;
                if (offset + 4 <= packet.body.size()) {
                    g_act624PlayCount = ReadInt32LE(body, offset);
                }
                if (result == 0) {
                    std::string jsonStr;
                    if (ReadLengthPrefixedString(packet.body, offset, jsonStr)) {
                        UIBridge::Instance().UpdateHelperText(L"采蘑菇的好伙伴：扫荡完成");
                    }
                } else {
                    UIBridge::Instance().UpdateHelperText(L"采蘑菇的好伙伴：扫荡失败");
                }
            }
        }
    }

    // ============ 海底激战功能实现 (SeaBattle / Act653) ============

    #define SEA_BATTLE_STATE ActivityStateManager::Instance().GetSeaBattleState()

    static bool g_seaBattleSweepSuccess = false;

    BOOL SendSeaBattlePacket(const std::string& operation, const std::vector<int32_t>& bodyValues) {
        return SendActivityPacket(SeaBattle::ACTIVITY_ID, operation, bodyValues);
    }

    BOOL SendSeaBattleUIInfoPacket() {
        SEA_BATTLE_STATE.waitingResponse = true;
        return SendSeaBattlePacket("ui_info", {});
    }

    BOOL SendSeaBattleStartGamePacket(int promptFlag) {
        SEA_BATTLE_STATE.waitingResponse = true;
        return SendSeaBattlePacket("start_game", {promptFlag});
    }

    BOOL SendSeaBattleEndGamePacket(int score) {
        std::vector<BYTE> extraPacket = PacketBuilder()
            .SetOpcode(SeaBattle::EXTRA_OPCODE)
            .SetParams(SeaBattle::EXTRA_PARAMS)
            .WriteInt32(3)
            .WriteInt32(SeaBattle::EXTRA_TASK_ID)
            .WriteInt32(0)
            .Build();
        SendPacket(0, extraPacket.data(), static_cast<DWORD>(extraPacket.size()));
        Sleep(100);

        const int passFlag = score >= SeaBattle::PASS_SCORE ? 0 : 1;
        const int clientCheckCode = SEA_BATTLE_STATE.checkCode.load()
            + static_cast<int>(g_userId.load() % 100)
            + passFlag
            + score;

        SEA_BATTLE_STATE.waitingResponse = true;
        return SendSeaBattlePacket("end_game", {passFlag, clientCheckCode, score});
    }

    BOOL SendSeaBattleSweepInfoPacket() {
        SEA_BATTLE_STATE.waitingResponse = true;
        return SendSeaBattlePacket("sweep_info", {});
    }

    BOOL SendSeaBattleSweepPacket() {
        SEA_BATTLE_STATE.waitingResponse = true;
        return SendSeaBattlePacket("sweep", {});
    }

    DWORD WINAPI SeaBattleThreadProc(LPVOID lpParam) {
        (void)lpParam;

        SEA_BATTLE_STATE.checkCode = 0;
        SEA_BATTLE_STATE.lastResult = -1;
        SEA_BATTLE_STATE.lastServerScore = 0;
        SEA_BATTLE_STATE.lastScoreMax = 0;
        SEA_BATTLE_STATE.sweepSuccess = false;
        g_seaBattleSweepSuccess = false;

        Sleep(300);
        UIBridge::Instance().UpdateHelperText(L"海底激战：正在获取活动信息...");

        SendSeaBattleUIInfoPacket();
        for (int i = 0; i < 30 && SEA_BATTLE_STATE.waitingResponse; ++i) {
            Sleep(100);
        }

        if (SEA_BATTLE_STATE.playCount <= 0) {
            UIBridge::Instance().UpdateHelperText(L"海底激战：今日次数已用完");
            return 0;
        }

        if (SEA_BATTLE_STATE.restTime > 0) {
            UIBridge::Instance().UpdateHelperText(L"海底激战：冷却中，请稍后再试");
            return 0;
        }

        if (SEA_BATTLE_STATE.useSweep) {
            UIBridge::Instance().UpdateHelperText(L"海底激战：正在获取扫荡信息...");

            Sleep(300);
            g_seaBattleSweepSuccess = false;
            SEA_BATTLE_STATE.sweepSuccess = false;
            SendSeaBattleSweepInfoPacket();
            for (int i = 0; i < 30 && SEA_BATTLE_STATE.waitingResponse; ++i) {
                Sleep(100);
            }

            if (!g_seaBattleSweepSuccess) {
                UIBridge::Instance().UpdateHelperText(L"海底激战：当前不可扫荡");
                return 0;
            }

            Sleep(300);
            g_seaBattleSweepSuccess = false;
            SEA_BATTLE_STATE.sweepSuccess = false;
            SendSeaBattleSweepPacket();
            for (int i = 0; i < 30 && SEA_BATTLE_STATE.waitingResponse; ++i) {
                Sleep(100);
            }

            if (SEA_BATTLE_STATE.sweepSuccess) {
                UIBridge::Instance().UpdateHelperText(L"海底激战：扫荡完成");
            } else {
                UIBridge::Instance().UpdateHelperText(L"海底激战：扫荡失败");
            }
            return 0;
        }

        const int targetScore = SeaBattle::TARGET_SCORE;

        Sleep(300);
        UIBridge::Instance().UpdateHelperText(L"海底激战：开始游戏...");

        SendSeaBattleStartGamePacket(SEA_BATTLE_STATE.promptFlag.load());
        for (int i = 0; i < 30 && SEA_BATTLE_STATE.waitingResponse; ++i) {
            Sleep(100);
        }

        if (SEA_BATTLE_STATE.checkCode == 0) {
            UIBridge::Instance().UpdateHelperText(L"海底激战：获取校验码失败");
            return 0;
        }

        Sleep(500);
        UIBridge::Instance().UpdateHelperText(L"海底激战：跳过客户端渲染，直接提交结算...");

        SendSeaBattleEndGamePacket(targetScore);
        for (int i = 0; i < 30 && SEA_BATTLE_STATE.waitingResponse; ++i) {
            Sleep(100);
        }

        if (SEA_BATTLE_STATE.lastResult == 0 || SEA_BATTLE_STATE.lastResult == 4) {
            wchar_t msg[256];
            swprintf_s(
                msg,
                L"海底激战：完成，服务器分数=%d，最高分=%d，勋章=%d",
                SEA_BATTLE_STATE.lastServerScore.load(),
                SEA_BATTLE_STATE.lastScoreMax.load(),
                SEA_BATTLE_STATE.medalNum.load());
            UIBridge::Instance().UpdateHelperText(msg);
        } else {
            UIBridge::Instance().UpdateHelperText(L"海底激战：结算失败");
        }

        return 0;
    }

    BOOL StartOneKeySeaBattlePacket(bool useSweep) {
        SEA_BATTLE_STATE.useSweep = useSweep;
        HANDLE hThread = CreateThread(nullptr, 0, SeaBattleThreadProc, nullptr, 0, nullptr);
        if (hThread) {
            CloseHandle(hThread);
            return TRUE;
        }
        return FALSE;
    }

    void ProcessSeaBattleResponse(const GamePacket& packet) {
        size_t offset = 0;
        std::string operation;
        if (!ReadLengthPrefixedString(packet.body, offset, operation)) {
            return;
        }

        const BYTE* body = packet.body.data();
        SEA_BATTLE_STATE.waitingResponse = false;

        if (operation == "ui_info") {
            if (offset + 60 <= packet.body.size()) {
                SEA_BATTLE_STATE.playCount = ReadInt32LE(body, offset);
                SEA_BATTLE_STATE.restTime = ReadInt32LE(body, offset);
                SEA_BATTLE_STATE.promptFlag = ReadInt32LE(body, offset);
                SEA_BATTLE_STATE.strengthenPopWinFlag = ReadInt32LE(body, offset);
                SEA_BATTLE_STATE.medalNum = ReadInt32LE(body, offset);
                SEA_BATTLE_STATE.bestRecord = ReadInt32LE(body, offset);
                SEA_BATTLE_STATE.finishedNum = ReadInt32LE(body, offset);

                ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);
                ReadInt32LE(body, offset);

                SEA_BATTLE_STATE.myScore = ReadInt32LE(body, offset);
                int star = ReadInt32LE(body, offset);
                if (star == 0) {
                    star = 2;
                }
                SEA_BATTLE_STATE.star = star;
                SEA_BATTLE_STATE.sweepAvailable = SEA_BATTLE_STATE.bestRecord.load() > 0;

                wchar_t msg[256];
                swprintf_s(
                    msg,
                    L"海底激战：次数=%d 冷却=%d秒 勋章=%d 星级=%d 最高分=%d",
                    SEA_BATTLE_STATE.playCount.load(),
                    SEA_BATTLE_STATE.restTime.load(),
                    SEA_BATTLE_STATE.medalNum.load(),
                    SEA_BATTLE_STATE.star.load(),
                    SEA_BATTLE_STATE.bestRecord.load());
                UIBridge::Instance().UpdateHelperText(msg);
            }
        } else if (operation == "start_game") {
            if (offset + 8 <= packet.body.size()) {
                const int result = ReadInt32LE(body, offset);
                SEA_BATTLE_STATE.lastResult = result;
                SEA_BATTLE_STATE.playCount = ReadInt32LE(body, offset);
                if (result == 0 && offset + 4 <= packet.body.size()) {
                    SEA_BATTLE_STATE.checkCode = ReadInt32LE(body, offset);
                }
            }
        } else if (operation == "sweep_info") {
            if (offset + 4 <= packet.body.size()) {
                const int result = ReadInt32LE(body, offset);
                SEA_BATTLE_STATE.lastResult = result;
                g_seaBattleSweepSuccess = (result == 0);
                SEA_BATTLE_STATE.sweepSuccess = g_seaBattleSweepSuccess;
                if (result == 0 && offset + 12 <= packet.body.size()) {
                    const int medal = ReadInt32LE(body, offset);
                    const int exp = ReadInt32LE(body, offset);
                    const int coin = ReadInt32LE(body, offset);
                    wchar_t msg[256];
                    swprintf_s(msg, L"海底激战：扫荡预览 勋章=%d 经验=%d 铜钱=%d", medal, exp, coin);
                    UIBridge::Instance().UpdateHelperText(msg);
                }
            }
        } else if (operation == "sweep") {
            if (offset + 4 <= packet.body.size()) {
                const int result = ReadInt32LE(body, offset);
                SEA_BATTLE_STATE.lastResult = result;
                g_seaBattleSweepSuccess = (result == 0);
                SEA_BATTLE_STATE.sweepSuccess = g_seaBattleSweepSuccess;
                if (result == 0 && offset + 24 <= packet.body.size()) {
                    SEA_BATTLE_STATE.playCount = ReadInt32LE(body, offset);
                    SEA_BATTLE_STATE.medalNum = ReadInt32LE(body, offset);
                    const int exp = ReadInt32LE(body, offset);
                    const int coin = ReadInt32LE(body, offset);
                    const bool isVip = ReadInt32LE(body, offset) == 1;
                    const bool isFirst = ReadInt32LE(body, offset) == 0;
                    (void)isVip;
                    (void)isFirst;
                    wchar_t msg[256];
                    swprintf_s(
                        msg,
                        L"海底激战：扫荡完成 勋章=%d 经验=%d 铜钱=%d 剩余次数=%d",
                        SEA_BATTLE_STATE.medalNum.load(),
                        exp,
                        coin,
                        SEA_BATTLE_STATE.playCount.load());
                    UIBridge::Instance().UpdateHelperText(msg);
                }
            }
        } else if (operation == "end_game") {
            if (offset + 4 <= packet.body.size()) {
                const int result = ReadInt32LE(body, offset);
                SEA_BATTLE_STATE.lastResult = result;
                if ((result == 0 || result == 4) && offset + 32 <= packet.body.size()) {
                    SEA_BATTLE_STATE.playCount = ReadInt32LE(body, offset);
                    SEA_BATTLE_STATE.restTime = ReadInt32LE(body, offset);
                    SEA_BATTLE_STATE.medalNum = ReadInt32LE(body, offset);
                    const int exp = ReadInt32LE(body, offset);
                    const int coin = ReadInt32LE(body, offset);
                    SEA_BATTLE_STATE.lastServerScore = ReadInt32LE(body, offset);
                    SEA_BATTLE_STATE.lastScoreMax = ReadInt32LE(body, offset);
                    const bool isVip = ReadInt32LE(body, offset) == 1;
                    const bool isFirst = ReadInt32LE(body, offset) == 0;
                    (void)isVip;
                    (void)isFirst;
                    SEA_BATTLE_STATE.bestRecord = (std::max)(
                        SEA_BATTLE_STATE.bestRecord.load(),
                        SEA_BATTLE_STATE.lastServerScore.load());
                    SEA_BATTLE_STATE.sweepAvailable = SEA_BATTLE_STATE.bestRecord.load() > 0;

                    wchar_t msg[256];
                    swprintf_s(
                        msg,
                        L"海底激战：结算 勋章=%d 经验=%d 铜钱=%d 分数=%d/%d",
                        SEA_BATTLE_STATE.medalNum.load(),
                        exp,
                        coin,
                        SEA_BATTLE_STATE.lastServerScore.load(),
                        SEA_BATTLE_STATE.lastScoreMax.load());
                    UIBridge::Instance().UpdateHelperText(msg);
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
        if (!ReadLengthPrefixedString(packet.body, offset, operation)) return;
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
            uint16_t length = ReadUInt16LE(packet.pData, offset); // Length: offset 2-3
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
                int result = OriginalSend(s, reinterpret_cast<const char*>(modifiedData.data()),
                                          static_cast<int>(dwSize), flags);
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

    // 检测进入世界封包，重置 counter
    if (len >= static_cast<int>(PacketProtocol::HEADER_SIZE)) {
        const BYTE* pData = reinterpret_cast<const BYTE*>(buf);
        if (IsGamePacket(pData, static_cast<DWORD>(len))) {
            uint32_t opcode = ReadOpcode(pData);
            if (opcode == Opcode::ENTER_SCENE_SEND) {
                g_battleCounter = 1;  // 进入世界时重置 counter
            }
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
    
    return OriginalSend(s, buf, len, flags);
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
    
    // 读取卡布名
    if (offset + 2 > body.size()) return;
    
    uint16_t nameLen = static_cast<uint16_t>(body[offset]) | 
                       (static_cast<uint16_t>(body[offset + 1]) << 8);
    offset += 2;
    
    if (offset + nameLen > body.size()) return;
    
    std::string nameUtf8(reinterpret_cast<const char*>(&body[offset]), nameLen);
    std::wstring kabuName = Utf8ToWide(nameUtf8);
    
    // 更新窗口标题
    std::wstring newTitle = L"卡布西游浮影微端 V1.07 - " + 
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
    auto& dispatcher = ResponseDispatcher::Instance();

    for (const auto& gp : gamePackets) {
        // 使用响应分发器分发封包
        dispatcher.Dispatch(gp);

        // 分发完成后再通知等待器，避免等待方在响应处理完成前提前返回
        ResponseWaiter::NotifyResponse(gp.opcode, gp.params);
        Sleep(0);
    }
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
    // 解析封包
    bool hasValidPackets = PacketParser::ParsePackets(pData, dwSize, FALSE, gamePackets);
    
    // ========================================================================
    // 第二步：封包级别过滤（支持黏包）
    // ========================================================================
    // 记录需要过滤的封包索引
    std::vector<size_t> packetsToFilter;
    bool hasBattleStart = false;
    bool shouldFilterBattleStart = false;
    
    for (size_t i = 0; i < gamePackets.size(); i++) {
        const auto& gp = gamePackets[i];
        const auto& packetBody = gp.rawBody.empty() ? gp.body : gp.rawBody;
        
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
            int32_t battleType = 0;
            if (gp.body.size() >= 4) {
                size_t offset = 0;
                battleType = ReadInt32LE(gp.body.data(), offset);
            }

            // 计算 counter
            uint32_t currentCounter = g_battleCounter.load();
            if (currentCounter == 0) {
                currentCounter = 1;
            }

            bool isSpecialBattle = (battleType == 3 || battleType == 5 || battleType == 6 || 
                                   battleType == 14 || battleType == 15);

            if (isSpecialBattle) {
                g_battleCounter = 1;
            } else {
                uint32_t newCounter = currentCounter + (g_userId.load() & 65535) + 9;
                g_battleCounter = newCounter & 65535;
            }

            g_battleStarted = true;

            // 屏蔽战斗功能
            if (g_blockBattle.load()) {
                shouldFilterBattleStart = true;
                packetsToFilter.push_back(i);
            }
        }

        // 万妖盛会自动流程中，监听两类结算弹窗回包：
        // 1317130 = battleNewExpBack 触发的胜负文本窗
        // 1317154 = battleFSPK 触发的 PVP 结算窗
        const bool isBattleSixFlowActive = g_battleSixAuto.IsInBattle() || g_battleSixAuto.IsAutoMatching();
        const bool isBattleSixSettlementPacket =
            gp.opcode == BATTLE_SIX_SETTLEMENT_TEXT_BACK ||
            gp.opcode == BATTLE_SIX_SETTLEMENT_PVP_BACK;
        if (isBattleSixFlowActive && isBattleSixSettlementPacket) {
            if (!g_battleSixPostSettlementEndSent.exchange(true)) {
                HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
                    Sleep(100);
                    SendBattleSixEndPacket();
                    return 0;
                }, nullptr, 0, nullptr);
                if (hThread) {
                    CloseHandle(hThread);
                } else {
                    g_battleSixPostSettlementEndSent = false;
                }
            }
        }
        
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
    if (!packetsToFilter.empty() && hasValidPackets) {
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
        
        // 如果所有封包都被过滤
        if (newBuffer.empty()) {
            memset(buf, 0, result);
            return 0;
        }
        
        // 复制新缓冲区到原始缓冲区
        size_t newSize = newBuffer.size();
        if (newSize <= result) {
            memset(buf, 0, result);
            memcpy(buf, newBuffer.data(), newSize);
            // 更新返回值为新大小
            result = static_cast<int>(newSize);
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
        ProcessReceivedGamePackets(pData, dwSize, gamePackets);
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

    status = g_pfnMH_EnableHook(MH_ALL_HOOKS);
    if (status != MH_OK) {
        g_pfnMH_Uninitialize();
        UnloadMinHookFromMemory();
        return FALSE;
    }
    
    return TRUE;
}

VOID UninitializeHooks() {
    if (g_hMinHookModule) {
        g_pfnMH_DisableHook(MH_ALL_HOOKS);
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

uint64_t ResponseWaiter::GetCurrentSerial() {
    EnterCriticalSection(&s_cs);
    const uint64_t serial = s_responseSerial;
    LeaveCriticalSection(&s_cs);
    return serial;
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
    for (auto& entry : m_opcodeOnlyHandlers) {
        if (entry.opcode == opcode) {
            entry.handler = handler;
            return TRUE;
        }
    }
    m_opcodeOnlyHandlers.push_back({ opcode, handler });
    return TRUE;
}

BOOL ResponseDispatcher::Register(uint32_t opcode, uint32_t params, ResponseHandler handler) {
    if (!handler) return FALSE;

    std::lock_guard<std::mutex> lock(m_mutex);
    const uint64_t key = MakeKey(opcode, params);
    for (auto& entry : m_handlers) {
        if (entry.key == key) {
            entry.handler = handler;
            return TRUE;
        }
    }
    m_handlers.push_back({ key, handler });
    return TRUE;
}

void ResponseDispatcher::Unregister(uint32_t opcode, uint32_t params) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (params != 0xFFFFFFFF) {
        const uint64_t key = MakeKey(opcode, params);
        m_handlers.erase(
            std::remove_if(m_handlers.begin(), m_handlers.end(),
                [key](const HandlerEntry& entry) {
                    return entry.key == key;
                }),
            m_handlers.end()
        );
        return;
    }

    m_opcodeOnlyHandlers.erase(
        std::remove_if(m_opcodeOnlyHandlers.begin(), m_opcodeOnlyHandlers.end(),
            [opcode](const OpcodeHandlerEntry& entry) {
                return entry.opcode == opcode;
            }),
        m_opcodeOnlyHandlers.end()
    );
    m_handlers.erase(
        std::remove_if(m_handlers.begin(), m_handlers.end(),
            [opcode](const HandlerEntry& entry) {
                return static_cast<uint32_t>(entry.key >> 32) == opcode;
            }),
        m_handlers.end()
    );
}

BOOL ResponseDispatcher::Dispatch(const GamePacket& packet) {
    std::lock_guard<std::mutex> lock(m_mutex);

    const uint64_t key = MakeKey(packet.opcode, packet.params);
    for (const auto& entry : m_handlers) {
        if (entry.key == key && entry.handler) {
            entry.handler(packet);
            return TRUE;
        }
    }

    for (const auto& entry : m_opcodeOnlyHandlers) {
        if (entry.opcode == packet.opcode && entry.handler) {
            entry.handler(packet);
            return TRUE;
        }
    }

    return FALSE;
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

        if (gp.opcode == Opcode::BATTLE_ROUND_START) {
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

    registerOpcode(Opcode::LINGYU_LIST, processLingyu);
    registerOpcode(Opcode::DECOMPOSE_RESPONSE, [](const GamePacket&) { HandleDecomposeResponse(); });
    registerOpcode(Opcode::MONSTER_LIST, processMonster);
    registerOpcode(Opcode::ENTER_WORLD, ProcessEnterWorldPacket);
    registerOpcode(Opcode::ENTER_SCENE_BACK, enterScene);
    registerOpcode(Opcode::USER_TASK_LIST_BACK, ProcessEightTrigramsUserTaskListResponse);
    registerOpcode(Opcode::TASK_TALK_BACK, ProcessEightTrigramsTaskTalkResponse);

    registerOpcode(Opcode::DEEP_DIG_BACK, ProcessDeepDigResponse);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, 668, ProcessDeepDigQueryResponse);

    registerOpcode(Opcode::DANCE_ACTIVITY_BACK, ProcessDanceActivityResponse);
    registerOpcode(Opcode::DANCE_STAGE_BACK, ProcessDanceStageResponse);

    registerParams(Opcode::TRIAL_BACK, 142, ProcessTrialResponse);
    registerParams(Opcode::TRIAL_BACK, 341, ProcessTowerActivityResponse);

    registerOpcode(Opcode::COLLECT_STATUS_BACK, ProcessCollectResponse);
    registerOpcode(Opcode::REQ_PACKAGE_DATA_BACK, ProcessPackageDataResponse);

    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act778::ACTIVITY_ID, ProcessAct778Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act793::ACTIVITY_ID, ProcessAct793Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act791::ACTIVITY_ID, ProcessAct791Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act782::ACTIVITY_ID, ProcessAct782Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act803::ACTIVITY_ID, ProcessAct803Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act624::ACTIVITY_ID, ProcessAct624Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, SeaBattle::ACTIVITY_ID, ProcessSeaBattleResponse);
    registerParams(Opcode::HORSE_COMPETITION_BACK, HORSE_COMPETITION_ACT_ID, ProcessHorseCompetitionResponse);

    registerParams(Opcode::HEAVEN_FURUI_BACK, HeavenFurui::ACTIVITY_ID, ProcessHeavenFuruiResponse);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, HeavenFurui::ACTIVITY_ID, ProcessHeavenFuruiResponse);

    registerOpcode(Opcode::BATTLESIX_COMBAT_INFO_BACK, ProcessBattleSixCombatInfoResponse);
    registerOpcode(Opcode::BATTLESIX_MATCH_BACK, ProcessBattleSixMatchResponse);
    registerOpcode(Opcode::BATTLESIX_PREPARE_COMBAT_BACK, ProcessBattleSixPrepareCombatResponse);
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

    StrawberryState& ActivityStateManager::GetStrawberryState() {
        return m_strawberryState;
    }

    TrialState& ActivityStateManager::GetTrialState() {
        return m_trialState;
    }

    void ActivityStateManager::ResetAll() {
        m_strawberryState.Reset();
        m_trialState.Reset();
        m_act778State.Reset();
        m_act793State.Reset();
        m_act791State.Reset();
        m_seaBattleState.Reset();
        m_horseCompetitionState.Reset();
    }

    Act778State& ActivityStateManager::GetAct778State() {
        return m_act778State;
    }

    Act793State& ActivityStateManager::GetAct793State() {
        return m_act793State;
    }

    Act791State& ActivityStateManager::GetAct791State() {
        return m_act791State;
    }

    SeaBattleState& ActivityStateManager::GetSeaBattleState() {
        return m_seaBattleState;
    }

    HorseCompetitionState& ActivityStateManager::GetHorseCompetitionState() {
        return m_horseCompetitionState;
    }


                

    

                

    

                    // ============================================================================

    

                

    

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

        int result = send(targetSocket, reinterpret_cast<const char*>(pData),
                          static_cast<int>(dwSize), 0);
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
        if (!TryParseIntDecimal(indexStr, indexValue)) {
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
    if (WinHttpSendRequest(hRequest, headers, -1,
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
                int32_t value = ReadInt32LE(body, offset);
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
        .SetOpcode(1184313)
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
        .SetOpcode(1184313)
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
                int32_t buycount = ReadInt32LE(packet.body.data(), offset);  // 购买次数

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
        .SetOpcode(1184313)
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
        return FALSE;
    }

    // 4. 等待gameState被设置为2（游戏中），最多等待15秒
    for (int i = 0; i < 150 && g_danceState.gameState != 2; i++) {
        Sleep(100);
    }
    
    // 5. 如果状态变为2（游戏中），启动跳舞线程并等待完成
    if (g_danceState.gameState == 2) {
        HANDLE hThread = CreateThread(NULL, 0, DanceContestThreadProc, NULL, 0, NULL);
        if (hThread) {
            WaitForSingleObject(hThread, 30000);
            CloseHandle(hThread);
            return TRUE;
        }
    }

    return FALSE;
}

// 跳舞大赛主流程（自动执行3次）
BOOL SendDanceContestPacket() {
    // 客户端自己计数，固定执行3次，每次点击从0开始
    int completedCount = 0;
    
    // 辅助函数：通过 UIBridge 更新UI计数
    auto updateDanceCountUI = [](int count) {
        std::wstring jsCode = L"if(window.updateDanceCount) window.updateDanceCount(" 
            + std::to_wstring(count) + L");";
        UIBridge::Instance().ExecuteJS(jsCode);
    };
    
    // 初始化UI显示为0
    updateDanceCountUI(0);

    // 循环执行3次
    for (int i = 0; i < WpeHook::DANCE_MAX_DAILY_COUNT; i++) {
        if (SendDanceContestOnce()) {
            completedCount++;
            
            // 每次完成后更新UI计数
            updateDanceCountUI(completedCount);
            
            // 等待一小段时间确保UI更新
            Sleep(300);
        } else {
            break;
        }
    }
    
    // 最终确保UI显示正确
    updateDanceCountUI(completedCount);
    
    return completedCount > 0;
}
// 跳舞大赛线程函数
DWORD WINAPI DanceContestThreadProc(LPVOID lpParam) {
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
// 保存和载入封包功能
// ============================================================================

BOOL SavePacketsToFile(const std::string& filePath) {
    std::ofstream file(filePath);
    if (!file.is_open()) {
        return FALSE;
    }
    
    CriticalSectionLock lock(g_packetListCS.Get());
    
    for (const auto& packet : g_PacketList) {
        if (packet.bSend) {  // 只保存发送包
            std::string hexStr = HexToString(packet.pData, packet.dwSize);
            file << hexStr << std::endl;
        }
    }
    
    file.close();
    return TRUE;
}

int LoadPacketsFromFile(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return -1;
    }
    
    std::string line;
    int count = 0;
    
    while (std::getline(file, line)) {
        // 去除空白字符
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        
        // 跳过空行
        if (line.empty()) continue;
        
        // 转换为字节数组
        std::vector<BYTE> data = StringToHex(line);
        if (!data.empty()) {
            // 创建封包并添加到列表
            PACKET packet;
            packet.dwSize = (DWORD)data.size();
            packet.bSend = true;  // 默认为发送包
            packet.pData = new BYTE[data.size()];
            memcpy(packet.pData, data.data(), data.size());
            packet.dwTime = GetTickCount();
            
            CriticalSectionLock lock(g_packetListCS.Get());
            g_PacketList.push_back(packet);
            
            count++;
        }
    }
    
    file.close();
    
    // 同步到UI
    SyncPacketsToUI();
    
    return count;
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
                    uint16_t length = ReadUInt16LE(packet.pData, offset); // Length: offset 2-3
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
                uint16_t length = ReadUInt16LE(packetData.data(), offset); // Length: offset 2-3
                
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
    
    for (const auto& packet : packetsCopy) {
        if (packet.bSend && packet.pData && packet.dwSize > 0) {
            std::string hexStr = HexToString(packet.pData, packet.dwSize);
            file << hexStr << std::endl;
        }
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
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        
        if (!line.empty()) {
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

// ============================================================================
// 任务区（八卦灵盘）
// ============================================================================

enum class TaskZoneStepKind {
    Talk,
    Acquire,
    AlertAcquire,
    SceneAcquire,
    RewardOnly,
    Battle,
    Flash,
    BranchOnly
};

struct EightTrigramsTaskData {
    unsigned long long sessionId;
};

struct EightTrigramsStep {
    uint32_t sceneId;
    uint32_t npcId;
    uint32_t dialogId;
    uint32_t chooseId;
    TaskZoneStepKind kind;
    const wchar_t* label;
};

static const EightTrigramsStep EIGHT_TRIGRAMS_STEPS[] = {
    // 八卦灵盘不能按 XML 外观顺序硬排。
    // 规则：先任务指令，再采集/战斗，再提交/领奖；带 choose flag="3" 的节点先发 TRAIN_INFO 再发 TASK_TALK。
    {2001, 21210, 400600101, 3, TaskZoneStepKind::Talk, L"墨乾"},
    {2005, 21511, 400600103, 0, TaskZoneStepKind::AlertAcquire, L"五彩灵芝"},
    {3001, 31101, 400600102, 0, TaskZoneStepKind::Talk, L"墨坤"},
    {2001, 21210, 400600104, 0, TaskZoneStepKind::BranchOnly, L"墨乾(拒绝)"},

    {3001, 31101, 400600201, 3, TaskZoneStepKind::Talk, L"墨坤"},
    {3002, 31231, 400600203, 0, TaskZoneStepKind::AlertAcquire, L"夺灵丹"},
    {3002, 31231, 400600204, 0, TaskZoneStepKind::Talk, L"火炎"},
    {4001, 41101, 400600202, 0, TaskZoneStepKind::Talk, L"墨坎"},

    {4001, 41101, 400600301, 3, TaskZoneStepKind::Talk, L"墨坎"},
    {4003, 41311, 400600303, 0, TaskZoneStepKind::SceneAcquire, L"菩提子"},
    {5002, 51206, 400600302, 0, TaskZoneStepKind::Talk, L"墨离"},

    {5002, 51206, 400600401, 3, TaskZoneStepKind::Talk, L"墨离"},
    {5003, 51305, 400600403, 0, TaskZoneStepKind::AlertAcquire, L"黑河水"},
    {6001, 61101, 400600402, 0, TaskZoneStepKind::Talk, L"墨震"},

    {6001, 61101, 400600501, 3, TaskZoneStepKind::Talk, L"墨震"},
    {6002, 401311, 400600506, 0, TaskZoneStepKind::Battle, L"水魂"},
    {7002, 71201, 400600507, 0, TaskZoneStepKind::RewardOnly, L"墨艮"},
    {7002, 71201, 400600505, 0, TaskZoneStepKind::Talk, L"墨艮"},

    {7002, 71201, 400600601, 3, TaskZoneStepKind::Talk, L"墨艮"},
    {8003, 81303, 400600603, 0, TaskZoneStepKind::AlertAcquire, L"火铜"},
    {9001, 91101, 400600602, 0, TaskZoneStepKind::Talk, L"墨巽"},

    {9001, 91101, 400600701, 3, TaskZoneStepKind::Talk, L"墨巽"},
    {9004, 91435, 400600703, 0, TaskZoneStepKind::AlertAcquire, L"柿果"},
    {10001, 101101, 400600702, 0, TaskZoneStepKind::Talk, L"墨兑"},
    {0, 0, 400600704, 0, TaskZoneStepKind::Flash, L"命运大转盘"},

    {10001, 101101, 400600801, 3, TaskZoneStepKind::Talk, L"墨兑"},
    {10002, 101201, 400600803, 0, TaskZoneStepKind::Talk, L"墨魂"},
    {10002, 101201, 400600804, 0, TaskZoneStepKind::Battle, L"墨魂"},
    {0, 0, 400600805, 0, TaskZoneStepKind::BranchOnly, L"墨魂(败北)"},
    {0, 0, 400600806, 0, TaskZoneStepKind::BranchOnly, L"墨魂(胜利)"},
    {2001, 21210, 400600802, 0, TaskZoneStepKind::Talk, L"墨乾"},
};

static constexpr size_t EIGHT_TRIGRAMS_STEP_COUNT = sizeof(EIGHT_TRIGRAMS_STEPS) / sizeof(EIGHT_TRIGRAMS_STEPS[0]);

static uint32_t GetEightTrigramsGroupId(const EightTrigramsStep& step) {
    return step.dialogId / 100;
}

static std::vector<uint32_t> BuildEightTrigramsGroupOrder() {
    std::vector<uint32_t> groupOrder;
    uint32_t lastGroupId = 0;
    for (const auto& step : EIGHT_TRIGRAMS_STEPS) {
        const uint32_t groupId = GetEightTrigramsGroupId(step);
        if (groupId == 0 || groupId == lastGroupId) {
            continue;
        }
        groupOrder.push_back(groupId);
        lastGroupId = groupId;
    }
    return groupOrder;
}

static size_t FindEightTrigramsStepIndexForGroup(uint32_t groupId) {
    for (size_t i = 0; i < EIGHT_TRIGRAMS_STEP_COUNT; ++i) {
        if (GetEightTrigramsGroupId(EIGHT_TRIGRAMS_STEPS[i]) == groupId) {
            return i;
        }
    }
    return EIGHT_TRIGRAMS_STEP_COUNT;
}

static size_t FindEightTrigramsStepIndexAfterGroup(uint32_t groupId) {
    bool groupStarted = false;
    for (size_t i = 0; i < EIGHT_TRIGRAMS_STEP_COUNT; ++i) {
        const uint32_t stepGroupId = GetEightTrigramsGroupId(EIGHT_TRIGRAMS_STEPS[i]);
        if (!groupStarted) {
            if (stepGroupId == groupId) {
                groupStarted = true;
            }
            continue;
        }
        if (stepGroupId != groupId && stepGroupId != 0) {
            return i;
        }
    }
    return EIGHT_TRIGRAMS_STEP_COUNT;
}

static size_t FindEightTrigramsStepIndexByDialogId(uint32_t dialogId) {
    for (size_t i = 0; i < EIGHT_TRIGRAMS_STEP_COUNT; ++i) {
        if (EIGHT_TRIGRAMS_STEPS[i].dialogId == dialogId) {
            return i;
        }
    }
    return EIGHT_TRIGRAMS_STEP_COUNT;
}

static void UpdateEightTrigramsResumeHint(size_t nextIndex) {
    g_eightTrigramsResumeStepIndex.store(static_cast<int>(nextIndex));
}

static void UpdateEightTrigramsResumeHintByDialogId(uint32_t dialogId);

static void ResetEightTrigramsUserTaskListCache() {
    std::lock_guard<std::mutex> lock(g_eightTrigramsProgress.mutex);
    g_eightTrigramsProgress.userTaskListLoaded = false;
    g_eightTrigramsProgress.acceptedSubtaskIds.clear();
    g_eightTrigramsProgress.finishedSubtaskIds.clear();
}

static void ResetEightTrigramsProgressState() {
    std::lock_guard<std::mutex> lock(g_eightTrigramsProgress.mutex);
    g_eightTrigramsProgress.userTaskListLoaded = false;
    g_eightTrigramsProgress.acceptedSubtaskIds.clear();
    g_eightTrigramsProgress.finishedSubtaskIds.clear();
    g_eightTrigramsProgress.taskTalkResponseReceived = false;
    g_eightTrigramsProgress.taskTalkResponseType = 0;
    g_eightTrigramsProgress.taskTalkResponseDialogId = 0;
    g_eightTrigramsProgress.taskTalkResponseNpcId = 0;
    g_eightTrigramsProgress.taskTalkResponseMatchNpcId = true;
    g_eightTrigramsProgress.talkCurrentId = 0;
    g_eightTrigramsProgress.talkDialogId = 0;
    g_eightTrigramsProgress.talkTrigram = 0;
    g_eightTrigramsProgress.talkExp = 0;
    g_eightTrigramsProgress.talkNpcId = 0;
    g_eightTrigramsProgress.talkItemId = 0;
}

static bool TryResolveEightTrigramsResumeIndex(size_t& startIndex) {
    const size_t defaultIndex = 0;
    const auto groupOrder = BuildEightTrigramsGroupOrder();

    const int resumeStepIndex = g_eightTrigramsResumeStepIndex.load();
    if (resumeStepIndex >= 0) {
        const size_t resumeIndex = static_cast<size_t>(resumeStepIndex);
        if (resumeIndex < EIGHT_TRIGRAMS_STEP_COUNT &&
            EIGHT_TRIGRAMS_STEPS[resumeIndex].dialogId == 400600704) {
            const size_t fallbackIndex = FindEightTrigramsStepIndexByDialogId(400600702);
            if (fallbackIndex < EIGHT_TRIGRAMS_STEP_COUNT) {
                startIndex = fallbackIndex;
                return true;
            }
        }
        startIndex = resumeIndex > EIGHT_TRIGRAMS_STEP_COUNT ? EIGHT_TRIGRAMS_STEP_COUNT : resumeIndex;
        return true;
    }

    std::lock_guard<std::mutex> lock(g_eightTrigramsProgress.mutex);
    if (g_eightTrigramsProgress.userTaskListLoaded) {
        if (!g_eightTrigramsProgress.acceptedSubtaskIds.empty()) {
            const uint32_t acceptedGroupId = *std::max_element(
                g_eightTrigramsProgress.acceptedSubtaskIds.begin(),
                g_eightTrigramsProgress.acceptedSubtaskIds.end());
            const size_t acceptedIndex = FindEightTrigramsStepIndexForGroup(acceptedGroupId);
            if (acceptedIndex < EIGHT_TRIGRAMS_STEP_COUNT) {
                startIndex = acceptedIndex;
                g_eightTrigramsResumeStepIndex.store(static_cast<int>(startIndex));
                return true;
            }
        }

        if (!g_eightTrigramsProgress.finishedSubtaskIds.empty()) {
            const uint32_t finishedGroupId = *std::max_element(
                g_eightTrigramsProgress.finishedSubtaskIds.begin(),
                g_eightTrigramsProgress.finishedSubtaskIds.end());
            const size_t nextIndex = FindEightTrigramsStepIndexAfterGroup(finishedGroupId);
            if (nextIndex < EIGHT_TRIGRAMS_STEP_COUNT) {
                startIndex = nextIndex;
                g_eightTrigramsResumeStepIndex.store(static_cast<int>(startIndex));
                return true;
            }
            startIndex = EIGHT_TRIGRAMS_STEP_COUNT;
            g_eightTrigramsResumeStepIndex.store(static_cast<int>(startIndex));
            return true;
        }
    }

    if (g_eightTrigramsProgress.talkCurrentId > 0) {
        const uint32_t currentId = g_eightTrigramsProgress.talkCurrentId;
        if (currentId >= groupOrder.size()) {
            startIndex = EIGHT_TRIGRAMS_STEP_COUNT;
            return true;
        }

        const uint32_t nextGroupId = groupOrder[currentId];
        const size_t nextIndex = FindEightTrigramsStepIndexForGroup(nextGroupId);
        if (nextIndex < EIGHT_TRIGRAMS_STEP_COUNT) {
            startIndex = nextIndex;
            g_eightTrigramsResumeStepIndex.store(static_cast<int>(startIndex));
            return true;
        }
    }

    startIndex = defaultIndex;
    return false;
}

static std::wstring BuildTaskZoneStatus(const EightTrigramsStep& step, size_t index, size_t total) {
    std::wstring status = L"任务区：";
    status += std::to_wstring(index + 1);
    status += L"/";
    status += std::to_wstring(total);
    status += L" ";

    switch (step.kind) {
        case TaskZoneStepKind::Talk:
            status += L"对话 ";
            break;
        case TaskZoneStepKind::Acquire:
        case TaskZoneStepKind::AlertAcquire:
        case TaskZoneStepKind::SceneAcquire:
        case TaskZoneStepKind::RewardOnly:
            status += (step.kind == TaskZoneStepKind::RewardOnly ? L"前往 " : L"获取 ");
            break;
        case TaskZoneStepKind::Battle:
            status += L"战斗 ";
            break;
        case TaskZoneStepKind::Flash:
            status += L"动画 ";
            break;
        case TaskZoneStepKind::BranchOnly:
            status += L"结果 ";
            break;
    }

    status += step.label;

    if (step.sceneId != 0) {
        std::wstring mapName = GetMapName(static_cast<int>(step.sceneId));
        if (mapName.empty()) {
            mapName = std::to_wstring(step.sceneId);
        }
        status += L" @";
        status += mapName;
    }

    return status;
}

static void ProcessEightTrigramsUserTaskListResponse(const GamePacket& packet) {
    if (packet.body.empty()) {
        return;
    }

    size_t offset = 0;
    std::string newestFlag;
    if (!ReadPacketString(packet.body.data(), packet.body.size(), offset, newestFlag)) {
        return;
    }

    std::vector<uint32_t> acceptedSubtaskIds;
    std::vector<uint32_t> finishedSubtaskIds;

    if (offset + 4 > packet.body.size()) {
        return;
    }
    const int acceptedCount = ReadInt32LE(packet.body.data(), offset);
    for (int i = 0; i < acceptedCount; ++i) {
        if (offset + 8 > packet.body.size()) {
            return;
        }

        const uint32_t subtaskId = static_cast<uint32_t>(ReadInt32LE(packet.body.data(), offset));
        const int taskParamCount = ReadInt32LE(packet.body.data(), offset);
        for (int j = 0; j < taskParamCount; ++j) {
            if (offset + 8 > packet.body.size()) {
                return;
            }
            ReadInt32LE(packet.body.data(), offset);
            ReadInt32LE(packet.body.data(), offset);
        }

        const uint32_t taskId = subtaskId - (subtaskId % 1000);
        if (taskId == 4006000) {
            acceptedSubtaskIds.push_back(subtaskId);
        }
    }

    if (offset + 4 > packet.body.size()) {
        return;
    }
    const int finishedCount = ReadInt32LE(packet.body.data(), offset);
    for (int i = 0; i < finishedCount; ++i) {
        if (offset + 8 > packet.body.size()) {
            return;
        }

        const uint32_t subtaskId = static_cast<uint32_t>(ReadInt32LE(packet.body.data(), offset));
        ReadInt32LE(packet.body.data(), offset);  // finishTime

        const uint32_t taskId = subtaskId - (subtaskId % 1000);
        if (taskId == 4006000) {
            finishedSubtaskIds.push_back(subtaskId);
        }
    }

    std::lock_guard<std::mutex> lock(g_eightTrigramsProgress.mutex);
    g_eightTrigramsProgress.acceptedSubtaskIds = std::move(acceptedSubtaskIds);
    g_eightTrigramsProgress.finishedSubtaskIds = std::move(finishedSubtaskIds);
    g_eightTrigramsProgress.userTaskListLoaded = true;
}

static void ProcessEightTrigramsTaskTalkResponse(const GamePacket& packet) {
    size_t offset = 0;
    uint32_t dialogId = 0;
    uint32_t npcId = 0;
    uint32_t currentId = 0;
    uint32_t trigram = 0;
    uint32_t exp = 0;
    uint32_t itemId = 0;
    bool hasDialogId = false;
    bool hasNpcId = false;
    bool hasCurrentId = false;
    bool hasTrigram = false;
    bool hasExp = false;
    bool hasItemId = false;

    auto readUInt32 = [&packet, &offset](uint32_t& value) -> bool {
        if (offset + 4 > packet.body.size()) {
            return false;
        }
        value = static_cast<uint32_t>(ReadInt32LE(packet.body.data(), offset));
        return true;
    };

    switch (packet.params) {
        case 1:
        case 5:
        case 22: {
            uint32_t itemCount = 0;
            if (!readUInt32(npcId) || !readUInt32(itemCount)) {
                return;
            }
            for (uint32_t i = 0; i < itemCount; ++i) {
                uint32_t itemType = 0;
                uint32_t itemIdValue = 0;
                uint32_t itemCountValue = 0;
                if (!readUInt32(itemType) || !readUInt32(itemIdValue) || !readUInt32(itemCountValue)) {
                    return;
                }
            }
            if (!readUInt32(dialogId)) {
                return;
            }
            hasNpcId = true;
            hasDialogId = true;
            break;
        }
        case 2:
        case 14:
        case 17:
            if (!readUInt32(dialogId)) {
                return;
            }
            hasDialogId = true;
            break;
        case 3:
            if (!readUInt32(dialogId)) {
                return;
            }
            hasDialogId = true;
            break;
        case 7:
            {
                uint32_t boardId = 0;
                if (!readUInt32(boardId) ||
                    !readUInt32(dialogId) ||
                    !readUInt32(npcId)) {
                    return;
                }
                hasDialogId = true;
                hasNpcId = true;
            }
            break;
        case 11:
            if (!readUInt32(dialogId) ||
                !readUInt32(currentId) ||
                !readUInt32(trigram) ||
                !readUInt32(exp) ||
                !readUInt32(npcId) ||
                !readUInt32(itemId)) {
                return;
            }
            hasDialogId = true;
            hasNpcId = true;
            hasCurrentId = true;
            hasTrigram = true;
            hasExp = true;
            hasItemId = true;
            break;
        case 21: {
            uint32_t paramCount = 0;
            if (!readUInt32(npcId) || !readUInt32(paramCount)) {
                return;
            }
            for (uint32_t i = 0; i < paramCount; ++i) {
                uint32_t itemType = 0;
                uint32_t itemIdValue = 0;
                if (!readUInt32(itemType) || !readUInt32(itemIdValue)) {
                    return;
                }
            }
            if (!readUInt32(dialogId)) {
                return;
            }
            hasNpcId = true;
            hasDialogId = true;
            break;
        }
        case 26:
            if (!readUInt32(dialogId)) {
                return;
            }
            hasDialogId = true;
            break;
        default:
            return;
    }

    uint32_t expectedDialogId = 0;
    uint32_t expectedNpcId = 0;
    bool matchNpcId = true;
    {
        std::lock_guard<std::mutex> lock(g_eightTrigramsProgress.mutex);
        expectedDialogId = g_eightTrigramsProgress.taskTalkResponseDialogId;
        expectedNpcId = g_eightTrigramsProgress.taskTalkResponseNpcId;
        matchNpcId = g_eightTrigramsProgress.taskTalkResponseMatchNpcId;
        if (!hasDialogId ||
            dialogId != expectedDialogId ||
            (matchNpcId && hasNpcId && npcId != expectedNpcId)) {
            return;
        }

        g_eightTrigramsProgress.taskTalkResponseReceived = true;
        g_eightTrigramsProgress.taskTalkResponseType = packet.params;
        g_eightTrigramsProgress.taskTalkResponseDialogId = dialogId;
        g_eightTrigramsProgress.talkDialogId = dialogId;
        if (hasNpcId) {
            g_eightTrigramsProgress.taskTalkResponseNpcId = npcId;
            g_eightTrigramsProgress.talkNpcId = npcId;
        }
        if (hasCurrentId) {
            g_eightTrigramsProgress.talkCurrentId = currentId;
        }
        if (hasTrigram) {
            g_eightTrigramsProgress.talkTrigram = trigram;
        }
        if (hasExp) {
            g_eightTrigramsProgress.talkExp = exp;
        }
        if (hasItemId) {
            g_eightTrigramsProgress.talkItemId = itemId;
        }
    }

    UpdateEightTrigramsResumeHintByDialogId(dialogId);
}

static void UpdateTaskZoneUi(const std::wstring& text, bool running) {
    UIBridge::Instance().UpdateHelperText(text);
    std::wstring script = L"if(window.updateTaskZoneStatus) { window.updateTaskZoneStatus('" +
                          UIBridge::EscapeJsonString(text) + L"', " +
                          (running ? L"true" : L"false") + L"); }";
    PostScriptToUI(script);
}

static BOOL QueryEightTrigramsTaskListProgress() {
    ResetEightTrigramsUserTaskListCache();

    auto packet = PacketBuilder()
        .SetOpcode(Opcode::USER_TASK_LIST_SEND)
        .Build();

    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()), Opcode::USER_TASK_LIST_BACK, 3000);
}

// 进图是全局响应事件，这里不用 ResponseWaiter，避免被其他自动流程抢走。
static bool WaitForEightTrigramsMapEnter(DWORD timeoutMs) {
    const DWORD startTick = GetTickCount();
    while (g_taskZoneRunning.load()) {
        if (g_taskZoneMapEntered.load()) {
            return true;
        }

        if (GetTickCount() - startTick >= timeoutMs) {
            return false;
        }

        Sleep(50);
    }

    return false;
}

static void UpdateEightTrigramsResumeHintByDialogId(uint32_t dialogId) {
    const size_t stepIndex = FindEightTrigramsStepIndexByDialogId(dialogId);
    if (stepIndex < EIGHT_TRIGRAMS_STEP_COUNT) {
        UpdateEightTrigramsResumeHint(stepIndex + 1);
    }
}

static BOOL SendTaskZoneTalkPacket(
    uint32_t npcId,
    uint32_t dialogId,
    uint32_t chooseId = 0,
    bool waitForBack = true,
    DWORD timeoutMs = 3000,
    bool matchNpcId = true) {
    if (chooseId != 0) {
        auto trainInfoPacket = PacketBuilder()
            .SetOpcode(Opcode::TRAIN_INFO_SEND)
            .SetParams(7)
            .WriteUInt32(dialogId)
            .WriteUInt32(chooseId)
            .Build();

        if (!SendPacket(0,
                        trainInfoPacket.data(),
                        static_cast<DWORD>(trainInfoPacket.size()))) {
            return FALSE;
        }
    }

    auto packet = PacketBuilder()
        .SetOpcode(Opcode::TASK_TALK_SEND)
        .SetParams(npcId)
        .WriteUInt32(dialogId)
        .Build();

    if (!waitForBack) {
        return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
    }

    {
        std::lock_guard<std::mutex> lock(g_eightTrigramsProgress.mutex);
        g_eightTrigramsProgress.taskTalkResponseReceived = false;
        g_eightTrigramsProgress.taskTalkResponseDialogId = dialogId;
        g_eightTrigramsProgress.taskTalkResponseNpcId = npcId;
        g_eightTrigramsProgress.taskTalkResponseMatchNpcId = matchNpcId;
    }

    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()), Opcode::TASK_TALK_BACK, timeoutMs);
}

static BOOL SendTaskZoneClickPacket(uint32_t npcId, bool battleMode = false) {
    uint32_t counter = battleMode ? static_cast<uint32_t>(g_battleCounter.load()) : 0;
    if (battleMode && counter == 0) {
        counter = 1;
    }

    auto packet = PacketBuilder()
        .SetOpcode(Opcode::CLICK_NPC)
        .SetParams(npcId)
        .WriteUInt32(counter)
        .Build();

    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

static void PauseAfterEightTrigramsStep(TaskZoneStepKind kind) {
    switch (kind) {
        case TaskZoneStepKind::Acquire:
        case TaskZoneStepKind::AlertAcquire:
            Sleep(800);
            break;
        case TaskZoneStepKind::Flash:
            Sleep(600);
            break;
        case TaskZoneStepKind::Battle:
            Sleep(400);
            break;
        case TaskZoneStepKind::BranchOnly:
            Sleep(250);
            break;
        case TaskZoneStepKind::Talk:
        default:
            Sleep(350);
            break;
    }
}

static uint32_t GetBattleTargetSid(const BattleData& battle) {
    if (battle.otherActiveIndex >= 0 &&
        battle.otherActiveIndex < static_cast<int>(battle.otherPets.size())) {
        return static_cast<uint32_t>(battle.otherPets[battle.otherActiveIndex].sid);
    }

    for (const auto& pet : battle.otherPets) {
        if (pet.sid != 0) {
            return static_cast<uint32_t>(pet.sid);
        }
    }

    return 0;
}

static uint32_t GetBattleSkillId(const BattleEntity& battleEntity) {
    for (const auto& skill : battleEntity.skills) {
        if (skill.pp > 0) {
            return skill.id;
        }
    }

    for (const auto& skill : battleEntity.skills) {
        if (skill.id != 0) {
            return skill.id;
        }
    }

    return 0;
}

static BOOL RunEightTrigramsBattleLoop(unsigned long long sessionId) {
    if (!SendBattleReadyPacket()) {
        return FALSE;
    }

    Sleep(300);

    for (int loop = 0; loop < 40 && g_taskZoneRunning.load(); ++loop) {
        if (g_taskZoneSession.load() != sessionId) {
            return FALSE;
        }

        BattleData& battle = PacketParser::GetCurrentBattle();
        if (battle.myPets.empty() || battle.otherPets.empty()) {
            break;
        }

        if (battle.myActiveIndex < 0 ||
            battle.myActiveIndex >= static_cast<int>(battle.myPets.size())) {
            Sleep(100);
            continue;
        }

        const BattleEntity& myPet = battle.myPets[battle.myActiveIndex];
        uint32_t targetSid = GetBattleTargetSid(battle);
        uint32_t skillId = GetBattleSkillId(myPet);
        if (targetSid == 0 || skillId == 0) {
            Sleep(200);
            continue;
        }

        auto packet = PacketBuilder()
            .SetOpcode(Opcode::USER_OP_SEND)
            .SetParams(0)
            .WriteInt32(static_cast<int32_t>(targetSid))
            .WriteInt32(static_cast<int32_t>(skillId))
            .Build();

        if (!SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()))) {
            return FALSE;
        }

        Sleep(1000);

        if (!SendBattlePlayOverPacket()) {
            return FALSE;
        }

        Sleep(500);

        if (PacketParser::GetCurrentBattle().myPets.empty() ||
            PacketParser::GetCurrentBattle().otherPets.empty()) {
            break;
        }
    }

    return TRUE;
}

static DWORD WINAPI EightTrigramsTaskThreadProc(LPVOID lpParam) {
    std::unique_ptr<EightTrigramsTaskData> taskData(static_cast<EightTrigramsTaskData*>(lpParam));
    const unsigned long long sessionId = taskData->sessionId;

    const size_t stepCount = sizeof(EIGHT_TRIGRAMS_STEPS) / sizeof(EIGHT_TRIGRAMS_STEPS[0]);
    size_t startIndex = 0;
    if (startIndex >= stepCount) {
        startIndex = 0;
    }

    uint32_t currentSceneId = 0;
    uint32_t lastRelevantNpcId = 0;
    bool flowFailed = false;
    std::wstring startText;

    UpdateTaskZoneUi(L"任务区：读取当前进度...", true);
    const bool progressLoaded = QueryEightTrigramsTaskListProgress();
    bool hasResumeIndex = TryResolveEightTrigramsResumeIndex(startIndex);
    if (!progressLoaded && !hasResumeIndex) {
        UpdateTaskZoneUi(L"任务区：读取当前进度失败", false);
        flowFailed = true;
        goto CLEANUP;
    }

    if (startIndex >= stepCount) {
        UpdateTaskZoneUi(L"任务区：八卦灵盘已经完成", false);
        g_eightTrigramsResumeStepIndex.store(-1);
        goto CLEANUP;
    }

    startText = (startIndex == 0) ? L"任务区：开始执行八卦灵盘..." : L"任务区：继续执行八卦灵盘...";
    UpdateTaskZoneUi(startText, true);
    Sleep(300);

    for (size_t i = startIndex; i < stepCount && g_taskZoneRunning.load(); ++i) {
        if (g_taskZoneSession.load() != sessionId) {
            goto CLEANUP;
        }

        const EightTrigramsStep& step = EIGHT_TRIGRAMS_STEPS[i];
        UpdateTaskZoneUi(BuildTaskZoneStatus(step, i - startIndex, stepCount - startIndex), true);

        if (step.kind == TaskZoneStepKind::Flash) {
            if (step.dialogId == 400600704) {
                const uint32_t turntableNpcId = lastRelevantNpcId != 0 ? lastRelevantNpcId : 61101;
                // 1111111 是特殊转盘结果回包，npcid 可能与发起时不一致，不做强匹配。
                if (!SendTaskZoneTalkPacket(turntableNpcId, 1111111, 0, true, 5000, false)) {
                    UpdateTaskZoneUi(L"任务区：转盘收口失败", false);
                    flowFailed = true;
                    break;
                }
                UpdateEightTrigramsResumeHint(i + 1);
            }
            continue;
        }

        if (step.kind == TaskZoneStepKind::BranchOnly) {
            continue;
        }

        if (step.sceneId != 0 && step.sceneId != currentSceneId) {
            std::wstring mapName = GetMapName(static_cast<int>(step.sceneId));
            if (mapName.empty()) {
                mapName = std::to_wstring(step.sceneId);
            }
            UpdateTaskZoneUi(L"任务区：进入 " + mapName + L"...", true);
            g_taskZoneMapEntered = false;
            if (!SendEnterScenePacket(static_cast<int>(step.sceneId))) {
                UpdateTaskZoneUi(L"任务区：进入场景失败", false);
                flowFailed = true;
                break;
            }
            if (!WaitForEightTrigramsMapEnter(5000)) {
                UpdateTaskZoneUi(L"任务区：进入场景失败", false);
                flowFailed = true;
                break;
            }
            currentSceneId = step.sceneId;
            UpdateEightTrigramsResumeHint(i + 1);
            Sleep(300);
        }

        if (step.kind == TaskZoneStepKind::Battle) {
            if (step.dialogId == 400600506) {
                // 水魂战斗按实际封包发送：CLICK_NPC + counter。
                // 抓包显示 params 末三字节对应 0x17 / 0x10 / 0x08。
                if (!SendBattlePacket(0x17, 0x10, 0x08, 1)) {
                    UpdateTaskZoneUi(L"任务区：发起战斗失败", false);
                    flowFailed = true;
                    break;
                }
            } else if (!SendTaskZoneClickPacket(step.npcId, true)) {
                UpdateTaskZoneUi(L"任务区：发起战斗失败", false);
                flowFailed = true;
                break;
            }

            Sleep(800);

            const DWORD battleWaitStart = GetTickCount();
            while (g_taskZoneRunning.load()) {
                if (g_taskZoneSession.load() != sessionId) {
                    break;
                }
                BattleData& battle = PacketParser::GetCurrentBattle();
                if (!battle.myPets.empty() && !battle.otherPets.empty()) {
                    break;
                }
                if (GetTickCount() - battleWaitStart > 10000) {
                    UpdateTaskZoneUi(L"任务区：等待战斗开始超时", false);
                    g_taskZoneRunning = false;
                    flowFailed = true;
                    return FALSE;
                }
                Sleep(100);
            }

            if (!g_taskZoneRunning.load() || g_taskZoneSession.load() != sessionId) {
                break;
            }

            if (!RunEightTrigramsBattleLoop(sessionId)) {
                UpdateTaskZoneUi(L"任务区：战斗流程失败", false);
                flowFailed = true;
                break;
            }

            UpdateEightTrigramsResumeHint(i + 1);
            continue;
        }

        const bool shouldClickNpc =
            (step.kind == TaskZoneStepKind::Talk ||
             step.kind == TaskZoneStepKind::Acquire ||
             step.kind == TaskZoneStepKind::AlertAcquire);

        if (shouldClickNpc) {
            if (!SendTaskZoneClickPacket(step.npcId, false)) {
                UpdateTaskZoneUi(L"任务区：点击目标失败", false);
                flowFailed = true;
                break;
            }
        }

        if (step.kind == TaskZoneStepKind::SceneAcquire) {
            // 菩提子这里按抓包样本走特殊区域点击包，场景对象是 41311，但实际发包参数是 41312。
            const uint32_t specialAreaNpcId = step.npcId + 1;
            if (!SendTaskZoneClickPacket(specialAreaNpcId, false)) {
                UpdateTaskZoneUi(L"任务区：获取失败", false);
                flowFailed = true;
                break;
            }
            lastRelevantNpcId = specialAreaNpcId;
            UpdateEightTrigramsResumeHint(i + 1);
            continue;
        }

        if (step.kind == TaskZoneStepKind::RewardOnly) {
            // 只做转场，不再主动发包。
            UpdateEightTrigramsResumeHint(i + 1);
            continue;
        }

        if (step.kind == TaskZoneStepKind::AlertAcquire) {
            if (!SendTaskZoneTalkPacket(step.npcId, step.dialogId, step.chooseId, true)) {
                UpdateTaskZoneUi(L"任务区：获取失败", false);
                flowFailed = true;
                break;
            }
            lastRelevantNpcId = step.npcId;
            UpdateEightTrigramsResumeHint(i + 1);
            continue;
        }

        if (step.kind == TaskZoneStepKind::Acquire) {
            if (!SendTaskZoneTalkPacket(step.npcId, step.dialogId, step.chooseId, true)) {
                UpdateTaskZoneUi(L"任务区：获取失败", false);
                flowFailed = true;
                break;
            }
            lastRelevantNpcId = step.npcId;
            UpdateEightTrigramsResumeHint(i + 1);
            continue;
        }

        if (step.dialogId != 0) {
            if (!SendTaskZoneTalkPacket(step.npcId, step.dialogId, step.chooseId, true)) {
                UpdateTaskZoneUi(L"任务区：对话发送失败", false);
                flowFailed = true;
                break;
            }

            lastRelevantNpcId = step.npcId;
            if (step.dialogId == 400600702) {
                // 702 是提交柿果后的过渡对话，保留在这里，避免重开直接落到转盘。
                UpdateEightTrigramsResumeHint(i);
            } else {
                UpdateEightTrigramsResumeHint(i + 1);
            }
        }
    }

    CLEANUP:
    const bool stillRunning = g_taskZoneRunning.exchange(false);
    g_taskZoneMapEntered = false;
    if (!flowFailed && stillRunning) {
        g_eightTrigramsResumeStepIndex.store(-1);
        UpdateTaskZoneUi(L"任务区：八卦灵盘流程已完成", false);
    } else {
        UpdateTaskZoneUi(flowFailed ? L"任务区：流程中断" : L"任务区：已停止", false);
    }

    return 0;
}

BOOL StartEightTrigramsTaskAsync() {
    if (g_taskZoneRunning.load()) {
        UpdateTaskZoneUi(L"任务区：八卦灵盘已在运行中", true);
        return FALSE;
    }

    if (g_LastGameSocket == 0 || g_userId.load() == 0) {
        UpdateTaskZoneUi(L"任务区：请先进入游戏", false);
        return FALSE;
    }

    if (g_battleSixAuto.IsInBattle() || g_battleSixAuto.IsAutoMatching() || g_shuangtaiAuto.IsRunning()) {
        UpdateTaskZoneUi(L"任务区：请先停止其他自动战斗", false);
        return FALSE;
    }

    const unsigned long long sessionId = g_taskZoneSession.fetch_add(1) + 1;
    g_taskZoneRunning = true;
    g_taskZoneMapEntered = false;
    ResetEightTrigramsProgressState();

    EightTrigramsTaskData* taskData = new EightTrigramsTaskData();
    taskData->sessionId = sessionId;

    HANDLE hThread = CreateThread(nullptr, 0, EightTrigramsTaskThreadProc, taskData, 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
        return TRUE;
    }

    delete taskData;
    g_taskZoneRunning = false;
    g_taskZoneMapEntered = false;
    UpdateTaskZoneUi(L"任务区：启动失败", false);
    return FALSE;
}

VOID StopEightTrigramsTask() {
    if (!g_taskZoneRunning.load()) {
        UpdateTaskZoneUi(L"任务区：已停止", false);
        return;
    }

    g_taskZoneRunning = false;
    g_taskZoneMapEntered = false;
    g_taskZoneSession.fetch_add(1);
    ResponseWaiter::CancelWait();
    UpdateTaskZoneUi(L"任务区：已停止", false);
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

/** 辅助函数：格式化十六进制数为字符串 */
static std::wstring fmt_hex(uint32_t value) {
    wchar_t buf[16];
    swprintf_s(buf, L"%04X", value);
    return std::wstring(buf);
}

/** 采集单个物品 */
static BOOL CollectSingleItem(const CollectItemData& item) {
    if (item.maxCount == 0) {
        // 坐骑蛋特殊处理
        return TRUE;
    }

    // 1. 跳转地图封包: Opcode 1184313, Params=mapId
    auto mapPacket = PacketBuilder()
        .SetOpcode(1184313)
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
    // 直接使用全局变量 g_battleCounter
    // 如果从未进入过战斗（counter 为 0），则使用默认值 1
    uint32_t counter = forcedCounter != 0 ? forcedCounter : g_battleCounter.load();
    if (counter == 0) {
        counter = 1;
    }
    
    // 封包结构：
    // Magic: 0x5344 (小端序)
    // Length: 4 (Body长度，4字节)
    // Opcode: 1186048 (OP_CLIENT_CLICK_NPC)
    // Params: 高16位=useId，低16位=(extraParam << 8 | spiritId & 0xFF)
    // Body: counter (小端序)
    
    // 组合 Params
    uint32_t params;
    if (extraParam == 0) {
        // BOSS 挑战：字节 0-1=spiritId (小端序)，字节 2=0，字节 3=useId
        params = (spiritId & 0xFFFF) | ((useId & 0xFF) << 24);
    } else {
        // 野怪战斗：字节 0=spiritId_low8，字节 1=extraParam，字节 2=0，字节 3=useId
        params = (spiritId & 0xFF) | ((extraParam & 0xFF) << 8) | ((useId & 0xFF) << 24);
    }
    
    // 小端序构造封包
    std::vector<BYTE> packet(16);  // 12字节头部 + 4字节Body
    
    // Magic: 0x5344 (小端序)
    packet[0] = 0x44;  // 'D'
    packet[1] = 0x53;  // 'S'
    
    // Length: 4 (小端序)
    packet[2] = 0x04;
    packet[3] = 0x00;
    
    // Opcode: 1186048 (0x00121900, 小端序)
    packet[4] = 0x00;
    packet[5] = 0x19;
    packet[6] = 0x12;
    packet[7] = 0x00;
    
    // Params (小端序)
    packet[8] = params & 0xFF;
    packet[9] = (params >> 8) & 0xFF;
    packet[10] = (params >> 16) & 0xFF;
    packet[11] = (params >> 24) & 0xFF;
    
    // Body: counter (小端序)
    packet[12] = counter & 0xFF;
    packet[13] = (counter >> 8) & 0xFF;
    packet[14] = (counter >> 16) & 0xFF;
    packet[15] = (counter >> 24) & 0xFF;
    
    // 发送封包
    return SendPacket(0, packet.data(), (DWORD)packet.size());
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
                int isVailTime = ReadInt32LE(body, offset);
                int clickIconFlag = ReadInt32LE(body, offset);
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
                    int openBoxNum = ReadInt32LE(body, offset);
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
// 万妖盛会PVP功能实现 (BattleSix)
// ============================================================================

// BattleSixAutoBattle 类实现
BattleSixAutoBattle::BattleSixAutoBattle()
    : m_currentSpiritIndex(0)
    , m_currentSkillIndex(0)
    , m_enemySid(0)
    , m_enemyUniqueId(0)
    , m_myUniqueId(0)
    , m_isInBattle(false)
    , m_autoBattleEnabled(false)
    , m_autoMatching(false)
    , m_matchCount(0)
    , m_totalMatchCount(0)
    , m_winCount(0)
    , m_loseCount(0) {
}

void BattleSixAutoBattle::StartBattle() {
    m_isInBattle = true;
    m_currentSpiritIndex = 0;
    m_currentSkillIndex = 0;
    g_battleSixBattleSession.fetch_add(1);
    g_battleSixRoundToken = 0;
    g_battleSixRoundResultToken = 0;
    g_battleSixPlayOverToken = 0;
    g_battleSixPostSettlementEndSent = false;
    g_battleSixSwitchTargetId = -1;
    g_battleSixSwitchRetryCount = 0;
    UIBridge::Instance().UpdateHelperText(L"战斗开始");
}

void BattleSixAutoBattle::EndBattle() {
    m_isInBattle = false;
    m_mySpirits.clear();
    m_enemySpirits.clear();
    m_currentSpiritIndex = -1;
    m_enemySid = 0;
    m_enemyUniqueId = 0;
    m_myUniqueId = 0;
    g_battleSixBattleSession.fetch_add(1);
    g_battleSixSwitchTargetId = -1;
    g_battleSixSwitchRetryCount = 0;
    UIBridge::Instance().UpdateHelperText(L"战斗结束");
}

void BattleSixAutoBattle::UpdateMySpiritHP(int spiritSid, int hp) {
    for (auto& spirit : m_mySpirits) {
        if (spirit.sid == spiritSid) {
            spirit.hp = hp;
            spirit.isDead = (hp <= 0);
            break;
        }
    }
}

void BattleSixAutoBattle::UpdateEnemySpiritHP(int spiritSid, int hp) {
    for (auto& spirit : m_enemySpirits) {
        if (spirit.sid == spiritSid) {
            spirit.hp = hp;
            spirit.isDead = (hp <= 0);
            break;
        }
    }

    RefreshEnemyTarget();
}

bool BattleSixAutoBattle::IsMySpiritBySid(int sid) const {
    for (const auto& spirit : m_mySpirits) {
        if (spirit.sid == sid) {
            return true;
        }
    }
    return false;
}

void BattleSixAutoBattle::RefreshEnemyTarget() {
    m_enemySid = 0;
    m_enemyUniqueId = 0;

    for (const auto& spirit : m_enemySpirits) {
        if (!spirit.isDead && spirit.hp > 0) {
            m_enemySid = spirit.sid;
            m_enemyUniqueId = spirit.uniqueId;
            break;
        }
    }
}

int BattleSixAutoBattle::FindNextAliveSpirit(int currentIndex) {
    for (int i = 0; i < (int)m_mySpirits.size(); i++) {
        int index = (currentIndex + i) % m_mySpirits.size();
        if (!m_mySpirits[index].isDead && m_mySpirits[index].hp > 0) {
            return index;
        }
    }
    return -1;  // 所有精灵都已死亡
}

BOOL BattleSixAutoBattle::OnBattleRoundStart() {
    if (!m_autoBattleEnabled) {
        return FALSE;
    }

    const unsigned long long battleSession = g_battleSixBattleSession.load();
    
    // 检查当前精灵是否死亡
    if (m_currentSpiritIndex >= 0 && m_currentSpiritIndex < (int)m_mySpirits.size()) {
        int currentHp = m_mySpirits[m_currentSpiritIndex].hp;
        bool isDead = m_mySpirits[m_currentSpiritIndex].isDead;
        
        if (isDead || currentHp <= 0) {
            // 尝试切换精灵
            if (!AutoSwitchSpirit()) {
                return FALSE;
            }
            return TRUE;
        }
    }
    
    // 选择最佳技能
    int skillIndex = SelectBestSkill();
    if (skillIndex >= 0) {
        const auto& skill = m_mySpirits[m_currentSpiritIndex].skills[skillIndex];
        
        // 技能攻击目标使用敌方当前出战精灵sid，切换精灵才使用uniqueId
        int targetId = g_battleSixAuto.GetEnemySid();

        if (targetId <= 0) {
            RefreshEnemyTarget();
            targetId = g_battleSixAuto.GetEnemySid();
        }

        if (targetId <= 0) {
            return FALSE;
        }
        
        // 使用异步线程发送技能封包
        struct SkillThreadData {
            unsigned long long battleSession;
            unsigned long long roundToken;
            unsigned long long roundResultToken;
            int skillId;
            int targetId;
        };
        SkillThreadData* threadData = new SkillThreadData{
            battleSession,
            g_battleSixRoundToken.load(),
            g_battleSixRoundResultToken.load(),
            skill.skillId,
            targetId
        };
        
        HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
            auto* data = static_cast<SkillThreadData*>(lpParam);
            Sleep(600);
            if (g_battleSixBattleSession.load() != data->battleSession || !g_battleSixAuto.IsInBattle()) {
                delete data;
                return 0;
            }
            SendBattleSixUserOpPacket(0, data->targetId, data->skillId);

            Sleep(2000);
            if (g_battleSixBattleSession.load() != data->battleSession || !g_battleSixAuto.IsInBattle()) {
                delete data;
                return 0;
            }
            if (g_battleSixRoundToken.load() != data->roundToken) {
                delete data;
                return 0;
            }
            if (g_battleSixRoundResultToken.load() != data->roundResultToken) {
                delete data;
                return 0;
            }

            SendBattleSixUserOpPacket(0, data->targetId, data->skillId);
            delete data;
            return 0;
        }, threadData, 0, nullptr);
        
        if (hThread) {
            CloseHandle(hThread);
            return TRUE;
        }
        delete threadData;
        return FALSE;
    }
    
    return FALSE;
}

void BattleSixAutoBattle::OnBattleRoundResult(const GamePacket& packet) {
    // packet.params 表示 cmdType
    // 0 = 普通攻击, 1 = 切换宠物, 2 = 使用道具, 3 = 逃跑
    int cmdType = static_cast<int>(packet.params);
    size_t offset = 0;
    
    if (packet.body.size() < 4) {
        return;
    }
    
    if (cmdType == 1) {
        // 切换宠物响应
        if (packet.body.size() < 56) {
            return;
        }
        
        int sid = ReadInt32LE(packet.body.data(), offset);
        int uniqueId = ReadInt32LE(packet.body.data(), offset);
        int sta = ReadInt32LE(packet.body.data(), offset);
        
        if (sta == 1 && offset + 44 <= packet.body.size()) {
            // 读取新宠物数据
            offset += 4; // skip state
            offset += 4; // skip sid
            offset += 4; // skip groupType
            int hp = ReadInt32LE(packet.body.data(), offset);
            int maxHp = ReadInt32LE(packet.body.data(), offset);
            int level = ReadInt32LE(packet.body.data(), offset);
            int elem = ReadInt32LE(packet.body.data(), offset);
            int spiritId = ReadInt32LE(packet.body.data(), offset);
            int newUniqueId = ReadInt32LE(packet.body.data(), offset);
            offset += 4; // skip userid
            int skillNum = ReadInt32LE(packet.body.data(), offset);
            
            // 更新当前宠物数据
            for (size_t i = 0; i < m_mySpirits.size(); i++) {
                if (m_mySpirits[i].uniqueId == newUniqueId) {
                    m_currentSpiritIndex = static_cast<int>(i);
                    m_myUniqueId = newUniqueId;
                    m_mySpirits[i].sid = sid;
                    m_mySpirits[i].hp = hp;
                    m_mySpirits[i].maxHp = maxHp;
                    m_mySpirits[i].level = level;
                    m_mySpirits[i].element = elem;
                    m_mySpirits[i].spiritId = spiritId;
                    m_mySpirits[i].isDead = (hp <= 0);
                    
                    // 更新技能
                    m_mySpirits[i].skills.clear();
                    for (int j = 0; j < skillNum && offset + 12 <= packet.body.size(); j++) {
                        BattleSixSkillInfo skill;
                        skill.skillId = ReadInt32LE(packet.body.data(), offset);
                        skill.currentPP = ReadInt32LE(packet.body.data(), offset);
                        skill.maxPP = ReadInt32LE(packet.body.data(), offset);
                        auto it = g_skillNames.find(skill.skillId);
                        if (it != g_skillNames.end()) skill.name = it->second;
                        auto powerIt = g_skillPowers.find(skill.skillId);
                        skill.power = (powerIt != g_skillPowers.end()) ? powerIt->second : 0;
                        skill.available = (skill.currentPP > 0);
                        m_mySpirits[i].skills.push_back(skill);
                    }
                    
                    wchar_t msg[256];
                    swprintf_s(msg, L"万妖盛会：新宠物上场[%d] %s", spiritId, 
                               m_mySpirits[i].name.empty() ? L"未知" : m_mySpirits[i].name.c_str());
                    UIBridge::Instance().UpdateHelperText(msg);
                    break;
                }
            }

            if (m_currentSpiritIndex >= 0 && m_currentSpiritIndex < (int)m_mySpirits.size() &&
                m_mySpirits[m_currentSpiritIndex].uniqueId == newUniqueId) {
                g_battleSixSwitchTargetId = -1;
                g_battleSixSwitchRetryCount = 0;
            }

            int newSid = sid;
            for (size_t i = 0; i < m_enemySpirits.size(); i++) {
                if (m_enemySpirits[i].uniqueId == uniqueId) {
                    m_enemySpirits[i].sid = newSid;
                    m_enemySpirits[i].hp = hp;
                    m_enemySpirits[i].maxHp = maxHp;
                    m_enemySpirits[i].level = level;
                    m_enemySpirits[i].spiritId = spiritId;
                    m_enemySpirits[i].isDead = (hp <= 0);
                    RefreshEnemyTarget();
                    break;
                }
            }
        }
    } else if (cmdType == 0) {
        // 普通攻击结果
        int haveBattle = ReadInt32LE(packet.body.data(), offset);
        
            if (haveBattle == 1) {
                int atkId = ReadInt32LE(packet.body.data(), offset);
                int skillId = ReadInt32LE(packet.body.data(), offset);
                int defId = ReadInt32LE(packet.body.data(), offset);
                int miss = ReadInt32LE(packet.body.data(), offset);

                // 对照 AS3/BattleModel：skillid 在 miss 之前就已确认读取，
                // 本轮是否命中只影响后续伤害字段，不影响“技能已经被使用”这一事实。
                // 因此 PP 扣减必须在 miss 分支之外执行，否则高威力技能打空后本地 PP 会滞后。
                const bool isAtkMy = IsMySpiritBySid(atkId);
                if (isAtkMy) {
                    AdjustBattleSixSpiritSkillPP(m_mySpirits, atkId, skillId, -1);
                }
            
            if (miss == 0) {
                int brust = ReadInt32LE(packet.body.data(), offset);
                int atkHp = ReadInt32LE(packet.body.data(), offset);
                int defHp = ReadInt32LE(packet.body.data(), offset);
                int reboundHp = ReadInt32LE(packet.body.data(), offset);
                
                // 更新攻击者和防御者的HP
                // 关键：atkHp和defHp的含义取决于谁是攻击者
                // 当我方攻击时：atkHp是我方当前出战妖怪的HP，defHp是敌方当前出战妖怪的HP
                // 当敌方攻击时：atkHp是敌方当前出战妖怪的HP，defHp是我方当前出战妖怪的HP
                
                if (isAtkMy) {
                    // 我方攻击：更新我方攻击者和敌方防御者
                    UpdateMySpiritHP(atkId, atkHp);
                    UpdateEnemySpiritHP(defId, defHp);
                } else {
                    // 敌方攻击：更新敌方攻击者和我方防御者
                    UpdateEnemySpiritHP(atkId, atkHp);
                    UpdateMySpiritHP(defId, defHp);
                }

                RefreshEnemyTarget();
            }
        }
    }
}

BOOL BattleSixAutoBattle::AutoSwitchSpirit() {
    const int pendingTargetId = g_battleSixSwitchTargetId.load();
    if (pendingTargetId > 0) {
        if (m_currentSpiritIndex >= 0 && m_currentSpiritIndex < (int)m_mySpirits.size() &&
            m_mySpirits[m_currentSpiritIndex].uniqueId == pendingTargetId) {
            g_battleSixSwitchTargetId = -1;
            g_battleSixSwitchRetryCount = 0;
        } else {
            return TRUE;
        }
    }

    const int startIndex = (m_currentSpiritIndex >= 0) ? (m_currentSpiritIndex + 1) : 0;
    int nextIndex = FindNextAliveSpirit(startIndex);

    if (nextIndex < 0 || nextIndex == m_currentSpiritIndex) {
        return FALSE;
    }

    const int spiritUniqueId = m_mySpirits[nextIndex].uniqueId;
    if (spiritUniqueId <= 0) {
        return FALSE;
    }

    struct SwitchThreadData {
        unsigned long long battleSession;
        int uniqueId;
    };

    const unsigned long long battleSession = g_battleSixBattleSession.load();
    SwitchThreadData* threadData = new SwitchThreadData{battleSession, spiritUniqueId};
    g_battleSixSwitchTargetId = spiritUniqueId;
    g_battleSixSwitchRetryCount = 0;

    HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
        auto* data = static_cast<SwitchThreadData*>(lpParam);
        Sleep(300);
        if (g_battleSixBattleSession.load() != data->battleSession || !g_battleSixAuto.IsInBattle()) {
            delete data;
            return 0;
        }
        SendBattleSixUserOpPacket(1, data->uniqueId, 0);
        delete data;
        return 0;
    }, threadData, 0, nullptr);

    if (hThread) {
        CloseHandle(hThread);
        return TRUE;
    }

    delete threadData;
    g_battleSixSwitchTargetId = -1;
    g_battleSixSwitchRetryCount = 0;
    return FALSE;
}

int BattleSixAutoBattle::GetAliveSpiritCount() {
    int count = 0;
    for (const auto& spirit : m_mySpirits) {
        if (!spirit.isDead && spirit.hp > 0) {
            count++;
        }
    }
    return count;
}

int BattleSixAutoBattle::GetEnemyAliveSpiritCount() {
    int count = 0;
    for (const auto& spirit : m_enemySpirits) {
        if (!spirit.isDead && spirit.hp > 0) {
            count++;
        }
    }
    return count;
}

int BattleSixAutoBattle::SelectBestSkill() {
    if (m_currentSpiritIndex < 0 || m_currentSpiritIndex >= (int)m_mySpirits.size()) {
        return -1;
    }
    
    auto& spirit = m_mySpirits[m_currentSpiritIndex];

    // 选技前用通用战斗层的当前实时 PP 同步本地缓存。
    // 通用解析会处理 PP 扣减以及 BufType 3/4/6 这类 PP 变化，本地 BattleSix 只维护最小状态，
    // 因此最终决策应以当前出战妖怪的通用战斗数据为准。
    BattleData& battleData = PacketParser::GetCurrentBattle();
    if (battleData.myActiveIndex >= 0 &&
        battleData.myActiveIndex < static_cast<int>(battleData.myPets.size())) {
        const auto& activePet = battleData.myPets[battleData.myActiveIndex];
        if (activePet.uniqueId == spirit.uniqueId) {
            for (auto& localSkill : spirit.skills) {
                for (const auto& battleSkill : activePet.skills) {
                    if (battleSkill.id == static_cast<uint32_t>(localSkill.skillId)) {
                        localSkill.currentPP = battleSkill.pp;
                        localSkill.maxPP = battleSkill.maxPp;
                        localSkill.available = (battleSkill.pp > 0);
                        break;
                    }
                }
            }
        }
    }

    std::vector<int> availableSkillIndices;
    int bestPower = 0;

    for (int i = 0; i < static_cast<int>(spirit.skills.size()); ++i) {
        const auto& skill = spirit.skills[i];
        if (!skill.available || skill.currentPP <= 0) {
            continue;
        }

        if (availableSkillIndices.empty() || skill.power > bestPower) {
            availableSkillIndices.clear();
            availableSkillIndices.push_back(i);
            bestPower = skill.power;
        } else if (skill.power == bestPower) {
            availableSkillIndices.push_back(i);
        }
    }

    if (availableSkillIndices.empty()) {
        return -1;
    }

    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, static_cast<int>(availableSkillIndices.size()) - 1);
    return availableSkillIndices[dist(rng)];
}

// 万妖盛会封包发送函数实现
BOOL SendBattleSixCombatInfoPacket() {
    std::vector<uint8_t> body;
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::BATTLESIX_COMBAT_INFO_SEND)
        .SetParams(0)
        .WriteBytes(body)
        .Build();
    
    return SendPacket(0, packet.data(), packet.size());
}

static BOOL SendBattleSixMatchPacketOnce() {
    std::vector<uint8_t> body;
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::BATTLESIX_MATCH_SEND)
        .SetParams(0)
        .WriteBytes(body)
        .Build();

    return SendPacket(0, packet.data(), packet.size());
}

static void StartBattleSixMatchRetryLoop(unsigned long long token) {
    struct MatchRetryContext {
        unsigned long long token;
    };

    auto* context = new MatchRetryContext{token};
    HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID param) -> DWORD {
        std::unique_ptr<MatchRetryContext> context(static_cast<MatchRetryContext*>(param));
        while (true) {
            Sleep(1000);

            if (!g_battleSixAuto.IsAutoMatching() || g_battleSixAuto.IsInBattle()) {
                return 0;
            }

            if (g_battleSixFlowToken.load() != context->token ||
                g_battleSixFlowStage.load() != BATTLESIX_FLOW_MATCHING ||
                !g_battleSixMatching.load()) {
                return 0;
            }

            SendBattleSixMatchPacketOnce();
        }
    }, context, 0, nullptr);

    if (hThread) {
        CloseHandle(hThread);
    } else {
        delete context;
    }
}

BOOL SendBattleSixMatchPacket() {
    g_battleSixMatching = true;
    g_battleSixMatchSuccess = false;
    const unsigned long long token = AdvanceBattleSixFlowStage(BATTLESIX_FLOW_MATCHING);
    ArmBattleSixFlowWatchdog(
        45000,
        BATTLESIX_FLOW_MATCHING,
        token,
        true,
        L"万妖盛会：匹配超时，取消后重试...");

    const BOOL sent = SendBattleSixMatchPacketOnce();
    StartBattleSixMatchRetryLoop(token);
    return sent;
}

BOOL SendBattleSixCancelMatchPacket() {
    std::vector<uint8_t> body;
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::BATTLESIX_CANCEL_MATCH_SEND)
        .SetParams(0)
        .WriteBytes(body)
        .Build();
    
    ResetBattleSixFlowState();
    
    return SendPacket(0, packet.data(), packet.size());
}

BOOL SendBattleSixReqStartPacket() {
    std::vector<uint8_t> body;
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::BATTLESIX_REQ_START_SEND)
        .SetParams(0)
        .WriteBytes(body)
        .Build();

    g_battleSixReadySupplementSent = false;
    return SendPacket(0, packet.data(), packet.size());
}

BOOL SendBattleSixUserOpPacket(int opType, int param1, int param2) {
    // 根据AS3 BattleControl.as代码：
    // opType=0 (技能攻击): sendMessage(opcode, 0, [obj.target, obj.skillId])
    // opType=1 (切换精灵): sendMessage(opcode, 1, [obj.spiritid])
    // opType=2 (使用道具): sendMessage(opcode, 2, [obj.packcode, obj.position, obj.sid])
    // opType=3 (逃跑): sendMessage(opcode, 3) - 无Body
    
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::BATTLESIX_USER_OP_SEND)
        .SetParams(opType);
    
    if (opType == 0) {
        // 技能攻击: Body = [target, skillId]
        packet.WriteInt32(param1);  // target
        packet.WriteInt32(param2);  // skillId
    } else if (opType == 1) {
        // 切换精灵: Body = [spiritid]
        packet.WriteInt32(param1);  // spiritUniqueId
    }
    // opType=3 逃跑无需Body
    
    auto finalPacket = packet.Build();
    
    return SendPacket(0, finalPacket.data(), finalPacket.size());
}

BOOL SendBattleSixEndPacket() {
    std::vector<uint8_t> body;
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::BATTLESIX_USER_OP_SEND)
        .SetParams(4)  // 结束战斗
        .WriteBytes(body)
        .Build();
    
    return SendPacket(0, packet.data(), packet.size());
}

static BOOL SendBattleReadyPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::BATTLE_READY)
        .SetParams(0)
        .Build();

    return SendPacket(0, packet.data(), packet.size());
}

static BOOL SendBattlePlayOverPacket() {
    g_battleSixPlayOverToken.fetch_add(1);

    auto packet = PacketBuilder()
        .SetOpcode(Opcode::BATTLE_PLAY_OVER)
        .SetParams(0)
        .Build();

    return SendPacket(0, packet.data(), packet.size());
}

// 万妖盛会响应处理函数实现
void ProcessBattleSixMatchResponse(const GamePacket& packet) {
    if (packet.params != 0) {
        g_battleSixMatchSuccess = false;
        g_battleSixMatching = false;
        ResetBattleSixFlowState();

        wchar_t msg[256];
        swprintf_s(msg, L"万妖盛会：匹配失败，错误码 %u", packet.params);
        UIBridge::Instance().UpdateHelperText(msg);
        return;
    }

    if (packet.body.size() < 4) {
        return;
    }

    size_t offset = 0;
    int opponentId = ReadInt32LE(packet.body.data(), offset);

    g_battleSixMatchSuccess = true;
    g_battleSixMatching = false;
    const unsigned long long token = AdvanceBattleSixFlowStage(BATTLESIX_FLOW_WAITING_BATTLE_START);

    wchar_t msg[256];
    swprintf_s(msg, L"万妖盛会：匹配成功，对手ID: %d", opponentId);
    UIBridge::Instance().UpdateHelperText(msg);
    ArmBattleSixFlowWatchdog(
        60000,
        BATTLESIX_FLOW_WAITING_BATTLE_START,
        token,
        false,
        L"万妖盛会：匹配成功但未正常进入战斗，准备重试...");

    // 异步等待后请求开始战斗，避免UI卡顿
    HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        Sleep(2000);
        SendBattleSixReqStartPacket();
        return 0;
    }, nullptr, 0, nullptr);
    if (hThread) CloseHandle(hThread);
}

void ProcessBattleSixPrepareCombatResponse(const GamePacket& packet) {
    const unsigned long long token = AdvanceBattleSixFlowStage(BATTLESIX_FLOW_PREPARING_COMBAT);
    UIBridge::Instance().UpdateHelperText(L"万妖盛会：准备战斗");
    ArmBattleSixFlowWatchdog(
        60000,
        BATTLESIX_FLOW_PREPARING_COMBAT,
        token,
        false,
        L"万妖盛会：准备战斗超时，重新匹配...");
}

void ProcessBattleSixReqStartResponse(const GamePacket& packet) {
    // 请求开始战斗的响应处理
    UIBridge::Instance().UpdateHelperText(L"万妖盛会：已请求开始战斗");
}

void ProcessBattleSixCombatInfoResponse(const GamePacket& packet) {
    // 查询战斗信息响应
    UIBridge::Instance().UpdateHelperText(L"万妖盛会：已打开界面");
    
    // 检查是否需要自动开始匹配
    // 注意：不要在这里清除 m_autoMatching 标志，它应该在所有匹配完成后才清除
    if (g_battleSixAuto.IsAutoMatching() &&
        !g_battleSixAuto.IsInBattle() &&
        g_battleSixFlowStage.load() == BATTLESIX_FLOW_IDLE &&
        !g_battleSixMatching.load()) {
        // 异步延迟1秒后开始匹配，避免UI卡顿
        HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            Sleep(1000);
            if (!g_battleSixAuto.IsAutoMatching() || g_battleSixAuto.IsInBattle()) {
                return 0;
            }
            if (g_battleSixFlowStage.load() != BATTLESIX_FLOW_IDLE || g_battleSixMatching.load()) {
                return 0;
            }
            SendBattleSixMatchPacket();
            return 0;
        }, nullptr, 0, nullptr);
        if (hThread) CloseHandle(hThread);
    }
}

void ProcessBattleSixBattleRoundResultResponse(const GamePacket& packet) {
    g_battleSixRoundResultToken.fetch_add(1);
    g_battleSixAuto.OnBattleRoundResult(packet);

    if (!g_battleSixAuto.IsInBattle()) {
        return;
    }

    const unsigned long long battleSession = g_battleSixBattleSession.load();
    const unsigned long long roundToken = g_battleSixRoundToken.load();
    const unsigned long long playOverToken = g_battleSixPlayOverToken.load();

    struct BattlePlayOverThreadData {
        unsigned long long battleSession;
        unsigned long long roundToken;
        unsigned long long playOverToken;
    };

    BattlePlayOverThreadData* threadData =
        new BattlePlayOverThreadData{battleSession, roundToken, playOverToken};

    HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
        std::unique_ptr<BattlePlayOverThreadData> data(
            static_cast<BattlePlayOverThreadData*>(lpParam));
        Sleep(1200);

        if (g_battleSixBattleSession.load() != data->battleSession || !g_battleSixAuto.IsInBattle()) {
            return 0;
        }
        if (g_battleSixRoundToken.load() != data->roundToken) {
            return 0;
        }
        if (g_battleSixPlayOverToken.load() != data->playOverToken) {
            return 0;
        }

        SendBattlePlayOverPacket();
        return 0;
    }, threadData, 0, nullptr);

    if (hThread) {
        CloseHandle(hThread);
    } else {
        delete threadData;
    }
}

void ProcessBattleSixBattleEndResponse(const GamePacket& packet) {
    // 记录战斗结果
    bool wasInBattle = g_battleSixAuto.IsInBattle();
    bool wasAutoMatching = g_battleSixAuto.IsAutoMatching();
    
    // 计算胜负
    int myAlive = g_battleSixAuto.GetAliveSpiritCount();
    bool isWin = (myAlive > 0);
    if (isWin) {
        g_battleSixAuto.IncrementWinCount();
    } else {
        g_battleSixAuto.IncrementLoseCount();
    }
    
    // 结束战斗
    g_battleSixAuto.EndBattle();
    
    // 只有万妖盛会自动匹配模式才显示特定提示
    if (wasAutoMatching) {
        wchar_t msg[256];
        if (isWin) {
            swprintf_s(msg, L"万妖盛会：战斗胜利");
        } else {
            swprintf_s(msg, L"万妖盛会：战斗失败，所有精灵已阵亡");
        }
        UIBridge::Instance().UpdateHelperText(msg);
    }
    
    // 确认战斗结束
    SendBattleSixEndPacket();
    
    // 检查是否需要继续匹配（仅万妖盛会模式）
    if (wasAutoMatching && g_battleSixAuto.GetMatchCount() > 0) {
        g_battleSixAuto.DecrementMatchCount();
        
        // 显示剩余次数
        int remaining = g_battleSixAuto.GetMatchCount();
        int total = g_battleSixAuto.GetTotalMatchCount();
        int wins = g_battleSixAuto.GetWinCount();
        int loses = g_battleSixAuto.GetLoseCount();
        
        wchar_t continueMsg[256];
        swprintf_s(continueMsg, L"万妖盛会：剩余%d次 (胜%d 负%d)", remaining, wins, loses);
        UIBridge::Instance().UpdateHelperText(continueMsg);
        
        if (remaining > 0) {
            // 异步延迟后开始下一轮匹配
            const unsigned long long battleSession = g_battleSixBattleSession.load();
            struct NextMatchThreadData {
                unsigned long long battleSession;
            };
            NextMatchThreadData* threadData = new NextMatchThreadData{battleSession};
            HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
                std::unique_ptr<NextMatchThreadData> data(static_cast<NextMatchThreadData*>(lpParam));
                Sleep(2000);  // 等待2秒
                if (g_battleSixBattleSession.load() == data->battleSession && !g_battleSixAuto.IsInBattle()) {
                    SendBattleSixMatchPacket();
                }
                return 0;
            }, threadData, 0, nullptr);
            if (hThread) {
                CloseHandle(hThread);
            } else {
                delete threadData;
            }
        } else {
            // 所有匹配完成，显示总结
            wchar_t summaryMsg[256];
            swprintf_s(summaryMsg, L"万妖盛会：完成%d次匹配 (胜%d 负%d)", total, wins, loses);
            UIBridge::Instance().UpdateHelperText(summaryMsg);
            g_battleSixAuto.SetAutoMatching(false);
        }
    }
}

// 万妖盛会一键功能实现
BOOL StartOneKeyBattleSixPacket(int matchCount) {
    // 启用自动战斗
    g_battleSixAuto.SetAutoBattle(true);
    
    // 设置自动匹配标志和匹配次数
    ResetBattleSixFlowState();
    g_battleSixAuto.SetAutoMatching(true);
    g_battleSixAuto.SetMatchCount(matchCount);
    
    // 先发送查询战斗信息封包（打开界面）
    return SendBattleSixCombatInfoPacket();
}

BOOL CancelBattleSixMatch() {
    // 取消匹配
    return SendBattleSixCancelMatchPacket();
}

// ============================================================================
// 精魄赠送系统功能实现 (SpiritCollect)
// ============================================================================

// 精魄系统全局状态
SpiritCollectState g_spiritCollectState;

/** 获取精魄列表 */
BOOL SendSpiritPresuresPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(SpiritCollect::PRESURES_SEND)
        .SetParams(0)
        .Build();
    return SendPacket(g_LastGameSocket, packet.data(), static_cast<DWORD>(packet.size()));
}

/** 验证玩家信息（通过卡布号） */
BOOL SendSpiritPlayerInfoPacket(uint32_t friendId) {
    auto packet = PacketBuilder()
        .SetOpcode(SpiritCollect::PLAYER_INFO_SEND)
        .SetParams(friendId)
        .Build();
    return SendPacket(g_LastGameSocket, packet.data(), static_cast<DWORD>(packet.size()));
}

/** 发送精魄给指定玩家 */
BOOL SendSpiritGiftPacket(uint32_t friendId, uint32_t eggId) {
    auto packet = PacketBuilder()
        .SetOpcode(SpiritCollect::SEND_SPIRIT_SEND)
        .SetParams(friendId)
        .WriteInt32(eggId)
        .Build();
    return SendPacket(g_LastGameSocket, packet.data(), static_cast<DWORD>(packet.size()));
}

/** 打开精魄系统 - 发送 open_ui 请求 */
BOOL SendSpiritOpenUIPacket() {
    auto packet = BuildActivityPacket(
        SpiritCollect::COLLECT_SEND,
        SpiritCollect::ACT_ID,
        "open_ui",
        {}
    );
    return SendPacket(g_LastGameSocket, packet.data(), static_cast<DWORD>(packet.size()));
}

/** 获取历史记录 */
BOOL SendSpiritHistoryPacket(int type) {
    auto packet = BuildActivityPacket(
        SpiritCollect::COLLECT_SEND,
        SpiritCollect::ACT_ID,
        "history",
        {type}
    );
    return SendPacket(g_LastGameSocket, packet.data(), static_cast<DWORD>(packet.size()));
}

// ============================================================================
// 副本跳层功能实现 (DungeonJump)
// ============================================================================

// 副本跳层全局状态
DungeonJumpState g_dungeonJumpState;

// 外部变量声明（来自packet_parser.cpp）
extern MonsterData g_monsterData;

BOOL SendDungeonJumpLayerPacket(int layer) {
    if (layer < 1 || layer > 9999) {
        return FALSE;
    }
    
    // 构造跳层封包: Opcode=1186180, Params=0, Body=[layer(4B小端序)]
    auto packet = PacketBuilder()
        .SetOpcode(DungeonJump::JUMP_LAYER_SEND)
        .SetParams(0)
        .WriteInt32(layer)
        .Build();
    
    BOOL result = SendPacket(g_LastGameSocket, packet.data(), static_cast<DWORD>(packet.size()),
                              DungeonJump::JUMP_LAYER_BACK, 3000);
    
    if (result) {
        wchar_t msg[256];
        swprintf_s(msg, L"副本跳层：发送跳层请求到第%d层", layer);
        UIBridge::Instance().UpdateHelperText(msg);
    }
    
    return result;
}

BOOL SendQueryDungeonInfoPacket() {
    // 构造查询副本信息封包: Opcode=1184317, Params=0, Body=[]
    auto packet = PacketBuilder()
        .SetOpcode(DungeonJump::QUERY_DUNGEON_INFO)
        .SetParams(0)
        .Build();
    
    return SendPacket(g_LastGameSocket, packet.data(), static_cast<DWORD>(packet.size()),
                      DungeonJump::QUERY_DUNGEON_BACK, 3000);
}

BOOL SendPrepareBattlePacket() {
    // 构造准备战斗封包: Opcode=1184782, Params=0, Body=[]
    auto packet = PacketBuilder()
        .SetOpcode(DungeonJump::PREPARE_BATTLE)
        .SetParams(0)
        .Build();
    
    return SendPacket(g_LastGameSocket, packet.data(), static_cast<DWORD>(packet.size()),
                      DungeonJump::PREPARE_BATTLE_BACK, 3000);
}

BOOL SendDungeonActivityPacket(int activityId, int opType, int param) {
    // 构造副本活动操作封包: Opcode=1184833, Params=activityId, Body=[opType, param]
    auto packet = PacketBuilder()
        .SetOpcode(DungeonJump::DUNGEON_ACTIVITY_SEND)
        .SetParams(activityId)
        .WriteInt32(opType)
        .WriteInt32(param)
        .Build();
    
    return SendPacket(g_LastGameSocket, packet.data(), static_cast<DWORD>(packet.size()),
                      DungeonJump::ACTIVITY_BACK, 3000, static_cast<uint32_t>(activityId), true);
}

BOOL SendCompleteJumpPacket() {
    // 构造完成跳层封包: Opcode=1184771, Params=19012, Body=[]
    auto packet = PacketBuilder()
        .SetOpcode(DungeonJump::COMPLETE_JUMP)
        .SetParams(19012)
        .Build();
    
    BOOL result = SendPacket(g_LastGameSocket, packet.data(), static_cast<DWORD>(packet.size()),
                             DungeonJump::COMPLETE_JUMP_BACK, 3000);
    
    if (result) {
        UIBridge::Instance().UpdateHelperText(L"副本跳层：完成跳层操作");
    }
    
    return result;
}

BOOL SendPutSpiritToStorePacket(int spiritId) {
    // 构造存入妖怪仓库封包: Opcode=1187333, Params=spiritId, Body=[4, 0]
    auto packet = PacketBuilder()
        .SetOpcode(DungeonJump::PUT_SPIRIT_STORE)
        .SetParams(spiritId)
        .WriteInt32(4)
        .WriteInt32(0)
        .Build();
    
    return SendPacket(g_LastGameSocket, packet.data(), static_cast<DWORD>(packet.size()));
}

BOOL SendGetSpiritFromStorePacket(int spiritId) {
    // 构造取出妖怪仓库封包: Opcode=1187333, Params=spiritId, Body=[0, 4]
    auto packet = PacketBuilder()
        .SetOpcode(DungeonJump::PUT_SPIRIT_STORE)
        .SetParams(spiritId)
        .WriteInt32(0)
        .WriteInt32(4)
        .Build();
    
    return SendPacket(g_LastGameSocket, packet.data(), static_cast<DWORD>(packet.size()));
}

BOOL SendSetFirstSpiritPacket(int spiritId) {
    // 构造设置首发妖怪封包: Opcode=1187344, Params=0, Body=[spiritId]
    auto packet = PacketBuilder()
        .SetOpcode(DungeonJump::SET_FIRST_SPIRIT)
        .SetParams(0)
        .WriteInt32(spiritId)
        .Build();
    
    return SendPacket(g_LastGameSocket, packet.data(), static_cast<DWORD>(packet.size()));
}

std::vector<MonsterItem> FilterHighLevelMonsters(const std::vector<MonsterItem>& monsters) {
    std::vector<MonsterItem> result;
    for (const auto& monster : monsters) {
        if (monster.level > 50) {
            result.push_back(monster);
        }
    }
    return result;
}

int FindFirstSpiritId(const std::vector<MonsterItem>& monsters) {
    for (const auto& monster : monsters) {
        if (monster.isfirst == 1) {
            return monster.id;
        }
    }
    return 0;
}

// 副本跳层异步线程
static DWORD WINAPI DungeonJumpThreadProc(LPVOID lpParam) {
    int targetLayer = g_dungeonJumpState.targetLayer;
    
    // 跳层开始
    UIBridge::Instance().UpdateHelperText(L"跳层开始");
    
    // 查询妖怪背包
    g_dungeonJumpState.monsterDataReceived = false;
    SendQueryMonsterPacket();
    
    // 等待妖怪背包数据响应（最多等待3秒）
    int waitCount = 0;
    const int maxWaitCount = 30;
    while (!g_dungeonJumpState.monsterDataReceived && waitCount < maxWaitCount) {
        Sleep(100);
        waitCount++;
    }
    
    if (!g_dungeonJumpState.monsterDataReceived) {
        g_dungeonJumpState.Reset();
        return 1;
    }
    
    // 获取妖怪数据
    MonsterData& monsterData = g_monsterData;
    if (monsterData.monsters.empty()) {
        g_dungeonJumpState.Reset();
        return 1;
    }
    
    // 筛选高等级妖怪并记录首发
    g_dungeonJumpState.highLevelMonsters = FilterHighLevelMonsters(monsterData.monsters);
    g_dungeonJumpState.originalFirstSpiritId = FindFirstSpiritId(monsterData.monsters);
    
    // 存入高等级妖怪（无提示）
    if (!g_dungeonJumpState.highLevelMonsters.empty()) {
        for (const auto& monster : g_dungeonJumpState.highLevelMonsters) {
            if (!g_dungeonJumpState.isRunning) {
                return 1;
            }
            
            if (SendPutSpiritToStorePacket(monster.id)) {
                g_dungeonJumpState.storedMonsterCount++;
                Sleep(200);
            }
        }
    }
    
    // ========== 执行完整跳层流程（使用ResponseWaiter等待响应） ==========
    
    // 1. 发送跳层封包，等待返回 1317252
    if (!SendDungeonJumpLayerPacket(targetLayer)) {
        goto CLEANUP;
    }
    
    if (!g_dungeonJumpState.isRunning) goto CLEANUP;
    
    // 2. 进入地图，等待返回 1315395 (使用项目中已有的 Opcode::ENTER_SCENE_BACK)
    if (!SendEnterScenePacket(DungeonJump::DUNGEON_MAP_ID, Opcode::ENTER_SCENE_BACK, 5000)) {
        goto CLEANUP;
    }
    
    if (!g_dungeonJumpState.isRunning) goto CLEANUP;
    
    // 3. 查询副本信息，等待返回 1315086
    if (!SendQueryDungeonInfoPacket()) {
        goto CLEANUP;
    }
    
    if (!g_dungeonJumpState.isRunning) goto CLEANUP;
    
    // 4. 准备战斗，等待返回 1315854
    if (!SendPrepareBattlePacket()) {
        goto CLEANUP;
    }
    
    if (!g_dungeonJumpState.isRunning) goto CLEANUP;
    
    // 5. 活动操作900，等待返回 1324097
    if (!SendDungeonActivityPacket(DungeonJump::ACTIVITY_ID_900, 1, DungeonJump::DUNGEON_MAP_ID)) {
        goto CLEANUP;
    }
    
    if (!g_dungeonJumpState.isRunning) goto CLEANUP;
    
    // 6. 活动操作902，等待返回 1324097
    if (!SendDungeonActivityPacket(DungeonJump::ACTIVITY_ID_902, 1, DungeonJump::DUNGEON_MAP_ID)) {
        goto CLEANUP;
    }
    
    if (!g_dungeonJumpState.isRunning) goto CLEANUP;
    
    // 7. 活动操作325，等待返回 1324097
    if (!SendDungeonActivityPacket(DungeonJump::ACTIVITY_ID_325, 4, DungeonJump::DUNGEON_MAP_ID)) {
        goto CLEANUP;
    }
    
    if (!g_dungeonJumpState.isRunning) goto CLEANUP;
    
    // 8. 完成跳层，等待返回 1315843
    if (!SendCompleteJumpPacket()) {
        goto CLEANUP;
    }
    
    // 跳层完成
    UIBridge::Instance().UpdateHelperText(L"跳层完成");
    
    // 清理和恢复流程（无提示）
CLEANUP:
    // 取回妖怪（无提示）
    if (g_dungeonJumpState.storedMonsterCount > 0) {
        for (const auto& monster : g_dungeonJumpState.highLevelMonsters) {
            if (!g_dungeonJumpState.isRunning) break;
            
            if (SendGetSpiritFromStorePacket(monster.id)) {
                g_dungeonJumpState.retrievedMonsterCount++;
                Sleep(200);
            }
        }
    }
    
    // 设置首发（无提示）
    if (g_dungeonJumpState.originalFirstSpiritId > 0 && g_dungeonJumpState.isRunning) {
        SendSetFirstSpiritPacket(g_dungeonJumpState.originalFirstSpiritId);
        Sleep(200);
    }
    
    g_dungeonJumpState.Reset();
    return 0;
}

BOOL StartOneKeyDungeonJumpPacket(int targetLayer) {
    if (g_dungeonJumpState.isRunning) {
        return FALSE;
    }
    
    // 设置状态
    g_dungeonJumpState.Reset();
    g_dungeonJumpState.isRunning = true;
    g_dungeonJumpState.targetLayer = targetLayer;
    
    // 创建异步线程执行跳层流程
    HANDLE hThread = CreateThread(nullptr, 0, DungeonJumpThreadProc, nullptr, 0, nullptr);
    if (!hThread) {
        g_dungeonJumpState.Reset();
        return FALSE;
    }
    
    CloseHandle(hThread);
    return TRUE;
}

void StopDungeonJump() {
    g_dungeonJumpState.Reset();
}

void ProcessDungeonJumpResponse(const GamePacket& packet) {
    // 处理副本跳层相关响应
    // 注意：使用ResponseWaiter自动处理响应，此函数保留用于未来扩展
    if (!g_dungeonJumpState.isRunning) {
        return;
    }
    
    // ResponseWaiter会自动处理以下返回opcode：
    // - 1317252 (JUMP_LAYER_BACK)
    // - 1315074 (ENTER_SCENE_BACK)
    // - 1315086 (QUERY_DUNGEON_BACK)
    // - 1315854 (PREPARE_BATTLE_BACK)
    // - 1324097 (ACTIVITY_BACK)
    // - 1315843 (COMPLETE_JUMP_BACK)
}

// ============================================================================
// 双台谷刷级功能实现 - 异步状态机模式
// ============================================================================

// 全局状态机实例
ShuangTaiAutoBattle g_shuangtaiAuto;

// 引用妖怪数据和技能威力映射
extern MonsterData g_monsterData;
extern std::unordered_map<int, int> g_skillPowers;

// ========== 工具函数 ==========

uint32_t GetLastSpiritId() {
    if (g_monsterData.monsters.empty()) {
        return 0;
    }
    return g_monsterData.monsters.back().id;
}

uint32_t GetHighestPowerSkillId(uint32_t spiritId) {
    const MonsterItem* monster = nullptr;
    for (const auto& m : g_monsterData.monsters) {
        if (m.id == spiritId) {
            monster = &m;
            break;
        }
    }
    
    if (!monster || monster->skills.empty()) {
        return 0;
    }
    
    int maxPower = -1;
    uint32_t bestSkillId = 0;
    
    for (const auto& skill : monster->skills) {
        auto powerIt = g_skillPowers.find(skill.id);
        int power = (powerIt != g_skillPowers.end()) ? powerIt->second : 0;
        
        if (power > maxPower && power > 0) {
            maxPower = power;
            bestSkillId = skill.id;
        }
    }
    
    return bestSkillId;
}

// ========== ShuangTaiAutoBattle 类实现 ==========

ShuangTaiAutoBattle::ShuangTaiAutoBattle()
    : m_running(false)
    , m_state(ShuangTaiState::IDLE)
    , m_blockBattle(false)
    , m_petIndex(0)
    , m_attackRound(0)
    , m_mainPetId(0)
    , m_mainPetSkillId(0)
    , m_totalRounds(0) {
}

bool ShuangTaiAutoBattle::Start(bool blockBattle) {
    if (m_running) {
        UIBridge::Instance().UpdateHelperText(L"双台谷刷级已在运行中");
        return false;
    }
    
    // 检查妖怪背包数据
    if (g_monsterData.monsters.empty()) {
        UIBridge::Instance().UpdateHelperText(L"双台谷刷级：请先点击查询按钮");
        return false;
    }
    
    // 检查妖怪数量（至少需要2只）
    if (g_monsterData.monsters.size() < 2) {
        UIBridge::Instance().UpdateHelperText(L"双台谷刷级：需要至少2只妖怪");
        return false;
    }
    
    // 初始化宠物数据
    if (!InitializePetData()) {
        UIBridge::Instance().UpdateHelperText(L"双台谷刷级：无法获取主宠技能");
        return false;
    }
    
    // 设置屏蔽战斗
    m_blockBattle = blockBattle;
    if (blockBattle) {
        g_blockBattle = true;
    }
    
    // 设置运行状态
    m_running = true;
    m_totalRounds = 0;
    
    UIBridge::Instance().UpdateHelperText(L"双台谷刷级：开始运行，共 " + 
        std::to_wstring(g_monsterData.monsters.size()) + L" 只妖怪...");
    
    // 开始流程：发送初始化封包
    SendInitPacket();
    
    return true;
}

void ShuangTaiAutoBattle::RequestStop() {
    if (!m_running) return;
    
    // 设置请求停止标志，等待当前轮次完成后再停止
    m_stopRequested = true;
    UIBridge::Instance().UpdateHelperText(L"双台谷刷级：将在本轮完成后停止...");
}

void ShuangTaiAutoBattle::Stop() {
    m_running = false;
    m_stopRequested = false;
    
    // 恢复屏蔽战斗状态
    if (m_blockBattle) {
        g_blockBattle = false;
    }
    
    UpdateState(ShuangTaiState::STOPPED);
    UIBridge::Instance().UpdateHelperText(L"双台谷刷级：已停止");
}

void ShuangTaiAutoBattle::Reset() {
    m_running = false;
    m_stopRequested = false;
    m_state = ShuangTaiState::IDLE;
    m_blockBattle = false;
    m_petIndex = 0;
    m_attackRound = 0;
    m_mainPetId = 0;
    m_mainPetSkillId = 0;
    m_petIds.clear();
    m_totalRounds = 0;
}

bool ShuangTaiAutoBattle::InitializePetData() {
    // 获取最后一只妖怪作为主宠
    m_mainPetId = g_monsterData.monsters.back().id;
    
    // 获取所有妖怪ID（包括主宠，都需要切换上场获得经验）
    // 第一只已出战不需要切换，所以 m_petIds 包含第2只到最后一只（主宠）
    m_petIds.clear();
    for (size_t i = 1; i < g_monsterData.monsters.size(); i++) {
        m_petIds.push_back(g_monsterData.monsters[i].id);
    }
    
    // 获取主宠最高威力技能及其PP值
    const MonsterItem* monster = nullptr;
    for (const auto& m : g_monsterData.monsters) {
        if (m.id == m_mainPetId) {
            monster = &m;
            break;
        }
    }
    
    if (!monster || monster->skills.empty()) {
        return false;
    }
    
    // 查找威力最高的技能
    int maxPower = -1;
    int bestSkillIndex = -1;
    
    for (size_t i = 0; i < monster->skills.size(); i++) {
        const auto& skill = monster->skills[i];
        auto powerIt = g_skillPowers.find(skill.id);
        int power = (powerIt != g_skillPowers.end()) ? powerIt->second : 0;
        
        if (power > maxPower && power > 0) {
            maxPower = power;
            bestSkillIndex = static_cast<int>(i);
        }
    }
    
    if (bestSkillIndex < 0) {
        return false;
    }
    
    m_mainPetSkillId = monster->skills[bestSkillIndex].id;
    m_mainPetSkillPP = monster->skills[bestSkillIndex].maxPp;
    
    // 设置最大攻击次数为技能PP值
    m_maxAttackCount = m_mainPetSkillPP > 0 ? m_mainPetSkillPP : 8;
    
    return m_mainPetSkillId != 0;
}

void ShuangTaiAutoBattle::UpdateState(ShuangTaiState newState) {
    m_state = newState;
}

// ========== 封包发送方法 ==========

void ShuangTaiAutoBattle::SendInitPacket() {
    if (!m_running) return;
    
    UpdateState(ShuangTaiState::INITIALIZING);
    UIBridge::Instance().UpdateHelperText(L"双台谷刷级：发送初始化封包...");
    
    auto packet = PacketBuilder()
        .SetOpcode(ShuangTai::INIT_SEND)
        .SetParams(1)
        .Build();
    SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
    
    // 延迟后发送跳层封包
    struct DelayedData { ShuangTaiAutoBattle* self; };
    DelayedData* data = new DelayedData{this};
    
    HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
        auto* d = static_cast<DelayedData*>(lpParam);
        Sleep(300);
        if (d->self->IsRunning()) {
            d->self->SendJumpLayerPacket();
        }
        delete d;
        return 0;
    }, data, 0, nullptr);
    
    if (hThread) CloseHandle(hThread);
}

void ShuangTaiAutoBattle::SendJumpLayerPacket() {
    if (!m_running) return;
    
    UpdateState(ShuangTaiState::JUMPING_LAYER);
    UIBridge::Instance().UpdateHelperText(L"双台谷刷级：跳层到第一层...");
    
    auto packet = PacketBuilder()
        .SetOpcode(ShuangTai::JUMP_LAYER_SEND)
        .SetParams(2)
        .WriteInt32(1)  // 第一层
        .Build();
    SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
    
    // 延迟后发送进入战斗封包
    struct DelayedData { ShuangTaiAutoBattle* self; };
    DelayedData* data = new DelayedData{this};
    
    HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
        auto* d = static_cast<DelayedData*>(lpParam);
        Sleep(300);
        if (d->self->IsRunning()) {
            d->self->SendEnterBattlePacket();
        }
        delete d;
        return 0;
    }, data, 0, nullptr);
    
    if (hThread) CloseHandle(hThread);
}

void ShuangTaiAutoBattle::SendEnterBattlePacket() {
    if (!m_running) return;
    
    UpdateState(ShuangTaiState::ENTERING_BATTLE);
    UIBridge::Instance().UpdateHelperText(L"双台谷刷级：进入战斗...");
    
    auto packet = PacketBuilder()
        .SetOpcode(ShuangTai::ENTER_BATTLE_SEND)
        .SetParams(0)
        .Build();
    SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
    
    // 战斗开始响应由 OnBattleStartResponse 处理
}

void ShuangTaiAutoBattle::SendBattleOp1Packet() {
    if (!m_running) return;
    
    auto packet = PacketBuilder()
        .SetOpcode(ShuangTai::BATTLE_OP1_SEND)
        .SetParams(0)
        .Build();
    SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

void ShuangTaiAutoBattle::SendSwitchPetPacket() {
    if (!m_running || m_petIndex >= static_cast<int>(m_petIds.size())) return;
    
    UpdateState(ShuangTaiState::SWITCHING_PETS);
    
    uint32_t uniqueId = m_petIds[m_petIndex];
    UIBridge::Instance().UpdateHelperText(L"双台谷刷级：切换宠物 " + 
        std::to_wstring(m_petIndex + 1) + L"/" + std::to_wstring(m_petIds.size()) + L"...");
    
    auto packet = PacketBuilder()
        .SetOpcode(ShuangTai::USER_OP_SEND)
        .SetParams(1)  // 切换精灵操作
        .WriteInt32(uniqueId)
        .Build();
    SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
    
    // 切换宠物响应由 OnBattleRoundResultResponse 处理
}

void ShuangTaiAutoBattle::SendSkillAttackPacket() {
    if (!m_running) return;
    
    UpdateState(ShuangTaiState::ATTACKING);
    UIBridge::Instance().UpdateHelperText(L"双台谷刷级：主宠攻击 " + 
        std::to_wstring(m_attackRound + 1) + L"/" + std::to_wstring(m_maxAttackCount.load()) + L"...");

    int targetSid = 0;
    BattleData& battleData = PacketParser::GetCurrentBattle();
    if (battleData.otherActiveIndex >= 0 &&
        battleData.otherActiveIndex < static_cast<int>(battleData.otherPets.size())) {
        targetSid = battleData.otherPets[battleData.otherActiveIndex].sid;
    }
    if (targetSid <= 0) {
        targetSid = 2;
    }
    
    auto packet = PacketBuilder()
        .SetOpcode(ShuangTai::USER_OP_SEND)
        .SetParams(0)  // 技能攻击操作
        .WriteInt32(targetSid)  // 目标 sid
        .WriteInt32(m_mainPetSkillId)
        .Build();
    SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
    
    // 技能攻击响应由 OnBattleRoundResultResponse 处理
}

void ShuangTaiAutoBattle::SendBattleEndPacket() {
    if (!m_running) return;
    
    UpdateState(ShuangTaiState::ENDING_BATTLE);
    UIBridge::Instance().UpdateHelperText(L"双台谷刷级：发送战斗结束封包...");
    
    auto packet = PacketBuilder()
        .SetOpcode(ShuangTai::BATTLE_END_SEND)
        .SetParams(300)
        .Build();
    SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
    
    // 战斗结束响应由 OnBattleEndResponse 处理
}

// ========== 状态转换方法 ==========

bool ShuangTaiAutoBattle::SwitchToNextPet() {
    if (m_petIndex < static_cast<int>(m_petIds.size())) {
        // 还有宠物需要切换
        SendSwitchPetPacket();
        return true;
    } else {
        // 所有宠物已切换完毕，开始主宠攻击
        m_attackRound = 0;
        SendSkillAttackPacket();
        return false;
    }
}

void ShuangTaiAutoBattle::StartNewRound() {
    // 重置回合状态
    // 注意：petIndex 会在 OnBattleStartResponse 中初始化
    m_attackRound = 0;
    
    UIBridge::Instance().UpdateHelperText(L"双台谷刷级：开始第 " + 
        std::to_wstring(m_totalRounds + 1) + L" 轮...");
    
    // 发送初始化封包开始新一轮
    SendInitPacket();
}

// ========== 响应处理方法 ==========

void ShuangTaiAutoBattle::OnBattleStartResponse(const GamePacket& packet) {
    if (!m_running) return;
    
    UpdateState(ShuangTaiState::IN_BATTLE);
    UIBridge::Instance().UpdateHelperText(L"双台谷刷级：战斗开始，准备切换宠物...");
    
    // m_petIds 包含第2只到最后一只（主宠）
    // 第一只妖怪已自动出战，从 m_petIds[0]（第2只）开始切换
    m_petIndex = 0;
    m_attackRound = 0;
    
    // 延时后开始切换宠物（有等待响应）
    struct DelayedData { ShuangTaiAutoBattle* self; };
    DelayedData* data = new DelayedData{this};
    
    HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
        auto* d = static_cast<DelayedData*>(lpParam);
        Sleep(300);
        if (d->self->IsRunning()) {
            // 发送战斗操作封包
            auto packet = PacketBuilder()
                .SetOpcode(ShuangTai::BATTLE_OP1_SEND)
                .SetParams(0)
                .Build();
            SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
            
            Sleep(300);
            if (d->self->IsRunning()) {
                d->self->SwitchToNextPet();
            }
        }
        delete d;
        return 0;
    }, data, 0, nullptr);
    
    if (hThread) CloseHandle(hThread);
}

void ShuangTaiAutoBattle::OnBattleRoundStartResponse() {
    if (!m_running) return;
    
    // 回合开始，仅在攻击状态时处理
    ShuangTaiState currentState = m_state.load();
    
    if (currentState == ShuangTaiState::ATTACKING) {
        // 攻击后的回合开始，继续攻击或结束战斗
        m_attackRound++;
        
        if (m_attackRound < m_maxAttackCount.load()) {
            // 延迟后继续攻击
            struct DelayedData { ShuangTaiAutoBattle* self; };
            DelayedData* data = new DelayedData{this};
            
            HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
                auto* d = static_cast<DelayedData*>(lpParam);
                Sleep(300);
                if (d->self->IsRunning()) {
                    d->self->SendSkillAttackPacket();
                }
                delete d;
                return 0;
            }, data, 0, nullptr);
            
            if (hThread) CloseHandle(hThread);
        } else {
            // 攻击完成，结束战斗
            struct DelayedData { ShuangTaiAutoBattle* self; };
            DelayedData* data = new DelayedData{this};
            
            HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
                auto* d = static_cast<DelayedData*>(lpParam);
                Sleep(300);
                if (d->self->IsRunning()) {
                    d->self->SendBattleEndPacket();
                }
                delete d;
                return 0;
            }, data, 0, nullptr);
            
            if (hThread) CloseHandle(hThread);
        }
    }
}

void ShuangTaiAutoBattle::OnBattleRoundResultResponse(const GamePacket& packet) {
    if (!m_running) return;
    
    // packet.params 表示 cmdType
    // 0 = 普通攻击, 1 = 切换宠物, 2 = 使用道具, 3 = 逃跑
    int cmdType = static_cast<int>(packet.params);
    
    // 发送确认封包
    auto ackPacket = PacketBuilder()
        .SetOpcode(ShuangTai::BATTLE_OP2_SEND)
        .SetParams(0)
        .Build();
    SendPacket(0, ackPacket.data(), static_cast<DWORD>(ackPacket.size()));
    
    // 只有切换宠物响应(cmdType=1)才继续下一只
    if (cmdType == 1 && m_state.load() == ShuangTaiState::SWITCHING_PETS) {
        // 切换成功，准备切换下一只
        m_petIndex++;
        
        // 延迟后继续切换
        struct DelayedData { ShuangTaiAutoBattle* self; };
        DelayedData* data = new DelayedData{this};
        
        HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
            auto* d = static_cast<DelayedData*>(lpParam);
            Sleep(300);
            if (d->self->IsRunning()) {
                d->self->SwitchToNextPet();
            }
            delete d;
            return 0;
        }, data, 0, nullptr);
        
        if (hThread) CloseHandle(hThread);
    }
}

void ShuangTaiAutoBattle::OnBattleEndResponse(const GamePacket& packet) {
    if (!m_running) return;
    
    m_totalRounds++;
    UIBridge::Instance().UpdateHelperText(L"双台谷刷级：第 " + 
        std::to_wstring(m_totalRounds) + L" 轮完成");
    
    // 检查是否请求停止
    if (m_stopRequested) {
        // 完成当前轮次后停止
        m_running = false;
        m_stopRequested = false;
        
        // 恢复屏蔽战斗状态
        if (m_blockBattle) {
            g_blockBattle = false;
        }
        
        UpdateState(ShuangTaiState::STOPPED);
        UIBridge::Instance().UpdateHelperText(L"双台谷刷级：已完成 " + 
            std::to_wstring(m_totalRounds) + L" 轮，已停止");
        return;
    }
    
    UIBridge::Instance().UpdateHelperText(L"双台谷刷级：第 " + 
        std::to_wstring(m_totalRounds) + L" 轮完成，准备下一轮...");
    
    // 延迟后开始新一轮
    struct DelayedData { ShuangTaiAutoBattle* self; };
    DelayedData* data = new DelayedData{this};
    
    HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
        auto* d = static_cast<DelayedData*>(lpParam);
        Sleep(300);
        if (d->self->IsRunning()) {
            d->self->StartNewRound();
        }
        delete d;
        return 0;
    }, data, 0, nullptr);
    
    if (hThread) CloseHandle(hThread);
}

// ========== 外部接口函数 ==========

BOOL StartOneKeyShuangTaiPacket(bool blockBattle) {
    return g_shuangtaiAuto.Start(blockBattle) ? TRUE : FALSE;
}

void StopShuangTai() {
    g_shuangtaiAuto.RequestStop();
}

// 查询相关的全局变量（用于异步查询）
bool g_shuangtaiWaitingForMonsterData = false;

BOOL QueryShuangTaiMonsters() {
    UIBridge::Instance().UpdateHelperText(L"双台谷：正在查询妖怪数据...");
    
    // 每次都重新查询，确保获取最新的妖怪背包数据
    // 设置标志，等待响应后更新UI
    g_shuangtaiWaitingForMonsterData = true;
    
    // 发送查询妖怪背包封包
    SendQueryMonsterPacket();
    
    return TRUE;
}

void UpdateShuangTaiUIFromMonsterData() {
    // 检查妖怪数量
    size_t petCount = g_monsterData.monsters.size();
    if (petCount < 2) {
        UIBridge::Instance().UpdateHelperText(L"双台谷：需要至少2只妖怪，当前只有 " + std::to_wstring(petCount) + L" 只");
        return;
    }
    
    // 获取最后一只妖怪信息（主宠）
    const auto& mainPet = g_monsterData.monsters.back();
    std::wstring petName = mainPet.name;
    
    // 获取最高威力技能名称
    uint32_t bestSkillId = GetHighestPowerSkillId(mainPet.id);
    std::wstring skillName = L"未知技能";
    for (const auto& skill : mainPet.skills) {
        if (skill.id == bestSkillId) {
            skillName = skill.name;
            break;
        }
    }
    
    // 更新UI
    std::wstring jsScript = L"if(window.updateShuangTaiUI) { window.updateShuangTaiUI('" 
        + petName + L"', '" + skillName + L"', " + std::to_wstring(petCount) + L", true); }";
    
    if (g_hWnd) {
        UIBridge::Instance().ExecuteJS(jsScript);
    }
    
    UIBridge::Instance().UpdateHelperText(L"双台谷：已读取 " + std::to_wstring(petCount) + L" 只妖怪，主宠: " + petName);
}

// ========== 响应处理函数（注册到 ResponseDispatcher）==========

void ProcessShuangTaiBattleStartResponse(const GamePacket& packet) {
    g_shuangtaiAuto.OnBattleStartResponse(packet);
}

void ProcessShuangTaiBattleRoundStartResponse(const GamePacket& packet) {
    g_shuangtaiAuto.OnBattleRoundStartResponse();
}

void ProcessShuangTaiBattleRoundResultResponse(const GamePacket& packet) {
    g_shuangtaiAuto.OnBattleRoundResultResponse(packet);
}

void ProcessShuangTaiBattleEndResponse(const GamePacket& packet) {
    g_shuangtaiAuto.OnBattleEndResponse(packet);
}

// ============================================================================
// 坐骑大赛实现已迁移到 src/hook/horse_competition.cpp.
// ============================================================================

// ============================================================================
// 精魄系统响应处理
// ============================================================================

/**
 * @brief 解析精魄列表响应
 * @param packet 响应封包
 */
void ProcessSpiritPresuresResponse(const GamePacket& packet) {
    const BYTE* body = packet.body.data();
    size_t offset = 0;
    const int32_t spiritCount = static_cast<int32_t>(packet.params);

    g_spiritCollectState.spiritList.clear();

    for (int i = 0; i < spiritCount && offset + 24 <= packet.body.size(); i++) {
        SpiritInfo spirit;
        spirit.eggId = ReadInt32LE(body, offset);
        spirit.eggIid = ReadInt32LE(body, offset);
        spirit.bornTime = ReadInt32LE(body, offset);
        spirit.needTime = ReadInt32LE(body, offset);
        spirit.eggType = ReadInt32LE(body, offset);
        spirit.character = ReadInt32LE(body, offset);

        if (offset + 4 > packet.body.size()) {
            break;
        }

        int32_t skillCount = ReadInt32LE(body, offset);
        if (skillCount < 0) {
            break;
        }

        if (offset + static_cast<size_t>(skillCount) * 4 > packet.body.size()) {
            break;
        }

        spirit.skillList.reserve(static_cast<size_t>(skillCount));
        for (int32_t j = 0; j < skillCount; ++j) {
            spirit.skillList.push_back(static_cast<uint32_t>(ReadInt32LE(body, offset)));
        }

        auto nameIt = g_petNames.find(static_cast<int>(spirit.eggIid));
        spirit.name = (nameIt != g_petNames.end())
            ? nameIt->second
            : (L"妖怪ID " + std::to_wstring(spirit.eggIid));

        spirit.elem = 0;
        auto elemIt = g_petElems.find(static_cast<int>(spirit.eggIid));
        if (elemIt != g_petElems.end()) {
            spirit.elem = static_cast<uint32_t>(elemIt->second);
            auto elemNameIt = g_elemNames.find(elemIt->second);
            spirit.elemName = (elemNameIt != g_elemNames.end()) ? elemNameIt->second : L"未知系别";
        } else {
            spirit.elemName = L"未知系别";
        }

        if (spirit.character > 0) {
            auto characterIt = g_geniusNames.find(static_cast<int>(spirit.character));
            spirit.characterName = (characterIt != g_geniusNames.end()) ? characterIt->second : L"未知性格";
        } else {
            spirit.characterName = L"未知性格";
        }

        spirit.skillNames.reserve(spirit.skillList.size());
        for (uint32_t skillId : spirit.skillList) {
            auto skillIt = g_skillNames.find(static_cast<int>(skillId));
            spirit.skillNames.push_back(
                skillIt != g_skillNames.end()
                    ? skillIt->second
                    : (L"技能" + std::to_wstring(skillId))
            );
        }

        g_spiritCollectState.spiritList.push_back(spirit);
    }

    std::wstring jsCode = L"if(window.handleSpiritCollectData) { window.handleSpiritCollectData({";
    jsCode += L"type: 'spiritList',";
    jsCode += L"data: [";

    for (size_t i = 0; i < g_spiritCollectState.spiritList.size(); i++) {
        const auto& spirit = g_spiritCollectState.spiritList[i];
        jsCode += L"{eggId: " + std::to_wstring(spirit.eggId) + L",";
        jsCode += L"eggIid: " + std::to_wstring(spirit.eggIid) + L",";
        jsCode += L"eggType: " + std::to_wstring(spirit.eggType) + L",";
        jsCode += L"bornTime: " + std::to_wstring(spirit.bornTime) + L",";
        jsCode += L"needTime: " + std::to_wstring(spirit.needTime) + L",";
        jsCode += L"character: " + std::to_wstring(spirit.character) + L",";
        jsCode += L"elem: " + std::to_wstring(spirit.elem) + L",";
        jsCode += L"name: '" + UIBridge::EscapeJsonString(spirit.name) + L"',";
        jsCode += L"elemName: '" + UIBridge::EscapeJsonString(spirit.elemName) + L"',";
        jsCode += L"characterName: '" + UIBridge::EscapeJsonString(spirit.characterName) + L"',";
        jsCode += L"skillList: [";
        for (size_t j = 0; j < spirit.skillList.size(); ++j) {
            jsCode += std::to_wstring(spirit.skillList[j]);
            if (j < spirit.skillList.size() - 1) {
                jsCode += L",";
            }
        }
        jsCode += L"],";
        jsCode += L"skillNames: [";
        for (size_t j = 0; j < spirit.skillNames.size(); ++j) {
            jsCode += L"'" + UIBridge::EscapeJsonString(spirit.skillNames[j]) + L"'";
            if (j < spirit.skillNames.size() - 1) {
                jsCode += L",";
            }
        }
        jsCode += L"]}";
        if (i < g_spiritCollectState.spiritList.size() - 1) {
            jsCode += L",";
        }
    }

    jsCode += L"]}); }";
    PostScriptToUI(jsCode);
}

/**
 * @brief 处理精魄系统活动协议响应
 * @param packet 响应封包
 */
void ProcessSpiritCollectResponse(const GamePacket& packet) {
    if (packet.params != SPIRIT_COLLECT_ACT_ID) return;
    if (packet.body.size() < 2) return;

    const BYTE* body = packet.body.data();
    size_t offset = 0;
    std::string oper;

    if (!ReadPacketString(body, packet.body.size(), offset, oper)) return;

    if (oper == "open_ui") {
        if (offset + 24 > packet.body.size()) return;

        int32_t love = ReadInt32LE(body, offset);
        g_spiritCollectState.weeklyOutLimit = ReadInt32LE(body, offset);
        int32_t monthlyOut = ReadInt32LE(body, offset);
        int32_t weeklyIn = ReadInt32LE(body, offset);
        int32_t monthlyIn = ReadInt32LE(body, offset);
        g_spiritCollectState.dailyOutLimit = ReadInt32LE(body, offset);

        std::wstring jsCode = L"if(window.handleSpiritCollectData) { window.handleSpiritCollectData({";
        jsCode += L"type: 'spiritState',";
        jsCode += L"data: {dOut: " + std::to_wstring(g_spiritCollectState.dailyOutLimit) + L",";
        jsCode += L"wOut: " + std::to_wstring(g_spiritCollectState.weeklyOutLimit) + L",";
        jsCode += L"love: " + std::to_wstring(love) + L",";
        jsCode += L"mOut: " + std::to_wstring(monthlyOut) + L",";
        jsCode += L"wIn: " + std::to_wstring(weeklyIn) + L",";
        jsCode += L"mIn: " + std::to_wstring(monthlyIn) + L"}}); }";
        PostScriptToUI(jsCode);
        return;
    }

    if (oper == "history") {
        if (offset + 4 > packet.body.size()) return;

        int32_t recordType = ReadInt32LE(body, offset);
        std::string jsonText;
        if (!ReadPacketString(body, packet.body.size(), offset, jsonText)) {
            jsonText.clear();
        }

        std::wstring jsCode = L"if(window.handleSpiritCollectData) { window.handleSpiritCollectData({";
        jsCode += L"type: 'history',";
        jsCode += L"recordType: " + std::to_wstring(recordType) + L",";
        jsCode += L"json: '" + UIBridge::EscapeJsonString(Utf8ToWide(jsonText)) + L"'}); }";
        PostScriptToUI(jsCode);
        return;
    }
}

/**
 * @brief 处理发送精魄响应
 * @param packet 响应封包
 */
void ProcessSpiritSendSpiritResponse(const GamePacket& packet) {
    const int32_t result = static_cast<int32_t>(packet.params);

    if (result == 0) {
        NotifySpiritAlert(L"精魄赠送失败，请稍后重试");
        return;
    }

    if (result != 1) {
        NotifySpiritAlert(L"精魄赠送失败，返回状态异常: " + std::to_wstring(result));
        return;
    }

    const BYTE* body = packet.body.data();
    size_t offset = 0;
    if (offset + 8 > packet.body.size()) {
        NotifySpiritSuccess(L"精魄赠送成功");
        return;
    }

    ReadInt32LE(body, offset);
    ReadInt32LE(body, offset);

    std::string friendNameUtf8;
    if (ReadPacketString(body, packet.body.size(), offset, friendNameUtf8)) {
        NotifySpiritSuccess(L"精魄已成功赠送给【" + Utf8ToWide(friendNameUtf8) + L"】");
    } else {
        NotifySpiritSuccess(L"精魄赠送成功");
    }
}

/**
 * @brief 处理验证玩家信息响应
 * @param packet 响应封包
 */
void ProcessSpiritPlayerInfoResponse(const GamePacket& packet) {
    if (packet.body.size() < 4) return;

    const BYTE* body = packet.body.data();
    size_t offset = 0;

    int32_t result = ReadInt32LE(body, offset);

    if (result == 1) {
        std::string nameUtf8;
        if (ReadPacketString(body, packet.body.size(), offset, nameUtf8)) {
            NotifySpiritConfirm(Utf8ToWide(nameUtf8));
        } else {
            NotifySpiritAlert(L"玩家信息返回不完整");
        }
        return;
    }

    if (result == 0) {
        NotifySpiritAlert(L"该卡布号玩家不存在");
        return;
    }

    if (result == 2) {
        NotifySpiritAlert(L"该卡布号玩家不在线");
        return;
    }

    NotifySpiritAlert(L"验证玩家信息失败，返回状态异常: " + std::to_wstring(result));
}
