from __future__ import annotations

import hashlib
import re
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Sequence, Tuple

from .lexicon import (
    EN_WORDS,
    MAX_WORD_LEN,
    PHRASE_MAX_LEN,
    PHRASES,
    PARTICLE_INSERTS,
    WORD_INDEX,
    WORDS,
)
from .protect import (
    context_language_hint,
    detect_ident_tokens,
    detect_protect_regions,
    free_regions,
)
from .romaji import RomajiResult, convert_full, convert_romaji
from .types import (
    Candidate,
    EditOp,
    InputSnapshot,
    ScoreFeatures,
    Span,
    SpanKind,
)
from .typo import fuzzy_phrase_cost, kana_repair_variants

W_EN = 1.0
W_JP = 1.0
W_EDIT = -1.6
W_PROTECT = 0.6
W_CONTEXT = 0.9
W_COMPLETION = 0.35
W_RAW = 0.45
W_COVERAGE = 6.0
W_PHRASE = 3.0
W_IDENT = 0.55
MIXED_BONUS = 0.55
SENT_FINAL_BONUS = 0.4

TITLE_RE = re.compile(r"^[A-Z][a-z]+")
CAMEL_RE = re.compile(r"[A-Z]+(?![a-z])|[A-Z][a-z]+|[a-z]+|\d+")
EN_TAIL_PUNCT = ".!?"


@dataclass
class _Seg:
    kind: SpanKind
    start: int
    end: int
    kana: str = ""
    reading: str = ""
    surface: str = ""
    edits: List[EditOp] = None  # type: ignore
    cost: float = 0.0
    ok: bool = True
    raw_text: str = ""
    romaji: Optional[RomajiResult] = None

    def __post_init__(self) -> None:
        if self.edits is None:
            self.edits = []


def _stable_id(output: str, rev: int, cg: int, ig: int) -> str:
    h = hashlib.sha1(f"{output}|{rev}|{cg}|{ig}".encode("utf-8")).hexdigest()
    return h[:12]


def _strip_tail_punct(text: str) -> Tuple[str, str]:
    i = len(text)
    while i > 0 and text[i - 1] in EN_TAIL_PUNCT + "。、":
        i -= 1
    return text[:i], text[i:]


def score_english(text: str) -> float:
    if not text:
        return -4.0
    score = 0.0
    stripped = text.strip()
    if not stripped:
        return -4.0
    if " " in text or "\t" in text:
        tokens = [t for t in re.split(r"\s+", text) if t]
        if not tokens:
            return -4.0
        hits = 0
        for t in tokens:
            core = t.strip(".,!?;:\"'()[]{}").lower()
            if core and core in EN_WORDS:
                hits += 1
        score += 1.2 * hits
        score += 0.35 * len(tokens)
        if hits >= 2:
            score += 2.0
        elif hits == 1 and len(tokens) >= 2:
            score += 0.8
        if re.search(r"[一-鿿-鿿぀-ヿ]", text):
            score -= 4.0
    else:
        core = stripped.strip(".,!?;:")
        low = core.lower().strip(".,!?;:")
        if low in EN_WORDS:
            score += 3.2
        if TITLE_RE.match(core):
            score += 2.2
            if len(core) >= 3:
                score += 0.4
        if re.fullmatch(r"[A-Z]{2,6}", core):
            score += 2.0
        if re.fullmatch(r"[a-z]+", core):
            score += 1.1
        if re.search(r"\d", core):
            score += 1.4
        if "_" in core:
            score += 1.4
        if re.search(r"[一-鿿぀-ヿ]", core):
            score -= 5.0
        if re.fullmatch(r"[a-z]+", core) and low not in EN_WORDS:
            jp_probe = convert_full(core)
            if jp_probe.unconsumed_start is None and jp_probe.kana:
                score -= 0.9
    return score


def _looks_japanese_capable(text: str) -> bool:
    if not text:
        return False
    if re.search(r"[一-鿿぀-ヿ]", text):
        return True
    if " " in text or "\t" in text:
        return False
    core = text.strip(EN_TAIL_PUNCT + "、。")
    if not core:
        return False
    if not re.fullmatch(r"[A-Za-z.!?]+", core):
        return False
    probe = convert_full(core)
    return probe.unconsumed_start is None


def _romaji_to_reading(raw_ja: str) -> Tuple[Optional[str], Optional[RomajiResult], List[EditOp]]:
    conv = convert_full(raw_ja)
    if conv.unconsumed_start is None and conv.kana:
        return conv.kana, conv, []
    best: Optional[Tuple[str, RomajiResult, List[EditOp]]] = None
    for edited, ops in _romaji_edits(raw_ja):
        c = convert_full(edited)
        if c.unconsumed_start is None and c.kana:
            edit_ops = [
                EditOp(op="romaji_edit", raw_range=(0, len(raw_ja)), detail=f"ops={ops}", cost=float(ops))
            ]
            cand = (c.kana, c, edit_ops)
            if best is None or ops < len(best[2]):
                best = cand
            if ops == 1:
                break
    if best:
        return best
    if conv.kana and (conv.unconsumed_start is None or conv.unconsumed_start > 0):
        return conv.kana, conv, [
            EditOp(
                op="partial_romaji",
                raw_range=(0, len(raw_ja)),
                detail=f"unconsumed={conv.unconsumed_start}",
                cost=2.0,
            )
        ]
    return None, None, []


def _romaji_edits(raw: str) -> List[Tuple[str, int]]:
    out: List[Tuple[str, int]] = [(raw, 0)]
    vowels = "aiueo"
    seen = {raw}
    for i, ch in enumerate(raw):
        if ch in vowels:
            for v in vowels:
                if v == ch:
                    continue
                cand = raw[:i] + v + raw[i + 1 :]
                if cand not in seen:
                    seen.add(cand)
                    out.append((cand, 1))
        cand = raw[:i] + raw[i + 1 :]
        if cand not in seen and ch not in " ":
            seen.add(cand)
            out.append((cand, 1))
        for v in vowels:
            cand = raw[:i] + v + raw[i:]
            if cand not in seen:
                seen.add(cand)
                out.append((cand, 1))
    if len(out) > 1 and len(raw) <= 40:
        base_list = list(out)
        for text, ops in base_list:
            if ops != 1:
                continue
            for i, ch in enumerate(text):
                if ch in vowels:
                    for v in vowels:
                        if v == ch:
                            continue
                        cand = text[:i] + v + text[i + 1 :]
                        if cand not in seen:
                            seen.add(cand)
                            out.append((cand, 2))
                for v in vowels:
                    cand = text[:i] + v + text[i:]
                    if cand not in seen:
                        seen.add(cand)
                        out.append((cand, 2))
            if len(out) > 400:
                break
    return out


@dataclass
class ReadingSolution:
    surface: str
    reading: str
    cost: float
    edits: List[EditOp]
    matched_phrase: Optional[str] = None


def solve_reading(reading: str) -> List[ReadingSolution]:
    solutions: List[ReadingSolution] = []
    seen_surfaces = set()

    def add(sol: ReadingSolution) -> None:
        key = (sol.surface, round(sol.cost, 3))
        if key in seen_surfaces:
            return
        seen_surfaces.add(key)
        solutions.append(sol)

    for p in PHRASES:
        if p.reading == reading:
            add(
                ReadingSolution(
                    surface=p.surface,
                    reading=p.reading,
                    cost=p.cost,
                    edits=[],
                    matched_phrase=p.reading,
                )
            )
    for text, ops in kana_repair_variants(reading, max_ops=2, limit=48):
        for p in PHRASES:
            if text == p.reading:
                    add(
                        ReadingSolution(
                            surface=p.surface,
                            reading=reading,
                            cost=p.cost + 6.0 * ops,
                            edits=[
                                EditOp(
                                    op="kana_repair",
                                    detail=f"{reading}->{p.reading}",
                                    cost=2.0 * ops,
                                )
                            ],
                            matched_phrase=p.reading,
                        )
                    )
    for p in PHRASES:
        if p.reading == reading:
            continue
        d = fuzzy_phrase_cost(reading, p.reading)
        if d is not None:
            add(
                ReadingSolution(
                    surface=p.surface,
                    reading=reading,
                    cost=p.cost + 7.5 * d,
                    edits=[
                        EditOp(
                            op="phrase_fuzzy",
                            detail=f"dist={d}:{reading}~{p.reading}",
                            cost=1.5 * d,
                        )
                    ],
                    matched_phrase=p.reading,
                )
            )
    word_sols = _segment_words(reading, allow_particles=True)
    for sol in word_sols:
        add(sol)
    if not solutions:
        word_sols = _segment_words(reading, allow_particles=False)
        for sol in word_sols:
            add(sol)
    solutions.sort(key=lambda s: s.cost)
    return solutions


def _segment_words(reading: str, allow_particles: bool) -> List[ReadingSolution]:
    n = len(reading)
    # dp[i] = list of (cost, surface, reading, edits, path)
    dp: List[List[Tuple[float, str, str, List[EditOp]]] ] = [[] for _ in range(n + 1)]
    dp[0].append((0.0, "", "", []))
    for i in range(n):
        if not dp[i]:
            continue
        for state in dp[i][:4]:
            base_cost, base_surf, base_read, base_edits = state
            matched = False
            max_l = min(MAX_WORD_LEN, n - i)
            for L in range(max_l, 0, -1):
                piece = reading[i : i + L]
                entries = WORD_INDEX.get(piece)
                if not entries:
                    continue
                matched = True
                for ent in entries:
                    if len(dp[i + L]) >= 4:
                        break
                    dp[i + L].append(
                        (
                            base_cost + ent.cost,
                            base_surf + ent.surface,
                            base_read + ent.reading,
                            list(base_edits),
                        )
                    )
            if allow_particles and not matched and i > 0:
                for pr, ps, pc in PARTICLE_INSERTS:
                    piece = reading[i : i + len(pr)]
                    if piece == pr and len(dp[i + len(pr)]) < 4:
                        dp[i + len(pr)].append(
                            (
                                base_cost + pc,
                                base_surf + ps,
                                base_read + pr,
                                list(base_edits)
                                + [
                                    EditOp(
                                        op="particle_insert",
                                        detail=pr,
                                        cost=1.2,
                                    )
                                ],
                            )
                        )
    out: List[ReadingSolution] = []
    for cost, surf, read, edits in dp[n]:
        if surf:
            out.append(
                ReadingSolution(
                    surface=surf,
                    reading=reading,
                    cost=cost,
                    edits=list(edits),
                )
            )
    return out


def _collect_cuts(text: str) -> List[int]:
    n = len(text)
    cuts = set()
    if n == 0:
        return []
    m = TITLE_RE.match(text)
    if m:
        cuts.add(m.end())
    for m in CAMEL_RE.finditer(text):
        if m.start() > 0:
            cuts.add(m.start())
        cuts.add(m.end())
    for i in range(1, n):
        prefix = text[:i]
        if prefix.lower().rstrip(".") in EN_WORDS and not prefix.isupper():
            cuts.add(i)
        if i < n and text[i - 1].isalpha() and text[i].isalpha():
            if text[i].isupper() and text[i - 1].islower():
                cuts.add(i)
            if text[i - 1].isdigit() and text[i].isalpha():
                cuts.add(i)
            if text[i - 1].isalpha() and text[i].isdigit():
                cuts.add(i)
    for i in range(1, n):
        if text[i - 1] == " " and text[i] != " ":
            cuts.add(i)
    cuts.discard(0)
    cuts.discard(n)
    ordered = sorted(c for c in cuts if 0 < c < n)
    if len(ordered) > 24:
        ordered = ordered[:24]
    return ordered


def _region_segmentations(text: str) -> List[List[Tuple[SpanKind, int, int]]]:
    n = len(text)
    if n == 0:
        return []
    hyps: List[List[Tuple[SpanKind, int, int]]] = []
    hyps.append([(SpanKind.JA, 0, n)])
    hyps.append([(SpanKind.EN, 0, n)])
    cuts = _collect_cuts(text)
    for c in cuts:
        hyps.append([(SpanKind.EN, 0, c), (SpanKind.JA, c, n)])
        hyps.append([(SpanKind.JA, 0, c), (SpanKind.EN, c, n)])
    for i, c1 in enumerate(cuts):
        for c2 in cuts[i + 1 :]:
            hyps.append([(SpanKind.EN, 0, c1), (SpanKind.JA, c1, c2), (SpanKind.EN, c2, n)])
            if len(hyps) > 80:
                break
        if len(hyps) > 80:
            break
    fixed_hyps: List[List[Tuple[SpanKind, int, int]]] = []
    punct_set = set(".,!?;、。！？")
    for h in hyps:
        merged: List[Tuple[SpanKind, int, int]] = []
        for kind, a, b in h:
            if (
                merged
                and kind == SpanKind.EN
                and text[a:b]
                and all(c in punct_set for c in text[a:b])
                and merged[-1][0] == SpanKind.JA
            ):
                prev = merged[-1]
                merged[-1] = (SpanKind.JA, prev[1], b)
                continue
            merged.append((kind, a, b))
        fixed_hyps.append(merged)
    unique = []
    seen = set()
    for h in fixed_hyps:
        key = tuple(h)
        if key in seen:
            continue
        seen.add(key)
        unique.append(h)
    return unique


def _build_span(
    kind: SpanKind,
    abs_start: int,
    abs_end: int,
    raw: str,
) -> _Seg:
    text = raw[abs_start:abs_end]
    seg = _Seg(kind=kind, start=abs_start, end=abs_end, raw_text=text)
    if kind in (SpanKind.EN, SpanKind.PROTECT, SpanKind.IDENT):
        seg.surface = text
        seg.reading = text
        seg.kana = ""
        seg.ok = True
        return seg
    ja_text = text
    trailing = ""
    if ja_text and ja_text[-1] in ".。":
        if ja_text[-1] == ".":
            trailing = "。"
            ja_text = ja_text[:-1]
        else:
            trailing = "。"
            ja_text = ja_text[:-1]
    reading, romaji, edits = _romaji_to_reading(ja_text)
    if reading is None:
        seg.ok = False
        seg.surface = text
        seg.reading = text
        seg.edits = edits
        return seg
    sols = solve_reading(reading)
    if not sols:
        seg.ok = False
        seg.surface = text
        seg.reading = reading
        seg.edits = edits
        seg.romaji = romaji
        return seg
    best = sols[0]
    seg.kana = reading
    seg.reading = reading
    seg.surface = best.surface + trailing
    seg.cost = best.cost
    seg.edits = list(edits) + list(best.edits)
    seg.romaji = romaji
    if trailing:
        seg.edits.append(EditOp(op="punct_map", raw_range=(abs_end - 1, abs_end), detail="->。", cost=0.0))
    return seg


def _coverage_ok(segs: Sequence[_Seg]) -> float:
    total = 0
    ok = 0.0
    for s in segs:
        length = s.end - s.start
        total += length
        if s.kind == SpanKind.JA:
            if s.ok and s.surface:
                ok += length
            elif s.ok:
                ok += 0.5 * length
        else:
            ok += length
    if total == 0:
        return 0.0
    return ok / total


class MixedDecoder:
    def __init__(self) -> None:
        pass

    def decode(self, snap: InputSnapshot) -> List[Candidate]:
        raw = snap.raw_text
        if not raw:
            return []
        field = snap.field
        strong = detect_protect_regions(raw, field, str(snap.field_hints.get("field_type", "text")))
        idents = detect_ident_tokens(raw, strong)
        fixed = strong + idents
        regions = free_regions(len(raw), fixed)
        free_hyps: List[List[Tuple[SpanKind, int, int]]] = []
        for s, e in regions:
            text = raw[s:e]
            local = _region_segmentations(text)
            shifted: List[Tuple[SpanKind, int, int]] = []
            converted = []
            for hyp in local:
                converted.append([(k, s + a, s + b) for k, a, b in hyp])
            free_hyps.append(converted)  # type: ignore
        if not fixed and not regions:
            return []
        combos: List[List[Tuple[SpanKind, int, int]]] = [[]]
        if free_hyps:
            for group in free_hyps:
                nxt = []
                for prefix in combos:
                    for hyp in group:
                        nxt.append(prefix + hyp)
                        if len(nxt) > 96:
                            break
                    if len(nxt) > 96:
                        break
                combos = nxt
        fixed_sorted = sorted(fixed, key=lambda r: r.start)
        candidates: List[Candidate] = []
        ctx = context_language_hint(snap.left_context, snap.right_context)
        raw_edit_cost = 0.0
        for combo in combos:
            pieces: List[Tuple[SpanKind, int, int]] = []
            for fr in fixed_sorted:
                pieces.append((fr.kind, fr.start, fr.end))
            pieces.extend(combo)
            pieces.sort(key=lambda p: p[1])
            if pieces and pieces[0][1] > 0:
                pieces.insert(0, (SpanKind.EN, 0, pieces[0][1]))
            valid = True
            for a, b in zip(pieces, pieces[1:]):
                if a[2] > b[1] or a[1] < 0 or a[2] > len(raw):
                    valid = False
                    break
            if not valid:
                continue
            merged: List[Tuple[SpanKind, int, int]] = []
            for p in pieces:
                if merged and merged[-1][0] == p[0] and merged[-1][2] == p[1]:
                    merged[-1] = (p[0], merged[-1][1], p[2])
                else:
                    merged.append(p)
            segs = [_build_span(k, a, b, raw) for k, a, b in merged]
            if any(s.kind == SpanKind.JA and not s.ok for s in segs):
                if not any(s.kind == SpanKind.EN and score_english(s.raw_text) > 1.0 for s in segs):
                    if not any(s.kind in (SpanKind.PROTECT, SpanKind.IDENT) for s in segs):
                        continue
            cand = self._assemble(segs, snap, ctx, raw_edit_cost)
            if cand is not None:
                candidates.append(cand)
        raw_cand = self._raw_candidate(snap)
        candidates.append(raw_cand)
        self._score_all(candidates, snap, ctx)
        candidates.sort(key=lambda c: (-c.score_features.total, c.output_text))
        uniq: List[Candidate] = []
        seen_out = set()
        for c in candidates:
            if c.output_text in seen_out:
                continue
            seen_out.add(c.output_text)
            if snap.punctuation_completion_enabled and snap.phase_hint == "sentence_end":
                completed = self._with_completion(c, snap, ctx)
                if completed is not None and completed.output_text not in seen_out:
                    seen_out.add(completed.output_text)
                    uniq.append(completed)
                    if completed.score_features.total > c.score_features.total:
                        pass
            uniq.append(c)
        if snap.punctuation_completion_enabled and snap.phase_hint == "sentence_end":
            uniq.sort(key=lambda c: (-c.score_features.total, c.output_text))
        limit = max(1, snap.candidate_limit)
        return uniq[:limit]

    def _assemble(
        self,
        segs: List[_Seg],
        snap: InputSnapshot,
        ctx: Dict[str, float],
        raw_edit_cost: float,
    ) -> Optional[Candidate]:
        out_parts: List[str] = []
        read_parts: List[str] = []
        spans: List[Span] = []
        edits: List[EditOp] = []
        out_cursor = 0
        read_cursor = 0
        for seg in segs:
            if seg.kind == SpanKind.JA and not seg.ok:
                return None
            out_text = seg.surface if seg.surface else seg.raw_text
            read_text = seg.reading if seg.reading else seg.raw_text
            span = Span(
                raw_range=(seg.start, seg.end),
                reading_range=(read_cursor, read_cursor + len(read_text)),
                output_range=(out_cursor, out_cursor + len(out_text)),
                kind=seg.kind,
                raw_text=seg.raw_text,
                reading=read_text,
                output_text=out_text,
            )
            spans.append(span)
            out_parts.append(out_text)
            read_parts.append(read_text)
            out_cursor += len(out_text)
            read_cursor += len(read_text)
            edits.extend(seg.edits)
        output = "".join(out_parts)
        cand = Candidate(
            output_text=output,
            spans=spans,
            edit_operations=edits,
            source_revision=snap.revision,
            source_context_generation=snap.context_generation,
            source_interaction_generation=snap.interaction_generation,
            stable_candidate_id=_stable_id(output, snap.revision, snap.context_generation, snap.interaction_generation),
            is_raw=False,
            reading_text="".join(read_parts),
        )
        return cand

    def _raw_candidate(self, snap: InputSnapshot) -> Candidate:
        raw = snap.raw_text
        span = Span(
            raw_range=(0, len(raw)),
            reading_range=(0, len(raw)),
            output_range=(0, len(raw)),
            kind=SpanKind.EN,
            raw_text=raw,
            reading=raw,
            output_text=raw,
        )
        cand = Candidate(
            output_text=raw,
            spans=[span],
            edit_operations=[],
            source_revision=snap.revision,
            source_context_generation=snap.context_generation,
            source_interaction_generation=snap.interaction_generation,
            stable_candidate_id=_stable_id(raw, snap.revision, snap.context_generation, snap.interaction_generation),
            is_raw=True,
            reading_text=raw,
        )
        return cand

    def _with_completion(
        self, cand: Candidate, snap: InputSnapshot, ctx: Dict[str, float]
    ) -> Optional[Candidate]:
        if cand.is_raw:
            return None
        if cand.output_text.endswith(("。", ".", "！", "？", "!", "?", "、", ",")):
            return None
        if not cand.output_text:
            return None
        body, tail = _strip_tail_punct(cand.output_text)
        if tail:
            return None
        completed = cand.output_text + "。"
        span = Span(
            raw_range=cand.spans[-1].raw_range if cand.spans else (0, 0),
            reading_range=cand.spans[-1].reading_range if cand.spans else (0, 0),
            output_range=(len(cand.output_text), len(completed)),
            kind=SpanKind.PUNCT,
            raw_text="",
            reading="",
            output_text="。",
        )
        c = Candidate(
            output_text=completed,
            spans=list(cand.spans) + [span],
            edit_operations=list(cand.edit_operations)
            + [EditOp(op="punct_complete", detail="append 。", cost=0.0)],
            source_revision=snap.revision,
            source_context_generation=snap.context_generation,
            source_interaction_generation=snap.interaction_generation,
            stable_candidate_id=_stable_id(
                completed, snap.revision, snap.context_generation, snap.interaction_generation
            ),
            is_raw=False,
            reading_text=cand.reading_text,
        )
        self._score_one(c, snap, ctx, completion=True)
        base = Candidate(
            output_text=cand.output_text,
            spans=cand.spans,
            edit_operations=cand.edit_operations,
            score_features=cand.score_features,
            source_revision=snap.revision,
            source_context_generation=snap.context_generation,
            source_interaction_generation=snap.interaction_generation,
            stable_candidate_id=cand.stable_candidate_id,
            is_raw=cand.is_raw,
            reading_text=cand.reading_text,
        )
        self._score_one(base, snap, ctx, completion=False)
        if c.score_features.total >= base.score_features.total:
            return c
        return c

    def _score_all(
        self, candidates: List[Candidate], snap: InputSnapshot, ctx: Dict[str, float]
    ) -> None:
        for c in candidates:
            self._score_one(c, snap, ctx, completion=False)

    def _score_one(
        self, cand: Candidate, snap: InputSnapshot, ctx: Dict[str, float], completion: bool
    ) -> None:
        f = ScoreFeatures()
        if cand.is_raw:
            f.en_score = score_english(cand.output_text)
            f.context_score = W_CONTEXT * ctx.get("en", 0.0) * 0.3
            f.total = W_RAW + 0.2 * f.en_score + f.context_score
            cand.score_features = f
            return
        kinds = {s.kind for s in cand.spans}
        for s in cand.spans:
            if s.kind in (SpanKind.EN, SpanKind.IDENT):
                f.en_score += score_english(s.output_text or s.raw_text)
            elif s.kind == SpanKind.PROTECT:
                f.protect_score += 1.5
            elif s.kind == SpanKind.JA:
                pass
            elif s.kind == SpanKind.PUNCT:
                pass
        ja_spans = [s for s in cand.spans if s.kind == SpanKind.JA]
        en_spans = [s for s in cand.spans if s.kind in (SpanKind.EN, SpanKind.IDENT)]
        glued_penalty = 0.0
        for idx, s in enumerate(cand.spans):
            if s.kind != SpanKind.EN:
                continue
            if " " in s.output_text or " " in s.raw_text:
                continue
            prev_ja = idx > 0 and cand.spans[idx - 1].kind == SpanKind.JA
            next_ja = idx + 1 < len(cand.spans) and cand.spans[idx + 1].kind == SpanKind.JA
            if prev_ja or next_ja:
                core = s.raw_text.strip(".,!?;、。")
                if core and core.lower() in EN_WORDS and len(core) <= 2:
                    glued_penalty += 3.5
                elif core and core.islower() and core.lower() not in EN_WORDS:
                    glued_penalty += 1.5
                elif core and core.islower() and len(core) <= 3:
                    glued_penalty += 2.5
        raw_total = len(snap.raw_text) or 1
        ja_raw_len = sum(s.raw_range[1] - s.raw_range[0] for s in ja_spans)
        cov_parts = 0.0
        for s in ja_spans:
            seg_len = s.raw_range[1] - s.raw_range[0]
            if s.output_text and s.output_text != s.raw_text:
                cov_parts += seg_len
            elif s.output_text:
                cov_parts += seg_len * 0.7
        f.coverage = (cov_parts / raw_total) if ja_spans else (1.0 if not en_spans else 0.0)
        jp_local = 0.0
        edit_cost = 0.0
        for e in cand.edit_operations:
            edit_cost += e.cost
        f.edit_cost = edit_cost
        has_kanji = any("一" <= ch <= "鿿" for ch in cand.output_text)
        for s in ja_spans:
            if not s.output_text:
                continue
            if s.output_text != s.raw_text and not s.raw_text[:1].isupper():
                jp_local += 1.2
            if any("一" <= ch <= "鿿" for ch in s.output_text):
                jp_local += 1.0
            core, tail = _strip_tail_punct(s.output_text)
            if core.endswith(("ます", "です", "ました", "ません")):
                jp_local += SENT_FINAL_BONUS
            if tail == "。":
                jp_local += 0.3
        f.jp_score = jp_local + W_COVERAGE * f.coverage
        if any(
            e.op in ("phrase_fuzzy", "kana_repair", "particle_insert", "romaji_edit")
            for e in cand.edit_operations
        ):
            phrase_bonus = 0.0
            for e in cand.edit_operations:
                if e.op == "phrase_fuzzy":
                    phrase_bonus = 1.5
                if e.op == "kana_repair":
                    phrase_bonus = max(phrase_bonus, 1.2)
            f.jp_score += phrase_bonus
        if has_kanji and ja_spans:
            f.jp_score += 1.0
        if len(ja_spans) >= 1 and en_spans:
            f.jp_score += MIXED_BONUS
            f.en_score += 0.3
        if kinds == {SpanKind.EN} or SpanKind.IDENT in kinds and not ja_spans:
            f.en_score += 0.5
        if not ja_spans and not en_spans and SpanKind.PROTECT in kinds:
            f.protect_score += 1.0
        if en_spans:
            f.context_score += W_CONTEXT * ctx.get("en", 0.0)
        if ja_spans:
            f.context_score += W_CONTEXT * ctx.get("ja", 0.0)
        if not ja_spans and not en_spans:
            f.context_score += W_CONTEXT * 0.5 * (ctx.get("en", 0.0) + ctx.get("ja", 0.0))
        if ja_spans and not en_spans and ctx.get("en", 0.0) >= 1.0:
            f.context_score -= 1.6
        if en_spans and not ja_spans and ctx.get("ja", 0.0) >= 1.0:
            f.context_score -= 1.2
        if completion:
            f.completion_bonus = W_COMPLETION
        total = (
            W_EN * f.en_score
            + W_JP * f.jp_score
            + W_EDIT * f.edit_cost
            + W_PROTECT * f.protect_score
            + f.context_score
            + f.completion_bonus
            + W_IDENT * sum(1 for s in cand.spans if s.kind == SpanKind.IDENT) * 0.3
            - glued_penalty
        )
        if f.coverage < 0.35 and ja_spans:
            total -= 3.0 * (0.35 - f.coverage)
        f.total = total
        cand.score_features = f


def decode_snapshot(snap: InputSnapshot) -> List[Candidate]:
    return MixedDecoder().decode(snap)
