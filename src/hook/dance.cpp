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

enum DanceFailReason {
    DANCE_FAIL_NONE = 0,
    DANCE_FAIL_ENTER_MAP,      ///< 进入地图失败
    DANCE_FAIL_START_SEND,     ///< 开始封包发送失败
    DANCE_FAIL_START_TIMEOUT,  ///< 未在超时内进入游戏（服务端未响应/未开局）
    DANCE_FAIL_POSITION_TAKEN, ///< 位置被其它小卡布占用或网络不稳（gameState 5/3）
    DANCE_FAIL_THREAD_CREATE,  ///< 跳舞线程创建失败
    DANCE_FAIL_ROUND_TIMEOUT,  ///< 跳舞线程未在限定时间内完成
};

const wchar_t* DanceFailReasonText(int reason) {
    switch (reason) {
        case DANCE_FAIL_ENTER_MAP: return L"进入地图失败";
        case DANCE_FAIL_START_SEND: return L"开始封包发送失败";
        case DANCE_FAIL_START_TIMEOUT: return L"开局超时";
        case DANCE_FAIL_POSITION_TAKEN: return L"位置被占用/网络不稳";
        case DANCE_FAIL_THREAD_CREATE: return L"线程创建失败";
        case DANCE_FAIL_ROUND_TIMEOUT: return L"本轮超时";
        default: return L"未知";
    }
}

struct DanceGameState {
    int processId = 0;           ///< 当前跳舞进程ID
    int serverTime = 0;          ///< 服务器时间
    int serverDifficulty = 5;    ///< 服务器难度
    int todayRewardCnt = 0;      ///< 今日已获得奖励次数
    int drawRewardCnt = 0;       ///< 抽奖次数
    int serverScore = 0;         ///< 服务器分数
    int remainCnt = 3;           ///< 剩余次数
    int gameState = 0;           ///< 游戏状态：0=未开始，2=游戏中
    int clothNum = 0;            ///< 跳舞服装加成
    int counter = 0;             ///< 跳舞计数
    bool waitingResponse = false;///< 等待响应
    bool enteredMap = false;     ///< 是否已进入地图
    bool abnormalRound = false;  ///< 本轮被服务端判为异常（gameState 5/3）
    int lastFailReason = DANCE_FAIL_NONE;  ///< 上一次失败原因（DanceFailReason）
};

static DanceGameState g_danceState;

// ============ 跳舞大赛功能实现 ============

// 进入地图（用于跳舞大赛）
// 封包格式: 44 53 00 00 39 12 12 00 [mapId:4字节] (12字节)
// Opcode: 1184313 (0x121239) 小端序 = 39 12 12 00
// Params: mapId 小端序写入
// 响应 Opcode: 1315395 = OP_CLIENT_REQ_ENTER_SCENE.back
BOOL SendEnterScenePacket(int mapId, uint32_t expectedOpcode, DWORD timeoutMs) {
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::ENTER_SCENE_SEND)
        .SetParams(static_cast<uint32_t>(mapId))
        .Build();

    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()), expectedOpcode, timeoutMs);
}

// ============ 跳舞大赛封包发送函数 ============

// 跳舞大赛 - 发送跳舞活动操作封包
// Opcode: 1187375 (0x121E2F)
// 小端序字节: 2F 1E 12 00
// params=3: 开始游戏, Body=[difficulty]
// params=4: 游戏过程, Body=[serverTime, processid, count, ...states]
// params=5: 结束游戏, Body=null
// params=6: 提交分数, Body=[7, serverScore]
BOOL SendDanceActivityPacketEx(int params, const std::vector<int32_t>& bodyValues) {
    auto packet = PacketBuilder()
        .SetOpcode(1187375)
        .SetParams(static_cast<uint32_t>(params))
        .WriteInt32Array(bodyValues)
        .Build();

    // 如果是开始游戏（params=3），不等待响应（让调用者处理等待）
    uint32_t expectedOpcode = 0;
    DWORD timeoutMs = 0;

    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()), expectedOpcode, timeoutMs);
}

// 跳舞大赛 - 发送阶段封包（进入/退出活动）
// Opcode: 1187368 (0x121E28)
// 小端序字节: 28 1E 12 00
// params=1: 进入活动, Body=[2]
// params=2: 退出活动, Body=[0xFFFFFFFF, userId]
BOOL SendDanceStagePacketEx(int params, const std::vector<int32_t>& bodyValues) {
    auto packet = PacketBuilder()
        .SetOpcode(1187368)
        .SetParams(static_cast<uint32_t>(params))
        .WriteInt32Array(bodyValues)
        .Build();
    
    return SendPacketBytes(packet);
}

// 跳舞大赛 - 进入活动（发送进入封包）
// 封包: 44 53 04 00 28 1E 12 00 01 00 00 00 02 00 00 00
// Opcode: 1184808, Params=1, Body=[2]
BOOL SendDanceEnterPacket() {
    return SendDanceStagePacketEx(1, {2});
}

// 跳舞大赛 - 开始游戏
BOOL SendDanceStartPacket() {
    g_danceState.waitingResponse = true;
    g_danceState.gameState = 0;
    g_danceState.counter = 5;
    return SendDanceActivityPacketEx(3, {g_danceState.serverDifficulty});
}

// 跳舞大赛 - 游戏过程封包（参考易语言代码）
// 封包格式: 44 53 20 00 2F 1E 12 00 04 00 00 00 [时间戳][跳舞计数] 05 00 00 00 02 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 02 00 00 00 02 00 00 00
// Body: [serverTime, danceCounter, 5, 2, 2, 0, 0, 2, 2]
// state=2 表示 Great (150分)
BOOL SendDanceProcessPacketEx(int serverTime, int danceCounter) {
    std::vector<int32_t> body;
    body.push_back(serverTime);        // serverTime (使用当前时间戳)
    body.push_back(danceCounter);      // 跳舞计数
    body.push_back(5);                 // count = 5
    body.push_back(2);                 // state1 = Great
    body.push_back(2);                 // state2 = Great
    body.push_back(0);                 // state3
    body.push_back(0);                 // state4
    body.push_back(2);                 // state5 = Great
    body.push_back(2);                 // state6 = Great

    return SendDanceActivityPacketEx(4, body);
}

// 跳舞大赛 - 结束游戏
// 封包: 44 53 00 00 2F 1E 12 00 05 00 00 00
// Opcode: 1185839, Params=5
BOOL SendDanceEndPacketEx() {
    return SendDanceActivityPacketEx(5, {});
}

// 跳舞大赛 - 退出活动
// 封包: 44 53 08 00 28 1E 12 00 02 00 00 00 FF FF FF FF [userId]
// Opcode: 1184808, Params=2, Body=[0xFFFFFFFF, userId]
BOOL SendDanceExitPacketEx() {
    return SendDanceStagePacketEx(2, {static_cast<int32_t>(0xFFFFFFFF), static_cast<int32_t>(g_userId)});
}

// 跳舞大赛 - 提交分数
// 封包: 44 53 08 00 2F 1E 12 00 06 00 00 00 07 00 00 00 [serverScore]
// Opcode: 1185839, Params=6, Body=[7, serverScore]
BOOL SendDanceSubmitScorePacket(int serverScore) {
    return SendDanceActivityPacketEx(6, {7, serverScore});
}

// 处理跳舞大赛活动响应
void ProcessDanceActivityResponse(const GamePacket& packet) {
    if (packet.body.size() < 4) {
        return;
    }
    
    uint32_t state = packet.params;
    const BYTE* body = packet.body.data();
    size_t length = packet.body.size();
    size_t offset = 0;
    
    int32_t gameState = ReadInt32LE(body, offset);
    
    if (gameState == 0 || gameState == 2 || gameState == 6) {
        switch (state) {
            case 3:  // 开始游戏响应
                if (offset + 16 <= length) {
                    g_danceState.serverDifficulty = ReadInt32LE(body, offset);
                    g_danceState.serverTime = ReadInt32LE(body, offset);
                    g_danceState.processId = ReadInt32LE(body, offset);
                    g_danceState.clothNum = ReadInt32LE(body, offset);
                    
                    g_danceState.gameState = 2;
                    g_danceState.waitingResponse = false;
                }
                break;
                
            case 4:  // 游戏过程响应
                if (offset + 16 <= length) {
                    offset += 4;  // serverTime
                    int32_t respProcessId = ReadInt32LE(body, offset);
                    offset += 8;  // processId, serverCombo
                    g_danceState.serverScore = ReadInt32LE(body, offset);
                    
                    if (respProcessId >= g_danceState.processId) {
                        g_danceState.processId = respProcessId;
                    }
                    g_danceState.waitingResponse = false;
                }
                break;
                
            case 5:  // 结束游戏响应
                // AS3: serverTime, processid, serverCombo, serverScore, serverExp, todayRewardExpCnt, todayRewardExp, drawRewardCnt
                if (offset + 32 <= length) {
                    offset += 12;  // serverTime(4), processid(4), serverCombo(4)
                    g_danceState.serverScore = ReadInt32LE(body, offset);
                    offset += 8;  // serverScore(4), serverExp(4)
                    g_danceState.todayRewardCnt = ReadInt32LE(body, offset);
                    offset += 8;  // todayRewardExpCnt(4), todayRewardExp(4)
                    g_danceState.drawRewardCnt = ReadInt32LE(body, offset);
                    
                    g_danceState.remainCnt = WpeHook::DANCE_MAX_DAILY_COUNT - g_danceState.todayRewardCnt;
                    if (g_danceState.remainCnt < 0) g_danceState.remainCnt = 0;
                    
                    if (g_ExecuteScriptFunc) {
                        wchar_t script[256];
                        swprintf(script, 256, 
                            L"if(window.updateDanceCount) window.updateDanceCount(%d);",
                            g_danceState.todayRewardCnt);
                        g_ExecuteScriptFunc(script);
                    }
                }
                g_danceState.gameState = 0;
                g_danceState.waitingResponse = false;
                break;
        }
    }
    else if (gameState == 5 || gameState == 3) {
        // AS3：5=“位置已被其它小卡布占用”，3=“网络不太稳定,请重新进入游戏吧”。
        // 二者都是本轮可重试的瞬时失败，不应当作整体失败中断后续次数。
        g_danceState.abnormalRound = true;
        g_danceState.lastFailReason = DANCE_FAIL_POSITION_TAKEN;
        g_danceState.gameState = 0;
        g_danceState.waitingResponse = false;
    }
    else {
        // 其它异常 gameState（AS3 走 Alert("异常"+gameState)）同样按本轮失败处理。
        g_danceState.abnormalRound = true;
        if (g_danceState.lastFailReason == DANCE_FAIL_NONE) {
            g_danceState.lastFailReason = DANCE_FAIL_START_TIMEOUT;
        }
        g_danceState.gameState = 0;
        g_danceState.waitingResponse = false;
    }
}

// 处理跳舞大赛阶段响应 (opcode 1318440)
void ProcessDanceStageResponse(const GamePacket& packet) {
    // 读取 params (mParams)
    uint32_t params = packet.params;
    
    // params == 777 时直接返回
    if (params == 777) return;
    
    // 根据params处理不同阶段
    // params == 0: 初始列表
    // params == 1: 选择物品
    // params == 2: 确认
}

// 跳舞大赛线程函数声明
DWORD WINAPI DanceContestThreadProc(LPVOID lpParam);

// 跳舞大赛单次执行（内部函数）
static BOOL SendDanceContestOnce() {
    // 初始化跳舞计数和状态
    g_danceState.counter = 5;
    g_danceState.gameState = 0;
    g_danceState.waitingResponse = false;
    g_danceState.enteredMap = false;
    g_danceState.abnormalRound = false;
    g_danceState.lastFailReason = DANCE_FAIL_NONE;

    // 1. 循环发送进入地图封包（最多10次）
    int enterCount = 0;
    while (!g_danceState.enteredMap && enterCount < 10) {
        if (SendEnterScenePacket(1028)) {
            // 成功发送并等待响应
            if (g_danceState.enteredMap) {
                break;  // 已进入地图
            }
            Sleep(800);
        }
        // 失败或未进入地图，继续重试
        enterCount++;
    }

    if (!g_danceState.enteredMap) {
        g_danceState.lastFailReason = DANCE_FAIL_ENTER_MAP;
        return FALSE;
    }

    Sleep(300);

    // 2. 发送进入活动封包
    SendDanceEnterPacket();
    Sleep(300);

    // 3. 发送开始游戏封包（不等待响应）
    g_danceState.waitingResponse = true;
    g_danceState.gameState = 0;  // 确保状态重置
    if (!SendDanceStartPacket()) {
        // 发送封包失败
        g_danceState.waitingResponse = false;
        g_danceState.lastFailReason = DANCE_FAIL_START_SEND;
        return FALSE;
    }

    // 4. 等待gameState被设置为2（游戏中），最多等待15秒。
    //    若响应已把本轮判为异常（位置被占/网络不稳），立即结束等待转入重试，
    //    避免白等满 15 秒。
    for (int i = 0; i < 150 && g_danceState.gameState != 2 && !g_danceState.abnormalRound; i++) {
        Sleep(100);
    }

    // 5. 如果状态变为2（游戏中），启动跳舞线程并等待完成
    if (g_danceState.gameState == 2) {
        HANDLE hThread = CreateManagedThread(NULL, 0, DanceContestThreadProc, NULL, 0, NULL);
        if (hThread) {
            const DWORD waitResult = WaitForSingleObject(hThread, 30000);
            CloseHandle(hThread);
            if (waitResult == WAIT_TIMEOUT) {
                g_danceState.lastFailReason = DANCE_FAIL_ROUND_TIMEOUT;
                return FALSE;
            }
            return TRUE;
        }
        g_danceState.lastFailReason = DANCE_FAIL_THREAD_CREATE;
        return FALSE;
    }

    // 未进入游戏中：若响应处理器标记了异常（位置被占/网络不稳），沿用其原因，
    // 否则说明服务端压根没回应，按开局超时处理。
    if (g_danceState.lastFailReason == DANCE_FAIL_NONE) {
        g_danceState.lastFailReason = DANCE_FAIL_START_TIMEOUT;
    }
    return FALSE;
}

// 跳舞大赛主流程（自动执行3次，允许瞬时失败重试）
BOOL SendDanceContestPacket() {
    const int maxRounds = WpeHook::DANCE_MAX_DAILY_COUNT;
    // 每次理论上最多重试 3 次，避免活动不可用时无限循环。
    const int maxAttempts = maxRounds + 3;

    int completedCount = 0;

    // 辅助函数：通过 UIBridge 更新UI计数
    auto updateDanceCountUI = [](int count) {
        std::wstring jsCode = L"if(window.updateDanceCount) window.updateDanceCount(" 
            + std::to_wstring(count) + L");";
        UIBridge::Instance().ExecuteJS(jsCode);
    };

    // 辅助函数：回显本轮失败原因，便于用户判断是位置被占还是网络问题。
    auto reportFailure = [](int reason, int attempt) {
        std::wstring msg = L"跳舞大赛：第 " + std::to_wstring(attempt) +
            L" 次尝试失败（" + DanceFailReasonText(reason) + L"），正在重试...";
        UIBridge::Instance().UpdateHelperText(msg);
    };

    // 初始化UI显示为0
    updateDanceCountUI(0);

    int attempts = 0;
    while (completedCount < maxRounds && attempts < maxAttempts) {
        attempts++;

        if (SendDanceContestOnce()) {
            completedCount++;

            // 每次完成后更新UI计数
            updateDanceCountUI(completedCount);

            // 等待一小段时间确保UI更新
            Sleep(300);
            continue;
        }

        // 本轮失败不中断整批，退出活动后重试。仅「本轮线程超时」例外：此时
        // 跳舞线程可能仍在运行并继续发包，重入会与之并发，故直接结束整批。
        if (g_danceState.lastFailReason == DANCE_FAIL_ROUND_TIMEOUT) {
            UIBridge::Instance().UpdateHelperText(
                L"跳舞大赛：本轮超时，线程状态未清干净，已停止以避免冲突");
            break;
        }

        reportFailure(g_danceState.lastFailReason, attempts);
        SendDanceExitPacketEx();
        Sleep(1200);
    }

    // 最终确保UI显示正确
    updateDanceCountUI(completedCount);

    if (completedCount < maxRounds) {
        std::wstring msg = L"跳舞大赛：完成 " + std::to_wstring(completedCount) +
            L"/" + std::to_wstring(maxRounds) + L" 次，部分尝试未成功";
        UIBridge::Instance().UpdateHelperText(msg);
    }

    return completedCount > 0;
}
// 跳舞大赛线程函数
DWORD WINAPI DanceContestThreadProc(LPVOID lpParam) {
    (void)lpParam;
    // 跳舞计数从5开始，每次+5，直到超过100
    while (g_danceState.counter <= 100) {
        Sleep(220);
        
        int serverTime = static_cast<int>(time(nullptr));
        SendDanceProcessPacketEx(serverTime, g_danceState.counter);
        
        g_danceState.counter += 5;
    }
    
    Sleep(220);
    
    // 发送结束封包两次
    SendDanceEndPacketEx();
    Sleep(220);
    SendDanceEndPacketEx();
    Sleep(220);
    
    // 发送退出封包两次
    SendDanceExitPacketEx();
    Sleep(220);
    SendDanceExitPacketEx();
    Sleep(220);
    
    // 发送提交分数封包
    SendDanceSubmitScorePacket(g_danceState.serverScore);
    
    Sleep(500);
    
    if (g_ExecuteScriptFunc) {
        g_ExecuteScriptFunc(L"if(window.onDanceComplete) window.onDanceComplete();");
    }
    
    // 重置状态，为下一次执行做准备
    g_danceState.gameState = 0;
    g_danceState.waitingResponse = false;
    return 0;
}

// 日常活动任务数据结构

// 一键完成日常活动的线程函数

// 进图标记：hook 核心的 ENTER_SCENE_BACK 回包里置位。
void SetDanceMapEntered(bool entered) {
    g_danceState.enteredMap = entered;
}
