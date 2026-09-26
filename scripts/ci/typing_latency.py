"""一文字ずつの入力と確定時の変換を、専用プロファイルで測る。"""
import argparse
import json
from pathlib import Path
import statistics
import time

from context_quality import Engine


def measure(host, output, measure_only=False):
    engine = Engine(host)
    rows = []
    try:
        engine.decode("a", phase="typing")
        for text in ("nakagakara", "karanoyouki", "aninsuto-ru", "ri-domi-wokousinnsitekudasaiGithubde"):
            for size in range(1, len(text) + 1):
                start = time.perf_counter()
                candidates = engine.decode(text[:size], phase="typing")
                rows.append(dict(raw=text[:size], ms=(time.perf_counter()-start)*1000,
                                 first=candidates[0] if candidates else ""))
        # 同じ読みでも文脈と入力段階が違えば、別の結果を取得する。
        first = engine.decode("nakagakara")
        engine.decode("nakagakara", phase="typing")
        repeated = engine.decode("nakagakara")
        paper = engine.decode("kiru", left="この紙を")
        clothes = engine.decode("kiru", left="新しい服を")
        repeated_paper = engine.decode("kiru", left="この紙を")
        if not measure_only:
            assert repeated == first and first[0] == "中が空"
            assert paper[0] == "切る" and clothes[0] == "着る"
            assert repeated_paper == paper
    finally:
        engine.close()
    times = sorted(row["ms"] for row in rows)
    summary = dict(keys=len(times), median_ms=statistics.median(times),
                   p95_ms=times[int(len(times)*.95)], maximum_ms=max(times))
    Path(output).write_text(json.dumps(dict(summary=summary, results=rows), ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(summary), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("host")
    parser.add_argument("output")
    parser.add_argument("--measure-only", action="store_true")
    args = parser.parse_args()
    measure(args.host, args.output, args.measure_only)
