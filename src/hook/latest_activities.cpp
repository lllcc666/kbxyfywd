#include "byte_reader.h"
#include "latest_activities.h"

#include "managed_thread.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <random>
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
    /// 的第二个参数同义：历史成绩达到它才允许直接扫荡。0 表示该活动没有上限最高分
    /// 常量，门槛退化为进入包里的「今天成功打完过一局」标志位（882/808）。
    int passScore;
    bool reportsDailyTask;  ///< 结算后是否上报每日任务（721/730 有）
    /// 是否满足自动执行的准入条件；false 时仅保留只读次数查询。
    bool playable;
};

// 列：id, name, openUiOp, targetScore, passScore, reportsDailyTask, playable
//
// passScore 一律取 AS3 里那个「显示给玩家看的上限最高分」常量，不要自己估：
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
//   808 欢乐跷跷板  无上限常量（KBQiaoData.as:96 的 max_score = 55 只是
//                    KBBatttleView.init:172 写进文本框的展示值，服务端按它生成
//                    掉落表；本局提交的是真实捡到的分，所以 targetScore = 0）；
//                    门槛同样退化为 open_ui 的 historyBestScore > 0。
//   883 风焰神殿大冒险 使用服务端逐题计分，走专用流程，不按猜测的成绩扫荡。
//
// 扫荡次数：全部活动的 AS3 扫荡窗口一律把次数一次交上去——
//   721 ActSweepWin.as:84       [SWEEP, _curCnt]
//   877 ActSweepWin.as:77       [SWEEP, _curCnt]
//   730 Act730SweepWin.as:79    [SWEEP, _curCnt]
//   880 SweepUI.as:80           [GET_SWEEP, _curCnt]
//   882 ActSweepWin.as:61       [SWEEP, int(_changePanel.text)]
//   808 Act808SweepWin.as:77    [SWEEP, this._curCnt]（updateSweepUI:124-125 把
//                               _curCnt 预置成全部剩余次数，一次扫完）
//   883 SweepPage.as:73         [GET_SWEEP, int(this._changePanel.text)]，
//                               _changePanel.max = KBDataCenter.restCount（:49）
// 所以 sweep 一律带次数、一次发完，没有「无参 sweep 逐次发」那种活动。
const LatestActDesc kActivities[] = {
    {LatestAct::ACT_STAR_PRINCESS, L"采菊东篱下", "open_ui", 400, 400, true, true},
    {LatestAct::ACT_FIND_CORNEL, L"寻找七色茱萸", "open_ui", 10, 10, false, true},
    {LatestAct::ACT_FIGHT_EVILS, L"智斗恶灵", "game_info", 160, 160, true, true},
    {LatestAct::ACT_TREASURE, L"夺宝行动", "open_ui", 55, 55, false, true},
    {LatestAct::ACT_PUMPKINS_PK, L"南瓜VS南瓜王", "open_ui", 0, 0, false, true},
    // 808 的 sendEndGame:75 顺带上报每日任务 [3, 4039001, 0]，与 882 一致。
    {LatestAct::ACT_QIAO, L"欢乐跷跷板", "open_ui", 0, 0, true, true},
    // 883 的成绩由服务端逐题累计，使用专用走棋和结算流程。
    {LatestAct::ACT_ADVENTURE, L"风焰神殿大冒险", "game_info", 0, 0, false, true},
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

// 808 欢乐跷跷板：start_game 回包固定下发 180 项掉落表（KBQiaoModel.decodeStartGame:138-141
// 的 for 循环上限），每项是一次掉落物的类型 id。整局由 KBTimeUtil 每秒 tick 一次、
// 每两个 tick 调 createAI(4)（KBBatttleView.updateTool:63-73），45 × 4 = 180，正好把整张表
// 消费完。结算时只回传按类型累计的计数器与总分，所以这里存下来就够，不必保留表本身。
constexpr int kQiaoDrops = 180;
constexpr int kQiaoTypeSlots = 15;   ///< awardNumArr / scoreArr / ACTIVE_TOOL_TOOL_NUM 长度
constexpr int kQiaoScoreType = 15;   ///< 计分类型：只有它让 scoreArr[14] 自增（KBPerker.as:130-142）
constexpr int kQiaoBoltType = 30;    ///< 闪电：只扣 HP，不进 scoreArr/awardNumArr，也不发 gameing
                                     ///< （KBPerker.as:105-117）
// 808 过程包间隔。整局最多 180 条 gameing，按 AS3 的 90 秒铺开是纯浪费；300ms 是
// 本模块实测能稳定结算的档位（与 730 的 200ms 同一量级），跑通后可以再往下压。
constexpr DWORD kQiaoPacketGapMs = 300;

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

// 883 风焰神殿大冒险：棋盘是 m0..m9 十个机关格（KBRankView.init:265-283），
// 三种机关类型共用这块十格棋盘，只是可见/可点的格子不同。
constexpr int kAdvBoardSlots = 10;
// 类型 2 的下排候选数：m5..m9 共 5 个（awardArr[1..5]）。
constexpr int kAdvMatchCandidates = 5;
// 类型 3 的行宽：上排 m0..m4 是目标顺序，下排 m5..m9 可交换。
constexpr int kAdvRowSlots = 5;
// 883 没有已确认的满分或可用于提前结束的奖励上限。
// KBTimeUtil 从 120 倒计时，计数小于 0 后才弹出结束确认。
// 从开局回包开始计时；结束仍发送 end_game,0，不上报本地成绩。
constexpr DWORD kAdvRoundDurationMs = 122000;
// 按用户指定的约 3 秒节奏随机等待；这不是对未知 result=3 原因的判定。
constexpr int kAdvMoveGapMinMs = 2700;
constexpr int kAdvMoveGapMaxMs = 3300;
constexpr DWORD kAdvWaitSliceMs = 50;
constexpr DWORD kAdvCleanupWaitMs = 2000;
// 结算回包后，AS3 还会异步刷新 game_info；给服务端写入次数留出几次重试。
constexpr int kAdvSettlementRefreshAttempts = 3;
constexpr DWORD kAdvSettlementRefreshGapMs = 1000;
// 不限制单局题数；AS3 由计时、生命耗尽或完成条件结束。这里的余量只防止
// 服务端计数异常导致无限开局，不把它当作正常结束条件。
constexpr int kAdvSafetyRunMargin = 2;
// 三种机关题面（KBDataCenter.iType，KBRankView.initGameByType:285-336 按它分派）。
constexpr int kAdvTypeOddOneOut = 1;  ///< 找出下列机关不同的图案
constexpr int kAdvTypeMatch = 2;      ///< 在下面一行找出与上面相同的图案
constexpr int kAdvTypeArrange = 3;    ///< 移动下面一行机关的位置使顺序与上排一致

// ---------------------------------------------------------------------------
// 每活动运行时状态
// ---------------------------------------------------------------------------
struct AdventureBoard {
    std::array<int, kAdvBoardSlots> tiles{};
    int type = 0;
    int score = 0;
    int hp = 0;
    bool ready = false;
};

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

    /// 808 专用：start_game 回包下发的 180 项掉落表原始类型 id
    /// （KBQiaoModel.decodeStartGame:138-141）。整局把整张表按类型累计成计数器，
    /// 结算时回传（KBQiaoControl.sendEndGame:68-73）。
    std::array<int, kQiaoDrops> qiaoDrops{};
    /// 掉落表是否已完整读到。写表在前、置位在后（release），读侧先读它再读表，
    /// 保证响应线程写的 180 个 int 不会被一键线程读到半截。
    std::atomic<bool> qiaoDropReady{false};
    /// 808 专用：结算要回传的 15 项奖励计数（KBPerker.as:149-153 每次命中 ++）。
    /// 只有一键线程写（RunPlayProcess 里按掉落表累计）与读（BuildEndGameBody），
    /// 不在响应线程里碰，所以不需要原子；qiaoAwardReady 置位后才可信。
    std::array<int, kQiaoTypeSlots> qiaoAwardNum{};
    std::atomic<bool> qiaoAwardReady{false};
    /// 808 专用：本局上报分 = 接到多少只计分怪（KBPerker.as:130-142 的 scoreArr[14]）。
    std::atomic<int> qiaoScore{0};
    /// 808 专用：start_game 下发的 randomCode（decodeStartGame:132）。
    /// 它算 checkCode 用的是乘法 randomCode * (uid % 1000) * (checkCode + 1)
    /// （KBQiaoControl.sendGaming:53-60），与其它活动的 & 掩码变换不同，
    /// 所以必须把原值留下来，不能只留变换后的结果。
    std::atomic<int> qiaoRandomCode{0};

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

    /// 响应线程和执行线程必须在同一把锁下读写完整棋盘快照。
    std::mutex advMutex;
    AdventureBoard advBoard;
    std::atomic<bool> advInterference{false};
    std::atomic<int> advEndScore{-1};
    // end_game 可在走子期间独立到达；收到不等于结算成功，但必须先停走子。
    std::atomic<bool> advEndReceived{false};
    std::atomic<bool> advClosing{false};
    // 首个未确认结果整局保留，不能被后续收尾成功覆盖。
    std::atomic<bool> advAbortRound{false};
    std::atomic<int> advAbortResult{-1};

    /// 占用标记：0 空闲，1 一键流程，2 次数探针。
    /// 二者共用 (ACTIVITY_QUERY_BACK, activityId) 路由到同一个响应处理器，而该处理器
    /// 会无条件清除 waitingResponse；若并发，探针的响应会提前清掉一键流程的等待标志，
    /// 令其误判收到响应。用 CAS 保证同一活动同一时刻只有一种流程在跑。
    std::atomic<int> opLock{0};
};

LatestActState g_state[kActivityNum];

// 883 的只读探针必须等 game_info 完整解析，不能由同族的手动对局回包唤醒。
// 此锁也保证 UI 中的次数与手动操作引起的失效通知按处理顺序投递。
std::mutex g_adventureProbeMutex;
std::condition_variable g_adventureResponse;
bool g_adventureProbePending = false;

// 手动原客户端和自动流程都记录883；独立锁保证收发日志按投递顺序排列。
// 请求发送时可能持有探针锁，不能反向获取请求锁或同步调用UI。
constexpr size_t kAdvTraceBytes = 256;
std::mutex g_advTraceMutex;
const auto g_advTraceStarted = std::chrono::steady_clock::now();

void AppendAdventureTraceLocked(const std::wstring& label, const BYTE* data, size_t size) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - g_advTraceStarted).count();
    std::wstring text = L"+" + std::to_wstring(elapsed) + L"ms " + label +
                        L" bytes=" + std::to_wstring(size) + L" hex=";
    constexpr wchar_t digits[] = L"0123456789ABCDEF";
    const size_t count = (std::min)(size, kAdvTraceBytes);
    for (size_t i = 0; i < count; ++i) {
        if (i != 0) text += L' ';
        text += digits[data[i] >> 4];
        text += digits[data[i] & 0x0F];
    }
    if (count != size) text += L" …";
    // UpdateHelperText只通过PostMessage投递，不在socket线程同步执行UI。
    UIBridge::Instance().UpdateHelperText(L"风焰神殿抓包：" + text);
}

/// 批量一键执行是否在进行：整批活动串行跑，同一时刻只允许一个 worker。
std::atomic<bool> g_latestBatchRunning{false};
// 停止只清 running，active 要等旧线程收尾后才释放，避免快速重启复活旧线程。
std::mutex g_latestBatchMutex;
std::condition_variable g_latestBatchStopped;
bool g_latestBatchActive = false;
uint64_t g_latestBatchGeneration = 0;
uint64_t g_latestBatchCompleted = 0;

struct LatestBatchCompletion {
    ~LatestBatchCompletion() {
        {
            std::lock_guard<std::mutex> lock(g_latestBatchMutex);
            g_latestBatchRunning = false;
            g_latestBatchActive = false;
            g_latestBatchCompleted = g_latestBatchGeneration;
        }
        g_latestBatchStopped.notify_all();
    }
};

// 只由当前批次 worker 读写。
bool g_latestBatchHadErrors = false;

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
// 发包与业务响应等待分开：由响应处理器解析完整数据后放行等待方，
// 避免仅凭 opcode 和活动 ID 唤醒后读到尚未解析的旧状态。
// SendPacketBytes 在发送错误时可能重试；成功发送后的等待超时不会触发重发。
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
    /// 883 的局内走子。其余活动的过程包（721 game_event、808 gameing）服务端
    /// 不回包或回包无意义，所以从来不在等待表里；883 的每一步都要读回包里的新棋盘，
    /// 必须像动作包一样排队等待，否则下一步会拿旧棋盘算走法。
    PlayGame,
};

/// 等待守卫：置位等待标志并登记期望回包，离开作用域时一并撤销，
/// 保证任何提前 return 都不会留下悬挂的等待状态。
struct PendingOpGuard {
    LatestActState& state;
    PendingOpGuard(LatestActState& s, PendingOp op) : state(s) {
        std::unique_lock<std::mutex> lock(g_adventureProbeMutex, std::defer_lock);
        if (&state == StateOf(LatestAct::ACT_ADVENTURE)) lock.lock();
        state.pendingOp = static_cast<uint32_t>(op);
        state.waitingResponse = true;
    }
    ~PendingOpGuard() {
        std::unique_lock<std::mutex> lock(g_adventureProbeMutex, std::defer_lock);
        if (&state == StateOf(LatestAct::ACT_ADVENTURE)) lock.lock();
        state.pendingOp = static_cast<uint32_t>(PendingOp::None);
        state.waitingResponse = false;
    }
    PendingOpGuard(const PendingOpGuard&) = delete;
    PendingOpGuard& operator=(const PendingOpGuard&) = delete;
};

// 发送调用不等待回包；持锁覆盖最后的干扰检查、登记等待与发包。
// 接收线程可在发送返回后进入，不会在此锁内等待服务端响应。
struct AdventureRequest {
    LatestActState& state;
    bool sent = false;
    AdventureRequest(LatestActState& s, PendingOp op, const char* operation,
                     const std::vector<int32_t>& values, bool cleanup = false) : state(s) {
        std::lock_guard<std::mutex> lock(g_adventureProbeMutex);
        if (state.advInterference.load() ||
            (!cleanup && (!g_latestBatchRunning.load() || ManagedThread::StopRequested()))) return;
        // 与回包处理器同锁：终局到达后不能再发下一步，也不能重复提交已确认结算。
        if ((op == PendingOp::PlayGame &&
             (state.advEndReceived.load() || state.advClosing.load())) ||
            (op == PendingOp::EndGame && state.advEndReceived.load())) return;
        state.pendingOp = static_cast<uint32_t>(op);
        state.waitingResponse = true;
        if (op == PendingOp::StartGame || op == PendingOp::EndGame) {
            state.playCount = -1;
            ReportActivityCount(LatestAct::ACT_ADVENTURE, -1);
        }
        if (op == PendingOp::OpenUi) {
            sent = SendLatestPacket(LatestAct::ACT_ADVENTURE, operation, values) != FALSE;
        } else {
            const auto packet = BuildActivityPacket(Opcode::ACTIVITY_QINGYANG_NEW_SEND,
                                                    LatestAct::ACT_ADVENTURE, operation, values);
            sent = (cleanup ? SendPacketBytesForCleanup(packet) : SendPacketBytesOnce(packet)) != FALSE;
        }
    }
    ~AdventureRequest() {
        std::lock_guard<std::mutex> lock(g_adventureProbeMutex);
        state.pendingOp = static_cast<uint32_t>(PendingOp::None);
        state.waitingResponse = false;
    }
    AdventureRequest(const AdventureRequest&) = delete;
    AdventureRequest& operator=(const AdventureRequest&) = delete;
};

bool WaitForResponse(LatestActState& state, DWORD timeoutMs) {
    return WaitForConditionEx([&state]() { return !state.waitingResponse.load(); },
                              []() { return !g_latestBatchRunning.load(); }, timeoutMs, 50) ==
           WaitOutcome::Signalled && g_latestBatchRunning.load() && !ManagedThread::StopRequested();
}

// 扫荡门槛——沿用仓库里周活动的统一标准，不要另立一套。
//
// 出处：ad5143f^:src/hook/activity_minigames.cpp:141
//   bool ShouldAutoSweep(int bestScore, int flowScore) {
//       if (bestScore <= 0) { return false; }
//       return bestScore >= flowScore;
//   }
// 全部旧周活动（822/826/827/641/684/631…）都是这一句判的：
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
// 不扣次数，也不该拿它当「有没有历史成绩」的门槛——门槛一律读进入包里的本地字段
// （CanSweepDirectly）。
bool ProbeSweepReady(const LatestActDesc& desc, LatestActState& state);

// 重查一次活动信息（次数/冷却/成绩），定义在后面；扫荡重试要用它防重复扫荡。
bool RefreshOneActivity(const LatestActDesc& desc, LatestActState& state);

// 本活动当前是否可以「直接扫荡」——就是用户流程里的第一步判据。
//
// 判据只有一条，与旧周活动完全一致：ShouldAutoSweep(历史最好成绩, 上限最高分)。
// 成绩一律读 open_ui / game_info 回包里已经解析好的 state.bestScore，不走网络。
// 唯一的分叉是「有没有上限最高分可比」：
//   ① 有上限最高分常量的活动（721/877/730/880）：直接 ShouldAutoSweep。
//   ② 没有上限最高分、回包只给「今天成功打完过一局」标志位的活动
//      （882 的 bestFlag、808 的 historyBestScore）：门槛退化为「标志位 > 0」。
//
// 这条判据必须留在本地，不能改成发 sweep_info 问服务端：sweep_info 是「预览这次
// 扫荡给什么奖」的包，不是「有没有历史成绩」的查询，服务端在没有成绩时也会按
// type 正常回包，探针据此放行，紧接着就发真实 sweep，白扣一次次数——这正是
// 「开局先扫荡一次、发现没成绩、再正常打一局」的成因（882 早期版本就这么错过）。
//
// 注意这里也不看「本进程内刚打完一局」这种旁证：那是执行历史，与「服务端上有没有
// 满分成绩」是两件事。混用会让成绩没到上限的活动也直接扫，按低分发奖还白扣次数。
bool CanSweepDirectly(const LatestActDesc& desc, LatestActState& state) {
    if (desc.passScore > 0) {
        return ShouldAutoSweep(state.bestScore.load(), desc.passScore);
    }
    // 无上限最高分，回包带的是「今天成功打完过一局」标志（bestFlag）。
    // 语义与 730 ActivityData.bestFlag 一致：ActEnterView.as:153 用它决定
    // 放不放行扫荡，否则弹「需要成功进行一局游戏才可以扫荡哦！」。
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

// 808 的 checkCode 不走 & 掩码，而是连乘：
//   randomCode * (uid % 1000) * (checkCode + 1)
// （KBQiaoControl.sendGaming:58 与 sendEndGame:67 是同一个式子）。
// AS3 的 int 乘法按 32 位回绕，这里用 uint32_t 算完再转回 int，语义一致。
// randomCode 为 0（没开局成功）时结果必为 0，服务端会判校验失败，
// 所以调用方要先确认 randomCode 非 0。
int QiaoCheckCode(int randomCode, int seed) {
    const uint32_t userId = g_userId.load();
    if (randomCode <= 0 || userId == 0) {
        return 0;
    }
    const uint32_t value = static_cast<uint32_t>(randomCode) * (userId % 1000u) *
                           static_cast<uint32_t>(seed + 1);
    return static_cast<int>(value);
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
        case LatestAct::ACT_QIAO:
            // playCount, restCdTime, medalNum, historyBestScore, catchNum,
            // isBuy, maxNum, isPop（KBQiaoModel.decodeOpenUI:101-121）
            if (offset + 32 > size) return;
            state.playCount = read();     // playCount
            state.restTime = read();      // restCdTime
            offset += 4;                  // medalNum（勋章数，本流程不消费）
            state.bestScore = read();     // historyBestScore -> 扫荡门槛
            offset += 4;                  // catchNum
            offset += 4;                  // isBuy
            offset += 4;                  // maxNum
            state.ruleFlag = read();      // isPop -> start_game 的参数
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

        case LatestAct::ACT_ADVENTURE:
            // result, playCount, coolTimeLeft, bestScore, isPop
            // （KBAdventureModel.decodeOpenUI:244-256，共 5 个 int，后面没有别的字段）
            // AS3 自己不判 result，照抄：次数为 0 时由流程层按「今日次数已用完」收尾。
            {
                read();                   // result（AS3 未使用）
                const int count = read(); // playCount -> restCount
                const int cooldown = read();
                const int bestScore = read();
                const int ruleFlag = read();
                if (!reader.ok() || count < 0) return;
                state.playCount = count;
                state.restTime = cooldown;
                state.bestScore = bestScore;
                state.ruleFlag = ruleFlag;
            }
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
// 除 721/808 外，本模块所有活动的回包都是「先 result、再一个被丢弃的字段、然后才是
// checkCode」，已逐个用 FFDec 反编译核对：
//   877 ActModel.as:118-146           : result, playCount, checkcode, num, rewardList[num]
//   730 ActModel.as:114-134           : result, playCount, check_code, hp, ActAwardDto
//   880 TreasureModel.as:58-78        : result, readInt(), checkCode
//   882 ActModel.as:114-118           : result, playCount, check_code, gameBonus, ...
//   721 ActModel.decodeBeginGame:127  : result, ActStartInfoDto{checkCode, ...}  <- 无丢弃字段
//   808 KBQiaoModel.decodeStartGame   : result, playCount, randomCode, checkcode, 180 项掉落表
//                                       <- 三个字段，且 randomCode 必须留住（见下面分支）
// 该字段不能被当作 checkCode 使用；漏跳会让 checkCode 整体前移 4 字节，
// end_game 的校验式全错，服务端只扣次数不下发奖励。
bool IsValidAdvBoard(int type, int length) {
    if (type == kAdvTypeOddOneOut || type == kAdvTypeArrange) return length == kAdvBoardSlots;
    if (type == kAdvTypeMatch) return length >= kAdvMatchCandidates + 1 && length <= kAdvBoardSlots;
    return false;
}

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

    // 808 的 start_game 回包不是「result + 一个丢弃字段 + checkCode」那种形状：
    // KBQiaoModel.decodeStartGame:123-149 是 result, playCount, randomCode,
    // checkcode，然后跟 180 项掉落表。走出去下面的通用分支会把 randomCode 当成
    // checkCode——结算校验式是 randomCode * (uid % 1000) * (checkCode + 1)
    // （KBQiaoControl.sendGaming:53-60），拿错就是每一局都被判校验失败。
    // 掉落表必须整张读进来（结算要按类型报计数器），所以开局成功以读满 180 项为准。
    if (desc.id == LatestAct::ACT_QIAO) {
        if (offset + 12 > size) return;
        state.playCount = reader.I32();           // playCount
        state.qiaoRandomCode = reader.I32();      // randomCode
        state.checkCode = reader.I32();           // checkcode
        state.qiaoDropReady = false;
        state.qiaoDrops.fill(0);
        int drops = 0;
        for (; drops < kQiaoDrops && offset + 4 <= size; ++drops) {
            state.qiaoDrops[static_cast<std::size_t>(drops)] = reader.I32();
        }
        state.qiaoDropReady = (drops == kQiaoDrops);
        return;
    }

    // 883 的开局棋盘由服务端直接下发，没有 checkCode。
    if (desc.id == LatestAct::ACT_ADVENTURE) {
        AdventureBoard board;
        const int count = reader.I32();
        board.score = reader.I32();
        board.hp = reader.I32();
        board.type = reader.I32();
        const int boardLen = reader.I32();
        if (!reader.ok() || count < 0 || board.hp < 0 ||
            !IsValidAdvBoard(board.type, boardLen) ||
            static_cast<size_t>(boardLen) > reader.Remaining() / sizeof(int32_t)) return;
        for (int i = 0; i < boardLen; ++i) {
            board.tiles[static_cast<std::size_t>(i)] = reader.I32();
        }
        if (!reader.ok()) return;
        board.ready = true;
        {
            std::lock_guard<std::mutex> lock(state.advMutex);
            state.advBoard = board;
        }
        state.playCount = count;
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

// 883 的每次走棋都要读取完整 DTO，再原子发布下一张棋盘。
void ParsePlayGame(LatestActState& state, const GamePacket& packet, size_t offset) {
    ByteReader reader(packet.body, offset);
    AdventureBoard board;
    const int result = reader.I32();
    board.score = reader.I32();
    board.hp = reader.I32();
    board.type = reader.I32();
    const int boardLen = reader.I32();
    // decodeGameMove 对所有结果都读取并发布棋盘，答错同样返回新题面。
    if (!reader.ok() || board.hp < 0 || boardLen < 0 ||
        static_cast<size_t>(boardLen) > reader.Remaining() / sizeof(int32_t)) return;
    for (int i = 0; i < boardLen; ++i) {
        const int tile = reader.I32();
        if (i < kAdvBoardSlots) board.tiles[static_cast<std::size_t>(i)] = tile;
    }
    const int toolLen = reader.I32();
    if (!reader.ok() || toolLen < 0 ||
        static_cast<size_t>(toolLen) > reader.Remaining() / sizeof(int32_t)) return;
    for (int i = 0; i < toolLen; ++i) reader.I32();
    reader.I32();  // xz；奖励由服务端累计，不需回传
    // 手动正常对局的成功回包在xz后结束；AS3的iAwardLen没有消费者。
    // 仅在存在尾数据时尝试读取；缺省不能使完整题面被当成截断包丢弃。
    if (result == 1 && reader.Remaining() != 0) reader.I32();  // 可选iAwardLen
    if (!reader.ok()) return;
    // 生命耗尽时不再求解，空棋盘也必须发布，让执行方走结束流程。
    // 活着时仍保留题面形状校验，不能用补零数据发送一个猜出的答案。
    board.ready = board.hp == 0 || IsValidAdvBoard(board.type, boardLen);
    if (!board.ready) {
        Notify(LatestAct::ACT_ADVENTURE, L"走子回包题面不支持（result=" +
               std::to_wstring(result) + L"，type=" + std::to_wstring(board.type) +
               L"，len=" + std::to_wstring(boardLen) + L"）");
    }
    std::lock_guard<std::mutex> lock(state.advMutex);
    state.advBoard = board;
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
        case LatestAct::ACT_QIAO:
            // 808 KBQiaoModel.decodeSweepInfo:152-171：首字段就是 result，
            // result == 0 才读 medal/exp/coin/rewardNum 与奖励对。1 = 还没成功
            // 打完一局、2 = 次数不足、3 = 冷却中（Act808ErrorCode.errorCodeSweep）。
            state.sweepReady = (first == 0);
            break;
        case LatestAct::ACT_ADVENTURE: {
            // 883 KBAdventureModel.decodeSweepInfo:449-476 有**两个**前导 int：
            // code 与 result，只有 code == 0 且 result ∈ {0,4} 才继续读
            // medal/exp/coin、才允许开扫荡窗；否则弹
            // 「需要成功进行一局游戏才可以扫荡哦！」。
            // 这是本模块唯一一个首字段不是结果码的派发：first 是 code，
            // 结果码必须再读一个 int 才拿得到。
            state.sweepReady = false;
            if (offset + 4 > size) {
                break;
            }
            const int result = reader.I32();
            state.sweepReady = (first == 0 && (result == 0 || result == 4));
            break;
        }
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
        case LatestAct::ACT_TREASURE:
        default:
            // 880 TreasureModel.as:307-356：result != 3。
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
        case LatestAct::ACT_QIAO:
            // 808 Act808SweepWin.as:104-118 / Act808ErrorCode.errorCodeSweep：
            // result == 0 是扫荡成功，4 是「奖励已发完但仍算成功」。
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
            // 它没有像 880 那样用「!= 3」的宽松判据，这里也不能放宽——
            // 否则 1（还没成功打完一局）/2（次数不足）/3（冷却中）都会被当成扫成功。
            state.sweepReady = (result == 0 || result == 4);
            break;
        case LatestAct::ACT_ADVENTURE:
            // 883 的 GET_SWEEP 回包与 end_game 走同一个解码函数
            // （KBAdventureModel.decodeEndGame:334-369，由 onSocketHandler:186-242
            // 同时挂在 END_GAME 与 GET_SWEEP 上），result ∈ {0,4} 才算成功。
            // 不能落到下面 fallback 的「!= 3」：883 的 1/2/3 都不是成功码。
            state.sweepReady = (result == 0 || result == 4);
            break;
        case LatestAct::ACT_FIND_CORNEL:
        case LatestAct::ACT_FIGHT_EVILS:
        case LatestAct::ACT_TREASURE:
        default:
            // 与旧周活动统一标准逐字一致：扫荡只看 result != 3
            // （ad5143f^:src/hook/activity_minigames.cpp:242
            //  `if (waitForResponse() && ACT685_STATE.endResult.load() != 3)`）。
            // AS3 各 Model 也都是 `if (params.result != 3)` 才读奖励：
            // ActModel.as:294-347（721/877）、ActModel.as:215-240（730）、
            // TreasureModel.as:79-136（880）。
            // 注意 11（冷却中）、12（次数不足）也会被这条宽松判据当成成功——
            // 所以调用方必须在扫荡前把冷却等干净（WaitForCooldown），
            // 不能靠结果码去兜。
            state.sweepReady = (result != 3);
            break;
    }
}

// end_game 响应：确认这一局真的被服务端结算了，而不是「包发出去了」就算完。
// 各活动首个字段都是结果码（名称不同：result / resultType），但语义不完全一样：
//   880 TreasureModel.as:137-146：result != 3 才读结算字段（3 = 校验失败/未通过）；
//   882 ActModel.as:161-193 同样只有 type == 3 是数据异常。
//   730 ActModel.decodeEndGame:142-167：resultType == 4 表示通关（isPass），其余仍会结算。
//   721/877/808 的 AS3 不判结果码，直接读结算字段。
// 因此这里只把「明确的失败码」判成失败：880/882 的 3。
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
        case LatestAct::ACT_TREASURE:
        case LatestAct::ACT_PUMPKINS_PK:
            state.endSettled = (result != 3);
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
        case LatestAct::ACT_ADVENTURE: {
            // 4 关联受伤离场，不是可以据此扫荡的满分标志。
            state.endSettled = false;
            state.advEndScore = -1;
            if (result != 0 && result != 4) break;
            reader.I32();  // playCount；流程通过 game_info 回查
            reader.I32();  // coolTimeLeft
            reader.I32();  // medal
            reader.I32();  // exp
            reader.I32();  // coin
            const int score = reader.I32();
            reader.I32();  // bestScore
            std::string bonusJson;
            if (reader.ok() && ReadBodyString(body, size, offset, bonusJson)) {
                state.advEndScore = score;
                state.endSettled = true;
            }
            break;
        }
        default:
            // 730 的 resultType 只是「是否通关」，不是「是否结算」，非 4 也可能正常发奖；
            // 877/808 的 AS3 完全不看这个字段（KBQiaoModel.decodeEndGame:175-183）。
            // 收到回包即视为已结算。
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

        case LatestAct::ACT_QIAO: {
            // [end_game, 0, checkCode, score, 15 项奖励计数]
            // KBQiaoControl.sendEndGame:62-76：list = [END_GAME, 0, checkCode, score]，
            // 再把 awardNumArr 的 15 项逐个接上；checkCode 是乘法变换（见 QiaoCheckCode）。
            // score 传的是 scoreArr[14]（KBBatttleView.as:491），也就是本次接到的计分怪
            // 数量，不是 desc.targetScore——808 的 max_score = 55 只是展示值。
            // state 为空（关残留会话）时按「0 分 + 15 项 0」这个合法空局发，shape 仍要完整：
            // 服务端按定长字段解包，缺字段的包会被当成坏包丢弃，关不掉残留会话。
            std::vector<int32_t> body{0, QiaoCheckCode(state ? state->qiaoRandomCode.load() : 0, seed)};
            body.push_back(state ? state->qiaoScore.load() : 0);
            if (state != nullptr && state->qiaoAwardReady.load()) {
                body.insert(body.end(), state->qiaoAwardNum.begin(), state->qiaoAwardNum.end());
            } else {
                AppendZeros(body, kQiaoTypeSlots);
            }
            return body;
        }

        case LatestAct::ACT_ADVENTURE:
            // [end_game, endFlag]：正常超时或主动退出时使用 0；
            // 成绩由 play_game 累计，客户端结算请求不包含目标分。
            return {0};

        default:
            return {score};
    }
}

// ---------------------------------------------------------------------------
// start_game 参数构造
// ---------------------------------------------------------------------------
std::vector<int32_t> BuildStartGameBody(const LatestActDesc& desc, LatestActState& state) {
    switch (desc.id) {
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
        case LatestAct::ACT_QIAO:
            // [start_game, popRule]，KBQiaoControl.sendStartGame:47-51：
            //   popRule = KBDataCenter.instance.isPop = open_ui 的第 8 个 int。
            return {state.ruleFlag.load()};
        case LatestAct::ACT_ADVENTURE:
            // game_info.isPop=0只决定先显示说明，不是本次开始请求的最终参数。
            // KBBatttleView.init将info.mc置于第1帧，startBtnHandler默认设置
            // ruleIntroduce=1后才toStart；isPop=1则直接沿用1开局。
            // 对齐该默认路径及手动正常对局抓包start_game [1]。
            return {1};
        default:
            return {};
    }
}

// start_game 被拒时的可读原因。
// 各活动的拒绝码不是同一套，只能逐活动翻译已核对过的部分：
//   721 ActModel.decodeBeginGame:127-148  → 1 次数用光 / 2 冷却中 / 3 还在游戏中
//   730 Act730ErrorCode.errorCodeStartGame → 10 游戏中 / 11 在冷却 / 12 没次数了 / 13 支付中
//   808 Act808ErrorCode.errorCodeStartGame → 与 882 完全相同的 10/11/12/13
//   883 KBAdventureModel.errorCodeStartGame → 与 882 完全相同的 10/11/12/13
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
    } else if (desc.id == LatestAct::ACT_PUMPKINS_PK || desc.id == LatestAct::ACT_QIAO ||
               desc.id == LatestAct::ACT_ADVENTURE) {
        // 882 Act882ErrorCode.errorCodeStartGame:13-40 与 808
        // Act808ErrorCode.errorCodeStartGame 是同一套数字，883
        // KBAdventureModel.errorCodeStartGame:29-56 又与前两者逐字相同：
        // 10 游戏中 / 11 在冷却 / 12 没次数了 / 13 支付中。
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

// 每次只按最新服务端棋盘提交一个动作；排列题可以需要多次交换。
std::vector<int32_t> SolveAdvMove(int type, const std::array<int, kAdvBoardSlots>& b) {
    switch (type) {
        case kAdvTypeOddOneOut: {
            int uniqueIndex = -1;
            for (int i = 0; i < kAdvBoardSlots; ++i) {
                int same = 0;
                for (int value : b) if (value == b[static_cast<std::size_t>(i)]) ++same;
                if (same == 1) {
                    if (uniqueIndex >= 0) return {};
                    uniqueIndex = i;
                } else if (same != kAdvBoardSlots - 1) {
                    return {};
                }
            }
            return uniqueIndex >= 0 ? std::vector<int32_t>{uniqueIndex + 1, 0}
                                    : std::vector<int32_t>{};
        }
        case kAdvTypeMatch: {
            int match = -1;
            for (int i = 1; i <= kAdvMatchCandidates; ++i) {
                if (b[static_cast<std::size_t>(i)] == b[0]) {
                    if (match >= 0) return {};
                    match = i;
                }
            }
            return match >= 0 ? std::vector<int32_t>{match + 1, 0}
                              : std::vector<int32_t>{};
        }
        case kAdvTypeArrange: {
            std::array<int, kAdvRowSlots> target{}, current{};
            std::copy_n(b.begin(), kAdvRowSlots, target.begin());
            std::copy_n(b.begin() + kAdvRowSlots, kAdvRowSlots, current.begin());
            auto sortedTarget = target;
            auto sortedCurrent = current;
            std::sort(sortedTarget.begin(), sortedTarget.end());
            std::sort(sortedCurrent.begin(), sortedCurrent.end());
            if (sortedTarget != sortedCurrent) return {};
            // AS3 每次提交两个下排槽号，不要求一次交换就整排归位。
            // 只修正首个错位，下一步重新使用回包；不预发剩余交换。
            for (int i = 0; i < kAdvRowSlots; ++i) {
                if (target[static_cast<std::size_t>(i)] == current[static_cast<std::size_t>(i)]) continue;
                for (int j = i + 1; j < kAdvRowSlots; ++j) {
                    if (current[static_cast<std::size_t>(j)] == target[static_cast<std::size_t>(i)] &&
                        current[static_cast<std::size_t>(j)] != target[static_cast<std::size_t>(j)]) {
                        return {kAdvRowSlots + i + 1, kAdvRowSlots + j + 1};
                    }
                }
                return {};
            }
            return {};
        }
        default:
            return {};
    }
}

// ---------------------------------------------------------------------------
// 一局游戏的过程包
//
// 只有真正需要在游戏过程中上报的活动才有内容；其余活动的过程全在本地（分数、
// 金币、星级都在客户端累计，结算时一次性回传），AS3 本身就不发中间包，这里也不该
// 凭空造包。凡是 AS3 有过程包的，必须逐条补上——start_game 直接接 end_game 等于
// 跳过整局，服务端算出来的成绩是空的。
//
// 节拍：包数包序照 AS3，间隔不照 AS3。全部旧周活动与本模块的实测结论一致——只有
// 极少数活动真的卡 AS3 时序，绝大多数活动把多个过程包按 200~300ms 连发就能正常
// 结算（730 用 200ms、808 用 300ms，都是这个档位）。所以默认 200~300ms，不要为了
// 「逼真」把一局铺满 AS3 的游戏时长：照抄 AS3 逐秒重放会让每局白等几十秒，整批活动
// 拖成几十分钟，而服务端并不会因此多认一分。升档的唯一依据是实测——跑完对 trace，
// 结算分 < 本地上报分或结算被拒，才逐档放慢并重测。目前唯一实测证明必须卡的是 721
// （见该分支注释），那是特例，新接活动没有实测依据时一律从 200~300ms 起步。
// ---------------------------------------------------------------------------
bool RunPlayProcess(const LatestActDesc& desc, LatestActState& state) {
    auto running = []() { return g_latestBatchRunning.load(); };

    switch (desc.id) {
        case LatestAct::ACT_STAR_PRINCESS: {
            // 721 采菊东篱下：过程包必须复现 AS3 真实一局的三类特征。
            // 这是全模块唯一实测证明必须卡 AS3 时序的分支，属于特例而非通则（节拍
            // 通则见函数头）。新接活动不要照抄这里的 90 秒。
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
            //      onTimer:416 计时走完才发 end_game。721 的服务端会按实时长核账
            //      （48 秒那版结算分只有上报的一半多），所以这一支必须撑满 90 秒；
            //      这是实测结论，不是所有活动的通用要求。
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

        case LatestAct::ACT_QIAO: {
            // 808 欢乐跷跷板：过程包是 [gameing, 类型 id, checkCode]，每接到一只怪发一条
            // （KBPerker.render:100-153 —— 命中后 sendGaming(this.type)，且只有非闪电类型
            // 才累加 awardNumArr）。
            //
            // 整局能接到什么完全由 start_game 下发的 180 项掉落表决定：
            //   KBBatttleView.updateTool:63-73  每 2 tick 调一次 createAI(4)，
            //                                   90 秒共 45 次 × 4 = 180，正好消费完整张表；
            //   createAI:144-167                按顺序取表项，type == 0 只消费不生成怪。
            // 所以这里严格按表顺序逐项发，表项为 0 的那一次没有怪、也就没有包。
            //
            // 闪电（type == kQiaoBoltType）由 createBolt 单独生成、不在掉落表里，它只扣血、
            // 不进 scoreArr/awardNumArr、也不发包（KBPerker.as:105-117）。表里真出现也必须
            // 跳过：awardNum 只有 kQiaoTypeSlots 格，30 会直接越界写。
            //
            // 结算只回传 awardNumArr 与 scoreArr[14]（KBQiaoControl.sendEndGame:68-73），
            // 因此本地只需两个累计量：每命中一只 ++awardNum[type-1]；只有计分类型才 +1 分。
            // 同一只怪被两名玩家同帧撞上时 AS3 会给 +2（KBPerker.as:132-141），本地无从
            // 复现是谁撞的，一律按 +1 报——宁可少报也不能虚报，虚报会被服务端判校验失败。
            if (!state.qiaoDropReady.load()) {
                Notify(desc.id, L"start_game 回包的 180 项掉落表没读全，无法复现过程包");
                return false;
            }
            const int seed = state.checkCode.load();
            const int randomCode = state.qiaoRandomCode.load();
            if (QiaoCheckCode(randomCode, seed) == 0) {
                // randomCode 为 0 时校验位恒为 0，每一条 gameing 都会被判校验失败。
                Notify(desc.id, L"没有拿到本局的 randomCode，过程包校验位无法计算");
                return false;
            }

            state.qiaoAwardNum.fill(0);
            state.qiaoAwardReady = false;
            int score = 0;
            for (int i = 0; i < kQiaoDrops; ++i) {
                if (!running() || ManagedThread::StopRequested()) {
                    return false;
                }
                const int type = state.qiaoDrops[static_cast<std::size_t>(i)];
                if (type <= 0 || type == kQiaoBoltType || type > kQiaoTypeSlots) {
                    continue;
                }
                if (!SendLatestPacket(desc.id, "gameing", {type, QiaoCheckCode(randomCode, seed)})) {
                    return false;
                }
                ++state.qiaoAwardNum[static_cast<std::size_t>(type - 1)];
                if (type == kQiaoScoreType) {
                    ++score;
                }
                if (!ManagedThread::SleepCancellable(kQiaoPacketGapMs)) {
                    return false;
                }
            }
            state.qiaoScore = score;
            // 顺序不能反：awardNum 先写满，ready 后置（release），结算侧才不会拿到半份表。
            state.qiaoAwardReady = true;
            return true;
        }

        default:
            // 877/880/882：AS3 整局都不发中间包，分数与奖励全在本地累计，
            // 结算时一次性回传（880 的金币、882 的 gameGetBonusList…）。
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
        const bool sent =
            SendLatestPacket(desc.id, "end_game", BuildEndGameBody(desc, 0, nullptr));
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
// 发一次 sweep_info 拿本次扫荡的奖励预览，与已下线的旧周活动 Act822ThreadProc
// （ad5143f^:src/hook/activity_minigames.cpp:841-842
//  「SendAct822SweepInfoPacket(); WaitForAct822Response() && g_act822SweepAvailable」）
// 同一形状。返回值只反映服务端对「预览」的态度，不扣次数，
// 不能当「有没有历史成绩」的判据——那个判据在 CanSweepDirectly 里本地算。
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
//   721 ActSweepWin.as:84             [SWEEP, _curCnt]
//   877 ActSweepWin.as:77             [SWEEP, _curCnt]
//   730 Act730SweepWin.as:79          [SWEEP, _curCnt]
//   880 SweepUI.as:80                 [GET_SWEEP, _curCnt]
//   882 ActSweepWin.as:61             [SWEEP, int(_changePanel.text)]
// 所以这里恒发一包、带次数，只等一次回包。
//
// 本函数只管「发 sweep + 等回包」，不负责前置的 sweep_info：两条调用路径都已经
// 各自按 AS3 顺序探过一次（③ 直接扫荡前探、④ 打完一局后探），再探一次纯属多发。
// 返回 true 表示这次扫荡被服务端接受（回包 result 表明 sweepReady）。
bool SweepRemaining(const LatestActDesc& desc, LatestActState& state) {
    const int count = state.playCount.load() > 0 ? state.playCount.load() : 1;
    if (!g_latestBatchRunning.load()) {
        return false;
    }

    // 扫荡被回 11 就是「冷却中」：结算后服务端会挂一个很短的冷却
    // （实测 880 结算后 open_ui 的 restCdTime = 1），此时扫荡必被拒。
    // 冷却秒数不可靠，唯一稳的做法是重试：隔 2 秒再发，最多 3 次。
    // 12（次数不足）与 3（校验失败）不重试，重试也只会再被拒一次。
    //
    // 冷却码有两套：730 一族（含 880）是 11，而 721 / 882 / 808 是另一套——
    // 721 Act721ErrorCode.errorCodeSweep、882 Act882ErrorCode.errorCodeSweep:117-140
    // 与 808 Act808ErrorCode.errorCodeSweep:307-330 都是
    // 3 = 冷却中、1 = 还没成功打完一局、2 = 次数不足。
    // 这套活动按 11 去重试等于永远不重试，冷却一过就整段放弃。
    const bool sweepErrorCodeSetB = (desc.id == LatestAct::ACT_STAR_PRINCESS ||
                                     desc.id == LatestAct::ACT_PUMPKINS_PK ||
                                     desc.id == LatestAct::ACT_QIAO);
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
        state.qiaoDropReady = false;
        state.qiaoAwardReady = false;
        state.qiaoScore = 0;
        state.qiaoRandomCode = 0;
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
        const bool sent =
            SendLatestPacket(desc.id, "end_game",
                             BuildEndGameBody(desc, state.checkCode.load(), &state));
        if (sent && WaitForResponse(state, kActionTimeoutMs + kWaitMarginMs)) {
            endSettled = true;
        }
    }
    if (!endSettled) {
        return false;
    }

    // 回包到了不等于服务端认了这一局：880/882 用 result == 3 表示校验失败，
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
        if (desc.id == LatestAct::ACT_ADVENTURE) {
            {
                std::lock_guard<std::mutex> lock(g_adventureProbeMutex);
                if (state.advInterference.load()) return false;
                state.playCount = -1;
            }
            bool answered = false;
            {
                AdventureRequest request(state, PendingOp::OpenUi, desc.openUiOp, {});
                answered = request.sent && WaitForResponse(state, kQueryTimeoutMs + kWaitMarginMs);
            }
            std::lock_guard<std::mutex> lock(g_adventureProbeMutex);
            if (state.advInterference.load()) return false;
            if (answered && state.playCount.load() >= 0) {
                ReportActivityCount(desc.id, state.playCount.load());
                return true;
            }
        } else {
            state.playCount = -1;
            {
                PendingOpGuard guard(state, PendingOp::OpenUi);
                if (!SendLatestPacket(desc.id, desc.openUiOp) ||
                    !WaitForResponse(state, kQueryTimeoutMs + kWaitMarginMs)) {
                    continue;
                }
            }
            // 响应处理器解析完成后才清除 waitingResponse。
            if (state.playCount.load() >= 0) {
                ReportActivityCount(desc.id, state.playCount.load());
                return true;
            }
        }
    }

    // 883 的回填与手动响应使用同一把锁，不能覆盖后续的未知状态。
    if (desc.id == LatestAct::ACT_ADVENTURE) {
        std::lock_guard<std::mutex> lock(g_adventureProbeMutex);
        state.playCount = -1;
        ReportActivityCount(desc.id, -1);
    } else {
        ReportActivityCount(desc.id, -1);
    }
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

// 883 不接统一满分/扫荡流程：成绩和奖励由服务端逐题累计。
bool RunAdventureUntilDone(const LatestActDesc& desc, LatestActState& state) {
    std::mt19937 moveGapGenerator(std::random_device{}());
    std::uniform_int_distribution<int> moveGapMs(kAdvMoveGapMinMs, kAdvMoveGapMaxMs);
    if (!RefreshOneActivity(desc, state)) {
        Notify(desc.id, L"获取活动信息失败");
        return false;
    }
    if (!WaitForCooldown(desc, state)) {
        Notify(desc.id, L"冷却未结束或任务已停止");
        return false;
    }

    int battleRuns = 0;
    int maxBattleRuns = 0;
    for (;;) {
        if (!g_latestBatchRunning.load() || ManagedThread::StopRequested() ||
            state.advInterference.load()) return false;
        const int before = state.playCount.load();
        if (before <= 0) {
            Notify(desc.id, before == 0 ? L"次数已用完" : L"次数未知，停止该活动");
            return before == 0;
        }
        if (battleRuns == 0) {
            maxBattleRuns = before + kAdvSafetyRunMargin;
        }
        if (battleRuns >= maxBattleRuns) {
            Notify(desc.id, L"服务端次数未按预期推进，达到安全上限，停止该活动");
            return false;
        }
        ++battleRuns;
        Notify(desc.id, L"开始对局，请勿手动操作风焰神殿；剩余 " + std::to_wstring(before) + L" 次");
        {
            std::lock_guard<std::mutex> lock(state.advMutex);
            state.advBoard = AdventureBoard{};
        }
        {
            std::lock_guard<std::mutex> lock(g_adventureProbeMutex);
            state.lastStartResult = -1;
            state.lastEndResult = -1;
            state.endSettled = false;
            state.advEndScore = -1;
            state.advEndReceived = false;
            state.advClosing = false;
            state.advAbortRound = false;
            state.advAbortResult = -1;
        }
        bool startSent = false;
        bool startAnswered = false;
        {
            AdventureRequest request(state, PendingOp::StartGame, "start_game",
                                     BuildStartGameBody(desc, state));
            startSent = request.sent;
            if (startSent) {
                startAnswered = WaitForCondition(
                    [&state]() { return !state.waitingResponse.load() || state.advInterference.load(); },
                    kActionTimeoutMs + kWaitMarginMs, 50) == WaitOutcome::Signalled;
            }
        }
        const int startResult = state.lastStartResult.load();
        if (state.advInterference.load()) {
            Notify(desc.id, L"收到未匹配的活动回包，已停止；请检查游戏当前状态");
            return false;
        }
        if (!startSent || (!startAnswered && startResult != 0)) {
            Notify(desc.id, L"开局状态未知，请检查游戏对局后再重试");
            return false;
        }
        if (startResult != 0) {
            Notify(desc.id, L"开局被拒绝（" + StartFailureText(desc, startResult) + L"）");
            return false;
        }

        const auto startedAt = std::chrono::steady_clock::now();
        int movesSent = 0;
        std::wstring lastMove;
        bool cleanRound = true;
        bool externalChange = false;
        while (g_latestBatchRunning.load() && !ManagedThread::StopRequested()) {
            if (state.advEndReceived.load()) break;
            if (state.advInterference.load()) {
                externalChange = true;
                cleanRound = false;
                break;
            }
            if (std::chrono::steady_clock::now() - startedAt >=
                std::chrono::milliseconds(kAdvRoundDurationMs)) break;
            AdventureBoard board;
            {
                std::lock_guard<std::mutex> lock(state.advMutex);
                board = state.advBoard;
            }
            if (!board.ready) {
                Notify(desc.id, L"棋盘回包不完整，结束本局并停止");
                cleanRound = false;
                break;
            }
            // iResult3 == 0 means the answer was wrong, not that the battle ended.
            // AS3 calls start() again while life remains; only exhaustion ends this loop.
            if (board.hp <= 0) {
                Notify(desc.id, L"生命耗尽，结束本局");
                break;
            }
            const auto args = SolveAdvMove(board.type, board.tiles);
            if (args.empty()) {
                Notify(desc.id, L"无法确定机关题的正确答案（type=" +
                                std::to_wstring(board.type) + L"，score=" +
                                std::to_wstring(board.score) + L"），结束本局并停止");
                cleanRound = false;
                break;
            }
            const auto sendNotBefore = std::chrono::steady_clock::now() +
                                      std::chrono::milliseconds(moveGapMs(moveGapGenerator));
            // 同时等服务端主动终局；停止或到时后不能因延迟队列再多发一步。
            bool sendReady = true;
            while (std::chrono::steady_clock::now() < sendNotBefore) {
                if (!g_latestBatchRunning.load() || ManagedThread::StopRequested() ||
                    state.advEndReceived.load() || state.advInterference.load() ||
                    std::chrono::steady_clock::now() - startedAt >=
                        std::chrono::milliseconds(kAdvRoundDurationMs)) {
                    sendReady = false;
                    break;
                }
                if (!ManagedThread::SleepCancellable(kAdvWaitSliceMs)) {
                    sendReady = false;
                    break;
                }
            }
            if (!sendReady) break;
            {
                std::lock_guard<std::mutex> lock(state.advMutex);
                state.advBoard.ready = false;
            }
            {
                AdventureRequest request(state, PendingOp::PlayGame, "play_game", args);
                if (!request.sent) {
                    // 发送锁下终局可能刚好先到；这时停止走子不是发包失败。
                    if (!state.advEndReceived.load()) cleanRound = false;
                    break;
                }
                ++movesSent;
                const auto sentAfterMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - startedAt).count();
                lastMove = L"开局后 " + std::to_wstring(sentAfterMs) + L"ms，type=" + std::to_wstring(board.type) +
                           L"，score=" + std::to_wstring(board.score) +
                           L"，hp=" + std::to_wstring(board.hp) + L"，board=[";
                for (std::size_t i = 0; i < board.tiles.size(); ++i) {
                    if (i != 0) lastMove += L",";
                    lastMove += std::to_wstring(board.tiles[i]);
                }
                lastMove += L"]，args=[" + std::to_wstring(args[0]) + L"," +
                            std::to_wstring(args[1]) + L"]";
                if (WaitForCondition(
                        [&state]() { return !state.waitingResponse.load() || state.advInterference.load(); },
                        kActionTimeoutMs + kWaitMarginMs, 50) != WaitOutcome::Signalled ||
                    state.advInterference.load()) {
                    cleanRound = false;
                    break;
                }
            }
            if (state.advEndReceived.load()) break;
        }
        if (!g_latestBatchRunning.load() || ManagedThread::StopRequested()) cleanRound = false;
        if (state.advInterference.load()) externalChange = true;
        if (externalChange) {
            Notify(desc.id, L"收到未匹配的活动回包，已停止；请检查游戏当前状态");
            return false;
        }

        // onEndBack 收到任意 end_game 后直接 game_info，不再次发送 end_game。
        // 只有本地超时、生命耗尽、停止或无法继续且尚未收到终局时才提交结束。
        {
            // 即使走子超时也先锁存收尾阶段，迟到的 play 不能再冒充外部干扰。
            std::lock_guard<std::mutex> lock(g_adventureProbeMutex);
            state.advClosing = true;
        }
        if (!state.advEndReceived.load()) {
            AdventureRequest request(state, PendingOp::EndGame, "end_game", {0}, true);
            if (request.sent) {
                // 即使全局停止也给收包线程有限时间完成结算确认。
                std::unique_lock<std::mutex> lock(g_adventureProbeMutex);
                g_adventureResponse.wait_for(
                    lock, std::chrono::milliseconds(kAdvCleanupWaitMs),
                    [&state]() { return !state.waitingResponse.load() || state.advInterference.load(); });
            }
        }
        if (state.advInterference.load()) {
            Notify(desc.id, L"收到未匹配的活动回包，已停止；请检查游戏当前状态");
            return false;
        }
        if (!state.endSettled.load() || !cleanRound || state.advAbortRound.load()) {
            const int cleanupResult = state.lastEndResult.load();
            // 与 onEndBack 一样，拒绝/截断回包也刷新次数；不能把次数减少当作奖励到账。
            const bool refreshed = g_latestBatchRunning.load() && !ManagedThread::StopRequested() &&
                                   RefreshOneActivity(desc, state);
            if (state.advAbortRound.load()) {
                // 记录被终局打断前实际提交的协议字段，避免把 result=3 猜成动画问题。
                const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - startedAt).count();
                Notify(desc.id, L"终局前已发送 " + std::to_wstring(movesSent) +
                       L" 次 play_game，耗时 " + std::to_wstring(elapsedMs) +
                       L"ms；最后请求：" + (lastMove.empty() ? L"无" : lastMove));
                Notify(desc.id, L"收到未确认的结束结果（result=" +
                       std::to_wstring(state.advAbortResult.load()) + L"，剩余次数=" +
                       (refreshed ? std::to_wstring(state.playCount.load()) : L"未知") +
                       L"），已停止；未计为成功结算");
            } else {
                Notify(desc.id, state.endSettled.load() ? L"对局已结束，自动执行中止" :
                       L"结算未确认（result=" + std::to_wstring(cleanupResult) +
                       L"），请检查游戏后再重试");
            }
            return false;
        }
        Notify(desc.id, L"本局已结算，服务端成绩 " + std::to_wstring(state.advEndScore.load()));
        // end_game 的处理器完成后，AS3 还会异步发送 game_info；第一次回查
        // 可能看到旧次数，不能把这种时序差异误判为本局失败。
        bool countAdvanced = false;
        int after = -1;
        for (int attempt = 0; attempt < kAdvSettlementRefreshAttempts; ++attempt) {
            if (attempt > 0 &&
                !ManagedThread::SleepCancellable(kAdvSettlementRefreshGapMs)) {
                return false;
            }
            if (RefreshOneActivity(desc, state)) {
                after = state.playCount.load();
                if (after >= 0 && after < before) {
                    countAdvanced = true;
                    break;
                }
            }
            if (attempt + 1 < kAdvSettlementRefreshAttempts) {
                Notify(desc.id, L"等待服务端更新结算次数...");
            }
        }
        if (!countAdvanced) {
            Notify(desc.id, L"结算后次数未确认减少，停止该活动");
            return false;
        }
        if (after == 0) {
            Notify(desc.id, L"次数已用完");
            return true;
        }
        if (!WaitForCooldown(desc, state)) {
            Notify(desc.id, L"冷却未结束或任务已停止");
            return false;
        }
    }
}

// 单活动循环执行：反复「扫荡 / 走一局」，直到次数耗尽、进入冷却或失败为止。
void RunOneActivityUntilDone(const LatestActDesc& desc) {
    const uint32_t activityId = desc.id;
    // 必须在扫荡判断之前拦截，不能仅靠 TryPlayOnce 的检查。
    if (!desc.playable) {
        Notify(activityId, L"暂不支持自动执行，请手动完成");
        return;
    }
    LatestActState* statePtr = StateOf(activityId);
    if (!statePtr) {
        return;
    }
    LatestActState& state = *statePtr;

    // 与次数探针互斥：二者共用同一响应通道（都会清 waitingResponse），
    // 抢不到占用就跳过本活动，避免探针的响应被这里误当成自己的回包。
    std::unique_lock<std::mutex> adventureLock(g_adventureProbeMutex, std::defer_lock);
    if (activityId == LatestAct::ACT_ADVENTURE) adventureLock.lock();
    int lockExpected = 0;
    if (!state.opLock.compare_exchange_strong(lockExpected, 1)) {
        Notify(activityId, L"正被其他流程占用，跳过");
        if (activityId == LatestAct::ACT_ADVENTURE) g_latestBatchHadErrors = true;
        return;
    }
    // 离开（含所有提前 return）时归还占用、清等待标志与期望回包。
    // pendingOp 必须一并归零：响应处理器只在 pendingOp 非 None 时才做匹配过滤，
    // 若残留一个旧值，后续次数探针的回包会被当成「不匹配」丢弃，次数就永远刷新不出来。
    struct OpLockGuard {
        LatestActState* s;
        ~OpLockGuard() {
            std::unique_lock<std::mutex> lock(g_adventureProbeMutex, std::defer_lock);
            if (s == StateOf(LatestAct::ACT_ADVENTURE)) lock.lock();
            // 先清 pendingOp、再放锁：锁一旦归零，次数探针随时可能接手，
            // 若此时 pendingOp 还残留旧值，探针的回包会被匹配逻辑丢弃，
            // 次数就会被误报成「不可用」。顺序不能颠倒。
            s->pendingOp = static_cast<uint32_t>(PendingOp::None);
            s->waitingResponse = false;
            int expected = 1;
            s->opLock.compare_exchange_strong(expected, 0);
        }
    } guard{&state};

    if (activityId == LatestAct::ACT_ADVENTURE) {
        state.advInterference = false;
        adventureLock.unlock();
        if (!RunAdventureUntilDone(desc, state)) g_latestBatchHadErrors = true;
        return;
    }

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
    // 统一标准流程——与历史周活动（822/826/827/641/684/631…，均已下线）同一条线，
    // 逐字对应 ad5143f^:src/hook/activity_minigames.cpp 里的 Act822ThreadProc：
    //
    //   ① open_ui / game_info：拿剩余次数、冷却、历史最好成绩
    //   ② 次数用完 -> 停；冷却中 -> 停
    //   ③ CanSweepDirectly 成立 -> 直接扫荡，一次扫完剩余次数。两档判据，都在本地判、
    //      不发包：有上限最高分常量 -> ShouldAutoSweep(最好成绩, 上限分)；没有上限分、
    //      回包只给「今天成功打完过一局」标志位 -> 标志位 > 0。sweep_info 的返回值
    //      一律不当闸门（服务端没成绩时也照常回包，拿它放行会白扣一次次数）。
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
        // 按 AS3 顺序补一包 sweep_info：各活动入口的扫荡按钮都是先发 SWEEP_INFO 拿
        // 奖励预览、再开扫荡窗点确认（如 882 ActEnterView.onSweep）。
        // 这一包只是预览、不扣次数，返回值**不能**当「有没有成绩」的判据——闸门已经由
        // open_ui 的权威字段给过了，这里只是照抄客户端的发包顺序。
        ProbeSweepReady(desc, state);
        const int before = state.playCount.load();
        const bool swept = SweepRemaining(desc, state);
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

    // 打一局 -> 扫荡剩余。正常只需一轮；但扫荡可能被服务端按冷却/次数不足拒掉，
    // 这时只能一局一局打到底——不能打一局就放弃，剩下的次数会白白浪费。
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
        } else {
            // 882 的 bestFlag / 808 的 historyBestScore 都是 open_ui 直接给出的权威值，
            // 0 = 今天还没成功打完过一局。打成一局后它变正数，此时再进循环只能是扫荡没成功。
            if (state.bestScore.load() > 0) {
                Notify(activityId, L"扫荡未成功，继续完整打一局...");
            } else {
                Notify(activityId, L"今天还没有成功打完过一局（bestScore=0），完整打一局...");
            }
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
        //
        // 这里的返回值**可以**当门槛，但问的已经不是同一个问题：走到这里说明本进程
        // 刚打完一整局，要确认的是「服务端有没有把这局记上账」，而不是「历史上有没有
        // 成绩」——后者由 CanSweepDirectly 在开局本地判完，从不问服务端。此时若服务端
        // 说没成绩，真发 sweep 会白扣一次次数，所以退回继续打一局是对的。
        // 反面用法（开局就拿这一包当闸门）见 CanSweepDirectly 的注释。
        if (!ProbeSweepReady(desc, state)) {
            Notify(activityId, L"服务端暂无可扫荡成绩，继续打下一局");
            continue;
        }

        Notify(activityId, L"扫荡剩余 " + std::to_wstring(state.playCount.load()) + L" 次...");
        const int beforeSweep = state.playCount.load();
        const bool swept = SweepRemaining(desc, state);

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
        // 721/882/808 用 2（Act721ErrorCode / Act882ErrorCode / Act808ErrorCode
        // 的 errorCodeSweep），730 一族（含 880）用 12。
        const bool errSetB = (desc.id == LatestAct::ACT_STAR_PRINCESS ||
                              desc.id == LatestAct::ACT_PUMPKINS_PK ||
                              desc.id == LatestAct::ACT_QIAO);
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

void QueryAdventureActivityCount(DWORD timeoutMs) {
    LatestActState& state = *StateOf(LatestAct::ACT_ADVENTURE);
    const LatestActDesc& desc = *FindDesc(LatestAct::ACT_ADVENTURE);
    struct ProbePendingGuard {
        LatestActState& state;
        ~ProbePendingGuard() {
            std::lock_guard<std::mutex> lock(g_adventureProbeMutex);
            if (g_adventureProbePending) {
                g_adventureProbePending = false;
                state.playCount = -1;
            }
        }
    } pendingGuard{state};
    bool sent = false;
    {
        // 登记和发送必须同锁，避免被手动操作取消后仍发出迟到查询。
        std::lock_guard<std::mutex> lock(g_adventureProbeMutex);
        g_adventureProbePending = true;
        state.playCount = -1;
        ReportActivityCount(desc.id, -1);
        sent = SendLatestPacket(desc.id, desc.openUiOp) != FALSE;
    }
    if (sent) {
        WaitForCondition([]() {
            std::lock_guard<std::mutex> lock(g_adventureProbeMutex);
            return !g_adventureProbePending;
        }, timeoutMs);
    }

    std::lock_guard<std::mutex> lock(g_adventureProbeMutex);
    if (!sent || g_adventureProbePending) {
        g_adventureProbePending = false;
        state.playCount = -1;
        ReportActivityCount(desc.id, -1);
    }
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
    if (desc.id == LatestAct::ACT_ADVENTURE && operation == "play_game") {
        return PendingOp::PlayGame;
    }
    return PendingOp::None;
}

const wchar_t* PendingOpName(PendingOp op) {
    switch (op) {
        case PendingOp::OpenUi: return L"game_info";
        case PendingOp::StartGame: return L"start_game";
        case PendingOp::PlayGame: return L"play_game";
        case PendingOp::EndGame: return L"end_game";
        case PendingOp::SweepInfo: return L"sweep_info";
        case PendingOp::Sweep: return L"sweep";
        case PendingOp::None: return L"无";
    }
    return L"未知";
}

void RecordAdventurePacketSend(const BYTE* data, size_t size, int sentBytes,
                               bool nativeRequest, uintptr_t socketId, const wchar_t* outcome) {
    std::lock_guard<std::mutex> lock(g_advTraceMutex);
    size_t offset = 0;
    ByteReader reader(data, size, offset);
    // 一次socket发送可包含多个帧；只记录883，其他帧不进入日志。
    while (reader.Remaining() >= PacketProtocol::HEADER_SIZE) {
        const size_t frameStart = offset;
        const uint16_t magic = reader.U16();
        const uint16_t bodyLength = reader.U16();
        const uint32_t opcode = reader.U32();
        const uint32_t params = reader.U32();
        if (magic != PacketProtocol::MAGIC_NORMAL || !reader.CanRead(bodyLength)) break;
        if (opcode == Opcode::ACTIVITY_QINGYANG_NEW_SEND && params == LatestAct::ACT_ADVENTURE) {
            AppendAdventureTraceLocked(
                std::wstring(nativeRequest ? L"SEND native " : L"SEND client ") + outcome +
                L" socket=" + std::to_wstring(socketId) + L" socketResult=" + std::to_wstring(sentBytes),
                data + frameStart, PacketProtocol::HEADER_SIZE + bodyLength);
        }
        reader.Skip(bodyLength);
    }
}

void RecordAdventurePacketReceive(const GamePacket& packet, uintptr_t socketId) {
    if (packet.opcode != Opcode::ACTIVITY_QUERY_BACK || packet.params != LatestAct::ACT_ADVENTURE) return;
    std::lock_guard<std::mutex> lock(g_advTraceMutex);
    AppendAdventureTraceLocked(L"RECV socket=" + std::to_wstring(socketId) +
        L" opcode=" + std::to_wstring(packet.opcode) + L" params=" +
        std::to_wstring(packet.params) + L" body", packet.body.data(), packet.body.size());
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

    const PendingOp op = PendingOpOf(*desc, operation);
    if (desc->id == LatestAct::ACT_ADVENTURE) {
        std::lock_guard<std::mutex> lock(g_adventureProbeMutex);
        const int owner = state->opLock.load();
        const auto pending = static_cast<PendingOp>(state->pendingOp.load());
        // AS3 按 operation 独立派发结束事件（KBAdventureModel.decodeEndGame）。
        // 在走子或间隔期收到它不代表外部干扰；结果码 3 也不能冒充 0/4 的有效 DTO。
        const bool roundStarted = state->lastStartResult.load() == 0;
        if (owner == 1 && roundStarted && op == PendingOp::EndGame &&
            (pending == PendingOp::PlayGame || pending == PendingOp::None ||
             pending == PendingOp::EndGame || state->advEndReceived.load() ||
             (pending == PendingOp::StartGame && !state->waitingResponse.load()))) {
            // 已确认的奖励快照不能被重复/迟到的拒绝回包覆盖。
            if (!state->endSettled.load()) {
                ParseEndGame(*desc, *state, packet, offset);
                state->advEndReceived = true;
                if (!state->endSettled.load() && !state->advAbortRound.load()) {
                    state->advAbortResult = state->lastEndResult.load();
                    state->advAbortRound = true;
                }
                state->playCount = -1;
                ReportActivityCount(desc->id, -1);
            }
            if (pending == PendingOp::PlayGame || pending == PendingOp::EndGame) {
                state->waitingResponse = false;
            }
            g_adventureResponse.notify_all();
            return;
        }
        // 原游戏 onEndBack 无条件发 game_info；它可能先于本地收尾回包到达。
        // 可以更新次数，但不能把它当作 end_game 的确认或取消收尾等待。
        if (owner == 1 && state->advEndReceived.load() && op == PendingOp::OpenUi &&
            pending != PendingOp::OpenUi) {
            ParseOpenUI(*desc, *state, packet, offset);
            ReportActivityCount(desc->id, state->playCount.load());
            return;
        }
        // 终局之后迟到的走子回包不能再发布棋盘或取消 game_info 的等待。
        if (owner == 1 && (state->advEndReceived.load() || state->advClosing.load()) &&
            op == PendingOp::PlayGame) return;
        if (owner == 1 && pending != PendingOp::None && pending == op &&
            state->waitingResponse.load()) {
            switch (op) {
                case PendingOp::OpenUi: ParseOpenUI(*desc, *state, packet, offset); break;
                case PendingOp::StartGame: ParseStartGame(*desc, *state, packet, offset); break;
                case PendingOp::PlayGame: ParsePlayGame(*state, packet, offset); break;
                case PendingOp::EndGame:
                    ParseEndGame(*desc, *state, packet, offset);
                    state->playCount = -1;
                    break;
                default: return;
            }
            state->waitingResponse = false;
            g_adventureResponse.notify_all();
            return;
        }
        if (owner == 2 && op == PendingOp::OpenUi && g_adventureProbePending) {
            state->playCount = -1;
            ParseOpenUI(*desc, *state, packet, offset);
            g_adventureProbePending = false;
            ReportActivityCount(desc->id, state->playCount.load());
        } else if (op == PendingOp::StartGame || op == PendingOp::PlayGame ||
                   op == PendingOp::EndGame || op == PendingOp::Sweep ||
                   operation == "buy_game_cnt") {
            // 未匹配回包可能是其他操作，也可能是重复或迟到的响应。
            // 协议没有序号，无法安全地推断局面；保留操作与阶段供排查。
            if (owner == 1 && !state->advInterference.exchange(true)) {
                const wchar_t* received = operation == "buy_game_cnt"
                    ? L"buy_game_cnt" : PendingOpName(op);
                const bool waiting = state->waitingResponse.load();
                ByteReader reader(packet.body, offset);
                const int result = reader.I32();
                const std::wstring resultText = reader.ok() ? std::to_wstring(result) : L"不完整";
                Notify(desc->id, L"收到未匹配回包（操作=" + std::wstring(received) +
                       L"，等待=" + PendingOpName(pending) +
                       L"，等待中=" + (waiting ? L"是" : L"否") +
                       L"，首字段=" + resultText + L"），请检查游戏状态");
                g_adventureResponse.notify_all();
            }
            g_adventureProbePending = false;
            state->playCount = -1;
            ReportActivityCount(desc->id, -1);
        }
        return;
    }

    // 不在本模块解析范围内的过程包不参与等待。
    if (op == PendingOp::None) {
        // 721 的 game_event 回音不解析、只计数：RunPlayProcess 用它做事件间节拍。
        if (operation == "game_event") {
            state->playEventAcks.fetch_add(1, std::memory_order_relaxed);
        }
        return;
    }

    // 不可自动执行的活动只处理次数查询；手动游戏的棋盘回包不能写入旧自动状态。
    if (!desc->playable && op != PendingOp::OpenUi) {
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
        case PendingOp::PlayGame:
            // 883 的走子回包只更新棋盘，不碰次数：次数由 end_game 结算后回查。
            ParsePlayGame(*state, packet, offset);
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

    // 请求中包含暂不支持的活动时拒绝整批，避免静默跳过后误报全部完成。
    std::vector<const LatestActDesc*> targets;
    for (uint32_t activityId : activityIds) {
        const LatestActDesc* desc = FindDesc(activityId);
        if (desc && !desc->playable) {
            UIBridge::Instance().UpdateHelperText(
                std::wstring(desc->name) + L"：暂不支持自动执行，请手动完成");
            return FALSE;
        }
        if (desc) {
            targets.push_back(desc);
        }
    }
    if (targets.empty()) {
        return FALSE;
    }

    {
        std::lock_guard<std::mutex> lock(g_latestBatchMutex);
        if (g_latestBatchActive) {
            UIBridge::Instance().UpdateHelperText(L"最新活动：一键执行进行中或正在收尾，请稍候");
            return FALSE;
        }
        g_latestBatchActive = true;
        g_latestBatchRunning = true;
        ++g_latestBatchGeneration;
    }

    // 工作线程自己释放 active；即使状态通知抛异常也必须放行等待者。
    bool started = false;
    try {
        started = ManagedThread::Start(L"latest_activities", [targets]() {
        LatestBatchCompletion completion;
        bool failed = false;
        g_latestBatchHadErrors = false;
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

        const bool hadErrors = g_latestBatchHadErrors;
        bool cancelled = false;
        {
            std::lock_guard<std::mutex> lock(g_latestBatchMutex);
            cancelled = !g_latestBatchRunning.load() || ManagedThread::StopRequested();
            g_latestBatchRunning = false;
        }
        if (failed) {
            UIBridge::Instance().UpdateHelperText(L"最新活动：一键执行异常中止");
        } else if (cancelled) {
            UIBridge::Instance().UpdateHelperText(L"最新活动：一键执行已停止");
        } else if (hadErrors) {
            UIBridge::Instance().UpdateHelperText(L"最新活动：部分活动未完成，请查看活动状态");
        } else {
            UIBridge::Instance().UpdateHelperText(L"最新活动：一键执行完成");
        }
        });
    } catch (...) {
        started = false;
    }
    if (!started) {
        LatestBatchCompletion completion;
        UIBridge::Instance().UpdateHelperText(L"最新活动：启动线程失败");
        return FALSE;
    }
    return TRUE;
}

void RequestStopLatestActivities() {
    std::lock_guard<std::mutex> lock(g_latestBatchMutex);
    if (g_latestBatchRunning.exchange(false)) {
        UIBridge::Instance().UpdateHelperText(L"最新活动：正在停止...");
    }
}

void StopLatestActivities() {
    std::unique_lock<std::mutex> lock(g_latestBatchMutex);
    const uint64_t generation = g_latestBatchGeneration;
    if (g_latestBatchRunning.exchange(false)) {
        UIBridge::Instance().UpdateHelperText(L"最新活动：正在停止...");
    }
    g_latestBatchStopped.wait(lock, [generation]() {
        return g_latestBatchCompleted >= generation;
    });
}
