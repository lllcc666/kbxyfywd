#include "packet_parser.h"
#include <MemoryModule.h>
#include "embedded/zlib_data.h"
#include "battle_six.h"
#include "dungeon_jump.h"
#include "wpe_hook.h"
#include "ui_bridge.h"
#include "utils.h"
#include <wininet.h>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <limits>
#include <mutex>
#include <sstream>
#include <unordered_map>

// 轻量级ZIP解压辅助函数（纯内存，使用已加载的zlib）
#include "embedded/minizip_helper.h"

// 外部窗口句柄（来自demo.cpp）
extern HWND g_hWnd;

// zlib function prototype for packet body decompression
typedef int (*PFN_UNCOMPRESS)(unsigned char* dest, unsigned long* destLen, const unsigned char* source, unsigned long sourceLen);
typedef int (*PFN_COMPRESS)(unsigned char* dest, unsigned long* destLen, const unsigned char* source, unsigned long sourceLen);
typedef int (*PFN_COMPRESS2)(unsigned char* dest, unsigned long* destLen, const unsigned char* source, unsigned long sourceLen, int level);
typedef int (*PFN_DEFLATEINIT)(void* strm, int level);
typedef int (*PFN_DEFLATE)(void* strm, int flush);
typedef int (*PFN_DEFLATEEND)(void* strm);
typedef int (*PFN_INFLATEINIT)(void* strm);
typedef int (*PFN_INFLATEINIT2)(void* strm, int windowBits);
typedef int (*PFN_INFLATE)(void* strm, int flush);
typedef int (*PFN_INFLATEEND)(void* strm);

static HMEMORYMODULE g_zlibModule = nullptr;
static PFN_UNCOMPRESS g_uncompress = nullptr;
static PFN_COMPRESS g_compress = nullptr;
static PFN_COMPRESS2 g_compress2 = nullptr;
static PFN_DEFLATEINIT g_deflateInit = nullptr;
static PFN_DEFLATE g_deflate = nullptr;
static PFN_DEFLATEEND g_deflateEnd = nullptr;
static PFN_INFLATEINIT g_inflateInit = nullptr;
static PFN_INFLATEINIT2 g_inflateInit2 = nullptr;
static PFN_INFLATE g_inflate = nullptr;
static PFN_INFLATEEND g_inflateEnd = nullptr;

// minizip module and functions
BattleData PacketParser::g_currentBattle;
std::mutex PacketParser::g_battleMutex;
std::vector<uint8_t> PacketParser::g_recvBuffer;
std::mutex g_parserLifecycleMutex;

// 全局数据映射表的互斥锁
static std::mutex g_dataMapsMutex;
std::unordered_map<int, std::wstring> g_petNames;
std::unordered_map<int, std::wstring> g_skillNames;
std::unordered_map<int, int> g_skillPowers;  // 技能ID -> 威力值
std::unordered_map<int, int> g_skillRanges;  // 技能ID -> range，2 表示作用于己方

static std::unordered_map<int, std::wstring> g_toolNames;
static std::unordered_map<int, std::wstring> g_mapNames;
std::unordered_map<int, std::wstring> g_elemNames;             // 系别名称映射
std::unordered_map<int, std::wstring> g_geniusNames;           // 性格名称映射
static std::unordered_map<int, std::wstring> g_aptitudeNames;  // 资质名称映射
std::unordered_map<int, int> g_petElems;                       // 妖怪ID -> 系别ID映射
static std::unordered_map<int, std::wstring> g_bufNames;       // Buff 名称映射 (从 bufInfo.xml)
static std::unordered_map<int, std::wstring> g_bufDescs;       // Buff 描述映射
static std::wstring g_lastItemName;
static std::wstring g_pendingRoundTip;

class BoundedReader {
public:
    BoundedReader(const uint8_t* data, size_t size) : m_data(data), m_size(size), m_offset(0) {}

    bool ReadI32(int32_t& value) {
        if (!CanRead(sizeof(int32_t))) return false;
        value = ReadInt32LE(m_data, m_offset);
        return true;
    }

    bool ReadU32(uint32_t& value) {
        if (!CanRead(sizeof(uint32_t))) return false;
        value = ReadUInt32LE(m_data, m_offset);
        return true;
    }

    bool ReadU16(uint16_t& value) {
        if (!CanRead(sizeof(uint16_t))) return false;
        value = ReadUInt16LE(m_data, m_offset);
        return true;
    }

    bool Skip(size_t count) {
        if (!CanRead(count)) return false;
        m_offset += count;
        return true;
    }

    bool CanRead(size_t count) const {
        return count <= m_size - m_offset;
    }

    size_t Remaining() const { return m_size - m_offset; }
    size_t Offset() const { return m_offset; }

    bool ReadUtf8(std::wstring& value) {
        uint16_t byteLength = 0;
        if (!ReadU16(byteLength) || !CanRead(byteLength)) return false;
        value = Utf8ToWide(std::string(reinterpret_cast<const char*>(m_data + m_offset), byteLength));
        m_offset += byteLength;
        return true;
    }

private:
    const uint8_t* m_data;
    size_t m_size;
    size_t m_offset;
};

static bool IsBattleObserver() {
    // AS3 uses GameData.lookBattle == 1. userId==0 is not spectator mode;
    // treating it as observer mis-parses the player mNum layout.
    return false;
}


// BOSS列表结构体和全局变量
struct BossInfo {
    int id;
    std::wstring name;
    int elem;
};
static std::vector<BossInfo> g_bossList;

// ============================================================================
// Buff 名称常量 (参考 AS3 BufData.as BUF_NAME)
// ============================================================================
struct BufNameEntry {
    int id;
    const wchar_t* name;
};

static const BufNameEntry BUF_NAME_ENTRIES[] = {
    {1, L"昏迷"}, {2, L"流血"}, {3, L"速度下降"}, {4, L"速度提升"},
    {5, L"防御力下降"}, {6, L"防御力提升"}, {7, L"攻击力下降"}, {8, L"攻击力提升"},
    {9, L"中毒"}, {10, L"受到法术伤害异常"}, {11, L"受到物理伤害异常"},
    {12, L"不能使用物理技能"}, {14, L"不能使用法术技能"},
    {18, L"法术下降"}, {19, L"法术提升"}, {20, L"命中率提升"}, {21, L"命中率下降"},
    {22, L"抗性提升"}, {23, L"抗性下降"}, {26, L"睡眠"}, {27, L"迷惑"},
    {28, L"疲劳"}, {29, L"灼伤"}, {30, L"激怒"}, {31, L"麻痹"},
    {32, L"混乱"}, {33, L"吸血"}, {34, L"窒息"}, {36, L"加血"},
    {37, L"加血"}, {47, L"冰冻"}, {48, L"自身血量低于50%时伤害加倍"},
    {49, L"所有增益状态被消除"}, {50, L"所有负面状态被去除"},
    {62, L"加血"}, {63, L"所有负面状态被去除"}
};

// 特殊 Buff ID 数组 (需要根据 param1 正负判断增益/减益)
static const int SPECIAL_BUF_IDS[] = {3, 4, 5, 6, 7, 8, 18, 19, 21, 20, 23, 22};

// ============================================================================
// XML解析辅助函数
// ============================================================================

static void TrimTrailingWhitespace(std::wstring& text) {
    while (!text.empty() && (text.back() == L' ' || text.back() == L'\t' ||
           text.back() == L'\n' || text.back() == L'\r')) {
        text.pop_back();
    }
}

static bool TryParseInt(const std::string& text, int& value) {
    if (text.empty()) {
        return false;
    }

    char* end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0') {
        return false;
    }

    value = static_cast<int>(parsed);
    return true;
}

static bool ExtractTagValue(const std::string& xml, const char* tagName, std::string& value, size_t startPos = 0) {
    const std::string openTag = std::string("<") + tagName + ">";
    const std::string closeTag = std::string("</") + tagName + ">";

    size_t openPos = xml.find(openTag, startPos);
    if (openPos == std::string::npos) {
        return false;
    }
    openPos += openTag.size();

    size_t closePos = xml.find(closeTag, openPos);
    if (closePos == std::string::npos) {
        return false;
    }

    value = xml.substr(openPos, closePos - openPos);
    return true;
}

static const wchar_t* FindBuiltInBufName(int bufId) {
    for (const auto& entry : BUF_NAME_ENTRIES) {
        if (entry.id == bufId) {
            return entry.name;
        }
    }
    return nullptr;
}

static bool IsSpecialBufId(int bufId) {
    for (int id : SPECIAL_BUF_IDS) {
        if (id == bufId) {
            return true;
        }
    }
    return false;
}

static bool ExtractAttributeValue(const std::string& tagText, const char* attrName, std::string& value) {
    const std::string pattern = std::string(attrName) + "=";
    size_t pos = tagText.find(pattern);
    if (pos == std::string::npos) {
        return false;
    }

    pos += pattern.size();
    while (pos < tagText.size() && (tagText[pos] == ' ' || tagText[pos] == '\t')) {
        ++pos;
    }
    if (pos >= tagText.size()) {
        return false;
    }

    char quote = 0;
    if (tagText[pos] == '\'' || tagText[pos] == '"') {
        quote = tagText[pos++];
    }

    const size_t end = quote ? tagText.find(quote, pos)
                             : tagText.find_first_of(" \t\r\n>", pos);
    if (end == std::string::npos) {
        return false;
    }

    value = tagText.substr(pos, end - pos);
    return true;
}

static bool FindElementBlock(const std::string& xml,
                             const char* tagName,
                             size_t searchStart,
                             size_t& contentStart,
                             size_t& closeStart,
                             size_t& nextPos,
                             std::string* startTagText = nullptr) {
    const std::string openPattern = std::string("<") + tagName;
    const std::string closePattern = std::string("</") + tagName + ">";

    size_t openPos = xml.find(openPattern, searchStart);
    if (openPos == std::string::npos) {
        return false;
    }

    const size_t tagEnd = xml.find('>', openPos);
    if (tagEnd == std::string::npos) {
        return false;
    }

    if (startTagText) {
        *startTagText = xml.substr(openPos, tagEnd - openPos + 1);
    }

    contentStart = tagEnd + 1;
    closeStart = xml.find(closePattern, contentStart);
    if (closeStart == std::string::npos) {
        return false;
    }

    nextPos = closeStart + closePattern.size();
    return true;
}

static bool FindSectionByType(const std::string& xml, const char* typeValue, std::string& section) {
    size_t searchPos = 0;
    std::string startTag;
    size_t contentStart = 0;
    size_t closeStart = 0;
    size_t nextPos = 0;

    while (FindElementBlock(xml, "items", searchPos, contentStart, closeStart, nextPos, &startTag)) {
        std::string actualType;
        if (ExtractAttributeValue(startTag, "type", actualType) && actualType == typeValue) {
            section = xml.substr(contentStart, closeStart - contentStart);
            return true;
        }
        searchPos = nextPos;
    }

    return false;
}

static void ParseNamedItemsSection(const std::string& section, std::unordered_map<int, std::wstring>& target, int startIndex) {
    size_t searchPos = 0;
    int index = startIndex;

    while (true) {
        size_t tagPos = section.find("<item", searchPos);
        if (tagPos == std::string::npos) {
            break;
        }

        const size_t tagEnd = section.find('>', tagPos);
        if (tagEnd == std::string::npos) {
            break;
        }

        std::string tagText = section.substr(tagPos, tagEnd - tagPos + 1);
        std::string name;
        if (ExtractAttributeValue(tagText, "name", name)) {
            target[index++] = Utf8ToWide(name);
        }

        searchPos = tagEnd + 1;
    }
}

// ============================================================================
// 妖怪背包数据全局变量（用于副本跳层等功能）
// ============================================================================
MonsterData g_monsterData;

/**
 * @brief 获取 Buff 名称
 * @param bufId Buff ID
 * @param param1 参数1 (用于特殊 Buff 的等级判断)
 * @return Buff 名称
 */
static std::wstring GetBufName(int bufId, int param1 = 0) {
    // 先从 bufInfo.xml 加载的数据中查找
    {
        std::lock_guard<std::mutex> lock(g_dataMapsMutex);
        auto it = g_bufNames.find(bufId);
        if (it != g_bufNames.end()) {
            return it->second;
        }
    }
    
    // 使用内置的名称映射
    const wchar_t* builtInName = FindBuiltInBufName(bufId);
    if (builtInName != nullptr) {
        std::wstring baseName = builtInName;

        if (IsSpecialBufId(bufId) && param1 != 0) {
            int level = (param1 > 0) ? param1 : -param1;
            return baseName + std::to_wstring(level) + L"级";
        }
        return baseName;
    }
    
    return L"未知状态";
}

/**
 * @brief 获取 Buff 提示文本
 * @param bufId Buff ID
 * @param param1 参数1
 * @param param2 参数2
 * @return 提示文本
 */
static std::wstring GetBufTipString(int bufId, int param1, int param2) {
    // 先从 bufInfo.xml 加载的数据中查找
    std::wstring desc;
    {
        std::lock_guard<std::mutex> lock(g_dataMapsMutex);
        auto it = g_bufDescs.find(bufId);
        if (it != g_bufDescs.end()) {
            desc = it->second;
        }
    }
    
    if (!desc.empty()) {
        // 替换占位符
        size_t pos;
        if ((pos = desc.find(L"#num1#")) != std::wstring::npos) {
            desc.replace(pos, 6, std::to_wstring(param1));
        }
        if ((pos = desc.find(L"#num2#")) != std::wstring::npos) {
            desc.replace(pos, 6, std::to_wstring(param2));
        }
        return desc;
    }
    
    // 返回基本名称
    return GetBufName(bufId, param1);
}


static void ApplyBattleBufToPet(BattleEntity& pet, const BufData& sourceBuf) {
    BufData buf = sourceBuf;
    buf.name = GetBufName(buf.bufId, buf.param1);
    buf.tipString = GetBufTipString(buf.bufId, buf.param1, buf.param2);

    // Standalone type 4/5 are blood/recover events, not persistent Buff rows.
    // Types 3/6 are PP append events and are likewise kept in the round record.
    if (buf.addOrRemove == BufDataType::BUF_TYPE_3 ||
        buf.addOrRemove == BufDataType::BUF_TYPE_4 ||
        buf.addOrRemove == BufDataType::BUF_TYPE_5 ||
        buf.addOrRemove == BufDataType::BUF_TYPE_6) {
        return;
    }

    if (buf.addOrRemove == BufDataType::BUF_TYPE_0) {
        for (auto it = pet.bufArr.begin(); it != pet.bufArr.end(); ) {
            if (it->bufId == buf.bufId) {
                it = pet.bufArr.erase(it);
            } else {
                ++it;
            }
        }
        return;
    }

    bool replaced = false;
    for (auto& existing : pet.bufArr) {
        if (existing.bufId == buf.bufId) {
            existing = buf;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        pet.bufArr.push_back(buf);
    }

    // HP changes are authoritative only when the protocol carries the resulting HP
    // fields (for example BATTLE_ROUND). Do not derive HP from a Buff id/parameter.
}

static void RemoveBattleBufFromPet(BattleEntity& pet, int32_t bufId) {
    for (auto it = pet.bufArr.begin(); it != pet.bufArr.end(); ) {
        if (it->bufId == bufId) {
            it = pet.bufArr.erase(it);
        } else {
            ++it;
        }
    }
}


static BattleEntity* FindBattleEntityBySid(BattleData& battle, int32_t sid) {
    for (auto& pet : battle.myPets) {
        if (pet.sid == sid) return &pet;
    }
    for (auto& pet : battle.otherPets) {
        if (pet.sid == sid) return &pet;
    }
    return nullptr;
}

static const BattleEntity* FindBattleEntityBySid(const BattleData& battle, int32_t sid) {
    for (const auto& pet : battle.myPets) {
        if (pet.sid == sid) return &pet;
    }
    for (const auto& pet : battle.otherPets) {
        if (pet.sid == sid) return &pet;
    }
    return nullptr;
}

static void ApplyBattleBufToBattle(BattleData& battle, int32_t sid, const BufData& bufData) {
    if (BattleEntity* pet = FindBattleEntityBySid(battle, sid)) {
        ApplyBattleBufToPet(*pet, bufData);
    }
}

static void RemoveBattleBufFromBattle(BattleData& battle, int32_t sid, int32_t bufId) {
    if (BattleEntity* pet = FindBattleEntityBySid(battle, sid)) {
        RemoveBattleBufFromPet(*pet, bufId);
    }
}

static void AdjustBattleEntityHp(BattleData& battle, int32_t sid, int32_t delta) {
    if (BattleEntity* pet = FindBattleEntityBySid(battle, sid)) {
        const int64_t nextHp = static_cast<int64_t>(pet->hp) + delta;
        if (nextHp < 0) pet->hp = 0;
        else if (nextHp > pet->maxHp) pet->hp = pet->maxHp;
        else pet->hp = static_cast<int32_t>(nextHp);

    }
}
static void ApplyBattleBuffBloodEffect(BattleData& battle, const BufData& buf) {
    switch (buf.bufId) {
        case 2: case 9: case 17: case 24: case 29: case 33: case 34: case 59: case 95: case 104: case 113:
            AdjustBattleEntityHp(battle, buf.defId, -buf.param1);
            if (buf.bufId == 33 && buf.param2 != 0) {
                const BattleEntity* activeMy = battle.myActiveIndex >= 0 && battle.myActiveIndex < static_cast<int32_t>(battle.myPets.size()) ? &battle.myPets[battle.myActiveIndex] : nullptr;
                const BattleEntity* activeOther = battle.otherActiveIndex >= 0 && battle.otherActiveIndex < static_cast<int32_t>(battle.otherPets.size()) ? &battle.otherPets[battle.otherActiveIndex] : nullptr;
                if (activeMy && activeOther) {
                    AdjustBattleEntityHp(battle, buf.defId == activeMy->sid ? activeOther->sid : activeMy->sid, buf.param2);
                }
            }
            break;
        case 36: case 37: case 45: case 46: case 62: case 103: case 9999:
            AdjustBattleEntityHp(battle, buf.defId, buf.param1);
            break;
        default:
            break;
    }
}
static void ApplyBattlePpDelta(BattleData& battle, int32_t sid, int32_t delta) {
    if (BattleEntity* pet = FindBattleEntityBySid(battle, sid)) {
        for (auto& skill : pet->skills) {
            const int64_t nextPp = static_cast<int64_t>(skill.pp) + delta;
            if (nextPp < 0) skill.pp = 0;
            else if (nextPp > skill.maxPp) skill.pp = skill.maxPp;
            else skill.pp = static_cast<int32_t>(nextPp);

            skill.time = skill.pp;
        }
    }
}
static void ApplyBattleSkillPpDelta(BattleData& battle, int32_t sid, int32_t skillId, int32_t delta) {
    if (BattleEntity* pet = FindBattleEntityBySid(battle, sid)) {
        for (auto& skill : pet->skills) {
            if (static_cast<int32_t>(skill.id) != skillId) continue;
            const int64_t nextPp = static_cast<int64_t>(skill.pp) + delta;
            skill.pp = static_cast<int32_t>(std::max<int64_t>(0, std::min<int64_t>(skill.maxPp, nextPp)));
            skill.time = skill.pp;
            return;
        }
    }
}
static void DecreaseBattleSkillPp(BattleData& battle, int32_t sid, int32_t skillId) {
    BattleEntity* pet = FindBattleEntityBySid(battle, sid);
    if (!pet || !pet->mySpirit) return;
    int delta = -1;
    for (const auto& buf : pet->bufArr) {
        if (buf.bufId == 76) { delta = -2; break; }
    }
    for (auto& skill : pet->skills) {
        if (static_cast<int32_t>(skill.id) == skillId) {
            skill.pp = skill.pp + delta < 0 ? 0 : skill.pp + delta;
            skill.time = skill.pp;
            break;
        }
    }
}
static void CopyBattleSixSpirit(const BattleEntity& source, BattleSixSpiritInfo& target) {
    target.sid = source.sid;
    target.spiritId = source.spiritId;
    target.uniqueId = source.uniqueId;
    target.userId = source.userId;
    target.hp = source.hp;
    target.maxHp = source.maxHp;
    target.level = source.level;
    target.element = source.elem;
    target.isDead = source.hp <= 0;
    target.name = source.name;
    target.skills.clear();
    target.skills.reserve(source.skills.size());
    for (const auto& sourceSkill : source.skills) {
        BattleSixSkillInfo skill;
        skill.skillId = static_cast<int>(sourceSkill.id);
        skill.currentPP = sourceSkill.pp;
        skill.maxPP = sourceSkill.maxPp;
        skill.available = sourceSkill.pp > 0;
        skill.name = sourceSkill.name;
        {
            std::lock_guard<std::mutex> lock(g_dataMapsMutex);
            const auto powerIt = g_skillPowers.find(skill.skillId);
            if (powerIt != g_skillPowers.end()) skill.power = powerIt->second;
        }
        target.skills.push_back(std::move(skill));
    }
}
static void SyncBattleSixAutoBattleState(const BattleData& battle, bool initialize) {
    std::lock_guard<std::recursive_mutex> stateLock(g_battleSixAuto.GetStateMutex());
    if (!g_battleSixAuto.IsAutoBattleEnabled()) return;
    if (initialize) {
        g_battleSixAuto.StartBattle();
        g_battleSixAuto.GetMySpirits().clear();
        g_battleSixAuto.GetEnemySpirits().clear();
    }
    if (!g_battleSixAuto.IsInBattle()) return;
    auto syncTeam = [](const std::vector<BattleEntity>& source, std::vector<BattleSixSpiritInfo>& target, int32_t activeIndex) {
        int activeTargetIndex = -1;
        for (size_t sourceIndex = 0; sourceIndex < source.size(); ++sourceIndex) {
            const BattleEntity& sourceSpirit = source[sourceIndex];
            if (sourceSpirit.placeholder || sourceSpirit.spiritId == 0) continue;
            int targetIndex = -1;
            for (size_t i = 0; i < target.size(); ++i) {
                if (sourceSpirit.uniqueId != 0 && target[i].uniqueId == sourceSpirit.uniqueId) { targetIndex = static_cast<int>(i); break; }
                if (targetIndex < 0 && sourceSpirit.sid != 0 && target[i].sid == sourceSpirit.sid) targetIndex = static_cast<int>(i);
            }
            if (targetIndex < 0) { target.emplace_back(); targetIndex = static_cast<int>(target.size() - 1); }
            CopyBattleSixSpirit(sourceSpirit, target[targetIndex]);
            target[targetIndex].position = targetIndex;
            if (static_cast<int32_t>(sourceIndex) == activeIndex || sourceSpirit.state == 1) activeTargetIndex = targetIndex;
        }
        return activeTargetIndex;
    };
    const int myActiveIndex = syncTeam(battle.myPets, g_battleSixAuto.GetMySpirits(), battle.myActiveIndex);
    const int enemyActiveIndex = syncTeam(battle.otherPets, g_battleSixAuto.GetEnemySpirits(), battle.otherActiveIndex);
    if (myActiveIndex >= 0) {
        g_battleSixAuto.SetCurrentSpiritIndex(myActiveIndex);
        g_battleSixAuto.SetMyUniqueId(g_battleSixAuto.GetMySpirits()[myActiveIndex].uniqueId);
    }
    if (enemyActiveIndex >= 0) g_battleSixAuto.SetEnemyActiveIndex(enemyActiveIndex);
    g_battleSixAuto.RefreshEnemyTarget();
}
static bool ReadBattleEntityAfterState(BoundedReader& reader, int32_t rawState, BattleEntity& pet) {
    pet.rawState = rawState;
    if (!reader.ReadI32(pet.sid) ||
        !reader.ReadI32(pet.groupType) ||
        !reader.ReadI32(pet.hp) ||
        !reader.ReadI32(pet.maxHp) ||
        !reader.ReadI32(pet.level) ||
        !reader.ReadI32(pet.elem) ||
        !reader.ReadI32(pet.spiritId) ||
        !reader.ReadI32(pet.uniqueId) ||
        !reader.ReadI32(pet.userId) ||
        !reader.ReadI32(pet.skillNum)) {
        return false;
    }

    if (pet.skillNum < 0 || pet.skillNum > 128) return false;
    pet.state = (pet.rawState == 2) ? 1 : pet.rawState;
    pet.skills.clear();
    pet.skills.reserve(static_cast<size_t>(pet.skillNum));
    for (int32_t i = 0; i < pet.skillNum; ++i) {
        BattleSkill skill;
        if (!reader.ReadU32(skill.id) ||
            !reader.ReadI32(skill.time) ||
            !reader.ReadI32(skill.maxTime)) {
            return false;
        }
        skill.pp = skill.time;
        skill.maxPp = skill.maxTime;
        {
            std::lock_guard<std::mutex> lock(g_dataMapsMutex);
            auto it = g_skillNames.find(static_cast<int>(skill.id));
            if (it != g_skillNames.end()) skill.name = it->second;
        }
        pet.skills.push_back(std::move(skill));
    }

    {
        std::lock_guard<std::mutex> lock(g_dataMapsMutex);
        auto it = g_petNames.find(pet.spiritId);
        if (it != g_petNames.end()) pet.name = it->second;
    }
    return true;
}

static bool ReadBattleEntity(BoundedReader& reader, BattleEntity& pet) {
    int32_t rawState = 0;
    return reader.ReadI32(rawState) && ReadBattleEntityAfterState(reader, rawState, pet);
}


static bool HttpGet(const wchar_t* url, std::vector<uint8_t>& out) {
    HINTERNET hInternet = InternetOpenW(L"KBWebUILoader", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) return false;
    HINTERNET hFile = InternetOpenUrlW(hInternet, url, NULL, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hFile) { InternetCloseHandle(hInternet); return false; }
    out.clear();
    BYTE buffer[4096];
    DWORD read = 0;
    while (InternetReadFile(hFile, buffer, sizeof(buffer), &read) && read > 0) {
        out.insert(out.end(), buffer, buffer + read);
    }
    InternetCloseHandle(hFile);
    InternetCloseHandle(hInternet);
    return !out.empty();
}

static bool ExtractZipEntry(const std::vector<uint8_t>& zip, const std::string& filename, std::vector<uint8_t>& out) {
    out.clear();

    // 安全检查：zip数据大小
    if (zip.empty() || zip.size() > (100 * 1024 * 1024)) { // 最大100MB
        return false;
    }

    // 使用轻量级ZIP解析器进行纯内存解压
    // 传入inflate函数以支持raw deflate格式（ZIP使用）
    return ExtractZipEntryFromMemory(zip, filename, out, g_uncompress, 
        reinterpret_cast<InflateInit2Func>(g_inflateInit2),
        reinterpret_cast<InflateFunc>(g_inflate),
        reinterpret_cast<InflateEndFunc>(g_inflateEnd));
}

static std::string NormalizeXmlUtf8(const std::vector<uint8_t>& data) {
    if (data.size() >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        size_t wc = (data.size() - 2) / 2;
        std::wstring w;
        w.resize(wc);
        memcpy(&w[0], data.data() + 2, wc * 2);
        return WideToUtf8(w);
    }
    if (data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        return std::string((const char*)data.data() + 3, data.size() - 3);
    }
    std::string head((const char*)data.data(), data.size() > 256 ? 256 : data.size());
    std::string lower = head;
    for (auto& c : lower) c = (char)tolower((unsigned char)c);
    if (lower.find("encoding=\"gbk\"") != std::string::npos || lower.find("encoding=\"gb2312\"") != std::string::npos) {
        std::string raw((const char*)data.data(), data.size());
        std::wstring w = MultiToWide(raw, 936);
        return WideToUtf8(w);
    }
    return std::string((const char*)data.data(), data.size());
}

static void ParseSpriteXml(const std::string& xml) {
    std::vector<BossInfo> bossList;
    std::vector<std::pair<int, std::wstring>> petNames;
    std::vector<std::pair<int, int>> petElems;

    size_t searchPos = 0;
    size_t contentStart = 0;
    size_t closeStart = 0;
    size_t nextPos = 0;
    std::string startTag;
    while (FindElementBlock(xml, "sprite", searchPos, contentStart, closeStart, nextPos, &startTag)) {
        std::string idText;
        if (!ExtractAttributeValue(startTag, "id", idText)) {
            searchPos = nextPos;
            continue;
        }

        int id = 0;
        if (!TryParseInt(idText, id)) {
            searchPos = nextPos;
            continue;
        }

        std::string content = xml.substr(contentStart, closeStart - contentStart);
        std::wstring name;
        std::string nameText;
        if (ExtractTagValue(content, "name", nameText)) {
            name = Utf8ToWide(nameText);
            TrimTrailingWhitespace(name);
            petNames.emplace_back(id, name);
        }

        int elemId = 0;
        std::string elemText;
        if (ExtractTagValue(content, "elem", elemText)) {
            TryParseInt(elemText, elemId);
            petElems.emplace_back(id, elemId);
        }

        if (id > 10000 && !name.empty()) {
            bossList.push_back({id, name, elemId});
        }

        searchPos = nextPos;
    }

    std::lock_guard<std::mutex> lock(g_dataMapsMutex);
    for (const auto& [petId, petName] : petNames) {
        g_petNames[petId] = petName;
    }
    for (const auto& [petId, elemId] : petElems) {
        g_petElems[petId] = elemId;
    }
    g_bossList = std::move(bossList);
}

static void ParseSkillXml(const std::string& xml) {
    std::vector<std::pair<int, std::wstring>> skillNames;
    std::vector<std::pair<int, int>> skillPowers;
    std::vector<std::pair<int, int>> skillRanges;


    size_t searchPos = 0;
    size_t contentStart = 0;
    size_t closeStart = 0;
    size_t nextPos = 0;
    while (FindElementBlock(xml, "skill", searchPos, contentStart, closeStart, nextPos)) {
        std::string content = xml.substr(contentStart, closeStart - contentStart);
        std::string idText;
        std::string nameText;
        std::string powerText;
        if (!ExtractTagValue(content, "idx", idText) ||
            !ExtractTagValue(content, "name", nameText) ||
            !ExtractTagValue(content, "power", powerText)) {
            searchPos = nextPos;
            continue;
        }

        int id = 0;
        if (!TryParseInt(idText, id)) {
            searchPos = nextPos;
            continue;
        }
        if (id == 0) {
            searchPos = nextPos;
            continue;
        }

        std::wstring name = Utf8ToWide(nameText);
        TrimTrailingWhitespace(name);
        int power = 0;
        if (!TryParseInt(powerText, power)) {
            searchPos = nextPos;
            continue;
        }
        skillNames.emplace_back(id, name);
        skillPowers.emplace_back(id, power);
        int range = 0;
        std::string rangeText;
        if (ExtractTagValue(content, "range", rangeText)) {
            TryParseInt(rangeText, range);
        }
        skillRanges.emplace_back(id, range);
        searchPos = nextPos;
    }


    std::lock_guard<std::mutex> lock(g_dataMapsMutex);
    for (const auto& [skillId, skillName] : skillNames) {
        g_skillNames[skillId] = skillName;
    }
    for (const auto& [skillId, power] : skillPowers) {
        g_skillPowers[skillId] = power;
    }
    for (const auto& [skillId, range] : skillRanges) {
        g_skillRanges[skillId] = range;
    }
}



static void ParseMapXml(const std::string& xml) {
    size_t searchPos = 0;
    while (true) {
        size_t tagPos = xml.find('<', searchPos);
        if (tagPos == std::string::npos) {
            break;
        }

        const size_t tagEnd = xml.find('>', tagPos);
        if (tagEnd == std::string::npos) {
            break;
        }

        const std::string tag = xml.substr(tagPos, tagEnd - tagPos + 1);
        if (tag.rfind("<country", 0) == 0 || tag.rfind("<scene", 0) == 0 || tag.rfind("<map", 0) == 0) {
            std::string idText;
            std::string nameText;
            if (ExtractAttributeValue(tag, "id", idText) && ExtractAttributeValue(tag, "name", nameText)) {
                int id = 0;
                if (TryParseInt(idText, id)) {
                    std::wstring name = Utf8ToWide(nameText);
                    std::lock_guard<std::mutex> lock(g_dataMapsMutex);
                    g_mapNames[id] = name;
                }
            }
        }

        searchPos = tagEnd + 1;
    }
}

static void ParseToolXml(const std::string& xml) {
    size_t searchPos = 0;
    size_t contentStart = 0;
    size_t closeStart = 0;
    size_t nextPos = 0;
    std::string startTag;
    while (FindElementBlock(xml, "tool", searchPos, contentStart, closeStart, nextPos, &startTag)) {
        std::string idText;
        std::string nameText;
        if (!ExtractAttributeValue(startTag, "id", idText) || !ExtractTagValue(xml.substr(contentStart, closeStart - contentStart), "name", nameText)) {
            searchPos = nextPos;
            continue;
        }

        int id = 0;
        if (!TryParseInt(idText, id)) {
            searchPos = nextPos;
            continue;
        }
        std::wstring name = Utf8ToWide(nameText);
        std::lock_guard<std::mutex> lock(g_dataMapsMutex);
        g_toolNames[id] = name;
        searchPos = nextPos;
    }
}

// 解析 Buff 信息 (bufInfo.xml)
static void ParseBufInfoXml(const std::string& xml) {
    std::lock_guard<std::mutex> lock(g_dataMapsMutex);
    g_bufNames.clear();
    g_bufDescs.clear();

    size_t searchPos = 0;
    size_t contentStart = 0;
    size_t closeStart = 0;
    size_t nextPos = 0;
    std::string startTag;
    while (FindElementBlock(xml, "bufInfo", searchPos, contentStart, closeStart, nextPos, &startTag)) {
        std::string idText;
        if (!ExtractAttributeValue(startTag, "id", idText)) {
            searchPos = nextPos;
            continue;
        }

        int id = 0;
        if (!TryParseInt(idText, id)) {
            searchPos = nextPos;
            continue;
        }

        std::string content = xml.substr(contentStart, closeStart - contentStart);
        std::string nameText;
        if (ExtractTagValue(content, "name", nameText)) {
            g_bufNames[id] = Utf8ToWide(nameText);
        }

        std::string descText;
        if (ExtractTagValue(content, "combat_desc", descText)) {
            g_bufDescs[id] = Utf8ToWide(descText);
        }

        searchPos = nextPos;
    }
}

// 解析系别和性格数据 (monsternature.xml)
static void ParseMonsterNatureXml(const std::string& xml) {
    std::lock_guard<std::mutex> lock(g_dataMapsMutex);
    g_elemNames.clear();
    g_geniusNames.clear();

    std::string elemSection;
    if (FindSectionByType(xml, "1", elemSection)) {
        ParseNamedItemsSection(elemSection, g_elemNames, 0);
    }

    if (g_elemNames.empty()) {
        static const wchar_t* defaultElems[] = {
            L"金", L"木", L"水", L"火", L"土", L"妖", L"魔", L"毒", L"圣",
            L"翼", L"雷", L"幻", L"怪", L"风", L"灵", L"特殊", L"无", L"冰",
            L"机械", L"火风", L"木灵", L"土幻", L"水妖", L"音", L"金怪"
        };
        for (int i = 0; i < 25; i++) {
            g_elemNames[i] = defaultElems[i];
        }
    }

    std::string geniusSection;
    if (FindSectionByType(xml, "0", geniusSection)) {
        ParseNamedItemsSection(geniusSection, g_geniusNames, 1);
    }
    
    // 如果解析失败，使用默认性格表
    if (g_geniusNames.empty()) {
        static const wchar_t* defaultGenius[] = {
            L"", L"孤僻", L"勇敢", L"固执", L"调皮", L"大胆", L"悠闲", L"淘气", L"无虑",
            L"胆小", L"急躁", L"开朗", L"天真", L"保守", L"稳重", L"冷静", L"马虎",
            L"沉着", L"狂妄", L"慎重", L"温顺", L"认真", L"实干", L"平静", L"坦率", L"浮躁"
        };
        for (int i = 1; i <= 25; i++) {
            g_geniusNames[i] = defaultGenius[i];
        }
    }

    // 资质名称 (根据AS3代码 CommonDefine.as 中的 aptLit)
    static const wchar_t* aptitudeLevelNames[] = {
        L"未知资质", L"泛泛之辈", L"璞玉之质", L"百里挑一", L"千载难逢", L"万众瞩目", L"绝代妖王"
    };
    for (int i = 0; i <= 6; i++) {
        g_aptitudeNames[i] = aptitudeLevelNames[i];
    }
}

// 前置声明
static void LoadHttpData();

bool PacketParser::Initialize() {
    std::lock_guard<std::mutex> lifecycleLock(g_parserLifecycleMutex);
    if (g_zlibModule) return true;

    // 内存加载 zlib.dll
    g_zlibModule = MemoryLoadLibrary(g_zlib1Data, g_zlib1Size);
    if (g_zlibModule) {
        g_uncompress = (PFN_UNCOMPRESS)MemoryGetProcAddress(g_zlibModule, "uncompress");
        g_compress = (PFN_COMPRESS)MemoryGetProcAddress(g_zlibModule, "compress");
        g_compress2 = (PFN_COMPRESS2)MemoryGetProcAddress(g_zlibModule, "compress2");
        g_deflateInit = (PFN_DEFLATEINIT)MemoryGetProcAddress(g_zlibModule, "deflateInit_");
        if (!g_deflateInit) {
            g_deflateInit = (PFN_DEFLATEINIT)MemoryGetProcAddress(g_zlibModule, "deflateInit");
        }
        g_deflate = (PFN_DEFLATE)MemoryGetProcAddress(g_zlibModule, "deflate");
        g_deflateEnd = (PFN_DEFLATEEND)MemoryGetProcAddress(g_zlibModule, "deflateEnd");
        g_inflateInit = (PFN_INFLATEINIT)MemoryGetProcAddress(g_zlibModule, "inflateInit_");
        if (!g_inflateInit) {
            g_inflateInit = (PFN_INFLATEINIT)MemoryGetProcAddress(g_zlibModule, "inflateInit");
        }
        g_inflateInit2 = (PFN_INFLATEINIT2)MemoryGetProcAddress(g_zlibModule, "inflateInit2_");
        if (!g_inflateInit2) {
            g_inflateInit2 = (PFN_INFLATEINIT2)MemoryGetProcAddress(g_zlibModule, "inflateInit2");
        }
        g_inflate = (PFN_INFLATE)MemoryGetProcAddress(g_zlibModule, "inflate");
        g_inflateEnd = (PFN_INFLATEEND)MemoryGetProcAddress(g_zlibModule, "inflateEnd");
    }

    // 加载HTTP数据
    LoadHttpData();

    return g_zlibModule != nullptr;
}

void PacketParser::Cleanup() {
    std::lock_guard<std::mutex> lifecycleLock(g_parserLifecycleMutex);
    g_recvBuffer.clear();
    {
        std::lock_guard<std::mutex> lock(g_battleMutex);
        g_currentBattle = BattleData{};
    }
    g_pendingRoundTip.clear();
    g_lastItemName.clear();
    g_battleStarted = false;
    if (g_zlibModule) {
        MemoryFreeLibrary(g_zlibModule);
        g_zlibModule = nullptr;
        g_uncompress = nullptr;
        g_compress = nullptr;
        g_compress2 = nullptr;
        g_deflateInit = nullptr;
        g_deflate = nullptr;
        g_deflateEnd = nullptr;
        g_inflateInit = nullptr;
        g_inflateInit2 = nullptr;
        g_inflate = nullptr;
        g_inflateEnd = nullptr;
    }
}

bool PacketParser::UncompressBody(const std::vector<uint8_t>& compressed, std::vector<uint8_t>& decompressed) {
    decompressed.clear();
    
    if (!g_uncompress || compressed.empty()) return false;
    
    // 安全检查：压缩数据大小限制（最大10MB）
    if (compressed.size() > (10 * 1024 * 1024)) return false;
    
    // 尝试多次解压，逐步增加缓冲区大小
    unsigned long destLen = compressed.size() * 4;
    const unsigned long maxDestLen = compressed.size() * 100; // 最大100倍扩展
    
    // 额外安全检查：防止整数溢出
    if (destLen > (50 * 1024 * 1024)) { // 最大解压后50MB
        return false;
    }
    
    int res = -5; // Z_BUF_ERROR
    
    while (destLen <= maxDestLen) {
        decompressed.resize(destLen);
        res = g_uncompress(decompressed.data(), &destLen, compressed.data(), (unsigned long)compressed.size());
        
        if (res == 0) { // Z_OK
            decompressed.resize(destLen);
            return true;
        }
        
        if (res != -5) { // 不是缓冲区不足错误，直接退出
            break;
        }
        
        // 缓冲区不足，增加大小重试
        unsigned long newDestLen = destLen * 2;
        
        // 检查是否会溢出或超过最大限制
        if (newDestLen < destLen || newDestLen > maxDestLen) {
            break;
        }
        
        destLen = newDestLen;
    }
    
    decompressed.clear();
    return false;
}

bool PacketParser::ParsePackets(const uint8_t* data, size_t size, BOOL bSend,
                                std::vector<GamePacket>& outPackets) {
    outPackets.clear();
    if (!data || size == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lifecycleLock(g_parserLifecycleMutex);

    if (bSend) {
        if (size < PacketProtocol::HEADER_SIZE) {
            return false;
        }

        size_t offset = 0;
        const uint16_t magic = ReadInt16LE(data, offset);
        const uint16_t bodyLength = ReadInt16LE(data, offset);
        if (magic != MAGIC_NUMBER_D && magic != MAGIC_NUMBER_C) {
            return false;
        }
        if (size < PacketProtocol::HEADER_SIZE + bodyLength) {
            return false;
        }

        GamePacket packet;
        packet.magic = magic;
        packet.length = bodyLength;
        packet.bSend = bSend;
        packet.opcode = ReadInt32LE(data, offset);
        packet.params = ReadInt32LE(data, offset);
        packet.body.assign(data + offset, data + offset + bodyLength);
        packet.rawBody = packet.body;

        if (magic == MAGIC_NUMBER_C) {
            std::vector<uint8_t> decompressed;
            if (UncompressBody(packet.body, decompressed)) {
                packet.body = std::move(decompressed);
            }
        }

        outPackets.push_back(std::move(packet));
        return true;
    }

    const size_t newSize = g_recvBuffer.size() + size;
    if (g_recvBuffer.capacity() < newSize) {
        g_recvBuffer.reserve((std::max)(newSize, g_recvBuffer.capacity() * 2));
    }
    g_recvBuffer.insert(g_recvBuffer.end(), data, data + size);

    if (g_recvBuffer.size() < PacketProtocol::HEADER_SIZE) {
        return false;
    }

    size_t readOffset = 0;
    const size_t dataSize = g_recvBuffer.size();
    bool foundAny = false;

    while (readOffset + PacketProtocol::HEADER_SIZE <= dataSize) {
        const uint16_t magic = static_cast<uint16_t>(g_recvBuffer[readOffset]) |
                               (static_cast<uint16_t>(g_recvBuffer[readOffset + 1]) << 8);
        if (magic != MAGIC_NUMBER_D && magic != MAGIC_NUMBER_C) {
            g_recvBuffer.clear();
            return foundAny;
        }

        const uint16_t bodyLength = static_cast<uint16_t>(g_recvBuffer[readOffset + 2]) |
                                    (static_cast<uint16_t>(g_recvBuffer[readOffset + 3]) << 8);
        const size_t packetSize = PacketProtocol::HEADER_SIZE + bodyLength;
        if (readOffset + packetSize > dataSize) {
            break;
        }

        GamePacket packet;
        packet.magic = magic;
        packet.length = bodyLength;
        packet.bSend = FALSE;
        packet.opcode = static_cast<uint32_t>(g_recvBuffer[readOffset + 4]) |
                        (static_cast<uint32_t>(g_recvBuffer[readOffset + 5]) << 8) |
                        (static_cast<uint32_t>(g_recvBuffer[readOffset + 6]) << 16) |
                        (static_cast<uint32_t>(g_recvBuffer[readOffset + 7]) << 24);
        packet.params = static_cast<uint32_t>(g_recvBuffer[readOffset + 8]) |
                        (static_cast<uint32_t>(g_recvBuffer[readOffset + 9]) << 8) |
                        (static_cast<uint32_t>(g_recvBuffer[readOffset + 10]) << 16) |
                        (static_cast<uint32_t>(g_recvBuffer[readOffset + 11]) << 24);

        const size_t bodyStart = readOffset + PacketProtocol::HEADER_SIZE;
        const size_t bodyEnd = bodyStart + bodyLength;
        packet.rawBody.assign(g_recvBuffer.begin() + bodyStart,
                              g_recvBuffer.begin() + bodyEnd);
        packet.body = packet.rawBody;

        if (magic == MAGIC_NUMBER_C) {
            std::vector<uint8_t> decompressed;
            if (UncompressBody(packet.body, decompressed)) {
                packet.body = std::move(decompressed);
            }
        }

        outPackets.push_back(std::move(packet));
        foundAny = true;
        readOffset += packetSize;
    }

    if (readOffset > 0) {
        g_recvBuffer.erase(g_recvBuffer.begin(), g_recvBuffer.begin() + readOffset);
    }

    return foundAny;
}

void PacketParser::SendToUI(const std::wstring& type, const std::wstring& data) {
    // 如果 g_hWnd 为空，尝试查找窗口
    if (!g_hWnd) {
        g_hWnd = FindWindowW(L"WebView2DemoWindowClass", L"卡布西游浮影微端 V1.15");
        if (!g_hWnd) {
            g_hWnd = FindWindowW(L"WebView2DemoWindowClass", nullptr);
        }
    }
    
    if (!g_hWnd) return;

    // Build JS call: window.addBattleData('type', 'data')
    // 使用 EscapeJsonString 转义字符串，防止特殊字符导致 JavaScript 语法错误
    std::wstring jsCode = L"if(window.addBattleData) { window.addBattleData('" + 
                          UIBridge::EscapeJsonString(type) + L"', '" + 
                          UIBridge::EscapeJsonString(data) + L"'); }";

    UIBridge::Instance().ExecuteJS(jsCode);
}

void PacketParser::SendBossListToUI() {
    if (g_bossList.empty()) return;
    
    // 如果 g_hWnd 为空，尝试查找窗口
    if (!g_hWnd) {
        g_hWnd = FindWindowW(L"WebView2DemoWindowClass", L"卡布西游浮影微端 V1.15");
        if (!g_hWnd) {
            g_hWnd = FindWindowW(L"WebView2DemoWindowClass", nullptr);
        }
    }
    
    if (!g_hWnd) return;
    
    std::wstring json = L"[";
    for (size_t i = 0; i < g_bossList.size(); i++) {
        if (i > 0) json += L",";
        json += L"{\"id\":" + std::to_wstring(g_bossList[i].id) + 
                L",\"name\":\"" + g_bossList[i].name + L"\"}";
    }
    json += L"]";
    
    std::wstring script = L"if(window.initBossList) { window.initBossList(" + json + L"); }";
    UIBridge::Instance().ExecuteJS(script);
}

static bool g_httpDataLoaded = false;

static void LoadHttpData() {
    if (g_httpDataLoaded) return;
    g_httpDataLoaded = true;

    // 从HTTP下载数据
    std::vector<uint8_t> zipbuf;
    if (!HttpGet(L"http://enter.wanwan4399.com/bin-debug/data/data", zipbuf)) {
        ParseMonsterNatureXml("");
        return;
    }

    if (zipbuf.size() < 4 || zipbuf[0] != 'P' || zipbuf[1] != 'K') {
        ParseMonsterNatureXml("");
        return;
    }

    std::vector<uint8_t> entry;

    // 解析sprite.xml
    entry.clear();
    if (ExtractZipEntry(zipbuf, "sprite.xml", entry)) {
        ParseSpriteXml(NormalizeXmlUtf8(entry));
    }

    // 解析skill.xml
    entry.clear();
    if (ExtractZipEntry(zipbuf, "skill.xml", entry)) {
        ParseSkillXml(NormalizeXmlUtf8(entry));
    }

    // 解析tool.xml
    entry.clear();
    if (ExtractZipEntry(zipbuf, "tool.xml", entry)) {
        ParseToolXml(NormalizeXmlUtf8(entry));
    }

    // 解析monsternature.xml
    entry.clear();
    bool natureOk = ExtractZipEntry(zipbuf, "monsternature.xml", entry);
    if (natureOk) {
        ParseMonsterNatureXml(NormalizeXmlUtf8(entry));
    } else {
        ParseMonsterNatureXml("");
    }
    
    // 解析bufInfo.xml (Buff 信息) - 如果之前本地加载失败，再尝试从 ZIP 加载
    {
        bool needLoad = false;
        {
            std::lock_guard<std::mutex> lock(g_dataMapsMutex);
            needLoad = g_bufNames.empty();
        }
        if (needLoad) {
            entry.clear();
            if (ExtractZipEntry(zipbuf, "bufInfo.xml", entry)) {
                ParseBufInfoXml(NormalizeXmlUtf8(entry));
            }
        }
    }

    // 解析map.xml (地图信息)
    entry.clear();
    if (ExtractZipEntry(zipbuf, "map.xml", entry)) {
        ParseMapXml(NormalizeXmlUtf8(entry));
    }
    
    // BOSS列表会在WebView2页面加载完成后由demo.cpp调用SendBossListToUI发送
}

std::wstring GetMapName(int mapId) {
    std::lock_guard<std::mutex> lock(g_dataMapsMutex);
    auto it = g_mapNames.find(mapId);
    if (it != g_mapNames.end()) {
        return it->second;
    }
    return L"";
}
int GetSkillPower(int skillId, int fallbackPower) {
    std::lock_guard<std::mutex> lock(g_dataMapsMutex);
    const auto it = g_skillPowers.find(skillId);
    return it != g_skillPowers.end() ? it->second : fallbackPower;
}

void PacketParser::UpdateUIBattleData() {
    std::lock_guard<std::mutex> battleLock(g_battleMutex);
    if (!g_hWnd) {
        // 使用正确的窗口类名
        g_hWnd = FindWindowW(L"WebView2DemoWindowClass", nullptr);
        if (!g_hWnd) {
            // 备用方案：通过窗口标题查找
        g_hWnd = FindWindowW(nullptr, L"卡布西游浮影微端 V1.15");
            if (!g_hWnd) {
                return;
            }
        }
    }

    // Convert BattleData to JSON string
    std::wstring jsData = L"{";
    const bool showBattleEntities = g_currentBattle.active;
    
    // My Pets
    jsData += L"\"myPets\":[";
    for (size_t i = 0; showBattleEntities && i < g_currentBattle.myPets.size(); ++i) {
        const auto& p = g_currentBattle.myPets[i];
        jsData += L"{\"spiritId\":" + std::to_wstring(p.spiritId) + 
                  L",\"name\":\"" + UIBridge::EscapeJsonString(p.name.empty() ? L"" : p.name) + L"\"" +
                  L",\"sid\":" + std::to_wstring(p.sid) +
                  L",\"uniqueId\":" + std::to_wstring(p.uniqueId) +
                  L",\"userId\":" + std::to_wstring(p.userId) +
                  L",\"groupType\":" + std::to_wstring(p.groupType) +
                  L",\"state\":" + std::to_wstring(p.state) +
                  L",\"rawState\":" + std::to_wstring(p.rawState) +
                  L",\"mNum\":" + std::to_wstring(p.mNum) +
                  L",\"hasMNum\":" + std::wstring(p.hasMNum ? L"true" : L"false") +
                  L",\"placeholder\":" + std::wstring(p.placeholder ? L"true" : L"false") +
                  L",\"hp\":" + std::to_wstring(p.hp) + 
                  L",\"maxHp\":" + std::to_wstring(p.maxHp) + 
                  L",\"level\":" + std::to_wstring(p.level) + 
                  L",\"skills\":[";
        for (size_t j = 0; j < p.skills.size(); ++j) {
            jsData += L"{\"id\":" + std::to_wstring(p.skills[j].id) + 
                      L",\"name\":\"" + UIBridge::EscapeJsonString(p.skills[j].name.empty() ? L"" : p.skills[j].name) + L"\"" +
                      L",\"pp\":" + std::to_wstring(p.skills[j].pp) + 
                      L",\"maxPp\":" + std::to_wstring(p.skills[j].maxPp) + L"}";
            if (j < p.skills.size() - 1) jsData += L",";
        }
        // 添加 Buff 列表
        jsData += L"],\"bufArr\":[";
        for (size_t j = 0; j < p.bufArr.size(); ++j) {
            const auto& buf = p.bufArr[j];
            jsData += L"{\"bufId\":" + std::to_wstring(buf.bufId) +
                      L",\"name\":\"" + UIBridge::EscapeJsonString(buf.name.empty() ? L"" : buf.name) + L"\"" +
                      L",\"tipString\":\"" + UIBridge::EscapeJsonString(buf.tipString) + L"\"" +
                      L",\"round\":" + std::to_wstring(buf.round) +
                      L",\"param1\":" + std::to_wstring(buf.param1) +
                      L",\"param2\":" + std::to_wstring(buf.param2) +
                      L",\"param3\":" + std::to_wstring(buf.param3) +
                      L",\"param4\":" + std::to_wstring(buf.param4) +
                      L",\"leftOrRight\":" + std::to_wstring(buf.leftOrRight) +
                      L",\"addOrRemove\":" + std::to_wstring(buf.addOrRemove) + L"}";
            if (j < p.bufArr.size() - 1) jsData += L",";
        }
        jsData += L"]}";
        if (i < g_currentBattle.myPets.size() - 1) jsData += L",";
    }
    jsData += L"],";

    // Other Pets
    jsData += L"\"otherPets\":[";
    for (size_t i = 0; showBattleEntities && i < g_currentBattle.otherPets.size(); ++i) {
        const auto& p = g_currentBattle.otherPets[i];
        jsData += L"{\"spiritId\":" + std::to_wstring(p.spiritId) + 
                  L",\"name\":\"" + UIBridge::EscapeJsonString(p.name.empty() ? L"" : p.name) + L"\"" +
                  L",\"sid\":" + std::to_wstring(p.sid) +
                  L",\"uniqueId\":" + std::to_wstring(p.uniqueId) +
                  L",\"userId\":" + std::to_wstring(p.userId) +
                  L",\"groupType\":" + std::to_wstring(p.groupType) +
                  L",\"state\":" + std::to_wstring(p.state) +
                  L",\"rawState\":" + std::to_wstring(p.rawState) +
                  L",\"mNum\":" + std::to_wstring(p.mNum) +
                  L",\"hasMNum\":" + std::wstring(p.hasMNum ? L"true" : L"false") +
                  L",\"placeholder\":" + std::wstring(p.placeholder ? L"true" : L"false") +
                  L",\"hp\":" + std::to_wstring(p.hp) + 
                  L",\"maxHp\":" + std::to_wstring(p.maxHp) + 
                  L",\"level\":" + std::to_wstring(p.level) + 
                  L",\"skills\":[";
        for (size_t j = 0; j < p.skills.size(); ++j) {
            jsData += L"{\"id\":" + std::to_wstring(p.skills[j].id) + 
                      L",\"name\":\"" + UIBridge::EscapeJsonString(p.skills[j].name.empty() ? L"" : p.skills[j].name) + L"\"" +
                      L",\"pp\":" + std::to_wstring(p.skills[j].pp) + 
                      L",\"maxPp\":" + std::to_wstring(p.skills[j].maxPp) + L"}";
            if (j < p.skills.size() - 1) jsData += L",";
        }
        // 添加 Buff 列表
        jsData += L"],\"bufArr\":[";
        for (size_t j = 0; j < p.bufArr.size(); ++j) {
            const auto& buf = p.bufArr[j];
            jsData += L"{\"bufId\":" + std::to_wstring(buf.bufId) +
                      L",\"name\":\"" + UIBridge::EscapeJsonString(buf.name.empty() ? L"" : buf.name) + L"\"" +
                      L",\"tipString\":\"" + UIBridge::EscapeJsonString(buf.tipString) + L"\"" +
                      L",\"round\":" + std::to_wstring(buf.round) +
                      L",\"param1\":" + std::to_wstring(buf.param1) +
                      L",\"param2\":" + std::to_wstring(buf.param2) +
                      L",\"param3\":" + std::to_wstring(buf.param3) +
                      L",\"param4\":" + std::to_wstring(buf.param4) +
                      L",\"leftOrRight\":" + std::to_wstring(buf.leftOrRight) +
                      L",\"addOrRemove\":" + std::to_wstring(buf.addOrRemove) + L"}";
            if (j < p.bufArr.size() - 1) jsData += L",";
        }
        jsData += L"]}";
        if (i < g_currentBattle.otherPets.size() - 1) jsData += L",";
    }
    jsData += L"],";
    
    jsData += L"\"myActiveIndex\":" + std::to_wstring(g_currentBattle.myActiveIndex) + L",";
    jsData += L"\"otherActiveIndex\":" + std::to_wstring(g_currentBattle.otherActiveIndex) + L",";
    jsData += L"\"battleType\":" + std::to_wstring(g_currentBattle.battleType) + L",";
    jsData += L"\"escape\":" + std::to_wstring(g_currentBattle.escape) + L",";
    jsData += L"\"round\":" + std::to_wstring(g_currentBattle.round) + L",";
    jsData += L"\"lastItemName\":\"" + UIBridge::EscapeJsonString(g_lastItemName) + L"\"";
    jsData += L"}";

    std::wstring jsCode = L"if(window.updateBattleUI) { window.updateBattleUI(" + jsData + L"); }";
    UIBridge::Instance().ExecuteJS(jsCode);
}

void PacketParser::ProcessLingyuPacket(const GamePacket& packet) {
    if (packet.bSend || packet.opcode != OPCODE_LINGYU_LIST) return;

    size_t offset = 0;
    const uint8_t* data = packet.body.data();
    size_t size = packet.body.size();

    if (offset + 4 > size) return;
    int32_t backFlag = ReadInt32LE(data, offset);

    std::vector<LingyuItem> allItems;
    auto mapping = std::unordered_map<int, std::wstring>{
        {25, L"体力"}, {21, L"攻击"}, {22, L"防御"}, {23, L"法术"}, {24, L"抗性"}, {26, L"速度"}, {11, L"威力"}, {12, L"PP"}
    };

    auto readList = [&](size_t& off) {
        if (off + 4 > size) return;
        int32_t count = ReadInt32LE(data, off);
        for (int i = 0; i < count; i++) {
            if (off + 12 > size) break;
            LingyuItem item;
            item.symmId = ReadInt32LE(data, off);
            {
                std::lock_guard<std::mutex> lock(g_dataMapsMutex);
                auto it = g_toolNames.find(item.symmId);
                std::wstring name = (it != g_toolNames.end()) ? it->second : L"未知灵玉";
                // Shorten name: e.g., "1级属性灵玉" -> "1级灵玉"
                size_t attrPos = name.find(L"属性");
                if (attrPos != std::wstring::npos) {
                    name.erase(attrPos, 2);
                }
                item.symmName = name;
            }
            item.symmIndex = ReadInt32LE(data, off);
            item.symmFlag = ReadInt32LE(data, off);

            if (off + 2 > size) break;
            uint16_t nameLen = ReadUInt16LE(data, off);
            if (off + nameLen > size) break;
            std::string nameUtf8((const char*)(data + off), nameLen);
            item.petName = Utf8ToWide(nameUtf8);
            off += nameLen;

            if (off + 4 > size) break;
            item.symmType = ReadInt32LE(data, off);

            if (off + 4 > size) break;
            int32_t nativeLen = ReadInt32LE(data, off);
            for (int j = 0; j < nativeLen; j++) {
                if (off + 8 > size) break;
                LingyuAttribute attr;
                attr.nativeEnum = ReadInt32LE(data, off);
                attr.nativeValue = ReadInt32LE(data, off);
                auto it = mapping.find(attr.nativeEnum);
                attr.nativeName = (it != mapping.end()) ? it->second : L"未知";
                item.nativeList.push_back(attr);
            }
            allItems.push_back(item);
        }
    };

    if (backFlag == 1) {
        readList(offset);
        readList(offset);
    } else if (backFlag == 2 || backFlag == 3) {
        readList(offset);
    }

    // Convert to JSON and send to UI
    std::wstring jsData = L"{\"backFlag\":" + std::to_wstring(backFlag) + L",\"items\":[";
    for (size_t i = 0; i < allItems.size(); i++) {
        const auto& item = allItems[i];
        jsData += L"{\"symmId\":" + std::to_wstring(item.symmId) +
                  L",\"symmName\":\"" + item.symmName + L"\"" +
                  L",\"symmIndex\":" + std::to_wstring(item.symmIndex) +
                  L",\"symmFlag\":" + std::to_wstring(item.symmFlag) +
                  L",\"petName\":\"" + item.petName + L"\"" +
                  L",\"symmType\":" + std::to_wstring(item.symmType) +
                  L",\"nativeList\":[";
        for (size_t j = 0; j < item.nativeList.size(); j++) {
            const auto& attr = item.nativeList[j];
            jsData += L"{\"nativeEnum\":" + std::to_wstring(attr.nativeEnum) +
                      L",\"nativeValue\":" + std::to_wstring(attr.nativeValue) +
                      L",\"nativeName\":\"" + attr.nativeName + L"\"}";
            if (j < item.nativeList.size() - 1) jsData += L",";
        }
        jsData += L"]}";
        if (i < allItems.size() - 1) jsData += L",";
    }
    jsData += L"]}";

    std::wstring jsCode = L"if(window.updateLingyuUI) { window.updateLingyuUI(" + jsData + L"); }";
    UIBridge::Instance().ExecuteJS(jsCode);
}

void PacketParser::ReplaceCurrentBattle(BattleData battle) {
    std::lock_guard<std::mutex> lock(g_battleMutex);
    g_currentBattle = std::move(battle);
}

void PacketParser::ProcessBattlePacketSafe(const GamePacket& packet) {
    if (packet.bSend || !packet.bodyDecoded) return;

    const int32_t params = static_cast<int32_t>(packet.params);
    const uint8_t* data = packet.body.data();
    const size_t size = packet.body.size();

    auto entityLabel = [](const BattleEntity& pet) {
        if (pet.placeholder) return std::wstring(L"待揭示妖怪");
        return pet.name.empty() ? (std::wstring(L"妖怪") + std::to_wstring(pet.spiritId)) : pet.name;
    };
    auto entityLabelBySid = [&](const BattleData& battle, int32_t sid) {
        if (const BattleEntity* pet = FindBattleEntityBySid(battle, sid)) {
            return entityLabel(*pet);
        }
        return sid == 0 ? std::wstring(L"未知妖怪") : (std::wstring(L"妖怪") + std::to_wstring(sid));
    };
    auto skillLabel = [](int32_t skillId) {
        std::lock_guard<std::mutex> lock(g_dataMapsMutex);
        const auto it = g_skillNames.find(skillId);
        return it != g_skillNames.end() ? it->second : (std::wstring(L"技能") + std::to_wstring(skillId));
    };
    auto flushPendingTip = []() {
        if (!g_pendingRoundTip.empty()) {
            PacketParser::SendToUI(L"回合", g_pendingRoundTip);
            g_pendingRoundTip.clear();
        }
    };
    auto sendJson = [](const wchar_t* type, const std::wstring& json) {
        PacketParser::SendToUI(type, json);
    };
    auto sendPrompt = [](const wchar_t* type, const std::wstring& prompt) {
        if (!prompt.empty()) PacketParser::SendToUI(type, prompt);
    };
    auto setHpBySid = [](BattleData& battle, int32_t sid, int32_t hp) {
        if (BattleEntity* pet = FindBattleEntityBySid(battle, sid)) {
            pet->hp = hp;
        }
    };
    if (packet.opcode == OPCODE_BATTLE_START) {
        if (params < 0) return;

        const int32_t localUserId = static_cast<int32_t>(g_userId.load());
        const bool observer = IsBattleObserver();

        BattleData parsedBattle;
        bool parsed = false;
        std::function<bool(BoundedReader, int32_t, BattleData)> parseEntities;
        parseEntities = [&](BoundedReader reader, int32_t state, BattleData battle) -> bool {
            if (state == -1) {
                int32_t escape = 0;
                if (!reader.ReadI32(escape) || reader.Remaining() != 0) return false;
                battle.escape = escape;
                parsedBattle = std::move(battle);
                return true;
            }
            if (battle.myPets.size() + battle.otherPets.size() >= 256) return false;

            BattleEntity pet;
            if (!ReadBattleEntityAfterState(reader, state, pet)) return false;
            const bool isMy = observer
                ? pet.groupType == 1
                : (localUserId > 0 ? pet.userId == localUserId : pet.groupType == 1);
            pet.mySpirit = isMy;


            BattleData base = std::move(battle);
            auto appendPet = [&](BattleData candidate, const BattleEntity& entity) {
                if (entity.state == 1) {
                    if (entity.mySpirit) {
                        candidate.myActiveIndex = static_cast<int32_t>(candidate.myPets.size());
                    } else {
                        candidate.otherActiveIndex = static_cast<int32_t>(candidate.otherPets.size());
                    }
                }
                if (entity.mySpirit) candidate.myPets.push_back(entity);
                else candidate.otherPets.push_back(entity);
                return candidate;
            };
            auto appendMNumPlaceholders = [](BattleData candidate, const BattleEntity& entity) {
                if (!entity.hasMNum || entity.mNum <= 1) return candidate;
                std::vector<BattleEntity>& team = entity.mySpirit ? candidate.myPets : candidate.otherPets;
                const int32_t placeholderCount = entity.mNum - 1;
                if (placeholderCount > 1024 || team.size() + static_cast<size_t>(placeholderCount) > 256) {
                    return BattleData{};
                }
                for (int32_t i = 0; i < placeholderCount; ++i) {
                    BattleEntity placeholder;
                    placeholder.placeholder = true;
                    placeholder.mySpirit = entity.mySpirit;
                    placeholder.groupType = entity.groupType;
                    team.push_back(std::move(placeholder));
                }
                return candidate;
            };
            auto continueWith = [&](BoundedReader nextReader, BattleData candidate) {
                int32_t nextState = 0;
                if (!nextReader.ReadI32(nextState)) return false;
                return parseEntities(nextReader, nextState, std::move(candidate));
            };

            const bool normalMNum = !observer && !isMy && pet.rawState == 2;
            const bool observerMNum = observer && pet.skillNum == 0;
            if (normalMNum) {
                if (!reader.ReadI32(pet.mNum) || pet.mNum < 0 || pet.mNum > 1024) return false;
                pet.hasMNum = true;
                BattleData candidate = appendMNumPlaceholders(appendPet(std::move(base), pet), pet);
                if (candidate.myPets.empty() && candidate.otherPets.empty()) return false;
                return continueWith(reader, std::move(candidate));
            }

            if (observerMNum) {
                // The AS3 observer parser has two wire layouts: the default look mode
                // carries mNum, while newLookType==2 does not. Try the no-mNum layout
                // first and accept only a complete entity list plus escape field.
                if (continueWith(reader, appendPet(base, pet))) return true;

                BoundedReader withMNumReader = reader;
                BattleEntity withMNumPet = pet;
                if (!withMNumReader.ReadI32(withMNumPet.mNum) ||
                    withMNumPet.mNum < 0 || withMNumPet.mNum > 1024) {
                    return false;
                }
                withMNumPet.hasMNum = true;
                BattleData candidate = appendMNumPlaceholders(appendPet(std::move(base), withMNumPet), withMNumPet);
                if (candidate.myPets.empty() && candidate.otherPets.empty()) return false;
                return continueWith(withMNumReader, std::move(candidate));
            }

            return continueWith(reader, appendPet(std::move(base), pet));
        };

        BoundedReader reader(data, size);
        int32_t firstState = 0;
        if (!reader.ReadI32(firstState)) return;
        BattleData initial;
        initial.battleType = params;
        initial.active = true;
        parsed = parseEntities(reader, firstState, std::move(initial));
        if (!parsed) return;

        flushPendingTip();
        ReplaceCurrentBattle(std::move(parsedBattle));
        g_battleStarted = true;
        UpdateUIBattleData();

        const BattleData battle = GetCurrentBattleSnapshot();
        SyncBattleSixAutoBattleState(battle, true);
        std::wstring myName;
        std::wstring otherName;
        if (battle.myActiveIndex >= 0 &&
            battle.myActiveIndex < static_cast<int32_t>(battle.myPets.size())) {
            myName = entityLabel(battle.myPets[battle.myActiveIndex]);
        }
        if (battle.otherActiveIndex >= 0 &&
            battle.otherActiveIndex < static_cast<int32_t>(battle.otherPets.size())) {
            otherName = entityLabel(battle.otherPets[battle.otherActiveIndex]);
        }
        sendJson(L"战斗开始", L"{\"battleType\":" + std::to_wstring(params) +
                 L",\"escape\":" + std::to_wstring(battle.escape) +
                 L",\"my\":\"" + UIBridge::EscapeJsonString(myName) +
                 L"\",\"other\":\"" + UIBridge::EscapeJsonString(otherName) + L"\"}");
        sendPrompt(L"战斗开始", L"我方" + myName + L" vs 敌方" + otherName);
        return;
    }

    if (packet.opcode == OPCODE_BATTLE_ROUND_START) {
        BattleData next = GetCurrentBattleSnapshot();
        if (!next.active) return;
        ++next.round;
        const int32_t roundNumber = next.round;
        ReplaceCurrentBattle(std::move(next));
        UpdateUIBattleData();
        if (g_battleSixAuto.IsInBattle() && g_battleSixAuto.IsAutoBattleEnabled()) {
            g_battleSixRoundToken.fetch_add(1);
            g_battleSixAuto.OnBattleRoundStart();
        }
        sendJson(L"回合开始", L"{\"round\":" + std::to_wstring(roundNumber) +
                 L",\"params\":" + std::to_wstring(params) + L"}");
        sendPrompt(L"回合开始", L"第" + std::to_wstring(roundNumber) + L"回合开始");
        return;
    }

    if (packet.opcode == Opcode::BATTLE_CHANGE_SPIRIT_ROUND) {
        if (params < 0 || params > 128) return;
        BoundedReader reader(data, size);
        std::vector<int32_t> sids;
        sids.reserve(static_cast<size_t>(params));
        for (int32_t i = 0; i < params; ++i) {
            int32_t sid = 0;
            if (!reader.ReadI32(sid)) return;
            sids.push_back(sid);
        }
        int32_t waitTime = 0;
        if (!reader.ReadI32(waitTime)) return;

        BattleData next = GetCurrentBattleSnapshot();
        next.roundChangeSids = std::move(sids);
        next.roundWaitTime = waitTime;
        ++next.round;
        const int32_t roundNumber = next.round;
        ReplaceCurrentBattle(std::move(next));
        UpdateUIBattleData();
        if (g_battleSixAuto.IsInBattle() && g_battleSixAuto.IsAutoBattleEnabled()) {
            g_battleSixRoundToken.fetch_add(1);
            g_battleSixAuto.OnBattleRoundStart();
        }
        sendJson(L"回合开始", L"{\"changeCount\":" + std::to_wstring(params) +
                 L",\"round\":" + std::to_wstring(roundNumber) +
                 L",\"waitTime\":" + std::to_wstring(waitTime) + L"}");
        sendPrompt(L"回合开始", L"进入换宠回合，等待" + std::to_wstring(waitTime) + L"毫秒");
        return;
    }

    if (packet.opcode == OPCODE_BATTLE_BUF) {
        BoundedReader reader(data, size);
        BufData buf;
        if (!reader.ReadI32(buf.addOrRemove) || !reader.ReadI32(buf.bufId) ||
            !reader.ReadI32(buf.defId) || !reader.ReadI32(buf.param1) ||
            !reader.ReadI32(buf.param2)) {
            return;
        }
        if (buf.addOrRemove == BufDataType::BUF_TYPE_7) {
            if (!reader.ReadI32(buf.param3) || !reader.ReadI32(buf.param4)) return;
        }
        buf.name = GetBufName(buf.bufId, buf.param1);
        buf.tipString = GetBufTipString(buf.bufId, buf.param1, buf.param2);

        BattleData next = GetCurrentBattleSnapshot();
        const std::wstring targetName = entityLabelBySid(next, buf.defId);
        if (buf.addOrRemove == BufDataType::BUF_TYPE_0) {
            RemoveBattleBufFromBattle(next, buf.defId, buf.bufId);
        } else if (buf.addOrRemove == BufDataType::BUF_TYPE_1 ||
                   buf.addOrRemove == BufDataType::BUF_TYPE_7) {
            ApplyBattleBufToBattle(next, buf.defId, buf);
        } else if (buf.addOrRemove == BufDataType::BUF_TYPE_2) {
            ApplyBattleBufToBattle(next, buf.defId, buf);
            ApplyBattleBuffBloodEffect(next, buf);
        } else if (buf.addOrRemove == BufDataType::BUF_TYPE_4) {
            AdjustBattleEntityHp(next, buf.defId, -buf.param1);
        } else if (buf.addOrRemove == BufDataType::BUF_TYPE_5) {
            AdjustBattleEntityHp(next, buf.defId, buf.param1);
        } else if (buf.addOrRemove == BufDataType::BUF_TYPE_3) {
            ApplyBattlePpDelta(next, buf.defId, -buf.param1);
        } else if (buf.addOrRemove == BufDataType::BUF_TYPE_6) {
            ApplyBattlePpDelta(next, buf.defId, buf.param1);
        }
        SyncBattleSixAutoBattleState(next, false);
        ReplaceCurrentBattle(std::move(next));
        UpdateUIBattleData();

        const wchar_t* eventType = buf.addOrRemove == BufDataType::BUF_TYPE_4 ? L"战斗血量事件" :
                                    (buf.addOrRemove == BufDataType::BUF_TYPE_5 ? L"战斗恢复事件" : L"战斗状态");
        sendJson(eventType,
                 L"{\"addOrRemove\":" + std::to_wstring(buf.addOrRemove) +
                 L",\"bufId\":" + std::to_wstring(buf.bufId) +
                 L",\"defId\":" + std::to_wstring(buf.defId) +
                 L",\"param1\":" + std::to_wstring(buf.param1) +
                 L",\"param2\":" + std::to_wstring(buf.param2) +
                 L",\"param3\":" + std::to_wstring(buf.param3) +
                 L",\"param4\":" + std::to_wstring(buf.param4) + L"}");
        std::wstring prompt;
        if (buf.addOrRemove == BufDataType::BUF_TYPE_0) {
            prompt = targetName + L"移除状态：" + buf.name;
        } else if (buf.addOrRemove == BufDataType::BUF_TYPE_4) {
            prompt = targetName + L"发生血量变化：" + buf.name;
        } else if (buf.addOrRemove == BufDataType::BUF_TYPE_5) {
            prompt = targetName + L"恢复状态：" + buf.name;
        } else {
            prompt = targetName + L"状态变化：" + buf.name;
            if (buf.round > 0) prompt += L"（" + std::to_wstring(buf.round) + L"回合）";
        }
        sendPrompt(eventType, prompt);
        return;
    }

    if (packet.opcode == OPCODE_BATTLE_BUF_DIS) {
        BoundedReader reader(data, size);
        int32_t bufId = 0;
        int32_t defId = 0;
        if (!reader.ReadI32(bufId) || !reader.ReadI32(defId)) return;
        BattleData next = GetCurrentBattleSnapshot();
        const std::wstring targetName = entityLabelBySid(next, defId);
        const std::wstring bufName = GetBufName(bufId);
        RemoveBattleBufFromBattle(next, defId, bufId);
        ReplaceCurrentBattle(std::move(next));
        UpdateUIBattleData();
        sendJson(L"战斗状态", L"{\"addOrRemove\":0,\"bufId\":" + std::to_wstring(bufId) +
                 L",\"defId\":" + std::to_wstring(defId) + L"}");
        sendPrompt(L"战斗状态", targetName + L"移除状态：" + bufName);
        return;
    }

    if (packet.opcode == OPCODE_BATTLE_BUFS) {
        BoundedReader reader(data, size);
        BattleData next = GetCurrentBattleSnapshot();
        int32_t sid = 0;
        if (!reader.ReadI32(sid)) return;
        int count = 0;
        std::wstring bulkTip;
        while (sid != -1 && sid < 1000) {
            if (++count > 1024) return;
            BufData buf;
            buf.defId = sid;
            buf.addOrRemove = BufDataType::BUF_TYPE_1;
            if (!reader.ReadI32(buf.bufId) || !reader.ReadI32(buf.param1) ||
                !reader.ReadI32(buf.param2)) return;
            buf.name = GetBufName(buf.bufId, buf.param1);
            buf.tipString = GetBufTipString(buf.bufId, buf.param1, buf.param2);
            if (!bulkTip.empty()) bulkTip += L"，";
            bulkTip += entityLabelBySid(next, buf.defId) + L"：" + buf.name;
            ApplyBattleBufToBattle(next, buf.defId, buf);
            if (!reader.ReadI32(sid)) return;
        }
        ReplaceCurrentBattle(std::move(next));
        UpdateUIBattleData();
        sendJson(L"战斗状态", L"{\"bulkCount\":" + std::to_wstring(count) + L"}");
        sendPrompt(L"战斗状态", bulkTip.empty() ? L"状态同步完成" : L"状态同步：" + bulkTip);
        return;
    }

    if (packet.opcode == OPCODE_BATTLE_ROUND) {
        if (params < 0 || params > 5) return;
        BattleData next = GetCurrentBattleSnapshot();
        BattleRoundData round;
        round.cmdType = params;
        BoundedReader reader(data, size);

        if (params == 0) {
            if (!reader.ReadI32(round.haveBattle)) return;
            if (round.haveBattle == 1) {
                if (!reader.ReadI32(round.atkId) || !reader.ReadI32(round.skillId) ||
                    !reader.ReadI32(round.defId) || !reader.ReadI32(round.miss)) return;
                if (round.miss == 0) {
                    if (!reader.ReadI32(round.brust) || !reader.ReadI32(round.atkHp) ||
                        !reader.ReadI32(round.defHp) || !reader.ReadI32(round.reboundHp)) return;
                    setHpBySid(next, round.atkId, round.atkHp);
                    setHpBySid(next, round.defId, round.defHp);
                }
                DecreaseBattleSkillPp(next, round.atkId, round.skillId);
            }

            int32_t haveBuf = 0;
            if (!reader.ReadI32(haveBuf)) return;
            while (haveBuf != 0) {
                if (haveBuf < 0) return;
                BufData buf;
                buf.bufId = haveBuf;
                if (!reader.ReadI32(buf.addOrRemove)) return;
                switch (buf.addOrRemove) {
                    case BufDataType::BUF_TYPE_0:
                        if (!reader.ReadI32(buf.defId)) return;
                        buf.name = GetBufName(buf.bufId);
                        buf.tipString = GetBufTipString(buf.bufId, 0, 0);
                        RemoveBattleBufFromBattle(next, buf.defId, buf.bufId);
                        round.bufs.push_back(buf);
                        break;
                    case BufDataType::BUF_TYPE_1:
                        if (!reader.ReadI32(buf.atkId) || !reader.ReadI32(buf.defId) ||
                            !reader.ReadI32(buf.round) || !reader.ReadI32(buf.param1) ||
                            !reader.ReadI32(buf.param2)) return;
                        buf.name = GetBufName(buf.bufId, buf.param1);
                        buf.tipString = GetBufTipString(buf.bufId, buf.param1, buf.param2);
                        ApplyBattleBufToBattle(next, buf.defId, buf);
                        round.bufs.push_back(buf);
                        break;
                    case BufDataType::BUF_TYPE_2:
                        if (!reader.ReadI32(buf.atkId) || !reader.ReadI32(buf.defId) ||
                            !reader.ReadI32(buf.param1) || !reader.ReadI32(buf.param2)) return;
                        buf.name = GetBufName(buf.bufId, buf.param1);
                        buf.tipString = GetBufTipString(buf.bufId, buf.param1, buf.param2);
                        ApplyBattleBufToBattle(next, buf.defId, buf);
                        ApplyBattleBuffBloodEffect(next, buf);
                        round.bufs.push_back(buf);
                        break;
                    case BufDataType::BUF_TYPE_3:
                    case BufDataType::BUF_TYPE_6: {
                        int32_t value = 0;
                        if (!reader.ReadI32(value)) return;
                        BattleRoundAppend append;
                        append.type = buf.addOrRemove;
                        append.paramKey = static_cast<int32_t>(buf.bufId);
                        append.paramValue = value;
                        ApplyBattlePpDelta(next, round.defId, buf.addOrRemove == BufDataType::BUF_TYPE_3 ? -value : value);
                        // AS3 does not carry a target SID for append entries.
                        round.appends.push_back(append);
                        break;
                    }
                    case BufDataType::BUF_TYPE_4: {
                        BattleRoundAppend append;
                        append.type = buf.addOrRemove;
                        append.paramKey = static_cast<int32_t>(buf.bufId);
                        if (!reader.ReadI32(append.skillId) || !reader.ReadI32(append.paramValue)) return;
                        buf.param1 = append.paramValue;
                        buf.name = GetBufName(buf.bufId);
                        if (append.paramKey == 1) {
                            ApplyBattleSkillPpDelta(next, round.defId, append.skillId, -append.paramValue);
                        }
                        round.appends.push_back(append);
                        round.bufs.push_back(buf);
                        break;
                    }
                    case BufDataType::BUF_TYPE_5:
                        // AS3 has no payload read for type 5; it is retained in bufArr.
                        buf.name = GetBufName(buf.bufId);
                        round.bufs.push_back(buf);
                        break;
                    default:
                        // No payload length is known for an unconfirmed round Buff type.
                        return;
                }
                if (!reader.ReadI32(haveBuf)) return;
            }
        } else if (params == 1) {
            if (!reader.ReadI32(round.sid) || !reader.ReadI32(round.uniqueId) ||
                !reader.ReadI32(round.sta)) return;

            BattleEntity* oldEntity = FindBattleEntityBySid(next, round.sid);
            bool isMySide = oldEntity ? oldEntity->mySpirit : false;
            if (round.sta == 1 && !IsBattleObserver()) {
                int32_t rawState = 0;
                if (!reader.ReadI32(rawState)) return;
                if (!ReadBattleEntityAfterState(reader, rawState, round.switchEntity)) return;
                round.switchEntity.mySpirit = isMySide ||
                    (g_userId.load() > 0 && round.switchEntity.userId == static_cast<int32_t>(g_userId.load()));
                round.hasSwitchEntity = true;

                std::vector<BattleEntity>& team = round.switchEntity.mySpirit ? next.myPets : next.otherPets;
                int32_t& activeIndex = round.switchEntity.mySpirit ? next.myActiveIndex : next.otherActiveIndex;
                int found = -1;
                for (size_t i = 0; i < team.size(); ++i) {
                    if (round.switchEntity.uniqueId != 0 && team[i].uniqueId == round.switchEntity.uniqueId) {
                        found = static_cast<int>(i);
                        break;
                    }
                }
                if (found < 0) {
                    for (size_t i = 0; i < team.size(); ++i) {
                        if (team[i].sid == round.sid || team[i].placeholder) {
                            found = static_cast<int>(i);
                            break;
                        }
                    }
                }
                if (found < 0) {
                    team.push_back(round.switchEntity);
                    found = static_cast<int>(team.size() - 1);
                } else {
                    team[found] = round.switchEntity;
                }
                activeIndex = found;
            } else if (round.sta == 1) {
                for (auto& pet : next.myPets) if (pet.uniqueId == round.uniqueId) next.myActiveIndex = static_cast<int32_t>(&pet - next.myPets.data());
                for (auto& pet : next.otherPets) if (pet.uniqueId == round.uniqueId) next.otherActiveIndex = static_cast<int32_t>(&pet - next.otherPets.data());
            }
        } else if (params == 2) {
            if (!reader.ReadI32(round.atkId) || !reader.ReadI32(round.itemId) ||
                !reader.ReadI32(round.defId) || !reader.ReadI32(round.param0) ||
                !reader.ReadI32(round.param1)) return;
            if ((round.itemId & 64) != 0 && round.param0 == 1) {
                int32_t* fields[] = {
                    &round.catchData.spiritId, &round.catchData.spiritType, &round.catchData.spiritHp,
                    &round.catchData.spiritAtk, &round.catchData.spiritDef, &round.catchData.spiritMagAtk,
                    &round.catchData.spiritMagDef, &round.catchData.spiritSpeed, &round.catchData.spiritLevel,
                    &round.catchData.spiritSid, &round.catchData.spiritPackId, &round.catchData.spiritSex,
                    &round.catchData.attackGeniusValue, &round.catchData.defenceGeniusValue,
                    &round.catchData.magicGeniusValue, &round.catchData.resistanceGeniusValue,
                    &round.catchData.hpGeniusValue, &round.catchData.speedGeniusValue
                };
                for (int32_t* field : fields) if (!reader.ReadI32(*field)) return;
                round.hasCatchData = true;
            }
        } else if (params == 3) {
            if (!reader.ReadI32(round.atkId) || !reader.ReadI32(round.avail)) return;
        } else if (params == 5) {
            if (!reader.ReadI32(round.status) ||
                (round.status != 1 && round.status != 2)) {
                // The AS3 client only dispatches the two documented restriction codes.
                return;
            }
        }

        std::wstring roundPrompt;
        if (params == 0) {
            if (round.haveBattle == 1) {
                const std::wstring atkName = entityLabelBySid(next, round.atkId);
                const std::wstring defName = entityLabelBySid(next, round.defId);
                const std::wstring skillName = skillLabel(round.skillId);
                if (round.miss != 0) {
                    roundPrompt = defName + L"闪避了" + atkName + L"的" + skillName;
                } else {
                    roundPrompt = atkName + L"使用" + skillName + L"，" + defName +
                                  L"当前生命：" + std::to_wstring(round.defHp);
                    if (const BattleEntity* target = FindBattleEntityBySid(next, round.defId)) {
                        roundPrompt += L"/" + std::to_wstring(target->maxHp);
                    }
                }
            } else {
                roundPrompt = L"本回合没有发生攻击";
            }
            for (const auto& buf : round.bufs) {
                if (!roundPrompt.empty()) roundPrompt += L"；";
                const std::wstring targetName = entityLabelBySid(next, buf.defId);
                if (buf.addOrRemove == BufDataType::BUF_TYPE_0) {
                    roundPrompt += targetName + L"移除状态：" + buf.name;
                } else {
                    roundPrompt += targetName + L"状态变化：" + buf.name;
                    if (buf.round > 0) roundPrompt += L"（" + std::to_wstring(buf.round) + L"回合）";
                }
            }
            if (!round.appends.empty()) {
                if (!roundPrompt.empty()) roundPrompt += L"；";
                roundPrompt += L"战斗附加数据已由服务端更新";
            }
        } else if (params == 1) {
            if (round.hasSwitchEntity) {
                roundPrompt = std::wstring(round.switchEntity.mySpirit ? L"我方" : L"敌方") +
                              L"上场" + entityLabel(round.switchEntity);
            } else {
                roundPrompt = L"切换宠物：" + entityLabelBySid(next, round.sid);
            }
        } else if (params == 2) {
            std::wstring itemName;
            {
                std::lock_guard<std::mutex> lock(g_dataMapsMutex);
                const auto it = g_toolNames.find(round.itemId);
                if (it != g_toolNames.end()) itemName = it->second;
            }
            if (itemName.empty()) itemName = L"道具" + std::to_wstring(round.itemId);
            roundPrompt = entityLabelBySid(next, round.atkId) + L"使用" + itemName +
                          L"作用于" + entityLabelBySid(next, round.defId);
            if (round.hasCatchData) roundPrompt += L"，捕捉数据已返回";
        } else if (params == 3) {
            roundPrompt = entityLabelBySid(next, round.atkId) + L"的战斗操作状态为" +
                          (round.avail != 0 ? L"可用" : L"不可用");
        } else if (params == 5) {
            roundPrompt = round.status == 1 ? L"该场战斗不能使用培元金丹" :
                          L"你已使用6次药剂，无法再使用";
        }

        next.lastCmdType = params;
        next.lastRound = round;
        SyncBattleSixAutoBattleState(next, false);
        ReplaceCurrentBattle(std::move(next));
        UpdateUIBattleData();

        std::wstring event = L"{\"cmdType\":" + std::to_wstring(params);
        if (params == 0) {
            event += L",\"haveBattle\":" + std::to_wstring(round.haveBattle) +
                     L",\"atkId\":" + std::to_wstring(round.atkId) +
                     L",\"skillId\":" + std::to_wstring(round.skillId) +
                     L",\"defId\":" + std::to_wstring(round.defId) +
                     L",\"miss\":" + std::to_wstring(round.miss) +
                     L",\"atkHp\":" + std::to_wstring(round.atkHp) +
                     L",\"defHp\":" + std::to_wstring(round.defHp);
        } else if (params == 1) {
            event += L",\"sid\":" + std::to_wstring(round.sid) +
                     L",\"uniqueId\":" + std::to_wstring(round.uniqueId) +
                     L",\"sta\":" + std::to_wstring(round.sta);
        } else if (params == 2) {
            event += L",\"atkId\":" + std::to_wstring(round.atkId) +
                     L",\"itemId\":" + std::to_wstring(round.itemId) +
                     L",\"defId\":" + std::to_wstring(round.defId) +
                     L",\"param0\":" + std::to_wstring(round.param0) +
                     L",\"param1\":" + std::to_wstring(round.param1);
            if (round.hasCatchData) {
                const BattleCatchData& catchData = round.catchData;
                event += L",\"catchData\":{\"spiritId\":" + std::to_wstring(catchData.spiritId) +
                         L",\"spiritType\":" + std::to_wstring(catchData.spiritType) +
                         L",\"spiritHp\":" + std::to_wstring(catchData.spiritHp) +
                         L",\"spiritAtk\":" + std::to_wstring(catchData.spiritAtk) +
                         L",\"spiritDef\":" + std::to_wstring(catchData.spiritDef) +
                         L",\"spiritMagAtk\":" + std::to_wstring(catchData.spiritMagAtk) +
                         L",\"spiritMagDef\":" + std::to_wstring(catchData.spiritMagDef) +
                         L",\"spiritSpeed\":" + std::to_wstring(catchData.spiritSpeed) +
                         L",\"spiritLevel\":" + std::to_wstring(catchData.spiritLevel) +
                         L",\"spiritSid\":" + std::to_wstring(catchData.spiritSid) +
                         L",\"spiritPackId\":" + std::to_wstring(catchData.spiritPackId) +
                         L",\"spiritSex\":" + std::to_wstring(catchData.spiritSex) +
                         L",\"attackGeniusValue\":" + std::to_wstring(catchData.attackGeniusValue) +
                         L",\"defenceGeniusValue\":" + std::to_wstring(catchData.defenceGeniusValue) +
                         L",\"magicGeniusValue\":" + std::to_wstring(catchData.magicGeniusValue) +
                         L",\"resistanceGeniusValue\":" + std::to_wstring(catchData.resistanceGeniusValue) +
                         L",\"hpGeniusValue\":" + std::to_wstring(catchData.hpGeniusValue) +
                         L",\"speedGeniusValue\":" + std::to_wstring(catchData.speedGeniusValue) + L"}";
            }
        } else if (params == 3) {
            event += L",\"atkId\":" + std::to_wstring(round.atkId) +
                     L",\"avail\":" + std::to_wstring(round.avail);
        } else if (params == 5) {
            event += L",\"status\":" + std::to_wstring(round.status);
        }
        if (!round.bufs.empty()) {
            event += L",\"bufs\":[";
            for (size_t i = 0; i < round.bufs.size(); ++i) {
                if (i) event += L",";
                const auto& buf = round.bufs[i];
                event += L"{\"bufId\":" + std::to_wstring(buf.bufId) +
                         L",\"addOrRemove\":" + std::to_wstring(buf.addOrRemove) +
                         L",\"atkId\":" + std::to_wstring(buf.atkId) +
                         L",\"defId\":" + std::to_wstring(buf.defId) +
                         L",\"round\":" + std::to_wstring(buf.round) +
                         L",\"param1\":" + std::to_wstring(buf.param1) +
                         L",\"param2\":" + std::to_wstring(buf.param2) +
                         L",\"param3\":" + std::to_wstring(buf.param3) +
                         L",\"param4\":" + std::to_wstring(buf.param4) + L"}";
            }
            event += L"]";
        }
        if (!round.appends.empty()) {
            event += L",\"appends\":[";
            for (size_t i = 0; i < round.appends.size(); ++i) {
                if (i) event += L",";
                const auto& append = round.appends[i];
                event += L"{\"type\":" + std::to_wstring(append.type) +
                         L",\"paramKey\":" + std::to_wstring(append.paramKey) +
                         L",\"paramValue\":" + std::to_wstring(append.paramValue) +
                         L",\"skillId\":" + std::to_wstring(append.skillId) + L"}";
            }
            event += L"]";
        }
        event += L"}";
        const wchar_t* eventType = params == 1 ? L"切换宠物" :
                                    (params == 2 ? L"使用道具" : (params == 5 ? L"战斗限制" : L"回合"));
        sendJson(eventType, event);
        sendPrompt(eventType, roundPrompt);
        return;
    }

    if (packet.opcode == OPCODE_BATTLE_END) {
        BoundedReader reader(data, size);
        std::vector<BattleEndResult> results;
        if (params == 1) {
            int32_t count = 0;
            if (!reader.ReadI32(count) || count < 0 || count > 1024) return;
            results.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; ++i) {
                BattleEndResult result;
                if (!reader.ReadI32(result.sid) || !reader.ReadI32(result.result)) return;
                results.push_back(result);
            }
        }

        flushPendingTip();
        std::wstring event = L"{\"param\":" + std::to_wstring(params) + L",\"results\":[";
        for (size_t i = 0; i < results.size(); ++i) {
            if (i) event += L",";
            event += L"{\"sid\":" + std::to_wstring(results[i].sid) +
                     L",\"res\":" + std::to_wstring(results[i].result) + L"}";
        }
        event += L"]}";
        sendJson(L"战斗结束", event);
        sendPrompt(L"战斗结束", results.empty() ? L"双方战斗结束" :
                   L"双方战斗结束，服务端返回" + std::to_wstring(results.size()) + L"条结算记录");

        // AS3 keeps BattleData until clearBattleView(); only the session flag
        // is cleared here so later NEWEXP/FSPK can still attach to the snapshot.
        BattleData next = GetCurrentBattleSnapshot();
        next.active = false;
        next.endParam = params;
        next.endResults = std::move(results);
        ReplaceCurrentBattle(std::move(next));
        g_battleStarted = false;
        UpdateUIBattleData();
        return;
    }


    if (packet.opcode == Opcode::BATTLE_FSPK) {
        BoundedReader reader(data, size);
        BattleFspkData fspk;
        fspk.params = params;
        if (!reader.ReadI32(fspk.win)) return;
        fspk.hasReward = fspk.win >= 0;
        if (fspk.hasReward && (!reader.ReadI32(fspk.xianli) || !reader.ReadI32(fspk.zhangong))) return;
        BattleData next = GetCurrentBattleSnapshot();
        next.hasFspk = true;
        next.fspk = fspk;
        ReplaceCurrentBattle(std::move(next));
        sendJson(L"战斗结算", L"{\"params\":" + std::to_wstring(params) +
                 L",\"win\":" + std::to_wstring(fspk.win) +
                 L",\"xianli\":" + std::to_wstring(fspk.xianli) +
                 L",\"zhangong\":" + std::to_wstring(fspk.zhangong) + L"}");
        std::wstring fspkPrompt = L"战斗结算：胜负标记" + std::to_wstring(fspk.win);
        if (fspk.hasReward) {
            fspkPrompt += L"，仙力" + std::to_wstring(fspk.xianli) +
                          L"，战功" + std::to_wstring(fspk.zhangong);
        }
        sendPrompt(L"战斗结算", fspkPrompt);
        return;
    }

    if (packet.opcode == Opcode::COMBAT_SITE_OPTION) {
        BattleSiteData site;
        site.params = params;
        BoundedReader reader(data, size);
        if (params == BufDataType::COMBAT_SITE_ADD) {
            if (!reader.ReadI32(site.sn) || !reader.ReadI32(site.id) || !reader.ReadI32(site.round)) return;
        } else if (params == BufDataType::COMBAT_SITE_MD) {
            if (!reader.ReadI32(site.id) || !reader.ReadI32(site.round)) return;
        } else if (params == BufDataType::COMBAT_SITE_DEL) {
            if (!reader.ReadI32(site.id)) return;
        }
        BattleData next = GetCurrentBattleSnapshot();
        next.hasSite = true;
        next.site = site;
        ReplaceCurrentBattle(std::move(next));
        sendJson(L"战斗场地", L"{\"params\":" + std::to_wstring(params) +
                 L",\"sn\":" + std::to_wstring(site.sn) +
                 L",\"id\":" + std::to_wstring(site.id) +
                 L",\"round\":" + std::to_wstring(site.round) + L"}");
        sendPrompt(L"战斗场地", L"战斗场地状态已更新");
        return;
    }

    if (packet.opcode == Opcode::COMBAT_SITE_EFFECT) {
        if (params != 1) return;
        BoundedReader reader(data, size);
        BattleSiteData site;
        site.params = params;
        BufData buf;
        buf.addOrRemove = BufDataType::BUF_TYPE_2;
        buf.bufId = BufDataType::SITE_RECOVER_ID;
        buf.param2 = 0;
        if (!reader.ReadI32(site.siteId) || !reader.ReadI32(buf.defId) || !reader.ReadI32(buf.param1)) return;
        buf.name = GetBufName(buf.bufId, buf.param1);
        buf.tipString = GetBufTipString(buf.bufId, buf.param1, buf.param2);
        BattleData next = GetCurrentBattleSnapshot();
        const std::wstring targetName = entityLabelBySid(next, buf.defId);
        ApplyBattleBuffBloodEffect(next, buf);
        SyncBattleSixAutoBattleState(next, false);
        // Site effects are transient BATTLE_BUF events in AS3; do not persist
        // the recovery marker as a normal Buff row.
        next.hasSite = true;
        next.site = site;
        ReplaceCurrentBattle(std::move(next));

        UpdateUIBattleData();
        sendJson(L"战斗场地", L"{\"params\":1,\"siteId\":" + std::to_wstring(site.siteId) +
                 L",\"defId\":" + std::to_wstring(buf.defId) +
                 L",\"param1\":" + std::to_wstring(buf.param1) + L"}");
        sendPrompt(L"战斗场地", targetName + L"受到场地效果：" + buf.name);
        return;
    }

    if (packet.opcode == Opcode::COMBAT_NET_PROBE_REQ) {
        BoundedReader reader(data, size);
        int32_t remote = 0, world = 0, gateway = 0;
        if (!reader.ReadI32(remote) || !reader.ReadI32(world) || !reader.ReadI32(gateway)) return;
        sendJson(L"战斗网络", L"{\"remote\":" + std::to_wstring(remote) +
                 L",\"world\":" + std::to_wstring(world) +
                 L",\"gateway\":" + std::to_wstring(gateway) + L"}");
        return;
    }

    if (packet.opcode == Opcode::COMBAT_NET_REPORT) {
        BoundedReader reader(data, size);
        int32_t report = 0;
        if (!reader.ReadI32(report)) return;
        sendJson(L"战斗网络", L"{\"report\":" + std::to_wstring(report) + L"}");
        return;
    }

    if (packet.opcode == Opcode::BATTLE_WITH || packet.opcode == Opcode::BATTLE_REQ_ON_SKILL) {
        sendJson(L"战斗关联", L"{\"opcode\":" + std::to_wstring(packet.opcode) +
                 L",\"params\":" + std::to_wstring(params) +
                 L",\"bodyBytes\":" + std::to_wstring(size) + L"}");
        return;
    }

    if (packet.opcode == Opcode::SPIRIT_LEVEL_UP) {
        BoundedReader reader(data, size);
        std::vector<int32_t> values;
        values.reserve(17);
        for (int i = 0; i < 17; ++i) {
            int32_t value = 0;
            if (!reader.ReadI32(value)) return;
            values.push_back(value);
        }
        std::vector<std::pair<int32_t, int32_t>> skills;
        int32_t skillId = 0;
        if (!reader.ReadI32(skillId)) return;
        while (skillId > 0) {
            if (skills.size() >= 128) return;
            int32_t time = 0;
            if (!reader.ReadI32(time)) return;
            skills.emplace_back(skillId, time);
            if (!reader.ReadI32(skillId)) return;
        }
        if (!skills.empty()) {
            int32_t key = 0, count = 0;
            if (!reader.ReadI32(key) || !reader.ReadI32(count) || count < 0 || count > 128) return;
            for (int32_t i = 0; i < count; ++i) {
                int32_t id = 0, time = 0;
                if (!reader.ReadI32(id) || !reader.ReadI32(time)) return;
            }
            int32_t mod = 0, attr = 0;
            if (!reader.ReadI32(mod) || !reader.ReadI32(attr)) return;
        }
        sendJson(L"战斗升级", L"{\"uniqueId\":" + std::to_wstring(values[0]) +
                 L",\"spiritId\":" + std::to_wstring(values[1]) +
                 L",\"level\":" + std::to_wstring(values[10]) +
                 L",\"maxHp\":" + std::to_wstring(values[11]) +
                 L",\"skillCount\":" + std::to_wstring(skills.size()) + L"}");
        return;
    }

    if (packet.opcode == Opcode::BATTLE_NEWEXP) {
        BoundedReader reader(data, size);
        uint32_t win = 0;
        if (!reader.ReadU32(win)) return;
        if (g_battleSixAuto.IsInBattle()) {
            g_battleSixSettlementKnown = true;
            g_battleSixSettlementWin = (win & 0x80000000u) != 0;
            g_battleSixSettlementFlags = win;
        }
        int goodsCount = 0;
        int learnCount = 0;
        int expCount = 0;

        int32_t goodId = 0;
        if (!reader.ReadI32(goodId)) return;
        while (goodId != 0) {
            if (++goodsCount > 1024) return;
            int32_t amount = 0;
            if (!reader.ReadI32(amount) || !reader.ReadI32(goodId)) return;
        }

        int32_t spiritId = 0;
        if (!reader.ReadI32(spiritId)) return;
        while (spiritId != 0) {
            if (++learnCount > 1024) return;
            for (int i = 0; i < 7; ++i) {
                int32_t value = 0;
                if (!reader.ReadI32(value)) return;
            }
            if (!reader.ReadI32(spiritId)) return;
        }

        int32_t expType = 0;
        if (!reader.ReadI32(expType)) return;
        while (expType != 0) {
            if (++expCount > 1024) return;
            if (expType == 1) {
                for (int i = 0; i < 8; ++i) {
                    int32_t value = 0;
                    if (!reader.ReadI32(value)) return;
                }
            } else if (expType == 2) {
                for (int i = 0; i < 18; ++i) {
                    int32_t value = 0;
                    if (!reader.ReadI32(value)) return;
                }
                int32_t skillFlag = 0;
                if (!reader.ReadI32(skillFlag)) return;
                int skillCount = 0;
                while (skillFlag > 0) {
                    if (++skillCount > 128) return;
                    int32_t time = 0;
                    if (!reader.ReadI32(time) || !reader.ReadI32(skillFlag)) return;
                }
                if (skillCount > 0) {
                    int32_t key = 0, count = 0;
                    if (!reader.ReadI32(key) || !reader.ReadI32(count) || count < 0 || count > 128) return;
                    for (int32_t i = 0; i < count; ++i) {
                        int32_t id = 0, time = 0;
                        if (!reader.ReadI32(id) || !reader.ReadI32(time)) return;
                    }
                    int32_t mod = 0, attr = 0;
                    if (!reader.ReadI32(mod) || !reader.ReadI32(attr)) return;
                }
            } else {
                return;
            }
            if (!reader.ReadI32(expType)) return;
        }
        int32_t saveExp = 0;
        if (!reader.ReadI32(saveExp)) return;
        sendJson(L"战斗经验", L"{\"type\":" + std::to_wstring(params) +
                 L",\"win\":" + std::to_wstring(win) +
                 L",\"goods\":" + std::to_wstring(goodsCount) +
                 L",\"learnPowers\":" + std::to_wstring(learnCount) +
                 L",\"spiritExp\":" + std::to_wstring(expCount) +
                 L",\"saveExp\":" + std::to_wstring(saveExp) + L"}");
    }
}

void PacketParser::ProcessBattlePacket(const GamePacket& packet) {
    ProcessBattlePacketSafe(packet);
}

void PacketParser::ProcessMonsterPacket(const GamePacket& packet) {
    if (packet.bSend || packet.opcode != OPCODE_MONSTER_LIST) return;

    size_t offset = 0;
    const uint8_t* data = packet.body.data();
    size_t size = packet.body.size();

    if (offset + 8 > size) return;
    
    int32_t sn = ReadInt32LE(data, offset);
    int32_t mcount = ReadInt32LE(data, offset);

    std::vector<MonsterItem> monsters;

    // 性格值对应属性映射
    auto getGeniusName = [](int32_t geniusValue) -> std::pair<std::wstring, int32_t> {
        // 性格值范围：-2到+2，对应属性加成
        static const wchar_t* const attrNames[] = {
            L"攻击", L"防御", L"法术", L"抗性", L"体力", L"速度"
        };
        // 简化：根据geniusValue判断主要加成属性
        int32_t idx = (geniusValue / 10) % 6;
        if (idx < 0) idx = -idx;
        int32_t bonus = (geniusValue % 10) * 5;  // 近似值
        return {attrNames[idx % 6], bonus};
    };

    for (int j = 0; j < mcount && offset + 4 <= size; j++) {
        MonsterItem monster;
        
        monster.id = ReadInt32LE(data, offset);
        if (monster.id == 0) break;
        
        if (offset + 8 > size) break;
        monster.type_id = ReadInt32LE(data, offset);
        monster.iid = ReadInt32LE(data, offset);
        
        // 从名称表获取妖怪名称
        {
            std::lock_guard<std::mutex> lock(g_dataMapsMutex);
            auto it = g_petNames.find(monster.iid);
            monster.name = (it != g_petNames.end()) ? it->second : L"未知妖怪";
        }
        
        // 从映射表获取系别（AS3中系别来自monsterIntro.elem，即sprite.xml）
        {
            std::lock_guard<std::mutex> lock(g_dataMapsMutex);
            auto elemIt = g_petElems.find(monster.iid);
            if (elemIt != g_petElems.end()) {
                monster.type = elemIt->second;
                auto nameIt = g_elemNames.find(monster.type);
                monster.typeName = (nameIt != g_elemNames.end()) ? nameIt->second : L"未知";
            } else {
                monster.type = 0;
                monster.typeName = L"未知";
            }
        }
        
        if (offset + 72 > size) break;
        monster.isfirst = ReadInt32LE(data, offset);
        monster.level = ReadInt32LE(data, offset);
        monster.exp = ReadInt32LE(data, offset);
        ReadInt32LE(data, offset);  // type字段（封包中的type，忽略，使用sprite.xml中的elem）
        monster.forbitItem = ReadInt32LE(data, offset);
        monster.attack = ReadInt32LE(data, offset);
        monster.defence = ReadInt32LE(data, offset);
        monster.magic = ReadInt32LE(data, offset);
        monster.resistance = ReadInt32LE(data, offset);
        monster.strength = ReadInt32LE(data, offset);
        monster.hp = ReadInt32LE(data, offset);
        monster.speed = ReadInt32LE(data, offset);
        monster.mold = ReadInt32LE(data, offset);
        monster.state = ReadInt32LE(data, offset);
        monster.needExp = ReadInt32LE(data, offset);
        monster.timetxt = ReadInt32LE(data, offset);
        monster.sex = ReadInt32LE(data, offset);
        
        // 学习力
        if (offset + 24 > size) break;
        ReadInt32LE(data, offset); // attackLearnValue
        ReadInt32LE(data, offset); // defenceLearnValue
        ReadInt32LE(data, offset); // magicLearnValue
        ReadInt32LE(data, offset); // resistanceLearnValue
        ReadInt32LE(data, offset); // hpLearnValue
        ReadInt32LE(data, offset); // speedLearnVale
        
        // 资质原始值（0-31范围）
        if (offset + 24 > size) break;
        int32_t attackGeniusValue = ReadInt32LE(data, offset);
        int32_t defenceGeniusValue = ReadInt32LE(data, offset);
        int32_t magicGeniusValue = ReadInt32LE(data, offset);
        int32_t resistanceGeniusValue = ReadInt32LE(data, offset);
        int32_t hpGeniusValue = ReadInt32LE(data, offset);
        int32_t speedGeniusValue = ReadInt32LE(data, offset);
        
        // 资质等级计算函数（根据SpiritGenius.cheakGenius）
        auto checkGenius = [](int32_t value) -> int {
            if (value >= 0 && value <= 5) return 2;
            if (value >= 6 && value <= 11) return 3;
            if (value >= 12 && value <= 17) return 4;
            if (value >= 18 && value <= 26) return 5;
            if (value >= 27 && value <= 31) return 6;
            return 1;  // 默认/异常值
        };
        
        // 计算各项资质等级
        int attackGenius = checkGenius(attackGeniusValue);
        int defenceGenius = checkGenius(defenceGeniusValue);
        int magicGenius = checkGenius(magicGeniusValue);
        int resistanceGenius = checkGenius(resistanceGeniusValue);
        int hpGenius = checkGenius(hpGeniusValue);
        int speedGenius = checkGenius(speedGeniusValue);
        
        // 总资质计算（根据SpiritGenius.countGeniusType）
        int totalScore = attackGenius + defenceGenius + magicGenius + resistanceGenius + hpGenius + speedGenius - 6;
        monster.geniusType = 0;
        if (totalScore >= 5 && totalScore <= 9) monster.geniusType = 1;
        else if (totalScore >= 10 && totalScore <= 14) monster.geniusType = 2;
        else if (totalScore >= 15 && totalScore <= 19) monster.geniusType = 3;
        else if (totalScore >= 20 && totalScore <= 24) monster.geniusType = 4;
        else if (totalScore >= 25 && totalScore <= 29) monster.geniusType = 5;
        else if (totalScore == 30) monster.geniusType = 6;
        
        // 获取资质名称
        {
            std::lock_guard<std::mutex> lock(g_dataMapsMutex);
            auto it = g_aptitudeNames.find(monster.geniusType);
            if (it != g_aptitudeNames.end()) {
                monster.aptitudeName = it->second;
            } else {
                monster.aptitudeName = L"未知资质";
            }
        }
        
        // 从mold获取性格名称（mold是性格索引，从g_geniusNames映射）
        {
            std::lock_guard<std::mutex> lock(g_dataMapsMutex);
            auto it = g_geniusNames.find(monster.mold);
            if (it != g_geniusNames.end()) {
                monster.geniusName = it->second;
            } else {
                monster.geniusName = L"未知";
            }
        }
        
        // 添加资质列表用于UI显示（包含原始值和等级）
        auto addGenius = [&](int32_t val, int32_t level, const std::wstring& name) {
            MonsterGenius g;
            g.name = name;
            g.value = val;
            g.level = level;
            monster.geniusList.push_back(g);
        };
        addGenius(attackGeniusValue, attackGenius, L"攻击");
        addGenius(defenceGeniusValue, defenceGenius, L"防御");
        addGenius(magicGeniusValue, magicGenius, L"法术");
        addGenius(resistanceGeniusValue, resistanceGenius, L"抗性");
        addGenius(hpGeniusValue, hpGenius, L"体力");
        addGenius(speedGeniusValue, speedGenius, L"速度");
        
        // aptitude 与 geniusType 保持一致（总资质等级）
        monster.aptitude = monster.geniusType;
        
        // 绝学
        if (offset + 12 > size) break;
        ReadInt32LE(data, offset); // peerlessId
        ReadInt32LE(data, offset); // peerlessStatus
        ReadInt32LE(data, offset); // tempPeerlessNum
        
        // 技能数量
        if (offset + 4 > size) break;
        int32_t skillcount = ReadInt32LE(data, offset);
        
        // 读取技能列表
        for (int i = 0; i < skillcount && offset + 12 <= size; i++) {
            MonsterSkill skill;
            skill.id = ReadInt32LE(data, offset);
            int32_t skillPp = ReadInt32LE(data, offset);
            skill.maxPp = ReadInt32LE(data, offset);
            skill.pp = skillPp;
            {
                std::lock_guard<std::mutex> lock(g_dataMapsMutex);
                auto it = g_skillNames.find(skill.id);
                skill.name = (it != g_skillNames.end()) ? it->second : L"";
            }
            monster.skills.push_back(skill);
        }
        
        // 读取额外技能列表（以0结尾）
        int32_t tempskillid = 1;
        while (tempskillid != 0 && offset + 4 <= size) {
            tempskillid = ReadInt32LE(data, offset);
            // 跳过，不处理额外技能
        }
        
        // 不可用技能数量
        if (offset + 4 > size) break;
        int32_t unableSkillCount = ReadInt32LE(data, offset);
        // 跳过不可用技能
        
        // 灵玉数量
        if (offset + 4 > size) break;
        int32_t symmLength = ReadInt32LE(data, offset);
        
        // 读取灵玉列表
        for (int z = 0; z < symmLength && offset + 12 <= size; z++) {
            MonsterSymm symm;
            symm.place = ReadInt32LE(data, offset);
            symm.id = ReadInt32LE(data, offset);
            symm.index = ReadInt32LE(data, offset);
            symm.name = L"";  // 灵玉名称需要另外查询
            monster.symmList.push_back(symm);
        }
        
        // 读取额外技能列表（以-1或0结尾）
        if (offset + 4 <= size) {
            tempskillid = ReadInt32LE(data, offset);
            while (tempskillid != -1 && tempskillid != 0 && offset + 4 <= size) {
                tempskillid = ReadInt32LE(data, offset);
            }
        }
        
        // 读取另一组额外技能列表（以-1或0结尾）
        if (offset + 4 <= size) {
            tempskillid = ReadInt32LE(data, offset);
            while (tempskillid != -1 && tempskillid != 0 && offset + 4 <= size) {
                tempskillid = ReadInt32LE(data, offset);
            }
        }
        
        monsters.push_back(monster);
    }

    // 保存妖怪数据到全局变量（用于副本跳层等功能）
    g_monsterData.sn = sn;
    g_monsterData.count = mcount;
    g_monsterData.monsters = monsters;

    // 通知副本跳层模块妖怪数据已收到
    extern struct DungeonJumpState g_dungeonJumpState;
    g_dungeonJumpState.monsterDataReceived = true;

    // 处理双台谷异步查询
    extern bool g_shuangtaiWaitingForMonsterData;
    extern void UpdateShuangTaiUIFromMonsterData();
    if (g_shuangtaiWaitingForMonsterData) {
        g_shuangtaiWaitingForMonsterData = false;
        UpdateShuangTaiUIFromMonsterData();
    }

    // 构建JSON并发送到UI
    std::wstring jsData = L"{\"sn\":" + std::to_wstring(sn) + L",\"count\":" + std::to_wstring(mcount) + L",\"monsters\":[";
    
    for (size_t i = 0; i < monsters.size(); i++) {
        const auto& m = monsters[i];
        jsData += L"{\"id\":" + std::to_wstring(m.id) +
                  L",\"iid\":" + std::to_wstring(m.iid) +
                  L",\"name\":\"" + m.name + L"\"" +
                  L",\"isfirst\":" + std::to_wstring(m.isfirst) +
                  L",\"level\":" + std::to_wstring(m.level) +
                  L",\"exp\":" + std::to_wstring(m.exp) +
                  L",\"needExp\":" + std::to_wstring(m.needExp) +
                  L",\"type\":" + std::to_wstring(m.type) +
                  L",\"typeName\":\"" + m.typeName + L"\"" +
                  L",\"attack\":" + std::to_wstring(m.attack) +
                  L",\"defence\":" + std::to_wstring(m.defence) +
                  L",\"magic\":" + std::to_wstring(m.magic) +
                  L",\"resistance\":" + std::to_wstring(m.resistance) +
                  L",\"hp\":" + std::to_wstring(m.hp) +
                  L",\"speed\":" + std::to_wstring(m.speed) +
                  L",\"mold\":" + std::to_wstring(m.mold) +
                  L",\"sex\":" + std::to_wstring(m.sex) +
                  L",\"geniusType\":" + std::to_wstring(m.geniusType) +
                  L",\"geniusName\":\"" + m.geniusName + L"\"" +
                  L",\"aptitude\":" + std::to_wstring(m.aptitude) +
                  L",\"aptitudeName\":\"" + m.aptitudeName + L"\"" +
                  L",\"geniusList\":[";
        
        for (size_t g = 0; g < m.geniusList.size(); g++) {
            jsData += L"{\"name\":\"" + m.geniusList[g].name + L"\",\"value\":" + std::to_wstring(m.geniusList[g].value) + L",\"level\":" + std::to_wstring(m.geniusList[g].level) + L"}";
            if (g < m.geniusList.size() - 1) jsData += L",";
        }
        
        jsData += L"],\"skills\":[";
        for (size_t s = 0; s < m.skills.size(); s++) {
            jsData += L"{\"id\":" + std::to_wstring(m.skills[s].id) +
                      L",\"name\":\"" + m.skills[s].name + L"\"" +
                      L",\"pp\":" + std::to_wstring(m.skills[s].pp) +
                      L",\"maxPp\":" + std::to_wstring(m.skills[s].maxPp) + L"}";
            if (s < m.skills.size() - 1) jsData += L",";
        }
        
        jsData += L"],\"symmList\":[";
        for (size_t z = 0; z < m.symmList.size(); z++) {
            jsData += L"{\"place\":" + std::to_wstring(m.symmList[z].place) +
                      L",\"id\":" + std::to_wstring(m.symmList[z].id) +
                      L",\"index\":" + std::to_wstring(m.symmList[z].index) + L"}";
            if (z < m.symmList.size() - 1) jsData += L",";
        }
        
        jsData += L"]}";
        if (i < monsters.size() - 1) jsData += L",";
    }
    
    jsData += L"]}";
    
    std::wstring jsCode = L"if(window.updateMonsterUI) { window.updateMonsterUI(" + jsData + L"); }";
    UIBridge::Instance().ExecuteJS(jsCode);
}

// ============================================================================
// zlib 函数导出实现
// ============================================================================

ZlibUncompressFunc GetZlibUncompress() {
    return g_uncompress;
}

ZlibCompressFunc GetZlibCompress() {
    return g_compress;
}

ZlibCompress2Func GetZlibCompress2() {
    return g_compress2;
}

ZlibInflateInitFunc GetZlibInflateInit() {
    return g_inflateInit;
}

ZlibInflateFunc GetZlibInflate() {
    return g_inflate;
}

ZlibInflateEndFunc GetZlibInflateEnd() {
    return g_inflateEnd;
}

ZlibDeflateInitFunc GetZlibDeflateInit() {
    return g_deflateInit;
}

ZlibDeflateFunc GetZlibDeflate() {
    return g_deflate;
}

ZlibDeflateEndFunc GetZlibDeflateEnd() {
    return g_deflateEnd;
}
