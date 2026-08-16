#pragma once

#include "scene.h"

#include <array>
#include <filesystem>
#include <string>

namespace rpg {

struct WorldLayerDef {
    int index = 0;
    const wchar_t* displayName = L"";
    const wchar_t* fileName = L"";
    int minSize = 1;
    int maxSize = kMaximumMapDimension;
    int defaultWidth = kMapWidth;
    int defaultHeight = kMapHeight;
    float treeRate = 0.0f;
    float resourceRate = 0.0f;
    const char* terrainId = "grass";
};

const std::array<WorldLayerDef, 4>& WorldLayers();
const WorldLayerDef* FindWorldLayer(const std::filesystem::path& path);
std::filesystem::path WorldLayerPath(const std::filesystem::path& mapsRoot, const WorldLayerDef& layer);
bool EnsureWorldLayerLandmarks(Scene& scene, const WorldLayerDef& layer);
Scene GenerateWorldLayer(const WorldLayerDef& layer, int width, int height, float resourceMultiplier = 1.0f);
void ResizeWorldLayer(Scene& scene, const WorldLayerDef& layer, int width, int height);
void RegenerateWorldLayerResources(Scene& scene, const WorldLayerDef& layer);
bool EnsureWorldLayerFiles(const std::filesystem::path& mapsRoot, std::string* error = nullptr);

} // namespace rpg
