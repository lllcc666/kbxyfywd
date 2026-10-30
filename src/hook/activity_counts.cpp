#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "activity_states_internal.h"
#include "battle_six.h"
#include "hook_shared.h"
#include "latest_activities.h"
#include "packet_builder.h"
#include "ui_bridge.h"
#include "wpe_hook_internal.h"

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
    {LatestAct::ACT_HULU, L"守护葫芦藤"},
    {LatestAct::ACT_STAR_PRINCESS, L"采菊东篱下"},
    {LatestAct::ACT_FIND_CORNEL, L"寻找七色茱萸"},
    {LatestAct::ACT_FIGHT_EVILS, L"智斗恶灵"},
    {LatestAct::ACT_TREASURE, L"夺宝行动"},
    {LatestAct::ACT_PUMPKINS_PK, L"南瓜VS南瓜王"},
};

constexpr int kActivityCountTargetNum =
    static_cast<int>(sizeof(kActivityCountTargets) / sizeof(kActivityCountTargets[0]));

// 返回单个活动剩余次数；< 0 表示未解析出（不含“次数为 0”的合法情况）。
int GetActivityRemainingCount(uint32_t activityId) {
    // 最新活动（周活动小游戏）由独立模块维护次数，其余活动不再有一键入口。
    return IsLatestActivity(activityId) ? GetLatestActivityPlayCount(activityId) : -1;
}

// 直接构造进入活动的探针封包，协议字节与各活动 SendActNNNOpenUIPacket 一致，
// 但使用更短超时。各活动响应处理器只读取 operation/playCount，不依赖
// waitingResponse 预置，故此处无需先置位等待标记。
bool ProbeActivityOpenUI(uint32_t activityId) {
    // 749/730 等活动的进入操作是 "game_info"，不能一律用 "open_ui"。
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

    // 走受管线程：退出时能收住，不再是一条没人等的线程。
    const bool threadStarted = StartManagedThreadOnce(L"activity_counts", [](LPVOID) -> DWORD {
        if (!IsWpeHookInitialized()) {
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
            if (!IsWpeHookInitialized() || ManagedThread::StopRequested()) {
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
                ManagedThread::SleepCancellable(120);
                continue;
            }

            // 响应等待被唤醒发生在 recv hook，而解析 playCount 的 handler 跑在
            // 派发 worker 线程上，二者存在先后差；等待 handler 落地后再读取。
            ManagedThread::SleepCancellable(150);
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
            ManagedThread::SleepCancellable(120);
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
    }, nullptr);

    if (threadStarted) {
        return;
    }

    g_activityCountRefreshing = false;
    UIBridge::Instance().UpdateHelperText(L"刷新活动次数失败：无法创建查询线程");
}

