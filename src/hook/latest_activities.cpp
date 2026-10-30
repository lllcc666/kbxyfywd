#include "byte_reader.h"
#include "latest_activities.h"

#include "managed_thread.h"

#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "hook_shared.h"
#include "packet_builder.h"
#include "ui_bridge.h"
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
    const char* openUiOp;   ///< 进入活动操作串：多数 "open_ui"，730 为 "game_info"
    int targetScore;        ///< 走游戏流程时提交的目标分（= 各自满分）
    /// 扫荡门槛用的「上限最高分」，与历史统一标准 ShouldAutoSweep(bestScore, passScore)
    /// 的第二个参数同义：历史成绩达到它才允许直接扫荡。0 表示该活动没有分数概念
    /// （742 守护葫芦藤按棋盘格计奖，open_ui 里也没有成绩字段），只能先打一局。
    int passScore;
    bool reportsDailyTask;  ///< 结算后是否上报每日任务（721/730 有）
    /// 本地是否有可自动化的游戏循环。全部收录活动都必须是 true：
    /// 只有「完整打完一局」和「扫荡」两条路径，没有第三条。
    bool playable;
    /// open_ui / game_info 回包里是否自带「能不能扫荡」的本地判据，用来在开局决定
    /// 要不要先打一局。不能靠发 sweep_info 去问——那是「预览本次扫荡奖励」的包，
    /// 不是「有没有历史成绩」的查询，拿它当门槛会在开局先扣掉一次真实扫荡。
    ///
    ///   true  + passScore > 0 ：回包带历史最好成绩，门槛 ShouldAutoSweep(成绩, 满分)。
    ///   true  + passScore == 0：回包带「今天成功打完过一局」标志位（bestFlag），
    ///                           门槛 bestFlag > 0。882 南瓜VS南瓜王属于这一档。
    ///   false                 ：进入包只有次数与冷却（742 守护葫芦藤
    ///                           KBHuluModel.as:71-85），本地无从判断，只能问服务端。
    bool scoreInOpenUi;
};

// 列：id, name, openUiOp, targetScore, passScore, reportsDailyTask, playable, scoreInOpenUi
//
// passScore 一律取 AS3 里那个「显示给玩家看的上限最高分」常量，不要自己估：
//   647 突破迷阵    data/MazeAward.as:19            maxScore = 180
//   742 守护葫芦藤  无分数概念（按棋盘格计奖）        = 0
//   721 采菊东篱下  enum/EDataType.as:79            MAX_SCORE = 400
//   877 寻找七色茱萸 emun/EType.as:92               max_score = 10
//   730 智斗恶灵    enum/EDataType.as:123           MAX_SCORE = 160
//   880 夺宝行动    data/TreasureAward.as:13        MAX_NUM = 55
//                   （RuleUI.as:42 与 ResultUI.as:54 都把它写进 txtMax 显示给玩家，
//                    即「本局金币最高 55」，此前误填 200——一局只有 90 秒
//                    （GameUI.as:106 gameTime = 90）、每捡一枚金币 +1，200 根本打不出来。）
//   882 南瓜VS南瓜王 无上限常量 = 0（grep 全 SWF 无 maxScore/MAX_SCORE/max_medal；
//                    EType.as 的 AWARD_MAX_CHANGE 是奖励索引上限，不是勋章上限。
//                    勋章上限只在服务端、客户端不显示，所以不判「成绩是否满分」；
//                    但 open_ui 的 bestFlag 明确是「今天成功打完过一局」，用它当门槛。）
//
// 扫荡次数：全部活动的 AS3 扫荡窗口一律把次数一次交上去——
//   647 NewSweepUI.as:78        [GET_SWEEP, _curCnt]
//   742 SweepView.as:80         [GET_SWEEP, _curCnt]
//   721 ActSweepWin.as:84       [SWEEP, _curCnt]
//   877 ActSweepWin.as:77       [SWEEP, _curCnt]
//   730 Act730SweepWin.as:79    [SWEEP, _curCnt]
//   880 SweepUI.as:80           [GET_SWEEP, _curCnt]
//   882 ActSweepWin.as:61       [SWEEP, int(_changePanel.text)]
// 所以 sweep 一律带次数、一次发完，没有「无参 sweep 逐次发」那种活动。
const LatestActDesc kActivities[] = {
    {LatestAct::ACT_MAZE, L"突破迷阵", "open_ui", 180, 180, false, true, true},
    {LatestAct::ACT_HULU, L"守护葫芦藤", "open_ui", 0, 0, false, true, false},
    {LatestAct::ACT_STAR_PRINCESS, L"采菊东篱下", "open_ui", 400, 400, true, true, true},
    {LatestAct::ACT_FIND_CORNEL, L"寻找七色茱萸", "open_ui", 10, 10, false, true, true},
    {LatestAct::ACT_FIGHT_EVILS, L"智斗恶灵", "game_info", 160, 160, true, true, true},
    {LatestAct::ACT_TREASURE, L"夺宝行动", "open_ui", 55, 55, false, true, true},
    {LatestAct::ACT_PUMPKINS_PK, L"南瓜VS南瓜王", "open_ui", 0, 0, false, true, true},
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

// 等待回包的时间预算。
// SendPacket 在「发送成功但等不到回包」时不会自动重发，而是直接返回 FALSE，
// 因此这里的等待上限只需略大于 SendPacket 自身的 timeoutMs，余量留给响应分发线程解析。
constexpr DWORD kQueryTimeoutMs = 3000;   ///< 只读查询类（open_ui/sweep_info/sweep）
constexpr DWORD kActionTimeoutMs = 5000;  ///< 动作类（start_game/end_game）
constexpr DWORD kWaitMarginMs = 1000;

// 877 寻找七色茱萸：EScene.AWARD_LIST 共 20 项，结算包补 len = 20 - 4 = 16 个计数
// （ActivityGame.onReqEndGame:226）。
constexpr int kFindCornelAwardSlots = 16;

// 742 守护葫芦藤：start_game 回包固定下发 165 格棋盘（KBHuluModel.as:95 的 for 循环上限），
// 结算时要把「打到了哪些格」按 {格号: 次数} 的 JSON 交回去。
constexpr int kHuluSquares = 165;

// 882 南瓜VS南瓜王：局内掉落表的消费口径。
//
// EScene.MONSTER_DIS_MAX = [[18,4,0],[10,16,3],[10,18,8]]（EScene.as:13）是三个阶段
// 每类怪的投放上限，每阶段三档之和恰好等于该阶段总怪数 TOTAL_MONSTER = [22,29,36]
// （EScene.as:9）；killMonster 累计到 getTotalMonsters(0) = 87 时 onReqEndGame(0) 通关
// （ActMainViewBase.as:230-233）。所以「打完一局」时三类怪的数量是确定的：
//   small 18+10+10 = 38、medium 4+16+18 = 38、large 0+3+8 = 11，合计 87。
// 客户端按类型各自独立消费（Ai_Monster.as:84-97 读 monsterKillList[mType] 后自增），
// 因此满通关一局 = 依次吃掉 monsterData[0][0..37]、[1][0..37]、[2][0..10]。
constexpr int kPumpkinMonsterTypes = 3;
constexpr int kPumpkinKillCap[kPumpkinMonsterTypes] = {38, 38, 11};
constexpr int kPumpkinBonusSlots = 12;   ///< ActivityData.gameGetBonusList 长度
constexpr int kPumpkinMedalAward = 23;   ///< award == 23 只进 gameMedal

// ---------------------------------------------------------------------------
// 每活动运行时状态
// ---------------------------------------------------------------------------
struct LatestActState {
    std::atomic<int> playCount{-1};  ///< 剩余次数；-1 未知
    std::atomic<int> restTime{0};    ///< 冷却剩余秒
    std::atomic<int> bestScore{0};   ///< 历史最好成绩（自动扫荡判定用）
    std::atomic<int> checkCode{0};   ///< start_game 下发的校验种子
    std::atomic<int> ruleFlag{0};    ///< start_game 需要的规则/提示参数
    std::atomic<bool> startPayloadReady{false}; ///< 721 的 start_game DTO 已完整消费
    std::atomic<bool> sweepReady{false};  ///< sweep_info 是否放行（有历史成绩）

    std::atomic<bool> waitingResponse{false};

    /// 最近一次 start_game 的 result（0 = 成功）。服务端拒绝开局时用它给出可读提示，
    /// 而不是静默判成失败。
    std::atomic<int> lastStartResult{-1};

    /// 最近一次 end_game 回包的结果码，以及这次结算是否被服务端接受。
    std::atomic<int> lastEndResult{-1};
    std::atomic<bool> endSettled{false};

    /// 721 专用：end_game 回包里服务端自己算出来的成绩与奖励。
    /// ActModel.decodeEndGame:154-161 读 resultType, playCount, frozenTime,
    /// ActEndGameDto{starCount, coin, lilian, 奖励对...}，ActEndView.as:57-59 直接把
    /// starCount 当本局得分显示。客户端上报多少星不等于服务端认多少星，
    /// 把这三个数读回来才能看出这一局到底算了几分——「奖励很少」就是从这里露出来的。
    std::atomic<int> endStarCount{-1};
    std::atomic<int> endCoin{-1};
    std::atomic<int> endLilian{-1};
    std::atomic<int> playProcessFailValue{-1}; ///< 721 game_event 失败的累计值，-2 表示取消
    std::atomic<int> playProcessSendError{0}; ///< 最近一次 game_event 发包失败的 Winsock 错误

    /// 721 专用：服务端对 game_event 的回送计数（操作串 game_event 的 back 包，
    /// :1764 早退点之前累计）。AS3 客户端同样不监听这个回包，服务端是否每事件
    /// 必答未知——等待方必须带超时兜底，不能裸等。
    std::atomic<uint32_t> playEventAcks{0};

    /// 最近一次 sweep 回包的结果码。0/4 是成功（4 = 满额通关），
    /// 3 = 校验失败，10 = 在游戏中，11 = 冷却中，12 = 次数不足。
    std::atomic<int> lastSweepResult{-1};

    /// 当前正在等待的回包操作（PendingOp）。响应处理器只在 operation 与它一致时
    /// 放行等待方，避免上一轮迟到的回包提前清掉 waitingResponse——那会让调用方
    /// 读到未更新的 checkCode/sweepReady，既可能误判扫荡放行，也可能误判执行失败。
    std::atomic<uint32_t> pendingOp{0};  ///< 0 = 未在等待

    /// 742 专用：start_game 回包下发的 165 格棋盘原始 award id
    /// （KBHuluModel.as:95-105）。本地对局就是按 levIndex 顺序把每一格记进
    /// rewardObj，结算时 JSON.stringify 回传。
    std::array<int, kHuluSquares> huluBoard{};
    /// 棋盘是否已完整收到。写棋盘在前、置位在后（release），读侧先读它再读棋盘，
    /// 保证响应线程写的 165 个 int 不会被一键线程读到半截。
    std::atomic<bool> huluBoardReady{false};

    /// 730 专用：start_game 下发的初始生命值（ActModel.decodeBeginGame 的 hp）。
    /// sub_hp 表示「角色挨了一下」，服务端回 leftLife；连发到 0 会把这一局打死，
    /// 所以次数按初始生命值封顶。
    std::atomic<int> startHp{0};

    /// 882 专用：start_game 下发的三组掉落表（monsterData）按上面的口径全量消费后的
    /// 累计结果。AS3 消耗的是列表本身，但结算包只回传累计后的 12 个计数器与勋章数
    /// （ActMainViewBase.as:183-184），所以这里直接存累计值，不必保留整张表。
    std::array<int, kPumpkinBonusSlots> pumpkinBonus{};
    std::atomic<int> pumpkinMedal{0};
    /// 三组列表是否都已完整读到。写累计值在前、置位在后（release），读侧先读它再读
    /// pumpkinBonus，保证结算线程不会拿到半份表算出来的计数。
    std::atomic<bool> pumpkinBonusReady{false};

    /// 占用标记：0 空闲，1 一键流程，2 次数探针。
    /// 二者共用 (ACTIVITY_QUERY_BACK, activityId) 路由到同一个响应处理器，而该处理器
    /// 会无条件清除 waitingResponse；若并发，探针的响应会提前清掉一键流程的等待标志，
    /// 令其误判收到响应。用 CAS 保证同一活动同一时刻只有一种流程在跑。
    std::atomic<int> opLock{0};
};

LatestActState g_state[kActivityNum];

/// 批量一键执行是否在进行：整批活动串行跑，同一时刻只允许一个 worker。
std::atomic<bool> g_latestBatchRunning{false};

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

// 读取 body 中的 UTF 字符串（长度 2 字节小端 + 内容）。与 hook_shared 的
// ReadPacketString 同一份协议，直接复用，避免第三份实现各自漂移。
bool ReadBodyString(const BYTE* body, size_t bodySize, size_t& offset, std::string& out) {
    return ReadPacketString(body, bodySize, offset, out);
}

// ---------------------------------------------------------------------------
// 发包
// ---------------------------------------------------------------------------
// 只发一次，不交给 SendPacket 去等回包。
//
// SendPacket 的 expectedOpcode 路径在等不到回包时会**自动重发同一个包**（最多 3 次）。
// 对 start_game 来说重发等于连开三局：服务端最后一次建局会把 checkCode 换掉，
// 本地手里那个就作废了，随后 end_game 的校验位算不对，服务端只扣次数不发奖
// （历史上那次 result=3 就是这个形状）。旧活动模块 activity_bus.cpp 的做法一直是
// 「发一次 + 自己等标志位」，这里对齐它：发一次，然后由 WaitForResponse 等处理器放行。
//
// 过程包（AS3 的 play_game / game_event）同样走这里：它们也是发完即走，
// 调用方本就不传 expectedOpcode，没有「等 / 不等」两种模式可分。
// ---------------------------------------------------------------------------
BOOL SendLatestPacket(uint32_t activityId, const std::string& operation,
                      const std::vector<int32_t>& bodyValues = {}) {
    return SendPacketBytes(BuildActivityPacket(Opcode::ACTIVITY_QINGYANG_NEW_SEND, activityId,
                                               operation, bodyValues));
}

// 上报每日任务进度。AS3 各活动在结算后用
// sendMessage(OP_CLIENT_DAILYTASK.send, 3, [3, 4039001, 0]) 上报，
// 注意其 opcode(1184812) 与 1184791 并非同一个协议。
void ReportDailyTask() {
    std::vector<BYTE> packet = PacketBuilder()
                                   .SetOpcode(Opcode::TASK_DAILY_REPORT_SEND)
                                   .SetParams(3)
                                   .WriteInt32(3)
                                   .WriteInt32(4039001)
                                   .WriteInt32(0)
                                   .Build();
    if (!packet.empty()) {
        SendPacketBytes(packet);
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
    // 收包线程把 waitingResponse 置 false 表示回包已到；超时/停止都返回 false。
    return WaitForCondition([&state]() { return !state.waitingResponse.load(); }, timeoutMs, 50) ==
           WaitOutcome::Signalled;
}

// 扫荡门槛——沿用仓库里周活动的统一标准，不要另立一套。
//
// 出处：ad5143f^:src/hook/activity_minigames.cpp:141
//   bool ShouldAutoSweep(int bestScore, int flowScore) {
//       if (bestScore <= 0) { return false; }
//       return bestScore >= flowScore;
//   }
// 全部 8 个旧周活动（822/826/827/641/684/631/808…）都是这一句判的：
// 「有成绩，且成绩不低于满分」才允许直接扫荡；否则必须先完整打一局。
//
// 这里的 flowScore 就是各活动的上限最高分（LatestActDesc::passScore）。
// bestScore <= 0（没成绩）与 bestScore < 满分（成绩不够）两种情况都要先打一局，
// 否则扫荡拿到的是按低分结算的奖励，白扣次数。
bool ShouldAutoSweep(int bestScore, int flowScore) {
    if (bestScore <= 0) {
        return false;
    }
    return bestScore >= flowScore;
}

// 发一次 sweep_info，等回包里的 sweepReady。它是「这次扫荡给什么奖」的预览包，
// 不扣次数；只有 742 那种进入包里没有判据的活动才把它当门槛（CanSweepDirectly ③）。
bool ProbeSweepReady(const LatestActDesc& desc, LatestActState& state);

// 重查一次活动信息（次数/冷却/成绩），定义在后面；扫荡重试要用它防重复扫荡。
bool RefreshOneActivity(const LatestActDesc& desc, LatestActState& state);

// 本活动当前是否可以「直接扫荡」——就是用户流程里的第一步判据。
//
// 判据只有一条，与旧周活动完全一致：ShouldAutoSweep(历史最好成绩, 上限最高分)。
// 唯一的分叉是「成绩从哪读」：
//   ① open_ui / game_info 回包里带成绩字段的活动（647/721/877/730/880）：
//      直接读 state.bestScore，不走网络。
//   ② 回包里只有「今天成功打完过一局」标志位的活动（882）：没有上限最高分可比，
//      门槛退化为「标志位 > 0」，同样直接读 state.bestScore，不走网络。
//   ③ 进入包只有次数与冷却的活动（742 守护葫芦藤）：本地无从判断，才问服务端。
//
// ②③ 的区别是这次修掉的 bug：882 的 open_ui 第 7 个 int 就是 bestFlag
// （ActModel.as:80/84），本来就在手里；此前把它当成「SWF 里没人读的死字段」，
// 于是 882 掉进 ③，开局先发一包 sweep_info。sweep_info 是「预览这次扫荡给什么奖」
// 的包，不是「有没有历史成绩」的查询，服务端在没有成绩时也会按 type 正常回包，
// 探针据此放行，紧接着就发真实 sweep，白扣一次次数——这正是「开局先扫荡一次、
// 发现没成绩、再正常打一局」的成因。
//
// 注意这里不看「本进程内刚打完一局」这种旁证：那是执行历史，与「服务端上有没有
// 满分成绩」是两件事。混用会让成绩没到上限的活动也直接扫，按低分发奖还白扣次数。
bool CanSweepDirectly(const LatestActDesc& desc, LatestActState& state) {
    if (desc.scoreInOpenUi) {
        if (desc.passScore > 0) {
            return ShouldAutoSweep(state.bestScore.load(), desc.passScore);
        }
        // 无上限最高分，但回包带了「今天成功打完过一局」标志（bestFlag）。
        // 语义与 730 ActivityData.bestFlag 一致：ActEnterView.as:153 用它决定
        // 放不放行扫荡，否则弹「需要成功进行一局游戏才可以扫荡哦！」。
        return state.bestScore.load() > 0;
    }
    // 进入包只有次数与冷却的活动（742 守护葫芦藤）：只能问服务端放不放行
    // （KBHuluModel.as:178-217 result==0）。这类活动没有成绩概念，探针不是判成绩，
    // 是判「服务端有没有这个活动的可扫状态」，与 882 的情形不同。
    return ProbeSweepReady(desc, state);
}

// 各活动通用的 checkCode 变换：checkCode & (uid % checkCode)
int MaskedCheckCode(int seed) {
    if (seed <= 0) {
        return 0;
    }
    const uint32_t userId = g_userId.load();
    return seed & static_cast<int>(userId % static_cast<uint32_t>(seed));
}

// 在 body 末尾补 n 个 0（877 的 awardNumList 初值都是全 0）。
void AppendZeros(std::vector<int32_t>& body, int count) {
    body.insert(body.end(), static_cast<std::size_t>(count), 0);
}

// ---------------------------------------------------------------------------
// open_ui / game_info 字段解析（各活动偏移不同，依据各自 Model.as）
// ---------------------------------------------------------------------------
void ParseOpenUI(const LatestActDesc& desc, LatestActState& state,
                 const GamePacket& packet, size_t offset) {
    const size_t size = packet.body.size();
    ByteReader reader(packet.body, offset);
    auto read = [&]() -> int { return reader.I32(); };

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

        case LatestAct::ACT_HULU:
            // restPlayCount, restCdTime, totalBadgeNum, catchId, catchList[2]
            // （KBHuluModel.as:71-85）
            if (offset + 8 > size) return;
            state.playCount = read();
            state.restTime = read();
            break;

        case LatestAct::ACT_STAR_PRINCESS:
            // ActBaseInfoDto.readFromStream: playCount, frozenTime, medalCount,
            // pop_win_flag, maxScore, hasList[3], ...
            if (offset + 20 > size) return;
            state.playCount = read();     // playCount
            state.restTime = read();      // frozenTime
            offset += 4;                  // medalCount
            state.ruleFlag = read();      // pop_win_flag
            state.bestScore = read();     // maxScore（实测是"今天成功打完过
                                          // 一局"的标志位：58 星一局后=1，
                                          // 再打 65 星仍=1；真客户端唯一
                                          // 用法 ActEnterView.as:119 >0 放行扫荡）
            break;

        case LatestAct::ACT_FIND_CORNEL:
            // playCount, restCdTime, buyCnt, medalNum, isPop, adaptMobile,
            // historyBestScore, catchId, catchList[2]
            // （ActModel.as:95-116）
            if (offset + 28 > size) return;
            state.playCount = read();     // playCount
            state.restTime = read();      // restCdTime
            offset += 4;                  // buyCnt
            offset += 4;                  // medalNum
            state.ruleFlag = read();      // isPop -> start_game 的 flag
            offset += 4;                  // adaptMobile
            state.bestScore = read();     // historyBestScore
            break;

        case LatestAct::ACT_FIGHT_EVILS:
            // result, playCount, time, isShow, bestFlag
            // （ActModel.as:91-107）
            if (offset + 4 > size) return;
            if (read() != 0) {
                state.playCount = 0;
                state.bestScore = 0;
                return;
            }
            if (offset + 16 > size) return;
            state.playCount = read();     // playCount
            state.restTime = read();      // time（冷却秒）
            state.ruleFlag = read();      // isShow
            state.bestScore = read();     // bestFlag
            break;

        case LatestAct::ACT_TREASURE:
            // restPlayCount, restCdTime, <ignored>, totalBadgeNum, promptFlag,
            // isMoveControl, bestRecord, catch_list[{id, limit_list[2]}]
            // （TreasureModel.as:40-57）
            if (offset + 28 > size) return;
            state.playCount = read();     // restPlayCount
            state.restTime = read();      // restCdTime
            offset += 8;                  // ignored, totalBadgeNum
            state.ruleFlag = read();      // promptFlag -> start_game 参数
            offset += 4;                  // isMoveControl
            state.bestScore = read();     // bestRecord
            break;

        case LatestAct::ACT_PUMPKINS_PK:
            // count, time, payCnt, wuzi, isShow, flag, bestFlag
            // （ActModel.decodeBasicInfo:72-94）
            if (offset + 28 > size) return;
            state.playCount = read();     // count（剩余次数）
            state.restTime = read();      // time（冷却秒）
            offset += 4;                  // payCnt（今日已买次数）
            state.ruleFlag = read();      // wuzi（勋章数，本流程不消费，留档）
            offset += 8;                  // isShow, flag
            // bestFlag：882 自己的 SWF 没读它（门禁压在服务端），但语义由 730 的同名
            // 字段确证——730 ActEnterView.as:151-165 就是 `if (bestFlag > 0)` 才开扫荡窗，
            // 否则弹「需要成功进行一局游戏才可以扫荡哦！」。所以这里照收，用作门槛。
            state.bestScore = read();     // bestFlag（今天成功打完过一局 > 0）
            break;

        default:
            break;
    }
}

// 882 的掉落表 → 结算计数器。
//
// AS3 的奖励不是「按类型给固定奖」，而是服务端每局下发一张怪物掉落表，客户端在怪被
// 打死时按产生顺序取下一个 id 消费（Ai_Monster.as:83-97）：
//   mType = EType.refleckMonsterType(type) - 1     -> monsterData 的下标
//   award = monsterData[mType][monsterKillList[mType]++]   -> 超出表长时数组取值即 undefined
//   award < 13  -> ++gameGetBonusList[award - 1]（award <= 0 时下标为负，不影响 12 个槽）
//   award == 23 -> ++gameMedal
//   award 13..22-> 客户端丢弃，不进任何计数器
// 一局最多打死 small 38 / medium 38 / large 11（EScene.MONSTER_DIS_MAX 三档之和恰好等于
// TOTAL_MONSTER = [22,29,36]，ActMainView.as:353 每刷一只 --_totalMonster，杀满 87 只触发
// ActMainViewBase.onReqEndGame(0) 通关）。本地无法真的打怪，就把整张表按同样口径一次性
// 消费完——这正是「满通关那一局」的确定结果，与服务端按计数发奖的口径一致。
void ConsumePumpkinMonsterData(LatestActState& state, const BYTE* body, size_t size,
                               size_t offset) {
    state.pumpkinBonusReady = false;
    state.pumpkinBonus.fill(0);
    state.pumpkinMedal = 0;
    ByteReader reader(body, size, offset);
    if (offset + 4 > size) {
        return;
    }
    offset += 4;   // gameBonus：读进 ActivityData.gameBonus，全 SWF 无读取点，丢弃
    for (int type = 0; type < kPumpkinMonsterTypes; ++type) {
        if (offset + 4 > size) {
            return;
        }
        const int len = reader.I32();
        if (len < 0 || offset + static_cast<size_t>(len) * 4 > size) {
            return;
        }
        // AS3 只会消费到第 kills 项，多给的尾巴永远用不上；
        // 表比理论值短就以表长为准（服务端给少了不该越界）。
        const int usable = (len < kPumpkinKillCap[type]) ? len : kPumpkinKillCap[type];
        size_t cursor = offset;   // I32() 会推进游标，尾部未消费的项靠 offset 整体跳过
        ByteReader cursorReader(body, size, cursor);
        for (int i = 0; i < usable; ++i) {
            const int award = cursorReader.I32();
            if (award >= 1 && award < 1 + kPumpkinBonusSlots) {
                ++state.pumpkinBonus[static_cast<std::size_t>(award - 1)];
            }
            if (award == kPumpkinMedalAward) {
                ++state.pumpkinMedal;
            }
        }
        offset += static_cast<size_t>(len) * 4;
    }
    state.pumpkinBonusReady = true;
}

// start_game 响应：result, <丢弃字段>, checkCode
//
// 除 721 外，本模块所有活动的回包都是「先 result、再一个被丢弃的字段、然后才是
// checkCode」，已逐个用 FFDec 反编译核对：
//   647 MazeModel.START_GAME          : result, readInt(), checkCode
//   877 ActModel.as:118-146           : result, playCount, checkcode, num, rewardList[num]
//   730 ActModel.as:114-134           : result, playCount, check_code, hp, ActAwardDto
//   880 TreasureModel.as:58-78        : result, readInt(), checkCode
//   882 ActModel.as:114-118           : result, playCount, check_code, gameBonus, ...
//   721 ActModel.decodeBeginGame:127  : result, ActStartInfoDto{checkCode, ...}  <- 无丢弃字段
// 该字段不能被当作 checkCode 使用；漏跳会让 checkCode 整体前移 4 字节，
// end_game 的校验式全错，服务端只扣次数不下发奖励。
void ParseStartGame(const LatestActDesc& desc, LatestActState& state,
                    const GamePacket& packet, size_t offset) {
    const BYTE* body = packet.body.data();
    const size_t size = packet.body.size();
    ByteReader reader(packet.body, offset);
    if (desc.id == LatestAct::ACT_STAR_PRINCESS) {
        state.startPayloadReady = false;
    }
    if (offset + 4 > size) return;
    const int result = reader.I32();
    state.lastStartResult = result;
    if (result != 0) return;                      // result != 0 视为开局失败
    // 注意：reader.I32() 内部已经推进 offset，这里不能再无条件补一次。
    // 此前这里多写了一句 offset += 4，导致非 721 活动的 checkCode 整体后移
    // 4 字节（读到的是紧跟着的那个字段，结算校验位算错 → 服务端判失败），
    // 而 721 因为没有丢弃字段直接越界返回，checkCode 永远是 0 → 判定开局失败、
    // 整局被跳过直接去扫荡。丢弃字段只在下面各分支里跳，且只跳一次。

    // 742 的 start_game 回包没有 checkCode：result, <丢弃>, 165 格棋盘
    // （KBHuluModel.as:86-111）。它的结算走 OP_CLIENT_LUA_ACTIVITY_V3 + JSON，
    // 不需要校验种子，因此这里直接把棋盘读进来就返回。
    if (desc.id == LatestAct::ACT_HULU) {
        offset += 4;                              // 丢弃字段
        state.huluBoardReady = false;
        state.huluBoard.fill(0);
        int squares = 0;
        for (; squares < kHuluSquares && offset + 4 <= size; ++squares) {
            state.huluBoard[static_cast<std::size_t>(squares)] = reader.I32();
        }
        state.huluBoardReady = (squares == kHuluSquares);
        return;
    }

    // 721 的 start_game 回包在 result 之后直接就是 ActStartInfoDto.checkCode，
    // 中间没有其他活动那个被丢弃的字段（ActStartInfoDto.readFromStream:19）。
    if (desc.id != LatestAct::ACT_STAR_PRINCESS) {
        offset += 4;                              // 丢弃字段（各活动含义不同）
    }
    if (offset + 4 > size) return;
    state.checkCode = reader.I32();

    // 730 在 checkCode 之后紧跟 hp（ActModel.decodeBeginGame:114-134）。
    if (desc.id == LatestAct::ACT_FIGHT_EVILS && offset + 4 <= size) {
        state.startHp = reader.I32();
    }

    // 882 在 checkCode 之后是 gameBonus + 三组掉落表（ActModel.as:114-143）。
    // 掉落表决定结算能报出多少奖励计数，读到才算开局成功。
    if (desc.id == LatestAct::ACT_PUMPKINS_PK) {
        ConsumePumpkinMonsterData(state, body, size, offset);
    }

    // 721 实测（parse721_debug.log）：服务端 start_game 回包到 checkCode 就结束
    // （body 共 20 字节：操作串 + result + checkCode），不下发 awardList。
    // AS3 的 ActStartInfoDto.readFromStream 此处会读越界抛异常，但被框架吞掉——
    // awardList 在 721 无消费者、checkCode 也是死代码，真客户端无感。这里同理：
    // 开局成功 = result==0 且 checkCode 读到；awardList 有就尽力消费（DTO 尽量
    // 读到底），没有不阻塞开局。
    if (desc.id == LatestAct::ACT_STAR_PRINCESS) {
        state.startPayloadReady = true;
        if (offset + 4 > size) {
            return;
        }
        const int awardNum = reader.I32();
        if (awardNum < 0 || static_cast<size_t>(awardNum) > (size - offset) / 8) {
            return;
        }
        for (int i = 0; i < awardNum; ++i) {
            reader.I32();  // awardIdx
            reader.I32();  // awardNum
        }
    }
}

// 721 ActEndGameDto.as:21-38：三项计数后，读取至多十组奖励对。
// 先解析到局部变量，避免截断回包将半份结果发布给运行线程。
bool ReadStarPrincessEndDto(const BYTE* body, size_t size, size_t& offset,
                           int& starCount, int& coin, int& lilian) {
    ByteReader reader(body, size, offset);
    if (offset > size || size - offset < 12) {
        return false;
    }
    starCount = reader.I32();
    coin = reader.I32();
    lilian = reader.I32();
    constexpr int kRewardTypeCount = 10;  // EIdReflect.REFLECT_STRUCT.length
    for (int i = 0; i < kRewardTypeCount; ++i) {
        if (size - offset < 8) {
            // 实测（parse721_debug.log 2026-10-24）：sweep_info 回包在三个计数
            // 之后只剩一个 4 字节的 0——awardIdx=0 的终止符，awardNum 没有跟着
            // 下发（接收端尾部还截了几字节）。AS3 在这里读越界抛异常被框架吞
            // 掉，但它的奖励表本就由三计数现场构造（ActEndGameDto.as:26），展示
            // 不受影响；本模块也只消费三计数。所以剩余 4 字节为 <=0 的终止符、
            // 或尾部干净截断（0 字节），都按正常收尾放行——三计数才是结算判据。
            return size - offset == 0 ||
                   (size - offset == 4 && reader.I32() <= 0);
        }
        const int awardIdx = reader.I32();
        reader.I32();  // awardNum
        if (awardIdx <= 0) {
            return true;
        }
        if (awardIdx >= kRewardTypeCount) {
            return false;
        }
    }
    return true;
}

// sweep_info 响应：判断是否放行扫荡（各活动首个语义字段不同）。
void ParseSweepInfo(const LatestActDesc& desc, LatestActState& state,
                    const GamePacket& packet, size_t offset) {
    const BYTE* body = packet.body.data();
    const size_t size = packet.body.size();
    ByteReader reader(packet.body, offset);
    if (offset + 4 > size) return;

    const int first = reader.I32();
    switch (desc.id) {
        case LatestAct::ACT_FIGHT_EVILS:
            // 730 ActModel.decodeSweepInfo:186-213 没有 result 字段，首字段直接是
            // medal，AS3 收到就开扫荡窗口。这里同样无条件放行，成败交给 sweep 回包判定。
            state.sweepReady = true;
            break;
        case LatestAct::ACT_HULU:
            // 742 KBHuluModel.as:178-217 result = readInt(); if (result == 0)。
            state.sweepReady = (first == 0);
            break;
        case LatestAct::ACT_FIND_CORNEL:
            // 877 ActModel.as:292-321 result == 0 才读奖励。
            state.sweepReady = (first == 0);
            break;
        case LatestAct::ACT_STAR_PRINCESS: {
            // ActModel.decodeSeepInfo:287-291 不判断 resultType；
            // 必须像 AS3 一样读完奖励 DTO，不能只收一个结果码就放行。
            int starCount = 0;
            int coin = 0;
            int lilian = 0;
            state.sweepReady = ReadStarPrincessEndDto(body, size, offset,
                                                      starCount, coin, lilian);
            break;
        }
        case LatestAct::ACT_PUMPKINS_PK:
            // 882 ActModel.decodeSweepInfo:195-227：type == 0 || type == 4 才读奖励并
            // 派发 SWEEP_INFO，其余（1 = 还没成功打完一局）只弹提示。
            state.sweepReady = (first == 0 || first == 4);
            break;
        case LatestAct::ACT_MAZE:
        case LatestAct::ACT_TREASURE:
        default:
            // 647 MazeModel.as:257-258、880 TreasureModel.as:307-356：result != 3。
            state.sweepReady = (first != 3);
            break;
    }
}

// sweep 响应：确认服务端确实完成扫荡（否则退回游戏流程）。
void ParseSweepResult(const LatestActDesc& desc, LatestActState& state,
                      const GamePacket& packet, size_t offset) {
    const BYTE* body = packet.body.data();
    const size_t size = packet.body.size();
    ByteReader reader(packet.body, offset);
    if (offset + 4 > size) {
        state.sweepReady = false;
        return;
    }

    const int result = reader.I32();
    state.lastSweepResult = result;
    switch (desc.id) {
        case LatestAct::ACT_HULU:
            // 742 KBHuluModel.as:129-177 if (result == 0 || result == 4) 才读奖励。
            state.sweepReady = (result == 0 || result == 4);
            break;
        case LatestAct::ACT_STAR_PRINCESS: {
            // ActModel.decodeSeep:294-300 与 end_game 的字段布局相同。
            // 实际模型不判断 resultType；只有完整结算才算扫荡被接受，
            // 旧错误码枚举不能替代 DTO，单独的错误码也不能视作成功。
            state.sweepReady = false;
            if (size - offset < 8) {
                break;
            }
            offset += 8;  // playCount, frozenTime；流程随后通过 open_ui 回查
            int starCount = 0;
            int coin = 0;
            int lilian = 0;
            state.sweepReady = ReadStarPrincessEndDto(body, size, offset,
                                                      starCount, coin, lilian);
            break;
        }
        case LatestAct::ACT_PUMPKINS_PK:
            // 882 ActModel.decodeSweepInfo:195-227 是 SWF 里唯一判断扫荡结果的地方：
            // 只有 type == 0 || type == 4 才读奖励，其余只弹提示、等于没扫。
            // 它没有像 647/880 那样用「!= 3」的宽松判据，这里也不能放宽——
            // 否则 1（还没成功打完一局）/2（次数不足）/3（冷却中）都会被当成扫成功。
            state.sweepReady = (result == 0 || result == 4);
            break;
        case LatestAct::ACT_MAZE:
        case LatestAct::ACT_FIND_CORNEL:
        case LatestAct::ACT_FIGHT_EVILS:
        case LatestAct::ACT_TREASURE:
        default:
            // 与旧周活动统一标准逐字一致：扫荡只看 result != 3
            // （ad5143f^:src/hook/activity_minigames.cpp:242
            //  `if (waitForResponse() && ACT685_STATE.endResult.load() != 3)`）。
            // AS3 各 Model 也都是 `if (params.result != 3)` 才读奖励：
            // MazeModel.as:155、ActModel.as:294-347（721/877）、
            // ActModel.as:215-240（730）、TreasureModel.as:79-136（880）。
            // 注意 11（冷却中）、12（次数不足）也会被这条宽松判据当成成功——
            // 所以调用方必须在扫荡前把冷却等干净（WaitForCooldown），
            // 不能靠结果码去兜。
            state.sweepReady = (result != 3);
            break;
    }
}

// end_game 响应：确认这一局真的被服务端结算了，而不是「包发出去了」就算完。
// 各活动首个字段都是结果码（名称不同：result / resultType），但语义不完全一样：
//   647 MazeModel.as:151-168、880 TreasureModel.as:137-146：result != 3 才读结算字段
//   （3 = 校验失败/未通过）；882 ActModel.as:161-193 同样只有 type == 3 是数据异常。
//   730 ActModel.decodeEndGame:142-167：resultType == 4 表示通关（isPass），其余仍会结算。
//   721/877 的 AS3 不判结果码，直接读结算字段。
// 因此这里只把「明确的失败码」判成失败：647/880/882 的 3。
void ParseEndGame(const LatestActDesc& desc, LatestActState& state,
                  const GamePacket& packet, size_t offset) {
    const BYTE* body = packet.body.data();
    const size_t size = packet.body.size();
    ByteReader reader(packet.body, offset);
    if (offset + 4 > size) {
        state.lastEndResult = -1;
        state.endSettled = false;
        return;
    }
    const int result = reader.I32();
    state.lastEndResult = result;
    state.endStarCount = -1;
    state.endCoin = -1;
    state.endLilian = -1;
    switch (desc.id) {
        case LatestAct::ACT_MAZE:
        case LatestAct::ACT_TREASURE:
        case LatestAct::ACT_PUMPKINS_PK:
            state.endSettled = (result != 3);
            break;
        case LatestAct::ACT_HULU:
            // 742 KBHuluModel.as:129-177：只有 result == 0 || result == 4 才读奖励并派发
            // GAME_END_BACK，其余分支只是补发一次 open_ui，等于这一局没结算。
            state.endSettled = (result == 0 || result == 4);
            break;
        case LatestAct::ACT_STAR_PRINCESS: {
            // ActModel.decodeEndGame:154-160：resultType, playCount, frozenTime, DTO。
            state.endSettled = false;
            if (size - offset < 8) {
                break;
            }
            offset += 8;  // playCount, frozenTime；流程随后通过 open_ui 回查
            int starCount = 0;
            int coin = 0;
            int lilian = 0;
            if (ReadStarPrincessEndDto(body, size, offset, starCount, coin, lilian)) {
                state.endStarCount = starCount;
                state.endCoin = coin;
                state.endLilian = lilian;
                state.endSettled = true;
            }
            break;
        }
        default:
            // 730 的 resultType 只是「是否通关」，不是「是否结算」，非 4 也可能正常发奖；
            // 877 的 AS3 完全不看这个字段。收到回包即视为已结算。
            state.endSettled = true;
            break;
    }
}

// ---------------------------------------------------------------------------
// end_game 参数构造（各活动 body 不同，依据各自发送处）
//
// seed 由调用方传入而不是直接读 state.checkCode：关闭「上一局残留会话」时必须
// 强制用 0（见 CloseStaleSession 的说明）。
// state 同理：关残留会话时传 nullptr，一切回传值按「空局」计（882 的奖励计数为 0）；
// 正常结算时传当前状态，882 才能把 start_game 掉落的整张表按满通关口径报上去。
// ---------------------------------------------------------------------------
std::vector<int32_t> BuildEndGameBody(const LatestActDesc& desc, int seed,
                                      const LatestActState* state) {
    const uint32_t userId = g_userId.load();
    const int score = desc.targetScore;

    switch (desc.id) {
        case LatestAct::ACT_MAZE:
            // [end_game, score, checkCode, isPass]
            return {score, MaskedCheckCode(seed), 1};

        case LatestAct::ACT_HULU:
            // 742 的结算包不是 int32 列表，而是一段 JSON 字符串，走
            // OP_CLIENT_LUA_ACTIVITY_V3（见 SendHuluEndGame）。这里只在
            // CloseStaleSession 里被当作「关会话用的空结算」使用。
            return {};

        case LatestAct::ACT_STAR_PRINCESS:
            // [end_game] —— 721 的结算包不带任何参数
            // （ActMainView.as:284 与 :416 都只发操作串）。星级完全靠游戏内
            // game_event 累计，见 TryPlayOnce 里逐包发送的累计值序列。
            return {};

        case LatestAct::ACT_FIND_CORNEL: {
            // [end_game, checkCode, 茱萸数, 16 个奖励计数]
            // ActivityGame.onReqEndGame:220-238：
            //   checkCode = DataCenter.checkCode & (uid % DataCenter.checkCode)
            //   arr = [END_GAME, checkCode, _zuyuNum]，再补 EScene.AWARD_LIST.length - 4 = 16 个
            std::vector<int32_t> body{MaskedCheckCode(seed), score};
            AppendZeros(body, kFindCornelAwardSlots);
            return body;
        }

        case LatestAct::ACT_FIGHT_EVILS:
            // [end_game, end_type, check_num, score]
            // ActMainView.as:450-457（计时满 120 秒）：
            //   end_type = 4; check_num = uid % 1000 + check_code + score + end_type
            return {4, static_cast<int>(userId % 1000u) + seed + score + 4, score};

        case LatestAct::ACT_TREASURE:
            // [end_game, code, goldNum, result]
            // GameControl.as:43-48：code = checkCode & (uid % checkCode)，
            // result 由 GameUI.endGame 给出：1 = 时间到，2 = 提前结束。
            // 完整打完一局是「90 秒走完」，也就是 result = 1
            // （GameUI.as:412-421 endGame: result = gameTime == 0 ? 1 : 2）。
            // 送 2 等于自报「提前结束」，与「完整跑完」的语义相反。
            return {MaskedCheckCode(seed), score, 1};

        case LatestAct::ACT_PUMPKINS_PK: {
            // [end_game, checkCode, medal, isWin, 12 个奖励计数]
            // ActMainViewBase.onReqEndGame:177-187：
            //   checkCode = ActivityData.checkcode & (uid % ActivityData.checkcode)
            //   arr = [END_GAME, checkCode, ActivityData.gameMedal, lose ? 0 : 1]
            //   newArr = arr.concat(ActivityData.gameGetBonusList)   // 12 个 0
            // lose = 0 表示正常通关（ActMainViewBase.as:218-234 杀满 87 只怪时调
            // onReqEndGame(0)），与 730/880 里 1 = 成功的方向相反。
            // gameMedal 只在 award == 23 时自增（Ai_Monster.as:82-97）。
            // 奖励按客户端报的这 12 个计数 + 勋章数发：报全 0 等于自报「通关但一只怪
            // 没打」，只能拿保底。start_game 下发了整张掉落表，本地按满通关口径把它
            // 一次消费完（ConsumePumpkinMonsterData），这里原样报上去。
            // state 为空（关残留会话）或掉落表没读全时，退回「0 勋章 + 12 个 0」，
            // 那是 AS3 允许的最小合法值，不会让结算被拒。
            std::vector<int32_t> body{MaskedCheckCode(seed)};
            if (state != nullptr && state->pumpkinBonusReady.load()) {
                body.push_back(state->pumpkinMedal.load());        // gameMedal
                body.push_back(1);                                 // isWin：1 = 通关
                body.insert(body.end(), state->pumpkinBonus.begin(), state->pumpkinBonus.end());
            } else {
                body.push_back(0);
                body.push_back(1);
                AppendZeros(body, kPumpkinBonusSlots);             // gameGetBonusList 全 0
            }
            return body;
        }

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
        case LatestAct::ACT_STAR_PRINCESS:
            return {1};                         // [start_game, 1]
        case LatestAct::ACT_FIND_CORNEL:
            // [start_game, flag]，ActivityGame.as:210-217：flag = (isPop == 1) ? 1 : 0
            return {state.ruleFlag.load() == 1 ? 1 : 0};
        case LatestAct::ACT_FIGHT_EVILS:
            return {1};                         // [start_game, 1]
        case LatestAct::ACT_TREASURE:
            // [start_game, promptFlag]，EntryControl.as:83-86
            return {state.ruleFlag.load()};
        case LatestAct::ACT_PUMPKINS_PK:
            // [start_game, status]，ActMainViewBase.as:171-175：
            //   status = mcCheckBox.currentFrame == 1 ? 1 : 0
            // mcCheckBox 是「是否使用道具/加成」的勾选框，initView:143 固定
            // gotoAndStop(2)，即默认不勾。恒送 0 与真实客户端默认行为一致。
            return {0};
        default:
            return {};
    }
}

// start_game 被拒时的可读原因。
// 各活动的拒绝码不是同一套，只能逐活动翻译已核对过的部分：
//   721 ActModel.decodeBeginGame:127-148  → 1 次数用光 / 2 冷却中 / 3 还在游戏中
//   730 Act730ErrorCode.errorCodeStartGame → 10 游戏中 / 11 在冷却 / 12 没次数了 / 13 支付中
// 其余活动没有可引用的枚举，原样回显数字，别把猜测写成结论。
std::wstring StartFailureText(const LatestActDesc& desc, int result) {
    if (desc.id == LatestAct::ACT_FIGHT_EVILS) {
        switch (result) {
            case 10: return L"result=10 游戏中";
            case 11: return L"result=11 在冷却";
            case 12: return L"result=12 没次数了";
            case 13: return L"result=13 支付中";
            default: break;
        }
    } else if (desc.id == LatestAct::ACT_STAR_PRINCESS) {
        switch (result) {
            case 1: return L"result=1 次数已用光";
            case 2: return L"result=2 冷却中";
            case 3: return L"result=3 还在游戏中";
            default: break;
        }
    } else if (desc.id == LatestAct::ACT_PUMPKINS_PK) {
        // Act882ErrorCode.errorCodeStartGame:13-40
        switch (result) {
            case 10: return L"result=10 还在游戏中";
            case 11: return L"result=11 还在冷却中";
            case 12: return L"result=12 今天已经没次数了";
            case 13: return L"result=13 支付中";
            default: break;
        }
    }
    return L"result=" + std::to_wstring(result);
}

// ---------------------------------------------------------------------------
// 742 守护葫芦藤的结算包
//
// AS3 的发送处是 control/KBHuluSendAction.as:57-62：
//   jsonStr = JSON.stringify(KBDataCenter.instance.rewardObj);
//   sendMessage(OP_CLIENT_LUA_ACTIVITY_V3.send, 742, [END_GAME, jsonStr]);
// rewardObj 的键来自 toSend3（:41-55）：awardIdx = int(event.data) + 1，命中一格就
// ++rewardObj[awardIdx]；而 createInsect（KBBatttleView.as:485-514）给每只虫子的
// awardId 是 awardArr[levIndex++] - 1，所以键就是棋盘上的原始 award id。
// dx < 0（即棋盘格为 0）的格子 AS3 直接 return，不计入。
// 回包仍然从 ACTIVITY_QINGYANG_NEW.back(1316501) 回来——KBHuluModel.as:36 只监听这一条，
// 与坐骑大赛(665) 走 HORSE_GAME_CMD_SEND 却在 HORSE_COMPETITION_BACK 收包是同一形状。
// ---------------------------------------------------------------------------
std::string BuildHuluRewardJson(const LatestActState& state) {
    std::map<int, int> counts;
    for (int i = 0; i < kHuluSquares; ++i) {
        const int raw = state.huluBoard[static_cast<std::size_t>(i)];
        if (raw < 1) {
            continue;
        }
        ++counts[raw];
    }

    std::string json = "{";
    bool first = true;
    for (const auto& entry : counts) {
        if (!first) {
            json += ',';
        }
        json += '"';
        json += std::to_string(entry.first);
        json += "\":";
        json += std::to_string(entry.second);
        first = false;
    }
    json += '}';
    return json;
}

BOOL SendHuluEndGame(uint32_t activityId, const std::string& json) {
    // body = [u16 长度 + "end_game"][u16 长度 + JSON]，与 CmdPacket.build 对 String
    // 的处理一致（org/green/server/data/CmdPacket.as:22-65，String 走 writeUTF）。
    std::vector<BYTE> body;
    auto appendString = [&body](const std::string& text) {
        const uint16_t len = static_cast<uint16_t>(text.size());
        body.push_back(static_cast<uint8_t>(len & 0xFF));
        body.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        body.insert(body.end(), text.begin(), text.end());
    };
    appendString("end_game");
    appendString(json);

    std::vector<BYTE> packet = PacketBuilder()
                                   .SetOpcode(Opcode::ACTIVITY_LUA_V3_SEND)
                                   .SetParams(activityId)
                                   .WriteBytes(body)
                                   .Build();
    if (packet.empty()) {
        return FALSE;
    }
    return SendPacketBytes(packet);
}

// ---------------------------------------------------------------------------
// 一局游戏的过程包
//
// 只有真正需要在游戏过程中上报的活动才有内容；其余活动的过程全在本地（分数、
// 金币、星级都在客户端累计，结算时一次性回传），AS3 本身就不发中间包，这里也不该
// 凭空造包。凡是 AS3 有过程包的，必须逐条补上——start_game 直接接 end_game 等于
// 跳过整局，服务端算出来的成绩是空的。
// ---------------------------------------------------------------------------
bool RunPlayProcess(const LatestActDesc& desc, LatestActState& state) {
    auto running = []() { return g_latestBatchRunning.load(); };

    switch (desc.id) {
        case LatestAct::ACT_STAR_PRINCESS: {
            // 721 采菊东篱下：过程包必须复现 AS3 真实一局的三类特征。
            // 此前按「120ms × 400 包」的最小路径 48 秒打完一局且从不发飞星事件，
            // 与真实客户端的行为画像对不上（时长只有 GAME_TIME 的一半多、事件频率
            // 是真客户端的 8 倍、满 400 分里最多 270 分来自飞星却一条标记包没有），
            // 服务端核账不过，这一局就废了。逐条对照 AS3：
            //
            //   ① 普通星：ActMainView.as:556-573  每次跳跃落地发一次【累计】分
            //      _starCount（一跳捡 1~4 颗，不是每颗一包）；
            //   ② 飞星：ActMainView.onTimer:408   每 10 秒出现一颗
            //      （_starTimer % 10 == 0），checkCollesion:611-629 接住后
            //      _starCount += 30 并发 _starCount + 10000 标记包；
            //   ③ 时长：ActMainViewBase.as:21     GAME_TIME = 90，
            //      onTimer:416 计时走完才发 end_game，整局必须撑满 90 秒。
            //
            // start_game 的 ActStartInfoDto.awardList 在 721 客户端没有任何
            // 消费者（全仓库 grep 无读取点），不是计分脚本，不能拿它当账本。
            //
            // 重放节奏（用户指定）：发一条 game_event → 等服务端回包（1s
            // 兜底；实测该 op 无回音，等效 ~1.3s 固定间隔）→ 300ms 后发下
            // 一条。事件序列不变：10 的
            // 倍数秒（t=10..80，共 8 颗）发飞星 +30 标记包；奇数秒发普通星
            // 累计包（+4/+3 交替，收官 5 跳 +4）。末条恰好累计到满分 400，
            // 交 end_game。playEventAcks 由回包处理器对 game_event 回音计数。
            // 历史：200ms 固定间隔实测服务端只认 58/65 星（节流）；更早的
            // 等回包版"只记 1 分"是误读——1 是 open_ui 的 maxScore（成功
            // 标志位），当时 DTO 解析失败没读到真分数。
            constexpr int kGameDurationSec = 90;    // ActMainViewBase.GAME_TIME
            constexpr int kFlyStarPeriodSec = 10;   // ActMainView.onTimer 飞星周期
            constexpr int kFlyStarBonus = 30;       // ActMainView.as:622
            constexpr int kMaxScore = 400;          // EDataType.MAX_SCORE

            state.playProcessFailValue = -1;
            state.playProcessSendError = 0;

            constexpr int kAckWaitMs = 1000;   // 兜底：回包超时也继续（实测
                                               // game_event 无回音，等效固定间隔）
            constexpr int kInterEventMs = 300; // 回包后再等 300ms 发下一条
            const auto waitAckThenGap = [&]() {
                const uint32_t expect =
                    state.playEventAcks.load(std::memory_order_relaxed) + 1;
                // 回音是"锦上添花"：等到就提前走，等不到就按固定间隔继续，
                // 所以这里只取超时/停止的结果，不区分两者。
                WaitForCondition(
                    [&state, expect]() {
                        return state.playEventAcks.load(std::memory_order_relaxed) >= expect;
                    },
                    kAckWaitMs,
                    10);
                ManagedThread::SleepCancellable(kInterEventMs);
            };

            int starCount = 0;
            int regularSlot = 0;   // 奇数秒跳跃事件序号（共 45 个）
            for (int sec = 1; sec <= kGameDurationSec; ++sec) {
                if (!running() || ManagedThread::StopRequested()) {
                    state.playProcessFailValue = -2;
                    return false;
                }
                if (sec % kFlyStarPeriodSec == 0 && sec < kGameDurationSec) {
                    starCount += kFlyStarBonus;
                    if (!SendLatestPacket(desc.id, "game_event",
                                                {starCount + 10000})) {
                        state.playProcessFailValue = starCount;
                        state.playProcessSendError = WSAGetLastError();
                        return false;
                    }
                    waitAckThenGap();
                } else if (sec % 2 == 1) {
                    // +4/+3 交替凑满 160 颗普通星（45 跳 = 25×4 + 20×3），
                    // 与飞星 240 合计恰为满分 400，收官一跳正好落在 400。
                    const int inc = (regularSlot < 40) ? ((regularSlot % 2 == 0) ? 4 : 3) : 4;
                    ++regularSlot;
                    starCount += inc;
                    if (!SendLatestPacket(desc.id, "game_event", {starCount})) {
                        state.playProcessFailValue = starCount;
                        state.playProcessSendError = WSAGetLastError();
                        return false;
                    }
                    waitAckThenGap();
                }
            }
            // 全程单调、不越过满分（飞星后最高 380+10000，最后一条是干净的 400）。
            return starCount == kMaxScore;
        }

        case LatestAct::ACT_FIGHT_EVILS: {
            // 730 智斗恶灵：过程中唯一的包是 [sub_hp]（无参）。
            //   main/Role.as:160-176        角色撞到怪时 item.beHit(2) 并发 [SUB_HP]
            //   main/ai/Ai_Monster.as:83-90 怪跑到 x<0 逃走时发 [SUB_HP]
            // 服务端回 leftLife（ActModel.decodeBeHurt:136-140），打到 0 这一局就结束，
            // 因此次数按 start_game 下发的 hp 封顶，留一条命收尾。
            const int hp = state.startHp.load();
            int hits = (hp > 1 && hp <= 200) ? hp - 1 : 0;
            if (hits > 30) {
                hits = 30;   // 血条走到底即可；end_type=4（时间到）本身与剩余生命无关
            }
            for (int i = 0; i < hits; ++i) {
                if (!running()) {
                    return false;
                }
                if (!SendLatestPacket(desc.id, "sub_hp")) {
                    return false;
                }
                if (!ManagedThread::SleepCancellable(200)) {
                    return false;
                }
            }
            return true;
        }

        default:
            // 647/877/880/742/882：AS3 整局都不发中间包，分数与奖励全在本地累计，
            // 结算时一次性回传（880 的金币、742 的 rewardObj、882 的 gameGetBonusList…）。
            return true;
    }
}

// 关掉「上一局残留会话」。
//
// 服务端对每个活动只允许同时存在一局。只要有一局 start_game 之后没收到合法
// end_game（进程被杀、结算包被拒、断线），会话就永远停在「在游戏中」，
// 之后每次 start_game 都只会回「游戏中」，活动再也打不开——表现就是
// 「这几个活动没一个能用」。
//
// AS3 客户端重开页面时也是这么自救的：ActivityData.checkCode 还是初值 0，
// 于是 end_game 的校验位带 0（730 ActMainViewBase.onReqEndGame:175-178、
// 721 ActMainView.onReqEndGame:284/:416 都只发操作串），服务端据此把上一局
// 判成异常结束并释放会话。这里照抄这个行为：seed 强制 0。
bool CloseStaleSession(const LatestActDesc& desc, LatestActState& state) {
    bool answered = false;
    {
        PendingOpGuard guard(state, PendingOp::EndGame);
        // 742 的结算包是 JSON 字符串（LUA_ACTIVITY_V3），关会话时交一个空对局即可。
        const bool sent = (desc.id == LatestAct::ACT_HULU)
                              ? SendHuluEndGame(desc.id, "{}")
                              : SendLatestPacket(desc.id, "end_game",
                                                 BuildEndGameBody(desc, 0, nullptr));
        if (sent && WaitForResponse(state, kActionTimeoutMs + kWaitMarginMs)) {
            answered = true;
        }
    }
    // 残留会话的结算结果本身没有意义（本来就没有合法成绩），只关心有没有回包。
    state.lastEndResult = -1;
    state.endSettled = false;
    return answered;
}

// 扫荡：sweep_info 预览 -> sweep 执行（失败则由调用方退回游戏流程）
// 发一次 sweep_info，问服务端「这个活动有没有可扫荡的历史成绩」。
// 返回 true 表示放行。与旧周活动 Act822ThreadProc 里
// 「SendAct822SweepInfoPacket(); WaitForAct822Response() && g_act822SweepAvailable」同一形状。
bool ProbeSweepReady(const LatestActDesc& desc, LatestActState& state) {
    state.sweepReady = false;
    PendingOpGuard guard(state, PendingOp::SweepInfo);
    if (!SendLatestPacket(desc.id, "sweep_info") ||
        !WaitForResponse(state, kQueryTimeoutMs + kWaitMarginMs)) {
        return false;
    }
    return state.sweepReady.load();
}

// 把剩余次数一次扫完，不能只扫一次就收工。
// 各活动的 AS3 扫荡窗口一律把剩余次数一次交上去（_curCnt / cnt / _playTime），
// 服务端自己结算多局，客户端不再 for 循环逐次发：
//   647 NewSweepUI.as:78              [GET_SWEEP, _curCnt]
//   742 SweepView.as:80               [GET_SWEEP, _curCnt]
//   721 ActSweepWin.as:84             [SWEEP, _curCnt]
//   877 ActSweepWin.as:77             [SWEEP, _curCnt]
//   730 Act730SweepWin.as:79          [SWEEP, _curCnt]
//   880 SweepUI.as:80                 [GET_SWEEP, _curCnt]
//   882 ActSweepWin.as:61             [SWEEP, int(_changePanel.text)]
// 所以这里恒发一包、带次数，只等一次回包。
//
// probeFirst = false 时跳过前置的 sweep_info：调用方刚刚探过，不再多发一次。
// 返回 true 表示这次扫荡被服务端接受（回包 result 表明 sweepReady）。
bool SweepRemaining(const LatestActDesc& desc, LatestActState& state, bool probeFirst = true) {
    if (probeFirst && !ProbeSweepReady(desc, state)) {
        return false;   // 尚无历史成绩，调用方应先走一局
    }

    const int count = state.playCount.load() > 0 ? state.playCount.load() : 1;
    if (!g_latestBatchRunning.load()) {
        return false;
    }

    // 扫荡被回 11 就是「冷却中」：结算后服务端会挂一个很短的冷却
    // （实测 880 结算后 open_ui 的 restCdTime = 1），此时扫荡必被拒。
    // 冷却秒数不可靠，唯一稳的做法是重试：隔 2 秒再发，最多 3 次。
    // 12（次数不足）与 3（校验失败）不重试，重试也只会再被拒一次。
    //
    // 冷却码有两套：730 一族（含 647/880）是 11，而 721 与 882 是另一套——
    // 721 Act721ErrorCode.errorCodeSweep 与 882 Act882ErrorCode.errorCodeSweep:117-140
    // 都是 3 = 冷却中、1 = 还没成功打完一局、2 = 次数不足。
    // 721/882 按 11 去重试等于永远不重试，冷却一过就整段放弃。
    const bool sweepErrorCodeSetB =
        (desc.id == LatestAct::ACT_STAR_PRINCESS || desc.id == LatestAct::ACT_PUMPKINS_PK);
    const int cooldownCode = sweepErrorCodeSetB ? 3 : 11;
    constexpr int kSweepAttempts = 3;
    int sentCount = count;
    for (int attempt = 0; attempt < kSweepAttempts; ++attempt) {
        if (!g_latestBatchRunning.load()) {
            return false;
        }
        if (attempt > 0) {
            if (!ManagedThread::SleepCancellable(2000) || !g_latestBatchRunning.load()) {
                return false;
            }
            // 上一次 sweep 可能其实已经成功了，只是回包丢了。重发前先查次数：
            // 次数已经掉下去就说明扫荡生效，直接算成功，绝不能再扫一次
            // （服务端会按新的剩余次数再扣一遍，等于白扔一天次数）。
            if (!RefreshOneActivity(desc, state)) {
                return false;
            }
            const int now = state.playCount.load();
            if (now >= 0 && now < count) {
                Notify(desc.id, L"上次扫荡其实已生效（剩余 " + std::to_wstring(now) + L" 次），不再重发");
                return true;
            }
            if (now == 0) {
                return true;
            }
            if (now > 0) {
                sentCount = now;
            }
        }
        state.sweepReady = false;
        state.lastSweepResult = -1;
        {
            PendingOpGuard guard(state, PendingOp::Sweep);
            if (!SendLatestPacket(desc.id, "sweep", std::vector<int32_t>{sentCount}) ||
                !WaitForResponse(state, kQueryTimeoutMs + kWaitMarginMs)) {
                return false;
            }
        }
        if (state.sweepReady.load()) {
            return true;
        }
        if (state.lastSweepResult.load() != cooldownCode) {
            return false;   // 不是冷却，重试无意义
        }
        Notify(desc.id, L"扫荡遇到冷却（result=" + std::to_wstring(cooldownCode) + L"），" +
                            std::to_wstring(kSweepAttempts - attempt - 1) + L" 次重试机会");
    }
    return false;
}

// 走一局：start_game -> 校验种子 -> （可选过程包）-> end_game 提交目标分
bool TryPlayOnce(const LatestActDesc& desc, LatestActState& state) {
    if (!desc.playable) {
        return false;
    }
    state.checkCode = 0;
    state.lastStartResult = -1;
    state.lastEndResult = -1;
    state.endSettled = false;

    auto attemptStart = [&]() -> bool {
        state.checkCode = 0;
        state.startPayloadReady = false;
        state.huluBoardReady = false;
        state.startHp = 0;
        state.pumpkinBonusReady = false;
        state.pumpkinBonus.fill(0);
        state.pumpkinMedal = 0;
        state.lastStartResult = -1;
        {
            PendingOpGuard guard(state, PendingOp::StartGame);
            if (!SendLatestPacket(desc.id, "start_game", BuildStartGameBody(desc, state)) ||
                !WaitForResponse(state, kActionTimeoutMs + kWaitMarginMs)) {
                return false;
            }
        }
        // 742 的 start_game 回包不带校验种子，开没开成看 165 格棋盘有没有到齐。
        if (desc.id == LatestAct::ACT_HULU) {
            return state.huluBoardReady.load();
        }
        // 721 的成功条件是 result == 0 且 ActStartInfoDto 已完整读完；
        // checkCode 允许为 0，不能把它当成开局失败。
        if (desc.id == LatestAct::ACT_STAR_PRINCESS) {
            return state.lastStartResult.load() == 0 && state.startPayloadReady.load();
        }
        // 其他活动仍要求服务端下发非零校验种子。
        return state.checkCode.load() != 0;
    };

    bool started = attemptStart();

    // 721 ActModel.decodeBeginGame:127-147：1 = 次数不足，2 = 冷却，
    // 3 才是「游戏中」；不使用该活动未被调用的旧错误码枚举。
    // 只有明确存在残留对局时才清局，其他活动保留原来的恢复策略。
    const int startResult = state.lastStartResult.load();
    const bool hasStaleSession = (desc.id == LatestAct::ACT_STAR_PRINCESS)
                                     ? startResult == 3
                                     : startResult > 0;
    if (!started && hasStaleSession) {
        Notify(desc.id, L"开局被拒绝（" + StartFailureText(desc, state.lastStartResult.load()) +
                            L"），先关闭上一局残留会话...");
        CloseStaleSession(desc, state);
        started = attemptStart();
    }

    if (!started) {
        return false;
    }

    // 整局的过程包：有过程包的活动必须一条条发完，再交结算。
    // 不允许 start_game 直接接 end_game——那不是「打了一局」，是空局。
    if (!RunPlayProcess(desc, state)) {
        return false;
    }

    bool endSettled = false;
    {
        PendingOpGuard guard(state, PendingOp::EndGame);
        const bool sent = (desc.id == LatestAct::ACT_HULU)
                              ? SendHuluEndGame(desc.id, BuildHuluRewardJson(state))
                              : SendLatestPacket(desc.id, "end_game",
                                                 BuildEndGameBody(desc, state.checkCode.load(), &state));
        if (sent && WaitForResponse(state, kActionTimeoutMs + kWaitMarginMs)) {
            endSettled = true;
        }
    }
    if (!endSettled) {
        return false;
    }

    // 回包到了不等于服务端认了这一局：647/880/882 用 result == 3 表示校验失败，
    // 那种情况下只扣次数不发奖。
    if (!state.endSettled.load()) {
        Notify(desc.id, L"结算被拒绝（result=" +
                            std::to_wstring(state.lastEndResult.load()) + L"）");
        return false;
    }

    // 与 AS3 一致：结算后会顺带上报每日任务 4039001。
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
            if (!SendLatestPacket(desc.id, desc.openUiOp) ||
                !WaitForResponse(state, kQueryTimeoutMs + kWaitMarginMs)) {
                continue;
            }
        }
        // waitingResponse 由响应处理器在解析完成后才清除，这里可直接读取解析结果。
        if (state.playCount.load() >= 0) {
            ReportActivityCount(desc.id, state.playCount.load());
            return true;
        }
    }

    // 查询彻底失败时上报「未知」(-1)，让前端显示 -- 而不是停留在过期次数上。
    ReportActivityCount(desc.id, -1);
    return false;
}

// 等冷却归零。
//
// 打完一局后服务端会给一个很短的冷却（实测 880 的 open_ui 在结算后回 restCdTime=1），
// 此时立刻发 sweep 会被回 11（冷却中）。旧周活动是在活动入口处就判 `restTime > 0`
// 直接放弃（ad5143f^:src/hook/activity_minigames.cpp:219），因为它是「一次活动一次」；
// 这里要连打连扫，就必须把冷却等干净，否则「扫荡完剩余次数」这一步永远做不到。
bool WaitForCooldown(const LatestActDesc& desc, LatestActState& state) {
    constexpr int kMaxCooldownWaitSec = 90;
    int waited = 0;
    while (state.restTime.load() > 0) {
        if (!g_latestBatchRunning.load()) {
            return false;
        }
        if (waited >= kMaxCooldownWaitSec) {
            return false;
        }
        Notify(desc.id, L"冷却中（剩余 " + std::to_wstring(state.restTime.load()) + L" 秒）...");
        if (!ManagedThread::SleepCancellable(1000)) {
            return false;
        }
        ++waited;
        if (!RefreshOneActivity(desc, state)) {
            return false;
        }
    }
    return true;
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

    // 活动开始前若有微小冷却，循环倒计时等待归零
    while (state.restTime.load() > 0 && g_latestBatchRunning.load()) {
        const int cd = state.restTime.load();
        if (cd > 60) {
            Notify(activityId, L"冷却中（剩余 " + std::to_wstring(cd) + L" 秒），跳过");
            return;
        }
        Notify(activityId, L"等待冷却（剩余 " + std::to_wstring(cd) + L" 秒）...");
        if (!ManagedThread::SleepCancellable(1000)) {
            break;
        }
        if (!RefreshOneActivity(desc, state)) {
            break;
        }
    }
    if (state.restTime.load() > 0) {
        return;
    }

    // ------------------------------------------------------------------
    // 统一标准流程——与仓库里全部旧周活动（822/826/827/641/684/631/808…）同一条线，
    // 逐字对应 Act822ThreadProc：
    //
    //   ① open_ui / game_info：拿剩余次数、冷却、历史最好成绩
    //   ② 次数用完 -> 停；冷却中 -> 停
    //   ③ ShouldAutoSweep(最好成绩, 上限最高分) 成立 -> 直接扫荡，一次扫完剩余次数
    //   ④ 不成立 -> 完整打完一局（过程包一条不少，打到满分）-> 再扫荡剩余次数
    //
    // 只有一条主线和一次扫荡，不逐轮试探。逐轮「先扫一次再说」会把成绩不够的
    // 活动按低分白扣一次次数，正是这次要修掉的毛病。
    // ------------------------------------------------------------------
    Notify(activityId, L"剩余 " + std::to_wstring(state.playCount.load()) + L" 次");

    if (CanSweepDirectly(desc, state)) {
        // ③ 本地已确认可以扫荡，直接扫荡，扫完即收工。
        if (desc.passScore > 0) {
            Notify(activityId, L"已有满分成绩（" + std::to_wstring(state.bestScore.load()) + L"/" +
                                   std::to_wstring(desc.passScore) + L"），直接扫荡");
        } else {
            Notify(activityId, L"今天已成功打完过一局，直接扫荡");
        }
        // 闸门在本地判的（scoreInOpenUi=true）这时还没发过 sweep_info，按 AS3 顺序
        // 补一包：ActEnterView.onSweep 先发 SWEEP_INFO 拿奖励预览，再开扫荡窗点确认。
        // 注意这一包只是预览，不扣次数，也不要再拿它的结果当「有没有成绩」的判据；
        // 742 的闸门本身就是这次探针，才不重复发。
        const bool probeFirst = desc.scoreInOpenUi;
        const int before = state.playCount.load();
        const bool swept = SweepRemaining(desc, state, probeFirst);
        const bool refreshed = RefreshOneActivity(desc, state);
        const int after = refreshed ? state.playCount.load() : -1;
        if (swept && after >= 0 && after < before) {
            Notify(activityId, L"扫荡完成，剩余 " + std::to_wstring(after) + L" 次");
            return;
        }
        if (swept) {
            // 回包说成功，但次数没动——服务端实际没扣，不能报「已完成」。
            Notify(activityId, L"扫荡未生效（剩余仍为 " +
                                   (after >= 0 ? std::to_wstring(after) : std::wstring(L"?")) +
                                   L" 次），改为完整打一局");
        } else {
            // 与旧周活动统一标准同一条退路：扫荡失败就改成直接完成
            // （ad5143f^:src/hook/activity_minigames.cpp 里 Act685ThreadProc 的
            //  「扫荡失败，改为直接完成」分支）。直接 return 会把当天次数全废掉。
            Notify(activityId, L"扫荡被拒绝（result=" +
                                   std::to_wstring(state.lastSweepResult.load()) + L"），改为完整打一局");
        }
        if (after == 0) {
            Notify(activityId, L"次数已用完");
            return;
        }
    }

    // ④ 没有成绩或成绩未达上限最高分：完整打一局，然后扫荡剩余次数。
    if (!desc.playable) {
        Notify(activityId, L"成绩未达上限最高分，且本地无法完成该游戏，跳过");
        RefreshOneActivity(desc, state);
        return;
    }

    // 打一局 -> 扫荡剩余。正常只需一轮；但有的活动（742 守护葫芦藤）本来就没有
    // 「历史成绩」这个概念，扫荡永远不放行，这时只能一局一局打到底，
    // 不能打一局就放弃，剩下的次数会白白浪费。
    constexpr int kMaxPlayRounds = 20;
    for (int round = 0; round < kMaxPlayRounds; ++round) {
        if (!g_latestBatchRunning.load()) {
            return;
        }
        // -1 表示「未知」（刚结算/刚扫荡完，次数被标记为待回查），不是「用完」。
        // 把它当 0 会在一局都没打的情况下就报「次数已用完」。
        if (state.playCount.load() < 0 && !RefreshOneActivity(desc, state)) {
            Notify(activityId, L"次数回查失败，停止该活动");
            return;
        }
        if (state.playCount.load() == 0) {
            Notify(activityId, L"次数已用完");
            return;
        }
        if (desc.id == LatestAct::ACT_STAR_PRINCESS) {
            Notify(activityId, L"今天还没有成功打完过一局（maxScore=" +
                                   std::to_wstring(state.bestScore.load()) + L"），完整打一局...");
        } else if (desc.passScore > 0) {
            Notify(activityId, L"成绩未达上限最高分（" + std::to_wstring(state.bestScore.load()) + L"/" +
                                   std::to_wstring(desc.passScore) + L"），完整打一局...");
        } else if (desc.scoreInOpenUi) {
            // 882：bestFlag 由 open_ui 回包给出，0 = 今天还没成功打完过一局。
            // 打成一局后 bestFlag 变 1，此时再进循环只能是扫荡没成功。
            if (state.bestScore.load() > 0) {
                Notify(activityId, L"扫荡未成功，继续完整打一局...");
            } else {
                Notify(activityId, L"今天还没有成功打完过一局（bestFlag=0），完整打一局...");
            }
        } else {
            Notify(activityId, L"没有可扫荡成绩，完整打一局...");
        }
        if (!TryPlayOnce(desc, state)) {
            // 开局被拒 / 结算被拒 / 回包没来 是三件完全不同的事，分开说。
            const int startResult = state.lastStartResult.load();
            const int endResult = state.lastEndResult.load();
            std::wstring reason;
            if (desc.id == LatestAct::ACT_STAR_PRINCESS && state.playProcessFailValue.load() >= 0) {
                reason = L"过程包发送失败（game_event 累计值=" +
                         std::to_wstring(state.playProcessFailValue.load()) +
                         L"，WSA=" + std::to_wstring(state.playProcessSendError.load()) + L"）";
            } else if (desc.id == LatestAct::ACT_STAR_PRINCESS &&
                       state.playProcessFailValue.load() == -2) {
                reason = L"过程包被停止";
            } else if (startResult > 0) {
                reason = L"开局被拒绝（" + StartFailureText(desc, startResult) + L"）";
            } else if (endResult >= 0) {
                reason = L"结算被拒绝（result=" + std::to_wstring(endResult) + L"）";
            } else {
                reason = L"没等到服务端回包（start=" + std::to_wstring(startResult) +
                         L"，end=" + std::to_wstring(endResult) + L"）";
            }
            Notify(activityId, reason + L"，停止该活动");
            RefreshOneActivity(desc, state);
            return;
        }
        // 721：服务端自己算出来的成绩才是这一局的得分（奖励按它发档），
        // 顺手报出来——客户端报 400 星、服务端认 30 星的话，一眼就能看见。
        if (desc.id == LatestAct::ACT_STAR_PRINCESS && state.endStarCount.load() >= 0) {
            std::wstring done = L"已完整打完一局并成功结算（服务端认定 " +
                                std::to_wstring(state.endStarCount.load()) + L"/" +
                                std::to_wstring(desc.targetScore) + L" 星";
            if (state.endCoin.load() >= 0) {
                done += L"，铜钱 +" + std::to_wstring(state.endCoin.load());
            }
            if (state.endLilian.load() >= 0) {
                done += L"，历练 +" + std::to_wstring(state.endLilian.load());
            }
            if (state.endStarCount.load() < desc.targetScore) {
                done += L"；未满额，奖励按这一档发";
            }
            Notify(activityId, done + L"）");
        } else {
            Notify(activityId, L"已完整打完一局并成功结算");
        }

        // 打完立刻重查：既刷新前端，也确认这一局确实扣了次数。
        if (!RefreshOneActivity(desc, state)) {
            Notify(activityId, L"次数回查失败，停止该活动");
            return;
        }
        if (state.playCount.load() <= 0) {
            Notify(activityId, L"次数已用完");
            return;
        }

        // 结算后服务端会挂一个短冷却，此时发 sweep 只会被回 11（冷却中）。
        if (!WaitForCooldown(desc, state)) {
            Notify(activityId, L"冷却未结束，停止该活动");
            return;
        }

        // 721 扫荡策略（用户指定）：只有上一局拿到满分（服务端认定的
        // endStarCount == targetScore=400）才扫荡剩余次数——扫荡奖励按
        // 最好成绩那一档发，低分局扫掉等于把剩余次数按低档贱卖；不满额
        // 就继续逐局打。（真客户端 ActEnterView.as:119 的 maxScore>0 只是
        // 入口开关，不管档次；档次是服务端按最好成绩算的。）
        if (desc.id == LatestAct::ACT_STAR_PRINCESS) {
            if (state.bestScore.load() <= 0) {
                Notify(activityId, L"今天还没有成功打完过一局（maxScore=0），不扫荡，继续打下一局");
                continue;
            }
            if (state.endStarCount.load() < desc.targetScore) {
                Notify(activityId, L"上一局成绩未满额（" +
                                       std::to_wstring(state.endStarCount.load()) + L"/" +
                                       std::to_wstring(desc.targetScore) +
                                       L"），不扫荡，继续打下一局");
                continue;
            }
        }

        // 扫荡前按 AS3 顺序发一包 sweep_info 拿奖励预览（不扣次数）。
        // 这一包不是成绩查询，别拿它的结果当门槛——882 的「有没有成绩」一律看
        // open_ui 的 bestFlag（上面 RefreshOneActivity 刚刷成权威值）。
        if (!ProbeSweepReady(desc, state)) {
            Notify(activityId, L"服务端暂无可扫荡成绩，继续打下一局");
            continue;
        }

        Notify(activityId, L"扫荡剩余 " + std::to_wstring(state.playCount.load()) + L" 次...");
        const int beforeSweep = state.playCount.load();
        const bool swept = SweepRemaining(desc, state, false);

        // 活动退出时执行最终刷新，确保前端 UI 永远展示最新的权威次数
        if (!RefreshOneActivity(desc, state)) {
            if (swept) {
                Notify(activityId, L"扫荡完成（次数回查失败）");
            }
            return;
        }
        if (swept) {
            const int after = state.playCount.load();
            if (after >= beforeSweep) {
                Notify(activityId, L"扫荡未生效（剩余仍为 " + std::to_wstring(after) + L" 次）");
            } else {
                Notify(activityId, L"扫荡完成，剩余 " + std::to_wstring(after) + L" 次");
            }
            return;
        }
        // 扫荡没成（冷却没清干净 / 服务端拒扫），剩下的次数只能继续用打的方式消耗，
        // 直接 return 会把当天剩余次数全部浪费。
        const int sweepResult = state.lastSweepResult.load();
        // 「次数不足」不该再打一局：start_game 一样会被拒，白等两个超时。
        // 721/882 用 2（Act721ErrorCode / Act882ErrorCode 的 errorCodeSweep），
        // 730 一族（含 647/880）用 12。
        const bool errSetB =
            (desc.id == LatestAct::ACT_STAR_PRINCESS || desc.id == LatestAct::ACT_PUMPKINS_PK);
        const int noCountCode = errSetB ? 2 : 12;
        if (sweepResult == noCountCode) {
            Notify(activityId, L"扫荡被回「次数不足」，今日已无剩余次数");
            return;
        }
        Notify(activityId, L"扫荡被拒绝（result=" + std::to_wstring(sweepResult) +
                               L"），继续打下一局");
    }
    Notify(activityId, L"达到单活动最大轮数，停止");
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
        // 解析不出操作串时不要放行等待方：放行会让调用方读到尚未更新的 checkCode
        // 而误判成「服务端没下发种子」，真实原因（回包结构不是「操作串 + 参数」）
        // 就被掩盖了。保持等待直到超时。
        return;
    }

    // play_game / game_event 这类过程包即使被服务端回送也不在本模块的解析范围内。
    const PendingOp op = PendingOpOf(*desc, operation);
    if (op == PendingOp::None) {
        // 721 的 game_event 回音不解析、只计数：RunPlayProcess 用它做事件间节拍。
        if (operation == "game_event") {
            state->playEventAcks.fetch_add(1, std::memory_order_relaxed);
        }
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

    // 走受管线程：退出时 CleanupWpeHook 会 RequestStopAll/JoinAll，不再是一条没人等的裸线程。
    const bool started = ManagedThread::Start(L"latest_activities", [targets]() {
        bool failed = false;
        try {
            for (const LatestActDesc* desc : targets) {
                if (!g_latestBatchRunning.load() || ManagedThread::StopRequested()) {
                    break;
                }
                RunOneActivityUntilDone(*desc);
            }
        } catch (...) {
            failed = true;
        }

        const bool cancelled = !g_latestBatchRunning.load() || ManagedThread::StopRequested();
        // 运行标志必须无条件放掉：否则一次异常就让「一键执行」永远报"进行中"。
        g_latestBatchRunning = false;
        if (failed) {
            UIBridge::Instance().UpdateHelperText(L"最新活动：一键执行异常中止");
        } else if (cancelled) {
            UIBridge::Instance().UpdateHelperText(L"最新活动：一键执行已停止");
        } else {
            UIBridge::Instance().UpdateHelperText(L"最新活动：一键执行完成");
        }
    });
    if (!started) {
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
