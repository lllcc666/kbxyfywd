#include "activity_bus.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include "hook_shared.h"
#include "managed_thread.h"
#include "packet_builder.h"
#include "packet_protocol.h"
#include "task_zone_internal.h"
#include "ui_bridge.h"
#include "wpe_hook.h"

// 活动总线实现。两条总线的报文形状见 activity_bus.h 的注释。

namespace {

// 活动动作包之间的发送间隔。这类包服务端不回即时确认，
// 发完就走即可；留 300ms 只是避免同一场景里连续动作过于密集。
constexpr DWORD ACTIVITY_STEP_MS = 300;

// 进场景后留出客户端拉图、拉 NPC 的时间。
constexpr DWORD SCENE_SETTLE_MS = 2500;

// 等 start_game 回包的上限。服务端要先建一局再回，通常 300ms 内到。
constexpr DWORD START_GAME_WAIT_MS = 3000;
constexpr DWORD START_GAME_POLL_MS = 100;

std::atomic<bool> g_runCompetitionRunning{false};
std::atomic<int> g_runCompetitionCheckCode{0};
std::atomic<bool> g_runCompetitionStartDone{false};
std::atomic<bool> g_jinpingfuWishRunning{false};
std::atomic<bool> g_gomokuRunning{false};

struct RunningGuard {
    std::atomic<bool>& flag;
    ~RunningGuard() { flag.store(false); }
};

struct SendCounter {
    int sent = 0;
    int failed = 0;
    const wchar_t* note = nullptr;

    void Add(BOOL ok) {
        if (ok) {
            ++sent;
        } else {
            ++failed;
        }
    }
};

// 把发包结果报回界面。这样"无效"至少能区分成
// "一条都没发出去"（socket 没就绪 / 包被拦）和"发出去了服务端没反应"。
void ReportActivityResult(const wchar_t* name, const SendCounter& counter) {
    wchar_t buf[256] = {0};
    if (counter.note != nullptr) {
        swprintf_s(buf, 256, L"%s：已发 %d 包，失败 %d 包，%s", name, counter.sent, counter.failed,
                   counter.note);
    } else {
        swprintf_s(buf, 256, L"%s：已发 %d 包，失败 %d 包", name, counter.sent, counter.failed);
    }

    std::wstring script = L"if (window.updateDailyTaskStatus) { window.updateDailyTaskStatus(\"";
    script += buf;
    script += L"\", false); }";
    PostScriptToUI(script);
}

// 无论从哪条 return / break 出去，都先把护栏复位、再把结果报到界面。
struct ReportOnExit {
    SendCounter& counter;
    const wchar_t* name;

    ~ReportOnExit() { ReportActivityResult(name, counter); }
};

BOOL SendEmptyPacket(uint32_t opcode, uint32_t params) {
    auto packet = PacketBuilder().SetOpcode(opcode).SetParams(params).Build();
    return SendPacketBytes(packet);
}

// 读 body 开头的 u16 长度前缀字符串（ASCII），与 PacketBuilder::WriteString 对称。
// length==0 时 ReadPacketString 同样返回空串并返回 true，语义一致。
bool ReadBodyString(const GamePacket& packet, size_t& offset, std::string& out) {
    return ReadPacketString(packet.body.data(), packet.body.size(), offset, out);
}

}  // namespace

BOOL SendActivityAction(int activityId, const char* action) {
    if (action == nullptr) {
        return FALSE;
    }
    return SendPacketBytes(BuildActivityPacket(Opcode::ACTIVITY_ACTION_SEND,
                                               static_cast<uint32_t>(activityId), action, {}));
}

BOOL SendActivityActionWithInts(int activityId,
                                const char* action,
                                const std::vector<int32_t>& bodyValues) {
    if (action == nullptr) {
        return FALSE;
    }
    return SendPacketBytes(BuildActivityPacket(Opcode::ACTIVITY_ACTION_SEND,
                                               static_cast<uint32_t>(activityId), action,
                                               bodyValues));
}

BOOL SendActivityQingyangPacket(int commandCode, const std::vector<int32_t>& bodyValues) {
    PacketBuilder builder;
    builder.SetOpcode(Opcode::ACTIVITY_QINGYANG_SEND)
        .SetParams(static_cast<uint32_t>(commandCode));

    for (const int32_t value : bodyValues) {
        builder.WriteInt32(value);
    }

    return SendPacketBytes(builder.Build());
}

void ProcessRunCompetitionResponse(const GamePacket& packet) {
    if (packet.params != static_cast<uint32_t>(Opcode::ACT_RUN_COMPETITION)) {
        return;
    }

    size_t offset = 0;
    std::string action;
    if (!ReadBodyString(packet, offset, action)) {
        return;
    }

    if (action == "start_game") {
        // RunModel.as:316-322：先读 result，紧接着读 checkCode 并存进 KBDataCenter。
        g_runCompetitionCheckCode.store(0);
        if (offset + 4 <= packet.body.size()) {
            const int result = ReadInt32LE(packet.body.data(), offset);
            if (result == 0 && offset + 4 <= packet.body.size()) {
                g_runCompetitionCheckCode.store(ReadInt32LE(packet.body.data(), offset));
            }
        }
        // 无论成功失败都放行，避免线程空等到超时。
        g_runCompetitionStartDone.store(true);
    }
}

void StartRunCompetitionAsync(int sweepTimes) {
    bool expected = false;
    if (!g_runCompetitionRunning.compare_exchange_strong(expected, true)) {
        return;  // 上一轮还在跑，忽略这次点击
    }

    if (sweepTimes <= 0) {
        sweepTimes = 1;
    }

    const bool started = ManagedThread::Start(L"run_competition", [sweepTimes]() {
        RunningGuard guard{g_runCompetitionRunning};
        SendCounter counter;
        ReportOnExit report{counter, L"障碍赛跑"};

        // 1) open_ui：打开活动界面（RunControl.as:120 OPEN_ACTIVE 分支）。
        counter.Add(SendActivityAction(Opcode::ACT_RUN_COMPETITION, "open_ui"));
        if (!ManagedThread::SleepCancellable(ACTIVITY_STEP_MS)) {
            return;
        }

        // 2) start_game 1：真开一局。RunControl.as:211-214 只有 _popRule == 1 时才发包。
        g_runCompetitionCheckCode.store(0);
        g_runCompetitionStartDone.store(false);
        counter.Add(SendActivityActionWithInts(Opcode::ACT_RUN_COMPETITION, "start_game", {1}));

        bool gotStart = false;
        for (DWORD waited = 0; waited < START_GAME_WAIT_MS; waited += START_GAME_POLL_MS) {
            if (ManagedThread::StopRequested()) {
                return;
            }
            if (g_runCompetitionStartDone.load()) {
                gotStart = true;
                break;
            }
            if (!ManagedThread::SleepCancellable(START_GAME_POLL_MS)) {
                return;
            }
        }
        if (!gotStart) {
            counter.note = L"start_game 无回包，扫荡前置条件不满足";
            return;
        }

        // 3) end_game：交回本局结果。checkCode 算法照 RunControl.as:176，
        //    checkCode & (userId % checkCode)；拿不到 checkCode 时发 0 让服务端自己判。
        const int rawCode = g_runCompetitionCheckCode.load();
        const uint32_t userId = g_userId.load();
        int checkCode = 0;
        if (rawCode > 0) {
            checkCode = rawCode & static_cast<int>(userId % static_cast<uint32_t>(rawCode));
        }

        std::vector<int32_t> endBody;
        endBody.push_back(checkCode);
        endBody.push_back(0);                  // iconNum：本次吃到的图标数
        endBody.insert(endBody.end(), 14, 0);  // _awardNums 初值就是 14 个 0
        counter.Add(SendActivityActionWithInts(Opcode::ACT_RUN_COMPETITION, "end_game", endBody));
        if (!ManagedThread::SleepCancellable(ACTIVITY_STEP_MS)) {
            return;
        }

        // 4) sweep_info：查扫荡产出（RunModel.as:126-146）。
        counter.Add(SendActivityAction(Opcode::ACT_RUN_COMPETITION, "sweep_info"));
        if (!ManagedThread::SleepCancellable(ACTIVITY_STEP_MS)) {
            return;
        }

        // 5) sweep：官方扫荡入口（SweepUI.as:80 sendCmd([GET_SWEEP])）。
        for (int i = 0; i < sweepTimes; ++i) {
            if (ManagedThread::StopRequested()) {
                break;
            }
            counter.Add(SendActivityAction(Opcode::ACT_RUN_COMPETITION, "sweep"));
            if (!ManagedThread::SleepCancellable(ACTIVITY_STEP_MS)) {
                return;
            }
        }

        // 6) reward：领兑换奖励。
        if (!ManagedThread::StopRequested()) {
            counter.Add(SendActivityAction(Opcode::ACT_RUN_COMPETITION, "reward"));
        }
    });

    if (!started) {
        g_runCompetitionRunning.store(false);
    }
}

void StartJinpingfuWishAsync(int roundCount, int holdMs) {
    bool expected = false;
    if (!g_jinpingfuWishRunning.compare_exchange_strong(expected, true)) {
        return;  // 上一轮还在跑，忽略这次点击
    }

    if (roundCount <= 0) {
        roundCount = 1;
    }
    if (holdMs <= 0) {
        holdMs = 300;
    }

    const bool started = ManagedThread::Start(L"jinpingfu_wish", [roundCount, holdMs]() {
        RunningGuard guard{g_jinpingfuWishRunning};
        SendCounter counter;
        ReportOnExit report{counter, L"金平府祈愿"};

        // 祈愿是场景活动：必须先站在 22004 里，否则 start 没有可作用的场景对象。
        SendEnterScenePacket(Opcode::SCENE_JINPINGFU);
        if (!ManagedThread::SleepCancellable(SCENE_SETTLE_MS)) {
            return;
        }

        for (int i = 0; i < roundCount; ++i) {
            if (ManagedThread::StopRequested()) {
                break;
            }

            counter.Add(SendActivityAction(Opcode::ACT_JINPINGFU_WISH, "start"));
            if (!ManagedThread::SleepCancellable(holdMs)) {
                return;
            }

            counter.Add(SendActivityAction(Opcode::ACT_JINPINGFU_WISH, "finish"));
            if (!ManagedThread::SleepCancellable(ACTIVITY_STEP_MS)) {
                return;
            }
        }
    });

    if (!started) {
        g_jinpingfuWishRunning.store(false);
    }
}

void StartGomokuAsync(int roundCount) {
    bool expected = false;
    if (!g_gomokuRunning.compare_exchange_strong(expected, true)) {
        return;  // 上一轮还在跑，忽略这次点击
    }

    if (roundCount <= 0) {
        roundCount = 1;
    }

    const bool started = ManagedThread::Start(L"gomoku_battle", [roundCount]() {
        RunningGuard guard{g_gomokuRunning};
        SendCounter counter;
        ReportOnExit report{counter, L"场景活动"};

        SendEnterScenePacket(Opcode::SCENE_GOMOKU);
        if (!ManagedThread::SleepCancellable(SCENE_SETTLE_MS)) {
            return;
        }

        for (int i = 0; i < roundCount; ++i) {
            if (ManagedThread::StopRequested()) {
                break;
            }

            // 进场后补拉玩家列表（1184317）和 NPC 列表（1184782），
            // 服务端才会把该场景的 NPC 推下来。
            counter.Add(SendEmptyPacket(Opcode::PLAYER_LIST_SEND, 0));
            if (!ManagedThread::SleepCancellable(ACTIVITY_STEP_MS)) {
                return;
            }
            counter.Add(SendEmptyPacket(Opcode::NPC_LIST_SEND, 0));
            if (!ManagedThread::SleepCancellable(ACTIVITY_STEP_MS)) {
                return;
            }

            // 旧活动总线：p0=900 登记进入 / p0=902 确认 / p0=325 收尾，
            // body 是 int32 列表（activityId 排在第二位）。
            counter.Add(SendActivityQingyangPacket(900, {1, Opcode::ACT_GOMOKU}));
            if (!ManagedThread::SleepCancellable(ACTIVITY_STEP_MS)) {
                return;
            }
            counter.Add(SendActivityQingyangPacket(902, {1, Opcode::ACT_GOMOKU}));
            if (!ManagedThread::SleepCancellable(ACTIVITY_STEP_MS)) {
                return;
            }
            counter.Add(SendActivityQingyangPacket(325, {4}));
            if (!ManagedThread::SleepCancellable(ACTIVITY_STEP_MS)) {
                return;
            }

            // 点 NPC 18004 拉起对局。后续对话由客户端自己的 AS3 推进——
            // 对话节点号每局都由服务端重新下发，复制历史节点号必然失效。
            counter.Add(SendTaskZoneClickPacket(static_cast<uint32_t>(Opcode::NPC_GOMOKU)));
            if (!ManagedThread::SleepCancellable(SCENE_SETTLE_MS)) {
                return;
            }
        }
    });

    if (!started) {
        g_gomokuRunning.store(false);
    }
}
