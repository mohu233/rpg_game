from collections import deque
from pathlib import Path

from PIL import Image, ImageOps


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets" / "player_walk.png"
OUT_PNG = ROOT / "assets" / "player_walk_formatted.png"
OUT_BMP = ROOT / "assets" / "player_walk.bmp"

FRAME = 96
GRID = 4
KEY = (255, 0, 255)


def is_background(pixel):
    r, g, b = pixel[:3]
    return r >= 226 and g >= 226 and b >= 226 and max(pixel[:3]) - min(pixel[:3]) <= 62


def foreground_from_cell(cell):
    rgb = cell.convert("RGB")
    w, h = rgb.size
    pix = rgb.load()
    bg = [[False for _ in range(w)] for _ in range(h)]
    q = deque()

    def push(x, y):
        if x < 0 or y < 0 or x >= w or y >= h or bg[y][x]:
            return
        if is_background(pix[x, y]):
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

    fg = [[not bg[y][x] for x in range(w)] for y in range(h)]
    keep_largest_component(fg)

    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    out_pix = out.load()
    for y in range(h):
        for x in range(w):
            if fg[y][x]:
                out_pix[x, y] = (*pix[x, y], 255)
    return out


def keep_largest_component(mask):
    h = len(mask)
    w = len(mask[0])
    seen = [[False for _ in range(w)] for _ in range(h)]
    best = []

    for sy in range(h):
        for sx in range(w):
            if seen[sy][sx] or not mask[sy][sx]:
                continue
            comp = []
            q = deque([(sx, sy)])
            seen[sy][sx] = True
            while q:
                x, y = q.popleft()
                comp.append((x, y))
                for nx in (x - 1, x, x + 1):
                    for ny in (y - 1, y, y + 1):
                        if nx < 0 or ny < 0 or nx >= w or ny >= h:
                            continue
                        if seen[ny][nx] or not mask[ny][nx]:
                            continue
                        seen[ny][nx] = True
                        q.append((nx, ny))
            if len(comp) > len(best):
                best = comp

    for y in range(h):
        for x in range(w):
            mask[y][x] = False
    for x, y in best:
        mask[y][x] = True


def content_bbox(img):
    alpha = img.getchannel("A")
    bbox = alpha.getbbox()
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


def normalize_frame(img, mirror=False):
    if mirror:
        img = ImageOps.mirror(img)
    crop = img.crop(content_bbox(img))
    max_w = 86
    max_h = 89
    scale = min(max_w / crop.width, max_h / crop.height)
    size = (max(1, round(crop.width * scale)), max(1, round(crop.height * scale)))
    crop = crop.resize(size, Image.Resampling.LANCZOS)

    canvas = Image.new("RGBA", (FRAME, FRAME), (0, 0, 0, 0))
    x = (FRAME - size[0]) // 2
    y = FRAME - size[1] - 3
    canvas.alpha_composite(crop, (x, y))
    return canvas


def extract(source, row, col):
    cell_w = source.width // GRID
    cell_h = source.height // GRID
    cell = source.crop((col * cell_w, row * cell_h, (col + 1) * cell_w, (row + 1) * cell_h))
    return foreground_from_cell(cell)


def main():
    source = Image.open(SOURCE).convert("RGB")
    cells = [[extract(source, row, col) for col in range(GRID)] for row in range(GRID)]

    # Output rows match the runtime contract:
    # row 0-1: right-facing side walk, 8 frames total
    # row 2: front/down walk, 4 frames
    # row 3: back/up walk, 4 frames
    rows = [
        [(0, 0, False), (0, 1, False), (0, 2, False), (0, 3, False)],
        [(1, 0, False), (1, 1, False), (1, 2, False), (1, 3, False)],
        [(2, 0, False), (2, 1, False), (2, 2, False), (2, 3, False)],
        [(3, 0, False), (3, 1, False), (3, 2, False), (3, 3, False)],
    ]

    sheet = Image.new("RGBA", (FRAME * GRID, FRAME * GRID), (0, 0, 0, 0))
    for out_row, frames in enumerate(rows):
        for out_col, (src_row, src_col, mirror) in enumerate(frames):
            frame = normalize_frame(cells[src_row][src_col], mirror=mirror)
            sheet.alpha_composite(frame, (out_col * FRAME, out_row * FRAME))

    sheet.save(OUT_PNG)

    keyed = Image.new("RGB", sheet.size, KEY)
    keyed.paste(sheet.convert("RGB"), mask=sheet.getchannel("A"))
    keyed.save(OUT_BMP)

    print(f"wrote {OUT_PNG}")
    print(f"wrote {OUT_BMP}")


if __name__ == "__main__":
    main()
