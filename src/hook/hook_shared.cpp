#include "byte_reader.h"
#include "hook_shared.h"
#include "managed_thread.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "packet_protocol.h"
#include "ui_bridge.h"

bool ReadPacketString(const BYTE* body, size_t bodySize, size_t& offset, std::string& out) {
    ByteReader reader(body, bodySize, offset);
    if (!body || offset + 2 > bodySize) {
        return false;
    }

    uint16_t len = reader.U16();
    if (offset + len > bodySize) {
        return false;
    }

    out.assign(reinterpret_cast<const char*>(body + offset), len);
    offset += len;
    return true;
}

void ReportActivityCount(uint32_t activityId, int remainingCount) {
    wchar_t jsCode[256];
    swprintf_s(
        jsCode,
        L"if(window.updateActivityCounts) { window.updateActivityCounts({ %u: %d }); }",
        activityId,
        remainingCount);
    UIBridge::Instance().ExecuteJS(jsCode);
}

namespace {

// 三条精魄提示除 type 与载荷字段名外完全相同，字段名只有 confirm 用 playerName。
void NotifySpirit(const wchar_t* type, const wchar_t* field, const std::wstring& value) {
    PostScriptToUI(std::wstring(L"if(window.handleSpiritCollectData) { window.handleSpiritCollectData({\"type\":\"") +
                   type + L"\",\"" + field + L"\":\"" +
                   UIBridge::EscapeJsonString(value) + L"\"}); }");
}

}  // namespace

void NotifySpiritAlert(const std::wstring& message) {
    NotifySpirit(L"alert", L"message", message);
}

void NotifySpiritSuccess(const std::wstring& message) {
    NotifySpirit(L"sendSuccess", L"message", message);
}

void NotifySpiritConfirm(const std::wstring& playerName) {
    NotifySpirit(L"confirm", L"playerName", playerName);
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
