#include "world_layers.h"

#include <algorithm>
#include <cstdint>

namespace rpg {
namespace {

constexpr std::array<WorldLayerDef, 4> kLayers{{
    {1, L"垂纱之地", L"veil_lands.json", 300, 500, 400, 400, 0.22f, 0.10f, "grass"},
    {2, L"迷雾禁区", L"mist_forbidden_zone.json", 200, 350, 275, 275, 0.18f, 0.13f, "dirt"},
    {3, L"暗影巢穴", L"shadow_nest.json", 150, 250, 200, 200, 0.12f, 0.17f, "gravel"},
    {4, L"上古祭坛", L"ancient_altar.json", 100, 180, 140, 140, 0.07f, 0.20f, "sand"},
}};

std::uint32_t TileHash(int x, int y, int layer, std::uint32_t salt) {
    std::uint32_t value = static_cast<std::uint32_t>(x) * 0x8da6b343u;
    value ^= static_cast<std::uint32_t>(y) * 0xd8163841u;
    value ^= static_cast<std::uint32_t>(layer) * 0xcb1ab31fu;
    value ^= salt;
    value ^= value >> 13;
    value *= 0x85ebca6bu;
    return value ^ (value >> 16);
}

float HashUnit(int x, int y, int layer, std::uint32_t salt) {
    return static_cast<float>(TileHash(x, y, layer, salt) & 0xffffu) / 65535.0f;
}

struct TilePoint {
    int x = 0;
    int y = 0;
};

TilePoint RandomEdgeTile(const Scene& scene, const WorldLayerDef& layer, std::uint32_t salt) {
    constexpr int margin = 1;
    const std::uint32_t edgeRoll = TileHash(scene.mapWidth, scene.mapHeight, layer.index, salt);
    const std::uint32_t positionRoll = TileHash(scene.mapHeight, scene.mapWidth, layer.index, salt ^ 0x9e3779b9u);
    switch (edgeRoll % 4u) {
    case 0:
        return {margin + static_cast<int>(positionRoll % static_cast<std::uint32_t>(scene.mapWidth - margin * 2)), margin};
    case 1:
        return {scene.mapWidth - margin - 1,
                margin + static_cast<int>(positionRoll % static_cast<std::uint32_t>(scene.mapHeight - margin * 2))};
    case 2:
        return {margin + static_cast<int>(positionRoll % static_cast<std::uint32_t>(scene.mapWidth - margin * 2)),
                scene.mapHeight - margin - 1};
    default:
        return {margin,
                margin + static_cast<int>(positionRoll % static_cast<std::uint32_t>(scene.mapHeight - margin * 2))};
    }
}

bool PortalDistanceValid(TilePoint first, TilePoint second, int mapWidth) {
    const int dx = first.x - second.x;
    const int dy = first.y - second.y;
    return dx * dx + dy * dy >= mapWidth * mapWidth;
}

std::array<TilePoint, 2> PortalTiles(const Scene& scene, const WorldLayerDef& layer) {
    TilePoint upward = RandomEdgeTile(scene, layer, 0x51f15e5du);
    for (std::uint32_t attempt = 0; attempt < 256; ++attempt) {
        const TilePoint downward = RandomEdgeTile(scene, layer, 0xa17e4d23u + attempt * 0x45d9f3bu);
        if (PortalDistanceValid(upward, downward, scene.mapWidth)) return {upward, downward};
    }
    return {TilePoint{1, 1}, TilePoint{scene.mapWidth - 2, scene.mapHeight - 2}};
}

bool IsReserved(int x, int y, int width, int height, const std::array<TilePoint, 2>& portals) {
    const int centerX = width / 2;
    const int centerY = height / 2;
    const auto near = [x, y](int px, int py, int radius) {
        const int dx = x - px;
        const int dy = y - py;
        return dx * dx + dy * dy <= radius * radius;
    };
    return near(centerX, centerY, 8) || near(portals[0].x, portals[0].y, 6) ||
           near(portals[1].x, portals[1].y, 6);
}

SceneObject MakeGeneratedObject(std::string_view type, Vec2 pos, int& nextId) {
    SceneObject object = MakeObject(type, pos, nextId++);
    object.id = "generated_" + std::to_string(nextId - 1);
    return object;
}

bool EnsurePortal(Scene& scene, const WorldLayerDef& layer, bool upward, TilePoint tile) {
    const int targetIndex = upward ? layer.index - 1 : layer.index + 1;
    if (targetIndex < 1 || targetIndex > static_cast<int>(kLayers.size())) return false;

    const WorldLayerDef& target = kLayers[static_cast<size_t>(targetIndex - 1)];
    const std::string id = upward ? "portal_up" : "portal_down";
    const std::string targetScene = std::filesystem::path(target.fileName).generic_u8string();
    const std::string targetId = upward ? "portal_down" : "portal_up";
    const Vec2 position{(tile.x + 0.5f) * kTileSize, (tile.y + 1.0f) * kTileSize};
    auto found = std::find_if(scene.objects.begin(), scene.objects.end(), [&](const SceneObject& object) {
        return object.id == id;
    });
    if (found == scene.objects.end()) {
        SceneObject portal = MakeObject("teleport_point", position, static_cast<int>(scene.objects.size()) + 1);
        portal.id = id;
        portal.targetScene = targetScene;
        portal.targetId = targetId;
        scene.objects.push_back(std::move(portal));
        return true;
    }
    const bool changed = found->type != "teleport_point" || found->pos.x != position.x || found->pos.y != position.y ||
                         found->targetScene != targetScene || found->targetId != targetId;
    if (changed) {
        SceneObject portal = MakeObject("teleport_point", position, static_cast<int>(scene.objects.size()) + 1);
        portal.id = id;
        portal.targetScene = targetScene;
        portal.targetId = targetId;
        *found = std::move(portal);
    }
    return changed;
}

} // namespace

const std::array<WorldLayerDef, 4>& WorldLayers() {
    return kLayers;
}

const WorldLayerDef* FindWorldLayer(const std::filesystem::path& path) {
    const std::wstring fileName = path.filename().wstring();
    for (const WorldLayerDef& layer : kLayers) {
        if (fileName == layer.fileName) {
            return &layer;
        }
    }
    return nullptr;
}

std::filesystem::path WorldLayerPath(const std::filesystem::path& mapsRoot, const WorldLayerDef& layer) {
    return mapsRoot / layer.fileName;
}

bool EnsureWorldLayerLandmarks(Scene& scene, const WorldLayerDef& layer) {
    const std::array<TilePoint, 2> portals = PortalTiles(scene, layer);
    bool changed = false;
    if (layer.index > 1) changed = EnsurePortal(scene, layer, true, portals[0]) || changed;
    if (layer.index < static_cast<int>(kLayers.size())) changed = EnsurePortal(scene, layer, false, portals[1]) || changed;

    if (layer.index == 4) {
        const Vec2 altarPosition{
            (scene.mapWidth / 2 + 0.5f) * kTileSize,
            (scene.mapHeight / 2 + 2.0f) * kTileSize,
        };
        auto altar = std::find_if(scene.objects.begin(), scene.objects.end(), [](const SceneObject& object) {
            return object.id == "ending_ritual_altar";
        });
        if (altar == scene.objects.end()) {
            SceneObject object = MakeObject("ending_ritual_altar", altarPosition, static_cast<int>(scene.objects.size()) + 1);
            object.id = "ending_ritual_altar";
            scene.objects.push_back(std::move(object));
            changed = true;
        } else if (altar->type != "ending_ritual_altar" || altar->pos.x != altarPosition.x || altar->pos.y != altarPosition.y) {
            SceneObject object = MakeObject("ending_ritual_altar", altarPosition, static_cast<int>(scene.objects.size()) + 1);
            object.id = "ending_ritual_altar";
            *altar = std::move(object);
            changed = true;
        }
    }

    const size_t objectCount = scene.objects.size();
    scene.objects.erase(
        std::remove_if(scene.objects.begin(), scene.objects.end(), [&](const SceneObject& object) {
            if (object.id.rfind("generated_", 0) != 0) return false;
            const int tx = static_cast<int>(object.pos.x / kTileSize);
            const int ty = static_cast<int>(object.pos.y / kTileSize) - 1;
            const auto near = [tx, ty](TilePoint point, int radius) {
                const int dx = tx - point.x;
                const int dy = ty - point.y;
                return dx * dx + dy * dy <= radius * radius;
            };
            if (layer.index > 1 && near(portals[0], 6)) return true;
            if (layer.index < static_cast<int>(kLayers.size()) && near(portals[1], 6)) return true;
            return layer.index == 4 && near({scene.mapWidth / 2, scene.mapHeight / 2}, 8);
        }),
        scene.objects.end());
    changed = changed || scene.objects.size() != objectCount;
    return changed;
}

Scene GenerateWorldLayer(const WorldLayerDef& layer, int width, int height, float resourceMultiplier) {
    Scene scene;
    scene.mapWidth = std::clamp(width, layer.minSize, layer.maxSize);
    scene.mapHeight = std::clamp(height, layer.minSize, layer.maxSize);
    scene.naturalTerrain.assign(static_cast<size_t>(scene.mapWidth) * scene.mapHeight, layer.terrainId);
    scene.builtTerrain.assign(static_cast<size_t>(scene.mapWidth) * scene.mapHeight, "none");
    scene.territory.assign(static_cast<size_t>(scene.mapWidth) * scene.mapHeight, 0);
    scene.playerStart = {
        (scene.mapWidth * 0.5f + 0.5f) * kTileSize,
        (scene.mapHeight * 0.5f + 1.0f) * kTileSize,
    };
    scene.hasPlayerStart = layer.index == 1;

    EnsureWorldLayerLandmarks(scene, layer);
    const std::array<TilePoint, 2> portals = PortalTiles(scene, layer);
    int nextId = 1;

    constexpr int spacing = 5;
    for (int y = 3; y < scene.mapHeight - 3; y += spacing) {
        for (int x = 3; x < scene.mapWidth - 3; x += spacing) {
            if (IsReserved(x, y, scene.mapWidth, scene.mapHeight, portals)) {
                continue;
            }

            const float roll = HashUnit(x, y, layer.index, 0x10203040u);
            std::string_view type;
            const float treeRate = std::clamp(layer.treeRate * resourceMultiplier, 0.0f, 0.8f);
            const float resourceRate = std::clamp(layer.resourceRate * resourceMultiplier, 0.0f, 0.8f - treeRate);
            if (roll < treeRate) {
                type = layer.index <= 2 ? "tree_oak" : "exotic_tree_01";
            } else if (roll < treeRate + resourceRate) {
                type = HashUnit(x, y, layer.index, 0x55667788u) < 0.55f ? "bush" : "stone_round";
            } else {
                continue;
            }

            const float jitterX = (HashUnit(x, y, layer.index, 0xabcdef01u) - 0.5f) * 2.0f;
            const float jitterY = (HashUnit(x, y, layer.index, 0x12345678u) - 0.5f) * 2.0f;
            const Vec2 pos{(x + 0.5f + jitterX) * kTileSize, (y + 1.0f + jitterY) * kTileSize};
            SceneObject object = MakeGeneratedObject(type, pos, nextId);
            const SceneObjectDef* def = FindObjectDef(type);
            if (def && !def->companionType.empty() && FindObjectDef(def->companionType)) {
                SceneObject companion = MakeGeneratedObject(def->companionType, pos, nextId);
                companion.groupId = object.id;
                object.groupId = object.id;
                scene.objects.push_back(std::move(companion));
            }
            scene.objects.push_back(std::move(object));
        }
    }
    return scene;
}

void RegenerateWorldLayerResources(Scene& scene, const WorldLayerDef& layer) {
    Scene generated = GenerateWorldLayer(layer, scene.mapWidth, scene.mapHeight);
    scene.objects.erase(
        std::remove_if(scene.objects.begin(), scene.objects.end(), [](const SceneObject& object) {
            return object.id.rfind("generated_", 0) == 0 || ObjectIsTeleport(object) ||
                   object.id == "ending_ritual_altar";
        }),
        scene.objects.end());
    scene.objects.insert(
        scene.objects.end(),
        std::make_move_iterator(generated.objects.begin()),
        std::make_move_iterator(generated.objects.end()));
}

void ResizeWorldLayer(Scene& scene, const WorldLayerDef& layer, int width, int height) {
    const int newWidth = std::clamp(width, layer.minSize, layer.maxSize);
    const int newHeight = std::clamp(height, layer.minSize, layer.maxSize);
    std::vector<std::string> natural(static_cast<size_t>(newWidth) * newHeight, layer.terrainId);
    std::vector<std::string> built(static_cast<size_t>(newWidth) * newHeight, "none");
    std::vector<std::uint8_t> territory(static_cast<size_t>(newWidth) * newHeight, 0);
    const int copyWidth = std::min(scene.mapWidth, newWidth);
    const int copyHeight = std::min(scene.mapHeight, newHeight);
    for (int y = 0; y < copyHeight; ++y) {
        for (int x = 0; x < copyWidth; ++x) {
            natural[static_cast<size_t>(y) * newWidth + x] = scene.naturalTerrain[static_cast<size_t>(y) * scene.mapWidth + x];
            built[static_cast<size_t>(y) * newWidth + x] = scene.builtTerrain[static_cast<size_t>(y) * scene.mapWidth + x];
            if (TerritoryAt(scene, x, y)) territory[static_cast<size_t>(y) * newWidth + x] = 1;
        }
    }

    scene.mapWidth = newWidth;
    scene.mapHeight = newHeight;
    scene.naturalTerrain = std::move(natural);
    scene.builtTerrain = std::move(built);
    scene.territory = std::move(territory);
    scene.playerStart.x = std::clamp(scene.playerStart.x, kTileSize * 1.5f, SceneWorldWidth(scene) - kTileSize * 1.5f);
    scene.playerStart.y = std::clamp(scene.playerStart.y, kTileSize * 1.5f, SceneWorldHeight(scene) - kTileSize * 1.5f);
    scene.objects.erase(
        std::remove_if(scene.objects.begin(), scene.objects.end(), [&scene](const SceneObject& object) {
            return object.pos.x < 0.0f || object.pos.y < 0.0f ||
                   object.pos.x > SceneWorldWidth(scene) || object.pos.y > SceneWorldHeight(scene);
        }),
        scene.objects.end());
    if (!SceneSupportsBackgroundImage(scene)) {
        scene.backgroundImagePath.clear();
    }
    RegenerateWorldLayerResources(scene, layer);
}

bool EnsureWorldLayerFiles(const std::filesystem::path& mapsRoot, std::string* error) {
    std::error_code ec;
    std::filesystem::create_directories(mapsRoot, ec);
    if (ec) {
        if (error) *error = "failed to create maps directory";
        return false;
    }

    for (const WorldLayerDef& layer : kLayers) {
        const std::filesystem::path path = WorldLayerPath(mapsRoot, layer);
        if (std::filesystem::is_regular_file(path, ec)) {
            Scene scene;
            if (!LoadSceneFromFile(path, scene, error)) return false;
            if (EnsureWorldLayerLandmarks(scene, layer) && !SaveSceneToFile(path, scene, error)) return false;
            continue;
        }
        Scene scene = GenerateWorldLayer(layer, layer.defaultWidth, layer.defaultHeight);
        if (!SaveSceneToFile(path, scene, error)) {
            return false;
        }
    }
    return true;
}

} // namespace rpg
