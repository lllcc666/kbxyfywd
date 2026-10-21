#include "hook_shared.h"
#include "managed_thread.h"

#include <string>
#include <utility>
#include <vector>

#include "packet_builder.h"
#include "packet_protocol.h"
#include "ui_bridge.h"

extern bool PostScriptToUI(const std::wstring& jsCode);

bool ReadPacketString(const BYTE* body, size_t bodySize, size_t& offset, std::string& out) {
    if (!body || offset + 2 > bodySize) {
        return false;
    }

    uint16_t len = ReadUInt16LE(body, offset);
    if (offset + len > bodySize) {
        return false;
    }

    out.assign(reinterpret_cast<const char*>(body + offset), len);
    offset += len;
    return true;
}

BOOL SendActivityPacket(uint32_t activityId, const std::string& operation,
                        const std::vector<int32_t>& bodyValues) {
    std::vector<BYTE> packet =
        BuildActivityPacket(Opcode::ACTIVITY_QINGYANG_NEW_SEND, activityId, operation, bodyValues);
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()),
                      Opcode::ACTIVITY_QUERY_BACK, WpeHook::TIMEOUT_RESPONSE,
                      activityId, true);
}


void NotifySpiritAlert(const std::wstring& message) {
    PostScriptToUI(L"if(window.handleSpiritCollectData) { window.handleSpiritCollectData({\"type\":\"alert\",\"message\":\"" +
                   UIBridge::EscapeJsonString(message) + L"\"}); }");
}

void NotifySpiritSuccess(const std::wstring& message) {
    PostScriptToUI(L"if(window.handleSpiritCollectData) { window.handleSpiritCollectData({\"type\":\"sendSuccess\",\"message\":\"" +
                   UIBridge::EscapeJsonString(message) + L"\"}); }");
}

void NotifySpiritConfirm(const std::wstring& playerName) {
    PostScriptToUI(L"if(window.handleSpiritCollectData) { window.handleSpiritCollectData({\"type\":\"confirm\",\"playerName\":\"" +
                   UIBridge::EscapeJsonString(playerName) + L"\"}); }");
}

void BuildAct757AwardList(int medal, int exp, int coin,
                          const std::vector<std::pair<int, int>>& rewards,
                          std::vector<std::pair<int, int>>& awardList) {
    int practiceNum = 0;
    std::vector<std::pair<int, int>> tempList;
    awardList.clear();
    awardList.emplace_back(0, medal);
    awardList.emplace_back(202, exp);
    awardList.emplace_back(201, coin);
    for (const auto& reward : rewards) {
        const int id = reward.first;
        const int num = reward.second;
        if (num <= 0) {
            continue;
        }
        switch (id) {
            case 208:
            case 209:
            case 210:
            case 211:
            case 212:
            case 213:
                practiceNum += num;
                break;
            default:
                tempList.emplace_back(id, num);
                break;
        }
    }
    awardList.emplace_back(203, practiceNum);
    awardList.insert(awardList.end(), tempList.begin(), tempList.end());
}

void BuildAct822AwardList(int medal, int exp, int coin,
                          const std::vector<std::pair<int, int>>& rewards,
                          std::vector<std::pair<int, int>>& awardList) {
    awardList.clear();
    awardList.emplace_back(0, medal);
    awardList.emplace_back(202, exp);
    awardList.emplace_back(201, coin);
    for (const auto& reward : rewards) {
        if (reward.second > 0) {
            awardList.emplace_back(reward.first, reward.second);
        }
    }
}

int FindAwardAmount(const std::vector<std::pair<int, int>>& awardList, int itemId) {
    for (const auto& award : awardList) {
        if (award.first == itemId) {
            return award.second;
        }
    }
    return 0;
}

bool StartManagedThreadOnce(const wchar_t* name, LPTHREAD_START_ROUTINE body, LPVOID param) {
    if (body == nullptr) {
        return false;
    }
    const std::wstring threadName = (name != nullptr) ? name : L"once";
    // 启动失败时 param 由调用方负责（与原 CreateThread 失败路径一致）。
    return ManagedThread::Start(threadName, [body, param]() {
        body(param);
    });
}
