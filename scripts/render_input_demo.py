"""Render a readable README animation from a native engine candidate log.

The source log is produced by debug_dump --readme-demo without desktop input.
Each displayed candidate is checked against a real engine result. The animation
omits transient frames for legibility.
"""

from __future__ import annotations

from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
LOG = ROOT / "audit/2026-09-26/readme-demo/engine-demo.out.txt"
ASSETS = ROOT / "docs/assets/readme"
SIZE = (1000, 510)

BG = "#08141e"
PANEL = "#10212e"
BORDER = "#294657"
CYAN = "#3de0e5"
AMBER = "#ffbd68"
TEXT = "#f8f4e9"
MUTED = "#98abb6"

JP_FONT = Path("C:/Windows/Fonts/YuGothM.ttc")
LATIN_FONT = Path("C:/Windows/Fonts/CascadiaMono.ttf")
UI_FONT = Path("C:/Windows/Fonts/seguisb.ttf")


def font(path: Path, size: int) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(str(path), size)


def events() -> dict[tuple[int, str], str]:
    raw = LOG.read_text(encoding="utf-8-sig")
    if raw.count("DEMO PASS") != 2 or "DEMO FAIL" in raw:
        raise ValueError("Both native-engine demo cases must pass before rendering")
    result: dict[tuple[int, str], str] = {}
    for line in raw.splitlines():
        if not line.startswith("DEMO_FRAME\t"):
            continue
        _, case, phase, typed, candidate = line.split("\t", 4)
        if phase in ("typing", "committed"):
            result[int(case), typed] = candidate.strip("\r\n")
    return result


def exact(log: dict[tuple[int, str], str], case: int, typed: str, candidate: str):
    actual = log.get((case, typed))
    if actual != candidate:
        raise ValueError(f"Missing exact E2E frame: {case} {typed!r}: {actual!r}")
    return case, typed, candidate


def fitting(draw: ImageDraw.ImageDraw, value: str, path: Path, start: int, width: int):
    for size in range(start, 20, -1):
        face = font(path, size)
        if draw.textbbox((0, 0), value, font=face)[2] <= width:
            return face
    raise ValueError(f"Text does not fit: {value}")


def render(case: int, typed: str, candidate: str, *, complete: bool, step: int, total: int):
    image = Image.new("RGB", SIZE, BG)
    d = ImageDraw.Draw(image)
    # READMEの配色に合わせ、文字の周囲に装飾を置く。
    d.rectangle((0, 0, 10, SIZE[1]), fill=CYAN)
    d.rectangle((40, 38, 54, 52), fill=AMBER)
    d.text((70, 27), "YOMITSUGU", font=font(LATIN_FONT, 31), fill=TEXT)
    d.text((726, 39), "INPUT DEMO  /  0%d" % case, font=font(LATIN_FONT, 17), fill=CYAN)
    d.line((40, 91, 960, 91), fill=BORDER, width=2)

    d.text((42, 115), "入力したキー", font=font(JP_FONT, 22), fill=MUTED)
    d.rounded_rectangle((40, 153, 960, 250), radius=15, fill=PANEL, outline=BORDER, width=2)
    typed_face = fitting(d, typed, LATIN_FONT, 30, 855)
    d.text((67, 181), typed, font=typed_face, fill=CYAN)
    cursor_x = 67 + d.textlength(typed, font=typed_face) + 7
    if not complete and cursor_x < 938:
        d.rounded_rectangle((cursor_x, 181, cursor_x + 3, 218), radius=1, fill=CYAN)

    label = "確定した文字" if complete else "入力中の候補"
    d.text((42, 278), label, font=font(JP_FONT, 22), fill=MUTED)
    d.rounded_rectangle((40, 316, 960, 427), radius=15, fill=PANEL, outline=AMBER if complete else BORDER, width=2)
    candidate_face = fitting(d, candidate, JP_FONT, 43, 855)
    d.text((67, 342), candidate, font=candidate_face, fill=TEXT)
    if complete:
        d.rounded_rectangle((851, 278, 959, 308), radius=12, fill="#233e39")
        d.text((865, 281), "Enter  確定", font=font(JP_FONT, 15), fill=CYAN)

    d.text((42, 460), "変換エンジンの実測候補から抜粋", font=font(JP_FONT, 17), fill=MUTED)
    d.text((816, 460), f"{step:02d} / {total:02d}", font=font(LATIN_FONT, 18), fill=AMBER)
    return image


def main():
    log = events()
    first = [
        ("kyou", "今日"),
        ("kyouha", "今日は"),
        ("kyouhaii", "今日はいい"),
        ("kyouhaiitennki", "今日はいい天気"),
        ("kyouhaiitennkide", "今日はいい天気で"),
        ("kyouhaiitennkidesu", "今日はいい天気です"),
        ("kyouhaiitennkidesune", "今日はいい天気ですね"),
        ("kyouhaiitennkidesune.", "今日はいい天気ですね。"),
    ]
    second = [
        ("API", "API"),
        ("APIwo", "APIを"),
        ("APIwokakunin", "APIを確認"),
        ("APIwokakuninshi", "APIを確認し"),
        ("APIwokakuninshite", "APIを確認して"),
        ("APIwokakuninshitekuda", "APIを確認してくだ"),
        ("APIwokakuninshitekudasai", "APIを確認してください"),
        ("APIwokakuninshitekudasai.", "APIを確認してください。"),
    ]
    keyframes = []
    keyframe_durations = []
    total = len(first) + len(second)
    step = 0
    still = None
    for case, series in ((1, first), (2, second)):
        for index, (typed, candidate) in enumerate(series):
            step += 1
            exact(log, case, typed, candidate)
            complete = index == len(series) - 1
            frame = render(case, typed, candidate, complete=complete, step=step, total=total)
            keyframes.append(frame)
            keyframe_durations.append(1250 if complete else 210)
            if case == 2 and complete:
                still = frame
    assert still is not None
    frames = []
    durations = []
    for index, frame in enumerate(keyframes):
        frames.append(frame)
        durations.append(keyframe_durations[index])
        if index + 1 == len(keyframes):
            continue
        following = keyframes[index + 1]
        # 表示の切り替えに短いフェードを入れ、打鍵間の動きをつなぐ。
        for fraction in (0.25, 0.5, 0.75):
            frames.append(Image.blend(frame, following, fraction))
            durations.append(55)
    ASSETS.mkdir(parents=True, exist_ok=True)
    still.save(ASSETS / "input-demo-still.png", optimize=True)
    frames[0].save(
        ASSETS / "input-demo.gif",
        save_all=True,
        append_images=frames[1:],
        duration=durations,
        loop=0,
        optimize=True,
        disposal=2,
    )
    print(f"Rendered {len(frames)} frames from {len(keyframes)} recorded engine states")


if __name__ == "__main__":
    main()
