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

CRITICAL_SECTION g_towerCS;  // 玄塔临界区

// 玄塔活动全局变量
static bool g_towerAutoMode = false;        // 自动模式
static int g_towerDiceCount = 0;            // 实际骰子数量
static int g_towerRemainingDice = 0;       // 剩余骰子数
bool g_towerMapEntered = false;     // 是否已进入地图
static bool g_towerBattleStarted = false;  // 是否已开始战斗
static bool g_towerResultReceived = false; // 是否收到查询结果响应
static bool g_towerWaitingResponse = false; // 等待响应
static HANDLE g_towerThread = nullptr;      // 玄塔线程句柄

// 投掷骰子结果追踪
static bool g_towerThrowResponseReceived = false;  // 是否收到投掷响应
static bool g_towerLastThrowSuccess = false;       // 最后一次投掷是否成功
static int g_towerLastThrowStep = 0;               // 最后一次投掷步数
static int g_towerLastThrowNodeType = 0;           // 最后一次投掷节点类型

// CHECK_INFO 响应追踪
static bool g_towerCheckInfoReceived = false;      // 是否收到CHECK_INFO响应
static int g_towerCheckInfoNbones = 0;             // CHECK_INFO返回的普通骰子数
static int g_towerPassFlag = 0;                    // 通关标志（0=未通关，1=已通关）
static bool g_towerIsCompleted = false;            // 玄塔是否已完成（nodetype==8）

// BUY_BONES 响应追踪
static bool g_towerBuyBonesReceived = false;       // 是否收到BUY_BONES响应
static bool g_towerBuyBonesSuccess = false;        // 购买是否成功
static int g_towerBuyBonesNum = 0;                 // 购买后的骰子数量

// ============ 玄塔寻宝功能实现 ============

// 存入妖怪仓库
// Opcode: 1187333, Params: 1, Body: [param1, param2]
BOOL SendPutToSpiritStorePacket(int param1, int param2) {
    auto packet = PacketBuilder()
        .SetOpcode(1187333)
        .SetParams(1)
        .WriteInt32(param1)
        .WriteInt32(param2)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 法宝操作（贝贝）
// Opcode: 1186832, Params: type (0=召唤, 2=收回), Body: [userId, 0]
BOOL SendFabaoPacket(int type, uint32_t userId) {
    auto packet = PacketBuilder()
        .SetOpcode(1186832)
        .SetParams(static_cast<uint32_t>(type))
        .WriteUInt32(userId)
        .WriteInt32(0)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 查询玄塔信息
// Opcode: 1184833, Params: 341, Body: [1] (CHECK_INFO)
BOOL SendTowerCheckInfoPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1184833)
        .SetParams(341)
        .WriteInt32(1)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 购买骰子（18个）
// Opcode: 1184833, Params: 341, Body: [6, 18] (BUY_BONES, 数量18)
BOOL SendBuyDicePacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1184833)
        .SetParams(341)
        .WriteInt32(6)
        .WriteInt32(18)
        .Build();
    BOOL result = SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
    if (result) {
        UIBridge::Instance().UpdateHelperText(L"已购买18个骰子");
    }
    return result;
}

// 购买骰子（5个）
// Opcode: 1184833, Params: 341, Body: [6, 5] (BUY_BONES, 数量5)
BOOL SendBuyDice5Packet() {
    auto packet = PacketBuilder()
        .SetOpcode(1184833)
        .SetParams(341)
        .WriteInt32(6)
        .WriteInt32(5)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 跳转到玄塔地图
// Opcode: 1184313, Params: 16006
BOOL SendEnterTowerMapPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::ENTER_SCENE_SEND)
        .SetParams(16006)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 投掷骰子
// Opcode: 1184833, Params: 341, Body: [2] (THROW_BONES)
BOOL SendThrowBonesPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1184833)
        .SetParams(341)
        .WriteInt32(2)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 退出世界（回家第一步）
// Opcode: 1184002, Params: userId
BOOL SendExitScenePacket(uint32_t userId) {
    auto packet = PacketBuilder()
        .SetOpcode(1184002)
        .SetParams(userId)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 查询玄塔结果
// Opcode: 1184833, Params: 341, Body: [4] (REACH_RESULT)
BOOL SendTowerResultPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(1184833)
        .SetParams(341)
        .WriteInt32(4)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 重新进入玄塔地图（回家第二步）
// Opcode: 1184313, Params: 1002
BOOL SendReenterTowerMapPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::ENTER_SCENE_SEND)
        .SetParams(1002)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

// 玄塔自动跑塔线程
// 流程：循环投掷20次（带响应等待）→ 退出回家
DWORD WINAPI TowerAutoThreadProc(LPVOID lpParam) {
    std::wstring* pStatus = reinterpret_cast<std::wstring*>(lpParam);
    bool success = true;
    int successCount = 0;

    // 辅助函数：更新状态到UI
    auto updateStatus = [&](const std::wstring& status) {
        *pStatus = status;
        UIBridge::Instance().UpdateHelperText(status);
    };

    // 辅助函数：投掷单次骰子并等待响应
    // 返回值：true=成功, false=失败
    auto throwOnceAndWait = [&]() -> bool {
        // 重置投掷响应标志
        {
            CriticalSectionLock lock(g_towerCS);
            g_towerThrowResponseReceived = false;
            g_towerLastThrowSuccess = false;
        }

        // 发送投掷骰子封包
        if (!SendThrowBonesPacket()) {
            return false;
        }

        // 等待 THROW_BONES 响应（最多3秒）
        int waitCount = 0;
        bool received = false;
        bool throwSuccess = false;
        
        while (waitCount < 30) {
            {
                CriticalSectionLock lock(g_towerCS);
                received = g_towerThrowResponseReceived;
                if (received) {
                    throwSuccess = g_towerLastThrowSuccess;
                }
            }
            if (received) break;
            Sleep(100);
            waitCount++;
        }

        return received && throwSuccess;
    };

    // 初始化状态
    {
        CriticalSectionLock lock(g_towerCS);
        g_towerDiceCount = 0;
        g_towerRemainingDice = 0;
        g_towerThrowResponseReceived = false;
        g_towerLastThrowSuccess = false;
        g_towerCheckInfoReceived = false;
        g_towerBuyBonesReceived = false;
        g_towerBuyBonesSuccess = false;
        g_towerBuyBonesNum = 0;
        g_towerPassFlag = 0;           // 重置通关标志
        g_towerIsCompleted = false;    // 重置完成标志
    }

    // ==================== 一键跑塔流程开始 ====================
    
    // 步骤1: 循环投掷骰子直到耗尽
    updateStatus(L"开始投掷骰子...");
    int totalAttempts = 0;
    int maxTotalAttempts = 100;  // 防止无限循环的安全限制
    
    while (totalAttempts < maxTotalAttempts) {
        // 1. 查询玄塔信息，获取当前骰子数量
        {
            CriticalSectionLock lock(g_towerCS);
            g_towerCheckInfoReceived = false;
        }
        SendTowerCheckInfoPacket();
        
        // 等待 CHECK_INFO 响应（最多3秒）
        int waitCount = 0;
        bool checkReceived = false;
        while (waitCount < 30) {
            {
                CriticalSectionLock lock(g_towerCS);
                checkReceived = g_towerCheckInfoReceived;
            }
            if (checkReceived) break;
            Sleep(100);
            waitCount++;
        }
        
        if (!checkReceived) {
            updateStatus(L"错误：查询骰子数量超时");
            success = false;
            goto cleanup;
        }
        
        // 2. 获取当前骰子数量和通关状态
        int currentDice = 0;
        int passFlag = 0;
        bool isCompleted = false;
        {
            CriticalSectionLock lock(g_towerCS);
            currentDice = g_towerRemainingDice;
            passFlag = g_towerPassFlag;
            isCompleted = g_towerIsCompleted;
        }
        
        // 3. 检查是否已通关（passflag==1 或之前已标记完成）
        if (passFlag == 1 || isCompleted) {
            updateStatus(L"玄塔已通关，停止投掷");
            break;
        }
        
        // 4. 如果骰子数量为0，退出循环
        if (currentDice <= 0) {
            updateStatus(L"骰子已耗尽，停止投掷");
            break;
        }
        
        // 4. 投掷一次骰子
        totalAttempts++;
        updateStatus(L"投掷骰子（剩余 " + std::to_wstring(currentDice) + L" 个）...");
        
        // 发送 THROW_BONES 并等待响应
        if (throwOnceAndWait()) {
            successCount++;
        }
        Sleep(50);
        
        // 检查是否通关（nodetype == 8）
        {
            CriticalSectionLock lock(g_towerCS);
            if (g_towerIsCompleted || g_towerLastThrowNodeType == 8) {
                updateStatus(L"已到达玄塔终点，通关！");
                // 发送 REACH_RESULT 后退出
                SendTowerResultPacket();
                Sleep(100);
                break;
            }
        }
        
        // 发送 REACH_RESULT
        SendTowerResultPacket();
        Sleep(100);
    }
    
    if (totalAttempts >= maxTotalAttempts) {
        updateStatus(L"警告：达到最大投掷次数限制（100次）");
    }
    
    updateStatus(L"投掷完成，成功 " + std::to_wstring(successCount) + L" 次");

    // 步骤2和3: 自动回家（如果勾选）
    if (g_autoGoHome.load()) {
        Sleep(200);

        // 步骤2: 退出场景回家
        updateStatus(L"步骤2: 退出场景回家...");
        if (!SendExitScenePacket(g_userId)) {
            updateStatus(L"错误：退出场景失败！");
            success = false;
            goto cleanup;
        }
        Sleep(800);

        // 步骤3: 重新进入玄塔地图（刷新活动状态）
        updateStatus(L"步骤3: 重新进入玄塔地图...");
        if (!SendReenterTowerMapPacket()) {
            updateStatus(L"错误：重新进入地图失败！");
            success = false;
            goto cleanup;
        }
        Sleep(800);
    }

cleanup:
    // 重置状态标志变量
    {
        CriticalSectionLock lock(g_towerCS);
        g_towerMapEntered = false;
        g_towerBattleStarted = false;
        g_towerResultReceived = false;
        g_towerThrowResponseReceived = false;
        g_towerCheckInfoReceived = false;
        g_towerBuyBonesReceived = false;
        g_towerAutoMode = false;
        g_towerPassFlag = 0;           // 重置通关标志
        g_towerIsCompleted = false;    // 重置完成标志
    }

    // 根据成功/失败状态更新 UI
    if (success) {
        updateStatus(L"✓ 一键玄塔完成！成功投掷 " + std::to_wstring(successCount) + L" 次");
    } else {
        updateStatus(L"✗ 一键玄塔失败，请查看日志");
    }

    return 0;
}

// 一键玄塔完整流程
BOOL StartOneKeyTowerPacket() {
    bool autoMode = false;
    {
        CriticalSectionLock lock(g_towerCS);
        autoMode = g_towerAutoMode;
    }

    if (autoMode) {
        UIBridge::Instance().UpdateHelperText(L"一键玄塔已在运行中，请勿重复点击");
        return FALSE;  // 已经在运行
    }

    if (g_userId == 0) {
        UIBridge::Instance().UpdateHelperText(L"错误：请先进入游戏获取卡布号");
        return FALSE;
    }

    // 初始化状态
    {
        CriticalSectionLock lock(g_towerCS);
        g_towerAutoMode = true;
        g_towerMapEntered = false;
        g_towerBattleStarted = false;
        g_towerResultReceived = false;
        g_towerDiceCount = 0;
        g_towerRemainingDice = 0;
        g_towerThrowResponseReceived = false;
        g_towerLastThrowSuccess = false;
        g_towerCheckInfoReceived = false;
        g_towerBuyBonesReceived = false;
        g_towerBuyBonesSuccess = false;
        g_towerBuyBonesNum = 0;
        g_towerPassFlag = 0;           // 重置通关标志
        g_towerIsCompleted = false;    // 重置完成标志
    }

    std::wstring* pStatus = new(std::nothrow) std::wstring(L"一键玄塔初始化...");
    if (!pStatus) {
        CriticalSectionLock lock(g_towerCS);
        g_towerAutoMode = false;
        return FALSE;
    }

    // 启动玄塔线程
    HANDLE hThread = CreateThread(nullptr, 0, TowerAutoThreadProc, pStatus, 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
        return TRUE;
    }

    delete pStatus;
    {
        CriticalSectionLock lock(g_towerCS);
        g_towerAutoMode = false;
    }
    return FALSE;
}

// 处理玄塔活动响应（在HookedRecv中调用）
void ProcessTowerActivityResponse(const GamePacket& packet) {
    // 处理地图进入响应（Opcode 1315395 = ENTER_SCENE_BACK）
    if (packet.opcode == Opcode::ENTER_SCENE_BACK) {
        CriticalSectionLock lock(g_towerCS);
        g_towerMapEntered = true;
    }

    // 处理战斗开始响应（Opcode 1317120 = BATTLE_START）
    if (packet.opcode == Opcode::BATTLE_START) {
        CriticalSectionLock lock(g_towerCS);
        g_towerBattleStarted = true;
    }

    // 处理玄塔活动响应（支持两个 Opcode：1185569 和 1324097 (TRIAL_BACK), Params = 341）
    if ((packet.opcode == 1185569 || packet.opcode == Opcode::TRIAL_BACK) && packet.params == 341) {
        if (packet.body.size() >= 4) {
            size_t offset = 0;
            int32_t operation = ReadInt32LE(packet.body.data(), offset);

            // CHECK_INFO 响应 (operation = 1)：查询玄塔信息
            // AS3 代码: mbody.nbones = body.readInt(); mbody.sbones = body.readInt(); ...
            // Body 结构：[operation:4][nbones:4][sbones:4][passflag:4][getMonsterTime:4][nowpos:4][buycount:4][noTip:4][nodeList:90*4]
            // 总大小：4 + 7*4 + 90*4 = 392 字节
            if (operation == 1 && packet.body.size() >= 392) {
                int32_t nbones = ReadInt32LE(packet.body.data(), offset);  // 普通骰子数量
                int32_t sbones = ReadInt32LE(packet.body.data(), offset);  // 特殊骰子数量
                int32_t passflag = ReadInt32LE(packet.body.data(), offset); // 通关标志（0=未通关，1=已通关）
                // 其余字段: getMonsterTime, nowpos, buycount, noTip, nodeList[90]

                CriticalSectionLock lock(g_towerCS);
                // 更新骰子数量（上限20）
                if (nbones > 20) nbones = 20;
                if (sbones > 20) sbones = 20;
                g_towerDiceCount = nbones;
                g_towerRemainingDice = nbones;
                g_towerCheckInfoNbones = nbones;
                g_towerPassFlag = passflag;  // 存储通关标志
                g_towerCheckInfoReceived = true;
            }
            // REACH_RESULT 响应 (operation = 4)：查询结果
            else if (operation == 4) {
                CriticalSectionLock lock(g_towerCS);
                g_towerResultReceived = true;
            }
            // UPDATE_BONES_NUM 响应 (operation = 9)：服务器通知更新骰子数量
            // AS3 代码: mbody.nbones = body.readInt(); mbody.sbones = body.readInt();
            // Body 结构：[operation:4][nbones:4][sbones:4]
            else if (operation == 9 && packet.body.size() >= 12) {
                int32_t nbones = ReadInt32LE(packet.body.data(), offset);
                int32_t sbones = ReadInt32LE(packet.body.data(), offset);

                CriticalSectionLock lock(g_towerCS);
                if (nbones > 20) nbones = 20;
                if (sbones > 20) sbones = 20;
                g_towerDiceCount = nbones;
                g_towerRemainingDice = nbones;
            }
            // BUY_BONES 响应 (operation = 6)：购买骰子结果
            // AS3 代码: mbody.result = body.readInt(); mbody.num = body.readInt(); mbody.buycount = body.readInt();
            // Body 结构：[operation:4][result:4][num:4][buycount:4]
            // 总大小：4 + 3*4 = 16 字节
            // result: 0=成功, -1=金币不足, -3=已达上限
            // num: 购买后的骰子数量
            // buycount: 今日购买次数
            else if (operation == 6 && packet.body.size() >= 16) {
                int32_t result = ReadInt32LE(packet.body.data(), offset);    // 结果 (0=成功)
                int32_t num = ReadInt32LE(packet.body.data(), offset);       // 骰子数量
                (void)ReadInt32LE(packet.body.data(), offset);               // buycount：占位字段

                CriticalSectionLock lock(g_towerCS);
                g_towerBuyBonesSuccess = (result == 0);
                g_towerBuyBonesNum = num;
                g_towerBuyBonesReceived = true;
                
                // 购买成功时更新骰子数量
                if (result == 0) {
                    if (num > 20) num = 20;
                    g_towerDiceCount = num;
                    g_towerRemainingDice = num;
                }
            }
            // THROW_BONES 响应 (operation = 2)：投掷骰子结果
            // AS3 代码: mbody.result = body.readInt(); mbody.step = body.readInt(); mbody.nodetype = body.readInt(); ...
            // Body 结构：[operation:4][result:4][step:4][nodetype:4][param1:4][param2:4][param3:4]
            // 总大小：4 + 6*4 = 28 字节
            // result: 0=成功, -1=失败
            // step: 投掷步数
            // nodetype: 节点类型 (8=通关终点)
            // param3: 随机骰子点数
            else if (operation == 2 && packet.body.size() >= 28) {
                int32_t result = ReadInt32LE(packet.body.data(), offset);    // 结果 (0=成功, -1=失败)
                int32_t step = ReadInt32LE(packet.body.data(), offset);      // 步数
                int32_t nodetype = ReadInt32LE(packet.body.data(), offset);  // 节点类型
                int32_t param1 = ReadInt32LE(packet.body.data(), offset);
                int32_t param2 = ReadInt32LE(packet.body.data(), offset);
                int32_t param3 = ReadInt32LE(packet.body.data(), offset);    // 随机骰子点数
                (void)param1;
                (void)param2;
                (void)param3;

                CriticalSectionLock lock(g_towerCS);
                // 记录投掷结果
                g_towerLastThrowSuccess = (result == 0);
                g_towerLastThrowStep = step;
                g_towerLastThrowNodeType = nodetype;
                g_towerThrowResponseReceived = true;
                
                // 检测是否通关（nodetype == 8 表示到达终点）
                if (nodetype == 8) {
                    g_towerIsCompleted = true;
                }
                
                // 投掷成功时减少剩余骰子数
                if (result == 0 && g_towerRemainingDice > 0) {
                    g_towerRemainingDice--;
                }
                g_towerResultReceived = true;
            }
        }
    }
}

// 玄塔临界区：hook 初始化/清理时建立与销毁。
void InitializeTowerState() {
    InitializeCriticalSection(&g_towerCS);
}

void CleanupTowerState() {
    DeleteCriticalSection(&g_towerCS);
}
