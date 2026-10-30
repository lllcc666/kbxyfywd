#pragma once
#include <windows.h>
#include <cstdint>

namespace WpeHook {
// 版本号的唯一真相源：字符串形态给窗口标题/关于框，数值形态给更新检查比较。
// 以前 demo.cpp 另写了一个 constexpr float CURRENT_VERSION = 1.18f，
// 靠注释「须与 APP_VERSION_W 保持一致」维系，改版本时漏改一处就是
// 「提示有新版本但下载下来的还是旧的」这类难查的问题。
constexpr wchar_t APP_VERSION_W[] = L"1.18";
constexpr float APP_VERSION_NUM = 1.18f;
constexpr wchar_t APP_WINDOW_TITLE_W[] = L"卡布西游浮影微端 V" L"1.18";
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
