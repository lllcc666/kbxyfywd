#pragma once

#define KB_PACKET_SHARED_TYPES_DEFINED 1

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "packet_protocol.h"

struct LingyuAttribute {
    int32_t nativeEnum;
    int32_t nativeValue;
    std::wstring nativeName;
};

struct LingyuItem {
    int32_t symmId;
    std::wstring symmName;
    int32_t symmIndex;
    int32_t symmFlag;
    std::wstring petName;
    int32_t symmType;
    std::vector<LingyuAttribute> nativeList;
};

struct LingyuData {
    int32_t backFlag;
    std::vector<LingyuItem> items;
};

struct BattleSkill {
    uint32_t id = 0;
    int32_t pp = 0;
    int32_t maxPp = 0;
    int32_t time = 0;
    int32_t maxTime = 0;
    std::wstring name;
};

namespace BufDataType {
constexpr int BUF_TYPE_0 = 0;
constexpr int BUF_TYPE_1 = 1;
constexpr int BUF_TYPE_2 = 2;
constexpr int BUF_TYPE_3 = 3;
constexpr int BUF_TYPE_4 = 4;
constexpr int BUF_TYPE_5 = 5;
constexpr int BUF_TYPE_6 = 6;
constexpr int BUF_TYPE_7 = 7;

constexpr int COMBAT_SITE_ADD = 1;
constexpr int COMBAT_SITE_MD = 2;
constexpr int COMBAT_SITE_DEL = 3;

constexpr int SITE_RECOVER_ID = 9999;

constexpr int ROUND_START_ADD[] = {62};
constexpr int DEALADD_BLOOD_1[] = {2, 9, 17, 24, 29, 33, 34, 59, 95};
constexpr int DEALADD_BLOOD_2[] = {36, 37, 46, 45, 62, 9999};
}  // namespace BufDataType

struct BufData {
    int32_t bufId = 0;
    int32_t addOrRemove = 0;
    int32_t atkId = 0;
    int32_t defId = 0;
    int32_t round = 0;
    int32_t param1 = 0;
    int32_t param2 = 0;
    int32_t param3 = 0;
    int32_t param4 = 0;
    int32_t leftOrRight = 0;
    std::wstring name;
    std::wstring tipString;
};

struct BattleEntity {
    int32_t sid = 0;
    int32_t groupType = 0;
    int32_t userId = 0;
    int32_t spiritId = 0;
    int32_t uniqueId = 0;
    int32_t elem = 0;
    int32_t state = 0;
    int32_t rawState = 0;
    int32_t hp = 0;
    int32_t maxHp = 0;
    int32_t level = 0;
    int32_t skillNum = 0;
    int32_t mNum = 0;
    bool hasMNum = false;
    bool placeholder = false;
    std::vector<BattleSkill> skills;
    std::vector<BufData> bufArr;
    std::wstring name;
    bool mySpirit = false;
};

struct BattleCatchData {
    int32_t spiritId = 0;
    int32_t spiritType = 0;
    int32_t spiritHp = 0;
    int32_t spiritAtk = 0;
    int32_t spiritDef = 0;
    int32_t spiritMagAtk = 0;
    int32_t spiritMagDef = 0;
    int32_t spiritSpeed = 0;
    int32_t spiritLevel = 0;
    int32_t spiritSid = 0;
    int32_t spiritPackId = 0;
    int32_t spiritSex = 0;
    int32_t attackGeniusValue = 0;
    int32_t defenceGeniusValue = 0;
    int32_t magicGeniusValue = 0;
    int32_t resistanceGeniusValue = 0;
    int32_t hpGeniusValue = 0;
    int32_t speedGeniusValue = 0;
};

struct BattleRoundAppend {
    int32_t type = 0;
    int32_t paramKey = 0;
    int32_t paramValue = 0;
    int32_t skillId = 0;
    int32_t targetSid = 0;
};

struct BattleRoundData {
    int32_t cmdType = -1;
    int32_t haveBattle = 0;
    int32_t atkId = 0;
    int32_t skillId = 0;
    int32_t defId = 0;
    int32_t miss = 0;
    int32_t brust = 0;
    int32_t atkHp = 0;
    int32_t defHp = 0;
    int32_t reboundHp = 0;
    int32_t sid = 0;
    int32_t uniqueId = 0;
    int32_t sta = 0;
    int32_t avail = 0;
    int32_t status = 0;
    int32_t itemId = 0;
    int32_t param0 = 0;
    int32_t param1 = 0;
    bool hasCatchData = false;
    BattleCatchData catchData;
    bool hasSwitchEntity = false;
    BattleEntity switchEntity;
    std::vector<BufData> bufs;
    std::vector<BattleRoundAppend> appends;
};

struct BattleEndResult {
    int32_t sid = 0;
    int32_t result = 0;
};

struct BattleFspkData {
    int32_t params = 0;
    int32_t win = 0;
    bool hasReward = false;
    int32_t xianli = 0;
    int32_t zhangong = 0;
};

struct BattleSiteData {
    int32_t params = 0;
    int32_t sn = 0;
    int32_t id = 0;
    int32_t round = 0;
    int32_t siteId = 0;
};

struct BattleData {
    std::vector<BattleEntity> myPets;
    std::vector<BattleEntity> otherPets;
    int32_t myActiveIndex = 0;
    int32_t otherActiveIndex = 0;
    int32_t battleType = 0;
    int32_t escape = 0;
    int32_t round = 0;
    bool active = false;
    int32_t lastCmdType = -1;
    BattleRoundData lastRound;
    int32_t endParam = 0;
    std::vector<BattleEndResult> endResults;
    bool hasFspk = false;
    BattleFspkData fspk;
    bool hasSite = false;
    BattleSiteData site;
    std::vector<int32_t> roundChangeSids;
    int32_t roundWaitTime = 0;
};

struct GamePacket {
    uint16_t magic = 0;
    uint16_t length = 0;
    uint32_t opcode = 0;
    uint32_t params = 0;
    std::vector<uint8_t> body;
    std::vector<uint8_t> rawBody;
    bool bodyDecoded = true;
    BOOL bSend = FALSE;
};

struct MonsterGenius {
    std::wstring name;
    int32_t value;
    int32_t level;
};

struct MonsterSkill {
    int32_t id;
    int32_t pp;
    int32_t maxPp;
    std::wstring name;
};

struct MonsterSymm {
    int32_t place;
    int32_t id;
    int32_t index;
    std::wstring name;
};

struct MonsterItem {
    int32_t id;
    int32_t type_id;
    int32_t iid;
    std::wstring name;
    int32_t isfirst;
    int32_t level;
    int32_t exp;
    int32_t needExp;
    int32_t type;
    std::wstring typeName;
    int32_t forbitItem;
    int32_t attack;
    int32_t defence;
    int32_t magic;
    int32_t resistance;
    int32_t strength;
    int32_t hp;
    int32_t speed;
    int32_t mold;
    int32_t state;
    int32_t timetxt;
    int32_t sex;
    int32_t geniusType;
    std::wstring geniusName;
    int32_t aptitude;
    std::wstring aptitudeName;
    std::vector<MonsterGenius> geniusList;
    std::vector<MonsterSkill> skills;
    std::vector<MonsterSymm> symmList;
};

struct MonsterData {
    int32_t sn;
    int32_t count;
    std::vector<MonsterItem> monsters;
};

inline int32_t ReadInt32LE(const uint8_t* data, size_t& offset) {
    int32_t val = static_cast<int32_t>(data[offset]) |
                  (static_cast<int32_t>(data[offset + 1]) << 8) |
                  (static_cast<int32_t>(data[offset + 2]) << 16) |
                  (static_cast<int32_t>(data[offset + 3]) << 24);
    offset += 4;
    return val;
}

inline uint32_t ReadUInt32LE(const uint8_t* data, size_t& offset) {
    uint32_t val = static_cast<uint32_t>(data[offset]) |
                   (static_cast<uint32_t>(data[offset + 1]) << 8) |
                   (static_cast<uint32_t>(data[offset + 2]) << 16) |
                   (static_cast<uint32_t>(data[offset + 3]) << 24);
    offset += 4;
    return val;
}

inline int16_t ReadInt16LE(const uint8_t* data, size_t& offset) {
    int16_t val = static_cast<int16_t>(data[offset]) |
                  (static_cast<int16_t>(data[offset + 1]) << 8);
    offset += 2;
    return val;
}

inline uint16_t ReadUInt16LE(const uint8_t* data, size_t& offset) {
    uint16_t val = static_cast<uint16_t>(data[offset]) |
                   (static_cast<uint16_t>(data[offset + 1]) << 8);
    offset += 2;
    return val;
}

inline uint8_t ReadByte(const uint8_t* data, size_t& offset) {
    return data[offset++];
}

inline uint32_t ReadInt32BE(const uint8_t* data, size_t& offset) {
    uint32_t val = (static_cast<uint32_t>(data[offset]) << 24) |
                   (static_cast<uint32_t>(data[offset + 1]) << 16) |
                   (static_cast<uint32_t>(data[offset + 2]) << 8) |
                   static_cast<uint32_t>(data[offset + 3]);
    offset += 4;
    return val;
}

inline uint16_t ReadInt16BE(const uint8_t* data, size_t& offset) {
    uint16_t val = (static_cast<uint16_t>(data[offset]) << 8) |
                   static_cast<uint16_t>(data[offset + 1]);
    offset += 2;
    return val;
}

inline std::string ReadStringBE(const uint8_t* data, size_t& offset) {
    uint16_t len = ReadInt16BE(data, offset);
    std::string str(reinterpret_cast<const char*>(data + offset), len);
    offset += len;
    return str;
}
