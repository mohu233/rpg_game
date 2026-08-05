#include "scene.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>

namespace rpg {
namespace {

constexpr CollisionBody kTreeCollision{CollisionShape::Rect, -18.0f, -31.0f, 36.0f, 30.0f, 0.0f, true};
constexpr CollisionBody kStoneCollision{CollisionShape::Rect, -24.0f, -23.0f, 48.0f, 22.0f, 0.0f, true};
constexpr CollisionBody kBushCollision{CollisionShape::Rect, -28.0f, -25.0f, 56.0f, 24.0f, 0.0f, true};

std::wstring ToWide(std::string_view text) {
    return std::wstring(text.begin(), text.end());
}

std::string ShapeToString(CollisionShape shape) {
    switch (shape) {
    case CollisionShape::Rect:
        return "rect";
    case CollisionShape::Circle:
        return "circle";
    case CollisionShape::None:
    default:
        return "none";
    }
}

CollisionShape ShapeFromString(std::string_view shape) {
    if (shape == "rect") {
        return CollisionShape::Rect;
    }
    if (shape == "circle") {
        return CollisionShape::Circle;
    }
    return CollisionShape::None;
}

void SetError(std::string* error, const std::string& text) {
    if (error) {
        *error = text;
    }
}

std::string ReadAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void SkipSpace(const std::string& text, size_t& pos) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
}

std::optional<size_t> FindFieldValue(const std::string& text, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    size_t pos = text.find(needle);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    pos = text.find(':', pos + needle.size());
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    ++pos;
    SkipSpace(text, pos);
    return pos;
}

std::optional<std::string> FindStringField(const std::string& text, std::string_view key) {
    auto value = FindFieldValue(text, key);
    if (!value || *value >= text.size() || text[*value] != '"') {
        return std::nullopt;
    }

    std::string out;
    for (size_t i = *value + 1; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\\' && i + 1 < text.size()) {
            out.push_back(text[++i]);
            continue;
        }
        if (c == '"') {
            return out;
        }
        out.push_back(c);
    }
    return std::nullopt;
}

std::optional<float> FindFloatField(const std::string& text, std::string_view key) {
    auto value = FindFieldValue(text, key);
    if (!value) {
        return std::nullopt;
    }

    size_t end = *value;
    while (end < text.size()) {
        const char c = text[end];
        if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E')) {
            break;
        }
        ++end;
    }

    float out = 0.0f;
    const char* beginPtr = text.data() + *value;
    const char* endPtr = text.data() + end;
    auto result = std::from_chars(beginPtr, endPtr, out);
    if (result.ec != std::errc()) {
        return std::nullopt;
    }
    return out;
}

std::optional<bool> FindBoolField(const std::string& text, std::string_view key) {
    auto value = FindFieldValue(text, key);
    if (!value) {
        return std::nullopt;
    }
    if (text.compare(*value, 4, "true") == 0) {
        return true;
    }
    if (text.compare(*value, 5, "false") == 0) {
        return false;
    }
    return std::nullopt;
}

std::optional<std::string> FindBracketedField(const std::string& text, std::string_view key, char open, char close) {
    auto value = FindFieldValue(text, key);
    if (!value || *value >= text.size() || text[*value] != open) {
        return std::nullopt;
    }

    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (size_t i = *value; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == open) {
            ++depth;
        } else if (c == close) {
            --depth;
            if (depth == 0) {
                return text.substr(*value, i - *value + 1);
            }
        }
    }

    return std::nullopt;
}

std::vector<std::string> ExtractObjectBlocks(const std::string& arrayText) {
    std::vector<std::string> blocks;
    bool inString = false;
    bool escaped = false;
    int depth = 0;
    size_t begin = std::string::npos;

    for (size_t i = 0; i < arrayText.size(); ++i) {
        const char c = arrayText[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '{') {
            if (depth == 0) {
                begin = i;
            }
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0 && begin != std::string::npos) {
                blocks.push_back(arrayText.substr(begin, i - begin + 1));
                begin = std::string::npos;
            }
        }
    }

    return blocks;
}

CollisionBody ParseCollision(const std::string& objectBlock, const SceneObjectDef* def) {
    CollisionBody collision = def ? def->collision : CollisionBody{};
    auto collisionBlock = FindBracketedField(objectBlock, "collision", '{', '}');
    if (!collisionBlock) {
        return collision;
    }

    if (auto shape = FindStringField(*collisionBlock, "shape")) {
        collision.shape = ShapeFromString(*shape);
    }
    if (auto value = FindFloatField(*collisionBlock, "x")) {
        collision.x = *value;
    }
    if (auto value = FindFloatField(*collisionBlock, "y")) {
        collision.y = *value;
    }
    if (auto value = FindFloatField(*collisionBlock, "w")) {
        collision.w = *value;
    }
    if (auto value = FindFloatField(*collisionBlock, "h")) {
        collision.h = *value;
    }
    if (auto value = FindFloatField(*collisionBlock, "radius")) {
        collision.radius = *value;
    }
    if (auto value = FindBoolField(*collisionBlock, "blocks")) {
        collision.blocks = *value;
    }
    return collision;
}

float Clamp(float value, float minValue, float maxValue) {
    return std::max(minValue, std::min(maxValue, value));
}

bool CircleIntersectsRect(Vec2 center, float radius, RectF rect) {
    const float closestX = Clamp(center.x, rect.left, rect.right);
    const float closestY = Clamp(center.y, rect.top, rect.bottom);
    const float dx = center.x - closestX;
    const float dy = center.y - closestY;
    return dx * dx + dy * dy <= radius * radius;
}

bool CircleIntersectsCircle(Vec2 a, float ar, Vec2 b, float br) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float sum = ar + br;
    return dx * dx + dy * dy <= sum * sum;
}

bool CircleIntersectsObject(const SceneObject& object, Vec2 center, float radius) {
    if (!object.collision.blocks || object.collision.shape == CollisionShape::None) {
        return false;
    }

    if (object.collision.shape == CollisionShape::Rect) {
        return CircleIntersectsRect(center, radius, ObjectCollisionRect(object));
    }

    if (object.collision.shape == CollisionShape::Circle) {
        Vec2 collisionCenter{object.pos.x + object.collision.x, object.pos.y + object.collision.y};
        return CircleIntersectsCircle(center, radius, collisionCenter, object.collision.radius);
    }

    return false;
}

} // namespace

const std::vector<SceneObjectDef>& ObjectDefs() {
    static const std::vector<SceneObjectDef> defs = {
        {"tree_oak", L"Oak Tree", L"objects/tree_oak.bmp", ObjectVisual::TreeOak, 118.0f, 132.0f, 0.0f, kTreeCollision},
        {"stone_round", L"Round Stone", L"objects/stone_round.bmp", ObjectVisual::StoneRound, 62.0f, 38.0f, 0.0f, kStoneCollision},
        {"bush", L"Bush", L"objects/bush.bmp", ObjectVisual::Bush, 74.0f, 46.0f, 0.0f, kBushCollision},
    };
    return defs;
}

const SceneObjectDef* FindObjectDef(std::string_view type) {
    const auto& defs = ObjectDefs();
    const auto it = std::find_if(defs.begin(), defs.end(), [type](const SceneObjectDef& def) {
        return def.type == type;
    });
    return it == defs.end() ? nullptr : &*it;
}

SceneObject MakeObject(std::string_view type, Vec2 pos, int index) {
    const SceneObjectDef* def = FindObjectDef(type);
    SceneObject object;
    object.id = "obj_" + std::to_string(index);
    object.type = std::string(type);
    object.pos = pos;
    if (def) {
        object.zOffset = def->zOffset;
        object.collision = def->collision;
    }
    return object;
}

Scene MakeDefaultScene() {
    Scene scene;
    scene.objects.push_back(MakeObject("tree_oak", {520.0f, 292.0f}, 1));
    scene.objects.push_back(MakeObject("tree_oak", {915.0f, 540.0f}, 2));
    scene.objects.push_back(MakeObject("stone_round", {655.0f, 410.0f}, 3));
    scene.objects.push_back(MakeObject("bush", {775.0f, 300.0f}, 4));
    return scene;
}

bool LoadSceneFromFile(const std::filesystem::path& path, Scene& scene, std::string* error) {
    const std::string text = ReadAll(path);
    if (text.empty()) {
        SetError(error, "scene file is missing or empty: " + path.string());
        return false;
    }

    auto objectsArray = FindBracketedField(text, "objects", '[', ']');
    if (!objectsArray) {
        SetError(error, "scene file does not contain an objects array: " + path.string());
        return false;
    }

    Scene loaded;
    int index = 1;
    for (const std::string& block : ExtractObjectBlocks(*objectsArray)) {
        auto type = FindStringField(block, "type");
        auto x = FindFloatField(block, "x");
        auto y = FindFloatField(block, "y");
        if (!type || !x || !y) {
            continue;
        }

        const SceneObjectDef* def = FindObjectDef(*type);
        SceneObject object;
        object.id = FindStringField(block, "id").value_or("obj_" + std::to_string(index));
        object.type = *type;
        object.pos = {*x, *y};
        object.zOffset = FindFloatField(block, "z").value_or(def ? def->zOffset : 0.0f);
        object.collision = ParseCollision(block, def);
        loaded.objects.push_back(object);
        ++index;
    }

    scene = std::move(loaded);
    return true;
}

bool SaveSceneToFile(const std::filesystem::path& path, const Scene& scene, std::string* error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        SetError(error, "failed to create scene directory: " + ec.message());
        return false;
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        SetError(error, "failed to write scene file: " + path.string());
        return false;
    }

    out << "{\n";
    out << "  \"version\": 1,\n";
    out << "  \"objects\": [\n";
    out << std::fixed << std::setprecision(1);
    for (size_t i = 0; i < scene.objects.size(); ++i) {
        const SceneObject& object = scene.objects[i];
        out << "    {\n";
        out << "      \"id\": \"" << object.id << "\",\n";
        out << "      \"type\": \"" << object.type << "\",\n";
        out << "      \"x\": " << object.pos.x << ",\n";
        out << "      \"y\": " << object.pos.y << ",\n";
        out << "      \"z\": " << object.zOffset << ",\n";
        out << "      \"collision\": {\n";
        out << "        \"shape\": \"" << ShapeToString(object.collision.shape) << "\",\n";
        out << "        \"x\": " << object.collision.x << ",\n";
        out << "        \"y\": " << object.collision.y << ",\n";
        out << "        \"w\": " << object.collision.w << ",\n";
        out << "        \"h\": " << object.collision.h << ",\n";
        out << "        \"radius\": " << object.collision.radius << ",\n";
        out << "        \"blocks\": " << (object.collision.blocks ? "true" : "false") << "\n";
        out << "      }\n";
        out << "    }" << (i + 1 == scene.objects.size() ? "\n" : ",\n");
    }
    out << "  ]\n";
    out << "}\n";

    return true;
}

RectF ObjectVisualBounds(const SceneObject& object) {
    float width = 48.0f;
    float height = 48.0f;
    if (const SceneObjectDef* def = FindObjectDef(object.type)) {
        width = def->width;
        height = def->height;
    }

    return {
        object.pos.x - width * 0.5f,
        object.pos.y - height,
        object.pos.x + width * 0.5f,
        object.pos.y,
    };
}

RectF ObjectCollisionRect(const SceneObject& object) {
    return {
        object.pos.x + object.collision.x,
        object.pos.y + object.collision.y,
        object.pos.x + object.collision.x + object.collision.w,
        object.pos.y + object.collision.y + object.collision.h,
    };
}

float ObjectSortY(const SceneObject& object) {
    return object.pos.y + object.zOffset;
}

bool PointInObjectVisual(const SceneObject& object, Vec2 point) {
    const RectF bounds = ObjectVisualBounds(object);
    return point.x >= bounds.left && point.x <= bounds.right && point.y >= bounds.top && point.y <= bounds.bottom;
}

bool CircleIntersectsScene(const Scene& scene, Vec2 center, float radius) {
    for (const SceneObject& object : scene.objects) {
        if (CircleIntersectsObject(object, center, radius)) {
            return true;
        }
    }
    return false;
}

} // namespace rpg
