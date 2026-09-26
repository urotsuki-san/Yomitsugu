"""隔離したプロファイルで文脈付きの変換精度を測定する。"""
import argparse
import json
import msvcrt
import os
import re
from pathlib import Path
import statistics
import struct
import subprocess
import tempfile
import threading
import time


def romanize(katakana):
    table = Path(__file__).resolve().parents[2] / "native/src/romaji_table.inc"
    reverse = {}
    for key, kana, pending in re.findall(r'\{u8"([^"]*)", u8"([^"]*)", u8"([^"]*)"\}', table.read_text(encoding="utf-8")):
        if not pending and "\\" not in key and kana and (kana not in reverse or len(key) < len(reverse[kana])):
            reverse[kana] = key
    reverse["ん"] = "n'"
    reverse["っ"] = "xtu"
    text = "".join(chr(ord(c)-0x60) if "ァ" <= c <= "ヶ" else c for c in katakana)
    result = ""
    while text:
        length = next((n for n in range(min(3,len(text)), 0, -1) if text[:n] in reverse), 0)
        if length:
            result += reverse[text[:length]]
            text = text[length:]
        else:
            result += text[0]
            text = text[1:]
    return result


class Engine:
    def __init__(self, host):
        self.profile = tempfile.TemporaryDirectory(prefix="yomitsugu-context-")
        self.errors = tempfile.TemporaryFile()
        child_read, self.write = os.pipe()
        self.read, child_write = os.pipe()
        handles = [msvcrt.get_osfhandle(child_read), msvcrt.get_osfhandle(child_write)]
        for handle in handles:
            os.set_handle_inheritable(handle, True)
        startup = subprocess.STARTUPINFO()
        startup.lpAttributeList = {"handle_list": handles}
        try:
            self.process = subprocess.Popen(
                [str(Path(host).resolve()), "--pipe", *map(str, handles), "--profile", self.profile.name],
                startupinfo=startup, close_fds=True, creationflags=subprocess.CREATE_NO_WINDOW,
                stdout=self.errors, stderr=self.errors,
            )
        finally:
            os.close(child_read)
            os.close(child_write)
        self.watchdog = threading.Timer(600, self.process.kill)
        self.watchdog.start()

    def receive(self, count):
        result = b""
        while len(result) < count:
            part = os.read(self.read, count - len(result))
            if not part:
                raise RuntimeError("Engine closed its pipe")
            result += part
        return result

    def decode(self, raw, left="", right="", phase="end_of_phrase"):
        data = json.dumps(dict(version=1, raw=raw, left=left, right=right, phase=phase, field="prose", limit=16)).encode()
        frame = struct.pack("<I", len(data)) + data
        assert os.write(self.write, frame) == len(frame)
        size = struct.unpack("<I", self.receive(4))[0]
        assert 0 < size <= 1024 * 1024
        return [item["text"] for item in json.loads(self.receive(size))]

    def close(self):
        os.close(self.write)
        os.close(self.read)
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self.watchdog.cancel()
        self.errors.close()
        self.profile.cleanup()


def evaluate(host, items, output):
    results = []
    engine = Engine(host)
    try:
        for item in items:
            raw = item.get("raw") or romanize(item["input"])
            started = time.perf_counter()
            candidates = engine.decode(raw, item.get("context_text", ""), item.get("right_context", ""))
            elapsed = (time.perf_counter()-started)*1000
            expected = item["expected_output"]
            rank = next((i+1 for i, c in enumerate(candidates) if c in expected), None)
            forbidden = [word for word in item.get("forbidden", []) if word in candidates]
            results.append(dict(index=item["index"], raw=raw, rank=rank, candidates=candidates, ms=elapsed,
                                passed=rank is not None and rank <= item.get("max_rank",16) and not forbidden,
                                forbidden=forbidden,
                                context=bool(item.get("context_text") or item.get("right_context"))))
            if len(results) % 25 == 0:
                print(f"Measured {len(results)}/{len(items)}", flush=True)
    finally:
        engine.close()
    summary = {}
    for name, subset in [("all", results), ("context", [r for r in results if r["context"]]),
                         ("no_context", [r for r in results if not r["context"]])]:
        if not subset:
            continue
        times = sorted(r["ms"] for r in subset)
        summary[name] = dict(total=len(subset), top1=sum(r["rank"] == 1 for r in subset),
                             top16=sum(r["rank"] is not None for r in subset),
                             median_ms=statistics.median(times), p95_ms=times[min(len(times)-1, int(len(times)*.95))])
    Path(output).write_text(json.dumps(dict(summary=summary, results=results), ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False), flush=True)
    return results


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("host")
    parser.add_argument("items")
    parser.add_argument("output")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--baseline", type=Path)
    args = parser.parse_args()
    results = evaluate(args.host, json.loads(Path(args.items).read_text(encoding="utf-8")), args.output)
    failures = [r["index"] for r in results if not r["passed"]] if args.check else []
    if args.baseline:
        baseline = {r["index"]: r for r in json.loads(args.baseline.read_text(encoding="utf-8"))["results"]}
        for result in results:
            previous = baseline[result["index"]]["rank"]
            if previous == 1 and result["rank"] != 1:
                failures.append(result["index"])
            elif previous is not None and previous <= 5 and (result["rank"] is None or result["rank"] > 5):
                failures.append(result["index"])
    if failures:
        raise SystemExit("Conversion regression: " + ", ".join(map(str, failures)))
