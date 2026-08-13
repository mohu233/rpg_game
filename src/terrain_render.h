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
    bool showGrid = false,
    float zoom = 1.0f,
    int viewportWidth = 0,
    int viewportHeight = 0);
void ReleaseTerrainRenderResources();

} // namespace rpg
