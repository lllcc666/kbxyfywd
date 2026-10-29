/**
 * @file packet_parser.h
 * @brief Packet parser public declarations.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "battle_session.h"
#include "packet_protocol.h"
#include "packet_types.h"
#include "packet_zlib_api.h"

class PacketParser {
public:
    static bool Initialize();
    static void Cleanup();

    static bool ParsePackets(const uint8_t* data, size_t size, BOOL bSend,
                             std::vector<GamePacket>& outPackets,
                             bool* canRewriteInput = nullptr);

    static void ProcessBattlePacket(const GamePacket& packet);
    static void ProcessLingyuPacket(const GamePacket& packet);
    static void ProcessMonsterPacket(const GamePacket& packet);

    // 退出战斗的唯一收口：服务端 BATTLE_END、客户端 BATTLE_END_SEND、
    // 以及 BATTLE_START 解析失败都会走这里，保证 UI 不会停留在上一场。
    // releaseEntities=true 时连实体快照一起释放（客户端已关战斗界面）。
    static void ClearBattleState(bool releaseEntities = false);

    static void SendToUI(const std::wstring& type, const std::wstring& data);
    static void SendBossListToUI();

    // Battle state is exposed as a snapshot so callers cannot retain an
    // unlocked reference while the parser replaces the current battle.
    static BattleData GetCurrentBattleSnapshot() {
        return BattleSession::Instance().Snapshot();
    }

private:
    static std::vector<uint8_t> g_recvBuffer;

    static void ProcessBattlePacketSafe(const GamePacket& packet);
    static void ReplaceCurrentBattle(BattleData battle);

    static bool UncompressBody(const std::vector<uint8_t>& compressed,
                               std::vector<uint8_t>& decompressed);
    static void UpdateUIBattleData();
};

std::wstring GetMapName(int mapId);
int GetSkillPower(int skillId, int fallbackPower);

// 只读查询：数据表在启动期加载完成后不再写入，读侧统一走带锁函数。
std::wstring GetPetName(int petId);
std::wstring GetSkillName(int skillId);
std::wstring GetElemName(int elemId);
std::wstring GetGeniusName(int geniusId);
int GetPetElem(int petId);
int GetSkillRange(int skillId, int fallbackRange = 0);
