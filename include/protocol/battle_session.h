/**
 * @file battle_session.h
 * @brief 战斗会话状态唯一 owner。
 *
 * 之前 `BattleData`、`g_battleStarted`、各活动自己的“是否在战斗”标志
 * 分散在 packet_parser / wpe_hook / battle_six / eight_trigrams 里，
 * 退出战斗时任何一条路径漏掉就会留下上一场的 UI。
 *
 * 现在所有战斗状态都经由 BattleSession：
 *   - Begin()   新一场开始，分配 sessionId；
 *   - Replace() 战斗中更新快照；
 *   - End()     服务端结束 / 客户端关闭 / 开场失败；
 *   - Snapshot() 调用方只拿拷贝，不持有锁内引用。
 */
#pragma once

#include <cstdint>
#include <mutex>

#include "packet_types.h"


class BattleSession {
public:
    static BattleSession& Instance();

    /// 开始新一场：active=true，sessionId 自增。
    uint64_t Begin(BattleData battle);
    /// 战斗中替换快照（不改变 sessionId / active）。
    void Replace(BattleData battle);
    /// 结束会话；releaseEntities=true 时连实体一起释放。sessionId 自增，
    /// 使清空前已经排队的旧 UI 数据失效。
    uint64_t End(bool releaseEntities);
    /// 彻底复位（退出程序 / 解析器 Cleanup）。
    void Reset();

    /// 同一把锁内取快照和会话号，避免“快照是旧局、会话号是新局”的竞态。
    BattleData Snapshot(uint64_t* sessionId = nullptr) const;
    bool IsActive() const;

private:
    BattleSession() = default;

    mutable std::mutex m_mutex;
    BattleData m_battle;
    uint64_t m_sessionId = 0;
};
