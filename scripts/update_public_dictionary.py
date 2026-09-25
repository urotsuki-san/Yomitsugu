"""Build a local IME lexicon from fixed public sources; no typed text is uploaded.

Run with --package-output during packaging, or without it to update the current
user's cached public dictionary. The cache is separate from user_dictionary.tsv.
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile
from urllib.request import Request, urlopen


SOURCES = {
    "mozc_symbols": "https://raw.githubusercontent.com/google/mozc/master/src/data/symbol/symbol.tsv",
    "edrdg_edict2": "https://www.edrdg.org/pub/Nihongo/edict2u.gz",
}
MAX_SOURCE_BYTES = {"mozc_symbols": 2_000_000, "edrdg_edict2": 20_000_000}
ROOT = Path(__file__).resolve().parent.parent
PACKAGE_OUTPUT = ROOT / "native/assets/public_dictionary.tsv"
USER_OUTPUT = Path(os.environ["LOCALAPPDATA"]) / "ImeMixed/public_dictionary.tsv"


def download(name: str) -> bytes:
    request = Request(SOURCES[name], headers={"User-Agent": "ImeMixed-DictionaryUpdater/0.2"})
    with urlopen(request, timeout=45) as response:
        data = response.read(MAX_SOURCE_BYTES[name] + 1)
    if len(data) > MAX_SOURCE_BYTES[name]:
        raise ValueError(f"Source too large: {name}")
    return data


def is_kana_reading(value: str) -> bool:
    return 2 <= len(value) <= 30 and all("ぁ" <= c <= "ゖ" or c == "ー" for c in value)


def to_hiragana(value: str) -> str:
    return "".join(chr(ord(c) - 0x60) if "ァ" <= c <= "ヶ" else c for c in value)


def lexical_entries(head: str, glosses: str):
    """EDICT2 headwords, not English gloss translations. Respect reading restrictions.

    Supplement loanwords and common words, leaving inflection and word costs to
    the main converter. Obsolete/irregular forms are not correction targets.
    """
    match = re.fullmatch(r"(.+?) \[([^]]+)\]", head)
    forms = (match.group(1) if match else head).split(';')
    readings = match.group(2).split(';') if match else forms
    common = '(P)' in glosses or '(P)' in head
    noun = bool(re.search(r'\(n(?:[,)-]|$)', glosses))
    for form in forms:
        word = re.sub(r'\([^)]*\)', '', form).strip()
        loanword = bool(re.fullmatch(r'[ァ-ヶー・]{2,30}', word))
        if not loanword and not common and not noun:
            continue
        if re.search(r'\((?:ik|iK|io|oK)\)', form):
            continue
        if re.search(r'\((?:arch|obs|obsc)\)', glosses) and not common:
            continue
        for annotated in readings:
            annotations = re.findall(r'\(([^)]*)\)', annotated)
            if any(flag in {'ik','ok','gikun'} for flag in annotations):
                continue
            restrictions = [flag for flag in annotations if flag not in {'P'}]
            if restrictions and not any(word in flag.split(',') for flag in restrictions):
                continue
            reading = to_hiragana(re.sub(r'\([^)]*\)', '', annotated).strip())
            if is_kana_reading(reading):
                yield reading, word, '補助語'


def english_entries(head: str, glosses: str):
    forms = head.split(' [', 1)[0].split(';')
    if '{comp}' not in glosses and not any(re.fullmatch(r'[ァ-ヶー・]{2,30}', re.sub(r'\([^)]*\)', '', form)) for form in forms):
        return
    for gloss in glosses.split('/'):
        word = re.sub(r'\([^)]*\)|\{[^}]*\}', '', gloss).strip()
        if re.fullmatch(r'[a-z][a-z-]{2,28}[a-z]', word):
            yield word, word, '英単語'
            if '-' in word:
                yield word.replace('-', ''), word.replace('-', ''), '英単語'


def build_rows(symbol_data: bytes, edict_data: bytes) -> tuple[list[tuple[str, str, str]], dict[str, int]]:
    rows: dict[str, list[tuple[str, str]]] = {}

    def add(reading: str, surface: str, pos: str) -> None:
        valid_reading = is_kana_reading(reading) or (pos == '英単語' and reading == surface and re.fullmatch(r'[a-z][a-z-]{2,28}[a-z]', reading))
        if not valid_reading or not surface or len(surface) > 48 or '\ufffd' in surface:
            return
        values = rows.setdefault(reading, [])
        for index, (existing, old_pos) in enumerate(values):
            if existing == surface:
                if old_pos == '補助語' and pos != '補助語':
                    values[index] = (surface, pos)
                return
        if len(values) < 32:
            values.append((surface, pos))

    term_count = 0
    lexical_count = 0
    import io
    with gzip.GzipFile(fileobj=io.BytesIO(edict_data)) as compressed:
        expanded = compressed.read(128 * 1024 * 1024 + 1)
    if len(expanded) > 128 * 1024 * 1024:
        raise ValueError('Expanded dictionary exceeds size limit')
    glossary = expanded.decode("utf-8-sig")
    for line in glossary.splitlines():
        head, sep, glosses = line.partition(" /")
        if not sep:
            continue
        for reading, word, pos in lexical_entries(head, glosses):
            before = len(rows.get(reading, []))
            add(reading, word, pos)
            lexical_count += len(rows.get(reading, [])) > before
        for reading, word, pos in english_entries(head, glosses):
            add(reading, word, pos)
        if "{comp}" not in line:
            continue
        reading_match = re.search(r"\[([^]]+)\]", head)
        reading = reading_match.group(1) if reading_match else head.split(";")[0].strip()
        reading = to_hiragana(reading)
        if not is_kana_reading(reading):
            continue
        for gloss in glosses.split("/"):
            word = re.sub(r"\([^)]*\)|\{[^}]*\}", "", gloss).strip()
            if re.fullmatch(r"[A-Z][A-Z0-9+._-]{1,30}", word):
                before = len(rows.get(reading, []))
                add(reading, word, "名詞")
                term_count += len(rows.get(reading, [])) > before

    symbol_count = 0
    for line in symbol_data.decode("utf-8-sig").splitlines()[1:]:
        columns = line.split("\t")
        if len(columns) < 3:
            continue
        surface = columns[1]
        for reading in columns[2].split():
            before = len(rows.get(reading, []))
            add(reading, surface, "記号")
            symbol_count += len(rows.get(reading, [])) > before

    flat = [(reading, word, pos) for reading, values in sorted(rows.items())
            for word, pos in sorted(values, key=lambda item: item[1] == '補助語')]
    symbol_count = sum(pos == '記号' for _, _, pos in flat)
    term_count = sum(pos == '名詞' for _, _, pos in flat)
    lexical_count = sum(pos == '補助語' for _, _, pos in flat)
    if not (500 <= symbol_count <= 20000 and 10 <= term_count <= 10000 and 20000 <= lexical_count <= 650000):
        raise ValueError(f"Source coverage changed: symbols={symbol_count}, terms={term_count}")
    required = {("りーどみー", "README"), ("やじるし", "→"), ("まる", "○")}
    if not required.issubset({(reading, word) for reading, word, _ in flat}):
        raise ValueError("Public dictionary is missing expected entries")
    return flat, {"readings": sum(is_kana_reading(key) for key in rows), "english_words": sum(pos == '英単語' for _, _, pos in flat), "symbols": symbol_count, "computing_terms": term_count,
                  "lexical_entries": lexical_count, "entries": len(flat)}


def atomic_write(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix=path.name + ".", delete=False) as temp:
        temp.write(payload)
        temp_path = Path(temp.name)
    try:
        os.replace(temp_path, path)
    finally:
        temp_path.unlink(missing_ok=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package-output", action="store_true", help="Write the source-tree bundled dictionary")
    args = parser.parse_args()
    sources = {name: download(name) for name in SOURCES}
    rows, counts = build_rows(sources["mozc_symbols"], sources["edrdg_edict2"])
    output = PACKAGE_OUTPUT if args.package_output else USER_OUTPUT
    payload = ("# Sources: Mozc symbol.tsv (BSD-3-Clause); EDRDG EDICT2 (CC BY-SA 4.0)\n"
               + "".join(f"{reading}\t{word}\t{pos}\n" for reading, word, pos in rows)).encode("utf-8")
    if len(payload) > 32 * 1024 * 1024:
        raise ValueError("Generated dictionary exceeds engine limit")
    atomic_write(output, payload)
    metadata = {"format_version": 2, "source_urls": SOURCES, "source_sha256": {name: hashlib.sha256(data).hexdigest() for name, data in sources.items()},
                "output_sha256": hashlib.sha256(payload).hexdigest(), **counts}
    atomic_write(output.with_suffix(".sources.json"), (json.dumps(metadata, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))
    print(f"Updated public dictionary: {output}")
    print(json.dumps(counts, ensure_ascii=False))


if __name__ == "__main__":
    main()
