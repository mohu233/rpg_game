# Walk Sprite Generation Requirements

Use this kit to generate the final walking sprites frame by frame. Do not ask the AI to redraw the whole 4x4 sheet in one pass unless the tool is very reliable with sprite sheets.

## Files

- `00_full_anchor_grid.png`: full 4x4 pose reference.
- `anchor_frames/*.png`: 16 separate pose references.
- `frame_manifest.csv`: frame order and meaning.
- `ai_outputs/`: put the final AI-generated 16 frame images here.

## Best Workflow

Generate one frame at a time.

For each frame, upload:

1. The character design reference image.
2. One matching pose image from `anchor_frames`.

Then use the frame prompt below. Replace `{FRAME_ID}`, `{DIRECTION}`, `{POSE}`, and `{ANCHOR_FILENAME}` for each frame.

## Frame Prompt

```text
Use the character reference image as the only source for character design.
Use the pose reference image named {ANCHOR_FILENAME} only for body pose, foot placement, weight shift, and tail direction.

Generate exactly one game sprite frame for {FRAME_ID}.
Direction: {DIRECTION}.
Pose phase: {POSE}.

Character:
- cute upright furry cat person
- about three-heads tall
- dark navy outer fur
- teal face, muzzle, chest, belly, paws, and cheek fur
- orange inner ears and orange tail tip
- large golden eyes
- large ears, round head, small muzzle, curled tail
- same character design in every frame

Pose:
- strictly follow the uploaded pose reference
- keep the same facing direction as the pose reference
- keep the feet on the same ground line
- keep the body centered in the frame
- show real walking motion, not just a static standing pose
- arms and legs must swing in opposite rhythm
- body height should subtly rise/fall according to the pose phase
- tail should follow the body motion but must not change shape drastically

Output:
- one isolated sprite frame only
- square canvas
- transparent background preferred; pure white background is acceptable
- no labels, no skeleton guide, no grid, no floor, no shadow
- no watermark, no text, no extra characters, no props
- do not crop ears, feet, hands, or tail
- do not invent new clothing, accessories, markings, or colors
```

## Negative Prompt

```text
text, watermark, labels, guide lines, skeleton lines, grid, background scene, floor shadow, extra character, props, clothing, accessories, wrong colors, different face, different ears, different tail, mirrored direction, cropped body, missing feet, missing tail, blurry, painterly, realistic, 3d render, inconsistent scale, inconsistent body shape
```

## Direction Rules

- `S1` to `S8` are side walk frames facing right.
- Do not draw left-facing side frames.
- The game mirrors the right-facing 8-frame side walk cycle for left movement.
- `D1` to `D4` are front/down walk frames.
- `U1` to `U4` are back/up walk frames.

## Frame Order

Use these exact output filenames in `ai_outputs/`:

```text
01_S1_side_right_contact.png
02_S2_side_right_recoil.png
03_S3_side_right_passing.png
04_S4_side_right_high.png
05_S5_side_right_contact.png
06_S6_side_right_recoil.png
07_S7_side_right_passing.png
08_S8_side_right_high.png
09_D1_front_down_contact.png
10_D2_front_down_recoil.png
11_D3_front_down_contact.png
12_D4_front_down_recoil.png
13_U1_back_up_contact.png
14_U2_back_up_recoil.png
15_U3_back_up_contact.png
16_U4_back_up_recoil.png
```

After all 16 images are in `ai_outputs/`, run:

```text
python tools/assemble_ai_walk_frames.py
```
