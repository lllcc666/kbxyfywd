#!/usr/bin/env python3
"""下载并解压 taskprop 任务节点文件（皇城危机等剧情任务的内容来源）

关键点：远程文件名没有 .xml 后缀。AS3 侧 PropertyPool.getTaskProps 用
    assets/taskprop/<dialogId / 1000 >> 0> + "0"
拼出组号，再直接请求该名字；带 .xml 一律 404。

组号与任务的对应关系（组号 = 任务号，taskinfo.xml 里的 task id）：
    2001000  皇城危机（8 个子任务：2001001~2001008）
    2002000  长安城支线
    2005000  五指山顶支线
    4006000  八卦灵盘
    1002000  资源探索
    1003000  捕捉的诀窍

用法：
    python3 scripts/fetch_taskprop.py               # 下载默认组
    python3 scripts/fetch_taskprop.py 2001000 2002000
"""
import sys
import urllib.request
import zlib
from pathlib import Path

BASE_URL = "http://enter.wanwan4399.com/bin-debug/assets/taskprop/"

# 默认下载的任务组；可按需在命令行覆盖
DEFAULT_GROUPS = [
    "2001000",
    "2002000",
    "2005000",
    "4006000",
    "1002000",
    "1003000",
]


def try_download(group):
    """下载单个任务组；远程不带扩展名"""
    url = BASE_URL + group
    try:
        print(f"尝试: {url}")
        headers = {
            "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                          "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120 Safari/537.36"
        }
        req = urllib.request.Request(url, headers=headers)
        with urllib.request.urlopen(req, timeout=20) as resp:
            return resp.read()
    except Exception as e:
        print(f"  失败: {e}")
        return None


def decompress_if_needed(data):
    """服务端下发的是 zlib 压缩数据；解压失败则原样返回"""
    try:
        decompressed = zlib.decompress(data)
        print(f"  已解压 (原始: {len(data)} 字节, 解压后: {len(decompressed)} 字节)")
        return decompressed
    except Exception:
        return data


def main():
    output_dir = Path(__file__).resolve().parents[1] / "swf_cache"
    output_dir.mkdir(exist_ok=True)

    groups = sys.argv[1:] or DEFAULT_GROUPS

    print("=" * 60)
    print("下载 taskprop 任务节点文件")
    print("=" * 60)

    for group in groups:
        raw = try_download(group)
        if not raw:
            continue

        # 保留原始压缩数据，便于和客户端行为对照
        (output_dir / f"taskprop_{group}.bin").write_bytes(raw)

        xml = decompress_if_needed(raw)
        (output_dir / f"taskprop_{group}.xml").write_bytes(xml)
        print(f"OK 已保存: swf_cache/taskprop_{group}.bin / .xml")
        print()

    print("=" * 60)
    print("完成")
    print("=" * 60)


if __name__ == "__main__":
    main()
