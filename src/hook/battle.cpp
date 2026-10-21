#include <windows.h>

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

BOOL SendBattlePacket(uint32_t spiritId, uint32_t useId, uint8_t extraParam, uint32_t forcedCounter) {
    uint32_t counter = forcedCounter != 0 ? forcedCounter : g_battleCounter.load();
    if (counter == 0) {
        counter = 1;
    }

    PacketBuilder builder;
    builder.SetOpcode(Opcode::CLICK_NPC);

    if (extraParam == 0) {
        // AS3 BattleControl.onClientNpc：Params=sid，Body=[counter]，
        // 带 useid 时追加第二个 Body 字段，而不是把 useid 编进 Params。
        builder.SetParams(spiritId)
               .WriteUInt32(counter);
        if (useId != 0) {
            builder.WriteUInt32(useId);
        }
    } else {
        // 任务区水魂等历史特殊入口的 Params 布局已经由抓包验证，保留其
        // 专用编码，不与普通 NPC/Boss 入口混用。
        const uint32_t params = (spiritId & 0xFF) |
                                ((static_cast<uint32_t>(extraParam) & 0xFF) << 8) |
                                ((useId & 0xFF) << 24);
        builder.SetParams(params)
               .WriteUInt32(counter);
    }

    const std::vector<uint8_t> packet = builder.Build();
    if (packet.empty()) {
        return FALSE;
    }
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}
