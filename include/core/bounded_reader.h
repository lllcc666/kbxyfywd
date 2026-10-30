#pragma once

/// 带边界检查的封包读取器。
///
/// 这是 ByteReader 的薄适配层：ByteReader 越界时返回 0 并置 fail 位，
/// 这里把"读失败"显式变成 false 交回调用方，调用方直接 return 即可，
/// 不需要自己维护一组 offset 比较。字节拼装逻辑只在 ByteReader 里有一份。

#include <cstddef>
#include <cstdint>

#include "byte_reader.h"

class BoundedReader {
public:
    BoundedReader(const uint8_t* data, size_t size) : m_reader(data, size, m_offset) {}

    // ByteReader 持有的是 m_offset 的引用，按值传递时要让新对象的读取器
    // 绑到自己的 m_offset 上，否则会继续推进源对象的游标。
    BoundedReader(const BoundedReader& other)
        : m_offset(other.m_offset),
          m_reader(other.m_reader.data(), other.m_reader.size(), m_offset) {}

    bool ReadI32(int32_t& value) {
        if (!m_reader.CanRead(4)) return false;
        value = m_reader.I32();
        return true;
    }

    bool ReadU32(uint32_t& value) {
        if (!m_reader.CanRead(4)) return false;
        value = m_reader.U32();
        return true;
    }

    bool CanRead(size_t count) const { return m_reader.CanRead(count); }
    size_t Remaining() const { return m_reader.Remaining(); }

private:
    // m_offset 必须声明在 m_reader 之前：m_reader 构造时持有 m_offset 的引用。
    size_t m_offset = 0;
    ByteReader m_reader;
};
