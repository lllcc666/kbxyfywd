#pragma once

#include <atomic>
#include <mutex>
#include <unordered_map>

#include "packet_types.h"
#include "wpe_hook.h"
#include "utils.h"

// 跨模块共享的封包/劫持/玄塔/跳舞状态（定义在各活动模块，hook 核心要用）。
extern std::vector<PACKET> g_PacketList;
extern CriticalSectionScope g_packetListCS;
extern std::vector<HijackRule> g_HijackRules;
extern CriticalSectionScope g_hijackRulesCS;
extern std::atomic<bool> g_bStopAutoSend;
extern std::atomic<bool> g_blockOpcodeSend;
extern std::atomic<bool> g_bHijackEnabled;
extern EXECUTE_SCRIPT_FUNC g_ExecuteScriptFunc;
extern CRITICAL_SECTION g_towerCS;

// 玄塔"已离开场景"标记：收包线程等 EXIT_WORLD 回包时置位，进场景回包时清零。
// 一键玄塔用它确认回家成功，不再靠固定 Sleep 盲等。
extern std::atomic<bool> g_towerMapLeft;

// 怪物列表（g_monsterData）由收包线程整体重建、活动线程读取。
// 统一走快照：拿到的是一份完整拷贝，不会读到正在被改的 vector。
MonsterData SnapshotMonsterData();

// 玄塔临界区的建立与销毁（hook 初始化/清理调用）。
void InitializeTowerState();
void CleanupTowerState();

// 跳舞进图标记：hook 核心的 ENTER_SCENE_BACK 回包里置位。
void SetDanceMapEntered(bool entered);

// 深层挖宝状态：日常任务收尾要发结束包，故跨模块可见。
// 这些字段被收包线程和日常工作线程并发读写，必须是原子量：
// 裸 int 在 x86 上虽然不撕裂，但编译器有权把轮询条件提升出循环。
struct DeepDigState {
    std::atomic<int> sessionId{0};             ///< 开局回包带回的会话ID
    std::atomic<int> remainingCount{0};        ///< open_ui 查询到的剩余次数
    std::atomic<int> completedCount{0};        ///< 已完成局数
    std::atomic<bool> waitingResponse{false};  ///< 等待开局回包
    std::atomic<bool> waitingQuery{false};     ///< 等待次数查询回包
};
extern DeepDigState g_deepDigState;

using ResponseHandler = void (*)(const GamePacket&);

// ============================================================================
// Response waiter (internal)
// ============================================================================

class ResponseWaiter {
public:
    // 定向取消表的表项。放在 public 段，供实现文件的自由函数使用。
    struct CancelEntry {
        uint32_t opcode;
        uint64_t serial;
    };

    static void Initialize();
    static void Cleanup();

    static uint64_t BeginWait();
    static void EndWait();

    static bool WaitForResponse(
        uint32_t expectedOpcode,
        DWORD timeoutMs,
        uint64_t baselineSerial = 0,
        uint32_t expectedParams = 0,
        bool matchExpectedParams = false,
        bool preRegistered = false
    );

    static void NotifyResponse(uint32_t opcode, uint32_t params);
    // CancelWait(nullptr) = 全局取消（停机用）；CancelWait(opcode) = 只取消等该 opcode 的
    // 等待者，不动其它活动的等待。原实现只能全局取消，停一条主线流程会把八卦灵盘、
    // 战斗、进图等所有正在等待的流程一起打断。
    static void CancelWait(uint32_t opcode = 0);

private:
    static CRITICAL_SECTION s_cs;
    static CONDITION_VARIABLE s_cv;
    static uint64_t s_responseSerial;
    static std::atomic<long> s_waitingCount;
    static std::atomic<uint64_t> s_cancelSerial;
};

// ============================================================================
// Response dispatcher
// ============================================================================

class ResponseDispatcher {
public:
    static ResponseDispatcher& Instance();

    BOOL Dispatch(const GamePacket& packet);
    void InitializeDefaultHandlers();

private:
    ResponseDispatcher() = default;
    ~ResponseDispatcher() = default;
    ResponseDispatcher(const ResponseDispatcher&) = delete;
    ResponseDispatcher& operator=(const ResponseDispatcher&) = delete;

    static uint64_t MakeKey(uint32_t opcode, uint32_t params);

    // 无锁版本，仅供 InitializeDefaultHandlers 在启动期单线程调用。
    // 启动期若走 std::mutex，MSVC 会生成 _Mtx_lock，失败时抛
    // _Throw_Cpp_error(5)；WinMain 里没有 catch，Release 下直接
    // std::terminate，用户侧表现为双击毫无反应。启动阶段无并发，
    // 这里不需要锁。
    void ClearUnlocked();
    BOOL RegisterUnlocked(uint32_t opcode, ResponseHandler handler);
    BOOL RegisterUnlocked(uint32_t opcode, uint32_t params, ResponseHandler handler);

    // Key为(opcode << 32 | params)，精确匹配优先于仅opcode匹配。
    std::unordered_map<uint64_t, ResponseHandler> m_handlers;
    std::unordered_map<uint32_t, ResponseHandler> m_opcodeOnlyHandlers;

    std::mutex m_mutex;
};

// hook 是否已安装：活动模块启动前要确认（g_bInitialized 是 hook 内部状态，只读访问走这里）。
bool IsWpeHookInitialized();

// ============================================================================
// Activity state
// ============================================================================

/**
 * @struct ActivityState
 * @brief Activity state base class
 */
