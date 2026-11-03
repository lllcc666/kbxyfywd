#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

typedef struct _PACKET {
    DWORD dwSize;
    BOOL bSend;
    BYTE* pData;
    DWORD dwTime;
} PACKET, *PPACKET;

BOOL SendPacket(SOCKET s, const BYTE* pData, DWORD dwSize,
                uint32_t expectedOpcode = 0, DWORD timeoutMs = 0,
                uint32_t expectedParams = 0, bool matchExpectedParams = false);
BOOL SendPacketBytes(const std::vector<BYTE>& packet);
BOOL SendPacketBytes(const std::vector<BYTE>& packet, uint32_t expectedOpcode,
                     DWORD timeoutMs, uint32_t expectedParams = 0,
                     bool matchExpectedParams = false);

VOID ClearPacketList();
VOID DeleteSelectedPackets(const std::vector<DWORD>& indices);
DWORD GetPacketCount();
// 内存里保留的封包数。UI 只渲染最后 kPacketUiRenderLimit 条，
// 但内存容量仍决定「往回翻」能否删除更早的包，故不宜过小。
constexpr std::size_t kMaxPacketListSize = 5000;

// 封包列表一次最多渲染的行数。
// 列表是「边收边看」的滚动窗，2 万行 DOM 带来的重排/内存开销远大于收益；
// 超出部分只保留在内存列表里，由 C++ 侧的序号继续累加。
constexpr std::size_t kPacketUiRenderLimit = 800;

struct PacketSnapshot {
    std::size_t index;
    bool bSend;
    DWORD dwTime;
    std::vector<BYTE> data;
};

void FormatPacketClock(DWORD tick, wchar_t* out, std::size_t outCount);
std::vector<PacketSnapshot> SnapshotPacketList();

/// 只深拷贝列表尾部 limit 条。
/// 整表重建只需要渲染最近若干条，全量快照会让 UI 线程在临界区里
/// 拷贝上万个包的字节数组，反过来卡住游戏 socket 线程。
std::vector<PacketSnapshot> SnapshotRecentPacketList(std::size_t limit);
std::string HexToString(const BYTE* pData, DWORD dwSize, bool spaced = true);
std::vector<BYTE> StringToHex(const std::string& str);
void AppendPacketAndShow(const PACKET& packet);
void SyncPacketsToUI();

extern std::atomic<uint32_t> g_userId;
extern std::atomic<HWND> g_hWnd;
extern std::atomic<bool> g_autoHeal;
extern std::atomic<bool> g_blockBattle;
extern std::atomic<bool> g_autoGoHome;
extern std::atomic<int32_t> g_battleCounter;
extern std::atomic<bool> g_collectFinished;
extern std::atomic<bool> g_collectAutoMode;
extern std::atomic<int> g_collectStatus;
SOCKET GetLastGameSocket();
