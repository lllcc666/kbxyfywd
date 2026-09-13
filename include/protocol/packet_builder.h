/**
 * @file packet_builder.h
 * @brief 封包构建器 - 统一封包构造接口
 * 
 * 提供链式调用的封包构造接口，自动处理小端序编码
 * 重构目标：减少 50% 的封包构造重复代码
 */

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <limits>
#include "packet_protocol.h"

/**
 * @class PacketBuilder
 * @brief 封包构建器
 * 
 * 使用链式调用构造游戏封包，自动处理小端序编码
 * 
 * @example
 * @code
 * auto packet = PacketBuilder()
 *     .SetOpcode(1185429)
 *     .SetParams(770)
 *     .WriteString("game_info")
 *     .Build();
 * @endcode
 */
class PacketBuilder {
public:
    PacketBuilder();
    ~PacketBuilder() = default;

    // ================================
    // 头部设置方法
    // ================================

    /**
     * @brief 设置 Magic（默认为 0x5344 "SD"）
     * @param magic Magic 值
     * @return 引用自身，支持链式调用
     */
    PacketBuilder& SetMagic(uint16_t magic);

    /**
     * @brief 设置 Opcode
     * @param opcode 操作码
     * @return 引用自身，支持链式调用
     */
    PacketBuilder& SetOpcode(uint32_t opcode);

    /**
     * @brief 设置 Params
     * @param params 参数值
     * @return 引用自身，支持链式调用
     */
    PacketBuilder& SetParams(uint32_t params);

    // ================================
    // Body 写入方法
    // ================================

    /**
     * @brief 写入字符串（带小端序长度前缀）
     * @param str 字符串内容
     * @return 引用自身，支持链式调用
     */
    PacketBuilder& WriteString(const std::string& str);

    /**
     * @brief 写入 int32 值（小端序）
     * @param value 整数值
     * @return 引用自身，支持链式调用
     */
    PacketBuilder& WriteInt32(int32_t value);

    /**
     * @brief 写入 uint32 值（小端序）
     * @param value 整数值
     * @return 引用自身，支持链式调用
     */
    PacketBuilder& WriteUInt32(uint32_t value);

    /**
     * @brief 写入 int16 值（小端序）
     * @param value 整数值
     * @return 引用自身，支持链式调用
     */
    PacketBuilder& WriteInt16(int16_t value);

    /**
     * @brief 写入 uint16 值（小端序）
     * @param value 整数值
     * @return 引用自身，支持链式调用
     */
    PacketBuilder& WriteUInt16(uint16_t value);

    /**
     * @brief 写入单字节
     * @param value 字节值
     * @return 引用自身，支持链式调用
     */
    PacketBuilder& WriteByte(uint8_t value);

    /**
     * @brief 写入字节数组
     * @param data 字节数组
     * @return 引用自身，支持链式调用
     */
    PacketBuilder& WriteBytes(const std::vector<uint8_t>& data);

    /**
     * @brief 写入 int32 数组（每个元素小端序）
     * @param values 整数数组
     * @return 引用自身，支持链式调用
     */
    PacketBuilder& WriteInt32Array(const std::vector<int32_t>& values);

    // ================================
    // 构建方法
    // ================================

    /**
     * @brief 构建最终封包
     * @return 封包字节数组
     */
    std::vector<uint8_t> Build();

    /**
     * @brief 清空缓冲区，重新开始构建
     */
    void Reset();

    // ================================
    // 工具方法
    // ================================

    /**
     * @brief 获取当前 Body 长度
     * @return Body 长度
     */
    size_t GetBodyLength() const { return m_body.size(); }

private:
    // 头部字段
    uint16_t m_magic;     ///< Magic 值（默认 0x5344）
    uint32_t m_opcode;    ///< Opcode
    uint32_t m_params;    ///< Params

    bool m_valid = true;       ///< Body 长度和字段长度是否仍符合协议限制
    std::vector<uint8_t> m_body;  ///< Body 数据

};

// ================================
// 快捷构造函数
// ================================

/**
 * @brief 快速构造活动操作封包（通用格式）
 * @param opcode 操作码
 * @param activityId 活动ID
 * @param operation 操作字符串
 * @param bodyValues Body值列表
 * @return 完整封包
 * 
 * 封包格式：
 * - Magic: 0x5344
 * - Length: Body长度
 * - Opcode: 指定值
 * - Params: 活动ID
 * - Body: [字符串长度(2B)][字符串内容][int32值们]
 */
std::vector<uint8_t> BuildActivityPacket(
    uint32_t opcode,
    uint32_t activityId,
    const std::string& operation,
    const std::vector<int32_t>& bodyValues = {}
);
