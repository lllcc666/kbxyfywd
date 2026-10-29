#pragma once
#include <windows.h>
#include <string>
#include <vector>

using PacketProgressCallback = void (*)(DWORD, DWORD, const std::string&);

struct HexPacketData { char* data; size_t len; };
enum HijackType { HIJACK_NONE = 0, HIJACK_BLOCK = 1, HIJACK_REPLACE = 2 };
struct HijackRule {
    HijackType type;
    bool forSend;
    bool forRecv;
    std::string searchHex;
    std::string replaceHex;
};

std::string GetPacketLabel(uint32_t opcode, bool bSend);
BOOL AddHijackRule(HijackType type, bool forSend, bool forRecv,
                   const std::string& searchHex, const std::string& replaceHex = "");
VOID ClearHijackRules();
VOID SetHijackEnabled(bool enable);
bool ProcessHijack(bool bSend, const BYTE* pData, DWORD* pdwSize, std::vector<BYTE>* pModifiedData);
DWORD SendAllPackets(DWORD intervalMs = 100, DWORD loopCount = 1,
                     PacketProgressCallback progressCallback = nullptr);
VOID StopAutoSend();
