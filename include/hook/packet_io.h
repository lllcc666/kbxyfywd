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
constexpr std::size_t kMaxPacketListSize = 20000;

struct PacketSnapshot {
    std::size_t index;
    bool bSend;
    DWORD dwTime;
    std::vector<BYTE> data;
};

void FormatPacketClock(DWORD tick, wchar_t* out, std::size_t outCount);
std::vector<PacketSnapshot> SnapshotPacketList();
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
