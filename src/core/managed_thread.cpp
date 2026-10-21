#include "managed_thread.h"

#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace {

struct ThreadState {
    std::thread thread;
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> finished{false};
    std::wstring name;
};

std::mutex g_registryMutex;
std::vector<std::shared_ptr<ThreadState>> g_registry;
thread_local ThreadState* t_currentThread = nullptr;

// ============================================================================
// 收养来的裸线程（历史 CreateThread 调用点）
// ============================================================================
//
// 这些线程不是 std::thread，拿不到 join，只能靠句柄等候。这里保存一份复制来的
// 句柄，调用方 CloseHandle 自己那份不影响等待。
namespace {

struct RawThreadState {
    HANDLE handle{nullptr};
    std::wstring name;
};

std::mutex g_rawMutex;
std::vector<std::shared_ptr<RawThreadState>> g_rawThreads;

}  // namespace

// 清掉已经结束的条目。必须在持锁状态下调用。
// 已结束但还没 join 的条目也要在这里 join 掉再删，否则线程句柄会一直挂着。
void PruneFinishedLocked() {
    for (auto it = g_registry.begin(); it != g_registry.end();) {
        if (!(*it)->finished.load()) {
            ++it;
            continue;
        }
        if ((*it)->thread.joinable()) {
            if (std::this_thread::get_id() == (*it)->thread.get_id()) {
                // 线程体里自己触发回收：不能 join 自己，跳过，下次再收。
                ++it;
                continue;
            }
            (*it)->thread.join();
        }
        it = g_registry.erase(it);
    }
}

}  // namespace

bool ManagedThread::Start(const std::wstring& name, std::function<void()> body) {
    if (!body) {
        return false;
    }

    auto state = std::make_shared<ThreadState>();
    state->name = name;

    std::shared_ptr<ThreadState> holder = state;
    try {
        state->thread = std::thread([holder, body]() {
            t_currentThread = holder.get();
            // 线程体里抛出的异常必须就地吃掉：让它跑出 std::thread 会直接 terminate 整个进程。
            try {
                body();
            } catch (...) {
                // 静默吞掉会让"为什么没跑"无从查起，至少留一条调试输出。
                const std::wstring message = L"ManagedThread: 线程体抛出异常 name=" + holder->name;
                OutputDebugStringW(message.c_str());
            }
            holder->finished.store(true);
            t_currentThread = nullptr;
        });
    } catch (...) {
        // 创建线程本身失败（句柄/内存耗尽）：按失败返回，别让异常穿给调用方。
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        PruneFinishedLocked();
        g_registry.push_back(state);
    }
    return true;
}

bool ManagedThread::StopRequested() {
    ThreadState* state = t_currentThread;
    return state != nullptr && state->stopRequested.load();
}

bool ManagedThread::SleepCancellable(DWORD totalMs, DWORD sliceMs) {
    if (sliceMs == 0) {
        sliceMs = 50;
    }
    DWORD slept = 0;
    while (slept < totalMs) {
        if (StopRequested()) {
            return false;
        }
        DWORD step = totalMs - slept;
        if (step > sliceMs) {
            step = sliceMs;
        }
        Sleep(step);
        slept += step;
    }
    return !StopRequested();
}

void ManagedThread::RequestStopAll() {
    std::lock_guard<std::mutex> lock(g_registryMutex);
    for (auto& state : g_registry) {
        state->stopRequested.store(true);
    }
}

bool ManagedThread::AdoptThreadHandle(HANDLE rawHandle, DWORD threadId, const wchar_t* name) {
    (void)threadId;
    if (rawHandle == nullptr) {
        return false;
    }

    HANDLE duplicate = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), rawHandle, GetCurrentProcess(), &duplicate,
                         0, FALSE, DUPLICATE_SAME_ACCESS)) {
        return false;
    }

    auto state = std::make_shared<RawThreadState>();
    state->handle = duplicate;
    state->name = (name != nullptr) ? name : L"raw";

    std::lock_guard<std::mutex> lock(g_rawMutex);
    g_rawThreads.push_back(std::move(state));
    return true;
}

size_t ManagedThread::JoinAll(DWORD timeoutMs) {
    std::vector<std::shared_ptr<ThreadState>> snapshot;
    {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        snapshot = g_registry;
        for (auto& state : snapshot) {
            state->stopRequested.store(true);
        }
    }

    // 每个线程各自算截止时间：共用一个 deadline 时，第一个线程吃满超时后
    // 后面的线程剩余等待为 0，等于直接 detach。
    for (auto& state : snapshot) {
        const ULONGLONG deadline = GetTickCount64() + timeoutMs;
        while (!state->finished.load() && GetTickCount64() < deadline) {
            Sleep(10);
        }
        if (state->thread.joinable()) {
            if (state->finished.load()) {
                state->thread.join();
            } else {
                // 到点仍未结束：脱离而不是无限等待，避免退出流程被卡死。
                state->thread.detach();
            }
        }
    }

    // 收养的裸线程：等它跑完再关句柄；到点没结束就计进"仍未退出"。
    std::vector<std::shared_ptr<RawThreadState>> rawSnapshot;
    {
        std::lock_guard<std::mutex> lock(g_rawMutex);
        rawSnapshot = g_rawThreads;
    }
    size_t rawStillRunning = 0;
    for (auto& raw : rawSnapshot) {
        const ULONGLONG deadline = GetTickCount64() + timeoutMs;
        bool finished = false;
        while (GetTickCount64() < deadline) {
            if (WaitForSingleObject(raw->handle, 20) == WAIT_OBJECT_0) {
                finished = true;
                break;
            }
        }
        if (!finished && WaitForSingleObject(raw->handle, 0) == WAIT_OBJECT_0) {
            finished = true;
        }

        if (!finished) {
            ++rawStillRunning;
            continue;
        }
        std::lock_guard<std::mutex> lock(g_rawMutex);
        for (auto it = g_rawThreads.begin(); it != g_rawThreads.end(); ++it) {
            if (*it == raw) {
                CloseHandle(raw->handle);
                raw->handle = nullptr;
                g_rawThreads.erase(it);
                break;
            }
        }
    }

    size_t stillRunning = rawStillRunning;
    {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        for (auto& state : g_registry) {
            if (!state->finished.load()) {
                ++stillRunning;
            }
        }
        PruneFinishedLocked();
    }
    return stillRunning;
}

size_t ManagedThread::ActiveCount() {
    std::lock_guard<std::mutex> lock(g_registryMutex);
    size_t count = 0;
    for (auto& state : g_registry) {
        if (!state->finished.load()) {
            ++count;
        }
    }
    return count;
}

std::wstring ManagedThread::DescribeActive() {
    std::lock_guard<std::mutex> lock(g_registryMutex);
    std::wstring result;
    for (auto& state : g_registry) {
        if (state->finished.load()) {
            continue;
        }
        if (!result.empty()) {
            result += L", ";
        }
        result += state->name;
    }
    return result;
}
