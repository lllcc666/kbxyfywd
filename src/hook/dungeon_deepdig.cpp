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
    g_deepDigState.waitingQuery.store(true);

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
    if (!g_deepDigState.waitingQuery.load()) return;
    if (packet.body.size() < 12) return;
    
    const BYTE* body = packet.body.data();
    size_t offset = 0;
    
    // 读取 UTF 字符串
    if (offset + 2 > packet.body.size()) return;
    uint16_t strLen = ReadUInt16LE(body, offset);
    offset += 2 + strLen;
    
    // 读取剩余次数
    if (offset + 4 > packet.body.size()) return;
    g_deepDigState.remainingCount.store(ReadInt32LE(body, offset));
    
    g_deepDigState.waitingQuery.store(false);
}

// 处理深度挖宝响应
void ProcessDeepDigResponse(const GamePacket& packet) {
    if (!g_deepDigState.waitingResponse.load()) return;
    // 1315861 是所有小游戏共用的回包 opcode（深挖 12、知识小天地 10003，
    // 以及用户手动开的任何一局），必须按 gameType 过滤。否则别的小游戏
    // 回的 act==0 会被当成本局的 sessionId 塞进结算包。
    if (packet.params != static_cast<uint32_t>(Opcode::GAME_TYPE_DEEP_DIG)) return;
    if (packet.body.size() < 8) return;

    const BYTE* body = packet.body.data();
    size_t offset = 0;

    int32_t act = ReadInt32LE(body, offset);
    int32_t result = ReadInt32LE(body, offset);

    // act == 0 才是正常开局，result 就是 sessionId（littlegameView.as:137-150）。
    // act == -1 服务端关连接、act == 4 是结算结果，都说明这局没法继续，
    // 一律清标志让调用方别再等，避免异常时白等满 5 秒。
    // 结算包由 DailyTaskThreadProc 统一发一次，这里绝不补发。
    if (act == 0) {
        g_deepDigState.sessionId.store(result);
    }
    g_deepDigState.waitingResponse.store(false);
}
