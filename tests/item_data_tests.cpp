#include "item.h"

#include <iostream>
#include <string>

int main() {
    std::string error;
    if (!rpg::ReloadItemDefs(&error)) {
        std::cerr << "failed to load item definitions: " << error << '\n';
        return 1;
    }
    if (rpg::ItemDefs().size() != 5) {
        std::cerr << "expected five item definitions\n";
        return 1;
    }

    const rpg::ItemDef* potion = rpg::FindItemDef("small_potion");
    const rpg::ItemDef* coin = rpg::FindItemDef("gold_coin");
    const rpg::ItemDef* ore = rpg::FindItemDef("iron_ore");
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
    return 0;
}
