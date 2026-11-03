/**
 * @file ui_bridge.h
 * @brief UI 桥接器 - 统一 JavaScript 交互接口
 * 
 * 提供简化的 JavaScript 调用接口，自动处理内存管理和消息发送
 * 重构目标：减少 70% 的 JS 调用重复代码
 */

#pragma once

#include <windows.h>
#include <cstddef>
#include <mutex>
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
    // 封包列表批量投递
    // ================================

    /**
     * @brief 一条待显示的封包记录（不含 JS，只描述数据）。
     *
     * 由 wpe_hook 侧填好后入队，UIBridge 负责拼成一次批量脚本。
     * 字段保持纯数据，避免把封包语义泄漏进 UI 桥接层。
     */
    struct PacketUiEntry {
        std::size_t index = 0;    ///< 1 基序号，同时作为列表显示序号
        bool bSend = false;
        std::wstring hex;
        std::wstring time;
        std::wstring label;
    };

    /**
     * @brief 入队一条封包记录（任意线程调用）。
     *
     * 只在队列为空且无待处理刷新时才 PostMessage 唤醒 UI 线程，
     * 因此高频调用下每次只付出一次字符串拼接的成本。
     */
    void QueuePacketEntry(PacketUiEntry entry);

    /**
     * @brief UI 线程收到 kFlushPacketEntries 后调用：把已入队的记录合成一次脚本下发。
     */
    void FlushPacketEntries();

    /**
     * @brief 丢弃尚未下发的封包记录，并递增列表世代号。
     *
     * 整表重建（切换窗口 / 删除 / 淘汰）之前必须调用，
     * 否则重建后的新列表会被残留的旧行追加，导致行序与内存列表错位。
     */
    void ClearPendingPacketEntries();

    /**
     * @brief 当前封包列表世代号。
     *
     * 已经投递但还没执行的增量脚本带的是旧世代号，JS 侧会把它丢掉，
     * 保证「重建之后到达的旧批次」不会污染新列表。
     */
    unsigned int PacketListGeneration();

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

    std::mutex m_queueMutex;                     ///< 保护下面三个字段
    std::wstring m_pendingEntries;               ///< 已拼好的 JS 数组项，项间以逗号分隔
    std::size_t m_pendingLastIndex = 0;          ///< 待下发批次里最后一条的序号，用作列表总数
    bool m_flushPosted = false;                  ///< 是否已投递刷新消息，避免每包一次 PostMessage
    unsigned int m_packetGeneration = 0;         ///< 封包列表世代号，整表重建时递增
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
