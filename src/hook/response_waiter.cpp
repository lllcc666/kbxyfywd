#include <windows.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "managed_thread.h"
#include "wpe_hook.h"
#include "wpe_hook_internal.h"

// ============================================================================
// 响应等待器实现
// ============================================================================

namespace {

struct ResponseRecord {
    uint64_t serial;
    uint32_t opcode;
    uint32_t params;
};

std::deque<ResponseRecord> g_responseQueue;

// 定向取消表：opcode -> 取消计数。0 号位保留给全局取消。
// 之前 CancelWait 只会把全局 s_cancelSerial +1，于是「停主线任务」会连带打断
// 八卦灵盘、自动战斗、进图等所有正卡在 WaitForResponse 里的流程。
std::vector<ResponseWaiter::CancelEntry> g_cancelTable;

uint64_t CancelSerialFor(uint32_t opcode) {
    for (const auto& entry : g_cancelTable) {
        if (entry.opcode == opcode) {
            return entry.serial;
        }
    }
    return 0;
}

void BumpCancelSerial(uint32_t opcode) {
    for (auto& entry : g_cancelTable) {
        if (entry.opcode == opcode) {
            ++entry.serial;
            return;
        }
    }
    // 新入表的 opcode：串号跟全局对齐，避免「刚入表」被 IsCancelled 读成「已取消」。
    g_cancelTable.push_back(ResponseWaiter::CancelEntry{opcode, CancelSerialFor(0)});
}

}  // namespace

CRITICAL_SECTION ResponseWaiter::s_cs;
CONDITION_VARIABLE ResponseWaiter::s_cv;
uint64_t ResponseWaiter::s_responseSerial = 0;
std::atomic<long> ResponseWaiter::s_waitingCount{0};  // 等待线程计数（真正的原子操作）

namespace {
// 停机中：新的等待直接失败，避免线程在被拆除的等待器上继续 Sleep。
std::atomic<bool> g_responseWaitShutdown{false};
}  // namespace
std::atomic<uint64_t> ResponseWaiter::s_cancelSerial{0};

void ResponseWaiter::Initialize() {
    InitializeCriticalSection(&s_cs);
    InitializeConditionVariable(&s_cv);
    s_waitingCount = 0;
    s_responseSerial = 0;
    s_cancelSerial = 0;
    g_responseQueue.clear();
    g_cancelTable.clear();
}

void ResponseWaiter::Cleanup() {
    // 先立停机标志：此后 BeginWait 直接返回，不会被已拆除的等待器收进来。
    g_responseWaitShutdown.store(true);
    CancelWait();

    const DWORD startTick = GetTickCount();
    while (s_waitingCount.load() > 0) {
        if (GetTickCount() - startTick > (WpeHook::TIMEOUT_SEND + WpeHook::TIMEOUT_RESPONSE + 1000)) {
            return;
        }
        Sleep(10);
    }

    EnterCriticalSection(&s_cs);
    g_responseQueue.clear();
    LeaveCriticalSection(&s_cs);
    // 这里不再 DeleteCriticalSection：等满超时被 detach 的等待线程可能随后
    // 才进入 BeginWait/EndWait，删掉临界区的代价（UB）远大于留到进程退出。
}

uint64_t ResponseWaiter::BeginWait() {
    if (g_responseWaitShutdown.load()) {
        return 0;  // 停机中：让调用方立刻超时返回，不要再登记等待
    }
    EnterCriticalSection(&s_cs);

    const long previousWaiting = s_waitingCount.fetch_add(1);
    if (previousWaiting == 0) {
        g_responseQueue.clear();
    }

    const uint64_t serial = s_responseSerial;
    LeaveCriticalSection(&s_cs);
    return serial;
}

void ResponseWaiter::EndWait() {
    EnterCriticalSection(&s_cs);

    const long remainingWaiting = s_waitingCount.fetch_sub(1) - 1;
    if (remainingWaiting <= 0) {
        g_responseQueue.clear();
    }

    LeaveCriticalSection(&s_cs);
}

bool ResponseWaiter::WaitForResponse(
    uint32_t expectedOpcode,
    DWORD timeoutMs,
    uint64_t baselineSerial,
    uint32_t expectedParams,
    bool matchExpectedParams,
    bool preRegistered) {
    if (expectedOpcode == 0 || timeoutMs == 0) {
        return true;  // 不等待，直接返回
    }

    bool ownsRegistration = false;
    if (!preRegistered) {
        BeginWait();
        ownsRegistration = true;
    }

    EnterCriticalSection(&s_cs);
    const uint64_t cancelSerial = s_cancelSerial.load();
    const uint64_t opcodeCancelSerial = CancelSerialFor(expectedOpcode);

    const DWORD startTick = GetTickCount();
    bool received = false;

    while (true) {
        // 全局取消（cancelSerial 变了）或**只针对本 opcode** 的定向取消，两者都让本等待退出；
        // 其它 opcode 的定向取消不再影响这里。
        if (cancelSerial != s_cancelSerial.load() ||
            opcodeCancelSerial != CancelSerialFor(expectedOpcode)) {
            break;
        }

        auto matched = std::find_if(
            g_responseQueue.begin(),
            g_responseQueue.end(),
            [&](const ResponseRecord& record) {
                if (record.serial <= baselineSerial) {
                    return false;
                }

                if (record.opcode != expectedOpcode) {
                    return false;
                }

                if (matchExpectedParams && record.params != expectedParams) {
                    return false;
                }

                return true;
            });

        if (matched != g_responseQueue.end()) {
            received = true;
            g_responseQueue.erase(matched);
            break;
        }

        const DWORD elapsed = GetTickCount() - startTick;
        if (elapsed >= timeoutMs) {
            break;
        }

        // 分片等待：线程被要求停止时能及时退出，不必睡满整个超时。
        DWORD slice = timeoutMs - elapsed;
        if (slice > 200) {
            slice = 200;
        }
        SleepConditionVariableCS(&s_cv, &s_cs, slice);
        if (ManagedThread::StopRequested()) {
            break;
        }
    }

    LeaveCriticalSection(&s_cs);

    if (ownsRegistration) {
        EndWait();
    }

    return received;
}

void ResponseWaiter::NotifyResponse(uint32_t opcode, uint32_t params) {
    EnterCriticalSection(&s_cs);
    ++s_responseSerial;
    if (s_waitingCount.load() > 0) {
        g_responseQueue.push_back(ResponseRecord{s_responseSerial, opcode, params});
    }
    LeaveCriticalSection(&s_cs);
    WakeAllConditionVariable(&s_cv);
}

void ResponseWaiter::CancelWait(uint32_t opcode) {
    EnterCriticalSection(&s_cs);
    BumpCancelSerial(opcode);
    if (opcode == 0) {
        // 全局取消：同时推进所有已知 opcode 的串号，保证「全局取消之后」任何
        // 按 opcode 取的基线都与取消前不同——否则后会入表的 opcode 会拿到
        // 旧 token，被误判成「已被定向取消」。
        for (auto& entry : g_cancelTable) {
            if (entry.opcode != 0) {
                ++entry.serial;
            }
        }
        s_cancelSerial.fetch_add(1);
    }
    LeaveCriticalSection(&s_cs);
    WakeAllConditionVariable(&s_cv);
}

bool ResponseWaiter::IsCancelled(uint32_t opcode) {
    // 只需要知道「此刻是否处于取消态」，用当前串号探一次即可。
    EnterCriticalSection(&s_cs);
    const bool cancelled = IsCancelledLocked(opcode, CancelSerialFor(opcode));
    LeaveCriticalSection(&s_cs);
    return cancelled;
}

bool ResponseWaiter::IsCancelledLocked(uint32_t opcode, uint64_t cancelToken) {
    // 全局取消对所有人都生效。
    if (s_cancelSerial.load() != CancelSerialFor(0)) {
        return true;
    }
    if (opcode == 0) {
        return false;
    }
    return CancelSerialFor(opcode) != cancelToken;
}

