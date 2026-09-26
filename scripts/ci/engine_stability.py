"""専用プロファイルで辞書の再読込と変換プロセスのメモリ推移を測る。"""
import ctypes
from ctypes import wintypes
import json
import msvcrt
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import threading


class Memory(ctypes.Structure):
    _fields_ = [("cb", wintypes.DWORD), ("faults", wintypes.DWORD)] + [
        (name, ctypes.c_size_t) for name in (
            "peak_working", "working", "peak_paged", "paged", "peak_nonpaged",
            "nonpaged", "pagefile", "peak_pagefile", "private",
        )
    ]


def verify(host):
    get_memory = ctypes.WinDLL("psapi", use_last_error=True).GetProcessMemoryInfo
    get_memory.argtypes = [wintypes.HANDLE, ctypes.POINTER(Memory), wintypes.DWORD]
    get_memory.restype = wintypes.BOOL
    child_read, parent_write = os.pipe()
    parent_read, child_write = os.pipe()
    handles = [msvcrt.get_osfhandle(child_read), msvcrt.get_osfhandle(child_write)]
    for handle in handles:
        os.set_handle_inheritable(handle, True)
    startup = subprocess.STARTUPINFO()
    startup.lpAttributeList = {"handle_list": handles}
    with tempfile.TemporaryDirectory(prefix="yomitsugu-stability-") as profile, tempfile.TemporaryFile() as errors:
        process = subprocess.Popen(
            [str(host.resolve()), "--pipe", *map(str, handles), "--profile", profile],
            startupinfo=startup, close_fds=True, creationflags=subprocess.CREATE_NO_WINDOW, stderr=errors,
        )
        os.close(child_read)
        os.close(child_write)
        watchdog = threading.Timer(240, process.kill)
        watchdog.start()

        def receive(size):
            result = b""
            while len(result) < size:
                chunk = os.read(parent_read, size - len(result))
                if not chunk:
                    raise RuntimeError("Engine closed its pipe")
                result += chunk
            return result

        def decode(raw):
            data = json.dumps(dict(version=1, raw=raw, field="prose", limit=8)).encode()
            os.write(parent_write, struct.pack("<I", len(data)) + data)
            size = struct.unpack("<I", receive(4))[0]
            assert 0 < size <= 1024 * 1024
            result = json.loads(receive(size))
            assert result and all(item["text"] for item in result)
            return result[0]["text"]

        try:
            dictionary = Path(profile) / "user_dictionary.tsv"
            temporary = Path(profile) / "next.tsv"
            stamp = 1_700_000_000_000_000_000
            for i in range(12):
                chosen = "検査甲" if i % 2 else "検査乙"
                temporary.write_text(f"こうしんしけん\t{chosen}\t名詞\n", encoding="utf-8")
                os.utime(temporary, ns=(stamp, stamp))
                os.replace(temporary, dictionary)
                assert decode("koushinshiken") == chosen
            print("PASS 12 dictionary replacements with equal size and timestamp", flush=True)
            temporary.write_bytes(b"\xff\xfe invalid")
            os.replace(temporary, dictionary)
            assert decode("koushinshiken") == chosen
            print("PASS invalid replacement preserves loaded dictionary", flush=True)
            dictionary.unlink()
            assert decode("koushinshiken") not in ("検査甲", "検査乙")
            print("PASS deleted user dictionary stops affecting conversion", flush=True)
            samples = []
            phrases = [a + b + c for a in ("kyouha", "ashitaha", "kinouha", "shuumatsuha")
                       for b in ("tokyouni", "kyoutoni", "oosakani", "gakkouni", "kaishani")
                       for c in ("ikimasu", "ikimashita", "ikitai")]
            for batch in range(4):
                for phrase in phrases:
                    decode(phrase)
                memory = Memory()
                memory.cb = ctypes.sizeof(memory)
                assert get_memory(int(process._handle), ctypes.byref(memory), memory.cb)
                samples.append(memory.private)
                print(f"MEMORY conversions={(batch + 1) * len(phrases)} private_bytes={memory.private}", flush=True)
            # 初回の辞書・モデル・キャッシュ確保後に、同じ入力群を繰り返す。
            assert max(samples[1:]) - samples[0] < 32 * 1024 * 1024, samples
            print("PASS 240 real-engine conversions; post-warmup growth below 32 MiB", flush=True)
        finally:
            os.close(parent_write)
            os.close(parent_read)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            watchdog.cancel()
            if process.returncode:
                errors.seek(0)
                print(errors.read().decode("utf-8", errors="replace")[-2000:])
        assert process.returncode == 0, process.returncode


if __name__ == "__main__":
    verify(Path(sys.argv[1]))
