/**
 * @file ui_bridge.h
 * @brief UI 桥接器 - 统一 JavaScript 交互接口
 * 
 * 提供简化的 JavaScript 调用接口，自动处理内存管理和消息发送
 * 重构目标：减少 70% 的 JS 调用重复代码
 */

#pragma once

#include <windows.h>
#include <string>
#include <functional>

/**
 * @class UIBridge
 * @brief UI 桥接器单例类
 * 
 * 统一管理所有 JavaScript 交互，提供类型安全的接口
 * 自动管理内存，防止泄漏
 */
class UIBridge {
public:
    /**
     * @brief 获取单例实例
     */
    static UIBridge& Instance();

    /**
     * @brief 初始化桥接器
     * @param hWnd 主窗口句柄
     */
    void Initialize(HWND hWnd);

    /**
     * @brief 检查是否已初始化
     */
    bool IsInitialized() const { return m_hwnd != nullptr; }

    // ================================
    // 常用 UI 更新方法
    // ================================

    /**
     * @brief 更新辅助文本（显示在界面上的提示信息）
     * @param text 文本内容
     */
    void UpdateHelperText(const std::wstring& text);

    /**
     * @brief 更新进度显示
     * @param current 当前进度
     * @param total 总数
     * @param prefix 前缀文本（可选）
     */
    void UpdateProgress(int current, int total, const std::wstring& prefix = L"");

    /**
     * @brief 通知任务完成
     * @param taskName 任务名称
     * @param success 是否成功
     * @param message 额外消息（可选）
     */
    void NotifyTaskComplete(const std::wstring& taskName, bool success, const std::wstring& message = L"");

    /**
     * @brief 显示对话框
     * @param type 对话框类型（info, warning, error）
     * @param message 消息内容
     */
    void ShowDialog(const std::wstring& type, const std::wstring& message);

    /**
     * @brief 更新封包计数
     * @param count 封包数量
     */
    void UpdatePacketCount(DWORD count);

    /**
     * @brief 更新静音按钮状态
     * @param muted 是否静音
     */
    void UpdateMuteButtonState(bool muted);

    // ================================
    // 通用 JavaScript 执行方法
    // ================================

    /**
     * @brief 执行 JavaScript 代码（异步，自动内存管理）
     * @param script JavaScript 代码
     */
    void ExecuteJS(const std::wstring& script);

    /**
     * @brief 调用 JavaScript 函数
     * @param funcName 函数名
     * @param args 参数列表（JSON 格式）
     */
    void CallJSFunction(const std::wstring& funcName, const std::wstring& args = L"");

    /**
     * @brief 调用带回调的 JavaScript 函数
     * @param funcName 函数名
     * @param args 参数列表
     */
    void CallJSFunctionWithCheck(const std::wstring& funcName, const std::wstring& args = L"");

    // ================================
    // 辅助方法
    // ================================

    /**
     * @brief JSON 字符串转义
     * @param input 原始字符串
     * @return 转义后的字符串
     */
    static std::wstring EscapeJsonString(const std::wstring& input);

    /**
     * @brief 格式化简单消息
     * @param format 格式字符串
     * @param ... 可变参数
     * @return 格式化后的字符串
     */
    static std::wstring FormatString(const wchar_t* format, ...);

private:
    UIBridge() = default;
    ~UIBridge() = default;
    
    // 禁止拷贝
    UIBridge(const UIBridge&) = delete;
    UIBridge& operator=(const UIBridge&) = delete;

    HWND m_hwnd = nullptr;  ///< 主窗口句柄
};

// ================================
// 便捷宏定义（可选使用）
// ================================

/**
 * @brief 快速更新辅助文本
 * 使用示例：UI_UPDATE_TEXT(L"正在执行任务...")
 */
#define UI_UPDATE_TEXT(text) UIBridge::Instance().UpdateHelperText(text)

/**
 * @brief 快速更新进度
 * 使用示例：UI_UPDATE_PROGRESS(5, 10)
 */
#define UI_UPDATE_PROGRESS(current, total) UIBridge::Instance().UpdateProgress(current, total)

/**
 * @brief 快速通知任务完成
 */
#define UI_TASK_COMPLETE(name, success) UIBridge::Instance().NotifyTaskComplete(name, success)
