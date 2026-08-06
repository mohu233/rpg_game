#include "../src/scene.h"

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

int main() {
    std::string error;
    if (!rpg::ReloadObjectDefs(&error)) {
        return Fail("failed to load object modules: " + error);
    }
    if (!rpg::FindObjectDef("tree_oak") ||
        !rpg::FindObjectDef("stone_round") ||
        !rpg::FindObjectDef("bush")) {
        return Fail("missing one of the built-in object modules");
    }
    if (!rpg::ReloadTerrainDefs(&error)) {
        return Fail("failed to load terrain modules: " + error);
    }
    if (rpg::NaturalTerrainDefs().size() != 7 || rpg::BuiltTerrainDefs().size() != 2) {
        return Fail("unexpected terrain module count");
    }

    rpg::Scene original;
    const std::filesystem::path scenePath = std::filesystem::path(RPG_ASSET_DIR) / L"scenes/demo_scene.json";
    if (!rpg::LoadSceneFromFile(scenePath, original, &error)) {
        return Fail("failed to load scene: " + error);
    }
    if (rpg::NaturalTerrainAt(original, 22, 3) != "gravel" ||
        rpg::NaturalTerrainAt(original, 4, 15) != "none" ||
        rpg::NaturalTerrainAt(original, 6, 8) != "none" ||
        rpg::NaturalTerrainAt(original, 19, 14) != "none" ||
        rpg::BuiltTerrainAt(original, 6, 3) != "none" ||
        rpg::BuiltTerrainAt(original, 20, 14) != "wood_floor") {
        return Fail("terrain layers did not decode as expected");
    }

    bool foundBlockedTerrain = false;
    for (int y = 0; y < rpg::kMapHeight && !foundBlockedTerrain; ++y) {
        for (int x = 0; x < rpg::kMapWidth && !foundBlockedTerrain; ++x) {
            const std::string_view terrainId = rpg::NaturalTerrainAt(original, x, y);
            const rpg::TerrainDef* terrain = rpg::FindTerrainDef(terrainId, rpg::TerrainLayer::Natural);
            if (!terrain || !terrain->blocksMovement) {
                continue;
            }

            foundBlockedTerrain = rpg::CircleIntersectsBlockedTerrain(
                original,
                {x * 48.0f + 24.0f, y * 48.0f + 24.0f},
                8.0f);
        }
    }
    if (!foundBlockedTerrain) {
        return Fail("water terrain should block movement");
    }

    const std::filesystem::path roundTripPath = std::filesystem::current_path() / L"scene_roundtrip_test.json";
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
            !NearlyEqual(loaded.objects[i].pos.y, original.objects[i].pos.y)) {
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

    return 0;
}
