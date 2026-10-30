#include <windows.h>

#include <atomic>
#include <cstdint>
#include <vector>

#include "byte_reader.h"
#include "hook_shared.h"
#include "packet_builder.h"

// ============ 试炼活动功能实现（重构版） ============

// 试炼活动全局变量
static int g_trialCheckCode = 0;           // 校验码
// 完成标志跨线程读写（试炼线程置位、启动方轮询），必须是 atomic：
// 裸 bool 的写在另一个核上看不到，等待就会白等到超时。
static std::atomic<bool> g_trialComplete{false};

// 试炼类型枚举
enum class TrialType {
    FireWind,  // 火风试炼
    Fire,      // 火焰试炼
    Storm      // 风暴试炼
};

// 试炼配置结构（从AS3代码分析得出）
struct TrialConfig {
    TrialType type;
    int startOp;       // 开始游戏协议ID
    int endOp;         // 结束游戏协议ID
    int award;         // 奖励印记数量
    int checkCodeFactor;  // 校验码系数
    const wchar_t* name; // 试炼名称（用于UI显示）
};

// 三种试炼的配置
static const TrialConfig TRIAL_CONFIGS[] = {
    {TrialType::FireWind, 24, 25, 450, 56, L"火风试炼"},
    {TrialType::Fire,      4,  5, 350, 56, L"火焰试炼"},
    {TrialType::Storm,    14, 15, 450, 72, L"风暴试炼"}
};

// 试炼线程函数声明
DWORD WINAPI TrialThreadProc(LPVOID lpParam);

// 通用试炼封包发送函数
// Opcode: 旧活动总线, Params: 142
// Body格式: [protocolId, ...其他参数]
static BOOL SendTrialPacket(
    const std::vector<int32_t>& bodyValues,
    uint32_t expectedOpcode = 0,
    DWORD timeoutMs = 0,
    uint32_t expectedParams = 0,
    bool matchExpectedParams = false) {
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::ACTIVITY_QINGYANG_SEND)
        .SetParams(142)
        .WriteInt32Array(bodyValues)
        .Build();

    return SendPacket(
        0,
        packet.data(),
        static_cast<DWORD>(packet.size()),
        expectedOpcode,
        timeoutMs,
        expectedParams,
        matchExpectedParams);
}

// 根据试炼类型获取配置
inline const TrialConfig& GetTrialConfig(TrialType type) {
    return TRIAL_CONFIGS[static_cast<int>(type)];
}

// ============ 通用试炼函数 ============

// 开始试炼游戏（通用）
static BOOL SendStartTrialPacket(TrialType type) {
    const TrialConfig& config = GetTrialConfig(type);
    g_trialComplete = false;

    // 发送封包并等待响应
    if (!SendTrialPacket({config.startOp}, Opcode::TRIAL_BACK, 5000, 142, true)) {
        return FALSE;
    }
    
    // 等待试炼线程完成（最多5秒）
    WaitForCondition([]() { return g_trialComplete.load(); }, 5000, 100);

    return g_trialComplete.load();
}

// 结束试炼游戏（通用）
static BOOL SendEndTrialPacket(TrialType type, int result, int award, int checkCode) {
    const TrialConfig& config = GetTrialConfig(type);
    return SendTrialPacket({config.endOp, result, award, checkCode});
}

// ============ 试炼对外接口函数 ============

// 火风试炼
BOOL SendFireWindTrialPacket() { return SendStartTrialPacket(TrialType::FireWind); }

// 火焰试炼
BOOL SendFireTrialPacket() { return SendStartTrialPacket(TrialType::Fire); }

// 风暴试炼
BOOL SendStormTrialPacket() { return SendStartTrialPacket(TrialType::Storm); }

// 试炼线程参数结构
struct TrialThreadParam {
    int checkCode;
    TrialType type;
};

// 处理试炼活动响应（在HookedRecv中调用）
// Opcode: TRIAL_BACK (旧活动总线回包), Params: 142
// Body格式: [protocolId, ...]
void ProcessTrialResponse(const GamePacket& packet) {
    if (packet.body.size() < 4) return;
    
    size_t offset = 0;
    ByteReader reader(packet.body, offset);
    
    // 读取 protocolId
    int32_t protocolId = reader.I32();
    
    // 读取 result
    int32_t result = 0;
    if (offset + 4 <= packet.body.size()) {
        result = reader.I32();
    }
    
    // 根据protocolId处理不同响应
    switch (protocolId) {
        case 4:   // 火焰试炼开始
        case 24:  // 火风试炼开始
        case 14:  // 风暴试炼开始
            if (result == 0 && offset + 4 <= packet.body.size()) {
                // 根据protocolId确定试炼类型
                TrialType type = (protocolId == 4) ? TrialType::Fire :
                                (protocolId == 24) ? TrialType::FireWind : TrialType::Storm;
                const TrialConfig& config = GetTrialConfig(type);
                
                // 获取校验码并计算真正的校验码
                int32_t checkCode = reader.I32();
                g_trialCheckCode = checkCode * config.checkCodeFactor + (g_userId % 100000);
                
                // 启动通用试炼线程
                TrialThreadParam* param = new TrialThreadParam{g_trialCheckCode, type};
                // 走受管线程：退出时统一 RequestStopAll/JoinAll；启动失败要自己回收参数。
                if (!ManagedThread::Start(L"trial", [param]() { TrialThreadProc(param); })) {
                    delete param;
                }
            }
            break;
            
        case 5:   // 火焰试炼结束
        case 25:  // 火风试炼结束
            // 印记数量不需要自己算：奖励结算由服务器完成，这里只推进完成标志。
            if (result == 0 && offset + 4 <= packet.body.size()) {
                (void)reader.I32();  // flame：印记数，仅作占位
            }
            break;
            
        case 15:  // 风暴试炼结束
            if (result == 0 && offset + 4 <= packet.body.size()) {
                (void)reader.I32();  // value：占位字段
            }
            break;
            
        case 1:   // 火焰试炼基础信息
        case 21:  // 火风试炼基础信息
        case 11:  // 风暴试炼基础信息
            if (offset + 12 <= packet.body.size()) {
                (void)reader.I32();  // GameCount（未使用）
                (void)reader.I32();  // CoolTime（未使用）
                (void)reader.I32();  // AwardNum（未使用）
            }
            break;
    }
}

// ============ 通用试炼线程实现 ============

DWORD WINAPI TrialThreadProc(LPVOID lpParam) {
    TrialThreadParam* param = static_cast<TrialThreadParam*>(lpParam);
    int checkCode = param->checkCode;
    const TrialConfig& config = GetTrialConfig(param->type);
    
    // 延时300ms（模拟游戏时间）
    ManagedThread::SleepCancellable(300);
    
    // 发送结束游戏封包: [endOp, -1, award, checkCode]
    SendEndTrialPacket(param->type, -1, config.award, checkCode);
    
    // 标记试炼完成
    g_trialComplete = true;
    
    // 释放参数内存
    delete param;
    
    return 0;
}

