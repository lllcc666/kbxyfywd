#include "packet_parser.h"
#include <MemoryModule.h>
#include "embedded/zlib_data.h"
#include "battle_six.h"
#include "dungeon_jump.h"
#include "wpe_hook.h"
#include "ui_bridge.h"
#include "utils.h"
#include <wininet.h>
#include <cstdlib>
#include <mutex>
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
std::vector<uint8_t> PacketParser::g_recvBuffer;

// 全局数据映射表的互斥锁
static std::mutex g_dataMapsMutex;
std::unordered_map<int, std::wstring> g_petNames;
std::unordered_map<int, std::wstring> g_skillNames;
std::unordered_map<int, int> g_skillPowers;  // 技能ID -> 威力值
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
        searchPos = nextPos;
    }

    std::lock_guard<std::mutex> lock(g_dataMapsMutex);
    for (const auto& [skillId, skillName] : skillNames) {
        g_skillNames[skillId] = skillName;
    }
    for (const auto& [skillId, power] : skillPowers) {
        g_skillPowers[skillId] = power;
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

bool PacketParser::ParsePackets(const uint8_t* data, size_t size, BOOL bSend, std::vector<GamePacket>& outPackets) {
    if (bSend) {
        if (size < PacketProtocol::HEADER_SIZE) {
            return false;
        }

        size_t hOffset = 0;
        const uint16_t magic = ReadInt16LE(data, hOffset);
        const uint16_t len = ReadInt16LE(data, hOffset);
        if (magic != MAGIC_NUMBER_D && magic != MAGIC_NUMBER_C) {
            return false;
        }
        if (size < PacketProtocol::HEADER_SIZE + len) {
            return false;
        }

        GamePacket packet;
        packet.magic = magic;
        packet.length = len;
        packet.bSend = bSend;
        packet.opcode = ReadInt32LE(data, hOffset);
        packet.params = ReadInt32LE(data, hOffset);
        packet.body.assign(data + hOffset, data + hOffset + len);
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

    if (size == 0) {
        return false;
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

        const uint16_t len = static_cast<uint16_t>(g_recvBuffer[readOffset + 2]) |
                             (static_cast<uint16_t>(g_recvBuffer[readOffset + 3]) << 8);
        const size_t packetSize = PacketProtocol::HEADER_SIZE + len;
        if (readOffset + packetSize > dataSize) {
            break;
        }

        GamePacket packet;
        packet.magic = magic;
        packet.length = len;
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
        const size_t bodyEnd = bodyStart + len;
        packet.rawBody.assign(g_recvBuffer.begin() + bodyStart, g_recvBuffer.begin() + bodyEnd);
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
        g_hWnd = FindWindowW(L"WebView2DemoWindowClass", L"卡布西游浮影微端 V1.09");
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
        g_hWnd = FindWindowW(L"WebView2DemoWindowClass", L"卡布西游浮影微端 V1.09");
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

void PacketParser::UpdateUIBattleData() {
    if (!g_hWnd) {
        // 使用正确的窗口类名
        g_hWnd = FindWindowW(L"WebView2DemoWindowClass", nullptr);
        if (!g_hWnd) {
            // 备用方案：通过窗口标题查找
            g_hWnd = FindWindowW(nullptr, L"卡布西游浮影微端 V1.09");
            if (!g_hWnd) {
                return;
            }
        }
    }

    // Convert BattleData to JSON string
    std::wstring jsData = L"{";
    
    // My Pets
    jsData += L"\"myPets\":[";
    for (size_t i = 0; i < g_currentBattle.myPets.size(); ++i) {
        const auto& p = g_currentBattle.myPets[i];
        jsData += L"{\"spiritId\":" + std::to_wstring(p.spiritId) + 
                  L",\"name\":\"" + (p.name.empty() ? L"" : p.name) + L"\"" +
                  L",\"sid\":" + std::to_wstring(p.sid) +
                  L",\"uniqueId\":" + std::to_wstring(p.uniqueId) +
                  L",\"userId\":" + std::to_wstring(p.userId) +
                  L",\"groupType\":" + std::to_wstring(p.groupType) +
                  L",\"state\":" + std::to_wstring(p.state) +
                  L",\"hp\":" + std::to_wstring(p.hp) + 
                  L",\"maxHp\":" + std::to_wstring(p.maxHp) + 
                  L",\"level\":" + std::to_wstring(p.level) + 
                  L",\"skills\":[";
        for (size_t j = 0; j < p.skills.size(); ++j) {
            jsData += L"{\"id\":" + std::to_wstring(p.skills[j].id) + 
                      L",\"name\":\"" + (p.skills[j].name.empty() ? L"" : p.skills[j].name) + L"\"" +
                      L",\"pp\":" + std::to_wstring(p.skills[j].pp) + 
                      L",\"maxPp\":" + std::to_wstring(p.skills[j].maxPp) + L"}";
            if (j < p.skills.size() - 1) jsData += L",";
        }
        // 添加 Buff 列表
        jsData += L"],\"bufArr\":[";
        for (size_t j = 0; j < p.bufArr.size(); ++j) {
            const auto& buf = p.bufArr[j];
            jsData += L"{\"bufId\":" + std::to_wstring(buf.bufId) +
                      L",\"name\":\"" + (buf.name.empty() ? L"" : buf.name) + L"\"" +
                      L",\"round\":" + std::to_wstring(buf.round) +
                      L",\"param1\":" + std::to_wstring(buf.param1) +
                      L",\"param2\":" + std::to_wstring(buf.param2) +
                      L",\"addOrRemove\":" + std::to_wstring(buf.addOrRemove) + L"}";
            if (j < p.bufArr.size() - 1) jsData += L",";
        }
        jsData += L"]}";
        if (i < g_currentBattle.myPets.size() - 1) jsData += L",";
    }
    jsData += L"],";

    // Other Pets
    jsData += L"\"otherPets\":[";
    for (size_t i = 0; i < g_currentBattle.otherPets.size(); ++i) {
        const auto& p = g_currentBattle.otherPets[i];
        jsData += L"{\"spiritId\":" + std::to_wstring(p.spiritId) + 
                  L",\"name\":\"" + (p.name.empty() ? L"" : p.name) + L"\"" +
                  L",\"sid\":" + std::to_wstring(p.sid) +
                  L",\"uniqueId\":" + std::to_wstring(p.uniqueId) +
                  L",\"userId\":" + std::to_wstring(p.userId) +
                  L",\"groupType\":" + std::to_wstring(p.groupType) +
                  L",\"state\":" + std::to_wstring(p.state) +
                  L",\"hp\":" + std::to_wstring(p.hp) + 
                  L",\"maxHp\":" + std::to_wstring(p.maxHp) + 
                  L",\"level\":" + std::to_wstring(p.level) + 
                  L",\"skills\":[";
        for (size_t j = 0; j < p.skills.size(); ++j) {
            jsData += L"{\"id\":" + std::to_wstring(p.skills[j].id) + 
                      L",\"name\":\"" + (p.skills[j].name.empty() ? L"" : p.skills[j].name) + L"\"" +
                      L",\"pp\":" + std::to_wstring(p.skills[j].pp) + 
                      L",\"maxPp\":" + std::to_wstring(p.skills[j].maxPp) + L"}";
            if (j < p.skills.size() - 1) jsData += L",";
        }
        // 添加 Buff 列表
        jsData += L"],\"bufArr\":[";
        for (size_t j = 0; j < p.bufArr.size(); ++j) {
            const auto& buf = p.bufArr[j];
            jsData += L"{\"bufId\":" + std::to_wstring(buf.bufId) +
                      L",\"name\":\"" + (buf.name.empty() ? L"" : buf.name) + L"\"" +
                      L",\"round\":" + std::to_wstring(buf.round) +
                      L",\"param1\":" + std::to_wstring(buf.param1) +
                      L",\"param2\":" + std::to_wstring(buf.param2) +
                      L",\"addOrRemove\":" + std::to_wstring(buf.addOrRemove) + L"}";
            if (j < p.bufArr.size() - 1) jsData += L",";
        }
        jsData += L"]}";
        if (i < g_currentBattle.otherPets.size() - 1) jsData += L",";
    }
    jsData += L"],";
    
    jsData += L"\"myActiveIndex\":" + std::to_wstring(g_currentBattle.myActiveIndex) + L",";
    jsData += L"\"otherActiveIndex\":" + std::to_wstring(g_currentBattle.otherActiveIndex) + L",";
    jsData += L"\"lastItemName\":\"" + g_lastItemName + L"\"";
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

void PacketParser::ProcessBattlePacket(const GamePacket& packet) {
    if (packet.bSend) return;

    // Only process specific Opcodes as battle packets
    if (packet.opcode != OPCODE_BATTLE_START &&
        packet.opcode != OPCODE_BATTLE_ROUND_START &&
        packet.opcode != OPCODE_BATTLE_ROUND &&
        packet.opcode != OPCODE_BATTLE_END &&
        packet.opcode != Opcode::BATTLE_CHANGE_SPIRIT_ROUND) {
        return;
    }

    // Battle protocol detected, proceed to parse and then sync

    size_t offset = 0;
    const uint8_t* data = packet.body.data();
    size_t size = packet.body.size();

    if (packet.opcode == OPCODE_BATTLE_START) {
        if (!g_pendingRoundTip.empty()) {
            SendToUI(L"回合", g_pendingRoundTip);
            g_pendingRoundTip.clear();
        }
        g_currentBattle.myPets.clear();
        g_currentBattle.otherPets.clear();
        g_currentBattle.myActiveIndex = 0;
        g_currentBattle.otherActiveIndex = 0;

        if (offset + 4 > size) goto start_done;
        int state = (int)ReadInt32LE(data, offset);
        
        // SWF: for(; state != -1; state = msgpack.body.readInt())
        int spiritDebugIndex = 0;  // 调试计数
        while (state != -1 && offset + 4 <= size) {
            BattleEntity pet;
            if (state == 2) pet.state = 1;
            else pet.state = state;

            pet.sid = ReadInt32LE(data, offset);
            pet.groupType = ReadInt32LE(data, offset);
            pet.hp = (int)ReadInt32LE(data, offset);
            pet.maxHp = (int)ReadInt32LE(data, offset);
            pet.level = (int)ReadInt32LE(data, offset);
            pet.elem = ReadInt32LE(data, offset);
            pet.spiritId = ReadInt32LE(data, offset);
            pet.uniqueId = ReadInt32LE(data, offset);
            pet.userId = ReadInt32LE(data, offset);
            pet.skillNum = (int)ReadInt32LE(data, offset);
            
            // 获取名称
            {
                std::lock_guard<std::mutex> lock(g_dataMapsMutex);
                auto it = g_petNames.find(pet.spiritId);
                if (it != g_petNames.end()) pet.name = it->second;
            }

            {
                int remainingSkills = (int)((size > offset) ? ((size - offset) / 12) : 0);
                int readCount = pet.skillNum;
                if (readCount < 0) readCount = 0;
                if (readCount > remainingSkills) readCount = remainingSkills;
                for (int j = 0; j < readCount; ++j) {
                if (offset + 12 > size) break;
                BattleSkill skill;
                skill.id = ReadInt32LE(data, offset);
                skill.pp = (int)ReadInt32LE(data, offset); // time in swf
                skill.maxPp = (int)ReadInt32LE(data, offset); // maxtime in swf
                {
                    std::lock_guard<std::mutex> lock(g_dataMapsMutex);
                    auto it2 = g_skillNames.find((int)skill.id);
                    if (it2 != g_skillNames.end()) skill.name = it2->second;
                }
                pet.skills.push_back(skill);
                }
            }

            {
                // 根据 AS3 代码：非观战模式用 userId 比较，观战模式用 groupType 判断
                // 非观战模式：GlobalConfig.userId == spiritdata.userid 则为我方
                // 观战模式：groupType == 1 则为我方
                // 
                // 注意：groupType 只在观战模式下有意义，正常战斗中双方 groupType 可能都是1
                // 万妖盛会PVP虽然是人与人对战，但仍属于正常战斗模式，应使用 userId 判断
                bool isMy;
                if (g_userId > 0) {
                    // 非观战模式：用 userId 与当前玩家ID比较
                    isMy = (pet.userId == (int32_t)g_userId);
                } else {
                    // 未知玩家ID时，回退到 groupType 判断（仅观战模式）
                    isMy = (pet.groupType == 1);
                }
                pet.mySpirit = isMy;
                
                if (isMy) {
                    // pet.state == 1 表示我方首发妖怪（转换后的值）
                    if (pet.state == 1) {
                        g_currentBattle.myActiveIndex = (int)g_currentBattle.myPets.size();
                    }
                    g_currentBattle.myPets.push_back(pet);
                    if (pet.skillNum == 0 && offset + 4 <= size) {
                        int mNum = (int)ReadInt32LE(data, offset);
                        if (mNum > 1 && mNum <= 12) {
                            for (int i = 1; i < mNum; ++i) {
                                BattleEntity ph{};
                                ph.sid = 1;
                                ph.groupType = 1;
                                ph.state = 0;
                                ph.hp = 0;
                                ph.maxHp = 0;
                                ph.level = 0;
                                ph.elem = 0;
                                ph.spiritId = 0;
                                ph.uniqueId = 0;
                                ph.userId = 0;
                                ph.skillNum = 0;
                                g_currentBattle.myPets.push_back(ph);
                            }
                        }
                    }
                } else {
                    // pet.state == 1 表示敌方首发妖怪（转换后的值）
                    if (pet.state == 1) {
                        g_currentBattle.otherActiveIndex = (int)g_currentBattle.otherPets.size();
                    }
                    g_currentBattle.otherPets.push_back(pet);
                    if (pet.skillNum == 0 && offset + 4 <= size) {
                        int mNum = (int)ReadInt32LE(data, offset);
                        if (mNum > 1 && mNum <= 12) {
                            for (int i = 1; i < mNum; ++i) {
                                BattleEntity ph{};
                                ph.sid = 2;
                                ph.groupType = 2;
                                ph.state = 0;
                                ph.hp = 0;
                                ph.maxHp = 0;
                                ph.level = 0;
                                ph.elem = 0;
                                ph.spiritId = 0;
                                ph.uniqueId = 0;
                                ph.userId = 0;
                                ph.skillNum = 0;
                                g_currentBattle.otherPets.push_back(ph);
                            }
                        }
                    }
                }
            }

            if (offset + 4 > size) break;
            state = (int)ReadInt32LE(data, offset);
        }
        
        if (offset + 4 <= size) {
            g_currentBattle.escape = (int)ReadInt32LE(data, offset);
        }
        
start_done:
        UpdateUIBattleData();
        {
            std::wstring myName = L"";
            std::wstring otherName = L"";
            if (g_currentBattle.myActiveIndex < (int)g_currentBattle.myPets.size()) {
                const auto& p = g_currentBattle.myPets[g_currentBattle.myActiveIndex];
                myName = p.name.empty() ? (std::wstring(L"妖怪") + std::to_wstring(p.spiritId)) : p.name;
            }
            if (g_currentBattle.otherActiveIndex < (int)g_currentBattle.otherPets.size()) {
                const auto& p = g_currentBattle.otherPets[g_currentBattle.otherActiveIndex];
                otherName = p.name.empty() ? (std::wstring(L"妖怪") + std::to_wstring(p.spiritId)) : p.name;
            }
            std::wstring info = L"我方" + myName + L" vs 敌方" + otherName;
            SendToUI(L"战斗开始", info);
        }

        // 万妖盛会共享主战斗的解析结果。
        // 这里初始化 BattleSix 的基础战斗快照；结算与补发由 wpe_hook.cpp 专门处理。
        if (g_battleSixAuto.IsAutoBattleEnabled() && !g_currentBattle.myPets.empty()) {
            g_battleSixAuto.StartBattle();
            g_battleSixAuto.GetMySpirits().clear();
            g_battleSixAuto.GetEnemySpirits().clear();
            
            // 复制所有我方妖怪信息
            int spiritIndex = 0;  // 实际妖怪计数（跳过占位符后）
            int activeSpiritIndex = 0;  // 当前出战妖怪在有效列表中的索引
            for (size_t i = 0; i < g_currentBattle.myPets.size(); i++) {
                const auto& src = g_currentBattle.myPets[i];
                // 跳过占位符（spiritId为0的）
                if (src.spiritId == 0) continue;
                
                BattleSixSpiritInfo spirit;
                spirit.sid = src.sid;  // 关键：复制sid，用于匹配atkid/defid
                spirit.spiritId = src.spiritId;
                spirit.uniqueId = src.uniqueId;
                spirit.hp = src.hp;
                spirit.maxHp = src.maxHp;
                spirit.level = src.level;
                spirit.position = spiritIndex;
                spirit.isDead = (src.hp <= 0);
                spirit.name = src.name;
                
                // 记录当前出战妖怪在有效列表中的索引
                if (static_cast<int>(i) == g_currentBattle.myActiveIndex) {
                    activeSpiritIndex = spiritIndex;
                }
                spiritIndex++;
                // 复制技能
                for (const auto& srcSkill : src.skills) {
                    BattleSixSkillInfo skill;
                    skill.skillId = static_cast<int>(srcSkill.id);
                    skill.name = srcSkill.name;
                    {
                        std::lock_guard<std::mutex> lock(g_dataMapsMutex);
                        auto powerIt = g_skillPowers.find(skill.skillId);
                        skill.power = (powerIt != g_skillPowers.end()) ? powerIt->second : 0;
                    }
                    skill.currentPP = srcSkill.pp;
                    skill.maxPP = srcSkill.maxPp;
                    skill.available = (srcSkill.pp > 0);
                    spirit.skills.push_back(skill);
                }
                g_battleSixAuto.GetMySpirits().push_back(spirit);
            }

            // 复制敌方妖怪信息。即使后续敌方会切宠，也需要保留当前出战敌方以便自动技能锁定目标。
            int enemySpiritIndex = 0;
            for (size_t i = 0; i < g_currentBattle.otherPets.size(); i++) {
                const auto& src = g_currentBattle.otherPets[i];
                if (src.spiritId == 0) continue;

                BattleSixSpiritInfo spirit;
                spirit.sid = src.sid;
                spirit.spiritId = src.spiritId;
                spirit.uniqueId = src.uniqueId;
                spirit.userId = src.userId;
                spirit.hp = src.hp;
                spirit.maxHp = src.maxHp;
                spirit.level = src.level;
                spirit.element = src.elem;
                spirit.position = enemySpiritIndex++;
                spirit.isDead = (src.hp <= 0);
                spirit.name = src.name;

                for (const auto& srcSkill : src.skills) {
                    BattleSixSkillInfo skill;
                    skill.skillId = static_cast<int>(srcSkill.id);
                    skill.name = srcSkill.name;
                    {
                        std::lock_guard<std::mutex> lock(g_dataMapsMutex);
                        auto powerIt = g_skillPowers.find(skill.skillId);
                        skill.power = (powerIt != g_skillPowers.end()) ? powerIt->second : 0;
                    }
                    skill.currentPP = srcSkill.pp;
                    skill.maxPP = srcSkill.maxPp;
                    skill.available = (srcSkill.pp > 0);
                    spirit.skills.push_back(skill);
                }

                g_battleSixAuto.GetEnemySpirits().push_back(spirit);
            }
            
            // 设置当前出战妖怪索引
            g_battleSixAuto.SetCurrentSpiritIndex(activeSpiritIndex);
            if (activeSpiritIndex < static_cast<int>(g_battleSixAuto.GetMySpirits().size())) {
                g_battleSixAuto.SetMyUniqueId(g_battleSixAuto.GetMySpirits()[activeSpiritIndex].uniqueId);
            }
            
            g_battleSixAuto.RefreshEnemyTarget();
        }
    }
    else if (packet.opcode == OPCODE_BATTLE_ROUND_START) {
        // 战斗回合开始 - 处理buf回合数减少
        // 根据 AS3 代码分析，buf的round字段只在首次添加时从服务端获取
        // 之后每回合服务端不会更新回合数，需要客户端自己维护

        std::vector<std::pair<int, std::vector<BufData>>> expiredBuffs; // 过期的buf (sid, buf列表)

        // 遍历我方所有妖怪的bufArr，减少回合数
        for (auto& pet : g_currentBattle.myPets) {
            auto it = pet.bufArr.begin();
            while (it != pet.bufArr.end()) {
                if (it->round > 0) {
                    it->round--;  // 回合数减1
                    if (it->round <= 0) {
                        // 回合数耗尽，记录过期buf以便稍后移除和通知
                        std::vector<BufData> expiredBufList;
                        expiredBufList.push_back(*it);
                        expiredBuffs.push_back(std::make_pair(pet.sid, expiredBufList));
                        it = pet.bufArr.erase(it);
                    } else {
                        ++it;
                    }
                } else {
                    ++it;
                }
            }
        }

        // 遍历敌方所有妖怪的bufArr，减少回合数
        for (auto& pet : g_currentBattle.otherPets) {
            auto it = pet.bufArr.begin();
            while (it != pet.bufArr.end()) {
                if (it->round > 0) {
                    it->round--;  // 回合数减1
                    if (it->round <= 0) {
                        // 回合数耗尽，记录过期buf以便稍后移除和通知
                        std::vector<BufData> expiredBufList;
                        expiredBufList.push_back(*it);
                        expiredBuffs.push_back(std::make_pair(pet.sid, expiredBufList));
                        it = pet.bufArr.erase(it);
                    } else {
                        ++it;
                    }
                } else {
                    ++it;
                }
            }
        }

        // 更新UI显示
        UpdateUIBattleData();

        // 如果有过期的buf，发送通知
        if (!expiredBuffs.empty()) {
            std::wstring expiredInfo = L"";
            for (const auto& pair : expiredBuffs) {
                for (const auto& buf : pair.second) {
                    if (!expiredInfo.empty()) expiredInfo += L"，";
                    expiredInfo += buf.name;
                }
            }
            if (!expiredInfo.empty()) {
                std::wstring sidStr = std::to_wstring(expiredBuffs[0].first);
                SendToUI(L"回合开始", L"状态结束：" + expiredInfo);
            }
        }

        g_battleSixRoundToken.fetch_add(1);

        // 万妖盛会自动战斗不依赖倒计时走完。
        // 对照 AS3: BattleView.showCountTime(value) 会直接把 canBattle 置为 true，
        // 因此回合开始包到达即可尝试出招或切换精灵。
        if (g_battleSixAuto.IsInBattle() && g_battleSixAuto.IsAutoBattleEnabled()) {
            g_battleSixAuto.OnBattleRoundStart();
        }
    }
    else if (packet.opcode == Opcode::BATTLE_CHANGE_SPIRIT_ROUND) {
        // AS3 中该包表示进入切换精灵回合，不是切换成功回包。
        g_battleSixRoundToken.fetch_add(1);
        if (g_battleSixAuto.IsInBattle() && g_battleSixAuto.IsAutoBattleEnabled()) {
            int currentIndex = g_battleSixAuto.GetCurrentSpiritIndex();
            if (currentIndex >= 0 && currentIndex < static_cast<int>(g_battleSixAuto.GetMySpirits().size())) {
                const auto& currentSpirit = g_battleSixAuto.GetMySpirits()[currentIndex];
                if (currentSpirit.isDead || currentSpirit.hp <= 0) {
                    g_battleSixAuto.OnBattleRoundStart();
                }
            }
        }
    }
    else if (packet.opcode == OPCODE_BATTLE_ROUND) {
        // Round result logic, based on SWF (onBattleRoundResultParse)
        // 重要：atkid 和 defid 是妖怪的 sid（服务端ID），不是简单的 1 或 2
        // 需要通过与当前出战妖怪的 sid 比较来判断敌我
        
        // 获取当前出战妖怪的 sid（在整个回合处理中都需要用到）
        int myActiveSid = 0;
        int otherActiveSid = 0;
        if (g_currentBattle.myActiveIndex < (int)g_currentBattle.myPets.size()) {
            myActiveSid = g_currentBattle.myPets[g_currentBattle.myActiveIndex].sid;
        }
        if (g_currentBattle.otherActiveIndex < (int)g_currentBattle.otherPets.size()) {
            otherActiveSid = g_currentBattle.otherPets[g_currentBattle.otherActiveIndex].sid;
        }
        
        int cmdType = packet.params; // msgpack.mParams
        
        if (cmdType == 0) { // Normal round
            int lastAtkSid = 0;
            int lastDefSid = 0;
            uint32_t lastSkillId = 0;
            
            if (offset + 4 > size) goto round_done;
            int haveBattle = (int)ReadInt32LE(data, offset);
            
            if (haveBattle == 1) {
                if (offset + 16 > size) goto round_done;
                int atkSid = (int)ReadInt32LE(data, offset);  // 攻击者的 sid
                uint32_t skillId = ReadInt32LE(data, offset);
                int defSid = (int)ReadInt32LE(data, offset);  // 防御者的 sid
                int miss = (int)ReadInt32LE(data, offset);

                lastAtkSid = atkSid;
                lastDefSid = defSid;
                lastSkillId = skillId;

                // 通过 sid 判断攻击者是我方还是敌方
                bool isMyAtk = (atkSid == myActiveSid);
                bool isOtherAtk = (atkSid == otherActiveSid);
                
                // 更新技能 PP
                {
                    auto& team = isMyAtk ? g_currentBattle.myPets : g_currentBattle.otherPets;
                    int activeIdx = isMyAtk ? g_currentBattle.myActiveIndex : g_currentBattle.otherActiveIndex;
                    if (activeIdx < (int)team.size()) {
                        for (auto& s : team[activeIdx].skills) {
                            if (s.id == skillId) {
                                if (s.pp > 0) s.pp -= 1;
                                break;
                            }
                        }
                    }
                }

                if (miss == 0) {
                    int oldAtkHp = 0;
                    int oldDefHp = 0;
                    
                    // 获取攻击前血量
                    if (isMyAtk) {
                        oldAtkHp = (g_currentBattle.myActiveIndex < (int)g_currentBattle.myPets.size()) 
                            ? g_currentBattle.myPets[g_currentBattle.myActiveIndex].hp : 0;
                        oldDefHp = (g_currentBattle.otherActiveIndex < (int)g_currentBattle.otherPets.size()) 
                            ? g_currentBattle.otherPets[g_currentBattle.otherActiveIndex].hp : 0;
                    } else {
                        oldAtkHp = (g_currentBattle.otherActiveIndex < (int)g_currentBattle.otherPets.size()) 
                            ? g_currentBattle.otherPets[g_currentBattle.otherActiveIndex].hp : 0;
                        oldDefHp = (g_currentBattle.myActiveIndex < (int)g_currentBattle.myPets.size()) 
                            ? g_currentBattle.myPets[g_currentBattle.myActiveIndex].hp : 0;
                    }
                    
                    if (offset + 16 > size) goto round_done;
                    ReadInt32LE(data, offset); // brust
                    int atk_hp = (int)ReadInt32LE(data, offset);
                    int def_hp = (int)ReadInt32LE(data, offset);
                    ReadInt32LE(data, offset); // rebound_hp
                    
                    // 更新 HP
                    if (isMyAtk) {
                        if (g_currentBattle.myActiveIndex < g_currentBattle.myPets.size())
                            g_currentBattle.myPets[g_currentBattle.myActiveIndex].hp = atk_hp;
                        if (g_currentBattle.otherActiveIndex < g_currentBattle.otherPets.size())
                            g_currentBattle.otherPets[g_currentBattle.otherActiveIndex].hp = def_hp;
                    } else {
                        if (g_currentBattle.otherActiveIndex < g_currentBattle.otherPets.size())
                            g_currentBattle.otherPets[g_currentBattle.otherActiveIndex].hp = atk_hp;
                        if (g_currentBattle.myActiveIndex < g_currentBattle.myPets.size())
                            g_currentBattle.myPets[g_currentBattle.myActiveIndex].hp = def_hp;
                    }
                    
                    // 同步HP到自动战斗模块
                    {
                        int myNewHp = isMyAtk ? atk_hp : def_hp;
                        bool isDead = (myNewHp <= 0);
                        
                        // 同步到自动战斗模块（如果启用了）
                        if (g_battleSixAuto.IsInBattle() && g_battleSixAuto.IsAutoBattleEnabled()) {
                            int currentIndex = g_battleSixAuto.GetCurrentSpiritIndex();
                            if (currentIndex >= 0 && currentIndex < (int)g_battleSixAuto.GetMySpirits().size()) {
                                g_battleSixAuto.GetMySpirits()[currentIndex].hp = myNewHp;
                                g_battleSixAuto.GetMySpirits()[currentIndex].isDead = isDead;
                                
                                // 如果当前精灵死亡，延迟1秒后发送切换封包
                                if (g_battleSixSwitchRetryCount.load() < 0 && isDead) {  // BattleSix 切怪时机改为 round start
                                    int nextIndex = g_battleSixAuto.FindNextAliveSpirit(currentIndex + 1);
                                    
                                    if (nextIndex >= 0) {
                                        int uniqueId = g_battleSixAuto.GetMySpirits()[nextIndex].uniqueId;
                                        
                                        // 更新内部状态
                                        g_battleSixAuto.SetCurrentSpiritIndex(nextIndex);
                                        g_battleSixAuto.SetMyUniqueId(uniqueId);
                                        
                                        // 在新线程中延迟2秒后发送
                                        struct SwitchData {
                                            int uniqueId;
                                            int nextIndex;
                                        };
                                        SwitchData* data = new SwitchData{uniqueId, nextIndex};
                                        
                                        // 设置切换目标
                                        g_battleSixSwitchTargetId = uniqueId;
                                        g_battleSixSwitchRetryCount = 0;
                                        
                                        CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
                                            SwitchData* d = (SwitchData*)lpParam;
                                            Sleep(2000); // 延迟2秒
                                            SendBattleSixUserOpPacket(1, d->uniqueId, 0);
                                            delete d;
                                            return 0;
                                        }, data, 0, nullptr);
                                    }
                                }
                            }
                        }
                    }
                    
                    // 构建战斗信息
                    {
                        std::wstring atkName = L"", defName = L"";
                        if (isMyAtk) {
                            if (g_currentBattle.myActiveIndex < (int)g_currentBattle.myPets.size()) {
                                const auto& p = g_currentBattle.myPets[g_currentBattle.myActiveIndex];
                                atkName = p.name.empty() ? (std::wstring(L"妖怪") + std::to_wstring(p.spiritId)) : p.name;
                            }
                            if (g_currentBattle.otherActiveIndex < (int)g_currentBattle.otherPets.size()) {
                                const auto& p = g_currentBattle.otherPets[g_currentBattle.otherActiveIndex];
                                defName = p.name.empty() ? (std::wstring(L"妖怪") + std::to_wstring(p.spiritId)) : p.name;
                            }
                        } else {
                            if (g_currentBattle.otherActiveIndex < (int)g_currentBattle.otherPets.size()) {
                                const auto& p = g_currentBattle.otherPets[g_currentBattle.otherActiveIndex];
                                atkName = p.name.empty() ? (std::wstring(L"妖怪") + std::to_wstring(p.spiritId)) : p.name;
                            }
                            if (g_currentBattle.myActiveIndex < (int)g_currentBattle.myPets.size()) {
                                const auto& p = g_currentBattle.myPets[g_currentBattle.myActiveIndex];
                                defName = p.name.empty() ? (std::wstring(L"妖怪") + std::to_wstring(p.spiritId)) : p.name;
                            }
                        }
                        
                        std::wstring skillName = L"";
                        {
                            std::lock_guard<std::mutex> lock(g_dataMapsMutex);
                            auto it = g_skillNames.find((int)lastSkillId);
                            if (it != g_skillNames.end()) skillName = it->second;
                            // 也尝试从当前妖怪技能列表获取
                            if (skillName.empty()) {
                                auto& team = isMyAtk ? g_currentBattle.myPets : g_currentBattle.otherPets;
                                int activeIdx = isMyAtk ? g_currentBattle.myActiveIndex : g_currentBattle.otherActiveIndex;
                                if (activeIdx < (int)team.size()) {
                                    for (const auto& s : team[activeIdx].skills) {
                                        if (s.id == lastSkillId) { skillName = s.name; break; }
                                    }
                                }
                            }
                            if (skillName.empty()) skillName = std::wstring(L"技能") + std::to_wstring(lastSkillId);
                        }
                        
                        int dmg = oldDefHp - def_hp;
                        if (dmg < 0) dmg = 0;
                        std::wstring info = atkName + std::wstring(L"使用") + skillName + std::wstring(L"，造成") + std::to_wstring(dmg) + std::wstring(L"伤害，") + defName + std::wstring(L"剩余") + std::to_wstring(def_hp);
                        if (g_pendingRoundTip.empty()) g_pendingRoundTip = info;
                        else {
                            std::wstring combined = g_pendingRoundTip + std::wstring(L"；") + info;
                            SendToUI(L"回合", combined);
                            g_pendingRoundTip.clear();
                        }
                    }
                }
                else {
                    // 闪避
                    std::wstring atkName = L"", defName = L"";
                    if (isMyAtk) {
                        if (g_currentBattle.myActiveIndex < (int)g_currentBattle.myPets.size()) {
                            const auto& p = g_currentBattle.myPets[g_currentBattle.myActiveIndex];
                            atkName = p.name.empty() ? (std::wstring(L"妖怪") + std::to_wstring(p.spiritId)) : p.name;
                        }
                        if (g_currentBattle.otherActiveIndex < (int)g_currentBattle.otherPets.size()) {
                            const auto& p = g_currentBattle.otherPets[g_currentBattle.otherActiveIndex];
                            defName = p.name.empty() ? (std::wstring(L"妖怪") + std::to_wstring(p.spiritId)) : p.name;
                        }
                    } else {
                        if (g_currentBattle.otherActiveIndex < (int)g_currentBattle.otherPets.size()) {
                            const auto& p = g_currentBattle.otherPets[g_currentBattle.otherActiveIndex];
                            atkName = p.name.empty() ? (std::wstring(L"妖怪") + std::to_wstring(p.spiritId)) : p.name;
                        }
                        if (g_currentBattle.myActiveIndex < (int)g_currentBattle.myPets.size()) {
                            const auto& p = g_currentBattle.myPets[g_currentBattle.myActiveIndex];
                            defName = p.name.empty() ? (std::wstring(L"妖怪") + std::to_wstring(p.spiritId)) : p.name;
                        }
                    }
                    
                    std::wstring skillName = L"";
                    {
                        std::lock_guard<std::mutex> lock(g_dataMapsMutex);
                        auto it = g_skillNames.find((int)lastSkillId);
                        if (it != g_skillNames.end()) skillName = it->second;
                        if (skillName.empty()) skillName = std::wstring(L"技能") + std::to_wstring(lastSkillId);
                    }
                    
                    std::wstring info = defName + std::wstring(L"闪避了") + atkName + std::wstring(L"的") + skillName;
                    if (g_pendingRoundTip.empty()) g_pendingRoundTip = info;
                    else {
                        std::wstring combined = g_pendingRoundTip + std::wstring(L"；") + info;
                        SendToUI(L"回合", combined);
                        g_pendingRoundTip.clear();
                    }
                }
            }

            // Buff handling loop - 完整实现参考 AS3 BattleModel.as
            // 使用之前记录的 myActiveSid 和 otherActiveSid 来判断敌我
            std::vector<BufData> roundBufArr;  // 本回合产生的 Buff 列表
            std::vector<std::pair<uint32_t, int>> ppChanges;  // PP 变化 (skillId, delta)
            
            if (offset + 4 <= size) {
                uint32_t haveBuf = ReadInt32LE(data, offset);
                while (haveBuf != 0 && offset + 4 <= size) {
                    BufData bufData;
                    bufData.bufId = haveBuf;
                    bufData.addOrRemove = (int)ReadInt32LE(data, offset);
                    
                    switch(bufData.addOrRemove) {
                        case BufDataType::BUF_TYPE_0:  // 移除 Buff
                            if (offset + 4 <= size) {
                                bufData.defId = (int)ReadInt32LE(data, offset);
                                // 从对应妖怪的 bufArr 中移除
                                {
                                    bool isMy = (bufData.defId == myActiveSid);
                                    auto& team = isMy ? g_currentBattle.myPets : g_currentBattle.otherPets;
                                    int activeIdx = isMy ? g_currentBattle.myActiveIndex : g_currentBattle.otherActiveIndex;
                                    if (activeIdx < (int)team.size()) {
                                        auto& bufArr = team[activeIdx].bufArr;
                                        for (auto it = bufArr.begin(); it != bufArr.end(); ) {
                                            if (it->bufId == bufData.bufId) {
                                                it = bufArr.erase(it);
                                            } else {
                                                ++it;
                                            }
                                        }
                                    }
                                }
                            }
                            break;
                            
                        case BufDataType::BUF_TYPE_1:  // 添加 Buff (带回合数)
                            if (offset + 20 <= size) {
                                bufData.atkId = (int)ReadInt32LE(data, offset);
                                bufData.defId = (int)ReadInt32LE(data, offset);
                                bufData.round = (int)ReadInt32LE(data, offset);
                                bufData.param1 = (int)ReadInt32LE(data, offset);
                                bufData.param2 = (int)ReadInt32LE(data, offset);
                                bufData.name = GetBufName(bufData.bufId, bufData.param1);
                                bufData.tipString = GetBufTipString(bufData.bufId, bufData.param1, bufData.param2);
                                
                                lastAtkSid = bufData.atkId;
                                lastDefSid = bufData.defId;
                                
                                // 添加到对应妖怪的 bufArr
                                {
                                    bool isMy = (bufData.defId == myActiveSid);
                                    auto& team = isMy ? g_currentBattle.myPets : g_currentBattle.otherPets;
                                    int activeIdx = isMy ? g_currentBattle.myActiveIndex : g_currentBattle.otherActiveIndex;
                                    if (activeIdx < (int)team.size()) {
                                        team[activeIdx].bufArr.push_back(bufData);
                                    }
                                }
                                roundBufArr.push_back(bufData);
                            }
                            break;
                            
                        case BufDataType::BUF_TYPE_2:  // 添加 Buff (带参数，通常用于血量变化)
                            if (offset + 16 <= size) {
                                bufData.atkId = (int)ReadInt32LE(data, offset);
                                bufData.defId = (int)ReadInt32LE(data, offset);
                                bufData.param1 = (int)ReadInt32LE(data, offset);
                                bufData.param2 = (int)ReadInt32LE(data, offset);
                                bufData.name = GetBufName(bufData.bufId, bufData.param1);
                                bufData.tipString = GetBufTipString(bufData.bufId, bufData.param1, bufData.param2);
                                
                                lastAtkSid = bufData.atkId;
                                lastDefSid = bufData.defId;
                                
                                // 检查是否为血量变化 Buff
                                bool isBloodChange = false;
                                for (int id : BufDataType::DEALADD_BLOOD_1) {
                                    if (id == bufData.bufId) { isBloodChange = true; break; }
                                }
                                for (int id : BufDataType::DEALADD_BLOOD_2) {
                                    if (id == bufData.bufId) { isBloodChange = true; break; }
                                }
                                
                                // 更新血量
                                if (isBloodChange && bufData.param1 != 0) {
                                    bool isMy = (bufData.defId == myActiveSid);
                                    auto& team = isMy ? g_currentBattle.myPets : g_currentBattle.otherPets;
                                    int activeIdx = isMy ? g_currentBattle.myActiveIndex : g_currentBattle.otherActiveIndex;
                                    if (activeIdx < (int)team.size()) {
                                        // DEALADD_BLOOD_1 中的 Buff 造成伤害，param1 为伤害值
                                        // DEALADD_BLOOD_2 中的 Buff 恢复血量，param1 为恢复值
                                        bool isDamage = false;
                                        for (int id : BufDataType::DEALADD_BLOOD_1) {
                                            if (id == bufData.bufId) { isDamage = true; break; }
                                        }
                                        int hpChange = isDamage ? -bufData.param1 : bufData.param1;
                                        team[activeIdx].hp += hpChange;
                                        if (team[activeIdx].hp < 0) team[activeIdx].hp = 0;
                                        if (team[activeIdx].hp > team[activeIdx].maxHp) team[activeIdx].hp = team[activeIdx].maxHp;
                                    }
                                }
                                
                                roundBufArr.push_back(bufData);
                            }
                            break;
                            
                        case BufDataType::BUF_TYPE_3:  // PP 减少 (所有技能)
                            if (offset + 4 <= size) {
                                int ppDelta = -(int)ReadInt32LE(data, offset);
                                ppChanges.push_back({0, ppDelta});  // skillId=0 表示所有技能
                            }
                            break;
                            
                        case BufDataType::BUF_TYPE_4:  // PP 设置 (指定技能)
                            if (offset + 8 <= size) {
                                uint32_t skillId = ReadInt32LE(data, offset);
                                int ppVal = (int)ReadInt32LE(data, offset);
                                // 直接设置 PP 值，作用于 defid
                                bool isMy = (lastDefSid == myActiveSid);
                                auto& team = isMy ? g_currentBattle.myPets : g_currentBattle.otherPets;
                                int activeIdx = isMy ? g_currentBattle.myActiveIndex : g_currentBattle.otherActiveIndex;
                                if (activeIdx < (int)team.size()) {
                                    for (auto& s : team[activeIdx].skills) {
                                        if (s.id == skillId) {
                                            s.pp = ppVal;
                                            break;
                                        }
                                    }
                                }
                            }
                            break;
                            
                        case BufDataType::BUF_TYPE_6:  // PP 增加 (所有技能)
                            if (offset + 4 <= size) {
                                int ppDelta = (int)ReadInt32LE(data, offset);
                                ppChanges.push_back({0, ppDelta});  // skillId=0 表示所有技能
                            }
                            break;
                    }
                    
                    if (offset + 4 > size) break;
                    haveBuf = ReadInt32LE(data, offset);
                }
                
                // 应用 PP 变化
                // 重要：PP 变化作用于 defid（被作用者），不是 atkid（攻击者）
                // 参考 AS3: if(this.playerRole.roleInfo.sid == this.boobj.defid)
                for (const auto& ppChange : ppChanges) {
                    bool isMy = (lastDefSid == myActiveSid);
                    auto& team = isMy ? g_currentBattle.myPets : g_currentBattle.otherPets;
                    int activeIdx = isMy ? g_currentBattle.myActiveIndex : g_currentBattle.otherActiveIndex;
                    if (activeIdx < (int)team.size()) {
                        for (auto& s : team[activeIdx].skills) {
                            if (ppChange.first == 0 || s.id == ppChange.first) {
                                s.pp += ppChange.second;
                                if (s.pp < 0) s.pp = 0;
                                if (s.pp > s.maxPp) s.pp = s.maxPp;
                            }
                        }
                    }
                }
                
                // 构建 Buff 信息文本
                if (!roundBufArr.empty()) {
                    std::wstring bufInfo = L"";
                    for (const auto& buf : roundBufArr) {
                        if (!bufInfo.empty()) bufInfo += L"，";
                        bufInfo += buf.name;
                        if (buf.round > 0) {
                            bufInfo += L"(" + std::to_wstring(buf.round) + L"回合)";
                        }
                    }
                    if (g_pendingRoundTip.empty()) {
                        g_pendingRoundTip = L"状态变化：" + bufInfo;
                    } else {
                        g_pendingRoundTip += L"；状态：" + bufInfo;
                    }
                }
            }
        } 
        else if (cmdType == 1) { // Switch Pet
            if (!g_pendingRoundTip.empty()) {
                SendToUI(L"回合", g_pendingRoundTip);
                g_pendingRoundTip.clear();
            }
            if (offset + 12 > size) goto round_done;
            int switcherSid = (int)ReadInt32LE(data, offset);  // 发起切换的一方的当前妖怪 sid
            uint32_t uniqueId = ReadInt32LE(data, offset);
            int sta = (int)ReadInt32LE(data, offset);
            
            // 通过比较 sid 与当前出战妖怪的 sid 判断是哪一方在切换宠物
            // 参考 AS3: if(this.playerRole.roleInfo.sid == this.boobj.sid)
            bool isMySideSwitching = (switcherSid == myActiveSid);
            
            if (sta == 1) {
                if (offset + 40 <= size) {
                    BattleEntity pet;
                    pet.state = (int)ReadInt32LE(data, offset);
                    pet.sid = ReadInt32LE(data, offset);
                    pet.groupType = ReadInt32LE(data, offset);
                    pet.hp = (int)ReadInt32LE(data, offset);
                    pet.maxHp = (int)ReadInt32LE(data, offset);
                    pet.level = (int)ReadInt32LE(data, offset);
                    pet.elem = ReadInt32LE(data, offset);
                    pet.spiritId = ReadInt32LE(data, offset);
                    pet.uniqueId = ReadInt32LE(data, offset);
                    pet.userId = ReadInt32LE(data, offset);
                    pet.skillNum = (int)ReadInt32LE(data, offset);
                    {
                        std::lock_guard<std::mutex> lock(g_dataMapsMutex);
                        auto it = g_petNames.find(pet.spiritId);
                        if (it != g_petNames.end()) pet.name = it->second;
                    }
                    for (int j = 0; j < pet.skillNum; ++j) {
                        if (offset + 12 > size) break;
                        BattleSkill skill;
                        skill.id = ReadInt32LE(data, offset);
                        skill.pp = (int)ReadInt32LE(data, offset);
                        skill.maxPp = (int)ReadInt32LE(data, offset);
                        {
                            std::lock_guard<std::mutex> lock(g_dataMapsMutex);
                            auto it2 = g_skillNames.find((int)skill.id);
                            if (it2 != g_skillNames.end()) skill.name = it2->second;
                        }
                        pet.skills.push_back(skill);
                    }
                    
                    // 根据切换方决定添加到哪个列表
                    // PVP特殊战斗：敌方开始时只有1只真实妖怪+5只空占位(uniqueId=0)
                    // 切换时新妖怪应替换空占位，而不是追加
                    auto& petList = isMySideSwitching ? g_currentBattle.myPets : g_currentBattle.otherPets;
                    
                    // 清空原出战妖怪的 bufArr（切换下场时状态消失）并更新 UI
                    int oldActiveIdx = isMySideSwitching ? g_currentBattle.myActiveIndex : g_currentBattle.otherActiveIndex;
                    if (oldActiveIdx >= 0 && oldActiveIdx < (int)petList.size()) {
                        petList[oldActiveIdx].bufArr.clear();
                    }
                    
                    // 新上场的妖怪不清空 bufArr（保留开场 buff）
                    
                    bool found = false;
                    // 1. 先按 uniqueId 精确匹配查找
                    for (auto& p : petList) {
                        if (p.uniqueId == pet.uniqueId && pet.uniqueId != 0) {
                            p = pet;
                            found = true;
                            break;
                        }
                    }
                    
                    // 2. 如果没找到，查找空占位（uniqueId==0 或 spiritId==0）进行替换
                    if (!found) {
                        for (auto& p : petList) {
                            if (p.uniqueId == 0 || p.spiritId == 0) {
                                p = pet;
                                found = true;
                                break;
                            }
                        }
                    }
                    
                    // 3. 只有列表中没有空占位时才追加
                    if (!found) {
                        petList.push_back(pet);
                    }
                }
            }

            // 更新活跃索引
            if (isMySideSwitching) {
                g_battleSixSwitchTargetId = -1;
                g_battleSixSwitchRetryCount = 0;
                for (size_t i = 0; i < g_currentBattle.myPets.size(); ++i) {
                    if (g_currentBattle.myPets[i].uniqueId == uniqueId) {
                        g_currentBattle.myActiveIndex = (int)i;
                        // 更新 myActiveSid 为新上场妖怪的 sid
                        myActiveSid = g_currentBattle.myPets[i].sid;
                        break;
                    }
                }
                // 同步切换到自动战斗模块
                if (g_battleSixAuto.IsInBattle()) {
                    // 在 g_battleSixAuto.GetMySpirits() 中找到对应的妖怪
                    for (size_t i = 0; i < g_battleSixAuto.GetMySpirits().size(); ++i) {
                        if (g_battleSixAuto.GetMySpirits()[i].uniqueId == static_cast<int>(uniqueId)) {
                            g_battleSixAuto.SetCurrentSpiritIndex(static_cast<int>(i));
                            g_battleSixAuto.SetMyUniqueId(static_cast<int>(uniqueId));
                            // 更新该精灵的HP
                            g_battleSixAuto.GetMySpirits()[i].hp = g_currentBattle.myPets[g_currentBattle.myActiveIndex].hp;
                            g_battleSixAuto.GetMySpirits()[i].isDead = (g_battleSixAuto.GetMySpirits()[i].hp <= 0);
                            break;
                        }
                    }
                }
            } else {
                for (size_t i = 0; i < g_currentBattle.otherPets.size(); ++i) {
                    if (g_currentBattle.otherPets[i].uniqueId == uniqueId) {
                        g_currentBattle.otherActiveIndex = (int)i;
                        // 更新 otherActiveSid 为新上场妖怪的 sid
                        otherActiveSid = g_currentBattle.otherPets[i].sid;
                        break;
                    }
                }
            }
            {
                std::wstring side = isMySideSwitching ? L"我方" : L"敌方";
                std::wstring petName = L"";
                if (isMySideSwitching) {
                    if (g_currentBattle.myActiveIndex < (int)g_currentBattle.myPets.size()) {
                        const auto& p = g_currentBattle.myPets[g_currentBattle.myActiveIndex];
                        petName = p.name.empty() ? (std::wstring(L"妖怪") + std::to_wstring(p.spiritId)) : p.name;
                    }
                } else {
                    if (g_currentBattle.otherActiveIndex < (int)g_currentBattle.otherPets.size()) {
                        const auto& p = g_currentBattle.otherPets[g_currentBattle.otherActiveIndex];
                        petName = p.name.empty() ? (std::wstring(L"妖怪") + std::to_wstring(p.spiritId)) : p.name;
                    }
                }
                SendToUI(L"切换宠物", side + std::wstring(L"上场") + petName);
            }
            // 更新 UI 以清空原妖怪的 buff 显示并显示新妖怪数据
            UpdateUIBattleData();
        }
        else if (cmdType == 2) { // Use Item
            if (!g_pendingRoundTip.empty()) {
                SendToUI(L"回合", g_pendingRoundTip);
                g_pendingRoundTip.clear();
            }
            if (offset + 20 > size) return;
            int atkSid = (int)ReadInt32LE(data, offset);  // 使用道具者的 sid
            uint32_t itemId = ReadInt32LE(data, offset);
            int defSid = (int)ReadInt32LE(data, offset);  // 道具作用对象的 sid
            int p0 = (int)ReadInt32LE(data, offset);
            int p1 = (int)ReadInt32LE(data, offset);
            {
                std::lock_guard<std::mutex> lock(g_dataMapsMutex);
                auto itn = g_toolNames.find((int)itemId);
                if (itn != g_toolNames.end()) g_lastItemName = itn->second;
                else g_lastItemName.clear();
            }
            
            // 使用 sid 判断敌我
            bool atkIsMy = (atkSid == myActiveSid);
            bool defIsMy = (defSid == myActiveSid);
            
            // 更新道具使用者的技能PP或HP
            {
                auto& team = atkIsMy ? g_currentBattle.myPets : g_currentBattle.otherPets;
                int activeIdx = atkIsMy ? g_currentBattle.myActiveIndex : g_currentBattle.otherActiveIndex;
                
                if (activeIdx < (int)team.size()) {
                    auto& pet = team[activeIdx];
                    
                    // PP 恢复道具
                    if (itemId == 100009) {
                        for (auto& s : pet.skills) s.pp = s.maxPp;
                    } else if (itemId == 100010 || itemId == 100081 || itemId == 100011 || itemId == 100012) {
                        int add = (itemId == 100010 || itemId == 100081) ? 5 : (itemId == 100011 ? 10 : 20);
                        for (auto& s : pet.skills) {
                            s.pp += add;
                            if (s.pp > s.maxPp) s.pp = s.maxPp;
                        }
                    }
                    
                    // HP 恢复道具
                    int hpAdd = 0;
                    switch(itemId) {
                        case 100005: case 100031: case 100179: hpAdd = 150; break;
                        case 100006: hpAdd = 100; break;
                        case 100007: hpAdd = 50; break;
                        case 100008: hpAdd = 20; break;
                        case 100034: hpAdd = 170; break;
                        case 100180: case 100758: hpAdd = 200; break;
                        case 100523: hpAdd = 160; break;
                    }
                    if (hpAdd > 0) {
                        pet.hp += hpAdd;
                        if (pet.hp > pet.maxHp) pet.hp = pet.maxHp;
                    }
                }
            }

            if (p0 == 1 && offset + 72 <= size) {
                for (int i = 0; i < 18; ++i) ReadInt32LE(data, offset);
            }
            {
                std::wstring atkName = L"";
                if (atkIsMy) {
                    if (g_currentBattle.myActiveIndex < (int)g_currentBattle.myPets.size()) {
                        const auto& p = g_currentBattle.myPets[g_currentBattle.myActiveIndex];
                        atkName = p.name.empty() ? (std::wstring(L"妖怪") + std::to_wstring(p.spiritId)) : p.name;
                    }
                } else {
                    if (g_currentBattle.otherActiveIndex < (int)g_currentBattle.otherPets.size()) {
                        const auto& p = g_currentBattle.otherPets[g_currentBattle.otherActiveIndex];
                        atkName = p.name.empty() ? (std::wstring(L"妖怪") + std::to_wstring(p.spiritId)) : p.name;
                    }
                }
                std::wstring itemName = g_lastItemName.empty() ? (std::wstring(L"道具") + std::to_wstring(itemId)) : g_lastItemName;
                std::wstring targetSide = defIsMy ? L"我方" : L"敌方";
                SendToUI(L"使用道具", atkName + std::wstring(L"使用") + itemName + std::wstring(L"作用于") + targetSide);
            }
        }
        else if (cmdType == 3) { // Escape/End
            if (!g_pendingRoundTip.empty()) {
                SendToUI(L"回合", g_pendingRoundTip);
                g_pendingRoundTip.clear();
            }
            g_currentBattle.myPets.clear();
            g_currentBattle.otherPets.clear();
            SendToUI(L"逃跑", L"本次战斗已结束");
        }

round_done:
        UpdateUIBattleData();
    } else if (packet.opcode == OPCODE_BATTLE_END) {
        if (!g_pendingRoundTip.empty()) {
            SendToUI(L"回合", g_pendingRoundTip);
            g_pendingRoundTip.clear();
        }
        g_currentBattle.myPets.clear();
        g_currentBattle.otherPets.clear();
        SendToUI(L"战斗结束", L"双方战斗结束");
        UpdateUIBattleData();

        // 注意：BattleSix 的私有结算、补发与自动匹配续跑在 wpe_hook.cpp 中处理，
        // 这里仅清理公共战斗数据，避免重复 EndBattle()。
    }
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
