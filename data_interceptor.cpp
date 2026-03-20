/**
 * @file data_interceptor.cpp
 * @brief data 文件拦截和修改模块实现
 * 
 * 拦截游戏下载的 spiritdata 文件，为所有妖怪添加 display 属性
 * 使所有妖怪在战斗中都能显示技能 PP 值
 * 
 * AS3 代码分析:
 * - SpiritXmlData.as 直接从 URLUtil.getSvnVer("config/spiritdata") 下载
 * - 使用 ByteArray.uncompress() 解压 (Flash 的 zlib 格式)
 * - 解析 XML，在 getShowDisplay() 中查找有 display 属性的妖怪
 * 
 * 重要: spiritdata 是独立的 zlib 压缩文件，不是 ZIP 包！
 */

#include "data_interceptor.h"
#include "packet_parser.h"
#include "utils.h"
#include <cstring>
#include <algorithm>
#include <string>

// 前置声明：从 packet_parser.h 中导入
typedef int (*ZlibCompress2Func)(unsigned char* dest, unsigned long* destLen,
                                  const unsigned char* source, unsigned long sourceLen, int level);
extern ZlibCompress2Func GetZlibCompress2();

// ============================================================================
// 内部常量和变量
// ============================================================================

namespace {

// spiritdata 文件的 URL 关键字（直接下载，不是从 data.zip 里）
// 匹配: http://enter.wanwan4399.com/bin-debug/config/spiritdata?v=xxx
const char* SPIRITDATA_URL_PATTERNS[] = {
    "spiritdata",           // 匹配任何包含 spiritdata 的 URL
    "config/spiritdata",    // 原始匹配
    nullptr
};

// 禁用修改模式：只返回原始数据（不解压不修改）
// 原因：AS3 代码中 getShowDisplay() 函数返回第一个有 display 属性的妖怪
// 如果给所有妖怪添加 display 属性，会导致返回错误的妖怪，显示错误的技能面板
// 原始游戏只允许特定的 29 个妖怪显示技能 PP 值
#ifdef _DEBUG
constexpr bool DISABLE_MODIFICATION = false;
#else
constexpr bool DISABLE_MODIFICATION = true;
#endif

// 测试模式：直接返回原始数据，不进行任何处理
constexpr bool TEST_BYPASS_MODE = false;

// 测试模式：仅解压再压缩，不修改内容
constexpr bool TEST_MODE_NO_MODIFY = false;

// 缓存已修改的 data
std::vector<BYTE> g_cachedModifiedData;
bool g_dataModified = false;

}  // anonymous namespace

// ============================================================================
// URL 检测
// ============================================================================

BOOL DataInterceptor::IsDataUrl(const char* url) {
    if (!url) return FALSE;
    
    for (int i = 0; SPIRITDATA_URL_PATTERNS[i] != nullptr; ++i) {
        if (strstr(url, SPIRITDATA_URL_PATTERNS[i]) != nullptr) {
            return TRUE;
        }
    }
    return FALSE;
}

// ============================================================================
// zlib 解压（Flash ByteArray.uncompress() 兼容格式）
// ============================================================================

/**
 * @brief 解压 Flash ByteArray.uncompress() 格式的数据
 * 
 * Flash 的 ByteArray.uncompress() 使用标准 zlib 格式:
 * - 前 2 字节是 zlib 头 (0x78 0x9C 表示默认压缩)
 * - 后面是压缩数据
 * - 最后 4 字节是 Adler-32 校验和
 */
BOOL DataInterceptor::DecompressData(
    const std::vector<BYTE>& compressedData,
    std::vector<BYTE>& decompressedData
) {
    if (compressedData.size() < 6) {
        // 数据太小，不可能是有效的压缩数据
        return FALSE;
    }
    
    // 获取内存加载的 zlib 函数
    auto uncompress = GetZlibUncompress();
    if (!uncompress) {
        return FALSE;
    }
    
    // 检查 zlib 头 (0x78 表示 zlib 格式)
    // 0x78 0x01 = 无压缩/低压缩
    // 0x78 0x9C = 默认压缩
    // 0x78 0xDA = 最大压缩
    if (compressedData[0] != 0x78) {
        // 不是 zlib 格式，可能是原始数据
        decompressedData = compressedData;
        return TRUE;
    }
    
    // 尝试多次解压，逐步增加缓冲区大小
    unsigned long destLen = static_cast<unsigned long>(compressedData.size()) * 4;
    const unsigned long maxDestLen = static_cast<unsigned long>(compressedData.size()) * 100;
    int res = -5; // Z_BUF_ERROR
    
    while (destLen <= maxDestLen) {
        decompressedData.resize(destLen);
        res = uncompress(decompressedData.data(), &destLen, 
                         compressedData.data(), 
                         static_cast<unsigned long>(compressedData.size()));
        
        if (res == 0) { // Z_OK
            decompressedData.resize(destLen);
            return TRUE;
        }
        
        if (res != -5) { // 不是缓冲区不足错误，直接退出
            break;
        }
        
        // 缓冲区不足，增加大小重试
        destLen *= 2;
    }
    
    decompressedData.clear();
    return FALSE;
}

// ============================================================================
// zlib 压缩（Flash ByteArray.compress() 兼容格式）
// ============================================================================

/**
 * @brief 压缩数据为 Flash ByteArray.compress() 兼容格式
 *
 * 使用标准 zlib 格式压缩，指定压缩级别为 9（Z_BEST_COMPRESSION）
 * 以匹配原始文件的 78 DA 头部
 * 注：Flash 的 ByteArray.uncompress() 支持 78 01, 78 9c, 78 da 等所有 zlib 头
 */
BOOL DataInterceptor::CompressData(
    const std::vector<BYTE>& data,
    std::vector<BYTE>& compressedData
) {
    if (data.empty()) {
        return FALSE;
    }

    // 获取内存加载的 zlib compress2 函数（支持指定压缩级别）
    auto compress2Func = GetZlibCompress2();
    if (!compress2Func) {
        // 如果 compress2 不可用，回退到 compress（默认级别）
        auto compressFunc = GetZlibCompress();
        if (!compressFunc) {
            return FALSE;
        }

        unsigned long destLen = static_cast<unsigned long>(data.size()) +
                                static_cast<unsigned long>(data.size() / 1000) + 128;

        compressedData.resize(destLen);

        int res = compressFunc(compressedData.data(), &destLen,
                               data.data(),
                               static_cast<unsigned long>(data.size()));

        if (res == 0) { // Z_OK
            compressedData.resize(destLen);
            return TRUE;
        }

        compressedData.clear();
        return FALSE;
    }

    // 使用 compress2 指定压缩级别为 9（Z_BEST_COMPRESSION）
    // 这会产生 78 DA 的 zlib 头部，匹配原始文件
    constexpr int Z_BEST_COMPRESSION = 9;

    // 计算压缩后最大大小
    unsigned long destLen = static_cast<unsigned long>(data.size()) +
                            static_cast<unsigned long>(data.size() / 1000) + 128;

    compressedData.resize(destLen);

    int res = compress2Func(compressedData.data(), &destLen,
                            data.data(),
                            static_cast<unsigned long>(data.size()),
                            Z_BEST_COMPRESSION);

    if (res == 0) { // Z_OK
        compressedData.resize(destLen);
        return TRUE;
    }

    compressedData.clear();
    return FALSE;
}

// ============================================================================
// XML 处理 - 为所有 spirit 节点添加 <display>1</display> 子元素
// ============================================================================

std::vector<char> DataInterceptor::AddDisplayAttribute(const std::vector<char>& xmlData) {
    std::string result(xmlData.begin(), xmlData.end());
    
    // 在每个 </sframe> 后面插入 <display>1</display>
    // 选择 </sframe> 而不是 <release> 的原因：
    // 1. 所有 spirit 节点都有 <sframe> 标签
    // 2. 原始文件中 display 就在 <sframe> 和 <url> 之间
    const std::string sframeEndTag = "</sframe>";
    const std::string displayTag = "\n        <display>1</display>";
    const std::string spiritStartTag = "<spirit ";
    const std::string spiritEndTag = "</spirit>";
    
    size_t pos = 0;
    int modifiedCount = 0;
    
    while (pos < result.size()) {
        // 查找 </sframe>
        size_t sframeEndPos = result.find(sframeEndTag, pos);
        
        if (sframeEndPos == std::string::npos) break;
        
        // 计算插入位置（</sframe> 之后）
        size_t insertPos = sframeEndPos + sframeEndTag.length();
        
        // 检查这个 spirit 节点是否已经有 <display> 子元素
        // 方法：找到当前 spirit 节点的开始和结束位置，然后检查这个节点内是否有 display
        
        // 向前查找最近的 <spirit 标签（注意：使用 "<spirit " 而不是 "<spirit" 以避免匹配 <spirits>）
        size_t spiritStartPos = result.rfind(spiritStartTag, sframeEndPos);
        if (spiritStartPos == std::string::npos) {
            // 没有找到 <spirit 标签，跳过
            pos = insertPos;
            continue;
        }
        
        // 查找对应的 </spirit> 标签
        size_t spiritEndPos = result.find(spiritEndTag, sframeEndPos);
        if (spiritEndPos == std::string::npos) {
            // 没有找到 </spirit> 标签，跳过
            pos = insertPos;
            continue;
        }
        
        // 检查在 <spirit> 和 </spirit> 之间是否有 <display> 标签
        size_t displayPos = result.find("<display", spiritStartPos);
        bool hasDisplay = (displayPos != std::string::npos && displayPos < spiritEndPos);
        
        if (!hasDisplay) {
            // 在 </sframe> 之后插入 <display>1</display>
            result.insert(insertPos, displayTag);
            modifiedCount++;
            // 更新位置，跳过刚插入的内容和 </sframe>
            pos = insertPos + displayTag.length();
        } else {
            // 已有 display，跳到下一个位置
            pos = insertPos;
        }
    }
    
    return std::vector<char>(result.begin(), result.end());
}

// ============================================================================
// HTTP 响应处理
// ============================================================================

BOOL DataInterceptor::ProcessHttpResponse(
    const char* url,
    const BYTE* pData,
    DWORD dwSize,
    std::vector<BYTE>& modifiedData
) {
    if (!IsDataUrl(url) || !pData || dwSize == 0) {
        return FALSE;
    }

    // 安全检查：防止过大文件
    if (dwSize > (10 * 1024 * 1024)) { // 最大10MB
        return FALSE;
    }

    // 禁用修改模式：直接返回原始数据
    // 原因：AS3 代码中 getShowDisplay() 返回第一个有 display 属性的妖怪
    // 如果给所有妖怪添加 display 属性，会导致返回错误的妖怪
    if constexpr (DISABLE_MODIFICATION) {
        modifiedData.assign(pData, pData + dwSize);
        return TRUE;
    }

    // 解压 zlib 数据
    std::vector<BYTE> compressedData(pData, pData + dwSize);
    std::vector<BYTE> decompressedData;

    if (!DecompressData(compressedData, decompressedData)) {
        return FALSE;
    }

    // 添加 display 子元素
    std::vector<char> xmlData(decompressedData.begin(), decompressedData.end());
    xmlData.push_back('\0');  // 添加 null 终止符

    std::vector<char> modifiedXml = AddDisplayAttribute(xmlData);

    // 移除末尾的 null 终止符
    std::vector<BYTE> xmlBytes(modifiedXml.begin(), modifiedXml.end());
    if (!xmlBytes.empty() && xmlBytes.back() == '\0') {
        xmlBytes.pop_back();
    }

    // 重新压缩
    if (!CompressData(xmlBytes, modifiedData)) {
        return FALSE;
    }

    return TRUE;
}

// ============================================================================
// 初始化和清理
// ============================================================================

BOOL DataInterceptor::Initialize() {
    g_cachedModifiedData.clear();
    g_dataModified = false;
    return TRUE;
}

VOID DataInterceptor::Cleanup() {
    g_cachedModifiedData.clear();
    g_dataModified = false;
}