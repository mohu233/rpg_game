#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace rpg {

struct ItemDef {
    std::string id;
    std::wstring displayName;
    std::wstring iconPath;
    std::wstring worldImagePath;
    int maxStack = 1;
    std::uint32_t colorRgb = 0x7a9cb5;
    std::map<std::string, float> properties;
};

const std::vector<ItemDef>& ItemDefs();
const ItemDef* FindItemDef(std::string_view id);
bool ReloadItemDefs(std::string* error = nullptr);

} // namespace rpg
