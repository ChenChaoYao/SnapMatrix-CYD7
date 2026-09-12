#!/usr/bin/env python3
import sys
import os
import hashlib
import time

try:
    import requests
except ImportError:
    print("[OTA] 安裝 requests 函式庫以進行 OTA 上傳...")
    os.system(f"{sys.executable} -m pip install requests")
    import requests

def upload_ota(bin_path, target_ip="192.168.3.221", port=80):
    if not os.path.isfile(bin_path):
        print(f"[OTA 錯誤] 找不到韌體檔案: {bin_path}")
        sys.exit(1)

    file_size = os.path.getsize(bin_path)
    with open(bin_path, "rb") as f:
        file_bytes = f.read()
    
    file_md5 = hashlib.md5(file_bytes).hexdigest()
    print(f"[OTA] 目標裝置: http://{target_ip}:{port}")
    print(f"[OTA] 韌體檔案: {bin_path} ({file_size / 1024 / 1024:.2f} MB)")
    print(f"[OTA] 檔案 MD5: {file_md5}")

    # Step 1: 呼叫 /ota/start
    start_url = f"http://{target_ip}:{port}/ota/start?mode=fr&hash={file_md5}"
    print(f"[OTA] 正在通知開發板準備進入 OTA 模式...")
    try:
        r_start = requests.get(start_url, timeout=10)
        if r_start.status_code != 200:
            print(f"[OTA 錯誤] /ota/start 回應失敗: HTTP {r_start.status_code} - {r_start.text}")
            sys.exit(1)
        print(f"[OTA] 開發板已就緒，開始傳輸韌體...")
    except Exception as e:
        print(f"[OTA 錯誤] 連線至開發板失敗: {e}")
        sys.exit(1)

    # Step 2: 呼叫 /ota/upload (multipart/form-data)
    upload_url = f"http://{target_ip}:{port}/ota/upload"
    
    # 建立自訂串流上傳以顯示即時進度條
    class ProgressReader:
        def __init__(self, data):
            self._data = data
            self._len = len(data)
            self._read = 0
            self._last_print = 0

        def read(self, size=-1):
            if size < 0 or size > (self._len - self._read):
                chunk = self._data[self._read:]
                self._read = self._len
            else:
                chunk = self._data[self._read : self._read + size]
                self._read += len(chunk)
            
            # 每 100ms 更新一次進度顯示
            now = time.time()
            if now - self._last_print > 0.15 or self._read == self._len:
                self._last_print = now
                pct = (self._read / self._len) * 100
                bar = "=" * int(pct // 4) + ">"
                print(f"\r[OTA 傳輸中] [{bar:<26}] {pct:5.1f}% ({self._read / 1024 / 1024:.2f} / {self._len / 1024 / 1024:.2f} MB)", end="", flush=True)
            return chunk

        def __len__(self):
            return self._len

    try:
        files = {
            'file': ('firmware.bin', ProgressReader(file_bytes), 'application/octet-stream')
        }
        start_time = time.time()
        r_upload = requests.post(upload_url, files=files, timeout=60)
        print() # 換行
        
        if r_upload.status_code == 200:
            elapsed = time.time() - start_time
            speed = (file_size / 1024) / elapsed if elapsed > 0 else 0
            print(f"==================================================")
            print(f"🎉 [OTA 成功] 韌體傳輸完成！耗時 {elapsed:.1f} 秒 (平均速度 {speed:.1f} KB/s)")
            print(f"🔄 開發板正在寫入 Flash 並自動重新啟動...")
            print(f"==================================================")
        else:
            print(f"[OTA 失敗] /ota/upload 回傳錯誤: HTTP {r_upload.status_code} - {r_upload.text}")
            sys.exit(1)
    except Exception as e:
        print(f"\n[OTA 異常] 傳輸中斷: {e}")
        sys.exit(1)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("用法: python3 ota_upload.py <firmware.bin> [target_ip]")
        sys.exit(1)
    bin_file = sys.argv[1]
    target_ip = sys.argv[2] if len(sys.argv) > 2 else "192.168.3.221"
    upload_ota(bin_file, target_ip)
