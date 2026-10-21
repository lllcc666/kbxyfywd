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
