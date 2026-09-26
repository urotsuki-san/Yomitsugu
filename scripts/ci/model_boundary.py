"""推論上限をまたぐ長い読みで、入力保持と次の変換への復帰を確認する。"""
import argparse
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("probe", type=Path)
args = parser.parse_args()
lengths = (210, 256, 260, 512)
requests = ["a" * length for length in lengths] + ["nihongo"]
result = subprocess.run(
    [str(args.probe.resolve()), "--runtime-json", "1", "確定済みの文章" * 10],
    input="\n".join(requests) + "\n", encoding="utf-8", capture_output=True, timeout=120,
    creationflags=subprocess.CREATE_NO_WINDOW,
)
if result.returncode:
    raise SystemExit(f"Model boundary process exited: {result.returncode}\n{result.stderr[-2000:]}")
lines = [line.split("\t", 2) for line in result.stdout.splitlines() if line.startswith("RUNTIME\t")]
assert len(lines) == len(requests), "Incomplete boundary responses"
for raw, row in zip(requests, lines):
    assert row[1] == raw
    candidates = json.loads(row[2])
    if raw == "nihongo":
        assert any(item["text"] == "日本語" for item in candidates), "Engine did not recover"
        print("PASS normal conversion after model capacity limit", flush=True)
    else:
        assert any(item["text"] == "あ" * len(raw) and item["correspondingCount"] == len(raw)
                   for item in candidates), "Long reading was truncated"
        print(f"PASS model boundary preserves {len(raw)} kana", flush=True)
