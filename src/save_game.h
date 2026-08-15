#pragma once

#include "scene.h"

#include <filesystem>
#include <string>
#include <vector>

namespace rpg {

enum class GameDifficulty {
    Easy,
    Normal,
    Hard,
};

struct SaveGameInfo {
    struct InventoryEntry {
        int slot = 0;
        std::string itemId;
        int count = 0;
    };

    struct ResidentEntry {
        std::wstring name;
        float x = 0.0f;
        float y = 0.0f;
        int spiritStoneMask = 0;
        bool following = false;
        int health = 60;
        int affinity = 50;
        std::wstring personality = L"谨慎";
        int taskMode = 0;
        std::string gatheringTarget = "any";
        std::string facilityId;
        std::wstring workMap;
        std::vector<InventoryEntry> cargo;
    };

    struct ExploredMapEntry {
        std::wstring mapName;
        int tileCount = 0;
        std::vector<std::uint8_t> tiles;
    };

    std::wstring name;
    GameDifficulty difficulty = GameDifficulty::Normal;
    float resourceMultiplier = 1.0f;
    std::wstring currentMap = L"veil_lands.json";
    Vec2 playerPosition{};
    std::vector<InventoryEntry> inventory;
    std::string heldItemId;
    int heldItemCount = 0;
    int selectedHotbar = 0;
    std::vector<int> followerNpcIndices;
    std::vector<ResidentEntry> residents;
    std::vector<InventoryEntry> anchorStorage;
    std::vector<std::string> unlockedBlueprints{"territory_anchor"};
    int territoryLevel = 1;
    float territoryStability = 100.0f;
    float territoryDayProgress = 0.0f;
    int territoryDaysPassed = 0;
    std::vector<ExploredMapEntry> exploredMaps;
};

std::filesystem::path DefaultSavesRoot();
std::filesystem::path SaveDirectory(const std::filesystem::path& root, std::wstring_view name);
std::filesystem::path SaveMapsDirectory(const std::filesystem::path& root, std::wstring_view name);
std::vector<SaveGameInfo> ListSaveGames(const std::filesystem::path& root);
bool CreateSaveGame(const std::filesystem::path& root, const SaveGameInfo& info, std::string* error = nullptr);
bool LoadSaveGame(const std::filesystem::path& directory, SaveGameInfo& info, std::string* error = nullptr);
bool SaveGameState(const std::filesystem::path& directory, const SaveGameInfo& info, std::string* error = nullptr);
bool DeleteSaveGame(const std::filesystem::path& directory, std::string* error = nullptr);
bool DeleteSaveGame(const std::filesystem::path& root, const std::filesystem::path& directory,
                    std::string* error = nullptr);
const wchar_t* DifficultyName(GameDifficulty difficulty);

} // namespace rpg
