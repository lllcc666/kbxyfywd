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
BOOL SendHorseRoomInfoPacket();
BOOL SendHorseReadyPacket();
BOOL SendHorseExitRoomPacket();
BOOL SendHorseUIInfoPacket();
BOOL SendHorseExchangeInfoPacket();
BOOL SendHorseExchangePacket(int exchangeId, int count);
BOOL SendHorsePlayGamePacket(int distance);
BOOL SendHorseUseItemPacket(int itemIdx);
BOOL SendHorseGetRegressionPacket(int idx);
BOOL StartOneKeyHorseCompetitionPacket(bool useTempMount = true);
void RequestStopHorseCompetition();
void SetHorseProgressCallback(HorseProgressCallback callback);
void ProcessHorseCompetitionResponse(const GamePacket& packet);
BOOL StartHorseCompetitionGame();
void StopHorseCompetitionGame();
