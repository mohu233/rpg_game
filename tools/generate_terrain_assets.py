from __future__ import annotations

import math
import random
from pathlib import Path

from PIL import Image, ImageDraw


TILE = 48
ROOT = Path(__file__).resolve().parents[1] / "assets" / "terrain"


def clamp(value: float) -> int:
    return max(0, min(255, round(value)))


def periodic_noise(x: int, y: int, seed: int) -> float:
    phase = seed * 0.731
    x_angle = math.tau * x / TILE
    y_angle = math.tau * y / TILE
    return (
        math.sin(x_angle + phase) * 0.45
        + math.cos(y_angle * 2.0 - phase * 0.7) * 0.25
        + math.sin((x_angle + y_angle) * 3.0 + phase * 1.3) * 0.18
        + math.cos((x_angle - y_angle) * 5.0 - phase) * 0.12
    )


def texture(base: tuple[int, int, int], spread: tuple[int, int, int], seed: int) -> Image.Image:
    image = Image.new("RGBA", (TILE, TILE))
    pixels = image.load()
    for y in range(TILE):
        for x in range(TILE):
            noise = periodic_noise(x, y, seed)
            pixels[x, y] = (
                clamp(base[0] + spread[0] * noise),
                clamp(base[1] + spread[1] * noise),
                clamp(base[2] + spread[2] * noise),
                255,
            )
    return image


def wrapped_line(draw: ImageDraw.ImageDraw, points: list[tuple[int, int]], fill: tuple[int, int, int, int], width: int = 1) -> None:
    for ox in (-TILE, 0, TILE):
        for oy in (-TILE, 0, TILE):
            draw.line([(x + ox, y + oy) for x, y in points], fill=fill, width=width)


def wrapped_ellipse(draw: ImageDraw.ImageDraw, box: tuple[int, int, int, int], fill: tuple[int, int, int, int]) -> None:
    for ox in (-TILE, 0, TILE):
        for oy in (-TILE, 0, TILE):
            draw.ellipse((box[0] + ox, box[1] + oy, box[2] + ox, box[3] + oy), fill=fill)


def make_grass(detail_seed: int) -> Image.Image:
    image = texture((105, 175, 112), (13, 19, 12), 11)
    draw = ImageDraw.Draw(image)
    rng = random.Random(detail_seed)
    for _ in range(12):
        x, y = rng.randrange(6, TILE - 6), rng.randrange(6, TILE - 6)
        color = rng.choice(((65, 132, 76, 150), (139, 204, 137, 150), (81, 151, 87, 150)))
        draw.line([(x, y + 3), (x - 1, y - 2)], fill=color)
        draw.line([(x, y + 3), (x + 2, y - 3)], fill=color)
    return image


def make_dirt(detail_seed: int) -> Image.Image:
    image = texture((145, 104, 70), (20, 17, 13), 23)
    draw = ImageDraw.Draw(image)
    rng = random.Random(detail_seed)
    for _ in range(16):
        x, y = rng.randrange(5, TILE - 5), rng.randrange(5, TILE - 5)
        radius = rng.choice((1, 1, 2))
        color = rng.choice(((104, 72, 52, 170), (183, 139, 91, 170), (128, 86, 58, 160)))
        draw.ellipse((x - radius, y - radius, x + radius + 1, y + radius), fill=color)
    return image


def make_sand(detail_seed: int) -> Image.Image:
    image = texture((211, 190, 126), (16, 14, 10), 37)
    draw = ImageDraw.Draw(image)
    rng = random.Random(detail_seed)
    for _ in range(12):
        x, y = rng.randrange(5, TILE - 5), rng.randrange(5, TILE - 5)
        color = rng.choice(((177, 153, 94, 130), (237, 220, 157, 150)))
        draw.ellipse((x - 1, y - 1, x + 1, y + 1), fill=color)
    for y in (11, 31):
        wrapped_line(draw, [(5, y), (14, y - 1), (22, y)], (179, 157, 99, 85))
    return image


def make_gravel(detail_seed: int) -> Image.Image:
    image = texture((126, 132, 128), (16, 17, 16), 41)
    draw = ImageDraw.Draw(image)
    rng = random.Random(detail_seed)
    for _ in range(18):
        x, y = rng.randrange(5, TILE - 5), rng.randrange(5, TILE - 5)
        rx, ry = rng.choice(((2, 1), (2, 2), (3, 2)))
        color = rng.choice(((89, 96, 92, 190), (159, 164, 157, 200), (111, 118, 114, 180)))
        draw.ellipse((x - rx, y - ry, x + rx, y + ry), fill=color)
    return image


def make_stone_floor() -> Image.Image:
    image = texture((151, 157, 153), (8, 9, 8), 53)
    draw = ImageDraw.Draw(image)
    seam = (87, 95, 92, 255)
    highlight = (181, 185, 180, 170)
    draw.rectangle((0, 0, TILE - 1, TILE - 1), outline=seam, width=2)
    draw.line((0, 24, 48, 24), fill=seam, width=2)
    draw.line((24, 0, 24, 24), fill=seam, width=2)
    draw.line((12, 24, 12, 48), fill=seam, width=2)
    draw.line((1, 2, 22, 2), fill=highlight)
    draw.line((25, 2, 46, 2), fill=highlight)
    draw.line((1, 26, 10, 26), fill=highlight)
    draw.line((14, 26, 46, 26), fill=highlight)
    return image


def make_wood_floor() -> Image.Image:
    image = texture((164, 109, 66), (13, 10, 7), 67)
    draw = ImageDraw.Draw(image)
    seam = (91, 55, 38, 255)
    light = (192, 137, 82, 125)
    for y in (0, 12, 24, 36, 47):
        draw.line((0, y, 47, y), fill=seam, width=2 if y in (0, 47) else 1)
    for y, joins in ((0, (18,)), (12, (35,)), (24, (11,)), (36, (29,))):
        for x in joins:
            draw.line((x, y, x, min(47, y + 12)), fill=seam)
    for y in (5, 18, 30, 42):
        draw.arc((6, y - 2, 31, y + 4), 190, 350, fill=light)
    return image


def smoothstep(value: float) -> float:
    value = max(0.0, min(1.0, value))
    return value * value * (3.0 - 2.0 * value)


def make_mask(direction: str) -> Image.Image:
    image = Image.new("RGBA", (TILE, TILE))
    pixels = image.load()
    for y in range(TILE):
        for x in range(TILE):
            along = y if direction in ("left", "right") else x
            distance = {
                "left": x,
                "right": TILE - 1 - x,
                "top": y,
                "bottom": TILE - 1 - y,
            }[direction]
            curve = 13.0 + math.sin(math.tau * along / TILE + 0.8) * 3.4
            curve += math.sin(math.tau * along * 3.0 / TILE - 1.1) * 1.8
            alpha = 1.0 - smoothstep((distance - curve + 2.5) / 5.0)
            value = clamp(alpha * 255.0)
            pixels[x, y] = (value, value, value, 255)
    return image


def save(relative: str, image: Image.Image) -> None:
    path = ROOT / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, format="PNG", optimize=True)


def main() -> None:
    natural_generators = {
        "grass": (make_grass, 1103),
        "dirt": (make_dirt, 2307),
        "sand": (make_sand, 3711),
        "gravel": (make_gravel, 4113),
    }
    for terrain_id, (generator, seed) in natural_generators.items():
        for variant in range(4):
            suffix = "" if variant == 0 else f"_{variant}"
            save(f"natural/{terrain_id}/texture{suffix}.png", generator(seed + variant * 101))
    save("built/stone_floor/texture.png", make_stone_floor())
    save("built/wood_floor/texture.png", make_wood_floor())
    for direction in ("left", "right", "top", "bottom"):
        save(f"masks/{direction}.png", make_mask(direction))


if __name__ == "__main__":
    main()
