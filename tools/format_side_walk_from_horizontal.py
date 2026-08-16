from collections import deque
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets" / "characters" / "player" / "player_walk_side_8.png"
BASE = ROOT / "art_source" / "player" / "player_walk_backup_20260805_134052.bmp"
OUT_PNG = ROOT / "art_source" / "player" / "player_walk_formatted.png"

FRAME = 96
GRID = 4


def is_generated_background(pixel):
    r, g, b = pixel[:3]
    return r >= 170 and b >= 145 and g <= 95 and abs(r - b) <= 100


def is_key(pixel):
    r, g, b = pixel[:3]
    return r >= 248 and g <= 8 and b >= 248


def component_boxes(mask):
    h = len(mask)
    w = len(mask[0])
    seen = [[False for _ in range(w)] for _ in range(h)]
    boxes = []

    for sy in range(h):
        for sx in range(w):
            if seen[sy][sx] or not mask[sy][sx]:
                continue

            q = deque([(sx, sy)])
            seen[sy][sx] = True
            min_x = max_x = sx
            min_y = max_y = sy
            count = 0

            while q:
                x, y = q.popleft()
                count += 1
                min_x = min(min_x, x)
                max_x = max(max_x, x)
                min_y = min(min_y, y)
                max_y = max(max_y, y)

                for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                    if nx < 0 or ny < 0 or nx >= w or ny >= h:
                        continue
                    if seen[ny][nx] or not mask[ny][nx]:
                        continue
                    seen[ny][nx] = True
                    q.append((nx, ny))

            if count > 500:
                boxes.append((count, min_x, min_y, max_x + 1, max_y + 1))

    return sorted(boxes, key=lambda box: box[1])


def source_to_rgba(source):
    rgb = source.convert("RGB")
    out = Image.new("RGBA", rgb.size, (0, 0, 0, 0))
    src = rgb.load()
    dst = out.load()
    mask = []

    for y in range(rgb.height):
        row = []
        for x in range(rgb.width):
            foreground = not is_generated_background(src[x, y])
            row.append(foreground)
            if foreground:
                dst[x, y] = (*src[x, y], 255)
        mask.append(row)

    return out, mask


def key_to_rgba(image):
    rgb = image.convert("RGB")
    out = Image.new("RGBA", rgb.size, (0, 0, 0, 0))
    src = rgb.load()
    dst = out.load()

    for y in range(rgb.height):
        for x in range(rgb.width):
            pixel = src[x, y]
            if not is_key(pixel):
                dst[x, y] = (*pixel, 255)

    return out


def extract_frames(source):
    rgba, mask = source_to_rgba(source)
    boxes = component_boxes(mask)
    if len(boxes) != 8:
        raise RuntimeError(f"expected 8 character components, found {len(boxes)}")

    pad = 8
    crops = []
    for _, left, top, right, bottom in boxes:
        box = (
            max(0, left - pad),
            max(0, top - pad),
            min(rgba.width, right + pad),
            min(rgba.height, bottom + pad),
        )
        crops.append(rgba.crop(box))

    max_w = max(crop.width for crop in crops)
    max_h = max(crop.height for crop in crops)
    scale = min(92 / max_w, 89 / max_h)
    baseline = 92

    frames = []
    for crop in crops:
        size = (max(1, round(crop.width * scale)), max(1, round(crop.height * scale)))
        scaled = crop.resize(size, Image.Resampling.LANCZOS)
        frame = Image.new("RGBA", (FRAME, FRAME), (0, 0, 0, 0))
        x = (FRAME - size[0]) // 2
        y = baseline - size[1]
        frame.alpha_composite(scaled, (x, y))
        frames.append(frame)

    return frames


def build_sheet(frames):
    sheet = Image.new("RGBA", (FRAME * GRID, FRAME * GRID), (0, 0, 0, 0))

    if BASE.exists():
        base = key_to_rgba(Image.open(BASE))
        front_back = base.crop((0, FRAME * 2, FRAME * GRID, FRAME * GRID))
        sheet.alpha_composite(front_back, (0, FRAME * 2))

    for index, frame in enumerate(frames):
        row = index // GRID
        col = index % GRID
        sheet.alpha_composite(frame, (col * FRAME, row * FRAME))

    return sheet


def save_outputs(sheet):
    OUT_PNG.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(OUT_PNG)

    print(f"wrote {OUT_PNG}")


def main():
    if not SOURCE.exists():
        raise FileNotFoundError(SOURCE)

    source = Image.open(SOURCE)
    frames = extract_frames(source)
    sheet = build_sheet(frames)
    save_outputs(sheet)


if __name__ == "__main__":
    main()
