/**
 * @file packet_builder.cpp
 * @brief 封包构建器实现
 * 
 * 统一封包构造，自动处理小端序编码
 */

#include "packet_builder.h"

// ============================================================================
// 构造函数
// ============================================================================

PacketBuilder::PacketBuilder()
    : m_magic(PacketProtocol::MAGIC_NORMAL)  // 默认 0x5344 "SD"
    , m_opcode(0)
    , m_params(0)
{
    m_body.reserve(256);  // 预分配空间
}

// ============================================================================
// 头部设置方法
// ============================================================================

PacketBuilder& PacketBuilder::SetMagic(uint16_t magic) {
    m_magic = magic;
    return *this;
}

PacketBuilder& PacketBuilder::SetOpcode(uint32_t opcode) {
    m_opcode = opcode;
    return *this;
}

PacketBuilder& PacketBuilder::SetParams(uint32_t params) {
    m_params = params;
    return *this;
}

// ============================================================================
// Body 写入方法
// ============================================================================

PacketBuilder& PacketBuilder::WriteString(const std::string& str) {
    // 写入字符串长度（小端序，2字节）
    uint16_t len = static_cast<uint16_t>(str.length());
    m_body.push_back(static_cast<uint8_t>(len & 0xFF));         // 低字节在前
    m_body.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));  // 高字节在后
    
    // 写入字符串内容
    for (char c : str) {
        m_body.push_back(static_cast<uint8_t>(c));
    }
    
    return *this;
}

PacketBuilder& PacketBuilder::WriteInt32(int32_t value) {
    m_body.push_back(static_cast<uint8_t>(value & 0xFF));
    m_body.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    m_body.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    m_body.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    return *this;
}

PacketBuilder& PacketBuilder::WriteUInt32(uint32_t value) {
    m_body.push_back(static_cast<uint8_t>(value & 0xFF));
    m_body.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    m_body.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    m_body.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    return *this;
}

PacketBuilder& PacketBuilder::WriteInt16(int16_t value) {
    m_body.push_back(static_cast<uint8_t>(value & 0xFF));
    m_body.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    return *this;
}

PacketBuilder& PacketBuilder::WriteUInt16(uint16_t value) {
    m_body.push_back(static_cast<uint8_t>(value & 0xFF));
    m_body.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    return *this;
}

PacketBuilder& PacketBuilder::WriteByte(uint8_t value) {
    m_body.push_back(value);
    return *this;
}

PacketBuilder& PacketBuilder::WriteBytes(const std::vector<uint8_t>& data) {
    m_body.insert(m_body.end(), data.begin(), data.end());
    return *this;
}

PacketBuilder& PacketBuilder::WriteInt32Array(const std::vector<int32_t>& values) {
    for (int32_t value : values) {
        WriteInt32(value);
    }
    return *this;
}

// ============================================================================
// 构建方法
// ============================================================================

std::vector<uint8_t> PacketBuilder::Build() {
    // 计算总长度：头部12字节 + Body
    size_t totalLen = PacketProtocol::HEADER_SIZE + m_body.size();
    std::vector<uint8_t> packet;
    packet.reserve(totalLen);
    
    // 写入头部
    // Magic (2B)
    packet.push_back(static_cast<uint8_t>(m_magic & 0xFF));
    packet.push_back(static_cast<uint8_t>((m_magic >> 8) & 0xFF));
    
    // Length (2B) - Body 长度
    uint16_t bodyLen = static_cast<uint16_t>(m_body.size());
    packet.push_back(static_cast<uint8_t>(bodyLen & 0xFF));
    packet.push_back(static_cast<uint8_t>((bodyLen >> 8) & 0xFF));
    
    // Opcode (4B, 小端序)
    packet.push_back(static_cast<uint8_t>(m_opcode & 0xFF));
    packet.push_back(static_cast<uint8_t>((m_opcode >> 8) & 0xFF));
    packet.push_back(static_cast<uint8_t>((m_opcode >> 16) & 0xFF));
    packet.push_back(static_cast<uint8_t>((m_opcode >> 24) & 0xFF));
    
    // Params (4B, 小端序)
    packet.push_back(static_cast<uint8_t>(m_params & 0xFF));
    packet.push_back(static_cast<uint8_t>((m_params >> 8) & 0xFF));
    packet.push_back(static_cast<uint8_t>((m_params >> 16) & 0xFF));
    packet.push_back(static_cast<uint8_t>((m_params >> 24) & 0xFF));
    
    // 写入 Body
    packet.insert(packet.end(), m_body.begin(), m_body.end());
    
    return packet;
}

void PacketBuilder::Reset() {
    m_magic = PacketProtocol::MAGIC_NORMAL;
    m_opcode = 0;
    m_params = 0;
    m_body.clear();
}

// ============================================================================
// 快捷构造函数
// ============================================================================

std::vector<uint8_t> BuildActivityPacket(
    uint32_t opcode,
    uint32_t activityId,
    const std::string& operation,
    const std::vector<int32_t>& bodyValues
) {
    return PacketBuilder()
        .SetOpcode(opcode)
        .SetParams(activityId)
        .WriteString(operation)
        .WriteInt32Array(bodyValues)
        .Build();
}
