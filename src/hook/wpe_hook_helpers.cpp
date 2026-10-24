#include "wpe_hook.h"
#include "utils.h"

#include <cctype>
#include <cstdlib>

namespace {

struct ItemNameEntry {
    uint32_t id;
    const wchar_t* name;
};

struct ItemPriceEntry {
    uint32_t id;
    uint32_t price;
};

struct PacketLabelEntry {
    uint32_t opcode;
    const char* label;
};

constexpr char HEX_DIGITS[] = "0123456789abcdef";

bool TryParseHexBytePair(char high, char low, BYTE& value) {
    auto hexValue = [](unsigned char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    int hi = hexValue(static_cast<unsigned char>(high));
    int lo = hexValue(static_cast<unsigned char>(low));
    if (hi < 0 || lo < 0) {
        return false;
    }

    value = static_cast<BYTE>((hi << 4) | lo);
    return true;
}

static const PacketLabelEntry g_packetLabelEntries[] = {
    {1186049, "战斗准备"},
    {1317120, "战斗开始"},
    {1317121, "战斗回合开始"},
    {1317122, "战斗回合结算"},
    {1317123, "战斗Buff移除"},
    {1317124, "战斗Buff"},
    {1317126, "切换妖怪回合"},
    {1317127, "战斗经验"},
    {1317130, "战斗新经验"},
    {1317131, "战斗Buff列表"},
    {1317154, "战斗技能"},
    {1317216, "妖怪升级"},
    {1317256, "战斗奖励"},
    {1186056, "战斗结束请求"},
    {1186232, "组队战斗查看"},
    {1186233, "查看战斗准备"},
    {1186113, "退出大型战斗"},
    {1316469, "玩家战斗"},
    {1184313, "进入场景"},
    {1315395, "进入场景响应"},
    {1184519, "切换线路"},
    {1315591, "切换线路响应"},
    {1184317, "场景玩家列表"},
    {1315086, "场景玩家列表响应"},
    {1315075, "添加玩家到场景"},
    {1315328, "从场景移除玩家"},
    {1315124, "玩家替换"},
    {1183750, "进入世界"},
    {1314822, "进入世界响应"},
    {1184029, "退出世界"},
    {1315101, "退出世界响应"},
    {1186818, "贝贝回血"},
    {1185809, "灵玉查询"},
    {1316881, "灵玉列表"},
    {1185814, "分解灵玉"},
    {1316886, "分解响应"},
    {1185819, "批量分解"},
    {1316891, "批量分解响应"},
    {1318401, "妖怪背包"},
    {1315083, "聊天消息"},
    {1316376, "家族聊天"},
};

}  // namespace

std::wstring GetItemName(uint32_t itemId) {
    static const ItemNameEntry itemNames[] = {
        {100005, L"蟠桃"},
        {100006, L"大金创药"},
        {100009, L"天仙玉露"},
        {100010, L"小型法力药剂"},
        {100011, L"中型法力药剂"},
        {100012, L"大型法力药剂"},
        {100031, L"活血散"},
        {100034, L"天香丸"},
        {100758, L"培元金丹"},
    };

    for (const auto& entry : itemNames) {
        if (entry.id == itemId) {
            return entry.name;
        }
    }

    return std::to_wstring(itemId);
}

std::string HexToString(const BYTE* pData, DWORD dwSize, bool spaced) {
    if (dwSize == 0) {
        return "";
    }

    std::string result;
    result.reserve(static_cast<size_t>(dwSize) * (spaced ? 3 : 2) - (spaced ? 1 : 0));
    for (DWORD i = 0; i < dwSize; i++) {
        BYTE value = pData[i];
        result.push_back(HEX_DIGITS[(value >> 4) & 0x0F]);
        result.push_back(HEX_DIGITS[value & 0x0F]);
        if (spaced && i < dwSize - 1) {
            result.push_back(' ');
        }
    }
    return result;
}

std::vector<BYTE> StringToHex(const std::string& str) {
    std::vector<BYTE> result;
    std::string cleanStr;
    cleanStr.reserve(str.size());

    for (char c : str) {
        if (std::isxdigit(static_cast<unsigned char>(c))) {
            cleanStr.push_back(c);
        }
    }

    if (cleanStr.length() % 2 != 0) {
        cleanStr.insert(cleanStr.begin(), '0');
    }

    result.reserve(cleanStr.length() / 2);
    for (size_t i = 0; i + 1 < cleanStr.length(); i += 2) {
        BYTE value = 0;
        if (!TryParseHexBytePair(cleanStr[i], cleanStr[i + 1], value)) {
            result.clear();
            return result;
        }
        result.push_back(value);
    }

    return result;
}

BOOL ExtractLoginKeyFromPacket(const BYTE* pData, DWORD len) {
    if (len < 12) return FALSE;

    std::string hexPacket = HexToString(pData, len, false);
    for (auto& c : hexPacket) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }

    SetLoginKey(Utf8ToWide(hexPacket));
    return TRUE;
}

std::wstring BuildLoginUrl(const std::wstring& hexPacket) {
    const size_t PREFIX_LEN = 28;
    if (hexPacket.length() <= PREFIX_LEN) {
        return L"";
    }

    std::wstring keyHex = hexPacket.substr(PREFIX_LEN);
    std::string keyText;
    keyText.reserve(keyHex.length() / 2);
    for (size_t i = 0; i + 1 < keyHex.length(); i += 2) {
        BYTE b = 0;
        if (!TryParseHexBytePair(static_cast<char>(keyHex[i]), static_cast<char>(keyHex[i + 1]), b)) {
            return L"";
        }
        keyText.push_back(static_cast<char>(b));
    }

    return L"http://enter.wanwan4399.com/bin-debug/KBgameindex.html?" + Utf8ToWide(keyText);
}

std::string GetPacketLabel(uint32_t opcode, bool bSend) {
    (void)bSend;
    for (const auto& entry : g_packetLabelEntries) {
        if (entry.opcode == opcode) {
            return entry.label;
        }
    }
    return "";
}
