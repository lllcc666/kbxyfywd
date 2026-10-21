#pragma once

/// 带边界检查的封包读取器。
///
/// 每读一个字段前先确认剩余字节够用，不足就返回 false 而不是把越界读丢给调用方。
/// 读失败不改变读取位置，调用方直接 return 即可，不需要自己维护一组 offset 比较。

#include <cstddef>
#include <cstdint>
#include <string>

#include "packet_types.h"
#include "utils.h"

class BoundedReader {
public:
    BoundedReader(const uint8_t* data, size_t size)
        : m_data(data), m_size(size), m_offset(0) {}

    bool ReadI32(int32_t& value) {
        if (!CanRead(sizeof(int32_t))) return false;
        value = ReadInt32LE(m_data, m_offset);
        return true;
    }

    bool ReadU32(uint32_t& value) {
        if (!CanRead(sizeof(uint32_t))) return false;
        value = ReadUInt32LE(m_data, m_offset);
        return true;
    }

    bool ReadU16(uint16_t& value) {
        if (!CanRead(sizeof(uint16_t))) return false;
        value = ReadUInt16LE(m_data, m_offset);
        return true;
    }

    bool Skip(size_t count) {
        if (!CanRead(count)) return false;
        m_offset += count;
        return true;
    }

    bool CanRead(size_t count) const {
        return m_offset <= m_size && count <= m_size - m_offset;
    }

    size_t Remaining() const { return m_size - m_offset; }
    size_t Offset() const { return m_offset; }

    bool ReadUtf8(std::wstring& value) {
        uint16_t byteLength = 0;
        if (!ReadU16(byteLength) || !CanRead(byteLength)) return false;
        value = Utf8ToWide(std::string(reinterpret_cast<const char*>(m_data + m_offset), byteLength));
        m_offset += byteLength;
        return true;
    }

private:
    const uint8_t* m_data;
    size_t m_size;
    size_t m_offset;
};
