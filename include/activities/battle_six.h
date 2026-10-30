#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include "packet_types.h"
#include "battle_packets.h"

namespace BattleSix {
    constexpr int ACTIVITY_ID = 999;
}

struct BattleSixSkillInfo {
    int skillId = 0;
    int power = 0;
    int currentPP = 0;
    int maxPP = 0;
    int skillType = 0;
    int element = 0;
    bool available = false;
    std::wstring name;
};

struct BattleSixSpiritInfo {
    int sid = 0;
    int spiritId = 0;
    int uniqueId = 0;
    int userId = 0;
    int hp = 0;
    int maxHp = 0;
    int level = 0;
    int element = 0;
    int position = 0;
    bool isDead = false;
    std::wstring name;
    std::vector<BattleSixSkillInfo> skills;
};

class BattleSixAutoBattle {
private:
    std::vector<BattleSixSpiritInfo> m_mySpirits;
    std::vector<BattleSixSpiritInfo> m_enemySpirits;
    int m_currentSpiritIndex;
    int m_enemySid;
    int m_enemyActiveIndex;
    unsigned long long m_lastActionRoundToken;
    mutable std::recursive_mutex m_stateMutex;
    std::atomic<bool> m_isInBattle;
    // 这些开关/计数会被 UI 线程写、包处理线程和延迟线程读；普通 bool/int
    // 在跨线程读写时是数据竞争，可能让自动战斗误判“已停止/还在战斗”。
    std::atomic<bool> m_autoBattleEnabled;
    std::atomic<bool> m_autoMatching;
    std::atomic<int> m_matchCount;
    std::atomic<int> m_totalMatchCount;
    std::atomic<int> m_winCount;
    std::atomic<int> m_loseCount;
public:
    BattleSixAutoBattle();

    void StartBattle();
    void EndBattle();
    bool IsInBattle() const { return m_isInBattle.load(); }
    void SetAutoBattle(bool enabled) { m_autoBattleEnabled.store(enabled); }
    bool IsAutoBattleEnabled() const { return m_autoBattleEnabled.load(); }
    void SetAutoMatching(bool enabled) { m_autoMatching.store(enabled); }
    bool IsAutoMatching() const { return m_autoMatching.load(); }

    void SetMatchCount(int count) {
        m_matchCount.store(count);
        m_totalMatchCount.store(count);
        m_winCount.store(0);
        m_loseCount.store(0);
    }
    int GetMatchCount() const { return m_matchCount.load(); }
    int GetTotalMatchCount() const { return m_totalMatchCount.load(); }
    void DecrementMatchCount() {
        int current = m_matchCount.load();
        while (current > 0 && !m_matchCount.compare_exchange_weak(current, current - 1)) {
        }
    }
    void IncrementWinCount() { m_winCount.fetch_add(1); }
    void IncrementLoseCount() { m_loseCount.fetch_add(1); }
    int GetWinCount() const { return m_winCount.load(); }
    int GetLoseCount() const { return m_loseCount.load(); }

    void UpdateMySpiritHP(int spiritSid, int hp);
    void UpdateEnemySpiritHP(int spiritSid, int hp);
    bool IsMySpiritBySid(int sid) const;
    int FindNextAliveSpirit(int currentIndex);
    void RefreshEnemyTarget();

    BOOL OnBattleRoundStart();
    void OnBattleRoundResult(const GamePacket& packet);
    BOOL AutoSwitchSpirit();
    int GetAliveSpiritCount();

    int SelectBestSkill();

    std::vector<BattleSixSpiritInfo>& GetMySpirits() { return m_mySpirits; }
    std::recursive_mutex& GetStateMutex() { return m_stateMutex; }
    std::vector<BattleSixSpiritInfo>& GetEnemySpirits() { return m_enemySpirits; }
    void SetEnemyActiveIndex(int index) { m_enemyActiveIndex = index; }
    void SetCurrentSpiritIndex(int index) { m_currentSpiritIndex = index; }
};
extern BattleSixAutoBattle g_battleSixAuto;
extern std::atomic<bool> g_battleSixMatching;
extern std::atomic<int> g_battleSixSwitchTargetId;
extern std::atomic<unsigned long long> g_battleSixBattleSession;
extern std::atomic<unsigned long long> g_battleSixRoundToken;

extern std::atomic<bool> g_battleSixSettlementKnown;
BOOL SendBattleSixCombatInfoPacket();
BOOL SendBattleSixMatchPacket();
extern std::atomic<uint32_t> g_battleSixSettlementFlags;
BOOL SendBattleSixCancelMatchPacket();
BOOL SendBattleSixReqStartPacket();
BOOL SendBattleSixUserOpPacket(int opType, int param1, int param2);
BOOL SendBattleSixEndPacket();

void ProcessBattleSixCancelMatchResponse(const GamePacket& packet);
void ProcessBattleSixPrepareCombatResponse(const GamePacket& packet);
void ProcessBattleSixMatchResponse(const GamePacket& packet);
void ProcessBattleSixReqStartResponse(const GamePacket& packet);
void ProcessBattleSixCombatInfoResponse(const GamePacket& packet);
void ProcessBattleSixBattleRoundResultResponse(const GamePacket& packet);
void ProcessBattleSixBattleEndResponse(const GamePacket& packet);
void ProcessBattleSixOtherEscapedResponse(const GamePacket& packet);
void ProcessBattleSixCombatOverResponse(const GamePacket& packet);
void ProcessBattleSixSelfBanSpiritResponse(const GamePacket& packet);
void ProcessBattleSixFinishFirstSpiritResponse(const GamePacket& packet);

BOOL StartOneKeyBattleSixPacket(int matchCount = 1);
BOOL CancelBattleSixMatch();

// 协议层战斗事件入口：由 battle_activity_hooks.cpp 调用，packet_parser 不直接
// 依赖 BattleSixAutoBattle 的内部状态。
void BattleSixSyncFromBattle(const BattleData& battle, bool initialize);
void BattleSixNotifyRoundStart();
void BattleSixMarkSettlement(uint32_t winFlags);

// ---------------------------------------------------------------------------
// 万妖盛会流程机制（实现在 src/hook/battle_six.cpp）
//
// 阶段与 token 用来让"迟到线程"和"迟到的回包"自行作废：每次推进阶段 token+1，
// 旧的看门狗/恢复线程拿到 token 不匹配就直接退出，不必再靠 Sleep 猜时序。
// 主线任务的战斗收尾复用 SendNormalBattleEndPacket / SendBattleReadyPacket /
// SendBattlePlayOverPacket 三个通用战斗封包，因此它们也是外部链接。
// ---------------------------------------------------------------------------
enum BattleSixFlowStage {
    BATTLESIX_FLOW_IDLE = 0,
    BATTLESIX_FLOW_MATCHING = 1,
    BATTLESIX_FLOW_WAITING_BATTLE_START = 2,
    BATTLESIX_FLOW_PREPARING_COMBAT = 3
};

extern std::atomic<int> g_battleSixFlowStage;
extern std::atomic<unsigned long long> g_battleSixFlowToken;
extern std::atomic<bool> g_battleSixReadySupplementSent;
extern std::atomic<unsigned long long> g_battleSixRoundResultToken;
extern std::atomic<unsigned long long> g_battleSixPlayOverToken;

unsigned long long AdvanceBattleSixFlowStage(int stage);
void ResetBattleSixFlowState();
void ScheduleBattleSixRecoveryViaCombatInfo(DWORD delayMs);
void ArmBattleSixFlowWatchdog(DWORD timeoutMs, int stage, unsigned long long token,
                              bool cancelBeforeRecovery, const wchar_t* timeoutText);

// 通用战斗封包（万妖盛会与主线任务共用）。
