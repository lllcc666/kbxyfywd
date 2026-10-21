#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "activity_minigames.h"
#include "activity_states_internal.h"
#include "battle_six.h"
#include "hook_shared.h"
#include "managed_thread.h"
#include "packet_builder.h"
#include "packet_parser.h"
#include "packet_protocol.h"
#include "shuangtai_internal.h"
#include "task_zone_internal.h"
#include "ui_bridge.h"
#include "utils.h"
#include "wpe_hook.h"
#include "wpe_hook_internal.h"

extern bool PostScriptToUI(const std::wstring& jsCode);

// 福瑞宝箱运行状态：hook 核心在 ENTER_SCENE_BACK 里置位 g_heavenFuruiEnteredMap，
// 因此这组标记放在文件作用域，并在 activity_minigames.h 里声明。
std::atomic<bool> g_heavenFuruiRunning{false};
std::atomic<bool> g_heavenFuruiQuerySuccess{false};
std::atomic<int> g_heavenFuruiBoxCount{0};
std::atomic<bool> g_heavenFuruiEnteredMap{false};
std::atomic<int> g_heavenFuruiMaxBoxes{30};  ///< 最大开启宝箱数量
std::atomic<int> g_heavenFuruiOpenedBoxes{0};  ///< 已开启宝箱数量

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










// ============ 天之骄子的特训功能实现 (Act666) ============

#define ACT666_STATE ActivityStateManager::Instance().GetAct666State()










// ============ 光辉穿梭大考验功能实现 (Act822) ============

#define ACT822_STATE ActivityStateManager::Instance().GetAct822State()













// ============ 圣石之战功能实现 (Act826) ============

#define ACT826_STATE ActivityStateManager::Instance().GetAct826State()












// ============ 齐心协力大挑战功能实现 (Act827) ============

#define ACT827_STATE ActivityStateManager::Instance().GetAct827State()












// ============ 清除煞气功能实现 (Act641) ============

#define ACT641_STATE ActivityStateManager::Instance().GetAct641State()










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










// ============ 土幻的曙光功能实现 (Act684) ============

#define ACT684_STATE ActivityStateManager::Instance().GetAct684State()










// ============ 驱赶毒蚊功能实现 (Act717) ============

#define ACT717_STATE ActivityStateManager::Instance().GetAct717State()










// ============ 守护梦境功能实现 (Act805) ============

#define ACT805_STATE ActivityStateManager::Instance().GetAct805State()











// ============ 果宝救援功能实现 (Act757) ============

#define ACT757_STATE ActivityStateManager::Instance().GetAct757State()












// ============ 妖力考验功能实现 (Act631) ============

#define ACT631_STATE ActivityStateManager::Instance().GetAct631State()











// Act808 剩余次数：活动次数查询模块要读它，因此提到文件作用域。
static std::atomic<int> g_act808PlayCount{0};

// ============ 欢乐跷跷板功能实现 (Act808) ============

    static constexpr int ACT808_WAIT_NONE = 0;
    static constexpr int ACT808_WAIT_OPEN_UI = 1;
    static constexpr int ACT808_WAIT_START_GAME = 2;
    static constexpr int ACT808_WAIT_END_GAME = 3;
    static constexpr int ACT808_WAIT_SWEEP_INFO = 4;
    static constexpr int ACT808_WAIT_SWEEP = 5;

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


int GetAct808PlayCount() {
    return g_act808PlayCount.load();
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
        // 走受管线程：退出时统一 RequestStopAll/JoinAll，不给脱管线程留活口。
        if (ManagedThread::Start(L"heavenfurui", []() { HeavenFuruiThreadProc(nullptr); })) {
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
    






