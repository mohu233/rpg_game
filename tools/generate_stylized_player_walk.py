from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
OUT_SHEET = ROOT / "art_source" / "player" / "player_walk_generated_sheet.png"
OUT_PNG = ROOT / "art_source" / "player" / "player_walk_formatted.png"

FRAME = 96
GRID = 4
SCALE = 4
KEY = (255, 0, 255)

OUTLINE = (10, 15, 26, 255)
FUR = (26, 54, 72, 255)
FUR_DARK = (15, 34, 50, 255)
FUR_LIGHT = (38, 74, 94, 255)
TEAL = (49, 190, 194, 255)
TEAL_DARK = (30, 139, 151, 255)
ORANGE = (255, 117, 73, 255)
ORANGE_DARK = (210, 75, 50, 255)
GOLD = (255, 195, 47, 255)
WHITE = (255, 247, 214, 255)
BLACK = (6, 8, 13, 255)


def s(value):
    return int(round(value * SCALE))


def sp(point):
    x, y = point
    return (s(x), s(y))


def sbox(box):
    return tuple(s(v) for v in box)


def ellipse(draw, box, fill, outline=OUTLINE, width=2):
    draw.ellipse(sbox(box), fill=fill, outline=outline, width=s(width))


def polygon(draw, points, fill, outline=OUTLINE, width=2):
    points = [sp(p) for p in points]
    draw.polygon(points, fill=fill)
    if outline:
        draw.line(points + [points[0]], fill=outline, width=s(width), joint="curve")


def capsule(draw, points, fill, width, outline=OUTLINE, outline_pad=2):
    scaled = [sp(p) for p in points]
    outer_width = s(width + outline_pad * 2)
    inner_width = s(width)

    if outline:
        draw.line(scaled, fill=outline, width=outer_width, joint="curve")
        radius = s(width / 2 + outline_pad)
        for x, y in scaled:
            draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=outline)

    draw.line(scaled, fill=fill, width=inner_width, joint="curve")
    radius = s(width / 2)
    for x, y in scaled:
        draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=fill)


def paw(draw, center, color=TEAL, w=11, h=7, outline=OUTLINE):
    x, y = center
    ellipse(draw, (x - w / 2, y - h / 2, x + w / 2, y + h / 2), color, outline, 2)


def eye(draw, cx, cy, looking="right"):
    ellipse(draw, (cx - 4.0, cy - 5.5, cx + 4.0, cy + 5.5), WHITE, OUTLINE, 1.4)
    ellipse(draw, (cx - 2.6, cy - 4.0, cx + 2.6, cy + 4.0), GOLD, None, 0)
    dx = 1.2 if looking == "right" else -1.2 if looking == "left" else 0
    ellipse(draw, (cx - 1.2 + dx, cy - 2.3, cx + 1.2 + dx, cy + 2.3), BLACK, None, 0)
    ellipse(draw, (cx - 0.2 + dx, cy - 2.0, cx + 0.8 + dx, cy - 1.0), WHITE, None, 0)


def draw_side_head(draw, dy):
    polygon(draw, [(42, 16 + dy), (47, 2 + dy), (54, 19 + dy)], FUR, OUTLINE, 2)
    polygon(draw, [(45, 15 + dy), (48, 6 + dy), (51, 17 + dy)], ORANGE, None, 0)
    polygon(draw, [(56, 17 + dy), (65, 4 + dy), (69, 25 + dy)], FUR, OUTLINE, 2)
    polygon(draw, [(59, 17 + dy), (64, 9 + dy), (66, 23 + dy)], ORANGE, None, 0)

    ellipse(draw, (32, 12 + dy, 68, 45 + dy), FUR, OUTLINE, 2.3)
    polygon(draw, [(36, 17 + dy), (42, 11 + dy), (41, 20 + dy), (48, 13 + dy), (47, 22 + dy)], FUR_DARK, None, 0)
    ellipse(draw, (47, 21 + dy, 70, 40 + dy), TEAL, None, 0)
    ellipse(draw, (40, 24 + dy, 62, 45 + dy), TEAL, None, 0)
    eye(draw, 58, 28 + dy, "right")
    ellipse(draw, (66, 32 + dy, 71, 36 + dy), BLACK, None, 0)
    draw.arc(sbox((59, 34 + dy, 68, 42 + dy)), 10, 70, fill=OUTLINE, width=s(1.3))


def draw_front_head(draw, dx, dy, face=True):
    polygon(draw, [(34 + dx, 18 + dy), (40 + dx, 2 + dy), (49 + dx, 22 + dy)], FUR, OUTLINE, 2)
    polygon(draw, [(37 + dx, 17 + dy), (40 + dx, 7 + dy), (45 + dx, 20 + dy)], ORANGE, None, 0)
    polygon(draw, [(62 + dx, 18 + dy), (56 + dx, 2 + dy), (47 + dx, 22 + dy)], FUR, OUTLINE, 2)
    polygon(draw, [(59 + dx, 17 + dy), (56 + dx, 7 + dy), (51 + dx, 20 + dy)], ORANGE, None, 0)

    ellipse(draw, (30 + dx, 10 + dy, 66 + dx, 47 + dy), FUR, OUTLINE, 2.3)
    polygon(draw, [(39 + dx, 13 + dy), (44 + dx, 9 + dy), (43 + dx, 17 + dy), (49 + dx, 10 + dy), (50 + dx, 18 + dy), (56 + dx, 13 + dy)], FUR_DARK, None, 0)

    if face:
        ellipse(draw, (30 + dx, 24 + dy, 48 + dx, 46 + dy), TEAL, None, 0)
        ellipse(draw, (48 + dx, 24 + dy, 66 + dx, 46 + dy), TEAL, None, 0)
        ellipse(draw, (38 + dx, 29 + dy, 58 + dx, 45 + dy), TEAL, None, 0)
        eye(draw, 40 + dx, 29 + dy, "right")
        eye(draw, 56 + dx, 29 + dy, "left")
        ellipse(draw, (46 + dx, 36 + dy, 50 + dx, 39 + dy), BLACK, None, 0)
        draw.arc(sbox((42 + dx, 36 + dy, 49 + dx, 43 + dy)), 20, 75, fill=OUTLINE, width=s(1.1))
        draw.arc(sbox((47 + dx, 36 + dy, 54 + dx, 43 + dy)), 105, 160, fill=OUTLINE, width=s(1.1))
    else:
        ellipse(draw, (33 + dx, 18 + dy, 63 + dx, 44 + dy), FUR_LIGHT, None, 0)
        polygon(draw, [(41 + dx, 14 + dy), (47 + dx, 8 + dy), (47 + dx, 18 + dy), (54 + dx, 11 + dy), (54 + dx, 21 + dy)], FUR_DARK, None, 0)


def draw_side_tail(draw, points, tip_t):
    capsule(draw, points, FUR, 9, OUTLINE, 2.4)
    if len(points) >= 2:
        a = points[-2]
        b = points[-1]
        tx = a[0] + (b[0] - a[0]) * tip_t
        ty = a[1] + (b[1] - a[1]) * tip_t
        capsule(draw, [(tx, ty), b], ORANGE, 8, OUTLINE, 1.6)


def draw_body(draw, dx, dy, side=False):
    if side:
        ellipse(draw, (37 + dx, 39 + dy, 62 + dx, 76 + dy), FUR, OUTLINE, 2.2)
        ellipse(draw, (44 + dx, 47 + dy, 60 + dx, 74 + dy), TEAL, None, 0)
        ellipse(draw, (40 + dx, 40 + dy, 55 + dx, 55 + dy), FUR_LIGHT, None, 0)
    else:
        ellipse(draw, (35 + dx, 39 + dy, 61 + dx, 78 + dy), FUR, OUTLINE, 2.2)
        ellipse(draw, (40 + dx, 47 + dy, 56 + dx, 76 + dy), TEAL, None, 0)
        ellipse(draw, (38 + dx, 39 + dy, 58 + dx, 54 + dy), FUR_LIGHT, None, 0)


SIDE_FRAMES = [
    {
        "bob": 0,
        "near_leg": [(50, 60), (61, 73), (67, 86)],
        "far_leg": [(45, 61), (35, 74), (27, 86)],
        "near_arm": [(42, 43), (37, 57), (34, 69)],
        "far_arm": [(52, 43), (59, 57), (62, 68)],
        "tail": [(59, 61), (72, 57), (77, 68), (69, 76)],
    },
    {
        "bob": 2,
        "near_leg": [(50, 62), (57, 75), (61, 87)],
        "far_leg": [(45, 63), (39, 76), (34, 87)],
        "near_arm": [(42, 45), (39, 60), (36, 70)],
        "far_arm": [(52, 45), (56, 58), (59, 68)],
        "tail": [(59, 63), (73, 60), (78, 70), (70, 77)],
    },
    {
        "bob": 1,
        "near_leg": [(50, 61), (48, 72), (51, 80)],
        "far_leg": [(45, 62), (45, 75), (46, 87)],
        "near_arm": [(42, 44), (45, 57), (48, 68)],
        "far_arm": [(52, 44), (52, 58), (50, 68)],
        "tail": [(59, 62), (72, 54), (79, 62), (74, 73)],
    },
    {
        "bob": -1,
        "near_leg": [(50, 59), (42, 70), (35, 83)],
        "far_leg": [(45, 60), (55, 72), (64, 86)],
        "near_arm": [(42, 42), (49, 55), (55, 66)],
        "far_arm": [(52, 42), (47, 56), (43, 68)],
        "tail": [(59, 60), (70, 52), (78, 57), (78, 69)],
    },
    {
        "bob": 0,
        "near_leg": [(50, 60), (38, 73), (30, 86)],
        "far_leg": [(45, 61), (57, 73), (67, 86)],
        "near_arm": [(42, 43), (58, 56), (63, 68)],
        "far_arm": [(52, 43), (38, 56), (34, 69)],
        "tail": [(59, 61), (69, 54), (78, 60), (75, 72)],
    },
    {
        "bob": 2,
        "near_leg": [(50, 62), (42, 75), (36, 87)],
        "far_leg": [(45, 63), (54, 76), (59, 87)],
        "near_arm": [(42, 45), (55, 58), (59, 68)],
        "far_arm": [(52, 45), (40, 59), (36, 70)],
        "tail": [(59, 63), (71, 57), (79, 64), (75, 74)],
    },
    {
        "bob": 1,
        "near_leg": [(50, 61), (50, 73), (48, 87)],
        "far_leg": [(45, 62), (45, 72), (42, 80)],
        "near_arm": [(42, 44), (51, 58), (51, 68)],
        "far_arm": [(52, 44), (45, 57), (48, 68)],
        "tail": [(59, 62), (73, 55), (80, 63), (75, 74)],
    },
    {
        "bob": -1,
        "near_leg": [(50, 59), (58, 71), (64, 83)],
        "far_leg": [(45, 60), (35, 72), (27, 86)],
        "near_arm": [(42, 42), (36, 56), (34, 68)],
        "far_arm": [(52, 42), (57, 55), (62, 66)],
        "tail": [(59, 60), (72, 57), (79, 68), (70, 76)],
    },
]


def draw_side_frame(index):
    frame = Image.new("RGBA", (FRAME * SCALE, FRAME * SCALE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(frame)
    data = SIDE_FRAMES[index]
    dy = data["bob"]

    draw_side_tail(draw, [(x, y + dy) for x, y in data["tail"]], 0.45)

    capsule(draw, [(x, y + dy) for x, y in data["far_leg"]], FUR_DARK, 7, OUTLINE, 1.7)
    paw(draw, (data["far_leg"][-1][0], data["far_leg"][-1][1] + dy), TEAL_DARK, 12, 6)
    capsule(draw, [(x, y + dy) for x, y in data["far_arm"]], FUR_DARK, 6, OUTLINE, 1.5)
    paw(draw, (data["far_arm"][-1][0], data["far_arm"][-1][1] + dy), TEAL_DARK, 8, 7)

    draw_body(draw, 0, dy, side=True)

    capsule(draw, [(x, y + dy) for x, y in data["near_leg"]], FUR, 7.5, OUTLINE, 1.8)
    paw(draw, (data["near_leg"][-1][0], data["near_leg"][-1][1] + dy), TEAL, 13, 7)
    capsule(draw, [(x, y + dy) for x, y in data["near_arm"]], FUR, 6.5, OUTLINE, 1.7)
    paw(draw, (data["near_arm"][-1][0], data["near_arm"][-1][1] + dy), TEAL, 8, 7)

    draw_side_head(draw, dy)
    return finalize_frame(frame)


FRONT_FRAMES = [
    {"dx": -1, "bob": 0, "left_foot": (39, 86), "right_foot": (57, 82), "left_hand": (31, 68), "right_hand": (62, 70), "tail": [(59, 62), (73, 58), (79, 70), (69, 78)]},
    {"dx": 0, "bob": 2, "left_foot": (42, 87), "right_foot": (55, 87), "left_hand": (34, 71), "right_hand": (61, 68), "tail": [(59, 64), (72, 61), (77, 72), (68, 79)]},
    {"dx": 1, "bob": 0, "left_foot": (40, 82), "right_foot": (58, 86), "left_hand": (34, 70), "right_hand": (65, 68), "tail": [(59, 62), (71, 55), (78, 64), (75, 76)]},
    {"dx": 0, "bob": -1, "left_foot": (42, 85), "right_foot": (55, 84), "left_hand": (31, 68), "right_hand": (62, 70), "tail": [(59, 61), (73, 56), (80, 67), (71, 78)]},
]

BACK_FRAMES = [
    {"dx": -1, "bob": 0, "left_foot": (39, 86), "right_foot": (57, 82), "left_hand": (31, 68), "right_hand": (62, 70), "tail": [(60, 61), (72, 57), (78, 69), (69, 78)]},
    {"dx": 0, "bob": 2, "left_foot": (42, 87), "right_foot": (55, 87), "left_hand": (34, 71), "right_hand": (61, 68), "tail": [(60, 64), (72, 61), (78, 72), (69, 80)]},
    {"dx": 1, "bob": 0, "left_foot": (40, 82), "right_foot": (58, 86), "left_hand": (34, 70), "right_hand": (65, 68), "tail": [(60, 62), (72, 55), (80, 63), (76, 75)]},
    {"dx": 0, "bob": -1, "left_foot": (42, 85), "right_foot": (55, 84), "left_hand": (31, 68), "right_hand": (62, 70), "tail": [(60, 61), (73, 57), (79, 69), (70, 78)]},
]


def draw_front_or_back_frame(index, back=False):
    frame = Image.new("RGBA", (FRAME * SCALE, FRAME * SCALE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(frame)
    data = BACK_FRAMES[index] if back else FRONT_FRAMES[index]
    dx = data["dx"]
    dy = data["bob"]

    draw_side_tail(draw, [(x + dx, y + dy) for x, y in data["tail"]], 0.45)

    left_hip = (42 + dx, 62 + dy)
    right_hip = (54 + dx, 62 + dy)
    left_foot = (data["left_foot"][0] + dx, data["left_foot"][1] + dy)
    right_foot = (data["right_foot"][0] + dx, data["right_foot"][1] + dy)
    left_hand = (data["left_hand"][0] + dx, data["left_hand"][1] + dy)
    right_hand = (data["right_hand"][0] + dx, data["right_hand"][1] + dy)

    capsule(draw, [right_hip, ((right_hip[0] + right_foot[0]) / 2, 74 + dy), right_foot], FUR_DARK, 7, OUTLINE, 1.7)
    paw(draw, right_foot, TEAL_DARK if not back else FUR_DARK, 12, 7)
    capsule(draw, [(56 + dx, 44 + dy), (60 + dx, 57 + dy), right_hand], FUR_DARK, 6, OUTLINE, 1.5)
    paw(draw, right_hand, TEAL_DARK if not back else FUR_DARK, 8, 7)

    capsule(draw, [left_hip, ((left_hip[0] + left_foot[0]) / 2, 74 + dy), left_foot], FUR, 7.5, OUTLINE, 1.8)
    paw(draw, left_foot, TEAL if not back else FUR, 13, 7)
    capsule(draw, [(40 + dx, 44 + dy), (36 + dx, 57 + dy), left_hand], FUR, 6.5, OUTLINE, 1.7)
    paw(draw, left_hand, TEAL if not back else FUR, 8, 7)

    draw_body(draw, dx, dy, side=False)
    draw_front_head(draw, dx, dy, face=not back)
    return finalize_frame(frame)


def finalize_frame(frame):
    frame = frame.resize((FRAME, FRAME), Image.Resampling.LANCZOS)
    alpha = frame.getchannel("A")
    mask = alpha.point(lambda a: 255 if a >= 72 else 0)
    out = Image.new("RGBA", (FRAME, FRAME), (0, 0, 0, 0))
    out.paste(frame.convert("RGB"), mask=mask)
    out.putalpha(mask)
    return out


def save_outputs(sheet):
    OUT_PNG.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(OUT_PNG)

    keyed = Image.new("RGB", sheet.size, KEY)
    keyed.paste(sheet.convert("RGB"), mask=sheet.getchannel("A"))
    keyed.save(OUT_SHEET)

    print(f"wrote {OUT_SHEET}")
    print(f"wrote {OUT_PNG}")


def main():
    frames = [draw_side_frame(i) for i in range(8)]
    frames += [draw_front_or_back_frame(i, back=False) for i in range(4)]
    frames += [draw_front_or_back_frame(i, back=True) for i in range(4)]

    sheet = Image.new("RGBA", (FRAME * GRID, FRAME * GRID), (0, 0, 0, 0))
    for index, frame in enumerate(frames):
        x = (index % GRID) * FRAME
        y = (index // GRID) * FRAME
        sheet.alpha_composite(frame, (x, y))

    save_outputs(sheet)


if __name__ == "__main__":
    main()
