#include "save_game.h"

#include "world_layers.h"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>

#ifndef RPG_ASSET_DIR
#define RPG_ASSET_DIR L"assets"
#endif

namespace rpg {
namespace {

std::string ReadAll(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string FindString(const std::string& text, const char* key, const std::string& fallback = {}) {
    const std::regex pattern(std::string("\\\"") + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch match;
    return std::regex_search(text, match, pattern) ? match[1].str() : fallback;
}

float FindNumber(const std::string& text, const char* key, float fallback) {
    const std::regex pattern(std::string("\\\"") + key + "\\\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
    std::smatch match;
    return std::regex_search(text, match, pattern) ? std::stof(match[1].str()) : fallback;
}

const char* DifficultyId(GameDifficulty difficulty) {
    switch (difficulty) {
    case GameDifficulty::Easy: return "easy";
    case GameDifficulty::Hard: return "hard";
    default: return "normal";
    }
}

GameDifficulty ParseDifficulty(const std::string& value) {
    if (value == "easy") return GameDifficulty::Easy;
    if (value == "hard") return GameDifficulty::Hard;
    return GameDifficulty::Normal;
}

int DifficultySize(const WorldLayerDef& layer, GameDifficulty difficulty) {
    if (difficulty == GameDifficulty::Easy) return layer.minSize;
    if (difficulty == GameDifficulty::Hard) return layer.maxSize;
    return layer.defaultWidth;
}

} // namespace

std::filesystem::path DefaultSavesRoot() {
    return std::filesystem::path(RPG_ASSET_DIR).parent_path() / L"saves";
}

std::filesystem::path SaveDirectory(const std::filesystem::path& root, std::wstring_view name) {
    return root / std::filesystem::path(name);
}

std::filesystem::path SaveMapsDirectory(const std::filesystem::path& root, std::wstring_view name) {
    return SaveDirectory(root, name) / L"maps";
}

const wchar_t* DifficultyName(GameDifficulty difficulty) {
    switch (difficulty) {
    case GameDifficulty::Easy: return L"简单";
    case GameDifficulty::Hard: return L"困难";
    default: return L"普通";
    }
}

bool SaveGameState(const std::filesystem::path& directory, const SaveGameInfo& info, std::string* error) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    std::ofstream output(directory / L"save.json", std::ios::binary | std::ios::trunc);
    if (!output) {
        if (error) *error = "failed to write save.json";
        return false;
    }
    output << "{\n"
           << "  \"version\": 1,\n"
           << "  \"name\": \"" << std::filesystem::path(info.name).generic_u8string() << "\",\n"
           << "  \"difficulty\": \"" << DifficultyId(info.difficulty) << "\",\n"
           << "  \"resource_multiplier\": " << info.resourceMultiplier << ",\n"
           << "  \"current_map\": \"" << std::filesystem::path(info.currentMap).generic_u8string() << "\",\n"
           << "  \"player_x\": " << info.playerPosition.x << ",\n"
           << "  \"player_y\": " << info.playerPosition.y << ",\n"
           << "  \"selected_hotbar\": " << info.selectedHotbar << ",\n"
           << "  \"held_item\": \"" << info.heldItemId << "\",\n"
           << "  \"held_count\": " << info.heldItemCount << ",\n"
           << "  \"followers\": [";
    for (size_t i = 0; i < info.followerNpcIndices.size(); ++i) {
        output << info.followerNpcIndices[i]
               << (i + 1 == info.followerNpcIndices.size() ? "" : ", ");
    }
    output << "],\n"
           << "  \"residents\": [\n";
    for (size_t i = 0; i < info.residents.size(); ++i) {
        const SaveGameInfo::ResidentEntry& resident = info.residents[i];
        output << "    {\"name\": \"" << std::filesystem::path(resident.name).generic_u8string()
               << "\", \"x\": " << resident.x << ", \"y\": " << resident.y
               << ", \"stones\": " << resident.spiritStoneMask
               << ", \"following\": " << (resident.following ? 1 : 0)
               << ", \"health\": " << resident.health
               << ", \"affinity\": " << resident.affinity
               << ", \"personality\": \"" << std::filesystem::path(resident.personality).generic_u8string() << "\"}"
               << (i + 1 == info.residents.size() ? "\n" : ",\n");
    }
    output << "  ],\n"
           << "  \"anchor_storage\": [\n";
    for (size_t i = 0; i < info.anchorStorage.size(); ++i) {
        const SaveGameInfo::InventoryEntry& entry = info.anchorStorage[i];
        output << "    {\"storage_slot\": " << entry.slot << ", \"item\": \"" << entry.itemId
               << "\", \"count\": " << entry.count << "}"
               << (i + 1 == info.anchorStorage.size() ? "\n" : ",\n");
    }
    output << "  ],\n"
           << "  \"inventory\": [\n";
    for (size_t i = 0; i < info.inventory.size(); ++i) {
        const SaveGameInfo::InventoryEntry& entry = info.inventory[i];
        output << "    {\"slot\": " << entry.slot << ", \"item\": \"" << entry.itemId
               << "\", \"count\": " << entry.count << "}"
               << (i + 1 == info.inventory.size() ? "\n" : ",\n");
    }
    output << "  ]\n"
           << "}\n";
    return true;
}

bool LoadSaveGame(const std::filesystem::path& directory, SaveGameInfo& info, std::string* error) {
    const std::string text = ReadAll(directory / L"save.json");
    if (text.empty()) {
        if (error) *error = "save.json is missing";
        return false;
    }
    info.name = std::filesystem::u8path(FindString(text, "name", directory.filename().generic_u8string())).wstring();
    info.difficulty = ParseDifficulty(FindString(text, "difficulty", "normal"));
    info.resourceMultiplier = std::clamp(FindNumber(text, "resource_multiplier", 1.0f), 0.25f, 4.0f);
    info.currentMap = std::filesystem::u8path(FindString(text, "current_map", "veil_lands.json")).wstring();
    info.playerPosition = {FindNumber(text, "player_x", 0.0f), FindNumber(text, "player_y", 0.0f)};
    info.selectedHotbar = std::clamp(static_cast<int>(FindNumber(text, "selected_hotbar", 0.0f)), 0, 9);
    info.heldItemId = FindString(text, "held_item");
    info.heldItemCount = std::max(0, static_cast<int>(FindNumber(text, "held_count", 0.0f)));
    info.followerNpcIndices.clear();
    const std::regex followersPattern("\\\"followers\\\"\\s*:\\s*\\[([^\\]]*)\\]");
    std::smatch followersMatch;
    if (std::regex_search(text, followersMatch, followersPattern)) {
        const std::string values = followersMatch[1].str();
        const std::regex numberPattern("[0-9]+");
        for (std::sregex_iterator it(values.begin(), values.end(), numberPattern), end; it != end; ++it) {
            const int index = std::stoi((*it)[0].str());
            if (index >= 0 && index < 64 &&
                std::find(info.followerNpcIndices.begin(), info.followerNpcIndices.end(), index) == info.followerNpcIndices.end()) {
                info.followerNpcIndices.push_back(index);
            }
        }
        if (info.followerNpcIndices.size() > 4) info.followerNpcIndices.resize(4);
    }
    info.residents.clear();
    const std::regex residentPattern(
        "\\{\\s*\\\"name\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"\\s*,\\s*\\\"x\\\"\\s*:\\s*(-?[0-9.]+)\\s*,\\s*"
        "\\\"y\\\"\\s*:\\s*(-?[0-9.]+)\\s*,\\s*\\\"stones\\\"\\s*:\\s*([0-7])\\s*,\\s*"
        "\\\"following\\\"\\s*:\\s*([01])\\s*,\\s*\\\"health\\\"\\s*:\\s*([0-9]+)"
        "(?:\\s*,\\s*\\\"affinity\\\"\\s*:\\s*([0-9]+)\\s*,\\s*\\\"personality\\\"\\s*:\\s*\\\"([^\\\"]*)\\\")?\\s*\\}");
    for (std::sregex_iterator it(text.begin(), text.end(), residentPattern), end; it != end; ++it) {
        SaveGameInfo::ResidentEntry resident;
        resident.name = std::filesystem::u8path((*it)[1].str()).wstring();
        resident.x = std::stof((*it)[2].str());
        resident.y = std::stof((*it)[3].str());
        resident.spiritStoneMask = std::stoi((*it)[4].str());
        resident.following = (*it)[5].str() == "1";
        resident.health = std::clamp(std::stoi((*it)[6].str()), 1, 60);
        resident.affinity = (*it)[7].matched ? std::clamp(std::stoi((*it)[7].str()), 0, 100) : 50;
        resident.personality = (*it)[8].matched
            ? std::filesystem::u8path((*it)[8].str()).wstring()
            : L"谨慎";
        info.residents.push_back(std::move(resident));
    }
    info.anchorStorage.clear();
    const std::regex storagePattern(
        "\\{\\s*\\\"storage_slot\\\"\\s*:\\s*([0-9]+)\\s*,\\s*\\\"item\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"\\s*,\\s*\\\"count\\\"\\s*:\\s*([0-9]+)\\s*\\}");
    for (std::sregex_iterator it(text.begin(), text.end(), storagePattern), end; it != end; ++it) {
        const int slot = std::stoi((*it)[1].str());
        const int count = std::stoi((*it)[3].str());
        if (slot >= 0 && slot < 100 && count > 0) {
            info.anchorStorage.push_back({slot, (*it)[2].str(), count});
        }
    }
    info.inventory.clear();
    const std::regex entryPattern(
        "\\{\\s*\\\"slot\\\"\\s*:\\s*([0-9]+)\\s*,\\s*\\\"item\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"\\s*,\\s*\\\"count\\\"\\s*:\\s*([0-9]+)\\s*\\}");
    for (std::sregex_iterator it(text.begin(), text.end(), entryPattern), end; it != end; ++it) {
        info.inventory.push_back({std::stoi((*it)[1].str()), (*it)[2].str(), std::stoi((*it)[3].str())});
    }
    return true;
}

std::vector<SaveGameInfo> ListSaveGames(const std::filesystem::path& root) {
    std::vector<SaveGameInfo> saves;
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) return saves;
    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (!entry.is_directory()) continue;
        SaveGameInfo info;
        if (LoadSaveGame(entry.path(), info)) saves.push_back(std::move(info));
    }
    std::sort(saves.begin(), saves.end(), [](const SaveGameInfo& a, const SaveGameInfo& b) { return a.name < b.name; });
    return saves;
}

bool CreateSaveGame(const std::filesystem::path& root, const SaveGameInfo& requested, std::string* error) {
    SaveGameInfo info = requested;
    const std::filesystem::path directory = SaveDirectory(root, info.name);
    const std::filesystem::path maps = directory / L"maps";
    std::error_code ec;
    std::filesystem::create_directories(maps, ec);
    if (ec) {
        if (error) *error = "failed to create save directory";
        return false;
    }

    for (const WorldLayerDef& layer : WorldLayers()) {
        const int size = DifficultySize(layer, info.difficulty);
        Scene scene = GenerateWorldLayer(layer, size, size, info.resourceMultiplier);
        if (!SaveSceneToFile(maps / layer.fileName, scene, error)) return false;
        if (layer.index == 1) info.playerPosition = scene.playerStart;
    }
    info.currentMap = WorldLayers().front().fileName;
    return SaveGameState(directory, info, error);
}

} // namespace rpg
