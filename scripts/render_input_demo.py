"""打鍵を一文字ずつ描き、実際の変換候補でREADMEの入力例を作る。"""

from __future__ import annotations

import argparse
import json
from functools import lru_cache
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "docs/assets/readme"
SIZE = (1000, 510)
BG, PANEL, BORDER = "#f3f0e7", "#e8e4d9", "#c7c3b9"
CYAN, AMBER, TEXT, MUTED = "#b94f2f", "#b94f2f", "#192b35", "#777b78"
JP = "C:/Windows/Fonts/YuGothM.ttc"
MONO = "C:/Windows/Fonts/CascadiaMono.ttf"
FRAME_MS = 40
KEY_MS = 160

CASES = [
    (1, "日本語を続けて打つ", [
        ("kyou", "今日"), ("kyouha", "今日は"), ("kyouhaii", "今日はいい"),
        ("kyouhaiitennki", "今日はいい天気"),
        ("kyouhaiitennkide", "今日はいい天気で"),
        ("kyouhaiitennkidesu", "今日はいい天気です"),
        ("kyouhaiitennkidesune", "今日はいい天気ですね"),
        ("kyouhaiitennkidesune.", "今日はいい天気ですね。"),
    ]),
    (2, "英語を交えて打つ", [
        ("API", "API"), ("APIwo", "APIを"), ("APIwokakunin", "APIを確認"),
        ("APIwokakuninshi", "APIを確認し"),
        ("APIwokakuninshite", "APIを確認して"),
        ("APIwokakuninshiteku", "APIを確認してく"),
        ("APIwokakuninshitekuda", "APIを確認してくだ"),
        ("APIwokakuninshitekudasai", "APIを確認してください"),
        ("APIwokakuninshitekudasai.", "APIを確認してください。"),
    ]),
]


@lru_cache(maxsize=32)
def font(path: str, size: int):
    return ImageFont.truetype(path, size)


def verified_events(path: Path):
    data = path.read_text(encoding="utf-8-sig")
    if data.count("DEMO PASS") != len(CASES) or "DEMO FAIL" in data:
        raise ValueError("Demo conversion did not pass")
    result = {}
    for line in data.splitlines():
        if line.startswith("DEMO_FRAME\t"):
            _, case, phase, typed, candidate = line.split("\t", 4)
            if phase == "typing":
                result[int(case), typed] = candidate
    for case, _, states in CASES:
        for typed, candidate in states:
            if result.get((case, typed)) != candidate:
                raise ValueError(f"Candidate mismatch: {case}, {typed}")
    return result


def draw_frame(case, title, typed, candidate, previous, changed_ms, key_ms, elapsed, committed):
    image = Image.new("RGB", SIZE, BG)
    d = ImageDraw.Draw(image)
    d.rectangle((0, 0, 16, SIZE[1]), fill=TEXT)
    d.text((51, 32), "YOMITSUGU", font=font(MONO, 24), fill=TEXT)
    d.text((740, 37), "ROMAJI / JAPANESE", font=font(MONO, 15), fill=MUTED)
    d.line((52, 84, 951, 84), fill=BORDER, width=1)
    d.text((52, 108), "ローマ字を、ことばへ。", font=font(JP, 22), fill=TEXT)
    d.text((905, 99), f"0{case}", font=font(MONO, 37), fill=CYAN)

    face = font(JP, 49)
    prefix = 0
    while prefix < min(len(previous), len(candidate)) and previous[prefix] == candidate[prefix]:
        prefix += 1
    x, y = 52, 205
    old = candidate[:prefix]
    fresh = candidate[prefix:]
    d.text((x, y), old, font=face, fill=TEXT)
    fresh_x = x + d.textlength(old, font=face)
    d.text((fresh_x, y), fresh, font=face, fill=CYAN if changed_ms < 240 and not committed else TEXT)
    end_x = x + d.textlength(candidate, font=face)
    if candidate and not committed and changed_ms < 280:
        d.line((fresh_x, 275, end_x, 275), fill=CYAN, width=2)
    if not committed and (key_ms < 350 or elapsed % 1000 < 560):
        old_x = x + d.textlength(previous, font=face)
        amount = min(changed_ms / 120, 1)
        cursor_x = old_x + (end_x - old_x) * (1 - (1 - amount) ** 3)
        d.rectangle((cursor_x + 4, y + 9, cursor_x + 7, y + 58), fill=CYAN)

    d.line((52, 322, 951, 322), fill=TEXT, width=1)
    d.text((52, 344), "KEYSTROKES", font=font(MONO, 13), fill=MUTED)
    d.text((818, 341), "確定" if committed else "入力中", font=font(JP, 16), fill=CYAN)
    # 打鍵は全て表示する。候補の切り替えでは文字列全体をフェードさせない。
    keys = list(typed)
    key_x = 52
    for index, key in enumerate(keys):
        latest = index == len(keys) - 1 and key_ms < KEY_MS and not committed
        lift = int(7 * (1 - min(key_ms / 120, 1)) ** 2) if latest else 0
        if latest:
            d.rectangle((key_x - 3, 386 + lift, key_x + 23, 427 + lift), fill=CYAN)
        glyph = font(MONO, 24)
        width = d.textlength(key, font=glyph)
        d.text((key_x + (20 - width) / 2, 388 + lift), key, font=glyph, fill=BG if latest else TEXT)
        key_x += 28
    if committed:
        d.rectangle((814, 385, 950, 429), fill=TEXT)
        d.text((831, 392), "Enter", font=font(MONO, 20), fill=BG)
        d.line((929, 397, 929, 410, 912, 410), fill=BG, width=2)
        d.line((918, 404, 912, 410, 918, 416), fill=BG, width=2)
    d.line((52, 452, 951, 452), fill=BORDER, width=1)
    d.text((52, 467), title, font=font(JP, 16), fill=MUTED)
    d.text((732, 470), "YOMITSUGU  /  INPUT", font=font(MONO, 13), fill=MUTED)
    return image


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--log", type=Path, default=ROOT / "audit/2026-09-26/readme-demo/engine-demo.out.txt")
    args = parser.parse_args()
    records = verified_events(args.log)
    frames, durations, samples = [], [], []
    still = None
    for case, title, states in CASES:
        raw = states[-1][0]
        checkpoints = {typed: value for typed, value in states}
        candidate = previous = ""
        last_change = 0
        length_ms = 480 + len(raw) * KEY_MS + 1760
        previous_count = 0
        for elapsed in range(0, length_ms, FRAME_MS):
            count = min(len(raw), max(0, (elapsed - 480) // KEY_MS + 1))
            typed = raw[:count]
            key_at = 480 + (count - 1) * KEY_MS if count else 0
            if count != previous_count:
                value = checkpoints.get(typed)
                if value is None and count < len(states[0][0]):
                    value = records[case, typed]
                if value is not None and value != candidate:
                    previous, candidate, last_change = candidate, value, elapsed
                previous_count = count
            committed = elapsed >= 480 + len(raw) * KEY_MS + 560
            frame = draw_frame(case, title, typed, candidate, previous, elapsed - last_change,
                               elapsed - key_at, elapsed, committed)
            frames.append(frame)
            durations.append(FRAME_MS)
            if count == len(raw) and committed and still is None and case == 1:
                still = frame
            if elapsed in (1120, 2240, 3360, length_ms - 40):
                samples.append(frame.resize((500, 255)))
    # 全フレームで同じパレットを使い、背景色のちらつきを防ぐ。
    palette_source = Image.new("RGB", (SIZE[0], SIZE[1] * len(CASES)))
    palette_source.paste(frames[0], (0, 0))
    palette_source.paste(frames[len(frames) // 2], (0, SIZE[1]))
    palette_source.paste(frames[-1].crop((0, 200, 1000, 470)), (0, 510))
    palette = palette_source.quantize(colors=192, method=Image.Quantize.MEDIANCUT)
    indexed = [frame.quantize(palette=palette, dither=Image.Dither.NONE) for frame in frames]
    ASSETS.mkdir(parents=True, exist_ok=True)
    still.save(ASSETS / "input-demo-still.png", optimize=True)
    indexed[0].save(ASSETS / "input-demo.gif", save_all=True, append_images=indexed[1:],
                    duration=durations, loop=0, optimize=True, disposal=1)
    sheet = Image.new("RGB", (1000, 255 * ((len(samples) + 1) // 2)), BG)
    for index, frame in enumerate(samples):
        sheet.paste(frame, ((index % 2) * 500, (index // 2) * 255))
    proof = args.log.parent / "demo-contact-sheet.png"
    sheet.save(proof)
    report = {"frames": len(frames), "duration_ms": sum(durations), "frame_ms": FRAME_MS,
              "key_interval_ms": KEY_MS, "typed_keys": sum(len(case[2][-1][0]) for case in CASES),
              "verified_candidate_states": sum(len(case[2]) for case in CASES),
              "gif_bytes": (ASSETS / "input-demo.gif").stat().st_size}
    (args.log.parent / "demo-render.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report))
    print(proof)


if __name__ == "__main__":
    main()
