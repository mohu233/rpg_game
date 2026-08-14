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
#include <cwctype>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int kWindowWidth = 960;
constexpr int kWindowHeight = 640;
constexpr int kTileSize = rpg::kTileSize;
constexpr float kPlayerRadius = 16.0f;
constexpr float kNpcRadius = 15.0f;
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
constexpr int kAnchorStorageSlotCount = 100;
constexpr int kAnchorStorageSlotBase = 1000;
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
    std::array<bool, 3> spiritStones{};
    bool inCombat = false;
    bool moving = false;
    float attackCooldown = 0.0f;
    bool returningHome = false;
    bool shadowForm = false;
    Vec2 wanderTarget{};
    float wanderTimer = 0.0f;
    int health = 60;
    int affinity = 50;
    std::wstring personality = L"谨慎";
};

std::vector<Npc> MakeDefaultNpcs() {
    return {
        {{360.0f, 170.0f}, {}, L"莉娜", L"活人的脚步会在囚地留下痕迹。"},
        {{780.0f, 430.0f}, {}, L"诺亚", L"结界之外，影子无法触碰现实。"},
        {{1040.0f, 250.0f}, {}, L"米拉", L"灵石能短暂赋予影子实体。"},
        {{520.0f, 640.0f}, {}, L"塞恩", L"锚点稳定时，我能感受到现实的重量。"},
    };
}

enum class WandererState { Waiting, Accepted, Expelled };

struct WanderingShadow {
    Vec2 pos;
    Vec2 origin;
    std::wstring name;
    WandererState state = WandererState::Waiting;
    float age = 0.0f;
    float fadeTime = 12.0f;
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
    bool placingBuilding = false;
    int placementSourceSlot = -1;
    std::string placementObjectType;
    POINT placementTile{};
    bool placementValid = false;
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
    std::array<ItemStack, kAnchorStorageSlotCount> anchorStorage;
    bool anchorPanelOpen = false;
    int anchorSelectedNpc = -1;
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
    std::vector<Npc> npcs = MakeDefaultNpcs();
    std::vector<WanderingShadow> wanderingShadows;
    int shadowDialogIndex = -1;
    int nextShadowNumber = 1;
    std::vector<Monster> monsters;
    std::wstring monsterSceneKey;
    int npcContextIndex = -1;
    POINT npcContextPoint{};
    int activeCombatMonster = -1;
    int playerHealth = 100;
    float teleportCooldown = 0.0f;
    bool mapOpen = false;
    float mapZoom = 1.0f;
    Vec2 mapPan{};
    bool mapDragging = false;
    POINT mapDragPoint{};
    std::map<std::wstring, std::vector<std::uint8_t>> exploredTilesByScene;
    LARGE_INTEGER lastTick{};
    LARGE_INTEGER freq{};
};

struct DebugConsole {
    bool open = false;
    std::wstring input;
    std::vector<std::wstring> lines;
    bool showCollisionLines = false;
};

enum class AppScreen {
    MainMenu,
    NewGame,
    LoadGame,
    Settings,
    Playing,
};

Game g_game;
DebugConsole g_console;
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

// Placement helpers are used by rendering and input code before their implementations.
rpg::Vec2 BuildingAnchorPosition(const std::string& objectType, int tx, int ty);
ItemStack* InventoryItemAt(int index);

struct ItemBitmapCache {
    std::string id;
    std::unique_ptr<Gdiplus::Bitmap> icon;
    std::unique_ptr<Gdiplus::Bitmap> world;
    bool iconAttempted = false;
    bool worldAttempted = false;
};

std::vector<ItemBitmapCache> g_itemBitmaps;
std::unique_ptr<Gdiplus::Bitmap> g_anchorButtonIcon;
bool g_anchorButtonIconAttempted = false;

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

bool CollidesWithNpcs(Vec2 pos, float radius, int ignoredNpcIndex = -1) {
    for (int i = 0; i < static_cast<int>(g_game.npcs.size()); ++i) {
        if (i == ignoredNpcIndex || g_game.npcs[i].shadowForm) continue;
        const float dx = pos.x - g_game.npcs[i].pos.x;
        const float dy = pos.y - g_game.npcs[i].pos.y;
        const float combinedRadius = radius + kNpcRadius;
        if (dx * dx + dy * dy < combinedRadius * combinedRadius) return true;
    }
    return false;
}

void TryMove(Player& player, Vec2 delta) {
    const auto blockedByFollower = [](Vec2 pos) {
        return std::any_of(g_game.npcs.begin(), g_game.npcs.end(), [&](const Npc& npc) {
            if (!npc.following || npc.shadowForm) return false;
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
    if (!CollidesWithMap(next, kPlayerRadius) &&
        !CollidesWithNpcs(next, kPlayerRadius) && !blockedByFollower(next)) {
        player.pos.x = next.x;
    }

    next = {player.pos.x, player.pos.y + delta.y};
    if (!CollidesWithMap(next, kPlayerRadius) &&
        !CollidesWithNpcs(next, kPlayerRadius) && !blockedByFollower(next)) {
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
            for (const Npc& npc : g_game.npcs) alreadyOwned = alreadyOwned || npc.spiritStones[i];
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

void AppendConsoleLine(std::wstring line) {
    constexpr size_t kMaximumLines = 8;
    g_console.lines.push_back(std::move(line));
    if (g_console.lines.size() > kMaximumLines) {
        g_console.lines.erase(g_console.lines.begin(), g_console.lines.begin() + (g_console.lines.size() - kMaximumLines));
    }
}

bool SpawnWanderingShadow();

std::wstring Lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });
    return value;
}

const rpg::ItemDef* FindConsoleItem(const std::wstring& name) {
    const std::wstring wanted = Lowercase(name);
    for (const rpg::ItemDef& def : rpg::ItemDefs()) {
        const std::wstring id(def.id.begin(), def.id.end());
        if (Lowercase(id) == wanted || Lowercase(def.displayName) == wanted) {
            return &def;
        }
    }
    return nullptr;
}

void ExecuteConsoleCommand() {
    std::wstring command = g_console.input;
    g_console.input.clear();
    while (!command.empty() && std::iswspace(command.front())) command.erase(command.begin());
    while (!command.empty() && std::iswspace(command.back())) command.pop_back();
    if (command.empty()) return;

    AppendConsoleLine(L"> " + command);
    if (Lowercase(command) == L"help") {
        AppendConsoleLine(L"add <item id/name> <quantity>");
        AppendConsoleLine(L"shadow - spawn a wandering shadow near territory");
        AppendConsoleLine(L"debug - toggle collision outlines");
        AppendConsoleLine(L"clear - clear console output");
        return;
    }
    if (Lowercase(command) == L"clear") {
        g_console.lines.clear();
        return;
    }

    if (Lowercase(command) == L"shadow") {
        AppendConsoleLine(SpawnWanderingShadow()
            ? L"A wandering shadow appeared near the territory."
            : L"No available territory boundary was found.");
        return;
    }
    if (Lowercase(command) == L"debug") {
        g_console.showCollisionLines = !g_console.showCollisionLines;
        AppendConsoleLine(g_console.showCollisionLines
            ? L"Collision outlines enabled."
            : L"Collision outlines disabled.");
        return;
    }

    std::wistringstream commandStream(command);
    std::wstring verb;
    commandStream >> verb;
    if (Lowercase(verb) != L"add") {
        AppendConsoleLine(L"Unknown command. Type help.");
        return;
    }

    std::wstring arguments;
    std::getline(commandStream, arguments);
    while (!arguments.empty() && std::iswspace(arguments.front())) arguments.erase(arguments.begin());
    const size_t quantitySeparator = arguments.find_last_of(L" \t");
    if (quantitySeparator == std::wstring::npos) {
        AppendConsoleLine(L"Usage: add <item id/name> <quantity>");
        return;
    }
    std::wstring itemName = arguments.substr(0, quantitySeparator);
    std::wstring quantityText = arguments.substr(quantitySeparator + 1);
    while (!itemName.empty() && std::iswspace(itemName.back())) itemName.pop_back();
    while (!quantityText.empty() && std::iswspace(quantityText.front())) quantityText.erase(quantityText.begin());

    wchar_t* end = nullptr;
    const long long parsed = std::wcstoll(quantityText.c_str(), &end, 10);
    if (quantityText.empty() || !end || *end != L'\0' || parsed <= 0 || parsed > 999999) {
        AppendConsoleLine(L"Quantity must be between 1 and 999999.");
        return;
    }
    const rpg::ItemDef* def = FindConsoleItem(itemName);
    if (!def) {
        AppendConsoleLine(L"Item not found: " + itemName);
        return;
    }

    const int requested = static_cast<int>(parsed);
    ItemStack item = MakeItemStack(def->id, requested);
    StoreItem(item);
    const int stored = requested - item.count;
    if (stored == 0) {
        AppendConsoleLine(L"Inventory is full.");
    } else {
        AppendConsoleLine(L"Added " + def->displayName + L" x" + std::to_wstring(stored) +
                          (item.count > 0 ? L" (inventory full)" : L""));
    }
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
    g_activeSave.anchorStorage.clear();
    for (int i = 0; i < static_cast<int>(g_game.anchorStorage.size()); ++i) {
        const ItemStack& item = g_game.anchorStorage[i];
        if (!item.id.empty() && item.count > 0) {
            g_activeSave.anchorStorage.push_back({i, item.id, item.count});
        }
    }
    g_activeSave.heldItemId = g_game.inventory.heldItem.id;
    g_activeSave.heldItemCount = g_game.inventory.heldItem.count;
    g_activeSave.selectedHotbar = g_game.inventory.selectedHotbar;
    g_activeSave.followerNpcIndices.clear();
    g_activeSave.residents.clear();
    for (int i = 0; i < static_cast<int>(g_game.npcs.size()); ++i) {
        const Npc& npc = g_game.npcs[i];
        if (npc.following) g_activeSave.followerNpcIndices.push_back(i);
        int stoneMask = 0;
        for (int stone = 0; stone < 3; ++stone) if (npc.spiritStones[stone]) stoneMask |= 1 << stone;
        g_activeSave.residents.push_back({npc.name, npc.pos.x, npc.pos.y, stoneMask, npc.following, npc.health,
                                          npc.affinity, npc.personality});
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
    g_game.anchorStorage = {};
    g_game.npcs = MakeDefaultNpcs();
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
    if (!g_activeSave.residents.empty()) {
        g_game.npcs.clear();
        for (const rpg::SaveGameInfo::ResidentEntry& saved : g_activeSave.residents) {
            Npc npc;
            npc.pos = {saved.x, saved.y};
            npc.name = saved.name;
            npc.text = L"领地让影子拥有了可以触碰的现实。";
            npc.following = saved.following;
            npc.health = saved.health;
            npc.affinity = saved.affinity;
            npc.personality = saved.personality;
            for (int stone = 0; stone < 3; ++stone) npc.spiritStones[stone] = (saved.spiritStoneMask & (1 << stone)) != 0;
            g_game.npcs.push_back(std::move(npc));
        }
    }
    for (const rpg::SaveGameInfo::InventoryEntry& entry : g_activeSave.anchorStorage) {
        if (entry.slot >= 0 && entry.slot < kAnchorStorageSlotCount) {
            g_game.anchorStorage[entry.slot] = MakeItemStack(entry.itemId, entry.count);
        }
    }
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
    g_game.wanderingShadows.clear();
    g_game.shadowDialogIndex = -1;
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
    g_game.wanderingShadows.clear();
    g_game.shadowDialogIndex = -1;
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
        npc.pos = CollidesWithMap(candidate, kNpcRadius) ||
                  CollidesWithNpcs(candidate, kNpcRadius, static_cast<int>(&npc - g_game.npcs.data()))
            ? g_game.player.pos : candidate;
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

bool SpawnWanderingShadow() {
    bool found = false;
    Vec2 best{};
    float bestDistance = std::numeric_limits<float>::max();
    constexpr int offsets[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    for (int ty = 0; ty < g_game.scene.mapHeight; ++ty) {
        for (int tx = 0; tx < g_game.scene.mapWidth; ++tx) {
            if (!rpg::TerritoryAt(g_game.scene, tx, ty)) continue;
            for (const auto& offset : offsets) {
                const int x = tx + offset[0];
                const int y = ty + offset[1];
                if (x < 0 || y < 0 || x >= g_game.scene.mapWidth || y >= g_game.scene.mapHeight ||
                    rpg::TerritoryAt(g_game.scene, x, y)) continue;
                const Vec2 candidate{(x + 0.5f) * kTileSize, (y + 0.5f) * kTileSize};
                if (CollidesWithMap(candidate, 14.0f)) continue;
                const float distance = Distance(candidate, g_game.player.pos);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = candidate;
                    found = true;
                }
            }
        }
    }
    g_game.nextShadowNumber = static_cast<int>(g_game.npcs.size()) + 1;
    if (!found) return false;
    static constexpr const wchar_t* names[] = {
        L"阿澜", L"白榆", L"迟月", L"冬青", L"归禾", L"槐安", L"见微", L"临川",
        L"青栀", L"时雨", L"闻溪", L"星回", L"遥岑", L"知夏", L"昭宁", L"子夜",
    };
    const int number = g_game.nextShadowNumber++;
    std::wstring name = names[(number - 1) % std::size(names)];
    const int cycle = (number - 1) / static_cast<int>(std::size(names));
    if (cycle > 0) name += L"·" + std::to_wstring(cycle + 1);
    g_game.wanderingShadows.push_back({best, best, name});
    g_game.pickupNotice = L"领地外出现了一个流浪影子";
    g_game.pickupNoticeTime = 2.0f;
    return true;
}

Vec2 NearestTerritoryPosition(Vec2 pos) {
    Vec2 best = pos;
    float bestDistance = std::numeric_limits<float>::max();
    for (int ty = 0; ty < g_game.scene.mapHeight; ++ty) {
        for (int tx = 0; tx < g_game.scene.mapWidth; ++tx) {
            if (!rpg::TerritoryAt(g_game.scene, tx, ty)) continue;
            const Vec2 candidate{(tx + 0.5f) * kTileSize, (ty + 0.5f) * kTileSize};
            const float distance = Distance(candidate, pos);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = candidate;
            }
        }
    }
    return best;
}

void UpdateWanderingShadows(float dt) {
    for (size_t i = 0; i < g_game.wanderingShadows.size();) {
        WanderingShadow& shadow = g_game.wanderingShadows[i];
        shadow.age += dt;
        if (shadow.state == WandererState::Accepted && ActorProtectedByTerritory(shadow.pos)) {
            const std::wstring name = shadow.name;
            g_game.npcs.push_back({shadow.pos, {}, name, L"这里有了真实的温度。"});
            g_game.pickupNotice = name + L" 已进入领地并化为实体";
            g_game.pickupNoticeTime = 2.0f;
            SaveCurrentGame();
            if (g_game.shadowDialogIndex == static_cast<int>(i)) g_game.shadowDialogIndex = -1;
            g_game.wanderingShadows.erase(g_game.wanderingShadows.begin() + i);
            continue;
        }
        if (shadow.state == WandererState::Expelled && shadow.age >= shadow.fadeTime) {
            if (g_game.shadowDialogIndex == static_cast<int>(i)) g_game.shadowDialogIndex = -1;
            g_game.wanderingShadows.erase(g_game.wanderingShadows.begin() + i);
            continue;
        }

        Vec2 next = shadow.pos;
        if (shadow.state == WandererState::Accepted) {
            const Vec2 direction = Normalize({NearestTerritoryPosition(shadow.pos).x - shadow.pos.x,
                                               NearestTerritoryPosition(shadow.pos).y - shadow.pos.y});
            next.x += direction.x * 58.0f * dt;
            next.y += direction.y * 58.0f * dt;
        } else {
            const float speed = shadow.state == WandererState::Expelled ? 18.0f : 9.0f;
            next.x += std::cos(shadow.age * 0.73f + static_cast<float>(i)) * speed * dt;
            next.y += std::sin(shadow.age * 0.51f + static_cast<float>(i)) * speed * dt;
            if (shadow.state == WandererState::Waiting && Distance(next, shadow.origin) > kTileSize * 1.5f) {
                const Vec2 home = Normalize({shadow.origin.x - next.x, shadow.origin.y - next.y});
                next.x += home.x * speed * dt;
                next.y += home.y * speed * dt;
            }
        }
        if (!CollidesWithMap(next, 14.0f)) shadow.pos = next;
        ++i;
    }
}

int HitWanderingShadow(int screenX, int screenY) {
    const float scale = RenderScale();
    for (int i = static_cast<int>(g_game.wanderingShadows.size()) - 1; i >= 0; --i) {
        const WanderingShadow& shadow = g_game.wanderingShadows[i];
        if (shadow.state != WandererState::Waiting) continue;
        const float dx = screenX - (shadow.pos.x * scale - g_game.camera.x);
        const float dy = screenY - (shadow.pos.y * scale - g_game.camera.y);
        const float radius = std::max(18.0f, 24.0f * scale);
        if (dx * dx + dy * dy <= radius * radius) return i;
    }
    return -1;
}

RECT ShadowDialogRect(const RECT& client) {
    const int width = std::min(520, std::max(360, static_cast<int>(client.right) - 80));
    const int left = (static_cast<int>(client.right) - width) / 2;
    return {left, std::max(30, static_cast<int>(client.bottom) - 220), left + width,
            std::max(30, static_cast<int>(client.bottom) - 220) + 150};
}

RECT ShadowAcceptButton(const RECT& dialog) { return {dialog.left + 28, dialog.bottom - 52, dialog.left + 148, dialog.bottom - 16}; }
RECT ShadowExpelButton(const RECT& dialog) { return {dialog.right - 148, dialog.bottom - 52, dialog.right - 28, dialog.bottom - 16}; }

bool MonsterPositionAvailable(Vec2 pos) {
    constexpr float monsterRadius = 16.0f;
    return !CollidesWithMap(pos, monsterRadius) && !CircleIntersectsTerritory(pos, monsterRadius);
}

void KnockbackMonster(Monster& monster, Vec2 attacker, float distance) {
    Vec2 direction{monster.pos.x - attacker.x, monster.pos.y - attacker.y};
    const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    if (length < 0.001f) {
        direction = {1.0f, 0.0f};
    } else {
        direction.x /= length;
        direction.y /= length;
    }

    constexpr int kSteps = 8;
    const Vec2 origin = monster.pos;
    for (int step = 1; step <= kSteps; ++step) {
        const float progress = static_cast<float>(step) / static_cast<float>(kSteps);
        const Vec2 candidate{origin.x + direction.x * distance * progress,
                             origin.y + direction.y * distance * progress};
        if (!MonsterPositionAvailable(candidate)) break;
        monster.pos = candidate;
    }
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

bool NpcHasAnyStone(const Npc& npc) {
    return std::any_of(npc.spiritStones.begin(), npc.spiritStones.end(), [](bool equipped) { return equipped; });
}

bool TakeSpiritStoneFromPlayer(int stoneIndex) {
    static constexpr const char* ids[] = {"gathering_stone", "building_stone", "combat_stone"};
    for (ItemStack& item : g_game.inventory.slots) {
        if (item.id != ids[stoneIndex] || item.count <= 0) continue;
        if (--item.count == 0) item = {};
        return true;
    }
    ItemStack& equipped = g_game.inventory.spiritSlots[stoneIndex];
    if (equipped.id != ids[stoneIndex]) return false;
    if (g_game.inventory.heldItem.id == equipped.id) g_game.inventory.heldItem = {};
    equipped = {};
    return true;
}

bool ReturnSpiritStoneToPlayer(int stoneIndex) {
    static constexpr const char* ids[] = {"gathering_stone", "building_stone", "combat_stone"};
    ItemStack stone = MakeItemStack(ids[stoneIndex], 1);
    return StoreItem(stone);
}

RECT NpcContextRect() {
    return {
        g_game.npcContextPoint.x,
        g_game.npcContextPoint.y,
        g_game.npcContextPoint.x + 176,
        g_game.npcContextPoint.y + 156,
    };
}

RECT NpcContextButtonRect(int row) {
    const RECT panel = NpcContextRect();
    return {panel.left + 6, panel.top + 6 + row * 36, panel.right - 6, panel.top + 36 + row * 36};
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
    if (CollidesWithMap(pos, kNpcRadius)) return false;
    if (!g_game.npcs[npcIndex].shadowForm &&
        Distance(pos, g_game.player.pos) < kNpcRadius + kPlayerRadius) return false;
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
        if (npc.following && npc.spiritStones[2] && !npc.shadowForm) npc.inCombat = true;
    }
}

void UpdateMonsters(float dt) {
    EnsureSceneMonsters();
    for (int i = 0; i < static_cast<int>(g_game.monsters.size()); ++i) {
        Monster& monster = g_game.monsters[i];
        if (!monster.alive) continue;
        if (!MoveMonsterOutsideTerritory(monster)) continue;
        monster.attackCooldown = std::max(0.0f, monster.attackCooldown - dt);
        int targetNpc = -1;
        Vec2 target = g_game.player.pos;
        float targetDistance = ActorProtectedByTerritory(g_game.player.pos)
            ? std::numeric_limits<float>::max()
            : Distance(monster.pos, g_game.player.pos);
        for (int npcIndex = 0; npcIndex < static_cast<int>(g_game.npcs.size()); ++npcIndex) {
            const Npc& npc = g_game.npcs[npcIndex];
            if (ActorProtectedByTerritory(npc.pos) || npc.shadowForm || !NpcHasAnyStone(npc)) continue;
            const float distance = Distance(monster.pos, npc.pos);
            if (distance < targetDistance) {
                targetDistance = distance;
                target = npc.pos;
                targetNpc = npcIndex;
            }
        }
        if (targetDistance < 300.0f && targetDistance > 46.0f) {
            const Vec2 direction = Normalize({target.x - monster.pos.x, target.y - monster.pos.y});
            const Vec2 next{monster.pos.x + direction.x * 72.0f * dt, monster.pos.y + direction.y * 72.0f * dt};
            if (MonsterPositionAvailable(next)) monster.pos = next;
        } else if (targetDistance <= 46.0f && monster.attackCooldown <= 0.0f) {
            monster.attackCooldown = 1.1f;
            if (targetNpc >= 0) {
                Npc& npc = g_game.npcs[targetNpc];
                npc.health = std::max(0, npc.health - 8);
                g_game.pickupNotice = L"畸变暗影击中了 " + npc.name;
                if (npc.health == 0) {
                    g_game.pickupNotice = npc.name + L" 被畸变暗影吞噬了";
                    g_game.npcs.erase(g_game.npcs.begin() + targetNpc);
                    g_game.npcContextIndex = -1;
                    g_game.nearbyNpc = -1;
                    SaveCurrentGame();
                }
            } else {
                g_game.playerHealth = std::max(0, g_game.playerHealth - 8);
                EnterFollowerCombat(i);
                g_game.pickupNotice = L"畸变暗影击中了你";
            }
            g_game.pickupNoticeTime = 1.0f;
        }
    }
}

Vec2 NextTerritoryWanderTarget(const Npc& npc, int npcIndex) {
    const int total = std::max(1, g_game.scene.mapWidth * g_game.scene.mapHeight);
    const int current = static_cast<int>(npc.pos.y / kTileSize) * g_game.scene.mapWidth +
                        static_cast<int>(npc.pos.x / kTileSize);
    const int start = (current + 17 + npcIndex * 31) % total;
    for (int offset = 0; offset < total; ++offset) {
        const int index = (start + offset * 13) % total;
        const int tx = index % g_game.scene.mapWidth;
        const int ty = index / g_game.scene.mapWidth;
        if (!rpg::TerritoryAt(g_game.scene, tx, ty)) continue;
        const Vec2 candidate{(tx + 0.5f) * kTileSize, (ty + 0.5f) * kTileSize};
        if (!CollidesWithMap(candidate, kNpcRadius)) return candidate;
    }
    return npc.pos;
}

void UpdateNpcs(float dt) {
    int followerSlot = 0;
    for (int i = 0; i < static_cast<int>(g_game.npcs.size()); ++i) {
        Npc& npc = g_game.npcs[i];
        npc.attackCooldown = std::max(0.0f, npc.attackCooldown - dt);
        npc.velocity = {};
        const bool insideTerritory = ActorProtectedByTerritory(npc.pos);
        npc.shadowForm = !insideTerritory && !NpcHasAnyStone(npc);
        if (npc.shadowForm) npc.inCombat = false;

        if (!npc.following) {
            if (!insideTerritory) npc.returningHome = true;
            if (npc.returningHome) {
                MoveNpcToward(i, NearestTerritoryPosition(npc.pos), 82.0f, dt, false);
                if (ActorProtectedByTerritory(npc.pos)) {
                    npc.returningHome = false;
                    npc.shadowForm = false;
                    npc.wanderTimer = 0.0f;
                }
                continue;
            }

            npc.wanderTimer -= dt;
            if (npc.wanderTimer <= 0.0f || Distance(npc.pos, npc.wanderTarget) < 12.0f ||
                !ActorProtectedByTerritory(npc.wanderTarget)) {
                npc.wanderTarget = NextTerritoryWanderTarget(npc, i);
                npc.wanderTimer = 3.0f + static_cast<float>((i * 7) % 5);
            }
            const Vec2 previous = npc.pos;
            MoveNpcToward(i, npc.wanderTarget, 34.0f, dt, false);
            if (!ActorProtectedByTerritory(npc.pos)) npc.pos = previous;
            continue;
        }

        npc.returningHome = false;

        if (npc.inCombat && npc.spiritStones[2] && !npc.shadowForm) {
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
                    KnockbackMonster(monster, npc.pos, 42.0f);
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
    if (g_console.open) {
        g_game.player.vel = {};
        g_game.player.animTime = 0.0f;
        g_game.interact = false;
        return;
    }
    if (g_game.shadowDialogIndex >= 0) {
        g_game.player.vel = {};
        g_game.player.animTime = 0.0f;
        g_game.interact = false;
        return;
    }
    if (g_game.mapOpen) {
        g_game.player.vel = {};
        g_game.player.animTime = 0.0f;
        g_game.interact = false;
        return;
    }
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
    {
        const std::wstring sceneKey = g_currentScenePath.filename().wstring();
        std::vector<std::uint8_t>& explored = g_game.exploredTilesByScene[sceneKey];
        const int tileCount = g_game.scene.mapWidth * g_game.scene.mapHeight;
        if (static_cast<int>(explored.size()) != tileCount) explored.assign(tileCount, 0);
        const int centerX = std::clamp(static_cast<int>(g_game.player.pos.x / kTileSize), 0, g_game.scene.mapWidth - 1);
        const int centerY = std::clamp(static_cast<int>(g_game.player.pos.y / kTileSize), 0, g_game.scene.mapHeight - 1);
        constexpr int kRevealRadius = 3;
        for (int ty = std::max(0, centerY - kRevealRadius); ty <= std::min(g_game.scene.mapHeight - 1, centerY + kRevealRadius); ++ty) {
            for (int tx = std::max(0, centerX - kRevealRadius); tx <= std::min(g_game.scene.mapWidth - 1, centerX + kRevealRadius); ++tx) {
                const int dx = tx - centerX;
                const int dy = ty - centerY;
                if (dx * dx + dy * dy <= kRevealRadius * kRevealRadius) explored[ty * g_game.scene.mapWidth + tx] = 1;
            }
        }
    }
    UpdateMonsters(dt);
    UpdateNpcs(dt);
    UpdateWanderingShadows(dt);
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

void DrawWorldMap(HDC hdc, const RECT& client) {
    if (!g_game.mapOpen) return;

    const int availableWidth = std::max(280, static_cast<int>(client.right) - 100);
    const int availableHeight = std::max(220, static_cast<int>(client.bottom) - 120);
    const float aspect = static_cast<float>(g_game.scene.mapWidth) / std::max(1, g_game.scene.mapHeight);
    int mapWidth = availableWidth;
    int mapHeight = static_cast<int>(std::round(mapWidth / aspect));
    if (mapHeight > availableHeight) {
        mapHeight = availableHeight;
        mapWidth = static_cast<int>(std::round(mapHeight * aspect));
    }
    const RECT panel{(client.right - mapWidth) / 2 - 22, (client.bottom - mapHeight) / 2 - 42,
                     (client.right + mapWidth) / 2 + 22, (client.bottom + mapHeight) / 2 + 22};
    const RECT mapRect{panel.left + 22, panel.top + 42, panel.right - 22, panel.bottom - 22};
    FillRectColor(hdc, RECT{0, 0, client.right, client.bottom}, RGB(12, 16, 17));
    FillRectColor(hdc, panel, RGB(27, 34, 34));
    HPEN border = CreatePen(PS_SOLID, 2, RGB(137, 168, 145));
    HGDIOBJ oldPen = SelectObject(hdc, border);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, panel.left, panel.top, panel.right, panel.bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(border);
    DrawTextLine(hdc, L"地图", panel.left + 16, panel.top + 12, RGB(224, 235, 207));
    DrawTextLine(hdc, L"M 关闭", panel.right - 78, panel.top + 12, RGB(170, 189, 177));
    FillRectColor(hdc, mapRect, RGB(17, 22, 24));

    const std::wstring sceneKey = g_currentScenePath.filename().wstring();
    const auto exploredIt = g_game.exploredTilesByScene.find(sceneKey);
    const std::vector<std::uint8_t>* explored = exploredIt == g_game.exploredTilesByScene.end() ? nullptr : &exploredIt->second;
    const float baseScale = std::min(
        static_cast<float>(mapRect.right - mapRect.left) / g_game.scene.mapWidth,
        static_cast<float>(mapRect.bottom - mapRect.top) / g_game.scene.mapHeight);
    const float mapScale = baseScale * g_game.mapZoom;
    const float worldWidth = g_game.scene.mapWidth * mapScale;
    const float worldHeight = g_game.scene.mapHeight * mapScale;
    const float maximumPanX = std::max(0.0f, (worldWidth - (mapRect.right - mapRect.left)) * 0.5f);
    const float maximumPanY = std::max(0.0f, (worldHeight - (mapRect.bottom - mapRect.top)) * 0.5f);
    g_game.mapPan.x = Clamp(g_game.mapPan.x, -maximumPanX, maximumPanX);
    g_game.mapPan.y = Clamp(g_game.mapPan.y, -maximumPanY, maximumPanY);
    const float originX = (mapRect.left + mapRect.right - worldWidth) * 0.5f + g_game.mapPan.x;
    const float originY = (mapRect.top + mapRect.bottom - worldHeight) * 0.5f + g_game.mapPan.y;
    const auto revealed = [explored](int index) { return explored && index >= 0 && index < static_cast<int>(explored->size()) && (*explored)[index] != 0; };

    SaveDC(hdc);
    IntersectClipRect(hdc, mapRect.left, mapRect.top, mapRect.right, mapRect.bottom);

    for (int ty = 0; ty < g_game.scene.mapHeight; ++ty) {
        for (int tx = 0; tx < g_game.scene.mapWidth; ++tx) {
            if (!revealed(ty * g_game.scene.mapWidth + tx)) continue;
            RECT tile{
                static_cast<LONG>(std::floor(originX + tx * mapScale)),
                static_cast<LONG>(std::floor(originY + ty * mapScale)),
                static_cast<LONG>(std::ceil(originX + (tx + 1) * mapScale)),
                static_cast<LONG>(std::ceil(originY + (ty + 1) * mapScale)),
            };
            FillRectColor(hdc, tile, rpg::TerritoryAt(g_game.scene, tx, ty) ? RGB(70, 116, 87) : RGB(54, 82, 65));
        }
    }

    for (const rpg::SceneObject& object : g_game.scene.objects) {
        const int tx = std::clamp(static_cast<int>(object.pos.x / kTileSize), 0, g_game.scene.mapWidth - 1);
        const int ty = std::clamp(static_cast<int>(object.pos.y / kTileSize), 0, g_game.scene.mapHeight - 1);
        if (!revealed(ty * g_game.scene.mapWidth + tx)) continue;
        const int x = static_cast<int>(std::round(originX + (tx + 0.5f) * mapScale));
        const int y = static_cast<int>(std::round(originY + (ty + 0.5f) * mapScale));
        const rpg::SceneObjectDef* def = rpg::FindObjectDef(object.type);
        COLORREF color = RGB(200, 174, 98);
        int radius = 3;
        if (rpg::ObjectIsTeleport(object)) color = RGB(91, 207, 224);
        else if (def && def->building) { color = RGB(204, 144, 79); radius = 5; }
        else if (def && def->visual == rpg::ObjectVisual::TreeOak) { color = RGB(54, 142, 80); radius = 4; }
        else if (def && def->visual == rpg::ObjectVisual::StoneRound) color = RGB(159, 166, 173);
        else if (def && def->visual == rpg::ObjectVisual::Bush) { color = RGB(99, 170, 86); radius = 2; }
        HBRUSH brush = CreateSolidBrush(color);
        HGDIOBJ previousBrush = SelectObject(hdc, brush);
        HGDIOBJ previousPen = SelectObject(hdc, GetStockObject(NULL_PEN));
        Ellipse(hdc, x - radius, y - radius, x + radius + 1, y + radius + 1);
        SelectObject(hdc, previousPen);
        SelectObject(hdc, previousBrush);
        DeleteObject(brush);
    }

    const int playerX = static_cast<int>(std::round(originX + g_game.player.pos.x / kTileSize * mapScale));
    const int playerY = static_cast<int>(std::round(originY + g_game.player.pos.y / kTileSize * mapScale));
    HBRUSH playerBrush = CreateSolidBrush(RGB(255, 231, 112));
    HGDIOBJ previousBrush = SelectObject(hdc, playerBrush);
    HGDIOBJ previousPen = SelectObject(hdc, GetStockObject(NULL_PEN));
    Ellipse(hdc, playerX - 5, playerY - 5, playerX + 6, playerY + 6);
    SelectObject(hdc, previousPen);
    SelectObject(hdc, previousBrush);
    DeleteObject(playerBrush);
    RestoreDC(hdc, -1);
}

void DrawDebugCircle(HDC hdc, Vec2 center, float radius, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    const float scale = RenderScale();
    const int x = static_cast<int>(std::round(center.x * scale - g_game.camera.x));
    const int y = static_cast<int>(std::round(center.y * scale - g_game.camera.y));
    const int r = std::max(1, static_cast<int>(std::round(radius * scale)));
    Ellipse(hdc, x - r, y - r, x + r, y + r);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void DrawDebugCollisionOverlay(HDC hdc) {
    if (!g_console.showCollisionLines) return;

    const float scale = RenderScale();
    HPEN terrainPen = CreatePen(PS_SOLID, 1, RGB(244, 150, 72));
    HGDIOBJ oldPen = SelectObject(hdc, terrainPen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    for (int ty = 0; ty < g_game.scene.mapHeight; ++ty) {
        for (int tx = 0; tx < g_game.scene.mapWidth; ++tx) {
            const rpg::TerrainDef* natural = rpg::FindTerrainDef(rpg::NaturalTerrainAt(g_game.scene, tx, ty), rpg::TerrainLayer::Natural);
            const rpg::TerrainDef* built = rpg::FindTerrainDef(rpg::BuiltTerrainAt(g_game.scene, tx, ty), rpg::TerrainLayer::Built);
            if ((!natural || !natural->blocksMovement) && (!built || !built->blocksMovement)) continue;
            const int left = static_cast<int>(std::round(tx * kTileSize * scale - g_game.camera.x));
            const int top = static_cast<int>(std::round(ty * kTileSize * scale - g_game.camera.y));
            const int right = static_cast<int>(std::round((tx + 1) * kTileSize * scale - g_game.camera.x));
            const int bottom = static_cast<int>(std::round((ty + 1) * kTileSize * scale - g_game.camera.y));
            Rectangle(hdc, left, top, right, bottom);
        }
    }
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(terrainPen);

    for (const rpg::SceneObject& object : g_game.scene.objects) {
        rpg::DrawSceneObjectCollision(hdc, object, g_game.camera.x, g_game.camera.y, RGB(255, 80, 110), scale);
    }
    DrawDebugCircle(hdc, g_game.player.pos, kPlayerRadius, RGB(80, 220, 255));
    for (const Npc& npc : g_game.npcs) {
        if (!npc.shadowForm) DrawDebugCircle(hdc, npc.pos, kNpcRadius, RGB(255, 224, 82));
    }
    for (const Monster& monster : g_game.monsters) {
        if (monster.alive) DrawDebugCircle(hdc, monster.pos, 16.0f, RGB(225, 102, 238));
    }
}

void DrawWanderingShadows(HDC hdc) {
    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const float scale = RenderScale();
    for (const WanderingShadow& shadow : g_game.wanderingShadows) {
        float opacity = shadow.state == WandererState::Waiting ? 0.52f : 0.72f;
        if (shadow.state == WandererState::Expelled) {
            opacity = std::max(0.0f, 1.0f - shadow.age / shadow.fadeTime) * 0.52f;
        }
        const int alpha = std::clamp(static_cast<int>(opacity * 255.0f), 0, 255);
        const float x = shadow.pos.x * scale - g_game.camera.x;
        const float y = shadow.pos.y * scale - g_game.camera.y;
        const float rx = std::max(8.0f, 15.0f * scale);
        const float ry = std::max(10.0f, 20.0f * scale);
        Gdiplus::SolidBrush brush(Gdiplus::Color(alpha, 86, 91, 96));
        Gdiplus::Pen pen(Gdiplus::Color(alpha, 185, 192, 198), std::max(1.0f, scale));
        graphics.FillEllipse(&brush, x - rx, y - ry, rx * 2.0f, ry * 2.0f);
        graphics.DrawEllipse(&pen, x - rx, y - ry, rx * 2.0f, ry * 2.0f);
        if (shadow.state != WandererState::Expelled || opacity > 0.3f) {
            DrawTextLine(hdc, shadow.name, static_cast<int>(x - 20), static_cast<int>(y - ry - 22), RGB(204, 210, 216));
        }
    }
}

void DrawShadowDialog(HDC hdc, const RECT& client) {
    if (g_game.shadowDialogIndex < 0 ||
        g_game.shadowDialogIndex >= static_cast<int>(g_game.wanderingShadows.size())) return;
    const RECT dialog = ShadowDialogRect(client);
    FillRectColor(hdc, dialog, RGB(32, 35, 37));
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(142, 150, 157));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, dialog.left, dialog.top, dialog.right, dialog.bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    const WanderingShadow& shadow = g_game.wanderingShadows[g_game.shadowDialogIndex];
    DrawTextLine(hdc, shadow.name, dialog.left + 22, dialog.top + 18, RGB(230, 233, 235));
    DrawTextLine(hdc, L"一个即将消散的影子在领地外徘徊，等待你的决定。", dialog.left + 22, dialog.top + 50, RGB(196, 202, 205));
    const RECT accept = ShadowAcceptButton(dialog);
    const RECT expel = ShadowExpelButton(dialog);
    FillRectColor(hdc, accept, RGB(54, 94, 70));
    FillRectColor(hdc, expel, RGB(91, 58, 61));
    DrawTextLine(hdc, L"收留", accept.left + 38, accept.top + 9, RGB(238, 244, 238));
    DrawTextLine(hdc, L"驱逐", expel.left + 38, expel.top + 9, RGB(244, 236, 236));
}

void DrawBuildingPlacementPreview(HDC hdc) {
    if (!g_game.inventory.placingBuilding) return;
    const POINT tile = g_game.inventory.placementTile;
    rpg::SceneObject preview = rpg::MakeObject(
        g_game.inventory.placementObjectType,
        BuildingAnchorPosition(g_game.inventory.placementObjectType, tile.x, tile.y), 0);
    const float scale = RenderScale();
    rpg::DrawSceneObject(hdc, preview, g_game.camera.x, g_game.camera.y, false, scale, 128);
    rpg::DrawSceneObjectCollision(hdc, preview, g_game.camera.x, g_game.camera.y,
                                  g_game.inventory.placementValid ? RGB(100, 235, 130) : RGB(240, 90, 90), scale);
}

void DrawDebugConsole(HDC hdc, const RECT& client) {
    if (!g_console.open) return;

    const int width = std::max(320, static_cast<int>(client.right) - 32);
    const int height = std::min(260, std::max(160, static_cast<int>(client.bottom) / 2));
    const RECT panel{16, 16, 16 + width, 16 + height};
    FillRectColor(hdc, panel, RGB(18, 21, 21));
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(111, 181, 147));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, panel.left, panel.top, panel.right, panel.bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    HFONT font = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             FIXED_PITCH | FF_MODERN, L"Consolas");
    HGDIOBJ oldFont = SelectObject(hdc, font);
    DrawTextLine(hdc, L"DEBUG CONSOLE  [~ close]", panel.left + 12, panel.top + 10, RGB(142, 216, 179));
    const int availableLines = std::max(1, (height - 70) / 20);
    const size_t firstLine = g_console.lines.size() > static_cast<size_t>(availableLines)
        ? g_console.lines.size() - availableLines
        : 0;
    int y = panel.top + 38;
    for (size_t i = firstLine; i < g_console.lines.size(); ++i, y += 20) {
        DrawTextLine(hdc, g_console.lines[i], panel.left + 12, y, RGB(215, 221, 216));
    }
    FillRectColor(hdc, RECT{panel.left + 8, panel.bottom - 34, panel.right - 8, panel.bottom - 8}, RGB(8, 10, 10));
    DrawTextLine(hdc, L"> " + g_console.input + L"_", panel.left + 12, panel.bottom - 31, RGB(245, 245, 235));
    SelectObject(hdc, oldFont);
    DeleteObject(font);
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

bool RectsOverlap(const rpg::RectF& a, const rpg::RectF& b) {
    return a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top;
}

bool PlacementObjectsOverlap(const rpg::SceneObject& a, const rpg::SceneObject& b) {
    if (!a.collision.blocks || !b.collision.blocks) return false;
    if (a.collision.shape == rpg::CollisionShape::Rect && b.collision.shape == rpg::CollisionShape::Rect) {
        return RectsOverlap(rpg::ObjectCollisionRect(a), rpg::ObjectCollisionRect(b));
    }
    const rpg::RectF aRect = rpg::ObjectCollisionRect(a);
    const rpg::RectF bRect = rpg::ObjectCollisionRect(b);
    return RectsOverlap(aRect, bRect);
}

const std::string* PlacementObjectTypeForItem(const ItemStack& item) {
    if (item.id == "territory_anchor") return &item.id;
    const rpg::SceneObjectDef* object = rpg::FindObjectDef(item.id);
    if (object && object->building && object->placeable) return &item.id;
    return nullptr;
}

rpg::Vec2 BuildingAnchorPosition(const std::string& objectType, int tx, int ty) {
    const rpg::SceneObjectDef* def = rpg::FindObjectDef(objectType);
    const int footprintWidth = def ? def->footprintWidth : 1;
    const int footprintHeight = def ? def->footprintHeight : 1;
    return {
        (tx + footprintWidth * 0.5f) * static_cast<float>(kTileSize),
        (ty + footprintHeight) * static_cast<float>(kTileSize)
    };
}

bool PlacementActorCollision(const rpg::SceneObject& candidate, Vec2 actor, float radius) {
    const rpg::RectF rect = rpg::ObjectCollisionRect(candidate);
    const float closestX = Clamp(actor.x, rect.left, rect.right);
    const float closestY = Clamp(actor.y, rect.top, rect.bottom);
    const float dx = actor.x - closestX;
    const float dy = actor.y - closestY;
    return dx * dx + dy * dy <= radius * radius;
}

bool BuildingCanBePlaced(const std::string& objectType, int tx, int ty, std::wstring* reason) {
    const rpg::SceneObjectDef* def = rpg::FindObjectDef(objectType);
    if (!def) {
        if (reason) *reason = L"找不到建筑定义";
        return false;
    }
    if (tx < 0 || ty < 0 || tx + def->footprintWidth > g_game.scene.mapWidth ||
        ty + def->footprintHeight > g_game.scene.mapHeight) {
        if (reason) *reason = L"建筑超出地图边界";
        return false;
    }
    if (objectType == "territory_anchor" &&
        (tx < 3 || ty < 3 ||
         tx + 3 >= g_game.scene.mapWidth || ty + 3 >= g_game.scene.mapHeight)) {
        if (reason) *reason = L"锚点需要完整的 7x7 空间";
        return false;
    }
    const rpg::Vec2 position = BuildingAnchorPosition(objectType, tx, ty);
    rpg::SceneObject candidate = rpg::MakeObject(objectType, position, 0);
    const float collisionRadius = std::max(def->collision.w, def->collision.h) * 0.5f;
    if (rpg::CircleIntersectsBlockedTerrain(g_game.scene, position, std::max(18.0f, collisionRadius))) {
        if (reason) *reason = L"建筑与地形碰撞";
        return false;
    }
    for (const rpg::SceneObject& object : g_game.scene.objects) {
        if (PlacementObjectsOverlap(candidate, object)) {
            if (reason) *reason = L"建筑与其他建筑碰撞";
            return false;
        }
    }
    if (PlacementActorCollision(candidate, g_game.player.pos, kPlayerRadius)) {
        if (reason) *reason = L"建筑与主角碰撞";
        return false;
    }
    for (const Npc& npc : g_game.npcs) {
    if (!npc.shadowForm && PlacementActorCollision(candidate, npc.pos, kNpcRadius)) {
            if (reason) *reason = L"建筑与 NPC 碰撞";
            return false;
        }
    }
    return true;
}

void UpdateBuildingPlacementPreview(int screenX, int screenY) {
    if (!g_game.inventory.placingBuilding) return;
    g_game.inventory.placementTile = ScreenToTile(screenX, screenY);
    std::wstring reason;
    g_game.inventory.placementValid = BuildingCanBePlaced(
        g_game.inventory.placementObjectType,
        g_game.inventory.placementTile.x,
        g_game.inventory.placementTile.y,
        &reason);
}

bool PlaceBuildingAtPreview() {
    if (!g_game.inventory.placingBuilding) return false;
    const POINT tile = g_game.inventory.placementTile;
    std::wstring reason;
    if (!BuildingCanBePlaced(g_game.inventory.placementObjectType, tile.x, tile.y, &reason)) {
        g_game.pickupNotice = reason;
        g_game.pickupNoticeTime = 1.5f;
        return false;
    }
    const int sourceSlot = g_game.inventory.placementSourceSlot;
    ItemStack* source = InventoryItemAt(sourceSlot);
    if (!source || source->id.empty()) return false;
    rpg::SceneObject object = rpg::MakeObject(
        g_game.inventory.placementObjectType,
        BuildingAnchorPosition(g_game.inventory.placementObjectType, tile.x, tile.y),
        static_cast<int>(g_game.scene.objects.size()) + 10000);
    object.id = g_game.inventory.placementObjectType + "_" + std::to_string(object.id.size() + g_game.scene.objects.size());
    g_game.scene.objects.push_back(std::move(object));
    if (source->count > 1) --source->count;
    else *source = {};
    const bool isAnchor = g_game.inventory.placementObjectType == "territory_anchor";
    if (isAnchor) {
        for (int y = tile.y - 3; y <= tile.y + 3; ++y)
            for (int x = tile.x - 3; x <= tile.x + 3; ++x) rpg::SetTerritory(g_game.scene, x, y, true);
        rpg::InvalidateTerritoryRenderCache();
    }
    g_game.inventory.placingBuilding = false;
    g_game.inventory.placingAnchor = false;
    g_game.inventory.placementSourceSlot = -1;
    g_game.pickupNotice = isAnchor ? L"现实锚点已建立，生成 7x7 领地" : L"建筑已放置";
    g_game.pickupNoticeTime = 2.0f;
    SaveCurrentScene();
    return true;
}

void CancelBuildingPlacement() {
    if (!g_game.inventory.placingBuilding) return;
    g_game.inventory.placingBuilding = false;
    g_game.inventory.placingAnchor = false;
    g_game.inventory.placementSourceSlot = -1;
    g_game.inventory.placementObjectType.clear();
    g_game.pickupNotice = L"已取消放置，物品返回原格";
    g_game.pickupNoticeTime = 1.5f;
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

ItemStack* AnchorStorageItemAt(int index) {
    if (index >= kAnchorStorageSlotBase && index < kAnchorStorageSlotBase + kAnchorStorageSlotCount) {
        return &g_game.anchorStorage[index - kAnchorStorageSlotBase];
    }
    return nullptr;
}

ItemStack* ItemAt(int index) {
    if (ItemStack* item = InventoryItemAt(index)) return item;
    return AnchorStorageItemAt(index);
}

const ItemStack& InventoryItemForDrawing(int index) {
    static const ItemStack empty;
    if (g_game.inventory.dragging && g_game.inventory.dragSource == index) {
        return empty;
    }
    const ItemStack* item = ItemAt(index);
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

bool PointInSpiritMenu(const RECT& client, int x, int y) {
    if (g_game.inventory.spiritMenuAmount < 0.75f) return false;
    const InventoryLayout layout = MakeInventoryLayout(client);
    const POINT center = SpiritMenuCenter(layout, g_game.inventory.spiritMenuAmount);
    constexpr int kInputPadding = 4;
    return std::abs(x - center.x) <= 72 + kInputPadding &&
           std::abs(y - center.y) <= 72 + kInputPadding;
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
    if (g_game.inventory.open) {
        g_game.inventory.spiritMenuAmount = 0.0f;
        return;
    }
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
    ItemStack* source = ItemAt(sourceIndex);
    ItemStack* target = ItemAt(targetIndex);
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
    ItemStack* source = ItemAt(sourceIndex);
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
    const ItemStack* item = ItemAt(g_game.inventory.dragSource);
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

RECT AnchorButtonRect() {
    return {16, 52, 60, 96};
}

RECT AnchorPanelRect(const RECT& client) {
    return {18, 104, client.right - 18, client.bottom - 18};
}

RECT AnchorStorageSlotRect(const RECT& panel, int index) {
    constexpr int cell = 34;
    constexpr int gap = 4;
    const int column = index % 10;
    const int row = index / 10;
    const int left = panel.left + 20 + column * (cell + gap);
    const int top = panel.top + 64 + row * (cell + gap);
    return {left, top, left + cell, top + cell};
}

RECT AnchorInventorySlotRect(const RECT& panel, int index) {
    constexpr int cell = 34;
    constexpr int gap = 4;
    const int column = index % 10;
    const int row = index / 10;
    const int left = panel.left + 422 + column * (cell + gap);
    const int top = panel.top + 64 + row * (cell + gap);
    return {left, top, left + cell, top + cell};
}

RECT AnchorNpcRowRect(const RECT& panel, int index) {
    const int top = panel.top + 252 + index * 31;
    return {panel.left + 422, top, panel.right - 18, top + 27};
}

RECT AnchorStoneButtonRect(const RECT& panel, int index) {
    const int left = panel.left + 422 + index * 126;
    return {left, panel.bottom - 48, left + 118, panel.bottom - 20};
}

bool PointInAnchorPanel(const RECT& client, int x, int y) {
    const RECT panel = AnchorPanelRect(client);
    return g_game.anchorPanelOpen && PtInRect(&panel, POINT{x, y});
}

int HitAnchorStorageSlot(const RECT& client, int x, int y) {
    if (!g_game.anchorPanelOpen) return -1;
    const RECT panel = AnchorPanelRect(client);
    for (int i = 0; i < kAnchorStorageSlotCount; ++i) {
        const RECT slot = AnchorStorageSlotRect(panel, i);
        if (PtInRect(&slot, POINT{x, y})) return kAnchorStorageSlotBase + i;
    }
    return -1;
}

int HitAnchorInventorySlot(const RECT& client, int x, int y) {
    if (!g_game.anchorPanelOpen) return -1;
    const RECT panel = AnchorPanelRect(client);
    for (int i = 0; i < kInventorySlotCount; ++i) {
        const RECT slot = AnchorInventorySlotRect(panel, i);
        if (PtInRect(&slot, POINT{x, y})) return i;
    }
    return -1;
}

int HitAnchorNpc(const RECT& client, int x, int y) {
    if (!g_game.anchorPanelOpen) return -1;
    const RECT panel = AnchorPanelRect(client);
    for (int i = 0; i < static_cast<int>(g_game.npcs.size()); ++i) {
        const RECT row = AnchorNpcRowRect(panel, i);
        if (PtInRect(&row, POINT{x, y})) return i;
    }
    return -1;
}

int HitAnchorNpcStone(const RECT& client, int x, int y) {
    if (!g_game.anchorPanelOpen || g_game.anchorSelectedNpc < 0 ||
        g_game.anchorSelectedNpc >= static_cast<int>(g_game.npcs.size())) return -1;
    const RECT panel = AnchorPanelRect(client);
    for (int i = 0; i < kSpiritSlotCount; ++i) {
        const RECT button = AnchorStoneButtonRect(panel, i);
        if (PtInRect(&button, POINT{x, y})) return i;
    }
    return -1;
}

void DrawAnchorButton(HDC hdc) {
    const RECT rect = AnchorButtonRect();
    const int cx = (rect.left + rect.right) / 2;
    const int cy = (rect.top + rect.bottom) / 2;
    HBRUSH brush = CreateSolidBrush(g_game.anchorPanelOpen ? RGB(101, 166, 141) : RGB(43, 82, 73));
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(213, 231, 191));
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    Ellipse(hdc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);

    if (!g_anchorButtonIconAttempted) {
        g_anchorButtonIconAttempted = true;
        if (EnsureGdiPlus()) {
            g_anchorButtonIcon = std::make_unique<Gdiplus::Bitmap>(AssetPath(L"ui/anchor_button.png").c_str());
            if (!g_anchorButtonIcon || g_anchorButtonIcon->GetLastStatus() != Gdiplus::Ok) g_anchorButtonIcon.reset();
        }
    }
    if (g_anchorButtonIcon) {
        Gdiplus::Graphics graphics(hdc);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
        graphics.DrawImage(g_anchorButtonIcon.get(), rect.left + 5, rect.top + 5, 34, 34);
        return;
    }

    HPEN iconPen = CreatePen(PS_SOLID, 2, RGB(240, 245, 211));
    HGDIOBJ oldIconPen = SelectObject(hdc, iconPen);
    MoveToEx(hdc, cx, cy - 13, nullptr);
    LineTo(hdc, cx + 10, cy);
    LineTo(hdc, cx, cy + 13);
    LineTo(hdc, cx - 10, cy);
    LineTo(hdc, cx, cy - 13);
    MoveToEx(hdc, cx, cy - 7, nullptr);
    LineTo(hdc, cx, cy + 8);
    SelectObject(hdc, oldIconPen);
    DeleteObject(iconPen);
}

void DrawAnchorPanel(HDC hdc, const RECT& client) {
    if (!g_game.anchorPanelOpen) return;
    const RECT panel = AnchorPanelRect(client);
    FillRectColor(hdc, panel, RGB(28, 35, 34));
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(116, 160, 139));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, panel.left, panel.top, panel.right, panel.bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    DrawTextLine(hdc, L"现实锚点", panel.left + 20, panel.top + 18, RGB(230, 239, 216));
    DrawTextLine(hdc, L"仓储  " + std::to_wstring(kAnchorStorageSlotCount) + L" 格", panel.left + 20, panel.top + 42, RGB(172, 205, 182));
    DrawTextLine(hdc, L"背包", panel.left + 422, panel.top + 42, RGB(230, 239, 216));
    DrawTextLine(hdc, L"领地居民  " + std::to_wstring(g_game.npcs.size()), panel.left + 422, panel.top + 222, RGB(172, 205, 182));

    for (int i = 0; i < kAnchorStorageSlotCount; ++i) {
        DrawInventorySlot(hdc, AnchorStorageSlotRect(panel, i), InventoryItemForDrawing(kAnchorStorageSlotBase + i), false, false);
    }
    for (int i = 0; i < kInventorySlotCount; ++i) {
        DrawInventorySlot(hdc, AnchorInventorySlotRect(panel, i), InventoryItemForDrawing(i), false, false);
    }

    for (int i = 0; i < static_cast<int>(g_game.npcs.size()); ++i) {
        const Npc& npc = g_game.npcs[i];
        const RECT row = AnchorNpcRowRect(panel, i);
        FillRectColor(hdc, row, i == g_game.anchorSelectedNpc ? RGB(68, 105, 86) : RGB(43, 52, 49));
        DrawTextLine(hdc, npc.name, row.left + 8, row.top + 5, RGB(239, 240, 226));
        DrawTextLine(hdc, npc.shadowForm ? L"影子" : L"实体", row.right - 42, row.top + 5,
                     npc.shadowForm ? RGB(183, 191, 197) : RGB(179, 224, 185));
    }

    if (g_game.anchorSelectedNpc >= 0 && g_game.anchorSelectedNpc < static_cast<int>(g_game.npcs.size())) {
        const Npc& npc = g_game.npcs[g_game.anchorSelectedNpc];
        const int infoY = panel.bottom - 108;
        DrawTextLine(hdc, npc.name + L"  " + npc.personality, panel.left + 422, infoY, RGB(235, 239, 222));
        DrawTextLine(hdc, L"好感度 " + std::to_wstring(npc.affinity) + L" / 100", panel.left + 422, infoY + 22, RGB(222, 190, 126));
        DrawTextLine(hdc, L"血量 " + std::to_wstring(npc.health) + L" / 60", panel.left + 590, infoY + 22, RGB(223, 132, 132));
        constexpr const wchar_t* labels[] = {L"采集石", L"建筑石", L"战斗石"};
        for (int i = 0; i < kSpiritSlotCount; ++i) {
            const RECT button = AnchorStoneButtonRect(panel, i);
            FillRectColor(hdc, button, npc.spiritStones[i] ? RGB(62, 116, 81) : RGB(50, 59, 56));
            DrawTextLine(hdc, npc.spiritStones[i] ? std::wstring(labels[i]) + L" 已装备" : std::wstring(labels[i]),
                         button.left + 7, button.top + 6,
                         npc.spiritStones[i] ? RGB(213, 244, 218) : RGB(222, 226, 210));
        }
    } else {
        DrawTextLine(hdc, L"选择居民查看资料", panel.left + 422, panel.bottom - 54, RGB(172, 185, 175));
    }
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

    DrawWanderingShadows(hdc);

    for (const Npc& npc : g_game.npcs) {
        DrawEllipse(hdc, npc.pos, 15.0f, 19.0f,
                    npc.shadowForm ? RGB(93, 98, 103) : RGB(222, 185, 94),
                    npc.shadowForm ? RGB(177, 184, 190) : RGB(76, 55, 32));
        DrawTextLine(
            hdc,
            npc.name,
            static_cast<int>(std::round(npc.pos.x * renderScale - g_game.camera.x - 18.0f * renderScale)),
            static_cast<int>(std::round(npc.pos.y * renderScale - g_game.camera.y - 38.0f * renderScale)),
            RGB(245, 244, 230));
        if (npc.following) {
            DrawTextLine(
                hdc,
                npc.shadowForm ? L"影子" : (npc.inCombat ? L"战斗" : L"跟随"),
                static_cast<int>(std::round(npc.pos.x * renderScale - g_game.camera.x - 16.0f * renderScale)),
                static_cast<int>(std::round(npc.pos.y * renderScale - g_game.camera.y + 22.0f * renderScale)),
                npc.shadowForm ? RGB(184, 191, 198) : (npc.inCombat ? RGB(244, 126, 126) : RGB(177, 228, 190)));
        } else if (npc.returningHome) {
            DrawTextLine(hdc, L"返回领地",
                         static_cast<int>(std::round(npc.pos.x * renderScale - g_game.camera.x - 28.0f * renderScale)),
                         static_cast<int>(std::round(npc.pos.y * renderScale - g_game.camera.y + 22.0f * renderScale)),
                         RGB(192, 215, 203));
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

    DrawDebugCollisionOverlay(hdc);

    const std::wstring zoomText = L"视角 " + std::to_wstring(static_cast<int>(std::round(renderScale * 100.0f))) + L"%";
    DrawTextLine(hdc, zoomText, 18, 18, RGB(220, 230, 202));
    DrawAnchorButton(hdc);

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

    DrawBuildingPlacementPreview(hdc);

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
        const RECT panel = NpcContextRect();
        FillRectColor(hdc, panel, RGB(35, 40, 38));
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(151, 162, 151));
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, panel.left, panel.top, panel.right, panel.bottom);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
        const Npc& npc = g_game.npcs[g_game.npcContextIndex];
        const std::wstring labels[] = {
            npc.following ? L"取消跟随" : L"跟随",
            npc.spiritStones[0] ? L"采集石：已装备" : L"采集石：未装备",
            npc.spiritStones[1] ? L"建筑石：已装备" : L"建筑石：未装备",
            npc.spiritStones[2] ? L"战斗石：已装备" : L"战斗石：未装备",
        };
        for (int row = 0; row < 4; ++row) {
            const RECT button = NpcContextButtonRect(row);
            FillRectColor(hdc, button, row == 0 ? RGB(55, 65, 60) : RGB(47, 55, 52));
            DrawTextLine(hdc, labels[row], button.left + 10, button.top + 7,
                         row > 0 && npc.spiritStones[row - 1] ? RGB(168, 225, 183) : RGB(235, 239, 226));
        }
    }

    DrawShadowDialog(hdc, client);

    DrawInventory(hdc, client);
    DrawAnchorPanel(hdc, client);
    DrawDraggedInventoryItem(hdc);
    DrawWorldMap(hdc, client);
    DrawDebugConsole(hdc, client);

    BitBlt(target, 0, 0, client.right, client.bottom, hdc, 0, 0, SRCCOPY);

    SelectObject(hdc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(hdc);
}

void ToggleAnchorPanel() {
    if (!HasTerritoryAnchor()) {
        g_game.pickupNotice = L"请先放置现实锚点";
        g_game.pickupNoticeTime = 1.5f;
        return;
    }
    g_game.anchorPanelOpen = !g_game.anchorPanelOpen;
    g_game.inventory.open = false;
    g_game.inventory.dragging = false;
    g_game.inventory.dragSource = -1;
    g_game.npcContextIndex = -1;
    g_game.showTalk = false;
    if (!g_game.anchorPanelOpen) g_game.anchorSelectedNpc = -1;
}

void ToggleAnchorNpcStone(int stoneIndex) {
    if (g_game.anchorSelectedNpc < 0 || g_game.anchorSelectedNpc >= static_cast<int>(g_game.npcs.size()) ||
        stoneIndex < 0 || stoneIndex >= kSpiritSlotCount) return;
    Npc& npc = g_game.npcs[g_game.anchorSelectedNpc];
    if (npc.spiritStones[stoneIndex]) {
        if (ReturnSpiritStoneToPlayer(stoneIndex)) {
            npc.spiritStones[stoneIndex] = false;
            g_game.pickupNotice = L"灵石已退回背包";
        } else {
            g_game.pickupNotice = L"背包已满，无法卸下灵石";
        }
    } else if (TakeSpiritStoneFromPlayer(stoneIndex)) {
        npc.spiritStones[stoneIndex] = true;
        g_game.pickupNotice = npc.name + L" 已装备灵石";
    } else {
        g_game.pickupNotice = L"你没有对应的灵石";
    }
    g_game.pickupNoticeTime = 1.8f;
    SaveCurrentGame();
}

void ToggleInventory() {
    if (g_game.anchorPanelOpen) {
        g_game.anchorPanelOpen = false;
        g_game.anchorSelectedNpc = -1;
    }
    g_game.inventory.open = !g_game.inventory.open;
    g_game.inventory.focusedSlot = g_game.inventory.selectedHotbar;
    g_game.up = false;
    g_game.down = false;
    g_game.left = false;
    g_game.right = false;
    g_game.interact = false;
    g_game.showTalk = false;
    if (g_game.inventory.open) {
        g_game.inventory.spiritMenuAmount = 0.0f;
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
        KnockbackMonster(monster, g_game.player.pos, 54.0f);
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

const char* GatheringDropForObject(const rpg::SceneObject& object) {
    if (object.type == "tree_oak" || object.type == "exotic_tree_01") return "wood";
    if (object.type == "stone_round") return "iron_ore";
    return nullptr;
}

void StartGathering() {
    if (g_game.inventory.heldItem.id != kGatheringStoneId) {
        g_game.pickupNotice = L"需要启用采集石";
        g_game.pickupNoticeTime = 1.2f;
        return;
    }
    if (g_game.inventory.open || g_game.anchorPanelOpen || g_game.player.attacking) return;

    constexpr float kGatheringRange = 78.0f;
    int targetIndex = -1;
    float nearest = kGatheringRange;
    for (int i = 0; i < static_cast<int>(g_game.scene.objects.size()); ++i) {
        const rpg::SceneObject& object = g_game.scene.objects[i];
        if (!GatheringDropForObject(object)) continue;
        const float distance = Distance(g_game.player.pos, {object.pos.x, object.pos.y});
        if (distance < nearest) {
            nearest = distance;
            targetIndex = i;
        }
    }
    if (targetIndex < 0) {
        g_game.pickupNotice = L"采集范围内没有树木或石头";
        g_game.pickupNoticeTime = 1.2f;
        return;
    }

    const rpg::SceneObject target = g_game.scene.objects[targetIndex];
    const char* dropId = GatheringDropForObject(target);
    const int count = target.type == "stone_round" ? 2 : (target.type == "exotic_tree_01" ? 5 : 3);
    for (int i = 0; i < count; ++i) {
        const float angle = static_cast<float>(i) * 6.2831853f / std::max(1, count);
        const float spread = 10.0f + static_cast<float>(i % 2) * 7.0f;
        ItemStack drop = MakeItemStack(dropId, 1);
        CurrentPickups().push_back({
            {target.pos.x + std::cos(angle) * spread, target.pos.y + std::sin(angle) * spread},
            std::move(drop),
        });
    }
    g_game.scene.objects.erase(
        std::remove_if(g_game.scene.objects.begin(), g_game.scene.objects.end(), [&](const rpg::SceneObject& object) {
            return object.id == target.id || (!target.groupId.empty() && object.groupId == target.groupId);
        }),
        g_game.scene.objects.end());
    g_game.player.attacking = true;
    g_game.player.attackTime = 0.0f;
    g_game.player.attackDir = g_game.player.dir;
    const rpg::ItemDef* def = rpg::FindItemDef(dropId);
    g_game.pickupNotice = L"采集获得 " + (def ? def->displayName : L"资源") + L" x" + std::to_wstring(count);
    g_game.pickupNoticeTime = 1.5f;
    SaveCurrentScene();
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
    if (g_game.anchorPanelOpen) {
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
        if (wParam == VK_OEM_3) {
            if ((lParam & (1LL << 30)) == 0) {
                g_console.open = !g_console.open;
                g_game.up = g_game.down = g_game.left = g_game.right = false;
                g_game.interact = false;
                g_game.player.attacking = false;
                g_game.player.attackTime = 0.0f;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        if (g_console.open) {
            if (wParam == VK_ESCAPE) {
                g_console.open = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        if (wParam == 'M' && (lParam & (1LL << 30)) == 0) {
            g_game.mapOpen = !g_game.mapOpen;
            g_game.mapDragging = false;
            ReleaseCapture();
            g_game.inventory.open = false;
            g_game.anchorPanelOpen = false;
            g_game.showTalk = false;
            g_game.up = g_game.down = g_game.left = g_game.right = false;
            g_game.interact = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (g_game.mapOpen) {
            if (wParam == VK_ESCAPE) {
                g_game.mapOpen = false;
                g_game.mapDragging = false;
                ReleaseCapture();
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (g_game.anchorPanelOpen && wParam == VK_ESCAPE) {
            g_game.anchorPanelOpen = false;
            g_game.anchorSelectedNpc = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (g_game.shadowDialogIndex >= 0 && wParam == VK_ESCAPE) {
            g_game.shadowDialogIndex = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (g_game.inventory.placingBuilding && (wParam == VK_SPACE || wParam == VK_ESCAPE)) {
            CancelBuildingPlacement();
            InvalidateRect(hwnd, nullptr, FALSE);
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
    case WM_CHAR:
        if (g_screen != AppScreen::Playing || !g_console.open) {
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
        if (wParam == L'\r') {
            ExecuteConsoleCommand();
        } else if (wParam == L'\b') {
            if (!g_console.input.empty()) g_console.input.pop_back();
        } else if (wParam != L'`' && wParam != L'~' && wParam >= L' ' && g_console.input.size() < 160) {
            g_console.input.push_back(static_cast<wchar_t>(wParam));
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_KEYUP:
        if (g_console.open) return 0;
        if (g_game.mapOpen) return 0;
        if (g_game.inventory.placingBuilding) {
            PlaceBuildingAtPreview();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
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
        if (g_console.open || g_game.mapOpen) return 0;
        if (g_game.inventory.open) {
            if (const int spirit = HitSpiritMenu(client, x, y); spirit >= 0) {
                constexpr const wchar_t* labels[] = {L"采集", L"建造", L"战斗"};
                AppendConsoleLine(L"[input] 扇形菜单选中：" + std::wstring(labels[spirit]));
                SelectSpiritStone(spirit);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (PointInSpiritMenu(client, x, y)) {
                AppendConsoleLine(L"[input] 扇形菜单边缘点击已拦截");
                return 0;
            }
        }
        if (g_game.inventory.dragging) {
            int target = HitAnchorStorageSlot(client, x, y);
            if (target < 0) target = HitAnchorInventorySlot(client, x, y);
            if (target < 0) target = HitInventorySlot(client, x, y);
            if (target >= 0) {
                MoveInventoryItem(g_game.inventory.dragSource, target);
                g_game.inventory.focusedSlot = target;
            } else {
                DropInventoryItemOnGround(g_game.inventory.dragSource, x, y);
            }
            g_game.inventory.dragging = false;
            g_game.inventory.dragSource = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        const RECT anchorButton = AnchorButtonRect();
        if (PtInRect(&anchorButton, POINT{x, y})) {
            ToggleAnchorPanel();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (g_game.anchorPanelOpen) {
            if (const int stone = HitAnchorNpcStone(client, x, y); stone >= 0) {
                ToggleAnchorNpcStone(stone);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (const int npc = HitAnchorNpc(client, x, y); npc >= 0) {
                g_game.anchorSelectedNpc = npc;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            int slot = HitAnchorStorageSlot(client, x, y);
            if (slot < 0) slot = HitAnchorInventorySlot(client, x, y);
            if (slot >= 0) {
                g_game.inventory.focusedSlot = slot;
                ItemStack* item = ItemAt(slot);
                if (item && !item->id.empty()) {
                    g_game.inventory.dragging = true;
                    g_game.inventory.dragSource = slot;
                    g_game.inventory.dragPoint = POINT{x, y};
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (PointInAnchorPanel(client, x, y)) return 0;
        }
        if (g_game.shadowDialogIndex >= 0) {
            const RECT dialog = ShadowDialogRect(client);
            const RECT accept = ShadowAcceptButton(dialog);
            const RECT expel = ShadowExpelButton(dialog);
            if (g_game.shadowDialogIndex < static_cast<int>(g_game.wanderingShadows.size())) {
                WanderingShadow& shadow = g_game.wanderingShadows[g_game.shadowDialogIndex];
                if (PtInRect(&accept, POINT{x, y})) {
                    shadow.state = WandererState::Accepted;
                    shadow.age = 0.0f;
                    g_game.pickupNotice = L"你收留了流浪影子，它正在走向领地";
                    g_game.pickupNoticeTime = 2.0f;
                    g_game.shadowDialogIndex = -1;
                } else if (PtInRect(&expel, POINT{x, y})) {
                    shadow.state = WandererState::Expelled;
                    shadow.age = 0.0f;
                    g_game.pickupNotice = L"影子被驱逐，正在缓慢消散";
                    g_game.pickupNoticeTime = 2.0f;
                    g_game.shadowDialogIndex = -1;
                }
            } else {
                g_game.shadowDialogIndex = -1;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (!g_game.inventory.open) {
            if (const int shadowIndex = HitWanderingShadow(x, y); shadowIndex >= 0) {
                g_game.shadowDialogIndex = shadowIndex;
                g_game.npcContextIndex = -1;
                g_game.showTalk = false;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }
        if (g_game.npcContextIndex >= 0) {
            if (g_game.npcContextIndex < static_cast<int>(g_game.npcs.size())) {
                Npc& npc = g_game.npcs[g_game.npcContextIndex];
                for (int row = 0; row < 4; ++row) {
                    const RECT button = NpcContextButtonRect(row);
                    if (!PtInRect(&button, POINT{x, y})) continue;
                    if (row == 0) {
                        if (npc.following) {
                            npc.following = false;
                            npc.inCombat = false;
                            npc.moving = false;
                            npc.returningHome = !ActorProtectedByTerritory(npc.pos);
                            g_game.pickupNotice = npc.name + (npc.returningHome ? L" 已取消跟随，正在返回领地" : L" 已取消跟随");
                        } else if (FollowerCount() >= kMaximumFollowers) {
                            g_game.pickupNotice = L"最多只能有 4 名跟随者";
                        } else {
                            npc.following = true;
                            npc.returningHome = false;
                            npc.moving = Distance(npc.pos, g_game.player.pos) > kNpcFollowStartDistance;
                            g_game.pickupNotice = npc.name + L" 已开始跟随";
                        }
                    } else {
                        const int stoneIndex = row - 1;
                        if (npc.spiritStones[stoneIndex]) {
                            if (ReturnSpiritStoneToPlayer(stoneIndex)) {
                                npc.spiritStones[stoneIndex] = false;
                                g_game.pickupNotice = L"灵石已退回背包";
                            } else {
                                g_game.pickupNotice = L"背包已满，无法卸下灵石";
                            }
                        } else if (TakeSpiritStoneFromPlayer(stoneIndex)) {
                            npc.spiritStones[stoneIndex] = true;
                            g_game.pickupNotice = npc.name + L" 已装备灵石";
                        } else {
                            g_game.pickupNotice = L"你没有对应的灵石";
                        }
                    }
                    g_game.pickupNoticeTime = 1.8f;
                    SaveCurrentGame();
                    g_game.npcContextIndex = -1;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
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
            if (item && PlacementObjectTypeForItem(*item)) {
                g_game.inventory.placingBuilding = true;
                g_game.inventory.placementSourceSlot = hit;
                g_game.inventory.placementObjectType = *PlacementObjectTypeForItem(*item);
                g_game.inventory.placementTile = ScreenToTile(x, y);
                UpdateBuildingPlacementPreview(x, y);
                g_game.inventory.open = false;
                g_game.pickupNotice = L"移动鼠标预览建筑，再次点击确认；按空格取消";
                g_game.pickupNoticeTime = 2.0f;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (item && !item->id.empty()) {
                g_game.inventory.dragging = true;
                g_game.inventory.dragSource = hit;
                g_game.inventory.dragPoint = POINT{x, y};
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
                PlaceBuildingAtPreview();
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
        if (g_game.inventory.heldItem.id == kGatheringStoneId) StartGathering();
        else StartAttack();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE:
        g_game.inventory.mousePoint = POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tracking);
        }
        if (g_game.mapOpen && g_game.mapDragging) {
            const POINT current{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            g_game.mapPan.x += static_cast<float>(current.x - g_game.mapDragPoint.x);
            g_game.mapPan.y += static_cast<float>(current.y - g_game.mapDragPoint.y);
            g_game.mapDragPoint = current;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (g_game.inventory.dragging) {
            g_game.inventory.dragPoint = POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        if (g_game.inventory.placingBuilding) {
            UpdateBuildingPlacementPreview(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        }
        if (g_game.inventory.selectingTerritory && (wParam & MK_RBUTTON)) {
            g_game.inventory.territoryEndTile = ScreenToTile(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_MOUSELEAVE:
        g_game.inventory.mousePoint = POINT{-10000, -10000};
        return 0;
    case WM_MOUSEWHEEL:
        if (g_screen == AppScreen::Playing && g_game.mapOpen) {
            const short wheel = GET_WHEEL_DELTA_WPARAM(wParam);
            g_game.mapZoom = Clamp(g_game.mapZoom * (wheel > 0 ? 1.2f : 1.0f / 1.2f), 1.0f, 4.0f);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        return 0;
    case WM_MBUTTONDOWN:
        if (g_screen == AppScreen::Playing && g_game.mapOpen) {
            g_game.mapDragging = true;
            g_game.mapDragPoint = POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            SetCapture(hwnd);
            return 0;
        }
        return 0;
    case WM_MBUTTONUP:
        if (g_game.mapDragging) {
            g_game.mapDragging = false;
            ReleaseCapture();
        }
        return 0;
    case WM_RBUTTONDOWN:
        if (g_console.open || g_game.mapOpen || g_game.anchorPanelOpen) return 0;
        if (g_game.inventory.placingBuilding) return 0;
        if (g_screen == AppScreen::Playing && !g_game.inventory.open) {
            const int npcIndex = HitNpc(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (npcIndex >= 0) {
                RECT client{};
                GetClientRect(hwnd, &client);
                g_game.npcContextIndex = npcIndex;
                g_game.npcContextPoint = {
                    std::clamp<int>(GET_X_LPARAM(lParam) + 8, 8, std::max<int>(8, client.right - 184)),
                    std::clamp<int>(GET_Y_LPARAM(lParam) + 8, 8, std::max<int>(8, client.bottom - 164)),
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
    case WM_LBUTTONUP:
        return 0;
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
        g_anchorButtonIcon.reset();
        g_anchorButtonIconAttempted = false;
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
