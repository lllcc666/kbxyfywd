#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
小端序封包 Opcode 计算工具

用于计算卡布西游封包中的 Opcode 值。
封包格式：Magic(2B) + Length(2B) + Opcode(4B) + Params(4B) + Body

小端序（Little Endian）：低字节在前，高字节在后
例如封包 "2F 1E 12 00" 的 Opcode = 0x00121E2F = 1187375
"""

def hex_to_int_le(hex_bytes: str) -> int:
    """
    将十六进制字节字符串（小端序）转换为整数
    
    Args:
        hex_bytes: 十六进制字节字符串，如 "2F 1E 12 00" 或 "2F1E1200"
    
    Returns:
        整数值
    """
    # 移除空格和常见分隔符
    hex_bytes = hex_bytes.replace(" ", "").replace("-", "").replace(":", "")
    
    # 确保是偶数长度
    if len(hex_bytes) % 2 != 0:
        raise ValueError("十六进制字符串长度必须是偶数")
    
    # 解析为字节数组（小端序）
    bytes_list = [int(hex_bytes[i:i+2], 16) for i in range(0, len(hex_bytes), 2)]
    
    # 小端序转整数：第一个字节是最低位
    result = 0
    for i, b in enumerate(bytes_list):
        result |= b << (i * 8)
    
    return result


def int_to_hex_le(value: int, byte_count: int = 4) -> str:
    """
    将整数转换为小端序十六进制字符串
    
    Args:
        value: 整数值
        byte_count: 字节数（默认4字节）
    
    Returns:
        十六进制字符串，如 "2F 1E 12 00"
    """
    hex_bytes = []
    for i in range(byte_count):
        b = (value >> (i * 8)) & 0xFF
        hex_bytes.append(f"{b:02X}")
    return " ".join(hex_bytes)


def parse_packet(hex_string: str) -> dict:
    """
    解析完整封包
    
    Args:
        hex_string: 完整封包的十六进制字符串
    
    Returns:
        包含 Magic, Length, Opcode, Params, Body 的字典
    """
    # 清理输入
    hex_string = hex_string.replace(" ", "").replace("\n", "").replace("\r", "")
    
    if len(hex_string) < 24:  # 最少12字节 = 24个十六进制字符
        raise ValueError("封包太短，至少需要12字节")
    
    # 解析各字段
    magic = hex_string[0:4]
    length_hex = hex_string[4:8]
    opcode_hex = hex_string[8:16]
    params_hex = hex_string[16:24]
    body_hex = hex_string[24:] if len(hex_string) > 24 else ""
    
    # 转换为整数
    magic_int = int(magic, 16)
    # 注意：Magic 是大端序 "SD" = 0x5344
    magic_str = chr(int(magic[0:2], 16)) + chr(int(magic[2:4], 16))
    
    length = hex_to_int_le(length_hex)
    opcode = hex_to_int_le(opcode_hex)
    params = hex_to_int_le(params_hex)
    
    result = {
        "magic_hex": magic,
        "magic_str": magic_str,
        "length": length,
        "opcode_hex": opcode_hex,
        "opcode": opcode,
        "params_hex": params_hex,
        "params": params,
        "body_hex": body_hex,
        "body_length": len(body_hex) // 2
    }
    
    return result


def main():
    print("=" * 60)
    print("小端序封包 Opcode 计算工具")
    print("=" * 60)
    
    # 示例计算 - 根据易语言代码
    examples = [
        ("跳舞大赛活动操作 (2F1E)", "2F 1E 12 00"),
        ("跳舞大赛阶段 (281E)", "28 1E 12 00"),
        ("进入活动 (281E Params=1)", "28 1E 12 00"),
        ("退出活动 (281E Params=2)", "28 1E 12 00"),
        ("进入世界 (FE14)", "FE 14 12 00"),
        ("灵玉列表 (111E)", "11 1E 12 00"),
    ]
    
    print("\n【常用 Opcode 计算】")
    print("-" * 60)
    for name, hex_bytes in examples:
        opcode = hex_to_int_le(hex_bytes)
        print(f"{name}:")
        print(f"  十六进制: {hex_bytes}")
        print(f"  十进制:   {opcode}")
        print(f"  验证:     {int_to_hex_le(opcode)}")
        print()
    
    # 完整封包解析示例
    print("\n【完整封包解析示例】")
    print("-" * 60)
    packet = "44 53 20 00 2F 1E 12 00 04 00 00 00"
    parsed = parse_packet(packet)
    print(f"封包: {packet}")
    print(f"  Magic:   {parsed['magic_str']} (0x{parsed['magic_hex']})")
    print(f"  Length:  {parsed['length']}")
    print(f"  Opcode:  {parsed['opcode']} (0x{parsed['opcode_hex']})")
    print(f"  Params:  {parsed['params']} (0x{parsed['params_hex']})")
    
    print("\n" + "=" * 60)
    print("使用方法:")
    print("  1. hex_to_int_le('2F 1E 12 00')  -> 1187375")
    print("  2. int_to_hex_le(1187375)         -> '2F 1E 12 00'")
    print("  3. parse_packet('4453...')        -> 解析完整封包")
    print("=" * 60)


if __name__ == "__main__":
    main()