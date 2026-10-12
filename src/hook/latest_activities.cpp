#include "latest_activities.h"

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
    int targetScore;        ///< 走游戏流程时提交的目标分（= 各自通关线）
    bool sweepWithCount;    ///< sweep 是否带次数参数（647/869/749/870 需要）
};

const LatestActDesc kActivities[] = {
    {LatestAct::ACT_MAZE, L"突破迷阵", "open_ui", 180, true},
    {LatestAct::ACT_TOUCH_STAR, L"摘星之旅", "open_ui", 301, true},
    {LatestAct::ACT_EXPLORE, L"寒冰洞历险记", "game_info", 300, true},
    {LatestAct::ACT_MOON_CAKE, L"月饼大胃王", "open_ui", 40, true},
    {LatestAct::ACT_FLOWER, L"夺回花束行动", "game_info", 70, false},
    {LatestAct::ACT_FOOD, L"美食大作战", "open_ui", 400, false},
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
};

LatestActState g_state[kActivityNum];

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
                      const std::vector<int32_t>& bodyValues = {}) {
    std::vector<BYTE> packet = BuildActivityPacket(
        Opcode::ACTIVITY_QINGYANG_NEW_SEND, activityId, operation, bodyValues);
    if (packet.empty()) {
        return FALSE;
    }
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()),
                      Opcode::ACTIVITY_QUERY_BACK, WpeHook::TIMEOUT_RESPONSE,
                      activityId, true);
}

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

// 与既有活动同策略：有可重放成绩且不低于「走一遍」的目标分时才扫荡。
bool ShouldAutoSweep(int bestScore, int flowScore) {
    return bestScore > 0 && bestScore >= flowScore;
}

// 各活动通用的 checkCode 变换：checkCode & (uid % checkCode)
int MaskedCheckCode(int seed) {
    if (seed <= 0) {
        return 0;
    }
    const uint32_t userId = g_userId.load();
    return seed & static_cast<int>(userId % static_cast<uint32_t>(seed));
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
            if (read() != 0) { state.playCount = 0; return; }
            if (offset + 12 > size) return;
            state.playCount = read();     // playTime
            state.restTime = read();
            state.ruleFlag = read();      // popRule
            break;

        case LatestAct::ACT_MOON_CAKE:
            // playCount, restTime, bubbleNum, ruleFlag, isSweep, rewardList[4], ...
            if (offset + 16 > size) return;
            state.playCount = read();
            state.restTime = read();
            offset += 4;                  // bubbleNum
            state.ruleFlag = read();      // ruleFlag（start_game 参数）
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

// start_game 响应：result, [leftPlay], checkCode[, checkCode2]
void ParseStartGame(const LatestActDesc& desc, LatestActState& state,
                    const GamePacket& packet, size_t offset) {
    const BYTE* body = packet.body.data();
    const size_t size = packet.body.size();
    if (offset + 12 > size) return;
    if (ReadInt32LE(body, offset) != 0) return;   // result != 0 视为开局失败
    offset += 4;
    if (desc.id == LatestAct::ACT_TOUCH_STAR) {
        offset += 4;                              // leftPlay
    }
    state.checkCode = ReadInt32LE(body, offset);
    if (desc.id == LatestAct::ACT_MOON_CAKE && offset + 4 <= size) {
        state.checkCode2 = ReadInt32LE(body, offset);
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
            // [end_game, checkCode, awardList...]；主流程不逐星收集，按长度补 0。
            std::vector<int32_t> body;
            body.reserve(18);
            body.push_back(MaskedCheckCode(seed));
            body.insert(body.end(), 16, 0);
            return body;
        }

        case LatestAct::ACT_EXPLORE:
            // [end_game, end_code]，end_code = uid % 1000 + check_code + num
            return {static_cast<int>(userId % 1000u) + seed + score};

        case LatestAct::ACT_MOON_CAKE:
            // [end_game, passSum, endType, checkCode, score]
            // checkCode = seed * (uid % 1000) * (checkCode2 + 1)
            return {0, 4, static_cast<int>(static_cast<int64_t>(seed) *
                                           static_cast<int64_t>(userId % 1000u) *
                                           static_cast<int64_t>(state.checkCode2.load() + 1)),
                    score};

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
    state.waitingResponse = true;
    if (!SendLatestPacket(desc.id, "sweep_info") || !WaitForResponse(state, 3000)) {
        state.waitingResponse = false;
        return false;
    }
    if (!state.sweepReady.load()) {
        return false;   // 尚无历史成绩，需先走一局
    }

    // 647/869/749/870 的 sweep 需要带上本次扫荡次数（取剩余次数）。
    const int count = state.playCount.load() > 0 ? state.playCount.load() : 1;
    const std::vector<int32_t> sweepBody = desc.sweepWithCount
        ? std::vector<int32_t>{count}
        : std::vector<int32_t>{};

    state.waitingResponse = true;
    if (!SendLatestPacket(desc.id, "sweep", sweepBody) || !WaitForResponse(state, 3000)) {
        state.waitingResponse = false;
        return false;
    }
    return state.sweepReady.load();
}

// 走一局：start_game -> 校验种子 -> end_game 提交目标分
bool TryPlayOnce(const LatestActDesc& desc, LatestActState& state) {
    state.checkCode = 0;
    state.checkCode2 = 0;

    state.waitingResponse = true;
    if (!SendLatestPacket(desc.id, "start_game", BuildStartGameBody(desc, state)) ||
        !WaitForResponse(state, 5000)) {
        state.waitingResponse = false;
        return false;
    }
    if (state.checkCode.load() == 0) {
        return false;
    }

    state.waitingResponse = true;
    if (!SendLatestPacket(desc.id, "end_game", BuildEndGameBody(desc, state)) ||
        !WaitForResponse(state, 5000)) {
        state.waitingResponse = false;
        return false;
    }
    return true;
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
    state->waitingResponse = true;
    const BOOL sent = SendLatestPacket(activityId, desc->openUiOp);
    if (!sent) {
        state->waitingResponse = false;
    }
    return sent;
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
        return;
    }

    state->waitingResponse = false;

    if (operation == desc->openUiOp) {
        ParseOpenUI(*desc, *state, packet, offset);
    } else if (operation == "start_game") {
        ParseStartGame(*desc, *state, packet, offset);
    } else if (operation == "sweep_info") {
        ParseSweepInfo(*desc, *state, packet, offset);
    } else if (operation == "sweep") {
        ParseSweepResult(*desc, *state, packet, offset);
        state->playCount = -1;
    } else if (operation == "end_game") {
        // 结算完成后次数已变化，标记为未知，由流程重查回填。
        state->playCount = -1;
    }
}

BOOL StartOneKeyLatestActivityPacket(uint32_t activityId) {
    const LatestActDesc* descPtr = FindDesc(activityId);
    if (!descPtr) {
        return FALSE;
    }
    const LatestActDesc desc = *descPtr;

    std::thread worker([desc]() {
        const uint32_t activityId = desc.id;
        LatestActState* state = StateOf(activityId);
        if (!state) {
            return;
        }

        state->playCount = -1;
        state->checkCode = 0;

        Notify(activityId, L"正在获取活动信息...");
        if (!SendLatestActivityOpenUIPacket(activityId) || !WaitForResponse(*state, 5000)) {
            Notify(activityId, L"获取活动信息失败");
            return;
        }
        if (state->playCount.load() <= 0) {
            Notify(activityId, L"今日次数已用完");
            return;
        }
        if (state->restTime.load() > 0) {
            Notify(activityId, L"冷却中，请稍后再试");
            return;
        }

        bool ok = false;
        if (ShouldAutoSweep(state->bestScore.load(), desc.targetScore)) {
            Notify(activityId, L"执行扫荡...");
            ok = TrySweep(desc, *state);
        }
        if (!ok) {
            Notify(activityId, L"执行游戏流程...");
            ok = TryPlayOnce(desc, *state);
        }
        if (!ok) {
            Notify(activityId, L"执行失败");
            return;
        }

        state->playCount = -1;
        if (SendLatestActivityOpenUIPacket(activityId)) {
            WaitForResponse(*state, 3000);
        }
        Notify(activityId, L"完成");
    });
    worker.detach();
    return TRUE;
}
