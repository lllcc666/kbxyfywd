#include <windows.h>

#include <atomic>
#include <string>
#include <vector>

#include "byte_reader.h"
#include "hook_shared.h"
#include "managed_thread.h"
#include "packet_builder.h"
#include "packet_parser.h"
#include "packet_protocol.h"
#include "spirit_collect.h"
#include "ui_bridge.h"
#include "utils.h"
#include "wpe_hook.h"

// ============================================================================
// 精魄赠送系统功能实现 (SpiritCollect)
// ============================================================================

// 精魄系统全局状态
SpiritCollectState g_spiritCollectState;

/** 获取精魄列表 */
BOOL SendSpiritPresuresPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(SpiritCollect::PRESURES_SEND)
        .SetParams(0)
        .Build();
    return SendPacket(g_LastGameSocket, packet.data(), static_cast<DWORD>(packet.size()));
}

/** 验证玩家信息（通过卡布号） */
BOOL SendSpiritPlayerInfoPacket(uint32_t friendId) {
    auto packet = PacketBuilder()
        .SetOpcode(SpiritCollect::PLAYER_INFO_SEND)
        .SetParams(friendId)
        .Build();
    return SendPacket(g_LastGameSocket, packet.data(), static_cast<DWORD>(packet.size()));
}

/** 发送精魄给指定玩家 */
BOOL SendSpiritGiftPacket(uint32_t friendId, uint32_t eggId) {
    auto packet = PacketBuilder()
        .SetOpcode(SpiritCollect::SEND_SPIRIT_SEND)
        .SetParams(friendId)
        .WriteInt32(eggId)
        .Build();
    return SendPacket(g_LastGameSocket, packet.data(), static_cast<DWORD>(packet.size()));
}

/** 打开精魄系统 - 发送 open_ui 请求 */
BOOL SendSpiritOpenUIPacket() {
    auto packet = BuildActivityPacket(
        SpiritCollect::COLLECT_SEND,
        SpiritCollect::ACT_ID,
        "open_ui",
        {}
    );
    return SendPacket(g_LastGameSocket, packet.data(), static_cast<DWORD>(packet.size()));
}

/** 获取历史记录 */
BOOL SendSpiritHistoryPacket(int type) {
    auto packet = BuildActivityPacket(
        SpiritCollect::COLLECT_SEND,
        SpiritCollect::ACT_ID,
        "history",
        {type}
    );
    return SendPacket(g_LastGameSocket, packet.data(), static_cast<DWORD>(packet.size()));
}



// ============================================================================
// 精魄系统响应处理
// ============================================================================

/**
 * @brief 解析精魄列表响应
 * @param packet 响应封包
 */
void ProcessSpiritPresuresResponse(const GamePacket& packet) {
    size_t offset = 0;
    ByteReader reader(packet.body, offset);
    const int32_t spiritCount = static_cast<int32_t>(packet.params);

    g_spiritCollectState.spiritList.clear();

    for (int i = 0; i < spiritCount && offset + 24 <= packet.body.size(); i++) {
        SpiritInfo spirit;
        spirit.eggId = reader.I32();
        spirit.eggIid = reader.I32();
        spirit.bornTime = reader.I32();
        spirit.needTime = reader.I32();
        spirit.eggType = reader.I32();
        spirit.character = reader.I32();

        if (offset + 4 > packet.body.size()) {
            break;
        }

        int32_t skillCount = reader.I32();
        if (skillCount < 0) {
            break;
        }

        if (offset + static_cast<size_t>(skillCount) * 4 > packet.body.size()) {
            break;
        }

        spirit.skillList.reserve(static_cast<size_t>(skillCount));
        for (int32_t j = 0; j < skillCount; ++j) {
            spirit.skillList.push_back(static_cast<uint32_t>(reader.I32()));
        }

        spirit.name = GetPetName(static_cast<int>(spirit.eggIid));
        if (spirit.name.empty()) spirit.name = L"妖怪ID " + std::to_wstring(spirit.eggIid);

        spirit.elem = 0;
        const int elemId = GetPetElem(static_cast<int>(spirit.eggIid));
        if (elemId != 0) {
            spirit.elem = static_cast<uint32_t>(elemId);
            spirit.elemName = GetElemName(elemId);
            if (spirit.elemName.empty()) spirit.elemName = L"未知系别";
        } else {
            spirit.elemName = L"未知系别";
        }

        if (spirit.character > 0) {
            spirit.characterName = GetGeniusName(static_cast<int>(spirit.character));
            if (spirit.characterName.empty()) spirit.characterName = L"未知性格";
        } else {
            spirit.characterName = L"未知性格";
        }

        spirit.skillNames.reserve(spirit.skillList.size());
        for (uint32_t skillId : spirit.skillList) {
            std::wstring skillName = GetSkillName(static_cast<int>(skillId));
            if (skillName.empty()) skillName = L"技能" + std::to_wstring(skillId);
            spirit.skillNames.push_back(std::move(skillName));
        }

        g_spiritCollectState.spiritList.push_back(spirit);
    }

    std::wstring jsCode = L"if(window.handleSpiritCollectData) { window.handleSpiritCollectData({";
    jsCode += L"type: 'spiritList',";
    jsCode += L"data: [";

    for (size_t i = 0; i < g_spiritCollectState.spiritList.size(); i++) {
        const auto& spirit = g_spiritCollectState.spiritList[i];
        jsCode += L"{eggId: " + std::to_wstring(spirit.eggId) + L",";
        jsCode += L"eggIid: " + std::to_wstring(spirit.eggIid) + L",";
        jsCode += L"eggType: " + std::to_wstring(spirit.eggType) + L",";
        jsCode += L"bornTime: " + std::to_wstring(spirit.bornTime) + L",";
        jsCode += L"needTime: " + std::to_wstring(spirit.needTime) + L",";
        jsCode += L"character: " + std::to_wstring(spirit.character) + L",";
        jsCode += L"elem: " + std::to_wstring(spirit.elem) + L",";
        jsCode += L"name: '" + UIBridge::EscapeJsonString(spirit.name) + L"',";
        jsCode += L"elemName: '" + UIBridge::EscapeJsonString(spirit.elemName) + L"',";
        jsCode += L"characterName: '" + UIBridge::EscapeJsonString(spirit.characterName) + L"',";
        jsCode += L"skillList: [";
        for (size_t j = 0; j < spirit.skillList.size(); ++j) {
            jsCode += std::to_wstring(spirit.skillList[j]);
            if (j < spirit.skillList.size() - 1) {
                jsCode += L",";
            }
        }
        jsCode += L"],";
        jsCode += L"skillNames: [";
        for (size_t j = 0; j < spirit.skillNames.size(); ++j) {
            jsCode += L"'" + UIBridge::EscapeJsonString(spirit.skillNames[j]) + L"'";
            if (j < spirit.skillNames.size() - 1) {
                jsCode += L",";
            }
        }
        jsCode += L"]}";
        if (i < g_spiritCollectState.spiritList.size() - 1) {
            jsCode += L",";
        }
    }

    jsCode += L"]}); }";
    PostScriptToUI(jsCode);
}

/**
 * @brief 处理精魄系统活动协议响应
 * @param packet 响应封包
 */
void ProcessSpiritCollectResponse(const GamePacket& packet) {
    if (packet.params != SpiritCollect::ACT_ID) return;
    if (packet.body.size() < 2) return;

    const BYTE* body = packet.body.data();
    size_t offset = 0;
    ByteReader reader(packet.body, offset);
    std::string oper;

    if (!ReadPacketString(body, packet.body.size(), offset, oper)) return;

    if (oper == "open_ui") {
        if (offset + 24 > packet.body.size()) return;

        int32_t love = reader.I32();
        g_spiritCollectState.weeklyOutLimit = reader.I32();
        int32_t monthlyOut = reader.I32();
        int32_t weeklyIn = reader.I32();
        int32_t monthlyIn = reader.I32();
        g_spiritCollectState.dailyOutLimit = reader.I32();

        std::wstring jsCode = L"if(window.handleSpiritCollectData) { window.handleSpiritCollectData({";
        jsCode += L"type: 'spiritState',";
        jsCode += L"data: {dOut: " + std::to_wstring(g_spiritCollectState.dailyOutLimit) + L",";
        jsCode += L"wOut: " + std::to_wstring(g_spiritCollectState.weeklyOutLimit) + L",";
        jsCode += L"love: " + std::to_wstring(love) + L",";
        jsCode += L"mOut: " + std::to_wstring(monthlyOut) + L",";
        jsCode += L"wIn: " + std::to_wstring(weeklyIn) + L",";
        jsCode += L"mIn: " + std::to_wstring(monthlyIn) + L"}}); }";
        PostScriptToUI(jsCode);
        return;
    }

    if (oper == "history") {
        if (offset + 4 > packet.body.size()) return;

        int32_t recordType = reader.I32();
        std::string jsonText;
        if (!ReadPacketString(body, packet.body.size(), offset, jsonText)) {
            jsonText.clear();
        }

        std::wstring jsCode = L"if(window.handleSpiritCollectData) { window.handleSpiritCollectData({";
        jsCode += L"type: 'history',";
        jsCode += L"recordType: " + std::to_wstring(recordType) + L",";
        jsCode += L"json: '" + UIBridge::EscapeJsonString(Utf8ToWide(jsonText)) + L"'}); }";
        PostScriptToUI(jsCode);
        return;
    }
}

/**
 * @brief 处理发送精魄响应
 * @param packet 响应封包
 */
void ProcessSpiritSendSpiritResponse(const GamePacket& packet) {
    const int32_t result = static_cast<int32_t>(packet.params);

    if (result == 0) {
        NotifySpiritAlert(L"精魄赠送失败，请稍后重试");
        return;
    }

    if (result != 1) {
        NotifySpiritAlert(L"精魄赠送失败，返回状态异常: " + std::to_wstring(result));
        return;
    }

    const BYTE* body = packet.body.data();
    size_t offset = 0;
    ByteReader reader(packet.body, offset);
    if (offset + 8 > packet.body.size()) {
        NotifySpiritSuccess(L"精魄赠送成功");
        return;
    }

    reader.I32();
    reader.I32();

    std::string friendNameUtf8;
    if (ReadPacketString(body, packet.body.size(), offset, friendNameUtf8)) {
        NotifySpiritSuccess(L"精魄已成功赠送给【" + Utf8ToWide(friendNameUtf8) + L"】");
    } else {
        NotifySpiritSuccess(L"精魄赠送成功");
    }
}

/**
 * @brief 处理验证玩家信息响应
 * @param packet 响应封包
 */
void ProcessSpiritPlayerInfoResponse(const GamePacket& packet) {
    if (packet.body.size() < 4) return;

    const BYTE* body = packet.body.data();
    size_t offset = 0;
    ByteReader reader(packet.body, offset);

    int32_t result = reader.I32();

    if (result == 1) {
        std::string nameUtf8;
        if (ReadPacketString(body, packet.body.size(), offset, nameUtf8)) {
            NotifySpiritConfirm(Utf8ToWide(nameUtf8));
        } else {
            NotifySpiritAlert(L"玩家信息返回不完整");
        }
        return;
    }

    if (result == 0) {
        NotifySpiritAlert(L"该卡布号玩家不存在");
        return;
    }

    if (result == 2) {
        NotifySpiritAlert(L"该卡布号玩家不在线");
        return;
    }

    NotifySpiritAlert(L"验证玩家信息失败，返回状态异常: " + std::to_wstring(result));
}
