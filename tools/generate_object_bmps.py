from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets" / "objects" / "nature"


def ellipse(draw, box, fill, outline, width=2):
    draw.ellipse(box, fill=fill, outline=outline, width=width)


def round_rect(draw, box, radius, fill, outline, width=2):
    draw.rounded_rectangle(box, radius=radius, fill=fill, outline=outline, width=width)


def tree_oak():
    im = Image.new("RGBA", (118, 132), (0, 0, 0, 0))
    draw = ImageDraw.Draw(im)

    round_rect(draw, (47, 68, 71, 123), 8, (126, 89, 51), (65, 49, 32), 3)
    draw.line((52, 83, 43, 62), fill=(87, 60, 35), width=3)
    draw.line((66, 86, 80, 62), fill=(87, 60, 35), width=3)

    ellipse(draw, (12, 18, 76, 82), (60, 128, 78), (31, 82, 50), 4)
    ellipse(draw, (41, 8, 109, 78), (74, 151, 88), (31, 82, 50), 4)
    ellipse(draw, (4, 48, 115, 122), (83, 164, 95), (31, 82, 50), 4)
    ellipse(draw, (37, 60, 123, 124), (91, 178, 104), (31, 82, 50), 3)

    ellipse(draw, (24, 36, 56, 62), (95, 181, 107), (95, 181, 107), 1)
    ellipse(draw, (66, 26, 98, 54), (105, 192, 114), (105, 192, 114), 1)
    return im


def stone_round():
    im = Image.new("RGBA", (62, 38), (0, 0, 0, 0))
    draw = ImageDraw.Draw(im)

    ellipse(draw, (0, 4, 61, 36), (124, 139, 146), (62, 77, 83), 3)
    ellipse(draw, (14, 8, 41, 25), (151, 165, 171), (151, 165, 171), 1)
    draw.arc((8, 8, 56, 34), 15, 155, fill=(83, 96, 101), width=2)
    return im


def bush():
    im = Image.new("RGBA", (74, 46), (0, 0, 0, 0))
    draw = ImageDraw.Draw(im)

    ellipse(draw, (2, 12, 42, 45), (64, 134, 77), (34, 83, 49), 3)
    ellipse(draw, (32, 6, 73, 45), (77, 154, 86), (34, 83, 49), 3)
    ellipse(draw, (14, 0, 62, 38), (91, 174, 95), (34, 83, 49), 3)
    ellipse(draw, (22, 10, 50, 30), (114, 194, 112), (114, 194, 112), 1)
    return im


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    images = {
        "tree_oak/tree_oak.png": tree_oak(),
        "stone_round/stone_round.png": stone_round(),
        "bush/bush.png": bush(),
    }
    for name, image in images.items():
        path = OUT / name
        path.parent.mkdir(parents=True, exist_ok=True)
        image.save(path)
        print(f"wrote {path}")


if __name__ == "__main__":
    main()
