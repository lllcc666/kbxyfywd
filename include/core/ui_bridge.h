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
     * @brief 更新封包计数
     * @param count 封包数量
     */
    void UpdatePacketCount(DWORD count);

    // ================================
    // 通用 JavaScript 执行方法
    // ================================

    /**
     * @brief 执行 JavaScript 代码（异步，自动内存管理）
     * @param script JavaScript 代码
     */
    void ExecuteJS(const std::wstring& script);

    // ================================
    // 辅助方法
    // ================================

    /**
     * @brief JSON 字符串转义
     * @param input 原始字符串
     * @return 转义后的字符串
     */
    static std::wstring EscapeJsonString(const std::wstring& input);

private:
    UIBridge() = default;
    ~UIBridge() = default;
    
    // 禁止拷贝
    UIBridge(const UIBridge&) = delete;
    UIBridge& operator=(const UIBridge&) = delete;

    HWND m_hwnd = nullptr;  ///< 主窗口句柄
};

// ================================
// 脚本下发入口
// ================================

/**
 * @brief 向 UI 线程安全地下发一段 JavaScript。
 *
 * 内部走 UIBridge 单例；未初始化或脚本为空时返回 false。
 * 原先有 24 个 cpp 各自手写一遍这个 extern 声明，统一收到这里。
 */
bool PostScriptToUI(const std::wstring& jsCode);
