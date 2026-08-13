#include <windows.h>
#include <windowsx.h>

#include "item.h"
#include "scene.h"
#include "scene_render.h"
#include "save_game.h"
#include "terrain_render.h"
#include "territory_render.h"
#include "world_layers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <gdiplus.h>
#include <iterator>
#include <chrono>
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
constexpr int kSpriteFrameSize = 379;
constexpr float kSpriteDrawSize = static_cast<float>(kSpriteFrameSize) / kWorldScale;
constexpr int kSideWalkFrames = 8;
constexpr int kFrontAttackFrames = 5;
constexpr int kSideAttackFrames = 4;
constexpr float kAttackFramesPerSecond = 16.0f;
constexpr float kDefaultZoom = 1.0f / kWorldScale;
constexpr float kMinimumZoom = kDefaultZoom;
constexpr float kMaximumZoom = 1.0f;
constexpr float kZoomStep = 1.0f / 6.0f;
constexpr UINT_PTR kFrameTimer = 1;
constexpr UINT kFrameMs = 16;
constexpr int kHotbarSlotCount = 10;
constexpr int kBackpackSlotCount = 30;
constexpr int kInventorySlotCount = kHotbarSlotCount + kBackpackSlotCount;
constexpr int kSpiritSlotCount = 3;
constexpr int kInventoryCellSize = 48;
constexpr int kInventoryCellGap = 5;
constexpr int kMaximumFollowers = 4;
constexpr float kNpcMinimumDistance = 80.0f;
constexpr float kNpcFollowStartDistance = 200.0f;
constexpr float kNpcFollowStopDistance = 145.0f;
constexpr float kNpcCombatLeashDistance = 500.0f;

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
    bool attacking = false;
    float attackTime = 0.0f;
    int attackDir = 0;
};

struct SpriteSheet {
    std::unique_ptr<Gdiplus::Bitmap> bitmap;
    bool loaded = false;
};

struct Npc {
    Vec2 pos;
    Vec2 velocity{};
    std::wstring name;
    std::wstring text;
    bool following = false;
    bool combatStoneEquipped = true;
    bool inCombat = false;
    bool moving = false;
    float attackCooldown = 0.0f;
};

struct Monster {
    Vec2 pos;
    int health = 60;
    int maxHealth = 60;
    float attackCooldown = 0.0f;
    bool alive = true;
};

struct ItemStack {
    std::string id;
    std::wstring displayName;
    int count = 0;
};

struct Inventory {
    std::array<ItemStack, kInventorySlotCount> slots;
    std::array<ItemStack, kSpiritSlotCount> spiritSlots;
    ItemStack heldItem;
    int selectedHotbar = 0;
    int focusedSlot = 0;
    bool open = false;
    bool dragging = false;
    int dragSource = -1;
    POINT dragPoint{};
    POINT mousePoint{};
    float spiritMenuAmount = 0.0f;
    bool placingAnchor = false;
    bool selectingTerritory = false;
    POINT territoryStartTile{};
    POINT territoryEndTile{};
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
    std::vector<Npc> npcs{
        {{360.0f, 170.0f}, {}, L"莉娜", L"活人的脚步会在囚地留下痕迹。"},
        {{780.0f, 430.0f}, {}, L"诺亚", L"结界之外，影子无法触碰现实。"},
        {{1040.0f, 250.0f}, {}, L"米拉", L"灵石能短暂赋予影子实体。"},
        {{520.0f, 640.0f}, {}, L"塞恩", L"锚点稳定时，我能感受到现实的重量。"},
    };
    std::vector<Monster> monsters;
    std::wstring monsterSceneKey;
    int npcContextIndex = -1;
    POINT npcContextPoint{};
    int activeCombatMonster = -1;
    int playerHealth = 100;
    float teleportCooldown = 0.0f;
    LARGE_INTEGER lastTick{};
    LARGE_INTEGER freq{};
};

enum class AppScreen {
    MainMenu,
    NewGame,
    LoadGame,
    Settings,
    Playing,
};

Game g_game;
AppScreen g_screen = AppScreen::MainMenu;
rpg::GameDifficulty g_newDifficulty = rpg::GameDifficulty::Normal;
float g_newResourceMultiplier = 1.0f;
float g_defaultZoom = kDefaultZoom;
std::vector<rpg::SaveGameInfo> g_saveList;
std::filesystem::path g_activeSaveDirectory;
std::filesystem::path g_currentScenePath;
rpg::SaveGameInfo g_activeSave;
std::wstring g_menuStatus;
std::filesystem::path g_requestedScenePath;
SpriteSheet g_playerSprite;
SpriteSheet g_playerRunFront;
SpriteSheet g_playerRunBack;
SpriteSheet g_playerAttackFront;
SpriteSheet g_playerAttackSide;
SpriteSheet g_playerAttackBack;
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
    if (!rpg::SceneSupportsBackgroundImage(g_game.scene)) {
        ReleaseBackgroundResources();
        return false;
    }
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
    if (!rpg::SceneSupportsBackgroundImage(g_game.scene)) {
        return false;
    }
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
    const auto blockedByFollower = [](Vec2 pos) {
        return std::any_of(g_game.npcs.begin(), g_game.npcs.end(), [&](const Npc& npc) {
            if (!npc.following) return false;
            const float dx = pos.x - npc.pos.x;
            const float dy = pos.y - npc.pos.y;
            const float nextDistanceSquared = dx * dx + dy * dy;
            const float currentDx = g_game.player.pos.x - npc.pos.x;
            const float currentDy = g_game.player.pos.y - npc.pos.y;
            const float currentDistanceSquared = currentDx * currentDx + currentDy * currentDy;
            return nextDistanceSquared < kNpcMinimumDistance * kNpcMinimumDistance &&
                   nextDistanceSquared <= currentDistanceSquared;
        });
    };
    Vec2 next = {player.pos.x + delta.x, player.pos.y};
    if (!CollidesWithMap(next, kPlayerRadius) && !blockedByFollower(next)) {
        player.pos.x = next.x;
    }

    next = {player.pos.x, player.pos.y + delta.y};
    if (!CollidesWithMap(next, kPlayerRadius) && !blockedByFollower(next)) {
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
    LoadSpriteSheet(g_playerAttackFront, AssetPath(L"player_attack_front.png"));
    LoadSpriteSheet(g_playerAttackSide, AssetPath(L"player_attack_side.png"));
    LoadSpriteSheet(g_playerAttackBack, AssetPath(L"player_attack_back.png"));
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
    if (path.filename() == L"veil_lands.json") {
        constexpr const char* stoneIds[] = {"gathering_stone", "building_stone", "combat_stone"};
        constexpr float offsets[] = {-58.0f, 0.0f, 58.0f};
        for (int i = 0; i < 3; ++i) {
            bool alreadyOwned = g_game.inventory.heldItem.id == stoneIds[i];
            for (const ItemStack& item : g_game.inventory.slots) alreadyOwned = alreadyOwned || item.id == stoneIds[i];
            for (const ItemStack& item : g_game.inventory.spiritSlots) alreadyOwned = alreadyOwned || item.id == stoneIds[i];
            if (alreadyOwned) continue;
            ItemStack stone = MakeItemStack(stoneIds[i], 1);
            if (!stone.id.empty()) {
                pickups.push_back({
                    {Clamp(g_game.player.pos.x + offsets[i], 30.0f, std::max(30.0f, width - 30.0f)),
                     Clamp(g_game.player.pos.y + 72.0f, 30.0f, std::max(30.0f, height - 30.0f))},
                    std::move(stone),
                });
            }
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

void SaveCurrentGame() {
    if (g_activeSaveDirectory.empty() || g_screen != AppScreen::Playing) {
        return;
    }
    g_activeSave.currentMap = g_currentScenePath.filename().wstring();
    g_activeSave.playerPosition = {g_game.player.pos.x, g_game.player.pos.y};
    g_activeSave.inventory.clear();
    for (int i = 0; i < static_cast<int>(g_game.inventory.slots.size()); ++i) {
        const ItemStack& item = g_game.inventory.slots[i];
        if (!item.id.empty() && item.count > 0) {
            g_activeSave.inventory.push_back({i, item.id, item.count});
        }
    }
    for (int i = 0; i < static_cast<int>(g_game.inventory.spiritSlots.size()); ++i) {
        const ItemStack& item = g_game.inventory.spiritSlots[i];
        if (!item.id.empty()) {
            g_activeSave.inventory.push_back({kInventorySlotCount + i, item.id, 1});
        }
    }
    g_activeSave.heldItemId = g_game.inventory.heldItem.id;
    g_activeSave.heldItemCount = g_game.inventory.heldItem.count;
    g_activeSave.selectedHotbar = g_game.inventory.selectedHotbar;
    g_activeSave.followerNpcIndices.clear();
    for (int i = 0; i < static_cast<int>(g_game.npcs.size()); ++i) {
        if (g_game.npcs[i].following) g_activeSave.followerNpcIndices.push_back(i);
    }
    rpg::SaveGameState(g_activeSaveDirectory, g_activeSave);
}

bool LoadSavedGame(const std::filesystem::path& saveDirectory) {
    rpg::ReloadObjectDefs();
    rpg::ReloadTerrainDefs();
    rpg::ReloadItemDefs();
    std::string error;
    if (!rpg::LoadSaveGame(saveDirectory, g_activeSave, &error)) {
        g_menuStatus = L"读取存档失败";
        return false;
    }
    const std::filesystem::path scenePath = saveDirectory / L"maps" / g_activeSave.currentMap;
    if (!rpg::LoadSceneFromFile(scenePath, g_game.scene, &error)) {
        g_menuStatus = L"存档地图损坏或缺失";
        return false;
    }
    rpg::InvalidateTerritoryRenderCache();
    g_activeSaveDirectory = saveDirectory;
    g_currentScenePath = scenePath;
    const bool savedPositionValid = g_activeSave.playerPosition.x > 0.0f && g_activeSave.playerPosition.y > 0.0f;
    g_game.player.pos = savedPositionValid
        ? Vec2{g_activeSave.playerPosition.x, g_activeSave.playerPosition.y}
        : Vec2{g_game.scene.playerStart.x, g_game.scene.playerStart.y};
    g_game.inventory = Inventory{};
    for (const rpg::SaveGameInfo::InventoryEntry& entry : g_activeSave.inventory) {
        if (entry.slot >= 0 && entry.slot < static_cast<int>(g_game.inventory.slots.size())) {
            g_game.inventory.slots[entry.slot] = MakeItemStack(entry.itemId, entry.count);
        } else if (entry.slot >= kInventorySlotCount && entry.slot < kInventorySlotCount + kSpiritSlotCount) {
            const int spiritIndex = entry.slot - kInventorySlotCount;
            constexpr const char* stoneIds[] = {"gathering_stone", "building_stone", "combat_stone"};
            if (entry.itemId == stoneIds[spiritIndex]) {
                g_game.inventory.spiritSlots[spiritIndex] = MakeItemStack(entry.itemId, 1);
            }
        }
    }
    for (const ItemStack& stone : g_game.inventory.spiritSlots) {
        if (stone.id == g_activeSave.heldItemId) {
            g_game.inventory.heldItem = stone;
            break;
        }
    }
    g_game.inventory.selectedHotbar = g_activeSave.selectedHotbar;
    g_game.inventory.focusedSlot = g_activeSave.selectedHotbar;
    for (Npc& npc : g_game.npcs) {
        npc.following = false;
        npc.inCombat = false;
        npc.moving = false;
    }
    for (const int index : g_activeSave.followerNpcIndices) {
        if (index >= 0 && index < static_cast<int>(g_game.npcs.size())) g_game.npcs[index].following = true;
    }
    bool hasAnchor = false;
    for (const ItemStack& item : g_game.inventory.slots) hasAnchor = hasAnchor || item.id == "territory_anchor";
    for (const rpg::SceneObject& object : g_game.scene.objects) hasAnchor = hasAnchor || object.type == "territory_anchor";
    if (!hasAnchor) {
        for (ItemStack& slot : g_game.inventory.slots) {
            if (slot.id.empty()) {
                slot = MakeItemStack("territory_anchor", 1);
                break;
            }
        }
    }
    g_game.zoom = g_defaultZoom;
    ActivateScenePickups(scenePath);
    g_screen = AppScreen::Playing;
    return true;
}

std::filesystem::path ResolveScenePath(const std::string& storedPath) {
    const std::filesystem::path path = std::filesystem::u8path(storedPath);
    if (path.is_absolute()) {
        return path;
    }
    if (!g_currentScenePath.empty()) {
        return g_currentScenePath.parent_path() / path;
    }
    return std::filesystem::path(RPG_ASSET_DIR) / path;
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
    rpg::InvalidateTerritoryRenderCache();
    g_game.player.pos = {destination.x, destination.y};
    g_game.player.animTime = 0.0f;
    g_game.player.attacking = false;
    g_game.player.attackTime = 0.0f;
    g_game.teleportCooldown = 0.8f;
    g_game.activeCombatMonster = -1;
    g_game.monsterSceneKey.clear();
    int followerSlot = 0;
    for (Npc& npc : g_game.npcs) {
        npc.inCombat = false;
        npc.moving = false;
        npc.velocity = {};
        if (!npc.following) continue;
        constexpr Vec2 offsets[kMaximumFollowers] = {
            {-125.0f, 0.0f}, {125.0f, 0.0f}, {0.0f, 125.0f}, {0.0f, -125.0f},
        };
        const Vec2 offset = offsets[std::clamp(followerSlot++, 0, kMaximumFollowers - 1)];
        const Vec2 candidate{g_game.player.pos.x + offset.x, g_game.player.pos.y + offset.y};
        npc.pos = CollidesWithMap(candidate, 15.0f) ? g_game.player.pos : candidate;
        npc.moving = npc.pos.x == g_game.player.pos.x && npc.pos.y == g_game.player.pos.y;
    }
    g_currentScenePath = path;
    ActivateScenePickups(path);
    SaveCurrentGame();
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

Vec2 Normalize(Vec2 value) {
    const float length = std::sqrt(value.x * value.x + value.y * value.y);
    return length > 0.001f ? Vec2{value.x / length, value.y / length} : Vec2{};
}

bool ActorProtectedByTerritory(Vec2 pos) {
    const int tx = static_cast<int>(std::floor(pos.x / kTileSize));
    const int ty = static_cast<int>(std::floor(pos.y / kTileSize));
    return rpg::TerritoryAt(g_game.scene, tx, ty);
}

bool CircleIntersectsTerritory(Vec2 pos, float radius) {
    const int left = static_cast<int>(std::floor((pos.x - radius) / kTileSize));
    const int right = static_cast<int>(std::floor((pos.x + radius) / kTileSize));
    const int top = static_cast<int>(std::floor((pos.y - radius) / kTileSize));
    const int bottom = static_cast<int>(std::floor((pos.y + radius) / kTileSize));
    for (int ty = top; ty <= bottom; ++ty) {
        for (int tx = left; tx <= right; ++tx) {
            if (!rpg::TerritoryAt(g_game.scene, tx, ty)) continue;
            const float tileLeft = static_cast<float>(tx * kTileSize);
            const float tileTop = static_cast<float>(ty * kTileSize);
            const float nearestX = Clamp(pos.x, tileLeft, tileLeft + kTileSize);
            const float nearestY = Clamp(pos.y, tileTop, tileTop + kTileSize);
            const float dx = pos.x - nearestX;
            const float dy = pos.y - nearestY;
            if (dx * dx + dy * dy < radius * radius) return true;
        }
    }
    return false;
}

bool MonsterPositionAvailable(Vec2 pos) {
    constexpr float monsterRadius = 16.0f;
    return !CollidesWithMap(pos, monsterRadius) && !CircleIntersectsTerritory(pos, monsterRadius);
}

bool MoveMonsterOutsideTerritory(Monster& monster) {
    if (!CircleIntersectsTerritory(monster.pos, 16.0f)) return true;
    const int originX = static_cast<int>(std::floor(monster.pos.x / kTileSize));
    const int originY = static_cast<int>(std::floor(monster.pos.y / kTileSize));
    const int maximumRadius = std::max(g_game.scene.mapWidth, g_game.scene.mapHeight);
    for (int radius = 1; radius <= maximumRadius; ++radius) {
        for (int y = originY - radius; y <= originY + radius; ++y) {
            for (int x = originX - radius; x <= originX + radius; ++x) {
                if (std::max(std::abs(x - originX), std::abs(y - originY)) != radius) continue;
                const Vec2 candidate{(x + 0.5f) * kTileSize, (y + 0.5f) * kTileSize};
                if (MonsterPositionAvailable(candidate)) {
                    monster.pos = candidate;
                    return true;
                }
            }
        }
    }
    return false;
}

int FollowerCount() {
    return static_cast<int>(std::count_if(g_game.npcs.begin(), g_game.npcs.end(), [](const Npc& npc) {
        return npc.following;
    }));
}

RECT NpcContextButtonRect() {
    return {
        g_game.npcContextPoint.x,
        g_game.npcContextPoint.y,
        g_game.npcContextPoint.x + 116,
        g_game.npcContextPoint.y + 36,
    };
}

int HitNpc(int screenX, int screenY) {
    const float scale = RenderScale();
    for (int i = static_cast<int>(g_game.npcs.size()) - 1; i >= 0; --i) {
        const float x = g_game.npcs[i].pos.x * scale - g_game.camera.x;
        const float y = g_game.npcs[i].pos.y * scale - g_game.camera.y;
        const float dx = screenX - x;
        const float dy = screenY - y;
        const float radius = std::max(16.0f, 22.0f * scale);
        if (dx * dx + dy * dy <= radius * radius) return i;
    }
    return -1;
}

bool NpcPositionAvailable(Vec2 pos, int npcIndex, bool keepPlayerDistance) {
    constexpr float npcRadius = 15.0f;
    if (CollidesWithMap(pos, npcRadius)) return false;
    if (keepPlayerDistance && Distance(pos, g_game.player.pos) < kNpcMinimumDistance &&
        Distance(pos, g_game.player.pos) <= Distance(g_game.npcs[npcIndex].pos, g_game.player.pos)) return false;
    for (int i = 0; i < static_cast<int>(g_game.npcs.size()); ++i) {
        if (i == npcIndex || !g_game.npcs[i].following) continue;
        if (Distance(pos, g_game.npcs[i].pos) < kNpcMinimumDistance) return false;
    }
    return true;
}

void MoveNpcToward(int npcIndex, Vec2 target, float speed, float dt, bool keepPlayerDistance = true) {
    Npc& npc = g_game.npcs[npcIndex];
    Vec2 direction = Normalize({target.x - npc.pos.x, target.y - npc.pos.y});
    Vec2 separation{};
    for (int i = 0; i < static_cast<int>(g_game.npcs.size()); ++i) {
        if (i == npcIndex || !g_game.npcs[i].following) continue;
        const Vec2 delta{npc.pos.x - g_game.npcs[i].pos.x, npc.pos.y - g_game.npcs[i].pos.y};
        const float distance = Distance(npc.pos, g_game.npcs[i].pos);
        if (distance < kNpcMinimumDistance + 24.0f && distance > 0.001f) {
            const Vec2 away = Normalize(delta);
            separation.x += away.x * (kNpcMinimumDistance + 24.0f - distance) / kNpcMinimumDistance;
            separation.y += away.y * (kNpcMinimumDistance + 24.0f - distance) / kNpcMinimumDistance;
        }
    }
    direction = Normalize({direction.x + separation.x * 1.4f, direction.y + separation.y * 1.4f});
    const Vec2 delta{direction.x * speed * dt, direction.y * speed * dt};
    Vec2 next{npc.pos.x + delta.x, npc.pos.y};
    if (NpcPositionAvailable(next, npcIndex, keepPlayerDistance)) npc.pos.x = next.x;
    next = {npc.pos.x, npc.pos.y + delta.y};
    if (NpcPositionAvailable(next, npcIndex, keepPlayerDistance)) npc.pos.y = next.y;
    npc.velocity = delta;
}

Vec2 FollowerSlotPosition(int followerSlot) {
    constexpr Vec2 offsets[kMaximumFollowers] = {
        {-125.0f, 0.0f},
        {125.0f, 0.0f},
        {0.0f, 125.0f},
        {0.0f, -125.0f},
    };
    const Vec2 offset = offsets[std::clamp(followerSlot, 0, kMaximumFollowers - 1)];
    return {g_game.player.pos.x + offset.x, g_game.player.pos.y + offset.y};
}

void EnsureSceneMonsters() {
    if (g_game.activePickupSceneKey.empty() || g_game.activePickupSceneKey == g_game.monsterSceneKey) return;
    g_game.monsterSceneKey = g_game.activePickupSceneKey;
    g_game.activeCombatMonster = -1;
    g_game.monsters.clear();
    const float width = rpg::SceneWorldWidth(g_game.scene);
    const float height = rpg::SceneWorldHeight(g_game.scene);
    constexpr Vec2 nearbyOffsets[] = {{240.0f, 0.0f}, {-240.0f, 0.0f}, {0.0f, 240.0f}, {0.0f, -240.0f}};
    for (const Vec2 offset : nearbyOffsets) {
        const Vec2 pos{g_game.player.pos.x + offset.x, g_game.player.pos.y + offset.y};
        if (MonsterPositionAvailable(pos)) {
            g_game.monsters.push_back({pos});
            break;
        }
    }
    constexpr Vec2 ratios[] = {{0.32f, 0.28f}, {0.63f, 0.42f}, {0.48f, 0.70f}};
    for (const Vec2 ratio : ratios) {
        Vec2 pos{Clamp(width * ratio.x, 40.0f, width - 40.0f), Clamp(height * ratio.y, 40.0f, height - 40.0f)};
        if (MonsterPositionAvailable(pos)) g_game.monsters.push_back({pos});
    }
}

void EnterFollowerCombat(int monsterIndex) {
    if (monsterIndex < 0 || monsterIndex >= static_cast<int>(g_game.monsters.size()) ||
        !g_game.monsters[monsterIndex].alive) return;
    g_game.activeCombatMonster = monsterIndex;
    for (Npc& npc : g_game.npcs) {
        if (npc.following && npc.combatStoneEquipped) npc.inCombat = true;
    }
}

void UpdateMonsters(float dt) {
    EnsureSceneMonsters();
    for (int i = 0; i < static_cast<int>(g_game.monsters.size()); ++i) {
        Monster& monster = g_game.monsters[i];
        if (!monster.alive) continue;
        if (!MoveMonsterOutsideTerritory(monster)) continue;
        monster.attackCooldown = std::max(0.0f, monster.attackCooldown - dt);
        if (ActorProtectedByTerritory(g_game.player.pos)) continue;
        const float playerDistance = Distance(monster.pos, g_game.player.pos);
        if (playerDistance < 300.0f && playerDistance > 46.0f) {
            const Vec2 direction = Normalize({g_game.player.pos.x - monster.pos.x, g_game.player.pos.y - monster.pos.y});
            const Vec2 next{monster.pos.x + direction.x * 72.0f * dt, monster.pos.y + direction.y * 72.0f * dt};
            if (MonsterPositionAvailable(next)) monster.pos = next;
        } else if (playerDistance <= 46.0f && monster.attackCooldown <= 0.0f) {
            g_game.playerHealth = std::max(0, g_game.playerHealth - 8);
            monster.attackCooldown = 1.1f;
            EnterFollowerCombat(i);
            g_game.pickupNotice = L"畸变暗影击中了你";
            g_game.pickupNoticeTime = 1.0f;
        }
    }
}

void UpdateNpcs(float dt) {
    int followerSlot = 0;
    for (int i = 0; i < static_cast<int>(g_game.npcs.size()); ++i) {
        Npc& npc = g_game.npcs[i];
        npc.attackCooldown = std::max(0.0f, npc.attackCooldown - dt);
        npc.velocity = {};
        if (!npc.following) continue;

        if (npc.inCombat) {
            const bool validTarget = g_game.activeCombatMonster >= 0 &&
                g_game.activeCombatMonster < static_cast<int>(g_game.monsters.size()) &&
                g_game.monsters[g_game.activeCombatMonster].alive;
            if (!validTarget || Distance(npc.pos, g_game.player.pos) > kNpcCombatLeashDistance) {
                npc.inCombat = false;
            } else {
                Monster& monster = g_game.monsters[g_game.activeCombatMonster];
                const float targetDistance = Distance(npc.pos, monster.pos);
                if (targetDistance > 58.0f) {
                    MoveNpcToward(i, monster.pos, 165.0f, dt, false);
                } else if (npc.attackCooldown <= 0.0f) {
                    monster.health -= 12;
                    npc.attackCooldown = 0.65f;
                    if (monster.health <= 0) {
                        monster.health = 0;
                        monster.alive = false;
                        g_game.activeCombatMonster = -1;
                        for (Npc& follower : g_game.npcs) follower.inCombat = false;
                        g_game.pickupNotice = L"畸变暗影已消散";
                        g_game.pickupNoticeTime = 1.5f;
                    }
                }
                ++followerSlot;
                continue;
            }
        }

        const float playerDistance = Distance(npc.pos, g_game.player.pos);
        if (!npc.moving && (playerDistance > kNpcFollowStartDistance || playerDistance < kNpcMinimumDistance)) npc.moving = true;
        if (npc.moving && playerDistance <= kNpcFollowStopDistance) npc.moving = false;
        if (npc.moving) MoveNpcToward(i, FollowerSlotPosition(followerSlot), 145.0f, dt);
        ++followerSlot;
    }
}

void UpdateNearbyNpc() {
    g_game.nearbyNpc = -1;
    for (int i = 0; i < static_cast<int>(g_game.npcs.size()); ++i) {
        if (Distance(g_game.player.pos, g_game.npcs[i].pos) < 72.0f) {
            g_game.nearbyNpc = i;
            return;
        }
    }
}

void UpdateGame(float dt) {
    g_game.teleportCooldown = std::max(0.0f, g_game.teleportCooldown - dt);
    g_game.pickupNoticeTime = std::max(0.0f, g_game.pickupNoticeTime - dt);
    if (g_game.player.attacking) {
        g_game.player.attackTime += dt;
        const int attackFrames = (g_game.player.attackDir == 1 || g_game.player.attackDir == 2)
            ? kSideAttackFrames
            : kFrontAttackFrames;
        if (g_game.player.attackTime >= attackFrames / kAttackFramesPerSecond) {
            g_game.player.attacking = false;
            g_game.player.attackTime = 0.0f;
        }
    }
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
    UpdateMonsters(dt);
    UpdateNpcs(dt);
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
    int frame = 0;
    int sx = 0;
    int sy = 0;
    bool mirror = false;
    bool drawingAttack = false;
    if (g_game.player.attacking) {
        const bool sideAttack = g_game.player.attackDir == 1 || g_game.player.attackDir == 2;
        if (sideAttack && g_playerAttackSide.bitmap) {
            sprite = &g_playerAttackSide;
            frameCount = kSideAttackFrames;
            mirror = g_game.player.attackDir == 1;
            drawingAttack = true;
        } else if (g_game.player.attackDir == 3 && g_playerAttackBack.bitmap) {
            sprite = &g_playerAttackBack;
            frameCount = kFrontAttackFrames;
            drawingAttack = true;
        } else if (g_playerAttackFront.bitmap) {
            sprite = &g_playerAttackFront;
            frameCount = kFrontAttackFrames;
            drawingAttack = true;
        }
        frame = std::min(
            frameCount - 1,
            static_cast<int>(g_game.player.attackTime * kAttackFramesPerSecond));
        sx = frame * kSpriteFrameSize;
    } else if (moving && g_game.player.dir == 0 && g_playerRunFront.bitmap) {
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

    if (!drawingAttack) {
        frame = moving
            ? (static_cast<int>(g_game.player.animTime * animationSpeed) % frameCount)
            : 0;
        mirror = moving ? g_game.player.dir == 1 : g_game.player.dir == 2;
        sx = (frame % 4) * kSpriteFrameSize;
        sy = (frame / 4) * kSpriteFrameSize;
    }

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
    int spiritX = 0;
    int spiritY = 0;
    RECT panel{};
};

constexpr const char* kGatheringStoneId = "gathering_stone";
constexpr const char* kBuildingStoneId = "building_stone";
constexpr const char* kCombatStoneId = "combat_stone";

POINT HeldSlotCenter(const InventoryLayout& layout) {
    return {layout.heldX + kInventoryCellSize / 2, layout.hotbarY + kInventoryCellSize / 2};
}

POINT ScreenToTile(int screenX, int screenY) {
    const float scale = RenderScale();
    return {
        static_cast<LONG>(std::floor((screenX + g_game.camera.x) / scale / kTileSize)),
        static_cast<LONG>(std::floor((screenY + g_game.camera.y) / scale / kTileSize)),
    };
}

bool HasTerritoryNeighbor(int tx, int ty) {
    return rpg::TerritoryAt(g_game.scene, tx - 1, ty) || rpg::TerritoryAt(g_game.scene, tx + 1, ty) ||
           rpg::TerritoryAt(g_game.scene, tx, ty - 1) || rpg::TerritoryAt(g_game.scene, tx, ty + 1);
}

bool HasTerritoryAnchor() {
    return std::any_of(g_game.scene.objects.begin(), g_game.scene.objects.end(), [](const rpg::SceneObject& object) {
        return object.type == "territory_anchor";
    });
}

void SaveCurrentScene() {
    if (!g_currentScenePath.empty()) rpg::SaveSceneToFile(g_currentScenePath, g_game.scene);
    SaveCurrentGame();
}

bool PlaceTerritoryAnchor(int tx, int ty) {
    if (HasTerritoryAnchor() || tx < 3 || ty < 3 || tx + 3 >= g_game.scene.mapWidth || ty + 3 >= g_game.scene.mapHeight) {
        g_game.pickupNotice = L"锚点需要完整的 7x7 空间";
        g_game.pickupNoticeTime = 1.5f;
        return false;
    }
    const rpg::Vec2 center{(tx + 0.5f) * kTileSize, (ty + 0.5f) * kTileSize};
    if (rpg::CircleIntersectsBlockedTerrain(g_game.scene, center, 18.0f) ||
        rpg::CircleIntersectsScene(g_game.scene, center, 18.0f)) {
        g_game.pickupNotice = L"该地格无法放置锚点";
        g_game.pickupNoticeTime = 1.5f;
        return false;
    }
    rpg::SceneObject anchor = rpg::MakeObject(
        "territory_anchor",
        {(tx + 0.5f) * kTileSize, (ty + 1.0f) * kTileSize},
        static_cast<int>(g_game.scene.objects.size()) + 10000);
    anchor.id = "territory_anchor";
    g_game.scene.objects.push_back(std::move(anchor));
    for (int y = ty - 3; y <= ty + 3; ++y) {
        for (int x = tx - 3; x <= tx + 3; ++x) rpg::SetTerritory(g_game.scene, x, y, true);
    }
    rpg::InvalidateTerritoryRenderCache();
    for (ItemStack& item : g_game.inventory.slots) {
        if (item.id == "territory_anchor") {
            item = {};
            break;
        }
    }
    g_game.inventory.placingAnchor = false;
    g_game.pickupNotice = L"现实锚点已建立，生成 7x7 领地";
    g_game.pickupNoticeTime = 2.0f;
    SaveCurrentScene();
    return true;
}

bool ExpandTerritoryRect(POINT first, POINT second) {
    const int left = std::clamp<int>(std::min(first.x, second.x), 0, g_game.scene.mapWidth - 1);
    const int right = std::clamp<int>(std::max(first.x, second.x), 0, g_game.scene.mapWidth - 1);
    const int top = std::clamp<int>(std::min(first.y, second.y), 0, g_game.scene.mapHeight - 1);
    const int bottom = std::clamp<int>(std::max(first.y, second.y), 0, g_game.scene.mapHeight - 1);
    bool changed = false;
    bool progress = true;
    while (progress) {
        progress = false;
        for (int y = top; y <= bottom; ++y) {
            for (int x = left; x <= right; ++x) {
                if (!rpg::TerritoryAt(g_game.scene, x, y) && HasTerritoryNeighbor(x, y)) {
                    rpg::SetTerritory(g_game.scene, x, y, true);
                    changed = true;
                    progress = true;
                }
            }
        }
    }
    g_game.pickupNotice = changed ? L"领地已扩张" : L"飞地无效：新区域必须连接现有领地";
    g_game.pickupNoticeTime = 1.6f;
    if (changed) {
        rpg::InvalidateTerritoryRenderCache();
        SaveCurrentScene();
    }
    return changed;
}

void DrawTerritoryPreview(HDC hdc) {
    const float scale = RenderScale();
    HPEN previewPen = CreatePen(PS_DOT, 2, RGB(255, 222, 116));
    HGDIOBJ oldPen = SelectObject(hdc, previewPen);
    if (g_game.inventory.selectingTerritory) {
        SelectObject(hdc, previewPen);
        const POINT a = g_game.inventory.territoryStartTile;
        const POINT b = g_game.inventory.territoryEndTile;
        const int left = static_cast<int>(std::round(std::min(a.x, b.x) * kTileSize * scale - g_game.camera.x));
        const int top = static_cast<int>(std::round(std::min(a.y, b.y) * kTileSize * scale - g_game.camera.y));
        const int right = static_cast<int>(std::round((std::max(a.x, b.x) + 1) * kTileSize * scale - g_game.camera.x));
        const int bottom = static_cast<int>(std::round((std::max(a.y, b.y) + 1) * kTileSize * scale - g_game.camera.y));
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, left, top, right, bottom);
        SelectObject(hdc, oldBrush);
    }
    SelectObject(hdc, oldPen);
    DeleteObject(previewPen);
}

POINT SpiritMenuCenter(const InventoryLayout& layout, float amount) {
    const POINT held = HeldSlotCenter(layout);
    return {held.x, held.y - static_cast<int>(std::round(78.0f * amount))};
}

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
    if (index >= kInventorySlotCount && index < kInventorySlotCount + kSpiritSlotCount) {
        return &g_game.inventory.spiritSlots[index - kInventorySlotCount];
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
    layout.spiritX = layout.heldX;

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
        layout.spiritY = layout.gridY;
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

void DrawHeldSkillSlot(HDC hdc, const InventoryLayout& layout) {
    const POINT center = HeldSlotCenter(layout);
    constexpr int radius = 24;
    const bool active = !g_game.inventory.heldItem.id.empty();
    HBRUSH brush = CreateSolidBrush(active ? RGB(74, 91, 70) : RGB(45, 53, 52));
    HPEN pen = CreatePen(PS_SOLID, active ? 3 : 2, active ? RGB(229, 207, 112) : RGB(135, 145, 139));
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    Ellipse(hdc, center.x - radius, center.y - radius, center.x + radius, center.y + radius);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);

    if (active) {
        RECT icon{center.x - 15, center.y - 15, center.x + 15, center.y + 15};
        if (!DrawItemImage(hdc, g_game.inventory.heldItem, icon, false)) {
            FillRectColor(hdc, icon, ItemColor(g_game.inventory.heldItem));
        }
    }
}

void DrawSpiritMenu(HDC hdc, const InventoryLayout& layout) {
    const float amount = g_game.inventory.spiritMenuAmount;
    if (amount <= 0.01f) return;
    const POINT center = SpiritMenuCenter(layout, amount);
    const int radius = std::max(1, static_cast<int>(std::round(72.0f * amount)));
    constexpr COLORREF colors[] = {RGB(72, 142, 91), RGB(197, 157, 65), RGB(169, 66, 68)};
    constexpr const wchar_t* labels[] = {L"采集", L"建造", L"战斗"};
    constexpr const char* ids[] = {kGatheringStoneId, kBuildingStoneId, kCombatStoneId};
    constexpr float starts[] = {210.0f, 330.0f, 90.0f};
    constexpr double pi = 3.14159265358979323846;

    for (int i = 0; i < 3; ++i) {
        const bool available = !g_game.inventory.spiritSlots[i].id.empty();
        HBRUSH brush = CreateSolidBrush(available ? colors[i] : RGB(48, 53, 52));
        HPEN pen = CreatePen(PS_SOLID, g_game.inventory.heldItem.id == ids[i] ? 4 : 2,
            g_game.inventory.heldItem.id == ids[i] ? RGB(255, 240, 142) : RGB(222, 226, 210));
        HGDIOBJ oldBrush = SelectObject(hdc, brush);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        const double start = starts[i] * pi / 180.0;
        const double end = (starts[i] + 120.0f) * pi / 180.0;
        Pie(hdc, center.x - radius, center.y - radius, center.x + radius, center.y + radius,
            center.x + static_cast<int>(std::cos(start) * radius), center.y - static_cast<int>(std::sin(start) * radius),
            center.x + static_cast<int>(std::cos(end) * radius), center.y - static_cast<int>(std::sin(end) * radius));
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);

        const double middle = (starts[i] + 60.0f) * pi / 180.0;
        if (available) {
            DrawTextLine(hdc, labels[i],
                center.x + static_cast<int>(std::cos(middle) * radius * 0.55) - 18,
                center.y - static_cast<int>(std::sin(middle) * radius * 0.55) - 9,
                RGB(255, 255, 238));
        }
    }
}

int HitSpiritMenu(const RECT& client, int x, int y) {
    if (g_game.inventory.spiritMenuAmount < 0.75f) return -1;
    const InventoryLayout layout = MakeInventoryLayout(client);
    const POINT center = SpiritMenuCenter(layout, g_game.inventory.spiritMenuAmount);
    const float dx = static_cast<float>(x - center.x);
    const float dy = static_cast<float>(center.y - y);
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance > 72.0f || distance < 12.0f) return -1;
    float degrees = static_cast<float>(std::atan2(dy, dx) * 180.0 / 3.14159265358979323846);
    if (degrees < 0.0f) degrees += 360.0f;
    if (degrees >= 210.0f && degrees < 330.0f) return 0;
    if (degrees >= 330.0f || degrees < 90.0f) return 1;
    return 2;
}

void SelectSpiritStone(int index) {
    constexpr const char* ids[] = {kGatheringStoneId, kBuildingStoneId, kCombatStoneId};
    if (index < 0 || index >= 3) return;
    if (g_game.inventory.spiritSlots[index].id != ids[index]) {
        g_game.pickupNotice = L"对应灵石槽为空";
        g_game.pickupNoticeTime = 1.3f;
        return;
    }
    if (g_game.inventory.heldItem.id == ids[index]) {
        g_game.inventory.heldItem = {};
        g_game.pickupNotice = L"已取消灵石技能";
    } else {
        g_game.inventory.heldItem = g_game.inventory.spiritSlots[index];
        g_game.pickupNotice = g_game.inventory.heldItem.displayName + L"已启用";
    }
    g_game.pickupNoticeTime = 1.5f;
    if (g_game.inventory.heldItem.id != kBuildingStoneId) {
        g_game.inventory.placingAnchor = false;
        g_game.inventory.selectingTerritory = false;
    }
    SaveCurrentGame();
}

void UpdateSpiritMenu(const RECT& client, float dt) {
    const InventoryLayout layout = MakeInventoryLayout(client);
    const POINT held = HeldSlotCenter(layout);
    const POINT menu = SpiritMenuCenter(layout, 1.0f);
    const float heldDx = static_cast<float>(g_game.inventory.mousePoint.x - held.x);
    const float heldDy = static_cast<float>(g_game.inventory.mousePoint.y - held.y);
    const float menuDx = static_cast<float>(g_game.inventory.mousePoint.x - menu.x);
    const float menuDy = static_cast<float>(g_game.inventory.mousePoint.y - menu.y);
    const float target = (heldDx * heldDx + heldDy * heldDy <= 30.0f * 30.0f ||
                          menuDx * menuDx + menuDy * menuDy <= 82.0f * 82.0f) ? 1.0f : 0.0f;
    const float step = dt * 7.5f;
    g_game.inventory.spiritMenuAmount = target > g_game.inventory.spiritMenuAmount
        ? std::min(target, g_game.inventory.spiritMenuAmount + step)
        : std::max(target, g_game.inventory.spiritMenuAmount - step);
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
        } else if (g_game.inventory.focusedSlot >= kInventorySlotCount &&
                   g_game.inventory.focusedSlot < kInventorySlotCount + kSpiritSlotCount) {
            focusedItem = &g_game.inventory.spiritSlots[g_game.inventory.focusedSlot - kInventorySlotCount];
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
        constexpr const wchar_t* spiritLabels[] = {L"采", L"建", L"战"};
        for (int i = 0; i < kSpiritSlotCount; ++i) {
            DrawInventorySlot(
                hdc,
                InventorySlotRect(layout.spiritX, layout.spiritY, 0, i),
                InventoryItemForDrawing(kInventorySlotCount + i),
                !g_game.inventory.spiritSlots[i].id.empty() &&
                    g_game.inventory.heldItem.id == g_game.inventory.spiritSlots[i].id,
                g_game.inventory.focusedSlot == kInventorySlotCount + i,
                spiritLabels[i]);
        }
        DrawTextLine(hdc, L"\u7269\u54c1\u680f", layout.gridX, layout.hotbarY - 22, RGB(205, 211, 198));
        DrawTextLine(hdc, L"灵石槽", layout.spiritX, layout.spiritY - 22, RGB(205, 211, 198));
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
    DrawHeldSkillSlot(hdc, layout);
    DrawSpiritMenu(hdc, layout);
}

int HitInventorySlot(const RECT& client, int x, int y) {
    const InventoryLayout layout = MakeInventoryLayout(client);
    for (int column = 0; column < kHotbarSlotCount; ++column) {
        RECT slot = InventorySlotRect(layout.gridX, layout.hotbarY, column);
        if (PtInRect(&slot, POINT{x, y})) {
            return column;
        }
    }
    if (!g_game.inventory.open) {
        return -1;
    }
    for (int i = 0; i < kSpiritSlotCount; ++i) {
        RECT slot = InventorySlotRect(layout.spiritX, layout.spiritY, 0, i);
        if (PtInRect(&slot, POINT{x, y})) {
            return kInventorySlotCount + i;
        }
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
    constexpr const char* stoneIds[] = {kGatheringStoneId, kBuildingStoneId, kCombatStoneId};
    const bool targetIsSpirit = targetIndex >= kInventorySlotCount && targetIndex < kInventorySlotCount + kSpiritSlotCount;
    if (targetIsSpirit && source->id != stoneIds[targetIndex - kInventorySlotCount]) {
        g_game.pickupNotice = L"该灵石只能放入对应槽位";
        g_game.pickupNoticeTime = 1.3f;
        return;
    }
    const bool sourceIsSpirit = sourceIndex >= kInventorySlotCount && sourceIndex < kInventorySlotCount + kSpiritSlotCount;
    if (sourceIsSpirit && !target->id.empty() && source->id != target->id) {
        g_game.pickupNotice = L"请拖到空背包格";
        g_game.pickupNoticeTime = 1.3f;
        return;
    }
    const std::string activeStone = g_game.inventory.heldItem.id;
    if (target->id.empty()) {
        *target = std::move(*source);
        *source = {};
        if (sourceIsSpirit && activeStone == target->id) {
            g_game.inventory.heldItem = {};
        }
        SaveCurrentGame();
        return;
    }
    if (source->id != target->id) {
        std::swap(*source, *target);
        SaveCurrentGame();
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
    SaveCurrentGame();
}

void DropInventoryItemOnGround(int sourceIndex, int screenX, int screenY) {
    ItemStack* source = InventoryItemAt(sourceIndex);
    if (!source || source->id.empty()) {
        return;
    }
    const std::string droppedId = source->id;
    const float scale = RenderScale();
    Vec2 world{
        (static_cast<float>(screenX) + g_game.camera.x) / scale,
        (static_cast<float>(screenY) + g_game.camera.y) / scale,
    };
    world.x = Clamp(world.x, 18.0f, std::max(18.0f, rpg::SceneWorldWidth(g_game.scene) - 18.0f));
    world.y = Clamp(world.y, 18.0f, std::max(18.0f, rpg::SceneWorldHeight(g_game.scene) - 18.0f));
    CurrentPickups().push_back({world, std::move(*source), true, false});
    *source = {};
    if (g_game.inventory.heldItem.id == droppedId) {
        g_game.inventory.heldItem = {};
    }
    SaveCurrentGame();
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

RECT MenuButtonRect(const RECT& client, int index, int count) {
    constexpr int width = 360;
    constexpr int height = 48;
    constexpr int gap = 14;
    const int totalHeight = count * height + (count - 1) * gap;
    const int left = (client.right - width) / 2;
    const int top = (client.bottom - totalHeight) / 2 + 35;
    return {left, top + index * (height + gap), left + width, top + index * (height + gap) + height};
}

bool PointInRect(const RECT& rect, int x, int y) {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

void DrawCenteredText(HDC hdc, const std::wstring& text, const RECT& rect, COLORREF color, int fontSize = 20) {
    HFONT font = CreateFontW(fontSize, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    HGDIOBJ oldFont = SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    RECT textRect = rect;
    DrawTextW(hdc, text.c_str(), -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(hdc, oldFont);
    DeleteObject(font);
}

void DrawMenuButton(HDC hdc, const RECT& rect, const std::wstring& label, bool selected = false) {
    FillRectColor(hdc, rect, selected ? RGB(92, 112, 78) : RGB(57, 68, 64));
    FrameRect(hdc, &rect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
    DrawCenteredText(hdc, label, rect, RGB(245, 242, 220));
}

void RenderMenu(HWND hwnd, HDC target) {
    RECT client{};
    GetClientRect(hwnd, &client);
    FillRectColor(target, client, RGB(31, 38, 36));
    RECT title{0, 65, client.right, 125};
    DrawCenteredText(target, L"垂纱之地", title, RGB(236, 224, 171), 36);

    if (g_screen == AppScreen::MainMenu) {
        constexpr const wchar_t* labels[] = {L"开始游戏", L"读取存档", L"设置", L"退出"};
        for (int i = 0; i < 4; ++i) DrawMenuButton(target, MenuButtonRect(client, i, 4), labels[i]);
    } else if (g_screen == AppScreen::NewGame) {
        const std::wstring difficulty = L"游戏难度：" + std::wstring(rpg::DifficultyName(g_newDifficulty));
        const std::wstring resources = L"资源生成倍率：" + std::to_wstring(static_cast<int>(g_newResourceMultiplier * 100.0f)) + L"%";
        const std::wstring labels[] = {difficulty, resources, L"开始", L"返回"};
        for (int i = 0; i < 4; ++i) DrawMenuButton(target, MenuButtonRect(client, i, 4), labels[i]);
    } else if (g_screen == AppScreen::LoadGame) {
        const int count = std::min(6, static_cast<int>(g_saveList.size())) + 1;
        for (int i = 0; i < count - 1; ++i) {
            const auto& save = g_saveList[i];
            DrawMenuButton(target, MenuButtonRect(client, i, count), save.name + L"  [" + rpg::DifficultyName(save.difficulty) + L"]");
        }
        DrawMenuButton(target, MenuButtonRect(client, count - 1, count), L"返回");
        if (g_saveList.empty()) {
            RECT hint{0, 145, client.right, 190};
            DrawCenteredText(target, L"暂无存档", hint, RGB(180, 190, 183), 18);
        }
    } else if (g_screen == AppScreen::Settings) {
        const std::wstring zoom = L"默认视野：" + std::to_wstring(static_cast<int>(g_defaultZoom * 300.0f)) + L"%";
        DrawMenuButton(target, MenuButtonRect(client, 0, 2), zoom);
        DrawMenuButton(target, MenuButtonRect(client, 1, 2), L"返回");
    }

    if (!g_menuStatus.empty()) {
        RECT status{20, client.bottom - 50, client.right - 20, client.bottom - 18};
        DrawCenteredText(target, g_menuStatus, status, RGB(235, 178, 126), 16);
    }
}

std::wstring NewSaveName() {
    const auto value = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    localtime_s(&local, &value);
    wchar_t buffer[64]{};
    wcsftime(buffer, std::size(buffer), L"存档_%Y%m%d_%H%M%S", &local);
    return buffer;
}

void BeginNewGame() {
    rpg::ReloadObjectDefs();
    rpg::ReloadTerrainDefs();
    rpg::SaveGameInfo info;
    info.name = NewSaveName();
    info.difficulty = g_newDifficulty;
    info.resourceMultiplier = g_newResourceMultiplier;
    std::string error;
    const std::filesystem::path root = rpg::DefaultSavesRoot();
    if (!rpg::CreateSaveGame(root, info, &error) || !LoadSavedGame(rpg::SaveDirectory(root, info.name))) {
        g_menuStatus = L"创建存档失败";
    }
}

void HandleMenuClick(HWND hwnd, int x, int y) {
    RECT client{};
    GetClientRect(hwnd, &client);
    g_menuStatus.clear();
    if (g_screen == AppScreen::MainMenu) {
        for (int i = 0; i < 4; ++i) {
            if (!PointInRect(MenuButtonRect(client, i, 4), x, y)) continue;
            if (i == 0) g_screen = AppScreen::NewGame;
            if (i == 1) { g_saveList = rpg::ListSaveGames(rpg::DefaultSavesRoot()); g_screen = AppScreen::LoadGame; }
            if (i == 2) g_screen = AppScreen::Settings;
            if (i == 3) DestroyWindow(hwnd);
            return;
        }
    } else if (g_screen == AppScreen::NewGame) {
        if (PointInRect(MenuButtonRect(client, 0, 4), x, y)) {
            g_newDifficulty = g_newDifficulty == rpg::GameDifficulty::Easy ? rpg::GameDifficulty::Normal :
                (g_newDifficulty == rpg::GameDifficulty::Normal ? rpg::GameDifficulty::Hard : rpg::GameDifficulty::Easy);
        } else if (PointInRect(MenuButtonRect(client, 1, 4), x, y)) {
            g_newResourceMultiplier = g_newResourceMultiplier >= 2.0f ? 0.5f : g_newResourceMultiplier + 0.5f;
        } else if (PointInRect(MenuButtonRect(client, 2, 4), x, y)) BeginNewGame();
        else if (PointInRect(MenuButtonRect(client, 3, 4), x, y)) g_screen = AppScreen::MainMenu;
    } else if (g_screen == AppScreen::LoadGame) {
        const int count = std::min(6, static_cast<int>(g_saveList.size())) + 1;
        for (int i = 0; i < count; ++i) {
            if (!PointInRect(MenuButtonRect(client, i, count), x, y)) continue;
            if (i == count - 1) g_screen = AppScreen::MainMenu;
            else LoadSavedGame(rpg::SaveDirectory(rpg::DefaultSavesRoot(), g_saveList[i].name));
            return;
        }
    } else if (g_screen == AppScreen::Settings) {
        if (PointInRect(MenuButtonRect(client, 0, 2), x, y)) {
            g_defaultZoom += kZoomStep;
            if (g_defaultZoom > kMaximumZoom) g_defaultZoom = kMinimumZoom;
        } else if (PointInRect(MenuButtonRect(client, 1, 2), x, y)) g_screen = AppScreen::MainMenu;
    }
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
    rpg::DrawTerrain(
        hdc,
        g_game.scene,
        g_game.camera.x,
        g_game.camera.y,
        false,
        renderScale,
        client.right - client.left,
        client.bottom - client.top);
    rpg::DrawTerritoryBoundary(
        hdc,
        g_game.scene,
        g_game.camera.x,
        g_game.camera.y,
        renderScale,
        client.right - client.left,
        client.bottom - client.top);
    DrawTerritoryPreview(hdc);

    std::vector<const rpg::SceneObject*> objects;
    objects.reserve(g_game.scene.objects.size());
    for (const rpg::SceneObject& object : g_game.scene.objects) {
        const rpg::RectF bounds = rpg::ObjectVisualBounds(object);
        constexpr float margin = 64.0f;
        if (bounds.right * renderScale - g_game.camera.x < -margin ||
            bounds.left * renderScale - g_game.camera.x > client.right + margin ||
            bounds.bottom * renderScale - g_game.camera.y < -margin ||
            bounds.top * renderScale - g_game.camera.y > client.bottom + margin) {
            continue;
        }
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

    for (const Monster& monster : g_game.monsters) {
        if (!monster.alive) continue;
        DrawEllipse(hdc, monster.pos, 17.0f, 17.0f, RGB(69, 57, 77), RGB(25, 19, 29));
        const int barX = static_cast<int>(std::round(monster.pos.x * renderScale - g_game.camera.x - 20.0f * renderScale));
        const int barY = static_cast<int>(std::round(monster.pos.y * renderScale - g_game.camera.y - 30.0f * renderScale));
        RECT healthBack{barX, barY, barX + std::max(1, static_cast<int>(40.0f * renderScale)), barY + 5};
        FillRectColor(hdc, healthBack, RGB(37, 28, 40));
        RECT healthFill = healthBack;
        healthFill.right = healthFill.left + static_cast<int>((healthBack.right - healthBack.left) *
            static_cast<float>(monster.health) / monster.maxHealth);
        FillRectColor(hdc, healthFill, RGB(151, 63, 76));
    }

    for (const Npc& npc : g_game.npcs) {
        DrawEllipse(hdc, npc.pos, 15.0f, 19.0f, RGB(222, 185, 94), RGB(76, 55, 32));
        DrawTextLine(
            hdc,
            npc.name,
            static_cast<int>(std::round(npc.pos.x * renderScale - g_game.camera.x - 18.0f * renderScale)),
            static_cast<int>(std::round(npc.pos.y * renderScale - g_game.camera.y - 38.0f * renderScale)),
            RGB(245, 244, 230));
        if (npc.following) {
            DrawTextLine(
                hdc,
                npc.inCombat ? L"战斗" : L"跟随",
                static_cast<int>(std::round(npc.pos.x * renderScale - g_game.camera.x - 16.0f * renderScale)),
                static_cast<int>(std::round(npc.pos.y * renderScale - g_game.camera.y + 22.0f * renderScale)),
                npc.inCombat ? RGB(244, 126, 126) : RGB(177, 228, 190));
        }
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

        const Npc& npc = g_game.npcs[g_game.nearbyNpc];
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

    if (g_game.npcContextIndex >= 0 && g_game.npcContextIndex < static_cast<int>(g_game.npcs.size())) {
        const RECT button = NpcContextButtonRect();
        FillRectColor(hdc, button, RGB(45, 51, 48));
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(151, 162, 151));
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, button.left, button.top, button.right, button.bottom);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
        DrawTextLine(
            hdc,
            g_game.npcs[g_game.npcContextIndex].following ? L"取消跟随" : L"跟随",
            button.left + 18,
            button.top + 9,
            RGB(235, 239, 226));
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
    if (g_game.inventory.open) {
        g_game.inventory.placingAnchor = false;
        g_game.inventory.selectingTerritory = false;
        g_game.player.attacking = false;
        g_game.player.attackTime = 0.0f;
    }
}

void StartAttack() {
    if (g_game.inventory.heldItem.id != kCombatStoneId) {
        g_game.pickupNotice = L"需要启用战斗石";
        g_game.pickupNoticeTime = 1.2f;
        return;
    }
    if (g_game.inventory.open || g_game.player.attacking) {
        return;
    }
    g_game.player.attacking = true;
    g_game.player.attackTime = 0.0f;
    g_game.player.attackDir = g_game.player.dir;
    int target = -1;
    float nearest = 78.0f;
    for (int i = 0; i < static_cast<int>(g_game.monsters.size()); ++i) {
        if (!g_game.monsters[i].alive) continue;
        const float distance = Distance(g_game.player.pos, g_game.monsters[i].pos);
        if (distance < nearest) {
            nearest = distance;
            target = i;
        }
    }
    if (target >= 0) {
        Monster& monster = g_game.monsters[target];
        monster.health -= 20;
        if (monster.health <= 0) {
            monster.health = 0;
            monster.alive = false;
            g_game.activeCombatMonster = -1;
            for (Npc& npc : g_game.npcs) npc.inCombat = false;
        } else {
            EnterFollowerCombat(target);
        }
    }
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
        SetTimer(hwnd, kFrameTimer, kFrameMs, nullptr);
        return 0;
    case WM_TIMER: {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const float dt = static_cast<float>(now.QuadPart - g_game.lastTick.QuadPart) / static_cast<float>(g_game.freq.QuadPart);
        g_game.lastTick = now;
        if (g_screen == AppScreen::Playing) {
            UpdateGame(std::min(dt, 0.05f));
            RECT client{};
            GetClientRect(hwnd, &client);
            UpdateSpiritMenu(client, std::min(dt, 0.05f));
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN:
        if (g_screen != AppScreen::Playing) {
            if (wParam == VK_ESCAPE) {
                g_screen = AppScreen::MainMenu;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            if (g_game.npcContextIndex >= 0) {
                g_game.npcContextIndex = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            SaveCurrentGame();
            g_screen = AppScreen::MainMenu;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
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
        if (wParam == VK_SPACE) {
            if ((lParam & (1LL << 30)) == 0) {
                StartAttack();
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
        if (g_screen != AppScreen::Playing) {
            HandleMenuClick(hwnd, x, y);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (g_game.npcContextIndex >= 0) {
            const RECT contextButton = NpcContextButtonRect();
            if (PtInRect(&contextButton, POINT{x, y})) {
                Npc& npc = g_game.npcs[g_game.npcContextIndex];
                if (npc.following) {
                    npc.following = false;
                    npc.inCombat = false;
                    npc.moving = false;
                    g_game.pickupNotice = npc.name + L" 已取消跟随";
                } else if (FollowerCount() >= kMaximumFollowers) {
                    g_game.pickupNotice = L"最多只能有 4 名跟随者";
                } else {
                    npc.following = true;
                    npc.moving = Distance(npc.pos, g_game.player.pos) > kNpcFollowStartDistance;
                    g_game.pickupNotice = npc.name + L" 已开始跟随";
                }
                g_game.pickupNoticeTime = 1.5f;
                g_game.npcContextIndex = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            g_game.npcContextIndex = -1;
        }
        if (const int spirit = HitSpiritMenu(client, x, y); spirit >= 0) {
            SelectSpiritStone(spirit);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        const int hit = HitInventorySlot(client, x, y);
        if (hit >= 0) {
            g_game.inventory.focusedSlot = hit;
            if (hit < kHotbarSlotCount) {
                g_game.inventory.selectedHotbar = hit;
            }
            ItemStack* item = InventoryItemAt(hit);
            if (item && item->id == "territory_anchor" && g_game.inventory.heldItem.id == kBuildingStoneId) {
                g_game.inventory.placingAnchor = true;
                g_game.inventory.open = false;
                g_game.pickupNotice = L"锚点放置：点击一个地格";
                g_game.pickupNoticeTime = 2.0f;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
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
        if (g_game.inventory.heldItem.id == kBuildingStoneId) {
            const POINT tile = ScreenToTile(x, y);
            if (g_game.inventory.placingAnchor) {
                PlaceTerritoryAnchor(tile.x, tile.y);
            } else if (HasTerritoryAnchor()) {
                ExpandTerritoryRect(tile, tile);
            } else {
                g_game.pickupNotice = L"请先从背包放置现实锚点";
                g_game.pickupNoticeTime = 1.5f;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        const int pickupIndex = HitWorldPickup(x, y);
        if (pickupIndex >= 0) {
            WorldPickup& pickup = CurrentPickups()[pickupIndex];
            pickup.activated = true;
            g_game.pickupNotice = pickup.item.displayName;
            g_game.pickupNoticeTime = 1.5f;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        StartAttack();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE:
        g_game.inventory.mousePoint = POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tracking);
        }
        if (g_game.inventory.dragging && (wParam & MK_LBUTTON)) {
            g_game.inventory.dragPoint = POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        if (g_game.inventory.selectingTerritory && (wParam & MK_RBUTTON)) {
            g_game.inventory.territoryEndTile = ScreenToTile(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_MOUSELEAVE:
        g_game.inventory.mousePoint = POINT{-10000, -10000};
        return 0;
    case WM_RBUTTONDOWN:
        if (g_screen == AppScreen::Playing && !g_game.inventory.open) {
            const int npcIndex = HitNpc(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (npcIndex >= 0) {
                RECT client{};
                GetClientRect(hwnd, &client);
                g_game.npcContextIndex = npcIndex;
                g_game.npcContextPoint = {
                    std::clamp<int>(GET_X_LPARAM(lParam) + 8, 8, std::max<int>(8, client.right - 124)),
                    std::clamp<int>(GET_Y_LPARAM(lParam) + 8, 8, std::max<int>(8, client.bottom - 44)),
                };
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            g_game.npcContextIndex = -1;
        }
        if (g_screen == AppScreen::Playing && !g_game.inventory.open &&
            g_game.inventory.heldItem.id == kBuildingStoneId && HasTerritoryAnchor()) {
            g_game.inventory.selectingTerritory = true;
            g_game.inventory.territoryStartTile = ScreenToTile(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            g_game.inventory.territoryEndTile = g_game.inventory.territoryStartTile;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_RBUTTONUP:
        if (g_game.inventory.selectingTerritory) {
            g_game.inventory.territoryEndTile = ScreenToTile(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            ExpandTerritoryRect(g_game.inventory.territoryStartTile, g_game.inventory.territoryEndTile);
            g_game.inventory.selectingTerritory = false;
            ReleaseCapture();
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
        if (g_screen == AppScreen::Playing) {
            RenderGame(hwnd, hdc);
        } else {
            RenderMenu(hwnd, hdc);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        SaveCurrentGame();
        KillTimer(hwnd, kFrameTimer);
        ReleaseBackgroundResources();
        rpg::ReleaseSceneRenderResources();
        rpg::ReleaseTerrainRenderResources();
        rpg::ReleaseTerritoryRenderResources();
        for (SpriteSheet& sprite : g_playerIdleSprites) {
            sprite.bitmap.reset();
            sprite.loaded = false;
        }
        for (SpriteSheet* sprite : {
                 &g_playerSprite,
                 &g_playerRunFront,
                 &g_playerRunBack,
                 &g_playerAttackFront,
                 &g_playerAttackSide,
                 &g_playerAttackBack}) {
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
    (void)commandLine;
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
