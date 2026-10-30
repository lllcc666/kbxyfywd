#include <windows.h>

#include "battle_six.h"
#include "hook_shared.h"
#include "little_game.h"
#include "managed_thread.h"
#include "packet_builder.h"
#include "packet_protocol.h"
#include "shuangtai_internal.h"
#include "story_task.h"
#include "wpe_hook.h"
#include "wpe_hook_internal.h"

struct DailyTaskData {
    DWORD flags;
};

// ============ 日常活动功能实现 ============

// 每日卡牌
// Opcode: 1184815 (AS3 OP_CLIENT_ACTIVITY), Params: 22 就是「每日卡牌」子命令
BOOL SendDailyCardPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1184815)
        .SetParams(22)
        .Build();
    return SendPacketBytes(packet);
}

// 每日礼包
// Opcode: 1186300 (OP_CLIENT_VIP_DAY_AWARD), Params: 0
BOOL SendDailyGiftPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1186300)
        .SetParams(0)
        .Build();
    return SendPacketBytes(packet);
}

// 每周礼包
// Opcode: 1186290 (OP_CLIENT_VIP_WEEK_AWARD), Params: 0
BOOL SendWeeklyGiftPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1186290)
        .SetParams(0)
        .Build();
    return SendPacketBytes(packet);
}

// 家族考勤
// Opcode: 1184770, Params: 0
BOOL SendFamilyCheckinPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1184770)
        .SetParams(0)
        .Build();
    return SendPacketBytes(packet);
}

// 家族报道
// Opcode: 1185336, Params: 0
BOOL SendFamilyReportPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1185336)
        .SetParams(0)
        .Build();
    return SendPacketBytes(packet);
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
    SendPacketBytes(packet1);
    ManagedThread::SleepCancellable(300);
    
    // 封包2: Opcode=1185313, Params=1, Body=[6]
    auto packet2 = PacketBuilder()
        .SetOpcode(1185313)
        .SetParams(1)
        .WriteInt32(6)
        .Build();
    SendPacketBytes(packet2);
    ManagedThread::SleepCancellable(300);
    
    // 封包3: Opcode=1184801, Params=3
    auto packet3 = PacketBuilder()
        .SetOpcode(1184801)
        .SetParams(3)
        .Build();
    SendPacketBytes(packet3);
    
    return TRUE;
}

// 商城惊喜
// 循环发送3次: Opcode=旧活动总线, Params=154, Body=[1]
BOOL SendShopSurprisePacket() {
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::ACTIVITY_QINGYANG_SEND)
        .SetParams(154)
        .WriteInt32(1)
        .Build();
    
    for (int i = 0; i < 3; i++) {
        SendPacketBytes(packet);
        if (i < 2) ManagedThread::SleepCancellable(300);
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
    return SendPacketBytes(packet);
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
        g_deepDigState.completedCount.store(0);
        SendQueryDeepDigCountPacket();

        // 执行剩余次数
        const int remaining = g_deepDigState.remainingCount.load();
        int execCount = remaining > 0 ? remaining : WpeHook::DEEP_DIG_DEFAULT_COUNT;
        for (int i = 0; i < execCount; i++) {
            if (ManagedThread::StopRequested()) {
                break;
            }
            g_deepDigState.sessionId.store(0);
            g_deepDigState.waitingResponse.store(true);

            // 开局走小游戏总线：params=gameType=12，body=[0,0,0,0,0,0]。
            if (!SendLittleGameStart(Opcode::GAME_TYPE_DEEP_DIG)) {
                g_deepDigState.waitingResponse.store(false);
                break;
            }

            // 等 ProcessDeepDigResponse 把回包里的 sessionId 填进来，最多 5 秒。
            for (int waited = 0; waited < 5000 && g_deepDigState.sessionId.load() <= 0; waited += 100) {
                if (ManagedThread::StopRequested()) {
                    break;
                }
                ManagedThread::SleepCancellable(100);
            }

            // 无论等到没等到，标志的生命周期都只覆盖这一次请求。
            // 超时和停止路径也要复位，否则下一次成功的开局之前，
            // 任何小游戏回的 act==0 都会被当成这一局的 sessionId。
            g_deepDigState.waitingResponse.store(false);

            const int sessionId = g_deepDigState.sessionId.load();
            if (sessionId > 0) {
                // 结算走同一个 opcode：body=[4,0,4,sessionId,0,0]。
                // score 必须发 4：DeepHole.as:29 一局上限 treasureNum=4，
                // 而 DeepHole.as:172 只在 treasureCount>0 时才显示发奖。
                SendLittleGameEnd(Opcode::GAME_TYPE_DEEP_DIG, 1, 4, sessionId);
                g_deepDigState.completedCount.fetch_add(1);
            }

            ManagedThread::SleepCancellable(800);
        }

        completedCount++;
        ManagedThread::SleepCancellable(300);
    }
    if (!ManagedThread::StopRequested() && (flags & 0x02)) {  // 每日卡牌
        SendDailyCardPacket();
        completedCount++;
        ManagedThread::SleepCancellable(300);
    }
    if (!ManagedThread::StopRequested() && (flags & 0x04)) {  // 每日礼包
        SendDailyGiftPacket();
        completedCount++;
        ManagedThread::SleepCancellable(300);
    }
    if (!ManagedThread::StopRequested() && (flags & 0x08)) {  // 每周礼包
        SendWeeklyGiftPacket();
        completedCount++;
        ManagedThread::SleepCancellable(300);
    }
    if (!ManagedThread::StopRequested() && (flags & 0x10)) {  // 家族考勤
        SendFamilyCheckinPacket();
        completedCount++;
        ManagedThread::SleepCancellable(300);
    }
    if (!ManagedThread::StopRequested() && (flags & 0x20)) {  // 家族报道
        SendFamilyReportPacket();
        completedCount++;
        ManagedThread::SleepCancellable(300);
    }
    if (!ManagedThread::StopRequested() && (flags & 0x40)) {  // 家族保卫
        SendFamilyDefendPacket();
        completedCount++;
        ManagedThread::SleepCancellable(300);
    }
    if (!ManagedThread::StopRequested() && (flags & 0x80)) {  // 商城惊喜
        SendShopSurprisePacket();
        completedCount++;
    }
    if (!ManagedThread::StopRequested() && (flags & 0x100)) {  // 跳舞大赛
        SendDanceContestPacket();
        completedCount++;
        ManagedThread::SleepCancellable(300);
    }
    if (!ManagedThread::StopRequested() && (flags & 0x200)) {  // 火风试炼
        SendFireWindTrialPacket();
        completedCount++;
        ManagedThread::SleepCancellable(300);
    }
    if (!ManagedThread::StopRequested() && (flags & 0x400)) {  // 火焰试炼
        SendFireTrialPacket();
        completedCount++;
        ManagedThread::SleepCancellable(300);
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
    
    // 走受管线程：进程退出时能统一 RequestStopAll/JoinAll，不再是一条没人等的裸线程。
    const bool started = ManagedThread::Start(L"daily_tasks", [taskData]() {
        DailyTaskThreadProc(taskData);
    });
    if (!started) {
        delete taskData;
    }
}

// TASK_TALK_BACK 分发链：八卦灵盘与主线任务共用一个 opcode，两者都要收到回包。
