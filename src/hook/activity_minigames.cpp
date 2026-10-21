#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include <random>
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
            ManagedThread::SleepCancellable(100);
        }
        const bool received = !ACT685_STATE.waitingResponse.load();
        ACT685_STATE.waitingResponse = false;
        return received;
    };

    ManagedThread::SleepCancellable(300);
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
        ManagedThread::SleepCancellable(300);
        UIBridge::Instance().UpdateHelperText(L"航海大挑战：正在获取扫荡信息...");

        SendAct685SweepInfoPacket();
        if (waitForResponse() && ACT685_STATE.sweepAvailable.load()) {
            ManagedThread::SleepCancellable(300);
            UIBridge::Instance().UpdateHelperText(L"航海大挑战：执行扫荡...");
            ACT685_STATE.endResult = -1;
            SendAct685SweepPacket();
            if (waitForResponse() && ACT685_STATE.endResult.load() != 3) {
                ManagedThread::SleepCancellable(300);
                SendAct685OpenUIPacket();
                waitForResponse();
                UIBridge::Instance().UpdateHelperText(L"航海大挑战：扫荡完成");
                return 0;
            }
        }

        UIBridge::Instance().UpdateHelperText(L"航海大挑战：扫荡失败，改为直接完成");
    }

    ManagedThread::SleepCancellable(300);
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

    ManagedThread::SleepCancellable(500);
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

    ManagedThread::SleepCancellable(300);
    SendAct685OpenUIPacket();
    waitForResponse();
    UIBridge::Instance().UpdateHelperText(L"航海大挑战：结算完成");
    return 0;
}

BOOL StartOneKeyAct685Packet(bool useSweep) {
    ACT685_STATE.useSweep = useSweep;
    // 走受管线程：退出时统一 RequestStopAll/JoinAll，不给脱管线程留活口。
    if (ManagedThread::Start(L"act685", []() { Act685ThreadProc(nullptr); })) {
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
            ManagedThread::SleepCancellable(100);
        }
        const bool received = !ACT666_STATE.waitingResponse.load();
        ACT666_STATE.waitingResponse = false;
        return received;
    };

    ManagedThread::SleepCancellable(300);
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

        ManagedThread::SleepCancellable(300);
        SendAct666SweepInfoPacket();
        if (waitForResponse()) {
            ManagedThread::SleepCancellable(300);
            SendAct666SweepPacket();
            if (waitForResponse()) {
                ManagedThread::SleepCancellable(300);
                SendAct666OpenUIPacket();
                waitForResponse();
                UIBridge::Instance().UpdateHelperText(L"天之骄子的特训：扫荡完成");
                return 0;
            }
        }

        UIBridge::Instance().UpdateHelperText(L"天之骄子的特训：扫荡失败，改为直接完成");
    }

    ManagedThread::SleepCancellable(300);
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

    ManagedThread::SleepCancellable(500);
    UIBridge::Instance().UpdateHelperText(L"天之骄子的特训：直接提交结算...");
    SendAct666EndGamePacket(1, true);
    if (!waitForResponse()) {
        UIBridge::Instance().UpdateHelperText(L"天之骄子的特训：结算失败");
        return 0;
    }

    ManagedThread::SleepCancellable(300);
    SendAct666OpenUIPacket();
    waitForResponse();
    UIBridge::Instance().UpdateHelperText(L"天之骄子的特训：结算完成");
    return 0;
}

BOOL StartOneKeyAct666Packet(bool useSweep) {
    ACT666_STATE.useSweep = useSweep;
    // 走受管线程：退出时统一 RequestStopAll/JoinAll，不给脱管线程留活口。
    if (ManagedThread::Start(L"act666", []() { Act666ThreadProc(nullptr); })) {
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

    ManagedThread::SleepCancellable(300);
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
        ManagedThread::SleepCancellable(300);
        UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：正在获取扫荡信息...");
        SendAct822SweepInfoPacket();
        if (WaitForAct822Response() && g_act822SweepAvailable.load()) {
            ManagedThread::SleepCancellable(300);
            UIBridge::Instance().UpdateHelperText(L"光辉穿梭大考验：执行扫荡...");
            SendAct822SweepPacket();
            // 必须在等待之后读取结果：原先在 WaitForAct822Response() 之前快照，
            // 拿到的是上一次（或初值 -1）的陈旧值。
            const bool sweepWaited = WaitForAct822Response();
            const int sweepResult = g_act822SweepResult.load();
            if (sweepWaited && (sweepResult == 0 || sweepResult == 4)) {
                ManagedThread::SleepCancellable(300);
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

    ManagedThread::SleepCancellable(300);
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
    ManagedThread::SleepCancellable(500);
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

    ManagedThread::SleepCancellable(300);
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
    // 走受管线程：退出时统一 RequestStopAll/JoinAll，不给脱管线程留活口。
    if (ManagedThread::Start(L"act822", [pUseSweep]() { Act822ThreadProc(pUseSweep); })) {
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

    ManagedThread::SleepCancellable(300);
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
        ManagedThread::SleepCancellable(300);
        UIBridge::Instance().UpdateHelperText(L"圣石之战：正在获取扫荡信息...");
        SendAct826SweepInfoPacket();
        if (WaitForAct826Response() && ACT826_STATE.sweepAvailable.load()) {
            ManagedThread::SleepCancellable(300);
            UIBridge::Instance().UpdateHelperText(L"圣石之战：执行扫荡...");
            ACT826_STATE.sweepResult = -1;
            SendAct826SweepPacket();
            if (WaitForAct826Response() && (ACT826_STATE.sweepResult.load() == 0 || ACT826_STATE.sweepResult.load() == 4)) {
                ManagedThread::SleepCancellable(300);
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

    ManagedThread::SleepCancellable(300);
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
    ManagedThread::SleepCancellable(500);
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

    ManagedThread::SleepCancellable(300);
    SendAct826OpenUIPacket();
    WaitForAct826Response();
    return 0;
}

BOOL StartOneKeyAct826Packet(bool useSweep) {
    ACT826_STATE.useSweep = useSweep;
    bool* pUseSweep = new bool(useSweep);
    // 走受管线程：退出时统一 RequestStopAll/JoinAll，不给脱管线程留活口。
    if (ManagedThread::Start(L"act826", [pUseSweep]() { Act826ThreadProc(pUseSweep); })) {
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

    ManagedThread::SleepCancellable(300);
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
        ManagedThread::SleepCancellable(300);
        UIBridge::Instance().UpdateHelperText(L"齐心协力大挑战：正在获取扫荡信息...");
        SendAct827SweepInfoPacket();
        if (WaitForAct827Response() && ACT827_STATE.sweepAvailable.load()) {
            ManagedThread::SleepCancellable(300);
            UIBridge::Instance().UpdateHelperText(L"齐心协力大挑战：执行扫荡...");
            ACT827_STATE.sweepResult = -1;
            SendAct827SweepPacket();
            if (WaitForAct827Response() && (ACT827_STATE.sweepResult.load() == 0 || ACT827_STATE.sweepResult.load() == 4)) {
                ManagedThread::SleepCancellable(300);
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

    ManagedThread::SleepCancellable(300);
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
    ManagedThread::SleepCancellable(500);
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

    ManagedThread::SleepCancellable(300);
    SendAct827OpenUIPacket();
    WaitForAct827Response();
    return 0;
}

BOOL StartOneKeyAct827Packet(bool useSweep) {
    ACT827_STATE.useSweep = useSweep;
    bool* pUseSweep = new bool(useSweep);
    // 走受管线程：退出时统一 RequestStopAll/JoinAll，不给脱管线程留活口。
    if (ManagedThread::Start(L"act827", [pUseSweep]() { Act827ThreadProc(pUseSweep); })) {
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
            ManagedThread::SleepCancellable(100);
        }
        const bool received = !ACT641_STATE.waitingResponse.load();
        ACT641_STATE.waitingResponse = false;
        return received;
    };

    ManagedThread::SleepCancellable(300);
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

        ManagedThread::SleepCancellable(300);
        SendAct641SweepInfoPacket();
        if (waitForResponse() && ACT641_STATE.sweepSuccess.load()) {
            ManagedThread::SleepCancellable(300);
            UIBridge::Instance().UpdateHelperText(L"清除煞气：执行扫荡...");
            SendAct641SweepPacket();
            if (waitForResponse() && ACT641_STATE.sweepSuccess.load()) {
                ManagedThread::SleepCancellable(300);
                SendAct641OpenUIPacket();
                waitForResponse();
                UIBridge::Instance().UpdateHelperText(L"清除煞气：扫荡完成");
                return 0;
            }
        }

        UIBridge::Instance().UpdateHelperText(L"清除煞气：扫荡失败，改为直接完成");
    }

    ManagedThread::SleepCancellable(300);
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

    ManagedThread::SleepCancellable(500);
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

    ManagedThread::SleepCancellable(300);
    SendAct641OpenUIPacket();
    waitForResponse();
    UIBridge::Instance().UpdateHelperText(L"清除煞气：结算完成");
    return 0;
}

BOOL StartOneKeyAct641Packet(bool useSweep, int targetScore) {
    ACT641_STATE.useSweep = useSweep;
    int* pTargetScore = new int(targetScore);
    // 走受管线程：退出时统一 RequestStopAll/JoinAll，不给脱管线程留活口。
    if (ManagedThread::Start(L"act641", [pTargetScore]() { Act641ThreadProc(pTargetScore); })) {
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
            ManagedThread::SleepCancellable(100);
        }
        const bool received = !g_act810WaitingResponse.load();
        g_act810WaitingResponse = false;
        return received;
    };

    ManagedThread::SleepCancellable(300);
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
        ManagedThread::SleepCancellable(300);
        SendAct810SweepInfoPacket();
        if (waitForResponse() && g_act810SweepAvailable.load()) {
            ManagedThread::SleepCancellable(300);
            UIBridge::Instance().UpdateHelperText(L"疾速特训：执行扫荡...");
            g_act810RewardMedalCount = 0;
            g_act810RewardExp = 0;
            g_act810RewardCoin = 0;
            SendAct810SweepPacket();
            if (waitForResponse()) {
                ManagedThread::SleepCancellable(300);
                SendAct810OpenUIPacket();
                waitForResponse();
                UIBridge::Instance().UpdateHelperText(L"疾速特训：扫荡完成");
                return 0;
            }
        }

        UIBridge::Instance().UpdateHelperText(L"疾速特训：扫荡失败，改为直接完成");
    }

    ManagedThread::SleepCancellable(300);
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
        ManagedThread::SleepCancellable(250);
        if (i == 0) {
            UIBridge::Instance().UpdateHelperText(L"疾速特训：游戏进行中...");
        } else if (i == progressSteps - 1) {
            UIBridge::Instance().UpdateHelperText(L"疾速特训：准备结算...");
        }
    }

    ManagedThread::SleepCancellable(500);
    UIBridge::Instance().UpdateHelperText(L"疾速特训：直接提交结算...");
    SendAct810EndGamePacket(50);
    if (!waitForResponse()) {
        UIBridge::Instance().UpdateHelperText(L"疾速特训：结算失败");
        return 0;
    }

    ManagedThread::SleepCancellable(300);
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
    // 走受管线程：退出时统一 RequestStopAll/JoinAll，不给脱管线程留活口。
    if (ManagedThread::Start(L"act810", []() { Act810ThreadProc(nullptr); })) {
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
            ManagedThread::SleepCancellable(100);
        }
        const bool received = !ACT684_STATE.waitingResponse.load();
        ACT684_STATE.waitingResponse = false;
        return received;
    };

    ManagedThread::SleepCancellable(300);
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
        ManagedThread::SleepCancellable(300);
        UIBridge::Instance().UpdateHelperText(L"土幻的曙光：正在获取扫荡信息...");
        ACT684_STATE.sweepSuccess = false;

        if (SendAct684SweepInfoPacket() && waitForResponse() && ACT684_STATE.sweepSuccess.load()) {
            ManagedThread::SleepCancellable(300);
            UIBridge::Instance().UpdateHelperText(L"土幻的曙光：执行扫荡...");

            if (SendAct684SweepPacket() && waitForResponse() && ACT684_STATE.sweepSuccess.load()) {
                ManagedThread::SleepCancellable(300);
                SendAct684OpenUIPacket();
                waitForResponse();
                UIBridge::Instance().UpdateHelperText(L"土幻的曙光：扫荡完成");
                return 0;
            }
        }

        UIBridge::Instance().UpdateHelperText(L"土幻的曙光：扫荡失败，改为直接完成");
    }

    ManagedThread::SleepCancellable(300);
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

    ManagedThread::SleepCancellable(500);
    UIBridge::Instance().UpdateHelperText(L"土幻的曙光：直接提交结算...");

    if (!SendAct684EndGamePacket(targetScore) || !waitForResponse()) {
        UIBridge::Instance().UpdateHelperText(L"土幻的曙光：结算失败");
        return 0;
    }

    if (ACT684_STATE.endResult.load() == 3) {
        UIBridge::Instance().UpdateHelperText(L"土幻的曙光：网络错误");
        return 0;
    }

    ManagedThread::SleepCancellable(300);
    SendAct684OpenUIPacket();
    waitForResponse();
    UIBridge::Instance().UpdateHelperText(L"土幻的曙光：完成");
    return 0;
}

BOOL StartOneKeyAct684Packet(bool useSweep, int targetScore) {
    ACT684_STATE.useSweep = useSweep;
    int* pTargetScore = new int(targetScore);
    // 走受管线程：退出时统一 RequestStopAll/JoinAll，不给脱管线程留活口。
    if (ManagedThread::Start(L"act684", [pTargetScore]() { Act684ThreadProc(pTargetScore); })) {
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
            ManagedThread::SleepCancellable(100);
        }
        const bool received = !ACT717_STATE.waitingResponse.load();
        ACT717_STATE.waitingResponse = false;
        return received;
    };

    ManagedThread::SleepCancellable(300);
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
        ManagedThread::SleepCancellable(300);
        UIBridge::Instance().UpdateHelperText(L"驱赶毒蚊：正在获取扫荡信息...");
        ACT717_STATE.sweepSuccess = false;

        if (SendAct717SweepInfoPacket() && waitForResponse() && ACT717_STATE.sweepSuccess.load()) {
            ManagedThread::SleepCancellable(300);
            UIBridge::Instance().UpdateHelperText(L"驱赶毒蚊：执行扫荡...");

            if (SendAct717SweepPacket() && waitForResponse() && ACT717_STATE.sweepSuccess.load()) {
                ManagedThread::SleepCancellable(300);
                SendAct717OpenUIPacket();
                waitForResponse();
                UIBridge::Instance().UpdateHelperText(L"驱赶毒蚊：扫荡完成");
                return 0;
            }
        }

        UIBridge::Instance().UpdateHelperText(L"驱赶毒蚊：扫荡失败，改为直接完成");
    }

    ManagedThread::SleepCancellable(300);
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
    ManagedThread::SleepCancellable(500);
    UIBridge::Instance().UpdateHelperText(L"驱赶毒蚊：直接提交结算...");

    if (!SendAct717EndGamePacket(finalScore) || !waitForResponse()) {
        UIBridge::Instance().UpdateHelperText(L"驱赶毒蚊：结算失败");
        return 0;
    }

    if (ACT717_STATE.endResult.load() == 1) {
        UIBridge::Instance().UpdateHelperText(L"驱赶毒蚊：结算失败");
        return 0;
    }

    ManagedThread::SleepCancellable(300);
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
    // 走受管线程：退出时统一 RequestStopAll/JoinAll，不给脱管线程留活口。
    if (ManagedThread::Start(L"act717", [pTargetScore]() { Act717ThreadProc(pTargetScore); })) {
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
        if (len <= 0) {
            return rewardList;
        }
        // 每条 8 字节，按包内剩余字节收紧，避免异常 len 触发巨额分配。
        rewardList.reserve((std::min)(static_cast<size_t>(len), packet.body.size() / 8));
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
            ManagedThread::SleepCancellable(100);
        }
        const bool received = !ACT805_STATE.waitingResponse.load();
        ACT805_STATE.waitingResponse = false;
        return received;
    };

    ManagedThread::SleepCancellable(300);
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

        ManagedThread::SleepCancellable(300);
        SendAct805SweepInfoPacket();
        if (waitForResponse()) {
            ManagedThread::SleepCancellable(300);
            SendAct805SweepPacket();
            if (waitForResponse() && ACT805_STATE.sweepSuccess.load()) {
                ManagedThread::SleepCancellable(300);
                SendAct805GameInfoPacket();
                waitForResponse();
                UIBridge::Instance().UpdateHelperText(L"守护梦境：扫荡完成");
                return 0;
            }
        }

        UIBridge::Instance().UpdateHelperText(L"守护梦境：扫荡失败，改为直接结算");
    }

    ManagedThread::SleepCancellable(300);
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
    ManagedThread::SleepCancellable(500);
    UIBridge::Instance().UpdateHelperText(L"守护梦境：直接提交结算...");
    SendAct805EndGamePacket(finalScore);
    if (!waitForResponse()) {
        UIBridge::Instance().UpdateHelperText(L"守护梦境：结算失败");
        return 0;
    }

    ManagedThread::SleepCancellable(300);
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
    // 走受管线程：退出时统一 RequestStopAll/JoinAll，不给脱管线程留活口。
    if (ManagedThread::Start(L"act805", [pTargetScore]() { Act805ThreadProc(pTargetScore); })) {
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
        ManagedThread::SleepCancellable(100);
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

    ManagedThread::SleepCancellable(300);
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
        ManagedThread::SleepCancellable(300);
        UIBridge::Instance().UpdateHelperText(L"果宝救援：正在获取扫荡信息...");
        SendAct757SweepInfoPacket();
        if (WaitForAct757Response() && g_act757SweepAvailable.load()) {
            ManagedThread::SleepCancellable(300);
            UIBridge::Instance().UpdateHelperText(L"果宝救援：执行扫荡...");
            SendAct757SweepPacket();
            if (WaitForAct757Response() && g_act757SweepResult.load() == 0) {
                ManagedThread::SleepCancellable(300);
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

    ManagedThread::SleepCancellable(300);
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
    ManagedThread::SleepCancellable(500);
    UIBridge::Instance().UpdateHelperText(L"果宝救援：直接提交结算...");
    SendAct757EndGamePacket(finalScore);
    if (!WaitForAct757Response()) {
        UIBridge::Instance().UpdateHelperText(L"果宝救援：结算失败");
        return 0;
    }

    ManagedThread::SleepCancellable(300);
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
    // 走受管线程：退出时统一 RequestStopAll/JoinAll，不给脱管线程留活口。
    if (ManagedThread::Start(L"act757", [pUseSweep]() { Act757ThreadProc(pUseSweep); })) {
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
            ManagedThread::SleepCancellable(100);
        }
        const bool received = !ACT631_STATE.waitingResponse.load();
        ACT631_STATE.waitingResponse = false;
        return received;
    };

    auto waitForSettlementResponse = []() -> bool {
        for (int i = 0; i < 60 && ACT631_STATE.waitingResponse; ++i) {
            ManagedThread::SleepCancellable(100);
        }
        const bool received = !ACT631_STATE.waitingResponse.load();
        ACT631_STATE.waitingResponse = false;
        return received;
    };

    ManagedThread::SleepCancellable(300);
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
        ManagedThread::SleepCancellable(300);
        UIBridge::Instance().UpdateHelperText(L"妖力考验：正在获取扫荡信息...");
        ACT631_STATE.sweepSuccess = false;

        if (SendAct631SweepInfoPacket() && waitForResponse() && ACT631_STATE.sweepSuccess.load()) {
            ManagedThread::SleepCancellable(300);
            UIBridge::Instance().UpdateHelperText(L"妖力考验：执行扫荡...");

            if (SendAct631SweepPacket() && waitForResponse()) {
                ManagedThread::SleepCancellable(300);
                SendAct631OpenUIPacket();
                waitForResponse();
                UIBridge::Instance().UpdateHelperText(L"妖力考验：扫荡完成");
                return 0;
            }
        }

        UIBridge::Instance().UpdateHelperText(L"妖力考验：扫荡失败，改为直接完成");
    }

    ManagedThread::SleepCancellable(300);
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

    ManagedThread::SleepCancellable(500);
    UIBridge::Instance().UpdateHelperText(L"妖力考验：直接提交结算...");
    (void)SendAct631EndGamePacket(isPass, clientCheckCode, score);
    if (!waitForSettlementResponse()) {
        UIBridge::Instance().UpdateHelperText(L"妖力考验：结算失败");
        return 0;
    }

    ManagedThread::SleepCancellable(300);
    SendAct631OpenUIPacket();
    waitForResponse();
    UIBridge::Instance().UpdateHelperText(L"妖力考验：结算完成");
    return 0;
}

BOOL StartOneKeyAct631Packet(bool useSweep) {
    ACT631_STATE.useSweep = useSweep;
    // 走受管线程：退出时统一 RequestStopAll/JoinAll，不给脱管线程留活口。
    if (ManagedThread::Start(L"act631", []() { Act631ThreadProc(nullptr); })) {
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

    BOOL StartOneKeyAct808Packet(bool useSweep) {
        bool* pUseSweep = new bool(useSweep);
        // 走受管线程：退出时统一 RequestStopAll/JoinAll，不给脱管线程留活口。
        if (ManagedThread::Start(L"act808", [pUseSweep]() { Act808ThreadProc(pUseSweep); })) {
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
        // 走受管线程：退出时统一 RequestStopAll/JoinAll，不给脱管线程留活口。
        if (ManagedThread::Start(L"act782", [pTargetScore]() { Act782ThreadProc(pTargetScore); })) {
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
        // 走受管线程：退出时统一 RequestStopAll/JoinAll，不给脱管线程留活口。
        if (ManagedThread::Start(L"act804", [pTargetScore]() { Act804ThreadProc(pTargetScore); })) {
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
        // 走受管线程：退出时统一 RequestStopAll/JoinAll，不给脱管线程留活口。
        if (ManagedThread::Start(L"act811", []() { Act811ThreadProc(nullptr); })) {
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
        // 走受管线程：退出时统一 RequestStopAll/JoinAll。
        if (ManagedThread::Start(L"act778", []() { Act778ThreadProc(nullptr); })) {
            return TRUE;
        }
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
    






