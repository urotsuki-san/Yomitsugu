"""Package the generated Yomitsugu artwork as a Windows multi-size icon."""
from pathlib import Path

from PIL import Image, ImageDraw


assets = Path(__file__).resolve().parent.parent / "assets"
source = assets / "yomitsugu-icon.png"
image = Image.open(source).convert("RGBA")
if image.width != image.height or image.width < 512:
    raise SystemExit("Expected a square source of at least 512 pixels")

sizes = [16, 24, 32, 48, 64, 128, 256]
icon = image.resize((256, 256), Image.Resampling.LANCZOS)
icon.save(assets / "yomitsugu.ico", format="ICO", sizes=[(s, s) for s in sizes])

# Human review at the actual taskbar sizes, with nearest-neighbor enlargement.
sheet = Image.new("RGB", (768, 304), "#e6e8eb")
draw = ImageDraw.Draw(sheet)
for index, size in enumerate([16, 24, 32, 48]):
    small = image.resize((size, size), Image.Resampling.LANCZOS)
    backdrop = Image.new("RGBA", (size, size), "white")
    backdrop.alpha_composite(small)
    display = backdrop.resize((144, 144), Image.Resampling.NEAREST)
    left = 26 + index * 190
    sheet.paste(display.convert("RGB"), (left, 48))
    draw.text((left, 208), f"{size} px", fill="#25313d")
sheet.save(assets / "yomitsugu-size-review.png")
print(assets / "yomitsugu.ico")
