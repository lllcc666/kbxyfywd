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

#include "activity_actions.h"
#include "activity_minigames.h"
#include "activity_states_internal.h"
#include "battle_six.h"
#include "byte_reader.h"
#include "hook_shared.h"
#include "managed_thread.h"
#include "packet_builder.h"
#include "packet_parser.h"
#include "packet_protocol.h"
#include "shuangtai_internal.h"
#include "ui_bridge.h"
#include "utils.h"
#include "wpe_hook.h"
#include "wpe_hook_internal.h"

// 福瑞宝箱运行状态：hook 核心在 ENTER_SCENE_BACK 里置位 g_heavenFuruiEnteredMap，
// 因此这组标记放在文件作用域，并在 activity_minigames.h 里声明。
std::atomic<bool> g_heavenFuruiRunning{false};
std::atomic<bool> g_heavenFuruiEnteredMap{false};
std::atomic<int> g_heavenFuruiMaxBoxes{HeavenFurui::DAILY_OPEN_LIMIT};

namespace {

// ============================================================================
// 福瑞宝箱（活动 900）
//
// 协议真相源：swf_cache/probe/furui/dec_* 下 HeavenFurui.swf / HeavenFuruiBox.swf
// 的反编译源码（model/ActiveModel.as、view/MainView.as、ConsumeCoinView.as）。
// 这个活动不是"走过去捡箱子"，而是"在活动场景里花铜钱开箱抽奖"：
// 每一条 [2, 地图id, 宝箱id] 都是一次付费操作，所以回包的 result 必须逐条判定。
// ============================================================================

// 单条请求的回包等待上限。服务端正常 <200ms 就回，超时说明这条请求没人应答。
constexpr DWORD kResponseTimeoutMs = WpeHook::TIMEOUT_RESPONSE;
// 进图后等 ENTER_SCENE_BACK 的时间。
constexpr DWORD kEnterSceneTimeoutMs = 2000;
// 进图后等服务端自动补发查询结果的时间（MainModel.onEnterSceneBack 会立刻补发一次）。
constexpr DWORD kEnterResyncTimeoutMs = 800;
// 所有等待都切成 50ms 的片，停止请求最多 50ms 就能生效，不会被一次长睡眠吞掉。
constexpr DWORD kPollSliceMs = 50;
// 每张地图最多容忍的重查次数，防止服务端持续回 flag=5 时打成死循环。
constexpr int kMaxResyncPerMap = 2;

// 收包分发的解析结果：写入方是收包分发线程，读取方是一键线程，全程由 g_stateMutex 保护。
// vector 重分配会释放旧缓冲，锁外持有引用或迭代器就是在读已释放内存。
struct HeavenFuruiState {
    std::vector<int> boxIds;  // 当前地图的宝箱 id
    int isVailTime = -1;      // -1 未收到；1 活动开放；0 未开放
    int openCount = 0;        // 服务端记录的当天已开箱次数
    int lastOp = 0;           // 最近一条响应的 operation
    int lastResult = -1;      // OP_OPEN_BOX / OP_PICK_REWARD 的结果码
    int lastFlag = -1;        // OP_STATE_PUSH 的状态码
    int responseSeq = 0;      // 每收到一条响应 +1；等待方靠它判断"新回包到了"
};

std::mutex g_stateMutex;
HeavenFuruiState g_state;

// 服务端通过 OP_STATE_PUSH 的 flag==5 要求重查当前场景。
// 不能在收包线程里直接发请求：SendPacket 要等回包，而回包正是这个线程在分发，会自我死锁。
// 所以这里只置标记，由一键线程在下一个开箱点之前去重查。
std::atomic<bool> g_resyncRequested{false};

// 会话号是唯一可靠的取消权威。
// 只看 g_heavenFuruiRunning 不够：用户"停止 → 立刻开始"时新会话会把它重新置 true，
// 旧线程读到 true 就再也退不出循环，两个线程会同时遍历同一张地图表。
std::atomic<int> g_heavenFuruiSession{0};

}  // namespace

// ============================================================================
// 活动地图表
//
// 只保留 HeavenFurui.swf MainView.as 里 specialMap 真正配置过的场景键。
// 服务端只在这些场景刷宝箱，表外场景查了必为空，只会白等一个回包超时。
// ============================================================================
static constexpr int HEAVEN_FURUI_MAPS[] = {
    2001, 2003, 2004, 2005, 2006, 2008,
    3001, 3002, 3003, 3004, 3005, 3006,
    4001, 4002, 4003,
    5001, 5002, 5003,
    6001, 6002, 6003, 6004, 6005,
    7001, 7002, 7003, 7004,
    9001, 9002, 9003, 9004, 9006,
    10001, 10002, 10003, 10004, 10006,
    11000, 11001, 11002, 11003, 11004,
    19001, 19002, 19003, 19004, 19005, 19006, 19007, 19008, 19009,
    21001, 21002, 21003, 21004, 21005, 21006, 21007
};

// ---- 状态访问：一律返回快照，不允许把锁内的引用带出去 ----------------------

static void ResetHeavenFuruiResponse() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    g_state.lastOp = 0;
    g_state.lastResult = -1;
    g_state.lastFlag = -1;
}

static int HeavenFuruiResponseSeq() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    return g_state.responseSeq;
}

static int HeavenFuruiLastResult(int operation) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    return g_state.lastOp == operation ? g_state.lastResult : -1;
}

static int HeavenFuruiIsVailTime() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    return g_state.isVailTime;
}

static int HeavenFuruiOpenCount() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    return g_state.openCount;
}

static std::vector<int> SnapshotHeavenFuruiBoxIds() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    return g_state.boxIds;
}

static bool HeavenFuruiSessionAlive(int session) {
    return g_heavenFuruiSession.load() == session &&
           g_heavenFuruiRunning.load() &&
           !ManagedThread::StopRequested();
}

// 等一条新的活动 900 回包：responseSeq 变化即视为"新回包到了"。
// 每 50ms 重新比对一次会话号，停止请求不会被 3 秒的 SendPacket 超时拖住。
static bool WaitHeavenFuruiResponse(int session, int fromSeq, DWORD timeoutMs) {
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    while (GetTickCount64() < deadline) {
        if (!HeavenFuruiSessionAlive(session)) return false;
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            if (g_state.responseSeq != fromSeq) return true;
        }
        ManagedThread::SleepCancellable(kPollSliceMs);
    }
    return false;
}

// 开箱间隔。AS3 客户端里的 3 秒是给手点用的防误触冷却（MainView.handlerClickBox），
// 自动流程不需要这么慢，这里只用 300ms 做基本节流，避免同一帧连发多条请求。
static bool WaitHeavenFuruiOpenCooldown(int session, ULONGLONG lastOpenTick) {
    while (HeavenFuruiSessionAlive(session)) {
        const ULONGLONG now = GetTickCount64();
        if (lastOpenTick == 0 ||
            now - lastOpenTick >= static_cast<ULONGLONG>(HeavenFurui::OPEN_INTERVAL_MS)) {
            return true;
        }
        ManagedThread::SleepCancellable(kPollSliceMs);
    }
    return false;
}

// ---- 封包发送 --------------------------------------------------------------

static std::vector<BYTE> BuildHeavenFuruiPacket(int opType, const std::vector<int32_t>& bodyValues) {
    PacketBuilder builder;
    builder.SetOpcode(Opcode::HEAVEN_FURUI_SEND)
           .SetParams(HeavenFurui::ACTIVITY_ID)
           .WriteInt32(opType)
           .WriteInt32Array(bodyValues);
    return builder.Build();
}

// 所有活动 900 的请求都走这里，并且必须校验回包的 params 等于 900。
// back opcode 1324097 上还挂着 902 / 325 / 182 等别的活动子命令，
// 不校验 params 时任何一条回包都会提前解除等待，读到的是别人的数据。
BOOL SendHeavenFuruiPacket(int opType, const std::vector<int32_t>& bodyValues) {
    std::vector<BYTE> packet = BuildHeavenFuruiPacket(opType, bodyValues);
    if (packet.empty()) return FALSE;
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()),
                      Opcode::HEAVEN_FURUI_BACK, kResponseTimeoutMs,
                      static_cast<uint32_t>(HeavenFurui::ACTIVITY_ID),
                      /*matchExpectedParams=*/true);
}

BOOL SendHeavenFuruiQueryPacket(int mapId) {
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_state.isVailTime = -1;
        g_state.boxIds.clear();
    }
    return SendHeavenFuruiPacket(HeavenFurui::OP_QUERY, {mapId});
}

BOOL SendHeavenFuruiOpenBoxPacket(int mapId, int boxId) {
    return SendHeavenFuruiPacket(HeavenFurui::OP_OPEN_BOX, {mapId, boxId});
}

// 开箱成功后领奖。AS3 只发两个 int：[4, 0] 抽奖，[4, 1] 放弃奖励。
BOOL SendHeavenFuruiPickRewardPacket() {
    return SendHeavenFuruiPacket(HeavenFurui::OP_PICK_REWARD, {0});
}

void StopHeavenFurui() {
    // 会话号一改，工作线程下一轮检查就立刻退出；
    // 它的收尾也因为会话号不匹配而跳过，不会把用户马上重开的新一轮运行标志清掉。
    g_heavenFuruiSession.fetch_add(1);
    g_heavenFuruiRunning = false;
}

void SetHeavenFuruiMaxBoxes(int maxBoxes) {
    if (maxBoxes < 1) maxBoxes = 1;
    if (maxBoxes > HeavenFurui::DAILY_OPEN_LIMIT) maxBoxes = HeavenFurui::DAILY_OPEN_LIMIT;
    g_heavenFuruiMaxBoxes = maxBoxes;
}

static void HeavenFuruiThreadProc(int session);

BOOL StartOneKeyHeavenFuruiPacket(int maxBoxes) {
    if (g_heavenFuruiRunning.load()) {
        // 运行期界面上的开始按钮是禁用的，能走到这里说明是脚本重放：
        // 先作废旧会话，用户重新点开始即可，不能只 return TRUE 冒充"已启动"。
        StopHeavenFurui();
        return TRUE;
    }
    SetHeavenFuruiMaxBoxes(maxBoxes);
    const int session = g_heavenFuruiSession.fetch_add(1) + 1;
    g_heavenFuruiRunning = true;
    const bool started = ManagedThread::Start(L"heavenfurui", [session]() { HeavenFuruiThreadProc(session); });
    if (!started) {
        g_heavenFuruiRunning = false;
        return FALSE;
    }
    return TRUE;
}

// ---- 一键开箱线程 ----------------------------------------------------------

static void HeavenFuruiThreadProc(int session) {
    // 收尾守卫：不管从哪条分支退出，都要清运行标志并通知界面。
    // 只有"自己仍是当前会话"时才收尾，否则旧线程会把新一轮的运行状态踩掉。
    struct SessionGuard {
        int session;
        ~SessionGuard() {
            if (g_heavenFuruiSession.load() != session) return;
            g_heavenFuruiRunning = false;
            {
                std::lock_guard<std::mutex> lock(g_stateMutex);
                g_state.boxIds.clear();
            }
            UIBridge::Instance().ExecuteJS(L"if(window.onHeavenFuruiComplete) window.onHeavenFuruiComplete();");
        }
    } guard{session};

    ManagedThread::SleepCancellable(300);
    if (!HeavenFuruiSessionAlive(session)) return;

    const int maxBoxes = g_heavenFuruiMaxBoxes.load();
    int openedBoxes = 0;
    // 0 正常走完；1 活动未开放；2 铜钱不足；3 当日上限；4 不在活动时间；5 用户停止
    int stopReason = 0;
    ULONGLONG lastOpenTick = 0;
    wchar_t msg[256];

    swprintf_s(msg, L"福瑞宝箱：开始查找宝箱（目标 %d 个）...", maxBoxes);
    UIBridge::Instance().UpdateHelperText(msg);

    for (int mapId : HEAVEN_FURUI_MAPS) {
        if (openedBoxes >= maxBoxes) break;
        if (!HeavenFuruiSessionAlive(session)) { stopReason = 5; break; }

        // 1) 先问服务端：这张图有几个箱、活动是否开着
        ResetHeavenFuruiResponse();
        const int seqQuery = HeavenFuruiResponseSeq();
        SendHeavenFuruiQueryPacket(mapId);
        WaitHeavenFuruiResponse(session, seqQuery, kResponseTimeoutMs);

        if (!HeavenFuruiSessionAlive(session)) { stopReason = 5; break; }

        // isVailTime 是活动总闸门：AS3 只有 ==1 才会去读 boxNum，否则按 0 个处理。
        // 不看它就会在活动时间外把整张地图表刷一遍，发出的开箱请求全部返回 result=4。
        const int isVailTime = HeavenFuruiIsVailTime();
        if (isVailTime == 0) { stopReason = 1; break; }
        if (isVailTime != 1) continue;  // 没拿到回包，换下一张图

        // 服务端每天只允许开 30 次，已经到顶就不用再跑地图了。
        if (HeavenFuruiOpenCount() >= HeavenFurui::DAILY_OPEN_LIMIT) { stopReason = 3; break; }

        std::vector<int> boxIds = SnapshotHeavenFuruiBoxIds();
        if (boxIds.empty()) continue;

        // 2) 进图。开箱请求里的第一个参数是当前场景 id，人不在图上就是无效请求。
        g_heavenFuruiEnteredMap = false;
        SendEnterScenePacket(mapId);
        {
            const ULONGLONG deadline = GetTickCount64() + kEnterSceneTimeoutMs;
            while (GetTickCount64() < deadline && !g_heavenFuruiEnteredMap.load()) {
                if (!HeavenFuruiSessionAlive(session)) break;
                ManagedThread::SleepCancellable(kPollSliceMs);
            }
        }
        if (!HeavenFuruiSessionAlive(session)) { stopReason = 5; break; }
        if (!g_heavenFuruiEnteredMap.load()) continue;  // 进不去就放弃这张图

        // 进图后客户端会自动补发一条 [1, currentScenenId]（MainModel.onEnterSceneBack），
        // 等这条推送落地再取快照，否则用的是进图前查到的旧列表。
        ResetHeavenFuruiResponse();
        WaitHeavenFuruiResponse(session, HeavenFuruiResponseSeq(), kEnterResyncTimeoutMs);
        boxIds = SnapshotHeavenFuruiBoxIds();
        if (boxIds.empty()) continue;

        swprintf_s(msg, L"福瑞宝箱：地图 [%s] 发现 %d 个宝箱，开始开箱",
                   GetMapName(mapId).c_str(), static_cast<int>(boxIds.size()));
        UIBridge::Instance().UpdateHelperText(msg);

        // 3) 逐个开箱：付费操作，必须一条一条等 result，成功一个才计一个。
        size_t index = 0;
        int resyncCount = 0;
        while (index < boxIds.size()) {
            if (openedBoxes >= maxBoxes) break;
            if (!HeavenFuruiSessionAlive(session)) { stopReason = 5; break; }

            // 服务端要求重查（他人开箱 / 数据过期）时先刷新本图列表，
            // 否则会对着已经被人开走的箱发请求，白扣一次失败。
            if (g_resyncRequested.exchange(false)) {
                if (++resyncCount > kMaxResyncPerMap) break;
                ResetHeavenFuruiResponse();
                const int seqResync = HeavenFuruiResponseSeq();
                SendHeavenFuruiQueryPacket(mapId);
                WaitHeavenFuruiResponse(session, seqResync, kResponseTimeoutMs);
                boxIds = SnapshotHeavenFuruiBoxIds();
                index = 0;
                if (boxIds.empty()) break;
                continue;
            }

            if (!WaitHeavenFuruiOpenCooldown(session, lastOpenTick)) { stopReason = 5; break; }

            const int boxId = boxIds[index++];
            ResetHeavenFuruiResponse();
            const int seqOpen = HeavenFuruiResponseSeq();
            SendHeavenFuruiOpenBoxPacket(mapId, boxId);
            lastOpenTick = GetTickCount64();
            WaitHeavenFuruiResponse(session, seqOpen, kResponseTimeoutMs);

            if (!HeavenFuruiSessionAlive(session)) { stopReason = 5; break; }

            const int result = HeavenFuruiLastResult(HeavenFurui::OP_OPEN_BOX);
            if (result == 0) {
                ++openedBoxes;
                swprintf_s(msg, L"if(window.updateHeavenFuruiProgress) window.updateHeavenFuruiProgress(%d, %d);",
                           openedBoxes, maxBoxes);
                UIBridge::Instance().ExecuteJS(msg);
            } else if (result == 1) {
                stopReason = 2;  // 铜钱不足：任何一张图都开不了，直接收尾
                break;
            } else if (result == 3) {
                stopReason = 3;  // 当天次数已达上限
                break;
            } else if (result == 4) {
                stopReason = 4;  // 不在活动时间
                break;
            } else {
                // result == -1（这条请求没回包）或未定义值：这一箱没开成，不计进度，接着下一个。
                continue;
            }

            // 开箱成功后服务端已经把 8 个候选奖励下发到客户端，紧接着发 [4, 0] 抽奖领奖。
            // 顺序不能颠倒：拖到下一箱之后再领，奖励数据会被下一轮覆盖。
            ResetHeavenFuruiResponse();
            const int seqReward = HeavenFuruiResponseSeq();
            SendHeavenFuruiPickRewardPacket();
            WaitHeavenFuruiResponse(session, seqReward, kResponseTimeoutMs);
            if (!HeavenFuruiSessionAlive(session)) { stopReason = 5; break; }

            const int rewardResult = HeavenFuruiLastResult(HeavenFurui::OP_PICK_REWARD);
            if (rewardResult != 0 && rewardResult != -1) {
                swprintf_s(msg, L"福瑞宝箱：领奖失败（result=%d），继续开下一个", rewardResult);
                UIBridge::Instance().UpdateHelperText(msg);
            }
        }

        if (stopReason != 0) break;
    }

    switch (stopReason) {
        case 1:
            UIBridge::Instance().UpdateHelperText(L"福瑞宝箱：活动未开放（11:00-13:00 / 17:00-19:00），已停止");
            break;
        case 2:
            swprintf_s(msg, L"福瑞宝箱：铜钱不足，已停止（本轮成功开箱 %d 个）", openedBoxes);
            UIBridge::Instance().UpdateHelperText(msg);
            break;
        case 3:
            UIBridge::Instance().UpdateHelperText(L"福瑞宝箱：今日开箱次数已达上限，已停止");
            break;
        case 4:
            UIBridge::Instance().UpdateHelperText(L"福瑞宝箱：不在活动时间内，已停止");
            break;
        case 5:
            break;  // 用户自己点的停止，界面已经给过提示，这里不覆盖
        default:
            swprintf_s(msg, L"福瑞宝箱：已完成！本轮成功开箱 %d 个", openedBoxes);
            UIBridge::Instance().UpdateHelperText(msg);
            break;
    }
}

// ---- 回包处理（收包分发线程） ----------------------------------------------

void ProcessHeavenFuruiResponse(const GamePacket& packet) {
    if (packet.params != HeavenFurui::ACTIVITY_ID) return;

    size_t offset = 0;
    ByteReader reader(packet.body, offset);
    const int operation = reader.I32();
    if (!reader.ok()) return;  // body 连 operation 都放不下

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);

        switch (operation) {
            case HeavenFurui::OP_QUERY: {
                // 字段顺序与 HeavenFuruiBox/ActiveModel.as case 1 逐字对应。
                // isVailTime != 1 时服务端不跟后续字段，客户端也按"没有宝箱"处理。
                g_state.isVailTime = reader.I32();
                g_state.boxIds.clear();
                if (g_state.isVailTime == 1 && reader.ok()) {
                    (void)reader.I32();  // clickIconFlag：只用于活动入口红点
                    const int boxNum = reader.I32();
                    // 服务端只在 1..5 时才跟 boxId 数组，超出一律当没有箱。
                    if (boxNum > 0 && boxNum <= HeavenFurui::MAX_BOX_PER_MAP) {
                        for (int i = 0; i < boxNum; ++i) {
                            const int32_t boxId = reader.I32();
                            if (!reader.ok()) break;
                            g_state.boxIds.push_back(boxId);
                        }
                        g_state.openCount = reader.I32();
                        (void)reader.I32();  // cartoonFlag：抽奖动画跳过标记，仅界面使用
                    }
                }
                break;
            }
            case HeavenFurui::OP_OPEN_BOX: {
                // result：0 成功 / 1 铜钱不足 / 3 当天上限 / 4 不在活动时间。
                // 成功时跟 openCount、8 个候选奖励 id、客户端选中的下标。
                g_state.lastResult = reader.I32();
                if (g_state.lastResult == 0 && reader.ok()) {
                    g_state.openCount = reader.I32();
                    for (int i = 0; i < HeavenFurui::REWARD_OPTION_COUNT; ++i) (void)reader.I32();
                    (void)reader.I32();  // selectId：奖励界面高亮的下标，自动流程不需要
                }
                break;
            }
            case HeavenFurui::OP_PICK_REWARD: {
                g_state.lastResult = reader.I32();
                if (g_state.lastResult == 0) (void)reader.I32();  // 抽到的奖励 id
                break;
            }
            case HeavenFurui::OP_BOX_BROADCAST: {
                // 别人开了这个箱：立刻从列表摘掉，否则会对已经消失的箱发开箱请求。
                const int32_t boxId = reader.I32();
                const uint32_t winnerUid = reader.U32();
                if (reader.ok() && winnerUid != g_userId.load()) {
                    auto& ids = g_state.boxIds;
                    const auto end = std::remove(ids.begin(), ids.end(), static_cast<int>(boxId));
                    if (end != ids.end()) {
                        ids.erase(end, ids.end());
                    }
                }
                break;
            }
            case HeavenFurui::OP_STATE_PUSH: {
                // 这里是状态码，不是宝箱数量：0 表示宝箱被开完了，5 表示本地数据过期要重查。
                g_state.lastFlag = reader.I32();
                if (g_state.lastFlag == 0) {
                    g_state.boxIds.clear();
                } else if (g_state.lastFlag == 5) {
                    g_resyncRequested = true;  // 由一键线程在安全时机重查，收包线程不能发包
                }
                break;
            }
            default:
                break;
        }

        g_state.lastOp = operation;
        ++g_state.responseSeq;
    }
}
