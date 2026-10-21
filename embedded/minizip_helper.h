/*
 * minizip_helper.h - 轻量级ZIP解压辅助函数
 * 纯内存操作，使用已加载的zlib进行解压
 */

#pragma once

#include <vector>
#include <cstdint>
#include <cstring>
#include <string>

// ZIP文件结构常量
constexpr uint32_t ZIP_LOCAL_FILE_HEADER_SIGNATURE = 0x04034b50;
constexpr uint32_t ZIP_CENTRAL_DIR_SIGNATURE = 0x02014b50;
constexpr uint32_t ZIP_END_OF_CENTRAL_DIR_SIGNATURE = 0x06054b50;
constexpr uint16_t ZIP_COMPRESSION_STORE = 0;
constexpr uint16_t ZIP_COMPRESSION_DEFLATE = 8;

// 本地文件头结构（简化版）
#pragma pack(push, 1)
struct ZipLocalFileHeader {
    uint32_t signature;          // 0x04034b50
    uint16_t versionNeeded;      // 解压所需版本
    uint16_t generalFlag;        // 通用标志
    uint16_t compressionMethod;  // 压缩方法
    uint16_t lastModTime;        // 最后修改时间
    uint16_t lastModDate;        // 最后修改日期
    uint32_t crc32;              // CRC-32
    uint32_t compressedSize;     // 压缩后大小
    uint32_t uncompressedSize;   // 解压后大小
    uint16_t fileNameLength;     // 文件名长度
    uint16_t extraFieldLength;   // 额外字段长度
};

struct ZipCentralDirHeader {
    uint32_t signature;          // 0x02014b50
    uint16_t versionMadeBy;      // 创建版本
    uint16_t versionNeeded;      // 解压所需版本
    uint16_t generalFlag;        // 通用标志
    uint16_t compressionMethod;  // 压缩方法
    uint16_t lastModTime;        // 最后修改时间
    uint16_t lastModDate;        // 最后修改日期
    uint32_t crc32;              // CRC-32
    uint32_t compressedSize;     // 压缩后大小
    uint32_t uncompressedSize;   // 解压后大小
    uint16_t fileNameLength;     // 文件名长度
    uint16_t extraFieldLength;   // 额外字段长度
    uint16_t commentLength;      // 注释长度
    uint16_t diskNumberStart;    // 起始磁盘号
    uint16_t internalAttr;       // 内部文件属性
    uint32_t externalAttr;       // 外部文件属性
    uint32_t relativeOffset;     // 本地文件头相对偏移
};

struct ZipEndOfCentralDir {
    uint32_t signature;          // 0x06054b50
    uint16_t diskNumber;         // 当前磁盘号
    uint16_t centralDirDisk;     // 中央目录所在磁盘
    uint16_t numEntriesOnDisk;   // 当前磁盘上的条目数
    uint16_t totalNumEntries;    // 总条目数
    uint32_t centralDirSize;     // 中央目录大小
    uint32_t centralDirOffset;   // 中央目录偏移
    uint16_t commentLength;      // 注释长度
};
#pragma pack(pop)

// zlib函数类型定义
typedef int (*UncompressFunc)(unsigned char* dest, unsigned long* destLen,
                               const unsigned char* source, unsigned long sourceLen);
typedef int (*InflateInit2Func)(void* strm, int windowBits, const char* version, int stream_size);
typedef int (*InflateFunc)(void* strm, int flush);
typedef int (*InflateEndFunc)(void* strm);

// z_stream结构体定义（与zlib兼容）
// 注意：如果本文件之前已经 include 了真正的 zlib.h，就用 zlib 自己的 z_stream_s，
// 否则会撞上 C2011 类型重定义。
#ifndef ZLIB_H
struct z_stream_s {
    unsigned char* next_in;
    unsigned int avail_in;
    unsigned long total_in;
    unsigned char* next_out;
    unsigned int avail_out;
    unsigned long total_out;
    char* msg;
    void* state;
    void* zalloc;
    void* zfree;
    void* opaque;
    int data_type;
    unsigned long adler;
    unsigned long reserved;
};
#endif  // ZLIB_H

// 使用inflate解压raw deflate数据（ZIP格式）
inline int UncompressRawDeflate(
    unsigned char* dest, unsigned long* destLen,
    const unsigned char* source, unsigned long sourceLen,
    InflateInit2Func inflateInit2,
    InflateFunc inflate,
    InflateEndFunc inflateEnd
) {
    if (!inflateInit2 || !inflate || !inflateEnd) {
        return -1;
    }
    
    z_stream_s strm = {};
    strm.next_in = const_cast<unsigned char*>(source);
    strm.avail_in = static_cast<unsigned int>(sourceLen);
    strm.next_out = dest;
    strm.avail_out = static_cast<unsigned int>(*destLen);
    
    // -15 = -MAX_WBITS 表示raw deflate（没有zlib头）
    int ret = (inflateInit2)(&strm, -15, "1.2.11", sizeof(z_stream_s));
    if (ret != 0) {
        return ret;
    }
    
    ret = inflate(&strm, 0); // Z_NO_FLUSH = 0
    if (ret != 1) { // Z_STREAM_END = 1
        inflateEnd(&strm);
        return ret;
    }
    
    *destLen = strm.total_out;
    inflateEnd(&strm);
    return 0;
}

/**
 * @brief 从ZIP内存数据中解压指定文件
 * @param zipData ZIP文件数据
 * @param filename 要解压的文件名
 * @param outData 输出解压后的数据
 * @param uncompress zlib的uncompress函数指针（备用）
 * @param inflateInit2 inflateInit2_函数指针
 * @param inflate inflate函数指针
 * @param inflateEnd inflateEnd函数指针
 * @return 解压是否成功
 */
inline bool ExtractZipEntryFromMemory(
    const std::vector<uint8_t>& zipData,
    const std::string& filename,
    std::vector<uint8_t>& outData,
    UncompressFunc uncompress,
    InflateInit2Func inflateInit2 = nullptr,
    InflateFunc inflate = nullptr,
    InflateEndFunc inflateEnd = nullptr
) {
    outData.clear();
    
    if (zipData.size() < sizeof(ZipEndOfCentralDir)) {
        return false;
    }
    
    const uint8_t* data = zipData.data();
    const size_t dataSize = zipData.size();
    
    // 查找中央目录结束标记 (EOCD在文件末尾64KB内)
    const ZipEndOfCentralDir* eocd = nullptr;
    const size_t searchStart = dataSize - sizeof(ZipEndOfCentralDir);
    const size_t searchLimit = (dataSize > 65536 + sizeof(ZipEndOfCentralDir))
                               ? dataSize - 65536 - sizeof(ZipEndOfCentralDir)
                               : 0;
    
    for (long long i = static_cast<long long>(searchStart);
         i >= static_cast<long long>(searchLimit); --i) {
        const uint32_t sig = *reinterpret_cast<const uint32_t*>(data + i);
        if (sig == ZIP_END_OF_CENTRAL_DIR_SIGNATURE) {
            eocd = reinterpret_cast<const ZipEndOfCentralDir*>(data + i);
            break;
        }
    }
    
    if (!eocd) {
        return false;
    }
    
    size_t cdOffset = eocd->centralDirOffset;
    const size_t numEntries = eocd->totalNumEntries;
    
    // 边界检查
    if (cdOffset >= dataSize || numEntries == 0 || numEntries > 65535) {
        return false;
    }
    
    // 遍历中央目录查找文件
    for (size_t i = 0; i < numEntries && cdOffset < dataSize; ++i) {
        if (cdOffset + sizeof(ZipCentralDirHeader) > dataSize) {
            break;
        }
        
        const ZipCentralDirHeader* cdh = 
            reinterpret_cast<const ZipCentralDirHeader*>(data + cdOffset);
        
        if (cdh->signature != ZIP_CENTRAL_DIR_SIGNATURE) {
            break;
        }
        
        const size_t fileNameOffset = cdOffset + sizeof(ZipCentralDirHeader);
        if (fileNameOffset + cdh->fileNameLength > dataSize) {
            break;
        }
        
        const std::string entryName(
            reinterpret_cast<const char*>(data + fileNameOffset),
            cdh->fileNameLength);
        
        if (entryName == filename) {
            // 找到目标文件
            const size_t localHeaderOffset = cdh->relativeOffset;
            if (localHeaderOffset + sizeof(ZipLocalFileHeader) > dataSize) {
                return false;
            }
            
            const ZipLocalFileHeader* lfh = 
                reinterpret_cast<const ZipLocalFileHeader*>(data + localHeaderOffset);
            if (lfh->signature != ZIP_LOCAL_FILE_HEADER_SIGNATURE) {
                return false;
            }
            
            // 使用中央目录头的大小信息
            const uint32_t compressedSize = cdh->compressedSize;
            const uint32_t uncompressedSize = cdh->uncompressedSize;
            const uint16_t compressionMethod = cdh->compressionMethod;
            
            if (compressedSize == 0 || uncompressedSize == 0 || 
                uncompressedSize > (50 * 1024 * 1024)) {
                return false;
            }
            
            const size_t dataOffset = localHeaderOffset + sizeof(ZipLocalFileHeader)
                                    + lfh->fileNameLength + lfh->extraFieldLength;
            
            if (dataOffset + compressedSize > dataSize) {
                return false;
            }
            
            // 解压数据
            if (compressionMethod == ZIP_COMPRESSION_STORE) {
                outData.resize(uncompressedSize);
                memcpy(outData.data(), data + dataOffset, uncompressedSize);
                return true;
            } else if (compressionMethod == ZIP_COMPRESSION_DEFLATE) {
                outData.resize(uncompressedSize);
                unsigned long destLen = uncompressedSize;
                int result = -1;
                
                // 优先使用inflate（支持raw deflate）
                if (inflateInit2 && inflate && inflateEnd) {
                    result = UncompressRawDeflate(
                        outData.data(), &destLen,
                        data + dataOffset, compressedSize,
                        inflateInit2, inflate, inflateEnd);
                } else if (uncompress) {
                    // 回退到uncompress（可能失败，因为期望zlib格式）
                    result = uncompress(outData.data(), &destLen,
                                        data + dataOffset, compressedSize);
                } else {
                    outData.clear();
                    return false;
                }
                
                if (result == 0) {
                    outData.resize(destLen);
                    return true;
                }
                outData.clear();
                return false;
            }
            return false;
        }
        
        // 移动到下一个中央目录条目
        const size_t entrySize = sizeof(ZipCentralDirHeader)
                               + cdh->fileNameLength + cdh->extraFieldLength 
                               + cdh->commentLength;
        
        if (cdOffset + entrySize < cdOffset || cdOffset + entrySize > dataSize) {
            break;
        }
        cdOffset += entrySize;
    }
    
    return false;
}