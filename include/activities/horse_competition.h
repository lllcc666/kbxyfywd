#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "packet_types.h"

using HorseProgressCallback = void (*)(const std::wstring&);

BOOL SendHorseCompetitionPacket(
    const std::string& operation,
    const std::vector<int32_t>& bodyValues = {},
    bool useGameCmd = false
);

BOOL SendHorseJoinGamePacket();
BOOL SendHorseReadyPacket();
BOOL SendHorseExitRoomPacket();
BOOL SendHorseUIInfoPacket();
BOOL SendHorsePlayGamePacket(int distance);
BOOL StartOneKeyHorseCompetitionPacket();
void RequestStopHorseCompetition();
void SetHorseProgressCallback(HorseProgressCallback callback);
void ProcessHorseCompetitionResponse(const GamePacket& packet);
BOOL StartHorseCompetitionGame();
void StopHorseCompetitionGame();
