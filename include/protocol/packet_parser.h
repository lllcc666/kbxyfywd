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

#include "packet_protocol.h"
#include "packet_types.h"
#include "packet_zlib_api.h"

class PacketParser {
public:
    static bool Initialize();
    static void Cleanup();

    static bool ParsePackets(const uint8_t* data, size_t size, BOOL bSend,
                             std::vector<GamePacket>& outPackets);

    static void ProcessBattlePacket(const GamePacket& packet);
    static void ProcessLingyuPacket(const GamePacket& packet);
    static void ProcessMonsterPacket(const GamePacket& packet);

    static void SendToUI(const std::wstring& type, const std::wstring& data);
    static void SendBossListToUI();

    static BattleData& GetCurrentBattle() { return g_currentBattle; }

private:
    static std::vector<uint8_t> g_recvBuffer;
    static BattleData g_currentBattle;

    static bool UncompressBody(const std::vector<uint8_t>& compressed,
                               std::vector<uint8_t>& decompressed);
    static void UpdateUIBattleData();
};

// Legacy compatibility aliases.
#define MAGIC_NUMBER_D PacketProtocol::MAGIC_NORMAL
#define MAGIC_NUMBER_C PacketProtocol::MAGIC_COMPRESSED

#define OPCODE_BATTLE_START Opcode::BATTLE_START
#define OPCODE_BATTLE_ROUND_START Opcode::BATTLE_ROUND_START
#define OPCODE_BATTLE_ROUND Opcode::BATTLE_ROUND
#define OPCODE_BATTLE_BUF_DIS Opcode::BATTLE_BUF_DIS
#define OPCODE_BATTLE_BUF Opcode::BATTLE_BUF
#define OPCODE_BATTLE_END Opcode::BATTLE_END
#define OPCODE_BATTLE_BUFS Opcode::BATTLE_BUFS
#define OPCODE_BATTLE_FSPK Opcode::BATTLE_FSPK
#define OPCODE_COMBAT_SITE_OPTION Opcode::COMBAT_SITE_OPTION
#define OPCODE_COMBAT_SITE_EFFECT Opcode::COMBAT_SITE_EFFECT
#define OPCODE_COMBAT_NET_PROBE_REQ Opcode::COMBAT_NET_PROBE_REQ
#define OPCODE_COMBAT_NET_REPORT Opcode::COMBAT_NET_REPORT

#define OPCODE_LINGYU_LIST Opcode::LINGYU_LIST
#define OPCODE_DECOMPOSE_RESPONSE Opcode::DECOMPOSE_RESPONSE
#define OPCODE_MONSTER_LIST Opcode::MONSTER_LIST
#define OPCODE_ENTER_WORLD Opcode::ENTER_WORLD

#define OPCODE_DANCE_ACTIVITY_SEND Opcode::DANCE_ACTIVITY_SEND
#define OPCODE_DANCE_ACTIVITY_BACK Opcode::DANCE_ACTIVITY_BACK
#define OPCODE_DANCE_STAGE_SEND Opcode::DANCE_STAGE_SEND
#define OPCODE_DANCE_STAGE_BACK Opcode::DANCE_STAGE_BACK

#define OPCODE_MALL_SEND Opcode::MALL_SEND
#define OPCODE_MALL_BACK Opcode::MALL_BACK

#define OPCODE_SPIRIT_EQUIP_ALL Opcode::SPIRIT_EQUIP_ALL_SEND
#define OPCODE_SPIRIT_EQUIP_ALL_BACK Opcode::SPIRIT_EQUIP_ALL_BACK
#define OPCODE_RESOLVE_SPIRIT Opcode::RESOLVE_SPIRIT_SEND
#define OPCODE_RESOLVE_SPIRIT_BACK Opcode::RESOLVE_SPIRIT_BACK
#define OPCODE_BATCH_RESOLVE_SPIRIT Opcode::BATCH_RESOLVE_SPIRIT_SEND
#define OPCODE_BATCH_RESOLVE_BACK Opcode::BATCH_RESOLVE_BACK

std::wstring GetMapName(int mapId);

extern std::unordered_map<int, std::wstring> g_petNames;
extern std::unordered_map<int, std::wstring> g_skillNames;
extern std::unordered_map<int, std::wstring> g_elemNames;
extern std::unordered_map<int, std::wstring> g_geniusNames;
extern std::unordered_map<int, int> g_skillPowers;
extern std::unordered_map<int, int> g_petElems;
