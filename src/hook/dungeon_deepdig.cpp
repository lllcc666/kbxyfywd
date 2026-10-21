#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
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

// 深度挖宝 owner：会话、剩余次数和自动循环状态统一由该状态对象维护。

DeepDigState g_deepDigState;

// 查询深度挖宝剩余次数
// Opcode: 1185685, Params: 668, Body: "open_ui"字符串
BOOL SendQueryDeepDigCountPacket() {
    g_deepDigState.waitingQuery = true;

    auto packet = PacketBuilder()
        .SetOpcode(1185685)
        .SetParams(668)
        .WriteString("open_ui")
        .Build();

    return SendPacket(
        0,
        packet.data(),
        static_cast<DWORD>(packet.size()),
        Opcode::ACTIVITY_QUERY_BACK,
        3000,
        668,
        true);
}

// 处理深度挖宝查询响应
void ProcessDeepDigQueryResponse(const GamePacket& packet) {
    if (!g_deepDigState.waitingQuery) return;
    if (packet.body.size() < 12) return;
    
    const BYTE* body = packet.body.data();
    size_t offset = 0;
    
    // 读取 UTF 字符串
    if (offset + 2 > packet.body.size()) return;
    uint16_t strLen = ReadUInt16LE(body, offset);
    offset += 2 + strLen;
    
    // 读取剩余次数
    if (offset + 4 > packet.body.size()) return;
    g_deepDigState.remainingCount = ReadInt32LE(body, offset);
    
    g_deepDigState.waitingQuery = false;
}

// 深度挖宝线程数据
struct DeepDigThreadData {
    int targetCount;
    HWND hwnd;
};

// 深度挖宝循环执行线程
DWORD WINAPI DeepDigLoopThreadProc(LPVOID lpParam) {
    DeepDigThreadData* data = static_cast<DeepDigThreadData*>(lpParam);
    int targetCount = data->targetCount;
    
    // 先查询剩余次数
    SendQueryDeepDigCountPacket();

    // 确定实际执行次数
    int actualCount = targetCount;
    if (g_deepDigState.remainingCount > 0 &&
        g_deepDigState.remainingCount < targetCount) {
        actualCount = g_deepDigState.remainingCount;
    }

    // 循环执行
    for (int i = 0; i < actualCount; i++) {
        g_deepDigState.waitingResponse = true;
        g_deepDigState.sessionId = 0;

        // 发送开始游戏封包: Opcode=DEEP_DIG_SEND(1185525), Params=12, Body=6个int32(0)
        auto startPacket = PacketBuilder()
            .SetOpcode(Opcode::DEEP_DIG_SEND)
            .SetParams(12)
            .WriteInt32(0)
            .WriteInt32(0)
            .WriteInt32(0)
            .WriteInt32(0)
            .WriteInt32(0)
            .WriteInt32(0)
            .Build();
        SendPacket(0, startPacket.data(), static_cast<DWORD>(startPacket.size()), Opcode::DEEP_DIG_BACK, 5000);
        
        Sleep(500);
        
        // 发送结束游戏封包
        if (g_deepDigState.sessionId > 0) {
            // Opcode=DEEP_DIG_SEND(1185525), Params=12, Body=[4, 0, 4, sessionId, 0, 0]
            auto endPacket = PacketBuilder()
                .SetOpcode(Opcode::DEEP_DIG_SEND)
                .SetParams(12)
                .WriteInt32(4)
                .WriteInt32(0)
                .WriteInt32(4)
                .WriteInt32(g_deepDigState.sessionId)
                .WriteInt32(0)
                .WriteInt32(0)
                .Build();
            SendPacket(0, endPacket.data(), static_cast<DWORD>(endPacket.size()));
        }
        
        g_deepDigState.completedCount++;
        
        if (i < actualCount - 1) {
            Sleep(1000);
        }
    }
    
    // 发送完成消息
    if (g_hWnd) {
        PostMessage(g_hWnd, AppMessage::kDailyTaskComplete, 
                    g_deepDigState.completedCount, actualCount);
    }
    
    g_deepDigState.autoMode = false;
    delete data;
    return 0;
}

// 深度挖宝 - 发送结束游戏封包
// Opcode: DEEP_DIG_SEND(1185525), Params: 12, Body: [4, 0, 4, sessionId, 0, 0]
static BOOL SendDeepDigEndPacket(int sessionId, int score) {
    (void)score;
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::DEEP_DIG_SEND)
        .SetParams(12)
        .WriteInt32(4)
        .WriteInt32(0)
        .WriteInt32(4)
        .WriteInt32(sessionId)
        .WriteInt32(0)
        .WriteInt32(0)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 处理深度挖宝响应
void ProcessDeepDigResponse(const GamePacket& packet) {
    if (!g_deepDigState.waitingResponse) return;
    if (packet.body.size() < 8) return;
    
    const BYTE* body = packet.body.data();
    size_t offset = 0;
    
    int32_t act = ReadInt32LE(body, offset);
    int32_t result = ReadInt32LE(body, offset);
    
    if (act == 0) {
        g_deepDigState.sessionId = result;
        g_deepDigState.waitingResponse = false;
        
        StartManagedThreadOnce(L"delay_deep_dig_end", [](LPVOID) -> DWORD {
            if (!ManagedThread::SleepCancellable(500)) {
                return 0;
            }
            SendDeepDigEndPacket(g_deepDigState.sessionId, 4);
            return 0;
        }, nullptr);
    }
}

// 每日卡牌
// Opcode: 1184815, Params: 22
