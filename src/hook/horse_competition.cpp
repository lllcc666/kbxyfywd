#include "horse_competition.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "activity_states_internal.h"
#include "packet_builder.h"

extern bool PostScriptToUI(const std::wstring& jsCode);

namespace {

// 坐骑大赛模块 owner：运行线程、运行标记和进度回调统一由这里维护。
std::thread g_horseGameThread;
std::atomic<bool> g_horseGameRunning{false};
HorseProgressCallback g_horseProgressCallback = nullptr;

bool TryParseUInt32Decimal(const std::string& text, uint32_t& value) {
    if (text.empty()) {
        return false;
    }

    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }
    if (start >= text.size()) {
        return false;
    }

    char* end = nullptr;
    const char* begin = text.c_str() + start;
    const unsigned long parsed = std::strtoul(begin, &end, 10);
    if (end == begin) {
        return false;
    }

    while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end))) {
        ++end;
    }
    if (*end != '\0' && *end != ',' && *end != '}' && *end != ']') {
        return false;
    }

    value = static_cast<uint32_t>(parsed);
    return true;
}

bool TryParseIntDecimal(const std::string& text, int& value) {
    if (text.empty()) {
        return false;
    }

    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }
    if (start >= text.size()) {
        return false;
    }

    char* end = nullptr;
    const char* begin = text.c_str() + start;
    const long parsed = std::strtol(begin, &end, 10);
    if (end == begin) {
        return false;
    }

    while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end))) {
        ++end;
    }
    if (*end != '\0' && *end != ',' && *end != '}' && *end != ']') {
        return false;
    }

    value = static_cast<int>(parsed);
    return true;
}

void NotifyHorseProgress(const std::wstring& msg) {
    if (g_horseProgressCallback) {
        g_horseProgressCallback(msg);
    }
}

namespace HorseCmd {

const std::string JOIN_GAME = "join_game";
const std::string ROOM_INFO = "room_info";
const std::string READY = "ready";
const std::string EXIT_ROOM = "exit_room";
const std::string UI_INFO = "ui_info";
const std::string EXCHANGE_INFO = "exchange_info";
const std::string EXCHANGE = "exchange";
const std::string PLAY_GAME = "play_game";
const std::string USE_ITEM = "use_item";
const std::string SYNC_MEMBER = "sync_member";
const std::string ROOM_STATUS = "room_status";
const std::string END_GAME = "end_game";
const std::string START_GAME = "start_game";
const std::string GET_REGRESSION = "back_pack_award";

}  // namespace HorseCmd

std::vector<uint8_t> BuildHorsePacketBody(
    const std::string& operation,
    const std::vector<int32_t>& bodyValues = {}) {
    std::vector<uint8_t> body;

    const uint16_t cmdLen = static_cast<uint16_t>(operation.length());
    body.push_back(static_cast<uint8_t>(cmdLen & 0xFF));
    body.push_back(static_cast<uint8_t>((cmdLen >> 8) & 0xFF));
    body.insert(body.end(), operation.begin(), operation.end());

    for (int32_t value : bodyValues) {
        body.push_back(static_cast<uint8_t>(value & 0xFF));
        body.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        body.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        body.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    }

    return body;
}

std::vector<uint8_t> BuildHorsePacketBodyWithJson(
    const std::string& operation,
    const std::string& jsonData) {
    std::vector<uint8_t> body = BuildHorsePacketBody(operation);
    const uint16_t jsonLen = static_cast<uint16_t>(jsonData.length());
    body.push_back(static_cast<uint8_t>(jsonLen & 0xFF));
    body.push_back(static_cast<uint8_t>((jsonLen >> 8) & 0xFF));
    body.insert(body.end(), jsonData.begin(), jsonData.end());
    return body;
}

std::vector<uint8_t> WrapHorsePacketBody(const std::vector<uint8_t>& body, bool useGameCmd) {
    const uint32_t opcode = useGameCmd ? Opcode::HORSE_GAME_CMD_SEND : Opcode::HORSE_COMPETITION_SEND;

    return PacketBuilder()
        .SetOpcode(opcode)
        .SetParams(Opcode::HORSE_COMPETITION_ACT_ID)
        .WriteBytes(body)
        .Build();
}

BOOL SendHorsePacketBody(const std::vector<uint8_t>& body, bool useGameCmd) {
    const std::vector<uint8_t> packet = WrapHorsePacketBody(body, useGameCmd);
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

bool ReadHorseJsonPayload(const GamePacket& packet, size_t& offset, std::string& json) {
    if (offset + 2 > packet.body.size()) {
        return false;
    }

    const uint16_t jsonLen = ReadUInt16LE(packet.body.data(), offset);
    if (offset + jsonLen > packet.body.size()) {
        return false;
    }

    json.assign(reinterpret_cast<const char*>(packet.body.data() + offset), jsonLen);
    offset += jsonLen;
    return true;
}

void SetHorsePhase(HorseCompetitionState& state, HorseCompetitionPhase phase) {
    state.phase = phase;
    state.inRoom = (phase == HORSE_PHASE_JOINING_ROOM || phase == HORSE_PHASE_WAITING_START ||
                    phase == HORSE_PHASE_GAMING || phase == HORSE_PHASE_SETTLING);
    state.isGaming = (phase == HORSE_PHASE_GAMING);
    state.isSettling = (phase == HORSE_PHASE_SETTLING);
    state.isFinished = (phase == HORSE_PHASE_FINISHED);
}

void ResetHorseRoundState(HorseCompetitionState& state) {
    SetHorsePhase(state, HORSE_PHASE_IDLE);
    state.inRoom = false;
    state.isGaming = false;
    state.isFinished = false;
    state.isSettling = false;
    state.receivedEndGame = false;
    state.localFinished = false;
    state.abnormalRoundEnd = false;
    state.distance = 0.0;
    state.syncCount = 0;
    state.resDayPoint = 0;
    state.isRide = 0;
    state.uiInfoReceived = false;
    state.myInfo.Reset();
}

BOOL WaitForHorseFlag(const std::atomic<bool>& flag, DWORD timeoutMs) {
    const auto start = std::chrono::steady_clock::now();
    while (!flag.load()) {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if (elapsed > timeoutMs) {
            return FALSE;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return TRUE;
}

void HorseGameMainLoop() {
    auto& state = ActivityStateManager::Instance().GetHorseCompetitionState();

    const auto frameDuration = std::chrono::duration<double, std::milli>(1000.0 / 60.0);
    const double ahp = 0.84;
    const double mhp = 3.33;

    constexpr double kTargetFinishSeconds = 36.0;
    constexpr int kSyncFrames = HorseCompetitionState::SYNC_INTERVAL;
    constexpr int kJumpFrames = 18;
    constexpr int kJumpCooldownFrames = 150;

    int frameCount = 0;
    int accHoldFrames = 0;
    int accCooldownFrames = 0;
    int jumpHoldFrames = 0;
    int jumpCooldownFrames = kJumpCooldownFrames;
    size_t nextItemIndex = 0;
    const auto raceStartTime = std::chrono::steady_clock::now();

    while (g_horseGameRunning && state.isGaming) {
        auto frameStart = std::chrono::steady_clock::now();
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(frameStart - raceStartTime).count();
        const double elapsedSeconds = static_cast<double>(elapsedMs) / 1000.0;
        const double desiredDistance = (std::min)(
            static_cast<double>(HorseCompetitionState::ROUTE_DISTANCE),
            HorseCompetitionState::ROUTE_DISTANCE * (elapsedSeconds / kTargetFinishSeconds));
        const double distanceGap = desiredDistance - state.distance;

        frameCount++;

        if (state.canControl) {
            if (accCooldownFrames > 0) {
                --accCooldownFrames;
            }
            if (jumpCooldownFrames > 0) {
                --jumpCooldownFrames;
            }

            if (jumpHoldFrames > 0) {
                state.state = "JUMP";
                --jumpHoldFrames;
                if (jumpHoldFrames == 0) {
                    state.state = state.lastState.empty() ? "RUN" : state.lastState;
                    if (state.state != "ACCRUN") {
                        state.state = "RUN";
                    }
                    jumpCooldownFrames = kJumpCooldownFrames;
                }
            } else {
                bool shouldJumpForItem = false;
                bool suppressAccForItem = false;
                if (nextItemIndex < state.itemDistances.size()) {
                    const int nextItemDistance = state.itemDistances[nextItemIndex];
                    const double distanceToItem = static_cast<double>(nextItemDistance) - state.distance;
                    if (distanceToItem < -40.0) {
                        ++nextItemIndex;
                    } else {
                        shouldJumpForItem = distanceToItem >= 18.0 && distanceToItem <= 42.0;
                        suppressAccForItem = distanceToItem >= -10.0 && distanceToItem <= 72.0;
                    }
                }

                if (state.state == "ACCRUN") {
                    --accHoldFrames;
                    if (accHoldFrames <= 0 || state.hp <= state.maxHp * 0.34 || suppressAccForItem || distanceGap < -6.0) {
                        state.state = "RUN";
                        accCooldownFrames = 18;
                    }
                } else {
                    state.state = "RUN";
                    const bool canAccNow = accCooldownFrames <= 0 && state.hp >= state.maxHp * 0.5;
                    if (canAccNow && !suppressAccForItem && distanceGap > 7.5) {
                        state.lastState = "RUN";
                        state.state = "ACCRUN";
                        accHoldFrames = (distanceGap > 16.0) ? 36 : 18;
                    }
                }

                const bool canJumpNow = jumpCooldownFrames <= 0;
                if (canJumpNow && shouldJumpForItem) {
                    state.lastState = state.state;
                    jumpHoldFrames = kJumpFrames;
                    state.state = "JUMP";
                    --jumpHoldFrames;
                    if (jumpHoldFrames <= 0) {
                        state.state = state.lastState.empty() ? "RUN" : state.lastState;
                        jumpCooldownFrames = kJumpCooldownFrames;
                    }
                }
            }

            double distanceDelta = 0.0;
            if (state.state == "RUN") {
                state.hp += ahp;
                distanceDelta = state.speed;
            } else if (state.state == "ACCRUN") {
                state.hp -= mhp;
                distanceDelta = state.accSpeed;
            } else if (state.state == "JUMP") {
                if (state.lastState == "ACCRUN") {
                    state.hp -= mhp;
                    distanceDelta = state.accSpeed;
                } else {
                    state.hp += ahp;
                    distanceDelta = state.speed;
                }
            }

            state.distance += distanceDelta;

            if (state.hp > state.maxHp) {
                state.hp = state.maxHp;
            }
            if (state.hp <= 0) {
                state.hp = 0;
                state.state = "RUN";
                accHoldFrames = 0;
                accCooldownFrames = 48;
            }

            if (state.distance >= HorseCompetitionState::ROUTE_DISTANCE) {
                state.distance = HorseCompetitionState::ROUTE_DISTANCE;
                if (!state.localFinished) {
                    state.localFinished = true;
                    SendHorsePlayGamePacket(static_cast<int>(state.distance));
                }
            }

            if (state.localFinished && state.receivedEndGame) {
                state.isGaming = false;
                state.isFinished = true;
                break;
            }

            state.syncCount++;
            if (state.syncCount >= kSyncFrames) {
                state.syncCount = 0;
                const int distToSend = static_cast<int>(state.distance);
                SendHorsePlayGamePacket(distToSend);
            }
        }

        auto frameEnd = std::chrono::steady_clock::now();
        const auto elapsed = frameEnd - frameStart;
        if (elapsed < frameDuration) {
            std::this_thread::sleep_for(frameDuration - elapsed);
        }
    }
}

int ExtractJsonInt(const std::string& json, const std::string& key) {
    const size_t pos = json.find("\"" + key + "\":");
    if (pos != std::string::npos) {
        int value = 0;
        if (TryParseIntDecimal(json.substr(pos + key.length() + 3), value)) {
            return value;
        }
    }
    return 0;
}

uint32_t ExtractJsonUInt(const std::string& json, const std::string& key) {
    const size_t pos = json.find("\"" + key + "\":");
    if (pos != std::string::npos) {
        uint32_t value = 0;
        if (TryParseUInt32Decimal(json.substr(pos + key.length() + 3), value)) {
            return value;
        }
    }
    return 0;
}

std::string ExtractJsonObjectByPlayerId(const std::string& json, uint32_t playerId, int* outRank = nullptr) {
    const std::string playerIdToken = "\"player_id\":" + std::to_string(playerId);
    size_t searchPos = 0;
    int rank = 0;

    while (true) {
        const size_t objectStart = json.find('{', searchPos);
        if (objectStart == std::string::npos) {
            break;
        }

        size_t depthPos = objectStart;
        int depth = 0;
        bool inString = false;
        bool escape = false;
        size_t objectEnd = std::string::npos;

        while (depthPos < json.size()) {
            const char ch = json[depthPos];
            if (escape) {
                escape = false;
            } else if (ch == '\\') {
                escape = true;
            } else if (ch == '"') {
                inString = !inString;
            } else if (!inString) {
                if (ch == '{') {
                    depth++;
                } else if (ch == '}') {
                    depth--;
                    if (depth == 0) {
                        objectEnd = depthPos;
                        break;
                    }
                }
            }
            depthPos++;
        }

        if (objectEnd == std::string::npos) {
            break;
        }

        rank++;
        const std::string objectJson = json.substr(objectStart, objectEnd - objectStart + 1);
        if (objectJson.find(playerIdToken) != std::string::npos) {
            if (outRank) {
                *outRank = rank;
            }
            return objectJson;
        }

        searchPos = objectEnd + 1;
    }

    if (outRank) {
        *outRank = 0;
    }
    return "";
}

std::vector<int> ExtractItemDistances(const std::string& json) {
    std::vector<int> distances;
    const size_t itemsPos = json.find("\"items\":[");
    if (itemsPos == std::string::npos) {
        return distances;
    }

    size_t searchPos = itemsPos;
    while (true) {
        const size_t pos = json.find("\"distance\":", searchPos);
        if (pos == std::string::npos) {
            break;
        }

        int value = 0;
        if (TryParseIntDecimal(json.substr(pos + 11), value) && value > 0 && value < HorseCompetitionState::ROUTE_DISTANCE) {
            distances.push_back(value);
        }
        searchPos = pos + 11;
    }

    std::sort(distances.begin(), distances.end());
    distances.erase(std::unique(distances.begin(), distances.end()), distances.end());
    return distances;
}

std::wstring MakeJsonPreview(const std::string& json, size_t maxLen = 220) {
    std::wstring preview(json.begin(), json.end());
    if (preview.size() > maxLen) {
        preview = preview.substr(0, maxLen) + L"...";
    }
    return preview;
}

void ApplyHorseRoomStateFromJson(HorseCompetitionState& state, const std::string& json) {
    state.roomId = ExtractJsonInt(json, "id");
    state.roomStatus = ExtractJsonInt(json, "status");
    state.updateTime = static_cast<double>(ExtractJsonInt(json, "update_time"));
    state.startTime = static_cast<double>(ExtractJsonInt(json, "start_time"));
}

void HandleHorseRoomInfoResponse(HorseCompetitionState& state, const std::string& json) {
    ApplyHorseRoomStateFromJson(state, json);
    state.inRoom = true;
    if (state.phase < HORSE_PHASE_GAMING) {
        SetHorsePhase(state, HORSE_PHASE_WAITING_START);
    }

    const size_t membersPos = json.find("\"members\":");
    if (membersPos != std::string::npos) {
        state.otherMembers.clear();
    }
}

void HandleHorseStartGameResponse(HorseCompetitionState& state, const std::string& json) {
    ApplyHorseRoomStateFromJson(state, json);
    if (!state.isGaming) {
        StartHorseCompetitionGame();
    }
}

void HandleHorseJoinGameResponse(HorseCompetitionState& state, const std::string& json) {
    const size_t horseInfoPos = json.find("\"horse_info\":");
    if (horseInfoPos != std::string::npos) {
        const std::string horseInfoJson = json.substr(horseInfoPos);
        state.myInfo.horse_base_Hp = ExtractJsonInt(horseInfoJson, "base_power");
        state.myInfo.horse_base_speed = ExtractJsonInt(horseInfoJson, "base_speed");
        state.myInfo.horse_base_intimate = ExtractJsonInt(horseInfoJson, "base_intimate");
    }

    state.itemDistances = ExtractItemDistances(json);
    state.inRoom = true;
    SetHorsePhase(state, HORSE_PHASE_WAITING_START);
}

void HandleHorseReadyResponse(HorseCompetitionState& state, const std::string& json) {
    const uint32_t playerId = ExtractJsonUInt(json, "player_id");
    if (playerId == g_userId.load()) {
        state.myInfo.status = HORSE_ROOM_READY;
    }
}

void HandleHorseRoomStatusResponse(HorseCompetitionState& state, const std::string& json) {
    ApplyHorseRoomStateFromJson(state, json);

    if (state.roomStatus == HORSE_ROOM_READY) {
        state.myInfo.status = HORSE_ROOM_READY;
    } else if (state.roomStatus == HORSE_ROOM_SETTLE) {
        SetHorsePhase(state, HORSE_PHASE_SETTLING);
    }
}

void HandleHorseUiInfoResponse(HorseCompetitionState& state, const GamePacket& packet, size_t& offset) {
    if (offset + 20 > packet.body.size()) {
        return;
    }

    state.cnt = ReadInt32LE(packet.body.data(), offset);
    state.isRide = ReadInt32LE(packet.body.data(), offset);
    state.day = ReadInt32LE(packet.body.data(), offset);
    state.resDayPoint = ReadInt32LE(packet.body.data(), offset);
    state.uiInfoReceived = true;

    PostScriptToUI(
        L"if(window.updateHorseCompetitionPoints) { window.updateHorseCompetitionPoints('" +
        std::to_wstring(state.resDayPoint.load()) +
        L"'); }"
    );
}

void HandleHorseEndGameResponse(HorseCompetitionState& state, const GamePacket& packet, size_t& offset) {
    if (offset + 12 > packet.body.size()) {
        return;
    }

    const int result = ReadInt32LE(packet.body.data(), offset);
    ReadInt32LE(packet.body.data(), offset);
    ReadInt32LE(packet.body.data(), offset);

    if (result != 0) {
        NotifyHorseProgress(L"游戏异常结束，立即退出房间准备下一轮");
        state.abnormalRoundEnd = true;
        state.receivedEndGame = true;
        SetHorsePhase(state, HORSE_PHASE_FINISHED);
        state.isSettling = false;
        return;
    }

    std::string rankListJson;
    if (ReadHorseJsonPayload(packet, offset, rankListJson)) {
        int myRank = 0;
        const std::string myRankJson = ExtractJsonObjectByPlayerId(rankListJson, g_userId.load(), &myRank);

        if (!myRankJson.empty()) {
            const int myPoint = ExtractJsonInt(myRankJson, "point");
            const int myCostTime = ExtractJsonInt(myRankJson, "cost_time");
            const int myHorseIid = ExtractJsonInt(myRankJson, "iid");
            state.myInfo.rank = myRank;
            if (myCostTime > 0) {
                state.myInfo.cost_time = myCostTime;
            }

            std::wstring msg = L"游戏结束！排名第" + std::to_wstring(myRank) +
                L"，point=" + std::to_wstring(myPoint);
            if (myCostTime > 0) {
                msg += L"，用时=" + std::to_wstring(myCostTime) + L"秒";
            }
            if (myHorseIid > 0) {
                msg += L"，坐骑IID=" + std::to_wstring(myHorseIid);
            }
            NotifyHorseProgress(msg);
        } else {
            NotifyHorseProgress(L"游戏结束！未在排名列表中找到自己，原始排名=" + MakeJsonPreview(rankListJson, 180));
        }
    }

    state.receivedEndGame = true;
    SetHorsePhase(state, HORSE_PHASE_FINISHED);
    state.isSettling = false;
}

void HandleHorseSyncMemberResponse(HorseCompetitionState& state, const std::string& json) {
    const uint32_t playerId = ExtractJsonUInt(json, "player_id");
    const int costTime = ExtractJsonInt(json, "cost_time");
    if (playerId == g_userId.load()) {
        state.myInfo.cost_time = costTime;
    }
}

}  // namespace

void SetHorseProgressCallback(HorseProgressCallback callback) {
    g_horseProgressCallback = callback;
}

void RequestStopHorseCompetition() {
    auto& state = ActivityStateManager::Instance().GetHorseCompetitionState();
    if (!state.isRunning) {
        NotifyHorseProgress(L"坐骑大赛当前未在运行");
        return;
    }

    state.stopRequested = true;
    if (state.isGaming) {
        NotifyHorseProgress(L"已请求停止，当前局完成后将自动停止");
    } else {
        NotifyHorseProgress(L"已请求停止，当前流程将尽快结束");
    }
}

BOOL SendHorseCompetitionPacket(
    const std::string& operation,
    const std::vector<int32_t>& bodyValues,
    bool useGameCmd) {
    return SendHorsePacketBody(BuildHorsePacketBody(operation, bodyValues), useGameCmd);
}

BOOL SendHorseJoinGamePacket() {
    return SendHorseCompetitionPacket(HorseCmd::JOIN_GAME);
}

BOOL SendHorseRoomInfoPacket() {
    return SendHorseCompetitionPacket(HorseCmd::ROOM_INFO);
}

BOOL SendHorseReadyPacket() {
    return SendHorseCompetitionPacket(HorseCmd::READY);
}

BOOL SendHorseExitRoomPacket() {
    return SendHorseCompetitionPacket(HorseCmd::EXIT_ROOM);
}

BOOL SendHorseUIInfoPacket() {
    return SendHorseCompetitionPacket(HorseCmd::UI_INFO);
}

BOOL SendHorseExchangeInfoPacket() {
    return SendHorseCompetitionPacket(HorseCmd::EXCHANGE_INFO);
}

BOOL SendHorseExchangePacket(int exchangeId, int count) {
    return SendHorseCompetitionPacket(HorseCmd::EXCHANGE, {exchangeId, count});
}

BOOL SendHorsePlayGamePacket(int distance) {
    return SendHorseCompetitionPacket(HorseCmd::PLAY_GAME, {distance});
}

BOOL SendHorseUseItemPacket(int itemIdx) {
    const std::string jsonData = "{\"item_idx\":" + std::to_string(itemIdx) + "}";
    return SendHorsePacketBody(BuildHorsePacketBodyWithJson(HorseCmd::USE_ITEM, jsonData), true);
}

BOOL SendHorseGetRegressionPacket(int idx) {
    return SendHorseCompetitionPacket(HorseCmd::GET_REGRESSION, {idx});
}

BOOL StartHorseCompetitionGame() {
    auto& state = ActivityStateManager::Instance().GetHorseCompetitionState();

    if (state.isGaming) {
        return FALSE;
    }

    int baseSpeed = state.myInfo.horse_base_speed;
    const int baseIntimate = state.myInfo.horse_base_intimate;
    int baseHp = state.myInfo.horse_base_Hp;

    if (baseSpeed <= 0) {
        baseSpeed = 600;
    }
    if (baseHp <= 0) {
        baseHp = 1000;
    }

    state.speed = (baseSpeed + 15.0) / 60.0;
    state.accSpeed = (baseSpeed + 15.0 + baseIntimate / 5.0) / 60.0;
    state.maxHp = baseHp;

    state.hp = state.maxHp;
    state.distance = 0.0;
    state.state = "RUN";
    state.lastState = "RUN";
    state.canControl = true;
    state.isDie = false;
    state.syncCount = 0;
    state.items.clear();
    state.localFinished = false;
    state.receivedEndGame = false;
    state.abnormalRoundEnd = false;
    SetHorsePhase(state, HORSE_PHASE_GAMING);
    g_horseGameRunning = true;

    if (g_horseGameThread.joinable()) {
        g_horseGameThread.join();
    }
    g_horseGameThread = std::thread(HorseGameMainLoop);

    return TRUE;
}

void StopHorseCompetitionGame() {
    g_horseGameRunning = false;
    if (g_horseGameThread.joinable()) {
        g_horseGameThread.join();
    }

    auto& state = ActivityStateManager::Instance().GetHorseCompetitionState();
    state.isGaming = false;
    if (!state.isFinished) {
        SetHorsePhase(state, HORSE_PHASE_IDLE);
    }
}

void ProcessHorseCompetitionResponse(const GamePacket& packet) {
    if (packet.body.size() < 4) {
        return;
    }

    auto& state = ActivityStateManager::Instance().GetHorseCompetitionState();

    size_t offset = 0;
    const uint16_t cmdLen = ReadUInt16LE(packet.body.data(), offset);
    if (offset + cmdLen > packet.body.size()) {
        return;
    }

    const std::string cmd(reinterpret_cast<const char*>(packet.body.data() + offset), cmdLen);
    offset += cmdLen;

    std::string json;
    if (cmd == HorseCmd::ROOM_INFO) {
        if (ReadHorseJsonPayload(packet, offset, json)) {
            HandleHorseRoomInfoResponse(state, json);
        }
    } else if (cmd == HorseCmd::START_GAME) {
        if (ReadHorseJsonPayload(packet, offset, json)) {
            HandleHorseStartGameResponse(state, json);
        }
    } else if (cmd == HorseCmd::JOIN_GAME) {
        if (ReadHorseJsonPayload(packet, offset, json)) {
            HandleHorseJoinGameResponse(state, json);
        }
    } else if (cmd == HorseCmd::READY) {
        if (ReadHorseJsonPayload(packet, offset, json)) {
            HandleHorseReadyResponse(state, json);
        }
    } else if (cmd == HorseCmd::ROOM_STATUS) {
        if (ReadHorseJsonPayload(packet, offset, json)) {
            HandleHorseRoomStatusResponse(state, json);
        }
    } else if (cmd == HorseCmd::UI_INFO) {
        HandleHorseUiInfoResponse(state, packet, offset);
    } else if (cmd == HorseCmd::END_GAME) {
        HandleHorseEndGameResponse(state, packet, offset);
    } else if (cmd == HorseCmd::SYNC_MEMBER) {
        if (ReadHorseJsonPayload(packet, offset, json)) {
            HandleHorseSyncMemberResponse(state, json);
        }
    }

    state.waitingResponse = false;
}

BOOL StartOneKeyHorseCompetitionPacket(bool useTempMount) {
    auto& state = ActivityStateManager::Instance().GetHorseCompetitionState();

    if (state.isRunning) {
        NotifyHorseProgress(L"坐骑大赛正在运行中，请勿重复启动");
        return FALSE;
    }

    state.Reset();
    state.isRunning = true;
    state.useTempMount = useTempMount;
    state.stopRequested = false;

    while (true) {
        ResetHorseRoundState(state);

        SetHorsePhase(state, HORSE_PHASE_FETCHING_UI_INFO);

        NotifyHorseProgress(L"正在获取活动信息...");
        SendHorseUIInfoPacket();

        if (!WaitForHorseFlag(state.uiInfoReceived, 5000)) {
            NotifyHorseProgress(L"获取活动信息超时");
            state.isRunning = false;
            return FALSE;
        }

        if (state.resDayPoint <= 0) {
            NotifyHorseProgress(L"坐骑大赛已完成：今日骑乘点已刷满");
            state.isRunning = false;
            return TRUE;
        }

        if (state.stopRequested) {
            NotifyHorseProgress(L"坐骑大赛已停止");
            state.isRunning = false;
            return TRUE;
        }

        NotifyHorseProgress(L"活动信息获取成功，剩余点数: " + std::to_wstring(state.resDayPoint.load()));
        NotifyHorseProgress(L"正在加入游戏房间...");

        SetHorsePhase(state, HORSE_PHASE_JOINING_ROOM);
        SendHorseJoinGamePacket();

        if (!WaitForHorseFlag(state.inRoom, 10000)) {
            NotifyHorseProgress(L"加入房间超时，本轮匹配失败，准备重试...");
            if (state.stopRequested) {
                NotifyHorseProgress(L"坐骑大赛已按请求停止");
                state.isRunning = false;
                state.stopRequested = false;
                return TRUE;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1200));
            continue;
        }

        NotifyHorseProgress(L"成功加入房间，正在准备...");
        SendHorseReadyPacket();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        NotifyHorseProgress(L"已准备，等待游戏开始...");
        SetHorsePhase(state, HORSE_PHASE_WAITING_START);

        if (!WaitForHorseFlag(state.isGaming, 30000)) {
            NotifyHorseProgress(L"匹配超时未正常开局，退出房间后重试...");
            SendHorseExitRoomPacket();
            state.inRoom = false;
            if (state.stopRequested) {
                NotifyHorseProgress(L"坐骑大赛已按请求停止");
                state.isRunning = false;
                state.stopRequested = false;
                return TRUE;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            continue;
        }

        NotifyHorseProgress(L"游戏开始！正在比赛中...");

        int waitCount = 0;
        bool settlingNotified = false;
        bool localFinishNotified = false;
        bool endGameNotified = false;
        while (!state.isFinished && waitCount < 1800) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            waitCount++;

            if (state.localFinished && !localFinishNotified) {
                localFinishNotified = true;
                NotifyHorseProgress(L"比赛已完成，等待服务器确认...");
            }

            if (state.isSettling && !settlingNotified) {
                settlingNotified = true;
                NotifyHorseProgress(L"游戏即将结束，等待服务器结算...");
            }

            if (state.receivedEndGame && !endGameNotified) {
                endGameNotified = true;
                NotifyHorseProgress(L"服务器已确认，游戏即将完成...");
            }

            if (waitCount % 100 == 0) {
                const int progress = static_cast<int>(state.distance * 100 / 1500);
                if (!state.localFinished) {
                    NotifyHorseProgress(L"比赛进行中... 进度: " + std::to_wstring(progress) + L"%");
                } else if (!state.receivedEndGame) {
                    NotifyHorseProgress(L"等待服务器确认... (进度: " + std::to_wstring(progress) + L"%)");
                }
            }
        }

        if (!state.isFinished) {
            NotifyHorseProgress(L"等待游戏结束超时");
            StopHorseCompetitionGame();
            SendHorseExitRoomPacket();
            state.isRunning = false;
            return FALSE;
        }

        StopHorseCompetitionGame();
        if (state.abnormalRoundEnd) {
            NotifyHorseProgress(L"本局异常提前结束，立即退房开始下一轮...");
        } else {
            NotifyHorseProgress(L"比赛完成！正在退出...");
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        }
        SendHorseExitRoomPacket();
        state.inRoom = false;

        if (state.stopRequested) {
            NotifyHorseProgress(L"坐骑大赛已按请求停止");
            state.isRunning = false;
            state.stopRequested = false;
            return TRUE;
        }

        NotifyHorseProgress(L"本局完成，准备继续刷取剩余骑乘点...");
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    }
}
