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

struct Md5FaceEntry {
    const char* md5;
    int face;
};

// 图片内容MD5 -> 正反值映射表 (1=正面/面向你, 0=反面/背向你)
static const Md5FaceEntry g_md5FaceEntries[] = {
    {"6817be5af4b0e77f8446e5a007a4cc28", 0},
    {"74b9314abf6f6cfd2238c6a6236eb5f6", 0},
    {"4d622957f07627b2bc3709f70d669042", 0},
    {"e17631cae5f6a80b47eeae71dbc3c33c", 1},
    {"85a15ce5f34ab6603ee4f7eb22af4c0d", 1},
    {"1278f375c7eb202feb67c119cc8e801b", 0},
    {"a088db9ab3699ea8b5c93117b102c6b9", 0},
    {"849fbb8920a2a65766fa5098265b9ac8", 0},
    {"cb10d81505ded8aa0464b154365125bd", 0},
    {"b8b14263e617489c74bd6a7a9dcdeec5", 0},
    {"7b1dee29c9cc5abe3b33b16d84c86558", 1},
    {"d3c48477832015bdf502f59c67af9e78", 1},
    {"194edd9073b6202eae074c98c063cf09", 1},
    {"a8ff055f0b9545a1452124552bb6f99d", 1},
    {"727f5d15b8fc454d41e7821993006a00", 1},
    {"05eff1b5db485099344d95b1b9335495", 1},
    {"d273efe1270692d9f953b1f589939690", 1},
    {"866eca22ca73a65ed0093d93e32d0ab9", 0},
    {"564aac45bea2042e3a03c32de6d4d27a", 0},
    {"e44739c71ec7522e9d2c11f48278f9d6", 0},
    {"95e59bb35ab652f315acf02a90da65cf", 0},
    {"5c923c72ec51f68216e50a7e57ebd390", 1},
    {"822824d609cf40465093dccd880f6071", 1},
    {"f22b2b947e75a434cf4e5d15f62b4058", 0},
    {"e3a1e2e14c88b23513282fa79cf9b1ef", 0},
    {"7d5d876c5a3ae740e3de8e3c9cff16d7", 1},
    {"0c2cccca9f03980f122864b4f724aed2", 0},
    {"dd1bd287a069788cd25c510b06b66513", 1},
    {"5eeb80b7fc6502a260ff0f6026690d53", 0},
    {"6fb5816f27fc166abaae49e9467ba4cb", 0},
    {"fb68bfa2140cc9d15d0a2115c80f6680", 1},
    {"073c262eb6b27fcd23c393ad19c74a6e", 0},
    {"85405a09136a8adb0c8dce3c623cecb0", 1},
    {"3ab7e8982f30bbc13250352df96aa3ee", 1},
    {"930a7013354000a2eb5d3bdcb93b303c", 1},
    {"8893db073a63d2f0af703a640ebeab89", 1},
    {"fb767d00dd3ebb1974801bfe80a56800", 0},
    {"8c0f86fc2e4555b0dbb737624d95be0f", 0},
    {"6fcfa01f4faf1198b683449ccca8b1de", 1},
    {"39e78f92c6e6041536b73287cf778668", 1}
};

// ============ MD5图片验证自动回复 ============

// 图片内容MD5 -> 正反值映射表 (1=正面/面向你, 0=反面/背向你)

// 下载图片并计算MD5 (POST方式)
static std::string DownloadImageAndGetMD5(const std::string& imageMd5) {
    HINTERNET hSession = WinHttpOpen(L"MD5CheckBot/1.0", 
                                      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, 
                                      WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";
    
    HINTERNET hConnect = WinHttpConnect(hSession, L"php.wanwan4399.com", 
                                         INTERNET_DEFAULT_HTTP_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return "";
    }
    
    // POST 方式
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/game_guard/image.php",
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }
    
    // POST 数据: md5=xxx
    std::string postData = "md5=" + imageMd5;
    
    // 发送请求
    std::string result;
    LPCWSTR headers = L"Content-Type: application/x-www-form-urlencoded\r\n";
    if (WinHttpSendRequest(hRequest, headers, static_cast<DWORD>(-1),
                           (LPVOID)postData.data(), (DWORD)postData.size(),
                           (DWORD)postData.size(), 0)) {
        if (WinHttpReceiveResponse(hRequest, NULL)) {
            DWORD dwSize = 0;
            DWORD dwDownloaded = 0;
            do {
                dwSize = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                if (dwSize == 0) break;
                
                std::vector<char> buffer(dwSize);
                if (!WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) break;
                result.append(buffer.data(), dwDownloaded);
            } while (dwSize > 0);
        }
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    if (result.empty()) return "";
    
    // 计算MD5
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    BYTE rgbHash[16];
    DWORD cbHash = 16;
    
    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        return "";
    }
    if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return "";
    }
    if (!CryptHashData(hHash, (BYTE*)result.data(), (DWORD)result.size(), 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return "";
    }
    if (!CryptGetHashParam(hHash, HP_HASHVAL, rgbHash, &cbHash, 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return "";
    }
    
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    
    // 转为十六进制字符串
    char hex[33];
    for (int i = 0; i < 16; i++) {
        sprintf_s(hex + i * 2, 3, "%02x", rgbHash[i]);
    }
    hex[32] = '\0';
    
    return std::string(hex);
}

// 查找图片正反值
static int FindFaceValue(const std::string& imageContentMd5) {
    for (const auto& entry : g_md5FaceEntries) {
        if (imageContentMd5 == entry.md5) {
            return entry.face;
        }
    }
    return -1;  // 未找到
}

// 计算正确的验证索引
static int CalculateCorrectIndex(const std::vector<std::string>& imageMd5s, int verifyType) {
    // verifyType: 1=面向你(找正面), 其他=背向你(找反面)
    // 正面=1, 反面=0
    
    int targetValue = (verifyType == 1) ? 1 : 0;
    
    std::vector<int> faceValues;
    for (const auto& md5 : imageMd5s) {
        std::string contentMd5 = DownloadImageAndGetMD5(md5);
        if (contentMd5.empty()) {
            faceValues.push_back(-1);
            continue;
        }
        int value = FindFaceValue(contentMd5);
        faceValues.push_back(value);
    }
    
    // 统计目标值数量
    int targetCount = 0;
    int firstTargetIndex = -1;
    int firstOtherIndex = -1;
    
    for (int i = 0; i < 4; i++) {
        if (faceValues[i] == targetValue) {
            targetCount++;
            if (firstTargetIndex < 0) firstTargetIndex = i;
        } else if (faceValues[i] >= 0 && firstOtherIndex < 0) {
            firstOtherIndex = i;
        }
    }
    
    // 如果只有1个目标值，返回其索引
    if (targetCount == 1 && firstTargetIndex >= 0) {
        return firstTargetIndex;
    }
    // 否则返回第一个其他值（或第一个目标值）
    if (firstOtherIndex >= 0) return firstOtherIndex;
    if (firstTargetIndex >= 0) return firstTargetIndex;
    
    return 0;  // 默认返回0
}

// MD5验证回复封包
// Opcode: 1186193, Params: index (0-3)
BOOL SendMD5CheckReplyPacket(int index) {
    auto packet = PacketBuilder()
        .SetOpcode(1186193)
        .SetParams(static_cast<uint32_t>(index))
        .Build();
    return SendPacketBytes(packet);
}

// 处理MD5验证封包并自动回复
void ProcessMD5CheckAndReply(const std::vector<BYTE>& body, uint32_t params) {
    // 先解压 body 数据（Flash ByteArray.uncompress() 格式）
    std::vector<uint8_t> decompressed;
    
    // 获取 zlib uncompress 函数
    auto uncompressFunc = GetZlibUncompress();
    if (uncompressFunc && !body.empty()) {
        // 检查 zlib 头 (0x78 表示 zlib 格式)
        if (body.size() >= 2 && body[0] == 0x78) {
            // 预分配解压缓冲区（通常是压缩前的4-10倍）
            decompressed.resize(body.size() * 10);
            unsigned long destLen = static_cast<unsigned long>(decompressed.size());
            
            int res = uncompressFunc(decompressed.data(), &destLen, body.data(), static_cast<unsigned long>(body.size()));
            if (res == 0) {
                decompressed.resize(destLen);
            } else {
                // 解压失败，使用原始数据
                decompressed.assign(body.begin(), body.end());
            }
        } else {
            // 不是 zlib 格式，直接使用原始数据
            decompressed.assign(body.begin(), body.end());
        }
    } else {
        decompressed.assign(body.begin(), body.end());
    }
    
    // 从解压后的数据中提取4个UTF字符串（图片MD5值）
    std::vector<std::string> imageMd5s;
    size_t offset = 0;
    
    for (int i = 0; i < 4 && offset < decompressed.size(); i++) {
        // UTF字符串格式：长度(2字节小端) + 内容
        if (offset + 2 > decompressed.size()) break;
        
        uint16_t strLen = decompressed[offset] | (decompressed[offset + 1] << 8);
        offset += 2;
        
        if (offset + strLen > decompressed.size()) break;
        
        std::string md5(decompressed.begin() + offset, decompressed.begin() + offset + strLen);
        imageMd5s.push_back(md5);
        offset += strLen;
    }
    
    if (imageMd5s.size() < 4) return;
    
    // 计算正确索引
    int correctIndex = CalculateCorrectIndex(imageMd5s, params);
    
    // 发送回复
    SendMD5CheckReplyPacket(correctIndex);
}

