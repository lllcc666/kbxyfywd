#include <windows.h>

#include <cstdint>
#include <vector>

#include "battle_packets.h"
#include "packet_io.h"
#include "packet_builder.h"

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
    return SendPacketBytes(packet);
}

BOOL SendNormalBattleEndPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::BATTLE_END_SEND)
        .SetParams(0)
        .Build();
    return SendPacketBytes(packet);
}

BOOL SendBattleReadyPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::BATTLE_READY)
        .SetParams(0)
        .Build();
    return SendPacketBytes(packet);
}

BOOL SendBattlePlayOverPacket() {
    // token 统一由 HookedSend 在真正发出 BATTLE_PLAY_OVER 时自增；
    // 这里不要再加一次，否则同一个包会让 token 跳 2。
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::BATTLE_PLAY_OVER)
        .SetParams(0)
        .Build();
    return SendPacketBytes(packet);
}
