#include "little_game.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdint>
#include <string>

#include "managed_thread.h"
#include "packet_builder.h"
#include "packet_protocol.h"
#include "wpe_hook.h"

extern bool PostScriptToUI(const std::wstring& jsCode);

// 小游戏总线实现。报文格式见 little_game.h 的注释与
// AS3 LittlegameControl.as:57/63/78。

BOOL SendLittleGameStart(int gameType) {
    auto packet = PacketBuilder()
                      .SetOpcode(Opcode::LITTLE_GAME_SEND)
                      .SetParams(static_cast<uint32_t>(gameType))
                      .WriteInt32(0)
                      .WriteInt32(0)
                      .WriteInt32(0)
                      .WriteInt32(0)
                      .WriteInt32(0)
                      .WriteInt32(0)
                      .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

BOOL SendLittleGameStartCompact(int gameType) {
    auto packet = PacketBuilder()
                      .SetOpcode(Opcode::LITTLE_GAME_SEND)
                      .SetParams(static_cast<uint32_t>(gameType))
                      .WriteInt32(0)
                      .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

BOOL SendLittleGameEnd(int gameType, int level, int score, int sessionId) {
    auto packet = PacketBuilder()
                      .SetOpcode(Opcode::LITTLE_GAME_SEND)
                      .SetParams(static_cast<uint32_t>(gameType))
                      .WriteInt32(4)
                      .WriteInt32(level - 1)
                      .WriteInt32(score)
                      .WriteInt32(sessionId)
                      .WriteInt32(0)
                      .WriteInt32(0)
                      .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

BOOL SendLittleGameEndCompact(int gameType, int level, int score, int sessionId) {
    auto packet = PacketBuilder()
                      .SetOpcode(Opcode::LITTLE_GAME_SEND)
                      .SetParams(static_cast<uint32_t>(gameType))
                      .WriteInt32(4)
                      .WriteInt32(level - 1)
                      .WriteInt32(score)
                      .WriteInt32(sessionId)
                      .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

namespace {
// ManagedThread 不按名字拦重复启动（见 managed_thread.h:20-21 的说明），
// 重入护栏得各活动自己加，否则连点两次会起两条线程互发结算包。
std::atomic<bool> g_knowledgeGardenRunning{false};
}  // namespace

void StartKnowledgeGardenAsync(int roundCount, int holdMs) {
    bool expected = false;
    if (!g_knowledgeGardenRunning.compare_exchange_strong(expected, true)) {
        return;  // 上一轮还没跑完，忽略这次点击
    }

    if (roundCount <= 0) {
        roundCount = 1;
    }
    if (holdMs <= 0) {
        holdMs = 300;
    }

    const bool started = ManagedThread::Start(L"knowledge_garden", [roundCount, holdMs]() {
        int sent = 0;
        int failed = 0;
        auto add = [&](BOOL ok) {
            if (ok) {
                ++sent;
            } else {
                ++failed;
            }
        };

        for (int i = 0; i < roundCount; ++i) {
            if (ManagedThread::StopRequested()) {
                break;
            }

            // 开局：gameType 10003 的开局包是 n=4 / body=00000000，
            // 不是深挖那种 n=24 的六元组。
            add(SendLittleGameStartCompact(Opcode::GAME_TYPE_KNOWLEDGE));

            // 知识小天地没有可自动作答的内容，开局后挂机 holdMs 再结算。
            if (!ManagedThread::SleepCancellable(holdMs)) {
                break;
            }

            // 结算：紧凑形态 body = [4, level-1, score, sessionId]。
            add(SendLittleGameEndCompact(Opcode::GAME_TYPE_KNOWLEDGE, 1, 0, 0));

            if (!ManagedThread::SleepCancellable(300)) {
                break;
            }
        }

        g_knowledgeGardenRunning.store(false);

        wchar_t buf[256] = {0};
        swprintf_s(buf, 256, L"知识小天地：已发 %d 包，失败 %d 包", sent, failed);
        std::wstring script = L"if (window.updateDailyTaskStatus) { window.updateDailyTaskStatus(\"";
        script += buf;
        script += L"\", false); }";
        PostScriptToUI(script);
    });

    if (!started) {
        g_knowledgeGardenRunning.store(false);
    }
}
