"""外来語31例から、正しい入力と4種類の誤打鍵を生成する。"""
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
neighbors = {}
for row in ("qwertyuiop", "asdfghjkl", "zxcvbnm"):
    for i, character in enumerate(row):
        neighbors[character] = row[i+1] if i+1 < len(row) else row[i-1]

cases = []
for line in (root / "native/tests/conversion_cases.tsv").read_text(encoding="utf-8").splitlines():
    if not line.startswith("loanword\t"):
        continue
    _, raw, word = line.split("\t")
    positions = [i for i, c in enumerate(raw[:-1])
                 if c in neighbors and raw[i+1] in neighbors and c != raw[i+1]]
    position = min(positions, key=lambda i: abs(i-len(raw)//2))
    variants = {
        "valid": raw,
        "omission": raw[:position] + raw[position+1:],
        "duplicate": raw[:position] + raw[position] + raw[position:],
        "swap": raw[:position] + raw[position+1] + raw[position] + raw[position+2:],
        "neighbor": raw[:position] + neighbors[raw[position]] + raw[position+1:],
    }
    cases.extend(dict(index=f"{raw}/{kind}", raw=text, expected_output=[word])
                 for kind, text in variants.items())

output = root / "tests/generated_typo_conversion.json"
output.write_text(json.dumps(cases, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(f"Generated {len(cases)} cases")
