#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
卡布西游 SWF 分析工具
用于下载和分析游戏SWF文件

使用方法:
    python analyze_swf.py --game 12        # 下载并分析小游戏12（深度挖宝）
    python analyze_swf.py --main           # 下载并分析主程序
    python analyze_swf.py --batch 1-20     # 批量下载小游戏
    python analyze_swf.py --info game.swf  # 分析本地SWF文件
    python analyze_swf.py --decompile game12.swf  # 使用FFDec反编译

依赖:
    pip install pylzma  # 可选，用于解压缩SWF
    FFDec: 用于完整反编译AS3代码
"""

import argparse
import struct
import os
import sys
import urllib.request
import urllib.error
import zlib
import subprocess
import shutil
from pathlib import Path
from io import BytesIO

# 游戏基础URL
BASE_URL = "http://enter.wanwan4399.com/bin-debug/"
MAIN_GAME = "MainGame.swf"
LITTLEGAME_PATH = "assets/littlegame/game{id}.swf"

# FFDec路径
FFDEC_PATHS = [
    r"D:\Program Files (x86)\FFDec\ffdec-cli.exe",
    r"C:\Program Files (x86)\FFDec\ffdec-cli.exe",
    r"D:\Program Files\FFDec\ffdec-cli.exe",
    r"C:\Program Files\FFDec\ffdec-cli.exe",
]

FFDEC_JAR_PATHS = [
    r"D:\Program Files (x86)\FFDec\ffdec-cli.jar",
    r"C:\Program Files (x86)\FFDec\ffdec-cli.jar",
    r"D:\Program Files\FFDec\ffdec-cli.jar",
    r"C:\Program Files\FFDec\ffdec-cli.jar",
    r"D:\Program Files (x86)\FFDec\ffdec.jar",
    r"C:\Program Files (x86)\FFDec\ffdec.jar",
    r"D:\Program Files\FFDec\ffdec.jar",
    r"C:\Program Files\FFDec\ffdec.jar",
]


def build_ffdec_command(candidate: str):
    """把 FFDec 可执行文件 / jar 路径转成 subprocess 命令数组"""
    if not candidate:
        return None

    candidate = os.path.abspath(os.path.expanduser(candidate))
    if not os.path.exists(candidate):
        return None

    lower = candidate.lower()
    if lower.endswith((".exe", ".bat", ".cmd")):
        return [candidate]

    if lower.endswith(".jar"):
        java = shutil.which("java")
        if java:
            return [java, "-jar", candidate]

    return None


def find_ffdec(ffdec_override: str = None):
    """查找 FFDec 命令，返回适合 subprocess.run 的命令数组"""
    candidates = []

    if ffdec_override:
        override = os.path.abspath(os.path.expanduser(ffdec_override))
        if os.path.isdir(override):
            for name in ("ffdec-cli.exe", "ffdec.exe", "ffdec-cli.jar", "ffdec.jar"):
                candidates.append(os.path.join(override, name))
        else:
            candidates.append(override)

    ffdec_cli = shutil.which("ffdec-cli")
    if ffdec_cli:
        candidates.append(ffdec_cli)

    ffdec_cli_exe = shutil.which("ffdec-cli.exe")
    if ffdec_cli_exe:
        candidates.append(ffdec_cli_exe)

    ffdec_exe = shutil.which("ffdec.exe")
    if ffdec_exe:
        candidates.append(ffdec_exe)

    ffdec = shutil.which("ffdec")
    if ffdec:
        candidates.append(ffdec)

    candidates.extend(FFDEC_PATHS)

    ffdec_jar = shutil.which("ffdec.jar")
    if ffdec_jar:
        candidates.append(ffdec_jar)

    ffdec_cli_jar = shutil.which("ffdec-cli.jar")
    if ffdec_cli_jar:
        candidates.append(ffdec_cli_jar)

    candidates.extend(FFDEC_JAR_PATHS)

    seen = set()
    for candidate in candidates:
        if not candidate:
            continue
        normalized = os.path.normcase(os.path.abspath(os.path.expanduser(candidate)))
        if normalized in seen:
            continue
        seen.add(normalized)

        command = build_ffdec_command(candidate)
        if command:
            return command

    return None

# SWF Tag 类型定义
SWF_TAGS = {
    0: "End",
    1: "ShowFrame",
    2: "DefineShape",
    4: "PlaceObject",
    5: "RemoveObject",
    6: "DefineBits",
    7: "DefineButton",
    8: "JPEGTables",
    9: "SetBackgroundColor",
    10: "DefineFont",
    11: "DefineText",
    12: "DoAction",
    13: "DefineFontInfo",
    14: "DefineSound",
    15: "StartSound",
    17: "DefineButtonSound",
    18: "SoundStreamHead",
    19: "SoundStreamBlock",
    20: "DefineBitsLossless",
    21: "DefineBitsJPEG2",
    22: "DefineShape2",
    23: "DefineButtonCxform",
    24: "Protect",
    26: "PlaceObject2",
    28: "RemoveObject2",
    32: "DefineShape3",
    33: "DefineText2",
    34: "DefineButton2",
    35: "DefineBitsJPEG3",
    36: "DefineBitsLossless2",
    37: "DefineEditText",
    39: "DefineSprite",
    43: "FrameLabel",
    45: "SoundStreamHead2",
    46: "DefineMorphShape",
    48: "DefineFont2",
    56: "ExportAssets",
    57: "ImportAssets",
    58: "EnableDebugger",
    59: "DoInitAction",
    60: "DefineVideoStream",
    61: "VideoFrame",
    62: "DefineFontInfo2",
    64: "EnableDebugger2",
    65: "ScriptLimits",
    66: "SetTabIndex",
    69: "FileAttributes",
    70: "PlaceObject3",
    71: "ImportAssets2",
    73: "DefineFontAlignZones",
    74: "CSMTextSettings",
    75: "DefineFont3",
    76: "SymbolClass",
    77: "Metadata",
    78: "DefineScalingGrid",
    82: "DoABC",           # AS3字节码
    83: "DefineShape4",
    84: "DefineMorphShape2",
    86: "DefineSceneAndFrameLabelData",
    87: "DefineBinaryData",
    88: "DefineFontName",
    89: "StartSound2",
    90: "DefineBitsJPEG4",
    91: "DefineFont4",
}

# ABC (AS3 Bytecode) 常量池
ABC_CONSTANT_TYPES = {
    0: "Utf8",
    1: "Int",
    2: "Uint",
    3: "Double",
    4: "PrivateNs",
    5: "Namespace",
    6: "PackageNamespace",
    7: "PackageInternalNs",
    8: "ProtectedNamespace",
    9: "ExplicitNamespace",
    10: "StaticProtectedNs",
    11: "Multiname",
    12: "QName",
    13: "QNameA",
    14: "RTQName",
    15: "RTQNameA",
    16: "RTQNameL",
    17: "RTQNameLA",
    18: "MultinameA",
    19: "MultinameL",
    20: "MultinameLA",
}


class SWFParser:
    """SWF文件解析器"""
    
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0
        self.compressed = False
        self.version = 0
        self.file_length = 0
        self.frame_size = None
        self.frame_rate = 0
        self.frame_count = 0
        self.tags = []
        
    def parse(self):
        """解析SWF文件"""
        if len(self.data) < 8:
            raise ValueError("文件太小，不是有效的SWF")
        
        # 读取签名
        signature = self.data[0:3]
        if signature == b'FWS':
            self.compressed = False
        elif signature == b'CWS':
            self.compressed = True
        elif signature == b'ZWS':
            self.compressed = True  # LZMA压缩
        else:
            raise ValueError(f"无效的SWF签名: {signature}")
        
        self.version = self.data[3]
        self.file_length = struct.unpack('<I', self.data[4:8])[0]
        
        # 解压缩数据
        if self.compressed:
            if signature == b'CWS':
                # zlib压缩
                try:
                    self.uncompressed_data = zlib.decompress(self.data[8:])
                except zlib.error:
                    raise ValueError("zlib解压失败")
            else:
                # LZMA压缩 (简化处理，可能需要pylzma)
                self.uncompressed_data = self.data[8:]
                print("警告: LZMA压缩格式，可能无法完全解析")
        else:
            self.uncompressed_data = self.data[8:]
        
        # 解析帧信息
        self._parse_frame_info()
        
        # 解析Tags
        self._parse_tags()
        
        return self
    
    def _parse_frame_info(self):
        """解析帧大小和帧率"""
        # 使用位读取器
        data = self.uncompressed_data
        pos = 0
        
        # 读取RECT (帧大小)
        n_bits = (data[0] >> 3) & 0x1F
        total_bits = 5 + n_bits * 4  # 5位nBits + 4个nBits字段
        
        # 计算需要的字节数
        bytes_needed = (total_bits + 7) // 8
        
        # 提取RECT值
        def read_bits(data, start_bit, num_bits):
            result = 0
            for i in range(num_bits):
                byte_pos = (start_bit + i) // 8
                bit_pos = 7 - ((start_bit + i) % 8)
                if byte_pos < len(data):
                    bit = (data[byte_pos] >> bit_pos) & 1
                    result = (result << 1) | bit
            return result
        
        x_min = read_bits(data, 5, n_bits)
        x_max = read_bits(data, 5 + n_bits, n_bits)
        y_min = read_bits(data, 5 + n_bits * 2, n_bits)
        y_max = read_bits(data, 5 + n_bits * 3, n_bits)
        
        self.frame_size = {
            'x_min': x_min / 20,  # twips -> pixels
            'x_max': x_max / 20,
            'y_min': y_min / 20,
            'y_max': y_max / 20,
        }
        
        pos = bytes_needed
        
        # 帧率和帧数
        self.frame_rate = struct.unpack('<H', data[pos:pos+2])[0] / 256
        self.frame_count = struct.unpack('<H', data[pos+2:pos+4])[0]
        
        self.tag_start_pos = pos + 4
    
    def _parse_tags(self):
        """解析所有Tags"""
        pos = self.tag_start_pos
        data = self.uncompressed_data
        
        while pos < len(data) - 1:
            # 读取Tag头
            tag_code_and_length = struct.unpack('<H', data[pos:pos+2])[0]
            tag_type = tag_code_and_length >> 6
            tag_length = tag_code_and_length & 0x3F
            
            pos += 2
            
            # 如果长度=63，读取扩展长度
            if tag_length == 0x3F:
                if pos + 4 > len(data):
                    break
                tag_length = struct.unpack('<I', data[pos:pos+4])[0]
                pos += 4
            
            # 读取Tag数据
            if pos + tag_length > len(data):
                tag_length = len(data) - pos
            
            tag_data = data[pos:pos+tag_length]
            pos += tag_length
            
            tag_name = SWF_TAGS.get(tag_type, f"Unknown({tag_type})")
            
            self.tags.append({
                'type': tag_type,
                'name': tag_name,
                'length': tag_length,
                'offset': pos - tag_length,
                'data': tag_data
            })
            
            # End Tag
            if tag_type == 0:
                break
    
    def get_info(self):
        """获取SWF基本信息"""
        return {
            'version': self.version,
            'compressed': self.compressed,
            'file_length': self.file_length,
            'frame_size': self.frame_size,
            'frame_rate': self.frame_rate,
            'frame_count': self.frame_count,
            'tag_count': len(self.tags),
        }
    
    def get_abc_tags(self):
        """获取所有DoABC标签（AS3字节码）"""
        return [tag for tag in self.tags if tag['type'] == 82]
    
    def get_symbol_classes(self):
        """获取SymbolClass映射"""
        symbols = []
        for tag in self.tags:
            if tag['type'] == 76:  # SymbolClass
                data = tag['data']
                pos = 0
                count = struct.unpack('<H', data[pos:pos+2])[0]
                pos += 2
                for _ in range(count):
                    if pos + 2 > len(data):
                        break
                    char_id = struct.unpack('<H', data[pos:pos+2])[0]
                    pos += 2
                    
                    # 读取null结尾字符串
                    name_end = data.find(b'\x00', pos)
                    if name_end == -1:
                        break
                    name = data[pos:name_end].decode('utf-8', errors='ignore')
                    pos = name_end + 1
                    
                    symbols.append({'id': char_id, 'name': name})
        return symbols


def read_u30(data: bytes, pos: int) -> tuple:
    """读取U30变长整数 (ABC格式使用)"""
    result = 0
    shift = 0
    while pos < len(data):
        byte = data[pos]
        pos += 1
        result |= (byte & 0x7F) << shift
        if not (byte & 0x80):
            break
        shift += 7
        if shift >= 32:
            break
    return result, pos


def read_s32(data: bytes, pos: int) -> tuple:
    """读取S32变长整数（有符号）"""
    result = 0
    shift = 0
    while pos < len(data):
        byte = data[pos]
        pos += 1
        result |= (byte & 0x7F) << shift
        shift += 7
        if not (byte & 0x80):
            if shift < 32 and (byte & 0x40):
                result |= (~0 << shift)
            break
    return result, pos


def parse_abc_basic(data: bytes) -> dict:
    """解析ABC文件基本信息（改进版）"""
    result = {
        'minor_version': 0,
        'major_version': 0,
        'int_count': 0,
        'uint_count': 0,
        'double_count': 0,
        'string_count': 0,
        'namespace_count': 0,
        'ns_set_count': 0,
        'multiname_count': 0,
        'method_count': 0,
        'metadata_count': 0,
        'class_count': 0,
        'script_count': 0,
        'strings': [],
        'class_names': [],
        'method_names': [],
    }
    
    try:
        pos = 0
        
        # 版本号
        result['minor_version'] = struct.unpack('<H', data[pos:pos+2])[0]
        pos += 2
        result['major_version'] = struct.unpack('<H', data[pos:pos+2])[0]
        pos += 2
        
        # ===== 常量池 =====
        
        # Int常量
        int_count, pos = read_u30(data, pos)
        result['int_count'] = int_count
        for _ in range(int_count - 1 if int_count > 0 else 0):
            _, pos = read_s32(data, pos)
        
        # Uint常量
        uint_count, pos = read_u30(data, pos)
        result['uint_count'] = uint_count
        for _ in range(uint_count - 1 if uint_count > 0 else 0):
            _, pos = read_u30(data, pos)
        
        # Double常量
        double_count, pos = read_u30(data, pos)
        result['double_count'] = double_count
        pos += (double_count - 1 if double_count > 0 else 0) * 8
        
        # String常量
        string_count, pos = read_u30(data, pos)
        result['string_count'] = string_count
        
        # 读取字符串
        for _ in range(string_count - 1 if string_count > 0 else 0):
            str_len, pos = read_u30(data, pos)
            if str_len > 0 and pos + str_len <= len(data):
                try:
                    s = data[pos:pos+str_len].decode('utf-8', errors='ignore')
                    result['strings'].append(s)
                except:
                    pass
                pos += str_len
        
        # Namespace常量
        ns_count, pos = read_u30(data, pos)
        result['namespace_count'] = ns_count
        for _ in range(ns_count - 1 if ns_count > 0 else 0):
            pos += 1  # kind
            _, pos = read_u30(data, pos)  # name index
        
        # NsSet常量
        ns_set_count, pos = read_u30(data, pos)
        result['ns_set_count'] = ns_set_count
        for _ in range(ns_set_count - 1 if ns_set_count > 0 else 0):
            count, pos = read_u30(data, pos)
            for _ in range(count):
                _, pos = read_u30(data, pos)
        
        # Multiname常量
        mn_count, pos = read_u30(data, pos)
        result['multiname_count'] = mn_count
        for _ in range(mn_count - 1 if mn_count > 0 else 0):
            kind = data[pos] if pos < len(data) else 0
            pos += 1
            if kind in [0x07, 0x0D]:  # QName, QNameA
                _, pos = read_u30(data, pos)  # ns
                _, pos = read_u30(data, pos)  # name
            elif kind in [0x0F, 0x10]:  # RTQName, RTQNameA
                _, pos = read_u30(data, pos)  # name
            elif kind in [0x11, 0x12]:  # RTQNameL, RTQNameLA
                pass  # no data
            elif kind in [0x09, 0x0E]:  # Multiname, MultinameA
                _, pos = read_u30(data, pos)  # name
                _, pos = read_u30(data, pos)  # ns_set
            elif kind in [0x1B, 0x1C]:  # MultinameL, MultinameLA
                _, pos = read_u30(data, pos)  # ns_set
            else:
                # 未知类型，尝试跳过
                pass
        
        # ===== 方法定义 =====
        method_count, pos = read_u30(data, pos)
        result['method_count'] = method_count
        
        for method_idx in range(method_count):
            _, pos = read_u30(data, pos)  # param_count
            _, pos = read_u30(data, pos)  # return_type
            for _ in range(method_idx + 1 if method_idx < 10 else 0):  # 只解析前几个
                _, pos = read_u30(data, pos)  # param_type
            _, pos = read_u30(data, pos)  # name (可选)
            pos += 1  # flags
            
            # 如果有可选参数，跳过
            # 如果有参数名，跳过
            # 简化处理，跳过剩余方法定义
            if method_idx >= 10:
                # 快速跳过剩余方法
                pass
        
        # ===== 元数据 =====
        metadata_count, pos = read_u30(data, pos)
        result['metadata_count'] = metadata_count
        
        # ===== 类定义 =====
        class_count, pos = read_u30(data, pos)
        result['class_count'] = class_count
        
        # 提取类名（从instance_info）
        for _ in range(min(class_count, 20)):  # 只解析前20个类
            _, pos = read_u30(data, pos)  # name index (multiname)
            _, pos = read_u30(data, pos)  # super name
            pos += 1  # flags
            _, pos = read_u30(data, pos)  # protectedNS
            _, pos = read_u30(data, pos)  # interface count
            # 跳过接口列表和iinit
            # 简化处理
        
        # ===== 脚本 =====
        script_count, pos = read_u30(data, pos)
        result['script_count'] = script_count
        
    except Exception as e:
        result['error'] = str(e)
    
    # 从字符串中提取类名和方法名
    keywords = ['class', 'game', 'score', 'level', 'send', 'packet', 'socket',
                'event', 'start', 'end', 'init', 'main', 'update', 'click',
                'button', 'player', 'map', 'item', 'data', 'util', 'manager',
                'controller', 'view', 'model', 'service', 'handler', 'loader']
    
    for s in result['strings']:
        if s and len(s) > 2:
            s_lower = s.lower()
            # 类名通常是大写开头
            if s[0].isupper() and '.' in s:
                result['class_names'].append(s)
            # 方法名通常是小写开头或包含关键词
            elif any(kw in s_lower for kw in keywords):
                result['method_names'].append(s)
    
    return result


def download_swf(url: str, output_path: str = None) -> bytes:
    """下载SWF文件"""
    print(f"下载: {url}")
    
    try:
        req = urllib.request.Request(url, headers={
            'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36'
        })
        with urllib.request.urlopen(req, timeout=30) as response:
            data = response.read()
        
        print(f"下载完成: {len(data)} 字节")
        
        if output_path:
            with open(output_path, 'wb') as f:
                f.write(data)
            print(f"已保存: {output_path}")
        
        return data
    except urllib.error.URLError as e:
        print(f"下载失败: {e}")
        return None


def analyze_swf(data: bytes, name: str = "SWF"):
    """分析SWF文件"""
    print(f"\n{'='*60}")
    print(f"分析: {name}")
    print('='*60)
    
    parser = SWFParser(data)
    try:
        parser.parse()
    except Exception as e:
        print(f"解析错误: {e}")
        return None
    
    # 基本信息
    info = parser.get_info()
    print(f"\n[基本信息]")
    print(f"  SWF版本: {info['version']}")
    print(f"  压缩: {'是' if info['compressed'] else '否'}")
    print(f"  文件大小: {info['file_length']} 字节")
    if info['frame_size']:
        print(f"  帧大小: {info['frame_size']['x_max']:.0f}x{info['frame_size']['y_max']:.0f} 像素")
    print(f"  帧率: {info['frame_rate']:.2f} FPS")
    print(f"  帧数: {info['frame_count']}")
    print(f"  Tag数量: {info['tag_count']}")
    
    # Tag统计
    print(f"\n[Tag统计]")
    tag_counts = {}
    for tag in parser.tags:
        name = tag['name']
        if name not in tag_counts:
            tag_counts[name] = 0
        tag_counts[name] += 1
    
    for name, count in sorted(tag_counts.items(), key=lambda x: -x[1]):
        print(f"  {name}: {count}")
    
    # DoABC分析
    abc_tags = parser.get_abc_tags()
    if abc_tags:
        print(f"\n[AS3字节码 (DoABC)]")
        print(f"  找到 {len(abc_tags)} 个DoABC标签")
        
        for i, tag in enumerate(abc_tags):
            print(f"\n  --- DoABC #{i+1} ({tag['length']} 字节) ---")
            
            # 解析ABC基本信息
            abc_data = tag['data']
            
            # 跳过Flag和Name，找到ABC数据开始
            # DoABC格式: flags(4B) + name(null-terminated) + abc_data
            pos = 0
            flags = struct.unpack('<I', abc_data[pos:pos+4])[0]
            pos += 4
            
            # 查找name结束位置
            name_end = abc_data.find(b'\x00', pos)
            if name_end != -1:
                abc_name = abc_data[pos:name_end].decode('utf-8', errors='ignore')
                pos = name_end + 1
            else:
                abc_name = ""
                pos = 4
            
            print(f"    名称: {abc_name or '(默认)'}")
            print(f"    Flags: {flags}")
            
            # 解析ABC内容
            abc_info = parse_abc_basic(abc_data[pos:])
            print(f"    ABC版本: {abc_info['major_version']}.{abc_info['minor_version']}")
            print(f"    方法数: {abc_info['method_count']}")
            print(f"    类数: {abc_info['class_count']}")
            print(f"    脚本数: {abc_info['script_count']}")
            print(f"    字符串数: {abc_info['string_count']}")
            
            if 'error' in abc_info:
                print(f"    解析警告: {abc_info['error']}")
            
            # 显示类名
            if abc_info['class_names']:
                print(f"\n    [类名] (前20个)")
                for name in abc_info['class_names'][:20]:
                    print(f"      - {name}")
            
            # 显示方法名/关键字符串
            if abc_info['method_names']:
                print(f"\n    [关键标识符] (前30个)")
                for name in abc_info['method_names'][:30]:
                    print(f"      - {name}")
            
            # 显示所有有意义的字符串
            meaningful_strings = [s for s in abc_info['strings'] 
                                  if s and len(s) > 3 and not s.startswith('_') 
                                  and not s.startswith('http') and '/' not in s]
            if meaningful_strings:
                print(f"\n    [字符串常量] (前40个)")
                for s in meaningful_strings[:40]:
                    print(f"      - {s}")
    
    # SymbolClass
    symbols = parser.get_symbol_classes()
    if symbols:
        print(f"\n[SymbolClass] ({len(symbols)} 个)")
        for sym in symbols[:20]:
            print(f"  ID {sym['id']}: {sym['name']}")
        if len(symbols) > 20:
            print(f"  ... 还有 {len(symbols) - 20} 个")
    
    return parser


def decompile_swf(swf_path: str, output_dir: str = None, export_as3: bool = True, ffdec_override: str = None):
    """使用FFDec反编译SWF文件"""
    ffdec_cmd = find_ffdec(ffdec_override)
    
    if not ffdec_cmd:
        print("错误: 未找到FFDec")
        if ffdec_override:
            print(f"  指定的 --ffdec-path 无法使用: {os.path.abspath(os.path.expanduser(ffdec_override))}")
        print("\n请安装 JPEXS Free Flash Decompiler:")
        print("  下载: https://github.com/jindrapetrik/jpexs-decompiler/releases")
        print("  安装后确保 ffdec-cli.exe 在以下位置之一:")
        for p in FFDEC_PATHS:
            print(f"    - {p}")
        print("  或使用 --ffdec-path 指向 ffdec-cli.exe / ffdec.exe / ffdec-cli.jar / ffdec.jar")
        return False
    
    swf_path = os.path.abspath(swf_path)
    if not os.path.exists(swf_path):
        print(f"错误: SWF文件不存在: {swf_path}")
        return False
    
    if output_dir is None:
        # 默认输出到 swf_cache/decompiled/文件名/
        output_dir = os.path.join(os.path.dirname(swf_path), "decompiled", 
                                   os.path.splitext(os.path.basename(swf_path))[0])
    
    output_dir = os.path.abspath(output_dir)
    os.makedirs(output_dir, exist_ok=True)
    
    print(f"\n{'='*60}")
    print(f"FFDec 反编译: {os.path.basename(swf_path)}")
    print('='*60)
    print(f"  当前目录: {os.getcwd()}")
    print(f"  FFDec: {subprocess.list2cmdline(ffdec_cmd)}")
    print(f"  输入: {swf_path}")
    print(f"  输出: {output_dir}")
    
    # 构建命令
    # 导出AS3源码: -export script
    # 导出所有资源: -export all
    cmd_parts = list(ffdec_cmd)
    
    if export_as3:
        # 导出ActionScript源码
        cmd_parts.extend(['-export', 'script', output_dir, swf_path])
    else:
        # 导出所有资源
        cmd_parts.extend(['-export', 'all', output_dir, swf_path])
    
    print(f"\n执行命令: {subprocess.list2cmdline(cmd_parts)}")
    print("-" * 40)
    
    try:
        result = subprocess.run(
            cmd_parts,
            capture_output=True,
            text=True,
            timeout=300  # 5分钟超时
        )
        
        if result.stdout:
            print(result.stdout)
        if result.stderr:
            print(result.stderr)
        
        if result.returncode == 0:
            print("-" * 40)
            print(f"反编译完成! 输出目录: {output_dir}")
            
            # 列出生成的文件
            if os.path.exists(output_dir):
                as_dir = os.path.join(output_dir, 'scripts')
                if os.path.exists(as_dir):
                    as_files = []
                    for root, dirs, files in os.walk(as_dir):
                        for f in files:
                            if f.endswith('.as'):
                                rel_path = os.path.relpath(os.path.join(root, f), as_dir)
                                as_files.append(rel_path)
                    
                    if as_files:
                        print(f"\n生成的AS3文件 ({len(as_files)} 个):")
                        for f in as_files[:30]:
                            print(f"  - {f}")
                        if len(as_files) > 30:
                            print(f"  ... 还有 {len(as_files) - 30} 个文件")
            
            return True
        else:
            print(f"反编译失败，返回码: {result.returncode}")
            return False
            
    except subprocess.TimeoutExpired:
        print("错误: 反编译超时（超过5分钟）")
        return False
    except Exception as e:
        print(f"错误: {e}")
        return False


def quick_decompile(swf_path: str, output_dir: str = None):
    """快速反编译，只提取关键AS3类"""
    return decompile_swf(swf_path, output_dir, export_as3=True)


def main():
    parser = argparse.ArgumentParser(description='卡布西游SWF分析工具')
    parser.add_argument('--game', type=int, help='下载并分析指定ID的小游戏')
    parser.add_argument('--main', action='store_true', help='下载并分析主程序')
    parser.add_argument('--batch', type=str, help='批量下载小游戏 (如: 1-20)')
    parser.add_argument('--info', type=str, help='分析本地SWF文件')
    parser.add_argument('--output', type=str, default='swf_cache', help='输出目录')
    parser.add_argument('--list-games', action='store_true', help='列出已知小游戏ID')
    parser.add_argument('--decompile', type=str, metavar='SWF', help='使用FFDec反编译SWF文件')
    parser.add_argument('--decompile-game', type=int, metavar='ID', help='下载并反编译指定小游戏')
    parser.add_argument('--full-decompile', action='store_true', help='反编译所有资源（不仅是AS3）')
    parser.add_argument('--ffdec-path', type=str, help='显式指定 FFDec 路径，可传 exe / jar / 安装目录')
    
    args = parser.parse_args()
    
    # 创建输出目录
    output_dir = Path(args.output)
    output_dir.mkdir(exist_ok=True)
    
    if args.list_games:
        print("已知小游戏ID:")
        print("  1  - 推箱子")
        print("  2  - 连连看")
        print("  3  - 排行榜游戏")
        print("  4  - 钓鱼")
        print("  6  - 特殊游戏")
        print("  12 - 深度挖宝 (日常活动)")
        print("  >1000 - 任务小游戏")
        return
    
    if args.decompile:
        # 反编译本地SWF文件
        decompile_swf(args.decompile, export_as3=not args.full_decompile, ffdec_override=args.ffdec_path)
        return
    
    if args.decompile_game:
        # 下载并反编译指定小游戏
        url = BASE_URL + LITTLEGAME_PATH.format(id=args.decompile_game)
        filename = f"game{args.decompile_game}.swf"
        output_path = output_dir / filename
        
        data = download_swf(url, str(output_path))
        if data:
            analyze_swf(data, filename)
            print()
            decompile_swf(str(output_path), export_as3=not args.full_decompile, ffdec_override=args.ffdec_path)
        return
    
    if args.info:
        # 分析本地文件
        info_path = os.path.abspath(args.info)
        if not os.path.exists(info_path):
            print(f"错误: 文件不存在: {info_path}")
            print(f"  当前目录: {os.getcwd()}")
            return
        with open(info_path, 'rb') as f:
            data = f.read()
        analyze_swf(data, info_path)
        return
    
    if args.main:
        # 下载主程序
        url = BASE_URL + MAIN_GAME
        output_path = output_dir / MAIN_GAME
        data = download_swf(url, str(output_path))
        if data:
            analyze_swf(data, MAIN_GAME)
        return
    
    if args.game:
        # 下载指定小游戏
        url = BASE_URL + LITTLEGAME_PATH.format(id=args.game)
        filename = f"game{args.game}.swf"
        output_path = output_dir / filename
        data = download_swf(url, str(output_path))
        if data:
            analyze_swf(data, filename)
        return
    
    if args.batch:
        # 批量下载
        try:
            start, end = map(int, args.batch.split('-'))
        except:
            print("无效的范围格式，使用: 1-20")
            return
        
        print(f"批量下载小游戏 {start}-{end}")
        for game_id in range(start, end + 1):
            url = BASE_URL + LITTLEGAME_PATH.format(id=game_id)
            filename = f"game{game_id}.swf"
            output_path = output_dir / filename
            
            data = download_swf(url, str(output_path))
            if data:
                analyze_swf(data, filename)
            print()
    
    # 没有参数时显示帮助
    if len(sys.argv) == 1:
        parser.print_help()


if __name__ == '__main__':
    main()
