#pragma once

#include <mutex>
#include <unordered_map>

#include "wpe_hook.h"

using ResponseHandler = void (*)(const GamePacket&);

// ============================================================================
// Response waiter (internal)
// ============================================================================

class ResponseWaiter {
public:
    static void Initialize();
    static void Cleanup();

    static uint64_t BeginWait();
    static void EndWait();

    static bool WaitForResponse(
        uint32_t expectedOpcode,
        DWORD timeoutMs,
        uint64_t baselineSerial = 0,
        uint32_t expectedParams = 0,
        bool matchExpectedParams = false,
        bool preRegistered = false
    );

    static void NotifyResponse(uint32_t opcode, uint32_t params);
    static void CancelWait();

private:
    static CRITICAL_SECTION s_cs;
    static CONDITION_VARIABLE s_cv;
    static uint64_t s_responseSerial;
    static std::atomic<long> s_waitingCount;
    static std::atomic<uint64_t> s_cancelSerial;
};

// ============================================================================
// Response dispatcher
// ============================================================================

class ResponseDispatcher {
public:
    static ResponseDispatcher& Instance();

    BOOL Register(uint32_t opcode, ResponseHandler handler);
    BOOL Register(uint32_t opcode, uint32_t params, ResponseHandler handler);
    void Unregister(uint32_t opcode, uint32_t params = 0xFFFFFFFF);
    BOOL Dispatch(const GamePacket& packet);
    void Clear();
    void InitializeDefaultHandlers();

private:
    ResponseDispatcher() = default;
    ~ResponseDispatcher() = default;
    ResponseDispatcher(const ResponseDispatcher&) = delete;
    ResponseDispatcher& operator=(const ResponseDispatcher&) = delete;

    static uint64_t MakeKey(uint32_t opcode, uint32_t params);

    // Key为(opcode << 32 | params)，精确匹配优先于仅opcode匹配。
    std::unordered_map<uint64_t, ResponseHandler> m_handlers;
    std::unordered_map<uint32_t, ResponseHandler> m_opcodeOnlyHandlers;

    std::mutex m_mutex;
};

// hook 是否已安装：活动模块启动前要确认（g_bInitialized 是 hook 内部状态，只读访问走这里）。
bool IsWpeHookInitialized();

// ============================================================================
// Activity state
// ============================================================================

/**
 * @struct ActivityState
 * @brief Activity state base class
 */
