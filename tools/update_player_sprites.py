#!/usr/bin/env python3
"""Rebuild every player sprite sheet from the PSD export folders."""

from __future__ import annotations

import argparse
import re
import shutil
from pathlib import Path

from PIL import Image


SOURCE_CHARACTER_HEIGHT = 380
TARGET_CHARACTER_HEIGHT = 288
FRAME_SIZE = round(500 * TARGET_CHARACTER_HEIGHT / SOURCE_CHARACTER_HEIGHT)
EXPECTED_COUNTS = {
    "front": 4,
    "side": 8,
    "back": 4,
    "attack_front": 5,
    "attack_side": 4,
    "attack_back": 5,
}


def natural_key(path: Path) -> list[object]:
    return [int(part) if part.isdigit() else part.lower() for part in re.split(r"(\d+)", path.name)]


def numeric_frames(directory: Path, expected_count: int) -> list[Path]:
    frames = sorted(directory.glob("*.png"), key=natural_key)
    if len(frames) != expected_count:
        raise ValueError(f"{directory}: expected {expected_count} PNG files, found {len(frames)}")
    return frames


def first_existing(directory: Path, names: tuple[str, ...]) -> Path:
    for name in names:
        path = directory / name
        if path.is_file():
            return path
    expected = ", ".join(names)
    raise FileNotFoundError(f"{directory}: none of these files exists: {expected}")


def source_groups(source_root: Path) -> dict[str, list[Path]]:
    standing = source_root / "\u7ad9\u7acb"
    return {
        "idle_front": [first_existing(standing, ("\u6b63\u9762.png",))],
        "idle_side": [first_existing(standing, ("\u4fa7\u8138.png", "\u6d4b\u8138.png"))],
        "idle_back": [first_existing(standing, ("\u80cc\u9762.png",))],
        "front": numeric_frames(source_root / "\u6b63\u8138\u8dd1\u6b65", EXPECTED_COUNTS["front"]),
        "side": numeric_frames(source_root / "\u4fa7\u8138\u8dd1\u6b65", EXPECTED_COUNTS["side"]),
        "back": numeric_frames(source_root / "\u80cc\u9762\u8dd1\u6b65", EXPECTED_COUNTS["back"]),
        "attack_front": numeric_frames(source_root / "\u6b63\u9762\u653b\u51fb", EXPECTED_COUNTS["attack_front"]),
        "attack_side": numeric_frames(source_root / "\u4fa7\u9762\u653b\u51fb", EXPECTED_COUNTS["attack_side"]),
        "attack_back": numeric_frames(source_root / "\u80cc\u9762\u653b\u51fb", EXPECTED_COUNTS["attack_back"]),
    }


def validate_sources(groups: dict[str, list[Path]]) -> tuple[int, int]:
    expected_size: tuple[int, int] | None = None
    for paths in groups.values():
        for path in paths:
            with Image.open(path) as image:
                size = image.size
                if expected_size is None:
                    expected_size = size
                elif size != expected_size:
                    raise ValueError(f"{path}: size {size} does not match {expected_size}")

                alpha = image.convert("RGBA").getchannel("A")
                if alpha.getextrema() == (255, 255):
                    raise ValueError(f"{path}: PNG has no transparent pixels")

    if expected_size is None:
        raise ValueError("no source images found")
    if expected_size[0] != expected_size[1]:
        raise ValueError(f"source canvas must be square, found {expected_size}")
    return expected_size


def resize_full_canvas(path: Path, source_size: tuple[int, int]) -> Image.Image:
    with Image.open(path) as source:
        frame = source.convert("RGBA")
        if frame.size != source_size:
            raise ValueError(f"{path}: source size changed while building")
        # The entire PSD-exported canvas is transformed identically. Do not crop,
        # trim, recenter, or calculate a separate scale from visible pixels.
        return frame.resize((FRAME_SIZE, FRAME_SIZE), Image.Resampling.LANCZOS)


def build_sheet(paths: list[Path], columns: int, source_size: tuple[int, int]) -> Image.Image:
    if len(paths) % columns != 0:
        raise ValueError(f"frame count {len(paths)} is not divisible by {columns}")
    rows = len(paths) // columns
    sheet = Image.new("RGBA", (columns * FRAME_SIZE, rows * FRAME_SIZE), (0, 0, 0, 0))
    for index, path in enumerate(paths):
        frame = resize_full_canvas(path, source_size)
        sheet.alpha_composite(frame, ((index % columns) * FRAME_SIZE, (index // columns) * FRAME_SIZE))
    return sheet


def build_outputs(groups: dict[str, list[Path]], source_size: tuple[int, int]) -> dict[str, Image.Image]:
    return {
        "player_idle_front.png": resize_full_canvas(groups["idle_front"][0], source_size),
        "player_idle_side.png": resize_full_canvas(groups["idle_side"][0], source_size),
        "player_idle_back.png": resize_full_canvas(groups["idle_back"][0], source_size),
        "player_walk_front.png": build_sheet(groups["front"], 4, source_size),
        "player_walk_side_8.png": build_sheet(groups["side"], 4, source_size),
        "player_walk_back.png": build_sheet(groups["back"], 4, source_size),
        "player_attack_front.png": build_sheet(groups["attack_front"], 5, source_size),
        "player_attack_side.png": build_sheet(groups["attack_side"], 4, source_size),
        "player_attack_back.png": build_sheet(groups["attack_back"], 5, source_size),
    }


def save_outputs(outputs: dict[str, Image.Image], output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    for name, image in outputs.items():
        path = output_dir / name
        image.save(path, optimize=True)
        print(f"wrote {path} ({image.width}x{image.height})")


def sync_build_assets(project_root: Path, output_dir: Path, names: list[str]) -> None:
    for relative in ("cmake-build-debug/assets", "build-msvc/assets"):
        target_dir = project_root / relative
        if not target_dir.is_dir() or target_dir.resolve() == output_dir.resolve():
            continue
        for name in names:
            shutil.copy2(output_dir / name, target_dir / name)
        print(f"synced {target_dir}")


def parse_args() -> argparse.Namespace:
    default_source = Path.home() / "Desktop" / "\u8d34\u56fe" / "\u89d2\u8272"
    default_project = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=default_source)
    parser.add_argument("--project-root", type=Path, default=default_project)
    parser.add_argument("--output-dir", type=Path, default=None)
    parser.add_argument("--sync-builds", action="store_true")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    source_root = args.source_root.resolve()
    project_root = args.project_root.resolve()
    output_dir = (args.output_dir or (project_root / "assets")).resolve()

    groups = source_groups(source_root)
    source_size = validate_sources(groups)
    outputs = build_outputs(groups, source_size)
    save_outputs(outputs, output_dir)
    if args.sync_builds:
        sync_build_assets(project_root, output_dir, list(outputs))

    scale = FRAME_SIZE / source_size[0]
    print(f"source canvas: {source_size[0]}x{source_size[1]}")
    print(f"character height basis: {SOURCE_CHARACTER_HEIGHT}px -> {TARGET_CHARACTER_HEIGHT}px")
    print(f"uniform canvas scale: {scale:.6f}")


if __name__ == "__main__":
    main()
