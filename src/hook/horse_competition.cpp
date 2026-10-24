#include "horse_competition.h"

#include <timeapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "activity_states_internal.h"
#include "packet_builder.h"
#include "ui_bridge.h"
#include "utils.h"

namespace {

// 坐骑大赛模块 owner：运行线程、运行标记和进度回调统一由这里维护。
std::thread g_horseGameThread;

// 比赛线程的存亡开关，**不是**「一键流程在跑」。
//   g_horseRaceThreadRunning : 比赛内主循环（跑圈计时）是否继续，只由 Start/Stop 游戏线程改写
//   state.isRunning         : 整轮一键流程（拉 ui_info → 入房 → 跑圈 → 结算 → 退房）是否在跑
// 两者生命周期不同：流程可能在入房阶段就放弃（比赛线程根本没起），
// 也可能一局结束线程已退出但流程还要接着跑下一局。别合并。
std::atomic<bool> g_horseRaceThreadRunning{false};
// UI 线程在启动时写、一键线程与比赛线程随时读，裸函数指针的并发读写是 UB
// （编译器完全可以把读缓存进寄存器，回调从此再也不更新）。函数指针是标量，
// atomic 化没有实际开销。
std::atomic<HorseProgressCallback> g_horseProgressCallback{nullptr};

// 游戏线程的启动/停止来自不同线程：StartHorseCompetitionGame 由收包分发线程触发
// （START_GAME 响应），StopHorseCompetitionGame 由一键工作线程触发（退出/超时）。
// std::thread 的 join/赋值不是线程安全的，两线程并发操作同一对象是 UB，故统一加锁。
std::mutex g_horseGameThreadMutex;

// 一键流程的独占标记：StartOneKey 的「检查 isRunning + Reset + 置位」必须是原子的，
// 否则连点两次会起两份流程（UI 每次点击都新开一个 detached 线程），互相 Reset 状态、
// 互相 StopHorseCompetitionGame 掐掉对方的比赛线程。
std::mutex g_horseRunMutex;

// 保护 HorseCompetitionState 里带堆分配的字段（myInfo 含 std::wstring、
// itemDistances 是 std::vector）。收包分发线程整体赋值它们的同时，
// 一键线程 ResetHorseRoundState 清空、比赛线程拷贝，都是同对象的并发访问 = UB
// （可能堆损坏）。锁只罩字段读写，不跨发包、不跨 UI 回调。
std::mutex g_horseInfoMutex;

// ---- 坐骑大赛的速度模型（严格对齐 AS3）----
// AS3 HorsePlayer.as:401-402 每帧推进：
//   RUN     (base_speed + 15) / 60
//   ACCRUN  (base_speed + 15 + base_intimate / 5) / 60
// HorseGameView.as:127 把 base_speed 显示成「+X%」，即它是百分比而非绝对速度；
// 按真实坐骑 base_speed 10~30 计算，只靠巡航跑完 1500 米要 33~60 秒。
// 同文件 232/245：RUN 每帧 hp += 0.84、ACCRUN 每帧 hp -= 3.33，折算每秒是回血 50.4、
// 耗血 199.8，即稳态冲刺占比 d = 0.84/(0.84+3.33) ≈ 20.1%，任何真实坐骑的平均速度
// 上限 ≈ v_run + 0.201·(v_acc − v_run)，合法完赛时间约 29~60 秒（坐骑越好越快）。
//
// 要点：坐骑速度「是什么就是什么」。base_speed / base_intimate / base_power 全靠
// 服务器在 JOIN_GAME 广播里下发（MemberInfo.as:96-102 就是解析这条包），本模块一个
// 字节都不改，也不为了凑时长整体抬速：进比赛时拿到多少速度，就按这个速度把这只
// 坐骑跑成它自己的最好成绩。
// 属性一定在开局前到手：流程上本工具要等 state.inRoom（由 JOIN_GAME 回包置真）才发
// READY，服务器也要房间里人齐才推 START_GAME，所以 START_GAME 必定晚于那条回包。
// 万一真拿不到（不该发生），就中止本局重来，绝不编造一个服务器没下发过的速度。
//
// 于是能优化的只有「怎么骑」，三件事：
//   1. 决策层（冲刺时机）：只看自己的血条——血够就冲、见底才收油（AS3 里 hp 归零
//      本来就只是收油，不是死亡）。亲密为 0 时 accSpeed == speed，冲刺只掉血不
//      加速，直接不冲。道具窗口（撞道具会掉血/减速）要提前收油。
//   2. 速度层：严格按 AS3 两条公式逐帧积分，不加任何系数、不补距离、不看任何
//      外部时间线。平均速度上限天生就是 v_run + 0.201·(v_acc − v_run)。
//   3. 人味层：逐帧 ±1.5% 抖动 + 偶发 0.4~0.8 秒掉速；冲刺的按压时长与收油后的
//      反应间隔都带随机，血条锯齿不是节拍器。
//
// 为什么「冲多深」不影响总时长：回血 0.84/帧、耗血 3.33/帧都与速度无关，稳态
// 冲刺占比恒为 20.1%，所以「深冲一段 + 长回血」与「浅冲多次」总行程相同；差别
// 只在开局那波满血冲刺能不能一次用满。因此「有血就冲」就是这只坐骑的最快跑法，
// 而不是靠改速度去凑某个秒数。
constexpr double kHorseTargetFps = 60.0;
// 看门狗与等待上限都按「这只坐骑自己的理论时长」推导（见 StartHorseCompetitionGame），
// 这里只有余量，不再出现任何「目标秒数」。
constexpr double kHorseWatchdogFactor = 1.8;      // 理论时长 × 1.8 还没冲线＝卡死
constexpr double kHorseWatchdogFloorSeconds = 60.0;  // 再快的坐骑也至少给 60 秒

void NotifyHorseProgress(const std::wstring& msg) {
    // 取一次快照再调用：回调可能在两次读之间被 UI 线程换掉，
    // 直接 `if (g_horseProgressCallback) g_horseProgressCallback(msg)` 是两次独立读。
    const HorseProgressCallback callback = g_horseProgressCallback.load();
    if (callback) {
        callback(msg);
    }
}

// 拟人化使用的随机源。thread_local：StartHorseCompetitionGame 跑在收包分发线程、
// 抖动跑在比赛线程，共用一个状态就是数据竞争。
// 只用得上 [0,1) 的均匀数，所以用 xorshift64* 代替 std::mt19937：
// mt19937 的 2.5KB 状态表和 uniform_real_distribution 那套都是白带的体积。
uint64_t HorseRngNext() {
    thread_local uint64_t state = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()) | 1u;
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    return state * 0x2545F4914F6CDD1DULL;
}

// [lo, hi] 均匀分布。
double HorseRand(double lo, double hi) {
    // 取高 53 位换成 [0, 1)：double 的有效位正好 53 位。
    const double unit = static_cast<double>(HorseRngNext() >> 11) * (1.0 / 9007199254740992.0);
    return lo + (hi - lo) * unit;
}

namespace HorseCmd {

const std::string JOIN_GAME = "join_game";
const std::string ROOM_INFO = "room_info";
const std::string READY = "ready";
const std::string EXIT_ROOM = "exit_room";
const std::string UI_INFO = "ui_info";
const std::string EXCHANGE_INFO = "exchange_info";
const std::string EXCHANGE = "exchange";
const std::string PLAY_GAME = "play_game";
const std::string USE_ITEM = "use_item";
const std::string SYNC_MEMBER = "sync_member";
const std::string ROOM_STATUS = "room_status";
const std::string END_GAME = "end_game";
const std::string START_GAME = "start_game";
const std::string GET_REGRESSION = "back_pack_award";

}  // namespace HorseCmd

std::vector<uint8_t> BuildHorsePacketBody(
    const std::string& operation,
    const std::vector<int32_t>& bodyValues = {}) {
    std::vector<uint8_t> body;

    const uint16_t cmdLen = static_cast<uint16_t>(operation.length());
    body.push_back(static_cast<uint8_t>(cmdLen & 0xFF));
    body.push_back(static_cast<uint8_t>((cmdLen >> 8) & 0xFF));
    body.insert(body.end(), operation.begin(), operation.end());

    for (int32_t value : bodyValues) {
        body.push_back(static_cast<uint8_t>(value & 0xFF));
        body.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        body.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        body.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    }

    return body;
}

std::vector<uint8_t> WrapHorsePacketBody(const std::vector<uint8_t>& body, bool useGameCmd) {
    const uint32_t opcode = useGameCmd ? Opcode::HORSE_GAME_CMD_SEND : Opcode::HORSE_COMPETITION_SEND;

    return PacketBuilder()
        .SetOpcode(opcode)
        .SetParams(Opcode::HORSE_COMPETITION_ACT_ID)
        .WriteBytes(body)
        .Build();
}

BOOL SendHorsePacketBody(const std::vector<uint8_t>& body, bool useGameCmd) {
    const std::vector<uint8_t> packet = WrapHorsePacketBody(body, useGameCmd);
    return SendPacketBytes(packet);
}

bool ReadHorseJsonPayload(const GamePacket& packet, size_t& offset, std::string& json) {
    if (offset + 2 > packet.body.size()) {
        return false;
    }

    const uint16_t jsonLen = ReadUInt16LE(packet.body.data(), offset);
    if (offset + jsonLen > packet.body.size()) {
        return false;
    }

    json.assign(reinterpret_cast<const char*>(packet.body.data() + offset), jsonLen);
    offset += jsonLen;
    return true;
}

void SetHorsePhase(HorseCompetitionState& state, HorseCompetitionPhase phase) {
    // JOINING_ROOM 刻意不算「已在房间」：这一步只是发出 JOIN_GAME 请求，服务器还没
    // 确认。若在这里就把 inRoom 置真，主循环的入房等待会立刻
    // 返回，READY 会在真正入房之前就发出去（服务器直接丢弃），接着只能等满 20 秒
    // 开局超时。inRoom 只由 JOIN_GAME / ROOM_INFO 的回包置真（本文件 508/558 行）。
    //
    // 写入顺序：先写派生标志，最后写 phase。这些字段是各自独立的 atomic，整组没有
    // 原子性；若先写 phase 再写 inRoom，收包线程/一键线程恰好读到「phase 已经是
    // JOINING_ROOM、inRoom 还是上一轮的 true」，入房等待会瞬时"成功"，READY 发往
    // 已经不存在的房间。phase 最后写，中间态就只会是「phase 还是旧的、inRoom 已是
    // 新的」，对每个调用点都是安全方向（等待条件不会提前成立）。
    state.inRoom = (phase == HORSE_PHASE_WAITING_START ||
                    phase == HORSE_PHASE_GAMING || phase == HORSE_PHASE_SETTLING);
    state.isGaming = (phase == HORSE_PHASE_GAMING);
    state.isSettling = (phase == HORSE_PHASE_SETTLING);
    state.isFinished = (phase == HORSE_PHASE_FINISHED);
    state.phase = phase;
}

void ResetHorseRoundState(HorseCompetitionState& state) {
    SetHorsePhase(state, HORSE_PHASE_IDLE);
    state.inRoom = false;
    state.isGaming = false;
    state.isFinished = false;
    state.isSettling = false;
    state.receivedEndGame = false;
    state.localFinished = false;
    state.abnormalRoundEnd = false;
    state.roomGone = false;
    state.awaitingStart = false;
    state.awaitingExit = false;
    state.distance = 0.0;
    state.resDayPoint = 0;
    state.isRide = 0;
    state.uiInfoReceived = false;
    state.uiInfoPending = false;
    state.raceAborted = false;
    // 新的一局尚未进入任何房间，房间状态必须回到 FREE，否则上一局的
    // READY/SETTLE 会残留下来，让 HandleHorseRoomStatusResponse 的
    // 「活跃状态 -> FREE」判定失去参照。
    state.roomStatus = HORSE_ROOM_FREE;
    // 房间号清零即可，不再留存 staleRoomId：服务器会复用房间号，上一局的房号
    // 很可能就是这一局的房号，拿它当判据会把新房间的回包全部误杀。
    // 「上一局的迟到包」改由 START_GAME 的 members 自检挡（HorseStartGameHasSelf）。
    state.roomId = 0;
    // 服务器时间戳同理：留着上一局的 update_time 会让结算预算按上一局的总长去算。
    // 只有在包里确实带这两个字段时才会被重写（见 ApplyHorseRoomStateFromJson），
    // 若服务器某轮不带，就退回 140 秒兜底，而不是拿旧值算出一个错预算。
    state.roomStartTime = 0;
    state.roomUpdateTime = 0;
    state.raceStartSteadyMs = 0;
    {
        // myInfo 里有 std::wstring、itemDistances 是 vector：清空它们的同时收包线程
        // 可能在写同一个字段，那是 UB（可能堆损坏），必须与解析侧共用一把锁。
        std::lock_guard<std::mutex> lock(g_horseInfoMutex);
        state.myInfo.Reset();
        // 每轮清空道具落点：某一轮的包没带 items 时，上一局的位置不能残留下来
        // 继续驱动跳跃与提前收油。
        state.itemDistances.clear();
    }
}

enum class HorseWaitOutcome { Signalled, TimedOut, Stopped };

// 统一等待：cond 成立即 Signalled；watchStop 为真时用户点停止立刻 Stopped，
// 不必等满超时（入房 6 秒、UI_INFO 3 秒这些等待不加就会「点了停止还要等十几秒」）。
// 退房确认传 watchStop=false：它本身就是停止路径的一部分，没有「等它时被停止」
// 这个语义，而且超时只有几百毫秒。
template <typename Cond>
HorseWaitOutcome WaitForHorseCondition(const HorseCompetitionState& state,
                                       Cond cond,
                                       DWORD timeoutMs,
                                       bool watchStop = true) {
    const auto start = std::chrono::steady_clock::now();
    while (true) {
        if (cond()) {
            return HorseWaitOutcome::Signalled;
        }
        if (watchStop && state.stopRequested.load()) {
            return HorseWaitOutcome::Stopped;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - start).count();
        if (elapsed > timeoutMs) {
            return HorseWaitOutcome::TimedOut;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void HorseGameMainLoop() {
    auto& state = ActivityStateManager::Instance().GetHorseCompetitionState();

    // AS3 HorseTimer 以固定 16.666ms（60Hz）驱动 HorsePlayer.checkControl，每帧按
    // 固定速度积分距离；PLAY_GAME 同步包也是每 60 帧（=1 秒）发一次。这里用
    // 定步长（fixed timestep）+ 累加器复刻该节奏：无论单帧处理耗时如何波动，
    // 都按固定步长推进模拟并均匀输出同步包，避免原先「处理耗时 + sleep 余量」
    // 写法受 Windows 睡眠粒度影响而导致帧率抖动、进度与发包节奏不稳。
    const auto frameDuration = std::chrono::duration<double, std::milli>(1000.0 / kHorseTargetFps);
    const double ahp = 0.84;
    const double mhp = 3.33;

    constexpr int kJumpFrames = 18;
    constexpr int kJumpCooldownFrames = 150;
    // 连续这么多次 PLAY_GAME 发不出去就判定连接已断、中止本局。
    // 同步包 1 秒一次，5 次约等于连续 5 秒上报失败，正常抖动不会连续这么久。
    constexpr int kMaxPlayGameFailStreak = 5;
    // 单次循环最多补的模拟帧数：进程卡顿时不无限追帧，防止瞬间冲过终点。
    // 30 帧 = 0.5 秒容忍度。原先 6 帧（100ms）太窄：多开或 CPU 满载时线程被连续
    // 抢占 100ms 是常态，而下面的累加器截断会把整段超额时间丢掉——等于白跑。
    // 0.5 秒的补帧量还远没到「瞬移」的量级，但能覆盖绝大多数调度抖动。
    constexpr int kMaxStepsPerTick = 30;

    // 提高系统定时器精度到 1ms。默认 15.6ms 粒度下 sleep_until(16.67ms) 会被
    // 上取整到 2 个刻度（约 31ms），60Hz 模拟实际只能跑到约 32Hz；配合下面的
    // 墙钟兜底虽然不会让总时长失控，但同步包节奏会明显抖动。
    const bool timerResRaised = (timeBeginPeriod(1) == TIMERR_NOERROR);

    // 开局快照道具距离：与 AS3「本轮 itemList 固定」一致；同时避免与响应分发
    // 线程重写 state.itemDistances 产生并发访问（vector 重分配会导致迭代器失效）。
    // 拷贝本身也必须加锁：分发线程是整体赋值（重分配），无锁拷贝同样是 UB。
    std::vector<int> itemDistances;
    {
        std::lock_guard<std::mutex> lock(g_horseInfoMutex);
        itemDistances = state.itemDistances;
    }

    int accCooldownFrames = 0;
    int accHoldFrames = 0;
    int jumpHoldFrames = 0;
    int jumpCooldownFrames = kJumpCooldownFrames;
    size_t nextItemIndex = 0;
    double nextSyncSeconds = 1.0;
    bool firstSprintDone = false;
    int playGameFailStreak = 0;
    // 本局距离只在本线程累加，再把结果 store 进 state.distance（atomic）供外层读：
    // 直接对 state.distance 做 += 是「读-改-写」，与一键线程的读并发会丢更新，
    // 而且裸 double 读写本身是数据竞争。
    double raceDistance = 0.0;

    auto lastTick = std::chrono::steady_clock::now();
    const auto gameStart = lastTick;
    auto accumulator = std::chrono::duration<double, std::milli>(0.0);

    while (g_horseRaceThreadRunning && state.isGaming) {
        const auto now = std::chrono::steady_clock::now();
        accumulator += std::chrono::duration<double, std::milli>(now - lastTick);
        lastTick = now;

        // 长时间卡顿后把累加器截到上限，而不是截成 1 帧：截成 1 帧等于把这期间
        // 真实流逝的时间整块丢掉，白跑。上面 kMaxStepsPerTick 已经限定了单次
        // 最多补多少，这里只需保证不超过该额度。
        if (accumulator > frameDuration * kMaxStepsPerTick) {
            accumulator = frameDuration * kMaxStepsPerTick;
        }

        // 只有墙钟计时（发同步包、看门狗用），不再有任何「配速线」：
        // 跑多快完全由这只坐骑的属性与血条决定。
        const double elapsedSeconds =
            std::chrono::duration<double>(now - gameStart).count();

        int steps = 0;
        while (accumulator >= frameDuration && steps < kMaxStepsPerTick
               && g_horseRaceThreadRunning && state.isGaming && !state.localFinished) {
            accumulator -= frameDuration;
            ++steps;

            {
                if (accCooldownFrames > 0) {
                    --accCooldownFrames;
                }
                if (jumpCooldownFrames > 0) {
                    --jumpCooldownFrames;
                }

                if (jumpHoldFrames > 0) {
                    state.state = "JUMP";
                    --jumpHoldFrames;
                    if (jumpHoldFrames == 0) {
                        state.state = state.lastState.empty() ? "RUN" : state.lastState;
                        if (state.state != "ACCRUN") {
                            state.state = "RUN";
                        }
                        jumpCooldownFrames = kJumpCooldownFrames;
                    }
                } else {
                    bool shouldJumpForItem = false;
                    if (nextItemIndex < itemDistances.size()) {
                        const int nextItemDistance = itemDistances[nextItemIndex];
                        const double distanceToItem = static_cast<double>(nextItemDistance) - raceDistance;
                        if (distanceToItem < -40.0) {
                            ++nextItemIndex;
                        } else {
                            shouldJumpForItem = distanceToItem >= 18.0 && distanceToItem <= 42.0;
                        }
                    }

                    // 冲刺时机只看自己的血条——这是把这只坐骑发挥到极限的唯一杠杆：
                    // 血够就冲（亲密 > 0 时冲刺永远正收益），一直按到血见底才收油
                    // （AS3 里 hp 归零只是收油、不是死亡），回上去再冲。
                    //
                    // 原先这里还有一个「道具窗口 −10~72 米内抑制冲刺」的分支，已删除：
                    // 道具链（USE_ITEM / use_item 回包）本工具压根没实现，itemDistances
                    // 只有距离没有 idx，所以那段时间既不会拾取道具、也不会被道具伤害，
                    // 抑制冲刺是纯丢速度、零收益。
                    const bool sprintUseful = state.accSpeed > state.speed + 1e-6;
                    if (state.state == "ACCRUN") {
                        --accHoldFrames;
                        if (accHoldFrames <= 0 || state.hp <= state.maxHp * 0.10
                            || !sprintUseful) {
                            state.state = "RUN";
                            // 人肉反应：血见底到松手，0.15~0.45 秒。
                            accCooldownFrames = 9 + static_cast<int>(HorseRand(0.0, 18.0));
                        }
                    } else {
                        state.state = "RUN";
                        const bool canAccNow = accCooldownFrames <= 0
                                               && state.hp >= state.maxHp * 0.85
                                               && sprintUseful;
                        if (canAccNow) {
                            state.lastState = "RUN";
                            state.state = "ACCRUN";
                            // 按压时长按「这一口气能把多少血烧掉」算，不再写死魔数：
                            // ACCRUN 每帧固定扣 mhp，从当前血扣到收油线（10%·maxHp）
                            // 需要的帧数就是这次该按多久。硬编码 600 帧在 base_power
                            // 很大时会先到期：血还剩 33% 就被迫收油，回满 85% 要白等
                            // 二十多秒巡航，占空比从 20% 掉到个位数。
                            const double usableHp =
                                (std::max)(0.0, state.hp - state.maxHp * 0.10);
                            const int fullBurnFrames =
                                static_cast<int>(usableHp / mhp);
                            if (!firstSprintDone) {
                                // 开局满血：一口气按到血见底。同样的血早冲早到，
                                // 这一波用不用得满直接决定这只坐骑的最快成绩。
                                // 多留 2 帧，靠 hp<=10% 那条收油而不是靠计数到期。
                                accHoldFrames = (std::max)(2, fullBurnFrames + 2);
                                firstSprintDone = true;
                            } else {
                                // 之后每次按 0.5~1.5 秒：按够时长或血低了都会松手。
                                // 也不许超过「这口气能烧完的血」对应的帧数。
                                const int randomHold =
                                    30 + static_cast<int>(HorseRand(0.0, 60.0));
                                accHoldFrames = (std::max)(
                                    2, (std::min)(randomHold, fullBurnFrames + 2));
                            }
                        }
                    }

                    const bool canJumpNow = jumpCooldownFrames <= 0;
                    if (canJumpNow && shouldJumpForItem) {
                        state.lastState = state.state;
                        jumpHoldFrames = kJumpFrames;
                        state.state = "JUMP";
                        --jumpHoldFrames;
                        if (jumpHoldFrames <= 0) {
                            state.state = state.lastState.empty() ? "RUN" : state.lastState;
                            jumpCooldownFrames = kJumpCooldownFrames;
                        }
                    }
                }

                double distanceDelta = 0.0;
                if (state.state == "RUN") {
                    state.hp += ahp;
                    distanceDelta = state.speed;
                } else if (state.state == "ACCRUN") {
                    state.hp -= mhp;
                    distanceDelta = state.accSpeed;
                } else if (state.state == "JUMP") {
                    if (state.lastState == "ACCRUN") {
                        state.hp -= mhp;
                        distanceDelta = state.accSpeed;
                    } else {
                        state.hp += ahp;
                        distanceDelta = state.speed;
                    }
                }

                // 人味：逐帧 ±2% 抖动。零均值，不影响期望速度，只让每秒上报的
                // distance 增量看起来不是恒定值。
                //
                // 原先还有一段「每秒约 7% 概率、持续 24~48 帧、速度 ×0.88」的掉速
                // 模拟，已删除：它是纯负收益（期望损失 = 0.0012/帧 × 36 帧 × 12%
                // ≈ 0.52%），一局 30 秒要白丢 0.15 秒。抖动幅度从 ±1.5% 提到 ±2%
                // 作为补偿，伪装度不降反升，而期望速度回到理论值。
                const double humanFactor = HorseRand(0.98, 1.02);
                raceDistance += distanceDelta * humanFactor;

                if (state.hp > state.maxHp) {
                    state.hp = state.maxHp;
                }
                if (state.hp <= 0) {
                    // 与 AS3 hp setter 一致（HorsePlayer.as:175-191）：hp 归零只是收油，
                    // 不是死亡（死亡由撞白石头或中黑雷道具触发，344-354 / 815-819）。
                    // 跳跃中归零时 AS3 只把 lastState 改成 RUN（落地后按巡航跑），
                    // 所以这里分开处理，避免 JUMP 分支继续按 accSpeed 白走 18 帧。
                    state.hp = 0;
                    if (state.state == "JUMP") {
                        state.lastState = "RUN";
                    } else {
                        state.state = "RUN";
                        accCooldownFrames = 48;
                    }
                }
            }
        }

        // ---- 每 tick 兜底：推进/结算不能只放在内层帧循环里 ----
        // 内层帧数被 kMaxStepsPerTick 截断、或线程被抢占时，可能一个 tick 一帧
        // 都没跑到。但这里**不补距离**：进度只由 AS3 速度逐帧积分产生，补距离等于
        // 凭空提速度。仅保留「彻底卡死」看门狗：超过 raceTargetSeconds 还没冲线，
        // 说明这一局已经废了——中止本局交给外层重试，绝不伪造 distance 去骗结算：
        // 伪造出来的 cost_time 与前面每秒上报的真实进度自相矛盾，正是最容易被判异常的形状。
        if (!state.localFinished && state.raceTargetSeconds > 1.0
            && elapsedSeconds > state.raceTargetSeconds) {
            state.raceAborted = true;
            break;
        }

        // 把本线程累计的距离发布给外层：只此一处写，读侧永远读到完整值。
        state.distance.store(raceDistance);

        if (!state.localFinished &&
            raceDistance >= static_cast<double>(HorseCompetitionState::ROUTE_DISTANCE)) {
            raceDistance = static_cast<double>(HorseCompetitionState::ROUTE_DISTANCE);
            state.distance.store(raceDistance);
            state.localFinished = true;
            if (!SendHorsePlayGamePacket(static_cast<int>(raceDistance))) {
                // 冲线包丢了只是让服务器晚一秒知道，按下面的连续失败计数处理。
                ++playGameFailStreak;
            }
        }

        // 同步包按墙钟 1 秒一次（AS3 是 distance setter 每 60 次调用发一次，
        // HorsePlayer.as:206-211，60Hz 下正好 1 秒）。用墙钟而不是帧计数，
        // 避免掉帧时服务器长时间收不到进度。
        if (!state.localFinished && elapsedSeconds >= nextSyncSeconds) {
            nextSyncSeconds += 1.0;
            if (SendHorsePlayGamePacket(static_cast<int>(raceDistance))) {
                playGameFailStreak = 0;
            } else {
                ++playGameFailStreak;
            }
        }

        // 连续发不出去说明连接已经断了（SendPacket 返回 FALSE，队列满/未连接）。
        // 本地还在跑但服务器收不到任何进度，最后只会按「distance 不足」判异常，
        // 甚至把这一局的奖励吞掉。与其空跑到看门狗，不如立刻中止交给外层重试：
        // 外层会退房、重新匹配，一轮的成本远低于白跑一整局。
        if (playGameFailStreak >= kMaxPlayGameFailStreak) {
            NotifyHorseProgress(L"连续多次无法上报比赛进度（连接可能已断开），中止本局");
            state.raceAborted = true;
            break;
        }

        if (state.localFinished && state.receivedEndGame) {
            state.isGaming = false;
            state.isFinished = true;
            break;
        }

        // 睡到下一个帧边界：剩余等待 = 帧长 - 累加器余量。若已落后（余量为负），
        // sleep_until 会立即返回，由下一轮循环的累加器补帧追上。
        std::this_thread::sleep_until(lastTick + frameDuration - accumulator);
    }

    if (timerResRaised) {
        timeEndPeriod(1);
    }
}

// 区分「字段缺失」与「值确实是 0」。status 缺失时若被当成 0，就会写成
// HORSE_ROOM_FREE（房间已解散），配合 HandleHorseRoomStatusResponse 的
// 「活跃态跌回 FREE 就判倒闭」语义，会把一个正常房间误判成解散并提前退房丢结算。
bool TryExtractJsonInt(const std::string& json, const std::string& key, int& outValue) {
    const size_t pos = json.find("\"" + key + "\":");
    if (pos == std::string::npos) {
        return false;
    }
    int value = 0;
    if (!TryParseInt32Decimal(json.substr(pos + key.length() + 3), value)) {
        return false;
    }
    outValue = value;
    return true;
}

int ExtractJsonInt(const std::string& json, const std::string& key) {
    int value = 0;
    return TryExtractJsonInt(json, key, value) ? value : 0;
}

uint32_t ExtractJsonUInt(const std::string& json, const std::string& key) {
    const size_t pos = json.find("\"" + key + "\":");
    if (pos != std::string::npos) {
        size_t valuePos = pos + key.length() + 3;  // 跳过 `"key":`
        // 服务器可能序列化成 `"player_id": 123`（冒号后带空格）。不跳过空白就会
        // 解析失败、把 0 当成「字段不存在」，成员自检与排名查找都会找不到自己。
        while (valuePos < json.size() && (json[valuePos] == ' ' || json[valuePos] == '\t')) {
            ++valuePos;
        }
        uint32_t value = 0;
        if (TryParseUInt32Decimal(json.substr(valuePos), value)) {
            return value;
        }
    }
    return 0;
}

// 从 pos 开始切出下一个「平衡括号对象」，并把 pos 移到该对象之后。跳过字符串与转义。
// 排名表、members 数组、结算包计数三处共用这一份：以前每处各写一遍扫描，其中一份
// 少了数组右界判断，改一处很容易漏另一处。
bool SliceNextJsonObject(const std::string& json, size_t& pos, std::string& outObject) {
    const size_t objectStart = json.find('{', pos);
    if (objectStart == std::string::npos) {
        return false;
    }

    int depth = 0;
    bool inString = false;
    bool escape = false;
    for (size_t i = objectStart; i < json.size(); ++i) {
        const char ch = json[i];
        if (escape) {
            escape = false;
        } else if (ch == '\\') {
            escape = true;
        } else if (ch == '"') {
            inString = !inString;
        } else if (!inString) {
            if (ch == '{') {
                ++depth;
            } else if (ch == '}') {
                --depth;
                if (depth == 0) {
                    outObject = json.substr(objectStart, i - objectStart + 1);
                    pos = i + 1;
                    return true;
                }
            }
        }
    }
    return false;
}

std::string ExtractJsonObjectByPlayerId(const std::string& json, uint32_t playerId, int* outRank = nullptr) {
    size_t searchPos = 0;
    int rank = 0;
    std::string objectJson;

    while (SliceNextJsonObject(json, searchPos, objectJson)) {
        ++rank;
        // 按字段值比较，不能匹配子串 "player_id":123——服务器若序列化成
        // `"player_id": 123`（冒号后带空格），子串匹配永远找不到自己：
        // START_GAME 的成员自检恒 false（每局开局被丢、无限重试），排名也永远找不自己。
        if (ExtractJsonUInt(objectJson, "player_id") == playerId) {
            if (outRank) {
                *outRank = rank;
            }
            return objectJson;
        }
    }

    if (outRank) {
        *outRank = 0;
    }
    return "";
}

// 这段 JSON 里能解析出多少个「带 player_id 的玩家对象」（最多数 maxCount 个）。
// 用来区分两种「找不到自己」：一种是对手都在、只是没有我（迟到包，应当丢弃）；
// 另一种是一个 player_id 都解析不出来（服务器字段形态与预期不符，丢弃会让每局
// 都白等到超时，只能照旧采纳并打出样包）。
int CountJsonObjectsWithPlayerId(const std::string& json, int maxCount = 8) {
    int count = 0;
    size_t pos = 0;
    std::string objectJson;
    while (count < maxCount && SliceNextJsonObject(json, pos, objectJson)) {
        if (ExtractJsonUInt(objectJson, "player_id") > 0) {
            ++count;
        }
    }
    return count;
}

std::vector<int> ExtractItemDistances(const std::string& json) {
    std::vector<int> distances;
    size_t itemsPos = json.find("\"items\":");
    if (itemsPos == std::string::npos) {
        return distances;
    }
    itemsPos += 8;  // 指到 "items": 之后
    while (itemsPos < json.size() && (json[itemsPos] == ' ' || json[itemsPos] == '\t')) {
        ++itemsPos;
    }
    if (itemsPos >= json.size() || json[itemsPos] != '[') {
        return distances;
    }

    // 先找出与 '[' 配对的 ']'，只在数组区间里找 "distance"：成员对象/房间对象自身
    // 也带 distance 字段（HorseMemberInfo.distance），越过数组继续扫会把它们误当成
    // 道具落点，跳跃窗口与提前收油（:326-327）就全判错了，平白掉速。
    size_t itemsEnd = std::string::npos;
    {
        int depth = 0;
        bool inString = false;
        bool escape = false;
        for (size_t i = itemsPos; i < json.size(); ++i) {
            const char ch = json[i];
            if (escape) {
                escape = false;
            } else if (ch == '\\') {
                escape = true;
            } else if (ch == '"') {
                inString = !inString;
            } else if (!inString) {
                if (ch == '[') {
                    ++depth;
                } else if (ch == ']') {
                    --depth;
                    if (depth == 0) {
                        itemsEnd = i;
                        break;
                    }
                }
            }
        }
    }
    if (itemsEnd == std::string::npos) {
        return distances;
    }

    size_t searchPos = itemsPos;
    while (true) {
        const size_t pos = json.find("\"distance\":", searchPos);
        if (pos == std::string::npos || pos >= itemsEnd) {
            break;
        }

        int value = 0;
        if (TryParseInt32Decimal(json.substr(pos + 11), value) && value > 0 && value < HorseCompetitionState::ROUTE_DISTANCE) {
            distances.push_back(value);
        }
        searchPos = pos + 11;
    }

    std::sort(distances.begin(), distances.end());
    distances.erase(std::unique(distances.begin(), distances.end()), distances.end());
    return distances;
}

std::wstring MakeJsonPreview(const std::string& json, size_t maxLen = 220) {
    // 按 UTF-8 正确解码：逐字节升宽（std::wstring(json.begin(), json.end())）会把
    // 中文昵称变成乱码，按字节截断还可能切在多字节字符中间，让「样包」这条诊断
    // 信息本身失去意义。这里按字符数截断。
    std::wstring preview;
    if (!json.empty()) {
        const int need = MultiByteToWideChar(
            CP_UTF8, 0, json.data(), static_cast<int>(json.size()), nullptr, 0);
        if (need > 0) {
            preview.resize(static_cast<size_t>(need));
            MultiByteToWideChar(
                CP_UTF8, 0, json.data(), static_cast<int>(json.size()), preview.data(), need);
        }
    }
    if (preview.size() > maxLen) {
        preview = preview.substr(0, maxLen) + L"...";
    }
    return preview;
}

void ApplyHorseRoomStateFromJson(HorseCompetitionState& state, const std::string& json) {
    const int newRoomId = ExtractJsonInt(json, "id");
    if (newRoomId != 0) {
        state.roomId = newRoomId;
    }
    // status 只在包里确实带了才采纳：缺字段保持上一次的已知状态，
    // 不能当成 0（= FREE，房间已解散）。
    int newStatus = 0;
    if (TryExtractJsonInt(json, "status", newStatus)) {
        state.roomStatus = newStatus;
    }
    // 结算倒计时用的两个服务器时间戳（AS3 RoomInfoData._start_time / _update_time）。
    // 只在包里确实带了才采纳：缺字段时保留上一次的已知值，让下面的剩余时间估算
    // 继续可用；真拿不到时 GetHorseSettleBudgetSeconds 会退回固定上限。
    int newStartTime = 0;
    if (TryExtractJsonInt(json, "start_time", newStartTime) && newStartTime > 0) {
        state.roomStartTime = newStartTime;
    }
    int newUpdateTime = 0;
    if (TryExtractJsonInt(json, "update_time", newUpdateTime) && newUpdateTime > 0) {
        state.roomUpdateTime = newUpdateTime;
    }
}

// ---- 坐骑属性的唯一来源：服务器下发的「成员对象」----
// AS3 里速度只取自 RoomInfoData.myMemberInfo（HorsePlayer.as:398-402），而这个成员
// 对象由 MemberInfo.init（MemberInfo.as:92-103）从 member_info 里读：
//   horse_info.base_power / horse_info.base_speed / horse_info.base_intimate，以及 items。
// 成员对象进入客户端的方式有两条（HorseModel.as）：
//   :86-94  JOIN_GAME  —— 包里就是「一个成员对象」，addMember(jsonData)；
//   :95-106 ROOM_INFO   —— 包里是 {id, status, members:[成员对象...]}，逐个 createMember；
//   :225-234 START_GAME —— 同样 members:[...]（RoomInfoData.members = jsonData["members"]）。
// RoomInfoData.updateList(:158-173) 再用 player_id == 自己 挑出 myMemberInfo。
// 1.13 只解析了 JOIN_GAME 里的 horse_info，且是在整包里找第一个 "horse_info" 键——
// 我们自己的开局广播并不一定带我们的成员对象（入房回包 ROOM_INFO / 开局包 START_GAME
// 的 members 数组才是权威来源），于是属性取不到、1.13 只能靠 base_speed=600 的兜底
// 跑出 2.4 秒的怪局。这里按 AS3 的做法补齐：先按 player_id 定位自己的成员对象，再解析。
bool ApplyHorseSelfFromMemberObject(HorseCompetitionState& state, const std::string& memberJson) {
    const size_t horseInfoPos = memberJson.find("\"horse_info\":");
    const std::string horseInfoJson =
        (horseInfoPos == std::string::npos) ? std::string() : memberJson.substr(horseInfoPos);
    const int baseHp = horseInfoJson.empty() ? 0 : ExtractJsonInt(horseInfoJson, "base_power");
    const int baseSpeed = horseInfoJson.empty() ? 0 : ExtractJsonInt(horseInfoJson, "base_speed");
    const int baseIntimate = horseInfoJson.empty() ? 0 : ExtractJsonInt(horseInfoJson, "base_intimate");
    // horse_info 缺失或字段不全（可能只是别人的残包）时属性不可用。
    const bool attrsUsable = (baseHp > 0 && baseSpeed > 0);

    // 道具距离独立解析（MemberInfo.items，HorseGameView.as:474 用它建 itemList）：
    // horse_info 有问题不能连 items 一起丢——那样跳跃窗口与收油抑制会整轮失效。
    const std::vector<int> items = ExtractItemDistances(memberJson);
    const uint32_t playerId = ExtractJsonUInt(memberJson, "player_id");

    {
        // myInfo 含 std::wstring、itemDistances 是 vector：与一键线程的
        // ResetHorseRoundState / 比赛线程的开局快照共用一把锁。
        std::lock_guard<std::mutex> lock(g_horseInfoMutex);
        if (attrsUsable) {
            if (playerId != 0) {
                state.myInfo.player_id = playerId;
            }
            state.myInfo.horse_base_Hp = baseHp;
            state.myInfo.horse_base_speed = baseSpeed;
            state.myInfo.horse_base_intimate = baseIntimate;
        }
        if (!items.empty()) {
            state.itemDistances = items;
        }
    }
    return attrsUsable;
}

// 本局是否已经拿到可用的坐骑属性（读侧统一走这里，顺带加锁）。
bool HorseSelfAttrsReady(HorseCompetitionState& state) {
    std::lock_guard<std::mutex> lock(g_horseInfoMutex);
    return state.myInfo.horse_base_speed > 0 && state.myInfo.horse_base_Hp > 0;
}

// 在一个带 "members":[...] 的包里找出「自己」那个成员对象并解析。
// 自己是谁由 player_id == g_userId 判定（同 AS3 updateList:164）。
// 拿不到自己的 id 时退一步：members 里只有一个人，那必然是自己。
bool ApplyHorseSelfFromMembersArray(HorseCompetitionState& state, const std::string& json) {
    size_t membersPos = json.find("\"members\":");
    if (membersPos == std::string::npos) {
        return false;
    }
    membersPos += 10;  // 指到 "members": 之后
    while (membersPos < json.size() && (json[membersPos] == ' ' || json[membersPos] == '\t')) {
        ++membersPos;
    }
    if (membersPos >= json.size() || json[membersPos] != '[') {
        return false;
    }

    const uint32_t myId = g_userId.load();
    std::string selfJson;
    std::string firstJson;
    int memberCount = 0;

    size_t pos = membersPos + 1;  // 跳过 '['
    std::string memberJson;
    while (true) {
        const size_t objStart = json.find('{', pos);
        if (objStart == std::string::npos) {
            break;
        }
        // 数组已结束（']' 在下一个 '{' 之前）就停，避免把后面的同名字段当成成员。
        const size_t arrEnd = json.find(']', pos);
        if (arrEnd != std::string::npos && arrEnd < objStart) {
            break;
        }
        // 切出这一个成员对象（内部还有 horse_info / items 等嵌套）。
        if (!SliceNextJsonObject(json, pos, memberJson)) {
            break;
        }

        ++memberCount;
        if (memberCount == 1) {
            firstJson = memberJson;
        }
        if (myId != 0 && ExtractJsonUInt(memberJson, "player_id") == myId) {
            selfJson = memberJson;
            break;
        }
    }

    if (selfJson.empty() && myId == 0 && memberCount == 1) {
        selfJson = firstJson;  // 单人房且不知道自己的 id：唯一那个人就是我
    }
    if (selfJson.empty()) {
        return false;
    }
    return ApplyHorseSelfFromMemberObject(state, selfJson);
}

// 判断一个带房间号的回包是不是「另一个房间」的包。
//
// 这里刻意不用 staleRoomId 兜底：服务器会复用房间号，上一局的房号极可能就是
// 这一局的房号。拿 staleRoomId 当判据，会把新房间的回包全部当成迟到包扔掉，
// 表现就是「第一次入房成功，之后每一轮都入房超时」。
// 所以只在「本轮房号已经确认」时，才用「房号不符」来判迟到；
// 本轮房号未知（roomId==0）时一律放行，宁可放过迟到包也不能误杀新房间。
bool IsStaleHorseRoomPacket(const HorseCompetitionState& state, int packetRoomId) {
    if (packetRoomId == 0) {
        return false;  // 包没带房间号，无从判断
    }
    const int currentRoomId = state.roomId.load();
    if (currentRoomId == 0) {
        return false;  // 本轮还没确认房号，不做判断
    }
    return packetRoomId != currentRoomId;
}

// 判断一份 JSON 里是否出现过指定 player_id（包括嵌在 members 数组里的那几份）。
//
// 不能用 ExtractJsonObjectByPlayerId 来做这件事：它依赖 SliceNextJsonObject，而那个
// 切片器只产出最外层的 {...}。对 start_game 这种 `{"id":..,"members":[{..},{..}]}`
// 的包，它只能切出最外层对象，ExtractJsonUInt 又只取第一个 "player_id"，于是判定
// 结果等价于「我的 player_id 是不是成员表里第一个」——只要别人先进房，自检就恒为
// false，整个开局包被丢弃：本地一直"等待其他玩家加入"，服务器却已经开局，此时退房
// 会被拒绝（游戏里那句「游戏开始无法退出房间」），流程就锁死在进不去也退不出。
bool JsonHasPlayerId(const std::string& json, uint32_t playerId) {
    if (playerId == 0) {
        return false;
    }
    static const std::string kKey = "\"player_id\":";
    size_t searchPos = 0;
    while (true) {
        const size_t pos = json.find(kKey, searchPos);
        if (pos == std::string::npos) {
            return false;
        }
        size_t valuePos = pos + kKey.length();
        while (valuePos < json.size() && (json[valuePos] == ' ' || json[valuePos] == '\t')) {
            ++valuePos;
        }
        uint32_t value = 0;
        if (TryParseUInt32Decimal(json.substr(valuePos), value) && value == playerId) {
            return true;
        }
        searchPos = pos + kKey.length();
    }
}

// START_GAME 的 members 是房间成员数组，每项带 player_id（RoomInfoData.as:149）。
// 我们的 player_id 在里面，说明这一局确实在房间里；不在里面说明这是上一局
// （已被 EXIT_ROOM 移出）的迟到包，采纳它就会起一个往死房间发 PLAY_GAME 的幽灵局。
//
// 这是比房间号、比「是否收到过 JOIN 回包」都可靠的判据：不受服务器复用房间号影响，
// 也不受 JOIN 回包与 START_GAME 到达顺序影响。判据本身必须扫全文（见 JsonHasPlayerId），
// 不能只扫最外层对象，否则房里有别人时会误杀正常开局。
bool HorseStartGameHasSelf(const std::string& json) {
    const uint32_t myId = g_userId.load();
    if (myId == 0) {
        return true;  // 拿不到自己的 id 时不做判断，宁可放过也不误杀正常开局
    }
    return JsonHasPlayerId(json, myId);
}

void HandleHorseRoomInfoResponse(HorseCompetitionState& state, const std::string& json) {
    // 只接受「本次入房请求」的回应。上一局迟到的 room_info 若在这里被采纳，
    // 会把 inRoom 无条件置真、并把 phase 打回 WAITING_START，结果就是外层
    // 入房等待瞬间"成功"、READY 被发往一个已经不存在的房间。
    if (state.phase != HORSE_PHASE_JOINING_ROOM && state.phase != HORSE_PHASE_WAITING_START) {
        return;
    }
    if (IsStaleHorseRoomPacket(state, ExtractJsonInt(json, "id"))) {
        return;
    }

    ApplyHorseRoomStateFromJson(state, json);
    // 入房回包自带 members（我方成员对象就在里面）：AS3 收到 ROOM_INFO 会
    // RoomInfoData.members = jsonData["members"] → createMember 逐个建成员，
    // 再由 updateList 按 player_id 定出 myMemberInfo（HorseModel.as:95-106、
    // RoomInfoData.as:133-173）。这才是本方坐骑属性的权威来源，1.13 完全没有解析。
    ApplyHorseSelfFromMembersArray(state, json);
    state.inRoom = true;
    if (state.phase < HORSE_PHASE_GAMING) {
        SetHorsePhase(state, HORSE_PHASE_WAITING_START);
    }

}

void HandleHorseStartGameResponse(HorseCompetitionState& state, const std::string& json) {
    // 三道门：
    // 1) awaitingStart——本工具确实在等这一局的开局。
    // 2) members 自检——START_GAME 的成员表里必须有我们自己，否则就是上一局
    //    已退出的房间补推过来的迟到包（见 HorseStartGameHasSelf）。
    // 3) 房号——本轮房号已确认且与本包不符时丢弃。
    // 少了第 2 道，上一局超时退房后补到的 START_GAME 会在下一轮入房窗口里被采纳，
    // 起一个往死房间发 PLAY_GAME 的幽灵局，外层白烧 160 秒。
    if (!state.awaitingStart) {
        return;
    }
    if (state.phase < HORSE_PHASE_JOINING_ROOM || state.phase >= HORSE_PHASE_SETTLING) {
        return;
    }
    if (!HorseStartGameHasSelf(json)) {
        return;
    }

    const int packetRoomId = ExtractJsonInt(json, "id");
    if (IsStaleHorseRoomPacket(state, packetRoomId)) {
        return;
    }

    ApplyHorseRoomStateFromJson(state, json);
    // START_GAME 同样带 members（AS3 HorseModel.as:225-234 也把它塞进
    // RoomInfoData.members → createMember），这是开局前最后一份成员表，
    // 属性以它为准最可靠。
    ApplyHorseSelfFromMembersArray(state, json);
    if (!HorseSelfAttrsReady(state)) {
        // 真取不到就不硬跑（StartHorseCompetitionGame 会置 raceAborted 让外层退房重来），
        // 同时把样包打出来，方便对着实际包结构核对字段名。
        NotifyHorseProgress(L"开局包里没找到本方的 horse_info，样包：" +
                            MakeJsonPreview(json, 400));
    }
    if (!state.isGaming) {
        StartHorseCompetitionGame();
    }
}

void HandleHorseJoinGameResponse(HorseCompetitionState& state, const std::string& json) {
    // AS3 里 JOIN_GAME 是房间内广播：任何成员入房都会下发一条，RoomInfoData.addMember
    // 把它并入成员表，再由 updateList() 用 player_id 区分「我」和「其他人」
    // （MemberInfo.init 里 horse_info / items 是每个成员各自持有的）。
    // 这里必须做同样的区分，否则别人入房的包会用他的 horse_info / items 覆盖
    // 本方状态；而且下面的 SetHorsePhase(WAITING_START) 会把 isGaming 置回 false，
    // 令比赛中的主循环（while (g_horseRaceThreadRunning && state.isGaming)）立即退出。
    const uint32_t playerId = ExtractJsonUInt(json, "player_id");
    if (playerId != 0 && playerId != g_userId.load()) {
        return;  // 其他成员入房，不影响本方状态
    }

    const int packetRoomId = ExtractJsonInt(json, "id");
    if (IsStaleHorseRoomPacket(state, packetRoomId)) {
        return;
    }

    // 坐骑属性与道具距离必须先解析：START_GAME 可能先于本包到达（此时 phase 已经是
    // GAMING，下面的阶段门会挡住整包），若解析放在门后，myInfo / itemDistances 就会
    // 一直沿用上一局的旧值或空值，导致跳跃与加速抑制全失效、排名掉档。
    // 这条包本身就是一个成员对象（AS3 HorseModel.as:86-94 addMember(jsonData)），
    // 直接按成员对象解析；万一服务器把入房广播也做成 {members:[...]} 形状，再退一步。
    // 归属判定：只有「明确是本人」（player_id == 自己）或「本方还没有任何可用属性」时
    // 才用它填 myInfo —— 否则别人入房的广播会把本方坐骑属性换成他的速度，
    // 或用空的道具表覆盖掉已经解析好的那几处。
    const bool attributableToSelf = (playerId != 0 && playerId == g_userId.load());
    if (attributableToSelf || !HorseSelfAttrsReady(state)) {
        if (!ApplyHorseSelfFromMemberObject(state, json)) {
            ApplyHorseSelfFromMembersArray(state, json);
        }
    }

    // 房间号/状态只在「本工具确实在入房/等开局」时采纳。上一局迟到的 JOIN_GAME
    // 若在这里被采纳，会把 inRoom 置真、phase 打回 WAITING_START。
    if (state.phase != HORSE_PHASE_JOINING_ROOM && state.phase != HORSE_PHASE_WAITING_START) {
        return;
    }

    if (packetRoomId != 0) {
        state.roomId = packetRoomId;
    }
    state.inRoom = true;
    // 入房已确认：从这一刻起 START_GAME 才是可信的（见 HandleHorseStartGameResponse）。
    if (state.phase < HORSE_PHASE_GAMING) {
        SetHorsePhase(state, HORSE_PHASE_WAITING_START);
    }
}

void HandleHorseReadyResponse(HorseCompetitionState& state, const std::string& json) {
    const uint32_t playerId = ExtractJsonUInt(json, "player_id");
    if (playerId == g_userId.load()) {
        std::lock_guard<std::mutex> lock(g_horseInfoMutex);
        state.myInfo.status = HORSE_ROOM_READY;
    }
}

void HandleHorseRoomStatusResponse(HorseCompetitionState& state, const std::string& json) {
    // 只在本局「已入房/比赛中/结算中」时采纳。上一局迟到的 ROOM_STATUS=FREE
    // 落进新一局的开局等待里，会因为 prevStatus 已经是 READY 而把 roomGone 置真，
    // 于是刚进的新房被判成「已关闭」，整局奖励白丢。这是概率最高的一条污染源。
    if (state.phase < HORSE_PHASE_JOINING_ROOM || state.phase >= HORSE_PHASE_FINISHED) {
        return;
    }
    if (IsStaleHorseRoomPacket(state, ExtractJsonInt(json, "id"))) {
        return;
    }

    const int prevStatus = state.roomStatus.load();
    ApplyHorseRoomStateFromJson(state, json);

    if (state.roomStatus == HORSE_ROOM_READY) {
        std::lock_guard<std::mutex> lock(g_horseInfoMutex);
        state.myInfo.status = HORSE_ROOM_READY;
    } else if (state.roomStatus == HORSE_ROOM_FREE) {
        // 房间已关闭/被解散。AS3 收到 FREE 会派发 HORSE_SHOW_CLOSE_BTN
        // （HorseModel.as:192-195），即客户端应立刻退出房间界面，而不是留在里面等。
        // 不处理的话：等待开局的分支会白等满 20 秒，比赛中的分支会白等满结算超时。
        // 必须要求「从活跃状态真正跌回 FREE」才算房间没了——入房瞬间服务器也可能
        // 先推一条 FREE，那是新房间的初始态，误判会让整局流程提前放弃。
        if (prevStatus == HORSE_ROOM_READY || prevStatus == HORSE_ROOM_GAMESTART ||
            prevStatus == HORSE_ROOM_INGAME || prevStatus == HORSE_ROOM_SETTLE) {
            state.roomGone = true;
            state.inRoom = false;
        }
    } else if (state.roomStatus == HORSE_ROOM_GAMESTART || state.roomStatus == HORSE_ROOM_INGAME) {
        // 服务器已经开局，本地却还没起跑：说明 start_game 没被采纳（迟到包判定、
        // 成员自检、或者根本没收到）。这正是「本地一直等待其他玩家加入、服务器却在
        // 比赛中」的错位——此时退房会被服务器以「游戏开始无法退出房间」拒绝，流程
        // 就锁死在进不去也退不出。这里按房间状态补一次起跑作为兜底：
        // 坐骑属性来自上一份 room_info / join_game 的成员对象，不需要这份成员表。
        // 宁可多跑一局，也不要原地干等到 10 分钟上限。
        //
        // 必须同时要求 inRoom：awaitingStart 在发出 JOIN_GAME 之前就已经置真，那个窗口
        // 里 phase 还是 JOINING_ROOM、roomId 已被 Reset 清 0（迟到包判定一律放行），
        // 上一局残留的 ROOM_STATUS=INGAME 会在这里触发一次「幽灵起跑」——myInfo 刚被
        // 清空，起跑必然失败并置 raceAborted，白丢一轮。
        if (!state.isGaming && state.awaitingStart && state.inRoom && !state.roomGone) {
            NotifyHorseProgress(L"房间状态显示比赛已开始，直接进入比赛");
            if (!StartHorseCompetitionGame()) {
                // 属性没拿到（拿不到时 Start 会置 raceAborted）：交给外层按本轮放弃处理，
                // 不要在这里补什么自造速度。
                NotifyHorseProgress(L"房间已开局但本地缺少坐骑属性，本轮放弃");
            }
        }
    } else if (state.roomStatus == HORSE_ROOM_SETTLE) {
        // 这里只能置「结算中」标记，绝不能走 SetHorsePhase(SETTLING)。
        // AS3 对 ROOM_STATUS=SETTLE 只派发 HORSE_FINISH（HorseModel.as:207-210），
        // 由 HorseGameView.onFinish 显示「剩余时间」倒计时（HorseGameView.as:461-470）；
        // HorsePlayer.checkControl 仍由 HorseTimer 每帧调用，distance 继续推进，
        // 直到 distance >= ROUTE 才 endGame（HorsePlayer.as:316-320）。
        // SetHorsePhase(SETTLING) 会把 isGaming 置 false，主循环当场退出、distance
        // 停在半路，服务器按「未跑完」结算（或直接判 cheat），外层再白等 180 秒超时。
        state.isSettling = true;
    }
}

void HandleHorseUiInfoResponse(HorseCompetitionState& state, const GamePacket& packet, size_t& offset) {
    // 4 个 int32 = 16 字节。旧代码要求 20 字节，若服务器正好只回 16 字节，
    // uiInfoReceived 永远不置真，表现为每轮都「获取活动信息超时」空转。
    // 只在本工具自己在跑时采纳：玩家手动打开游戏内的坐骑大赛界面也会收到同一条
    // ui_info 回包，而它会改写 resDayPoint —— 那正是外层「今日点数已刷满」的判据。
    if (!state.isRunning) {
        return;
    }
    if (offset + 16 > packet.body.size()) {
        return;
    }

    // 只有在「确实有一次请求还没被应答」时，这条包才算本次的新鲜数据。
    // 光把 ack 记成「收包那一刻的 seq」没用：上一轮迟到的回包到达时 seq 也已经
    // 递增过了，两者无法区分，于是点数刷满后仍会继续刷、或拿旧点数误判剩余量。
    // 判定必须放在写字段之前——否则旧包照样会覆盖 resDayPoint。
    if (!state.uiInfoPending) {
        return;  // 没人等这条包：多半是上一轮迟到的回包，不能当新鲜数据用
    }
    state.uiInfoPending = false;

    state.cnt = ReadInt32LE(packet.body.data(), offset);
    state.isRide = ReadInt32LE(packet.body.data(), offset);
    state.day = ReadInt32LE(packet.body.data(), offset);
    state.resDayPoint = ReadInt32LE(packet.body.data(), offset);
    state.uiInfoAckSeq = state.uiInfoSeq.load();
    state.uiInfoReceived = true;

    PostScriptToUI(
        L"if(window.updateHorseCompetitionPoints) { window.updateHorseCompetitionPoints('" +
        std::to_wstring(state.resDayPoint.load()) +
        L"'); }"
    );
}

void HandleHorseEndGameResponse(HorseCompetitionState& state, const GamePacket& packet, size_t& offset) {
    if (offset + 12 > packet.body.size()) {
        return;
    }

    // 只在本局处于「等开局/比赛中/结算中」时采纳。超时退房后服务器补推的 END_GAME
    // 若在这里被采纳，会 SetHorsePhase(FINISHED)：isGaming 被打回 false（游戏线程
    // 当场退出）、isFinished 置真（外层立刻判定本局完成），实际一帧都没骑。
    // 但「等开局」阶段必须放进来：错过 start_game 时服务器照样会把这一局跑完并发结算，
    // 若此时把包丢掉，本地就会白等满 10 分钟开局上限，房间还一直挂在服务器那里。
    // phase 只读一次：读两次的话，中间若被补起跑推成 GAMING，真正的结算包会掉进
    // 下面「本地没参赛」的分支，外层空转到整段比赛预算才收场。
    const int phaseNow = state.phase.load();
    if (phaseNow < HORSE_PHASE_WAITING_START || phaseNow >= HORSE_PHASE_FINISHED) {
        return;
    }
    if (state.receivedEndGame) {
        return;
    }

    // 本机还没起跑就收到结算：只记标记、不动 phase。比赛线程此刻没在跑，改 phase 只会
    // 让外层误以为「本局已经正常完成」；外层等待循环见到 receivedEndGame 会自己收尾。
    //
    // 但这条分支必须自带迟到包判据——END_GAME 没有房号可校（对比 room_status）。
    // 新房间在入房/准备阶段的 roomStatus 只可能是 FREE/READY，只有服务器真把这一局
    // 开起来过（GAMESTART/INGAME/SETTLE），这份结算才可能是本房间的；否则就是上一局
    // 退房后补推的迟到包，采纳它只会白丢一轮。
    if (phaseNow < HORSE_PHASE_GAMING && !state.isGaming) {
        const int roomStatus = state.roomStatus.load();
        const bool serverRanThisRoom = roomStatus == HORSE_ROOM_GAMESTART ||
                                       roomStatus == HORSE_ROOM_INGAME ||
                                       roomStatus == HORSE_ROOM_SETTLE;
        if (!serverRanThisRoom) {
            return;
        }
        state.receivedEndGame = true;
        NotifyHorseProgress(L"服务器已结算本局（本地未能参赛），进入下一轮");
        return;
    }

    const int result = ReadInt32LE(packet.body.data(), offset);
    ReadInt32LE(packet.body.data(), offset);
    ReadInt32LE(packet.body.data(), offset);

    if (result != 0) {
        NotifyHorseProgress(L"游戏异常结束，立即退出房间准备下一轮");
        state.abnormalRoundEnd = true;
        state.receivedEndGame = true;
        SetHorsePhase(state, HORSE_PHASE_FINISHED);
        state.isSettling = false;
        return;
    }

    std::string rankListJson;
    if (ReadHorseJsonPayload(packet, offset, rankListJson)) {
        int myRank = 0;
        const std::string myRankJson = ExtractJsonObjectByPlayerId(rankListJson, g_userId.load(), &myRank);

        // 迟到包防护：正常的 END_GAME 一定带一份含全部玩家的排名表（其中必有自己）。
        // 排名表里「能解析出别人、却没有我」说明这不是本局的结算（多半是上一局退房后
        // 才补推的）。若照样采纳会立刻 SetHorsePhase(FINISHED)：isGaming 被打回 false
        // （比赛线程当场退出）、外层按「提前结束」重试，白丢一局还累加失败计数。
        // 但一个 player_id 都解析不出来时不能丢：那说明服务器字段形态与预期不符，
        // 丢掉会让每一局都白等到超时，只能照旧采纳并把样包打出来便于核对。
        if (myRankJson.empty() && g_userId.load() != 0 && !state.localFinished) {
            if (CountJsonObjectsWithPlayerId(rankListJson) > 0) {
                NotifyHorseProgress(L"收到一份不含自己的结算包，按迟到包忽略：" +
                                    MakeJsonPreview(rankListJson, 180));
                return;
            }
            NotifyHorseProgress(L"结算包里解析不到 player_id，先按完成处理，样包：" +
                                MakeJsonPreview(rankListJson, 200));
        }

        if (!myRankJson.empty()) {
            const int myPoint = ExtractJsonInt(myRankJson, "point");
            const int myCostTime = ExtractJsonInt(myRankJson, "cost_time");
            const int myHorseIid = ExtractJsonInt(myRankJson, "iid");
            {
                std::lock_guard<std::mutex> lock(g_horseInfoMutex);
                state.myInfo.rank = myRank;
                if (myCostTime > 0) {
                    state.myInfo.cost_time = myCostTime;
                }
            }

            std::wstring msg = L"游戏结束！排名第" + std::to_wstring(myRank) +
                L"，point=" + std::to_wstring(myPoint);
            if (myCostTime > 0) {
                msg += L"，用时=" + std::to_wstring(myCostTime) + L"秒";
            }
            if (myHorseIid > 0) {
                msg += L"，坐骑IID=" + std::to_wstring(myHorseIid);
            }
            NotifyHorseProgress(msg);
        } else {
            NotifyHorseProgress(L"游戏结束！未在排名列表中找到自己，原始排名=" + MakeJsonPreview(rankListJson, 180));
        }
    }

    state.receivedEndGame = true;
    SetHorsePhase(state, HORSE_PHASE_FINISHED);
    state.isSettling = false;
}

void HandleHorseExitRoomResponse(HorseCompetitionState& state, const std::string& json) {
    // AS3 HorseModel.as:159-167：EXIT_ROOM 回包带 player_id 与 code，
    // player_id 是自己时派发 HORSE_DESTORY_ALL（房间对自己已销毁），
    // 是别人时只是移除成员。只有自己那条才算「退房成功」。
    const uint32_t playerId = ExtractJsonUInt(json, "player_id");
    if (playerId != 0 && playerId == g_userId.load()) {
        // 只认「我们刚发了 EXIT_ROOM、正在等确认」这个窗口里的回包。
        // 上一局的退房回包可能晚几百毫秒才到，落进下一局的入房/开局等待里，
        // 会把 roomGone 置真、inRoom 置假，于是刚进的新房被白等 6 秒后退掉，
        // 或者开局第一个 tick 就 break，本局奖励直接丢。
        if (!state.awaitingExit) {
            return;
        }
        state.roomGone = true;
        state.inRoom = false;
    }
}

void HandleHorseSyncMemberResponse(HorseCompetitionState& state, const std::string& json) {
    const uint32_t playerId = ExtractJsonUInt(json, "player_id");
    const int costTime = ExtractJsonInt(json, "cost_time");
    if (playerId == g_userId.load()) {
        std::lock_guard<std::mutex> lock(g_horseInfoMutex);
        state.myInfo.cost_time = costTime;
    }
}

}  // namespace

void SetHorseProgressCallback(HorseProgressCallback callback) {
    g_horseProgressCallback.store(callback);
}

void RequestStopHorseCompetition() {
    auto& state = ActivityStateManager::Instance().GetHorseCompetitionState();
    if (!state.isRunning) {
        NotifyHorseProgress(L"坐骑大赛当前未在运行");
        return;
    }

    state.stopRequested = true;
    if (state.isGaming) {
        NotifyHorseProgress(L"已请求停止，当前局完成后将自动停止");
    } else {
        NotifyHorseProgress(L"已请求停止，当前流程将尽快结束");
    }
}

BOOL SendHorseCompetitionPacket(
    const std::string& operation,
    const std::vector<int32_t>& bodyValues,
    bool useGameCmd) {
    return SendHorsePacketBody(BuildHorsePacketBody(operation, bodyValues), useGameCmd);
}

BOOL SendHorseJoinGamePacket() {
    return SendHorseCompetitionPacket(HorseCmd::JOIN_GAME);
}

BOOL SendHorseReadyPacket() {
    return SendHorseCompetitionPacket(HorseCmd::READY);
}

BOOL SendHorseExitRoomPacket() {
    return SendHorseCompetitionPacket(HorseCmd::EXIT_ROOM);
}

// 退房并尽力确认。不等确认就立刻 JOIN_GAME 会撞上服务器侧的「已在房间」，
// 表现为入房超时死循环。等不到回包也不阻塞流程，只是退得慢一点。
// 发包失败会重试一次：SendPacket 偶发失败（队列满/连接抖动）时若直接放行，
// 下一次 JOIN_GAME 必然回「已在房间」，白等 6 秒。
//
// acceptSentOnly：不在房间时服务器通常不会回 EXIT_ROOM，此时「等不到确认」
// 并不代表退房失败。入房前的预防性清理用这个模式，只确认包发出去了，
// 否则每一轮都会白等满超时，还永远把 lastExitConfirmed 记成 false。
static bool LeaveHorseRoomAndConfirm(DWORD confirmTimeoutMs = 800, bool acceptSentOnly = false) {
    auto& state = ActivityStateManager::Instance().GetHorseCompetitionState();
    state.roomGone = false;
    state.awaitingExit = true;

    BOOL sent = SendHorseExitRoomPacket();
    if (!sent) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        sent = SendHorseExitRoomPacket();
    }

    bool confirmed = false;
    if (sent) {
        confirmed = WaitForHorseCondition(
                        state, [&state] { return state.roomGone.load(); }, confirmTimeoutMs, false) ==
                    HorseWaitOutcome::Signalled;
    }

    state.awaitingExit = false;
    state.roomGone = false;
    state.inRoom = false;
    return confirmed || (acceptSentOnly && sent != FALSE);
}

// 重试退避：200ms 起、每次翻倍、封顶 3 秒。既不会把服务器刷爆，也不至于
// 长时间空转。注意它只影响「两次尝试之间」的间隔，不会终止流程。
static DWORD HorseRetryBackoffMs(int failures) {
    if (failures <= 1) {
        return 200;
    }
    const int shift = (std::min)(failures - 1, 4);
    return static_cast<DWORD>((std::min)(3000, 200 << shift));
}

// 本局「还要等多久结算」的预算（秒），用来替代原来写死的 140 秒。
//
// AS3 的算法是 update_time - systemTimes（HorseGameView.as:479），两个都是服务器
// 时间戳，直接相减就是剩余秒数。C++ 没有服务器的 systemTimes（AS3 那个值来自
// PLAYER_STATUS 包，本模块并不解析），所以换个等价算法：
//
//   服务器侧本局总长 = update_time - start_time
//   本地已跑时长     = steady_clock::now() - raceStartSteadyMs
//   剩余             = 总长 - 已跑
//
// 两个时间戳同源（都是服务器写入 room_status 的），差值不受时钟偏移影响；
// 本地已跑时长用 steady_clock，不受系统时间被改的影响。剩下唯一的误差是
// 「本地起跑到服务器 start_time 之间那点偏差」，量级在几百毫秒。
//
// 拿不到任一字段时返回 0，调用方自己决定兜底多久。
static int GetHorseSettleBudgetSeconds(const HorseCompetitionState& state) {
    const int startTime = state.roomStartTime.load();
    const int updateTime = state.roomUpdateTime.load();
    const int64_t raceStartMs = state.raceStartSteadyMs.load();
    if (startTime <= 0 || updateTime <= 0 || raceStartMs <= 0 || updateTime <= startTime) {
        return 0;
    }

    using namespace std::chrono;
    const int64_t nowMs =
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    const double elapsedSeconds = static_cast<double>(nowMs - raceStartMs) / 1000.0;
    const double totalSeconds = static_cast<double>(updateTime - startTime);
    const double remaining = totalSeconds - elapsedSeconds;
    if (remaining <= 0.0) {
        // 按服务器时间戳算已经到点了，但我们还没收到结算包。给 15 秒宽限：
        // 不能给 1 秒（结算包慢一拍就把整局奖励丢了），也不该退回 140 秒
        // （那样「时间戳已过期」这条路径等于没优化）。
        return 15;
    }
    // 上限 300 秒只是防服务器给一个离谱的 update_time（比如跨天）把流程卡住。
    return static_cast<int>((std::min)(300.0, remaining));
}

BOOL SendHorseUIInfoPacket() {
    auto& state = ActivityStateManager::Instance().GetHorseCompetitionState();
    // 序号 +1、清掉「已收到」、标记「有一次请求在等应答」：只有这次请求之后到达的
    // 回包才被 handler 采纳（上一轮迟到的 ui_info 会被 pending==false 挡掉，否则
    // 结算前预取会拿到赛前的旧点数，刷满了也发现不了）。
    ++state.uiInfoSeq;
    state.uiInfoReceived = false;
    state.uiInfoPending = true;
    const BOOL sent = SendHorseCompetitionPacket(HorseCmd::UI_INFO);
    if (!sent) {
        state.uiInfoPending = false;  // 包没发出去，别让下一条迟到回包被当成这次的应答
    }
    return sent;
}

BOOL SendHorsePlayGamePacket(int distance) {
    return SendHorseCompetitionPacket(HorseCmd::PLAY_GAME, {distance});
}

BOOL StartHorseCompetitionGame() {
    auto& state = ActivityStateManager::Instance().GetHorseCompetitionState();

    if (state.isGaming) {
        return FALSE;
    }

    int baseSpeed = 0;
    int baseIntimate = 0;
    int baseHp = 0;
    {
        // myInfo 由收包分发线程写，这里在收包线程/一键线程上读，必须同锁。
        std::lock_guard<std::mutex> lock(g_horseInfoMutex);
        baseSpeed = state.myInfo.horse_base_speed;
        baseIntimate = state.myInfo.horse_base_intimate;
        baseHp = state.myInfo.horse_base_Hp;
    }

    // 速度只能来自服务器下发的 horse_info（JOIN_GAME 广播里每个成员各自的
    // base_speed / base_intimate / base_power，见 MemberInfo.as:96-102）。
    // 旧代码在拿不到时兜底成 base_speed=600（→ 每帧 10.25、2.4 秒跑完 1500 米），
    // 那是「偶发秒完成/被判异常」的来源；后来改成兜底 30 也是凭空编一个服务器没下发
    // 过的速度。两者都不要：拿不到就中止本局重来，绝不自造速度。
    if (baseSpeed <= 0 || baseHp <= 0) {
        // 复用看门狗标记：它同样表示「本局没法正常跑完，交给外层重试」，
        // 等开局循环会立刻跳出并按「属性缺失」收尾。
        state.raceAborted = true;
        return FALSE;
    }

    // 速度基础值严格按 AS3 公式（HorsePlayer.as:401-402）。
    state.speed = (baseSpeed + 15.0) / 60.0;
    state.accSpeed = (baseSpeed + 15.0 + baseIntimate / 5.0) / 60.0;
    state.maxHp = baseHp;

    // 这只坐骑的「理论最好成绩」：速度一帧不改，只按 AS3 的稳态冲刺占比 20.1%
    // （回血 0.84/帧、耗血 3.33/帧）估出它自己跑完 1500 米要多久。
    // 这个数只用来定看门狗，绝不反过来改速度。
    const double vRunPerSec = state.speed * kHorseTargetFps;
    const double vAccPerSec = state.accSpeed * kHorseTargetFps;
    constexpr double kAccDutySteady = 0.84 / (0.84 + 3.33);  // ≈ 20.1%
    const double shapeAvgSpeed = vRunPerSec + kAccDutySteady * (vAccPerSec - vRunPerSec);
    const double naturalSeconds =
        (shapeAvgSpeed > 1.0)
            ? (static_cast<double>(HorseCompetitionState::ROUTE_DISTANCE) / shapeAvgSpeed)
            : kHorseWatchdogFloorSeconds;

    // 看门狗：进度只由上面这两条速度积分产生，墙钟不补距离，所以这里只是
    // 「彻底卡死就中止本局」的兜底（理论时长 × 1.8，且不低于 60 秒）。
    state.raceTargetSeconds = (std::max)(kHorseWatchdogFloorSeconds,
                                         naturalSeconds * kHorseWatchdogFactor);

    state.hp = state.maxHp;
    state.distance = 0.0;
    state.state = "RUN";
    state.lastState = "RUN";
    state.isDie = false;
    state.items.clear();
    state.localFinished = false;
    state.receivedEndGame = false;
    state.abnormalRoundEnd = false;
    state.awaitingStart = false;
    state.raceAborted = false;
    // 记下本局在本地时钟上的起点：结算剩余时间要靠
    // 「(update_time - start_time) - 已跑秒数」来算（见 GetHorseSettleBudgetSeconds）。
    using namespace std::chrono;
    state.raceStartSteadyMs = duration_cast<milliseconds>(
                                  steady_clock::now().time_since_epoch())
                                  .count();

    // 先回收上一局的线程，再置运行标志——顺序反过来会挂死：
    // 旧线程的循环条件是 g_horseRaceThreadRunning && state.isGaming，
    // 若先写 g_horseRaceThreadRunning=true / phase=GAMING 再去 join，旧线程会在这
    // 16ms 窗口里看到条件重新为真而"复活"，join 就永久阻塞，而这里跑在收包
    // 分发线程上，整个收包链路当场卡死。
    //
    // 整个「停旧线程 + 置状态 + 起新线程」必须在一把锁里完成：StopHorseCompetitionGame
    // 在另一个线程（一键工作线程）上跑，若 phase/running 的写入落在锁外，
    // 两边交错会留下「线程已退出、phase 还是 GAMING」的错位状态。
    {
        std::lock_guard<std::mutex> lock(g_horseGameThreadMutex);
        // 锁内再确认一次：外面那次检查到进入锁之间有窗口，StopHorseCompetitionGame
        // 在另一个线程上可能刚好把这一局掐掉，重复起线程会让两局抢同一份 state。
        if (state.isGaming) {
            return FALSE;
        }
        g_horseRaceThreadRunning = false;
        if (g_horseGameThread.joinable()) {
            g_horseGameThread.join();
        }
        SetHorsePhase(state, HORSE_PHASE_GAMING);
        g_horseRaceThreadRunning = true;
        g_horseGameThread = std::thread(HorseGameMainLoop);
    }

    return TRUE;
}

void StopHorseCompetitionGame() {
    auto& state = ActivityStateManager::Instance().GetHorseCompetitionState();

    // 与 StartHorseCompetitionGame 共用同一把锁，避免「线程已退出、phase 还是
    // GAMING」这种两边交错留下的错位状态。
    std::lock_guard<std::mutex> lock(g_horseGameThreadMutex);
    g_horseRaceThreadRunning = false;
    if (g_horseGameThread.joinable()) {
        g_horseGameThread.join();
    }

    state.isGaming = false;
    if (!state.isFinished) {
        SetHorsePhase(state, HORSE_PHASE_IDLE);
    }
}

void ProcessHorseCompetitionResponse(const GamePacket& packet) {
    if (packet.body.size() < 4) {
        return;
    }

    auto& state = ActivityStateManager::Instance().GetHorseCompetitionState();

    size_t offset = 0;
    const uint16_t cmdLen = ReadUInt16LE(packet.body.data(), offset);
    if (offset + cmdLen > packet.body.size()) {
        return;
    }

    const std::string cmd(reinterpret_cast<const char*>(packet.body.data() + offset), cmdLen);
    offset += cmdLen;

    std::string json;
    if (cmd == HorseCmd::ROOM_INFO) {
        if (ReadHorseJsonPayload(packet, offset, json)) {
            HandleHorseRoomInfoResponse(state, json);
        }
    } else if (cmd == HorseCmd::START_GAME) {
        if (ReadHorseJsonPayload(packet, offset, json)) {
            HandleHorseStartGameResponse(state, json);
        }
    } else if (cmd == HorseCmd::JOIN_GAME) {
        if (ReadHorseJsonPayload(packet, offset, json)) {
            HandleHorseJoinGameResponse(state, json);
        }
    } else if (cmd == HorseCmd::READY) {
        if (ReadHorseJsonPayload(packet, offset, json)) {
            HandleHorseReadyResponse(state, json);
        }
    } else if (cmd == HorseCmd::ROOM_STATUS) {
        if (ReadHorseJsonPayload(packet, offset, json)) {
            HandleHorseRoomStatusResponse(state, json);
        }
    } else if (cmd == HorseCmd::UI_INFO) {
        HandleHorseUiInfoResponse(state, packet, offset);
    } else if (cmd == HorseCmd::END_GAME) {
        HandleHorseEndGameResponse(state, packet, offset);
    } else if (cmd == HorseCmd::SYNC_MEMBER) {
        if (ReadHorseJsonPayload(packet, offset, json)) {
            HandleHorseSyncMemberResponse(state, json);
        }
    } else if (cmd == HorseCmd::EXIT_ROOM) {
        if (ReadHorseJsonPayload(packet, offset, json)) {
            HandleHorseExitRoomResponse(state, json);
        }
    }

    state.waitingResponse = false;
}

BOOL StartOneKeyHorseCompetitionPacket() {
    auto& state = ActivityStateManager::Instance().GetHorseCompetitionState();

    bool alreadyRunning = false;
    {
        // 「检查 + Reset + 置位」必须在同一把锁里完成：UI 每次点击都会新开一个
        // detached 线程，连点两次会让两份流程都通过 isRunning 检查，各自 Reset
        // 对方的状态、互相 StopHorseCompetitionGame 掐掉对方的比赛线程。
        std::lock_guard<std::mutex> runLock(g_horseRunMutex);
        if (state.isRunning) {
            alreadyRunning = true;
        } else {
            // Reset 会清 myInfo（含 std::wstring）和 itemDistances，与收包分发线程
            // 的解析并发是 UB，需与解析侧共用一把锁。
            std::lock_guard<std::mutex> infoLock(g_horseInfoMutex);
            state.Reset();
            state.isRunning = true;
            state.stopRequested = false;
        }
    }
    if (alreadyRunning) {
        // 提示放到锁外：持 g_horseRunMutex 调 UI 回调没有好处，回调里若再触发
        // 坐骑命令就是自锁（非递归 mutex）。
        NotifyHorseProgress(L"坐骑大赛正在运行中，请勿重复启动");
        return FALSE;
    }

    bool rideHintShown = false;
    // 失败计数只用于「退避 + 提示」，绝不终止流程：坐骑大赛的语义就是
    // 「一直匹配，直到分数刷满或手动停止」。服务器拒绝、活动没开、房间凑不齐
    // 都是可重试的常态，停掉整个流程反而要人工再点一次启动。
    int consecutiveFailures = 0;
    // 「等满 10 分钟也没开局」的连续次数，以及「除等开局外的其它失败」连续次数。
    //
    // 这两个计数存在的理由：consecutiveFailures 只用来做退避，从不终止流程，
    // 于是一旦活动没开 / 服务器维护 / 房间彻底卡死，ui_info 会永远超时、
    // 10 分钟上限会一轮一轮空转，用户在界面上看到的是「一直在重试」但永远没结果，
    // 只能手动点停止。给一个明确的上限，到点就直接收工并说清楚原因，
    // 比无限空转好排查。
    //
    // 阈值取保守值：等开局 3 次（最坏 30 分钟）说明服务器那边确实没人跟我们在同一
    // 个房间，再等下去也不会自己好；其它失败 30 次（退避到 3 秒时约两三分钟）
    // 说明连包都发不出去或活动压根没开。两者都会在提示里写明原因。
    int startGiveUpStreak = 0;
    int hardFailureStreak = 0;
    constexpr int kMaxStartGiveUpStreak = 3;
    constexpr int kMaxHardFailureStreak = 30;
    // 是否需要在下一轮开头重新拉 UI_INFO。只有「首轮」和「上一局正常结算后」
    // 才需要——中途入房失败重试时数据还是新鲜的，重拉只会白等 3 秒。
    bool needUiInfo = true;
    // 上一局退房时顺手提前取到的剩余点数（-1 表示没取到）。
    // 目的是把「退房 + 结算」这段本来就要等的时间用来做 UI_INFO 往返，
    // 省掉下一轮开头整整 3 秒的纯等待。
    int pendingDayPoint = -1;
    // 上一轮退房是否拿到服务器确认。false 时下一轮入房前先补一次 EXIT_ROOM，
    // 否则服务器会回「已在房间」，每次白等满 6 秒入房超时。
    bool lastExitConfirmed = false;
    // 本工具是否真的进过房间。首轮一律不发 EXIT_ROOM：这时我们根本不在房间里，
    // 一个凭空多出来的退房包会让服务器把这次会话记成「异常退出」，
    // 之后即使 JOIN 回了包也不给推开局（实测就是这样：首轮入房成功但 20 秒不开赛）。
    bool everJoined = false;

    // 统一停止出口：停止是用户主动行为，按成功返回（不是失败），并且要顺手退房，
    // 否则下次启动会撞上「已在房间」。alreadyLeft 用于已经退过房的收尾路径，
    // 避免多补一个 EXIT_ROOM。
    auto stopAndReturn = [&state](const wchar_t* reason, bool alreadyLeft = false) -> BOOL {
        NotifyHorseProgress(reason);
        StopHorseCompetitionGame();
        if (!alreadyLeft) {
            if (!LeaveHorseRoomAndConfirm(400)) {
                // 服务器拒绝退房（比赛阶段里会这样，游戏内提示「游戏开始无法退出房间」）。
                // 这时房间在服务器侧还留着我们，下次启动会先补一次 EXIT_ROOM 清理；
                // 必须提示出来，否则用户只看到游戏内那句报错，不知道工具已经停了。
                // 文案要写实际行为：下一轮首轮不会再补发 EXIT_ROOM（everJoined 是 false），
                // 真正的兜底是「入房 6 秒超时 → 标记 everJoined → 退房后重试」。
                NotifyHorseProgress(
                    L"退房确认未收到，服务器可能仍认为你在房间里；下次入房超时会自动清房重试");
            }
        }
        state.isRunning = false;
        state.stopRequested = false;
        return TRUE;
    };

    // 统一重试出口：失败计数 + 提示 + 停线程 + 退房 + 退避。调用方负责 continue。
    // 停线程必须在退房之前：START_GAME 可能先于回包到达，线程已经起来的话会在
    // 退房窗口里继续往死房间发 PLAY_GAME。
    //
    // startGiveUp 标记「这次失败的原因是等满 10 分钟没开局」，用来区分两种失败形态：
    // 凑不到人（可能换一班人就好了，给 3 次机会）和包都发不出去（活动没开，给 30 次）。
    // 到阈值时只记一个 reason，真正停机放在循环开头——这样不必让每个调用点都去
    // 检查返回值（有十几个 `retryAndContinue(...); continue;`，逐个改成 if 反而更容易漏）。
    auto retryAndContinue = [&](const std::wstring& message, bool startGiveUp = false) {
        ++consecutiveFailures;
        if (startGiveUp) {
            ++startGiveUpStreak;
            hardFailureStreak = 0;
        } else {
            ++hardFailureStreak;
            // 反方向也要清：中间夹一次「等满 10 分钟没开局」然后又恢复成普通失败，
            // 说明房间其实是能进的，之前那几次等不到开局不该继续累计到三次上限。
            startGiveUpStreak = 0;
        }
        NotifyHorseProgress(message + L"（第 " + std::to_wstring(consecutiveFailures) + L" 次）");
        StopHorseCompetitionGame();
        lastExitConfirmed = LeaveHorseRoomAndConfirm(400);
        // 重试轮必须重新拉一次 ui_info：本轮提前取到的 pendingDayPoint 已经在
        // 循环开头被消费掉，若不重置，下一轮 haveFreshPoints 恒为 false，
        // 点数已刷满也发现不了（会多跑一局，甚至一直跑下去）。
        pendingDayPoint = -1;
        needUiInfo = true;
        std::this_thread::sleep_for(
            std::chrono::milliseconds(HorseRetryBackoffMs(consecutiveFailures)));
    };

    // 放弃条件命中后由循环开头统一收尾，避免在 lambda 里直接 return。

    while (true) {        // 放弃条件检查放在循环开头：所有重试路径都是 `retryAndContinue(...); continue;`，
        // 统一在这里判断，比在每个调用点插 if 可靠。
        if (startGiveUpStreak >= kMaxStartGiveUpStreak) {
            NotifyHorseProgress(
                L"连续 " + std::to_wstring(startGiveUpStreak) +
                L" 次等满 10 分钟仍未开局，房间一直凑不齐人，已停止。"
                L"（该时段可能没有其他玩家参赛，稍后重新启动即可）");
            state.isRunning = false;
            return TRUE;
        }
        if (hardFailureStreak >= kMaxHardFailureStreak) {
            NotifyHorseProgress(
                L"连续 " + std::to_wstring(hardFailureStreak) +
                L" 次操作失败，活动可能未开启或服务器维护中，已停止。");
            state.isRunning = false;
            return TRUE;
        }

        ResetHorseRoundState(state);

        // 本轮的剩余点数是否有新鲜数据。ResetHorseRoundState 会把 resDayPoint
        // 清 0，只有「刚取到」的那一轮才能据此判断是否刷满，否则重试轮会被
        // 误判成「已完成」而整条流程提前结束。
        bool haveFreshPoints = false;

        if (pendingDayPoint >= 0) {
            // 上一局收尾时已经取到，直接沿用，不再发一轮往返。
            state.resDayPoint = pendingDayPoint;
            state.uiInfoReceived = true;
            pendingDayPoint = -1;
            needUiInfo = false;
            haveFreshPoints = true;
        } else if (needUiInfo) {
            SetHorsePhase(state, HORSE_PHASE_FETCHING_UI_INFO);

            NotifyHorseProgress(L"正在获取活动信息...");
            SendHorseUIInfoPacket();
            const int uiInfoWaitSeq = state.uiInfoSeq.load();

            if (WaitForHorseCondition(
                    state,
                    [&state, uiInfoWaitSeq] {
                        return state.uiInfoReceived.load()
                               && state.uiInfoAckSeq.load() == uiInfoWaitSeq;
                    },
                    3000) != HorseWaitOutcome::Signalled) {
                if (state.stopRequested) {
                    return stopAndReturn(L"坐骑大赛已按请求停止");
                }
                retryAndContinue(L"获取活动信息超时，稍后重试");
                continue;
            }

            NotifyHorseProgress(L"活动信息获取成功，剩余点数: " +
                                std::to_wstring(state.resDayPoint.load()));
            needUiInfo = false;
            haveFreshPoints = true;
        }

        if (haveFreshPoints && state.resDayPoint <= 0) {
            NotifyHorseProgress(L"坐骑大赛已完成：今日骑乘点已刷满");
            state.isRunning = false;
            return TRUE;
        }

        // AS3 中 is_ride <= 0 会改走「赠送坐骑」界面而不入房（HorseMainView.onStartConnect）。
        // 本工具以临时坐骑参赛（join_game 本身不带坐骑参数），故不阻断，只提示一次以便排查。
        if (!rideHintShown && state.isRide.load() <= 0) {
            rideHintShown = true;
            NotifyHorseProgress(L"提示：当前未检测到已骑乘坐骑，将以临时坐骑参赛");
        }

        if (state.stopRequested) {
            return stopAndReturn(L"坐骑大赛已按请求停止");
        }

        NotifyHorseProgress(L"正在加入游戏房间...");

        // 只有「真的进过房间」才需要清残留房籍。首轮不发：这时我们不在任何房间里，
        // 凭空一个 EXIT_ROOM 会让服务器把这次会话记成异常退出，之后即使 JOIN 回了包
        // 也不给推开局（实测就是首轮入房成功但 20 秒不开赛）。
        if (everJoined && !lastExitConfirmed) {
            NotifyHorseProgress(L"先清理上一轮的房间状态...");
            // acceptSentOnly：不在房间时服务器不会回 EXIT_ROOM，等确认只会白等。
            lastExitConfirmed = LeaveHorseRoomAndConfirm(300, true);
        }

        SetHorsePhase(state, HORSE_PHASE_JOINING_ROOM);
        state.awaitingStart = true;
        if (!SendHorseJoinGamePacket()) {
            // 包都没发出去就别干等 6 秒了，直接进重试。
            state.awaitingStart = false;
            retryAndContinue(L"发送入房请求失败，重试");
            continue;
        }

        if (WaitForHorseCondition(
                state, [&state] { return state.inRoom.load(); }, 6000) !=
            HorseWaitOutcome::Signalled) {
            state.awaitingStart = false;
            if (state.stopRequested) {
                return stopAndReturn(L"坐骑大赛已按请求停止");
            }
            // 入房超时也必须退房：服务器侧可能仍把我们挂在旧房间里，
            // 下一次 JOIN_GAME 会直接回「已在房间」，形成死循环。
            // 同时按「可能已经进过房」处理：回包丢了但我们其实在房间里，
            // 下一轮入房前要先清残留房籍，否则会一直撞「已在房间」。
            everJoined = true;
            retryAndContinue(L"加入房间超时，退房后重试");
            continue;
        }

        NotifyHorseProgress(L"成功加入房间，正在准备...");
        everJoined = true;
        if (!SendHorseReadyPacket()) {
            // READY 发不出去就开局无望，别白等 20 秒；退房重来。
            state.awaitingStart = false;
            retryAndContinue(L"发送准备请求失败，退房后重试");
            continue;
        }

        // 这里不能无条件 SetHorsePhase(WAITING_START)：如果服务器在我们 ready
        // 之后、这行之前就推了 START_GAME，收包线程已经把 phase 置成 GAMING，
        // 再写 WAITING_START 会把 isGaming 打回 false，主循环当场退出，
        // 比赛一帧都不跑，外层白等满超时。AS3 同样只在未开局时改房间状态
        // （HorseModel.as:181 的 `if(RoomInfoData.instance.status != status)`）。
        if (!state.isGaming) {
            NotifyHorseProgress(L"已准备，等待其他玩家加入...");
            SetHorsePhase(state, HORSE_PHASE_WAITING_START);
        }

        // 进房之后就是等开局，这里绝不能 20 秒不开局就退房。
        //
        // 服务器开局的条件是「房间里凑齐人」，不是「我们准备好了」。我们一个人
        // 在房间里干等，服务器就不会推开局；而每 20 秒退房重进，等于把匹配进度
        // 清零重来，永远等不到第二个人进来——实测日志里那个
        // 「入房成功 → 20 秒超时 → 退房 → 再入房 → 又 20 秒超时」的死循环就是这么来的。
        //
        // AS3 那边同样是「等」：HorseGameView.as:130 进房跑 setInverse(...,16)
        // 的 16 秒自动准备倒计时，到 0 才派发 readyBtn 的 MOUSE_DOWN，说明服务器
        // 本来就预期玩家在房间里待着等人。我们主动发 READY 只是省掉那 16 秒。
        //
        // 所以：一直等到开局为止。只有三种情况会离开这个等待——
        //   1) 开局了（isGaming）
        //   2) 房间被服务器解散（roomGone）
        //   3) 用户点了停止（stopRequested）
        // 另外每 3 分钟补发一次 READY，防止我们那一个 ready 包在半路丢了导致
        // 服务器一直等我们准备；总上限 10 分钟只是「房间彻底卡死」的兜底。
        constexpr DWORD kReadyRepingMs = 180000;   // 3 分钟补一次 READY
        constexpr DWORD kStartGiveUpMs = 600000;   // 10 分钟还开不了就退房重来
        constexpr DWORD kWaitNotifyMs = 30000;     // 每 30 秒报一次等待进度
        DWORD waitedMs = 0;
        DWORD sinceReadyPing = 0;
        DWORD sinceNotify = 0;
        while (true) {
            if (state.isGaming) {
                break;
            }
            if (state.roomGone || state.stopRequested) {
                break;
            }
            // StartHorseCompetitionGame 拒绝起跑（拿不到服务器下发的坐骑属性）时
            // 会置这个标记：立刻按「属性缺失」收尾，不在这里死等满 10 分钟。
            if (state.raceAborted) {
                break;
            }
            // 服务器把这一局跑完并推了结算（我们错过了 start_game，本地没参赛）。
            // 继续等只会在房间已结算后白等，交给下面的收尾分支进下一轮。
            if (state.receivedEndGame) {
                break;
            }
            if (waitedMs >= kStartGiveUpMs) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            waitedMs += 100;
            sinceReadyPing += 100;
            sinceNotify += 100;
            if (sinceReadyPing >= kReadyRepingMs) {
                sinceReadyPing = 0;
                SendHorseReadyPacket();
            }
            if (sinceNotify >= kWaitNotifyMs) {
                sinceNotify = 0;
                NotifyHorseProgress(L"仍在等待其他玩家加入（已等待 " +
                                    std::to_wstring(waitedMs / 1000) + L" 秒）...");
            }
        }
        if (!state.isGaming) {
            const bool missedHorseInfo = state.raceAborted;
            state.awaitingStart = false;
            if (state.stopRequested) {
                return stopAndReturn(L"坐骑大赛已按请求停止");
            }
            if (state.receivedEndGame) {
                // 本局在我们起跑之前就被服务器结算掉了（见 HandleHorseEndGameResponse：
                // 错过 start_game 时服务器照样跑完并推结算）。这不属于本工具失败，不能
                // 走下面的"长时间未开局"重试分支——那会记一次失败退避，还可能重复发
                // EXIT_ROOM 撞「游戏开始无法退出房间」。
                NotifyHorseProgress(L"本局已由服务器结算，退房后重新匹配");
                StopHorseCompetitionGame();
                lastExitConfirmed = LeaveHorseRoomAndConfirm(400);
                pendingDayPoint = -1;
                needUiInfo = true;
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(HorseRetryBackoffMs(consecutiveFailures)));
                continue;
            }
            if (missedHorseInfo) {
                retryAndContinue(L"开局包缺少坐骑属性，退房后重新匹配");
                continue;
            }
            // 走到这里且已经等满 10 分钟，就是「房间凑不齐人」这一类失败：
            // 单独计数，连续三次就不再无限空转（见 kMaxStartGiveUpStreak）。
            const bool waitedFullTimeout = (waitedMs >= kStartGiveUpMs);
            retryAndContinue(state.roomGone ? L"房间已关闭，重新匹配"
                                            : L"长时间未开局，退房后重新匹配",
                             waitedFullTimeout);
            continue;
        }

        state.awaitingStart = false;
        NotifyHorseProgress(L"游戏开始！正在比赛中...");

        // 结算等待必须分两段，不能用一个总上限：
        // - 比赛段（localFinished 之前）：进度严格按这只坐骑的真实速度推进，慢坐骑
        //   （base_speed 10 → v_run 25 m/s）单局要 60 秒左右，快坐骑 25 秒。所以预算
        //   不写死秒数，按看门狗（state.raceTargetSeconds = 理论时长 × 1.8）再加 30 秒。
        // - 结算段（localFinished 之后）：AS3 收到 ROOM_STATUS=SETTLE 后跑的是服务器
        //   下发的 update_time 倒计时（HorseGameView.as:477-486 overInverse，到 0 才
        //   弹结算界面），而服务器还要等其他玩家跑完（真人 33~60 秒）。
        //   若沿用同一个上限，「我跑完了、服务器还没结算」就会被判超时退房，
        //   本局奖励直接丢掉。
        //
        // 结算预算优先用服务器下发的 update_time 换算（见 GetHorseSettleBudgetSeconds），
        // 拿不到才用兜底值：写死 140 秒在慢结算时会让本局奖励白丢
        // （服务器 160 秒才结算，我们 140 秒就走了），在快结算时又要白等一百多秒。
        //
        // 注意不能在比赛开始前算一次就定死：update_time 是服务器在 ROOM_STATUS 里推的，
        // AS3 也只在收到那个包时才写（HorseModel.as:184-187），往往是我们冲线之后
        // 才到。所以预算在下面循环里跟着 update_time 刷新，见 waitLimit 的抬高逻辑。
        constexpr int kSettleFallbackTicks = 600;  // 拿不到时间戳时冲线后再等 60 秒
        const int raceWaitTicks =
            static_cast<int>((state.raceTargetSeconds + 30.0) * 10.0);
        int waitCount = 0;
        int waitLimit = raceWaitTicks;
        // 结算截止点（waitCount 刻度），-1 表示「还没能算出来」。
        // 只算一次就锁死：一旦时间戳到齐就把绝对截止点固定下来，避免
        // 每 tick 重算导致截止点跟着时间一起长出去（那就是永不超时）。
        int settleDeadlineTick = -1;
        bool settlingNotified = false;
        bool localFinishNotified = false;
        bool endGameNotified = false;
        // break 的原因，决定后续是「重试」还是「正常收尾」：
        // roomGone 是服务器主动解散房间（结算可能已发过），不该算本工具的失败。
        bool stoppedByUser = false;
        bool stoppedByRoomGone = false;
        int roomGoneTicks = 0;
        while (!state.isFinished) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            waitCount++;

            // 用户点了停止：必须能中断。这里最长可能等 40+120 秒，
            // 不检查 stopRequested 就会出现「点了停止几十秒没反应」。
            if (state.stopRequested) {
                stoppedByUser = true;
                break;
            }

            // 比赛线程的看门狗已中止本局（进度长时间不动）：不必再等满比赛段预算。
            if (state.raceAborted) {
                break;
            }

            // 跑到终点后切换到「等结算」预算。
            //
            // update_time 往往就是我们冲线那一刻（或之后）才由 ROOM_STATUS 推下来的
            // （AS3 同样只在收到那个包时才写，HorseModel.as:184-187），所以这段时间的
            // 预算不能进循环前算一次就定死，得等时间戳到齐。
            //
            // 但「每次都重算」会踩一个坑：budgetSeconds 是「还剩多少秒」，会随时间递减，
            // 若每 tick 都算 waitCount + budget*10，遇到 GetHorseSettleBudgetSeconds
            // 返回固定值的情况（时间戳已过期时它返回 15），截止点就会跟着 waitCount
            // 一起长，等于永不超时。所以这里只采信「第一份到齐的时间戳」，算出一个
            // 绝对截止点之后就锁死，后面再来的 update_time 一律不采用。
            if (state.localFinished && settleDeadlineTick < 0) {
                const int budgetSeconds = GetHorseSettleBudgetSeconds(state);
                if (budgetSeconds > 0) {
                    settleDeadlineTick = waitCount + budgetSeconds * 10;
                    NotifyHorseProgress(L"按服务器下发的结算时间等待，预计 " +
                                        std::to_wstring(budgetSeconds) + L" 秒");
                } else if (!state.isSettling && !state.receivedEndGame) {
                    // 时间戳没到齐，且服务器还没表示进入结算：下一 tick 再等，
                    // 给 ROOM_STATUS 一点时间。真的一直不来就靠比赛段预算收尾。
                } else {
                    // 已经进入结算段却仍没有时间戳：60 秒兜底，只生效一次。
                    settleDeadlineTick = waitCount + kSettleFallbackTicks;
                    NotifyHorseProgress(L"未拿到服务器结算时间，按 60 秒兜底等待");
                }
                if (settleDeadlineTick > waitLimit) {
                    waitLimit = settleDeadlineTick;
                }
            }

            // 房间被解散 / 被踢：结算包不会再来了，继续等只是白等 120 秒。
            // 但给自己 3 秒宽限：服务器可能先推 FREE 再推 END_GAME，
            // 一看到 FREE 就退房会漏掉结算回包。
            if (state.roomGone) {
                if (state.receivedEndGame) {
                    break;
                }
                ++roomGoneTicks;
                if (roomGoneTicks > 30) {
                    NotifyHorseProgress(L"房间已关闭，本局提前结束");
                    stoppedByRoomGone = true;
                    break;
                }
            }

            if (waitCount > waitLimit) {
                break;
            }

            if (state.localFinished && !localFinishNotified) {
                localFinishNotified = true;
                NotifyHorseProgress(L"比赛已完成，等待服务器确认...");
            }

            if (state.isSettling && !settlingNotified) {
                settlingNotified = true;
                NotifyHorseProgress(L"游戏即将结束，等待服务器结算...");
            }

            if (state.receivedEndGame && !endGameNotified) {
                endGameNotified = true;
                NotifyHorseProgress(L"服务器已确认，游戏即将完成...");
            }

            if (waitCount % 100 == 0) {
                const int progress = (std::min)(
                    100, static_cast<int>(state.distance.load() * 100 /
                                          HorseCompetitionState::ROUTE_DISTANCE));
                if (!state.localFinished) {
                    NotifyHorseProgress(L"比赛进行中... 进度: " + std::to_wstring(progress) + L"%");
                } else if (!state.receivedEndGame) {
                    NotifyHorseProgress(L"等待服务器确认... (进度: " + std::to_wstring(progress) + L"%)");
                }
            }
        }

        if (stoppedByUser) {
            return stopAndReturn(L"坐骑大赛已按请求停止");
        }

        if (!state.isFinished) {
            // 看门狗中止：进度卡死，本局已经没有意义，退房重排。
            if (state.raceAborted) {
                retryAndContinue(L"比赛进度长时间无进展，已中止本局，退房后重新匹配");
                continue;
            }
            // 房间被解散：结算可能早就发过了，只是我们没等到。这种不该记失败，
            // 但也不能当「正常刷完」清零计数——保持原值，避免把一次真实异常洗掉。
            if (stoppedByRoomGone) {
                NotifyHorseProgress(L"房间已关闭，退房后重新匹配...");
                StopHorseCompetitionGame();
                lastExitConfirmed = LeaveHorseRoomAndConfirm(400);
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(HorseRetryBackoffMs(consecutiveFailures)));
                continue;
            }

            // 单局没等到结算：计一次失败，退房后继续下一局。
            // 这里绝不能终止整条流程——一次服务器抖动就要整轮重来的体验太差。
            retryAndContinue(L"本局未等到结算，退房后重试");
            continue;
        }

        StopHorseCompetitionGame();

        if (state.abnormalRoundEnd) {
            // 服务器明确回 result != 0：本局被判异常（没跑完 / 被判作弊 / 房间异常）。
            // 这不算「正常刷完」，不能清零失败计数，否则一直异常也发现不了。
            retryAndContinue(L"本局异常提前结束，退房后重新匹配");
            continue;
        }

        // 服务器回 result == 0 且给了排名，就是这一局正常结算完了。本地 localFinished
        // 为假不代表出错：服务器按 update_time 倒计时结算，可能比我们本地到点更早
        // （例如卡顿导致补帧令牌桶跟不上、进度停在 1400 米左右）。
        // 只有「本地明显没跑完」（不足 90% 路程）才当作被服务器提前结束。
        const double finishedRatio =
            state.distance.load() / static_cast<double>(HorseCompetitionState::ROUTE_DISTANCE);
        if (!state.localFinished && finishedRatio < 0.9) {
            // 服务器提前结束了这一局（进度被判定异常，或者房间被人拉走）。
            // 不能当「正常刷完」，否则失败计数被清零、问题永远发现不了。
            retryAndContinue(L"本局被服务器提前结束，仅跑到 " +
                             std::to_wstring(static_cast<int>(finishedRatio * 100)) + L"%，重新匹配");
            continue;
        }

        NotifyHorseProgress(L"比赛完成！正在退出...");
        // 一局正常走完就清零失败计数：只有「连续」失败才累积。
        consecutiveFailures = 0;
        // 放弃计数同理：这一局完整跑完了，说明活动开着、房间能凑人、包也发得出去，
        // 之前累积的「等不到开局 / 连不上」全都作废，从下一次失败重新数起。
        startGiveUpStreak = 0;
        hardFailureStreak = 0;
        // 退房之前就把下一轮要用的 UI_INFO 发出去：退房确认本身就要等一轮往返，
        // 够它回包了。这样下一轮开头不用再单独等一次 3 秒往返。
        //
        // 这里只留 50ms：UI_INFO 的往返通常 30~80ms，之后的 LeaveHorseRoomAndConfirm
        // 还要再等最多 400ms，两者相加足够。原先写死 150ms 是纯等待，收不到回包也
        // 只是退回「下一轮开头重新拉」的老路径，功能不受影响。
        SendHorseUIInfoPacket();
        const int uiInfoPrefetchSeq = state.uiInfoSeq.load();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        lastExitConfirmed = LeaveHorseRoomAndConfirm(400);
        // 只采信「应答了本次预取」的那份回包：要的是新鲜点数，不是赛前的旧值。
        if (state.uiInfoReceived && state.uiInfoAckSeq.load() == uiInfoPrefetchSeq) {
            pendingDayPoint = state.resDayPoint.load();
        }

        if (state.stopRequested) {
            return stopAndReturn(L"坐骑大赛已按请求停止", /*alreadyLeft=*/true);
        }

        // 没提前取到就退回「下一轮开头重新拉」的老路径，功能不受影响。
        needUiInfo = (pendingDayPoint < 0);
        NotifyHorseProgress(L"本局完成，准备继续刷取剩余骑乘点...");
        // 原先这里还有一次 150ms 的「局间间隔」sleep，已删除：上面的
        // LeaveHorseRoomAndConfirm 无论成败都已经等了一轮往返（最长 400ms），
        // 再补一次固定等待没有意义。
    }
}
