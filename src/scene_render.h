#pragma once

#include "scene.h"

#include <windows.h>

namespace rpg {

void DrawSceneObject(HDC hdc, const SceneObject& object, float cameraX, float cameraY, bool selected = false, float zoom = 1.0f, BYTE opacity = 255);
void DrawSceneObjectCollision(HDC hdc, const SceneObject& object, float cameraX, float cameraY, COLORREF color, float zoom = 1.0f);
void ReleaseSceneRenderResources();

} // namespace rpg
