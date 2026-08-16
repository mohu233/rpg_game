from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "art_source" / "player" / "player_walk_anchor.png"
OUT_LARGE = ROOT / "art_source" / "player" / "player_walk_anchor_large.png"

FRAME = 96
GRID = 4
SCALE = 4

BG = (248, 246, 236, 255)
GRID_LINE = (204, 211, 197, 255)
GUIDE = (134, 162, 146, 255)
BODY = (72, 82, 96, 255)
BODY_FADE = (126, 137, 151, 255)
NEAR = (28, 43, 67, 255)
FAR = (142, 151, 162, 255)
ACCENT = (228, 92, 70, 255)
TAIL = (76, 105, 146, 255)
HEAD = (102, 92, 80, 255)
TEXT = (65, 69, 76, 255)


def font(size=8):
    try:
        return ImageFont.truetype("arial.ttf", size)
    except OSError:
        return ImageFont.load_default()


def p(ox, oy, x, y):
    return ox + x, oy + y


def line(draw, ox, oy, pts, color, width=2):
    draw.line([p(ox, oy, x, y) for x, y in pts], fill=color, width=width, joint="curve")


def ellipse(draw, ox, oy, cx, cy, rx, ry, outline, width=2, fill=None):
    draw.ellipse((ox + cx - rx, oy + cy - ry, ox + cx + rx, oy + cy + ry), outline=outline, width=width, fill=fill)


def polygon(draw, ox, oy, pts, outline, fill=None):
    points = [p(ox, oy, x, y) for x, y in pts]
    draw.polygon(points, outline=outline, fill=fill)


SIDE_POSES = [
    {
        "name": "S1 contact",
        "bob": 0,
        "near_leg": [(47, 56), (38, 72), (22, 88)],
        "far_leg": [(47, 56), (57, 71), (74, 88)],
        "near_arm": [(45, 38), (35, 54), (27, 70)],
        "far_arm": [(47, 38), (58, 53), (69, 68)],
        "tail": [(40, 59), (66, 55), (77, 44), (71, 34)],
    },
    {
        "name": "S2 recoil",
        "bob": 3,
        "near_leg": [(47, 59), (40, 74), (31, 88)],
        "far_leg": [(47, 59), (59, 73), (69, 88)],
        "near_arm": [(45, 41), (37, 56), (31, 71)],
        "far_arm": [(47, 41), (56, 55), (64, 69)],
        "tail": [(40, 62), (64, 57), (76, 47), (72, 37)],
    },
    {
        "name": "S3 passing",
        "bob": 0,
        "near_leg": [(47, 56), (47, 72), (49, 88)],
        "far_leg": [(47, 56), (40, 68), (45, 78)],
        "near_arm": [(45, 38), (43, 54), (42, 70)],
        "far_arm": [(47, 38), (53, 52), (58, 67)],
        "tail": [(40, 59), (63, 55), (75, 45), (74, 34)],
    },
    {
        "name": "S4 high",
        "bob": -2,
        "near_leg": [(47, 54), (52, 69), (58, 82)],
        "far_leg": [(47, 54), (45, 72), (42, 88)],
        "near_arm": [(45, 36), (49, 51), (54, 66)],
        "far_arm": [(47, 36), (43, 52), (36, 68)],
        "tail": [(40, 57), (64, 53), (78, 42), (74, 31)],
    },
    {
        "name": "S5 contact",
        "bob": 0,
        "near_leg": [(47, 56), (58, 71), (74, 88)],
        "far_leg": [(47, 56), (38, 72), (22, 88)],
        "near_arm": [(45, 38), (58, 53), (69, 68)],
        "far_arm": [(47, 38), (35, 54), (27, 70)],
        "tail": [(40, 59), (66, 55), (77, 44), (71, 34)],
    },
    {
        "name": "S6 recoil",
        "bob": 3,
        "near_leg": [(47, 59), (59, 73), (69, 88)],
        "far_leg": [(47, 59), (40, 74), (31, 88)],
        "near_arm": [(45, 41), (56, 55), (64, 69)],
        "far_arm": [(47, 41), (37, 56), (31, 71)],
        "tail": [(40, 62), (64, 57), (76, 47), (72, 37)],
    },
    {
        "name": "S7 passing",
        "bob": 0,
        "near_leg": [(47, 56), (40, 68), (45, 78)],
        "far_leg": [(47, 56), (47, 72), (49, 88)],
        "near_arm": [(45, 38), (53, 52), (58, 67)],
        "far_arm": [(47, 38), (43, 54), (42, 70)],
        "tail": [(40, 59), (63, 55), (75, 45), (74, 34)],
    },
    {
        "name": "S8 high",
        "bob": -2,
        "near_leg": [(47, 54), (45, 72), (42, 88)],
        "far_leg": [(47, 54), (52, 69), (58, 82)],
        "near_arm": [(45, 36), (43, 52), (36, 68)],
        "far_arm": [(47, 36), (49, 51), (54, 66)],
        "tail": [(40, 57), (64, 53), (78, 42), (74, 31)],
    },
]

FRONT_BACK_POSES = [
    ("D1 contact", 0, -8, 8),
    ("D2 down", 3, -2, 2),
    ("D3 contact", 0, 8, -8),
    ("D4 down", 3, 2, -2),
    ("U1 contact", 0, -8, 8),
    ("U2 down", 3, -2, 2),
    ("U3 contact", 0, 8, -8),
    ("U4 down", 3, 2, -2),
]


def draw_common_guides(draw, ox, oy, label):
    draw.rectangle((ox, oy, ox + FRAME - 1, oy + FRAME - 1), outline=GRID_LINE)
    draw.line((ox, oy + 88, ox + FRAME, oy + 88), fill=GUIDE, width=1)
    draw.line((ox, oy + 8, ox + FRAME, oy + 8), fill=GRID_LINE, width=1)
    draw.line((ox + 48, oy, ox + 48, oy + FRAME), fill=GRID_LINE, width=1)
    draw.text((ox + 3, oy + 3), label, fill=TEXT, font=font(7))
    draw.ellipse((ox + 45, oy + 85, ox + 51, oy + 91), fill=ACCENT)


def draw_side_pose(draw, ox, oy, pose):
    bob = pose["bob"]
    shoulder = (46, 37 + bob)
    hip = (47, 56 + bob)
    neck = (46, 29 + bob)
    head = (48, 20 + bob)

    draw_common_guides(draw, ox, oy, pose["name"])
    line(draw, ox, oy, pose["tail"], TAIL, 3)
    ellipse(draw, ox, oy, head[0], head[1], 13, 13, HEAD, width=2)
    polygon(draw, ox, oy, [(38, 12 + bob), (34, 0 + bob), (47, 9 + bob)], HEAD)
    polygon(draw, ox, oy, [(58, 12 + bob), (62, 0 + bob), (49, 9 + bob)], HEAD)
    line(draw, ox, oy, [neck, shoulder, hip], BODY, 3)
    ellipse(draw, ox, oy, 48, 45 + bob, 9, 18, BODY_FADE, width=2)
    line(draw, ox, oy, pose["far_leg"], FAR, 3)
    line(draw, ox, oy, pose["far_arm"], FAR, 3)
    line(draw, ox, oy, pose["near_leg"], NEAR, 4)
    line(draw, ox, oy, pose["near_arm"], NEAR, 4)
    for x, y in [pose["near_leg"][-1], pose["far_leg"][-1]]:
        draw.line((ox + x - 5, oy + y, ox + x + 8, oy + y), fill=ACCENT, width=2)


def draw_front_back_pose(draw, ox, oy, name, bob, left_step, right_step, back=False):
    draw_common_guides(draw, ox, oy, name)
    head_y = 20 + bob
    shoulder_y = 38 + bob
    hip_y = 58 + bob

    if back:
        line(draw, ox, oy, [(65, hip_y), (76, 55 + bob), (70, 38 + bob)], TAIL, 3)
    else:
        line(draw, ox, oy, [(63, hip_y), (76, 54 + bob), (70, 39 + bob)], TAIL, 3)

    ellipse(draw, ox, oy, 48, head_y, 15, 13, HEAD, width=2)
    polygon(draw, ox, oy, [(35, 12 + bob), (29, 1 + bob), (43, 9 + bob)], HEAD)
    polygon(draw, ox, oy, [(61, 12 + bob), (67, 1 + bob), (53, 9 + bob)], HEAD)
    ellipse(draw, ox, oy, 48, 47 + bob, 12, 18, BODY_FADE, width=2)
    line(draw, ox, oy, [(48, 31 + bob), (48, shoulder_y), (48, hip_y)], BODY, 3)

    left_foot = (38 + left_step, 88)
    right_foot = (58 + right_step, 88)
    left_knee = (39 + left_step // 2, 72 + bob)
    right_knee = (57 + right_step // 2, 72 + bob)
    line(draw, ox, oy, [(43, hip_y), left_knee, left_foot], NEAR, 4)
    line(draw, ox, oy, [(53, hip_y), right_knee, right_foot], FAR, 3)

    line(draw, ox, oy, [(39, shoulder_y), (34 - right_step // 2, 56 + bob), (32 - right_step, 72 + bob)], FAR, 3)
    line(draw, ox, oy, [(57, shoulder_y), (62 - left_step // 2, 56 + bob), (64 - left_step, 72 + bob)], NEAR, 4)
    for x, y in [left_foot, right_foot]:
        draw.line((ox + x - 6, oy + y, ox + x + 7, oy + y), fill=ACCENT, width=2)

    if not back:
        draw.ellipse((ox + 40, oy + head_y - 1, ox + 44, oy + head_y + 5), fill=NEAR)
        draw.ellipse((ox + 52, oy + head_y - 1, ox + 56, oy + head_y + 5), fill=NEAR)


def main():
    OUT.parent.mkdir(parents=True, exist_ok=True)
    img = Image.new("RGBA", (FRAME * GRID, FRAME * GRID), BG)
    draw = ImageDraw.Draw(img)

    for i, pose in enumerate(SIDE_POSES):
        row = i // 4
        col = i % 4
        draw_side_pose(draw, col * FRAME, row * FRAME, pose)

    for i, pose in enumerate(FRONT_BACK_POSES):
        row = 2 + i // 4
        col = i % 4
        name, bob, left_step, right_step = pose
        draw_front_back_pose(draw, col * FRAME, row * FRAME, name, bob, left_step, right_step, back=row == 3)

    img.save(OUT)
    img.resize((img.width * SCALE, img.height * SCALE), Image.Resampling.NEAREST).save(OUT_LARGE)
    print(f"wrote {OUT}")
    print(f"wrote {OUT_LARGE}")


if __name__ == "__main__":
    main()
