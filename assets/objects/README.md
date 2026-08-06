# Object Modules

Each direct child directory is one placeable object module. The editor scans
these directories when it starts and whenever `R` is pressed.

```text
objects/
  pine_tree/
    object.json
    pine_tree.bmp
```

Example `object.json`:

```json
{
  "type": "pine_tree",
  "display_name": "Pine Tree",
  "image": "pine_tree.bmp",
  "width": 96.0,
  "height": 128.0,
  "z_offset": 0.0,
  "collision": {
    "shape": "rect",
    "x": -16.0,
    "y": -28.0,
    "w": 32.0,
    "h": 28.0,
    "radius": 0.0,
    "blocks": true
  }
}
```

`type` is optional and defaults to the directory name. It may contain letters,
numbers, underscores, and hyphens. `image` must point to a BMP inside the same
module directory. BMP transparency uses the color `#ff00ff`.

Optional `placeable: false` hides a module from the editor palette while still
allowing scenes to load it. Optional `companion_type` names another object type
that the editor should place together with this object; both scene objects share
one group so they move and delete together.

Supported collision shapes are `none`, `rect`, and `circle`. For a circle,
`x` and `y` are offsets from the object's bottom-center origin and `radius`
sets its size. New scene files store only object placement and the module
`type`, so placed objects use updated module metadata after a reload. Legacy
scene files may still contain `z` and `collision` fields; the loader accepts
them as explicit per-instance overrides.
