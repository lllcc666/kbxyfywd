#pragma once
#include <windows.h>
#include <string>

namespace AppHost {

/// 宿主能力接口：UI 线程/主窗口相关操作的最小依赖面。
/// 唯一实现是桌面窗口（DesktopAppHost），调用方只依赖本接口。
class IAppHost {
public:
    virtual ~IAppHost() = default;
    virtual HWND GetMainWindow() = 0;
    virtual void ExecuteScript(const std::wstring& script) = 0;
    virtual void ShowBrowserWindow(bool show) = 0;
    virtual bool IsPacketWindowVisible() = 0;
};

IAppHost& Instance();

}  // namespace AppHost
