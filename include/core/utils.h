/**
 * @file utils.h
 * @brief 工具函数和通用组件
 * 
 * 提供编码转换、线程同步等通用功能
 */

#pragma once

#include <string>
#include <vector>
#include <windows.h>
#include <cstdint>

// ============================================================================
// 编码转换函数
// ============================================================================

/**
 * @brief 将UTF-8字符串转换为宽字符字符串
 * @param utf8 UTF-8编码的字符串
 * @return 宽字符字符串
 */
std::wstring Utf8ToWide(const std::string& utf8);

/**
 * @brief 将宽字符字符串转换为UTF-8字符串
 * @param wide 宽字符字符串
 * @return UTF-8编码的字符串
 */
std::string WideToUtf8(const std::wstring& wide);

/**
 * @brief 将指定编码的字节序列转换为宽字符字符串
 * @param bytes 字节序列
 * @param codepage 代码页（如 936 表示 GBK）
 * @return 宽字符字符串
 */
std::wstring MultiToWide(const std::string& bytes, unsigned int codepage);

bool TryParseInt32Decimal(const std::string& text, int32_t& value);
bool TryParseUInt32Decimal(const std::string& text, uint32_t& value);

// ============================================================================
// RAII 临界区锁
// ============================================================================

/**
 * @class CriticalSectionGuard
 * @brief RAII风格的临界区自动锁
 * 
 * 构造时自动进入临界区，析构时自动离开临界区
 * 确保异常安全，避免死锁
 * 
 * @example
 * @code
 * CriticalSectionLock lock(cs);
 * // 临界区代码...
 * // 离开作用域时自动解锁
 * @endcode
 */
class CriticalSectionLock {
public:
    /**
     * @brief 构造函数，自动进入临界区
     * @param cs 临界区对象的引用
     */
    explicit CriticalSectionLock(CRITICAL_SECTION& cs) 
        : m_cs(cs) 
        , m_locked(true) {
        EnterCriticalSection(&m_cs);
    }

    /**
     * @brief 析构函数，自动离开临界区
     */
    ~CriticalSectionLock() {
        if (m_locked) {
            LeaveCriticalSection(&m_cs);
        }
    }

    // 禁止拷贝
    CriticalSectionLock(const CriticalSectionLock&) = delete;
    CriticalSectionLock& operator=(const CriticalSectionLock&) = delete;

    /**
     * @brief 手动提前解锁
     */
    void Unlock() {
        if (m_locked) {
            LeaveCriticalSection(&m_cs);
            m_locked = false;
        }
    }

    /**
     * @brief 手动重新加锁
     */
    void Lock() {
        if (!m_locked) {
            EnterCriticalSection(&m_cs);
            m_locked = true;
        }
    }

private:
    CRITICAL_SECTION& m_cs;  ///< 临界区引用
    bool m_locked;           ///< 是否已锁定
};

// ============================================================================
// RAII 临界区生命周期管理
// ============================================================================

/**
 * @class CriticalSectionScope
 * @brief 临界区的生命周期管理类
 * 
 * 构造时初始化临界区，析构时删除临界区
 * 配合 CriticalSectionLock 使用
 */
class CriticalSectionScope {
public:
    CriticalSectionScope() {
        InitializeCriticalSection(&m_cs);
    }

    ~CriticalSectionScope() {
        DeleteCriticalSection(&m_cs);
    }

    // 禁止拷贝
    CriticalSectionScope(const CriticalSectionScope&) = delete;
    CriticalSectionScope& operator=(const CriticalSectionScope&) = delete;

    /**
     * @brief 获取临界区引用
     * @return 临界区的引用
     */
    CRITICAL_SECTION& Get() { return m_cs; }

private:
    CRITICAL_SECTION m_cs;  ///< 临界区对象
};
