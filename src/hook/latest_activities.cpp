#include "latest_activities.h"

#include <array>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "packet_builder.h"
#include "ui_bridge.h"
#include "utils.h"
#include "wpe_hook.h"

// ============================================================================
// 最新活动（周活动小游戏）主流程实现
//
// 协议族：OP_CLIENT_ACTIVITY_QINGYANG_NEW（send 1185429 / back 1316501），
// body = [操作字符串][int32 参数...]。各活动的字段偏移与参数变换均按下表实现，
// 依据为对应 SWF 的 FFDec 反编译结果（见 include/activities/latest_activities.h）。
//
// 只接主流程：进入活动查次数 -> 自动判定扫荡或走一局 -> 回填次数；
// 不展开兑换/礼包/买宠等入口。
// ============================================================================

namespace {

// ---------------------------------------------------------------------------
// 描述符：把各活动的差异集中在此，避免复制多份流程代码
// ---------------------------------------------------------------------------
struct LatestActDesc {
    uint32_t id;
    const wchar_t* name;
    const char* openUiOp;   ///< 进入活动操作串：多数 "open_ui"，749/704 为 "game_info"
    int targetScore;        ///< 走游戏流程时提交的目标分（= 各自满分；869 无分数参数，此值仅占位）
    bool sweepWithCount;    ///< sweep 是否带次数参数（647/869/749/870 需要）
    bool reportsDailyTask;  ///< 结算后是否上报每日任务（869/870/697 有）
    /// 扫荡门槛：true = 需要本地已确认「有过一局成绩」才尝试（869 看 historyBestScore、
    /// 749 看 game_info 的 topNum，均对应 AS3 的扫荡按钮前置判断）；
    /// false = 直接发 sweep_info 由服务器回包判定（647/870/704/697 的 AS3 做法）。
    bool sweepNeedsHistory;
};

const LatestActDesc kActivities[] = {
    {LatestAct::ACT_MAZE, L"突破迷阵", "open_ui", 180, true, false, false},
    {LatestAct::ACT_TOUCH_STAR, L"摘星之旅", "open_ui", 301, true, true, true},
    {LatestAct::ACT_EXPLORE, L"寒冰洞历险记", "game_info", 70, true, false, true},
    {LatestAct::ACT_MOON_CAKE, L"月饼大胃王", "open_ui", 40, true, true, false},
    {LatestAct::ACT_FLOWER, L"夺回花束行动", "game_info", 70, false, false, false},
    {LatestAct::ACT_FOOD, L"美食大作战", "open_ui", 400, false, true, false},
};

constexpr int kActivityNum = static_cast<int>(sizeof(kActivities) / sizeof(kActivities[0]));

const LatestActDesc* FindDesc(uint32_t activityId) {
    for (const auto& desc : kActivities) {
        if (desc.id == activityId) {
            return &desc;
        }
    }
    return nullptr;
}

// 869 星 id 上限：BodyPanel.gameStart() 把 AS3 的 gameAwardList 初始化成 19 个 0，
// 命中更大的星 id 时数组会随之变长，这里留足余量。
constexpr int kAwardSlots = 64;

// 等待回包的时间预算。
// SendPacket 在「发送成功但等不到回包」时不会自动重发，而是直接返回 FALSE，
// 因此这里的等待上限只需略大于 SendPacket 自身的 timeoutMs，余量留给响应分发线程解析。
constexpr DWORD kQueryTimeoutMs = 3000;   ///< 只读查询类（open_ui/sweep_info/sweep）
constexpr DWORD kActionTimeoutMs = 5000;  ///< 动作类（start_game/end_game）
constexpr DWORD kWaitMarginMs = 1000;

// ---------------------------------------------------------------------------
// 每活动运行时状态
// ---------------------------------------------------------------------------
struct LatestActState {
    std::atomic<int> playCount{-1};  ///< 剩余次数；-1 未知
    std::atomic<int> restTime{0};    ///< 冷却剩余秒
    std::atomic<int> bestScore{0};   ///< 历史最好成绩（自动扫荡判定用）
    std::atomic<int> checkCode{0};   ///< start_game 下发的校验种子
    std::atomic<int> checkCode2{0};  ///< 870 的第二校验种子
    std::atomic<int> ruleFlag{0};    ///< start_game 需要的规则/提示参数
    std::atomic<bool> sweepReady{false};  ///< sweep_info 是否放行（有历史成绩）
    std::atomic<bool> waitingResponse{false};

    /// 最近一次 start_game 的 result（0 = 成功）。服务端拒绝开局时（如 870 的
    /// result=3「在游戏中」）用它给出可读提示，而不是静默判成失败。
    std::atomic<int> lastStartResult{-1};

    /// 最近一次 end_game 回包的 passSum（仅 870 有该字段，-1 表示未解析到）。
    /// 870 的收尾包只在 passSum != 1 时补发，见 TryPlayOnce。
    std::atomic<int> lastPassSum{-1};

    /// 最近一次「成功开局」留下的校验种子（跨局保留，不随 TryPlayOnce 重置）。
    /// 仅 870 用于从「上一局未结束」的自锁状态中恢复：服务端回 result=3 时，
    /// 用它补发收尾包把上一局关掉，再重试开局。
    std::atomic<int> lastGameCheckCode{0};
    std::atomic<int> lastGameCheckCode2{0};

    /// 当前正在等待的回包操作（PendingOp）。响应处理器只在 operation 与它一致时
    /// 放行等待方，避免上一轮迟到的回包提前清掉 waitingResponse——那会让调用方
    /// 读到未更新的 checkCode/sweepReady，既可能误判扫荡放行，也可能误判执行失败。
    std::atomic<uint32_t> pendingOp{0};  ///< 0 = 未在等待

    /// 869 专用：start_game 回包下发的 61 项星级布局，按星 id 累计命中次数。
    /// AS3 的 gameAwardList 就是这张计数表，结算时随 end_game 原样回传。
    std::array<int, kAwardSlots> awardCounts{};
    int maxAwardId{0};               ///< 计数表中出现过的最大星 id（决定回传长度）

    /// 占用标记：0 空闲，1 一键流程，2 次数探针。
    /// 二者共用 (ACTIVITY_QUERY_BACK, activityId) 路由到同一个响应处理器，而该处理器
    /// 会无条件清除 waitingResponse；若并发，探针的响应会提前清掉一键流程的等待标志，
    /// 令其误判收到响应。用 CAS 保证同一活动同一时刻只有一种流程在跑。
    std::atomic<int> opLock{0};
};

LatestActState g_state[kActivityNum];

/// 批量一键执行是否在进行：整批活动串行跑，同一时刻只允许一个 worker。
std::atomic<bool> g_latestBatchRunning{false};

/// 把单个活动的剩余次数回填到前端（与 wpe_hook 的 ReportActivityCount 等价，
/// 但本模块不能依赖那边的内部实现，故自带一份）。
void ReportActivityCountToUi(uint32_t activityId, int remainingCount) {
    wchar_t jsCode[256];
    swprintf_s(
        jsCode,
        L"if(window.updateActivityCounts) { window.updateActivityCounts({ %u: %d }); }",
        activityId,
        remainingCount);
    UIBridge::Instance().ExecuteJS(jsCode);
}

LatestActState* StateOf(uint32_t activityId) {
    const LatestActDesc* desc = FindDesc(activityId);
    if (!desc) {
        return nullptr;
    }
    return &g_state[desc - kActivities];
}

void Notify(uint32_t activityId, const std::wstring& message) {
    const LatestActDesc* desc = FindDesc(activityId);
    const wchar_t* name = desc ? desc->name : L"最新活动";
    UIBridge::Instance().UpdateHelperText(std::wstring(name) + L"：" + message);
}

// 读取 body 中的 UTF 字符串（长度 2 字节小端 + 内容）。
bool ReadBodyString(const BYTE* body, size_t bodySize, size_t& offset, std::string& out) {
    if (offset + 2 > bodySize) {
        return false;
    }
    const uint16_t len = ReadUInt16LE(body, offset);
    if (offset + len > bodySize) {
        return false;
    }
    out.assign(reinterpret_cast<const char*>(body + offset), len);
    offset += len;
    return true;
}

// ---------------------------------------------------------------------------
// 发包
// ---------------------------------------------------------------------------
BOOL SendLatestPacket(uint32_t activityId, const std::string& operation,
                      const std::vector<int32_t>& bodyValues = {},
                      DWORD timeoutMs = WpeHook::TIMEOUT_RESPONSE) {
    std::vector<BYTE> packet = BuildActivityPacket(
        Opcode::ACTIVITY_QINGYANG_NEW_SEND, activityId, operation, bodyValues);
    if (packet.empty()) {
        return FALSE;
    }
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()),
                      Opcode::ACTIVITY_QUERY_BACK, timeoutMs,
                      activityId, true);
}

// 不等待回包的过程包（AS3 的 play_game 也是发完即走，没有对应回包处理）。
BOOL SendLatestPacketNoWait(uint32_t activityId, const std::string& operation,
                            const std::vector<int32_t>& bodyValues = {}) {
    std::vector<BYTE> packet = BuildActivityPacket(
        Opcode::ACTIVITY_QINGYANG_NEW_SEND, activityId, operation, bodyValues);
    if (packet.empty()) {
        return FALSE;
    }
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 上报每日任务进度。AS3 各活动在结算后用
// sendMessage(OP_CLIENT_DAILYTASK.send, 3, [3, 4039001, 0]) 上报，
// 注意其 opcode(1184812) 与 TASK_DAILY_SEND(1184791) 并非同一个协议。
void ReportDailyTask() {
    std::vector<BYTE> packet = PacketBuilder()
                                   .SetOpcode(Opcode::TASK_DAILY_REPORT_SEND)
                                   .SetParams(3)
                                   .WriteInt32(3)
                                   .WriteInt32(4039001)
                                   .WriteInt32(0)
                                   .Build();
    if (!packet.empty()) {
        SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
    }
}

/// 正在等待的回包类型。响应处理器用它区分「本次等待的回包」与「上一轮迟到的回包」：
/// 只有两者一致才放行等待方。否则一个迟到的 sweep_info 回包会在等 sweep/end_game
/// 时提前清掉 waitingResponse，让调用方读到旧的 sweepReady/checkCode 而误判成功。
enum class PendingOp : uint32_t {
    None = 0,
    OpenUi,
    StartGame,
    SweepInfo,
    Sweep,
    EndGame,
};

/// 等待守卫：置位等待标志并登记期望回包，离开作用域时一并撤销，
/// 保证任何提前 return 都不会留下悬挂的等待状态。
struct PendingOpGuard {
    LatestActState& state;
    PendingOpGuard(LatestActState& s, PendingOp op) : state(s) {
        state.pendingOp = static_cast<uint32_t>(op);
        state.waitingResponse = true;
    }
    ~PendingOpGuard() {
        state.pendingOp = static_cast<uint32_t>(PendingOp::None);
        state.waitingResponse = false;
    }
    PendingOpGuard(const PendingOpGuard&) = delete;
    PendingOpGuard& operator=(const PendingOpGuard&) = delete;
};

bool WaitForResponse(LatestActState& state, DWORD timeoutMs) {
    const auto start = std::chrono::steady_clock::now();
    while (state.waitingResponse.load()) {
        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() > timeoutMs) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return true;
}

// 是否值得先尝试扫荡（对应 AS3 里「点扫荡按钮」这一动作是否被允许）。
// 本地可判的前置条件只有两处，AS3 都是先判它们再发 sweep_info：
//   869 EnterPanel.buySweepBtnHandler : historyBestScore > 0
//   749 EnterView.openSweepWin        : _topNum > 0
// 其余活动（647/870/704/697）的 AS3 直接发 sweep_info、由服务器回包决定能否扫荡
// （647 result != 3、870 medal > 0、697 result == 0、704 medal > 0），
// 因此这里不设本地门槛。
// 注意：749/870 若本地误加分数门槛，会让「从未通关」的活动永远走不到扫荡分支。
// 749 加门槛是对的（AS3 确实先判 topNum），870 则不能加（AS3 的扫荡按钮只看
// 次数与冷却，MainView.as:136 `bg.sdBtn.visible = bg.startBtn.visible`）。
bool ShouldAttemptSweep(const LatestActDesc& desc, const LatestActState& state) {
    if (!desc.sweepNeedsHistory) {
        return true;
    }
    return state.bestScore.load() > 0;
}

// 各活动通用的 checkCode 变换：checkCode & (uid % checkCode)
int MaskedCheckCode(int seed) {
    if (seed <= 0) {
        return 0;
    }
    const uint32_t userId = g_userId.load();
    return seed & static_cast<int>(userId % static_cast<uint32_t>(seed));
}

// 870 专用的校验式（GameView.gameOver / GameOverView.handlerClose 完全一致）：
//   checkCode = seed * (uid % 1000) * (checkCode2 + 1)
// 三个因子缺一不可；uid 或 checkCode2 尚未就绪时结果为 0，服务端会判为无效结算。
int MoonCakeCheckCode(int seed, int code2) {
    const int64_t userId = static_cast<int64_t>(g_userId.load());
    return static_cast<int>(static_cast<int64_t>(seed) * (userId % 1000) *
                            (static_cast<int64_t>(code2) + 1));
}

// ---------------------------------------------------------------------------
// open_ui / game_info 字段解析（各活动偏移不同，依据各自 Model.as）
// ---------------------------------------------------------------------------
void ParseOpenUI(const LatestActDesc& desc, LatestActState& state,
                 const GamePacket& packet, size_t offset) {
    const BYTE* body = packet.body.data();
    const size_t size = packet.body.size();
    auto read = [&]() -> int { return ReadInt32LE(body, offset); };

    switch (desc.id) {
        case LatestAct::ACT_MAZE:
            // restPlayCount, restCdTime, buyPlayCount, totalBadgeNum, awardFlag,
            // bestRecord, ignored, isExchangeEvo, itemNum, ...
            if (offset + 24 > size) return;
            state.playCount = read();
            state.restTime = read();
            offset += 12;                 // buyPlayCount/totalBadgeNum/awardFlag
            state.bestScore = read();
            break;

        case LatestAct::ACT_TOUCH_STAR:
            // leftPlay, restCdTime, buyCnt, goodFeelNumber, ruleIntroduce, reset,
            // historyBestScore, ...
            if (offset + 28 > size) return;
            state.playCount = read();     // leftPlay
            state.restTime = read();      // restCdTime
            offset += 8;                  // buyCnt, goodFeelNumber
            state.ruleFlag = read();      // ruleIntroduce（start_game 参数）
            offset += 4;                  // reset
            state.bestScore = read();     // historyBestScore
            break;

        case LatestAct::ACT_EXPLORE:
            // result, playTime, restTime, popRule, flowerNum, topNum, lastNum
            if (offset + 4 > size) return;
            if (read() != 0) {
                // 进入失败：次数与历史成绩都不可信，一并清掉，
                // 否则旧 bestScore 会让扫荡门槛误放行。
                state.playCount = 0;
                state.bestScore = 0;
                return;
            }
            if (offset + 12 > size) return;
            state.playCount = read();     // playTime
            state.restTime = read();
            state.ruleFlag = read();      // popRule
            // topNum = 历史最高花数。AS3 的扫荡入口先判它
            // （EnterView.openSweepWin: _topNum <= 0 直接提示「需要成功进行一局游戏才可以扫荡哦」），
            // 不读它就会在从未通关时也发 sweep，把当日次数白白扫掉。
            if (offset + 8 > size) return;
            offset += 4;                  // flowerNum
            state.bestScore = read();     // topNum
            break;

        case LatestAct::ACT_MOON_CAKE:
            // playCount, restTime, bubbleNum, ruleFlag, isSweep, rewardList[4], ...
            // isSweep 在 AS3 里被读入 mbody 但从未参与判断（ActiveModel.as 仅此一处出现），
            // 因此它不能作为扫荡门槛，这里跳过即可。
            if (offset + 20 > size) return;
            state.playCount = read();
            state.restTime = read();
            offset += 4;                  // bubbleNum
            state.ruleFlag = read();      // ruleFlag（start_game 参数）
            offset += 4;                  // isSweep（AS3 读了但不用）
            break;

        case LatestAct::ACT_FLOWER:
            // result, restPlayCount, restCdTime, closePrompt
            if (offset + 4 > size) return;
            if (read() != 0) { state.playCount = 0; return; }
            if (offset + 12 > size) return;
            state.playCount = read();
            state.restTime = read();
            state.ruleFlag = read();      // closePrompt -> start_game 的 promptFlag
            break;

        case LatestAct::ACT_FOOD:
            // playCount, restTime, bubbleNum, ruleFlag, passBonusFlag,
            // maxScore, passCount, ignored, ...
            if (offset + 24 > size) return;
            state.playCount = read();
            state.restTime = read();
            offset += 4;                  // bubbleNum
            state.ruleFlag = read();
            offset += 4;                  // passBonusFlag
            state.bestScore = read();     // maxScore
            break;

        default:
            break;
    }
}

// start_game 响应：result, <丢弃字段>, checkCode[, checkCode2]
//
// 全部 6 个活动（647/869/749/870/704/697）的回包都是「先 result、再一个被丢弃的
// 字段、然后才是 checkCode」，已逐个用 FFDec 反编译核对：
//   647 MazeModel.START_GAME     : result, readInt(), checkCode
//   869 TouchStarModel.START_GAME: result, leftPlay, checkCode, awardList[61]
//   749 ExploreModel.START_GAME  : result, readInt(), check_code
//   870 ActiveModel.START_GAME   : result, readInt(), checkCode, checkCode2
//   704 MainModel.START_GAME     : result, readInt(), checkCode, round[5][20]
//   697 ActiveModel.START_GAME   : result, readInt(), checkCode
// 该字段不能被当作 checkCode 使用；漏跳会让 checkCode 整体前移 4 字节，
// end_game 的校验式全错，服务端只扣次数不下发奖励。
// 注：870 额外多一个 checkCode2。
void ParseStartGame(const LatestActDesc& desc, LatestActState& state,
                    const GamePacket& packet, size_t offset) {
    const BYTE* body = packet.body.data();
    const size_t size = packet.body.size();
    if (offset + 8 > size) return;                // 至少要能读 result + 丢弃字段
    const int result = ReadInt32LE(body, offset);
    state.lastStartResult = result;
    if (result != 0) return;                      // result != 0 视为开局失败
    offset += 4;                                  // 丢弃字段（各活动含义不同）

    const bool hasSecondCode = (desc.id == LatestAct::ACT_MOON_CAKE);
    const size_t remaining = 4 + (hasSecondCode ? 4 : 0);
    if (offset + remaining > size) return;

    state.checkCode = ReadInt32LE(body, offset);
    if (hasSecondCode) {
        state.checkCode2 = ReadInt32LE(body, offset);
    }

    // 869 的 start_game 回包在 checkCode 之后还有 61 项星级布局（awardList）。
    // AS3 命中星星时执行 ++gameAwardList[awardId]，结算时把这张计数表回传；
    // 这里必须先读进来，否则 end_game 只能送全 0（等于 0 星通关）。
    if (desc.id == LatestAct::ACT_TOUCH_STAR) {
        state.awardCounts.fill(0);
        state.maxAwardId = 0;
        for (int i = 0; i < 61 && offset + 4 <= size; ++i) {
            const int awardId = ReadInt32LE(body, offset);
            if (awardId >= 1 && awardId < kAwardSlots) {
                ++state.awardCounts[static_cast<std::size_t>(awardId)];
                if (awardId > state.maxAwardId) {
                    state.maxAwardId = awardId;
                }
            }
        }
    }
}

// sweep_info 响应：判断是否放行扫荡（各活动首个语义字段不同）。
void ParseSweepInfo(const LatestActDesc& desc, LatestActState& state,
                    const GamePacket& packet, size_t offset) {
    const BYTE* body = packet.body.data();
    const size_t size = packet.body.size();
    if (offset + 4 > size) return;

    const int first = ReadInt32LE(body, offset);
    switch (desc.id) {
        case LatestAct::ACT_FLOWER:
            // 首字段即勋章数：> 0 表示有历史成绩可扫荡。
            state.sweepReady = (first > 0);
            break;
        case LatestAct::ACT_MOON_CAKE:
            // result, medal, ...：medal > 0 表示有历史成绩可扫荡。
            if (offset + 4 > size) return;
            state.sweepReady = (ReadInt32LE(body, offset) > 0);
            break;
        case LatestAct::ACT_MAZE:
            // result：!= 3 表示放行。
            state.sweepReady = (first != 3);
            break;
        case LatestAct::ACT_TOUCH_STAR:
            // AS3 的 TouchStar 不看 sweep_info 的 resultType，能否扫荡由
            // historyBestScore > 0 决定（EnterPanel.buySweepBtnHandler）；走到这里
            // 说明 ShouldAttemptSweep 已确认有成绩，故直接放行，避免误判退回走一局。
            state.sweepReady = true;
            break;
        case LatestAct::ACT_EXPLORE:
        case LatestAct::ACT_FOOD:
            // result == 0 表示放行。
            state.sweepReady = (first == 0);
            break;
        default:
            state.sweepReady = (first != 3);
            break;
    }
}

// sweep 响应：确认服务端确实完成扫荡（否则退回游戏流程）。
void ParseSweepResult(const LatestActDesc& desc, LatestActState& state,
                      const GamePacket& packet, size_t offset) {
    const BYTE* body = packet.body.data();
    const size_t size = packet.body.size();
    if (offset + 4 > size) {
        state.sweepReady = false;
        return;
    }

    const int result = ReadInt32LE(body, offset);
    switch (desc.id) {
        case LatestAct::ACT_MOON_CAKE:
            state.sweepReady = (result == 0 || result == 4);
            break;
        case LatestAct::ACT_TOUCH_STAR:
            // decodeSweep 首字段为 resultType，0 视为成功。
            state.sweepReady = (result == 0);
            break;
        default:
            // 647/749/704/697：result == 3 表示失败。
            state.sweepReady = (result != 3);
            break;
    }
}

// end_game 回包：仅 870 需要解析 passSum（见 TryPlayOnce 的收尾包逻辑）。
// AS3 ActiveModel 的 "end_game" 分支：result, <丢弃>, <丢弃>, score, passSum, propList[3]。
// 客户端把服务端回的 passSum 存进 ActivityData.passSum，GameOverView.handlerClose
// 再据此判断是否补发收尾包，因此这里必须真正解析它，而不能只看本地初值 0。
void ParseEndGame(const LatestActDesc& desc, LatestActState& state,
                  const GamePacket& packet, size_t offset) {
    if (desc.id != LatestAct::ACT_MOON_CAKE) {
        return;
    }
    const BYTE* body = packet.body.data();
    const size_t size = packet.body.size();
    if (offset + 20 > size) {
        return;
    }

    const int result = ReadInt32LE(body, offset);
    if (result != 0 && result != 4) {
        return;   // 结算未被接受，passSum 无效
    }
    offset += 8;                                  // 两个丢弃字段
    offset += 4;                                  // score
    state.lastPassSum = ReadInt32LE(body, offset);  // passSum
}

// ---------------------------------------------------------------------------
// end_game 参数构造（各活动 body 不同，依据各自发送处）
// ---------------------------------------------------------------------------
std::vector<int32_t> BuildEndGameBody(const LatestActDesc& desc, LatestActState& state) {
    const uint32_t userId = g_userId.load();
    const int seed = state.checkCode.load();
    const int score = desc.targetScore;

    switch (desc.id) {
        case LatestAct::ACT_MAZE:
            // [end_game, score, checkCode, isPass]
            return {score, MaskedCheckCode(seed), 1};

        case LatestAct::ACT_TOUCH_STAR: {
            // [end_game, checkCode, 各星命中次数...]
            // AS3 的 gameAwardList 由 BodyPanel.gameStart() 初始化成 19 个 0，
            // 因此长度至少 18（index 1..18）；命中更大的星 id 时数组会随之变长。
            const int slots = state.maxAwardId > 18 ? state.maxAwardId : 18;
            std::vector<int32_t> body;
            body.reserve(static_cast<std::size_t>(slots) + 1);
            body.push_back(MaskedCheckCode(seed));
            for (int id = 1; id <= slots; ++id) {
                body.push_back(state.awardCounts[static_cast<std::size_t>(id)]);
            }
            return body;
        }

        case LatestAct::ACT_EXPLORE:
            // [end_game, end_code]，end_code = uid % 1000 + check_code + num
            return {static_cast<int>(userId % 1000u) + seed + score};

        case LatestAct::ACT_MOON_CAKE:
            // [end_game, passSum, endType, checkCode, score]
            // endType = 4 表示通关（GameView.gameOver 中 score >= passScore 时置 4）。
            return {0, 4, MoonCakeCheckCode(seed, state.checkCode2.load()), score};

        case LatestAct::ACT_FLOWER:
            // [end_game, score, checkCode]，此处用原始 checkCode（不做掩码）
            return {score, seed};

        case LatestAct::ACT_FOOD:
            // [end_game, score, checkCode]，checkCode = seed & (uid % seed)
            return {score, MaskedCheckCode(seed)};

        default:
            return {score};
    }
}

// ---------------------------------------------------------------------------
// start_game 参数构造
// ---------------------------------------------------------------------------
std::vector<int32_t> BuildStartGameBody(const LatestActDesc& desc, LatestActState& state) {
    switch (desc.id) {
        case LatestAct::ACT_MAZE:
            return {0, 0};                      // [start_game, 0, 0]
        case LatestAct::ACT_TOUCH_STAR:
            return {state.ruleFlag.load()};     // [start_game, isPop]
        case LatestAct::ACT_EXPLORE:
            return {1};                         // [start_game, state]
        case LatestAct::ACT_MOON_CAKE:
            return {state.ruleFlag.load()};     // [start_game, ruleFlag]
        case LatestAct::ACT_FLOWER:
            return {state.ruleFlag.load()};     // [start_game, promptFlag]
        case LatestAct::ACT_FOOD:
            return {};                          // [start_game]
        default:
            return {};
    }
}

// 扫荡：sweep_info 预览 -> sweep 执行（失败则由调用方退回游戏流程）
bool TrySweep(const LatestActDesc& desc, LatestActState& state) {
    state.sweepReady = false;
    {
        PendingOpGuard guard(state, PendingOp::SweepInfo);
        if (!SendLatestPacket(desc.id, "sweep_info", {}, kQueryTimeoutMs) ||
            !WaitForResponse(state, kQueryTimeoutMs + kWaitMarginMs)) {
            return false;
        }
    }
    if (!state.sweepReady.load()) {
        return false;   // 尚无历史成绩，需先走一局
    }

    // 647/869/749/870 的 sweep 需要带上本次扫荡次数（取剩余次数）。
    const int count = state.playCount.load() > 0 ? state.playCount.load() : 1;
    const std::vector<int32_t> sweepBody = desc.sweepWithCount
        ? std::vector<int32_t>{count}
        : std::vector<int32_t>{};

    state.sweepReady = false;
    {
        PendingOpGuard guard(state, PendingOp::Sweep);
        if (!SendLatestPacket(desc.id, "sweep", sweepBody, kQueryTimeoutMs) ||
            !WaitForResponse(state, kQueryTimeoutMs + kWaitMarginMs)) {
            return false;
        }
    }
    return state.sweepReady.load();
}

// 走一局：start_game -> 校验种子 -> end_game 提交目标分
bool TryPlayOnce(const LatestActDesc& desc, LatestActState& state) {
    state.checkCode = 0;
    state.checkCode2 = 0;
    state.lastStartResult = -1;
    state.lastPassSum = -1;

    {
        PendingOpGuard guard(state, PendingOp::StartGame);
        if (!SendLatestPacket(desc.id, "start_game", BuildStartGameBody(desc, state),
                              kActionTimeoutMs) ||
            !WaitForResponse(state, kActionTimeoutMs + kWaitMarginMs)) {
            return false;
        }
    }
    if (state.checkCode.load() == 0) {
        // 870 的 checkCode = seed * (uid % 1000) * (checkCode2 + 1) 含 uid 因子，
        // uid 未就绪时结算式必错。此处不静默放弃，交由调用方按 lastStartResult 报错。
        return false;
    }
    // 870 还要额外确认结算式整体不为 0：seed/uid/checkCode2 任一为 0 都会让
    // 服务端判成无效结算（只扣次数不发奖励），因此提前拦下而不是发一个必错的包。
    if (desc.id == LatestAct::ACT_MOON_CAKE &&
        MoonCakeCheckCode(state.checkCode.load(), state.checkCode2.load()) == 0) {
        return false;
    }
    // 记下本局的种子，供「在游戏中」自锁恢复时补发收尾包使用。
    state.lastGameCheckCode = state.checkCode.load();
    state.lastGameCheckCode2 = state.checkCode2.load();

    // 749 的结算分由服务端按 play_game(1) 次数累计（ExploreControl.getFlower），
    // end_code 里的 num 必须等于该计数。先按目标花数（desc.targetScore=70）补齐过程包，每包间隔 150ms，不无延时连发。
    if (desc.id == LatestAct::ACT_EXPLORE) {
        for (int i = 0; i < desc.targetScore; ++i) {
            if (!g_latestBatchRunning.load()) {
                return false;
            }
            if (i > 0) { std::this_thread::sleep_for(std::chrono::milliseconds(150)); }
            if (!g_latestBatchRunning.load() || !SendLatestPacketNoWait(desc.id, "play_game", {1})) {
                return false;
            }
        }
    }

    bool endSettled = false;
    {
        PendingOpGuard guard(state, PendingOp::EndGame);
        if (SendLatestPacket(desc.id, "end_game", BuildEndGameBody(desc, state),
                             kActionTimeoutMs) &&
            WaitForResponse(state, kActionTimeoutMs + kWaitMarginMs)) {
            endSettled = true;
        }
    }

    // 870 的结算分两步：GameView.gameOver() 发第一次 end_game（endType 表示是否通关），
    // 玩家点掉结算面板时 GameOverView.handlerClose 在 passSum == 0 的前提下再发一次
    // end_game{passSum=0, endType=1, checkCode, score=0} 收尾。漏掉这一步，服务端会
    // 停留在「本局未结束」，之后每次 start_game 都返回 result=3（在游戏中），
    // 表现就是月饼大胃王永远打不完。
    // passSum == 1 只可能来自「复活」（GameOverView.handlerNext），本流程从不复活，
    // 因此除该情形外一律补发收尾包；即便首次 end_game 的回包超时（lastPassSum 仍为 -1）
    // 也照样补发，否则会把会话留在未结束状态。
    if (desc.id == LatestAct::ACT_MOON_CAKE && state.lastPassSum.load() != 1) {
        SendLatestPacketNoWait(desc.id, "end_game",
                              {0, 1, MoonCakeCheckCode(state.checkCode.load(),
                                                       state.checkCode2.load()), 0});
    }

    if (!endSettled) {
        // 870 例外：首次结算的回包超时并不代表服务端没处理，但此时收尾包已按上面的
        // 逻辑发出，会话不会停在「未结束」。本轮到底成没成，交给调用方的次数回查判定
        // （after >= before 会停止该活动），此处不再直接判死整个活动。
        return desc.id == LatestAct::ACT_MOON_CAKE;
    }

    // 与 AS3 一致：869/870/697 结算后会顺带上报每日任务 4039001。
    if (desc.reportsDailyTask) {
        ReportDailyTask();
    }
    return true;
}

// 查询一次活动信息，回填剩余次数/冷却/最好成绩，并同步到前端。
// 返回 false 表示查询失败（拿不到可信次数）。
// 单次查询会因丢包/服务端排队而失败，重试两次可避免「一个活动因一次查询失败就整段放弃」。
bool RefreshOneActivity(const LatestActDesc& desc, LatestActState& state) {
    constexpr int kRefreshAttempts = 3;
    for (int attempt = 0; attempt < kRefreshAttempts; ++attempt) {
        if (!g_latestBatchRunning.load()) {
            return false;   // 已被要求停止，不再继续发查询
        }
        state.playCount = -1;
        {
            PendingOpGuard guard(state, PendingOp::OpenUi);
            if (!SendLatestPacket(desc.id, desc.openUiOp, {}, kQueryTimeoutMs) ||
                !WaitForResponse(state, kQueryTimeoutMs + kWaitMarginMs)) {
                continue;
            }
        }
        // waitingResponse 由响应处理器在解析完成后才清除，这里可直接读取解析结果。
        if (state.playCount.load() >= 0) {
            ReportActivityCountToUi(desc.id, state.playCount.load());
            return true;
        }
    }

    // 查询彻底失败时上报「未知」(-1)，让前端显示 -- 而不是停留在过期次数上。
    ReportActivityCountToUi(desc.id, -1);
    return false;
}

// 870 专用：服务端认为上一局还没结束时（start_game 回 result=3「在游戏中」），
// 用上一局保存的种子补发收尾 end_game 把会话关掉。
// 依据 AS3：这一局本该由 GameOverView.handlerClose 补发 end_game 才真正结束；
// 若那一步丢失（进程中断、回包超时、结算包未被接受），服务端就会一直停在游戏内，
// 之后每次 start_game 都返回 3，形成自锁——这正是「月饼大胃王无法完成」的表现。
void TryRecoverMoonCakeSession(LatestActState& state) {
    const int seed = state.lastGameCheckCode.load();
    const int code2 = state.lastGameCheckCode2.load();
    if (seed == 0) {
        return;   // 没有可用的历史种子，无法构造合法收尾包
    }
    SendLatestPacketNoWait(LatestAct::ACT_MOON_CAKE, "end_game",
                           {0, 1, MoonCakeCheckCode(seed, code2), 0});
}

// 单活动循环执行：反复「扫荡 / 走一局」，直到次数耗尽、进入冷却或失败为止。
void RunOneActivityUntilDone(const LatestActDesc& desc) {
    const uint32_t activityId = desc.id;
    LatestActState* statePtr = StateOf(activityId);
    if (!statePtr) {
        return;
    }
    LatestActState& state = *statePtr;

    // 与次数探针互斥：二者共用同一响应通道（都会清 waitingResponse），
    // 抢不到占用就跳过本活动，避免探针的响应被这里误当成自己的回包。
    int lockExpected = 0;
    if (!state.opLock.compare_exchange_strong(lockExpected, 1)) {
        Notify(activityId, L"正被其他流程占用，跳过");
        return;
    }
    // 离开（含所有提前 return）时归还占用、清等待标志与期望回包。
    // pendingOp 必须一并归零：响应处理器只在 pendingOp 非 None 时才做匹配过滤，
    // 若残留一个旧值，后续次数探针的回包会被当成「不匹配」丢弃，次数就永远刷新不出来。
    struct OpLockGuard {
        LatestActState* s;
        ~OpLockGuard() {
            // 先清 pendingOp、再放锁：锁一旦归零，次数探针随时可能接手，
            // 若此时 pendingOp 还残留旧值，探针的回包会被匹配逻辑丢弃，
            // 次数就会被误报成「不可用」。顺序不能颠倒。
            s->pendingOp = static_cast<uint32_t>(PendingOp::None);
            s->waitingResponse = false;
            int expected = 1;
            s->opLock.compare_exchange_strong(expected, 0);
        }
    } guard{&state};

    Notify(activityId, L"正在获取活动信息...");
    if (!RefreshOneActivity(desc, state)) {
        Notify(activityId, L"获取活动信息失败");
        return;
    }
    if (state.playCount.load() <= 0) {
        Notify(activityId, L"今日次数已用完");
        return;
    }
    if (state.restTime.load() > 0) {
        Notify(activityId, L"冷却中，跳过");
        return;
    }

    // 最多循环一个安全上限，防服务端不回次数时不至于死循环。
    constexpr int kMaxRoundsPerActivity = 60;
    int round = 0;
    while (g_latestBatchRunning.load() && round < kMaxRoundsPerActivity) {
        const int before = state.playCount.load();
        if (before <= 0) {
            break;
        }

        ++round;
        Notify(activityId, L"第 " + std::to_wstring(round) + L" 次（剩余 "
                              + std::to_wstring(before) + L"）...");

        bool ok = false;
        if (ShouldAttemptSweep(desc, state)) {
            ok = TrySweep(desc, state);
        }
        if (!ok) {
            ok = TryPlayOnce(desc, state);

            // 870 的自锁恢复：服务端回 result=3「在游戏中」，说明上一局的收尾
            // end_game 没送达。补发一次收尾包后重试开局，而不是直接放弃整个活动
            // ——否则用户会看到「月饼大胃王无法完成」且当天再也进不去。
            if (!ok && desc.id == LatestAct::ACT_MOON_CAKE &&
                state.lastStartResult.load() == 3) {
                if (state.lastGameCheckCode.load() != 0) {
                    Notify(activityId, L"检测到上一局未结束，正在收尾后重试...");
                    TryRecoverMoonCakeSession(state);
                    std::this_thread::sleep_for(std::chrono::milliseconds(300));
                    ok = TryPlayOnce(desc, state);
                } else {
                    Notify(activityId, L"服务端仍处在上一局游戏中且无本地会话种子，请手动进游戏完成或退出一局后重试");
                }
            }
        }
        if (!ok) {
            // 开局被服务端拒绝时给出可读原因。
            const int startResult = state.lastStartResult.load();
            if (startResult == 3) {
                if (desc.id == LatestAct::ACT_MOON_CAKE && state.lastGameCheckCode.load() == 0) {
                    // 已在前序分支给出“无本地会话种子”的具体指引，不再覆盖
                } else {
                    Notify(activityId, L"服务端仍认为在游戏中，停止该活动");
                }
            } else if (startResult > 0) {
                Notify(activityId, L"开局被拒绝（result=" +
                                       std::to_wstring(startResult) + L"），停止该活动");
            } else {
                Notify(activityId, L"执行失败，停止该活动");
            }
            break;
        }

        // 每轮结束重查次数：次数变化既用于续跑判断，也自动回填前端显示。
        if (!RefreshOneActivity(desc, state)) {
            Notify(activityId, L"次数回查失败，停止该活动");
            break;
        }
        const int after = state.playCount.load();
        if (after <= 0) {
            Notify(activityId, L"次数已用完");
            break;
        }
        if (state.restTime.load() > 0) {
            Notify(activityId, L"进入冷却");
            break;
        }
        if (after >= before) {
            // 次数没有减少说明这一轮没被服务端接受，继续循环只会空转。
            Notify(activityId, L"次数未减少，停止该活动");
            break;
        }
    }

    if (round >= kMaxRoundsPerActivity) {
        Notify(activityId, L"达到单次执行上限，停止该活动");
    }
}

}  // anonymous namespace

// ============================================================================
// 对外接口
// ============================================================================

bool IsLatestActivity(uint32_t activityId) {
    return FindDesc(activityId) != nullptr;
}

const char* GetLatestActivityOpenUIOperation(uint32_t activityId) {
    const LatestActDesc* desc = FindDesc(activityId);
    return desc ? desc->openUiOp : "open_ui";
}

// 次数探针与一键流程会路由到同一响应处理器并互相清除等待标志，故串行化。
// 探针拿不到锁时返回 false，调用方应上报「未知」而非「不可用」。
bool BeginLatestActivityProbe(uint32_t activityId) {
    LatestActState* state = StateOf(activityId);
    if (!state) {
        return false;
    }
    int expected = 0;
    return state->opLock.compare_exchange_strong(expected, 2);
}

void EndLatestActivityProbe(uint32_t activityId) {
    LatestActState* state = StateOf(activityId);
    if (!state) {
        return;
    }
    int expected = 2;
    state->opLock.compare_exchange_strong(expected, 0);
}

int GetLatestActivityPlayCount(uint32_t activityId) {
    LatestActState* state = StateOf(activityId);
    return state ? state->playCount.load() : -1;
}

BOOL SendLatestActivityOpenUIPacket(uint32_t activityId) {
    const LatestActDesc* desc = FindDesc(activityId);
    LatestActState* state = StateOf(activityId);
    if (!desc || !state) {
        return FALSE;
    }
    state->pendingOp = static_cast<uint32_t>(PendingOp::OpenUi);
    state->waitingResponse = true;
    const BOOL sent = SendLatestPacket(activityId, desc->openUiOp);
    if (!sent) {
        state->pendingOp = static_cast<uint32_t>(PendingOp::None);
        state->waitingResponse = false;
    }
    return sent;
}

/// 把回包的 operation 串映射成本模块的等待类型；None 表示本模块不认识的操作。
PendingOp PendingOpOf(const LatestActDesc& desc, const std::string& operation) {
    if (operation == desc.openUiOp) {
        return PendingOp::OpenUi;
    }
    if (operation == "start_game") {
        return PendingOp::StartGame;
    }
    if (operation == "sweep_info") {
        return PendingOp::SweepInfo;
    }
    if (operation == "sweep") {
        return PendingOp::Sweep;
    }
    if (operation == "end_game") {
        return PendingOp::EndGame;
    }
    return PendingOp::None;
}

void ProcessLatestActivityResponse(const GamePacket& packet) {
    LatestActState* state = StateOf(packet.params);
    const LatestActDesc* desc = FindDesc(packet.params);
    if (!state || !desc) {
        return;
    }

    size_t offset = 0;
    std::string operation;
    if (!ReadBodyString(packet.body.data(), packet.body.size(), offset, operation)) {
        state->waitingResponse = false;
        return;
    }

    // play_game 这类过程包即使被服务端回送也不在本模块的解析范围内。
    const PendingOp op = PendingOpOf(*desc, operation);
    if (op == PendingOp::None) {
        return;
    }

    // 关键：正在等待某个回包时，只接受与之匹配的那一个。
    // 否则上一轮迟到的回包（例如刚超时的 sweep_info）会提前清掉 waitingResponse，
    // 调用方随即读到尚未更新的 sweepReady/checkCode，把「没扫荡成功」误判成成功。
    const uint32_t pending = state->pendingOp.load();
    if (pending != static_cast<uint32_t>(PendingOp::None) &&
        pending != static_cast<uint32_t>(op)) {
        return;
    }

    switch (op) {
        case PendingOp::OpenUi:
            ParseOpenUI(*desc, *state, packet, offset);
            break;
        case PendingOp::StartGame:
            ParseStartGame(*desc, *state, packet, offset);
            break;
        case PendingOp::SweepInfo:
            ParseSweepInfo(*desc, *state, packet, offset);
            break;
        case PendingOp::Sweep:
            ParseSweepResult(*desc, *state, packet, offset);
            state->playCount = -1;
            break;
        case PendingOp::EndGame:
            ParseEndGame(*desc, *state, packet, offset);
            // 结算完成后次数已变化，标记为未知，由流程重查回填。
            state->playCount = -1;
            break;
        case PendingOp::None:
            return;
    }

    // 解析完成后才放行等待方，避免调用方读到上一轮的旧值（checkCode/sweepReady）。
    state->waitingResponse = false;
}

BOOL StartOneKeyLatestActivityPacket(uint32_t activityId) {
    return StartOneKeyLatestActivitiesPacket(std::vector<uint32_t>{activityId});
}

// 批量一键执行：单线程顺序处理，逐活动循环到次数耗尽为止。
//
// 早期实现由前端按 300ms 间隔逐个下发，每个活动各起一个线程，属于并发执行：
// 多个活动会同时占用响应等待、互相抢 ACTIVITY_QUERY_BACK，且只跑一次就收工。
// 现改为单个 worker 串行执行：一个活动彻底做完（次数为 0 或进入冷却）再换下一个，
// 每完成一轮就把新次数回填给前端，无需手动点刷新。
BOOL StartOneKeyLatestActivitiesPacket(const std::vector<uint32_t>& activityIds) {
    if (GetLastGameSocket() == 0 || g_userId.load() == 0) {
        UIBridge::Instance().UpdateHelperText(L"最新活动：未连接游戏，无法执行");
        return FALSE;
    }

    // 过滤掉本模块不认识的活动。
    std::vector<const LatestActDesc*> targets;
    for (uint32_t activityId : activityIds) {
        const LatestActDesc* desc = FindDesc(activityId);
        if (desc) {
            targets.push_back(desc);
        }
    }
    if (targets.empty()) {
        return FALSE;
    }

    bool expected = false;
    if (!g_latestBatchRunning.compare_exchange_strong(expected, true)) {
        UIBridge::Instance().UpdateHelperText(L"最新活动：一键执行进行中，请勿重复启动");
        return FALSE;
    }

    try {
        std::thread worker([targets]() {
            for (const LatestActDesc* desc : targets) {
                if (!g_latestBatchRunning.load()) {
                    break;
                }
                RunOneActivityUntilDone(*desc);
            }
            const bool cancelled = !g_latestBatchRunning.load();
            g_latestBatchRunning = false;
            if (cancelled) {
                UIBridge::Instance().UpdateHelperText(L"最新活动：一键执行已停止");
            } else {
                UIBridge::Instance().UpdateHelperText(L"最新活动：一键执行完成");
            }
        });
        worker.detach();
    } catch (...) {
        g_latestBatchRunning = false;
        UIBridge::Instance().UpdateHelperText(L"最新活动：启动线程失败");
        return FALSE;
    }
    return TRUE;
}

void StopLatestActivities() {
    if (g_latestBatchRunning.exchange(false)) {
        UIBridge::Instance().UpdateHelperText(L"最新活动：正在停止...");
    }
}
