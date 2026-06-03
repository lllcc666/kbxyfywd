#!/usr/bin/env python3
"""下载活动SWF文件"""
import urllib.request
import os

BASE_URL = "http://enter.wanwan4399.com/bin-debug/"

def download_swf(url_path, output_name=None):
    """下载SWF文件"""
    if url_path == '0' or not url_path:
        print(f"跳过: {url_path} (无效URL)")
        return False
    
    url = BASE_URL + url_path
    if not output_name:
        output_name = os.path.basename(url_path)
    
    output_path = f'D:/AItrace/CE/.trae/kbwebui/swf_cache/{output_name}'
    
    print(f'下载: {url}')
    print(f'保存: {output_path}')
    
    try:
        headers = {
            'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36'
        }
        req = urllib.request.Request(url, headers=headers)
        
        with urllib.request.urlopen(req, timeout=30) as resp:
            data = resp.read()
            
            with open(output_path, 'wb') as f:
                f.write(data)
            
            print(f'OK 成功 ({len(data)} 字节)')
            return True
            
    except Exception as e:
        print(f'FAIL 失败: {e}')
        return False

if __name__ == '__main__':
    # 下载磐石御天火活动SWF
    swf_url = "assets/activity/201403/Active20140320ET/ETMain.swf"
    download_swf(swf_url, "PanshiYutianhuo.swf")
