#include "scene.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <system_error>

namespace rpg {
namespace {

#ifndef RPG_ASSET_DIR
#define RPG_ASSET_DIR L"assets"
#endif

std::vector<SceneObjectDef> g_objectDefs;
bool g_objectDefsLoaded = false;
std::vector<TerrainDef> g_naturalTerrainDefs;
std::vector<TerrainDef> g_builtTerrainDefs;
bool g_terrainDefsLoaded = false;

constexpr std::string_view kTerrainPaletteCodes =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_";

std::wstring ToWide(std::string_view text) {
    return std::filesystem::u8path(text.begin(), text.end()).wstring();
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

std::vector<std::string> ExtractStringValues(const std::string& arrayText) {
    std::vector<std::string> values;
    bool inString = false;
    bool escaped = false;
    std::string value;

    for (char c : arrayText) {
        if (!inString) {
            if (c == '"') {
                inString = true;
                value.clear();
            }
            continue;
        }

        if (escaped) {
            value.push_back(c);
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '"') {
            values.push_back(value);
            inString = false;
        } else {
            value.push_back(c);
        }
    }
    return values;
}

std::string LegacyNaturalTerrainId(char code) {
    switch (code) {
    case 'd':
        return "dirt";
    case 's':
        return "sand";
    case 'r':
        return "gravel";
    case 'g':
    default:
        return "grass";
    }
}

std::string LegacyBuiltTerrainId(char code) {
    switch (code) {
    case 's':
        return "stone_floor";
    case 'w':
        return "wood_floor";
    case '.':
    default:
        return "none";
    }
}

std::string DecodeTerrainCell(
    char code,
    const std::vector<std::string>& palette,
    TerrainLayer layer) {
    if (!palette.empty()) {
        const size_t index = kTerrainPaletteCodes.find(code);
        if (index < palette.size()) {
            return palette[index];
        }
    }
    return layer == TerrainLayer::Natural ? LegacyNaturalTerrainId(code) : LegacyBuiltTerrainId(code);
}

std::vector<std::string> CollectTerrainPalette(
    const std::vector<std::string>& layer) {
    std::vector<std::string> palette;
    for (const std::string& id : layer) {
        if (std::find(palette.begin(), palette.end(), id) == palette.end()) {
            palette.push_back(id);
        }
    }
    return palette;
}

char EncodeTerrainCell(std::string_view id, const std::vector<std::string>& palette) {
    const auto it = std::find(palette.begin(), palette.end(), id);
    const size_t index = it == palette.end() ? 0 : static_cast<size_t>(std::distance(palette.begin(), it));
    return index < kTerrainPaletteCodes.size() ? kTerrainPaletteCodes[index] : kTerrainPaletteCodes.front();
}

void LoadTerrainLayer(const std::string& text, Scene& scene) {
    const auto terrainBlock = FindBracketedField(text, "terrain", '{', '}');
    if (!terrainBlock) {
        return;
    }

    std::vector<std::string> naturalPalette;
    if (const auto paletteArray = FindBracketedField(*terrainBlock, "natural_palette", '[', ']')) {
        naturalPalette = ExtractStringValues(*paletteArray);
    }
    if (const auto naturalArray = FindBracketedField(*terrainBlock, "natural", '[', ']')) {
        const auto rows = ExtractStringValues(*naturalArray);
        for (int y = 0; y < std::min(scene.mapHeight, static_cast<int>(rows.size())); ++y) {
            for (int x = 0; x < std::min(scene.mapWidth, static_cast<int>(rows[y].size())); ++x) {
                scene.naturalTerrain[y * scene.mapWidth + x] = DecodeTerrainCell(rows[y][x], naturalPalette, TerrainLayer::Natural);
            }
        }
    }

    std::vector<std::string> builtPalette;
    if (const auto paletteArray = FindBracketedField(*terrainBlock, "built_palette", '[', ']')) {
        builtPalette = ExtractStringValues(*paletteArray);
    }
    if (const auto builtArray = FindBracketedField(*terrainBlock, "built", '[', ']')) {
        const auto rows = ExtractStringValues(*builtArray);
        for (int y = 0; y < std::min(scene.mapHeight, static_cast<int>(rows.size())); ++y) {
            for (int x = 0; x < std::min(scene.mapWidth, static_cast<int>(rows[y].size())); ++x) {
                scene.builtTerrain[y * scene.mapWidth + x] = DecodeTerrainCell(rows[y][x], builtPalette, TerrainLayer::Built);
            }
        }
    }
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

bool IsValidObjectType(std::string_view type) {
    return !type.empty() && std::all_of(type.begin(), type.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '-';
    });
}

bool IsSafeRelativePath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) {
        return false;
    }
    return std::none_of(path.begin(), path.end(), [](const std::filesystem::path& part) {
        return part == L"..";
    });
}

bool LoadObjectDef(
    const std::filesystem::path& moduleDirectory,
    SceneObjectDef& def,
    std::string& error) {
    const std::filesystem::path metadataPath = moduleDirectory / L"object.json";
    const std::string text = ReadAll(metadataPath);
    if (text.empty()) {
        error = "missing or empty object.json";
        return false;
    }

    const std::string folderType = moduleDirectory.filename().u8string();
    def.type = FindStringField(text, "type").value_or(folderType);
    if (!IsValidObjectType(def.type)) {
        error = "type must contain only letters, numbers, '-' or '_'";
        return false;
    }

    const auto image = FindStringField(text, "image");
    if (!image) {
        error = "missing image field";
        return false;
    }

    const std::filesystem::path imageRelative = std::filesystem::u8path(*image);
    if (!IsSafeRelativePath(imageRelative)) {
        error = "image must be a relative path inside the module folder";
        return false;
    }

    const std::filesystem::path imagePath = moduleDirectory / imageRelative;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(imagePath, ec)) {
        error = "image file does not exist: " + imagePath.u8string();
        return false;
    }
    if (imagePath.extension() != L".bmp" && imagePath.extension() != L".png") {
        error = "runtime images must use the .bmp or .png format";
        return false;
    }

    def.displayName = ToWide(FindStringField(text, "display_name").value_or(def.type));
    def.bitmapPath = (std::filesystem::path(L"objects") / moduleDirectory.filename() / imageRelative).generic_wstring();
    def.width = FindFloatField(text, "width").value_or(48.0f);
    def.height = FindFloatField(text, "height").value_or(48.0f);
    def.zOffset = FindFloatField(text, "z_offset").value_or(0.0f);
    def.collision = ParseCollision(text, nullptr);
    def.placeable = FindBoolField(text, "placeable").value_or(true);
    def.companionType = FindStringField(text, "companion_type").value_or("");
    def.teleport = FindBoolField(text, "teleport").value_or(false);
    def.building = FindBoolField(text, "building").value_or(false);
    def.footprintWidth = std::clamp(
        static_cast<int>(std::lround(FindFloatField(text, "footprint_width").value_or(1.0f))),
        1,
        256);
    def.footprintHeight = std::clamp(
        static_cast<int>(std::lround(FindFloatField(text, "footprint_height").value_or(1.0f))),
        1,
        256);
    def.draggable = FindBoolField(text, "draggable").value_or(true);
    if (!def.companionType.empty() && !IsValidObjectType(def.companionType)) {
        error = "companion_type must contain only letters, numbers, '-' or '_'";
        return false;
    }

    if (def.width <= 0.0f || def.height <= 0.0f) {
        error = "width and height must be greater than zero";
        return false;
    }
    return true;
}

std::optional<std::uint32_t> ParseRgb(std::string_view value) {
    if (!value.empty() && value.front() == '#') {
        value.remove_prefix(1);
    }
    if (value.size() != 6) {
        return std::nullopt;
    }
    std::uint32_t color = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), color, 16);
    return result.ec == std::errc() && result.ptr == value.data() + value.size()
        ? std::optional<std::uint32_t>(color)
        : std::nullopt;
}

bool LoadTerrainDef(
    const std::filesystem::path& moduleDirectory,
    TerrainLayer layer,
    TerrainDef& def,
    std::string& error) {
    const std::filesystem::path metadataPath = moduleDirectory / L"terrain.json";
    const std::string text = ReadAll(metadataPath);
    if (text.empty()) {
        error = "missing or empty terrain.json";
        return false;
    }

    def.id = FindStringField(text, "id").value_or(moduleDirectory.filename().u8string());
    if (!IsValidObjectType(def.id)) {
        error = "id must contain only letters, numbers, '-' or '_'";
        return false;
    }

    const auto image = FindStringField(text, "image");
    if (!image) {
        error = "missing image field";
        return false;
    }
    const std::filesystem::path imageRelative = std::filesystem::u8path(*image);
    if (!IsSafeRelativePath(imageRelative)) {
        error = "image must be a relative path inside the module folder";
        return false;
    }

    const std::filesystem::path imagePath = moduleDirectory / imageRelative;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(imagePath, ec)) {
        error = "image file does not exist: " + imagePath.u8string();
        return false;
    }
    std::wstring extension = imagePath.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    if (extension != L".png") {
        error = "terrain images must use the .png format";
        return false;
    }

    def.variants = static_cast<int>(FindFloatField(text, "variants").value_or(1.0f));
    if (def.variants < 1 || def.variants > 8) {
        error = "variants must be between 1 and 8";
        return false;
    }
    for (int variant = 1; variant < def.variants; ++variant) {
        const std::filesystem::path variantPath = imagePath.parent_path() /
            (imagePath.stem().wstring() + L"_" + std::to_wstring(variant) + imagePath.extension().wstring());
        if (!std::filesystem::is_regular_file(variantPath, ec)) {
            error = "variant image does not exist: " + variantPath.u8string();
            return false;
        }
    }

    def.displayName = ToWide(FindStringField(text, "display_name").value_or(def.id));
    const std::filesystem::path layerDirectory = layer == TerrainLayer::Natural ? L"natural" : L"built";
    def.imagePath = (std::filesystem::path(L"terrain") / layerDirectory / moduleDirectory.filename() / imageRelative).generic_wstring();
    def.layer = layer;
    def.priority = static_cast<int>(FindFloatField(text, "priority").value_or(0.0f));
    def.blocksMovement = FindBoolField(text, "blocks_movement").value_or(false);
    def.fallbackRgb = layer == TerrainLayer::Natural ? 0x6fbd84u : 0x888888u;
    if (const auto color = FindStringField(text, "fallback_color")) {
        if (const auto parsed = ParseRgb(*color)) {
            def.fallbackRgb = *parsed;
        } else {
            error = "fallback_color must use #RRGGBB";
            return false;
        }
    }
    return true;
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
    if (!g_objectDefsLoaded) {
        ReloadObjectDefs();
    }
    return g_objectDefs;
}

bool ReloadObjectDefs(std::string* error) {
    const std::filesystem::path root = std::filesystem::path(RPG_ASSET_DIR) / L"objects";
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
        g_objectDefsLoaded = true;
        SetError(error, "object module directory does not exist: " + root.u8string());
        return false;
    }

    std::vector<std::filesystem::path> moduleDirectories;
    for (std::filesystem::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory()) {
            continue;
        }
        if (std::filesystem::is_regular_file(it->path() / L"object.json")) {
            moduleDirectories.push_back(it->path());
        }
    }
    if (ec) {
        g_objectDefsLoaded = true;
        SetError(error, "failed to scan object modules: " + ec.message());
        return false;
    }

    std::sort(moduleDirectories.begin(), moduleDirectories.end());

    std::vector<SceneObjectDef> loaded;
    std::vector<std::string> warnings;
    for (const std::filesystem::path& directory : moduleDirectories) {
        SceneObjectDef def;
        std::string moduleError;
        if (!LoadObjectDef(directory, def, moduleError)) {
            warnings.push_back(directory.filename().u8string() + ": " + moduleError);
            continue;
        }

        const bool duplicate = std::any_of(loaded.begin(), loaded.end(), [&def](const SceneObjectDef& item) {
            return item.type == def.type;
        });
        if (duplicate) {
            warnings.push_back(directory.filename().u8string() + ": duplicate type '" + def.type + "'");
            continue;
        }
        loaded.push_back(std::move(def));
    }

    g_objectDefsLoaded = true;
    if (loaded.empty()) {
        SetError(error, warnings.empty() ? "no object modules found under " + root.u8string() : warnings.front());
        return false;
    }

    g_objectDefs = std::move(loaded);
    if (error) {
        error->clear();
        for (size_t i = 0; i < warnings.size(); ++i) {
            if (i > 0) {
                *error += "; ";
            }
            *error += warnings[i];
        }
    }
    return true;
}

const std::vector<TerrainDef>& NaturalTerrainDefs() {
    if (!g_terrainDefsLoaded) {
        ReloadTerrainDefs();
    }
    return g_naturalTerrainDefs;
}

const std::vector<TerrainDef>& BuiltTerrainDefs() {
    if (!g_terrainDefsLoaded) {
        ReloadTerrainDefs();
    }
    return g_builtTerrainDefs;
}

const TerrainDef* FindTerrainDef(std::string_view id, TerrainLayer layer) {
    const auto& defs = layer == TerrainLayer::Natural ? NaturalTerrainDefs() : BuiltTerrainDefs();
    const auto it = std::find_if(defs.begin(), defs.end(), [id](const TerrainDef& def) {
        return def.id == id;
    });
    return it == defs.end() ? nullptr : &*it;
}

bool ReloadTerrainDefs(std::string* error) {
    const std::filesystem::path root = std::filesystem::path(RPG_ASSET_DIR) / L"terrain";
    std::vector<TerrainDef> natural;
    std::vector<TerrainDef> built;
    std::vector<std::string> warnings;
    std::error_code ec;

    const auto scanLayer = [&](TerrainLayer layer, const wchar_t* directoryName, std::vector<TerrainDef>& output) {
        const std::filesystem::path layerRoot = root / directoryName;
        std::vector<std::filesystem::path> directories;
        for (std::filesystem::directory_iterator it(layerRoot, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_directory() && std::filesystem::is_regular_file(it->path() / L"terrain.json")) {
                directories.push_back(it->path());
            }
        }
        if (ec) {
            warnings.push_back("failed to scan " + layerRoot.u8string() + ": " + ec.message());
            ec.clear();
            return;
        }

        std::sort(directories.begin(), directories.end());
        for (const std::filesystem::path& directory : directories) {
            TerrainDef def;
            std::string moduleError;
            if (!LoadTerrainDef(directory, layer, def, moduleError)) {
                warnings.push_back(directory.filename().u8string() + ": " + moduleError);
                continue;
            }
            const bool duplicate = std::any_of(output.begin(), output.end(), [&def](const TerrainDef& item) {
                return item.id == def.id;
            });
            if (duplicate) {
                warnings.push_back(directory.filename().u8string() + ": duplicate terrain id '" + def.id + "'");
                continue;
            }
            output.push_back(std::move(def));
        }
    };

    if (!std::filesystem::is_directory(root, ec)) {
        g_terrainDefsLoaded = true;
        SetError(error, "terrain module directory does not exist: " + root.u8string());
        return false;
    }

    scanLayer(TerrainLayer::Natural, L"natural", natural);
    scanLayer(TerrainLayer::Built, L"built", built);
    g_terrainDefsLoaded = true;
    if (natural.empty()) {
        SetError(error, warnings.empty() ? "no natural terrain modules found" : warnings.front());
        return false;
    }

    g_naturalTerrainDefs = std::move(natural);
    g_builtTerrainDefs = std::move(built);
    if (error) {
        error->clear();
        for (size_t i = 0; i < warnings.size(); ++i) {
            if (i > 0) {
                *error += "; ";
            }
            *error += warnings[i];
        }
    }
    return true;
}

const SceneObjectDef* FindObjectDef(std::string_view type) {
    const auto& defs = ObjectDefs();
    const auto it = std::find_if(defs.begin(), defs.end(), [type](const SceneObjectDef& def) {
        return def.type == type;
    });
    return it == defs.end() ? nullptr : &*it;
}

Scene::Scene() {
    naturalTerrain.assign(mapWidth * mapHeight, "grass");
    builtTerrain.assign(mapWidth * mapHeight, "none");
    territory.assign(mapWidth * mapHeight, 0);
}

std::string_view NaturalTerrainAt(const Scene& scene, int tx, int ty) {
    if (tx < 0 || ty < 0 || tx >= scene.mapWidth || ty >= scene.mapHeight) {
        return "grass";
    }
    return scene.naturalTerrain[ty * scene.mapWidth + tx];
}

std::string_view BuiltTerrainAt(const Scene& scene, int tx, int ty) {
    if (tx < 0 || ty < 0 || tx >= scene.mapWidth || ty >= scene.mapHeight) {
        return "none";
    }
    return scene.builtTerrain[ty * scene.mapWidth + tx];
}

bool SetNaturalTerrain(Scene& scene, int tx, int ty, std::string_view terrainId) {
    if (tx < 0 || ty < 0 || tx >= scene.mapWidth || ty >= scene.mapHeight) {
        return false;
    }
    std::string& cell = scene.naturalTerrain[ty * scene.mapWidth + tx];
    if (cell == terrainId) {
        return false;
    }
    cell = terrainId;
    return true;
}

bool SetBuiltTerrain(Scene& scene, int tx, int ty, std::string_view terrainId) {
    if (tx < 0 || ty < 0 || tx >= scene.mapWidth || ty >= scene.mapHeight) {
        return false;
    }
    std::string& cell = scene.builtTerrain[ty * scene.mapWidth + tx];
    if (cell == terrainId) {
        return false;
    }
    cell = terrainId;
    return true;
}

bool TerritoryAt(const Scene& scene, int tx, int ty) {
    if (tx < 0 || ty < 0 || tx >= scene.mapWidth || ty >= scene.mapHeight) return false;
    const size_t index = static_cast<size_t>(ty) * scene.mapWidth + tx;
    return index < scene.territory.size() && scene.territory[index] != 0;
}

bool SetTerritory(Scene& scene, int tx, int ty, bool claimed) {
    if (tx < 0 || ty < 0 || tx >= scene.mapWidth || ty >= scene.mapHeight) return false;
    if (scene.territory.size() != static_cast<size_t>(scene.mapWidth) * scene.mapHeight) {
        scene.territory.assign(static_cast<size_t>(scene.mapWidth) * scene.mapHeight, 0);
    }
    std::uint8_t& cell = scene.territory[static_cast<size_t>(ty) * scene.mapWidth + tx];
    const std::uint8_t value = claimed ? 1 : 0;
    if (cell == value) return false;
    cell = value;
    return true;
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
    return Scene{};
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
    loaded.mapWidth = std::clamp(
        static_cast<int>(std::lround(FindFloatField(text, "width").value_or(kMapWidth))),
        1,
        kMaximumMapDimension);
    loaded.mapHeight = std::clamp(
        static_cast<int>(std::lround(FindFloatField(text, "height").value_or(kMapHeight))),
        1,
        kMaximumMapDimension);
    loaded.naturalTerrain.assign(loaded.mapWidth * loaded.mapHeight, "grass");
    loaded.builtTerrain.assign(loaded.mapWidth * loaded.mapHeight, "none");
    loaded.territory.assign(loaded.mapWidth * loaded.mapHeight, 0);
    if (const auto playerStartBlock = FindBracketedField(text, "player_start", '{', '}')) {
        loaded.playerStart.x = FindFloatField(*playerStartBlock, "x").value_or(loaded.playerStart.x);
        loaded.playerStart.y = FindFloatField(*playerStartBlock, "y").value_or(loaded.playerStart.y);
        loaded.hasPlayerStart = true;
    }
    if (const auto background = FindStringField(text, "background_image")) {
        loaded.backgroundImagePath = std::filesystem::u8path(*background).wstring();
    }
    LoadTerrainLayer(text, loaded);
    if (const auto territoryArray = FindBracketedField(text, "territory", '[', ']')) {
        const auto rows = ExtractStringValues(*territoryArray);
        for (int y = 0; y < loaded.mapHeight && y < static_cast<int>(rows.size()); ++y) {
            for (int x = 0; x < loaded.mapWidth && x < static_cast<int>(rows[y].size()); ++x) {
                loaded.territory[static_cast<size_t>(y) * loaded.mapWidth + x] = rows[y][x] == '1' ? 1 : 0;
            }
        }
    }
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
        object.groupId = FindStringField(block, "group").value_or("");
        object.type = *type;
        object.pos = {*x, *y};
        object.zOffset = FindFloatField(block, "z").value_or(def ? def->zOffset : 0.0f);
        object.collision = ParseCollision(block, def);
        object.targetScene = FindStringField(block, "target_scene").value_or("");
        object.targetId = FindStringField(block, "target_id").value_or("");
        object.cropId = FindStringField(block, "crop_id").value_or("");
        object.cropGrowthDays = std::max(0, static_cast<int>(std::lround(
            FindFloatField(block, "crop_growth_days").value_or(0.0f))));
        loaded.objects.push_back(object);
        ++index;
    }

    scene = std::move(loaded);
    return true;
}

bool SaveSceneToFile(const std::filesystem::path& path, const Scene& scene, std::string* error) {
    const std::vector<std::string> naturalPalette = CollectTerrainPalette(scene.naturalTerrain);
    const std::vector<std::string> builtPalette = CollectTerrainPalette(scene.builtTerrain);
    if (naturalPalette.size() > kTerrainPaletteCodes.size() || builtPalette.size() > kTerrainPaletteCodes.size()) {
        SetError(error, "a terrain layer contains more than 64 terrain types");
        return false;
    }

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
    out << "  \"version\": 4,\n";
    out << "  \"width\": " << scene.mapWidth << ",\n";
    out << "  \"height\": " << scene.mapHeight << ",\n";
    out << "  \"terrain\": {\n";
    out << "    \"natural_palette\": [";
    for (size_t i = 0; i < naturalPalette.size(); ++i) {
        out << (i == 0 ? "" : ", ") << "\"" << naturalPalette[i] << "\"";
    }
    out << "],\n";
    out << "    \"natural\": [\n";
    for (int y = 0; y < scene.mapHeight; ++y) {
        out << "      \"";
        for (int x = 0; x < scene.mapWidth; ++x) {
            out << EncodeTerrainCell(scene.naturalTerrain[y * scene.mapWidth + x], naturalPalette);
        }
        out << "\"" << (y + 1 == scene.mapHeight ? "\n" : ",\n");
    }
    out << "    ],\n";
    out << "    \"built_palette\": [";
    for (size_t i = 0; i < builtPalette.size(); ++i) {
        out << (i == 0 ? "" : ", ") << "\"" << builtPalette[i] << "\"";
    }
    out << "],\n";
    out << "    \"built\": [\n";
    for (int y = 0; y < scene.mapHeight; ++y) {
        out << "      \"";
        for (int x = 0; x < scene.mapWidth; ++x) {
            out << EncodeTerrainCell(scene.builtTerrain[y * scene.mapWidth + x], builtPalette);
        }
        out << "\"" << (y + 1 == scene.mapHeight ? "\n" : ",\n");
    }
    out << "    ]\n";
    out << "  },\n";
    out << "  \"territory\": [\n";
    for (int y = 0; y < scene.mapHeight; ++y) {
        out << "    \"";
        for (int x = 0; x < scene.mapWidth; ++x) out << (TerritoryAt(scene, x, y) ? '1' : '0');
        out << "\"" << (y + 1 == scene.mapHeight ? "\n" : ",\n");
    }
    out << "  ],\n";
    if (scene.hasPlayerStart) {
        out << std::fixed << std::setprecision(1);
        out << "  \"player_start\": {\"x\": " << scene.playerStart.x
            << ", \"y\": " << scene.playerStart.y << "},\n";
    }
    if (!scene.backgroundImagePath.empty()) {
        out << "  \"background_image\": \"" << std::filesystem::path(scene.backgroundImagePath).generic_u8string() << "\",\n";
    }
    out << "  \"objects\": [\n";
    out << std::fixed << std::setprecision(1);
    for (size_t i = 0; i < scene.objects.size(); ++i) {
        const SceneObject& object = scene.objects[i];
        out << "    {\n";
        out << "      \"id\": \"" << object.id << "\",\n";
        if (!object.groupId.empty()) {
            out << "      \"group\": \"" << object.groupId << "\",\n";
        }
        out << "      \"type\": \"" << object.type << "\",\n";
        out << "      \"x\": " << object.pos.x << ",\n";
        out << "      \"y\": " << object.pos.y;
        if (!object.targetScene.empty()) {
            out << ",\n      \"target_scene\": \"" << object.targetScene << "\"";
        }
        if (!object.targetId.empty()) {
            out << ",\n      \"target_id\": \"" << object.targetId << "\"";
        }
        if (!object.cropId.empty()) {
            out << ",\n      \"crop_id\": \"" << object.cropId << "\"";
            out << ",\n      \"crop_growth_days\": " << std::max(0, object.cropGrowthDays);
        }
        out << "\n";
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

bool ObjectIsGroundOverlay(const SceneObject& object) {
    return object.zOffset < 0.0f;
}

bool ObjectIsTeleport(const SceneObject& object) {
    const SceneObjectDef* def = FindObjectDef(object.type);
    return def && def->teleport;
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

bool CircleIntersectsBlockedTerrain(const Scene& scene, Vec2 center, float radius, bool canTraverseWater) {
    const int left = static_cast<int>(std::floor((center.x - radius) / kTileSize));
    const int right = static_cast<int>(std::floor((center.x + radius) / kTileSize));
    const int top = static_cast<int>(std::floor((center.y - radius) / kTileSize));
    const int bottom = static_cast<int>(std::floor((center.y + radius) / kTileSize));

    for (int ty = top; ty <= bottom; ++ty) {
        for (int tx = left; tx <= right; ++tx) {
            const std::string_view builtId = BuiltTerrainAt(scene, tx, ty);
            const TerrainDef* built = FindTerrainDef(builtId, TerrainLayer::Built);
            const bool builtBlocks = built && built->blocksMovement;
            if (builtBlocks) {
                const RectF tileRect{
                    static_cast<float>(tx * kTileSize),
                    static_cast<float>(ty * kTileSize),
                    static_cast<float>((tx + 1) * kTileSize),
                    static_cast<float>((ty + 1) * kTileSize),
                };
                if (CircleIntersectsRect(center, radius, tileRect)) {
                    return true;
                }
                continue;
            }

            if (canTraverseWater) {
                continue;
            }

            const std::string_view naturalId = NaturalTerrainAt(scene, tx, ty);
            const TerrainDef* natural = FindTerrainDef(naturalId, TerrainLayer::Natural);
            if (!natural || !natural->blocksMovement) {
                continue;
            }

            const RectF tileRect{
                static_cast<float>(tx * kTileSize),
                static_cast<float>(ty * kTileSize),
                static_cast<float>((tx + 1) * kTileSize),
                static_cast<float>((ty + 1) * kTileSize),
            };
            if (CircleIntersectsRect(center, radius, tileRect)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace rpg
