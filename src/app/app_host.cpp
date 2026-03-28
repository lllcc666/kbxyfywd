#include "app_host.h"

#include <windows.h>
#include <exdisp.h>
#include <atlbase.h>

#include <string>

extern CComPtr<IWebBrowser2> g_pWebBrowser;
extern HWND g_hWnd;
extern HWND g_hwnd_webbrowser;
extern bool g_is_packet_window_visible;
extern RECT g_packet_window_rect;

using SpeedhackInitializeFunc = void (__stdcall *)(float);
extern SpeedhackInitializeFunc g_pfnInitializeSpeedhack;

bool LoadSpeedhackFromMemory();
bool ToggleProgramVolume();
bool ClearIECache();
HRESULT WINAPI ExecuteScriptInWebView2(const WCHAR* script);
void UpdateIEEWindowRegion();
std::wstring ShowSaveFileDialog(const std::wstring& initialDir = L"",
                                const std::wstring& defaultFileName = L"",
                                const std::wstring& filter = L"");
std::wstring ShowOpenFileDialog(const std::wstring& initialDir = L"",
                                const std::wstring& filter = L"");

namespace AppHost {

void BeginWindowDrag() {
    if (!g_hWnd) {
        return;
    }

    ReleaseCapture();
    SendMessage(g_hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
}

void MinimizeMainWindow() {
    if (g_hWnd) {
        ShowWindow(g_hWnd, SW_MINIMIZE);
    }
}

void CloseMainWindow() {
    if (g_hWnd) {
        PostMessage(g_hWnd, WM_CLOSE, 0, 0);
    }
}

void ShowBrowserWindow(bool show) {
    if (g_hwnd_webbrowser) {
        ShowWindow(g_hwnd_webbrowser, show ? SW_SHOW : SW_HIDE);
    }
}

void NavigateBrowser(const std::wstring& url) {
    if (!g_pWebBrowser || url.empty()) {
        return;
    }

    VARIANT vEmpty;
    VariantInit(&vEmpty);
    BSTR bstrURL = SysAllocString(url.c_str());
    if (!bstrURL) {
        return;
    }

    g_pWebBrowser->Navigate(bstrURL, &vEmpty, &vEmpty, &vEmpty, &vEmpty);
    SysFreeString(bstrURL);
}

void RefreshBrowser() {
    if (g_pWebBrowser) {
        g_pWebBrowser->Refresh();
    }
}

bool ToggleProgramVolume() {
    return ::ToggleProgramVolume();
}

bool ClearIECache() {
    return ::ClearIECache();
}

bool ApplySpeedhack(float speed) {
    if (!LoadSpeedhackFromMemory() || !g_pfnInitializeSpeedhack) {
        return false;
    }

    g_pfnInitializeSpeedhack(speed);
    return true;
}

void ExecuteScript(const std::wstring& script) {
    if (!script.empty()) {
        ExecuteScriptInWebView2(script.c_str());
    }
}

void SetPacketWindowState(bool visible, const RECT& rect) {
    g_is_packet_window_visible = visible;
    if (visible) {
        g_packet_window_rect = rect;
    }
}

void SetPacketWindowVisible(bool visible) {
    g_is_packet_window_visible = visible;
}

bool IsPacketWindowVisible() {
    return g_is_packet_window_visible;
}

void UpdateBrowserRegion() {
    UpdateIEEWindowRegion();
}

std::wstring ShowSaveFileDialog(const std::wstring& initialDir,
                                const std::wstring& defaultFileName,
                                const std::wstring& filter) {
    return ::ShowSaveFileDialog(initialDir, defaultFileName, filter);
}

std::wstring ShowOpenFileDialog(const std::wstring& initialDir,
                                const std::wstring& filter) {
    return ::ShowOpenFileDialog(initialDir, filter);
}

}  // namespace AppHost
