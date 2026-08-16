from collections import deque
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
KIT = ROOT / "art_source" / "reference" / "walk_generation_kit"
INPUT_DIR = KIT / "ai_outputs"
OUT_PNG = ROOT / "art_source" / "player" / "player_walk_formatted.png"

FRAME = 96

ORDER = [
    "01_S1_side_right_contact.png",
    "02_S2_side_right_recoil.png",
    "03_S3_side_right_passing.png",
    "04_S4_side_right_high.png",
    "05_S5_side_right_contact.png",
    "06_S6_side_right_recoil.png",
    "07_S7_side_right_passing.png",
    "08_S8_side_right_high.png",
    "09_D1_front_down_contact.png",
    "10_D2_front_down_recoil.png",
    "11_D3_front_down_contact.png",
    "12_D4_front_down_recoil.png",
    "13_U1_back_up_contact.png",
    "14_U2_back_up_recoil.png",
    "15_U3_back_up_contact.png",
    "16_U4_back_up_recoil.png",
]


def is_white(pixel):
    r, g, b = pixel[:3]
    return r >= 235 and g >= 235 and b >= 235 and max(pixel[:3]) - min(pixel[:3]) <= 35


def remove_edge_white(img):
    img = img.convert("RGBA")
    if img.getchannel("A").getbbox() and img.getchannel("A").getextrema()[0] < 255:
        return img

    rgb = img.convert("RGB")
    w, h = rgb.size
    src = rgb.load()
    bg = [[False for _ in range(w)] for _ in range(h)]
    q = deque()

    def push(x, y):
        if x < 0 or y < 0 or x >= w or y >= h or bg[y][x]:
            return
        if is_white(src[x, y]):
            bg[y][x] = True
            q.append((x, y))

    for x in range(w):
        push(x, 0)
        push(x, h - 1)
    for y in range(h):
        push(0, y)
        push(w - 1, y)

    while q:
        x, y = q.popleft()
        for nx in (x - 1, x, x + 1):
            for ny in (y - 1, y, y + 1):
                if nx == x and ny == y:
                    continue
                push(nx, ny)

    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    out_pix = out.load()
    for y in range(h):
        for x in range(w):
            if not bg[y][x]:
                out_pix[x, y] = (*src[x, y], 255)
    return out


def content_bbox(img):
    bbox = img.getchannel("A").getbbox()
    if bbox is None:
        return (0, 0, img.width, img.height)
    left, top, right, bottom = bbox
    pad = 10
    return (
        max(0, left - pad),
        max(0, top - pad),
        min(img.width, right + pad),
        min(img.height, bottom + pad),
    )


def normalize(img):
    img = remove_edge_white(img)
    crop = img.crop(content_bbox(img))
    max_w = 88
    max_h = 90
    scale = min(max_w / crop.width, max_h / crop.height)
    size = (max(1, round(crop.width * scale)), max(1, round(crop.height * scale)))
    crop = crop.resize(size, Image.Resampling.LANCZOS)

    canvas = Image.new("RGBA", (FRAME, FRAME), (0, 0, 0, 0))
    x = (FRAME - size[0]) // 2
    y = FRAME - size[1] - 3
    canvas.alpha_composite(crop, (x, y))
    return canvas


def main():
    missing = [name for name in ORDER if not (INPUT_DIR / name).exists()]
    if missing:
        print("Missing generated frames:")
        for name in missing:
            print(f"  {INPUT_DIR / name}")
        raise SystemExit(1)

    sheet = Image.new("RGBA", (FRAME * 4, FRAME * 4), (0, 0, 0, 0))
    for index, name in enumerate(ORDER):
        img = Image.open(INPUT_DIR / name)
        frame = normalize(img)
        x = (index % 4) * FRAME
        y = (index // 4) * FRAME
        sheet.alpha_composite(frame, (x, y))

    OUT_PNG.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(OUT_PNG)

    print(f"wrote {OUT_PNG}")


if __name__ == "__main__":
    main()
