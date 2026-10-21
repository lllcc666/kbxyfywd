#include "managed_thread.h"

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

// 清掉已经结束的条目。必须在持锁状态下调用。
void PruneFinishedLocked() {
    for (auto it = g_registry.begin(); it != g_registry.end();) {
        if ((*it)->finished.load() && !(*it)->thread.joinable()) {
            it = g_registry.erase(it);
        } else {
            ++it;
        }
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
    state->thread = std::thread([holder, body]() {
        t_currentThread = holder.get();
        body();
        holder->finished.store(true);
        t_currentThread = nullptr;
    });

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

size_t ManagedThread::JoinAll(DWORD timeoutMs) {
    std::vector<std::shared_ptr<ThreadState>> snapshot;
    {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        snapshot = g_registry;
        for (auto& state : snapshot) {
            state->stopRequested.store(true);
        }
    }

    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    for (auto& state : snapshot) {
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

    size_t stillRunning = 0;
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
