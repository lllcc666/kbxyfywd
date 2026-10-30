#pragma once

#include <windows.h>

#include <string>

namespace AppHost {

/// 主窗口类名：demo.cpp 注册窗口与 app_host.cpp 兜底 FindWindow 共用一份。
inline constexpr wchar_t kMainWindowClass[] = L"WebView2DemoWindowClass";

/// 登录页地址：启动导航与「重新登录」按钮共用一份。
inline constexpr wchar_t kLoginUrl[] = L"http://news.4399.com/login/kbxy.html";

/// 自绘标题栏高度（像素）：WM_NCHITTEST 判定、IE 控件区域计算、封包窗口打孔三处共用。
inline constexpr int kTitleBarHeight = 32;

/// 主窗口句柄唯一访问点（替代各模块手写 `extern HWND g_hWnd`）。
HWND GetMainWindow();

void BeginWindowDrag();
void MinimizeMainWindow();
void CloseMainWindow();

void ShowBrowserWindow(bool show);
void NavigateBrowser(const std::wstring& url);
void RefreshBrowser();

bool ToggleProgramVolume();
bool ClearIECache();
bool ApplySpeedhack(float speed);

void ExecuteScript(const std::wstring& script);

void SetPacketWindowState(bool visible, const RECT& rect);
void SetPacketWindowVisible(bool visible);
bool IsPacketWindowVisible();
void UpdateBrowserRegion();

std::wstring ShowSaveFileDialog(const std::wstring& initialDir = L"",
                                const std::wstring& defaultFileName = L"",
                                const std::wstring& filter = L"");
std::wstring ShowOpenFileDialog(const std::wstring& initialDir = L"",
                                const std::wstring& filter = L"");

}  // namespace AppHost
