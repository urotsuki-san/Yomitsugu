"""インストール先の辞書とモデルだけで変換できるかを確認する。"""
import json
import msvcrt
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import threading


def verify(host: Path) -> None:
    cases = [
        ("insuto-ru", "インストール"), ("innsuto-ru", "インストール"),
        ("aninsuto-ru", "アンインストール"), ("anninsuto-ru", "アンインストール"),
        ("softwarewokoushinsuru", "softwareを更新する"),
        ("ri-domi-wokousinnsitekudasaiGithubde", "READMEを更新してくださいGithubで"),
        ("samukunaltutekimasitane", "寒くなってきましたね"),
        ("sannkai", "散開"), ("yajirushi", "→"), ("ltu", "っ"), ("a", "あ"),
        ("nakagakara", "中が空"), ("konitiha", "こんにちは"),
    ]
    child_read, parent_write = os.pipe()
    parent_read, child_write = os.pipe()
    handles = [msvcrt.get_osfhandle(child_read), msvcrt.get_osfhandle(child_write)]
    for handle in handles:
        os.set_handle_inheritable(handle, True)
    startup = subprocess.STARTUPINFO()
    startup.lpAttributeList = {"handle_list": handles}
    with tempfile.TemporaryDirectory(prefix="yomitsugu-installed-") as profile, tempfile.TemporaryFile() as errors:
        process = subprocess.Popen(
            [str(host.resolve()), "--pipe", *map(str, handles), "--profile", profile],
            startupinfo=startup, close_fds=True, creationflags=subprocess.CREATE_NO_WINDOW, stderr=errors,
        )
        os.close(child_read)
        os.close(child_write)
        watchdog = threading.Timer(90, process.kill)
        watchdog.start()

        def receive(size):
            data = b""
            while len(data) < size:
                chunk = os.read(parent_read, size - len(data))
                if not chunk:
                    raise RuntimeError("Installed engine closed its pipe")
                data += chunk
            return data

        try:
            for raw, expected in cases:
                request = json.dumps(dict(version=1, raw=raw, field="prose", limit=8)).encode()
                os.write(parent_write, struct.pack("<I", len(request)) + request)
                size = struct.unpack("<I", receive(4))[0]
                if not 0 < size < 1024 * 1024:
                    raise RuntimeError("Invalid engine response size")
                response = json.loads(receive(size))
                actual = response[0]["text"] if response else ""
                if actual != expected:
                    raise AssertionError(f"{raw}: {ascii(actual)} != {ascii(expected)}")
                print(f"PASS {raw}", flush=True)
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
        if process.returncode:
            raise RuntimeError(f"Installed engine exited with {process.returncode}")
    print(f"Installed conversion: {len(cases)}/{len(cases)}")


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    verify(Path(sys.argv[1]))
