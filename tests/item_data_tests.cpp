#include "item.h"

#include <iostream>
#include <string>

int main() {
    std::string error;
    if (!rpg::ReloadItemDefs(&error)) {
        std::cerr << "failed to load item definitions: " << error << '\n';
        return 1;
    }
    if (rpg::ItemDefs().size() != 9) {
        std::cerr << "expected nine item definitions\n";
        return 1;
    }

    const rpg::ItemDef* potion = rpg::FindItemDef("small_potion");
    const rpg::ItemDef* coin = rpg::FindItemDef("gold_coin");
    const rpg::ItemDef* ore = rpg::FindItemDef("iron_ore");
    const rpg::ItemDef* gatheringStone = rpg::FindItemDef("gathering_stone");
    const rpg::ItemDef* buildingStone = rpg::FindItemDef("building_stone");
    const rpg::ItemDef* combatStone = rpg::FindItemDef("combat_stone");
    const rpg::ItemDef* territoryAnchor = rpg::FindItemDef("territory_anchor");
    if (!potion || potion->maxStack != 20 || potion->properties.at("heal") != 25.0f ||
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
    return 0;
}
