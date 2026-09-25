from __future__ import annotations

from typing import List, Tuple

UTF16 = "utf16"
SCALAR = "scalar"
UTF8 = "utf8"


def scalar_len(text: str) -> int:
    return len(text)


def utf16_len(text: str) -> int:
    n = 0
    for ch in text:
        n += 2 if ord(ch) > 0xFFFF else 1
    return n


def utf8_len(text: str) -> int:
    return len(text.encode("utf-8"))


def utf16_index_from_scalar(text: str, scalar_index: int) -> int:
    return utf16_len(text[:scalar_index])


def scalar_index_from_utf16(text: str, utf16_index: int) -> int:
    n = 0
    for i, ch in enumerate(text):
        if n >= utf16_index:
            return i
        n += 2 if ord(ch) > 0xFFFF else 1
    return len(text)


def utf8_index_from_scalar(text: str, scalar_index: int) -> int:
    return len(text[:scalar_index].encode("utf-8"))


UNIT_TABLE: List[Tuple[str, str]] = [
    (SCALAR, "Unicode scalar / Python code point (surrogate pair counted as 1)"),
    (UTF16, "Windows UTF-16 code unit (U+10000+ counted as 2)"),
    (UTF8, "UTF-8 byte"),
]


def convert_range(text: str, rng: Tuple[int, int], src: str, dst: str) -> Tuple[int, int]:
    a, b = rng
    if src == dst:
        return a, b
    if src == SCALAR and dst == UTF16:
        return utf16_index_from_scalar(text, a), utf16_index_from_scalar(text, b)
    if src == UTF16 and dst == SCALAR:
        return scalar_index_from_utf16(text, a), scalar_index_from_utf16(text, b)
    if src == SCALAR and dst == UTF8:
        return utf8_index_from_scalar(text, a), utf8_index_from_scalar(text, b)
    if src == UTF8 and dst == SCALAR:
        raw = text.encode("utf-8")
        return len(raw[:a].decode("utf-8", errors="ignore")), len(
            raw[:b].decode("utf-8", errors="ignore")
        )
    raise ValueError(f"unsupported conversion {src}->{dst}")


def split_safe(text: str, index: int) -> Tuple[str, str]:
    if index <= 0:
        return "", text
    if index >= len(text):
        return text, ""
    ch = text[index]
    prev = text[index - 1]
    if 0xD800 <= ord(prev) <= 0xDBFF and 0xDC00 <= ord(ch) <= 0xDFFF:
        index -= 1
    return text[:index], text[index:]
