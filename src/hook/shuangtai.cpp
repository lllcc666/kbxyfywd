#include <windows.h>

#include <atomic>
#include <string>
#include <vector>

#include "battle_six.h"
#include "hook_shared.h"
#include "managed_thread.h"
#include "packet_builder.h"
#include "wpe_hook_internal.h"
#include "packet_parser.h"
#include "packet_protocol.h"
#include "shuangtai.h"
#include "shuangtai_internal.h"
#include "ui_bridge.h"
#include "wpe_hook.h"
#include "wpe_hook_internal.h"

extern MonsterData g_monsterData;
extern std::unordered_map<int, int> g_skillPowers;

extern bool PostScriptToUI(const std::wstring& jsCode);

// ============================================================================
// 双台谷刷级功能实现 - 异步状态机模式
// ============================================================================

// 全局状态机实例
ShuangTaiAutoBattle g_shuangtaiAuto;

// 引用妖怪数据和技能威力映射
extern MonsterData g_monsterData;
extern std::unordered_map<int, int> g_skillPowers;

// ========== 工具函数 ==========

uint32_t GetHighestPowerSkillId(uint32_t spiritId) {
    // 先取快照：monster->skills 是指向列表元素的指针，直接指进全局表会被收包线程改掉。
    const MonsterData snapshot = SnapshotMonsterData();
    const MonsterItem* monster = nullptr;
    for (const auto& m : snapshot.monsters) {
        if (static_cast<uint32_t>(m.id) == spiritId) {
            monster = &m;
            break;
        }
    }
    
    if (!monster || monster->skills.empty()) {
        return 0;
    }
    
    int maxPower = -1;
    uint32_t bestSkillId = 0;
    
    for (const auto& skill : monster->skills) {
        auto powerIt = g_skillPowers.find(skill.id);
        int power = (powerIt != g_skillPowers.end()) ? powerIt->second : 0;
        
        if (power > maxPower && power > 0) {
            maxPower = power;
            bestSkillId = skill.id;
        }
    }
    
    return bestSkillId;
}

// ========== ShuangTaiAutoBattle 类实现 ==========

ShuangTaiAutoBattle::ShuangTaiAutoBattle()
    : m_running(false)
    , m_state(ShuangTaiState::IDLE)
    , m_blockBattle(false)
    , m_petIndex(0)
    , m_attackRound(0)
    , m_mainPetId(0)
    , m_mainPetSkillId(0)
    , m_totalRounds(0) {
}

bool ShuangTaiAutoBattle::Start(bool blockBattle) {
    if (m_running) {
        UIBridge::Instance().UpdateHelperText(L"双台谷刷级已在运行中");
        return false;
    }
    
    // 检查妖怪背包数据
    const size_t monsterCount = SnapshotMonsterData().monsters.size();
    if (monsterCount == 0) {
        UIBridge::Instance().UpdateHelperText(L"双台谷刷级：请先点击查询按钮");
        return false;
    }
    
    // 检查妖怪数量（至少需要2只）
    if (monsterCount < 2) {
        UIBridge::Instance().UpdateHelperText(L"双台谷刷级：需要至少2只妖怪");
        return false;
    }
    
    // 初始化宠物数据
    if (!InitializePetData()) {
        UIBridge::Instance().UpdateHelperText(L"双台谷刷级：无法获取主宠技能");
        return false;
    }
    
    // 设置屏蔽战斗
    m_blockBattle = blockBattle;
    if (blockBattle) {
        g_blockBattle = true;
    }
    
    // 设置运行状态
    m_running = true;
    m_totalRounds = 0;
    m_roundActionPending = false;

    
    UIBridge::Instance().UpdateHelperText(L"双台谷刷级：开始运行，共 " + 
        std::to_wstring(monsterCount) + L" 只妖怪...");
    
    // 开始流程：发送初始化封包
    SendInitPacket();
    
    return true;
}

void ShuangTaiAutoBattle::RequestStop() {
    if (!m_running) return;
    
    // 设置请求停止标志，等待当前轮次完成后再停止
    m_stopRequested = true;
    UIBridge::Instance().UpdateHelperText(L"双台谷刷级：将在本轮完成后停止...");
}

void ShuangTaiAutoBattle::Stop() {
    m_running = false;
    m_stopRequested = false;
    
    // 恢复屏蔽战斗状态
    if (m_blockBattle) {
        g_blockBattle = false;
    }
    
    UpdateState(ShuangTaiState::STOPPED);
    UIBridge::Instance().UpdateHelperText(L"双台谷刷级：已停止");
}

void ShuangTaiAutoBattle::Reset() {
    m_running = false;
    m_stopRequested = false;
    m_state = ShuangTaiState::IDLE;
    m_blockBattle = false;
    m_petIndex = 0;
    m_attackRound = 0;
    m_mainPetId = 0;
    m_mainPetSkillId = 0;
    m_petIds.clear();
    m_totalRounds = 0;
    m_roundActionPending = false;
}



bool ShuangTaiAutoBattle::InitializePetData() {
    const MonsterData snapshot = SnapshotMonsterData();
    if (snapshot.monsters.empty()) {
        return false;
    }

    // 获取最后一只妖怪作为主宠
    m_mainPetId = snapshot.monsters.back().id;
    
    // 获取所有妖怪ID（包括主宠，都需要切换上场获得经验）
    // 第一只已出战不需要切换，所以 m_petIds 包含第2只到最后一只（主宠）
    m_petIds.clear();
    for (size_t i = 1; i < snapshot.monsters.size(); i++) {
        m_petIds.push_back(snapshot.monsters[i].id);
    }
    
    // 获取主宠最高威力技能及其PP值
    const MonsterItem* monster = nullptr;
    for (const auto& m : snapshot.monsters) {
        if (static_cast<uint32_t>(m.id) == m_mainPetId) {
            monster = &m;
            break;
        }
    }
    
    if (!monster || monster->skills.empty()) {
        return false;
    }
    
    // 查找威力最高的技能
    int maxPower = -1;
    int bestSkillIndex = -1;
    
    for (size_t i = 0; i < monster->skills.size(); i++) {
        const auto& skill = monster->skills[i];
        auto powerIt = g_skillPowers.find(skill.id);
        int power = (powerIt != g_skillPowers.end()) ? powerIt->second : 0;
        
        if (power > maxPower && power > 0) {
            maxPower = power;
            bestSkillIndex = static_cast<int>(i);
        }
    }
    
    if (bestSkillIndex < 0) {
        return false;
    }
    
    m_mainPetSkillId = monster->skills[bestSkillIndex].id;
    m_mainPetSkillPP = monster->skills[bestSkillIndex].maxPp;
    
    // 设置最大攻击次数为技能PP值
    m_maxAttackCount = m_mainPetSkillPP > 0 ? m_mainPetSkillPP : 8;
    
    return m_mainPetSkillId != 0;
}

void ShuangTaiAutoBattle::UpdateState(ShuangTaiState newState) {
    m_state = newState;
}

// ========== 封包发送方法 ==========

void ShuangTaiAutoBattle::SendInitPacket() {
    if (!m_running) return;
    
    UpdateState(ShuangTaiState::INITIALIZING);
    UIBridge::Instance().UpdateHelperText(L"双台谷刷级：发送初始化封包...");
    
    auto packet = PacketBuilder()
        .SetOpcode(ShuangTai::INIT_SEND)
        .SetParams(1)
        .Build();
    SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
    
    // 延迟后发送跳层封包
    struct DelayedData { ShuangTaiAutoBattle* self; };
    DelayedData* data = new DelayedData{this};
    
    HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
        auto* d = static_cast<DelayedData*>(lpParam);
        Sleep(300);
        if (d->self->IsRunning()) {
            d->self->SendJumpLayerPacket();
        }
        delete d;
        return 0;
    }, data, 0, nullptr);
    
    if (hThread) CloseHandle(hThread);
}

void ShuangTaiAutoBattle::SendJumpLayerPacket() {
    if (!m_running) return;
    
    UpdateState(ShuangTaiState::JUMPING_LAYER);
    UIBridge::Instance().UpdateHelperText(L"双台谷刷级：跳层到第一层...");
    
    auto packet = PacketBuilder()
        .SetOpcode(ShuangTai::JUMP_LAYER_SEND)
        .SetParams(2)
        .WriteInt32(1)  // 第一层
        .Build();
    SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
    
    // 延迟后发送进入战斗封包
    struct DelayedData { ShuangTaiAutoBattle* self; };
    DelayedData* data = new DelayedData{this};
    
    HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
        auto* d = static_cast<DelayedData*>(lpParam);
        Sleep(300);
        if (d->self->IsRunning()) {
            d->self->SendEnterBattlePacket();
        }
        delete d;
        return 0;
    }, data, 0, nullptr);
    
    if (hThread) CloseHandle(hThread);
}

void ShuangTaiAutoBattle::SendEnterBattlePacket() {
    if (!m_running) return;
    
    UpdateState(ShuangTaiState::ENTERING_BATTLE);
    UIBridge::Instance().UpdateHelperText(L"双台谷刷级：进入战斗...");
    
    auto packet = PacketBuilder()
        .SetOpcode(ShuangTai::ENTER_BATTLE_SEND)
        .SetParams(0)
        .Build();
    SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
    
    // 战斗开始响应由 OnBattleStartResponse 处理
}

void ShuangTaiAutoBattle::SendBattleOp1Packet() {
    if (!m_running) return;
    
    auto packet = PacketBuilder()
        .SetOpcode(ShuangTai::BATTLE_OP1_SEND)
        .SetParams(0)
        .Build();
    SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

void ShuangTaiAutoBattle::SendSwitchPetPacket() {
    if (!m_running || m_petIndex >= static_cast<int>(m_petIds.size())) return;
    
    UpdateState(ShuangTaiState::SWITCHING_PETS);
    
    uint32_t uniqueId = m_petIds[m_petIndex];
    UIBridge::Instance().UpdateHelperText(L"双台谷刷级：切换宠物 " + 
        std::to_wstring(m_petIndex + 1) + L"/" + std::to_wstring(m_petIds.size()) + L"...");
    
    auto packet = PacketBuilder()
        .SetOpcode(ShuangTai::USER_OP_SEND)
        .SetParams(1)  // 切换精灵操作
        .WriteInt32(uniqueId)
        .Build();
    SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
    m_roundActionPending = true;

    // 切换宠物响应由 OnBattleRoundResultResponse 处理
}



void ShuangTaiAutoBattle::SendSkillAttackPacket() {
    if (!m_running) return;
    
    UpdateState(ShuangTaiState::ATTACKING);
    UIBridge::Instance().UpdateHelperText(L"双台谷刷级：主宠攻击 " + 
        std::to_wstring(m_attackRound + 1) + L"/" + std::to_wstring(m_maxAttackCount.load()) + L"...");

    int targetSid = 0;
    const BattleData battleData = PacketParser::GetCurrentBattleSnapshot();
    if (battleData.otherActiveIndex >= 0 &&
        battleData.otherActiveIndex < static_cast<int>(battleData.otherPets.size())) {
        targetSid = battleData.otherPets[battleData.otherActiveIndex].sid;
    }
    if (targetSid <= 0) {
        targetSid = 2;
    }
    
    auto packet = PacketBuilder()
        .SetOpcode(ShuangTai::USER_OP_SEND)
        .SetParams(0)  // 技能攻击操作
        .WriteInt32(targetSid)  // 目标 sid
        .WriteInt32(m_mainPetSkillId)
        .Build();
    SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
    m_roundActionPending = true;

    // 技能攻击响应由 OnBattleRoundResultResponse 处理
}



void ShuangTaiAutoBattle::SendBattleEndPacket() {
    if (!m_running) return;
    
    UpdateState(ShuangTaiState::ENDING_BATTLE);
    UIBridge::Instance().UpdateHelperText(L"双台谷刷级：发送战斗结束封包...");
    
    // AS3 clears a normal battle with OP_GATEWAY_BATTLE_END (1186323).
    if (!SendNormalBattleEndPacket()) {
        UIBridge::Instance().UpdateHelperText(L"双台谷刷级：战斗结束确认发送失败");
    }
    
    // 战斗结束响应由 OnBattleEndResponse 处理
}

// ========== 状态转换方法 ==========

bool ShuangTaiAutoBattle::SwitchToNextPet() {
    if (m_petIndex < static_cast<int>(m_petIds.size())) {
        // 还有宠物需要切换
        SendSwitchPetPacket();
        return true;
    } else {
        // 所有宠物已切换完毕，开始主宠攻击
        m_attackRound = 0;
        SendSkillAttackPacket();
        return false;
    }
}

void ShuangTaiAutoBattle::StartNewRound() {
    // 重置回合状态
    // 注意：petIndex 会在 OnBattleStartResponse 中初始化
    m_attackRound = 0;
    
    UIBridge::Instance().UpdateHelperText(L"双台谷刷级：开始第 " + 
        std::to_wstring(m_totalRounds + 1) + L" 轮...");
    
    // 发送初始化封包开始新一轮
    SendInitPacket();
}

// ========== 响应处理方法 ==========

void ShuangTaiAutoBattle::OnBattleStartResponse(const GamePacket& packet) {
    (void)packet;
    if (!m_running) return;

    UpdateState(ShuangTaiState::IN_BATTLE);
    UIBridge::Instance().UpdateHelperText(L"双台谷刷级：战斗开始，等待回合开始...");


    // m_petIds 包含第2只到最后一只（主宠）
    // 第一只妖怪已自动出战，从 m_petIds[0]（第2只）开始切换
    m_petIndex = 0;
    m_attackRound = 0;
    m_roundActionPending = false;

    // AS3 BattleControl.onSpiritEnterOver: send READY (1186049), then wait for
    // BATTLE_ROUND_START before any USER_OP. Switching here races the first round.
    SendBattleOp1Packet();
}

void ShuangTaiAutoBattle::OnBattleRoundStartResponse() {
    if (!m_running) return;
    if (m_roundActionPending.load()) return;

    const ShuangTaiState currentState = m_state.load();

    auto delayCall = [](ShuangTaiAutoBattle* self, int action) {
        struct DelayedData {
            ShuangTaiAutoBattle* self;
            int action;
        };
        DelayedData* data = new DelayedData{self, action};
        HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
            auto* d = static_cast<DelayedData*>(lpParam);
            Sleep(300);
            if (d->self->IsRunning() && !d->self->m_roundActionPending.load()) {
                if (d->action == 0) {
                    d->self->SwitchToNextPet();
                } else if (d->action == 1) {
                    d->self->SendSkillAttackPacket();
                } else {
                    d->self->SendBattleEndPacket();
                }
            }
            delete d;
            return 0;
        }, data, 0, nullptr);
        if (hThread) CloseHandle(hThread);
        else delete data;
    };

    if (currentState == ShuangTaiState::IN_BATTLE ||
        currentState == ShuangTaiState::SWITCHING_PETS) {
        delayCall(this, 0);
        return;
    }

    if (currentState == ShuangTaiState::ATTACKING) {
        m_attackRound++;
        if (m_attackRound < m_maxAttackCount.load()) {
            delayCall(this, 1);
        } else {
            delayCall(this, 2);
        }
    }
}


void ShuangTaiAutoBattle::OnBattleRoundResultResponse(const GamePacket& packet) {
    if (!m_running) return;

    // packet.params 表示 cmdType
    // 0 = 普通攻击, 1 = 切换宠物, 2 = 使用道具, 3 = 逃跑
    int cmdType = static_cast<int>(packet.params);

    // AS3 onBattlePlayOver: send PLAY_OVER (1186056) after the round result,
    // then wait for the next BATTLE_ROUND_START before the next USER_OP.
    auto ackPacket = PacketBuilder()
        .SetOpcode(ShuangTai::BATTLE_OP2_SEND)
        .SetParams(0)
        .Build();
    SendPacket(0, ackPacket.data(), static_cast<DWORD>(ackPacket.size()));
    m_roundActionPending = false;

    if (cmdType == 1 && m_state.load() == ShuangTaiState::SWITCHING_PETS) {
        m_petIndex++;
    }
}


void ShuangTaiAutoBattle::OnBattleEndResponse(const GamePacket& packet) {
    if (!m_running) return;
    (void)packet;
    m_roundActionPending = false;

    m_totalRounds++;
    UIBridge::Instance().UpdateHelperText(L"双台谷刷级：第 " + 
        std::to_wstring(m_totalRounds) + L" 轮完成");
    
    // 检查是否请求停止
    if (m_stopRequested) {
        // 完成当前轮次后停止
        m_running = false;
        m_stopRequested = false;
        
        // 恢复屏蔽战斗状态
        if (m_blockBattle) {
            g_blockBattle = false;
        }
        
        UpdateState(ShuangTaiState::STOPPED);
        UIBridge::Instance().UpdateHelperText(L"双台谷刷级：已完成 " + 
            std::to_wstring(m_totalRounds) + L" 轮，已停止");
        return;
    }
    
    UIBridge::Instance().UpdateHelperText(L"双台谷刷级：第 " + 
        std::to_wstring(m_totalRounds) + L" 轮完成，准备下一轮...");
    
    // 延迟后开始新一轮
    struct DelayedData { ShuangTaiAutoBattle* self; };
    DelayedData* data = new DelayedData{this};
    
    HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
        auto* d = static_cast<DelayedData*>(lpParam);
        Sleep(300);
        if (d->self->IsRunning()) {
            d->self->StartNewRound();
        }
        delete d;
        return 0;
    }, data, 0, nullptr);
    
    if (hThread) CloseHandle(hThread);
}

// ========== 外部接口函数 ==========

BOOL StartOneKeyShuangTaiPacket(bool blockBattle) {
    return g_shuangtaiAuto.Start(blockBattle) ? TRUE : FALSE;
}

void StopShuangTai() {
    g_shuangtaiAuto.RequestStop();
}

// 查询相关的全局变量（用于异步查询）
bool g_shuangtaiWaitingForMonsterData = false;

BOOL QueryShuangTaiMonsters() {
    UIBridge::Instance().UpdateHelperText(L"双台谷：正在查询妖怪数据...");
    
    // 每次都重新查询，确保获取最新的妖怪背包数据
    // 设置标志，等待响应后更新UI
    g_shuangtaiWaitingForMonsterData = true;
    
    // 发送查询妖怪背包封包
    SendQueryMonsterPacket();
    
    return TRUE;
}

void UpdateShuangTaiUIFromMonsterData() {
    // 检查妖怪数量
    const MonsterData snapshot = SnapshotMonsterData();
    size_t petCount = snapshot.monsters.size();
    if (petCount < 2) {
        UIBridge::Instance().UpdateHelperText(L"双台谷：需要至少2只妖怪，当前只有 " + std::to_wstring(petCount) + L" 只");
        return;
    }
    
    // 获取最后一只妖怪信息（主宠）
    const auto& mainPet = snapshot.monsters.back();
    std::wstring petName = mainPet.name;
    
    // 获取最高威力技能名称
    uint32_t bestSkillId = GetHighestPowerSkillId(mainPet.id);
    std::wstring skillName = L"未知技能";
    for (const auto& skill : mainPet.skills) {
        if (static_cast<uint32_t>(skill.id) == bestSkillId) {
            skillName = skill.name;
            break;
        }
    }
    
    // 更新UI
    std::wstring jsScript = L"if(window.updateShuangTaiUI) { window.updateShuangTaiUI('" 
        + petName + L"', '" + skillName + L"', " + std::to_wstring(petCount) + L", true); }";
    
    if (g_hWnd) {
        UIBridge::Instance().ExecuteJS(jsScript);
    }
    
    UIBridge::Instance().UpdateHelperText(L"双台谷：已读取 " + std::to_wstring(petCount) + L" 只妖怪，主宠: " + petName);
}

// ========== 响应处理函数（注册到 ResponseDispatcher）==========

void ProcessShuangTaiBattleStartResponse(const GamePacket& packet) {
    g_shuangtaiAuto.OnBattleStartResponse(packet);
}

void ProcessShuangTaiBattleRoundStartResponse(const GamePacket& packet) {
    (void)packet;
    g_shuangtaiAuto.OnBattleRoundStartResponse();
}

void ProcessShuangTaiBattleRoundResultResponse(const GamePacket& packet) {
    g_shuangtaiAuto.OnBattleRoundResultResponse(packet);
}

void ProcessShuangTaiBattleEndResponse(const GamePacket& packet) {
    g_shuangtaiAuto.OnBattleEndResponse(packet);
}
