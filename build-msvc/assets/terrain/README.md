# Terrain Modules

Natural and built terrain types are discovered from direct child folders:

```text
terrain/
  natural/<terrain-id>/terrain.json
  built/<terrain-id>/terrain.json
  masks/{left,right,top,bottom}.png
```

Each module contains a 48x48 seamless RGBA PNG named by its `image` field.
Natural modules also use `priority`: the higher-priority texture blends into a
lower-priority neighbor through the shared directional Alpha masks. Built
modules are drawn as exact 48x48 tiles above the natural layer.

```json
{
  "id": "meadow",
  "display_name": "Meadow",
  "image": "texture.png",
  "variants": 4,
  "priority": 5,
  "fallback_color": "#72b879"
}
```

IDs may contain letters, numbers, underscores, and hyphens. Add the folder and
press `R` in SceneEditor; no C++ registration is required. `fallback_color` is
used for the editor swatch and whenever the PNG cannot be loaded.

`variants` is optional and defaults to 1. Additional files append `_1`, `_2`,
and so on to the base filename, such as `texture_1.png`. Variant textures must
share compatible edge pixels so neighboring tiles remain seamless.

Run `python tools/generate_terrain_assets.py` to regenerate the bundled sample
textures and shared masks.

Set `blocks_movement` to `true` for terrain like water or cliffs that should block player movement.
Built floors can still sit on top of natural terrain and remain walkable if the built layer itself is non-blocking.

