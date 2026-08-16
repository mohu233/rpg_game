from pathlib import Path

from PIL import Image, ImageDraw


OUTPUT = Path(__file__).resolve().parents[1] / "assets" / "objects" / "special" / "ending_ritual_altar" / "ending_ritual_altar.png"


def main() -> None:
    image = Image.new("RGBA", (288, 240), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image, "RGBA")

    draw.ellipse((22, 177, 266, 229), fill=(10, 13, 17, 105))
    draw.polygon([(30, 172), (144, 111), (258, 172), (144, 231)], fill=(38, 42, 46, 255))
    draw.polygon([(30, 172), (144, 129), (258, 172), (144, 214)], fill=(69, 72, 72, 255))
    draw.line([(30, 172), (144, 111), (258, 172), (144, 231), (30, 172)], fill=(141, 126, 99, 255), width=4)

    draw.ellipse((57, 119, 231, 207), fill=(49, 46, 52, 255), outline=(173, 142, 91, 255), width=6)
    draw.ellipse((78, 132, 210, 194), fill=(22, 30, 38, 255), outline=(91, 183, 191, 255), width=5)
    draw.ellipse((96, 143, 192, 187), fill=(37, 85, 94, 220), outline=(176, 231, 218, 255), width=3)
    draw.polygon([(144, 148), (179, 165), (144, 183), (109, 165)], fill=(92, 38, 63, 255), outline=(250, 203, 101, 255))
    draw.ellipse((137, 158, 151, 172), fill=(225, 250, 232, 255))

    for x, y in ((61, 126), (205, 126), (61, 177), (205, 177)):
        draw.rectangle((x, y - 62, x + 22, y), fill=(49, 54, 59, 255), outline=(157, 135, 96, 255), width=3)
        draw.polygon([(x - 4, y - 62), (x + 11, y - 82), (x + 26, y - 62)], fill=(81, 45, 67, 255), outline=(226, 181, 91, 255))
        draw.rectangle((x + 8, y - 49, x + 14, y - 28), fill=(84, 184, 188, 255))

    for angle_point in ((144, 117), (224, 163), (144, 207), (64, 163)):
        x, y = angle_point
        draw.rectangle((x - 5, y - 5, x + 5, y + 5), fill=(232, 194, 105, 255))

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    image.save(OUTPUT)
    print(OUTPUT)


if __name__ == "__main__":
    main()
