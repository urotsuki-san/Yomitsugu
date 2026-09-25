from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

VOWELS = set("aiueo")

CORE: Dict[str, str] = {
    "a": "あ", "i": "い", "u": "う", "e": "え", "o": "お",
    "ka": "か", "ki": "き", "ku": "く", "ke": "け", "ko": "こ",
    "ga": "が", "gi": "ぎ", "gu": "ぐ", "ge": "げ", "go": "ご",
    "sa": "さ", "si": "し", "su": "す", "se": "せ", "so": "そ",
    "sha": "しゃ", "shi": "し", "shu": "しゅ", "she": "しぇ", "sho": "しょ",
    "sya": "しゃ", "syu": "しゅ", "syo": "しょ",
    "za": "ざ", "zi": "じ", "zu": "ず", "ze": "ぜ", "zo": "ぞ",
    "ja": "じゃ", "ji": "じ", "ju": "じゅ", "je": "じぇ", "jo": "じょ",
    "jya": "じゃ", "jyu": "じゅ", "jyo": "じょ", "zya": "じゃ", "zyu": "じゅ", "zyo": "じょ",
    "ta": "た", "ti": "ち", "tu": "つ", "te": "て", "to": "と",
    "cha": "ちゃ", "chi": "ち", "chu": "ちゅ", "che": "ちぇ", "cho": "ちょ",
    "tya": "ちゃ", "tyu": "ちゅ", "tyo": "ちょ", "cyi": "ちぃ",
    "tsu": "つ", "tsa": "つぁ", "tsi": "つぃ", "tse": "つぇ", "tso": "つぉ",
    "da": "だ", "di": "ぢ", "du": "づ", "de": "で", "do": "ど",
    "dya": "ぢゃ", "dyu": "ぢゅ", "dyo": "ぢょ",
    "na": "な", "ni": "に", "nu": "ぬ", "ne": "ね", "no": "の",
    "ha": "は", "hi": "ひ", "hu": "ふ", "he": "へ", "ho": "ほ",
    "fu": "ふ", "fa": "ふぁ", "fi": "ふぃ", "fe": "ふぇ", "fo": "ふぉ",
    "ba": "ば", "bi": "び", "bu": "ぶ", "be": "べ", "bo": "ぼ",
    "pa": "ぱ", "pi": "ぴ", "pu": "ぷ", "pe": "ぺ", "po": "ぽ",
    "ma": "ま", "mi": "み", "mu": "む", "me": "め", "mo": "も",
    "ya": "や", "yu": "ゆ", "yo": "よ",
    "ra": "ら", "ri": "り", "ru": "る", "re": "れ", "ro": "ろ",
    "wa": "わ", "wo": "を",
    "kya": "きゃ", "kyi": "きぃ", "kyu": "きゅ", "kye": "きぇ", "kyo": "きょ",
    "gya": "ぎゃ", "gyu": "ぎゅ", "gyo": "ぎょ",
    "nya": "にゃ", "nyi": "にぃ", "nyu": "にゅ", "nye": "にぇ", "nyo": "にょ",
    "hya": "ひゃ", "hyu": "ひゅ", "hyo": "ひょ",
    "bya": "びゃ", "byu": "びゅ", "byo": "びょ",
    "pya": "ぴゃ", "pyu": "ぴゅ", "pyo": "ぴょ",
    "mya": "みゃ", "myu": "みゅ", "myo": "みょ",
    "rya": "りゃ", "ryu": "りゅ", "ryo": "りょ",
    "gyu": "ぎゅ",
    "xa": "ぁ", "xi": "ぃ", "xu": "ぅ", "xe": "ぇ", "xo": "ぉ",
    "la": "ぁ", "li": "ぃ", "lu": "ぅ", "le": "ぇ", "lo": "ぉ",
    "xya": "ゃ", "xyu": "ゅ", "xyo": "ょ",
    "lya": "ゃ", "lyu": "ゅ", "lyo": "ょ",
    "thi": "てぃ", "tha": "てゃ", "thu": "てゅ", "the": "てぇ", "tho": "てょ",
    "dhi": "でぃ", "dha": "でゃ", "dhu": "でゅ", "dhe": "でぇ", "dho": "でょ",
    "va": "ヴぁ", "vi": "ヴぃ", "vu": "ヴ", "ve": "ヴぇ", "vo": "ヴぉ", "v": "ヴ",
    "nn": "ん", "xn": "ん", "n'": "ん",
    "kwa": "くぁ", "kwi": "くぃ", "kwe": "くぇ", "kwo": "くぉ",
}

TABLE: Dict[str, str] = dict(CORE)
TABLE.update({
    "-": "ー", ".": "。", ",": "、", "?": "？", "!": "！", ":": "：",
    ";": "；", "[": "「", "]": "」",
})
for _d in "0123456789":
    TABLE[_d] = "０１２３４５６７８９"["0123456789".index(_d)]

MAX_KEY = max(len(k) for k in TABLE)


@dataclass
class KanaSpan:
    kana: str
    raw_range: Tuple[int, int]
    reading_range: Tuple[int, int]


@dataclass
class RomajiResult:
    kana: str
    spans: List[KanaSpan]
    unconsumed_start: Optional[int]
    raw: str


def convert_romaji(raw: str, start: int = 0, end: Optional[int] = None) -> RomajiResult:
    if end is None:
        end = len(raw)
    src = raw[start:end]
    out: List[str] = []
    spans: List[KanaSpan] = []

    def emit(text: str, raw_a: int, raw_b: int) -> None:
        reading_a = sum(len(x) for x in out)
        out.append(text)
        spans.append(
            KanaSpan(
                kana=text,
                raw_range=(start + raw_a, start + raw_b),
                reading_range=(reading_a, reading_a + len(text)),
            )
        )

    def try_table(i: int) -> Optional[int]:
        maxk = min(MAX_KEY, len(src) - i)
        for L in range(maxk, 0, -1):
            piece = src[i : i + L]
            if piece in TABLE:
                emit(TABLE[piece], i, i + L)
                return i + L
        return None

    i = 0
    while i < len(src):
        ch = src[i]
        if ch == "n":
            if i + 1 < len(src) and src[i + 1] == "n":
                if i + 2 < len(src) and src[i + 2] in VOWELS:
                    emit("ん", i, i + 1)
                    i += 1
                    continue
                emit("ん", i, i + 2)
                i += 2
                continue
            if i + 1 < len(src) and src[i + 1] == "'":
                emit("ん", i, i + 2)
                i += 2
                continue
            if i + 1 < len(src) and src[i + 1] in VOWELS:
                j = try_table(i)
                if j is not None:
                    i = j
                    continue
            if i + 1 < len(src) and src[i + 1] == "y":
                j = try_table(i)
                if j is not None:
                    i = j
                    continue
                emit("ん", i, i + 1)
                i += 1
                continue
            emit("ん", i, i + 1)
            i += 1
            continue
        j = try_table(i)
        if j is not None:
            i = j
            continue
        if ch.isalpha():
            return RomajiResult(
                kana="".join(out),
                spans=spans,
                unconsumed_start=start + i,
                raw=raw,
            )
        if ch in " \t":
            emit(ch, i, i + 1)
            i += 1
            continue
        return RomajiResult(
            kana="".join(out),
            spans=spans,
            unconsumed_start=start + i,
            raw=raw,
        )
    return RomajiResult(kana="".join(out), spans=spans, unconsumed_start=None, raw=raw)


def convert_full(raw: str) -> RomajiResult:
    return convert_romaji(raw, 0, len(raw))
