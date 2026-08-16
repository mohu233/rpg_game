#include "save_game.h"

#include "world_layers.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <regex>
#include <sstream>
#include <string_view>

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

std::string EncodeExploredBits(const std::vector<std::uint8_t>& tiles, int tileCount) {
    static constexpr char digits[] = "0123456789abcdef";
    const int count = std::clamp(tileCount, 0, kMaximumMapDimension * kMaximumMapDimension);
    std::string encoded((count + 3) / 4, '0');
    for (int i = 0; i < count && i < static_cast<int>(tiles.size()); ++i) {
        if (!tiles[i]) continue;
        const int shift = (3 - (i % 4)) * 1;
        const int current = encoded[i / 4] >= 'a' ? encoded[i / 4] - 'a' + 10 : encoded[i / 4] - '0';
        encoded[i / 4] = digits[current | (1 << shift)];
    }
    return encoded;
}

int HexDigit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

std::vector<std::uint8_t> DecodeExploredBits(const std::string& encoded, int tileCount) {
    const int count = std::clamp(tileCount, 0, kMaximumMapDimension * kMaximumMapDimension);
    std::vector<std::uint8_t> tiles(count, 0);
    for (int i = 0; i < count && i / 4 < static_cast<int>(encoded.size()); ++i) {
        const int nibble = HexDigit(encoded[i / 4]);
        if (nibble < 0) break;
        tiles[i] = (nibble & (1 << (3 - i % 4))) != 0 ? 1 : 0;
    }
    return tiles;
}

size_t JsonValueStart(std::string_view text, std::string_view key) {
    const std::string quotedKey = "\"" + std::string(key) + "\"";
    size_t position = text.find(quotedKey);
    if (position == std::string_view::npos) return position;
    position = text.find(':', position + quotedKey.size());
    if (position == std::string_view::npos) return position;
    ++position;
    while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position]))) ++position;
    return position;
}

bool ReadJsonString(std::string_view text, std::string_view key, std::string& value) {
    size_t position = JsonValueStart(text, key);
    if (position == std::string_view::npos || position >= text.size() || text[position] != '"') return false;
    const size_t start = ++position;
    while (position < text.size()) {
        if (text[position] == '"') {
            value.assign(text.substr(start, position - start));
            return true;
        }
        if (text[position] == '\\') {
            // Save files currently use unescaped map names and hexadecimal bit strings.
            return false;
        }
        ++position;
    }
    return false;
}

bool ReadJsonNonNegativeInt(std::string_view text, std::string_view key, int& value) {
    size_t position = JsonValueStart(text, key);
    if (position == std::string_view::npos || position >= text.size() || !std::isdigit(static_cast<unsigned char>(text[position]))) {
        return false;
    }
    int parsed = 0;
    while (position < text.size() && std::isdigit(static_cast<unsigned char>(text[position]))) {
        const int digit = text[position++] - '0';
        if (parsed > (std::numeric_limits<int>::max() - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    value = parsed;
    return true;
}

std::vector<SaveGameInfo::ExploredMapEntry> DecodeExploredMaps(const std::string& text) {
    std::vector<SaveGameInfo::ExploredMapEntry> maps;
    size_t cursor = JsonValueStart(text, "explored_maps");
    if (cursor == std::string::npos || cursor >= text.size() || text[cursor] != '[') return maps;
    ++cursor;
    while (cursor < text.size()) {
        const size_t objectStart = text.find('{', cursor);
        const size_t arrayEnd = text.find(']', cursor);
        if (arrayEnd == std::string::npos || objectStart == std::string::npos || objectStart > arrayEnd) break;
        const size_t objectEnd = text.find('}', objectStart + 1);
        if (objectEnd == std::string::npos || objectEnd > arrayEnd) break;

        const std::string_view object(text.data() + objectStart, objectEnd - objectStart + 1);
        std::string mapName;
        std::string bits;
        int tileCount = 0;
        if (ReadJsonString(object, "map", mapName) &&
            ReadJsonNonNegativeInt(object, "tile_count", tileCount) &&
            ReadJsonString(object, "bits", bits)) {
            tileCount = std::clamp(tileCount, 0, kMaximumMapDimension * kMaximumMapDimension);
            SaveGameInfo::ExploredMapEntry explored;
            explored.mapName = std::filesystem::u8path(mapName).wstring();
            explored.tileCount = tileCount;
            explored.tiles = DecodeExploredBits(bits, tileCount);
            maps.push_back(std::move(explored));
        }
        cursor = objectEnd + 1;
    }
    return maps;
}

std::string EncodeResidentCargo(const std::vector<SaveGameInfo::InventoryEntry>& cargo) {
    std::ostringstream encoded;
    bool first = true;
    for (const SaveGameInfo::InventoryEntry& entry : cargo) {
        if (entry.slot < 0 || entry.slot >= 10 || entry.itemId.empty() || entry.count <= 0) continue;
        if (!first) encoded << '|';
        encoded << entry.slot << ':' << entry.itemId << ':' << entry.count;
        first = false;
    }
    return encoded.str();
}

std::vector<SaveGameInfo::InventoryEntry> DecodeResidentCargo(const std::string& encoded) {
    std::vector<SaveGameInfo::InventoryEntry> cargo;
    const std::regex entryPattern("([0-9]+):([A-Za-z0-9_]+):([0-9]+)");
    for (std::sregex_iterator it(encoded.begin(), encoded.end(), entryPattern), end; it != end; ++it) {
        const int slot = std::stoi((*it)[1].str());
        const int count = std::stoi((*it)[3].str());
        if (slot >= 0 && slot < 10 && count > 0) cargo.push_back({slot, (*it)[2].str(), count});
    }
    return cargo;
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
           << "  \"version\": 5,\n"
           << "  \"name\": \"" << std::filesystem::path(info.name).generic_u8string() << "\",\n"
           << "  \"difficulty\": \"" << DifficultyId(info.difficulty) << "\",\n"
           << "  \"resource_multiplier\": " << info.resourceMultiplier << ",\n"
           << "  \"current_map\": \"" << std::filesystem::path(info.currentMap).generic_u8string() << "\",\n"
           << "  \"player_x\": " << info.playerPosition.x << ",\n"
           << "  \"player_y\": " << info.playerPosition.y << ",\n"
           << "  \"selected_hotbar\": " << info.selectedHotbar << ",\n"
           << "  \"held_item\": \"" << info.heldItemId << "\",\n"
           << "  \"held_count\": " << info.heldItemCount << ",\n"
           << "  \"territory_level\": " << info.territoryLevel << ",\n"
           << "  \"territory_stability\": " << info.territoryStability << ",\n"
           << "  \"territory_day_progress\": " << info.territoryDayProgress << ",\n"
           << "  \"territory_days_passed\": " << info.territoryDaysPassed << ",\n"
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
               << ", \"personality\": \"" << std::filesystem::path(resident.personality).generic_u8string()
               << "\", \"task\": " << resident.taskMode
               << ", \"gather_target\": \"" << resident.gatheringTarget
               << "\", \"facility_id\": \"" << resident.facilityId
               << "\", \"work_map\": \"" << std::filesystem::path(resident.workMap).generic_u8string()
               << "\", \"cargo\": \"" << EncodeResidentCargo(resident.cargo) << "\"}"
               << (i + 1 == info.residents.size() ? "\n" : ",\n");
    }
    output << "  ],\n"
           << "  \"unlocked_blueprints\": [";
    for (size_t i = 0; i < info.unlockedBlueprints.size(); ++i) {
        output << "\"" << info.unlockedBlueprints[i] << "\""
               << (i + 1 == info.unlockedBlueprints.size() ? "" : ", ");
    }
    output << "],\n"
           << "  \"explored_maps\": [\n";
    for (size_t i = 0; i < info.exploredMaps.size(); ++i) {
        const SaveGameInfo::ExploredMapEntry& explored = info.exploredMaps[i];
        output << "    {\"map\": \"" << std::filesystem::path(explored.mapName).generic_u8string()
               << "\", \"tile_count\": " << explored.tileCount
               << ", \"bits\": \"" << EncodeExploredBits(explored.tiles, explored.tileCount) << "\"}"
               << (i + 1 == info.exploredMaps.size() ? "\n" : ",\n");
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

bool DeleteSaveGame(const std::filesystem::path& root, const std::filesystem::path& directory, std::string* error) {
    std::error_code ec;
    const std::filesystem::path savesRoot = std::filesystem::weakly_canonical(root, ec);
    if (ec) {
        if (error) *error = "failed to resolve saves root";
        return false;
    }
    const std::filesystem::path target = std::filesystem::weakly_canonical(directory, ec);
    if (ec || target.empty() || target == savesRoot || target.parent_path() != savesRoot ||
        !std::filesystem::is_directory(target, ec) || !std::filesystem::is_regular_file(target / L"save.json", ec)) {
        if (error) *error = "refusing to delete path outside saves root";
        return false;
    }
    std::filesystem::remove_all(target, ec);
    if (ec || std::filesystem::exists(target, ec)) {
        if (error) *error = "failed to delete save directory";
        return false;
    }
    return true;
}

bool DeleteSaveGame(const std::filesystem::path& directory, std::string* error) {
    return DeleteSaveGame(DefaultSavesRoot(), directory, error);
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
    info.territoryLevel = std::clamp(static_cast<int>(FindNumber(text, "territory_level", 1.0f)), 1, 4);
    info.territoryStability = std::clamp(FindNumber(text, "territory_stability", 100.0f), 0.0f, 100.0f);
    info.territoryDayProgress = std::clamp(FindNumber(text, "territory_day_progress", 0.0f), 0.0f, 0.9999f);
    info.territoryDaysPassed = std::max(0, static_cast<int>(FindNumber(text, "territory_days_passed", 0.0f)));
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
        "(?:\\s*,\\s*\\\"affinity\\\"\\s*:\\s*([0-9]+)\\s*,\\s*\\\"personality\\\"\\s*:\\s*\\\"([^\\\"]*)\\\")?"
        "(?:\\s*,\\s*\\\"task\\\"\\s*:\\s*([0-3])\\s*,\\s*\\\"gather_target\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"\\s*,\\s*"
        "\\\"facility_id\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"\\s*,\\s*\\\"work_map\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"\\s*,\\s*"
        "\\\"cargo\\\"\\s*:\\s*\\\"([^\\\"]*)\\\")?\\s*\\}");
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
        resident.taskMode = (*it)[9].matched ? std::clamp(std::stoi((*it)[9].str()), 0, 3) : 0;
        resident.gatheringTarget = (*it)[10].matched && !(*it)[10].str().empty() ? (*it)[10].str() : "any";
        resident.facilityId = (*it)[11].matched ? (*it)[11].str() : "";
        resident.workMap = (*it)[12].matched ? std::filesystem::u8path((*it)[12].str()).wstring() : L"";
        resident.cargo = (*it)[13].matched ? DecodeResidentCargo((*it)[13].str())
                                           : std::vector<SaveGameInfo::InventoryEntry>{};
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
    info.unlockedBlueprints.clear();
    const std::regex blueprintsPattern("\\\"unlocked_blueprints\\\"\\s*:\\s*\\[([^\\]]*)\\]");
    std::smatch blueprintsMatch;
    if (std::regex_search(text, blueprintsMatch, blueprintsPattern)) {
        const std::string values = blueprintsMatch[1].str();
        const std::regex idPattern("\\\"([^\\\"]+)\\\"");
        for (std::sregex_iterator it(values.begin(), values.end(), idPattern), end; it != end; ++it) {
            const std::string id = (*it)[1].str();
            if (std::find(info.unlockedBlueprints.begin(), info.unlockedBlueprints.end(), id) == info.unlockedBlueprints.end()) {
                info.unlockedBlueprints.push_back(id);
            }
        }
    }
    if (std::find(info.unlockedBlueprints.begin(), info.unlockedBlueprints.end(), "territory_anchor") == info.unlockedBlueprints.end()) {
        info.unlockedBlueprints.push_back("territory_anchor");
    }
    info.exploredMaps = DecodeExploredMaps(text);
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
