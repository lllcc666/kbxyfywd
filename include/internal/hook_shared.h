#pragma once

#include <windows.h>

#include <atomic>
#include <string>
#include <utility>
#include <vector>

#include "managed_thread.h"
#include "packet_types.h"
#include "wpe_hook.h"

// ============================================================================
// 活动模块共享原语
// ============================================================================
//
// wpe_hook.cpp 拆分后，各活动模块（battle_six / dungeon_jump / shuangtai /
// spirit_collect / story_task / activity_minigames）需要的底层设施都从这里取。
// 这里只放"跨模块复用、且没有业务语义"的东西：
//   * 回包字符串读取（uint16 长度前缀）
//   * 活动协议发包 + 每日任务上报
//   * 活动模块专用的 UI 提示
//   * 奖励表构造
// 业务状态一律留在各自模块内，不出现在这个头里。

// uint16 长度前缀字符串读取；越界返回 false，不推进 offset。
bool ReadPacketString(const BYTE* body, size_t bodySize, size_t& offset, std::string& out);

// ============================================================================
// 统一的"等某个状态成立"原语
// ============================================================================
//
// 以前每个活动模块各自写一遍
//   `while (!flag) { if (超时) return; Sleep(50); }`
// 于是全仓出现过三套等待语义：ResponseWaiter（条件变量）、各模块的
// std::this_thread::sleep_for 轮询、以及裸 Sleep 自旋。差别只在细节上：
// 有的能被停止请求打断，有的不能；有的用 steady_clock 有的用帧计数。
//
// 这里收成一个实现：谓词 + 超时 + 可中断睡眠。它不替代 ResponseWaiter
// （那个是"等特定 opcode 的回包"，由收包线程通知），只替代手写轮询。
enum class WaitOutcome {
    Signalled,  ///< 谓词成立
    TimedOut,   ///< 超时仍未成立
    Stopped,    ///< 收到停止请求，提前放弃
};

/// 轮询 cond 直到成立、超时或被停止。
/// @param timeoutMs 总超时（毫秒）
/// @param sliceMs   每次轮询间隔，同时也是响应停止请求的粒度
template <typename Cond>
WaitOutcome WaitForCondition(Cond cond, DWORD timeoutMs, DWORD sliceMs = 50);

/// 带模块自有取消条件的版本：cancelled 成立时不返回 TimedOut 而返回 Stopped。
///
/// 活动模块的等待常常有两个退出条件——全局停止请求，以及"模块自己的会话号
/// 变了 / 运行标记被清了"。后者比超时更该立刻返回，否则用户点了停止，线程还要
/// 把剩下的超时耗完。第一个模板把 cancelled 固定成 StopRequested()。
template <typename Cond, typename Cancelled>
WaitOutcome WaitForConditionEx(Cond cond, Cancelled cancelled, DWORD timeoutMs,
                               DWORD sliceMs = 50) {
    const ULONGLONG start = GetTickCount64();
    for (;;) {
        if (cond()) {
            return WaitOutcome::Signalled;
        }
        if (ManagedThread::StopRequested() || cancelled()) {
            return WaitOutcome::Stopped;
        }
        if (GetTickCount64() - start >= timeoutMs) {
            return WaitOutcome::TimedOut;
        }
        if (!ManagedThread::SleepCancellable(sliceMs)) {
            return WaitOutcome::Stopped;
        }
    }
}

template <typename Cond>
WaitOutcome WaitForCondition(Cond cond, DWORD timeoutMs, DWORD sliceMs) {
    return WaitForConditionEx(cond, []() { return false; }, timeoutMs, sliceMs);
}

// 把单个活动的剩余次数回填到前端（window.updateActivityCounts）。
// 原先 activity_counts.cpp 与 latest_activities.cpp 各有一份逐字相同的实现。
// remainingCount < 0 表示尚未解析到。
void ReportActivityCount(uint32_t activityId, int remainingCount);

// 精魄系统的三条 UI 提示（window.handleSpiritCollectData）。
void NotifySpiritAlert(const std::wstring& message);
void NotifySpiritSuccess(const std::wstring& message);
void NotifySpiritConfirm(const std::wstring& playerName);


// hook 侧共享全局：最近的游戏套接字（SendPacket 传 0 时用它兜底）。
// 定义在 wpe_hook.cpp，拆分后的模块只读不写。
extern std::atomic<SOCKET> g_LastGameSocket;

/// 把"延时一次动作"这类线程体放到受管线程上跑。
///
/// 等价于 `CreateThread(...)` + `CloseHandle(...)`，区别是线程会登记到
/// ManagedThread：退出流程里 RequestStopAll/JoinAll 能收住它，不会出现
/// 钩子卸载之后还有线程在发包。body 里原有的 Sleep 请自行换成
/// ManagedThread::SleepCancellable，才能真正被中断。
///
/// @param name 线程名（日志/排查用），可为 nullptr
/// @param body Win32 线程体，参数由 param 传入，所有权仍归 body 自己处理
bool StartManagedThreadOnce(const wchar_t* name, LPTHREAD_START_ROUTINE body, LPVOID param);

/// CreateThread 的替身：把原句柄照旧交回调用方，同时复制一份登记到
/// ManagedThread。退出流程 JoinAll 会等它跑完，不会出现"钩子已卸载、线程还在发包"。
///
/// 前六个参数与 CreateThread 完全一致，调用点只需替换函数名；末尾多一个
/// name 用于登记（日志/排查）。以前所有线程都叫 "raw"，JoinAll 超时时打印
/// 「还有 2 条线程没结束：raw, raw」，等于没有信息——线程名是排查退出卡死的
/// 唯一线索，所以强制调用方给一个。
inline HANDLE CreateManagedThread(LPSECURITY_ATTRIBUTES threadAttributes,
                                  SIZE_T stackSize,
                                  LPTHREAD_START_ROUTINE startAddress,
                                  LPVOID parameter,
                                  DWORD creationFlags,
                                  LPDWORD threadId,
                                  const wchar_t* name) {
    DWORD tid = 0;
    HANDLE hThread = CreateThread(threadAttributes, stackSize, startAddress, parameter,
                                  creationFlags, &tid);
    if (hThread != nullptr) {
        ManagedThread::AdoptThreadHandle(hThread, tid,
                                         (name != nullptr) ? name : L"unnamed");
        if (threadId != nullptr) {
            *threadId = tid;
        }
    }
    return hThread;
}
