#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "packet_protocol.h"

namespace SpiritCollect {
    // 这三个在全局协议表里已有定义，此处只是模块内的叫法。
    constexpr uint32_t PRESURES_SEND = Opcode::SPIRIT_PRESURES_SEND;
    constexpr uint32_t SEND_SPIRIT_SEND = 1187129;
    constexpr uint32_t PLAYER_INFO_SEND = Opcode::SPIRIT_PLAYER_INFO_SEND;
    constexpr uint32_t COLLECT_SEND = Opcode::ACTIVITY_ACTION_SEND;
    constexpr int ACT_ID = 754;
}

struct SpiritInfo {
    uint32_t eggId = 0;
    uint32_t eggIid = 0;
    uint32_t needTime = 0;
    uint32_t eggType = 0;
    uint32_t character = 0;
    std::wstring name;
    uint32_t elem = 0;
    std::wstring elemName;
    std::wstring characterName;
    uint32_t bornTime = 0;
    std::vector<uint32_t> skillList;
    std::vector<std::wstring> skillNames;
};

struct SpiritCollectState {
    bool isActive = false;
    std::vector<SpiritInfo> spiritList;
    int dailyOutLimit = 0;
    int weeklyOutLimit = 0;
    int selectedFriendId = 0;

    void Reset() {
        isActive = false;
        spiritList.clear();
        dailyOutLimit = 0;
        weeklyOutLimit = 0;
        selectedFriendId = 0;
    }
};

// 精魄收集状态唯一 owner。
extern SpiritCollectState g_spiritCollectState;

BOOL SendSpiritPresuresPacket();
BOOL SendSpiritPlayerInfoPacket(uint32_t friendId);
BOOL SendSpiritGiftPacket(uint32_t friendId, uint32_t eggId);
BOOL SendSpiritOpenUIPacket();
BOOL SendSpiritHistoryPacket(int type);
