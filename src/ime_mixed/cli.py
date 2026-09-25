from __future__ import annotations

import argparse
import json
import sys
from typing import Any, List, Optional

from .evaluate import decode_text, evaluate_seed_file, measure_latency


def _cmd_decode(args: argparse.Namespace) -> int:
    results = decode_text(
        args.text,
        left=args.left or "",
        right=args.right or "",
        field=args.field,
        phase=args.phase,
        punctuation_completion=args.punct,
        candidate_limit=args.topk,
    )
    if args.json:
        print(json.dumps(results, ensure_ascii=False, indent=2))
    else:
        for i, c in enumerate(results, 1):
            feats = c["score_features"]
            print(f"{i}. {c['output_text']!r}  total={feats['total']} raw={c['is_raw']}")
            if args.verbose:
                print(f"   feats={feats}")
                for s in c["spans"]:
                    print(f"   span {s['kind']} raw{s['raw_range']} -> {s['output_text']!r}")
                for e in c["edit_operations"]:
                    print(f"   edit {e['op']} {e['detail']} cost={e['cost']}")
    return 0


def _cmd_eval(args: argparse.Namespace) -> int:
    report = evaluate_seed_file(args.path, topk=args.topk)
    summary = report["summary"]
    print(
        f"passed {summary['passed']}/{summary['total']} "
        f"(text {summary['text_passed']}/{summary['text_total']}, "
        f"seq {summary['sequence_passed']}/{summary['sequence_total']})"
    )
    for name, g in summary["eval_groups"].items():
        print(f"  [{name}] {g['passed']}/{g['n']} rate={g['rate']}")
    if "latency_ms" in summary:
        lat = summary["latency_ms"]
        print(f"  latency ms p50={lat['p50']} p95={lat['p95']} max={lat['max']}")
    failed = [r for r in report["results"] if not r["passed"]]
    if failed:
        print("failures:")
        for r in failed:
            print(
                f"  - {r['id']} ({r.get('evaluation') or r.get('scenario')}): {r.get('detail')}"
            )
            if "outputs" in r:
                print(f"      outputs={r['outputs'][:6]}")
            if "expected" in r:
                print(f"      expected={r['expected']}")
    if args.json:
        print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0 if summary["failed"] == 0 else 1


def _cmd_latency(args: argparse.Namespace) -> int:
    stats = measure_latency(
        args.text,
        n=args.n,
        warm=args.warm,
        field=args.field,
        phase=args.phase,
        punctuation_completion=args.punct,
        left=args.left or "",
        right=args.right or "",
    )
    print(json.dumps(stats, ensure_ascii=False, indent=2))
    return 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="ime-mixed", description="Mixed EN/JA headless decoder CLI")
    sub = p.add_subparsers(dest="command", required=True)

    d = sub.add_parser("decode", help="decode a raw string to ranked candidates")
    d.add_argument("text")
    d.add_argument("--left", default="")
    d.add_argument("--right", default="")
    d.add_argument("--field", default="prose", choices=["prose", "url", "path", "email", "code", "identifier"])
    d.add_argument("--phase", default="end_of_phrase", choices=["end_of_phrase", "sentence_end"])
    d.add_argument("--punct", action="store_true")
    d.add_argument("--topk", type=int, default=8)
    d.add_argument("--json", action="store_true")
    d.add_argument("-v", "--verbose", action="store_true")
    d.set_defaults(func=_cmd_decode)

    e = sub.add_parser("eval", help="run acceptance seed JSONL")
    e.add_argument("path")
    e.add_argument("--topk", type=int, default=8)
    e.add_argument("--json", action="store_true")
    e.set_defaults(func=_cmd_eval)

    lat = sub.add_parser("latency", help="measure decode latency")
    lat.add_argument("text")
    lat.add_argument("--n", type=int, default=200)
    lat.add_argument("--warm", type=int, default=20)
    lat.add_argument("--field", default="prose")
    lat.add_argument("--phase", default="end_of_phrase")
    lat.add_argument("--punct", action="store_true")
    lat.add_argument("--left", default="")
    lat.add_argument("--right", default="")
    lat.set_defaults(func=_cmd_latency)

    sim = sub.add_parser("simulate", help="interactive key-level simulator")
    sim.set_defaults(func=_cmd_simulate)
    return p


def _cmd_simulate(args: argparse.Namespace) -> int:
    from .simulate import run_interactive

    return run_interactive()


def main(argv: Optional[List[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return int(args.func(args))


if __name__ == "__main__":
    sys.exit(main())
