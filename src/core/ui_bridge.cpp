/**
 * @file ui_bridge.cpp
 * @brief UI 桥接器实现
 * 
 * 统一管理所有 JavaScript 交互
 */

#include "ui_bridge.h"

// Windows 自定义消息
#include "window_messages.h"

// 外部声明：执行 JavaScript 的函数（在 demo.cpp 中定义）

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

void UIBridge::UpdatePacketCount(DWORD count) {
    std::wstring script = L"if(window.updatePacketCount) { window.updatePacketCount(" + 
                          std::to_wstring(count) + L"); }";
    ExecuteJS(script);
}

// ============================================================================
// 通用 JavaScript 执行方法
// ============================================================================

void UIBridge::ExecuteJS(const std::wstring& script) {
    if (script.empty() || !m_hwnd) return;

    const size_t len = script.length() + 1;
    wchar_t* pScript = new (std::nothrow) wchar_t[len];
    if (!pScript) return;

    wmemcpy_s(pScript, len, script.c_str(), len);

    if (!PostMessage(m_hwnd, AppMessage::kExecuteJs, 0, (LPARAM)pScript)) {
        delete[] pScript;
    }
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
            case L'\'': result += L"\\'"; break;
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
