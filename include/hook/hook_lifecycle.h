#pragma once
#include <windows.h>
#include <cstdint>

typedef HRESULT (*EXECUTE_SCRIPT_FUNC)(const WCHAR* script);

namespace WpeHook {
constexpr wchar_t APP_VERSION_W[] = L"1.18";
constexpr wchar_t APP_WINDOW_TITLE_W[] = L"卡布西游浮影微端 V1.18";
constexpr int DANCE_MAX_DAILY_COUNT = 3;
constexpr int DEEP_DIG_DEFAULT_COUNT = 2;
constexpr DWORD TIMEOUT_RESPONSE = 3000;
constexpr DWORD TIMEOUT_SEND = 5000;
constexpr DWORD TIMEOUT_RETRY_INTERVAL = 500;
}

BOOL InitializeHooks();
VOID UninitializeHooks();
VOID StartIntercept();
VOID StopIntercept();
VOID SetInterceptType(BOOL bSend, BOOL bRecv);
void SetExecuteScriptFunction(EXECUTE_SCRIPT_FUNC func);
