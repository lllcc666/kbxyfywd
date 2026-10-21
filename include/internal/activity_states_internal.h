#pragma once

#include "wpe_hook.h"

#include <utility>

struct ActivityState {
    std::atomic<bool> isRunning{false};
    std::atomic<bool> waitingResponse{false};
    std::atomic<int> playCount{0};
    std::atomic<int> restTime{0};
    std::atomic<int> checkCode{0};
    std::atomic<bool> sweepAvailable{false};

    virtual ~ActivityState() = default;

    virtual void Reset() {
        isRunning = false;
        waitingResponse = false;
        playCount = 0;
        restTime = 0;
        checkCode = 0;
        sweepAvailable = false;
    }
};

struct Act778State : ActivityState {
    std::atomic<int> bestScore{0};
    std::atomic<bool> useSweep{false};
    std::atomic<bool> sweepSuccess{false};
    std::atomic<int> gameTime{0};

    void Reset() override {
        ActivityState::Reset();
        bestScore = 0;
        useSweep = false;
        sweepSuccess = false;
        gameTime = 0;
    }
};

struct Act685State : ActivityState {
    std::atomic<int> bubbleNum{0};
    std::atomic<int> ruleFlag{0};
    std::atomic<int> maxScore{0};
    std::atomic<int> passCount{0};
    std::atomic<int> passBonusFlag{0};
    std::atomic<int> startResult{0};
    std::atomic<int> endResult{0};
    std::atomic<int> rewardBubbleNum{0};
    std::atomic<int> rewardExp{0};
    std::atomic<int> rewardCoin{0};
    std::atomic<int> catchCnt{0};
    std::atomic<bool> useSweep{false};
    std::vector<int> catchList;
    std::vector<std::pair<int, int>> passBonusList;
    std::vector<int> propList;

    void Reset() override {
        ActivityState::Reset();
        bubbleNum = 0;
        ruleFlag = 0;
        maxScore = 0;
        passCount = 0;
        passBonusFlag = 0;
        startResult = 0;
        endResult = 0;
        rewardBubbleNum = 0;
        rewardExp = 0;
        rewardCoin = 0;
        catchCnt = 0;
        useSweep = false;
        catchList.assign(2, 0);
        passBonusList.assign(3, std::pair<int, int>{0, 0});
        propList.assign(3, 0);
    }
};

struct Act666State : ActivityState {
    std::atomic<int> medalCount{0};
    std::atomic<int> passCount{0};
    std::atomic<int> getAwardFlag{0};
    std::atomic<int> bestFlag{0};
    std::atomic<int> startResult{0};
    std::atomic<int> rewardMedalCount{0};
    std::atomic<int> rewardExp{0};
    std::atomic<int> rewardCoin{0};
    std::atomic<int> catchId{0};
    std::atomic<bool> useSweep{false};
    std::vector<int> rewardList;
    std::vector<int> catchList;

    void Reset() override {
        ActivityState::Reset();
        medalCount = 0;
        passCount = 0;
        getAwardFlag = 0;
        bestFlag = 0;
        startResult = 0;
        rewardMedalCount = 0;
        rewardExp = 0;
        rewardCoin = 0;
        catchId = 0;
        useSweep = false;
        rewardList.assign(4, 0);
        catchList.assign(2, 0);
    }
};

struct Act641State : ActivityState {
    std::atomic<int> medalCount{0};
    std::atomic<int> passCount{0};
    std::atomic<int> awardFlag{0};
    std::atomic<int> bestRecord{0};
    std::atomic<int> promptFlag{0};
    std::atomic<int> targetScore{0};
    std::atomic<int> startResult{0};
    std::atomic<int> endResult{0};
    std::atomic<int> rewardMedalCount{0};
    std::atomic<int> rewardExp{0};
    std::atomic<int> rewardCoin{0};
    std::atomic<int> catchPetId{0};
    std::atomic<bool> useSweep{false};
    std::atomic<bool> sweepSuccess{false};
    std::vector<int> bonusList;
    std::vector<int> catchList;

    void Reset() override {
        ActivityState::Reset();
        medalCount = 0;
        passCount = 0;
        awardFlag = 0;
        bestRecord = 0;
        promptFlag = 0;
        targetScore = 0;
        startResult = 0;
        endResult = 0;
        rewardMedalCount = 0;
        rewardExp = 0;
        rewardCoin = 0;
        catchPetId = 0;
        useSweep = false;
        sweepSuccess = false;
        bonusList.assign(6, 0);
        catchList.assign(2, 0);
    }
};

struct Act684State : ActivityState {
    std::atomic<int> bubbleNum{0};
    std::atomic<int> rewardCount{0};
    std::atomic<int> todayCount{0};
    std::atomic<int> ruleFlag{0};
    std::atomic<int> drawFlag{0};
    std::atomic<int> maxScore{0};
    std::atomic<int> targetScore{0};
    std::atomic<int> startResult{0};
    std::atomic<int> endResult{0};
    std::atomic<int> rewardMedalCount{0};
    std::atomic<int> rewardExp{0};
    std::atomic<int> rewardCoin{0};
    std::atomic<bool> useSweep{false};
    std::atomic<bool> sweepSuccess{false};
    std::vector<int> catchList;

    void Reset() override {
        ActivityState::Reset();
        bubbleNum = 0;
        rewardCount = 0;
        todayCount = 0;
        ruleFlag = 0;
        drawFlag = 0;
        maxScore = 0;
        targetScore = 0;
        startResult = 0;
        endResult = 0;
        rewardMedalCount = 0;
        rewardExp = 0;
        rewardCoin = 0;
        useSweep = false;
        sweepSuccess = false;
        catchList.assign(2, 0);
    }
};

struct Act717State : ActivityState {
    std::atomic<int> totalBadgeNum{0};
    std::atomic<int> promptFlag{0};
    std::atomic<int> startResult{0};
    std::atomic<int> endResult{0};
    std::atomic<int> sweepResult{0};
    std::atomic<bool> useSweep{false};
    std::atomic<bool> sweepSuccess{false};
    std::vector<int> catchList;
    std::vector<std::pair<int, int>> rewardList;

    void Reset() override {
        ActivityState::Reset();
        totalBadgeNum = 0;
        promptFlag = 0;
        startResult = 0;
        endResult = 0;
        sweepResult = 0;
        useSweep = false;
        sweepSuccess = false;
        catchList.assign(2, 0);
        rewardList.clear();
    }
};

struct Act805State : ActivityState {
    std::atomic<int> medalNum{0};
    std::atomic<int> historyBestScore{0};
    std::atomic<int> lastScore{0};
    std::atomic<int> isPop{0};
    std::atomic<int> targetScore{0};
    std::atomic<int> startResult{0};
    std::atomic<int> rewardMedalCount{0};
    std::atomic<int> rewardExp{0};
    std::atomic<int> rewardCoin{0};
    std::atomic<bool> useSweep{false};
    std::atomic<bool> sweepSuccess{false};

    void Reset() override {
        ActivityState::Reset();
        medalNum = 0;
        historyBestScore = 0;
        lastScore = 0;
        isPop = 0;
        targetScore = 0;
        startResult = 0;
        rewardMedalCount = 0;
        rewardExp = 0;
        rewardCoin = 0;
        useSweep = false;
        sweepSuccess = false;
    }
};

struct Act757State : ActivityState {
    std::atomic<int> totalBadgeNum{0};
    std::atomic<int> buyCnt{0};
    std::atomic<int> passCount{0};
    std::atomic<int> isPop{0};
    std::atomic<int> historyBestScore{0};
    std::atomic<int> lastScore{0};
    std::atomic<int> passFlag{0};
    std::atomic<int> catchId{0};
    std::atomic<int> checkCode{0};
    std::atomic<int> startResult{0};
    std::atomic<int> endResult{0};
    std::atomic<int> sweepResult{0};
    std::atomic<bool> useSweep{false};
    std::atomic<bool> sweepSuccess{false};
    std::vector<int> passAwards;
    std::vector<std::pair<int, int>> catchList;
    std::vector<std::pair<int, int>> sweepAwards;
    std::vector<std::pair<int, int>> resultAwards;

    void Reset() override {
        ActivityState::Reset();
        totalBadgeNum = 0;
        buyCnt = 0;
        passCount = 0;
        isPop = 0;
        historyBestScore = 0;
        lastScore = 0;
        passFlag = 0;
        catchId = 0;
        checkCode = 0;
        startResult = 0;
        endResult = 0;
        sweepResult = 0;
        useSweep = false;
        sweepSuccess = false;
        passAwards.assign(3, 0);
        catchList.assign(1, {0, 0});
        sweepAwards.clear();
        resultAwards.clear();
    }
};

struct Act822State : ActivityState {
    std::atomic<int> playCount{0};
    std::atomic<int> restTime{0};
    std::atomic<int> totalBadgeNum{0};
    std::atomic<int> buyCnt{0};
    std::atomic<int> passCount{0};
    std::atomic<int> isPop{0};
    std::atomic<int> historyBestScore{0};
    std::atomic<int> lastScore{0};
    std::atomic<int> passFlag{0};
    std::atomic<int> catchId{0};
    std::atomic<int> checkCode{0};
    std::atomic<int> startResult{0};
    std::atomic<int> endResult{0};
    std::atomic<int> sweepResult{0};
    std::atomic<bool> useSweep{false};
    std::atomic<bool> sweepSuccess{false};
    std::vector<int> passAwards;
    std::vector<std::pair<int, int>> catchList;
    std::vector<std::pair<int, int>> sweepAwards;
    std::vector<std::pair<int, int>> resultAwards;

    void Reset() override {
        ActivityState::Reset();
        playCount = 0;
        restTime = 0;
        totalBadgeNum = 0;
        buyCnt = 0;
        passCount = 0;
        isPop = 0;
        historyBestScore = 0;
        lastScore = 0;
        passFlag = 0;
        catchId = 0;
        checkCode = 0;
        startResult = 0;
        endResult = 0;
        sweepResult = 0;
        useSweep = false;
        sweepSuccess = false;
        passAwards.assign(3, 0);
        catchList.assign(1, {0, 0});
        sweepAwards.clear();
        resultAwards.clear();
    }
};

struct Act826State : ActivityState {
    std::atomic<int> bubbleNum{0};
    std::atomic<int> passCount{0};
    std::atomic<int> ruleFlag{0};
    std::atomic<int> maxScore{0};
    std::atomic<int> lastScore{0};
    std::atomic<int> passFlag{0};
    std::atomic<int> petFlag{0};
    std::atomic<int> itemFlag{0};
    std::atomic<int> catchId{0};
    std::atomic<int> catchLimitCur{0};
    std::atomic<int> catchLimitMax{0};
    std::atomic<int> startResult{0};
    std::atomic<int> endResult{0};
    std::atomic<int> sweepResult{0};
    std::atomic<int> rewardMedal{0};
    std::atomic<int> rewardExp{0};
    std::atomic<int> rewardCoin{0};
    std::atomic<bool> isMoveControl{false};
    std::atomic<bool> useSweep{false};
    std::atomic<bool> sweepSuccess{false};

    void Reset() override {
        ActivityState::Reset();
        bubbleNum = 0;
        passCount = 0;
        ruleFlag = 0;
        maxScore = 0;
        lastScore = 0;
        passFlag = 0;
        petFlag = 0;
        itemFlag = 0;
        catchId = 0;
        catchLimitCur = 0;
        catchLimitMax = 0;
        startResult = 0;
        endResult = 0;
        sweepResult = 0;
        rewardMedal = 0;
        rewardExp = 0;
        rewardCoin = 0;
        isMoveControl = false;
        useSweep = false;
        sweepSuccess = false;
    }
};

struct Act827State : ActivityState {
    std::atomic<int> totalBadgeNum{0};
    std::atomic<int> promptFlag{0};
    std::atomic<int> flag{0};
    std::atomic<int> bestRecord{0};
    std::atomic<int> monsterId{0};
    std::atomic<int> checkCode{0};
    std::atomic<int> startResult{0};
    std::atomic<int> endResult{0};
    std::atomic<int> sweepResult{0};
    std::atomic<int> rewardScore{0};
    std::atomic<int> rewardMedalNum{0};
    std::atomic<int> rewardExp{0};
    std::atomic<int> rewardCoin{0};
    std::atomic<int> lastScore{0};
    std::atomic<bool> useSweep{false};
    std::atomic<bool> sweepSuccess{false};
    std::vector<int> catchList;

    void Reset() override {
        ActivityState::Reset();
        totalBadgeNum = 0;
        promptFlag = 0;
        flag = 0;
        bestRecord = 0;
        monsterId = 0;
        checkCode = 0;
        startResult = 0;
        endResult = 0;
        sweepResult = 0;
        rewardScore = 0;
        rewardMedalNum = 0;
        rewardExp = 0;
        rewardCoin = 0;
        lastScore = 0;
        useSweep = false;
        sweepSuccess = false;
        catchList.assign(2, 0);
    }
};

struct Act631State : ActivityState {
    std::atomic<int> bubbleNum{0};
    std::atomic<int> flag{0};
    std::atomic<int> passCount{0};
    std::atomic<int> maxScore{0};
    std::atomic<int> ruleFlag{0};
    std::atomic<int> isPass{2};
    std::atomic<int> score{0};
    std::atomic<int> isVip{0};
    std::atomic<int> isFirst{0};
    std::atomic<int> startResult{0};
    std::atomic<int> endResult{0};
    std::atomic<int> sweepResult{0};
    std::atomic<int> exp{0};
    std::atomic<int> coin{0};
    std::atomic<int> rewardBubbleNum{0};
    std::atomic<int> rewardExp{0};
    std::atomic<int> rewardCoin{0};
    std::atomic<bool> useSweep{false};
    std::atomic<bool> sweepSuccess{false};
    std::vector<int> rewardList;
    std::vector<int> catchList;

    void Reset() override {
        ActivityState::Reset();
        bubbleNum = 0;
        flag = 0;
        passCount = 0;
        maxScore = 0;
        ruleFlag = 0;
        isPass = 2;
        score = 0;
        isVip = 0;
        isFirst = 0;
        startResult = 0;
        endResult = 0;
        sweepResult = 0;
        exp = 0;
        coin = 0;
        rewardBubbleNum = 0;
        rewardExp = 0;
        rewardCoin = 0;
        useSweep = false;
        sweepSuccess = false;
        rewardList.assign(3, 0);
        catchList.assign(2, 0);
    }
};

enum HorseRoomStatus {
    HORSE_ROOM_FREE = 0,
    HORSE_ROOM_READY = 1,
    HORSE_ROOM_GAMESTART = 2,
    HORSE_ROOM_INGAME = 3,
    HORSE_ROOM_SETTLE = 4
};

enum HorseCompetitionPhase {
    HORSE_PHASE_IDLE = 0,
    HORSE_PHASE_FETCHING_UI_INFO,
    HORSE_PHASE_JOINING_ROOM,
    HORSE_PHASE_WAITING_START,
    HORSE_PHASE_GAMING,
    HORSE_PHASE_SETTLING,
    HORSE_PHASE_FINISHED
};

enum HorseItemType {
    HORSE_ITEM_MUEN = 1,
    HORSE_ITEM_JIE = 2,
    HORSE_ITEM_HEILEI = 3,
    HORSE_ITEM_LIUJIAN = 4,
    HORSE_ITEM_HUOLIDOU = 5
};

struct HorseMemberInfo {
    int room_id = 0;
    uint32_t player_id = 0;
    std::wstring player_name;
    int world_id = 0;
    int status = 0;
    int sex = 0;
    int join_time = 0;
    int settle_time = 0;
    int distance = 0;
    int cheat = 0;
    int rank = 0;
    int horse_id = 0;
    int horse_Iid = 0;
    int horse_base_Hp = 0;
    int horse_base_speed = 0;
    int horse_base_intimate = 0;
    double cost_time = 0.0;
    std::vector<int> flag;

    void Reset() {
        room_id = 0;
        player_id = 0;
        player_name.clear();
        world_id = 0;
        status = 0;
        sex = 0;
        join_time = 0;
        settle_time = 0;
        distance = 0;
        cheat = 0;
        rank = 0;
        horse_id = 0;
        horse_Iid = 0;
        horse_base_Hp = 0;
        horse_base_speed = 0;
        horse_base_intimate = 0;
        cost_time = 0.0;
        flag.clear();
    }
};

struct HorseCompetitionState : ActivityState {
    static constexpr int ROUTE_DISTANCE = 1500;

    std::atomic<bool> inRoom{false};
    std::atomic<bool> isGaming{false};
    std::atomic<bool> isFinished{false};
    std::atomic<bool> isSettling{false};
    std::atomic<bool> receivedEndGame{false};
    std::atomic<bool> localFinished{false};
    std::atomic<bool> abnormalRoundEnd{false};
    // 房间已失效：收到自己的 EXIT_ROOM 回包，或服务器把 ROOM_STATUS 推成 FREE
    // （AS3 HorseModel.as:192-195 收到 FREE 会派发 HORSE_SHOW_CLOSE_BTN，即房间已关闭）。
    // 用于确认退房成功、以及让等待循环提前结束，不必死等超时。
    std::atomic<bool> roomGone{false};
    // 本工具正在等服务器开局。迟到的 START_GAME（上一局超时退房后才补到的）
    // 必须被拒绝，否则会起一个「幽灵局」往已经不存在的房间继续发 PLAY_GAME，
    // 而外层白等满整个结算预算。
    std::atomic<bool> awaitingStart{false};
    // 已发出 EXIT_ROOM、正在等确认。只有这个窗口内的退房回包才算数——
    // 上一局迟到的回包否则会把下一局的等待当场打断。
    std::atomic<bool> awaitingExit{false};
    std::atomic<int> phase{HORSE_PHASE_IDLE};

    std::atomic<int> cnt{0};
    std::atomic<int> isRide{0};
    std::atomic<int> day{0};
    std::atomic<int> resDayPoint{0};
    std::atomic<int> exchangeNum{0};
    std::atomic<bool> uiInfoReceived{false};
    // ui_info 的请求/应答序号：回包必须晚于这次请求才算新鲜数据。
    // 只有一个 bool 的话，「上一轮迟到的回包」与「本轮真正的回包」无法区分，
    // 结算前预取点数时会拿到赛前的旧值（见 horse_competition.cpp 的 pendingDayPoint）。
    std::atomic<int> uiInfoSeq{0};
    std::atomic<int> uiInfoAckSeq{0};
    // 「有一次 ui_info 请求还没被应答」。只有它为真时到达的回包才算新鲜数据：
    // 单靠 ack = 当前 seq 无法区分「本轮回包」与「上一轮迟到的回包」（两者到达时
    // 的 seq 都等于当前值），那会让点数刷满后仍继续刷。
    std::atomic<bool> uiInfoPending{false};

    // roomId 会被收包线程（ApplyHorseRoomStateFromJson）和一键工作线程
    // （ResetHorseRoundState）同时读写，必须是 atomic，否则是数据竞争（UB）。
    std::atomic<int> roomId{0};
    std::atomic<int> roomStatus{HORSE_ROOM_FREE};
    double startTime = 0.0;

    HorseMemberInfo myInfo;
    std::vector<std::pair<int, int>> exchangeLimits;
    std::vector<int> itemDistances;

    double hp = 1000.0;
    double maxHp = 1000.0;
    double speed = 10.0;
    double accSpeed = 15.0;
    // 本局看门狗（自开始计时的秒数）：只用于「彻底卡死就中止本局」的兜底，
    // 不参与节奏。由 StartHorseCompetitionGame 按这只坐骑自己的理论时长 × 1.8
    // 设置（下限 60 秒），正常对局永远不会触发（详见 horse_competition.cpp 顶部注释）。
    double raceTargetSeconds = 0.0;
    // 看门狗触发标记：卡死到超时也不许伪造 distance，只中止本局让外层重试。
    std::atomic<bool> raceAborted{false};
    // distance 由比赛线程每帧积分，一键线程要读它算进度百分比与「是否真跑完」，
    // 裸 double 的并发读写是数据竞争（可能读到撕裂值，把正常局判成提前结束）。
    std::atomic<double> distance{0.0};
    std::string state = "RUN";
    std::string lastState = "RUN";
    std::atomic<bool> isDie{false};
    std::vector<int> items;

    std::atomic<bool> stopRequested{false};

    void Reset() override {
        ActivityState::Reset();
        inRoom = false;
        isGaming = false;
        isFinished = false;
        isSettling = false;
        receivedEndGame = false;
        localFinished = false;
        abnormalRoundEnd = false;
        roomGone = false;
        awaitingStart = false;
        awaitingExit = false;
        phase = HORSE_PHASE_IDLE;
        cnt = 0;
        isRide = 0;
        day = 0;
        resDayPoint = 0;
        exchangeNum = 0;
        uiInfoReceived = false;
        uiInfoSeq = 0;
        uiInfoAckSeq = 0;
        uiInfoPending = false;
        roomId = 0;
        stopRequested = false;
        roomStatus = HORSE_ROOM_FREE;
        startTime = 0.0;
        myInfo.Reset();
        exchangeLimits.clear();
        itemDistances.clear();
        hp = 1000.0;
        maxHp = 1000.0;
        speed = 10.0;
        accSpeed = 15.0;
        raceTargetSeconds = 0.0;
        raceAborted = false;
        distance = 0.0;
        state = "RUN";
        lastState = "RUN";
        isDie = false;
        items.clear();
    }
};

// 活动业务状态唯一 owner：统一持有并重置各活动状态，避免分散维护。
class ActivityStateManager {
public:
    static ActivityStateManager& Instance();

    Act778State& GetAct778State();
    Act685State& GetAct685State();
    Act666State& GetAct666State();
    Act757State& GetAct757State();
    Act822State& GetAct822State();
    Act826State& GetAct826State();
    Act827State& GetAct827State();
    Act641State& GetAct641State();
    Act684State& GetAct684State();
    Act717State& GetAct717State();
    Act805State& GetAct805State();
    Act631State& GetAct631State();
    HorseCompetitionState& GetHorseCompetitionState();

private:
    ActivityStateManager() = default;
    ~ActivityStateManager() = default;
    ActivityStateManager(const ActivityStateManager&) = delete;
    ActivityStateManager& operator=(const ActivityStateManager&) = delete;

    Act778State m_act778State;
    Act685State m_act685State;
    Act666State m_act666State;
    Act757State m_act757State;
    Act822State m_act822State;
    Act826State m_act826State;
    Act827State m_act827State;
    Act641State m_act641State;
    Act684State m_act684State;
    Act717State m_act717State;
    Act805State m_act805State;
    Act631State m_act631State;
    HorseCompetitionState m_horseCompetitionState;
};
