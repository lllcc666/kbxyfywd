/**
 * @file battle_roster.h
 * @brief 战斗名册（敌我双方妖怪列表）的定位规则。纯逻辑、无副作用，可直接单测。
 *
 * 语义唯一来源：反编译 AS3 客户端
 *   - com/game/modules/model/BattleModel.as:357-385  BATTLE_ROUND params==1 的字段顺序
 *   - com/game/modules/view/battle/BattleView.as:2119-2189  dealChangeSpirit
 *   - com/game/modules/view/battle/BattleView.as:1836 / 1864  dealBattle（用 sid 判阵营）
 *   - com/game/modules/view/battle/BaseInfoPanel.as:469 / 494  addBuf（用 sid 判目标）
 *
 * 三条必须先说清楚的结论（它们是本文件所有规则的依据）：
 *
 *  1) `sid` 是一场战斗内全局唯一的妖怪编号（敌我共用一个键空间，服务端不会给两只
 *     不同妖怪发同一个 sid）。AS3 从不拿 sid 的数值去推阵营，一律用 `userid` /
 *     `groupType` / “和当前出战那只的 sid 是否相等”来判。所以“敌我 sid 都从 1 开始
 *     会撞号”这个假设不成立，凡是靠它做的启发式（例如把某类 Buff 归属翻转给对面）
 *     都会把数据写到错的妖怪身上。
 *
 *  2) BATTLE_ROUND params==1 的封包里：`sid` 是**离场**（也就是当前出战）那只的 sid，
 *     `uniqueid` 才是**进场**那只的 uniqueId。把两者弄反，或者拿 `sid` 去名册里找
 *     “进场槽位”，必然命中离场那只 —— 表现就是换宠后出战索引不动、面板继续显示
 *     离场那只的血量/PP/状态。
 *
 *  3) 进场槽位只能由 `uniqueid` 决定，顺序是：名册里 uniqueId 精确命中 → 待揭示的
 *     占位槽 → 追加到队尾。离场的 sid 只用于“把那只置为离场并清空状态栏”，绝不参与
 *     进场槽位查找。
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "packet_types.h"

namespace BattleRoster {

/// “没有可用槽位” —— 需要新增一只，或者根本定位不到。
constexpr int32_t kNoSlot = -1;

enum class Side {
    Unknown = 0,
    My = 1,
    Other = -1,
};

inline bool IsValidSlot(const std::vector<BattleEntity>& team, int32_t slot) {
    return slot >= 0 && slot < static_cast<int32_t>(team.size());
}

/// 按 sid 找槽位。sid == 0 表示“没填”（未揭示的占位妖怪 sid 也是 0），不参与匹配。
inline int32_t FindSlotBySid(const std::vector<BattleEntity>& team, int32_t sid) {
    if (sid == 0) return kNoSlot;
    for (size_t i = 0; i < team.size(); ++i) {
        if (team[i].sid == sid) return static_cast<int32_t>(i);
    }
    return kNoSlot;
}

/// 按 uniqueId 找槽位。uniqueId == 0 表示“没填”，不参与匹配。
inline int32_t FindSlotByUniqueId(const std::vector<BattleEntity>& team, int32_t uniqueId,
                                  bool includePlaceholder = false) {
    if (uniqueId == 0) return kNoSlot;
    for (size_t i = 0; i < team.size(); ++i) {
        if (team[i].uniqueId != uniqueId) continue;
        if (!includePlaceholder && team[i].placeholder) continue;
        return static_cast<int32_t>(i);
    }
    return kNoSlot;
}

/// 第一个待揭示占位槽。BATTLE_START 里敌人（或观战下的未知一方）只会给出数量。
inline int32_t FindPlaceholderSlot(const std::vector<BattleEntity>& team) {
    for (size_t i = 0; i < team.size(); ++i) {
        if (team[i].placeholder) return static_cast<int32_t>(i);
    }
    return kNoSlot;
}

inline bool TeamHasSid(const std::vector<BattleEntity>& team, int32_t sid) {
    return FindSlotBySid(team, sid) != kNoSlot;
}

/// 该队当前出战的是不是这只 sid。对应 AS3 `playerRole.roleInfo.sid == boobj.sid`。
inline bool ActiveSidMatches(const std::vector<BattleEntity>& team, int32_t activeIndex, int32_t sid) {
    if (sid == 0) return false;
    return IsValidSlot(team, activeIndex) && team[static_cast<size_t>(activeIndex)].sid == sid;
}

inline const std::vector<BattleEntity>* SelectTeam(const BattleData& battle, Side side) {
    if (side == Side::My) return &battle.myPets;
    if (side == Side::Other) return &battle.otherPets;
    return nullptr;
}

inline std::vector<BattleEntity>* SelectTeam(BattleData& battle, Side side) {
    if (side == Side::My) return &battle.myPets;
    if (side == Side::Other) return &battle.otherPets;
    return nullptr;
}

inline int32_t ActiveIndexOf(const BattleData& battle, Side side) {
    if (side == Side::My) return battle.myActiveIndex;
    if (side == Side::Other) return battle.otherActiveIndex;
    return kNoSlot;
}

inline int32_t& ActiveIndexOf(BattleData& battle, Side side) {
    return side == Side::My ? battle.myActiveIndex : battle.otherActiveIndex;
}

/**
 * @brief 判定这次换宠发生在哪一侧。
 * @param leavingSid      封包里的 sid（= 离场那只）
 * @param enteringUniqueId 封包里的 uniqueid（= 进场那只）
 * @param enteringUserId  进场实体自带的 userid；sta != 1 时为 0
 * @param enteringGroupType 进场实体自带的 groupType；sta != 1 时为 0
 *
 * 判定顺序完全对齐 AS3：
 *   ① 离场 sid 等于我方 / 敌方“当前出战”那只的 sid（dealChangeSpirit 的原始判断）；
 *   ② 进场实体 userid 与本地 userId 比较（BATTLE_START / isPlayerRole 的同一条判据）；
 *   ③ 进场 uniqueid 只在某一侧名册里出现；
 *   ④ 离场 sid 只在某一侧名册里出现；
 *   ⑤ userId 还没拿到时（启动早期）退回 groupType（与观战解析一致）。
 * 全部判不出来时返回 Unknown，调用方不要瞎猜，保持原状态。
 */
inline Side ResolveSwitchSide(const BattleData& battle, int32_t leavingSid,
                              int32_t enteringUniqueId, int32_t enteringUserId,
                              int32_t localUserId, int32_t enteringGroupType) {
    const bool myActive = ActiveSidMatches(battle.myPets, battle.myActiveIndex, leavingSid);
    const bool otherActive = ActiveSidMatches(battle.otherPets, battle.otherActiveIndex, leavingSid);
    if (myActive != otherActive) return myActive ? Side::My : Side::Other;

    if (enteringUserId > 0 && localUserId > 0) {
        return enteringUserId == localUserId ? Side::My : Side::Other;
    }

    if (enteringUniqueId != 0) {
        const bool myHas = FindSlotByUniqueId(battle.myPets, enteringUniqueId) != kNoSlot;
        const bool otherHas = FindSlotByUniqueId(battle.otherPets, enteringUniqueId) != kNoSlot;
        if (myHas != otherHas) return myHas ? Side::My : Side::Other;
    }

    if (leavingSid != 0) {
        const bool myHas = TeamHasSid(battle.myPets, leavingSid);
        const bool otherHas = TeamHasSid(battle.otherPets, leavingSid);
        if (myHas != otherHas) return myHas ? Side::My : Side::Other;
    }

    if (localUserId <= 0 && enteringGroupType == 1) return Side::My;

    return Side::Unknown;
}

struct SwitchPlan {
    Side side = Side::Unknown;
    /// 进场妖怪应该落在哪个槽位；kNoSlot 表示名册里没有它、也没有占位槽，需要追加。
    int32_t slot = kNoSlot;
};

/**
 * @brief 定位进场槽位。
 * @details 顺序：uniqueId 精确命中（非占位）→ 待揭示占位 → 追加（kNoSlot）。
 *          绝不使用离场 sid —— 那正好会命中离场那只。
 */
inline SwitchPlan PlanSwitch(const BattleData& battle, Side side, int32_t enteringUniqueId) {
    SwitchPlan plan;
    plan.side = side;
    const std::vector<BattleEntity>* team = SelectTeam(battle, side);
    if (!team) return plan;

    int32_t slot = FindSlotByUniqueId(*team, enteringUniqueId);
    if (slot == kNoSlot) slot = FindPlaceholderSlot(*team);

    plan.slot = slot;
    return plan;
}



/**
 * @brief 执行一次换宠，把 AS3 dealChangeSpirit 的副作用落到名册上。
 * @param leavingSid       封包里的 sid（离场那只）
 * @param enteringUniqueId 封包里的 uniqueid（进场那只）
 * @param enteringEntity   进场实体；sta != 1（封包不带实体）时传 nullptr
 *
 * 规则：
 *   - 离场那只：state = 0 + 清空 bufArr（AS3 2149-2150）；同一只刷新时跳过。
 *   - 进场那只：state = 1、placeholder = false；换人时 bufArr 为空，
 *     同一只刷新时保留原有状态。
 *   - 出战索引只指向进场的那个槽位；原出战槽位（若换到了别的槽）收成 state = 0。
 *   - 阵营判不出来（side == Unknown）时什么都不做。
 */
inline void ApplySwitch(BattleData& battle, Side side, int32_t leavingSid,
                        int32_t enteringUniqueId, const BattleEntity* enteringEntity) {
    std::vector<BattleEntity>* team = SelectTeam(battle, side);
    if (!team) return;

    SwitchPlan plan = PlanSwitch(battle, side, enteringUniqueId);
    // 兜底去重：uniqueId 匹配不上时（首发包缺 uniqueId，或换宠包的 uniqueid
    // 与名册里存的值不同），封包实体自带的 sid 仍是一场战斗内唯一的编号。
    // 名册里若已有同 sid 的槽位，那就是它自己再次上场，必须复用该槽位 ——
    // 否则会往队尾追加一只同 sid 的副本，之后所有按 sid 定位的血量与 PP 都
    // 写在已经离场的旧副本上，面板就再也不动了。
    if (plan.slot == kNoSlot && enteringEntity != nullptr && enteringEntity->uniqueId != 0 &&
        enteringEntity->uniqueId != enteringUniqueId) {
        // 第二候选：实体内部的 uniqueId 与三元组给的进场键不同时，两边都试一次。
        const int32_t byUnique = FindSlotByUniqueId(*team, enteringEntity->uniqueId);
        if (byUnique != kNoSlot) {
            plan.slot = byUnique;
        }
    }
    if (plan.slot == kNoSlot && enteringEntity != nullptr && enteringEntity->sid != 0) {
        const int32_t bySid = FindSlotBySid(*team, enteringEntity->sid);
        if (bySid != kNoSlot) {
            plan.slot = bySid;
        }
    }
    int32_t& activeIndex = ActiveIndexOf(battle, side);
    const int32_t previousActive = activeIndex;
    const int32_t leavingSlot = FindSlotBySid(*team, leavingSid);

    // 离场那只：state 归零并清空状态栏（换人不继承对方的状态）。
    if (leavingSlot != kNoSlot && leavingSlot != plan.slot) {
        BattleEntity& leaving = (*team)[static_cast<size_t>(leavingSlot)];
        leaving.state = 0;
        leaving.bufArr.clear();
    }

    if (enteringEntity) {
        BattleEntity entering = *enteringEntity;
        entering.mySpirit = (side == Side::My);
        entering.state = 1;
        entering.placeholder = false;

        if (plan.slot == kNoSlot) {
            team->push_back(std::move(entering));
            activeIndex = static_cast<int32_t>(team->size()) - 1;
        } else {
            BattleEntity& target = (*team)[static_cast<size_t>(plan.slot)];
            // AS3 的权威语义（BattleView.as:890-895）：换宠包里的实体只在
            // dealChangeSpirit **没在名册里找到这只妖怪**时才拿去新建角色
            // （`singleLoad(this._newData, false)`）；只要名册里已经有这只，
            // 包里的 hp / PP / skills / sid / uniqueid 一个都不用，只切引用。
            //
            // 反过来做整只替换的后果：PP 与技能表被换成封包那份（换宠包经常不带
            // 技能表，或只回状态不回 PP），之后 DecreaseBattleSkillPp 撞上
            // `skill.time <= 0` 直接 return，属性栏就再也不动了。
            if (!target.placeholder) {
                // 名册里已有这只：只把出战标记切过来，实体数据一律保留。
                target.state = 1;
            } else {
                // 占位槽没有身份也没有数据，用封包实体整只填进去。占位槽本来
                // 就没内容，这里仅保住“已经解析出来的名字/数量”这类边角信息。
                if (entering.bufArr.empty()) entering.bufArr = target.bufArr;
                if (entering.name.empty()) entering.name = target.name;
                target = std::move(entering);
            }
            activeIndex = plan.slot;
        }
    } else if (plan.slot != kNoSlot) {
        (*team)[static_cast<size_t>(plan.slot)].state = 1;
        activeIndex = plan.slot;
    } else {
        return;
    }
    // 原出战槽位收成 state = 0（不碰其他槽位，避免把未揭示的一方搞乱）。
    if (previousActive != kNoSlot && previousActive != activeIndex &&
        IsValidSlot(*team, previousActive)) {
        (*team)[static_cast<size_t>(previousActive)].state = 0;
    }

    return;
}

}  // namespace BattleRoster
