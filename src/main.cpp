#include <windows.h>
#include <windowsx.h>

#include "item.h"
#include "scene.h"
#include "scene_render.h"
#include "terrain_render.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <gdiplus.h>
#include <iterator>
#include <memory>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr int kWindowWidth = 960;
constexpr int kWindowHeight = 640;
constexpr int kTileSize = rpg::kTileSize;
constexpr float kPlayerRadius = 16.0f;
constexpr float kPlayerSpeed = 190.0f;
constexpr float kWorldScale = 3.0f;
constexpr int kSpriteFrameSize = 288;
constexpr int kSpriteDrawSize = 96;
constexpr int kSideWalkFrames = 8;
constexpr float kDefaultZoom = 1.0f / kWorldScale;
constexpr float kMinimumZoom = kDefaultZoom;
constexpr float kMaximumZoom = 1.0f;
constexpr float kZoomStep = 1.0f / 6.0f;
constexpr UINT_PTR kFrameTimer = 1;
constexpr UINT kFrameMs = 16;
constexpr int kHotbarSlotCount = 10;
constexpr int kBackpackSlotCount = 30;
constexpr int kInventorySlotCount = kHotbarSlotCount + kBackpackSlotCount;
constexpr int kInventoryCellSize = 48;
constexpr int kInventoryCellGap = 5;

#ifndef RPG_ASSET_DIR
#define RPG_ASSET_DIR L"assets"
#endif

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Player {
    Vec2 pos{180.0f, 170.0f};
    Vec2 vel{};
    int dir = 0;
    float animTime = 0.0f;
};

struct SpriteSheet {
    std::unique_ptr<Gdiplus::Bitmap> bitmap;
    bool loaded = false;
};

struct Npc {
    Vec2 pos;
    const wchar_t* name;
    const wchar_t* text;
};

struct ItemStack {
    std::string id;
    std::wstring displayName;
    int count = 0;
};

struct Inventory {
    std::array<ItemStack, kInventorySlotCount> slots;
    ItemStack heldItem;
    int selectedHotbar = 0;
    int focusedSlot = 0;
    bool open = false;
    bool dragging = false;
    int dragSource = -1;
    POINT dragPoint{};
};

struct WorldPickup {
    Vec2 pos;
    ItemStack item;
    bool requiresClick = false;
    bool activated = false;
};

struct Game {
    Player player;
    Inventory inventory;
    std::map<std::wstring, std::vector<WorldPickup>> pickupsByScene;
    std::wstring activePickupSceneKey;
    std::wstring pickupNotice;
    float pickupNoticeTime = 0.0f;
    rpg::Scene scene;
    Vec2 camera{};
    float zoom = kDefaultZoom;
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool interact = false;
    bool showTalk = false;
    int nearbyNpc = -1;
    float teleportCooldown = 0.0f;
    LARGE_INTEGER lastTick{};
    LARGE_INTEGER freq{};
};

constexpr std::array<Npc, 3> kNpcs = {{
    {{360.0f, 170.0f}, L"Lina", L"Movement uses continuous coordinates, not grid steps."},
    {{780.0f, 430.0f}, L"Noah", L"Later multiplayer can sync position and animation state."},
    {{1040.0f, 250.0f}, L"Mira", L"Get movement, collision, camera, and interaction solid first."},
}};

Game g_game;
std::filesystem::path g_requestedScenePath;
SpriteSheet g_playerSprite;
SpriteSheet g_playerRunFront;
SpriteSheet g_playerRunBack;
std::array<SpriteSheet, 3> g_playerIdleSprites;

struct ItemBitmapCache {
    std::string id;
    std::unique_ptr<Gdiplus::Bitmap> icon;
    std::unique_ptr<Gdiplus::Bitmap> world;
    bool iconAttempted = false;
    bool worldAttempted = false;
};

std::vector<ItemBitmapCache> g_itemBitmaps;

float RenderScale() {
    return kWorldScale * g_game.zoom;
}

struct BackgroundCache {
    std::wstring path;
    std::unique_ptr<Gdiplus::Bitmap> bitmap;
    bool attempted = false;
};

BackgroundCache g_background;
ULONG_PTR g_gdiplusToken = 0;

std::filesystem::path AssetPath(const wchar_t* relative) {
    return std::filesystem::path(RPG_ASSET_DIR) / relative;
}

float Clamp(float value, float minValue, float maxValue) {
    return std::max(minValue, std::min(maxValue, value));
}

bool EnsureGdiPlus() {
    if (g_gdiplusToken != 0) {
        return true;
    }

    Gdiplus::GdiplusStartupInput input{};
    return Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr) == Gdiplus::Ok;
}

std::filesystem::path ResolveBackgroundPath(const std::wstring& storedPath) {
    if (storedPath.empty()) {
        return {};
    }

    const std::filesystem::path path(storedPath);
    if (path.is_absolute()) {
        return path;
    }

    const std::filesystem::path assetPath = AssetPath(L"") / path;
    std::error_code ec;
    if (std::filesystem::exists(assetPath, ec)) {
        return assetPath;
    }
    return path;
}

void ReleaseBackgroundResources() {
    g_background.bitmap.reset();
    g_background.path.clear();
    g_background.attempted = false;
}

bool EnsureBackgroundBitmap() {
    if (g_background.path != g_game.scene.backgroundImagePath) {
        ReleaseBackgroundResources();
        g_background.path = g_game.scene.backgroundImagePath;
    }

    if (g_background.attempted) {
        return g_background.bitmap != nullptr;
    }

    g_background.attempted = true;
    if (g_background.path.empty() || !EnsureGdiPlus()) {
        return false;
    }

    const std::filesystem::path imagePath = ResolveBackgroundPath(g_background.path);
    g_background.bitmap = std::make_unique<Gdiplus::Bitmap>(imagePath.c_str());
    if (!g_background.bitmap || g_background.bitmap->GetLastStatus() != Gdiplus::Ok) {
        g_background.bitmap.reset();
        OutputDebugStringW((L"background:load-fail path=" + imagePath.wstring() + L"\n").c_str());
        return false;
    }

    return true;
}

bool DrawBackgroundImage(HDC hdc) {
    if (!EnsureBackgroundBitmap()) {
        return false;
    }

    Gdiplus::Bitmap* bitmap = g_background.bitmap.get();
    if (!bitmap) {
        return false;
    }

    Gdiplus::Graphics graphics(hdc);
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighSpeed);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    const Gdiplus::Rect destination(
        static_cast<INT>(std::round(-g_game.camera.x)),
        static_cast<INT>(std::round(-g_game.camera.y)),
        static_cast<INT>(std::round(rpg::SceneWorldWidth(g_game.scene) * RenderScale())),
        static_cast<INT>(std::round(rpg::SceneWorldHeight(g_game.scene) * RenderScale())));
    return graphics.DrawImage(
               bitmap,
               destination,
               0,
               0,
               static_cast<INT>(bitmap->GetWidth()),
               static_cast<INT>(bitmap->GetHeight()),
               Gdiplus::UnitPixel) == Gdiplus::Ok;
}

bool CollidesWithMap(Vec2 pos, float radius) {
    if (pos.x - radius < 0.0f ||
        pos.y - radius < 0.0f ||
         pos.x + radius > rpg::SceneWorldWidth(g_game.scene) ||
         pos.y + radius > rpg::SceneWorldHeight(g_game.scene)) {
        return true;
    }

    if (rpg::CircleIntersectsBlockedTerrain(g_game.scene, {pos.x, pos.y}, radius)) {
        return true;
    }

    return rpg::CircleIntersectsScene(g_game.scene, {pos.x, pos.y}, radius);
}

void TryMove(Player& player, Vec2 delta) {
    Vec2 next = {player.pos.x + delta.x, player.pos.y};
    if (!CollidesWithMap(next, kPlayerRadius)) {
        player.pos.x = next.x;
    }

    next = {player.pos.x, player.pos.y + delta.y};
    if (!CollidesWithMap(next, kPlayerRadius)) {
        player.pos.y = next.y;
    }
}

void LoadSpriteSheet(SpriteSheet& sprite, const std::filesystem::path& path) {
    if (sprite.loaded) {
        return;
    }

    sprite.loaded = true;
    if (!EnsureGdiPlus()) {
        return;
    }

    const std::wstring spritePath = path.wstring();
    sprite.bitmap = std::make_unique<Gdiplus::Bitmap>(spritePath.c_str());
    if (!sprite.bitmap || sprite.bitmap->GetLastStatus() != Gdiplus::Ok) {
        sprite.bitmap.reset();
        OutputDebugStringW((L"sprite:load-fail path=" + spritePath + L"\n").c_str());
    }
}

void LoadPlayerSprites() {
    LoadSpriteSheet(g_playerSprite, AssetPath(L"player_walk_side_8.png"));
    LoadSpriteSheet(g_playerRunFront, AssetPath(L"player_walk_front.png"));
    LoadSpriteSheet(g_playerRunBack, AssetPath(L"player_walk_back.png"));
    LoadSpriteSheet(g_playerIdleSprites[0], AssetPath(L"player_idle_front.png"));
    LoadSpriteSheet(g_playerIdleSprites[1], AssetPath(L"player_idle_side.png"));
    LoadSpriteSheet(g_playerIdleSprites[2], AssetPath(L"player_idle_back.png"));
}

ItemStack MakeItemStack(std::string_view id, int count) {
    ItemStack stack;
    if (const rpg::ItemDef* def = rpg::FindItemDef(id)) {
        stack.id = std::string(id);
        stack.displayName = def->displayName;
        stack.count = std::max(1, count);
    }
    return stack;
}

std::wstring ScenePickupKey(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    return (ec ? path : absolute).lexically_normal().generic_wstring();
}

void ActivateScenePickups(const std::filesystem::path& path) {
    g_game.activePickupSceneKey = ScenePickupKey(path);
    if (g_game.pickupsByScene.find(g_game.activePickupSceneKey) != g_game.pickupsByScene.end()) {
        return;
    }

    const float width = rpg::SceneWorldWidth(g_game.scene);
    const float height = rpg::SceneWorldHeight(g_game.scene);
    struct Spawn {
        float xRatio;
        float yRatio;
        const char* id;
        int count;
    };
    constexpr Spawn spawns[] = {
        {0.20f, 0.22f, "healing_herb", 3},
        {0.36f, 0.32f, "wood", 4},
        {0.58f, 0.20f, "gold_coin", 12},
        {0.74f, 0.38f, "iron_ore", 2},
        {0.30f, 0.68f, "small_potion", 1},
        {0.55f, 0.72f, "healing_herb", 2},
        {0.82f, 0.66f, "gold_coin", 8},
    };

    std::vector<WorldPickup> pickups;
    for (const Spawn& spawn : spawns) {
        ItemStack item = MakeItemStack(spawn.id, spawn.count);
        if (!item.id.empty()) {
            pickups.push_back({
                {Clamp(width * spawn.xRatio, 30.0f, std::max(30.0f, width - 30.0f)),
                 Clamp(height * spawn.yRatio, 30.0f, std::max(30.0f, height - 30.0f))},
                std::move(item),
            });
        }
    }
    g_game.pickupsByScene.emplace(g_game.activePickupSceneKey, std::move(pickups));
}

std::vector<WorldPickup>& CurrentPickups() {
    return g_game.pickupsByScene[g_game.activePickupSceneKey];
}

bool StoreItem(ItemStack& incoming) {
    const rpg::ItemDef* def = rpg::FindItemDef(incoming.id);
    const int maxStack = def ? def->maxStack : 1;
    const int originalCount = incoming.count;

    for (ItemStack& slot : g_game.inventory.slots) {
        if (slot.id != incoming.id || slot.count >= maxStack) {
            continue;
        }
        const int moved = std::min(incoming.count, maxStack - slot.count);
        slot.count += moved;
        incoming.count -= moved;
        if (incoming.count == 0) {
            break;
        }
    }
    for (ItemStack& slot : g_game.inventory.slots) {
        if (incoming.count == 0) {
            break;
        }
        if (!slot.id.empty()) {
            continue;
        }
        const int moved = std::min(incoming.count, maxStack);
        slot = incoming;
        slot.count = moved;
        incoming.count -= moved;
    }

    const int stored = originalCount - incoming.count;
    if (stored > 0) {
        g_game.pickupNotice = L"+ " + incoming.displayName + L" x" + std::to_wstring(stored);
        g_game.pickupNoticeTime = 2.0f;
    }
    return incoming.count == 0;
}

void UpdateWorldPickups(float dt) {
    constexpr float kMagnetRadius = 130.0f;
    constexpr float kCollectRadius = 22.0f;
    constexpr float kMagnetSpeed = 360.0f;
    auto& pickups = CurrentPickups();
    for (size_t i = 0; i < pickups.size();) {
        WorldPickup& pickup = pickups[i];
        const float dx = g_game.player.pos.x - pickup.pos.x;
        const float dy = g_game.player.pos.y - pickup.pos.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        const bool canMagnetize = !pickup.requiresClick || pickup.activated;
        if (!canMagnetize) {
            ++i;
            continue;
        }
        if (distance <= kCollectRadius) {
            if (StoreItem(pickup.item)) {
                pickups.erase(pickups.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
        } else if ((pickup.activated || distance <= kMagnetRadius) && distance > 0.001f) {
            const float movement = std::min(distance - kCollectRadius, kMagnetSpeed * dt);
            pickup.pos.x += dx / distance * movement;
            pickup.pos.y += dy / distance * movement;
        }
        ++i;
    }
}

void LoadGameScene() {
    rpg::ReloadObjectDefs();
    rpg::ReloadTerrainDefs();
    rpg::ReloadItemDefs();
    std::string error;
    const std::filesystem::path scenePath = g_requestedScenePath.empty()
        ? AssetPath(L"maps/demo_scene.json")
        : g_requestedScenePath;
    if (!rpg::LoadSceneFromFile(scenePath, g_game.scene, &error)) {
        g_game.scene = rpg::MakeDefaultScene();
        rpg::SaveSceneToFile(scenePath, g_game.scene);
    }
    g_game.player.pos = {g_game.scene.playerStart.x, g_game.scene.playerStart.y};
    ActivateScenePickups(scenePath);
}

std::filesystem::path ResolveScenePath(const std::string& storedPath) {
    const std::filesystem::path path = std::filesystem::u8path(storedPath);
    return path.is_absolute() ? path : std::filesystem::path(RPG_ASSET_DIR) / path;
}

bool LoadRuntimeScene(const std::filesystem::path& path, std::string_view targetId) {
    rpg::Scene nextScene;
    std::string error;
    if (!rpg::LoadSceneFromFile(path, nextScene, &error)) {
        return false;
    }

    rpg::Vec2 destination = nextScene.playerStart;
    if (!targetId.empty()) {
        for (const rpg::SceneObject& object : nextScene.objects) {
            if (object.id == targetId && rpg::ObjectIsTeleport(object)) {
                destination = object.pos;
                break;
            }
        }
    }

    ReleaseBackgroundResources();
    g_game.scene = std::move(nextScene);
    g_game.player.pos = {destination.x, destination.y};
    g_game.player.animTime = 0.0f;
    g_game.teleportCooldown = 0.8f;
    ActivateScenePickups(path);
    return true;
}

bool TryTeleport() {
    for (const rpg::SceneObject& object : g_game.scene.objects) {
        if (!rpg::ObjectIsTeleport(object) || object.targetScene.empty()) {
            continue;
        }

        const float dx = g_game.player.pos.x - object.pos.x;
        const float dy = g_game.player.pos.y - object.pos.y;
        if (dx * dx + dy * dy > 28.0f * 28.0f) {
            continue;
        }

        if (LoadRuntimeScene(ResolveScenePath(object.targetScene), object.targetId)) {
            return true;
        }
    }
    return false;
}

float Distance(Vec2 a, Vec2 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

void UpdateNearbyNpc() {
    g_game.nearbyNpc = -1;
    for (int i = 0; i < static_cast<int>(kNpcs.size()); ++i) {
        if (Distance(g_game.player.pos, kNpcs[i].pos) < 72.0f) {
            g_game.nearbyNpc = i;
            return;
        }
    }
}

void UpdateGame(float dt) {
    g_game.teleportCooldown = std::max(0.0f, g_game.teleportCooldown - dt);
    g_game.pickupNoticeTime = std::max(0.0f, g_game.pickupNoticeTime - dt);
    if (g_game.inventory.open) {
        g_game.player.vel = {};
        g_game.player.animTime = 0.0f;
        g_game.interact = false;
        return;
    }
    Vec2 input{};
    if (g_game.left) {
        input.x -= 1.0f;
    }
    if (g_game.right) {
        input.x += 1.0f;
    }
    if (g_game.up) {
        input.y -= 1.0f;
    }
    if (g_game.down) {
        input.y += 1.0f;
    }

    const float len = std::sqrt(input.x * input.x + input.y * input.y);
    if (len > 0.0f) {
        input.x /= len;
        input.y /= len;
    }

    g_game.player.vel = {input.x * kPlayerSpeed, input.y * kPlayerSpeed};
    if (len > 0.0f) {
        if (std::fabs(input.x) > std::fabs(input.y)) {
            g_game.player.dir = input.x < 0.0f ? 1 : 2;
        } else {
            g_game.player.dir = input.y < 0.0f ? 3 : 0;
        }
        g_game.player.animTime += dt;
    } else {
        g_game.player.animTime = 0.0f;
    }
    TryMove(g_game.player, {g_game.player.vel.x * dt, g_game.player.vel.y * dt});
    UpdateWorldPickups(dt);
    if (g_game.teleportCooldown <= 0.0f) {
        TryTeleport();
    }

    UpdateNearbyNpc();
    if (g_game.interact && g_game.nearbyNpc >= 0) {
        g_game.showTalk = !g_game.showTalk;
    }
    if (g_game.nearbyNpc < 0) {
        g_game.showTalk = false;
    }
    g_game.interact = false;

    const float renderScale = RenderScale();
    const float worldW = rpg::SceneWorldWidth(g_game.scene) * renderScale;
    const float worldH = rpg::SceneWorldHeight(g_game.scene) * renderScale;
    g_game.camera.x = Clamp(
        g_game.player.pos.x * renderScale - kWindowWidth * 0.5f,
        0.0f,
        std::max(0.0f, worldW - kWindowWidth));
    g_game.camera.y = Clamp(
        g_game.player.pos.y * renderScale - kWindowHeight * 0.5f,
        0.0f,
        std::max(0.0f, worldH - kWindowHeight));
}

RECT WorldRect(float x, float y, float w, float h) {
    return RECT{
        static_cast<LONG>(std::round(x * RenderScale() - g_game.camera.x)),
        static_cast<LONG>(std::round(y * RenderScale() - g_game.camera.y)),
        static_cast<LONG>(std::round((x + w) * RenderScale() - g_game.camera.x)),
        static_cast<LONG>(std::round((y + h) * RenderScale() - g_game.camera.y)),
    };
}

void FillRectColor(HDC hdc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
}

void DrawEllipse(HDC hdc, Vec2 center, float rx, float ry, COLORREF fill, COLORREF outline) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 2, outline);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    Ellipse(
        hdc,
        static_cast<int>(std::round((center.x - rx) * RenderScale() - g_game.camera.x)),
        static_cast<int>(std::round((center.y - ry) * RenderScale() - g_game.camera.y)),
        static_cast<int>(std::round((center.x + rx) * RenderScale() - g_game.camera.x)),
        static_cast<int>(std::round((center.y + ry) * RenderScale() - g_game.camera.y)));
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void DrawPlayer(HDC hdc) {
    LoadPlayerSprites();
    const bool moving = std::fabs(g_game.player.vel.x) + std::fabs(g_game.player.vel.y) > 1.0f;
    const int idleIndex = g_game.player.dir == 0 ? 0 : (g_game.player.dir == 3 ? 2 : 1);
    const SpriteSheet* sprite = moving ? &g_playerSprite : &g_playerIdleSprites[idleIndex];
    int frameCount = kSideWalkFrames;
    if (moving && g_game.player.dir == 0 && g_playerRunFront.bitmap) {
        sprite = &g_playerRunFront;
        frameCount = 4;
    } else if (moving && g_game.player.dir == 3 && g_playerRunBack.bitmap) {
        sprite = &g_playerRunBack;
        frameCount = 4;
    }
    if (!sprite->bitmap) {
        DrawEllipse(hdc, g_game.player.pos, kPlayerRadius, 20.0f, RGB(93, 176, 219), RGB(22, 73, 96));
        return;
    }

    // const int frame = moving ? (static_cast<int>(g_game.player.animTime * 5.0f) % frameCount) : 0;
    const float animationSpeed =
    (moving && (g_game.player.dir == 0 || g_game.player.dir == 3))
        ? 5.0f
        : 10.0f;

    const int frame = moving
        ? (static_cast<int>(g_game.player.animTime * animationSpeed) % frameCount)
        : 0;

    const bool mirror = moving ? g_game.player.dir == 1 : g_game.player.dir == 2;

    const int sx = (frame % 4) * kSpriteFrameSize;
    const int sy = (frame / 4) * kSpriteFrameSize;

    const float renderScale = RenderScale();
    const int spriteDrawSize = std::max(1, static_cast<int>(std::round(kSpriteDrawSize * renderScale)));
    const int dx = static_cast<int>(std::round(
        g_game.player.pos.x * renderScale - g_game.camera.x - spriteDrawSize * 0.5f));
    const int dy = static_cast<int>(std::round(
        g_game.player.pos.y * renderScale - g_game.camera.y - spriteDrawSize + 12.0f * renderScale));

    Gdiplus::Graphics graphics(hdc);
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighSpeed);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    if (mirror) {
        const Gdiplus::GraphicsState state = graphics.Save();
        graphics.TranslateTransform(
            static_cast<float>(dx + spriteDrawSize),
            static_cast<float>(dy));
        graphics.ScaleTransform(-1.0f, 1.0f);
        graphics.DrawImage(
            sprite->bitmap.get(),
            Gdiplus::Rect(0, 0, spriteDrawSize, spriteDrawSize),
            sx,
            sy,
            kSpriteFrameSize,
            kSpriteFrameSize,
            Gdiplus::UnitPixel);
        graphics.Restore(state);
        return;
    }

    graphics.DrawImage(
        sprite->bitmap.get(),
        Gdiplus::Rect(dx, dy, spriteDrawSize, spriteDrawSize),
        sx,
        sy,
        kSpriteFrameSize,
        kSpriteFrameSize,
        Gdiplus::UnitPixel);
}

void DrawTextLine(HDC hdc, const std::wstring& text, int x, int y, COLORREF color) {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    TextOutW(hdc, x, y, text.c_str(), static_cast<int>(text.size()));
}

COLORREF ItemColor(const ItemStack& item) {
    const rpg::ItemDef* def = rpg::FindItemDef(item.id);
    const std::uint32_t rgb = def ? def->colorRgb : 0x7a9cb5;
    return RGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
}

Gdiplus::Bitmap* ItemBitmap(const ItemStack& item, bool worldImage) {
    const rpg::ItemDef* def = rpg::FindItemDef(item.id);
    if (!def || !EnsureGdiPlus()) {
        return nullptr;
    }
    auto it = std::find_if(g_itemBitmaps.begin(), g_itemBitmaps.end(), [&](const ItemBitmapCache& cache) {
        return cache.id == item.id;
    });
    if (it == g_itemBitmaps.end()) {
        g_itemBitmaps.push_back({});
        it = std::prev(g_itemBitmaps.end());
        it->id = item.id;
    }

    bool& attempted = worldImage ? it->worldAttempted : it->iconAttempted;
    std::unique_ptr<Gdiplus::Bitmap>& bitmap = worldImage ? it->world : it->icon;
    if (attempted) {
        return bitmap.get();
    }
    attempted = true;
    const std::wstring& storedPath = worldImage ? def->worldImagePath : def->iconPath;
    if (storedPath.empty()) {
        return nullptr;
    }
    const std::filesystem::path path = std::filesystem::path(RPG_ASSET_DIR) / storedPath;
    bitmap = std::make_unique<Gdiplus::Bitmap>(path.c_str());
    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
        bitmap.reset();
    }
    return bitmap.get();
}

bool DrawItemImage(HDC hdc, const ItemStack& item, const RECT& rect, bool worldImage) {
    Gdiplus::Bitmap* bitmap = ItemBitmap(item, worldImage);
    if (!bitmap) {
        return false;
    }
    Gdiplus::Graphics graphics(hdc);
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    return graphics.DrawImage(
               bitmap,
               Gdiplus::Rect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top),
               0,
               0,
               static_cast<INT>(bitmap->GetWidth()),
               static_cast<INT>(bitmap->GetHeight()),
               Gdiplus::UnitPixel) == Gdiplus::Ok;
}

void DrawWorldPickups(HDC hdc) {
    const float scale = RenderScale();
    const int imageSize = std::max(28, static_cast<int>(std::round(48.0f * scale)));
    const int half = imageSize / 2;
    for (const WorldPickup& pickup : CurrentPickups()) {
        const int x = static_cast<int>(std::round(pickup.pos.x * scale - g_game.camera.x));
        const int y = static_cast<int>(std::round(pickup.pos.y * scale - g_game.camera.y));
        RECT imageRect{x - half, y - half, x - half + imageSize, y - half + imageSize};
        if (!DrawItemImage(hdc, pickup.item, imageRect, true)) {
            const int radius = std::max(7, imageSize / 4);
            POINT diamond[] = {
                {x, y - radius},
                {x + radius, y},
                {x, y + radius},
                {x - radius, y},
            };
            HBRUSH brush = CreateSolidBrush(ItemColor(pickup.item));
            HPEN pen = CreatePen(PS_SOLID, 2, RGB(240, 232, 186));
            HGDIOBJ oldBrush = SelectObject(hdc, brush);
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            Polygon(hdc, diamond, 4);
            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(pen);
            DeleteObject(brush);
        }

        const COLORREF outline = pickup.requiresClick && !pickup.activated
            ? RGB(138, 146, 142)
            : RGB(240, 232, 186);
        HPEN pen = CreatePen(pickup.requiresClick && !pickup.activated ? PS_DOT : PS_SOLID, 1, outline);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Ellipse(hdc, imageRect.left + 3, imageRect.top + 3, imageRect.right - 3, imageRect.bottom - 3);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(pen);

        if (pickup.item.count > 1) {
            DrawTextLine(hdc, std::to_wstring(pickup.item.count), imageRect.right - 13, imageRect.bottom - 18, RGB(255, 250, 224));
        }
    }
}

struct InventoryLayout {
    int gridX = 0;
    int gridY = 0;
    int hotbarY = 0;
    int heldX = 0;
    RECT panel{};
};

int InventoryRowWidth() {
    return kHotbarSlotCount * kInventoryCellSize + (kHotbarSlotCount - 1) * kInventoryCellGap;
}

RECT InventorySlotRect(int x, int y, int column, int row = 0) {
    const int left = x + column * (kInventoryCellSize + kInventoryCellGap);
    const int top = y + row * (kInventoryCellSize + kInventoryCellGap);
    return RECT{left, top, left + kInventoryCellSize, top + kInventoryCellSize};
}

ItemStack* InventoryItemAt(int index) {
    if (index >= 0 && index < kInventorySlotCount) {
        return &g_game.inventory.slots[index];
    }
    if (index == kInventorySlotCount) {
        return &g_game.inventory.heldItem;
    }
    return nullptr;
}

const ItemStack& InventoryItemForDrawing(int index) {
    static const ItemStack empty;
    if (g_game.inventory.dragging && g_game.inventory.dragSource == index) {
        return empty;
    }
    const ItemStack* item = InventoryItemAt(index);
    return item ? *item : empty;
}

InventoryLayout MakeInventoryLayout(const RECT& client) {
    const int clientWidth = static_cast<int>(client.right - client.left);
    const int clientHeight = static_cast<int>(client.bottom - client.top);
    const int rowWidth = InventoryRowWidth();
    const int combinedWidth = rowWidth + 18 + kInventoryCellSize;
    InventoryLayout layout;
    layout.gridX = std::max(16, (clientWidth - rowWidth) / 2);
    layout.hotbarY = clientHeight - kInventoryCellSize - 18;
    layout.heldX = layout.gridX + rowWidth + 18;

    if (g_game.inventory.open) {
        const int gridHeight = 3 * kInventoryCellSize + 2 * kInventoryCellGap;
        const int panelHeight = 288;
        layout.panel = RECT{
            std::max(12, (clientWidth - combinedWidth - 32) / 2),
            std::max(12, (clientHeight - panelHeight) / 2),
            std::min(clientWidth - 12, (clientWidth + combinedWidth + 32) / 2),
            std::min(clientHeight - 12, (clientHeight + panelHeight) / 2),
        };
        layout.gridY = layout.panel.top + 42;
        layout.hotbarY = layout.gridY + gridHeight + 38;
    }
    return layout;
}

void DrawInventorySlot(
    HDC hdc,
    const RECT& rect,
    const ItemStack& item,
    bool selected,
    bool focused,
    const std::wstring& cornerLabel = {}) {
    FillRectColor(hdc, rect, selected ? RGB(91, 107, 78) : RGB(49, 55, 55));
    HPEN pen = CreatePen(
        PS_SOLID,
        focused ? 3 : 1,
        focused ? RGB(245, 210, 102) : (selected ? RGB(210, 224, 155) : RGB(126, 134, 128)));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    if (!cornerLabel.empty()) {
        DrawTextLine(hdc, cornerLabel, rect.left + 5, rect.top + 4, RGB(190, 198, 188));
    }
    if (!item.id.empty()) {
        RECT icon{rect.left + 7, rect.top + 7, rect.right - 7, rect.bottom - 7};
        if (!DrawItemImage(hdc, item, icon, false)) {
            FillRectColor(hdc, icon, ItemColor(item));
            if (!item.displayName.empty()) {
                DrawTextLine(hdc, item.displayName.substr(0, 1), icon.left + 11, icon.top + 8, RGB(255, 255, 238));
            }
        }
        if (item.count > 1) {
            DrawTextLine(hdc, std::to_wstring(item.count), rect.right - 18, rect.bottom - 19, RGB(255, 255, 240));
        }
    }
}

void DrawInventory(HDC hdc, const RECT& client) {
    const InventoryLayout layout = MakeInventoryLayout(client);
    const int hotbarStart = layout.gridX;

    if (g_game.inventory.open) {
        FillRectColor(hdc, layout.panel, RGB(30, 34, 34));
        HPEN panelPen = CreatePen(PS_SOLID, 2, RGB(119, 128, 119));
        HGDIOBJ oldPen = SelectObject(hdc, panelPen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, layout.panel.left, layout.panel.top, layout.panel.right, layout.panel.bottom);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(panelPen);

        DrawTextLine(hdc, L"\u80cc\u5305", layout.gridX, layout.panel.top + 14, RGB(238, 238, 220));
        const ItemStack* focusedItem = nullptr;
        if (g_game.inventory.focusedSlot >= 0 && g_game.inventory.focusedSlot < kInventorySlotCount) {
            focusedItem = &g_game.inventory.slots[g_game.inventory.focusedSlot];
        } else if (g_game.inventory.focusedSlot == kInventorySlotCount) {
            focusedItem = &g_game.inventory.heldItem;
        }
        if (focusedItem && !focusedItem->id.empty()) {
            DrawTextLine(hdc, focusedItem->displayName, layout.gridX + 78, layout.panel.top + 14, RGB(210, 224, 176));
        }
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < kHotbarSlotCount; ++column) {
                const int index = kHotbarSlotCount + row * kHotbarSlotCount + column;
                DrawInventorySlot(
                    hdc,
                    InventorySlotRect(layout.gridX, layout.gridY, column, row),
                    InventoryItemForDrawing(index),
                    false,
                    g_game.inventory.focusedSlot == index);
            }
        }
        DrawTextLine(hdc, L"\u7269\u54c1\u680f", layout.gridX, layout.hotbarY - 22, RGB(205, 211, 198));
        DrawTextLine(hdc, L"\u624b\u6301", layout.heldX, layout.hotbarY - 22, RGB(205, 211, 198));
    }

    for (int column = 0; column < kHotbarSlotCount; ++column) {
        const std::wstring keyLabel = column == 9 ? L"0" : std::to_wstring(column + 1);
        DrawInventorySlot(
            hdc,
            InventorySlotRect(hotbarStart, layout.hotbarY, column),
            InventoryItemForDrawing(column),
            g_game.inventory.selectedHotbar == column,
            g_game.inventory.open && g_game.inventory.focusedSlot == column,
            keyLabel);
    }
    DrawInventorySlot(
        hdc,
        RECT{layout.heldX, layout.hotbarY, layout.heldX + kInventoryCellSize, layout.hotbarY + kInventoryCellSize},
        InventoryItemForDrawing(kInventorySlotCount),
        true,
        g_game.inventory.open && g_game.inventory.focusedSlot == kInventorySlotCount);
}

int HitInventorySlot(const RECT& client, int x, int y) {
    const InventoryLayout layout = MakeInventoryLayout(client);
    for (int column = 0; column < kHotbarSlotCount; ++column) {
        RECT slot = InventorySlotRect(layout.gridX, layout.hotbarY, column);
        if (PtInRect(&slot, POINT{x, y})) {
            return column;
        }
    }
    RECT held{layout.heldX, layout.hotbarY, layout.heldX + kInventoryCellSize, layout.hotbarY + kInventoryCellSize};
    if (PtInRect(&held, POINT{x, y})) {
        return kInventorySlotCount;
    }
    if (!g_game.inventory.open) {
        return -1;
    }
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < kHotbarSlotCount; ++column) {
            RECT slot = InventorySlotRect(layout.gridX, layout.gridY, column, row);
            if (PtInRect(&slot, POINT{x, y})) {
                return kHotbarSlotCount + row * kHotbarSlotCount + column;
            }
        }
    }
    return -1;
}

bool PointInInventoryPanel(const RECT& client, int x, int y) {
    if (!g_game.inventory.open) {
        return false;
    }
    const InventoryLayout layout = MakeInventoryLayout(client);
    return PtInRect(&layout.panel, POINT{x, y}) != FALSE;
}

void MoveInventoryItem(int sourceIndex, int targetIndex) {
    if (sourceIndex == targetIndex) {
        return;
    }
    ItemStack* source = InventoryItemAt(sourceIndex);
    ItemStack* target = InventoryItemAt(targetIndex);
    if (!source || !target || source->id.empty()) {
        return;
    }
    if (target->id.empty()) {
        *target = std::move(*source);
        *source = {};
        return;
    }
    if (source->id != target->id) {
        std::swap(*source, *target);
        return;
    }

    const rpg::ItemDef* def = rpg::FindItemDef(source->id);
    const int maxStack = def ? def->maxStack : 1;
    const int moved = std::min(source->count, std::max(0, maxStack - target->count));
    target->count += moved;
    source->count -= moved;
    if (source->count == 0) {
        *source = {};
    }
}

void DropInventoryItemOnGround(int sourceIndex, int screenX, int screenY) {
    ItemStack* source = InventoryItemAt(sourceIndex);
    if (!source || source->id.empty()) {
        return;
    }
    const float scale = RenderScale();
    Vec2 world{
        (static_cast<float>(screenX) + g_game.camera.x) / scale,
        (static_cast<float>(screenY) + g_game.camera.y) / scale,
    };
    world.x = Clamp(world.x, 18.0f, std::max(18.0f, rpg::SceneWorldWidth(g_game.scene) - 18.0f));
    world.y = Clamp(world.y, 18.0f, std::max(18.0f, rpg::SceneWorldHeight(g_game.scene) - 18.0f));
    CurrentPickups().push_back({world, std::move(*source), true, false});
    *source = {};
}

int HitWorldPickup(int screenX, int screenY) {
    const float scale = RenderScale();
    auto& pickups = CurrentPickups();
    for (int i = static_cast<int>(pickups.size()) - 1; i >= 0; --i) {
        const int x = static_cast<int>(std::round(pickups[i].pos.x * scale - g_game.camera.x));
        const int y = static_cast<int>(std::round(pickups[i].pos.y * scale - g_game.camera.y));
        const int dx = screenX - x;
        const int dy = screenY - y;
        if (dx * dx + dy * dy <= 18 * 18) {
            return i;
        }
    }
    return -1;
}

void DrawDraggedInventoryItem(HDC hdc) {
    if (!g_game.inventory.dragging) {
        return;
    }
    const ItemStack* item = InventoryItemAt(g_game.inventory.dragSource);
    if (!item || item->id.empty()) {
        return;
    }
    const int half = kInventoryCellSize / 2;
    RECT rect{
        g_game.inventory.dragPoint.x - half,
        g_game.inventory.dragPoint.y - half,
        g_game.inventory.dragPoint.x + half,
        g_game.inventory.dragPoint.y + half,
    };
    DrawInventorySlot(hdc, rect, *item, false, true);
}

void RenderGame(HWND hwnd, HDC target) {
    RECT client{};
    GetClientRect(hwnd, &client);

    HDC hdc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
    HGDIOBJ oldBitmap = SelectObject(hdc, bitmap);

    FillRectColor(hdc, client, RGB(41, 49, 47));
    DrawBackgroundImage(hdc);

    const float renderScale = RenderScale();
    rpg::DrawTerrain(hdc, g_game.scene, g_game.camera.x, g_game.camera.y, false, renderScale);

    std::vector<const rpg::SceneObject*> objects;
    objects.reserve(g_game.scene.objects.size());
    for (const rpg::SceneObject& object : g_game.scene.objects) {
        objects.push_back(&object);
    }
    std::stable_sort(objects.begin(), objects.end(), [](const rpg::SceneObject* a, const rpg::SceneObject* b) {
        return rpg::ObjectSortY(*a) < rpg::ObjectSortY(*b);
    });

    const float playerSortY = g_game.player.pos.y;
    for (const rpg::SceneObject* object : objects) {
        if (rpg::ObjectIsGroundOverlay(*object)) {
            rpg::DrawSceneObject(hdc, *object, g_game.camera.x, g_game.camera.y, false, renderScale);
        }
    }

    DrawWorldPickups(hdc);

    for (const Npc& npc : kNpcs) {
        DrawEllipse(hdc, npc.pos, 15.0f, 19.0f, RGB(222, 185, 94), RGB(76, 55, 32));
        DrawTextLine(
            hdc,
            npc.name,
            static_cast<int>(std::round(npc.pos.x * renderScale - g_game.camera.x - 18.0f * renderScale)),
            static_cast<int>(std::round(npc.pos.y * renderScale - g_game.camera.y - 38.0f * renderScale)),
            RGB(245, 244, 230));
    }

    for (const rpg::SceneObject* object : objects) {
        if (!rpg::ObjectIsGroundOverlay(*object) && rpg::ObjectSortY(*object) <= playerSortY) {
            rpg::DrawSceneObject(hdc, *object, g_game.camera.x, g_game.camera.y, false, renderScale);
        }
    }

    DrawPlayer(hdc);

    for (const rpg::SceneObject* object : objects) {
        if (!rpg::ObjectIsGroundOverlay(*object) && rpg::ObjectSortY(*object) > playerSortY) {
            rpg::DrawSceneObject(hdc, *object, g_game.camera.x, g_game.camera.y, false, renderScale);
        }
    }

    const std::wstring zoomText = L"视角 " + std::to_wstring(static_cast<int>(std::round(renderScale * 100.0f))) + L"%";
    DrawTextLine(hdc, zoomText, 18, 18, RGB(220, 230, 202));

    if (g_game.nearbyNpc >= 0 && !g_game.showTalk) {
        DrawTextLine(hdc, L"Press E", kWindowWidth / 2 - 40, kWindowHeight - 70, RGB(255, 250, 190));
    }

    if (g_game.showTalk && g_game.nearbyNpc >= 0) {
        RECT box{120, kWindowHeight - 150, kWindowWidth - 120, kWindowHeight - 42};
        FillRectColor(hdc, box, RGB(246, 241, 220));
        HPEN pen = CreatePen(PS_SOLID, 3, RGB(64, 57, 47));
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, box.left, box.top, box.right, box.bottom);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(pen);

        const Npc& npc = kNpcs[g_game.nearbyNpc];
        DrawTextLine(hdc, npc.name, box.left + 22, box.top + 18, RGB(44, 39, 34));
        DrawTextLine(hdc, npc.text, box.left + 22, box.top + 52, RGB(44, 39, 34));
    }

    if (g_game.pickupNoticeTime > 0.0f && !g_game.pickupNotice.empty()) {
        const int noticeWidth = 220;
        RECT notice{
            client.right - noticeWidth - 18,
            18,
            client.right - 18,
            50,
        };
        FillRectColor(hdc, notice, RGB(32, 40, 37));
        DrawTextLine(hdc, g_game.pickupNotice, notice.left + 12, notice.top + 8, RGB(225, 235, 188));
    }

    DrawInventory(hdc, client);
    DrawDraggedInventoryItem(hdc);

    BitBlt(target, 0, 0, client.right, client.bottom, hdc, 0, 0, SRCCOPY);

    SelectObject(hdc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(hdc);
}

void ToggleInventory() {
    g_game.inventory.open = !g_game.inventory.open;
    g_game.inventory.focusedSlot = g_game.inventory.selectedHotbar;
    g_game.up = false;
    g_game.down = false;
    g_game.left = false;
    g_game.right = false;
    g_game.interact = false;
    g_game.showTalk = false;
}

void SetKey(WPARAM key, bool pressed) {
    if (key >= '1' && key <= '9' && pressed) {
        g_game.inventory.selectedHotbar = static_cast<int>(key - '1');
        g_game.inventory.focusedSlot = g_game.inventory.selectedHotbar;
        return;
    }
    if (key == '0' && pressed) {
        g_game.inventory.selectedHotbar = 9;
        g_game.inventory.focusedSlot = 9;
        return;
    }
    if (g_game.inventory.open) {
        return;
    }
    switch (key) {
    case 'W':
    case VK_UP:
        g_game.up = pressed;
        break;
    case 'S':
    case VK_DOWN:
        g_game.down = pressed;
        break;
    case 'A':
    case VK_LEFT:
        g_game.left = pressed;
        break;
    case 'D':
    case VK_RIGHT:
        g_game.right = pressed;
        break;
    case 'E':
        if (pressed) {
            g_game.interact = true;
        }
        break;
    case VK_OEM_PLUS:
    case VK_ADD:
        if (pressed) {
            g_game.zoom = Clamp(g_game.zoom + kZoomStep, kMinimumZoom, kMaximumZoom);
        }
        break;
    case VK_OEM_MINUS:
    case VK_SUBTRACT:
        if (pressed) {
            g_game.zoom = Clamp(g_game.zoom - kZoomStep, kMinimumZoom, kMaximumZoom);
        }
        break;
    default:
        break;
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize = POINT{680, 500};
        return 0;
    }
    case WM_CREATE:
        QueryPerformanceFrequency(&g_game.freq);
        QueryPerformanceCounter(&g_game.lastTick);
        LoadGameScene();
        SetTimer(hwnd, kFrameTimer, kFrameMs, nullptr);
        return 0;
    case WM_TIMER: {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const float dt = static_cast<float>(now.QuadPart - g_game.lastTick.QuadPart) / static_cast<float>(g_game.freq.QuadPart);
        g_game.lastTick = now;
        UpdateGame(std::min(dt, 0.05f));
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_TAB) {
            if ((lParam & (1LL << 30)) == 0) {
                if (g_game.inventory.dragging) {
                    g_game.inventory.dragging = false;
                    g_game.inventory.dragSource = -1;
                    ReleaseCapture();
                }
                ToggleInventory();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        SetKey(wParam, true);
        return 0;
    case WM_KEYUP:
        SetKey(wParam, false);
        return 0;
    case WM_LBUTTONDOWN: {
        RECT client{};
        GetClientRect(hwnd, &client);
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        const int hit = HitInventorySlot(client, x, y);
        if (hit >= 0) {
            g_game.inventory.focusedSlot = hit;
            if (hit < kHotbarSlotCount) {
                g_game.inventory.selectedHotbar = hit;
            }
            ItemStack* item = InventoryItemAt(hit);
            if (item && !item->id.empty()) {
                g_game.inventory.dragging = true;
                g_game.inventory.dragSource = hit;
                g_game.inventory.dragPoint = POINT{x, y};
                SetCapture(hwnd);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (PointInInventoryPanel(client, x, y)) {
            return 0;
        }
        const int pickupIndex = HitWorldPickup(x, y);
        if (pickupIndex >= 0) {
            WorldPickup& pickup = CurrentPickups()[pickupIndex];
            pickup.activated = true;
            g_game.pickupNotice = pickup.item.displayName;
            g_game.pickupNoticeTime = 1.5f;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g_game.inventory.dragging && (wParam & MK_LBUTTON)) {
            g_game.inventory.dragPoint = POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP: {
        if (!g_game.inventory.dragging) {
            return 0;
        }
        RECT client{};
        GetClientRect(hwnd, &client);
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        const int source = g_game.inventory.dragSource;
        const int target = HitInventorySlot(client, x, y);
        if (target >= 0) {
            MoveInventoryItem(source, target);
            g_game.inventory.focusedSlot = target;
        } else if (x >= client.left && x < client.right && y >= client.top && y < client.bottom &&
                   !PointInInventoryPanel(client, x, y)) {
            DropInventoryItemOnGround(source, x, y);
        }
        g_game.inventory.dragging = false;
        g_game.inventory.dragSource = -1;
        ReleaseCapture();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (g_game.inventory.dragging) {
            g_game.inventory.dragging = false;
            g_game.inventory.dragSource = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        RenderGame(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, kFrameTimer);
        ReleaseBackgroundResources();
        rpg::ReleaseSceneRenderResources();
        rpg::ReleaseTerrainRenderResources();
        for (SpriteSheet& sprite : g_playerIdleSprites) {
            sprite.bitmap.reset();
            sprite.loaded = false;
        }
        for (SpriteSheet* sprite : {&g_playerSprite, &g_playerRunFront, &g_playerRunBack}) {
            sprite->bitmap.reset();
            sprite->loaded = false;
        }
        g_itemBitmaps.clear();
        if (g_gdiplusToken != 0) {
            Gdiplus::GdiplusShutdown(g_gdiplusToken);
            g_gdiplusToken = 0;
        }
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR commandLine, int showCmd) {
    if (commandLine && *commandLine) {
        std::wstring value(commandLine);
        if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') {
            value = value.substr(1, value.size() - 2);
        }
        g_requestedScenePath = std::filesystem::path(value);
    }
    const wchar_t kClassName[] = L"FreeWalkRpgDemoWindow";

    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    RegisterClassW(&wc);

    RECT windowRect{0, 0, kWindowWidth, kWindowHeight};
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowExW(
        0,
        kClassName,
        L"Free Walk RPG Demo",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (!hwnd) {
        return 1;
    }

    ShowWindow(hwnd, showCmd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}
