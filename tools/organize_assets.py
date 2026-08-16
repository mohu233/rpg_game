#!/usr/bin/env python3
"""Organize runtime assets into canonical folders and convert object BMPs to PNG."""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path

from PIL import Image


PLAYER_RUNTIME_FILES = {
    "player_attack_back.png",
    "player_attack_front.png",
    "player_attack_side.png",
    "player_idle_back.png",
    "player_idle_front.png",
    "player_idle_side.png",
    "player_walk_back.png",
    "player_walk_front.png",
    "player_walk_side_8.png",
}

PLAYER_SOURCE_FILES = {
    "player_idle_back.bmp",
    "player_idle_front.bmp",
    "player_idle_side.bmp",
    "player_walk_back.bmp",
    "player_walk_backup_20260805_134052.bmp",
    "player_walk_front.bmp",
    "player_walk.bmp",
    "player_walk.png",
    "player_walk.pmb",
    "player_walk_anchor.png",
    "player_walk_anchor_large.png",
    "player_walk_formatted.png",
    "player_walk_generated_failed_horizontal.png",
    "player_walk_generated_raw.png",
}

NATURE_OBJECTS = {
    "bush",
    "exotic_tree_01",
    "exotic_tree_01_shadow",
    "stone_round",
    "tree_oak",
}

SPECIAL_OBJECTS = {
    "ending_ritual_altar",
    "teleport_point",
    "territory_anchor",
}

ITEM_CATEGORY_FOLDERS = {
    "building": "buildings",
    "crop": "crops",
    "currency": "currency",
    "fragment": "rare_materials",
    "gem": "gems",
    "ingot": "materials",
    "ore": "raw_resources",
    "plank": "materials",
    "plant": "raw_resources",
    "potion": "consumables",
    "seed": "seeds",
    "spirit": "spirit_stones",
    "stone": "raw_resources",
    "wood": "raw_resources",
}

EXTERNAL_ITEM_PATHS = {
    "cottage_4x3": "objects/buildings/cottage_3x4/cottage_4x3.png",
    "teleport_point": "objects/special/teleport_point/teleport_point.png",
}

REFERENCE_DIRECTORIES = {
    "019f9223-ba56-71c0-af8e-d218c9e724b6",
    "019f9226-68dd-7f52-9fbd-4d7d4808bd33",
    "019f9226-a0f8-7a20-b223-c989feee90dd",
    "screen_scan",
    "walk_generation_kit",
    "新建文件夹",
}

REFERENCE_FILES = {
    "chatgpt_top_capture.png",
    "codex_desktop_capture.png",
    "imagegen_before_files.json",
    "imagegen_before_front_back_retry.json",
    "imagegen_before_regen.json",
    "剧本.docx",
}


def move_path(source: Path, destination: Path) -> None:
    if not source.exists():
        return
    if destination.exists():
        raise FileExistsError(f"refusing to merge existing paths: {source} -> {destination}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.move(str(source), str(destination))
    print(f"moved {source} -> {destination}")


def write_json(path: Path, data: dict) -> None:
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def convert_magenta_bmp(source: Path, destination: Path) -> None:
    with Image.open(source) as image:
        rgba = image.convert("RGBA")
    pixels = list(rgba.getdata())
    rgba.putdata([(r, g, b, 0 if (r, g, b) == (255, 0, 255) else a) for r, g, b, a in pixels])
    destination.parent.mkdir(parents=True, exist_ok=True)
    rgba.save(destination, optimize=True)
    print(f"converted {source} -> {destination}")


def object_group(object_id: str) -> str:
    if object_id in NATURE_OBJECTS:
        return "nature"
    if object_id in SPECIAL_OBJECTS:
        return "special"
    return "buildings"


def item_group(item_id: str, category: str) -> str:
    if item_id in {"cottage_4x3", "teleport_point", "territory_anchor"}:
        return "buildings"
    if item_id == "gold_coin":
        return "currency"
    return ITEM_CATEGORY_FOLDERS.get(category, "misc")


def organize_objects(assets: Path, archive: Path) -> None:
    objects = assets / "objects"
    for module in sorted(path for path in objects.iterdir() if path.is_dir()):
        metadata_path = module / "object.json"
        if not metadata_path.is_file():
            continue
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        object_id = metadata.get("type", module.name)
        image_name = metadata.get("image", "")
        image_path = module / image_name
        if image_path.suffix.lower() == ".bmp":
            png_path = image_path.with_suffix(".png")
            convert_magenta_bmp(image_path, png_path)
            metadata["image"] = png_path.name
            write_json(metadata_path, metadata)
            move_path(image_path, archive / "legacy_bmp" / "objects" / module.name / image_path.name)

        destination = objects / object_group(object_id) / module.name
        move_path(module, destination)

    for bmp in sorted(objects.glob("*.bmp")):
        move_path(bmp, archive / "legacy_bmp" / "objects_flat" / bmp.name)

    unused_cottage = objects / "buildings" / "cottage_3x4" / "cottage_3x4.png"
    move_path(unused_cottage, archive / "object_extras" / "cottage_3x4.png")


def organize_items(assets: Path) -> None:
    items = assets / "items"
    for module in sorted(path for path in items.iterdir() if path.is_dir()):
        metadata_path = module / "item.json"
        if not metadata_path.is_file():
            continue
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        item_id = metadata.get("id", module.name)
        group = item_group(item_id, metadata.get("category", ""))
        destination = items / group / module.name

        if item_id in EXTERNAL_ITEM_PATHS:
            metadata["icon"] = EXTERNAL_ITEM_PATHS[item_id]
            metadata["world_image"] = EXTERNAL_ITEM_PATHS[item_id]
        else:
            if (module / "icon.png").is_file():
                metadata["icon"] = f"items/{group}/{module.name}/icon.png"
            if (module / "world.png").is_file():
                metadata["world_image"] = f"items/{group}/{module.name}/world.png"
        write_json(metadata_path, metadata)
        move_path(module, destination)

    catalog = assets / "item_catalog.json"
    move_path(catalog, items / "catalog.json")


def organize_player(assets: Path, archive: Path) -> None:
    player = assets / "characters" / "player"
    for name in sorted(PLAYER_RUNTIME_FILES):
        move_path(assets / name, player / name)
    for name in sorted(PLAYER_SOURCE_FILES):
        move_path(assets / name, archive / "player" / name)


def organize_references(assets: Path, archive: Path) -> None:
    for name in sorted(REFERENCE_DIRECTORIES):
        move_path(assets / name, archive / "reference" / name)
    for name in sorted(REFERENCE_FILES):
        move_path(assets / name, archive / "reference" / name)
    move_path(assets / "scenes", archive / "legacy_scenes")


def validate_runtime_assets(assets: Path) -> None:
    bmps = sorted(assets.rglob("*.bmp"))
    if bmps:
        raise RuntimeError("BMP files remain in runtime assets:\n" + "\n".join(str(path) for path in bmps))

    missing: list[str] = []
    for metadata_path in assets.rglob("item.json"):
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        for field in ("icon", "world_image"):
            relative = metadata.get(field)
            if relative and not (assets / relative).is_file():
                missing.append(f"{metadata_path}: {field} -> {relative}")
    for metadata_path in assets.rglob("object.json"):
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        relative = metadata.get("image")
        if not relative or Path(relative).suffix.lower() != ".png" or not (metadata_path.parent / relative).is_file():
            missing.append(f"{metadata_path}: image -> {relative}")
    if missing:
        raise RuntimeError("invalid runtime image references:\n" + "\n".join(missing))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", type=Path, default=Path(__file__).resolve().parents[1])
    return parser.parse_args()


def main() -> None:
    root = parse_args().project_root.resolve()
    assets = root / "assets"
    archive = root / "art_source"
    organize_objects(assets, archive)
    organize_items(assets)
    organize_player(assets, archive)
    organize_references(assets, archive)
    validate_runtime_assets(assets)
    print("asset organization complete")


if __name__ == "__main__":
    main()
