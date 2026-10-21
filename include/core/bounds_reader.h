#pragma once

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ============================================================================
// 协议读取器
// ============================================================================
//
// 背景：历史代码里每个回包解析器都手写 `offset + 4 <= body.size()` 这类判断，
// 一旦字段变长或漏写一个判断就变成越界读（Release 读脏数据、Debug 直接崩）。
// 这里把「读 + 边界检查」合成一步：越界只让 ok() 变 false 并记住首个越界位置，
// 调用方在解析结束后统一检查一次即可，不必逐字段判断。
//
// 用法：
//     BoundsReader r(packet.body.data(), packet.body.size(), "Act822/open_ui");
//     int32_t playCount = 0, restTime = 0;
//     r.ReadI32(playCount);
//     r.ReadI32(restTime);
//     if (!r.ok()) {
//         LogParserFailure(r);   // 带 tag 与偏移，定位比裸 offset 判断快得多
//         return;
//     }
//
// 约定：
//   * 构造时 data 为 nullptr 视作 size 为 0，任何读取都失败；
//   * 失败后后续读取一律返回 false，不会继续推进 m_offset；
//   * 不做字符串编码转换，读到的是原始字节，需宽字符时由调用方决定编码。
class BoundsReader {
public:
    BoundsReader(const BYTE* data, size_t size, const char* tag = nullptr)
        : m_data(data), m_size(data ? size : 0), m_tag(tag ? tag : "") {}

    BoundsReader(const std::vector<BYTE>& body, const char* tag = nullptr)
        : m_data(body.data()), m_size(body.size()), m_tag(tag ? tag : "") {}

    bool ok() const { return !m_failed; }
    size_t offset() const { return m_offset; }
    size_t size() const { return m_size; }
    size_t remaining() const { return m_offset <= m_size ? m_size - m_offset : 0; }
    size_t failOffset() const { return m_failOffset; }
    const char* tag() const { return m_tag; }

    // 失败时给出可直接打进日志的描述，例如 "Act822/open_ui 越界读 @12/16"。
    std::string FailureReport() const {
        if (!m_failed) {
            return std::string();
        }
        char buf[160] = {0};
        sprintf_s(buf, "%s 越界读 @%zu/%zu", m_tag, m_failOffset, m_size);
        return std::string(buf);
    }

    void Reset() {
        m_offset = 0;
        m_failed = false;
        m_failOffset = 0;
    }

    bool Skip(size_t n) {
        if (!CanRead(n)) {
            return false;
        }
        m_offset += n;
        return true;
    }

    bool ReadBytes(void* dst, size_t n) {
        if (!CanRead(n)) {
            return false;
        }
        if (n > 0) {
            memcpy(dst, m_data + m_offset, n);
        }
        m_offset += n;
        return true;
    }

    bool ReadU8(uint8_t& v) {
        return ReadPod(v);
    }

    bool ReadI8(int8_t& v) {
        return ReadPod(v);
    }

    bool ReadU16(uint16_t& v) {
        return ReadPod(v);
    }

    bool ReadI16(int16_t& v) {
        return ReadPod(v);
    }

    bool ReadU32(uint32_t& v) {
        return ReadPod(v);
    }

    bool ReadI32(int32_t& v) {
        return ReadPod(v);
    }

    bool ReadI32(int& v) {
        int32_t tmp = 0;
        if (!ReadPod(tmp)) {
            return false;
        }
        v = static_cast<int>(tmp);
        return true;
    }

    bool ReadF32(float& v) {
        return ReadPod(v);
    }

    // uint16 长度前缀 + 原始字节（AS3 writeUTF 风格）。长度超过剩余字节即失败，
    // 不截断、不吞掉异常，避免"读到 0 长度当成功"这种静默错位。
    bool ReadString(std::string& out) {
        uint16_t len = 0;
        if (!ReadU16(len)) {
            return false;
        }
        if (!CanRead(len)) {
            return false;
        }
        out.assign(reinterpret_cast<const char*>(m_data + m_offset), len);
        m_offset += len;
        return true;
    }

private:
    template <typename T>
    bool ReadPod(T& v) {
        if (!CanRead(sizeof(T))) {
            return false;
        }
        memcpy(&v, m_data + m_offset, sizeof(T));
        m_offset += sizeof(T);
        return true;
    }

    bool CanRead(size_t n) {
        if (m_failed) {
            return false;
        }
        if (n > m_size || m_offset > m_size - n) {
            m_failed = true;
            m_failOffset = m_offset;
            return false;
        }
        return true;
    }

    const BYTE* m_data = nullptr;
    size_t m_size = 0;
    size_t m_offset = 0;
    size_t m_failOffset = 0;
    bool m_failed = false;
    const char* m_tag = "";
};
