#include "item.h"

#include <iostream>
#include <string>

int main() {
    std::string error;
    if (!rpg::ReloadItemDefs(&error)) {
        std::cerr << "failed to load item definitions: " << error << '\n';
        return 1;
    }
    if (rpg::ItemDefs().size() != 59) {
        std::cerr << "expected 59 item definitions\n";
        return 1;
    }

    constexpr const char* gameplayItems[] = {
        "wood", "exotic_wood", "stone", "iron_ore", "copper_ore", "silver_ore", "gold_ore",
        "ruby", "emerald", "sapphire", "topaz", "amethyst", "anchor_fragment", "exotic_ore",
        "healing_herb", "berry", "wheat", "fodder",
        "rice", "rice_seed", "corn", "corn_seed", "potato", "potato_seed",
        "sweet_potato", "sweet_potato_seed", "cabbage", "cabbage_seed",
        "iron_ingot", "copper_ingot", "silver_ingot", "gold_ingot", "plank", "exotic_plank",
        "gathering_stone", "gathering_stone_ii", "gathering_stone_iii",
        "combat_stone", "combat_stone_ii", "combat_stone_iii",
        "building_stone", "building_stone_ii", "building_stone_iii", "small_potion",
        "farmland", "flower_bed", "pond", "stone_floor", "wood_floor",
        "warehouse", "campfire", "apothecary", "kitchen", "furnace", "sawmill",
    };
    for (const char* id : gameplayItems) {
        const rpg::ItemDef* item = rpg::FindItemDef(id);
        if (!item || item->iconPath.empty() || item->worldImagePath.empty()) {
            std::cerr << "missing gameplay item or image: " << id << '\n';
            return 1;
        }
    }

    const rpg::ItemDef* potion = rpg::FindItemDef("small_potion");
    const rpg::ItemDef* coin = rpg::FindItemDef("gold_coin");
    const rpg::ItemDef* ore = rpg::FindItemDef("iron_ore");
    const rpg::ItemDef* gatheringStone = rpg::FindItemDef("gathering_stone");
    const rpg::ItemDef* buildingStone = rpg::FindItemDef("building_stone");
    const rpg::ItemDef* combatStone = rpg::FindItemDef("combat_stone");
    const rpg::ItemDef* territoryAnchor = rpg::FindItemDef("territory_anchor");
    const rpg::ItemDef* cottage = rpg::FindItemDef("cottage_4x3");
    const rpg::ItemDef* teleportPoint = rpg::FindItemDef("teleport_point");
    const rpg::ItemDef* riceSeed = rpg::FindItemDef("rice_seed");
    const rpg::ItemDef* farmland = rpg::FindItemDef("farmland");
    const rpg::ItemDef* warehouse = rpg::FindItemDef("warehouse");
    const rpg::ItemDef* sawmill = rpg::FindItemDef("sawmill");
    const rpg::ItemDef* plank = rpg::FindItemDef("plank");
    if (!potion || potion->maxStack != 20 || potion->properties.at("heal") != 30.0f ||
        potion->iconPath.empty() || potion->worldImagePath.empty()) {
        std::cerr << "small_potion data mismatch\n";
        return 1;
    }
    if (!coin || coin->maxStack != 999 || coin->properties.at("currency") != 1.0f) {
        std::cerr << "gold_coin data mismatch\n";
        return 1;
    }
    if (!ore || ore->maxStack != 50 || ore->properties.at("weight") != 2.5f) {
        std::cerr << "iron_ore data mismatch\n";
        return 1;
    }
    if (!gatheringStone || gatheringStone->properties.at("spirit_skill") != 1.0f ||
        !buildingStone || buildingStone->properties.at("spirit_skill") != 2.0f ||
        !combatStone || combatStone->properties.at("spirit_skill") != 3.0f) {
        std::cerr << "spirit stone data mismatch\n";
        return 1;
    }
    if (!territoryAnchor || territoryAnchor->properties.at("territory_radius") != 3.0f) {
        std::cerr << "territory anchor data mismatch\n";
        return 1;
    }
    if (!riceSeed || riceSeed->maxStack != 99 || riceSeed->properties.at("seed") != 1.0f ||
        riceSeed->properties.at("crop_days") != 2.0f || riceSeed->properties.at("harvest_count") != 2.0f) {
        std::cerr << "crop seed data mismatch\n";
        return 1;
    }
    if (!farmland || farmland->properties.at("planting") != 1.0f ||
        !warehouse || warehouse->properties.at("storage_slots") != 500.0f ||
        !sawmill || sawmill->properties.at("sawmill") != 1.0f ||
        !plank || plank->maxStack != 99) {
        std::cerr << "building item data mismatch\n";
        return 1;
    }
    const rpg::ItemDef* gatheringStoneIII = rpg::FindItemDef("gathering_stone_iii");
    const rpg::ItemDef* combatStoneIII = rpg::FindItemDef("combat_stone_iii");
    const rpg::ItemDef* buildingStoneIII = rpg::FindItemDef("building_stone_iii");
    if (!gatheringStoneIII || gatheringStoneIII->properties.at("stone_tier") != 3.0f ||
        !combatStoneIII || combatStoneIII->properties.at("life_steal") != 10.0f ||
        !buildingStoneIII || buildingStoneIII->properties.at("teleport_building") != 1.0f) {
        std::cerr << "tier-three spirit stone data mismatch\n";
        return 1;
    }
    if (!cottage || cottage->maxStack != 1 || cottage->properties.at("building") != 1.0f || cottage->iconPath.empty() ||
        !teleportPoint || teleportPoint->maxStack != 1 || teleportPoint->properties.at("building") != 1.0f ||
        teleportPoint->iconPath.empty()) {
        std::cerr << "workbench building item data mismatch\n";
        return 1;
    }
    return 0;
}
