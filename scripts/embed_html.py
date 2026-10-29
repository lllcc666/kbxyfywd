#!/usr/bin/env python3

import sys
import os
import re

def compress_html(html_content):
    """
    压缩HTML内容：仅移除注释，保持空白不变
    注意：不压缩空白，因为可能破坏JavaScript代码中的字符串
    """
    original_len = len(html_content)
    
    # 1. 移除HTML注释（保留条件注释）
    html_content = re.sub(r'(?<!\[)<!--(?!\[).*?-->(?!\])', '', html_content, flags=re.DOTALL)
    
    # 2. 移除CSS注释
    def remove_css_comments(match):
        css = match.group(0)
        css = re.sub(r'/\*.*?\*/', '', css, flags=re.DOTALL)
        return css
    html_content = re.sub(r'<style[^>]*>.*?</style>', remove_css_comments, html_content, flags=re.DOTALL | re.IGNORECASE)
    
    # 不再压缩空白，因为这可能破坏JavaScript代码
    
    compressed_len = len(html_content)
    print(f"HTML压缩: {original_len} -> {compressed_len} 字节 (节省 {original_len - compressed_len} 字节, {(original_len - compressed_len) * 100 / original_len:.1f}%)")
    
    return html_content

def embed_html_to_header(html_file_path, header_file_path):
    """
    将HTML文件转换为C++头文件，包含一个字符数组。
    """
    try:
        with open(html_file_path, 'r', encoding='utf-8') as f:
            html_content = f.read()

        # 压缩HTML内容
        html_content = compress_html(html_content)
        
        max_length = 1000
        chunks = []
        for i in range(0, len(html_content), max_length):
            chunks.append(html_content[i:i+max_length])
        
        header_content = f"""#pragma once

// 嵌入的HTML内容 - 多个原始字符串字面量拼接
const char* g_html_content = \
"""
        
        for i, chunk in enumerate(chunks):
            if i == len(chunks) - 1:
                header_content += f"R\"HTML({chunk})HTML\"\n";
            else:
                header_content += f"R\"HTML({chunk})HTML\" \\\n";
        
        header_content += ";\n"
        
        with open(header_file_path, 'w', encoding='utf-8') as f:
            f.write(header_content)
        
        print(f"成功生成头文件: {header_file_path}")
        return True
    except Exception as e:
        print(f"生成头文件失败: {str(e)}")
        return False

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("用法: python3 embed_html.py <html_file_path> <header_file_path>")
        sys.exit(1)
    
    html_file_path = sys.argv[1]
    header_file_path = sys.argv[2]
    
    if not os.path.exists(html_file_path):
        print(f"HTML文件不存在: {html_file_path}")
        sys.exit(1)
    
    if embed_html_to_header(html_file_path, header_file_path):
        sys.exit(0)
    else:
        sys.exit(1)
