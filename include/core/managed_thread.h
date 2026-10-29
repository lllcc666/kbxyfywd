#pragma once

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

// ============================================================================
// 托管线程
// ============================================================================
//
// 背景：历史实现到处是
//     HANDLE h = CreateThread(nullptr, 0, XxxThreadProc, param, 0, nullptr);
//     CloseHandle(h);            // 句柄立刻丢弃
//     线程体里全是 Sleep(100);
// 结果是：没人能取消、没人能等待、退出时线程仍在跑并回调已经释放的 UI，
// 连点两次还会起两份流程互相清状态。本类解决三件事（同名去重由各活动自己的
// running 标志负责，本类不按名字拦重复启动）：
//   1. 每个线程都有名字和一个可查询的停止标志；
//   2. Sleep 换成可被停止请求打断的分片睡眠，长流程能及时收尾；
//   3. 退出时 RequestStopAll() + JoinAll() 一条链路，不再靠 Sleep 猜时间。
//
// 用法：
//     ManagedThread::Start(L"act822", [] {
//         while (!ManagedThread::StopRequested()) {
//             ... // 长流程
//             ManagedThread::SleepCancellable(200);
//         }
//     });
//     // 退出：
//     ManagedThread::RequestStopAll();
//     ManagedThread::JoinAll(3000);
//
// 注意：JoinAll 不能从被 join 的线程自身调用；它只等待注册表里记录的线程。
class ManagedThread {
public:
    // 启动一个受管线程。body 内部可用 StopRequested/SleepCancellable 响应取消。
    static bool Start(const std::wstring& name, std::function<void()> body);

    // 当前线程是否收到了停止请求；非受管线程调用恒为 false。
    static bool StopRequested();

    // 可被停止请求打断的睡眠，返回 false 表示被提前打断。
    static bool SleepCancellable(DWORD totalMs, DWORD sliceMs = 50);

    // 请求所有受管线程停止。
    static void RequestStopAll();

    // 等待所有受管线程结束；返回仍未结束的线程数（0 表示干净退出）。
    static size_t JoinAll(DWORD timeoutMs = 5000);

    // 仍在运行的受管线程数量（日志定位用）。
    static size_t ActiveCount();

    // 收养一条不是通过 Start() 创建的线程（历史 CreateThread 调用点）。
    // 这里复制一份句柄登记，JoinAll 时等它退出；调用方自己的句柄照旧 CloseHandle，
    // 两边互不影响。rawHandle 无效或复制失败返回 false。
    static bool AdoptThreadHandle(HANDLE rawHandle, DWORD threadId, const wchar_t* name);
};
