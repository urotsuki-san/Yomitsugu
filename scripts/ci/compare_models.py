"""Compare a Yomitsugu setting on the same ordered conversion cases (Windows)."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
from pathlib import Path
import statistics
import time

from context_quality import Engine, romanize
from engine_stability import Memory


def memory(process):
    counters = Memory()
    counters.cb = ctypes.sizeof(counters)
    get_memory = ctypes.WinDLL("psapi", use_last_error=True).GetProcessMemoryInfo
    get_memory.argtypes = [wintypes.HANDLE, ctypes.POINTER(Memory), wintypes.DWORD]
    if not get_memory(int(process._handle), ctypes.byref(counters), counters.cb):
        raise ctypes.WinError(ctypes.get_last_error())
    return dict(private_MiB=counters.private / 2**20, ws_MiB=counters.working / 2**20)


def run(args):
    engine = Engine(args.host, args.model)
    results = {}
    try:
        start = time.perf_counter()
        engine.decode("nihongo")
        results["first_ms"] = (time.perf_counter() - start) * 1000
        for path in args.cases:
            source = path.read_bytes()
            rows = []
            for item in json.loads(source):
                raw = item.get("raw") or romanize(item["input"])
                start = time.perf_counter()
                candidates = engine.decode(raw, item.get("context_text", ""), item.get("right_context", ""))
                elapsed = (time.perf_counter() - start) * 1000
                rank = next((n for n, value in enumerate(candidates, 1) if value in item["expected_output"]), None)
                rows.append(dict(index=item["index"], rank=rank, ms=elapsed))
            times = sorted(row["ms"] for row in rows)
            results[path.name] = dict(sha256=hashlib.sha256(source).hexdigest(), n=len(rows),
                                      top1=sum(row["rank"] == 1 for row in rows),
                                      top16=sum(row["rank"] is not None for row in rows),
                                      median_ms=statistics.median(times), p95_ms=times[int(len(times) * .95)],
                                      memory=memory(engine.process), rows=rows)
    finally:
        engine.close()
    args.output.write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host", type=Path)
    parser.add_argument("model", choices=("standard", "compact", "extended", "compact_extended", "dictionary"))
    parser.add_argument("output", type=Path)
    parser.add_argument("cases", type=Path, nargs="+")
    run(parser.parse_args())
