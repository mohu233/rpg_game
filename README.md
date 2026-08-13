# Free Walk RPG Demo

> **项目设计基准：**[《回环囚地》世界观与玩法契约](docs/world_bible_zh.md) 是后续开发的最高优先级依据。所有系统、剧情、地图和存档设计都不得偏离其中定义的六大法则、核心玩法闭环与三大结局。

A small C++17 + Win32/GDI prototype for a free-movement 2D RPG. It is meant as a first step toward a Pony Town style single-player prototype before adding multiplayer.

## Features

- Continuous movement, not grid-locked movement
- WASD / arrow-key player control
- Simple wall collision
- JSON scene-object loading
- Placeable trees, stones, and bushes with per-object collision bodies
- Two-layer terrain painting with organic natural transitions and crisp built floors
- Camera following
- 8-frame side-walk furry sprite, mirrored for left movement
- Pony Town inspired grass, path, pond, flowers, bushes, and tree-edge scenery
- NPC proximity detection
- Press `E` to open/close NPC dialog
- Standard CMake project for CLion, with game and editor targets

## Open With CLion

1. Open CLion.
2. Choose `Open`.
3. Select the repository root directory.
4. Let CLion configure CMake.
5. Run the `FreeWalkRpgDemo` target for the game.
6. Run the `SceneEditor` target for the scene editor.

## Scene Editor

The editable scene file is `assets/scenes/demo_scene.json`.

Scene objects are discovered from module folders under `assets/objects`:

- `tree_oak` -> `assets/objects/tree_oak/object.json`
- `stone_round` -> `assets/objects/stone_round/object.json`
- `bush` -> `assets/objects/bush/object.json`

Each module folder contains an `object.json` metadata file and its BMP image.
To add an object type, add another folder with those two files and press `R`
in the editor. No C++ registration is required. See
`assets/objects/README.md` for the schema and a complete example.

Use a flat magenta `#ff00ff` background in object BMPs for transparency. The object origin is bottom-center: the image bottom sits on the object's world `x/y` position, while the collision body can stay smaller than the visible image.

Current editor controls:

- Choose `Natural` to paint grass, dirt, sand, or gravel on the base layer.
- Choose `Built` to paint or erase stone and wood flooring above the natural layer.
- Hold the left mouse button and drag to paint continuously.
- Click a tool on the left to choose an object type.
- Click the map to place a tree, stone, or bush.
- Drag an existing object to move it.
- Press `Delete` to remove the selected object.
- Press `C` to show or hide collision bodies.
- Press `R` to reload the scene and rescan object modules.
- Press `Ctrl+S` to save the scene.
- Use `WASD` or arrow keys to pan the editor camera.

Terrain is stored in `assets/scenes/demo_scene.json` as compact palette-indexed
rows. Palette entries contain terrain module IDs, so new terrain types do not
require new hard-coded characters. Terrain modules live under `assets/terrain`;
natural PNG textures use shared directional Alpha masks, while built PNG tiles
stay grid-aligned. See `assets/terrain/README.md` for the module contract.

## Next Steps

The source player art lives at `assets/player_walk.png`. Run `python tools/format_player_walk.py` to convert it into engine-ready files. Use `assets/player_walk_anchor.png` as the drawing template for new art.

The runtime spritesheet lives at `assets/player_walk.bmp`. It uses a 4x4 layout with 96x96 frames. Rows 0-1 are one right-facing 8-frame side walk cycle, row 2 is a 4-frame front/down walk, and row 3 is a 4-frame back/up walk. The renderer mirrors the right-facing side walk for left movement. The magenta background is treated as transparent by the renderer. `assets/player_walk_formatted.png` is the transparent preview/export version.

For AI frame-by-frame generation, use `assets/walk_generation_kit`. Put finished AI frames into `assets/walk_generation_kit/ai_outputs`, then run `python tools/assemble_ai_walk_frames.py`.

Good next steps are editing collision sizes directly in the editor, moving the hard-coded tile map into data, and splitting more runtime systems into engine modules.
