#pragma once

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
    std::vector<SceneObject> objects;
};

const std::vector<SceneObjectDef>& ObjectDefs();
const SceneObjectDef* FindObjectDef(std::string_view type);

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
