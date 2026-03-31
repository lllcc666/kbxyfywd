#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "packet_types.h"

namespace BattleSix {
    constexpr int ACTIVITY_ID = 999;
    constexpr int MAX_SPIRIT_COUNT = 6;
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
    int m_currentSkillIndex;
    int m_enemySid;
    int m_enemyUniqueId;
    int m_myUniqueId;
    bool m_isInBattle;
    bool m_autoBattleEnabled;
    bool m_autoMatching;
    int m_matchCount;
    int m_totalMatchCount;
    int m_winCount;
    int m_loseCount;

public:
    BattleSixAutoBattle();

    void StartBattle();
    void EndBattle();
    bool IsInBattle() const { return m_isInBattle; }
    void SetAutoBattle(bool enabled) { m_autoBattleEnabled = enabled; }
    bool IsAutoBattleEnabled() const { return m_autoBattleEnabled; }
    void SetAutoMatching(bool enabled) { m_autoMatching = enabled; }
    bool IsAutoMatching() const { return m_autoMatching; }

    void SetMatchCount(int count) { m_matchCount = count; m_totalMatchCount = count; m_winCount = 0; m_loseCount = 0; }
    int GetMatchCount() const { return m_matchCount; }
    int GetTotalMatchCount() const { return m_totalMatchCount; }
    void DecrementMatchCount() { if (m_matchCount > 0) m_matchCount--; }
    void IncrementWinCount() { m_winCount++; }
    void IncrementLoseCount() { m_loseCount++; }
    int GetWinCount() const { return m_winCount; }
    int GetLoseCount() const { return m_loseCount; }

    void UpdateMySpiritHP(int spiritSid, int hp);
    void UpdateEnemySpiritHP(int spiritSid, int hp);
    bool IsMySpiritBySid(int sid) const;
    int FindNextAliveSpirit(int currentIndex);
    void RefreshEnemyTarget();

    BOOL OnBattleRoundStart();
    void OnBattleRoundResult(const GamePacket& packet);
    BOOL AutoSwitchSpirit();
    int GetAliveSpiritCount();
    int GetEnemyAliveSpiritCount();

    int SelectBestSkill();

    std::vector<BattleSixSpiritInfo>& GetMySpirits() { return m_mySpirits; }
    std::vector<BattleSixSpiritInfo>& GetEnemySpirits() { return m_enemySpirits; }
    int GetCurrentSpiritIndex() const { return m_currentSpiritIndex; }
    int GetEnemySid() const { return m_enemySid; }
    int GetEnemyUniqueId() const { return m_enemyUniqueId; }
    int GetMyUniqueId() const { return m_myUniqueId; }
    void SetMyUniqueId(int id) { m_myUniqueId = id; }
    void SetEnemySid(int id) { m_enemySid = id; }
    void SetEnemyUniqueId(int id) { m_enemyUniqueId = id; }
    void SetCurrentSpiritIndex(int index) { m_currentSpiritIndex = index; }
};

extern BattleSixAutoBattle g_battleSixAuto;
extern std::atomic<bool> g_battleSixMatching;
extern std::atomic<bool> g_battleSixMatchSuccess;
extern std::atomic<int> g_battleSixSwitchTargetId;
extern std::atomic<int> g_battleSixSwitchRetryCount;
extern std::atomic<unsigned long long> g_battleSixBattleSession;
extern std::atomic<unsigned long long> g_battleSixRoundToken;

BOOL SendBattleSixCombatInfoPacket();
BOOL SendBattleSixMatchPacket();
BOOL SendBattleSixCancelMatchPacket();
BOOL SendBattleSixReqStartPacket();
BOOL SendBattleSixUserOpPacket(int opType, int param1, int param2);
BOOL SendBattleSixEndPacket();

void ProcessBattleSixMatchResponse(const GamePacket& packet);
void ProcessBattleSixPrepareCombatResponse(const GamePacket& packet);
void ProcessBattleSixReqStartResponse(const GamePacket& packet);
void ProcessBattleSixCombatInfoResponse(const GamePacket& packet);
void ProcessBattleSixBattleRoundResultResponse(const GamePacket& packet);
void ProcessBattleSixBattleEndResponse(const GamePacket& packet);

BOOL SendOneKeyBattleSixPacket(int matchCount = 1);
BOOL SendCancelBattleSixPacket();
