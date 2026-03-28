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
    uint32_t id;
    int32_t pp;
    int32_t maxPp;
    int32_t time;
    int32_t maxTime;
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

constexpr int COMBAT_SITE_ADD = 1;
constexpr int COMBAT_SITE_MD = 2;
constexpr int COMBAT_SITE_DEL = 3;

constexpr int SITE_RECOVER_ID = 9999;

constexpr int ROUND_START_ADD[] = {62};
constexpr int DEALADD_BLOOD_1[] = {2, 9, 17, 24, 29, 33, 34, 59, 95};
constexpr int DEALADD_BLOOD_2[] = {36, 37, 46, 45, 62, 9999};
}  // namespace BufDataType

struct BufData {
    int32_t bufId;
    int32_t addOrRemove;
    int32_t atkId;
    int32_t defId;
    int32_t round;
    int32_t param1;
    int32_t param2;
    int32_t leftOrRight;
    std::wstring name;
    std::wstring tipString;

    BufData()
        : bufId(0)
        , addOrRemove(0)
        , atkId(0)
        , defId(0)
        , round(0)
        , param1(0)
        , param2(0)
        , leftOrRight(0) {}
};

struct BattleEntity {
    int32_t sid;
    int32_t groupType;
    int32_t userId;
    int32_t spiritId;
    int32_t uniqueId;
    int32_t elem;
    int32_t state;
    int32_t hp;
    int32_t maxHp;
    int32_t level;
    int32_t skillNum;
    std::vector<BattleSkill> skills;
    std::vector<BufData> bufArr;
    std::wstring name;
    bool mySpirit;
};

struct BattleData {
    std::vector<BattleEntity> myPets;
    std::vector<BattleEntity> otherPets;
    int32_t myActiveIndex;
    int32_t otherActiveIndex;
    int32_t battleType;
    int32_t escape;
    int32_t round;
};

struct GamePacket {
    uint16_t magic;
    uint16_t length;
    uint32_t opcode;
    uint32_t params;
    std::vector<uint8_t> body;
    std::vector<uint8_t> rawBody;
    BOOL bSend;
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
