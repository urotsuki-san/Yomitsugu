from __future__ import annotations

import json
from pathlib import Path

import pytest

from ime_mixed.evaluate import evaluate_seed_file, evaluate_text_case, evaluate_sequence_case
from ime_mixed.romaji import convert_full
from ime_mixed.units import convert_range, split_safe, utf16_len
from ime_mixed.session import ImeSession, SessionConfig

ROOT = Path(__file__).resolve().parents[1]
SEED = ROOT / "ime_acceptance_seed_cases.jsonl"


def test_romaji_konnitiwa():
    r = convert_full("konnitiwa")
    assert r.unconsumed_start is None
    assert r.kana == "こんにちわ"


def test_romaji_no_and_ne():
    assert convert_full("no").kana == "の"
    assert convert_full("desune").kana == "ですね"
    assert convert_full("noripoji").kana == "のりぽじ"


def test_romaji_position_mapping():
    r = convert_full("konnitiwa")
    assert r.spans
    total = r.kana
    for span in r.spans:
        piece = total[span.reading_range[0] : span.reading_range[1]]
        assert piece == span.kana
        assert span.raw_range[1] > span.raw_range[0]


def test_utf16_surrogate():
    text = "こんにちは😀"
    assert len(text) == 6
    assert utf16_len(text) == 7
    a, b = convert_range(text, (0, 6), "scalar", "utf16")
    assert b == 7
    left, right = split_safe(text, 5)
    assert right == "😀" or left.endswith("😀")


def test_seed_file_runs():
    report = evaluate_seed_file(str(SEED))
    assert report["summary"]["total"] > 0


def test_u01_top1():
    case = {
        "id": "U01",
        "type": "text",
        "raw": "Githubnoripojitoriwokousinnsitekudasai",
        "evaluation": "desired_top1",
        "expected": ["Githubのリポジトリを更新してください。"],
        "context": {"field": "prose", "left": "", "right": "", "phase": "sentence_end", "punctuation_completion": True},
    }
    r = evaluate_text_case(case)
    assert r["passed"], r


def test_hello_world_space_preserve():
    case = {
        "id": "E04",
        "type": "text",
        "raw": "hello world",
        "evaluation": "exact_preserve",
        "expected": ["hello world"],
        "context": {"field": "prose", "phase": "end_of_phrase", "punctuation_completion": False},
    }
    r = evaluate_text_case(case)
    assert r["passed"], r


def test_sequence_stale_result():
    case = {
        "id": "S01",
        "type": "event_sequence",
        "scenario": "stale_result",
        "steps": [
            {"action": "type", "text": "konnitiwa", "revision": 1},
            {"action": "backspace", "revision": 2},
            {"action": "deliver_result", "source_revision": 1},
        ],
        "assertions": ["revision=1の結果を適用しない", "削除後の生入力を維持"],
    }
    r = evaluate_sequence_case(case)
    assert r["passed"], r


def test_space_contract_english_inserts_space():
    s = ImeSession(SessionConfig(punctuation_completion_enabled=False))
    s.type("hello")
    s.press_space()
    assert s.raw_text == "hello "


def test_space_contract_starts_conversion_for_ja_preview():
    s = ImeSession(SessionConfig(punctuation_completion_enabled=True))
    s.type("konnitiwa")
    action = s.space_action()
    assert action.value in ("start_conversion", "next_candidate")
    s.press_space()
    assert s.phase.value in ("manual_conversion", "auto_composing")


def test_enter_does_not_passthrough_when_composing():
    s = ImeSession()
    s.type("konnitiwa")
    s.press_enter()
    assert s.output_log
    assert "enter_passthrough" not in s.output_log
