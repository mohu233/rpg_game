#pragma once

#include "scene.h"

#include <windows.h>

namespace rpg {

void InvalidateTerritoryRenderCache();
void DrawTerritoryBoundary(
    HDC hdc,
    const Scene& scene,
    float cameraX,
    float cameraY,
    float scale,
    int viewportWidth,
    int viewportHeight);
void ReleaseTerritoryRenderResources();

} // namespace rpg
