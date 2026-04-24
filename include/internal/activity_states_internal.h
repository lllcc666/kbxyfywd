#pragma once

#include "wpe_hook.h"

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

struct StrawberryState : ActivityState {
    std::atomic<int> ruleFlag{0};
    std::atomic<int> redBerryCount{0};
    std::atomic<bool> useSweep{false};
    std::atomic<int> awardType{0};
    std::vector<int> awardArr;
    std::vector<int> toolArr;

    void Reset() override {
        ActivityState::Reset();
        ruleFlag = 0;
        redBerryCount = 0;
        useSweep = false;
        awardType = 0;
        awardArr.clear();
        toolArr.clear();
    }
};

struct TrialState : ActivityState {
    std::atomic<int> gameCount{0};
    std::atomic<int> awardNum{0};
    std::atomic<bool> complete{false};
    std::atomic<bool> waitingResponse{false};

    void Reset() override {
        ActivityState::Reset();
        gameCount = 0;
        awardNum = 0;
        complete = false;
        waitingResponse = false;
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

struct Act793State : ActivityState {
    std::atomic<int> bestScore{0};
    std::atomic<int> medalCount{0};
    std::atomic<int> targetMedals{0};
    std::atomic<bool> useSweep{false};
    std::atomic<bool> sweepSuccess{false};

    void Reset() override {
        ActivityState::Reset();
        bestScore = 0;
        medalCount = 0;
        targetMedals = 0;
        useSweep = false;
        sweepSuccess = false;
    }
};

struct Act791State : ActivityState {
    std::atomic<int> medalNum{0};
    std::atomic<int> bestScore{0};
    std::atomic<int> lastScore{0};
    std::atomic<int> superEvolutionFlag{0};
    std::atomic<int> targetScore{0};
    std::atomic<bool> useSweep{false};
    std::atomic<bool> sweepSuccess{false};

    void Reset() override {
        ActivityState::Reset();
        medalNum = 0;
        bestScore = 0;
        lastScore = 0;
        superEvolutionFlag = 0;
        targetScore = 0;
        useSweep = false;
        sweepSuccess = false;
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
    static constexpr int SYNC_INTERVAL = 60;

    std::atomic<bool> inRoom{false};
    std::atomic<bool> isGaming{false};
    std::atomic<bool> isFinished{false};
    std::atomic<bool> isSettling{false};
    std::atomic<bool> receivedEndGame{false};
    std::atomic<bool> localFinished{false};
    std::atomic<bool> abnormalRoundEnd{false};
    std::atomic<int> phase{HORSE_PHASE_IDLE};

    std::atomic<int> cnt{0};
    std::atomic<int> isRide{0};
    std::atomic<int> day{0};
    std::atomic<int> resDayPoint{0};
    std::atomic<int> exchangeNum{0};
    std::atomic<bool> uiInfoReceived{false};

    int roomId = 0;
    std::atomic<int> roomStatus{HORSE_ROOM_FREE};
    double updateTime = 0.0;
    double startTime = 0.0;

    HorseMemberInfo myInfo;
    std::vector<HorseMemberInfo> otherMembers;
    std::vector<std::pair<uint32_t, int>> rankList;
    std::vector<std::pair<int, int>> exchangeLimits;
    std::vector<int> itemDistances;

    double hp = 1000.0;
    double maxHp = 1000.0;
    double speed = 10.0;
    double accSpeed = 15.0;
    double distance = 0.0;
    std::string state = "RUN";
    std::string lastState = "RUN";
    std::atomic<bool> canControl{true};
    std::atomic<bool> isDie{false};
    int syncCount = 0;
    std::vector<int> items;

    std::atomic<bool> useTempMount{true};
    std::atomic<bool> useItems{false};
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
        phase = HORSE_PHASE_IDLE;
        cnt = 0;
        isRide = 0;
        day = 0;
        resDayPoint = 0;
        exchangeNum = 0;
        uiInfoReceived = false;
        roomId = 0;
        useTempMount = true;
        useItems = false;
        stopRequested = false;
        roomStatus = HORSE_ROOM_FREE;
        updateTime = 0.0;
        startTime = 0.0;
        myInfo.Reset();
        otherMembers.clear();
        rankList.clear();
        exchangeLimits.clear();
        itemDistances.clear();
        hp = 1000.0;
        maxHp = 1000.0;
        speed = 10.0;
        accSpeed = 15.0;
        distance = 0.0;
        state = "RUN";
        lastState = "RUN";
        canControl = true;
        isDie = false;
        syncCount = 0;
        items.clear();
    }
};

// 活动业务状态唯一 owner：统一持有并重置各活动状态，避免分散维护。
class ActivityStateManager {
public:
    static ActivityStateManager& Instance();

    StrawberryState& GetStrawberryState();
    TrialState& GetTrialState();
    Act778State& GetAct778State();
    Act666State& GetAct666State();
    Act805State& GetAct805State();
    Act793State& GetAct793State();
    Act791State& GetAct791State();
    HorseCompetitionState& GetHorseCompetitionState();
    void ResetAll();

private:
    ActivityStateManager() = default;
    ~ActivityStateManager() = default;
    ActivityStateManager(const ActivityStateManager&) = delete;
    ActivityStateManager& operator=(const ActivityStateManager&) = delete;

    StrawberryState m_strawberryState;
    TrialState m_trialState;
    Act778State m_act778State;
    Act666State m_act666State;
    Act805State m_act805State;
    Act793State m_act793State;
    Act791State m_act791State;
    HorseCompetitionState m_horseCompetitionState;
};
