#pragma once

#include "world.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace rpg {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct RectF {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

enum class CollisionShape {
    None,
    Rect,
    Circle,
};

enum class ObjectVisual {
    TreeOak,
    StoneRound,
    Bush,
};

enum class TerrainLayer {
    Natural,
    Built,
};

struct TerrainDef {
    std::string id;
    std::wstring displayName;
    std::wstring imagePath;
    TerrainLayer layer = TerrainLayer::Natural;
    int priority = 0;
    int variants = 1;
    std::uint32_t fallbackRgb = 0;
};

struct CollisionBody {
    CollisionShape shape = CollisionShape::None;
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float radius = 0.0f;
    bool blocks = true;
};

struct SceneObjectDef {
    std::string type;
    std::wstring displayName;
    std::wstring bitmapPath;
    ObjectVisual visual = ObjectVisual::StoneRound;
    float width = 48.0f;
    float height = 48.0f;
    float zOffset = 0.0f;
    CollisionBody collision;
};

struct SceneObject {
    std::string id;
    std::string type;
    Vec2 pos;
    float zOffset = 0.0f;
    CollisionBody collision;
};

struct Scene {
    Scene();

    std::array<std::string, kMapWidth * kMapHeight> naturalTerrain;
    std::array<std::string, kMapWidth * kMapHeight> builtTerrain;
    std::vector<SceneObject> objects;
};

const std::vector<SceneObjectDef>& ObjectDefs();
const SceneObjectDef* FindObjectDef(std::string_view type);
bool ReloadObjectDefs(std::string* error = nullptr);

const std::vector<TerrainDef>& NaturalTerrainDefs();
const std::vector<TerrainDef>& BuiltTerrainDefs();
const TerrainDef* FindTerrainDef(std::string_view id, TerrainLayer layer);
bool ReloadTerrainDefs(std::string* error = nullptr);

std::string_view NaturalTerrainAt(const Scene& scene, int tx, int ty);
std::string_view BuiltTerrainAt(const Scene& scene, int tx, int ty);
bool SetNaturalTerrain(Scene& scene, int tx, int ty, std::string_view terrainId);
bool SetBuiltTerrain(Scene& scene, int tx, int ty, std::string_view terrainId);

Scene MakeDefaultScene();
SceneObject MakeObject(std::string_view type, Vec2 pos, int index);

bool LoadSceneFromFile(const std::filesystem::path& path, Scene& scene, std::string* error = nullptr);
bool SaveSceneToFile(const std::filesystem::path& path, const Scene& scene, std::string* error = nullptr);

RectF ObjectVisualBounds(const SceneObject& object);
RectF ObjectCollisionRect(const SceneObject& object);
float ObjectSortY(const SceneObject& object);
bool PointInObjectVisual(const SceneObject& object, Vec2 point);
bool CircleIntersectsScene(const Scene& scene, Vec2 center, float radius);

} // namespace rpg
