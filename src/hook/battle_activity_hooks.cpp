#include "battle_activity_hooks.h"

#include <atomic>

#include "battle_six.h"
#include "dungeon_jump.h"
#include "shuangtai.h"

namespace BattleActivity {

void SyncAutoBattle(const BattleData& battle, bool initialize) {
    BattleSixSyncFromBattle(battle, initialize);
}

void NotifyRoundStart() {
    BattleSixNotifyRoundStart();
}

void MarkSettlement(uint32_t winFlags) {
    BattleSixMarkSettlement(winFlags);
}

bool IsAutoBattleActive() {
    return BattleSixIsAutoBattleActive();
}

void NotifyMonsterDataReceived() {
    g_dungeonJumpState.monsterDataReceived.store(true, std::memory_order_release);
    if (g_shuangtaiWaitingForMonsterData.exchange(false, std::memory_order_acq_rel)) {
        UpdateShuangTaiUIFromMonsterData();
    }
}

}  // namespace BattleActivity
