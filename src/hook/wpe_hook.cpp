/**
 * @file wpe_hook.cpp
 * @brief WPE网络封包拦截模块实现
 * 
 * 实现 Windows Socket API 的 Hook，拦截游戏网络封包
 */

#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <condition_variable>
#include <mutex>
#include <string>
#include <array>
#include <vector>
#include <memory>
#include <tuple>
#include <algorithm>
#include <deque>
#include <map>
#include <unordered_map>
#include <fstream>
#include <thread>
#include <random>
#include <cctype>
#include <chrono>

// 第三方库
#include <MemoryModule.h>
#include <MinHook.h>

// 项目头文件
#include "wpe_hook.h"
#include "wpe_hook_internal.h"
#include "hook_shared.h"
#include "story_task_internal.h"
#include "task_zone_internal.h"
#include "activity_states_internal.h"
#include "activity_minigames.h"
#include "battle_six.h"
#include "dungeon_jump.h"
#include "horse_competition.h"
#include "latest_activities.h"
#include "shuangtai.h"
#include "shuangtai_internal.h"
#include "spirit_collect.h"
#include "story_task.h"
#include "story_task_recipe.h"
#include "utils.h"
#include "packet_parser.h"
#include "packet_builder.h"
#include "app_host.h"
#include "ui_bridge.h"
#include "window_messages.h"

// 坐骑大赛活动ID（如果在packet_parser.h中未定义）
#ifndef HORSE_COMPETITION_ACT_ID
constexpr int HORSE_COMPETITION_ACT_ID = 665;
#endif

// 精魄系统活动ID（如果在packet_parser.h中未定义）
#ifndef SPIRIT_COLLECT_ACT_ID
constexpr int SPIRIT_COLLECT_ACT_ID = 754;
#endif

// 精魄系统响应处理函数声明
void ProcessSpiritPresuresResponse(const GamePacket& packet);
void ProcessSpiritCollectResponse(const GamePacket& packet);
void ProcessSpiritSendSpiritResponse(const GamePacket& packet);
void ProcessSpiritPlayerInfoResponse(const GamePacket& packet);
void ProcessAct808Response(const GamePacket& packet);
static void ProcessStoryTaskTalkResponse(const GamePacket& packet);
static void ProcessStoryNpcTaskListResponse(const GamePacket& packet);
static void ProcessStoryNpcListResponse(const GamePacket& packet);
static void ProcessStoryMagicIndexResponse(const GamePacket& packet);
static void ProcessStoryTaskFinishedResponse(const GamePacket& packet);

extern bool PostScriptToUI(const std::wstring& jsCode);

// 嵌入资源
#include "embedded/minhook_data.h"

// 外部变量声明（来自 packet_parser.cpp）
extern std::unordered_map<int, std::wstring> g_petNames;
extern std::unordered_map<int, std::wstring> g_skillNames;
extern std::unordered_map<int, std::wstring> g_elemNames;
extern std::unordered_map<int, std::wstring> g_geniusNames;
extern std::unordered_map<int, int> g_skillPowers;
extern std::unordered_map<int, int> g_skillRanges;
extern std::unordered_map<int, int> g_petElems;


// MinHook 宏定义
#ifndef MH_ALL_HOOKS
#define MH_ALL_HOOKS (nullptr)
#endif

// ============================================================================
// 前向声明
// ============================================================================

struct ICoreWebView2;
extern ICoreWebView2* g_webview;

// 前向声明
void ProcessMD5CheckAndReply(const std::vector<BYTE>& body, uint32_t params);


// ============================================================================
// 内部命名空间 - 封装实现细节
// ============================================================================

namespace {

// -------------------------
// Opcode 常量（内部使用）
// -------------------------

// 需要阻止发送的 Opcode（灵玉分解过程中）
constexpr uint32_t BLOCKED_OPCODE_QUERY_1 = 1185792;
constexpr uint32_t BLOCKED_OPCODE_QUERY_2 = 1185809;

// -------------------------
// 默认屏蔽的封包（无条件屏蔽，无需用户勾选）
// -------------------------

/**
 * @brief 默认屏蔽的封包结构
 * 用于匹配需要无条件屏蔽的封包
 */
struct DefaultBlockedPacket {
    uint32_t opcode;        ///< Opcode（必须匹配）
    uint32_t params;        ///< Params（0xFFFFFFFF 表示匹配任意值）
    const BYTE* bodyPrefix; ///< Body 前缀匹配（nullptr 表示不匹配 Body）
    size_t bodyPrefixLen;   ///< Body 前缀长度
};

// 默认屏蔽的 Opcode 列表（仅匹配 Opcode）
constexpr uint32_t DEFAULT_BLOCKED_OPCODES[] = {
    1311544,  // 38111400 小端序
    1314916,  // 64161400 小端序
    1311499,  // 0B111400 小端序
};

// 默认屏蔽的完整封包（需要匹配 Opcode + Params，可选匹配 Body）
// 封包1: 44531000021314000100000004000000000000000100000000000000
//   Opcode: 1314050, Params: 1, Body: 04 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00
static const BYTE g_blockBodyPrefix1[] = { 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
// 封包2: 44530400001014000500000000000000
//   Opcode: 1312768, Params: 5, Body: 空

// 默认屏蔽封包列表
static const DefaultBlockedPacket g_defaultBlockedPackets[] = {
    { 1314050, 1, g_blockBodyPrefix1, sizeof(g_blockBodyPrefix1) },  // 匹配完整封包1
    { 1312768, 5, nullptr, 0 },  // 匹配完整封包2
};

/**
 * @brief 检查封包是否在默认屏蔽列表中
 * @param opcode 封包 Opcode
 * @param params 封包 Params
 * @param body 封包 Body 数据
 * @param bodyLen Body 长度
 * @return true 表示应该屏蔽
 */
inline bool IsDefaultBlockedPacket(uint32_t opcode, uint32_t params, 
                                    const BYTE* body, size_t bodyLen) {
    // 1. 检查是否在默认屏蔽 Opcode 列表中
    for (uint32_t blockedOp : DEFAULT_BLOCKED_OPCODES) {
        if (opcode == blockedOp) {
            return true;
        }
    }
    
    // 2. 检查是否匹配完整的默认屏蔽封包
    for (const auto& blocked : g_defaultBlockedPackets) {
        // 匹配 Opcode
        if (opcode != blocked.opcode) continue;
        
        // 匹配 Params（0xFFFFFFFF 表示任意值）
        if (blocked.params != 0xFFFFFFFF && params != blocked.params) continue;
        
        // 匹配 Body 前缀（如果有）
        if (blocked.bodyPrefix != nullptr && blocked.bodyPrefixLen > 0) {
            if (bodyLen < blocked.bodyPrefixLen) continue;
            if (memcmp(body, blocked.bodyPrefix, blocked.bodyPrefixLen) != 0) continue;
        }
        
        return true;
    }
    
    return false;
}

// -------------------------
// 临界区管理（RAII）
// -------------------------

CriticalSectionScope g_pendingPacketsCS;   ///< 暂存封包临界区

// -------------------------
// 全局状态
// -------------------------

bool g_bInitialized = false;                         ///< 初始化标志

std::vector<std::tuple<BOOL, std::vector<BYTE>, DWORD, DWORD>> g_PendingPackets;

// 拦截设置
bool g_bInterceptSend = true;     ///< 是否拦截发送包
bool g_bInterceptRecv = false;    ///< 是否拦截接收包
bool g_bInterceptEnabled = false; ///< 是否启用拦截



// 自动发送状态
bool g_bAutoSendEnabled = false;  ///< 是否启用自动发送


}  // anonymous namespace

// ============================================================================
// 跨模块共享的封包/劫持/脚本回调状态
// 定义在文件作用域；各活动模块通过 wpe_hook_internal.h 访问。
// ============================================================================
// 网络状态
bool g_blockOpcodeSend = false;   ///< 是否阻止特定Opcode发送
// 劫持功能
bool g_bHijackEnabled = false;    ///< 是否启用劫持功能
CriticalSectionScope g_packetListCS;       ///< 封包列表临界区
// 封包列表
std::vector<PACKET> g_PacketList;
std::vector<HijackRule> g_HijackRules;  ///< 劫持规则列表
CriticalSectionScope g_hijackRulesCS;   ///< 劫持规则临界区
volatile bool g_bStopAutoSend = false;  ///< 停止自动发送标志
EXECUTE_SCRIPT_FUNC g_ExecuteScriptFunc = nullptr;  ///< JS执行函数指针

// ============================================================================
// 导出全局变量（在头文件中声明为 extern）
// ============================================================================

std::atomic<uint32_t> g_userId{0};  ///< 卡布号（从进入世界封包获取）

// 最近的游戏套接字：SendPacket 传 0 时兜底，同时供拆分后的活动模块读取。
// 声明在 include/internal/hook_shared.h。
std::atomic<SOCKET> g_LastGameSocket{0};

// 注意：g_hWnd 在 demo.cpp 中定义，这里通过 wpe_hook.h 的 extern 声明使用

// 基础战斗开关 owner：回血、屏蔽战斗、自动回家和战斗校验标记统一由这一组维护。
std::atomic<bool> g_autoHeal{false};  ///< 自动回血
std::atomic<bool> g_blockBattle{false};  ///< 屏蔽战斗
std::atomic<bool> g_autoGoHome{false};   ///< 自动回家（玄塔）
std::atomic<int32_t> g_battleCounter{0};  ///< 战斗counter（用于战斗校验）
std::atomic<bool> g_battleStarted{false};  ///< 是否已进入战斗

// 一键采集 owner：完成标志、自动模式和流程状态统一由这一组维护。
std::atomic<bool> g_collectFinished{false};  ///< 采集完成标志
std::atomic<bool> g_collectAutoMode{false};  ///< 一键采集模式
std::atomic<int> g_collectStatus{0};  ///< 采集状态





// 背包缓存 owner：位置映射和物品列表统一由这一组维护。
std::map<uint32_t, uint32_t> g_itemPositionMap;
std::vector<PackItemInfo> g_packItems;

// 万妖盛会状态变量
BattleSixAutoBattle g_battleSixAuto;




// 登录 key 捕获 owner：原始 key 字符串由这里维护。
std::wstring g_loginKey;                     ///< 整个封包的十六进制字符串（大写）

namespace {

// 跳舞大赛 owner：进程、响应状态和进度统一由该状态对象维护。

// 单次跳舞失败原因：用于在重试日志中说明是哪一步出的问题。


// -------------------------
// 回调函数
PACKET_CALLBACK g_PacketCallback = nullptr;
static std::mutex g_receiveDispatchMutex;
static std::condition_variable g_receiveDispatchCv;
static std::deque<std::vector<GamePacket>> g_receiveDispatchQueue;
static std::atomic<bool> g_receiveDispatchAccepting{false};
static std::thread g_receiveDispatchWorker;
static bool g_receiveDispatchStarted = false;
static bool g_receiveDispatchStop = false;
static constexpr size_t kMaxReceiveDispatchQueue = 64;
// -------------------------
// 辅助函数
// -------------------------

/**
 * @brief 检查是否为游戏封包
 */
inline bool IsGamePacket(const BYTE* pData, DWORD dwSize) {
    if (!pData || dwSize < 2) return false;
    uint16_t magic = static_cast<uint16_t>(pData[0]) | 
                     (static_cast<uint16_t>(pData[1]) << 8);
    return (magic == PacketProtocol::MAGIC_NORMAL || 
            magic == PacketProtocol::MAGIC_COMPRESSED);
}

/**
 * @brief 检查是否应该阻止该封包的发送
 */
inline bool ShouldBlockPacketSend(const BYTE* pData, DWORD dwSize) {
    if (!g_blockOpcodeSend || dwSize < PacketProtocol::HEADER_SIZE) {
        return false;
    }
    
    // 读取 Opcode（小端序，偏移4）
    uint32_t opcode = static_cast<uint32_t>(pData[4]) | 
                      (static_cast<uint32_t>(pData[5]) << 8) |
                      (static_cast<uint32_t>(pData[6]) << 16) |
                      (static_cast<uint32_t>(pData[7]) << 24);
    
    return (opcode == BLOCKED_OPCODE_QUERY_1 || opcode == BLOCKED_OPCODE_QUERY_2);
}

/**
 * @brief 从封包数据读取 Opcode
 */
inline uint32_t ReadOpcode(const BYTE* pData) {
    return static_cast<uint32_t>(pData[4]) | 
           (static_cast<uint32_t>(pData[5]) << 8) |
           (static_cast<uint32_t>(pData[6]) << 16) |
           (static_cast<uint32_t>(pData[7]) << 24);
}

/**
 * @brief 从封包数据读取 Params
 */
inline uint32_t ReadParams(const BYTE* pData) {
    return static_cast<uint32_t>(pData[8]) | 
           (static_cast<uint32_t>(pData[9]) << 8) |
           (static_cast<uint32_t>(pData[10]) << 16) |
           (static_cast<uint32_t>(pData[11]) << 24);
}

/**
 * @brief 从封包数据读取 Body 长度
 */
inline uint16_t ReadBodyLength(const BYTE* pData) {
    return static_cast<uint16_t>(pData[2]) | 
           (static_cast<uint16_t>(pData[3]) << 8);
}

}  // anonymous namespace

// ============================================================================
// 玄塔活动临界区（在匿名命名空间外）
// ============================================================================


// ============================================================================
// 初始化与清理
// ============================================================================

void SetExecuteScriptFunction(EXECUTE_SCRIPT_FUNC func) {
    g_ExecuteScriptFunc = func;
}

BOOL InitializeWpeHook() {
    if (g_bInitialized) {
        return TRUE;
    }

    // 初始化玄塔临界区
    InitializeTowerState();

    // 初始化解析器
    if (!PacketParser::Initialize()) {
        CleanupTowerState();
        return FALSE;
    }

    // 初始化响应处理器
    ResponseDispatcher::Instance().InitializeDefaultHandlers();

    g_bInitialized = true;
    return TRUE;
}

bool IsWpeHookInitialized() {
    return g_bInitialized;
}

VOID CleanupWpeHook() {
    if (!g_bInitialized) return;

    // 停止拦截并标记为未初始化
    g_bInterceptEnabled = false;
    g_bInitialized = false;
    
    // 停止脚本执行回调
    SetExecuteScriptFunction(nullptr);
    
    // TODO: 任务执行器关闭（待完善）
    // TaskExecutor::Instance().Shutdown();
    
    // 等待可能的 Hook 函数退出
    Sleep(10);
    
    // 清理封包数据
    {
        CriticalSectionLock lock(g_packetListCS.Get());
        for (auto& packet : g_PacketList) {
            delete[] packet.pData;
            packet.pData = nullptr;
        }
        g_PacketList.clear();
    }
    
    {
        CriticalSectionLock lock(g_pendingPacketsCS.Get());
        g_PendingPackets.clear();
    }

    // 清理玄塔临界区
    CleanupTowerState();

    // 清理解析器
    PacketParser::Cleanup();
}

// ============================================================================
// 原始函数指针
// ============================================================================

typedef int (WINAPI* SEND_FUNC)(SOCKET s, const char* buf, int len, int flags);
typedef int (WINAPI* RECV_FUNC)(SOCKET s, char* buf, int len, int flags);

SEND_FUNC OriginalSend = nullptr;
RECV_FUNC OriginalRecv = nullptr;

// ============================================================================
// MinHook 函数指针
// ============================================================================

typedef MH_STATUS(WINAPI* PFN_MH_INITIALIZE)(void);
typedef MH_STATUS(WINAPI* PFN_MH_UNINITIALIZE)(void);
typedef MH_STATUS(WINAPI* PFN_MH_CREATEHOOK)(LPVOID pTarget, LPVOID pDetour, LPVOID* ppOriginal);
typedef MH_STATUS(WINAPI* PFN_MH_ENABLEHOOK)(LPVOID pTarget);
typedef MH_STATUS(WINAPI* PFN_MH_DISABLEHOOK)(LPVOID pTarget);

PFN_MH_INITIALIZE g_pfnMH_Initialize = nullptr;
PFN_MH_UNINITIALIZE g_pfnMH_Uninitialize = nullptr;
PFN_MH_CREATEHOOK g_pfnMH_CreateHook = nullptr;
PFN_MH_ENABLEHOOK g_pfnMH_EnableHook = nullptr;
PFN_MH_DISABLEHOOK g_pfnMH_DisableHook = nullptr;

// ============================================================================
// MinHook 内存加载
// ============================================================================

namespace {

HMEMORYMODULE g_hMinHookModule = nullptr;

BOOL LoadMinHookFromMemory() {
    g_hMinHookModule = MemoryLoadLibrary(g_minhook_x64Data, g_minhook_x64Size);
    if (!g_hMinHookModule) {
        return FALSE;
    }
    
    // 获取函数指针
    g_pfnMH_Initialize = (PFN_MH_INITIALIZE)MemoryGetProcAddress(g_hMinHookModule, "MH_Initialize");
    g_pfnMH_Uninitialize = (PFN_MH_UNINITIALIZE)MemoryGetProcAddress(g_hMinHookModule, "MH_Uninitialize");
    g_pfnMH_CreateHook = (PFN_MH_CREATEHOOK)MemoryGetProcAddress(g_hMinHookModule, "MH_CreateHook");
    g_pfnMH_EnableHook = (PFN_MH_ENABLEHOOK)MemoryGetProcAddress(g_hMinHookModule, "MH_EnableHook");
    g_pfnMH_DisableHook = (PFN_MH_DISABLEHOOK)MemoryGetProcAddress(g_hMinHookModule, "MH_DisableHook");
    
    // 验证所有函数指针
    if (!g_pfnMH_Initialize || !g_pfnMH_Uninitialize || !g_pfnMH_CreateHook || 
        !g_pfnMH_EnableHook || !g_pfnMH_DisableHook) {
        MemoryFreeLibrary(g_hMinHookModule);
        g_hMinHookModule = nullptr;
        return FALSE;
    }
    
    return TRUE;
}

VOID UnloadMinHookFromMemory() {
    if (g_hMinHookModule) {
        MemoryFreeLibrary(g_hMinHookModule);
        g_hMinHookModule = nullptr;
        
        g_pfnMH_Initialize = nullptr;
        g_pfnMH_Uninitialize = nullptr;
        g_pfnMH_CreateHook = nullptr;
        g_pfnMH_EnableHook = nullptr;
        g_pfnMH_DisableHook = nullptr;
    }
}

}  // namespace

// ============================================================================
// 道具功能实现
// ============================================================================

/**
 * @brief 请求背包数据
 * @param packType 包类型 (0xFFFFFFFF = 全部)
 * @return 发送是否成功
 */
void SetPacketCallback(PACKET_CALLBACK callback) {
    g_PacketCallback = callback;
}

// ============================================================================
// 十六进制转换工具
// ============================================================================

// ============================================================================
// UI 同步函数
// ============================================================================

void SyncPacketsToUI() {
    // 拷贝数据，减少锁持有时间
    std::vector<PACKET> packetsCopy;
    {
        CriticalSectionLock lock(g_packetListCS.Get());
        if (!g_hWnd || g_PacketList.empty()) {
            return;
        }
        packetsCopy = g_PacketList;
    }
    
    // 检查窗口句柄有效性
    if (!IsWindow(g_hWnd)) {
        return;
    }
    
    // 清空UI列表项
    {
        std::wstring jsClear = 
            L"(function(){"
            L" var pList=document.getElementById('packet-list');"
            L" if(pList){"
            L"   var pListItems=document.getElementById('packet-list-items');"
            L"   if(pListItems){ pListItems.innerHTML=''; }"
            L" }"
            L"})();";
        
        UIBridge::Instance().ExecuteJS(jsClear);
    }
    
    // 批量同步封包
    for (size_t i = 0; i < packetsCopy.size(); ++i) {
        const auto& packet = packetsCopy[i];
        if (!packet.pData) continue;

        std::string hexStr = HexToString(packet.pData, packet.dwSize);
        std::wstring wideHexStr = Utf8ToWide(hexStr);
        std::wstring direction = packet.bSend ? L"发送包" : L"接收包";
        
        // 获取封包标签
        std::string labelUtf8 = "";
        if (packet.dwSize >= PacketProtocol::HEADER_SIZE) {
            size_t offset = 0;
            uint16_t magic = ReadUInt16LE(packet.pData, offset);  // Magic: offset 0-1
            (void)ReadUInt16LE(packet.pData, offset);             // Length: offset 2-3
            if (magic == PacketProtocol::MAGIC_NORMAL || magic == PacketProtocol::MAGIC_COMPRESSED) {
                uint32_t opcode = ReadUInt32LE(packet.pData, offset);  // Opcode: offset 4-7
                labelUtf8 = GetPacketLabel(opcode, packet.bSend);
            }
        }
        std::wstring wideLabel = Utf8ToWide(labelUtf8);
        
        // 创建时间戳
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t timeStr[64];
        swprintf_s(timeStr, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
        
        std::wstring jsCode = 
            L"if(window.addPacketToList) { window.addPacketToList(" 
            + std::to_wstring(i + 1) + L", '" + direction + L"', '" 
            + wideHexStr + L"', '" + timeStr + L"', '" + wideLabel + L"'); }";
        
        UIBridge::Instance().ExecuteJS(jsCode);
    }
    
    // 在最后统一更新一次计数
    std::wstring jsUpdateCount = 
        L"if(window.updatePacketCount) { window.updatePacketCount(" 
        + std::to_wstring(packetsCopy.size()) + L"); }";
    
    UIBridge::Instance().ExecuteJS(jsUpdateCount);
}

void AddPacketToUI(BOOL bSend, const BYTE* pData, DWORD dwSize, DWORD dwTime) {
    (void)bSend;
    (void)dwTime;
    if (!g_hWnd || !pData || dwSize == 0) return;
    if (!g_bInterceptEnabled) return;

    size_t packetIndex;
    {
        CriticalSectionLock lock(g_packetListCS.Get());
        packetIndex = g_PacketList.size();  // 封包已添加，直接使用size()，不加1
    }

    // 如果窗口可见，同步整个列表到UI
    if (AppHost::IsPacketWindowVisible()) {
        SyncPacketsToUI();
    } else {
        // 窗口关闭时，只更新计数
        std::wstring jsCountCode = 
            L"if(window.updatePacketCount) { window.updatePacketCount(" 
            + std::to_wstring(packetIndex) + L"); }";
        
        UIBridge::Instance().ExecuteJS(jsCountCode);
    }
}

// ============================================================================
// Hook 函数
// ============================================================================

namespace {

thread_local bool g_sendPacketRequiresComplete = false;

int SendAllOriginal(SOCKET socket, const char* data, int length, int flags) {
    if (!OriginalSend || !data || length < 0) {
        WSASetLastError(WSAEINVAL);
        return SOCKET_ERROR;
    }

    int sent = 0;
    while (sent < length) {
        const int result = OriginalSend(socket, data + sent, length - sent, flags);
        if (result == SOCKET_ERROR) {
            return sent > 0 ? sent : SOCKET_ERROR;
        }
        if (result == 0) {
            return sent;
        }
        sent += result;
    }
    return sent;
}

struct SendPacketCompleteGuard {
    bool previous = g_sendPacketRequiresComplete;
    SendPacketCompleteGuard() { g_sendPacketRequiresComplete = true; }
    ~SendPacketCompleteGuard() { g_sendPacketRequiresComplete = previous; }
};

}  // namespace

int WINAPI HookedSend(SOCKET s, const char* buf, int len, int flags) {
    // 总是记录游戏套接字
    if (len > 0) {
        const BYTE* pData = reinterpret_cast<const BYTE*>(buf);
        if (IsGamePacket(pData, static_cast<DWORD>(len))) {
            g_LastGameSocket = s;
        }
    }
    
    // 劫持检测和修改
    if (g_bHijackEnabled && len > 0) {
        const BYTE* pData = reinterpret_cast<const BYTE*>(buf);
        DWORD dwSize = static_cast<DWORD>(len);
        std::vector<BYTE> modifiedData;
        
        if (ProcessHijack(true, pData, &dwSize, &modifiedData)) {
            // 劫持成功：拦截或替换封包
            if (dwSize == 0) {
                // 拦截：返回已发送的假象
                return len;
            } else if (!modifiedData.empty()) {
                // 替换：发送修改后的封包
                const char* sendData = reinterpret_cast<const char*>(modifiedData.data());
                const int sendLength = static_cast<int>(dwSize);
                int result = g_sendPacketRequiresComplete
                    ? SendAllOriginal(s, sendData, sendLength, flags)
                    : OriginalSend(s, sendData, sendLength, flags);
                // 记录修改后的封包
                if (g_bInterceptEnabled && g_bInterceptSend && g_bInitialized) {
                    PACKET packet;
                    packet.dwSize = dwSize;
                    packet.bSend = TRUE;
                    packet.pData = new BYTE[dwSize];
                    memcpy(packet.pData, modifiedData.data(), dwSize);
                    packet.dwTime = GetTickCount();

                    {
                        CriticalSectionLock lock(g_packetListCS.Get());
                        g_PacketList.push_back(packet);
                    }
                    // 锁外调用 UI 更新，避免死锁
                    AddPacketToUI(TRUE, packet.pData, packet.dwSize, packet.dwTime);

                    if (g_PacketCallback) {
                        g_PacketCallback(TRUE, modifiedData.data(), dwSize);
                    }
                }
                return result;
            }
        }
    }
    
    // ========================================================================
    // 战斗封包处理
    // Counter 在收到 BATTLE_START 响应后计算并存储在全局变量中
    // ========================================================================
    
    // 检查是否需要阻止发送
    if (len > 0 && ShouldBlockPacketSend(reinterpret_cast<const BYTE*>(buf), static_cast<DWORD>(len))) {
        return len;  // 欺骗调用者认为已发送
    }

    // 进入场景不会重置战斗 counter；AS3 BattleControl.counter 会跨场景保留。
    if (len >= static_cast<int>(PacketProtocol::HEADER_SIZE)) {
        const BYTE* pData = reinterpret_cast<const BYTE*>(buf);
        if (IsGamePacket(pData, static_cast<DWORD>(len))) {
            uint32_t opcode = ReadOpcode(pData);
            // 检测账号验证封包，提取登录 key
            if (opcode == Opcode::CHECK_ACCOUNT_SEND) {
                ExtractLoginKeyFromPacket(pData, static_cast<DWORD>(len));
            }
            if (opcode == Opcode::BATTLE_PLAY_OVER && g_battleSixAuto.IsInBattle()) {
                g_battleSixPlayOverToken.fetch_add(1);
            }
        }
    }
    // 拦截并记录封包
    if (g_bInterceptEnabled && g_bInterceptSend && g_bInitialized && len > 0) {
        const BYTE* pData = reinterpret_cast<const BYTE*>(buf);
        DWORD dwSize = static_cast<DWORD>(len);

        if (IsGamePacket(pData, dwSize)) {
            PACKET packet;
            packet.dwSize = dwSize;
            packet.bSend = TRUE;
            packet.pData = new BYTE[dwSize];
            memcpy(packet.pData, pData, dwSize);
            packet.dwTime = GetTickCount();

            {
                CriticalSectionLock lock(g_packetListCS.Get());
                g_PacketList.push_back(packet);
            }
            // 锁外调用 UI 更新，避免死锁
            AddPacketToUI(TRUE, packet.pData, packet.dwSize, packet.dwTime);

            if (g_PacketCallback) {
                g_PacketCallback(TRUE, pData, dwSize);
            }
        }
    }
    
    return g_sendPacketRequiresComplete
        ? SendAllOriginal(s, buf, len, flags)
        : OriginalSend(s, buf, len, flags);
}

// ============================================================================
// 接收封包处理器（拆分自 HookedRecv）
// ============================================================================



namespace {

/**
 * @brief 处理进入世界封包
 */
void ProcessEnterWorldPacket(const GamePacket& gp) {
    const auto& body = gp.body;
    if (body.size() < 4) return;
    
    size_t offset = 0;
    
    // 跳过 timeleft, isNewHand, coin, x_coin (4 * 4 = 16字节)
    offset += 16;
    
    // 跳过 UTF 字符串
    if (offset + 2 <= body.size()) {
        uint16_t strLen = static_cast<uint16_t>(body[offset]) | 
                          (static_cast<uint16_t>(body[offset + 1]) << 8);
        offset += 2 + strLen;
    }
    
    // 跳过 5 个 int (20字节)
    offset += 20;
    
    // 读取卡布号
    if (offset + 4 > body.size()) return;
    
    uint32_t kabuId = static_cast<uint32_t>(body[offset]) |
                      (static_cast<uint32_t>(body[offset + 1]) << 8) |
                      (static_cast<uint32_t>(body[offset + 2]) << 16) |
                      (static_cast<uint32_t>(body[offset + 3]) << 24);
    offset += 4;
    
    g_userId = kabuId;
    ResetEightTrigramsSessionState();
    
    g_battleCounter = 1;
    // 读取卡布名
    if (offset + 2 > body.size()) return;
    
    uint16_t nameLen = static_cast<uint16_t>(body[offset]) | 
                       (static_cast<uint16_t>(body[offset + 1]) << 8);
    offset += 2;
    
    if (offset + nameLen > body.size()) return;
    
    std::string nameUtf8(reinterpret_cast<const char*>(&body[offset]), nameLen);
    std::wstring kabuName = Utf8ToWide(nameUtf8);
    
    // 更新窗口标题
    std::wstring newTitle = std::wstring(WpeHook::APP_WINDOW_TITLE_W) + L" - " + 
                           std::to_wstring(kabuId) + L" " + kabuName;
    SetWindowTextW(g_hWnd, newTitle.c_str());
}

/**
 * @brief 处理分解响应后的自动查询
 */
void HandleDecomposeResponse() {
    static DWORD g_lastDecomposeTime = 0;
    DWORD currentTime = GetTickCount();
    
    if (currentTime - g_lastDecomposeTime >= 300) {
        g_lastDecomposeTime = currentTime;
        HANDLE hThread = CreateThread(NULL, 0, [](LPVOID) -> DWORD {
            Sleep(300);
            SendQueryBagPacket();
            return 0;
        }, NULL, 0, NULL);
        if (hThread) {
            CloseHandle(hThread); // 关闭线程句柄，避免资源泄漏
        }
    }
}

/**
 * @brief 处理接收到的游戏封包
 */
void ProcessReceivedGamePackets(const BYTE* pData, DWORD dwSize,
                                const std::vector<GamePacket>& gamePackets) {
    (void)pData;
    (void)dwSize;
    auto& dispatcher = ResponseDispatcher::Instance();

    for (const auto& gp : gamePackets) {
        dispatcher.Dispatch(gp);
        Sleep(0);
    }
}

static void ReceiveDispatchWorkerProc() {
    for (;;) {
        std::vector<GamePacket> packets;
        {
            std::unique_lock<std::mutex> lock(g_receiveDispatchMutex);
            g_receiveDispatchCv.wait(lock, [] {
                return g_receiveDispatchStop || !g_receiveDispatchQueue.empty();
            });

            if (g_receiveDispatchStop && g_receiveDispatchQueue.empty()) {
                return;
            }

            packets = std::move(g_receiveDispatchQueue.front());
            g_receiveDispatchQueue.pop_front();
        }

        ProcessReceivedGamePackets(nullptr, 0, packets);
    }
}

static void StartReceiveDispatchWorker() {
    std::lock_guard<std::mutex> lock(g_receiveDispatchMutex);
    if (!g_receiveDispatchAccepting.load() || g_receiveDispatchStarted) {
        return;
    }

    g_receiveDispatchStop = false;
    try {
        g_receiveDispatchWorker = std::thread(ReceiveDispatchWorkerProc);
        g_receiveDispatchStarted = true;
    } catch (...) {
        g_receiveDispatchStarted = false;
    }
}

static void StopReceiveDispatchWorker() {
    {
        std::lock_guard<std::mutex> lock(g_receiveDispatchMutex);
        if (!g_receiveDispatchStarted) {
            return;
        }
        g_receiveDispatchStop = true;
    }
    g_receiveDispatchCv.notify_all();

    if (g_receiveDispatchWorker.joinable()) {
        g_receiveDispatchWorker.join();
    }

    std::lock_guard<std::mutex> lock(g_receiveDispatchMutex);
    g_receiveDispatchQueue.clear();
    g_receiveDispatchStarted = false;
    g_receiveDispatchStop = false;
}

void DispatchReceivedGamePacketsAsync(const std::vector<GamePacket>& gamePackets) {
    if (gamePackets.empty() || !g_receiveDispatchAccepting.load()) {
        return;
    }

    // Notify while still in the recv hook.  The business worker may be busy
    // in another handler, but protocol waiters must not wait behind it.
    for (const auto& gp : gamePackets) {
        ResponseWaiter::NotifyResponse(gp.opcode, gp.params);
    }

    StartReceiveDispatchWorker();
    {
        std::lock_guard<std::mutex> lock(g_receiveDispatchMutex);
        if (!g_receiveDispatchAccepting.load() || !g_receiveDispatchStarted) {
            return;
        }

        // Bound memory even if a handler is slower than the network.  The
        // newest packets are more useful for current state than stale ones.
        if (g_receiveDispatchQueue.size() >= kMaxReceiveDispatchQueue) {
            g_receiveDispatchQueue.pop_front();
        }
        g_receiveDispatchQueue.emplace_back(gamePackets);
    }
    g_receiveDispatchCv.notify_one();
}

std::vector<BYTE> BuildReceivedPacketBytes(const GamePacket& packet) {
    const std::vector<uint8_t>& bodyBytes = packet.rawBody.empty() ? packet.body : packet.rawBody;
    std::vector<BYTE> packetData(PacketProtocol::HEADER_SIZE + bodyBytes.size());

    size_t offset = 0;
    packetData[offset++] = static_cast<BYTE>(packet.magic & 0xFF);
    packetData[offset++] = static_cast<BYTE>((packet.magic >> 8) & 0xFF);

    const uint16_t bodyLen = static_cast<uint16_t>(bodyBytes.size());
    packetData[offset++] = static_cast<BYTE>(bodyLen & 0xFF);
    packetData[offset++] = static_cast<BYTE>((bodyLen >> 8) & 0xFF);

    packetData[offset++] = static_cast<BYTE>(packet.opcode & 0xFF);
    packetData[offset++] = static_cast<BYTE>((packet.opcode >> 8) & 0xFF);
    packetData[offset++] = static_cast<BYTE>((packet.opcode >> 16) & 0xFF);
    packetData[offset++] = static_cast<BYTE>((packet.opcode >> 24) & 0xFF);

    packetData[offset++] = static_cast<BYTE>(packet.params & 0xFF);
    packetData[offset++] = static_cast<BYTE>((packet.params >> 8) & 0xFF);
    packetData[offset++] = static_cast<BYTE>((packet.params >> 16) & 0xFF);
    packetData[offset++] = static_cast<BYTE>((packet.params >> 24) & 0xFF);

    if (!bodyBytes.empty()) {
        memcpy(packetData.data() + offset, bodyBytes.data(), bodyBytes.size());
    }

    return packetData;
}

}  // anonymous namespace

// ============================================================================
// 登录 Key 提取函数实现
// ============================================================================

// ============================================================================
// Hook 函数实现
// ============================================================================

int WINAPI HookedRecv(SOCKET s, char* buf, int len, int flags) {
    int result = OriginalRecv(s, buf, len, flags);

    if (result <= 0 || !g_bInitialized) {
        return result;
    }

    const BYTE* pData = reinterpret_cast<const BYTE*>(buf);
    DWORD dwSize = static_cast<DWORD>(result);

    // ========================================================================
    // 第一步：解析黏包，得到完整的封包列表
    // ========================================================================
    std::vector<GamePacket> gamePackets;
    bool canRewriteRecvData = false;
    bool hasValidPackets = PacketParser::ParsePackets(
        pData, dwSize, FALSE, gamePackets, &canRewriteRecvData);
    
    // ========================================================================
    // 第二步：封包级别过滤（支持黏包）
    // ========================================================================
    // 记录需要过滤的封包索引
    std::vector<size_t> packetsToFilter;
    bool hasBattleStart = false;
    bool shouldFilterBattleStart = false;
    
    for (size_t i = 0; i < gamePackets.size(); i++) {
        const auto& gp = gamePackets[i];
        const auto& packetBody = gp.body;
        
        // 0. 默认屏蔽检查（无条件屏蔽，无需用户勾选）
        if (IsDefaultBlockedPacket(gp.opcode, gp.params,
                                   packetBody.empty() ? nullptr : packetBody.data(),
                                   packetBody.size())) {
            packetsToFilter.push_back(i);
            continue;  // 跳过后续处理
        }
        
        // 1. 战斗开始响应 - 始终计算并存储 counter
        if (gp.opcode == Opcode::BATTLE_START) {
            hasBattleStart = true;
            
            // 读取战斗类型
            // AS3 stores battleType in the packet params; body[0] is the first entity state.
            int32_t battleType = static_cast<int32_t>(gp.params);

            // 计算 counter
            uint32_t currentCounter = g_battleCounter.load();
            if (currentCounter == 0) {
                currentCounter = 1;
            }

            bool isSpecialBattle = (battleType == 3 || battleType == 5 || battleType == 6 ||
                                   battleType == 14 || battleType == 15 ||
                                   battleType == 17 || battleType == 18 ||
                                   battleType == 19 || battleType == 20 ||
                                   battleType == 21 || battleType == 22);

            if (isSpecialBattle) {
                g_battleCounter = 1;
            } else {
                uint32_t newCounter = currentCounter + (g_userId.load() & 65535) + 9;
                g_battleCounter = newCounter & 65535;
            }


            // 屏蔽战斗功能
            if (g_blockBattle.load()) {
                shouldFilterBattleStart = true;
                packetsToFilter.push_back(i);
            }
        }

        // Settlement packets are parsed by PacketParser.  The common
        // BATTLE_END lifecycle is the only place that sends the end-confirmation,
        // matching BattleControl.clearBattleView() in AS3.
        
        // 2. MD5 图片验证自动回复（不屏蔽，只自动回复）
        if (gp.opcode == Opcode::BATTLE_MD5_CHECK) {
            // 复制body和params用于异步处理
            struct MD5ThreadData {
                std::vector<BYTE> body;
                uint32_t params;
            };
            MD5ThreadData* threadData = new MD5ThreadData{ gp.body, gp.params };
            
            // 异步处理验证（需要下载图片，耗时操作）
            CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
                MD5ThreadData* data = (MD5ThreadData*)lpParam;
                Sleep(100);  // 延迟100ms
                ProcessMD5CheckAndReply(data->body, data->params);
                delete data;
                return 0;
            }, threadData, 0, nullptr);
        }

        // 3. 自动回血功能
        if (g_autoHeal && gp.opcode == Opcode::BATTLE_END && gp.params == 1) {
            CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
                (void)lpParam;
                Sleep(100);
                SendBeibeiHealPacket();
                return 0;
            }, nullptr, 0, nullptr);
        }

        // 4. 劫持检测和修改（接收包）
        if (g_bHijackEnabled) {
            std::vector<BYTE> packetData = BuildReceivedPacketBytes(gp);
            
            std::vector<BYTE> modifiedData;
            DWORD modifiedSize = static_cast<DWORD>(packetData.size());

            if (ProcessHijack(false, packetData.data(), &modifiedSize, &modifiedData)) {
                // TODO: 实现封包替换功能
            }
        }
    }

    // ========================================================================
    // 第三步：重构封包缓冲区（过滤掉需要屏蔽的封包）
    // ========================================================================
    if (!packetsToFilter.empty() && hasValidPackets && canRewriteRecvData) {
        // 创建新的封包缓冲区
        std::vector<BYTE> newBuffer;
        newBuffer.reserve(dwSize);
        
        for (size_t i = 0; i < gamePackets.size(); i++) {
            // 检查是否需要过滤此封包
            bool shouldFilter = false;
            for (size_t filterIdx : packetsToFilter) {
                if (filterIdx == i) {
                    shouldFilter = true;
                    break;
                }
            }
            
            if (!shouldFilter) {
                const auto packetData = BuildReceivedPacketBytes(gamePackets[i]);
                newBuffer.insert(newBuffer.end(), packetData.begin(), packetData.end());
            }
        }
        
        // Never return 0 for a successful recv: that is EOF to the game. If every
        // complete packet was selected for filtering, preserve the original bytes
        // and keep the connection semantics intact.
        if (!newBuffer.empty()) {
            // 复制新缓冲区到原始缓冲区
            size_t newSize = newBuffer.size();
            if (newSize <= result) {
                memset(buf, 0, result);
                memcpy(buf, newBuffer.data(), newSize);
                // 更新返回值为新大小
                result = static_cast<int>(newSize);
            }
        }
    }

    // ========================================================================
    // 第四步：处理解析后的封包（通知等待器、战斗处理等）
    // ========================================================================
    
    // 额外的 ENTER_SCENE_BACK 检测（即使 ParsePackets 失败也检测）
    if (dwSize >= PacketProtocol::HEADER_SIZE) {
        uint32_t opcode = pData[4] | (pData[5] << 8) | (pData[6] << 16) | (pData[7] << 24);
        uint16_t magic = pData[0] | (pData[1] << 8);
        
        // 直接检测 ENTER_SCENE_BACK 响应
        if (opcode == Opcode::ENTER_SCENE_BACK && 
            (magic == PacketProtocol::MAGIC_NORMAL || magic == PacketProtocol::MAGIC_COMPRESSED)) {
            CriticalSectionLock lock(g_towerCS);
            g_towerMapEntered = true;
            g_taskZoneMapEntered = true;
        }
    }
    

    if (hasValidPackets) {
        DispatchReceivedGamePacketsAsync(gamePackets);
    }

    // ========================================================================
    // 第五步：显示封包（仅当启用接收拦截时）
    // ========================================================================
    if (g_bInterceptEnabled && g_bInterceptRecv && !gamePackets.empty()) {
        for (const auto& gp : gamePackets) {
            std::vector<BYTE> packetData = BuildReceivedPacketBytes(gp);
            
            // 添加到封包列表
            PACKET packet;
            packet.dwSize = static_cast<DWORD>(packetData.size());
            packet.bSend = FALSE;
            packet.pData = new BYTE[packetData.size()];
            memcpy(packet.pData, packetData.data(), packetData.size());
            packet.dwTime = GetTickCount();

            {
                CriticalSectionLock lock(g_packetListCS.Get());
                g_PacketList.push_back(packet);
            }
            // 锁外调用 UI 更新，避免死锁
            AddPacketToUI(FALSE, packet.pData, packet.dwSize, packet.dwTime);

            // 调用回调函数
            if (g_PacketCallback) {
                g_PacketCallback(FALSE, packetData.data(), static_cast<DWORD>(packetData.size()));
            }
        }
    }

    return result;
}

// ============================================================================
// Hook 初始化
// ============================================================================

BOOL InitializeHooks() {
    // 初始化 ResponseWaiter
    ResponseWaiter::Initialize();

    if (!LoadMinHookFromMemory()) {
        return FALSE;
    }
    
    MH_STATUS status = g_pfnMH_Initialize();
    if (status != MH_OK) {
        UnloadMinHookFromMemory();
        return FALSE;
    }
    
    // Hook ws2_32.dll (socket)
    HMODULE hWs2Module = LoadLibraryA("ws2_32.dll");
    if (!hWs2Module) {
        g_pfnMH_Uninitialize();
        UnloadMinHookFromMemory();
        return FALSE;
    }
    
    LPVOID pSendAddr = (LPVOID)GetProcAddress(hWs2Module, "send");
    LPVOID pRecvAddr = (LPVOID)GetProcAddress(hWs2Module, "recv");
    
    if (!pSendAddr || !pRecvAddr) {
        g_pfnMH_Uninitialize();
        UnloadMinHookFromMemory();
        return FALSE;
    }
    
    if (g_pfnMH_CreateHook(pSendAddr, HookedSend, (LPVOID*)&OriginalSend) != MH_OK ||
        g_pfnMH_CreateHook(pRecvAddr, HookedRecv, (LPVOID*)&OriginalRecv) != MH_OK) {
        g_pfnMH_Uninitialize();
        UnloadMinHookFromMemory();
        return FALSE;
    }

    g_receiveDispatchAccepting.store(true);
    status = g_pfnMH_EnableHook(MH_ALL_HOOKS);
    if (status != MH_OK) {
        g_receiveDispatchAccepting.store(false);
        g_pfnMH_Uninitialize();
        UnloadMinHookFromMemory();
        return FALSE;
    }
    
    return TRUE;
}

VOID UninitializeHooks() {
    g_receiveDispatchAccepting.store(false);

    if (g_hMinHookModule) {
        g_pfnMH_DisableHook(MH_ALL_HOOKS);
    }

    ResponseWaiter::CancelWait();
    StopReceiveDispatchWorker();

    if (g_hMinHookModule) {
        g_pfnMH_Uninitialize();
        UnloadMinHookFromMemory();
    }

    // 清理 ResponseWaiter
    ResponseWaiter::Cleanup();
}

// ============================================================================
// 拦截控制
// ============================================================================

VOID StartIntercept() {
    g_bInterceptEnabled = true;
}

VOID StopIntercept() {
    g_bInterceptEnabled = false;
}

VOID SetInterceptType(BOOL bSend, BOOL bRecv) {
    g_bInterceptSend = bSend;
    g_bInterceptRecv = bRecv;
}

// ============================================================================
// 响应等待器实现
// ============================================================================

namespace {

struct ResponseRecord {
    uint64_t serial;
    uint32_t opcode;
    uint32_t params;
};

std::deque<ResponseRecord> g_responseQueue;

}  // namespace

CRITICAL_SECTION ResponseWaiter::s_cs;
CONDITION_VARIABLE ResponseWaiter::s_cv;
uint64_t ResponseWaiter::s_responseSerial = 0;
std::atomic<long> ResponseWaiter::s_waitingCount{0};  // 等待线程计数（真正的原子操作）
std::atomic<uint64_t> ResponseWaiter::s_cancelSerial{0};

void ResponseWaiter::Initialize() {
    InitializeCriticalSection(&s_cs);
    InitializeConditionVariable(&s_cv);
    s_waitingCount = 0;
    s_responseSerial = 0;
    s_cancelSerial = 0;
    g_responseQueue.clear();
}

void ResponseWaiter::Cleanup() {
    CancelWait();

    const DWORD startTick = GetTickCount();
    while (s_waitingCount.load() > 0) {
        if (GetTickCount() - startTick > (WpeHook::TIMEOUT_SEND + WpeHook::TIMEOUT_RESPONSE + 1000)) {
            return;
        }
        Sleep(10);
    }

    EnterCriticalSection(&s_cs);
    g_responseQueue.clear();
    LeaveCriticalSection(&s_cs);
    DeleteCriticalSection(&s_cs);
}

uint64_t ResponseWaiter::BeginWait() {
    EnterCriticalSection(&s_cs);

    const long previousWaiting = s_waitingCount.fetch_add(1);
    if (previousWaiting == 0) {
        g_responseQueue.clear();
    }

    const uint64_t serial = s_responseSerial;
    LeaveCriticalSection(&s_cs);
    return serial;
}

void ResponseWaiter::EndWait() {
    EnterCriticalSection(&s_cs);

    const long remainingWaiting = s_waitingCount.fetch_sub(1) - 1;
    if (remainingWaiting <= 0) {
        g_responseQueue.clear();
    }

    LeaveCriticalSection(&s_cs);
}

bool ResponseWaiter::WaitForResponse(
    uint32_t expectedOpcode,
    DWORD timeoutMs,
    uint64_t baselineSerial,
    uint32_t expectedParams,
    bool matchExpectedParams,
    bool preRegistered) {
    if (expectedOpcode == 0 || timeoutMs == 0) {
        return true;  // 不等待，直接返回
    }

    bool ownsRegistration = false;
    if (!preRegistered) {
        BeginWait();
        ownsRegistration = true;
    }

    EnterCriticalSection(&s_cs);
    const uint64_t cancelSerial = s_cancelSerial.load();

    const DWORD startTick = GetTickCount();
    bool received = false;

    while (true) {
        if (cancelSerial != s_cancelSerial.load()) {
            break;
        }

        auto matched = std::find_if(
            g_responseQueue.begin(),
            g_responseQueue.end(),
            [&](const ResponseRecord& record) {
                if (record.serial <= baselineSerial) {
                    return false;
                }

                if (record.opcode != expectedOpcode) {
                    return false;
                }

                if (matchExpectedParams && record.params != expectedParams) {
                    return false;
                }

                return true;
            });

        if (matched != g_responseQueue.end()) {
            received = true;
            g_responseQueue.erase(matched);
            break;
        }

        const DWORD elapsed = GetTickCount() - startTick;
        if (elapsed >= timeoutMs) {
            break;
        }

        SleepConditionVariableCS(
            &s_cv,
            &s_cs,
            timeoutMs - elapsed
        );
    }

    LeaveCriticalSection(&s_cs);

    if (ownsRegistration) {
        EndWait();
    }

    return received;
}

void ResponseWaiter::NotifyResponse(uint32_t opcode, uint32_t params) {
    EnterCriticalSection(&s_cs);
    ++s_responseSerial;
    if (s_waitingCount.load() > 0) {
        g_responseQueue.push_back(ResponseRecord{s_responseSerial, opcode, params});
    }
    LeaveCriticalSection(&s_cs);
    WakeAllConditionVariable(&s_cv);
}

void ResponseWaiter::CancelWait() {
    EnterCriticalSection(&s_cs);
    s_cancelSerial.fetch_add(1);
    LeaveCriticalSection(&s_cs);
    WakeAllConditionVariable(&s_cv);
}



// ============================================================================

// ResponseDispatcher 实现

// ============================================================================



ResponseDispatcher& ResponseDispatcher::Instance() {
    static ResponseDispatcher instance;
    return instance;
}

uint64_t ResponseDispatcher::MakeKey(uint32_t opcode, uint32_t params) {
    return (static_cast<uint64_t>(opcode) << 32) | static_cast<uint64_t>(params);
}

BOOL ResponseDispatcher::Register(uint32_t opcode, ResponseHandler handler) {
    if (!handler) return FALSE;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_opcodeOnlyHandlers[opcode] = handler;
    return TRUE;
}

BOOL ResponseDispatcher::Register(uint32_t opcode, uint32_t params, ResponseHandler handler) {
    if (!handler) return FALSE;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_handlers[MakeKey(opcode, params)] = handler;
    return TRUE;
}

void ResponseDispatcher::Unregister(uint32_t opcode, uint32_t params) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (params != 0xFFFFFFFF) {
        m_handlers.erase(MakeKey(opcode, params));
        return;
    }

    m_opcodeOnlyHandlers.erase(opcode);
    for (auto it = m_handlers.begin(); it != m_handlers.end();) {
        if (static_cast<uint32_t>(it->first >> 32) == opcode) {
            it = m_handlers.erase(it);
        } else {
            ++it;
        }
    }
}

BOOL ResponseDispatcher::Dispatch(const GamePacket& packet) {
    // 锁内只取handler指针，锁外再执行：handler可能耗时甚至回调注册接口。
    ResponseHandler handler = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto exact = m_handlers.find(MakeKey(packet.opcode, packet.params));
        if (exact != m_handlers.end()) {
            handler = exact->second;
        } else {
            auto byOpcode = m_opcodeOnlyHandlers.find(packet.opcode);
            if (byOpcode != m_opcodeOnlyHandlers.end()) {
                handler = byOpcode->second;
            }
        }
    }

    if (!handler) {
        return FALSE;
    }

    handler(packet);
    return TRUE;
}

void ResponseDispatcher::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_handlers.clear();
    m_opcodeOnlyHandlers.clear();
}

void ResponseDispatcher::InitializeDefaultHandlers() {
    Clear();

    const ResponseHandler processBattle = [](const GamePacket& gp) {
        // 共享战斗包统一由通用解析维护公共战斗态/UI。
        // BattleSix 仅在这里补自己的私有自动化状态与结算收尾。
        PacketParser::ProcessBattlePacket(gp);

        // 剧情战斗的回合驱动（万妖盛会/双台谷各自处理，这里只接普通战斗）。
        HandleStoryBattlePacket(gp);

        if (gp.opcode == Opcode::BATTLE_START) {
            if (g_battleSixAuto.IsAutoMatching() &&
                g_battleSixFlowStage.load() != BATTLESIX_FLOW_IDLE) {
                ResetBattleSixFlowState();
            }

            if (g_battleSixAuto.IsInBattle() &&
                g_battleSixAuto.IsAutoMatching() &&
                !g_battleSixReadySupplementSent.exchange(true)) {
                const unsigned long long battleSession = g_battleSixBattleSession.load();
                struct ReadyThreadData {
                    unsigned long long battleSession;
                };
                ReadyThreadData* threadData = new ReadyThreadData{battleSession};
                HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
                    std::unique_ptr<ReadyThreadData> data(static_cast<ReadyThreadData*>(lpParam));
                    Sleep(800);
                    if (g_battleSixBattleSession.load() == data->battleSession && g_battleSixAuto.IsInBattle()) {
                        SendBattleReadyPacket();
                    }
                    return 0;
                }, threadData, 0, nullptr);
                if (hThread) {
                    CloseHandle(hThread);
                } else {
                    delete threadData;
                    g_battleSixReadySupplementSent = false;
                }
            }

            if (g_shuangtaiAuto.IsRunning()) {
                ProcessShuangTaiBattleStartResponse(gp);
            }
            return;
        }

        if (gp.opcode == Opcode::BATTLE_ROUND_START ||
            gp.opcode == Opcode::BATTLE_CHANGE_SPIRIT_ROUND) {
            if (g_shuangtaiAuto.IsRunning()) {
                ProcessShuangTaiBattleRoundStartResponse(gp);
            }
            return;
        }

        if (gp.opcode == Opcode::BATTLE_ROUND) {
            if (g_battleSixAuto.IsInBattle()) {
                ProcessBattleSixBattleRoundResultResponse(gp);
            }

            if (g_shuangtaiAuto.IsRunning()) {
                ProcessShuangTaiBattleRoundResultResponse(gp);
            }
            return;
        }

        if (gp.opcode == Opcode::BATTLE_END) {
            if (g_battleSixAuto.IsInBattle()) {
                ProcessBattleSixBattleEndResponse(gp);
            }

            if (g_shuangtaiAuto.IsRunning()) {
                ProcessShuangTaiBattleEndResponse(gp);
            }
        }

        if (gp.opcode == Opcode::BATTLE_BUF ||
            gp.opcode == Opcode::BATTLE_BUF_DIS ||
            gp.opcode == Opcode::BATTLE_BUFS ||
            gp.opcode == Opcode::BATTLE_FSPK ||
            gp.opcode == Opcode::COMBAT_SITE_OPTION ||
            gp.opcode == Opcode::COMBAT_SITE_EFFECT ||
            gp.opcode == Opcode::COMBAT_NET_PROBE_REQ ||
            gp.opcode == Opcode::COMBAT_NET_REPORT) {
            return;
        }
    };
    const ResponseHandler processLingyu = [](const GamePacket& gp) {
        PacketParser::ProcessLingyuPacket(gp);
    };
    const ResponseHandler processMonster = [](const GamePacket& gp) {
        PacketParser::ProcessMonsterPacket(gp);
    };
    const ResponseHandler enterScene = [](const GamePacket&) {
        SetDanceMapEntered(true);
        {
            CriticalSectionLock lock(g_towerCS);
            g_towerMapEntered = true;
        }
        g_heavenFuruiEnteredMap = true;
        g_taskZoneMapEntered = true;
        g_storyTaskMapEntered = true;
        g_storyTaskMapEnterSession = g_storyTaskSession.load();
    };

    const auto registerOpcode = [this](uint32_t opcode, const ResponseHandler& handler) {
        Register(opcode, handler);
    };
    const auto registerParams = [this](uint32_t opcode, uint32_t params, const ResponseHandler& handler) {
        Register(opcode, params, handler);
    };

    registerOpcode(Opcode::BATTLE_START, processBattle);
    registerOpcode(Opcode::BATTLE_ROUND_START, processBattle);
    registerOpcode(Opcode::BATTLE_ROUND, processBattle);
    registerOpcode(Opcode::BATTLE_END, processBattle);
    registerOpcode(Opcode::BATTLE_CHANGE_SPIRIT_ROUND, processBattle);
    registerOpcode(Opcode::BATTLE_BUF, processBattle);
    registerOpcode(Opcode::BATTLE_BUF_DIS, processBattle);
    registerOpcode(Opcode::BATTLE_BUFS, processBattle);
    registerOpcode(Opcode::BATTLE_FSPK, processBattle);
    registerOpcode(Opcode::BATTLE_NEWEXP, processBattle);
    registerOpcode(Opcode::SPIRIT_LEVEL_UP, processBattle);
    registerOpcode(Opcode::BATTLE_WITH, processBattle);
    registerOpcode(Opcode::BATTLE_REQ_ON_SKILL, processBattle);
    registerOpcode(Opcode::COMBAT_SITE_OPTION, processBattle);
    registerOpcode(Opcode::COMBAT_SITE_EFFECT, processBattle);
    registerOpcode(Opcode::COMBAT_NET_PROBE_REQ, processBattle);
    registerOpcode(Opcode::COMBAT_NET_REPORT, processBattle);

    registerOpcode(Opcode::LINGYU_LIST, processLingyu);
    registerOpcode(Opcode::DECOMPOSE_RESPONSE, [](const GamePacket&) { HandleDecomposeResponse(); });
    registerOpcode(Opcode::MONSTER_LIST, processMonster);
    registerOpcode(Opcode::ENTER_WORLD, ProcessEnterWorldPacket);
    registerOpcode(Opcode::ENTER_SCENE_BACK, enterScene);
    registerOpcode(Opcode::USER_TASK_LIST_BACK, ProcessTaskZoneUserTaskListResponse);
    registerOpcode(Opcode::TASK_TALK_BACK, ProcessStoryTaskTalkResponse);
    registerOpcode(Opcode::NPCTASKLIST_BACK, ProcessStoryNpcTaskListResponse);
    registerOpcode(Opcode::NPC_LIST_BACK, ProcessStoryNpcListResponse);
    registerOpcode(Opcode::GET_MAGIC_INDEX_BACK, ProcessStoryMagicIndexResponse);
    registerOpcode(Opcode::TASK_FINISHED_BACK, ProcessStoryTaskFinishedResponse);

    registerOpcode(Opcode::DEEP_DIG_BACK, ProcessDeepDigResponse);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, 668, ProcessDeepDigQueryResponse);

    registerOpcode(Opcode::DANCE_ACTIVITY_BACK, ProcessDanceActivityResponse);
    registerOpcode(Opcode::DANCE_STAGE_BACK, ProcessDanceStageResponse);

    registerParams(Opcode::TRIAL_BACK, 142, ProcessTrialResponse);
    registerParams(Opcode::TRIAL_BACK, 341, ProcessTowerActivityResponse);

    registerOpcode(Opcode::COLLECT_STATUS_BACK, ProcessCollectResponse);
    registerOpcode(Opcode::REQ_PACKAGE_DATA_BACK, ProcessPackageDataResponse);

    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act778::ACTIVITY_ID, ProcessAct778Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act685::ACTIVITY_ID, ProcessAct685Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act666::ACTIVITY_ID, ProcessAct666Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act757::ACTIVITY_ID, ProcessAct757Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act641::ACTIVITY_ID, ProcessAct641Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act684::ACTIVITY_ID, ProcessAct684Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act717::ACTIVITY_ID, ProcessAct717Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act808::ACTIVITY_ID, ProcessAct808Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act805::ACTIVITY_ID, ProcessAct805Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act631::ACTIVITY_ID, ProcessAct631Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act782::ACTIVITY_ID, ProcessAct782Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act804::ACTIVITY_ID, ProcessAct804Response);
    registerParams(Opcode::ACTIVITY_LUA_V3_BACK, Act804::ACTIVITY_ID, ProcessAct804Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act810::ACTIVITY_ID, ProcessAct810Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act811::ACTIVITY_ID, ProcessAct811Response);
    registerParams(Opcode::ACTIVITY_LUA_V3_BACK, Act811::ACTIVITY_ID, ProcessAct811Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act822::ACTIVITY_ID, ProcessAct822Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act826::ACTIVITY_ID, ProcessAct826Response);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, Act827::ACTIVITY_ID, ProcessAct827Response);
    registerParams(Opcode::HORSE_COMPETITION_BACK, HORSE_COMPETITION_ACT_ID, ProcessHorseCompetitionResponse);

    // 最新活动（周活动小游戏）：按活动 ID 路由到统一处理器
    registerParams(Opcode::ACTIVITY_QUERY_BACK, LatestAct::ACT_MAZE, ProcessLatestActivityResponse);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, LatestAct::ACT_TOUCH_STAR, ProcessLatestActivityResponse);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, LatestAct::ACT_EXPLORE, ProcessLatestActivityResponse);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, LatestAct::ACT_MOON_CAKE, ProcessLatestActivityResponse);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, LatestAct::ACT_FLOWER, ProcessLatestActivityResponse);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, LatestAct::ACT_FOOD, ProcessLatestActivityResponse);

    registerParams(Opcode::HEAVEN_FURUI_BACK, HeavenFurui::ACTIVITY_ID, ProcessHeavenFuruiResponse);
    registerParams(Opcode::ACTIVITY_QUERY_BACK, HeavenFurui::ACTIVITY_ID, ProcessHeavenFuruiResponse);

    registerOpcode(Opcode::BATTLESIX_COMBAT_INFO_BACK, ProcessBattleSixCombatInfoResponse);
    registerOpcode(Opcode::BATTLESIX_MATCH_BACK, ProcessBattleSixMatchResponse);
    registerOpcode(Opcode::BATTLESIX_CANCEL_MATCH_BACK, ProcessBattleSixCancelMatchResponse);
    registerOpcode(Opcode::BATTLESIX_PREPARE_COMBAT_BACK, ProcessBattleSixPrepareCombatResponse);
    registerOpcode(Opcode::BATTLESIX_OTHER_ESCAPED_BACK, ProcessBattleSixOtherEscapedResponse);
    registerOpcode(Opcode::BATTLESIX_COMBAT_OVER_BACK, ProcessBattleSixCombatOverResponse);
    registerOpcode(Opcode::BATTLESIX_SELF_BAN_SPIRIT_BACK, ProcessBattleSixSelfBanSpiritResponse);
    registerOpcode(Opcode::BATTLESIX_FINISH_FIRST_SPIRIT_BACK, ProcessBattleSixFinishFirstSpiritResponse);
    registerOpcode(Opcode::BATTLESIX_REQ_START_BACK, ProcessBattleSixReqStartResponse);

    // 精魄系统响应处理
    registerOpcode(Opcode::SPIRIT_PRESURES_BACK, ProcessSpiritPresuresResponse);
    registerOpcode(Opcode::SPIRIT_SEND_SPIRIT_BACK, ProcessSpiritSendSpiritResponse);
    registerOpcode(Opcode::SPIRIT_PLAYER_INFO_BACK, ProcessSpiritPlayerInfoResponse);
    registerParams(Opcode::SPIRIT_COLLECT_BACK, SPIRIT_COLLECT_ACT_ID, ProcessSpiritCollectResponse);
}



    



    // ============================================================================



    // ActivityStateManager 实现






    



    



    ActivityStateManager& ActivityStateManager::Instance() {
        static ActivityStateManager instance;
        return instance;
    }

    Act778State& ActivityStateManager::GetAct778State() {
        return m_act778State;
    }

    Act685State& ActivityStateManager::GetAct685State() {
        return m_act685State;
    }

    Act666State& ActivityStateManager::GetAct666State() {
        return m_act666State;
    }

    Act757State& ActivityStateManager::GetAct757State() {
        return m_act757State;
    }

    Act822State& ActivityStateManager::GetAct822State() {
        return m_act822State;
    }

    Act826State& ActivityStateManager::GetAct826State() {
        return m_act826State;
    }

    Act827State& ActivityStateManager::GetAct827State() {
        return m_act827State;
    }

    Act641State& ActivityStateManager::GetAct641State() {
        return m_act641State;
    }

    Act684State& ActivityStateManager::GetAct684State() {
        return m_act684State;
    }

    Act717State& ActivityStateManager::GetAct717State() {
        return m_act717State;
    }

    Act805State& ActivityStateManager::GetAct805State() {
        return m_act805State;
    }

    Act631State& ActivityStateManager::GetAct631State() {
        return m_act631State;
    }

    HorseCompetitionState& ActivityStateManager::GetHorseCompetitionState() {
        return m_horseCompetitionState;
    }

    

                    // 封包发送（带超时、重试和自动等待响应）



    // ============================================================================


BOOL SendPacket(
    SOCKET s,
    const BYTE* pData,
    DWORD dwSize,
    uint32_t expectedOpcode,
    DWORD timeoutMs,
    uint32_t expectedParams,
    bool matchExpectedParams) {
    SOCKET targetSocket = (s != 0) ? s : g_LastGameSocket;
    if (targetSocket == 0) {
        return FALSE;
    }

    struct PendingResponseGuard {
        bool active = false;
        ~PendingResponseGuard() {
            if (active) {
                ResponseWaiter::EndWait();
            }
        }
    } pendingResponseGuard;

    uint64_t responseSerial = 0;
    const bool shouldWait = expectedOpcode != 0 && timeoutMs > 0;
    if (shouldWait) {
        responseSerial = ResponseWaiter::BeginWait();
        pendingResponseGuard.active = true;
    }

    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0) {
            Sleep(WpeHook::TIMEOUT_RETRY_INTERVAL);
        }

        targetSocket = (s != 0) ? s : g_LastGameSocket;
        if (targetSocket == 0) {
            continue;
        }

        DWORD sendTimeout = WpeHook::TIMEOUT_SEND;
        DWORD originalTimeout = sendTimeout;
        int optLen = sizeof(originalTimeout);
        getsockopt(targetSocket, SOL_SOCKET, SO_SNDTIMEO,
                   reinterpret_cast<char*>(&originalTimeout), &optLen);
        setsockopt(targetSocket, SOL_SOCKET, SO_SNDTIMEO,
                   reinterpret_cast<char*>(&sendTimeout), sizeof(sendTimeout));

        struct SocketTimeoutGuard {
            SOCKET socket = 0;
            DWORD originalTimeout = 0;
            ~SocketTimeoutGuard() {
                if (socket != 0) {
                    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
                               reinterpret_cast<const char*>(&originalTimeout),
                               sizeof(originalTimeout));
                }
            }
        } timeoutGuard{targetSocket, originalTimeout};

        int result = SOCKET_ERROR;
        {
            SendPacketCompleteGuard completeSendGuard;
            result = HookedSend(targetSocket,
                                reinterpret_cast<const char*>(pData),
                                static_cast<int>(dwSize),
                                0);
        }
        if (result == SOCKET_ERROR) {
            const int error = WSAGetLastError();
            if (error == WSAECONNRESET || error == WSAECONNABORTED) {
                return FALSE;
            }
            continue;
        }

        if (result != static_cast<int>(dwSize)) {
            return FALSE;
        }

        if (shouldWait) {
            const bool received = ResponseWaiter::WaitForResponse(
                expectedOpcode,
                timeoutMs,
                responseSerial,
                expectedParams,
                matchExpectedParams,
                true);
            if (!received) {
                return FALSE;
            }
        }

        return TRUE;
    }

    return FALSE;
}
// ============================================================================

static void ProcessStoryTaskTalkResponse(const GamePacket& packet) {
    ProcessEightTrigramsTaskTalkResponse(packet);
    StoryTaskOnDialogResponse(packet);
}

static void ProcessStoryNpcTaskListResponse(const GamePacket& packet) {
    StoryTaskOnNpcTaskListResponse(packet);
}

static void ProcessStoryNpcListResponse(const GamePacket& packet) {
    StoryTaskOnNpcListResponse(packet);
}

static void ProcessStoryMagicIndexResponse(const GamePacket& packet) {
    StoryTaskOnMagicIndexResponse(packet);
}

static void ProcessStoryTaskFinishedResponse(const GamePacket& packet) {
    StoryTaskOnTaskFinishedResponse(packet);
}

// ============================================================================
// 坐骑大赛实现已迁移到 src/hook/horse_competition.cpp.
// ============================================================================


SOCKET GetLastGameSocket() {
    return g_LastGameSocket.load();
}
