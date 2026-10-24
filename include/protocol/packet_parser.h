/**
 * @file packet_parser.h
 * @brief Packet parser public declarations.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

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

    static void SendToUI(const std::wstring& type, const std::wstring& data);
    static void SendBossListToUI();

    // Battle state is exposed as a snapshot so callers cannot retain an
    // unlocked reference while the parser replaces the current battle.
    static BattleData GetCurrentBattleSnapshot() {
        std::lock_guard<std::mutex> lock(g_battleMutex);
        return g_currentBattle;
    }

private:
    static std::vector<uint8_t> g_recvBuffer;
    static BattleData g_currentBattle;
    static std::mutex g_battleMutex;

    static void ProcessBattlePacketSafe(const GamePacket& packet);
    static void ReplaceCurrentBattle(BattleData battle);

    static bool UncompressBody(const std::vector<uint8_t>& compressed,
                               std::vector<uint8_t>& decompressed);
    static void UpdateUIBattleData();
};

std::wstring GetMapName(int mapId);
int GetSkillPower(int skillId, int fallbackPower);

extern std::unordered_map<int, std::wstring> g_petNames;
extern std::unordered_map<int, std::wstring> g_skillNames;
extern std::unordered_map<int, std::wstring> g_elemNames;
extern std::unordered_map<int, std::wstring> g_geniusNames;
extern std::unordered_map<int, int> g_skillPowers;
extern std::unordered_map<int, int> g_skillRanges;
extern std::unordered_map<int, int> g_petElems;
