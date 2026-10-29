#include <windows.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "battle_six.h"
#include "hook_shared.h"
#include "managed_thread.h"
#include "packet_builder.h"
#include "packet_parser.h"
#include "packet_protocol.h"
#include "ui_bridge.h"
#include "utils.h"
#include "wpe_hook.h"
#include "wpe_hook_internal.h"

// ============================================================================
// 万妖盛会流程机制（阶段/token/看门狗/PP 修正）
// ============================================================================

// 万妖盛会流程 owner：阶段、token 和结算标记统一由该流程维护。
std::atomic<bool> g_battleSixMatching{false};  ///< 是否正在匹配
std::atomic<bool> g_battleSixMatchSuccess{false};  ///< 匹配是否成功
std::atomic<int> g_battleSixSwitchTargetId{-1};  ///< 切换目标精灵uniqueId
std::atomic<int> g_battleSixSwitchRetryCount{0};  ///< 切换重试次数
std::atomic<unsigned long long> g_battleSixBattleSession{0};  ///< 当前战斗会话号，避免延迟线程串场

std::atomic<int> g_battleSixFlowStage{BATTLESIX_FLOW_IDLE};
std::atomic<unsigned long long> g_battleSixFlowToken{0};
constexpr uint32_t BATTLE_SIX_SETTLEMENT_TEXT_BACK = 1317130;  // 万妖盛会胜负文本弹窗回包
constexpr uint32_t BATTLE_SIX_SETTLEMENT_PVP_BACK = 1317154;   // 万妖盛会 PVP 结算弹窗回包
std::atomic<bool> g_battleSixPostSettlementEndSent{false};
std::atomic<bool> g_battleSixReadySupplementSent{false};
std::atomic<unsigned long long> g_battleSixRoundToken{0};
std::atomic<unsigned long long> g_battleSixRoundResultToken{0};
std::atomic<unsigned long long> g_battleSixPlayOverToken{0};
std::atomic<bool> g_battleSixSettlementKnown{false};
std::atomic<uint32_t> g_battleSixSettlementFlags{0};

unsigned long long AdvanceBattleSixFlowStage(int stage) {
    g_battleSixFlowStage = stage;
    return g_battleSixFlowToken.fetch_add(1) + 1;
}

void ResetBattleSixFlowState() {
    g_battleSixMatching = false;
    g_battleSixMatchSuccess = false;
    g_battleSixFlowStage = BATTLESIX_FLOW_IDLE;
    g_battleSixFlowToken.fetch_add(1);
}

void ScheduleBattleSixRecoveryViaCombatInfo(DWORD delayMs) {
    DWORD* pDelay = new DWORD(delayMs);
    HANDLE hThread = CreateManagedThread(nullptr, 0, [](LPVOID param) -> DWORD {
        std::unique_ptr<DWORD> delay(static_cast<DWORD*>(param));
        Sleep(*delay);
        if (!g_battleSixAuto.IsAutoMatching() || g_battleSixAuto.IsInBattle()) {
            return 0;
        }
        if (g_battleSixFlowStage.load() != BATTLESIX_FLOW_IDLE) {
            return 0;
        }
        UIBridge::Instance().UpdateHelperText(L"万妖盛会：重新查询并继续匹配...");
        SendBattleSixCombatInfoPacket();
        return 0;
    }, pDelay, 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
    } else {
        delete pDelay;
    }
}

static void AdjustBattleSixSpiritSkillPP(std::vector<BattleSixSpiritInfo>& spirits,
                                         int spiritSid,
                                         int skillId,
                                         int delta) {
    if (skillId <= 0 || delta == 0) {
        return;
    }

    for (auto& spirit : spirits) {
        if (spirit.sid != spiritSid) {
            continue;
        }

        for (auto& skill : spirit.skills) {
            if (skill.skillId != skillId) {
                continue;
            }

            const int nextPP = skill.currentPP + delta;
            if (skill.maxPP > 0) {
                if (nextPP < 0) {
                    skill.currentPP = 0;
                } else if (nextPP > skill.maxPP) {
                    skill.currentPP = skill.maxPP;
                } else {
                    skill.currentPP = nextPP;
                }
            } else {
                skill.currentPP = (nextPP < 0) ? 0 : nextPP;
            }
            skill.available = (skill.currentPP > 0);
            return;
        }

        return;
    }
}

void ArmBattleSixFlowWatchdog(
    DWORD timeoutMs,
    int stage,
    unsigned long long token,
    bool cancelBeforeRecovery,
    const wchar_t* timeoutText) {
    struct BattleSixWatchdogContext {
        DWORD timeoutMs;
        int stage;
        unsigned long long token;
        bool cancelBeforeRecovery;
        std::wstring timeoutText;
    };

    auto* context = new BattleSixWatchdogContext{timeoutMs, stage, token, cancelBeforeRecovery, timeoutText};
    HANDLE hThread = CreateManagedThread(nullptr, 0, [](LPVOID param) -> DWORD {
        std::unique_ptr<BattleSixWatchdogContext> context(static_cast<BattleSixWatchdogContext*>(param));
        Sleep(context->timeoutMs);

        if (!g_battleSixAuto.IsAutoMatching() || g_battleSixAuto.IsInBattle()) {
            return 0;
        }
        if (g_battleSixFlowToken.load() != context->token ||
            g_battleSixFlowStage.load() != context->stage) {
            return 0;
        }

        UIBridge::Instance().UpdateHelperText(context->timeoutText);
        if (context->cancelBeforeRecovery) {
            SendBattleSixCancelMatchPacket();
        } else {
            ResetBattleSixFlowState();
        }
        ScheduleBattleSixRecoveryViaCombatInfo(1500);
        return 0;
    }, context, 0, nullptr);

    if (hThread) {
        CloseHandle(hThread);
    } else {
        delete context;
    }
}

// ============================================================================
// 万妖盛会PVP功能实现 (BattleSix)
// ============================================================================

// BattleSixAutoBattle 类实现
BattleSixAutoBattle::BattleSixAutoBattle()
    : m_currentSpiritIndex(0)
    , m_currentSkillIndex(0)
    , m_enemySid(0)
    , m_enemyUniqueId(0)
    , m_enemyActiveIndex(-1)
    , m_myUniqueId(0)
    , m_lastActionRoundToken(0)
    , m_isInBattle(false)
    , m_autoBattleEnabled(false)
    , m_autoMatching(false)
    , m_matchCount(0)
    , m_totalMatchCount(0)
    , m_winCount(0)
    , m_loseCount(0) {
}
void BattleSixAutoBattle::UpdateCombatInfo(
    int winStreak,
    int teamNum,
    int currentCount,
    int firstSpiritId,
    std::vector<BattleSixLineupEntry> lineup) {
    std::lock_guard<std::recursive_mutex> stateLock(m_stateMutex);
    m_winStreak = winStreak;
    m_teamNum = teamNum;
    m_lineupCurrentCount = currentCount;
    m_firstSpiritId = firstSpiritId;
    m_lineup = std::move(lineup);
}

void BattleSixAutoBattle::StartBattle() {
    std::lock_guard<std::recursive_mutex> stateLock(m_stateMutex);
    m_isInBattle.store(true);
    m_currentSpiritIndex = 0;
    m_currentSkillIndex = 0;
    m_lastActionRoundToken = 0;
    g_battleSixBattleSession.fetch_add(1);
    g_battleSixRoundToken = 0;
    g_battleSixRoundResultToken = 0;
    g_battleSixPlayOverToken = 0;
    g_battleSixPostSettlementEndSent = false;
    g_battleSixSettlementKnown = false;
    g_battleSixSettlementFlags = 0;
    g_battleSixSwitchTargetId = -1;
    g_battleSixSwitchRetryCount = 0;
    UIBridge::Instance().UpdateHelperText(L"战斗开始");
}

void BattleSixAutoBattle::EndBattle() {
    std::lock_guard<std::recursive_mutex> stateLock(m_stateMutex);
    m_isInBattle.store(false);
    m_mySpirits.clear();
    m_enemySpirits.clear();
    m_currentSpiritIndex = -1;
    m_enemySid = 0;
    m_enemyUniqueId = 0;
    m_enemyActiveIndex = -1;
    g_battleSixSettlementKnown = false;
    g_battleSixSettlementFlags = 0;
    m_myUniqueId = 0;
    g_battleSixBattleSession.fetch_add(1);
    g_battleSixSwitchTargetId = -1;
    g_battleSixSwitchRetryCount = 0;
    UIBridge::Instance().UpdateHelperText(L"战斗结束");
}

void BattleSixAutoBattle::UpdateMySpiritHP(int spiritSid, int hp) {
    std::lock_guard<std::recursive_mutex> stateLock(m_stateMutex);
    for (auto& spirit : m_mySpirits) {
        if (spirit.sid == spiritSid) {
            spirit.hp = hp;
            spirit.isDead = (hp <= 0);
            break;
        }
    }
}

void BattleSixAutoBattle::UpdateEnemySpiritHP(int spiritSid, int hp) {
    std::lock_guard<std::recursive_mutex> stateLock(m_stateMutex);
    for (auto& spirit : m_enemySpirits) {
        if (spirit.sid == spiritSid) {
            spirit.hp = hp;
            spirit.isDead = (hp <= 0);
            break;
        }
    }

    RefreshEnemyTarget();
}

bool BattleSixAutoBattle::IsMySpiritBySid(int sid) const {
    std::lock_guard<std::recursive_mutex> stateLock(m_stateMutex);
    for (const auto& spirit : m_mySpirits) {
        if (spirit.sid == sid) {
            return true;
        }
    }
    return false;
}

void BattleSixAutoBattle::RefreshEnemyTarget() {
    std::lock_guard<std::recursive_mutex> stateLock(m_stateMutex);
    m_enemySid = 0;
    m_enemyUniqueId = 0;
    if (m_enemyActiveIndex >= 0 && m_enemyActiveIndex < static_cast<int>(m_enemySpirits.size())) {
        const auto& active = m_enemySpirits[m_enemyActiveIndex];
        if (!active.isDead && active.hp > 0) {
            m_enemySid = active.sid;
            m_enemyUniqueId = active.uniqueId;
            return;
        }
    }
    for (size_t i = 0; i < m_enemySpirits.size(); ++i) {
        const auto& spirit = m_enemySpirits[i];
        if (!spirit.isDead && spirit.hp > 0) {
            m_enemyActiveIndex = static_cast<int>(i);
            m_enemySid = spirit.sid;
            m_enemyUniqueId = spirit.uniqueId;
            break;
        }
    }
}

int BattleSixAutoBattle::FindNextAliveSpirit(int currentIndex) {
    std::lock_guard<std::recursive_mutex> stateLock(m_stateMutex);
    for (int i = 0; i < (int)m_mySpirits.size(); i++) {
        int index = (currentIndex + i) % m_mySpirits.size();
        if (!m_mySpirits[index].isDead && m_mySpirits[index].hp > 0) {
            return index;
        }
    }
    return -1;  // 所有精灵都已死亡
}

BOOL BattleSixAutoBattle::OnBattleRoundStart() {
    std::lock_guard<std::recursive_mutex> stateLock(m_stateMutex);
    if (!m_autoBattleEnabled.load()) {
        return FALSE;
    }

    const unsigned long long battleSession = g_battleSixBattleSession.load();
    const unsigned long long roundToken = g_battleSixRoundToken.load();
    if (roundToken == 0 || m_lastActionRoundToken == roundToken) {
        return FALSE;
    }
    const BattleData battleSnapshot = PacketParser::GetCurrentBattleSnapshot();
    if (!battleSnapshot.roundChangeSids.empty()) {
        m_lastActionRoundToken = roundToken;
        return AutoSwitchSpirit();
    }

    // 每个回合只提交一次动作，避免重复收到回合开始包时重复出招。
    if (m_currentSpiritIndex >= 0 && m_currentSpiritIndex < static_cast<int>(m_mySpirits.size())) {
        const auto& currentSpirit = m_mySpirits[m_currentSpiritIndex];
        if (currentSpirit.isDead || currentSpirit.hp <= 0) {
            m_lastActionRoundToken = roundToken;
            return AutoSwitchSpirit();
        }
    }

    const int skillIndex = SelectBestSkill();
    if (skillIndex < 0 || m_currentSpiritIndex < 0 ||
        m_currentSpiritIndex >= static_cast<int>(m_mySpirits.size())) {
        return FALSE;
    }

    const auto& skill = m_mySpirits[m_currentSpiritIndex].skills[skillIndex];
    int targetId = 0;
    int skillRange = 0;
    skillRange = GetSkillRange(skill.skillId, 0);
    if (skillRange == 2) {
        targetId = m_mySpirits[m_currentSpiritIndex].sid;
    } else {
        targetId = m_enemySid;
        if (targetId <= 0) {
            RefreshEnemyTarget();
            targetId = m_enemySid;
        }
    }
    if (targetId <= 0 || skill.skillId <= 0) {
        return FALSE;
    }

    m_lastActionRoundToken = roundToken;

    struct SkillThreadData {
        unsigned long long battleSession;
        unsigned long long roundToken;
        unsigned long long roundResultToken;
        int skillId;
        int targetId;
    };
    auto* threadData = new SkillThreadData{
        battleSession,
        roundToken,
        g_battleSixRoundResultToken.load(),
        skill.skillId,
        targetId
    };
    HANDLE hThread = CreateManagedThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
        std::unique_ptr<SkillThreadData> data(static_cast<SkillThreadData*>(lpParam));
        Sleep(600);
        if (g_battleSixBattleSession.load() != data->battleSession ||
            g_battleSixRoundToken.load() != data->roundToken ||
            g_battleSixRoundResultToken.load() != data->roundResultToken ||
            !g_battleSixAuto.IsInBattle()) {
            return 0;
        }
        SendBattleSixUserOpPacket(0, data->targetId, data->skillId);
        return 0;
    }, threadData, 0, nullptr);

    if (hThread) {
        CloseHandle(hThread);
        return TRUE;
    }
    delete threadData;
    m_lastActionRoundToken = 0;
    return FALSE;
}

void BattleSixAutoBattle::OnBattleRoundResult(const GamePacket& packet) {
    std::lock_guard<std::recursive_mutex> stateLock(m_stateMutex);
    // packet.params 表示 cmdType
    // 0 = 普通攻击, 1 = 切换宠物, 2 = 使用道具, 3 = 逃跑
    int cmdType = static_cast<int>(packet.params);
    size_t offset = 0;
    
    if (packet.body.size() < 4) {
        return;
    }
    
    if (cmdType == 1) {
        // 切换宠物响应
        if (packet.body.size() < 56) {
            return;
        }
        
        int sid = ReadInt32LE(packet.body.data(), offset);
        (void)ReadInt32LE(packet.body.data(), offset);  // uniqueId：占位字段，仅需推进 offset
        int sta = ReadInt32LE(packet.body.data(), offset);
        
        if (sta == 1 && offset + 44 <= packet.body.size()) {
            const int rawState = ReadInt32LE(packet.body.data(), offset);
            const int newSid = ReadInt32LE(packet.body.data(), offset);
            const int groupType = ReadInt32LE(packet.body.data(), offset);
            const int hp = ReadInt32LE(packet.body.data(), offset);
            const int maxHp = ReadInt32LE(packet.body.data(), offset);
            const int level = ReadInt32LE(packet.body.data(), offset);
            const int elem = ReadInt32LE(packet.body.data(), offset);
            const int spiritId = ReadInt32LE(packet.body.data(), offset);
            const int newUniqueId = ReadInt32LE(packet.body.data(), offset);
            const int userId = ReadInt32LE(packet.body.data(), offset);
            const int skillNum = ReadInt32LE(packet.body.data(), offset);
            if (skillNum < 0 || skillNum > 128) return;
            (void)rawState;
            (void)groupType;
            const bool isMySide = (g_userId.load() > 0 && userId == static_cast<int>(g_userId.load())) || IsMySpiritBySid(sid);
            auto updateSpirit = [&](std::vector<BattleSixSpiritInfo>& spirits, bool mySide) {
                int index = -1;
                for (size_t i = 0; i < spirits.size(); ++i) {
                    if (newUniqueId != 0 && spirits[i].uniqueId == newUniqueId) { index = static_cast<int>(i); break; }
                    if (index < 0 && sid != 0 && spirits[i].sid == sid) index = static_cast<int>(i);
                }
                if (index < 0) { spirits.emplace_back(); index = static_cast<int>(spirits.size() - 1); }
                auto& spirit = spirits[index];
                spirit.sid = newSid;
                spirit.spiritId = spiritId;
                spirit.uniqueId = newUniqueId;
                spirit.userId = userId;
                spirit.hp = hp;
                spirit.maxHp = maxHp;
                spirit.level = level;
                spirit.element = elem;
                spirit.isDead = hp <= 0;
                spirit.position = index;
                spirit.skills.clear();
                spirit.skills.reserve(static_cast<size_t>(skillNum));
                for (int j = 0; j < skillNum; ++j) {
                    BattleSixSkillInfo skill;
                    skill.skillId = ReadInt32LE(packet.body.data(), offset);
                    skill.currentPP = ReadInt32LE(packet.body.data(), offset);
                    skill.maxPP = ReadInt32LE(packet.body.data(), offset);
                    skill.available = skill.currentPP > 0;
                    skill.name = GetSkillName(skill.skillId);
                    skill.power = GetSkillPower(skill.skillId, 0);
                    spirit.skills.push_back(std::move(skill));
                }
                if (mySide) {
                    m_currentSpiritIndex = index;
                    m_myUniqueId = newUniqueId;
                } else {
                    m_enemyActiveIndex = index;
                }
            };
            if (isMySide) updateSpirit(m_mySpirits, true);
            else updateSpirit(m_enemySpirits, false);
            if (isMySide && m_currentSpiritIndex >= 0 && m_currentSpiritIndex < static_cast<int>(m_mySpirits.size()) &&
                m_mySpirits[m_currentSpiritIndex].uniqueId == newUniqueId) {
                g_battleSixSwitchTargetId = -1;
                g_battleSixSwitchRetryCount = 0;
            }
            RefreshEnemyTarget();
        }
    } else if (cmdType == 0) {
        // 普通攻击结果
        int haveBattle = ReadInt32LE(packet.body.data(), offset);
        
            if (haveBattle == 1) {
                int atkId = ReadInt32LE(packet.body.data(), offset);
                int skillId = ReadInt32LE(packet.body.data(), offset);
                int defId = ReadInt32LE(packet.body.data(), offset);
                int miss = ReadInt32LE(packet.body.data(), offset);

                // 对照 AS3/BattleModel：skillid 在 miss 之前就已确认读取，
                // 本轮是否命中只影响后续伤害字段，不影响“技能已经被使用”这一事实。
                // 因此 PP 扣减必须在 miss 分支之外执行，否则高威力技能打空后本地 PP 会滞后。
                const bool isAtkMy = IsMySpiritBySid(atkId);
                if (isAtkMy) {
                    AdjustBattleSixSpiritSkillPP(m_mySpirits, atkId, skillId, -1);
                }
            
            if (miss == 0) {
                (void)ReadInt32LE(packet.body.data(), offset);  // brust：占位字段
                int atkHp = ReadInt32LE(packet.body.data(), offset);
                int defHp = ReadInt32LE(packet.body.data(), offset);
                (void)ReadInt32LE(packet.body.data(), offset);  // reboundHp：占位字段
                
                // 更新攻击者和防御者的HP
                // 关键：atkHp和defHp的含义取决于谁是攻击者
                // 当我方攻击时：atkHp是我方当前出战妖怪的HP，defHp是敌方当前出战妖怪的HP
                // 当敌方攻击时：atkHp是敌方当前出战妖怪的HP，defHp是我方当前出战妖怪的HP
                
                if (isAtkMy) {
                    // 我方攻击：更新我方攻击者和敌方防御者
                    UpdateMySpiritHP(atkId, atkHp);
                    UpdateEnemySpiritHP(defId, defHp);
                } else {
                    // 敌方攻击：更新敌方攻击者和我方防御者
                    UpdateEnemySpiritHP(atkId, atkHp);
                    UpdateMySpiritHP(defId, defHp);
                }

                RefreshEnemyTarget();
            }
        }
    }
}

BOOL BattleSixAutoBattle::AutoSwitchSpirit() {
    std::lock_guard<std::recursive_mutex> stateLock(m_stateMutex);
    const int pendingTargetId = g_battleSixSwitchTargetId.load();
    if (pendingTargetId > 0) {
        if (m_currentSpiritIndex >= 0 && m_currentSpiritIndex < (int)m_mySpirits.size() &&
            m_mySpirits[m_currentSpiritIndex].uniqueId == pendingTargetId) {
            g_battleSixSwitchTargetId = -1;
            g_battleSixSwitchRetryCount = 0;
        } else {
            return TRUE;
        }
    }

    const int startIndex = (m_currentSpiritIndex >= 0) ? (m_currentSpiritIndex + 1) : 0;
    int nextIndex = FindNextAliveSpirit(startIndex);

    if (nextIndex < 0 || nextIndex == m_currentSpiritIndex) {
        return FALSE;
    }

    const int spiritUniqueId = m_mySpirits[nextIndex].uniqueId;
    if (spiritUniqueId <= 0) {
        return FALSE;
    }

    struct SwitchThreadData {
        unsigned long long battleSession;
        int uniqueId;
    };

    const unsigned long long battleSession = g_battleSixBattleSession.load();
    SwitchThreadData* threadData = new SwitchThreadData{battleSession, spiritUniqueId};
    g_battleSixSwitchTargetId = spiritUniqueId;
    g_battleSixSwitchRetryCount = 0;

    HANDLE hThread = CreateManagedThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
        auto* data = static_cast<SwitchThreadData*>(lpParam);
        Sleep(300);
        if (g_battleSixBattleSession.load() != data->battleSession || !g_battleSixAuto.IsInBattle()) {
            delete data;
            return 0;
        }
        SendBattleSixUserOpPacket(1, data->uniqueId, 0);
        delete data;
        return 0;
    }, threadData, 0, nullptr);

    if (hThread) {
        CloseHandle(hThread);
        return TRUE;
    }

    delete threadData;
    g_battleSixSwitchTargetId = -1;
    g_battleSixSwitchRetryCount = 0;
    return FALSE;
}

int BattleSixAutoBattle::GetAliveSpiritCount() {
    std::lock_guard<std::recursive_mutex> stateLock(m_stateMutex);
    int count = 0;
    for (const auto& spirit : m_mySpirits) {
        if (!spirit.isDead && spirit.hp > 0) {
            count++;
        }
    }
    return count;
}

int BattleSixAutoBattle::GetEnemyAliveSpiritCount() {
    std::lock_guard<std::recursive_mutex> stateLock(m_stateMutex);
    int count = 0;
    for (const auto& spirit : m_enemySpirits) {
        if (!spirit.isDead && spirit.hp > 0) {
            count++;
        }
    }
    return count;
}

int BattleSixAutoBattle::SelectBestSkill() {
    std::lock_guard<std::recursive_mutex> stateLock(m_stateMutex);
    if (m_currentSpiritIndex < 0 || m_currentSpiritIndex >= (int)m_mySpirits.size()) {
        return -1;
    }

    auto& spirit = m_mySpirits[m_currentSpiritIndex];
    const BattleData battleData = PacketParser::GetCurrentBattleSnapshot();
    const BattleEntity* activePet = nullptr;
    if (battleData.myActiveIndex >= 0 &&
        battleData.myActiveIndex < static_cast<int>(battleData.myPets.size())) {
        const BattleEntity& candidate = battleData.myPets[battleData.myActiveIndex];
        bool sameSpirit = false;
        if (candidate.uniqueId != 0 && spirit.uniqueId != 0) {
            sameSpirit = candidate.uniqueId == spirit.uniqueId;
        }
        if (!sameSpirit && candidate.sid != 0 && spirit.sid != 0) {
            sameSpirit = candidate.sid == spirit.sid;
        }
        if (!sameSpirit && candidate.spiritId != 0 && spirit.spiritId != 0) {
            sameSpirit = candidate.spiritId == spirit.spiritId;
        }
        if (sameSpirit) {
            activePet = &candidate;
            for (auto& localSkill : spirit.skills) {
                for (const auto& battleSkill : candidate.skills) {
                    if (battleSkill.id == static_cast<uint32_t>(localSkill.skillId)) {
                        localSkill.currentPP = battleSkill.pp;
                        localSkill.maxPP = battleSkill.maxPp;
                        localSkill.available = (battleSkill.pp > 0);
                        break;
                    }
                }
            }
        }
    }

    // 先从当前战斗快照的原始技能数组选出最高威力技能，确保 skillId 属于本场服务端列表。
    if (activePet != nullptr) {
        uint32_t bestSkillId = 0;
        int bestPower = -1;
        for (const auto& battleSkill : activePet->skills) {
            if (battleSkill.pp <= 0) {
                continue;
            }
            const int power = GetSkillPower(static_cast<int>(battleSkill.id), 0);
            if (bestSkillId == 0 || power > bestPower) {
                bestSkillId = battleSkill.id;
                bestPower = power;
            }
        }
        if (bestSkillId != 0) {
            for (int i = 0; i < static_cast<int>(spirit.skills.size()); ++i) {
                if (static_cast<uint32_t>(spirit.skills[i].skillId) == bestSkillId) {
                    spirit.skills[i].power = bestPower;
                    return i;
                }
            }
            for (const auto& battleSkill : activePet->skills) {
                if (battleSkill.id != bestSkillId) {
                    continue;
                }
                BattleSixSkillInfo syncedSkill;
                syncedSkill.skillId = static_cast<int>(battleSkill.id);
                syncedSkill.currentPP = battleSkill.pp;
                syncedSkill.maxPP = battleSkill.maxPp;
                syncedSkill.available = battleSkill.pp > 0;
                syncedSkill.power = bestPower;
                syncedSkill.name = battleSkill.name;
                spirit.skills.push_back(std::move(syncedSkill));
                return static_cast<int>(spirit.skills.size()) - 1;
            }
        }
    }

    // 战斗快照已进入本场战斗时，不能再用旧本地技能列表兜底；
    // 否则换宠或 PP 更新后可能再次发送上一只妖怪的技能 ID。
    if (battleData.active) {
        return -1;
    }

    // 快照暂时不可用时，保留本地已同步技能列表作为降级路径。
    int bestSkillIndex = -1;
    int bestPower = -1;
    for (int i = 0; i < static_cast<int>(spirit.skills.size()); ++i) {
        auto& skill = spirit.skills[i];
        if (!skill.available || skill.currentPP <= 0) {
            continue;
        }
        const int power = GetSkillPower(skill.skillId, skill.power);
        skill.power = power;
        if (bestSkillIndex < 0 || power > bestPower) {
            bestSkillIndex = i;
            bestPower = power;
        }
    }
    return bestSkillIndex;
}

// 万妖盛会封包发送函数实现
BOOL SendBattleSixCombatInfoPacket() {
    std::vector<uint8_t> body;
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::BATTLESIX_COMBAT_INFO_SEND)
        .SetParams(0)
        .WriteBytes(body)
        .Build();
    
    return SendPacketBytes(packet);
}

static BOOL SendBattleSixMatchPacketOnce() {
    std::vector<uint8_t> body;
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::BATTLESIX_MATCH_SEND)
        .SetParams(0)
        .WriteBytes(body)
        .Build();

    return SendPacketBytes(packet);
}

static void StartBattleSixMatchRetryLoop(unsigned long long token) {
    struct MatchRetryContext {
        unsigned long long token;
    };

    auto* context = new MatchRetryContext{token};
    HANDLE hThread = CreateManagedThread(nullptr, 0, [](LPVOID param) -> DWORD {
        std::unique_ptr<MatchRetryContext> context(static_cast<MatchRetryContext*>(param));
        while (true) {
            Sleep(1000);

            if (!g_battleSixAuto.IsAutoMatching() || g_battleSixAuto.IsInBattle()) {
                return 0;
            }

            if (g_battleSixFlowToken.load() != context->token ||
                g_battleSixFlowStage.load() != BATTLESIX_FLOW_MATCHING ||
                !g_battleSixMatching.load()) {
                return 0;
            }

            SendBattleSixMatchPacketOnce();
        }
    }, context, 0, nullptr);

    if (hThread) {
        CloseHandle(hThread);
    } else {
        delete context;
    }
}

BOOL SendBattleSixMatchPacket() {
    g_battleSixMatching = true;
    g_battleSixMatchSuccess = false;
    const unsigned long long token = AdvanceBattleSixFlowStage(BATTLESIX_FLOW_MATCHING);
    ArmBattleSixFlowWatchdog(
        45000,
        BATTLESIX_FLOW_MATCHING,
        token,
        true,
        L"万妖盛会：匹配超时，取消后重试...");

    const BOOL sent = SendBattleSixMatchPacketOnce();
    StartBattleSixMatchRetryLoop(token);
    return sent;
}

BOOL SendBattleSixCancelMatchPacket() {
    std::vector<uint8_t> body;
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::BATTLESIX_CANCEL_MATCH_SEND)
        .SetParams(0)
        .WriteBytes(body)
        .Build();
    
    ResetBattleSixFlowState();
    
    return SendPacketBytes(packet);
}

BOOL SendBattleSixReqStartPacket() {
    std::vector<uint8_t> body;
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::BATTLESIX_REQ_START_SEND)
        .SetParams(0)
        .WriteBytes(body)
        .Build();

    g_battleSixReadySupplementSent = false;
    return SendPacketBytes(packet);
}

BOOL SendBattleSixUserOpPacket(int opType, int param1, int param2) {
    // 根据AS3 BattleControl.as代码：
    // opType=0 (技能攻击): sendMessage(opcode, 0, [obj.target, obj.skillId])
    // opType=1 (切换精灵): sendMessage(opcode, 1, [obj.spiritid])
    // opType=2 (使用道具): sendMessage(opcode, 2, [obj.packcode, obj.position, obj.sid])
    // opType=3 (逃跑): sendMessage(opcode, 3) - 无Body
    
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::BATTLESIX_USER_OP_SEND)
        .SetParams(opType);
    
    if (opType == 0) {
        // 技能攻击: Body = [target, skillId]
        packet.WriteInt32(param1);  // target
        packet.WriteInt32(param2);  // skillId
    } else if (opType == 1) {
        // 切换精灵: Body = [spiritid]
        packet.WriteInt32(param1);  // spiritUniqueId
    }
    // opType=3 逃跑无需Body
    
    auto finalPacket = packet.Build();
    
    return SendPacketBytes(finalPacket);
}

BOOL SendBattleSixEndPacket() {
    // BattleSix uses the common battle lifecycle; AS3 sends 1186323 here.
    return SendNormalBattleEndPacket();
}
// 万妖盛会响应处理函数实现
void ProcessBattleSixMatchResponse(const GamePacket& packet) {
    if (packet.params != 0) {
        g_battleSixMatchSuccess = false;
        g_battleSixMatching = false;
        ResetBattleSixFlowState();

        wchar_t msg[256];
        swprintf_s(msg, L"万妖盛会：匹配失败，错误码 %u", packet.params);
        UIBridge::Instance().UpdateHelperText(msg);
        if (g_battleSixAuto.IsAutoMatching()) {
            ScheduleBattleSixRecoveryViaCombatInfo(1500);
        }
        return;
    }

    // AS3: params=0 且 Body 为空表示仍在匹配，不是异常响应。
    if (packet.body.empty()) {
        g_battleSixMatchSuccess = false;
        g_battleSixMatching = true;
        AdvanceBattleSixFlowStage(BATTLESIX_FLOW_MATCHING);
        UIBridge::Instance().UpdateHelperText(L"万妖盛会：正在匹配");
        return;
    }
    if (packet.body.size() < sizeof(int32_t)) {
        return;
    }

    size_t offset = 0;
    const int opponentId = ReadInt32LE(packet.body.data(), offset);
    g_battleSixMatchSuccess = true;
    g_battleSixMatching = false;
    const unsigned long long token = AdvanceBattleSixFlowStage(BATTLESIX_FLOW_WAITING_BATTLE_START);

    wchar_t msg[256];
    swprintf_s(msg, L"万妖盛会：匹配成功，对手ID: %d", opponentId);
    UIBridge::Instance().UpdateHelperText(msg);
    ArmBattleSixFlowWatchdog(
        60000,
        BATTLESIX_FLOW_WAITING_BATTLE_START,
        token,
        false,
        L"万妖盛会：匹配成功但未正常进入战斗，准备重试...");

    struct ReqStartThreadData { unsigned long long token; };
    auto* reqStartData = new ReqStartThreadData{token};
    HANDLE hThread = CreateManagedThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
        std::unique_ptr<ReqStartThreadData> data(static_cast<ReqStartThreadData*>(lpParam));
        Sleep(2000);
        if (g_battleSixAuto.IsAutoMatching() && !g_battleSixAuto.IsInBattle() &&
            g_battleSixFlowStage.load() == BATTLESIX_FLOW_WAITING_BATTLE_START &&
            g_battleSixFlowToken.load() == data->token) {
            SendBattleSixReqStartPacket();
        }
        return 0;
    }, reqStartData, 0, nullptr);
    if (hThread) CloseHandle(hThread); else delete reqStartData;
}
void ProcessBattleSixCancelMatchResponse(const GamePacket& packet) {
    if (packet.params != 0) {
        UIBridge::Instance().UpdateHelperText(
            L"万妖盛会：取消匹配失败，错误码 " + std::to_wstring(packet.params));
        return;
    }
    g_battleSixMatching = false;
    g_battleSixMatchSuccess = false;
    ResetBattleSixFlowState();
    UIBridge::Instance().UpdateHelperText(L"万妖盛会：已取消匹配");
}

void ProcessBattleSixPrepareCombatResponse(const GamePacket& packet) {
    if (packet.body.size() < sizeof(int32_t) * 2) {
        return;
    }
    size_t offset = 0;
    const int prepareCode = ReadInt32LE(packet.body.data(), offset);
    const int prepareValue = ReadInt32LE(packet.body.data(), offset);
    const unsigned long long token = AdvanceBattleSixFlowStage(BATTLESIX_FLOW_PREPARING_COMBAT);
    UIBridge::Instance().UpdateHelperText(
        L"万妖盛会：准备战斗（" + std::to_wstring(prepareCode) +
        L"," + std::to_wstring(prepareValue) + L"）");
    ArmBattleSixFlowWatchdog(
        60000,
        BATTLESIX_FLOW_PREPARING_COMBAT,
        token,
        false,
        L"万妖盛会：准备战斗超时，重新匹配...");
}
void ProcessBattleSixOtherEscapedResponse(const GamePacket& packet) {
    (void)packet;
    UIBridge::Instance().UpdateHelperText(L"万妖盛会：对手已逃跑，本场判定胜利");
    if (g_battleSixAuto.IsInBattle()) {
        ProcessBattleSixBattleEndResponse(packet);
    } else {
        g_battleSixMatching = false;
        ResetBattleSixFlowState();
    }
}
void ProcessBattleSixCombatOverResponse(const GamePacket& packet) {
    std::wstring reason;
    if ((packet.params == 1 || packet.params == 3 || packet.params == 4) &&
        packet.body.size() >= sizeof(int32_t)) {
        size_t offset = 0;
        const int userId = ReadInt32LE(packet.body.data(), offset);
        reason = L"，关联玩家 " + std::to_wstring(userId);
    }
    g_battleSixMatching = false;
    g_battleSixMatchSuccess = false;
    g_battleSixAuto.EndBattle();
    g_battleSixAuto.SetAutoMatching(false);
    ResetBattleSixFlowState();
    UIBridge::Instance().UpdateHelperText(
        L"万妖盛会：比赛被服务端终止（原因 " + std::to_wstring(packet.params) + L"）" + reason);
}
void ProcessBattleSixSelfBanSpiritResponse(const GamePacket& packet) {
    if (packet.params != 0 || packet.body.size() < sizeof(int32_t)) {
        UIBridge::Instance().UpdateHelperText(L"万妖盛会：读取禁用妖怪列表失败");
        return;
    }
    size_t offset = 0;
    const int count = ReadInt32LE(packet.body.data(), offset);
    if (count < 0 || count > 128 || packet.body.size() < sizeof(int32_t) * (1u + static_cast<size_t>(count))) {
        return;
    }
    std::wstring message = L"万妖盛会：阵容包含禁用妖怪";
    for (int i = 0; i < count; ++i) {
        const int spiritId = ReadInt32LE(packet.body.data(), offset);
        message += (i == 0 ? L" " : L"," ) + std::to_wstring(spiritId);
    }
    UIBridge::Instance().UpdateHelperText(message);
}
void ProcessBattleSixFinishFirstSpiritResponse(const GamePacket& packet) {
    (void)packet;
    UIBridge::Instance().UpdateHelperText(L"万妖盛会：首发妖怪设置完成");
}

void ProcessBattleSixReqStartResponse(const GamePacket& packet) {
    (void)packet;
    // 请求开始战斗的响应处理
    UIBridge::Instance().UpdateHelperText(L"万妖盛会：已请求开始战斗");
}

void ProcessBattleSixCombatInfoResponse(const GamePacket& packet) {
    // AS3: [winStreak, teamNum, curNum, firstSpiritId, monsterNum,
    //       (position, id, spiritId, level) * monsterNum].
    if (packet.body.size() < sizeof(int32_t) * 5) {
        UIBridge::Instance().UpdateHelperText(L"万妖盛会：战斗信息响应不完整");
        return;
    }

    size_t offset = 0;
    const int winStreak = ReadInt32LE(packet.body.data(), offset);
    const int teamNum = ReadInt32LE(packet.body.data(), offset);
    const int currentCount = ReadInt32LE(packet.body.data(), offset);
    const int firstSpiritId = ReadInt32LE(packet.body.data(), offset);
    const int monsterCount = ReadInt32LE(packet.body.data(), offset);
    if (monsterCount < 0 || monsterCount > 128 ||
        packet.body.size() < offset + sizeof(int32_t) * 4u * static_cast<size_t>(monsterCount)) {
        UIBridge::Instance().UpdateHelperText(L"万妖盛会：战斗信息阵容数据无效");
        return;
    }

    std::vector<BattleSixLineupEntry> lineup;
    lineup.reserve(static_cast<size_t>(monsterCount));
    for (int i = 0; i < monsterCount; ++i) {
        BattleSixLineupEntry entry;
        entry.position = ReadInt32LE(packet.body.data(), offset);
        entry.id = ReadInt32LE(packet.body.data(), offset);
        entry.spiritId = ReadInt32LE(packet.body.data(), offset);
        entry.level = ReadInt32LE(packet.body.data(), offset);
        lineup.push_back(entry);
    }
    std::sort(lineup.begin(), lineup.end(), [](const auto& left, const auto& right) {
        return left.position < right.position;
    });
    g_battleSixAuto.UpdateCombatInfo(winStreak, teamNum, currentCount, firstSpiritId, std::move(lineup));

    UIBridge::Instance().UpdateHelperText(
        L"万妖盛会：已打开界面，连胜" + std::to_wstring(winStreak) +
        L"，阵容" + std::to_wstring(monsterCount) + L"只");

    if (g_battleSixAuto.IsAutoMatching() &&
        !g_battleSixAuto.IsInBattle() &&
        g_battleSixFlowStage.load() == BATTLESIX_FLOW_IDLE &&
        !g_battleSixMatching.load()) {
        HANDLE hThread = CreateManagedThread(nullptr, 0, [](LPVOID) -> DWORD {
            Sleep(1000);
            if (!g_battleSixAuto.IsAutoMatching() || g_battleSixAuto.IsInBattle()) return 0;
            if (g_battleSixFlowStage.load() != BATTLESIX_FLOW_IDLE || g_battleSixMatching.load()) return 0;
            SendBattleSixMatchPacket();
            return 0;
        }, nullptr, 0, nullptr);
        if (hThread) CloseHandle(hThread);
    }
}
void ProcessBattleSixBattleRoundResultResponse(const GamePacket& packet) {
    g_battleSixRoundResultToken.fetch_add(1);
    g_battleSixAuto.OnBattleRoundResult(packet);

    if (!g_battleSixAuto.IsInBattle()) {
        return;
    }

    const unsigned long long battleSession = g_battleSixBattleSession.load();
    const unsigned long long roundToken = g_battleSixRoundToken.load();
    const unsigned long long playOverToken = g_battleSixPlayOverToken.load();

    struct BattlePlayOverThreadData {
        unsigned long long battleSession;
        unsigned long long roundToken;
        unsigned long long playOverToken;
    };

    BattlePlayOverThreadData* threadData =
        new BattlePlayOverThreadData{battleSession, roundToken, playOverToken};

    HANDLE hThread = CreateManagedThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
        std::unique_ptr<BattlePlayOverThreadData> data(
            static_cast<BattlePlayOverThreadData*>(lpParam));
        Sleep(1200);

        if (g_battleSixBattleSession.load() != data->battleSession || !g_battleSixAuto.IsInBattle()) {
            return 0;
        }
        if (g_battleSixRoundToken.load() != data->roundToken) {
            return 0;
        }
        if (g_battleSixPlayOverToken.load() != data->playOverToken) {
            return 0;
        }

        SendBattlePlayOverPacket();
        return 0;
    }, threadData, 0, nullptr);

    if (hThread) {
        CloseHandle(hThread);
    } else {
        delete threadData;
    }
}

void ProcessBattleSixBattleEndResponse(const GamePacket& packet) {
    (void)packet;
    // 记录战斗结果
    bool wasInBattle = g_battleSixAuto.IsInBattle();
    bool wasAutoMatching = g_battleSixAuto.IsAutoMatching();
    
    // 计算胜负
    int myAlive = g_battleSixAuto.GetAliveSpiritCount();
    const uint32_t settlementFlags = g_battleSixSettlementFlags.load();
    bool isWin = myAlive > 0;
    if (g_battleSixSettlementKnown.load()) {
        // 对照 AS3 BattleControl.applyExpBody：逃跑、特殊战场和正常胜利
        // 使用不同位，不再仅凭本地存活数量猜测结果。
        if ((settlementFlags & 8u) != 0) {
            isWin = true;
        } else if ((settlementFlags & 64u) != 0) {
            isWin = (settlementFlags & 128u) != 0;
        } else {
            isWin = (settlementFlags & 0x80000000u) != 0;
        }
    }
    if (isWin) {
        g_battleSixAuto.IncrementWinCount();
    } else {
        g_battleSixAuto.IncrementLoseCount();
    }
    
    // 结束战斗
    // 服务端可能先发 BATTLE_END，导致延迟线程因 EndBattle() 的 session 失效。
    // AS3 需要 BATTLE_PLAY_OVER 才会清理 BattleView，否则下一场 BATTLE_START 会被丢弃。
    if (wasInBattle &&
        g_battleSixPlayOverToken.load() < g_battleSixRoundResultToken.load()) {
        SendBattlePlayOverPacket();
    }
    g_battleSixAuto.EndBattle();
    
    // 只有万妖盛会自动匹配模式才显示特定提示
    if (wasAutoMatching) {
        wchar_t msg[256];
        if (isWin) {
            swprintf_s(msg, L"万妖盛会：战斗胜利");
        } else {
            swprintf_s(msg, L"万妖盛会：战斗失败，所有精灵已阵亡");
        }
        UIBridge::Instance().UpdateHelperText(msg);
    }
    
    // 确认战斗结束
    // 普通任务战斗必须走 1186323；1186050(params=4) 仅属于万妖盛会。
    if (wasAutoMatching) {
        SendBattleSixEndPacket();
    } else if (wasInBattle) {
        SendNormalBattleEndPacket();
    }
    
    // 检查是否需要继续匹配（仅万妖盛会模式）
    if (wasAutoMatching && g_battleSixAuto.GetMatchCount() > 0) {
        g_battleSixAuto.DecrementMatchCount();
        
        // 显示剩余次数
        int remaining = g_battleSixAuto.GetMatchCount();
        int total = g_battleSixAuto.GetTotalMatchCount();
        int wins = g_battleSixAuto.GetWinCount();
        int loses = g_battleSixAuto.GetLoseCount();
        
        wchar_t continueMsg[256];
        swprintf_s(continueMsg, L"万妖盛会：剩余%d次 (胜%d 负%d)", remaining, wins, loses);
        UIBridge::Instance().UpdateHelperText(continueMsg);
        
        if (remaining > 0) {
            // 异步延迟后开始下一轮匹配
            const unsigned long long battleSession = g_battleSixBattleSession.load();
            struct NextMatchThreadData {
                unsigned long long battleSession;
            };
            NextMatchThreadData* threadData = new NextMatchThreadData{battleSession};
            HANDLE hThread = CreateManagedThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
                std::unique_ptr<NextMatchThreadData> data(static_cast<NextMatchThreadData*>(lpParam));
                Sleep(2000);  // 等待2秒
                if (g_battleSixBattleSession.load() == data->battleSession &&
                    g_battleSixAuto.IsAutoMatching() && !g_battleSixAuto.IsInBattle()) {
                    SendBattleSixMatchPacket();
                }
                return 0;
            }, threadData, 0, nullptr);
            if (hThread) {
                CloseHandle(hThread);
            } else {
                delete threadData;
            }
        } else {
            // 所有匹配完成，显示总结
            wchar_t summaryMsg[256];
            swprintf_s(summaryMsg, L"万妖盛会：完成%d次匹配 (胜%d 负%d)", total, wins, loses);
            UIBridge::Instance().UpdateHelperText(summaryMsg);
            g_battleSixAuto.SetAutoMatching(false);
        }
    }
}

// 万妖盛会一键功能实现
BOOL StartOneKeyBattleSixPacket(int matchCount) {
    // 启用自动战斗
    g_battleSixAuto.SetAutoBattle(true);
    
    // 设置自动匹配标志和匹配次数
    ResetBattleSixFlowState();
    g_battleSixAuto.SetAutoMatching(true);
    g_battleSixAuto.SetMatchCount(matchCount);
    
    // 先发送查询战斗信息封包（打开界面）
    return SendBattleSixCombatInfoPacket();
}

BOOL CancelBattleSixMatch() {
    // 取消匹配
    return SendBattleSixCancelMatchPacket();
}

// ============================================================================
// 协议层战斗事件适配
// ============================================================================

static void CopyBattleSixSpiritFromBattle(const BattleEntity& source, BattleSixSpiritInfo& target) {
    target.sid = source.sid;
    target.spiritId = source.spiritId;
    target.uniqueId = source.uniqueId;
    target.userId = source.userId;
    target.hp = source.hp;
    target.maxHp = source.maxHp;
    target.level = source.level;
    target.element = source.elem;
    target.isDead = source.hp <= 0;
    target.name = source.name;
    target.skills.clear();
    target.skills.reserve(source.skills.size());
    for (const auto& sourceSkill : source.skills) {
        BattleSixSkillInfo skill;
        skill.skillId = static_cast<int>(sourceSkill.id);
        skill.currentPP = sourceSkill.pp;
        skill.maxPP = sourceSkill.maxPp;
        skill.available = sourceSkill.pp > 0;
        skill.name = sourceSkill.name;
        skill.power = GetSkillPower(skill.skillId, 0);
        target.skills.push_back(std::move(skill));
    }
}

void BattleSixSyncFromBattle(const BattleData& battle, bool initialize) {
    std::lock_guard<std::recursive_mutex> stateLock(g_battleSixAuto.GetStateMutex());
    if (!g_battleSixAuto.IsAutoBattleEnabled()) return;
    if (initialize) {
        g_battleSixAuto.StartBattle();
        g_battleSixAuto.GetMySpirits().clear();
        g_battleSixAuto.GetEnemySpirits().clear();
    }
    if (!g_battleSixAuto.IsInBattle()) return;

    auto syncTeam = [](const std::vector<BattleEntity>& source,
                       std::vector<BattleSixSpiritInfo>& target,
                       int32_t activeIndex) {
        int activeTargetIndex = -1;
        for (size_t sourceIndex = 0; sourceIndex < source.size(); ++sourceIndex) {
            const BattleEntity& sourceSpirit = source[sourceIndex];
            if (sourceSpirit.placeholder || sourceSpirit.spiritId == 0) continue;
            int targetIndex = -1;
            for (size_t i = 0; i < target.size(); ++i) {
                if (sourceSpirit.uniqueId != 0 && target[i].uniqueId == sourceSpirit.uniqueId) {
                    targetIndex = static_cast<int>(i);
                    break;
                }
                if (targetIndex < 0 && sourceSpirit.sid != 0 && target[i].sid == sourceSpirit.sid) {
                    targetIndex = static_cast<int>(i);
                }
            }
            if (targetIndex < 0) {
                target.emplace_back();
                targetIndex = static_cast<int>(target.size() - 1);
            }
            CopyBattleSixSpiritFromBattle(sourceSpirit, target[targetIndex]);
            target[targetIndex].position = targetIndex;
            if (static_cast<int32_t>(sourceIndex) == activeIndex || sourceSpirit.state == 1) {
                activeTargetIndex = targetIndex;
            }
        }
        return activeTargetIndex;
    };

    const int myActiveIndex = syncTeam(battle.myPets, g_battleSixAuto.GetMySpirits(), battle.myActiveIndex);
    const int enemyActiveIndex = syncTeam(battle.otherPets, g_battleSixAuto.GetEnemySpirits(), battle.otherActiveIndex);
    if (myActiveIndex >= 0) {
        g_battleSixAuto.SetCurrentSpiritIndex(myActiveIndex);
        g_battleSixAuto.SetMyUniqueId(g_battleSixAuto.GetMySpirits()[myActiveIndex].uniqueId);
    }
    if (enemyActiveIndex >= 0) g_battleSixAuto.SetEnemyActiveIndex(enemyActiveIndex);
    g_battleSixAuto.RefreshEnemyTarget();
}

void BattleSixNotifyRoundStart() {
    if (g_battleSixAuto.IsInBattle() && g_battleSixAuto.IsAutoBattleEnabled()) {
        g_battleSixRoundToken.fetch_add(1);
        g_battleSixAuto.OnBattleRoundStart();
    }
}

void BattleSixMarkSettlement(uint32_t winFlags) {
    if (g_battleSixAuto.IsInBattle()) {
        g_battleSixSettlementKnown = true;
        g_battleSixSettlementFlags = winFlags;
    }
}

bool BattleSixIsAutoBattleActive() {
    return g_battleSixAuto.IsInBattle() && g_battleSixAuto.IsAutoBattleEnabled();
}
