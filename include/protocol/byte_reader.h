#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

/// 越界安全的小端读取器。
///
/// 绑定一段内存 (data,size) 与调用方的 offset 变量：读成功时推进 offset，
/// 越界时不推进、返回 0，并把 fail 位记下来（ok() 查询）。
/// 之所以越界返回 0 而不是让调用方逐个判错：这些字段绝大多数是
/// "协议尾部可选字段"，读不到就当没有，给 0 与旧代码里的 (void) 丢弃等价；
/// 而真正的价值是——无论调用方忘了写守卫、还是守卫的字节数算错了，
/// 都不可能读到缓冲区之外。
class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size, size_t& offset)
        : m_data(data), m_size(data != nullptr ? size : 0), m_offset(offset) {}
    // 便捷构造：body.data() / body.size()
    ByteReader(const std::vector<uint8_t>& body, size_t& offset)
        : ByteReader(body.data(), body.size(), offset) {}

    int32_t I32() {
        if (!CanReadRaw(sizeof(int32_t))) {
            m_ok = false;
            return 0;
        }
        const int32_t val = static_cast<int32_t>(m_data[m_offset]) |
                            (static_cast<int32_t>(m_data[m_offset + 1]) << 8) |
                            (static_cast<int32_t>(m_data[m_offset + 2]) << 16) |
                            (static_cast<int32_t>(m_data[m_offset + 3]) << 24);
        m_offset += sizeof(int32_t);
        return val;
    }

    uint32_t U32() {
        if (!CanReadRaw(sizeof(uint32_t))) {
            m_ok = false;
            return 0;
        }
        const uint32_t val = static_cast<uint32_t>(m_data[m_offset]) |
                             (static_cast<uint32_t>(m_data[m_offset + 1]) << 8) |
                             (static_cast<uint32_t>(m_data[m_offset + 2]) << 16) |
                             (static_cast<uint32_t>(m_data[m_offset + 3]) << 24);
        m_offset += sizeof(uint32_t);
        return val;
    }

    int16_t I16() {
        if (!CanReadRaw(sizeof(int16_t))) {
            m_ok = false;
            return 0;
        }
        const int16_t val = static_cast<int16_t>(m_data[m_offset]) |
                            (static_cast<int16_t>(m_data[m_offset + 1]) << 8);
        m_offset += sizeof(int16_t);
        return val;
    }

    uint16_t U16() {
        if (!CanReadRaw(sizeof(uint16_t))) {
            m_ok = false;
            return 0;
        }
        const uint16_t val = static_cast<uint16_t>(m_data[m_offset]) |
                             (static_cast<uint16_t>(m_data[m_offset + 1]) << 8);
        m_offset += sizeof(uint16_t);
        return val;
    }

    bool ok() const { return m_ok; }  // 至今为止没有任何一次越界
    bool CanRead(size_t count) const { return CanReadRaw(count); }
    // size - offset，offset > size 时为 0（不下溢）
    size_t Remaining() const { return m_offset < m_size ? m_size - m_offset : 0; }
    size_t offset() const { return m_offset; }
    void Skip(size_t count) {
        if (!CanReadRaw(count)) {
            m_ok = false;
            return;
        }
        m_offset += count;
    }

    // 持有的是 offset 的引用，拷贝会指向另一个对象的游标，语义上是错的；
    // 需要"值语义"的地方（BoundedReader 按值传参）由 BoundedReader 自己重新绑定。
    ByteReader(const ByteReader&) = delete;
    ByteReader& operator=(const ByteReader&) = delete;

    // 供 BoundedReader 的拷贝构造把新对象重新绑到自己的 offset 上。
    const uint8_t* data() const { return m_data; }
    size_t size() const { return m_size; }

private:
    bool CanReadRaw(size_t count) const {
        return m_offset <= m_size && count <= m_size - m_offset;
    }

    const uint8_t* m_data;
    size_t m_size;
    size_t& m_offset;
    bool m_ok = true;
};
