#pragma once

#include "scene.h"

#include <windows.h>

namespace rpg {

void DrawSceneObject(HDC hdc, const SceneObject& object, float cameraX, float cameraY, bool selected = false);
void DrawSceneObjectCollision(HDC hdc, const SceneObject& object, float cameraX, float cameraY, COLORREF color);
void ReleaseSceneRenderResources();

} // namespace rpg
