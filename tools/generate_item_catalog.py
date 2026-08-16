from __future__ import annotations

import json
from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
ITEMS_ROOT = ROOT / "assets" / "items"
OBJECTS_ROOT = ROOT / "assets" / "objects"
CATALOG_PATH = ROOT / "assets" / "item_catalog.json"
CONTACT_SHEET_PATH = ROOT / "outputs" / "item_icon_contact_sheet.png"


def item(item_id, name, color, category, max_stack, source, properties=None, recipe=None):
    return {
        "id": item_id,
        "display_name": name,
        "color": color,
        "category": category,
        "max_stack": max_stack,
        "source": source,
        "properties": properties or {},
        "recipe": recipe,
    }


def recipe(station, *ingredients, output_count=1):
    result = {
        "station": station,
        "ingredients": [{"item": item_id, "count": count} for item_id, count in ingredients],
    }
    if output_count != 1:
        result["output_count"] = output_count
    return result


ITEMS = [
    item("wood", "木头", "#9c6238", "wood", 99, "砍伐树木获得", {"material_grade": 1}),
    item("exotic_wood", "特异木头", "#527e72", "wood", 99, "砍伐特异树木获得", {"material_grade": 2}),
    item("stone", "石头", "#7d8584", "stone", 99, "挖掘石头获得", {"material_grade": 1}),
    item("iron_ore", "铁矿石", "#77858a", "ore", 50, "挖掘铁矿石获得", {"material_grade": 1, "value": 8, "weight": 2.5}),
    item("copper_ore", "铜矿石", "#4f9b7c", "ore", 50, "挖掘铜矿石获得", {"material_grade": 1}),
    item("silver_ore", "银矿石", "#b9c4c8", "ore", 50, "挖掘银矿石获得", {"material_grade": 2}),
    item("gold_ore", "金矿石", "#d7ad3f", "ore", 50, "挖掘金矿石获得", {"material_grade": 2}),
    item("ruby", "红宝石", "#d7434c", "gem", 20, "挖掘任意矿石小概率获得", {"rarity": 3}),
    item("emerald", "绿宝石", "#3cb878", "gem", 20, "挖掘任意矿石小概率获得", {"rarity": 3}),
    item("sapphire", "蓝宝石", "#397fd3", "gem", 20, "挖掘任意矿石小概率获得", {"rarity": 3}),
    item("topaz", "黄宝石", "#e1b83f", "gem", 20, "挖掘任意矿石小概率获得", {"rarity": 3}),
    item("amethyst", "紫宝石", "#9a58c7", "gem", 20, "挖掘任意矿石小概率获得", {"rarity": 3}),
    item("anchor_fragment", "锚点碎片", "#b9e0d0", "fragment", 10, "击杀怪物极低概率获得", {"rarity": 5}),
    item("exotic_ore", "特异矿石", "#4e8e91", "ore", 50, "挖掘特异矿石获得", {"material_grade": 3}),
    item("healing_herb", "草药", "#5ea562", "plant", 50, "采集草药苗或击杀怪物获得", {"material_grade": 1}),
    item("berry", "浆果", "#b9475e", "plant", 50, "采集浆果丛或击杀怪物获得", {"food": 1}),
    item("wheat", "小麦", "#d6b94d", "crop", 99, "采集小麦或击杀怪物获得", {"food": 1}),
    item("fodder", "草料", "#6f9d52", "plant", 99, "采集草获得", {"food": 1}),
    item("rice", "水稻", "#d9d18a", "crop", 99, "采集水稻获得", {"food": 1}),
    item("rice_seed", "水稻种子", "#c8bd76", "seed", 99, "水稻分解或击杀怪物获得", {"seed": 1, "crop_days": 2, "harvest_count": 2}, recipe("decompose", ("rice", 1), output_count=4)),
    item("corn", "玉米", "#e1bd38", "crop", 99, "采集玉米获得", {"food": 1}),
    item("corn_seed", "玉米种子", "#d8a92c", "seed", 99, "玉米分解或击杀怪物获得", {"seed": 1, "crop_days": 2, "harvest_count": 2}, recipe("decompose", ("corn", 1), output_count=4)),
    item("potato", "土豆", "#a77c4f", "crop", 99, "采集土豆获得", {"food": 1}),
    item("potato_seed", "土豆种子", "#8c6945", "seed", 99, "土豆分解或击杀怪物获得", {"seed": 1, "crop_days": 2, "harvest_count": 2}, recipe("decompose", ("potato", 1), output_count=4)),
    item("sweet_potato", "红薯", "#9d5543", "crop", 99, "采集红薯获得", {"food": 1}),
    item("sweet_potato_seed", "红薯种子", "#87483b", "seed", 99, "红薯分解或击杀怪物获得", {"seed": 1, "crop_days": 2, "harvest_count": 2}, recipe("decompose", ("sweet_potato", 1), output_count=4)),
    item("cabbage", "白菜", "#7fb75b", "crop", 99, "采集白菜获得", {"food": 1}),
    item("cabbage_seed", "白菜种子", "#689947", "seed", 99, "白菜分解或击杀怪物获得", {"seed": 1, "crop_days": 2, "harvest_count": 2}, recipe("decompose", ("cabbage", 1), output_count=4)),
    item("iron_ingot", "铁锭", "#9ba6a9", "ingot", 50, "熔炉合成", {"material_grade": 2}, recipe("furnace", ("iron_ore", 2), ("wood", 1))),
    item("copper_ingot", "铜锭", "#cf7f4d", "ingot", 50, "熔炉合成", {"material_grade": 2}, recipe("furnace", ("copper_ore", 2), ("wood", 1))),
    item("silver_ingot", "银锭", "#d2dadc", "ingot", 50, "熔炉合成", {"material_grade": 3}, recipe("furnace", ("silver_ore", 2), ("wood", 1))),
    item("gold_ingot", "金锭", "#e2ba45", "ingot", 50, "熔炉合成", {"material_grade": 3}, recipe("furnace", ("gold_ore", 2), ("wood", 1))),
    item("plank", "木板", "#b57b4d", "plank", 99, "锯木台生产", {"material_grade": 1}, recipe("sawmill", ("wood", 1), output_count=2)),
    item("exotic_plank", "特异木板", "#4f8f83", "plank", 99, "锯木台生产", {"material_grade": 2}, recipe("sawmill", ("exotic_wood", 1), output_count=2)),
    item("gathering_stone", "采集石I", "#4f9f69", "spirit", 1, "工作台合成", {"spirit_skill": 1, "stone_tier": 1}, recipe("workbench", ("emerald", 1), ("stone", 1), ("wood", 1))),
    item("gathering_stone_ii", "采集石II", "#3fba79", "spirit", 1, "工作台合成", {"spirit_skill": 1, "stone_tier": 2, "drop_rate_bonus": 15}, recipe("workbench", ("emerald", 1), ("exotic_ore", 1), ("exotic_wood", 1))),
    item("gathering_stone_iii", "采集石III", "#66d99a", "spirit", 1, "工作台合成", {"spirit_skill": 1, "stone_tier": 3, "drop_rate_bonus": 35, "rare_drop_bonus": 20}, recipe("workbench", ("emerald", 2), ("exotic_ore", 1), ("exotic_wood", 1), ("anchor_fragment", 1))),
    item("combat_stone", "战斗石I", "#b64e52", "spirit", 1, "工作台合成", {"spirit_skill": 3, "stone_tier": 1, "attack_bonus": 0}, recipe("workbench", ("ruby", 1), ("stone", 1), ("wood", 1))),
    item("combat_stone_ii", "战斗石II", "#cf4d55", "spirit", 1, "工作台合成", {"spirit_skill": 3, "stone_tier": 2, "attack_bonus": 25, "ultimate": 1}, recipe("workbench", ("ruby", 1), ("exotic_ore", 1), ("exotic_wood", 1))),
    item("combat_stone_iii", "战斗石III", "#e86a71", "spirit", 1, "工作台合成", {"spirit_skill": 3, "stone_tier": 3, "attack_bonus": 50, "life_steal": 10}, recipe("workbench", ("ruby", 2), ("exotic_ore", 1), ("exotic_wood", 1), ("anchor_fragment", 1))),
    item("building_stone", "建造石I", "#c89e43", "spirit", 1, "工作台合成", {"spirit_skill": 2, "stone_tier": 1, "build_discount": 0}, recipe("workbench", ("topaz", 1), ("stone", 1), ("wood", 1))),
    item("building_stone_ii", "建造石II", "#d5ad4d", "spirit", 1, "工作台合成", {"spirit_skill": 2, "stone_tier": 2, "build_discount": 15}, recipe("workbench", ("topaz", 1), ("exotic_ore", 1), ("exotic_wood", 1))),
    item("building_stone_iii", "建造石III", "#e5c35b", "spirit", 1, "工作台合成", {"spirit_skill": 2, "stone_tier": 3, "build_discount": 35, "teleport_building": 1}, recipe("workbench", ("topaz", 2), ("exotic_ore", 1), ("exotic_wood", 1), ("anchor_fragment", 1))),
    item("small_potion", "治疗药水", "#68b8c4", "potion", 20, "工作台合成", {"heal": 30}, recipe("workbench", ("healing_herb", 1), ("fodder", 1), ("berry", 1))),
    item("farmland", "耕地", "#806342", "building", 20, "建造石建造", {"building": 1, "ground_overlay": 1, "planting": 1}),
    item("flower_bed", "花坛", "#9b6f89", "building", 20, "建造石建造", {"building": 1, "ground_overlay": 1, "comfort": 2}, recipe("workbench", ("fodder", 1))),
    item("pond", "水池", "#5f9eb3", "building", 20, "建造石建造", {"building": 1, "ground_overlay": 1, "comfort": 2}, recipe("workbench", ("stone", 1))),
    item("stone_floor", "石头地板", "#8f9693", "building", 99, "建造石建造", {"building": 1, "ground_overlay": 1, "comfort": 0.5}, recipe("workbench", ("stone", 1), output_count=4)),
    item("wood_floor", "木头地板", "#9c704b", "building", 99, "建造石建造", {"building": 1, "ground_overlay": 1, "comfort": 0.5}, recipe("workbench", ("wood", 1), output_count=4)),
    item("warehouse", "仓库", "#7f725e", "building", 5, "建造石建造", {"building": 1, "storage_slots": 500}, recipe("workbench", ("wood", 2), ("stone", 2))),
    item("campfire", "篝火", "#d47a45", "building", 10, "建造石建造", {"building": 1, "cooking_tier": 1, "comfort": 2}, recipe("workbench", ("wood", 2), ("fodder", 1))),
    item("apothecary", "药房", "#668f82", "building", 10, "建造石建造", {"building": 1, "potion_station": 1}, recipe("workbench", ("wood", 2), ("stone", 2))),
    item("kitchen", "厨房", "#a07b62", "building", 5, "建造石建造", {"building": 1, "cooking_tier": 2}, recipe("workbench", ("wood", 2), ("stone", 2))),
    item("furnace", "熔炉", "#6f7777", "building", 5, "建造石建造", {"building": 1, "furnace": 1}, recipe("workbench", ("wood", 2), ("stone", 4))),
    item("sawmill", "锯木台", "#8b755e", "building", 1, "制作面板建造", {"building": 1, "sawmill": 1}, recipe("workbench", ("wood", 1), ("iron_ingot", 1))),
]


BUILDING_SPECS = {
    "farmland": (1, 1, True),
    "flower_bed": (1, 1, True),
    "pond": (1, 1, True),
    "stone_floor": (1, 1, True),
    "wood_floor": (1, 1, True),
    "warehouse": (2, 2, False),
    "campfire": (1, 1, False),
    "apothecary": (1, 1, False),
    "kitchen": (2, 2, False),
    "furnace": (2, 2, False),
    "sawmill": (2, 2, False),
}

EXTERNAL_ITEM_IDS = ("gold_coin", "territory_anchor", "cottage_4x3", "teleport_point")


def rgb(hex_color):
    value = hex_color.lstrip("#")
    return tuple(int(value[index:index + 2], 16) for index in (0, 2, 4))


def lighten(color, amount=45):
    return tuple(min(255, value + amount) for value in color)


def darken(color, amount=45):
    return tuple(max(0, value - amount) for value in color)


def draw_icon(definition):
    color = rgb(definition["color"])
    image = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    category = definition["category"]
    draw.ellipse((13, 50, 51, 57), fill=(15, 22, 21, 80))

    if category in {"wood", "plank"}:
        if category == "plank":
            draw.polygon([(10, 43), (17, 18), (55, 18), (48, 43)], fill=color + (255,), outline=darken(color) + (255,))
            draw.line((20, 25, 51, 25), fill=lighten(color, 25) + (255,), width=2)
            draw.line((17, 34, 49, 34), fill=darken(color, 24) + (255,), width=2)
            return image
        draw.rounded_rectangle((13, 23, 53, 42), radius=6, fill=color + (255,), outline=darken(color) + (255,), width=3)
        draw.ellipse((8, 23, 25, 42), fill=lighten(color) + (255,), outline=darken(color) + (255,), width=3)
        draw.ellipse((13, 28, 20, 37), outline=darken(color, 25) + (255,), width=2)
        draw.line((30, 25, 45, 40), fill=lighten(color, 25) + (255,), width=3)
    elif category in {"stone", "ore"}:
        points = [(10, 43), (16, 24), (29, 13), (46, 18), (55, 36), (47, 51), (24, 53)]
        draw.polygon(points, fill=darken(color, 18) + (255,), outline=darken(color, 55) + (255,))
        draw.polygon([(17, 39), (23, 25), (31, 19), (43, 22), (49, 36), (41, 46), (26, 48)], fill=color + (255,))
        if category == "ore":
            draw.line((22, 43, 30, 29, 39, 34, 46, 24), fill=lighten(color, 70) + (255,), width=4)
    elif category == "gem":
        draw.polygon([(32, 8), (51, 24), (43, 48), (32, 57), (21, 48), (13, 24)], fill=color + (255,), outline=darken(color) + (255,))
        draw.polygon([(32, 8), (39, 25), (32, 48), (25, 25)], fill=lighten(color, 45) + (220,))
        draw.line((13, 24, 51, 24), fill=(235, 250, 246, 210), width=2)
    elif category == "fragment":
        draw.polygon([(34, 5), (48, 22), (39, 31), (47, 52), (28, 57), (23, 37), (13, 27)], fill=color + (245,), outline=darken(color) + (255,))
        draw.line((31, 13, 35, 29, 28, 44), fill=(240, 255, 249, 230), width=3)
    elif category == "seed":
        draw.rounded_rectangle((14, 19, 50, 53), radius=6, fill=(174, 139, 82, 255), outline=(84, 64, 39, 255), width=3)
        draw.polygon([(14, 27), (50, 27), (43, 16), (21, 16)], fill=(210, 180, 119, 255), outline=(84, 64, 39, 255))
        for x, y in ((24, 35), (36, 34), (30, 44), (41, 43)):
            draw.ellipse((x - 4, y - 2, x + 4, y + 3), fill=color + (255,), outline=darken(color, 50) + (255,))
    elif category in {"plant", "crop"}:
        draw.line((32, 51, 32, 19), fill=(57, 105, 55, 255), width=4)
        draw.ellipse((11, 20, 34, 39), fill=color + (255,), outline=darken(color) + (255,))
        draw.ellipse((31, 13, 54, 34), fill=lighten(color, 22) + (255,), outline=darken(color) + (255,))
        draw.ellipse((23, 35, 43, 53), fill=lighten(color, 45) + (255,), outline=darken(color) + (255,))
        if definition["id"] == "berry":
            for x, y in ((23, 31), (36, 28), (31, 42), (44, 39)):
                draw.ellipse((x - 5, y - 5, x + 5, y + 5), fill=color + (255,), outline=darken(color) + (255,))
    elif category == "ingot":
        draw.polygon([(12, 39), (21, 22), (48, 22), (55, 39), (47, 50), (20, 50)], fill=color + (255,), outline=darken(color) + (255,))
        draw.polygon([(21, 22), (48, 22), (43, 31), (25, 31)], fill=lighten(color, 50) + (255,))
    elif category == "spirit":
        tier = int(definition["properties"]["stone_tier"])
        draw.ellipse((8, 8, 56, 56), fill=darken(color, 30) + (255,), outline=lighten(color, 65) + (255,), width=3)
        draw.ellipse((16, 16, 48, 48), fill=color + (255,), outline=(235, 244, 220, 220), width=2)
        draw.polygon([(32, 20), (43, 41), (21, 41)], outline=(245, 248, 220, 240))
        for index in range(tier):
            left = 25 + index * 6 - (tier - 1) * 3
            draw.rectangle((left, 49, left + 3, 53), fill=(250, 236, 145, 255))
    elif category == "potion":
        draw.rectangle((26, 9, 38, 19), fill=(184, 174, 150, 255), outline=(75, 68, 58, 255), width=2)
        draw.polygon([(22, 18), (42, 18), (49, 31), (45, 52), (19, 52), (15, 31)], fill=(190, 225, 230, 210), outline=(75, 105, 110, 255))
        draw.polygon([(18, 34), (46, 34), (43, 49), (21, 49)], fill=color + (240,))
        draw.ellipse((25, 25, 32, 32), fill=(235, 255, 255, 180))
    elif category == "building":
        draw.rectangle((12, 28, 52, 53), fill=color + (255,), outline=darken(color, 55) + (255,), width=3)
        draw.polygon([(8, 29), (32, 10), (56, 29)], fill=lighten(color, 25) + (255,), outline=darken(color, 55) + (255,))
        draw.rectangle((27, 37, 37, 53), fill=darken(color, 40) + (255,))

    return image


def draw_building_world(definition, width_tiles, height_tiles, ground_overlay):
    width = width_tiles * 48
    height = height_tiles * 48 if ground_overlay else height_tiles * 48 + 40
    color = rgb(definition["color"])
    image = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    item_id = definition["id"]
    if ground_overlay:
        draw.rectangle((2, 2, width - 3, height - 3), fill=color + (220,), outline=darken(color, 45) + (255,), width=3)
        if item_id == "farmland":
            for y in range(10, height, 10):
                draw.line((5, y, width - 6, y), fill=lighten(color, 20) + (230,), width=2)
        elif item_id == "flower_bed":
            for x, y in ((13, 14), (31, 12), (23, 28), (38, 34), (12, 37)):
                draw.ellipse((x - 4, y - 4, x + 4, y + 4), fill=(225, 113, 151, 255), outline=(103, 60, 82, 255))
        elif item_id == "pond":
            draw.ellipse((5, 7, width - 6, height - 6), fill=(79, 157, 184, 235), outline=(46, 105, 124, 255), width=3)
            draw.arc((12, 13, width - 13, height - 13), 205, 335, fill=(178, 224, 229, 220), width=2)
        elif item_id == "stone_floor":
            draw.line((width // 2, 3, width // 2, height - 4), fill=darken(color, 25) + (255,), width=2)
            draw.line((3, height // 2, width - 4, height // 2), fill=darken(color, 25) + (255,), width=2)
        elif item_id == "wood_floor":
            for y in (12, 24, 36):
                draw.line((3, y, width - 4, y), fill=darken(color, 25) + (255,), width=2)
    elif item_id == "campfire":
        draw.ellipse((5, height - 18, width - 5, height - 8), fill=(24, 27, 25, 90))
        draw.line((11, height - 18, 37, height - 38), fill=(91, 55, 35, 255), width=7)
        draw.line((37, height - 18, 11, height - 38), fill=(91, 55, 35, 255), width=7)
        draw.polygon([(24, height - 20), (13, height - 43), (25, height - 59), (35, height - 41)], fill=(238, 113, 52, 255), outline=(120, 57, 35, 255))
        draw.polygon([(24, height - 24), (20, height - 42), (29, height - 48), (31, height - 34)], fill=(255, 211, 86, 255))
    else:
        base_top = 38
        draw.rectangle((3, base_top, width - 4, height - 4), fill=color + (255,), outline=darken(color, 55) + (255,), width=3)
        draw.polygon([(0, base_top + 2), (width // 2, 4), (width - 1, base_top + 2)], fill=lighten(color, 25) + (255,), outline=darken(color, 55) + (255,))
        door_width = max(12, width // 5)
        draw.rectangle((width // 2 - door_width // 2, height - 36, width // 2 + door_width // 2, height - 4), fill=darken(color, 45) + (255,))
        if item_id == "furnace":
            draw.rectangle((width - 25, 8, width - 10, 42), fill=(82, 76, 72, 255), outline=(47, 43, 41, 255), width=2)
            for y in range(base_top + 12, height - 8, 14):
                draw.line((5, y, width - 6, y), fill=darken(color, 25) + (255,), width=2)
        elif item_id == "sawmill":
            draw.rectangle((12, base_top + 15, width - 13, base_top + 35), fill=(144, 92, 52, 255), outline=(74, 49, 34, 255), width=2)
            draw.ellipse((width // 2 - 20, base_top + 7, width // 2 + 20, base_top + 47), fill=(191, 197, 194, 255), outline=(69, 75, 74, 255), width=3)
            draw.line((width // 2, base_top + 8, width // 2, base_top + 46), fill=(106, 112, 110, 255), width=2)
        elif item_id == "apothecary":
            draw.ellipse((width // 2 - 8, base_top + 12, width // 2 + 8, base_top + 30), fill=(111, 194, 164, 255), outline=(37, 88, 71, 255), width=2)
        else:
            for x in (width // 4, width * 3 // 4):
                draw.rectangle((x - 8, base_top + 14, x + 8, base_top + 31), fill=(155, 204, 207, 255), outline=(53, 75, 76, 255), width=2)
    return image


def write_building_object(definition):
    width_tiles, height_tiles, ground_overlay = BUILDING_SPECS[definition["id"]]
    object_dir = OBJECTS_ROOT / definition["id"]
    object_dir.mkdir(parents=True, exist_ok=True)
    image = draw_building_world(definition, width_tiles, height_tiles, ground_overlay)
    image_name = f"{definition['id']}.png"
    image.save(object_dir / image_name)
    collision = {
        "shape": "none" if ground_overlay else "rect",
        "x": 0.0 if ground_overlay else -width_tiles * 24.0,
        "y": 0.0 if ground_overlay else -height_tiles * 48.0,
        "w": 0.0 if ground_overlay else width_tiles * 48.0,
        "h": 0.0 if ground_overlay else height_tiles * 48.0,
        "blocks": not ground_overlay,
    }
    data = {
        "type": definition["id"],
        "display_name": definition["display_name"],
        "image": image_name,
        "width": float(image.width),
        "height": float(image.height),
        "z_offset": -1.0 if ground_overlay else 0.0,
        "building": True,
        "footprint_width": width_tiles,
        "footprint_height": height_tiles,
        "draggable": False,
        "placeable": True,
        "collision": collision,
    }
    (object_dir / "object.json").write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def write_definition(definition):
    item_dir = ITEMS_ROOT / definition["id"]
    item_dir.mkdir(parents=True, exist_ok=True)
    data = {
        "id": definition["id"],
        "display_name": definition["display_name"],
        "max_stack": definition["max_stack"],
        "color": definition["color"],
        "icon": f"items/{definition['id']}/icon.png",
        "world_image": f"items/{definition['id']}/world.png",
        "source": definition["source"],
        "category": definition["category"],
        "properties": definition["properties"],
    }
    if definition["recipe"]:
        data["recipe"] = definition["recipe"]
    (item_dir / "item.json").write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    icon = draw_icon(definition)
    icon.save(item_dir / "icon.png")
    icon.save(item_dir / "world.png")


def main():
    for definition in ITEMS:
        write_definition(definition)
        if definition["id"] in BUILDING_SPECS:
            write_building_object(definition)
    catalog_items = [{key: value for key, value in definition.items() if key != "color"} for definition in ITEMS]
    for item_id in EXTERNAL_ITEM_IDS:
        external = json.loads((ITEMS_ROOT / item_id / "item.json").read_text(encoding="utf-8"))
        catalog_items.append({
            "id": external["id"],
            "display_name": external["display_name"],
            "category": "building" if external.get("properties", {}).get("building") else "currency",
            "max_stack": external.get("max_stack", 1),
            "source": external.get("source", "已有游戏资源"),
            "properties": external.get("properties", {}),
            "recipe": external.get("recipe"),
        })
    catalog = {
        "version": 1,
        "items": catalog_items,
    }
    CATALOG_PATH.write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    columns = 8
    cell = 76
    rows = (len(ITEMS) + columns - 1) // columns
    sheet = Image.new("RGBA", (columns * cell, rows * cell), (28, 34, 32, 255))
    for index, definition in enumerate(ITEMS):
        icon = Image.open(ITEMS_ROOT / definition["id"] / "icon.png").convert("RGBA")
        x = (index % columns) * cell + (cell - icon.width) // 2
        y = (index // columns) * cell + (cell - icon.height) // 2
        sheet.alpha_composite(icon, (x, y))
    CONTACT_SHEET_PATH.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(CONTACT_SHEET_PATH)
    print(f"generated {len(ITEMS)} item definitions in {ITEMS_ROOT}")


if __name__ == "__main__":
    main()
