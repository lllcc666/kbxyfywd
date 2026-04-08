#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace SpiritCollect {
    constexpr uint32_t PRESURES_SEND = 1187124;
    constexpr uint32_t PRESURES_BACK = 1330484;
    constexpr uint32_t SEND_SPIRIT_SEND = 1187129;
    constexpr uint32_t SEND_SPIRIT_BACK = 1330489;
    constexpr uint32_t PLAYER_INFO_SEND = 1187585;
    constexpr uint32_t PLAYER_INFO_BACK = 1318657;
    constexpr uint32_t COLLECT_SEND = 1185429;
    constexpr uint32_t COLLECT_BACK = 1316501;
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
    SpiritInfo selectedSpirit;
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
