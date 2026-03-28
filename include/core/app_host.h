#pragma once

#include <windows.h>

#include <string>

namespace AppHost {

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
