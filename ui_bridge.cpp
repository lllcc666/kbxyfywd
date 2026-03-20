/**
 * @file ui_bridge.cpp
 * @brief UI 桥接器实现
 * 
 * 统一管理所有 JavaScript 交互
 */

#include "ui_bridge.h"
#include <cstdarg>
#include <sstream>

// Windows 自定义消息
#define WM_EXECUTE_JS (WM_USER + 101)

// 外部声明：执行 JavaScript 的函数（在 demo.cpp 中定义）
extern HRESULT WINAPI ExecuteScriptInWebView2(const WCHAR* script);
extern HWND g_hWnd;

// ============================================================================
// 单例实现
// ============================================================================

UIBridge& UIBridge::Instance() {
    static UIBridge instance;
    return instance;
}

// ============================================================================
// 初始化
// ============================================================================

void UIBridge::Initialize(HWND hWnd) {
    m_hwnd = hWnd;
}

// ============================================================================
// 常用 UI 更新方法
// ============================================================================

void UIBridge::UpdateHelperText(const std::wstring& text) {
    std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('" + 
                          EscapeJsonString(text) + L"'); }";
    ExecuteJS(script);
}

void UIBridge::UpdateProgress(int current, int total, const std::wstring& prefix) {
    std::wstring script;
    if (prefix.empty()) {
        script = L"if(window.updateProgress) { window.updateProgress(" + 
                  std::to_wstring(current) + L", " + std::to_wstring(total) + L"); }";
    } else {
        script = L"if(window.updateProgress) { window.updateProgress(" + 
                  std::to_wstring(current) + L", " + std::to_wstring(total) + L", '" + 
                  EscapeJsonString(prefix) + L"'); }";
    }
    ExecuteJS(script);
}

void UIBridge::NotifyTaskComplete(const std::wstring& taskName, bool success, const std::wstring& message) {
    std::wstring script = L"if(window.onTaskComplete) { window.onTaskComplete('" + 
                          EscapeJsonString(taskName) + L"', " + 
                          (success ? L"true" : L"false") + L", '" +
                          EscapeJsonString(message) + L"'); }";
    ExecuteJS(script);
}

void UIBridge::ShowDialog(const std::wstring& type, const std::wstring& message) {
    std::wstring script = L"if(window.showDialog) { window.showDialog('" + 
                          EscapeJsonString(type) + L", '" + 
                          EscapeJsonString(message) + L"'); }";
    ExecuteJS(script);
}

void UIBridge::UpdatePacketCount(DWORD count) {
    std::wstring script = L"if(window.updatePacketCount) { window.updatePacketCount(" + 
                          std::to_wstring(count) + L"); }";
    ExecuteJS(script);
}

void UIBridge::UpdateMuteButtonState(bool muted) {
    std::wstring script = L"if(window.updateMuteButtonState) { window.updateMuteButtonState(" + 
                          std::wstring(muted ? L"true" : L"false") + L"); }";
    ExecuteJS(script);
}

// ============================================================================
// 通用 JavaScript 执行方法
// ============================================================================

void UIBridge::ExecuteJS(const std::wstring& script) {
    if (script.empty()) return;

    // 分配内存并复制字符串
    wchar_t* pScript = new (std::nothrow) wchar_t[script.length() + 1];
    if (!pScript) return;

    wcscpy_s(pScript, script.length() + 1, script.c_str());

    // 使用 PostMessage 异步执行（线程安全）
    if (m_hwnd) {
        if (!PostMessage(m_hwnd, WM_EXECUTE_JS, 0, (LPARAM)pScript)) {
            // PostMessage 失败，释放内存
            delete[] pScript;
        }
    } else {
        // 窗口句柄未初始化，释放内存
        delete[] pScript;
    }
}

void UIBridge::CallJSFunction(const std::wstring& funcName, const std::wstring& args) {
    std::wstring script = L"if(window." + funcName + L") { window." + funcName + L"(" + args + L"); }";
    ExecuteJS(script);
}

void UIBridge::CallJSFunctionWithCheck(const std::wstring& funcName, const std::wstring& args) {
    std::wstring script = L"if(typeof window." + funcName + L" === 'function') { window." + funcName + L"(" + args + L"); }";
    ExecuteJS(script);
}

// ============================================================================
// 辅助方法
// ============================================================================

std::wstring UIBridge::EscapeJsonString(const std::wstring& input) {
    std::wstring result;
    result.reserve(input.length() * 2); // 预留更多空间，因为转义字符会增加长度
    
    for (wchar_t c : input) {
        switch (c) {
            case L'"':  result += L"\\\""; break;
            case L'\\': result += L"\\\\"; break;
            case L'\n': result += L"\\n"; break;
            case L'\r': result += L"\\r"; break;
            case L'\t': result += L"\\t"; break;
            case L'\b': result += L"\\b"; break;
            case L'\f': result += L"\\f"; break;
            default:    
                // 对于控制字符（小于0x20），使用\uXXXX格式
                if (c < 0x20) {
                    wchar_t buf[8];
                    swprintf_s(buf, L"\\u%04x", static_cast<unsigned int>(c));
                    result += buf;
                } else {
                    result += c; 
                }
                break;
        }
    }
    return result;
}

std::wstring UIBridge::FormatString(const wchar_t* format, ...) {
    if (!format) return L"";

    va_list args;
    va_start(args, format);

    // 计算所需长度
    int len = _vscwprintf(format, args);
    if (len <= 0) {
        va_end(args);
        return L"";
    }

    // 分配缓冲区
    std::wstring result(len + 1, L'\0');
    vswprintf_s(&result[0], result.length(), format, args);
    result.resize(len);  // 移除末尾的 null

    va_end(args);
    return result;
}
