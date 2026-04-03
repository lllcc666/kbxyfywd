#pragma once

#include <mutex>

#include "wpe_hook.h"

using ResponseHandler = void (*)(const GamePacket&);

// ============================================================================
// 响应等待器（内部使用）
// ============================================================================

/**
 * @class ResponseWaiter
 * @brief 响应等待器（内部使用，用于SendPacket自动等待响应）
 *
 * 使用条件变量实现高效的事件通知，替代Sleep轮询。
 * 优化：只在有等待线程时才加锁，避免对每个封包都加锁。
 */
class ResponseWaiter {
public:
    /**
     * @brief 初始化ResponseWaiter（在InitializeHooks中调用）
     */
    static void Initialize();

    /**
     * @brief 清理ResponseWaiter（在UninitializeHooks中调用）
     */
    static void Cleanup();

    /**
     * @brief 等待特定Opcode的响应
     * @param expectedOpcode 期望的Opcode
     * @param timeoutMs 超时时间（毫秒）
     * @return 是否收到响应
     */
    static bool WaitForResponse(
        uint32_t expectedOpcode,
        DWORD timeoutMs
    );

    /**
     * @brief 通知收到响应（在ProcessReceivedGamePackets中调用）
     * @param opcode 收到的Opcode
     *
     * 优化：使用原子变量快速检查是否有等待线程，避免不必要的加锁
     */
    static void NotifyResponse(uint32_t opcode);

    /**
     * @brief 取消等待
     */
    static void CancelWait();

private:
    static CRITICAL_SECTION s_cs;
    static CONDITION_VARIABLE s_cv;
    static bool s_responseReceived;
    static uint32_t s_receivedOpcode;
    static std::atomic<long> s_waitingCount;  // 等待线程计数（真正的原子操作）
};

// ============================================================================
// 响应分发器
// ============================================================================

/**
 * @brief 响应处理器类型定义
 */
using ResponseHandler = void (*)(const GamePacket&);
using PacketProgressCallback = void (*)(DWORD, DWORD, const std::string&);

/**
 * @class ResponseDispatcher
 * @brief 响应分发器 - 基于Opcode和Params分发响应到对应处理器
 * 
 * 使用示例：
 * @code
 * // 注册处理器
 * ResponseDispatcher::Instance().Register(
 *     Opcode::ACTIVITY_QUERY_BACK,
 *     activityId,
 *     ProcessActivityResponse
 * );
 * 
 * // 在HookedRecv中分发
 * ResponseDispatcher::Instance().Dispatch(packet);
 * @endcode
 */
class ResponseDispatcher {
public:
    /**
     * @brief 获取单例实例
     */
    static ResponseDispatcher& Instance();

    /**
     * @brief 注册响应处理器（仅匹配Opcode）
     * @param opcode 操作码
     * @param handler 处理器函数
     * @return 注册是否成功
     */
    BOOL Register(uint32_t opcode, ResponseHandler handler);

    /**
     * @brief 注册响应处理器（匹配Opcode + Params）
     * @param opcode 操作码
     * @param params 参数值
     * @param handler 处理器函数
     * @return 注册是否成功
     */
    BOOL Register(uint32_t opcode, uint32_t params, ResponseHandler handler);

    /**
     * @brief 注销处理器
     * @param opcode 操作码
     * @param params 参数值（0xFFFFFFFF表示匹配任意params）
     */
    void Unregister(uint32_t opcode, uint32_t params = 0xFFFFFFFF);

    /**
     * @brief 分发封包到对应处理器
     * @param packet 游戏封包
     * @return 是否有处理器处理了该封包
     */
    BOOL Dispatch(const GamePacket& packet);

    /**
     * @brief 清空所有处理器
     */
    void Clear();

    /**
     * @brief 初始化默认处理器（在InitializeHooks中调用）
     */
    void InitializeDefaultHandlers();

private:
    ResponseDispatcher() = default;
    ~ResponseDispatcher() = default;
    ResponseDispatcher(const ResponseDispatcher&) = delete;
    ResponseDispatcher& operator=(const ResponseDispatcher&) = delete;

    // 生成唯一key: 高32位是opcode，低32位是params
    static uint64_t MakeKey(uint32_t opcode, uint32_t params);

    // 处理器映射表
    struct HandlerEntry {
        uint64_t key;
        ResponseHandler handler;
    };
    std::vector<HandlerEntry> m_handlers;
    
    // 仅匹配opcode的处理器（params = 0xFFFFFFFF）
    struct OpcodeHandlerEntry {
        uint32_t opcode;
        ResponseHandler handler;
    };
    std::vector<OpcodeHandlerEntry> m_opcodeOnlyHandlers;
    
    // 线程安全
    std::mutex m_mutex;
};

// ============================================================================
// 活动状态管理
// ============================================================================

/**
 * @struct ActivityState
 * @brief 活动状态基类 - 包含所有活动共有的状态字段
 */
