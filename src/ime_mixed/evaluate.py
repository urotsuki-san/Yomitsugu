from __future__ import annotations

import json
import statistics
import time
from typing import Any, Dict, List, Optional

from .decoder import MixedDecoder
from .session import ImeSession, SessionConfig
from .types import CompositionPhase, InputSnapshot


def build_snapshot(
    raw: str,
    left: str = "",
    right: str = "",
    field: str = "prose",
    phase: str = "end_of_phrase",
    punctuation_completion: bool = False,
    revision: int = 1,
    context_generation: int = 1,
    interaction_generation: int = 1,
    candidate_limit: int = 8,
) -> InputSnapshot:
    return InputSnapshot(
        session_id="cli",
        revision=revision,
        context_generation=context_generation,
        interaction_generation=interaction_generation,
        phase=CompositionPhase.AUTO_COMPOSING,
        raw_text=raw,
        raw_cursor=len(raw),
        left_context=left,
        right_context=right,
        field_hints={"field": field, "phase": phase},
        punctuation_completion_enabled=punctuation_completion,
        candidate_limit=candidate_limit,
    )


def decode_text(
    raw: str,
    left: str = "",
    right: str = "",
    field: str = "prose",
    phase: str = "end_of_phrase",
    punctuation_completion: bool = False,
    candidate_limit: int = 8,
) -> List[Dict[str, Any]]:
    snap = build_snapshot(
        raw,
        left=left,
        right=right,
        field=field,
        phase=phase,
        punctuation_completion=punctuation_completion,
        candidate_limit=candidate_limit,
    )
    decoder = MixedDecoder()
    cands = decoder.decode(snap)
    return [c.to_dict() for c in cands]


def measure_latency(
    raw: str,
    n: int = 200,
    warm: int = 20,
    **kwargs: Any,
) -> Dict[str, Any]:
    decoder = MixedDecoder()
    snap = build_snapshot(raw, **kwargs)
    times: List[float] = []
    for i in range(n + warm):
        t0 = time.perf_counter()
        decoder.decode(snap)
        t1 = time.perf_counter()
        if i >= warm:
            times.append((t1 - t0) * 1000.0)
    times_sorted = sorted(times)

    def pct(p: float) -> float:
        if not times_sorted:
            return 0.0
        idx = min(len(times_sorted) - 1, max(0, int(round((p / 100.0) * (len(times_sorted) - 1)))))
        return round(times_sorted[idx], 4)

    return {
        "raw": raw,
        "n": len(times),
        "p50_ms": pct(50),
        "p95_ms": pct(95),
        "p99_ms": pct(99),
        "mean_ms": round(statistics.fmean(times), 4) if times else 0.0,
        "max_ms": round(max(times), 4) if times else 0.0,
    }


def load_seed_cases(path: str) -> List[Dict[str, Any]]:
    cases: List[Dict[str, Any]] = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            cases.append(json.loads(line))
    return cases


def evaluate_text_case(case: Dict[str, Any], topk: int = 8) -> Dict[str, Any]:
    ctx = case.get("context", {})
    field = ctx.get("field", "prose")
    left = ctx.get("left", "")
    right = ctx.get("right", "")
    phase = ctx.get("phase", "end_of_phrase")
    punct = bool(ctx.get("punctuation_completion", False))
    raw = case["raw"]
    expected: List[str] = list(case.get("expected", []))
    evaluation = case.get("evaluation", "")
    snap = build_snapshot(
        raw,
        left=left,
        right=right,
        field=field,
        phase=phase,
        punctuation_completion=punct,
        candidate_limit=topk,
    )
    decoder = MixedDecoder()
    t0 = time.perf_counter()
    cands = decoder.decode(snap)
    ms = (time.perf_counter() - t0) * 1000.0
    outputs = [c.output_text for c in cands]
    passed = False
    detail = ""
    if evaluation == "exact_preserve":
        passed = bool(outputs) and outputs[0] == expected[0]
        detail = f"top1={outputs[0] if outputs else None!r}"
    elif evaluation == "desired_top1":
        passed = bool(outputs) and outputs[0] in expected
        detail = f"top1={outputs[0] if outputs else None!r}"
    elif evaluation == "desired_candidate":
        hit = [e for e in expected if e in outputs]
        passed = bool(hit)
        detail = f"hit={hit[0] if hit else None!r} outputs={outputs[:4]}"
    elif evaluation == "safe_ambiguous":
        passed = raw in outputs or (bool(outputs) and outputs[0] == raw)
        detail = f"raw_in_outputs={raw in outputs} outputs={outputs[:4]}"
    else:
        detail = f"unknown_evaluation={evaluation}"
    return {
        "id": case.get("id", ""),
        "type": case.get("type", "text"),
        "passed": passed,
        "evaluation": evaluation,
        "raw": raw,
        "expected": expected,
        "outputs": outputs,
        "detail": detail,
        "latency_ms": round(ms, 4),
    }


def _session_for_case(case: Dict[str, Any]) -> ImeSession:
    cfg = SessionConfig()
    session = ImeSession(cfg)
    steps = case.get("steps", [])
    for step in steps:
        action = step.get("action")
        if action == "type":
            session.type(step.get("text", ""))
            if "revision" in step:
                session.revision = int(step["revision"])
            if "context_generation" in step:
                session.context_generation = int(step["context_generation"])
        elif action == "backspace":
            session.backspace()
            if "revision" in step:
                session.revision = int(step["revision"])
            if "context_generation" in step:
                session.context_generation = int(step["context_generation"])
        elif action == "focus_other_field":
            session.focus_other_field(
                left=step.get("left", ""),
                right=step.get("right", ""),
                field_type=step.get("field", "prose"),
            )
            if "context_generation" in step:
                session.context_generation = int(step["context_generation"])
        elif action == "deliver_result":
            cand = None
            rev = int(step.get("source_revision", session.revision))
            cg = int(step.get("source_context_generation", session.context_generation))
            ig = int(step.get("source_interaction_generation", 0))
            if session.candidates:
                cand = session.candidates[0]
            else:
                from .types import Candidate as Cand

                cand = Cand(
                    output_text="こんにちは。",
                    stable_candidate_id="stale",
                    source_revision=rev,
                )
            ok = session.deliver_result(cand, rev, cg, ig)
            session._last_deliver_ok = ok  # type: ignore[attr-defined]
        elif action == "compose":
            session.type(step.get("text", ""))
        elif action == "press":
            key = step.get("key", "")
            if key == "Enter":
                session.press_enter()
            elif key == "Escape":
                session.press_escape()
            elif key == "Space":
                session.press_space()
            elif key == "Backspace":
                session.backspace()
        elif action == "choose_literal":
            session.choose_literal()
        elif action == "simulate_worker_crash":
            session.simulate_worker_crash()
        elif action == "activate_text_service_disabled_context":
            session.activate_text_service_disabled_context()
        elif action == "simulate_keys":
            session.simulate_keys(step.get("text", "hello"))
        elif action == "set_learning":
            session.set_learning(bool(step.get("allowed", True)))
        elif action == "undo_commit":
            session.undo_commit()
        elif action == "commit":
            if step.get("text") and not session.raw_text:
                session.type(step["text"])
            session.press_enter()
        else:
            raise ValueError(f"unknown action {action}")
    return session


def evaluate_sequence_case(case: Dict[str, Any]) -> Dict[str, Any]:
    assertions = list(case.get("assertions", []))
    scenario = case.get("scenario", case.get("id", ""))
    try:
        session = _session_for_case(case)
    except Exception as exc:  # noqa: BLE001
        return {
            "id": case.get("id", ""),
            "type": "event_sequence",
            "passed": False,
            "scenario": scenario,
            "detail": f"error: {exc}",
            "assertions": assertions,
        }
    failures: List[str] = []
    raw = case.get("id", "")
    if scenario == "stale_result":
        ok = bool(getattr(session, "_last_deliver_ok", False))
        if ok:
            failures.append("stale result applied")
        if session.raw_text != "konnitiw":
            failures.append(f"raw={session.raw_text!r}")
    elif scenario == "focus_change":
        ok = bool(getattr(session, "_last_deliver_ok", False))
        if ok:
            failures.append("stale result applied after focus change")
        if session.left_context or session.right_context:
            if session.context_generation <= 1:
                failures.append("context generation not advanced")
    elif scenario == "enter_safety":
        if not session.committed_history:
            failures.append("nothing committed")
        else:
            rec = session.committed_history[-1]
            if rec["text"] != "こんにちは":
                failures.append(f"committed={rec['text']!r}")
        if any("send" in e and "passthrough" not in e for e in session.send_events):
            failures.append("send event with commit")
        if any(e == "enter_passthrough" for e in session.output_log):
            failures.append("enter passthrough with composition")
    elif scenario == "literal_restore":
        if not session.output_log:
            failures.append("no commit")
        else:
            top = [o for o in session.output_log if not o.startswith("undo:") and not o.startswith("backspace")]
            if not top or top[0] != "Githubnoripojitoriwokousinnsitekudasai":
                failures.append(f"literal={top[:1]!r}")
    elif scenario == "engine_failure":
        if session.engine_alive:
            failures.append("engine not crashed")
        if session.raw_text == "hello" or "hello" in session.output_log:
            pass
        else:
            failures.append(f"input not maintained raw={session.raw_text!r} out={session.output_log!r}")
        if session.pressed_keys.count("type:hello") != 1:
            failures.append(f"key loss/dup: {session.pressed_keys!r}")
    elif scenario == "disabled_context":
        if session.raw_text:
            failures.append("captured input while disabled")
        if session.learning_log:
            failures.append("learning while disabled")
        if session.diagnostic_log:
            failures.append("diagnostics while disabled")
    elif scenario == "unicode_undo":
        committed = [h["text"] for h in session.committed_history]
        if committed:
            text = committed[-1]
            if "\ud83d" in text or "\udc00" in text:
                failures.append("surrogate leak in committed text")
            if "こんにちは😀" not in text and "こんにちは😀" not in session.output_log:
                if not any("こんにちは😀" in c for c in committed):
                    failures.append(f"emoji broken: {text!r}")
        undone = [o for o in session.output_log if o.startswith("undo:")]
        if not undone:
            failures.append("undo not performed")
        else:
            restored = undone[-1][len("undo:") :]
            if restored != "こんにちは😀" and "こんにちは😀" not in restored:
                if session.raw_text != "こんにちは😀":
                    failures.append(f"restore={session.raw_text!r}")
    elif scenario == "learning_disabled":
        if session.learning_log:
            failures.append("learning recorded")
    else:
        failures.append(f"unknown scenario {scenario}")
    passed = not failures
    return {
        "id": raw,
        "type": "event_sequence",
        "passed": passed,
        "scenario": scenario,
        "detail": "; ".join(failures) if failures else "ok",
        "assertions": assertions,
    }


def evaluate_seed_file(path: str, topk: int = 8) -> Dict[str, Any]:
    cases = load_seed_cases(path)
    results: List[Dict[str, Any]] = []
    for case in cases:
        if case.get("type") == "event_sequence":
            results.append(evaluate_sequence_case(case))
        else:
            results.append(evaluate_text_case(case, topk=topk))
    text_results = [r for r in results if r["type"] == "text"]
    seq_results = [r for r in results if r["type"] == "event_sequence"]
    summary = {
        "total": len(results),
        "passed": sum(1 for r in results if r["passed"]),
        "failed": sum(1 for r in results if not r["passed"]),
        "text_total": len(text_results),
        "text_passed": sum(1 for r in text_results if r["passed"]),
        "sequence_total": len(seq_results),
        "sequence_passed": sum(1 for r in seq_results if r["passed"]),
        "eval_groups": {},
    }
    groups: Dict[str, List[bool]] = {}
    for r in text_results:
        groups.setdefault(r["evaluation"], []).append(r["passed"])
    for k, v in groups.items():
        summary["eval_groups"][k] = {
            "n": len(v),
            "passed": sum(1 for x in v if x),
            "rate": round(sum(1 for x in v if x) / len(v), 3) if v else 0.0,
        }
    latencies = [r["latency_ms"] for r in text_results if "latency_ms" in r]
    if latencies:
        s = sorted(latencies)
        summary["latency_ms"] = {
            "p50": s[len(s) // 2],
            "p95": s[min(len(s) - 1, int(len(s) * 0.95))],
            "max": s[-1],
        }
    return {"summary": summary, "results": results}
