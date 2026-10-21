#pragma once

#include <windows.h>

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

// 活动通用发包：opcode=ACTIVITY_QINGYANG_NEW_SEND，body 由 activityId + operation +
// bodyValues 组成。activityId 同时作为 params 参与回包匹配。
BOOL SendActivityPacket(uint32_t activityId, const std::string& operation,
                        const std::vector<int32_t>& bodyValues = {});


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
/// 签名与 CreateThread 完全一致，调用点只需替换函数名。
inline HANDLE CreateManagedThread(LPSECURITY_ATTRIBUTES threadAttributes,
                                  SIZE_T stackSize,
                                  LPTHREAD_START_ROUTINE startAddress,
                                  LPVOID parameter,
                                  DWORD creationFlags,
                                  LPDWORD threadId) {
    DWORD tid = 0;
    HANDLE hThread = CreateThread(threadAttributes, stackSize, startAddress, parameter,
                                  creationFlags, &tid);
    if (hThread != nullptr) {
        ManagedThread::AdoptThreadHandle(hThread, tid, L"raw");
        if (threadId != nullptr) {
            *threadId = tid;
        }
    }
    return hThread;
}
