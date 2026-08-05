#pragma once

#include "scene.h"

#include <windows.h>

namespace rpg {

COLORREF TerrainFallbackColor(const TerrainDef& terrain);

void DrawTerrain(
    HDC hdc,
    const Scene& scene,
    float cameraX,
    float cameraY,
    bool showGrid = false);
void ReleaseTerrainRenderResources();

} // namespace rpg
