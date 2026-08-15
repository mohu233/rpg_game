from __future__ import annotations

import json
from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
ITEMS_ROOT = ROOT / "assets" / "items"
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


def recipe(station, *ingredients):
    return {
        "station": station,
        "ingredients": [{"item": item_id, "count": count} for item_id, count in ingredients],
    }


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
    item("corn", "玉米", "#e1bd38", "crop", 99, "采集玉米获得", {"food": 1}),
    item("potato", "土豆", "#a77c4f", "crop", 99, "采集土豆获得", {"food": 1}),
    item("sweet_potato", "红薯", "#9d5543", "crop", 99, "采集红薯获得", {"food": 1}),
    item("cabbage", "白菜", "#7fb75b", "crop", 99, "采集白菜获得", {"food": 1}),
    item("iron_ingot", "铁锭", "#9ba6a9", "ingot", 50, "熔炉合成", {"material_grade": 2}, recipe("furnace", ("iron_ore", 2), ("wood", 1))),
    item("copper_ingot", "铜锭", "#cf7f4d", "ingot", 50, "熔炉合成", {"material_grade": 2}, recipe("furnace", ("copper_ore", 2), ("wood", 1))),
    item("silver_ingot", "银锭", "#d2dadc", "ingot", 50, "熔炉合成", {"material_grade": 3}, recipe("furnace", ("silver_ore", 2), ("wood", 1))),
    item("gold_ingot", "金锭", "#e2ba45", "ingot", 50, "熔炉合成", {"material_grade": 3}, recipe("furnace", ("gold_ore", 2), ("wood", 1))),
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
]


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

    if category == "wood":
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

    return image


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
    catalog = {
        "version": 1,
        "items": [{key: value for key, value in definition.items() if key != "color"} for definition in ITEMS],
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
