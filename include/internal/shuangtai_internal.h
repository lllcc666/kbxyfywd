#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include "packet_types.h"

enum class ShuangTaiState {
    IDLE,
    INITIALIZING,
    JUMPING_LAYER,
    ENTERING_BATTLE,
    IN_BATTLE,
    SWITCHING_PETS,
    ATTACKING,
    ENDING_BATTLE,
    STOPPED
};

class ShuangTaiAutoBattle {
public:
    ShuangTaiAutoBattle();

    bool IsRunning() const { return m_running; }
    ShuangTaiState GetState() const { return m_state; }

    bool Start(bool blockBattle);
    void RequestStop();
    void Stop();
    bool IsStopRequested() const { return m_stopRequested; }
    void Reset();

    void OnBattleStartResponse(const GamePacket& packet);
    void OnBattleRoundStartResponse();
    void OnBattleRoundResultResponse(const GamePacket& packet);
    void OnBattleEndResponse(const GamePacket& packet);

    bool InitializePetData();

private:
    void SendInitPacket();
    void SendJumpLayerPacket();
    void SendEnterBattlePacket();
    void SendBattleOp1Packet();
    void SendSwitchPetPacket();
    void SendSkillAttackPacket();
    void SendBattleEndPacket();
    bool SwitchToNextPet();
    void StartNewRound();
    void UpdateState(ShuangTaiState newState);

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<ShuangTaiState> m_state{ShuangTaiState::IDLE};
    std::atomic<bool> m_blockBattle{false};
    std::atomic<int> m_petIndex{0};
    std::atomic<int> m_attackRound{0};
    std::atomic<int> m_maxAttackCount{8};
    uint32_t m_mainPetId = 0;
    uint32_t m_mainPetSkillId = 0;
    int m_mainPetSkillPP = 8;
    std::vector<uint32_t> m_petIds;
    std::atomic<int> m_totalRounds{0};
};

// 双台谷自动战斗状态唯一 owner。
extern ShuangTaiAutoBattle g_shuangtaiAuto;

uint32_t GetLastSpiritId();
uint32_t GetHighestPowerSkillId(uint32_t spiritId);

