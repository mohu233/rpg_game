#include "../src/scene.h"
#include "../src/world_layers.h"
#include "../src/save_game.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifndef RPG_ASSET_DIR
#define RPG_ASSET_DIR L"assets"
#endif

namespace {

bool NearlyEqual(float a, float b) {
    return std::fabs(a - b) < 0.001f;
}

int Fail(const std::string& message) {
    std::cerr << message << '\n';
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    std::string error;
    if (!rpg::ReloadObjectDefs(&error)) {
        return Fail("failed to load object modules: " + error);
    }
    if (!rpg::FindObjectDef("tree_oak") ||
        !rpg::FindObjectDef("stone_round") ||
        !rpg::FindObjectDef("bush")) {
        return Fail("missing one of the built-in object modules");
    }
    constexpr const char* goodsBuildings[] = {
        "farmland", "flower_bed", "pond", "stone_floor", "wood_floor",
        "warehouse", "campfire", "apothecary", "kitchen", "furnace", "sawmill",
        "cottage_4x3", "teleport_point",
    };
    for (const char* id : goodsBuildings) {
        const rpg::SceneObjectDef* def = rpg::FindObjectDef(id);
        if (!def || !def->building || !def->placeable) {
            return Fail(std::string("missing or non-placeable goods building: ") + id);
        }
    }
    const rpg::SceneObjectDef* altarDef = rpg::FindObjectDef("ending_ritual_altar");
    if (!altarDef || !altarDef->building || altarDef->placeable || altarDef->draggable ||
        altarDef->footprintWidth != 5 || altarDef->footprintHeight != 4) {
        return Fail("ending ritual altar definition is missing or invalid");
    }
    if (!rpg::ReloadTerrainDefs(&error)) {
        return Fail("failed to load terrain modules: " + error);
    }
    if (argc == 3 && std::string_view(argv[1]) == "--generate-world") {
        if (!rpg::EnsureWorldLayerFiles(std::filesystem::u8path(argv[2]), &error)) {
            return Fail("failed to generate world layers: " + error);
        }
        return 0;
    }
    if (rpg::NaturalTerrainDefs().size() != 7 || rpg::BuiltTerrainDefs().size() != 2) {
        return Fail("unexpected terrain module count");
    }

    rpg::Scene original;
    const std::filesystem::path scenePath = std::filesystem::path(RPG_ASSET_DIR) / L"scenes/demo_scene.json";
    if (!rpg::LoadSceneFromFile(scenePath, original, &error)) {
        return Fail("failed to load scene: " + error);
    }
    if (rpg::NaturalTerrainAt(original, 0, 0) != "none" ||
        rpg::NaturalTerrainAt(original, 4, 0) != "grass" ||
        rpg::NaturalTerrainAt(original, 27, 17) != "grass" ||
        rpg::BuiltTerrainAt(original, 0, 0) != "none" ||
        rpg::BuiltTerrainAt(original, 27, 17) != "none") {
        return Fail("terrain layers did not decode as expected");
    }

    rpg::Scene waterCollisionScene;
    rpg::SetNaturalTerrain(waterCollisionScene, 2, 2, "shallow_water");
    if (!rpg::CircleIntersectsBlockedTerrain(
            waterCollisionScene,
            {2.0f * rpg::kTileSize + rpg::kTileSize * 0.5f,
             2.0f * rpg::kTileSize + rpg::kTileSize * 0.5f},
            8.0f)) {
        return Fail("water terrain should block movement");
    }

    const std::filesystem::path roundTripPath = std::filesystem::current_path() / L"scene_roundtrip_test.json";
    rpg::SetTerritory(original, 8, 8, true);
    rpg::SceneObject plantedFarmland = rpg::MakeObject("farmland", {408.0f, 432.0f}, 9901);
    plantedFarmland.id = "roundtrip_farmland";
    plantedFarmland.cropId = "rice";
    plantedFarmland.cropGrowthDays = 1;
    original.objects.push_back(plantedFarmland);
    if (!rpg::SaveSceneToFile(roundTripPath, original, &error)) {
        return Fail("failed to save round-trip scene: " + error);
    }

    rpg::Scene loaded;
    const bool loadedOk = rpg::LoadSceneFromFile(roundTripPath, loaded, &error);
    std::error_code removeError;
    std::filesystem::remove(roundTripPath, removeError);
    if (!loadedOk) {
        return Fail("failed to reload round-trip scene: " + error);
    }
    if (loaded.naturalTerrain != original.naturalTerrain || loaded.builtTerrain != original.builtTerrain) {
        return Fail("terrain layers changed during round trip");
    }
    if (loaded.backgroundImagePath != original.backgroundImagePath) {
        return Fail("background image path changed during round trip");
    }
    if (loaded.objects.size() != original.objects.size()) {
        return Fail("scene objects changed during round trip");
    }
    for (size_t i = 0; i < original.objects.size(); ++i) {
        if (loaded.objects[i].id != original.objects[i].id ||
            loaded.objects[i].type != original.objects[i].type ||
            !NearlyEqual(loaded.objects[i].pos.x, original.objects[i].pos.x) ||
            !NearlyEqual(loaded.objects[i].pos.y, original.objects[i].pos.y) ||
            loaded.objects[i].cropId != original.objects[i].cropId ||
            loaded.objects[i].cropGrowthDays != original.objects[i].cropGrowthDays) {
            return Fail("scene object changed during round trip");
        }
    }

    const std::filesystem::path legacyPath = std::filesystem::current_path() / L"scene_legacy_test.json";
    {
        std::ofstream legacy(legacyPath, std::ios::binary);
        legacy << "{\"version\":2,\"terrain\":{\"natural\":[\"gdsr\"],\"built\":[\".sw.\"]},\"objects\":[]}";
    }
    rpg::Scene legacy;
    const bool legacyOk = rpg::LoadSceneFromFile(legacyPath, legacy, &error);
    std::filesystem::remove(legacyPath, removeError);
    if (!legacyOk ||
        rpg::NaturalTerrainAt(legacy, 0, 0) != "grass" ||
        rpg::NaturalTerrainAt(legacy, 1, 0) != "dirt" ||
        rpg::NaturalTerrainAt(legacy, 2, 0) != "sand" ||
        rpg::NaturalTerrainAt(legacy, 3, 0) != "gravel" ||
        rpg::BuiltTerrainAt(legacy, 0, 0) != "none" ||
        rpg::BuiltTerrainAt(legacy, 1, 0) != "stone_floor" ||
        rpg::BuiltTerrainAt(legacy, 2, 0) != "wood_floor") {
        return Fail("legacy terrain format did not load correctly");
    }
    if (!rpg::TerritoryAt(loaded, 8, 8) || rpg::TerritoryAt(loaded, 9, 8)) {
        return Fail("territory changed during round trip");
    }

    const std::filesystem::path largePath = std::filesystem::current_path() / L"scene_large_test.json";
    {
        std::ofstream large(largePath, std::ios::binary);
        large << "{\"map\":{\"width\":500,\"height\":500},\"objects\":[]}";
    }
    rpg::Scene large;
    const bool largeOk = rpg::LoadSceneFromFile(largePath, large, &error);
    std::filesystem::remove(largePath, removeError);
    if (!largeOk ||
        large.mapWidth != rpg::kMaximumMapDimension ||
        large.mapHeight != rpg::kMaximumMapDimension ||
        large.naturalTerrain.size() != 250000 ||
        rpg::SceneSupportsBackgroundImage(large)) {
        return Fail("large map limits or background policy are incorrect");
    }

    const std::filesystem::path oversizedPath = std::filesystem::current_path() / L"scene_oversized_test.json";
    {
        std::ofstream oversized(oversizedPath, std::ios::binary);
        oversized << "{\"map\":{\"width\":900,\"height\":700},\"objects\":[]}";
    }
    rpg::Scene oversized;
    const bool oversizedOk = rpg::LoadSceneFromFile(oversizedPath, oversized, &error);
    std::filesystem::remove(oversizedPath, removeError);
    if (!oversizedOk ||
        oversized.mapWidth != rpg::kMaximumMapDimension ||
        oversized.mapHeight != rpg::kMaximumMapDimension) {
        return Fail("oversized map dimensions were not clamped");
    }

    const auto& layers = rpg::WorldLayers();
    if (layers.size() != 4 ||
        layers[0].minSize != 300 || layers[0].maxSize != 500 ||
        layers[1].minSize != 200 || layers[1].maxSize != 350 ||
        layers[2].minSize != 150 || layers[2].maxSize != 250 ||
        layers[3].minSize != 100 || layers[3].maxSize != 180) {
        return Fail("world layer size definitions are incorrect");
    }

    for (const rpg::WorldLayerDef& layer : layers) {
        rpg::Scene generated = rpg::GenerateWorldLayer(layer, layer.defaultWidth, layer.defaultHeight, 1.0f);
        if (generated.mapWidth < layer.minSize || generated.mapWidth > layer.maxSize ||
            generated.mapHeight < layer.minSize || generated.mapHeight > layer.maxSize) {
            return Fail("generated world layer has invalid dimensions");
        }
        int portalCount = 0;
        int altarCount = 0;
        std::vector<rpg::Vec2> portalPositions;
        for (const rpg::SceneObject& object : generated.objects) {
            if (object.type == "ending_ritual_altar") ++altarCount;
            if (!rpg::ObjectIsTeleport(object)) {
                continue;
            }
            ++portalCount;
            portalPositions.push_back(object.pos);
            if (object.targetScene.empty() || object.targetId.empty()) {
                return Fail("generated portal is missing its destination");
            }
            const int tx = static_cast<int>(object.pos.x / rpg::kTileSize);
            const int ty = static_cast<int>(object.pos.y / rpg::kTileSize) - 1;
            if (tx != 1 && tx != generated.mapWidth - 2 && ty != 1 && ty != generated.mapHeight - 2) {
                return Fail("generated portal is not on a map edge");
            }
        }
        const int expectedPortals = layer.index == 1 || layer.index == 4 ? 1 : 2;
        if (portalCount != expectedPortals || altarCount != (layer.index == 4 ? 1 : 0) ||
            generated.objects.size() <= static_cast<size_t>(portalCount + altarCount)) {
            return Fail("generated layer has incorrect portals or no resources");
        }
        if (portalPositions.size() == 2) {
            const float dx = portalPositions[0].x - portalPositions[1].x;
            const float dy = portalPositions[0].y - portalPositions[1].y;
            if (dx * dx + dy * dy < std::pow(generated.mapWidth * rpg::kTileSize, 2.0f)) {
                return Fail("generated portals are closer than one map width");
            }
        }
    }

    const std::filesystem::path savesRoot = std::filesystem::current_path() / L"save_game_test_root";
    std::filesystem::remove_all(savesRoot, removeError);
    rpg::SaveGameInfo saveRequest;
    saveRequest.name = L"测试存档";
    saveRequest.difficulty = rpg::GameDifficulty::Easy;
    saveRequest.resourceMultiplier = 1.5f;
    saveRequest.inventory.push_back({3, "wood", 7});
    saveRequest.inventory.push_back({40, "gathering_stone", 1});
    saveRequest.heldItemId = "small_potion";
    saveRequest.heldItemCount = 1;
    saveRequest.selectedHotbar = 3;
    saveRequest.followerNpcIndices = {1, 3};
    saveRequest.territoryLevel = 2;
    saveRequest.territoryStability = 67.5f;
    saveRequest.territoryDayProgress = 0.625f;
    saveRequest.territoryDaysPassed = 9;
    saveRequest.exploredMaps.push_back({L"veil_lands.json", 9, {1, 0, 1, 1, 0, 0, 1, 0, 1}});
    saveRequest.exploredMaps.push_back({L"ancient_altar.json", 5, {0, 1, 0, 1, 1}});
    std::vector<std::uint8_t> largeExploredMap(160000, 0);
    largeExploredMap[0] = 1;
    largeExploredMap[79999] = 1;
    largeExploredMap[159999] = 1;
    saveRequest.exploredMaps.push_back({L"large_map.json", static_cast<int>(largeExploredMap.size()), largeExploredMap});
    rpg::SaveGameInfo::ResidentEntry worker;
    worker.name = L"青栀";
    worker.x = 144.0f;
    worker.y = 192.0f;
    worker.spiritStoneMask = 5;
    worker.health = 52;
    worker.affinity = 73;
    worker.personality = L"勤勉";
    worker.taskMode = 2;
    worker.gatheringTarget = "exotic_wood";
    worker.workMap = L"veil_lands.json";
    worker.cargo.push_back({0, "exotic_wood", 12});
    worker.cargo.push_back({4, "stone", 7});
    saveRequest.residents.push_back(worker);
    if (!rpg::CreateSaveGame(savesRoot, saveRequest, &error)) {
        return Fail("failed to create save game: " + error);
    }
    const std::filesystem::path saveDirectory = rpg::SaveDirectory(savesRoot, saveRequest.name);
    rpg::SaveGameInfo loadedSave;
    const bool saveLoaded = rpg::LoadSaveGame(saveDirectory, loadedSave, &error);
    const auto listedSaves = rpg::ListSaveGames(savesRoot);
    rpg::Scene savedFirstLayer;
    const bool layerLoaded = rpg::LoadSceneFromFile(
        saveDirectory / L"maps" / layers.front().fileName,
        savedFirstLayer,
        &error);
    std::ifstream savedJsonFile(saveDirectory / L"save.json", std::ios::binary);
    const std::string savedJson{std::istreambuf_iterator<char>(savedJsonFile), std::istreambuf_iterator<char>()};
    savedJsonFile.close();
    const bool saveDeleted = rpg::DeleteSaveGame(savesRoot, saveDirectory, &error);
    const bool saveStillExists = std::filesystem::exists(saveDirectory);
    const bool rootDeletionRejected = !rpg::DeleteSaveGame(savesRoot, savesRoot, &error);
    const bool rootStillExists = std::filesystem::exists(savesRoot);
    std::filesystem::remove_all(savesRoot, removeError);
    if (!saveLoaded || !layerLoaded || !saveDeleted || saveStillExists || !rootDeletionRejected ||
        !rootStillExists || listedSaves.size() != 1 ||
        loadedSave.name != saveRequest.name ||
        loadedSave.difficulty != rpg::GameDifficulty::Easy ||
        !NearlyEqual(loadedSave.resourceMultiplier, 1.5f) ||
        loadedSave.inventory.size() != 2 || loadedSave.inventory.front().slot != 3 ||
        loadedSave.inventory.front().itemId != "wood" || loadedSave.inventory.front().count != 7 ||
        loadedSave.inventory[1].slot != 40 || loadedSave.inventory[1].itemId != "gathering_stone" ||
        loadedSave.heldItemId != "small_potion" || loadedSave.heldItemCount != 1 ||
        loadedSave.selectedHotbar != 3 ||
        loadedSave.followerNpcIndices != std::vector<int>({1, 3}) ||
        loadedSave.territoryLevel != 2 ||
        !NearlyEqual(loadedSave.territoryStability, 67.5f) ||
        !NearlyEqual(loadedSave.territoryDayProgress, 0.625f) ||
        loadedSave.territoryDaysPassed != 9 ||
        loadedSave.exploredMaps.size() != 3 ||
        loadedSave.exploredMaps[0].mapName != L"veil_lands.json" ||
        loadedSave.exploredMaps[0].tiles != std::vector<std::uint8_t>({1, 0, 1, 1, 0, 0, 1, 0, 1}) ||
        loadedSave.exploredMaps[1].mapName != L"ancient_altar.json" ||
        loadedSave.exploredMaps[1].tiles != std::vector<std::uint8_t>({0, 1, 0, 1, 1}) ||
        loadedSave.exploredMaps[2].mapName != L"large_map.json" ||
        loadedSave.exploredMaps[2].tileCount != static_cast<int>(largeExploredMap.size()) ||
        loadedSave.exploredMaps[2].tiles != largeExploredMap ||
        loadedSave.residents.size() != 1 ||
        loadedSave.residents[0].taskMode != 2 ||
        loadedSave.residents[0].gatheringTarget != "exotic_wood" ||
        loadedSave.residents[0].workMap != L"veil_lands.json" ||
        loadedSave.residents[0].cargo.size() != 2 ||
        loadedSave.residents[0].cargo[0].slot != 0 ||
        loadedSave.residents[0].cargo[0].itemId != "exotic_wood" ||
        loadedSave.residents[0].cargo[0].count != 12 ||
        loadedSave.residents[0].cargo[1].slot != 4 ||
        loadedSave.residents[0].cargo[1].itemId != "stone" ||
        loadedSave.residents[0].cargo[1].count != 7 ||
        savedJson.find("\"explored_maps\"") == std::string::npos ||
        savedJson.find("\"bits\": \"b28\"") == std::string::npos ||
        savedJson.find("\"bits\": \"58\"") == std::string::npos ||
        savedFirstLayer.mapWidth != layers.front().minSize ||
        savedFirstLayer.mapHeight != layers.front().minSize) {
        return Fail("save game generation or round trip failed");
    }

    return 0;
}
