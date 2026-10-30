#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>

#include "battle_six.h"
#include "byte_reader.h"
#include "hook_shared.h"
#include "packet_builder.h"
#include "ui_bridge.h"
#include "wpe_hook_internal.h"

CRITICAL_SECTION g_towerCS;  // 玄塔临界区

// 玄塔"已离开场景"标记：EXIT_WORLD 回包置位，进场景回包清零。
// 不进 g_towerCS，收包热路径上直接存原子量。
std::atomic<bool> g_towerMapLeft{false};

// 玄塔活动全局变量
// 只投普通骰子：如意骰子归用户手动操作，这里只读它的数量用于提示。
static bool g_towerAutoMode = false;        // 自动模式
static int g_towerSpecDiceCount = 0;        // 如意骰子数量（不投，仅用于提示）
static int g_towerRemainingDice = 0;       // 剩余普通骰子数

// 塔顶判定：玄塔共 90 格（0..89），9 层以后到 89 即终点。
// 依据 AS3 FindTowerCache.as:137 的 if(this._nowpos >= 89) this._nowpos = 89;
static constexpr int kTowerTopNodeIndex = 89;

// 投掷骰子结果追踪
static bool g_towerThrowResponseReceived = false;  // 是否收到投掷响应
static bool g_towerLastThrowSuccess = false;       // 最后一次投掷是否成功

// CHECK_INFO 响应追踪
static bool g_towerCheckInfoReceived = false;      // 是否收到CHECK_INFO响应
static int g_towerPassFlag = 0;                    // 通关标志（0=未通关，1=已通关）
static int g_towerNowPos = 0;                      // 当前所在格（0..89）

// 骰子数量的"权威版本号"：CHECK_INFO / UPDATE_BONES_NUM / BUY_BONES 每写一次就 +1。
// 投掷成功时本地先减 1 是"预扣"，但如果服务器在这期间已经推了权威数量（op9），
// 再减就成了重复扣减、少投一次。用版本号对一下，权威值更新过就不减。
static uint32_t g_towerBonesGen = 0;               // 权威数量版本号
static uint32_t g_towerThrowGenAtSend = 0;         // 本次投掷发包时的版本号

// 投掷窗口：op2/op3 只在"已发包、还没收到 op2"这段时间里才认。
// 不设窗口的话，上一投迟到的回包会被记到这一投头上，step/result 全错位。
static bool g_towerThrowInFlight = false;
// 本次投掷期间是否已收到 op3（跳层通知，绝对定位）：收到后 op2 的 step 就不能再累加
static bool g_towerJumpSeenForThrow = false;

// 查询玄塔信息
// Opcode: 旧活动总线, Params: 341, Body: [1] (CHECK_INFO)
// 带真正的回包等待：回包 opcode 与 params 都能对上（与 trial.cpp:109 同款），
// 省掉原来 3 秒盲轮询，也不会误收其它模块的 341 包。
BOOL SendTowerCheckInfoPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::ACTIVITY_QINGYANG_SEND)
        .SetParams(341)
        .WriteInt32(1)
        .Build();
    return SendPacketBytes(packet, Opcode::TRIAL_BACK, 3000, 341, true);
}

// 购买普通骰子
// Opcode: 旧活动总线, Params: 341, Body: [6, num] (BUY_BONES, 数量)
// 返回 TRUE 只代表封包发出去了；买没买到由 operation==6 回包的 result 决定，
// 结果文案在 ProcessTowerActivityResponse 里按 result 分支给出。
BOOL SendBuyDicePacket() {
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::ACTIVITY_QINGYANG_SEND)
        .SetParams(341)
        .WriteInt32(6)
        .WriteInt32(18)
        .Build();
    return SendPacketBytes(packet);
}

// 投掷普通骰子
// Opcode: 旧活动总线, Params: 341, Body: [2] (THROW_BONES)
BOOL SendThrowBonesPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::ACTIVITY_QINGYANG_SEND)
        .SetParams(341)
        .WriteInt32(2)
        .Build();
    // 同样带回包等待，THROW_BONES 的 operation==2 回包是本模块解析的那一条
    return SendPacketBytes(packet, Opcode::TRIAL_BACK, 3000, 341, true);
}

// 退出世界（回家第一步）
// Opcode: 退出世界, Params: userId
BOOL SendExitScenePacket(uint32_t userId) {
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::EXIT_WORLD_SEND)
        .SetParams(userId)
        .Build();
    return SendPacketBytes(packet);
}

// 查询玄塔结果
// Opcode: 旧活动总线, Params: 341, Body: [4] (REACH_RESULT)
// 回包无正文（AS3 只用来触发 resultEndBack），我们不解析，所以不等回包，
// 免得服务端偶尔不回时每投一次白等 3 秒。
BOOL SendTowerResultPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::ACTIVITY_QINGYANG_SEND)
        .SetParams(341)
        .WriteInt32(4)
        .Build();
    return SendPacketBytes(packet);
}

// 回到个人空间（回家第二步）
// Opcode: 进场景, Params: 1002
// 注意 1002 是个人空间，不是玄塔地图：AS3 MapControl:1232/1236 回城送的就是 1002。
BOOL SendReenterTowerMapPacket() {
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::ENTER_SCENE_SEND)
        .SetParams(1002)
        .Build();
    return SendPacketBytes(packet);
}

// 玄塔自动跑塔线程
// 流程：查一次状态 → 循环投掷普通骰子直到耗尽/到顶 → 退出回家
DWORD WINAPI TowerAutoThreadProc(LPVOID lpParam) {
    (void)lpParam;
    bool success = true;
    int successCount = 0;

    // 运行标志的兜底：异常被别的层 catch(...) 吞掉时也要把 g_towerAutoMode 放下，
    // 否则一键玄塔会永远停在"已在运行中，请勿重复点击"。
    struct AutoModeGuard {
        ~AutoModeGuard() {
            CriticalSectionLock lock(g_towerCS);
            g_towerAutoMode = false;
        }
    } autoModeGuard;

    // 辅助函数：更新状态到UI
    auto updateStatus = [](const std::wstring& status) {
        UIBridge::Instance().UpdateHelperText(status);
    };

    // 投掷结果：只有服务器明确回了 op2 才算"成功/被拒"；
    // 3 秒没回包是"结果未知"——这时服务器多半已经扣骰并推进了位置，
    // 当失败重投等于白扣一个骰子，必须先校准再继续。
    enum class ThrowOutcome { kSuccess, kRejected, kUnknown };

    // 辅助函数：投掷单次普通骰子并等待响应
    auto throwOnceAndWait = [&]() -> ThrowOutcome {
        {
            CriticalSectionLock lock(g_towerCS);
            g_towerThrowResponseReceived = false;
            g_towerLastThrowSuccess = false;
            g_towerThrowGenAtSend = g_towerBonesGen;  // 记住发包时的权威版本号
            g_towerJumpSeenForThrow = false;
            g_towerThrowInFlight = true;              // 打开投掷窗口，只认这段时间里的 op2/op3
        }

        SendThrowBonesPacket();  // 发没发出去都从这里开始等，回包超时按"未知"处理

        // 回包已到（或已超时），业务解析在分发线程上，补一段等待等 operation==2 落值。
        // 最多等 2 秒：正常情况下 throw 回包紧跟 UPDATE_BONES_NUM 到达，先后不定。
        bool lastSuccess = false;
        const WaitOutcome outcome = WaitForCondition(
            [&lastSuccess]() {
                CriticalSectionLock lock(g_towerCS);
                if (!g_towerThrowResponseReceived) {
                    return false;
                }
                lastSuccess = g_towerLastThrowSuccess;
                return true;
            },
            2000,
            20);
        if (outcome == WaitOutcome::Signalled) {
            return lastSuccess ? ThrowOutcome::kSuccess : ThrowOutcome::kRejected;
        }

        // 放弃这次等待：关掉窗口，别让迟到的 op2 记到下一投头上
        {
            CriticalSectionLock lock(g_towerCS);
            g_towerThrowInFlight = false;
        }
        return ThrowOutcome::kUnknown;
    };

    // 初始化状态
    {
        CriticalSectionLock lock(g_towerCS);
        g_towerSpecDiceCount = 0;
        g_towerRemainingDice = 0;
        g_towerThrowResponseReceived = false;
        g_towerLastThrowSuccess = false;
        g_towerCheckInfoReceived = false;
        g_towerPassFlag = 0;
        g_towerNowPos = 0;
    }

    // ==================== 一键跑塔流程开始 ====================

    // 只查一次状态：骰子数之后由 UPDATE_BONES_NUM(operation=9) 与本地投掷实时维护，
    // 不必每次投掷前都发一遍 CHECK_INFO 再等 3 秒。
    updateStatus(L"查询玄塔状态...");
    {
        CriticalSectionLock lock(g_towerCS);
        g_towerCheckInfoReceived = false;
    }
    if (!SendTowerCheckInfoPacket()) {
        updateStatus(L"错误：查询玄塔信息超时");
        success = false;
        goto cleanup;
    }

    // 发包函数已等到回包，这里补一小段自旋等业务解析落值
    {
        bool checkReceived = false;
        for (int i = 0; i < 50; ++i) {
            {
                CriticalSectionLock lock(g_towerCS);
                checkReceived = g_towerCheckInfoReceived;
            }
            if (checkReceived) break;
            ManagedThread::SleepCancellable(20);
        }
        if (!checkReceived) {
            updateStatus(L"错误：查询骰子数量超时");
            success = false;
            goto cleanup;
        }
    }

    // 开跑前看一次达标状态与骰子库存
    {
        int passFlag = 0;
        int nowPos = 0;
        int normalDice = 0;
        int specDice = 0;
        {
            CriticalSectionLock lock(g_towerCS);
            passFlag = g_towerPassFlag;
            nowPos = g_towerNowPos;
            normalDice = g_towerRemainingDice;
            specDice = g_towerSpecDiceCount;
        }

        if (passFlag == 1 || nowPos >= kTowerTopNodeIndex) {
            updateStatus(L"玄塔已通关，今日无需再投");
            goto cleanup;
        }
        if (normalDice <= 0) {
            // 如意骰子不做自动投掷，只提示用户自己手动处理
            if (specDice > 0) {
                updateStatus(L"普通骰子已用完（如意骰子 " + std::to_wstring(specDice) +
                             L" 个，请手动使用）");
            } else {
                updateStatus(L"骰子已耗尽，请先购买");
            }
            goto cleanup;
        }
    }

    // 步骤1: 循环投掷普通骰子直到耗尽 / 到顶 / 连续失败
    updateStatus(L"开始投掷骰子...");
    {
        // 骰子单次上限 20（服务器硬上限），40 次是给中途补发留的余量。
        // 真正的退出条件是下面三条；原 100 次上限永远触发不到，是死代码。
        const int maxTotalAttempts = 40;
        // 连续失败熔断：服务器持续回 result==-1（骰子已扣光、活动结算中）时不要空转
        const int kMaxConsecutiveFailures = 5;
        // 回包丢失熔断：连续丢了这么多投就认网络不行，别再瞎投
        const int kMaxUnknownResponses = 2;
        int totalAttempts = 0;
        int consecutiveFailures = 0;
        int unknownResponses = 0;
        bool hitAttemptLimit = false;

        while (totalAttempts < maxTotalAttempts) {
            // 退出流程走 RequestStopAll + JoinAll：这里必须能被打断。
            // 打断不了的后果是 JoinAll 超时后线程被 detach，然后继续往已卸载的钩子上发包、
            // 回调已销毁的 UI —— 那正是"关窗口时闪退"的经典长相。
            if (ManagedThread::StopRequested()) {
                break;
            }
            int currentDice = 0;
            {
                CriticalSectionLock lock(g_towerCS);
                currentDice = g_towerRemainingDice;
            }
            if (currentDice <= 0) {
                updateStatus(L"骰子已耗尽，停止投掷");
                break;
            }

            totalAttempts++;
            updateStatus(L"投掷骰子（剩余 " + std::to_wstring(currentDice) + L" 个）...");

            const ThrowOutcome outcome = throwOnceAndWait();

            if (outcome == ThrowOutcome::kUnknown) {
                // 不知道服务器扣没扣骰、走了几步：先重新查一次状态校准，再决定要不要继续。
                if (++unknownResponses >= kMaxUnknownResponses) {
                    updateStatus(L"错误：连续多次投掷无回包，已停止（网络不稳定）");
                    success = false;
                    break;
                }
                updateStatus(L"投掷回包丢失，重新校准状态...");
                if (SendTowerCheckInfoPacket()) {
                    ManagedThread::SleepCancellable(200);  // 等 op1 的业务解析落值
                }
                continue;
            }

            if (outcome == ThrowOutcome::kRejected) {
                ++consecutiveFailures;
                if (consecutiveFailures >= kMaxConsecutiveFailures) {
                    updateStatus(L"错误：连续 " + std::to_wstring(consecutiveFailures) +
                                 L" 次投掷失败，停止（可能骰子已耗尽或活动结算中）");
                    success = false;
                    break;
                }
                continue;  // 失败不结算，直接重试
            }

            successCount++;
            consecutiveFailures = 0;
            unknownResponses = 0;

            // 到顶判定：位置由 op2 的 step 累加、op3 的绝对定位覆盖，统一封顶 89。
            // nodetype==8 只是"本次投掷触发了终点特效"，不等于真站在 89 格，不能拿它判通关。
            bool reachedTop = false;
            {
                CriticalSectionLock lock(g_towerCS);
                reachedTop = (g_towerNowPos >= kTowerTopNodeIndex);
            }

            // 结算本次移动（AS3 每次移动落地后都会发 REACH_RESULT）
            SendTowerResultPacket();

            if (reachedTop) {
                updateStatus(L"已到达玄塔终点，通关！");
                break;
            }
        }

        if (totalAttempts >= maxTotalAttempts) {
            hitAttemptLimit = true;
            updateStatus(L"警告：达到最大投掷次数限制，已停止");
        }

        bool reachedTop = false;
        {
            CriticalSectionLock lock(g_towerCS);
            reachedTop = (g_towerNowPos >= kTowerTopNodeIndex);
        }
        if (!reachedTop && !hitAttemptLimit) {
            updateStatus(L"投掷完成，成功 " + std::to_wstring(successCount) + L" 次");
        }
    }

    // 步骤2和3: 自动回家（如果勾选）
    if (g_autoGoHome.load() && !ManagedThread::StopRequested()) {
        ManagedThread::SleepCancellable(200);

        // 步骤2: 退出场景回家
        updateStatus(L"步骤2: 退出场景回家...");
        if (!SendExitScenePacket(g_userId)) {
            updateStatus(L"错误：退出场景失败！");
            success = false;
            goto cleanup;
        }
        // 等到真的收到 EXIT_WORLD_BACK 再进下一步，弱网下固定等一段时间不够
        {
            bool left = false;
            for (int i = 0; i < 50; ++i) {
                ManagedThread::SleepCancellable(40);
                if (g_towerMapLeft.load()) {
                    left = true;
                    break;
                }
                if (ManagedThread::StopRequested()) {
                    break;
                }
            }
            if (!left && !ManagedThread::StopRequested()) {
                ManagedThread::SleepCancellable(400);  // 没等到回执就再兜一下，后面进地图时服务器会自己纠正
            }
        }

        // 步骤3: 回到个人空间（1002=个人空间，AS3 MapControl:1232 送的就是它）
        if (ManagedThread::StopRequested()) {
            goto cleanup;
        }
        updateStatus(L"步骤3: 回到个人空间...");
        if (!SendReenterTowerMapPacket()) {
            updateStatus(L"错误：回到个人空间失败！");
            success = false;
            goto cleanup;
        }
        ManagedThread::SleepCancellable(400);
    }

cleanup:
    // 重置状态标志变量
    {
        CriticalSectionLock lock(g_towerCS);
        g_towerThrowResponseReceived = false;
        g_towerThrowInFlight = false;
        g_towerJumpSeenForThrow = false;
        g_towerCheckInfoReceived = false;
        g_towerAutoMode = false;
        g_towerPassFlag = 0;           // 重置通关标志
        g_towerNowPos = 0;
    }

    // 被退出流程打断的收尾不要再回调 UI：这时宿主窗口可能已经在销毁，
    // 从正在收尾的线程里往那边推消息就是踩空（关窗口闪退的典型路径）。
    if (ManagedThread::StopRequested()) {
        return 0;
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
    if (g_userId == 0) {
        UIBridge::Instance().UpdateHelperText(L"错误：请先进入游戏获取卡布号");
        return FALSE;
    }

    // 占位与判定放在同一把锁里：分两步做的话，连点两下会双双读到 false，
    // 起两份跑塔线程互相清状态，还会把对方的投掷结果当自己的。
    bool alreadyRunning = false;
    {
        CriticalSectionLock lock(g_towerCS);
        alreadyRunning = g_towerAutoMode;
        if (!alreadyRunning) {
            g_towerAutoMode = true;
        }
    }
    if (alreadyRunning) {
        UIBridge::Instance().UpdateHelperText(L"一键玄塔已在运行中，请勿重复点击");
        return FALSE;  // 已经在运行
    }

    // 初始化状态
    {
        CriticalSectionLock lock(g_towerCS);
        g_towerSpecDiceCount = 0;
        g_towerRemainingDice = 0;
        g_towerThrowResponseReceived = false;
        g_towerLastThrowSuccess = false;
        g_towerCheckInfoReceived = false;
        g_towerPassFlag = 0;           // 重置通关标志
        g_towerNowPos = 0;
    }
    g_towerMapLeft.store(false);

    // 启动玄塔线程
    // 走受管线程：退出时 RequestStopAll/JoinAll 能收住它，不会留下 detach 线程继续发包。
    if (StartManagedThreadOnce(L"tower_auto", TowerAutoThreadProc, nullptr)) {
        return TRUE;
    }

    {
        CriticalSectionLock lock(g_towerCS);
        g_towerAutoMode = false;
    }
    return FALSE;
}

// 处理玄塔活动响应（在HookedRecv中调用）
void ProcessTowerActivityResponse(const GamePacket& packet) {
    // 退出世界回包：一键玄塔用它确认"家已经回成了"，而不是盲等 800ms
    if (packet.opcode == Opcode::EXIT_WORLD_BACK) {
        g_towerMapLeft.store(true);
    }

    // 玄塔走旧活动总线：Opcode = ACTIVITY_QINGYANG_BACK(1324097, 别名 TRIAL_BACK)，Params = 341
    if (packet.opcode == Opcode::TRIAL_BACK && packet.params == 341) {
        if (packet.body.size() >= 4) {
            size_t offset = 0;
            ByteReader reader(packet.body, offset);
            int32_t operation = reader.I32();

            // CHECK_INFO 响应 (operation = 1)：查询玄塔信息
            // AS3 代码: mbody.nbones = body.readInt(); mbody.sbones = body.readInt(); ...
            // Body 结构：[operation:4][nbones:4][sbones:4][passflag:4][getMonsterTime:4][nowpos:4][buycount:4][noTip:4][nodeList:90*4]
            // 总大小：4 + 7*4 + 90*4 = 392 字节
            if (operation == 1 && packet.body.size() >= 392) {
                int32_t nbones = reader.I32();  // 普通骰子数量
                int32_t sbones = reader.I32();  // 如意骰子数量
                int32_t passflag = reader.I32(); // 通关标志（0=未通关，1=已通关）
                (void)reader.I32();              // getMonsterTime：未使用
                int32_t nowpos = reader.I32();   // 当前所在格（0..89）
                // 其余字段: buycount, noTip, nodeList[90]

                CriticalSectionLock lock(g_towerCS);
                // 更新骰子数量（上限20）
                if (nbones > 20) nbones = 20;
                if (sbones > 20) sbones = 20;
                g_towerRemainingDice = nbones;
                // 如意骰子只记账不投掷，留给用户手动用
                g_towerSpecDiceCount = sbones;
                ++g_towerBonesGen;           // 权威数量已更新
                g_towerPassFlag = passflag;  // 存储通关标志
                g_towerNowPos = nowpos;
                g_towerCheckInfoReceived = true;
            }
            // REACH_RESULT 响应 (operation = 4)：查询结果，无可读字段
            // UPDATE_BONES_NUM 响应 (operation = 9)：服务器通知更新骰子数量
            // AS3 代码: mbody.nbones = body.readInt(); mbody.sbones = body.readInt();
            // Body 结构：[operation:4][nbones:4][sbones:4]
            else if (operation == 9 && packet.body.size() >= 12) {
                int32_t nbones = reader.I32();
                int32_t sbones = reader.I32();

                CriticalSectionLock lock(g_towerCS);
                if (nbones > 20) nbones = 20;
                if (sbones > 20) sbones = 20;
                g_towerRemainingDice = nbones;
                g_towerSpecDiceCount = sbones;
                ++g_towerBonesGen;           // 权威数量已更新
            }
            // BUY_BONES 响应 (operation = 6)：购买骰子结果
            // AS3 代码: mbody.result = body.readInt(); mbody.num = body.readInt(); mbody.buycount = body.readInt();
            // Body 结构：[operation:4][result:4][num:4][buycount:4]
            // 总大小：4 + 3*4 = 16 字节
            // result: 0=成功, -1=金币不足, -3=已达上限
            // num: 购买后的骰子数量
            // buycount: 今日购买次数
            else if (operation == 6 && packet.body.size() >= 16) {
                int32_t result = reader.I32();    // 结果 (0=成功)
                int32_t num = reader.I32();       // 骰子数量
                (void)reader.I32();               // buycount：占位字段

                {
                    CriticalSectionLock lock(g_towerCS);
                    // 购买成功时更新骰子数量
                    if (result == 0) {
                        if (num > 20) num = 20;
                        g_towerRemainingDice = num;
                        ++g_towerBonesGen;  // 权威数量已更新
                    }
                }

                // 文案按 AS3 FindTowerControl.as:162-177 的三个分支给，
                // 不能再用"发包成功=已购买"骗人。
                if (result == 0) {
                    UIBridge::Instance().UpdateHelperText(L"购买骰子成功！");
                } else if (result == -1) {
                    UIBridge::Instance().UpdateHelperText(L"铜钱不足，购买失败");
                } else if (result == -3) {
                    UIBridge::Instance().UpdateHelperText(L"当天购买的普通骰子已经达到上限了");
                } else {
                    UIBridge::Instance().UpdateHelperText(L"购买骰子失败（result=" +
                                                          std::to_wstring(result) + L"）");
                }
            }
            // THROW_BONES 响应 (operation = 2)：投掷骰子结果
            // AS3 代码: mbody.result = body.readInt(); mbody.step = body.readInt(); mbody.nodetype = body.readInt(); ...
            // Body 结构：[operation:4][result:4][step:4][nodetype:4][param1:4][param2:4][param3:4]
            // 总大小：4 + 6*4 = 28 字节
            // result: 0=成功, -1=失败
            // step: 投掷步数
            // nodetype: 本次投掷触发的事件类型（8=终点特效, 9=跳层），不是"落点格类型"，不能当通关判据
            // param3: 随机骰子点数
            else if (operation == 2 && packet.body.size() >= 28) {
                int32_t result = reader.I32();    // 结果 (0=成功, -1=失败)
                int32_t step = reader.I32();      // 投掷步数
                (void)reader.I32();               // nodetype：不参与通关判定
                (void)reader.I32();               // param1
                (void)reader.I32();               // param2
                (void)reader.I32();               // param3

                CriticalSectionLock lock(g_towerCS);
                // 不在投掷窗口里 = 上一投迟到的回包，认了会把 step/result 错记到这一投上
                if (!g_towerThrowInFlight) {
                    return;
                }
                g_towerThrowInFlight = false;

                // 记录投掷结果
                g_towerLastThrowSuccess = (result == 0);
                g_towerThrowResponseReceived = true;

                // 投掷成功：本地同步推进位置与剩余骰子。
                // 与 AS3 FindTowerCache.as:134 addPos 一致，位置封顶在 89。
                if (result == 0) {
                    // 跳层时服务器紧接着发 op3（绝对定位）；op3 先到就不要再累加，
                    // 否则 30/60 这种层起点会被再加一次步数，位置越跑越偏。
                    if (!g_towerJumpSeenForThrow && step > 0) {
                        g_towerNowPos += step;
                    }
                    if (g_towerNowPos >= kTowerTopNodeIndex) {
                        g_towerNowPos = kTowerTopNodeIndex;
                    }
                    // 预扣一个骰子；但如果服务器在本次投掷期间已经推过权威数量（op9），
                    // 那份值里已经扣过了，再减就重复扣减、少投一次。
                    if (g_towerBonesGen == g_towerThrowGenAtSend && g_towerRemainingDice > 0) {
                        g_towerRemainingDice--;
                    }
                }
            }
            // SRV_NOTIFY_JUMP_BACK (operation = 3)：跳层通知，step 是"跳到的格号"，不是步数。
            // AS3 FindTowerModel.as:71 case3 → FindTowerControl:190 playerJumpStep(step)
            //  → TowerTreasureView:211 dataCache.setNewpos(step)（绝对定位）。
            // 漏掉这条，跳层后本地位置会比真实位置小一截，到不了 89，剩下的骰子全白投。
            else if (operation == 3 && packet.body.size() >= 8) {
                int32_t newPos = reader.I32();

                CriticalSectionLock lock(g_towerCS);
                if (newPos < 0) {
                    newPos = 0;
                }
                if (newPos > kTowerTopNodeIndex) {
                    newPos = kTowerTopNodeIndex;
                }
                g_towerNowPos = newPos;
                g_towerJumpSeenForThrow = true;  // 本投的 op2 到货时不要再累加步数
            }
        }
    }
}

// 玄塔临界区：hook 初始化时建立；销毁见 CleanupTowerState（故意不删）。
void InitializeTowerState() {
    InitializeCriticalSection(&g_towerCS);
}

void CleanupTowerState() {
    // 临界区一律不删，理由同 response_waiter：ManagedThread::Start 是先建线程、
    // 后登记进注册表，中间那段窗口里没有任何登记信息可查——窗口里退出就会把
    // g_towerCS 删掉，而刚起来的跑塔线程正拿它当锁。进程马上就退出，泄漏一个
    // CRITICAL_SECTION 没有任何代价，换的是不会踩到已删除的锁。
    OutputDebugStringW(L"CleanupTowerState: 保留 g_towerCS（进程退出即回收）\n");
}
