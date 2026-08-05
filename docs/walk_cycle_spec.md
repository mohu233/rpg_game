# Walk Cycle Sprite Contract

Use `assets/player_walk_anchor.png` as the drawing template.

Runtime sheet:

- Size: 384x384
- Grid: 4 columns x 4 rows
- Frame size: 96x96
- Transparent key for BMP runtime export: RGB(255, 0, 255)

Layout:

- Row 0: side walk frames 1-4, facing right
- Row 1: side walk frames 5-8, facing right
- Row 2: front/down walk frames 1-4
- Row 3: back/up walk frames 1-4

Runtime behavior:

- Right movement plays the 8 side frames as drawn.
- Left movement mirrors the right-facing side frames in code.
- Down movement plays row 2 as a 4-frame loop.
- Up movement plays row 3 as a 4-frame loop.

Anchor notes:

- Keep both feet aligned to the ground line at y=88 for contact frames.
- Preserve the center pivot at x=48 for all frames.
- Side frames should show a full 8-frame walk cycle: contact, recoil, passing, high point, opposite contact, opposite recoil, opposite passing, opposite high point.
- Front/back frames can stay 4-frame loops, but should still include visible weight shift and opposite arm/leg motion.
- Keep the body inside each 96x96 frame with a little padding for ears and tail.

After replacing `assets/player_walk.png` with final art following this layout, run:

```text
python tools/format_player_walk.py
```
