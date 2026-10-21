#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "activity_minigames.h"
#include "activity_states_internal.h"
#include "app_host.h"
#include "battle_six.h"
#include "dungeon_jump.h"
#include "hook_shared.h"
#include "latest_activities.h"
#include "managed_thread.h"
#include "packet_builder.h"
#include "packet_parser.h"
#include "packet_protocol.h"
#include "shuangtai_internal.h"
#include "story_task.h"
#include "story_task_internal.h"
#include "task_zone_internal.h"
#include "ui_bridge.h"
#include "utils.h"
#include "wpe_hook.h"
#include "wpe_hook_internal.h"

extern bool PostScriptToUI(const std::wstring& jsCode);

struct DailyTaskData {
    DWORD flags;
    HWND hwnd;
};

// ============ 日常活动功能实现 ============

// 深度挖宝 - 发送开始游戏封包
// Opcode: 1185525, Params: 12, Body: 6个int32(都是0)
BOOL SendDailyCardPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1184815)
        .SetParams(22)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 每日礼包
// Opcode: 1186300 (OP_CLIENT_VIP_DAY_AWARD), Params: 0
BOOL SendDailyGiftPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1186300)
        .SetParams(0)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 每周礼包
// Opcode: 1186290 (OP_CLIENT_VIP_WEEK_AWARD), Params: 0
BOOL SendWeeklyGiftPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1186290)
        .SetParams(0)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 家族考勤
// Opcode: 1184770, Params: 0
BOOL SendFamilyCheckinPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1184770)
        .SetParams(0)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 家族报道
// Opcode: 1185336, Params: 0
BOOL SendFamilyReportPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1185336)
        .SetParams(0)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 家族保卫战
// 发送3个封包
BOOL SendFamilyDefendPacket() {
    // 封包1: Opcode=1185313, Params=0, Body=[0]
    auto packet1 = PacketBuilder()
        .SetOpcode(1185313)
        .SetParams(0)
        .WriteInt32(0)
        .Build();
    SendPacket(0, packet1.data(), static_cast<DWORD>(packet1.size()));
    Sleep(300);
    
    // 封包2: Opcode=1185313, Params=1, Body=[6]
    auto packet2 = PacketBuilder()
        .SetOpcode(1185313)
        .SetParams(1)
        .WriteInt32(6)
        .Build();
    SendPacket(0, packet2.data(), static_cast<DWORD>(packet2.size()));
    Sleep(300);
    
    // 封包3: Opcode=1184801, Params=3
    auto packet3 = PacketBuilder()
        .SetOpcode(1184801)
        .SetParams(3)
        .Build();
    SendPacket(0, packet3.data(), static_cast<DWORD>(packet3.size()));
    
    return TRUE;
}

// 商城惊喜
// 循环发送3次: Opcode=1184833, Params=154, Body=[1]
BOOL SendShopSurprisePacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1184833)
        .SetParams(154)
        .WriteInt32(1)
        .Build();
    
    for (int i = 0; i < 3; i++) {
        SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
        if (i < 2) Sleep(300);
    }
    
    return TRUE;
}

// ============ 自动回血功能实现 ============

// 发送贝贝回血封包
// Opcode: 1186818, Params: 1
BOOL SendBeibeiHealPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1186818)
        .SetParams(1)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}


DWORD WINAPI DailyTaskThreadProc(LPVOID lpParam) {
    DailyTaskData* taskData = static_cast<DailyTaskData*>(lpParam);
    DWORD flags = taskData->flags;
    
    int completedCount = 0;
    int totalCount = 0;
    
    // 计算总数（支持12个活动）
    for (int i = 0; i < 12; i++) {
        if (flags & (1 << i)) totalCount++;
    }
    
    // 按顺序执行
    if (!ManagedThread::StopRequested() && (flags & 0x01)) {  // 深度挖宝
        g_deepDigState.autoMode = true;
        g_deepDigState.completedCount = 0;
        SendQueryDeepDigCountPacket();
        
        // 执行剩余次数
        int execCount = g_deepDigState.remainingCount > 0 ?
                        g_deepDigState.remainingCount : WpeHook::DEEP_DIG_DEFAULT_COUNT;
        for (int i = 0; i < execCount; i++) {
            if (ManagedThread::StopRequested()) {
                break;
            }
            g_deepDigState.waitingResponse = true;
            g_deepDigState.sessionId = 0;

            // 发送开始游戏封包
            BYTE startPacket[36] = {
                0x44, 0x53, 0x18, 0x00,
                0x15, 0x14, 0x12, 0x00,
                0x0C, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00
            };
            SendPacket(0, startPacket, sizeof(startPacket), Opcode::DEEP_DIG_BACK, 5000);
            
            Sleep(500);
            
            // 发送结束游戏封包
            if (g_deepDigState.sessionId > 0) {
                BYTE endPacket[36] = {
                    0x44, 0x53, 0x18, 0x00,
                    0x15, 0x14, 0x12, 0x00,
                    0x0C, 0x00, 0x00, 0x00,
                    0x04, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00,
                    0x04, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00
                };
                endPacket[24] = static_cast<BYTE>(g_deepDigState.sessionId & 0xFF);
                endPacket[25] = static_cast<BYTE>((g_deepDigState.sessionId >> 8) & 0xFF);
                endPacket[26] = static_cast<BYTE>((g_deepDigState.sessionId >> 16) & 0xFF);
                endPacket[27] = static_cast<BYTE>((g_deepDigState.sessionId >> 24) & 0xFF);
                SendPacket(0, endPacket, sizeof(endPacket));
            }
            
            Sleep(800);
        }
        
        g_deepDigState.autoMode = false;
        completedCount++;
        Sleep(300);
    }
    if (!ManagedThread::StopRequested() && (flags & 0x02)) {  // 每日卡牌
        SendDailyCardPacket();
        completedCount++;
        Sleep(300);
    }
    if (!ManagedThread::StopRequested() && (flags & 0x04)) {  // 每日礼包
        SendDailyGiftPacket();
        completedCount++;
        Sleep(300);
    }
    if (!ManagedThread::StopRequested() && (flags & 0x08)) {  // 每周礼包
        SendWeeklyGiftPacket();
        completedCount++;
        Sleep(300);
    }
    if (!ManagedThread::StopRequested() && (flags & 0x10)) {  // 家族考勤
        SendFamilyCheckinPacket();
        completedCount++;
        Sleep(300);
    }
    if (!ManagedThread::StopRequested() && (flags & 0x20)) {  // 家族报道
        SendFamilyReportPacket();
        completedCount++;
        Sleep(300);
    }
    if (!ManagedThread::StopRequested() && (flags & 0x40)) {  // 家族保卫
        SendFamilyDefendPacket();
        completedCount++;
        Sleep(300);
    }
    if (!ManagedThread::StopRequested() && (flags & 0x80)) {  // 商城惊喜
        SendShopSurprisePacket();
        completedCount++;
    }
    if (!ManagedThread::StopRequested() && (flags & 0x100)) {  // 跳舞大赛
        SendDanceContestPacket();
        completedCount++;
        Sleep(300);
    }
    if (!ManagedThread::StopRequested() && (flags & 0x200)) {  // 火风试炼
        SendFireWindTrialPacket();
        completedCount++;
        Sleep(300);
    }
    if (!ManagedThread::StopRequested() && (flags & 0x400)) {  // 火焰试炼
        SendFireTrialPacket();
        completedCount++;
        Sleep(300);
    }
    if (!ManagedThread::StopRequested() && (flags & 0x800)) {  // 风暴试炼
        SendStormTrialPacket();
        completedCount++;
    }
    
    // 发送完成消息
    if (g_hWnd) {
        PostMessage(g_hWnd, AppMessage::kDailyTaskComplete, completedCount, totalCount);
    }
    
    delete taskData;
    return 0;
}

void StartDailyTasksAsync(DWORD flags) {
    if (flags == 0) return;
    
    DailyTaskData* taskData = new DailyTaskData();
    taskData->flags = flags;
    taskData->hwnd = g_hWnd;
    
    // 走受管线程：进程退出时能统一 RequestStopAll/JoinAll，不再是一条没人等的裸线程。
    const bool started = ManagedThread::Start(L"daily_tasks", [taskData]() {
        DailyTaskThreadProc(taskData);
    });
    if (!started) {
        delete taskData;
    }
}

// TASK_TALK_BACK 分发链：八卦灵盘与主线任务共用一个 opcode，两者都要收到回包。
