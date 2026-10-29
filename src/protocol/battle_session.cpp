#include "battle_session.h"

#include <utility>

BattleSession& BattleSession::Instance() {
    static BattleSession instance;
    return instance;
}

uint64_t BattleSession::Begin(BattleData battle) {
    std::lock_guard<std::mutex> lock(m_mutex);
    battle.active = true;
    m_battle = std::move(battle);
    return ++m_sessionId;
}

void BattleSession::Replace(BattleData battle) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // 已结束的会话不再接受“active=true”的迟到包，避免重新打开旧战斗。
    if (!m_battle.active && battle.active) return;
    battle.active = m_battle.active;
    m_battle = std::move(battle);
}

uint64_t BattleSession::End(bool releaseEntities) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (releaseEntities) {
        m_battle = BattleData{};
    } else {
        // 服务端 END：保留实体给 FSPK/NEWEXP 结算，但立即标记非战斗。
        m_battle.active = false;
    }
    // 清空 UI 的 sessionId 必须比战斗中的快照更大；否则已经排队的旧
    // updateBattleUI(旧快照) 会和 clearBattleUI 竞争，把面板重新画出来。
    return ++m_sessionId;
}

void BattleSession::Reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_battle = BattleData{};
    ++m_sessionId;
}

BattleData BattleSession::Snapshot(uint64_t* sessionId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (sessionId) *sessionId = m_sessionId;
    return m_battle;
}

bool BattleSession::IsActive() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_battle.active;
}
