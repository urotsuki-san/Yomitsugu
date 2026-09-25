from __future__ import annotations

from typing import Iterator, List, Optional, Tuple

VOWELS = "aiueo"
INSERT_KANA = tuple("あいうえおはをっゃゅょ")
YOUON_MAP = {"ゃ": "よ", "ゅ": "ゆ", "ょ": "よ", "ぁ": "あ", "ぃ": "い", "ぅ": "う", "ぇ": "え", "ぉ": "お"}
YOUON_REV = {"よ": "ょ", "ゆ": "ゅ", "あ": "ゃ", "い": "ぃ", "う": "ぅ", "え": "ぇ", "お": "ぉ"}


def levenshtein(a: str, b: str, cap: Optional[int] = None) -> int:
    if a == b:
        return 0
    la, lb = len(a), len(b)
    if cap is not None and abs(la - lb) > cap:
        return cap + 1
    prev = list(range(lb + 1))
    for i in range(1, la + 1):
        cur = [i] + [0] * lb
        row_min = i
        ca = a[i - 1]
        for j in range(1, lb + 1):
            cost = 0 if ca == b[j - 1] else 1
            val = min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost)
            cur[j] = val
            if val < row_min:
                row_min = val
        if cap is not None and row_min > cap:
            return cap + 1
        prev = cur
    return prev[lb]


def normalize_youon(text: str) -> str:
    return "".join(YOUON_MAP.get(ch, ch) for ch in text)


def phrase_threshold(reading_len: int, key_len: int) -> int:
    n = max(reading_len, key_len)
    if n <= 6:
        return 1
    if n <= 10:
        return 2
    if n <= 14:
        return 4
    return max(4, n // 3)


def fuzzy_phrase_cost(reading: str, key: str) -> Optional[int]:
    cap = phrase_threshold(len(reading), len(key))
    d = levenshtein(reading, key, cap)
    if d <= cap:
        return d
    d2 = levenshtein(normalize_youon(reading), normalize_youon(key), cap)
    if d2 <= cap:
        return d2
    return None


def kana_repair_variants(reading: str, max_ops: int = 2, limit: int = 64) -> List[Tuple[str, int]]:
    seen = {reading}
    results: List[Tuple[str, int]] = [(reading, 0)]
    frontier = [reading]
    for ops in range(1, max_ops + 1):
        nxt: List[str] = []
        for base in frontier:
            for variant in _one_edit_kana(base):
                if variant in seen:
                    continue
                seen.add(variant)
                nxt.append(variant)
                results.append((variant, ops))
                if len(results) >= limit:
                    return results
        frontier = nxt
        if not frontier:
            break
    return results


def _one_edit_kana(text: str) -> Iterator[str]:
    for i in range(len(text)):
        yield text[:i] + text[i + 1 :]
        for rep in INSERT_KANA:
            yield text[:i] + rep + text[i:]
        ch = text[i]
        if ch == "は":
            yield text[:i] + "わ" + text[i + 1 :]
        elif ch == "わ":
            yield text[:i] + "は" + text[i + 1 :]
        elif ch in YOUON_REV:
            yield text[:i] + YOUON_REV[ch] + text[i + 1 :]
        elif ch in YOUON_MAP:
            yield text[:i] + YOUON_MAP[ch] + text[i + 1 :]
        elif ch in VOWELS:
            for v in VOWELS:
                if v != ch:
                    yield text[:i] + v + text[i + 1 :]
    for i in range(len(text) - 1):
        yield text[:i] + text[i + 1 : i + 2] + text[i] + text[i + 2 :]


def romaji_vowel_edits(raw: str, max_ops: int = 1, limit: int = 48) -> List[Tuple[str, int]]:
    results: List[Tuple[str, int]] = [(raw, 0)]
    seen = {raw}
    frontier = [raw]
    for ops in range(1, max_ops + 1):
        nxt: List[str] = []
        for base in frontier:
            for i, ch in enumerate(base):
                cand = base[:i] + base[i + 1 :]
                if cand not in seen:
                    seen.add(cand)
                    nxt.append(cand)
                    results.append((cand, ops))
                if ch in VOWELS:
                    for v in VOWELS:
                        if v == ch:
                            continue
                        cand = base[:i] + v + base[i + 1 :]
                        if cand not in seen:
                            seen.add(cand)
                            nxt.append(cand)
                            results.append((cand, ops))
                for v in VOWELS:
                    cand = base[:i] + v + base[i:]
                    if cand not in seen:
                        seen.add(cand)
                        nxt.append(cand)
                        results.append((cand, ops))
                if len(results) >= limit:
                    return results
        frontier = nxt
        if not frontier:
            break
    return results
