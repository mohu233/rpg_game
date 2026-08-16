#include "item.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <windows.h>

namespace rpg {
namespace {

#ifndef RPG_ASSET_DIR
#define RPG_ASSET_DIR L"assets"
#endif

std::vector<ItemDef> g_itemDefs;
bool g_loaded = false;

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::optional<std::string> StringField(const std::string& text, const char* name) {
    const std::regex pattern(std::string("\\\"") + name + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch match;
    if (!std::regex_search(text, match, pattern)) {
        return std::nullopt;
    }
    return match[1].str();
}

std::optional<int> IntField(const std::string& text, const char* name) {
    const std::regex pattern(std::string("\\\"") + name + "\\\"\\s*:\\s*(-?[0-9]+)");
    std::smatch match;
    if (!std::regex_search(text, match, pattern)) {
        return std::nullopt;
    }
    return std::stoi(match[1].str());
}

std::uint32_t ParseColor(const std::string& value) {
    if (value.size() != 7 || value[0] != '#') {
        return 0x7a9cb5;
    }
    try {
        return static_cast<std::uint32_t>(std::stoul(value.substr(1), nullptr, 16));
    } catch (...) {
        return 0x7a9cb5;
    }
}

void ParseProperties(const std::string& text, ItemDef& def) {
    const size_t key = text.find("\"properties\"");
    const size_t open = key == std::string::npos ? key : text.find('{', key);
    const size_t close = open == std::string::npos ? open : text.find('}', open);
    if (open == std::string::npos || close == std::string::npos) {
        return;
    }
    const std::string block = text.substr(open + 1, close - open - 1);
    const std::regex numberPattern("\\\"([^\\\"]+)\\\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
    for (auto it = std::sregex_iterator(block.begin(), block.end(), numberPattern); it != std::sregex_iterator(); ++it) {
        def.properties[(*it)[1].str()] = std::stof((*it)[2].str());
    }
}

bool LoadDefinition(const std::filesystem::path& path, ItemDef& def, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot open " + path.generic_u8string();
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string text = buffer.str();

    const auto id = StringField(text, "id");
    const auto name = StringField(text, "display_name");
    if (!id || id->empty() || !name || name->empty()) {
        error = "item requires id and display_name: " + path.generic_u8string();
        return false;
    }
    def.id = *id;
    def.displayName = Utf8ToWide(*name);
    def.iconPath = Utf8ToWide(StringField(text, "icon").value_or(""));
    def.worldImagePath = Utf8ToWide(StringField(text, "world_image").value_or(""));
    def.maxStack = std::clamp(IntField(text, "max_stack").value_or(1), 1, 999);
    def.colorRgb = ParseColor(StringField(text, "color").value_or("#7a9cb5"));
    ParseProperties(text, def);
    return true;
}

} // namespace

const std::vector<ItemDef>& ItemDefs() {
    if (!g_loaded) {
        ReloadItemDefs();
    }
    return g_itemDefs;
}

const ItemDef* FindItemDef(std::string_view id) {
    const auto& defs = ItemDefs();
    const auto it = std::find_if(defs.begin(), defs.end(), [id](const ItemDef& def) { return def.id == id; });
    return it == defs.end() ? nullptr : &*it;
}

bool ReloadItemDefs(std::string* error) {
    g_loaded = true;
    g_itemDefs.clear();
    std::string messages;
    const std::filesystem::path root = std::filesystem::path(RPG_ASSET_DIR) / L"items";
    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) {
        if (error) {
            *error = "items directory does not exist";
        }
        return false;
    }

    std::vector<std::filesystem::path> definitionPaths;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    for (std::filesystem::recursive_directory_iterator it(root, options, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file() && it->path().filename() == L"item.json") {
            definitionPaths.push_back(it->path());
        }
    }
    if (ec) {
        if (error) {
            *error = "failed to scan item definitions: " + ec.message();
        }
        return false;
    }
    std::sort(definitionPaths.begin(), definitionPaths.end());

    for (const std::filesystem::path& definitionPath : definitionPaths) {
        ItemDef def;
        std::string itemError;
        if (LoadDefinition(definitionPath, def, itemError)) {
            const bool duplicate = std::any_of(g_itemDefs.begin(), g_itemDefs.end(), [&def](const ItemDef& item) {
                return item.id == def.id;
            });
            if (!duplicate) {
                g_itemDefs.push_back(std::move(def));
            } else {
                messages += "duplicate item id '" + def.id + "': " + definitionPath.generic_u8string() + "\n";
            }
        } else {
            messages += itemError + "\n";
        }
    }
    std::sort(g_itemDefs.begin(), g_itemDefs.end(), [](const ItemDef& a, const ItemDef& b) { return a.id < b.id; });
    if (error) {
        *error = messages;
    }
    return !g_itemDefs.empty();
}

} // namespace rpg
