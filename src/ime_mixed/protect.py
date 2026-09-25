from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple

from .types import SpanKind


@dataclass
class ProtectRegion:
    start: int
    end: int
    kind: SpanKind
    strength: str
    reason: str = ""

    @property
    def text(self) -> str:
        return ""


URL_RE = re.compile(r"(?:https?://|www\.)[^\s\"'<>]+", re.IGNORECASE)
EMAIL_RE = re.compile(r"[A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,}")
WIN_PATH_RE = re.compile(r"[A-Za-z]:\\[^\s\"'<>|]+")
UNIX_PATH_RE = re.compile(r"(?:/[A-Za-z0-9._\-]+){2,}")
IDENT_RE = re.compile(
    r"[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z0-9_]+)+"
    r"|[A-Za-z][a-z0-9]+(?:[A-Z][a-z0-9]+)+"
    r"|[A-Z]{2,}[0-9]*"
    r"|[A-Za-z]+\d+(?:\.\d+)*[A-Za-z0-9]*"
)
DIGIT_TOKEN_RE = re.compile(r"[A-Za-z]+\d+(?:\.\d+)*[A-Za-z0-9.]*")


def _overlaps(a: Tuple[int, int], b: Tuple[int, int]) -> bool:
    return a[0] < b[1] and b[0] < a[1]


def detect_protect_regions(raw: str, field_hint: str, field_type: str = "text") -> List[ProtectRegion]:
    if field_hint in ("url", "path", "email", "code") or field_type in ("url", "path", "email", "code"):
        return [
            ProtectRegion(
                0,
                len(raw),
                SpanKind.PROTECT,
                "strong",
                f"field={field_hint or field_type}",
            )
        ]
    regions: List[ProtectRegion] = []
    for m in URL_RE.finditer(raw):
        regions.append(ProtectRegion(m.start(), m.end(), SpanKind.PROTECT, "strong", "url"))
    for m in EMAIL_RE.finditer(raw):
        regions.append(ProtectRegion(m.start(), m.end(), SpanKind.PROTECT, "strong", "email"))
    for m in WIN_PATH_RE.finditer(raw):
        regions.append(ProtectRegion(m.start(), m.end(), SpanKind.PROTECT, "strong", "win_path"))
    for m in UNIX_PATH_RE.finditer(raw):
        regions.append(ProtectRegion(m.start(), m.end(), SpanKind.PROTECT, "strong", "unix_path"))
    sorted_regions = sorted(regions, key=lambda r: (r.start, -(r.end - r.start)))
    merged: List[ProtectRegion] = []
    for reg in sorted_regions:
        if merged and _overlaps((merged[-1].start, merged[-1].end), (reg.start, reg.end)):
            if reg.end > merged[-1].end:
                merged[-1] = ProtectRegion(
                    merged[-1].start, reg.end, merged[-1].kind, merged[-1].strength, merged[-1].reason
                )
            continue
        merged.append(reg)
    return merged


KNOWN_EXT = {
    "md", "py", "js", "ts", "jsx", "tsx", "json", "txt", "html", "css", "scss",
    "yml", "yaml", "toml", "cfg", "ini", "log", "sh", "bat", "ps1", "psm1",
    "rs", "go", "java", "cpp", "cc", "cxx", "c", "h", "hpp", "cs", "rb", "php",
    "swift", "kt", "kts", "png", "jpg", "jpeg", "gif", "svg", "pdf", "zip",
    "gz", "tar", "exe", "dll", "so", "dylib", "lock", "mod", "sum", "sql",
    "xml", "csv", "tsv", "env", "gitignore", "npmrc", "editorconfig",
}


def _shrink_dotted(text: str) -> str:
    if "." not in text:
        return text
    parts = text.split(".")
    if len(parts) < 2:
        return text
    last = parts[-1]
    if not last:
        return ".".join(parts[:-1])
    m = re.match(r"^([0-9]+)([a-z]{4,})$", last)
    if m and m.group(2) not in KNOWN_EXT:
        return ".".join(parts[:-1] + [m.group(1)])
    if last.isalpha() and last.islower() and len(last) >= 3 and last not in KNOWN_EXT:
        for ext in sorted(KNOWN_EXT, key=len, reverse=True):
            if (
                last.startswith(ext)
                and len(last) > len(ext)
                and last[len(ext) :].isalpha()
                and last[len(ext) :].islower()
            ):
                return ".".join(parts[:-1] + [ext])
        return ".".join(parts[:-1])
    return text


def detect_ident_tokens(raw: str, base: List[ProtectRegion]) -> List[ProtectRegion]:
    taken = [(r.start, r.end) for r in base]
    idents: List[ProtectRegion] = []
    for m in IDENT_RE.finditer(raw):
        s, e = m.start(), m.end()
        if e - s < 2:
            continue
        text = _shrink_dotted(raw[s:e])
        e = s + len(text)
        if e - s < 2:
            continue
        if any(_overlaps((s, e), t) for t in taken):
            continue
        if text.lower() in {"no", "in", "on", "at", "to", "or", "is", "be", "do", "go", "me", "we"}:
            continue
        idents.append(ProtectRegion(s, e, SpanKind.IDENT, "ident", "ident_token"))
    return idents


def free_regions(length: int, protects: List[ProtectRegion]) -> List[Tuple[int, int]]:
    if length == 0:
        return []
    ordered = sorted(protects, key=lambda r: r.start)
    regions: List[Tuple[int, int]] = []
    cursor = 0
    for reg in ordered:
        if reg.start > cursor:
            regions.append((cursor, reg.start))
        cursor = max(cursor, reg.end)
    if cursor < length:
        regions.append((cursor, length))
    return regions


def context_language_hint(left: str, right: str) -> Dict[str, float]:
    sample = (left + " " + right).strip()
    ja_chars = sum(1 for c in sample if "぀" <= c <= "ヿ" or "一" <= c <= "鿿")
    en_chars = sum(1 for c in sample if c.isascii() and c.isalpha())
    space_words = len(sample.split())
    ja_boost = 0.0
    en_boost = 0.0
    if ja_chars > 0:
        ja_boost += min(1.0, ja_chars / 4.0)
    if en_chars > 0:
        en_boost += min(0.6, en_chars / 20.0)
    if space_words >= 2 and en_chars > 0:
        en_boost += 0.5
    common_en = {"yes", "no", "or", "and", "the", "a", "is", "answer", "please", "hello", "world"}
    tokens = {t.strip(".,!?").lower() for t in sample.split()}
    overlap = len(tokens & common_en)
    if overlap:
        en_boost += 0.4 * min(3, overlap)
    return {"ja": ja_boost, "en": en_boost}
