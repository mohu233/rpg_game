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
#include <random>
#include <cwctype>
#include <limits>
#include <sstream>
#include <set>
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
constexpr float kGameDaySeconds = 12.0f * 60.0f;
constexpr float kMinimumRecruitmentStability = 25.0f;
constexpr int kResidentRealityLoad = 10;
constexpr int kWorkbenchRealityLoad = 2;
constexpr int kTeleportRealityLoad = 8;
constexpr float kNpcMinimumDistance = 80.0f;
constexpr float kNpcFollowStartDistance = 200.0f;
constexpr float kNpcFollowStopDistance = 145.0f;
constexpr float kNpcCombatLeashDistance = 500.0f;
constexpr int kNpcMaximumHealth = 60;
constexpr int kNpcRetreatHealth = kNpcMaximumHealth * 30 / 100;

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

enum class NpcTaskMode { Idle, Facility, Gathering, Returning };

struct NpcCargoStack {
    std::string id;
    int count = 0;
};

struct Npc {
    Vec2 pos;
    Vec2 velocity{};
    std::wstring name;
    std::wstring text;
    bool following = false;
    std::array<bool, 3> spiritStones{};
    bool inCombat = false;
    bool evading = false;
    int threatMonster = -1;
    float evadeTime = 0.0f;
    bool moving = false;
    float attackCooldown = 0.0f;
    bool returningHome = false;
    bool shadowForm = false;
    Vec2 wanderTarget{};
    float wanderTimer = 0.0f;
    int health = 60;
    int affinity = 50;
    std::wstring personality = L"谨慎";
    NpcTaskMode taskMode = NpcTaskMode::Idle;
    std::string gatheringTarget = "any";
    std::string facilityId;
    std::wstring workMap;
    std::string gatheringObjectId;
    float workTimer = 0.0f;
    std::array<NpcCargoStack, 10> cargo{};
};

std::vector<Npc> MakeDefaultNpcs() {
    return {};
}

bool IsLegacyDefaultNpcName(const std::wstring& name) {
    return name == L"莉娜" || name == L"诺亚" || name == L"米拉" || name == L"塞恩";
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

enum class BuildingToolMode {
    Move,
    Farmland,
    Territory,
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
    BuildingToolMode buildingToolMode = BuildingToolMode::Move;
    bool movingExistingBuilding = false;
    rpg::SceneObject movingBuilding;
    bool placingCraftedBuilding = false;
    std::vector<ItemStack> reservedBuildingMaterials;
    bool selectingTerritory = false;
    bool selectionRemoving = false;
    POINT territoryStartTile{};
    POINT territoryEndTile{};
};

struct WorldPickup {
    Vec2 pos;
    ItemStack item;
    bool requiresClick = false;
    bool activated = false;
};

enum class AnchorPanelTab {
    Storage,
    Workbench,
    Crafting,
    Residents,
    Work,
    Technology,
};

enum class CraftingStation {
    Workbench,
    Decompose,
    Production,
};

enum class CraftingCategory {
    Items,
    Buildings,
};

enum class DeathPhase {
    None,
    CompensationIntro,
    AwaitingSacrifice,
    RevivalFade,
    FinalEnding,
};

struct Game {
    Player player;
    Inventory inventory;
    std::array<ItemStack, kAnchorStorageSlotCount> anchorStorage;
    bool anchorPanelOpen = false;
    AnchorPanelTab anchorPanelTab = AnchorPanelTab::Residents;
    int anchorSelectedNpc = -1;
    int anchorSelectedWorkbenchRecipe = 0;
    CraftingStation craftingStation = CraftingStation::Workbench;
    CraftingCategory craftingCategory = CraftingCategory::Items;
    std::set<std::string> unlockedBlueprints{"territory_anchor"};
    int territoryLevel = 1;
    float territoryStability = 100.0f;
    float territoryDayProgress = 0.0f;
    int territoryDaysPassed = 0;
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
    DeathPhase deathPhase = DeathPhase::None;
    float deathTime = 0.0f;
    float teleportCooldown = 0.0f;
    bool mapOpen = false;
    float mapZoom = 1.0f;
    Vec2 mapPan{};
    bool mapDragging = false;
    POINT mapDragPoint{};
    std::map<std::wstring, std::vector<std::uint8_t>> exploredTilesByScene;
    bool explorationDirty = false;
    float explorationSaveTimer = 0.0f;
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

bool NpcInCurrentScene(const Npc& npc) {
    return npc.workMap.empty() || npc.workMap == g_currentScenePath.filename().wstring();
}

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

void SaveCurrentGame();
void SaveCurrentScene();
bool StoreAnchorItem(ItemStack& incoming);
bool UpdateDeathSequence(float dt);
void CancelBuildingPlacement();
void DrawCenteredText(HDC hdc, const std::wstring& text, const RECT& rect, COLORREF color, int fontSize);

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
        if (i == ignoredNpcIndex || g_game.npcs[i].shadowForm || !NpcInCurrentScene(g_game.npcs[i])) continue;
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
            if (!npc.following || npc.shadowForm || !NpcInCurrentScene(npc)) return false;
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

int SpiritSkillIndex(std::string_view id) {
    if (id.rfind("gathering_stone", 0) == 0) return 0;
    if (id.rfind("building_stone", 0) == 0) return 1;
    if (id.rfind("combat_stone", 0) == 0) return 2;
    return -1;
}

int SpiritStoneTier(std::string_view id) {
    if (SpiritSkillIndex(id) < 0) return 0;
    if (id.size() >= 4 && id.substr(id.size() - 4) == "_iii") return 3;
    if (id.size() >= 3 && id.substr(id.size() - 3) == "_ii") return 2;
    return 1;
}

bool HeldSpiritSkill(int skillIndex) {
    return SpiritSkillIndex(g_game.inventory.heldItem.id) == skillIndex;
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

std::mt19937 g_lootRandom{std::random_device{}()};

void SpawnPickup(Vec2 position, std::string_view itemId, int count = 1) {
    ItemStack item = MakeItemStack(itemId, count);
    if (item.id.empty()) return;
    CurrentPickups().push_back({position, std::move(item)});
}

void SpawnMonsterLoot(Vec2 position) {
    std::uniform_real_distribution<float> roll(0.0f, 1.0f);
    if (roll(g_lootRandom) < 0.01f) SpawnPickup(position, "anchor_fragment");
    if (roll(g_lootRandom) < 0.12f) SpawnPickup({position.x - 10.0f, position.y}, "healing_herb");
    if (roll(g_lootRandom) < 0.10f) SpawnPickup({position.x + 10.0f, position.y}, "berry");
    if (roll(g_lootRandom) < 0.06f) SpawnPickup({position.x, position.y + 10.0f}, "wheat");
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
void SettleTerritoryDay();
void EnsureSceneMonsters();

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
        AppendConsoleLine(L"add monster/怪物 <quantity> - spawn at mouse");
        AppendConsoleLine(L"shadow - spawn a wandering shadow near territory");
        AppendConsoleLine(L"day - settle one territory day");
        AppendConsoleLine(L"fog - reveal the entire current map");
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
    if (Lowercase(command) == L"fog" || Lowercase(command) == L"fog clear" || Lowercase(command) == L"迷雾") {
        if (g_currentScenePath.empty() || g_game.scene.mapWidth <= 0 || g_game.scene.mapHeight <= 0) {
            AppendConsoleLine(L"No active map is available.");
            return;
        }
        const int tileCount = g_game.scene.mapWidth * g_game.scene.mapHeight;
        std::vector<std::uint8_t>& explored =
            g_game.exploredTilesByScene[g_currentScenePath.filename().wstring()];
        explored.assign(tileCount, 1);
        g_game.explorationDirty = true;
        SaveCurrentGame();
        AppendConsoleLine(L"Fog cleared for the current map and saved.");
        g_game.pickupNotice = L"当前地图迷雾已全部清除";
        g_game.pickupNoticeTime = 2.0f;
        return;
    }
    if (Lowercase(command) == L"day") {
        SettleTerritoryDay();
        AppendConsoleLine(L"Territory day settled.");
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
    const std::wstring targetName = Lowercase(itemName);
    if (targetName == L"monster" || targetName == L"怪物" || targetName == L"畸变暗影") {
        if (parsed > 100) {
            AppendConsoleLine(L"Monster quantity must be between 1 and 100.");
            return;
        }
        const POINT mouse = g_game.inventory.mousePoint;
        if (mouse.x < 0 || mouse.y < 0) {
            AppendConsoleLine(L"Move the mouse into the game window first.");
            return;
        }
        EnsureSceneMonsters();
        const float scale = RenderScale();
        constexpr float radius = 16.0f;
        const Vec2 position{
            Clamp((static_cast<float>(mouse.x) + g_game.camera.x) / scale,
                  radius, std::max(radius, rpg::SceneWorldWidth(g_game.scene) - radius)),
            Clamp((static_cast<float>(mouse.y) + g_game.camera.y) / scale,
                  radius, std::max(radius, rpg::SceneWorldHeight(g_game.scene) - radius)),
        };
        for (int i = 0; i < static_cast<int>(parsed); ++i) g_game.monsters.push_back({position});
        AppendConsoleLine(L"Spawned monster x" + std::to_wstring(parsed) + L" at mouse position.");
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

bool RevealCurrentSceneAroundPlayer() {
    if (g_currentScenePath.empty() || g_game.scene.mapWidth <= 0 || g_game.scene.mapHeight <= 0) return false;
    const std::wstring sceneKey = g_currentScenePath.filename().wstring();
    std::vector<std::uint8_t>& explored = g_game.exploredTilesByScene[sceneKey];
    const int tileCount = g_game.scene.mapWidth * g_game.scene.mapHeight;
    bool changed = false;
    if (static_cast<int>(explored.size()) != tileCount) {
        explored.assign(tileCount, 0);
        changed = true;
    }
    const int centerX = std::clamp(static_cast<int>(g_game.player.pos.x / kTileSize), 0, g_game.scene.mapWidth - 1);
    const int centerY = std::clamp(static_cast<int>(g_game.player.pos.y / kTileSize), 0, g_game.scene.mapHeight - 1);
    constexpr int kRevealRadius = 3;
    for (int ty = std::max(0, centerY - kRevealRadius); ty <= std::min(g_game.scene.mapHeight - 1, centerY + kRevealRadius); ++ty) {
        for (int tx = std::max(0, centerX - kRevealRadius); tx <= std::min(g_game.scene.mapWidth - 1, centerX + kRevealRadius); ++tx) {
            const int dx = tx - centerX;
            const int dy = ty - centerY;
            const int index = ty * g_game.scene.mapWidth + tx;
            if (dx * dx + dy * dy <= kRevealRadius * kRevealRadius && explored[index] == 0) {
                explored[index] = 1;
                changed = true;
            }
        }
    }
    if (changed) g_game.explorationDirty = true;
    return changed;
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
    g_activeSave.unlockedBlueprints.assign(g_game.unlockedBlueprints.begin(), g_game.unlockedBlueprints.end());
    g_activeSave.territoryLevel = g_game.territoryLevel;
    g_activeSave.territoryStability = g_game.territoryStability;
    g_activeSave.territoryDayProgress = g_game.territoryDayProgress;
    g_activeSave.territoryDaysPassed = g_game.territoryDaysPassed;
    g_activeSave.exploredMaps.clear();
    for (const auto& [mapName, tiles] : g_game.exploredTilesByScene) {
        if (tiles.empty()) continue;
        g_activeSave.exploredMaps.push_back({mapName, static_cast<int>(tiles.size()), tiles});
    }
    g_activeSave.followerNpcIndices.clear();
    g_activeSave.residents.clear();
    for (int i = 0; i < static_cast<int>(g_game.npcs.size()); ++i) {
        const Npc& npc = g_game.npcs[i];
        if (npc.following) g_activeSave.followerNpcIndices.push_back(i);
        int stoneMask = 0;
        for (int stone = 0; stone < 3; ++stone) if (npc.spiritStones[stone]) stoneMask |= 1 << stone;
        rpg::SaveGameInfo::ResidentEntry saved;
        saved.name = npc.name;
        saved.x = npc.pos.x;
        saved.y = npc.pos.y;
        saved.spiritStoneMask = stoneMask;
        saved.following = npc.following;
        saved.health = npc.health;
        saved.affinity = npc.affinity;
        saved.personality = npc.personality;
        saved.taskMode = static_cast<int>(npc.taskMode);
        saved.gatheringTarget = npc.gatheringTarget;
        saved.facilityId = npc.facilityId;
        saved.workMap = npc.workMap;
        for (int slot = 0; slot < static_cast<int>(npc.cargo.size()); ++slot) {
            if (!npc.cargo[slot].id.empty() && npc.cargo[slot].count > 0) {
                saved.cargo.push_back({slot, npc.cargo[slot].id, npc.cargo[slot].count});
            }
        }
        g_activeSave.residents.push_back(std::move(saved));
    }
    if (rpg::SaveGameState(g_activeSaveDirectory, g_activeSave)) {
        g_game.explorationDirty = false;
        g_game.explorationSaveTimer = 0.0f;
    }
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
    if (const rpg::WorldLayerDef* layer = rpg::FindWorldLayer(scenePath);
        layer && rpg::EnsureWorldLayerLandmarks(g_game.scene, *layer)) {
        rpg::SaveSceneToFile(scenePath, g_game.scene);
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
    g_game.unlockedBlueprints.clear();
    g_game.unlockedBlueprints.insert(g_activeSave.unlockedBlueprints.begin(), g_activeSave.unlockedBlueprints.end());
    g_game.unlockedBlueprints.insert("territory_anchor");
    g_game.territoryLevel = g_activeSave.territoryLevel;
    g_game.territoryStability = g_activeSave.territoryStability;
    g_game.territoryDayProgress = g_activeSave.territoryDayProgress;
    g_game.territoryDaysPassed = g_activeSave.territoryDaysPassed;
    g_game.exploredTilesByScene.clear();
    for (const rpg::SaveGameInfo::ExploredMapEntry& explored : g_activeSave.exploredMaps) {
        if (explored.tileCount > 0 && explored.tileCount == static_cast<int>(explored.tiles.size())) {
            g_game.exploredTilesByScene[explored.mapName] = explored.tiles;
        }
    }
    g_game.explorationDirty = false;
    g_game.explorationSaveTimer = 0.0f;
    g_game.npcs = MakeDefaultNpcs();
    for (const rpg::SaveGameInfo::InventoryEntry& entry : g_activeSave.inventory) {
        if (entry.slot >= 0 && entry.slot < static_cast<int>(g_game.inventory.slots.size())) {
            g_game.inventory.slots[entry.slot] = MakeItemStack(entry.itemId, entry.count);
        } else if (entry.slot >= kInventorySlotCount && entry.slot < kInventorySlotCount + kSpiritSlotCount) {
            const int spiritIndex = entry.slot - kInventorySlotCount;
            if (SpiritSkillIndex(entry.itemId) == spiritIndex) {
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
            if (IsLegacyDefaultNpcName(saved.name)) continue;
            Npc npc;
            npc.pos = {saved.x, saved.y};
            npc.name = saved.name;
            npc.text = L"领地让影子拥有了可以触碰的现实。";
            npc.following = saved.following;
            npc.health = saved.health;
            npc.affinity = saved.affinity;
            npc.personality = saved.personality;
            npc.taskMode = static_cast<NpcTaskMode>(std::clamp(saved.taskMode, 0, 3));
            npc.gatheringTarget = saved.gatheringTarget.empty() ? "any" : saved.gatheringTarget;
            npc.facilityId = saved.facilityId;
            npc.workMap = saved.workMap.empty() ? g_currentScenePath.filename().wstring() : saved.workMap;
            for (const rpg::SaveGameInfo::InventoryEntry& cargo : saved.cargo) {
                if (cargo.slot >= 0 && cargo.slot < static_cast<int>(npc.cargo.size())) {
                    npc.cargo[cargo.slot] = {cargo.itemId, cargo.count};
                }
            }
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
        npc.evading = false;
        npc.threatMonster = -1;
        npc.evadeTime = 0.0f;
        npc.moving = false;
    }
    for (const int index : g_activeSave.followerNpcIndices) {
        if (index >= 0 && index < static_cast<int>(g_game.npcs.size())) {
            g_game.npcs[index].following = true;
            g_game.npcs[index].taskMode = NpcTaskMode::Idle;
        }
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
    g_game.playerHealth = 100;
    g_game.deathPhase = DeathPhase::None;
    g_game.deathTime = 0.0f;
    g_game.wanderingShadows.clear();
    g_game.shadowDialogIndex = -1;
    RevealCurrentSceneAroundPlayer();
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
    if (const rpg::WorldLayerDef* layer = rpg::FindWorldLayer(path);
        layer && rpg::EnsureWorldLayerLandmarks(nextScene, *layer)) {
        rpg::SaveSceneToFile(path, nextScene);
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
        npc.evading = false;
        npc.threatMonster = -1;
        npc.evadeTime = 0.0f;
        npc.moving = false;
        npc.velocity = {};
        if (!npc.following) continue;
        npc.workMap = path.filename().wstring();
        npc.taskMode = NpcTaskMode::Idle;
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
    RevealCurrentSceneAroundPlayer();
    ActivateScenePickups(path);
    SaveCurrentGame();
    return true;
}

void CenterCameraAt(Vec2 position) {
    const float renderScale = RenderScale();
    const float worldW = rpg::SceneWorldWidth(g_game.scene) * renderScale;
    const float worldH = rpg::SceneWorldHeight(g_game.scene) * renderScale;
    g_game.camera.x = Clamp(position.x * renderScale - kWindowWidth * 0.5f, 0.0f,
                            std::max(0.0f, worldW - kWindowWidth));
    g_game.camera.y = Clamp(position.y * renderScale - kWindowHeight * 0.5f, 0.0f,
                            std::max(0.0f, worldH - kWindowHeight));
}

bool LoadSceneForDeath(const std::filesystem::path& path) {
    rpg::Scene nextScene;
    std::string error;
    if (!rpg::LoadSceneFromFile(path, nextScene, &error)) return false;
    if (const rpg::WorldLayerDef* layer = rpg::FindWorldLayer(path);
        layer && rpg::EnsureWorldLayerLandmarks(nextScene, *layer)) {
        rpg::SaveSceneToFile(path, nextScene);
    }
    if (!g_currentScenePath.empty()) rpg::SaveSceneToFile(g_currentScenePath, g_game.scene);
    ReleaseBackgroundResources();
    g_game.scene = std::move(nextScene);
    g_game.wanderingShadows.clear();
    g_game.shadowDialogIndex = -1;
    g_game.monsters.clear();
    g_game.monsterSceneKey.clear();
    g_game.activeCombatMonster = -1;
    for (Npc& npc : g_game.npcs) {
        npc.inCombat = false;
        npc.evading = false;
        npc.threatMonster = -1;
        npc.evadeTime = 0.0f;
    }
    rpg::InvalidateTerritoryRenderCache();
    g_currentScenePath = path;
    ActivateScenePickups(path);
    return true;
}

bool ReturnViewToRealityAnchor() {
    if (g_activeSaveDirectory.empty()) return false;
    const std::filesystem::path maps = g_activeSaveDirectory / L"maps";
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(maps, ec)) {
        if (ec || !entry.is_regular_file() || entry.path().extension() != L".json") continue;
        rpg::Scene scene;
        if (!rpg::LoadSceneFromFile(entry.path(), scene)) continue;
        const auto anchor = std::find_if(scene.objects.begin(), scene.objects.end(), [](const rpg::SceneObject& object) {
            return object.type == "territory_anchor";
        });
        if (anchor == scene.objects.end()) continue;
        if (entry.path() != g_currentScenePath && !LoadSceneForDeath(entry.path())) return false;
        const auto loadedAnchor = std::find_if(g_game.scene.objects.begin(), g_game.scene.objects.end(), [](const rpg::SceneObject& object) {
            return object.type == "territory_anchor";
        });
        if (loadedAnchor != g_game.scene.objects.end()) CenterCameraAt({loadedAnchor->pos.x, loadedAnchor->pos.y});
        return true;
    }
    return false;
}

void BeginDeathSequence() {
    g_game.deathPhase = g_game.npcs.empty() ? DeathPhase::FinalEnding : DeathPhase::CompensationIntro;
    g_game.deathTime = 0.0f;
    g_game.player.vel = {};
    g_game.player.attacking = false;
    g_game.player.attackTime = 0.0f;
    g_game.up = g_game.down = g_game.left = g_game.right = false;
    g_game.interact = false;
    g_game.inventory.open = false;
    g_game.inventory.dragging = false;
    g_game.anchorPanelOpen = false;
    g_game.mapOpen = false;
    g_game.showTalk = false;
    g_game.shadowDialogIndex = -1;
    g_game.npcContextIndex = -1;
    g_console.open = false;
    ReleaseCapture();
}

void FinishTerminalDeath() {
    std::string error;
    const std::filesystem::path deletedSave = g_activeSaveDirectory;
    const bool deleted = !deletedSave.empty() && rpg::DeleteSaveGame(deletedSave, &error);
    const LARGE_INTEGER frequency = g_game.freq;
    const LARGE_INTEGER lastTick = g_game.lastTick;
    ReleaseBackgroundResources();
    g_game = Game{};
    g_game.freq = frequency;
    g_game.lastTick = lastTick;
    g_console = DebugConsole{};
    g_activeSaveDirectory.clear();
    g_currentScenePath.clear();
    g_activeSave = rpg::SaveGameInfo{};
    g_saveList = rpg::ListSaveGames(rpg::DefaultSavesRoot());
    g_menuStatus = deleted ? L"轮回已经终结，存档已删除" : L"轮回已经终结，但存档删除失败";
    g_screen = AppScreen::MainMenu;
}

bool UpdateDeathSequence(float dt) {
    if (g_game.deathPhase == DeathPhase::None) {
        if (g_game.playerHealth > 0) return false;
        BeginDeathSequence();
    }
    g_game.player.vel = {};
    g_game.player.animTime = 0.0f;
    g_game.deathTime += dt;
    if (g_game.deathPhase == DeathPhase::CompensationIntro && g_game.deathTime >= 3.0f) {
        ReturnViewToRealityAnchor();
        g_game.deathPhase = DeathPhase::AwaitingSacrifice;
        g_game.deathTime = 0.0f;
    } else if (g_game.deathPhase == DeathPhase::RevivalFade && g_game.deathTime >= 1.0f) {
        g_game.deathPhase = DeathPhase::None;
        g_game.deathTime = 0.0f;
    } else if (g_game.deathPhase == DeathPhase::FinalEnding && g_game.deathTime >= 4.5f) {
        FinishTerminalDeath();
    }
    return true;
}

bool SacrificeNpc(int index) {
    if (g_game.deathPhase != DeathPhase::AwaitingSacrifice || index < 0 ||
        index >= static_cast<int>(g_game.npcs.size())) return false;
    const Npc sacrificed = g_game.npcs[index];
    const std::wstring mapName = sacrificed.workMap.empty() ? g_currentScenePath.filename().wstring() : sacrificed.workMap;
    const std::filesystem::path destinationMap = g_activeSaveDirectory / L"maps" / mapName;
    if (destinationMap != g_currentScenePath && !LoadSceneForDeath(destinationMap)) {
        g_game.pickupNotice = L"无法读取被献祭居民所在的地图";
        g_game.pickupNoticeTime = 2.0f;
        return false;
    }
    g_game.npcs.erase(g_game.npcs.begin() + index);
    g_game.anchorSelectedNpc = -1;
    g_game.nearbyNpc = -1;
    g_game.npcContextIndex = -1;
    g_game.activeCombatMonster = -1;
    g_game.player.pos = sacrificed.pos;
    g_game.playerHealth = 100;
    g_game.player.animTime = 0.0f;
    CenterCameraAt(g_game.player.pos);
    RevealCurrentSceneAroundPlayer();
    g_game.deathPhase = DeathPhase::RevivalFade;
    g_game.deathTime = 0.0f;
    g_game.pickupNotice = sacrificed.name + L" 已代替锚点消散";
    g_game.pickupNoticeTime = 2.5f;
    if (!g_currentScenePath.empty()) rpg::SaveSceneToFile(g_currentScenePath, g_game.scene);
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

int TerritoryAreaLimit() {
    constexpr int limits[] = {225, 625, 1225, 2025};
    return limits[std::clamp(g_game.territoryLevel, 1, 4) - 1];
}

int TerritoryTileCount() {
    return static_cast<int>(std::count_if(g_game.scene.territory.begin(), g_game.scene.territory.end(),
        [](std::uint8_t claimed) { return claimed != 0; }));
}

int RealityCapacityLimit() {
    constexpr int limits[] = {40, 70, 110, 160};
    return limits[std::clamp(g_game.territoryLevel, 1, 4) - 1];
}

bool RealityAnchorExists() {
    return std::any_of(g_game.scene.objects.begin(), g_game.scene.objects.end(), [](const rpg::SceneObject& object) {
        return object.type == "territory_anchor";
    });
}

int PendingResidentCount() {
    return static_cast<int>(std::count_if(g_game.wanderingShadows.begin(), g_game.wanderingShadows.end(),
        [](const WanderingShadow& shadow) { return shadow.state == WandererState::Accepted; }));
}

POINT ObjectFootprintOrigin(const rpg::SceneObject& object) {
    const rpg::SceneObjectDef* def = rpg::FindObjectDef(object.type);
    const int width = def ? def->footprintWidth : 1;
    const int height = def ? def->footprintHeight : 1;
    return {
        static_cast<LONG>(std::lround(object.pos.x / kTileSize - width * 0.5f)),
        static_cast<LONG>(std::lround(object.pos.y / kTileSize - height)),
    };
}

bool ObjectInsideTerritory(const rpg::SceneObject& object) {
    const POINT tile = ObjectFootprintOrigin(object);
    return rpg::TerritoryAt(g_game.scene, tile.x, tile.y);
}

int RealityLoad() {
    int load = (static_cast<int>(g_game.npcs.size()) + PendingResidentCount()) * kResidentRealityLoad;
    if (RealityAnchorExists()) load += kWorkbenchRealityLoad;
    for (const rpg::SceneObject& object : g_game.scene.objects) {
        if (object.type == "teleport_point" && ObjectInsideTerritory(object)) {
            load += kTeleportRealityLoad;
        }
    }
    return load;
}

bool CanAcceptWanderingShadow(std::wstring* reason = nullptr) {
    if (!RealityAnchorExists()) {
        if (reason) *reason = L"需要先建立现实锚点";
        return false;
    }
    if (g_game.territoryStability < kMinimumRecruitmentStability) {
        if (reason) *reason = L"领地稳定度低于 25，暂时无法收留居民";
        return false;
    }
    if (RealityLoad() + kResidentRealityLoad > RealityCapacityLimit()) {
        if (reason) *reason = L"现实承载力不足，无法固化新的居民";
        return false;
    }
    return true;
}

int ItemFoodValue(const ItemStack& item) {
    if (item.id.empty() || item.count <= 0) return 0;
    const rpg::ItemDef* def = rpg::FindItemDef(item.id);
    if (!def) return 0;
    const auto property = def->properties.find("food");
    return property == def->properties.end() ? 0 : std::max(0, static_cast<int>(std::lround(property->second)));
}

int StoredFoodPoints() {
    int total = 0;
    for (const ItemStack& item : g_game.anchorStorage) total += ItemFoodValue(item) * item.count;
    return total;
}

int ConsumeAnchorFood(int required) {
    int supplied = 0;
    for (ItemStack& item : g_game.anchorStorage) {
        const int value = ItemFoodValue(item);
        while (value > 0 && item.count > 0 && supplied < required) {
            --item.count;
            supplied += value;
        }
        if (item.count == 0) item = {};
        if (supplied >= required) break;
    }
    return std::min(supplied, required);
}

int ItemPropertyInt(std::string_view itemId, std::string_view property, int fallback) {
    const rpg::ItemDef* def = rpg::FindItemDef(itemId);
    if (!def) return fallback;
    const auto value = def->properties.find(std::string(property));
    return value == def->properties.end() ? fallback : static_cast<int>(std::lround(value->second));
}

std::string CropIdForSeed(std::string_view seedId) {
    if (ItemPropertyInt(seedId, "seed", 0) <= 0 || seedId.size() <= 5 || seedId.substr(seedId.size() - 5) != "_seed") {
        return {};
    }
    return std::string(seedId.substr(0, seedId.size() - 5));
}

std::string SeedIdForCrop(std::string_view cropId) {
    const std::string seedId = std::string(cropId) + "_seed";
    return CropIdForSeed(seedId).empty() ? std::string{} : seedId;
}

int CropGrowthDays(std::string_view cropId) {
    const std::string seedId = SeedIdForCrop(cropId);
    return seedId.empty() ? 2 : std::max(1, ItemPropertyInt(seedId, "crop_days", 2));
}

int CropHarvestCount(std::string_view cropId) {
    const std::string seedId = SeedIdForCrop(cropId);
    return seedId.empty() ? 1 : std::max(1, ItemPropertyInt(seedId, "harvest_count", 1));
}

int AdvanceCropGrowth() {
    int matured = 0;
    for (rpg::SceneObject& object : g_game.scene.objects) {
        if (object.type != "farmland" || object.cropId.empty()) continue;
        const int requiredDays = CropGrowthDays(object.cropId);
        if (object.cropGrowthDays >= requiredDays) continue;
        ++object.cropGrowthDays;
        if (object.cropGrowthDays >= requiredDays) ++matured;
    }
    return matured;
}

void SettleTerritoryDay() {
    const int demand = static_cast<int>(g_game.npcs.size());
    const int supplied = ConsumeAnchorFood(demand);
    const int missing = demand - supplied;
    const int overload = std::max(0, RealityLoad() - RealityCapacityLimit());
    float change = missing == 0 ? 2.0f : -(8.0f + missing * 4.0f);
    if (overload > 0) change -= 10.0f + overload * 0.5f;
    g_game.territoryStability = Clamp(g_game.territoryStability + change, 0.0f, 100.0f);
    ++g_game.territoryDaysPassed;
    const int maturedCrops = AdvanceCropGrowth();

    if (missing > 0) {
        g_game.pickupNotice = L"领地缺粮：" + std::to_wstring(missing) + L" 名居民未获得食物，稳定度下降";
        g_game.pickupNoticeTime = 3.0f;
    } else if (overload > 0) {
        g_game.pickupNotice = L"现实承载超限，稳定度正在下降";
        g_game.pickupNoticeTime = 3.0f;
    } else if (maturedCrops > 0) {
        g_game.pickupNotice = std::to_wstring(maturedCrops) + L" 块耕地的作物已经成熟";
        g_game.pickupNoticeTime = 3.0f;
    }
    SaveCurrentScene();
}

void UpdateTerritorySystems(float dt) {
    if (!RealityAnchorExists()) return;
    g_game.territoryDayProgress += dt / kGameDaySeconds;
    while (g_game.territoryDayProgress >= 1.0f) {
        g_game.territoryDayProgress -= 1.0f;
        SettleTerritoryDay();
    }
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
            g_game.npcs.back().workMap = g_currentScenePath.filename().wstring();
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

const char* GatheringDropForObject(const rpg::SceneObject& object);
void SaveCurrentScene();

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
        if (!NpcInCurrentScene(g_game.npcs[i])) continue;
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

Vec2 ClampToFollowerLeash(Vec2 target) {
    Vec2 offset{target.x - g_game.player.pos.x, target.y - g_game.player.pos.y};
    const float distance = std::sqrt(offset.x * offset.x + offset.y * offset.y);
    constexpr float maximumCenterDistance = kNpcCombatLeashDistance - kNpcRadius;
    if (distance <= maximumCenterDistance || distance < 0.001f) return target;
    const float scale = maximumCenterDistance / distance;
    return {g_game.player.pos.x + offset.x * scale, g_game.player.pos.y + offset.y * scale};
}

void MoveFollowingNpcToward(int npcIndex, Vec2 target, float speed, float dt) {
    MoveNpcToward(npcIndex, ClampToFollowerLeash(target), speed, dt);
}

Vec2 FollowerEvadeTarget(const Npc& npc, const Monster& monster) {
    Vec2 away = Normalize({npc.pos.x - monster.pos.x, npc.pos.y - monster.pos.y});
    if (std::fabs(away.x) + std::fabs(away.y) < 0.001f) away = Normalize({npc.pos.x - g_game.player.pos.x, npc.pos.y - g_game.player.pos.y});
    if (std::fabs(away.x) + std::fabs(away.y) < 0.001f) away = {1.0f, 0.0f};
    Vec2 target = ClampToFollowerLeash({npc.pos.x + away.x * 180.0f, npc.pos.y + away.y * 180.0f});
    if (Distance(target, npc.pos) >= 24.0f) return target;

    const Vec2 radial = Normalize({npc.pos.x - g_game.player.pos.x, npc.pos.y - g_game.player.pos.y});
    const Vec2 tangent{-radial.y, radial.x};
    const Vec2 clockwise = ClampToFollowerLeash({npc.pos.x + tangent.x * 180.0f, npc.pos.y + tangent.y * 180.0f});
    const Vec2 counterClockwise = ClampToFollowerLeash({npc.pos.x - tangent.x * 180.0f, npc.pos.y - tangent.y * 180.0f});
    return Distance(clockwise, monster.pos) >= Distance(counterClockwise, monster.pos) ? clockwise : counterClockwise;
}

bool NpcLowHealth(const Npc& npc) {
    return npc.health <= kNpcRetreatHealth;
}

bool ActiveMonster(int index) {
    return index >= 0 && index < static_cast<int>(g_game.monsters.size()) && g_game.monsters[index].alive;
}

int NearestMonster(Vec2 position, float maximumDistance) {
    int target = -1;
    float nearest = maximumDistance;
    for (int i = 0; i < static_cast<int>(g_game.monsters.size()); ++i) {
        if (!g_game.monsters[i].alive) continue;
        const float distance = Distance(position, g_game.monsters[i].pos);
        if (distance < nearest) {
            nearest = distance;
            target = i;
        }
    }
    return target;
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
        if (npc.following && npc.spiritStones[2] && !npc.shadowForm && !NpcLowHealth(npc)) {
            npc.inCombat = true;
            npc.evading = false;
            npc.threatMonster = monsterIndex;
        }
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
            if (!NpcInCurrentScene(npc) || ActorProtectedByTerritory(npc.pos) || npc.shadowForm || !NpcHasAnyStone(npc)) continue;
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
                npc.threatMonster = i;
                npc.evadeTime = 3.0f;
                g_game.pickupNotice = L"畸变暗影击中了 " + npc.name;
                if (npc.health == 0) {
                    g_game.pickupNotice = npc.name + L" 被畸变暗影吞噬了";
                    g_game.npcs.erase(g_game.npcs.begin() + targetNpc);
                    g_game.npcContextIndex = -1;
                    g_game.nearbyNpc = -1;
                    g_game.anchorSelectedNpc = -1;
                    SaveCurrentGame();
                } else if (npc.taskMode == NpcTaskMode::Gathering && NpcLowHealth(npc)) {
                    npc.taskMode = NpcTaskMode::Returning;
                    npc.gatheringObjectId.clear();
                    npc.inCombat = false;
                    npc.evading = false;
                    g_game.pickupNotice = npc.name + L" 生命过低，正在返回领地";
                } else if (npc.following) {
                    g_game.activeCombatMonster = i;
                    npc.inCombat = npc.spiritStones[2] && !NpcLowHealth(npc);
                    npc.evading = !npc.inCombat;
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

int NpcCargoUsedSlots(const Npc& npc) {
    return static_cast<int>(std::count_if(npc.cargo.begin(), npc.cargo.end(), [](const NpcCargoStack& stack) {
        return !stack.id.empty() && stack.count > 0;
    }));
}

bool StoreNpcCargo(Npc& npc, std::string_view id, int count) {
    const rpg::ItemDef* def = rpg::FindItemDef(id);
    const int maximum = def ? def->maxStack : 1;
    for (NpcCargoStack& stack : npc.cargo) {
        if (stack.id != id || stack.count >= maximum) continue;
        const int moved = std::min(count, maximum - stack.count);
        stack.count += moved;
        count -= moved;
        if (count == 0) return true;
    }
    for (NpcCargoStack& stack : npc.cargo) {
        if (!stack.id.empty()) continue;
        const int moved = std::min(count, maximum);
        stack = {std::string(id), moved};
        count -= moved;
        if (count == 0) return true;
    }
    return false;
}

bool NpcCargoCanStore(const Npc& npc, std::string_view id, int count) {
    Npc copy = npc;
    return StoreNpcCargo(copy, id, count);
}

bool StoreAnchorItem(ItemStack& incoming) {
    const rpg::ItemDef* def = rpg::FindItemDef(incoming.id);
    const int maximum = def ? def->maxStack : 1;
    for (ItemStack& stack : g_game.anchorStorage) {
        if (stack.id != incoming.id || stack.count >= maximum) continue;
        const int moved = std::min(incoming.count, maximum - stack.count);
        stack.count += moved;
        incoming.count -= moved;
        if (incoming.count == 0) return true;
    }
    for (ItemStack& stack : g_game.anchorStorage) {
        if (!stack.id.empty()) continue;
        const int moved = std::min(incoming.count, maximum);
        stack = incoming;
        stack.count = moved;
        incoming.count -= moved;
        if (incoming.count == 0) return true;
    }
    return false;
}

int AnchorStoredItemCount(std::string_view id) {
    int count = 0;
    for (const ItemStack& item : g_game.anchorStorage) if (item.id == id) count += item.count;
    return count;
}

bool AnchorCanStore(std::string_view id, int count) {
    const rpg::ItemDef* def = rpg::FindItemDef(id);
    if (!def) return false;
    int capacity = 0;
    for (const ItemStack& item : g_game.anchorStorage) {
        if (item.id.empty()) capacity += def->maxStack;
        else if (item.id == id) capacity += std::max(0, def->maxStack - item.count);
        if (capacity >= count) return true;
    }
    return false;
}

void ConsumeAnchorStoredItem(std::string_view id, int count) {
    for (ItemStack& item : g_game.anchorStorage) {
        if (count <= 0) break;
        if (item.id != id || item.count <= 0) continue;
        const int removed = std::min(count, item.count);
        item.count -= removed;
        count -= removed;
        if (item.count <= 0) item = {};
    }
}

bool RunNpcFurnace(std::wstring& result) {
    struct SmeltRecipe { const char* ore; const char* ingot; };
    constexpr SmeltRecipe recipes[] = {
        {"iron_ore", "iron_ingot"}, {"copper_ore", "copper_ingot"},
        {"silver_ore", "silver_ingot"}, {"gold_ore", "gold_ingot"},
    };
    if (AnchorStoredItemCount("wood") < 1) return false;
    for (const SmeltRecipe& recipe : recipes) {
        if (AnchorStoredItemCount(recipe.ore) < 2 || !AnchorCanStore(recipe.ingot, 1)) continue;
        ConsumeAnchorStoredItem(recipe.ore, 2);
        ConsumeAnchorStoredItem("wood", 1);
        ItemStack output = MakeItemStack(recipe.ingot, 1);
        StoreAnchorItem(output);
        const rpg::ItemDef* def = rpg::FindItemDef(recipe.ingot);
        result = L"完成熔炼：" + (def ? def->displayName : L"矿锭");
        return true;
    }
    return false;
}

bool RunNpcSawmill(std::wstring& result) {
    struct SawRecipe { const char* wood; const char* plank; };
    constexpr SawRecipe recipes[] = {{"exotic_wood", "exotic_plank"}, {"wood", "plank"}};
    for (const SawRecipe& recipe : recipes) {
        if (AnchorStoredItemCount(recipe.wood) < 1 || !AnchorCanStore(recipe.plank, 2)) continue;
        ConsumeAnchorStoredItem(recipe.wood, 1);
        ItemStack output = MakeItemStack(recipe.plank, 2);
        StoreAnchorItem(output);
        const rpg::ItemDef* def = rpg::FindItemDef(recipe.plank);
        result = L"完成加工：" + (def ? def->displayName : L"木板") + L" x2";
        return true;
    }
    return false;
}

bool RunNpcFarm(std::wstring& result) {
    for (rpg::SceneObject& farmland : g_game.scene.objects) {
        if (farmland.type != "farmland" || farmland.cropId.empty() ||
            farmland.cropGrowthDays < CropGrowthDays(farmland.cropId)) continue;
        const int count = CropHarvestCount(farmland.cropId);
        if (!AnchorCanStore(farmland.cropId, count)) continue;
        ItemStack output = MakeItemStack(farmland.cropId, count);
        const std::wstring cropName = output.displayName;
        StoreAnchorItem(output);
        farmland.cropId.clear();
        farmland.cropGrowthDays = 0;
        result = L"收获：" + cropName + L" x" + std::to_wstring(count);
        return true;
    }
    constexpr const char* seedIds[] = {
        "rice_seed", "corn_seed", "potato_seed", "sweet_potato_seed", "cabbage_seed",
    };
    for (rpg::SceneObject& farmland : g_game.scene.objects) {
        if (farmland.type != "farmland" || !farmland.cropId.empty()) continue;
        for (const char* seedId : seedIds) {
            if (AnchorStoredItemCount(seedId) <= 0) continue;
            ConsumeAnchorStoredItem(seedId, 1);
            farmland.cropId = CropIdForSeed(seedId);
            farmland.cropGrowthDays = 0;
            const rpg::ItemDef* def = rpg::FindItemDef(seedId);
            result = L"完成播种：" + (def ? def->displayName : L"种子");
            return true;
        }
    }
    return false;
}

bool UnloadNpcCargo(Npc& npc) {
    bool emptied = true;
    for (NpcCargoStack& cargo : npc.cargo) {
        if (cargo.id.empty() || cargo.count <= 0) continue;
        ItemStack incoming = MakeItemStack(cargo.id, cargo.count);
        StoreAnchorItem(incoming);
        cargo.count = incoming.count;
        if (cargo.count <= 0) cargo = {};
        else emptied = false;
    }
    return emptied;
}

bool WorldPositionExplored(Vec2 position) {
    const std::wstring key = g_currentScenePath.filename().wstring();
    const auto found = g_game.exploredTilesByScene.find(key);
    if (found == g_game.exploredTilesByScene.end()) return false;
    const int tx = static_cast<int>(position.x / kTileSize);
    const int ty = static_cast<int>(position.y / kTileSize);
    const int index = ty * g_game.scene.mapWidth + tx;
    return tx >= 0 && ty >= 0 && tx < g_game.scene.mapWidth && ty < g_game.scene.mapHeight &&
           index >= 0 && index < static_cast<int>(found->second.size()) && found->second[index] != 0;
}

bool NpcGatheringObjectMatches(const Npc& npc, const rpg::SceneObject& object) {
    const char* drop = GatheringDropForObject(object);
    return drop && (npc.gatheringTarget == "any" || npc.gatheringTarget == drop) &&
           !ActorProtectedByTerritory({object.pos.x, object.pos.y}) &&
           WorldPositionExplored({object.pos.x, object.pos.y});
}

int FindNpcGatheringObject(const Npc& npc) {
    int best = -1;
    float nearest = std::numeric_limits<float>::max();
    for (int i = 0; i < static_cast<int>(g_game.scene.objects.size()); ++i) {
        const rpg::SceneObject& object = g_game.scene.objects[i];
        if (!NpcGatheringObjectMatches(npc, object)) continue;
        const float distance = Distance(npc.pos, {object.pos.x, object.pos.y});
        if (distance < nearest) {
            nearest = distance;
            best = i;
        }
    }
    return best;
}

bool UpdateNpcWorkerCombat(int npcIndex, float dt) {
    Npc& npc = g_game.npcs[npcIndex];
    if (!npc.spiritStones[2] || NpcLowHealth(npc)) return false;
    const int target = NearestMonster(npc.pos, 150.0f);
    if (target < 0) return false;
    Monster& monster = g_game.monsters[target];
    const float nearest = Distance(npc.pos, monster.pos);
    npc.inCombat = true;
    npc.threatMonster = target;
    if (nearest > 58.0f) {
        MoveNpcToward(npcIndex, monster.pos, 150.0f, dt, false);
    } else if (npc.attackCooldown <= 0.0f) {
        monster.health -= 12;
        KnockbackMonster(monster, npc.pos, 42.0f);
        npc.attackCooldown = 0.65f;
        if (monster.health <= 0) {
            monster.health = 0;
            monster.alive = false;
            SpawnMonsterLoot(monster.pos);
        }
    }
    return true;
}

bool UpdateNpcTask(int npcIndex, float dt) {
    Npc& npc = g_game.npcs[npcIndex];
    if (npc.taskMode == NpcTaskMode::Idle) return false;
    if (!npc.workMap.empty() && npc.workMap != g_currentScenePath.filename().wstring()) return true;
    npc.following = false;

    if (npc.taskMode == NpcTaskMode::Returning) {
        if (!ActorProtectedByTerritory(npc.pos)) {
            MoveNpcToward(npcIndex, NearestTerritoryPosition(npc.pos), 96.0f, dt, false);
            return true;
        }
        if (UnloadNpcCargo(npc)) {
            npc.taskMode = NpcTaskMode::Idle;
            npc.returningHome = false;
            npc.gatheringObjectId.clear();
            g_game.pickupNotice = npc.name + L" 已回到领地并卸下物资";
            g_game.pickupNoticeTime = 2.0f;
            SaveCurrentGame();
        } else {
            npc.workTimer += dt;
            if (npc.workTimer >= 2.0f) {
                npc.workTimer = 0.0f;
                g_game.pickupNotice = L"锚点仓库已满，" + npc.name + L" 无法卸货";
                g_game.pickupNoticeTime = 1.5f;
            }
        }
        return true;
    }

    if (npc.taskMode == NpcTaskMode::Facility) {
        const auto facility = std::find_if(g_game.scene.objects.begin(), g_game.scene.objects.end(), [&](const rpg::SceneObject& object) {
            return object.id == npc.facilityId && ObjectInsideTerritory(object);
        });
        if (facility == g_game.scene.objects.end()) {
            npc.taskMode = NpcTaskMode::Idle;
            npc.facilityId.clear();
            return true;
        }
        const Vec2 position{facility->pos.x, facility->pos.y};
        if (Distance(npc.pos, position) > 70.0f) {
            MoveNpcToward(npcIndex, position, 72.0f, dt, false);
        } else {
            npc.workTimer += dt;
            if (npc.workTimer >= 5.0f) {
                npc.workTimer = 0.0f;
                std::wstring result;
                const bool produced = facility->type == "furnace" ? RunNpcFurnace(result) :
                                      (facility->type == "sawmill" ? RunNpcSawmill(result) :
                                      (facility->type == "farmland" ? RunNpcFarm(result) : false));
                if (produced) {
                    g_game.pickupNotice = npc.name + L" " + result;
                    g_game.pickupNoticeTime = 2.0f;
                    SaveCurrentScene();
                }
            }
        }
        return true;
    }

    if (!npc.spiritStones[0]) {
        npc.taskMode = NpcTaskMode::Returning;
        npc.gatheringObjectId.clear();
        g_game.pickupNotice = npc.name + L" 缺少采集石，正在返回领地";
        g_game.pickupNoticeTime = 2.0f;
        return true;
    }
    if (NpcLowHealth(npc)) {
        npc.taskMode = NpcTaskMode::Returning;
        npc.gatheringObjectId.clear();
        npc.inCombat = false;
        npc.evading = false;
        g_game.pickupNotice = npc.name + L" 生命低于 30%，正在返回领地";
        g_game.pickupNoticeTime = 2.0f;
        return true;
    }
    if (UpdateNpcWorkerCombat(npcIndex, dt)) return true;
    npc.inCombat = false;

    int targetIndex = -1;
    if (!npc.gatheringObjectId.empty()) {
        for (int i = 0; i < static_cast<int>(g_game.scene.objects.size()); ++i) {
            if (g_game.scene.objects[i].id == npc.gatheringObjectId && NpcGatheringObjectMatches(npc, g_game.scene.objects[i])) {
                targetIndex = i;
                break;
            }
        }
    }
    if (targetIndex < 0) {
        targetIndex = FindNpcGatheringObject(npc);
        npc.gatheringObjectId = targetIndex >= 0 ? g_game.scene.objects[targetIndex].id : "";
        npc.workTimer = 0.0f;
    }
    if (targetIndex < 0) {
        npc.taskMode = NpcTaskMode::Returning;
        return true;
    }

    const rpg::SceneObject target = g_game.scene.objects[targetIndex];
    const char* dropId = GatheringDropForObject(target);
    const int count = target.type == "stone_round" ? 2 : (target.type == "exotic_tree_01" ? 5 : 3);
    if (!NpcCargoCanStore(npc, dropId, count)) {
        npc.taskMode = NpcTaskMode::Returning;
        npc.gatheringObjectId.clear();
        return true;
    }
    if (Distance(npc.pos, {target.pos.x, target.pos.y}) > 76.0f) {
        MoveNpcToward(npcIndex, {target.pos.x, target.pos.y}, 92.0f, dt, false);
        npc.workTimer = 0.0f;
        return true;
    }
    npc.workTimer += dt;
    if (npc.workTimer < 1.2f) return true;
    npc.workTimer = 0.0f;
    StoreNpcCargo(npc, dropId, count);
    g_game.scene.objects.erase(
        std::remove_if(g_game.scene.objects.begin(), g_game.scene.objects.end(), [&](const rpg::SceneObject& object) {
            return object.id == target.id || (!target.groupId.empty() && object.groupId == target.groupId);
        }),
        g_game.scene.objects.end());
    npc.gatheringObjectId.clear();
    g_game.pickupNotice = npc.name + L" 采集了 " + MakeItemStack(dropId, 1).displayName + L" x" + std::to_wstring(count);
    g_game.pickupNoticeTime = 1.2f;
    SaveCurrentScene();
    return true;
}

void UpdateNpcs(float dt) {
    int followerSlot = 0;
    for (int i = 0; i < static_cast<int>(g_game.npcs.size()); ++i) {
        Npc& npc = g_game.npcs[i];
        if (!NpcInCurrentScene(npc)) continue;
        npc.attackCooldown = std::max(0.0f, npc.attackCooldown - dt);
        npc.evadeTime = std::max(0.0f, npc.evadeTime - dt);
        npc.velocity = {};
        npc.evading = false;
        const bool insideTerritory = ActorProtectedByTerritory(npc.pos);
        npc.shadowForm = !insideTerritory && !NpcHasAnyStone(npc);
        if (npc.shadowForm) npc.inCombat = false;

        if (UpdateNpcTask(i, dt)) continue;

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

        if (!ActiveMonster(npc.threatMonster) ||
            (ActiveMonster(npc.threatMonster) && Distance(npc.pos, g_game.monsters[npc.threatMonster].pos) > 350.0f)) {
            npc.threatMonster = -1;
        }
        if (NpcLowHealth(npc) && npc.threatMonster < 0) npc.threatMonster = NearestMonster(npc.pos, 300.0f);
        const bool hasThreat = ActiveMonster(npc.threatMonster);
        const bool shouldEvade = hasThreat && (NpcLowHealth(npc) || (!npc.spiritStones[2] && npc.evadeTime > 0.0f));
        if (shouldEvade) {
            npc.inCombat = false;
            npc.evading = true;
            MoveFollowingNpcToward(i, FollowerEvadeTarget(npc, g_game.monsters[npc.threatMonster]), 185.0f, dt);
            ++followerSlot;
            continue;
        }
        npc.evading = false;

        if (npc.inCombat && npc.spiritStones[2] && !npc.shadowForm && !NpcLowHealth(npc)) {
            const int combatTarget = ActiveMonster(npc.threatMonster) ? npc.threatMonster : g_game.activeCombatMonster;
            const bool validTarget = ActiveMonster(combatTarget);
            if (!validTarget || Distance(npc.pos, g_game.player.pos) > kNpcCombatLeashDistance) {
                npc.inCombat = false;
            } else {
                npc.threatMonster = combatTarget;
                Monster& monster = g_game.monsters[combatTarget];
                const float targetDistance = Distance(npc.pos, monster.pos);
                if (targetDistance > 58.0f) {
                    MoveFollowingNpcToward(i, monster.pos, 165.0f, dt);
                } else if (npc.attackCooldown <= 0.0f) {
                    monster.health -= 12;
                    KnockbackMonster(monster, npc.pos, 42.0f);
                    npc.attackCooldown = 0.65f;
                    if (monster.health <= 0) {
                        monster.health = 0;
                        monster.alive = false;
                        SpawnMonsterLoot(monster.pos);
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
        if (npc.moving) MoveFollowingNpcToward(i, FollowerSlotPosition(followerSlot), 145.0f, dt);
        ++followerSlot;
    }
}

void UpdateNearbyNpc() {
    g_game.nearbyNpc = -1;
    for (int i = 0; i < static_cast<int>(g_game.npcs.size()); ++i) {
        if (!NpcInCurrentScene(g_game.npcs[i])) continue;
        if (Distance(g_game.player.pos, g_game.npcs[i].pos) < 72.0f) {
            g_game.nearbyNpc = i;
            return;
        }
    }
}

void UpdateGame(float dt) {
    if (UpdateDeathSequence(dt)) return;
    g_game.teleportCooldown = std::max(0.0f, g_game.teleportCooldown - dt);
    g_game.pickupNoticeTime = std::max(0.0f, g_game.pickupNoticeTime - dt);
    UpdateTerritorySystems(dt);
    if (g_game.explorationDirty) {
        g_game.explorationSaveTimer += dt;
        if (g_game.explorationSaveTimer >= 5.0f) SaveCurrentGame();
    }
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
    RevealCurrentSceneAroundPlayer();
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

void DrawTeleportMapMarker(HDC hdc, int x, int y) {
    constexpr int radius = 8;
    const POINT diamond[] = {
        {x, y - radius},
        {x + radius, y},
        {x, y + radius},
        {x - radius, y},
    };
    HBRUSH brush = CreateSolidBrush(RGB(32, 105, 119));
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(115, 235, 247));
    HGDIOBJ previousBrush = SelectObject(hdc, brush);
    HGDIOBJ previousPen = SelectObject(hdc, pen);
    Polygon(hdc, diamond, static_cast<int>(std::size(diamond)));
    SelectObject(hdc, previousPen);
    SelectObject(hdc, previousBrush);
    DeleteObject(pen);
    DeleteObject(brush);

    HBRUSH centerBrush = CreateSolidBrush(RGB(236, 255, 249));
    previousBrush = SelectObject(hdc, centerBrush);
    previousPen = SelectObject(hdc, GetStockObject(NULL_PEN));
    Ellipse(hdc, x - 2, y - 2, x + 3, y + 3);
    SelectObject(hdc, previousPen);
    SelectObject(hdc, previousBrush);
    DeleteObject(centerBrush);
}

void DrawRitualAltarMapMarker(HDC hdc, int x, int y) {
    HBRUSH brush = CreateSolidBrush(RGB(103, 55, 72));
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(245, 197, 96));
    HGDIOBJ previousBrush = SelectObject(hdc, brush);
    HGDIOBJ previousPen = SelectObject(hdc, pen);
    Ellipse(hdc, x - 8, y - 8, x + 9, y + 9);
    MoveToEx(hdc, x - 5, y, nullptr);
    LineTo(hdc, x + 6, y);
    MoveToEx(hdc, x, y - 5, nullptr);
    LineTo(hdc, x, y + 6);
    SelectObject(hdc, previousPen);
    SelectObject(hdc, previousBrush);
    DeleteObject(pen);
    DeleteObject(brush);
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
    DrawTeleportMapMarker(hdc, panel.left + 92, panel.top + 21);
    DrawTextLine(hdc, L"传送点", panel.left + 106, panel.top + 12, RGB(137, 225, 232));
    if (std::any_of(g_game.scene.objects.begin(), g_game.scene.objects.end(), [](const rpg::SceneObject& object) {
            return object.type == "ending_ritual_altar";
        })) {
        DrawRitualAltarMapMarker(hdc, panel.left + 190, panel.top + 21);
        DrawTextLine(hdc, L"仪式祭坛", panel.left + 204, panel.top + 12, RGB(238, 202, 126));
    }
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
        if (object.type == "ending_ritual_altar") {
            DrawRitualAltarMapMarker(hdc, x, y);
            continue;
        }
        if (rpg::ObjectIsTeleport(object)) {
            DrawTeleportMapMarker(hdc, x, y);
            continue;
        }
        COLORREF color = RGB(200, 174, 98);
        int radius = 3;
        if (def && def->building) { color = RGB(204, 144, 79); radius = 5; }
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

void DrawWorldFog(HDC hdc, const RECT& client) {
    const std::wstring sceneKey = g_currentScenePath.filename().wstring();
    const auto exploredIt = g_game.exploredTilesByScene.find(sceneKey);
    const std::vector<std::uint8_t>* explored = exploredIt == g_game.exploredTilesByScene.end()
        ? nullptr
        : &exploredIt->second;
    const float scale = RenderScale();
    const float tilePixels = kTileSize * scale;
    const int left = std::clamp(static_cast<int>(std::floor(g_game.camera.x / tilePixels)), 0, g_game.scene.mapWidth - 1);
    const int top = std::clamp(static_cast<int>(std::floor(g_game.camera.y / tilePixels)), 0, g_game.scene.mapHeight - 1);
    const int right = std::clamp(static_cast<int>(std::ceil((g_game.camera.x + client.right) / tilePixels)), 0, g_game.scene.mapWidth - 1);
    const int bottom = std::clamp(static_cast<int>(std::ceil((g_game.camera.y + client.bottom) / tilePixels)), 0, g_game.scene.mapHeight - 1);
    for (int ty = top; ty <= bottom; ++ty) {
        for (int tx = left; tx <= right; ++tx) {
            const int index = ty * g_game.scene.mapWidth + tx;
            const bool revealed = explored && index >= 0 && index < static_cast<int>(explored->size()) && (*explored)[index] != 0;
            if (revealed) continue;
            const RECT tile{
                static_cast<LONG>(std::floor(tx * tilePixels - g_game.camera.x)),
                static_cast<LONG>(std::floor(ty * tilePixels - g_game.camera.y)),
                static_cast<LONG>(std::ceil((tx + 1) * tilePixels - g_game.camera.x)) + 1,
                static_cast<LONG>(std::ceil((ty + 1) * tilePixels - g_game.camera.y)) + 1,
            };
            const int shade = ((tx * 17 + ty * 31) & 3) * 2;
            FillRectColor(hdc, tile, RGB(13 + shade, 17 + shade, 19 + shade));
        }
    }
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
        if (NpcInCurrentScene(npc) && !npc.shadowForm) DrawDebugCircle(hdc, npc.pos, kNpcRadius, RGB(255, 224, 82));
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
    std::wstring unavailableReason;
    const bool canAccept = CanAcceptWanderingShadow(&unavailableReason);
    FillRectColor(hdc, accept, canAccept ? RGB(54, 94, 70) : RGB(62, 66, 65));
    FillRectColor(hdc, expel, RGB(91, 58, 61));
    DrawTextLine(hdc, L"收留", accept.left + 38, accept.top + 9,
                 canAccept ? RGB(238, 244, 238) : RGB(151, 157, 153));
    DrawTextLine(hdc, L"驱逐", expel.left + 38, expel.top + 9, RGB(244, 236, 236));
    if (!canAccept) DrawTextLine(hdc, unavailableReason, dialog.left + 22, dialog.top + 74, RGB(222, 151, 134));
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

bool BuildingFootprintsOverlap(const rpg::SceneObject& a, const rpg::SceneObject& b) {
    const rpg::SceneObjectDef* aDef = rpg::FindObjectDef(a.type);
    const rpg::SceneObjectDef* bDef = rpg::FindObjectDef(b.type);
    if (!aDef || !bDef || !aDef->building || !bDef->building) return false;
    const POINT aOrigin = ObjectFootprintOrigin(a);
    const POINT bOrigin = ObjectFootprintOrigin(b);
    return aOrigin.x < bOrigin.x + bDef->footprintWidth &&
           aOrigin.x + aDef->footprintWidth > bOrigin.x &&
           aOrigin.y < bOrigin.y + bDef->footprintHeight &&
           aOrigin.y + aDef->footprintHeight > bOrigin.y;
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

bool FootprintInsideTerritory(const std::string& objectType, int tx, int ty) {
    const rpg::SceneObjectDef* def = rpg::FindObjectDef(objectType);
    if (!def) return false;
    for (int y = ty; y < ty + def->footprintHeight; ++y) {
        for (int x = tx; x < tx + def->footprintWidth; ++x) {
            if (!rpg::TerritoryAt(g_game.scene, x, y)) return false;
        }
    }
    return true;
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
    if (objectType != "teleport_point" && objectType != "territory_anchor" &&
        !FootprintInsideTerritory(objectType, tx, ty)) {
        if (reason) *reason = L"建筑必须完整放置在领地范围内";
        return false;
    }
    if (objectType == "teleport_point" && rpg::TerritoryAt(g_game.scene, tx, ty) &&
        RealityLoad() + kTeleportRealityLoad > RealityCapacityLimit()) {
        if (reason) *reason = L"现实承载力不足，无法启用新的传送点";
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
        if (BuildingFootprintsOverlap(candidate, object) || PlacementObjectsOverlap(candidate, object)) {
            if (reason) *reason = L"建筑与其他建筑碰撞";
            return false;
        }
    }
    if (PlacementActorCollision(candidate, g_game.player.pos, kPlayerRadius)) {
        if (reason) *reason = L"建筑与主角碰撞";
        return false;
    }
    for (const Npc& npc : g_game.npcs) {
    if (NpcInCurrentScene(npc) && !npc.shadowForm && PlacementActorCollision(candidate, npc.pos, kNpcRadius)) {
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
    rpg::SceneObject object;
    if (g_game.inventory.movingExistingBuilding) {
        object = g_game.inventory.movingBuilding;
        object.pos = BuildingAnchorPosition(g_game.inventory.placementObjectType, tile.x, tile.y);
    } else if (g_game.inventory.placingCraftedBuilding) {
        object = rpg::MakeObject(
            g_game.inventory.placementObjectType,
            BuildingAnchorPosition(g_game.inventory.placementObjectType, tile.x, tile.y),
            static_cast<int>(g_game.scene.objects.size()) + 10000);
        object.id = g_game.inventory.placementObjectType + "_crafted_" + std::to_string(g_game.scene.objects.size() + 1);
    } else {
        const int sourceSlot = g_game.inventory.placementSourceSlot;
        ItemStack* source = InventoryItemAt(sourceSlot);
        if (!source || source->id.empty()) return false;
        object = rpg::MakeObject(
            g_game.inventory.placementObjectType,
            BuildingAnchorPosition(g_game.inventory.placementObjectType, tile.x, tile.y),
            static_cast<int>(g_game.scene.objects.size()) + 10000);
        object.id = g_game.inventory.placementObjectType + "_" + std::to_string(object.id.size() + g_game.scene.objects.size());
        if (source->count > 1) --source->count;
        else *source = {};
    }
    g_game.scene.objects.push_back(std::move(object));
    const bool isAnchor = g_game.inventory.placementObjectType == "territory_anchor";
    if (isAnchor) {
        for (int y = tile.y - 3; y <= tile.y + 3; ++y)
            for (int x = tile.x - 3; x <= tile.x + 3; ++x) rpg::SetTerritory(g_game.scene, x, y, true);
        rpg::InvalidateTerritoryRenderCache();
    }
    g_game.inventory.placingBuilding = false;
    g_game.inventory.placingAnchor = false;
    const bool movedBuilding = g_game.inventory.movingExistingBuilding;
    const bool craftedBuilding = g_game.inventory.placingCraftedBuilding;
    g_game.inventory.movingExistingBuilding = false;
    g_game.inventory.movingBuilding = {};
    g_game.inventory.placingCraftedBuilding = false;
    g_game.inventory.reservedBuildingMaterials.clear();
    g_game.inventory.placementSourceSlot = -1;
    g_game.inventory.placementObjectType.clear();
    g_game.pickupNotice = isAnchor ? L"现实锚点已建立，生成 7x7 领地" :
        (movedBuilding ? L"建筑已移动" : L"建筑已放置");
    g_game.pickupNoticeTime = 2.0f;
    if (craftedBuilding) {
        g_game.anchorPanelOpen = true;
        g_game.anchorPanelTab = AnchorPanelTab::Crafting;
        g_game.craftingCategory = CraftingCategory::Buildings;
    }
    SaveCurrentScene();
    return true;
}

void CancelBuildingPlacement() {
    if (!g_game.inventory.placingBuilding) return;
    if (g_game.inventory.movingExistingBuilding) {
        g_game.scene.objects.push_back(std::move(g_game.inventory.movingBuilding));
    }
    const bool craftedBuilding = g_game.inventory.placingCraftedBuilding;
    if (craftedBuilding) {
        for (const ItemStack& reserved : g_game.inventory.reservedBuildingMaterials) {
            ItemStack refund = reserved;
            StoreItem(refund);
            if (refund.count > 0) StoreAnchorItem(refund);
            if (refund.count > 0) SpawnPickup(g_game.player.pos, refund.id, refund.count);
        }
    }
    g_game.inventory.placingBuilding = false;
    g_game.inventory.placingAnchor = false;
    g_game.inventory.movingExistingBuilding = false;
    g_game.inventory.movingBuilding = {};
    g_game.inventory.placingCraftedBuilding = false;
    g_game.inventory.reservedBuildingMaterials.clear();
    g_game.inventory.placementSourceSlot = -1;
    g_game.inventory.placementObjectType.clear();
    g_game.pickupNotice = craftedBuilding ? L"已取消建造，材料已退回" : L"已取消放置，物品返回原格";
    g_game.pickupNoticeTime = 1.5f;
    if (craftedBuilding) {
        g_game.anchorPanelOpen = true;
        g_game.anchorPanelTab = AnchorPanelTab::Crafting;
        g_game.craftingCategory = CraftingCategory::Buildings;
        SaveCurrentGame();
    }
}

bool BeginMovingBuildingAt(POINT tile) {
    for (int i = static_cast<int>(g_game.scene.objects.size()) - 1; i >= 0; --i) {
        const rpg::SceneObject& object = g_game.scene.objects[i];
        const rpg::SceneObjectDef* def = rpg::FindObjectDef(object.type);
        if (!def || !def->building || object.type == "territory_anchor" || object.type == "farmland") continue;
        const POINT origin = ObjectFootprintOrigin(object);
        if (tile.x < origin.x || tile.y < origin.y ||
            tile.x >= origin.x + def->footprintWidth || tile.y >= origin.y + def->footprintHeight) continue;

        g_game.inventory.movingBuilding = object;
        g_game.inventory.movingExistingBuilding = true;
        g_game.inventory.placingBuilding = true;
        g_game.inventory.placingAnchor = false;
        g_game.inventory.placementSourceSlot = -1;
        g_game.inventory.placementObjectType = object.type;
        g_game.inventory.placementTile = origin;
        g_game.inventory.placementValid = true;
        g_game.scene.objects.erase(g_game.scene.objects.begin() + i);
        g_game.pickupNotice = L"建筑已选中，移动鼠标后再次点击放下；空格取消";
        g_game.pickupNoticeTime = 2.0f;
        return true;
    }
    g_game.pickupNotice = L"这里没有可移动的建筑";
    g_game.pickupNoticeTime = 1.4f;
    return false;
}

bool TerritoryMaskConnected(const std::vector<std::uint8_t>& territory) {
    const int width = g_game.scene.mapWidth;
    const int height = g_game.scene.mapHeight;
    int start = -1;
    int claimedCount = 0;
    for (int i = 0; i < static_cast<int>(territory.size()); ++i) {
        if (!territory[i]) continue;
        if (start < 0) start = i;
        ++claimedCount;
    }
    if (start < 0) return false;

    std::vector<std::uint8_t> visited(territory.size(), 0);
    std::vector<int> pending{start};
    visited[start] = 1;
    int visitedCount = 0;
    while (!pending.empty()) {
        const int index = pending.back();
        pending.pop_back();
        ++visitedCount;
        const int x = index % width;
        const int y = index / width;
        constexpr int dx[] = {-1, 1, 0, 0};
        constexpr int dy[] = {0, 0, -1, 1};
        for (int direction = 0; direction < 4; ++direction) {
            const int nx = x + dx[direction];
            const int ny = y + dy[direction];
            if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
            const int next = ny * width + nx;
            if (!territory[next] || visited[next]) continue;
            visited[next] = 1;
            pending.push_back(next);
        }
    }
    return visitedCount == claimedCount;
}

bool FootprintInsideTerritoryMask(
    const rpg::SceneObject& object,
    const std::vector<std::uint8_t>& territory) {
    const rpg::SceneObjectDef* def = rpg::FindObjectDef(object.type);
    if (!def) return true;
    const POINT origin = ObjectFootprintOrigin(object);
    for (int y = origin.y; y < origin.y + def->footprintHeight; ++y) {
        for (int x = origin.x; x < origin.x + def->footprintWidth; ++x) {
            if (x < 0 || y < 0 || x >= g_game.scene.mapWidth || y >= g_game.scene.mapHeight ||
                !territory[y * g_game.scene.mapWidth + x]) return false;
        }
    }
    return true;
}

bool ModifyTerritoryRect(POINT first, POINT second, bool claim) {
    const int left = std::clamp<int>(std::min(first.x, second.x), 0, g_game.scene.mapWidth - 1);
    const int right = std::clamp<int>(std::max(first.x, second.x), 0, g_game.scene.mapWidth - 1);
    const int top = std::clamp<int>(std::min(first.y, second.y), 0, g_game.scene.mapHeight - 1);
    const int bottom = std::clamp<int>(std::max(first.y, second.y), 0, g_game.scene.mapHeight - 1);
    if (claim && g_game.territoryStability < kMinimumRecruitmentStability) {
        g_game.pickupNotice = L"领地稳定度低于 25，暂时无法扩张";
        g_game.pickupNoticeTime = 2.0f;
        return false;
    }
    std::vector<std::uint8_t> changedTerritory = g_game.scene.territory;
    int changed = 0;
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            std::uint8_t& tile = changedTerritory[y * g_game.scene.mapWidth + x];
            const std::uint8_t value = claim ? 1 : 0;
            if (tile == value) continue;
            tile = value;
            ++changed;
        }
    }
    if (changed == 0) {
        g_game.pickupNotice = claim ? L"所选区域已经属于领地" : L"所选区域没有可删除的领地";
        g_game.pickupNoticeTime = 1.6f;
        return false;
    }
    const int resultingCount = static_cast<int>(std::count(changedTerritory.begin(), changedTerritory.end(), 1));
    if (claim && resultingCount > TerritoryAreaLimit()) {
        g_game.pickupNotice = L"领地面积已达上限（" + std::to_wstring(TerritoryAreaLimit()) + L" 格）";
        g_game.pickupNoticeTime = 2.0f;
        return false;
    }
    if (!TerritoryMaskConnected(changedTerritory)) {
        g_game.pickupNotice = claim ? L"飞地无效：新增领地必须与现有领地连通" :
                                      L"无法删除：操作会把领地分割成飞地";
        g_game.pickupNoticeTime = 2.0f;
        return false;
    }
    const auto anchor = std::find_if(g_game.scene.objects.begin(), g_game.scene.objects.end(), [](const rpg::SceneObject& object) {
        return object.type == "territory_anchor";
    });
    if (anchor != g_game.scene.objects.end() && !FootprintInsideTerritoryMask(*anchor, changedTerritory)) {
        g_game.pickupNotice = L"无法删除现实锚点所在的领地";
        g_game.pickupNoticeTime = 2.0f;
        return false;
    }
    if (!claim) {
        for (const rpg::SceneObject& object : g_game.scene.objects) {
            if (object.type == "teleport_point" || object.type == "territory_anchor") continue;
            const rpg::SceneObjectDef* def = rpg::FindObjectDef(object.type);
            if (!def || !def->building) continue;
            if (!FootprintInsideTerritoryMask(object, changedTerritory)) {
                g_game.pickupNotice = L"无法删除：普通建筑必须保留在领地范围内";
                g_game.pickupNoticeTime = 2.0f;
                return false;
            }
        }
    }
    int projectedLoad = (static_cast<int>(g_game.npcs.size()) + PendingResidentCount()) * kResidentRealityLoad +
                        (RealityAnchorExists() ? kWorkbenchRealityLoad : 0);
    for (const rpg::SceneObject& object : g_game.scene.objects) {
        if (object.type != "teleport_point") continue;
        const POINT tile = ObjectFootprintOrigin(object);
        if (tile.x >= 0 && tile.y >= 0 && tile.x < g_game.scene.mapWidth && tile.y < g_game.scene.mapHeight &&
            changedTerritory[tile.y * g_game.scene.mapWidth + tile.x]) projectedLoad += kTeleportRealityLoad;
    }
    if (claim && projectedLoad > RealityCapacityLimit()) {
        g_game.pickupNotice = L"扩张会启用传送点并超过现实承载上限";
        g_game.pickupNoticeTime = 2.0f;
        return false;
    }
    g_game.scene.territory = std::move(changedTerritory);
    g_game.pickupNotice = claim ? L"领地已扩张 " + std::to_wstring(changed) + L" 格" :
                                  L"领地已收回 " + std::to_wstring(changed) + L" 格";
    g_game.pickupNoticeTime = 1.6f;
    rpg::InvalidateTerritoryRenderCache();
    SaveCurrentScene();
    return true;
}

bool ModifyFarmlandRect(POINT first, POINT second, bool create) {
    const int left = std::clamp<int>(std::min(first.x, second.x), 0, g_game.scene.mapWidth - 1);
    const int right = std::clamp<int>(std::max(first.x, second.x), 0, g_game.scene.mapWidth - 1);
    const int top = std::clamp<int>(std::min(first.y, second.y), 0, g_game.scene.mapHeight - 1);
    const int bottom = std::clamp<int>(std::max(first.y, second.y), 0, g_game.scene.mapHeight - 1);
    int changed = 0;
    if (!create) {
        const auto newEnd = std::remove_if(g_game.scene.objects.begin(), g_game.scene.objects.end(), [&](const rpg::SceneObject& object) {
            if (object.type != "farmland") return false;
            const POINT origin = ObjectFootprintOrigin(object);
            const bool remove = origin.x >= left && origin.x <= right && origin.y >= top && origin.y <= bottom;
            if (remove) ++changed;
            return remove;
        });
        g_game.scene.objects.erase(newEnd, g_game.scene.objects.end());
    } else {
        for (int y = top; y <= bottom; ++y) {
            for (int x = left; x <= right; ++x) {
                const bool exists = std::any_of(g_game.scene.objects.begin(), g_game.scene.objects.end(), [&](const rpg::SceneObject& object) {
                    if (object.type != "farmland") return false;
                    const POINT origin = ObjectFootprintOrigin(object);
                    return origin.x == x && origin.y == y;
                });
                if (exists) continue;
                std::wstring reason;
                if (!BuildingCanBePlaced("farmland", x, y, &reason)) continue;
                rpg::SceneObject farmland = rpg::MakeObject(
                    "farmland", BuildingAnchorPosition("farmland", x, y),
                    static_cast<int>(g_game.scene.objects.size()) + 20000 + changed);
                int suffix = static_cast<int>(g_game.scene.objects.size()) + 1;
                do {
                    farmland.id = "farmland_runtime_" + std::to_string(suffix++);
                } while (std::any_of(g_game.scene.objects.begin(), g_game.scene.objects.end(), [&](const rpg::SceneObject& object) {
                    return object.id == farmland.id;
                }));
                g_game.scene.objects.push_back(std::move(farmland));
                ++changed;
            }
        }
    }
    g_game.pickupNotice = changed > 0
        ? (create ? L"已创建耕地 " : L"已删除耕地 ") + std::to_wstring(changed) + L" 格"
        : (create ? L"所选区域没有可创建的耕地" : L"所选区域没有耕地");
    g_game.pickupNoticeTime = 1.8f;
    if (changed > 0) SaveCurrentScene();
    return changed > 0;
}

bool InteractWithFarmland(int screenX, int screenY) {
    const POINT tile = ScreenToTile(screenX, screenY);
    const auto farmland = std::find_if(g_game.scene.objects.begin(), g_game.scene.objects.end(), [&](const rpg::SceneObject& object) {
        if (object.type != "farmland") return false;
        const POINT origin = ObjectFootprintOrigin(object);
        return origin.x == tile.x && origin.y == tile.y;
    });
    if (farmland == g_game.scene.objects.end()) return false;
    if (Distance(g_game.player.pos, {farmland->pos.x, farmland->pos.y}) > 190.0f) {
        g_game.pickupNotice = L"距离耕地太远";
        g_game.pickupNoticeTime = 1.4f;
        return true;
    }

    if (!farmland->cropId.empty()) {
        const int requiredDays = CropGrowthDays(farmland->cropId);
        if (farmland->cropGrowthDays < requiredDays) {
            g_game.pickupNotice = L"作物生长中：" + std::to_wstring(farmland->cropGrowthDays) + L" / " +
                                  std::to_wstring(requiredDays) + L" 天";
            g_game.pickupNoticeTime = 1.6f;
            return true;
        }
        const std::string harvestedCrop = farmland->cropId;
        const int harvestCount = CropHarvestCount(harvestedCrop);
        ItemStack harvest = MakeItemStack(harvestedCrop, harvestCount);
        const std::wstring cropName = harvest.displayName;
        StoreItem(harvest);
        if (harvest.count > 0) SpawnPickup({farmland->pos.x, farmland->pos.y - 18.0f}, harvestedCrop, harvest.count);
        farmland->cropId.clear();
        farmland->cropGrowthDays = 0;
        g_game.pickupNotice = L"收获 " + cropName + L" x" + std::to_wstring(harvestCount);
        g_game.pickupNoticeTime = 2.0f;
        SaveCurrentScene();
        return true;
    }

    ItemStack& selected = g_game.inventory.slots[g_game.inventory.selectedHotbar];
    const std::string cropId = CropIdForSeed(selected.id);
    if (cropId.empty()) {
        g_game.pickupNotice = L"请在快捷栏选中一种种子";
        g_game.pickupNoticeTime = 1.6f;
        return true;
    }
    farmland->cropId = cropId;
    farmland->cropGrowthDays = 0;
    if (--selected.count <= 0) selected = {};
    const rpg::ItemDef* crop = rpg::FindItemDef(cropId);
    g_game.pickupNotice = L"已播种：" + (crop ? crop->displayName : L"作物");
    g_game.pickupNoticeTime = 1.8f;
    SaveCurrentScene();
    return true;
}

void FinishBuildingToolSelection() {
    if (!g_game.inventory.selectingTerritory) return;
    const bool create = !g_game.inventory.selectionRemoving;
    if (g_game.inventory.buildingToolMode == BuildingToolMode::Farmland) {
        ModifyFarmlandRect(g_game.inventory.territoryStartTile, g_game.inventory.territoryEndTile, create);
    } else if (g_game.inventory.buildingToolMode == BuildingToolMode::Territory) {
        if (!HasTerritoryAnchor()) {
            g_game.pickupNotice = L"请先从背包放置现实锚点";
            g_game.pickupNoticeTime = 1.5f;
        } else {
            ModifyTerritoryRect(g_game.inventory.territoryStartTile, g_game.inventory.territoryEndTile, create);
        }
    }
    g_game.inventory.selectingTerritory = false;
    g_game.inventory.selectionRemoving = false;
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
        HPEN operationPen = CreatePen(PS_DOT, 2, g_game.inventory.selectionRemoving
            ? RGB(240, 102, 102) : RGB(112, 232, 139));
        SelectObject(hdc, operationPen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, left, top, right, bottom);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, previewPen);
        DeleteObject(operationPen);
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

RECT BuildingToolButtonRect(const InventoryLayout& layout, int index) {
    constexpr int buttonWidth = 84;
    constexpr int buttonHeight = 32;
    constexpr int gap = 6;
    constexpr int buttonCount = 3;
    const int totalWidth = buttonCount * buttonWidth + (buttonCount - 1) * gap;
    const int left = layout.gridX + (InventoryRowWidth() - totalWidth) / 2 + index * (buttonWidth + gap);
    const int top = layout.hotbarY - buttonHeight - 10;
    return RECT{left, top, left + buttonWidth, top + buttonHeight};
}

void DrawBuildingToolBar(HDC hdc, const InventoryLayout& layout) {
    if (!HeldSpiritSkill(1)) return;
    constexpr const wchar_t* labels[] = {L"移动", L"耕地", L"领地"};
    const int selected = static_cast<int>(g_game.inventory.buildingToolMode);
    for (int i = 0; i < 3; ++i) {
        const RECT button = BuildingToolButtonRect(layout, i);
        FillRectColor(hdc, button, i == selected ? RGB(133, 104, 49) : RGB(50, 58, 55));
        HPEN pen = CreatePen(PS_SOLID, i == selected ? 2 : 1,
                             i == selected ? RGB(244, 216, 124) : RGB(128, 138, 131));
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, button.left, button.top, button.right, button.bottom);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
        DrawCenteredText(hdc, labels[i], button, i == selected ? RGB(255, 247, 210) : RGB(220, 225, 215), 15);
    }
}

void DrawFarmCrops(HDC hdc) {
    const float scale = RenderScale();
    for (const rpg::SceneObject& farmland : g_game.scene.objects) {
        if (farmland.type != "farmland" || farmland.cropId.empty()) continue;
        const int requiredDays = CropGrowthDays(farmland.cropId);
        const bool mature = farmland.cropGrowthDays >= requiredDays;
        const float progress = Clamp(static_cast<float>(farmland.cropGrowthDays + 1) /
                                     static_cast<float>(requiredDays + 1), 0.25f, 1.0f);
        const int imageSize = std::max(12, static_cast<int>(std::round((mature ? 38.0f : 30.0f) * progress * scale)));
        const int centerX = static_cast<int>(std::round(farmland.pos.x * scale - g_game.camera.x));
        const int centerY = static_cast<int>(std::round((farmland.pos.y - 24.0f) * scale - g_game.camera.y));
        RECT image{centerX - imageSize / 2, centerY - imageSize / 2,
                   centerX - imageSize / 2 + imageSize, centerY - imageSize / 2 + imageSize};
        ItemStack visual = MakeItemStack(mature ? farmland.cropId : SeedIdForCrop(farmland.cropId), 1);
        if (!visual.id.empty() && DrawItemImage(hdc, visual, image, true)) continue;

        const COLORREF fill = mature ? RGB(113, 176, 77) : RGB(100, 139, 72);
        HBRUSH brush = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(48, 83, 44));
        HGDIOBJ oldBrush = SelectObject(hdc, brush);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        Ellipse(hdc, image.left, image.top, image.right, image.bottom);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
    }
}

int HitBuildingToolButton(const RECT& client, int x, int y) {
    if (!HeldSpiritSkill(1)) return -1;
    const InventoryLayout layout = MakeInventoryLayout(client);
    for (int i = 0; i < 3; ++i) {
        const RECT button = BuildingToolButtonRect(layout, i);
        if (PtInRect(&button, POINT{x, y})) return i;
    }
    return -1;
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
    constexpr float starts[] = {210.0f, 330.0f, 90.0f};
    constexpr double pi = 3.14159265358979323846;

    for (int i = 0; i < 3; ++i) {
        const bool available = !g_game.inventory.spiritSlots[i].id.empty();
        HBRUSH brush = CreateSolidBrush(available ? colors[i] : RGB(48, 53, 52));
        const bool active = available && g_game.inventory.heldItem.id == g_game.inventory.spiritSlots[i].id;
        HPEN pen = CreatePen(PS_SOLID, active ? 4 : 2,
            active ? RGB(255, 240, 142) : RGB(222, 226, 210));
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
    if (index < 0 || index >= 3) return;
    const ItemStack& equipped = g_game.inventory.spiritSlots[index];
    if (equipped.id.empty() || SpiritSkillIndex(equipped.id) != index) {
        g_game.pickupNotice = L"对应灵石槽为空";
        g_game.pickupNoticeTime = 1.3f;
        return;
    }
    if (g_game.inventory.heldItem.id == equipped.id) {
        g_game.inventory.heldItem = {};
        g_game.pickupNotice = L"已取消灵石技能";
    } else {
        g_game.inventory.heldItem = equipped;
        g_game.pickupNotice = g_game.inventory.heldItem.displayName + L" 已启用";
    }
    g_game.pickupNoticeTime = 1.5f;
    if (!HeldSpiritSkill(1)) {
        if (g_game.inventory.placingBuilding) CancelBuildingPlacement();
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
    DrawBuildingToolBar(hdc, layout);
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
    const bool targetIsSpirit = targetIndex >= kInventorySlotCount && targetIndex < kInventorySlotCount + kSpiritSlotCount;
    if (targetIsSpirit && SpiritSkillIndex(source->id) != targetIndex - kInventorySlotCount) {
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

bool AnchorStorageAvailable() {
    return ActorProtectedByTerritory(g_game.player.pos);
}

RECT AnchorTabRect(const RECT& panel, AnchorPanelTab tab) {
    const bool storageAvailable = AnchorStorageAvailable();
    int visibleIndex = 0;
    if (tab == AnchorPanelTab::Storage) {
        if (!storageAvailable) return {};
    } else if (tab == AnchorPanelTab::Workbench) {
        if (!storageAvailable) return {};
        visibleIndex = 1;
    } else if (tab == AnchorPanelTab::Crafting) {
        if (!storageAvailable) return {};
        visibleIndex = 2;
    } else if (tab == AnchorPanelTab::Residents) {
        visibleIndex = storageAvailable ? 3 : 0;
    } else if (tab == AnchorPanelTab::Work) {
        visibleIndex = storageAvailable ? 4 : 1;
    } else {
        visibleIndex = storageAvailable ? 5 : 2;
    }
    constexpr int gap = 5;
    const int visibleCount = storageAvailable ? 6 : 3;
    const int availableWidth = std::max(300, static_cast<int>(panel.right - panel.left) - 186);
    const int width = std::max(82, (availableWidth - (visibleCount - 1) * gap) / visibleCount);
    const int left = panel.left + 166 + visibleIndex * (width + gap);
    return {left, panel.top + 12, left + width, panel.top + 46};
}

int HitAnchorTab(const RECT& client, int x, int y) {
    if (!g_game.anchorPanelOpen) return -1;
    const RECT panel = AnchorPanelRect(client);
    for (const AnchorPanelTab tab : {AnchorPanelTab::Storage, AnchorPanelTab::Workbench,
                                     AnchorPanelTab::Crafting, AnchorPanelTab::Residents,
                                     AnchorPanelTab::Work, AnchorPanelTab::Technology}) {
        const RECT rect = AnchorTabRect(panel, tab);
        if (rect.right > rect.left && PtInRect(&rect, POINT{x, y})) return static_cast<int>(tab);
    }
    return -1;
}

RECT AnchorStorageSlotRect(const RECT& panel, int index) {
    constexpr int cell = 34;
    constexpr int gap = 4;
    const int column = index % 10;
    const int row = index / 10;
    const int left = panel.left + 20 + column * (cell + gap);
    const int top = panel.top + 96 + row * (cell + gap);
    return {left, top, left + cell, top + cell};
}

RECT AnchorInventorySlotRect(const RECT& panel, int index) {
    constexpr int cell = 34;
    constexpr int gap = 4;
    const int column = index % 10;
    const int row = index / 10;
    const int left = panel.left + 422 + column * (cell + gap);
    const int top = panel.top + 96 + row * (cell + gap);
    return {left, top, left + cell, top + cell};
}

RECT AnchorNpcRowRect(const RECT& panel, int index) {
    const int top = panel.top + 94 + index * 31;
    return {panel.left + 20, top, panel.left + 302, top + 27};
}

RECT AnchorStoneButtonRect(const RECT& panel, int index) {
    const int left = panel.left + 342 + index * 156;
    return {left, panel.top + 300, left + 144, panel.top + 334};
}

RECT AnchorBlueprintRect(const RECT& panel, int index) {
    constexpr int width = 190;
    constexpr int height = 72;
    if (index == 0) return {panel.left + 60, panel.top + 112, panel.left + 60 + width, panel.top + 112 + height};
    const int left = panel.left + 360;
    const int top = panel.top + 76 + (index - 1) * 118;
    return {left, top, left + width, top + height};
}

struct AnchorBlueprintDef {
    const char* id;
    const wchar_t* name;
    const wchar_t* description;
    const char* dependency;
};

constexpr std::array<AnchorBlueprintDef, 3> kAnchorBlueprints{{
    {"territory_anchor", L"现实锚点", L"固化领地并开启锚点功能", nullptr},
    {"cottage_4x3", L"4x3 小屋", L"解锁居民小屋建筑蓝图", "territory_anchor"},
    {"teleport_point", L"传送点", L"解锁场景传送点建筑蓝图", "territory_anchor"},
}};

bool BlueprintUnlocked(const char* id);

struct WorkbenchRecipeDef {
    struct Ingredient {
        const char* id;
        const wchar_t* name;
        int count;
    };

    const char* outputId;
    const wchar_t* name;
    const char* requiredBlueprint;
    std::array<Ingredient, 4> ingredients;
    int ingredientCount;
    CraftingStation station = CraftingStation::Workbench;
    int outputCount = 1;
    const char* requiredFacilityType = nullptr;
};

constexpr std::array<WorkbenchRecipeDef, 33> kWorkbenchRecipes{{
    {"gathering_stone", L"采集石I", nullptr, {{{"emerald", L"绿宝石", 1}, {"stone", L"石头", 1}, {"wood", L"木头", 1}, {nullptr, nullptr, 0}}}, 3},
    {"gathering_stone_ii", L"采集石II", nullptr, {{{"emerald", L"绿宝石", 1}, {"exotic_ore", L"特异矿石", 1}, {"exotic_wood", L"特异木头", 1}, {nullptr, nullptr, 0}}}, 3},
    {"gathering_stone_iii", L"采集石III", nullptr, {{{"emerald", L"绿宝石", 2}, {"exotic_ore", L"特异矿石", 1}, {"exotic_wood", L"特异木头", 1}, {"anchor_fragment", L"锚点碎片", 1}}}, 4},
    {"combat_stone", L"战斗石I", nullptr, {{{"ruby", L"红宝石", 1}, {"stone", L"石头", 1}, {"wood", L"木头", 1}, {nullptr, nullptr, 0}}}, 3},
    {"combat_stone_ii", L"战斗石II", nullptr, {{{"ruby", L"红宝石", 1}, {"exotic_ore", L"特异矿石", 1}, {"exotic_wood", L"特异木头", 1}, {nullptr, nullptr, 0}}}, 3},
    {"combat_stone_iii", L"战斗石III", nullptr, {{{"ruby", L"红宝石", 2}, {"exotic_ore", L"特异矿石", 1}, {"exotic_wood", L"特异木头", 1}, {"anchor_fragment", L"锚点碎片", 1}}}, 4},
    {"building_stone", L"建造石I", nullptr, {{{"topaz", L"黄宝石", 1}, {"stone", L"石头", 1}, {"wood", L"木头", 1}, {nullptr, nullptr, 0}}}, 3},
    {"building_stone_ii", L"建造石II", nullptr, {{{"topaz", L"黄宝石", 1}, {"exotic_ore", L"特异矿石", 1}, {"exotic_wood", L"特异木头", 1}, {nullptr, nullptr, 0}}}, 3},
    {"building_stone_iii", L"建造石III", nullptr, {{{"topaz", L"黄宝石", 2}, {"exotic_ore", L"特异矿石", 1}, {"exotic_wood", L"特异木头", 1}, {"anchor_fragment", L"锚点碎片", 1}}}, 4},
    {"small_potion", L"治疗药水", nullptr, {{{"healing_herb", L"草药", 1}, {"fodder", L"草料", 1}, {"berry", L"浆果", 1}, {nullptr, nullptr, 0}}}, 3},
    {"cottage_4x3", L"4x3 小屋", "cottage_4x3", {{{"wood", L"木头", 30}, {"iron_ore", L"铁矿石", 8}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}}, 2},
    {"teleport_point", L"传送点", "teleport_point", {{{"iron_ore", L"铁矿石", 12}, {"gold_coin", L"金币", 20}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}}, 2},
    {"rice_seed", L"水稻分解为种子", nullptr, {{{"rice", L"水稻", 1}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}}, 1, CraftingStation::Decompose, 4},
    {"corn_seed", L"玉米分解为种子", nullptr, {{{"corn", L"玉米", 1}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}}, 1, CraftingStation::Decompose, 4},
    {"potato_seed", L"土豆分解为种子", nullptr, {{{"potato", L"土豆", 1}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}}, 1, CraftingStation::Decompose, 4},
    {"sweet_potato_seed", L"红薯分解为种子", nullptr, {{{"sweet_potato", L"红薯", 1}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}}, 1, CraftingStation::Decompose, 4},
    {"cabbage_seed", L"白菜分解为种子", nullptr, {{{"cabbage", L"白菜", 1}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}}, 1, CraftingStation::Decompose, 4},
    {"iron_ingot", L"铁锭", nullptr, {{{"iron_ore", L"铁矿石", 2}, {"wood", L"木头", 1}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}}, 2, CraftingStation::Production, 1, "furnace"},
    {"copper_ingot", L"铜锭", nullptr, {{{"copper_ore", L"铜矿石", 2}, {"wood", L"木头", 1}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}}, 2, CraftingStation::Production, 1, "furnace"},
    {"silver_ingot", L"银锭", nullptr, {{{"silver_ore", L"银矿石", 2}, {"wood", L"木头", 1}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}}, 2, CraftingStation::Production, 1, "furnace"},
    {"gold_ingot", L"金锭", nullptr, {{{"gold_ore", L"金矿石", 2}, {"wood", L"木头", 1}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}}, 2, CraftingStation::Production, 1, "furnace"},
    {"plank", L"木板", nullptr, {{{"wood", L"木头", 1}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}}, 1, CraftingStation::Production, 2, "sawmill"},
    {"exotic_plank", L"特异木板", nullptr, {{{"exotic_wood", L"特异木头", 1}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}}, 1, CraftingStation::Production, 2, "sawmill"},
    {"flower_bed", L"花坛", nullptr, {{{"fodder", L"草料", 1}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}}, 1},
    {"pond", L"水池", nullptr, {{{"stone", L"石头", 1}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}}, 1},
    {"stone_floor", L"石头地板", nullptr, {{{"stone", L"石头", 1}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}}, 1},
    {"wood_floor", L"木头地板", nullptr, {{{"wood", L"木头", 1}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}}, 1},
    {"warehouse", L"仓库", nullptr, {{{"wood", L"木头", 2}, {"stone", L"石头", 2}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}}, 2},
    {"campfire", L"篝火", nullptr, {{{"wood", L"木头", 2}, {"fodder", L"草料", 1}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}}, 2},
    {"apothecary", L"药房", nullptr, {{{"wood", L"木头", 2}, {"stone", L"石头", 2}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}}, 2},
    {"kitchen", L"厨房", nullptr, {{{"wood", L"木头", 2}, {"stone", L"石头", 2}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}}, 2},
    {"furnace", L"熔炉", nullptr, {{{"wood", L"木头", 2}, {"stone", L"石头", 4}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}}, 2},
    {"sawmill", L"锯木台", nullptr, {{{"wood", L"木头", 1}, {"iron_ingot", L"铁锭", 1}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}}, 2},
}};

RECT WorkbenchRecipeRect(const RECT& panel, int index) {
    const int top = panel.top + 130 + index * 31;
    return {panel.left + 28, top, panel.left + 304, top + 27};
}

RECT CraftingStationButtonRect(const RECT& panel, int index) {
    const int left = panel.left + 28 + index * 132;
    return {left, panel.top + 64, left + 124, panel.top + 94};
}

bool CraftingStationAvailable(CraftingStation station) {
    if (station != CraftingStation::Production) return true;
    return std::any_of(g_game.scene.objects.begin(), g_game.scene.objects.end(), [](const rpg::SceneObject& object) {
        return (object.type == "furnace" || object.type == "sawmill") && ObjectInsideTerritory(object);
    });
}

bool RecipeIsBuilding(const WorkbenchRecipeDef& recipe) {
    const rpg::SceneObjectDef* object = rpg::FindObjectDef(recipe.outputId);
    return object && object->building && object->placeable;
}

bool RecipeFacilityAvailable(const WorkbenchRecipeDef& recipe) {
    if (!recipe.requiredFacilityType) return true;
    return std::any_of(g_game.scene.objects.begin(), g_game.scene.objects.end(), [&](const rpg::SceneObject& object) {
        return object.type == recipe.requiredFacilityType && ObjectInsideTerritory(object);
    });
}

bool RecipeVisibleInCurrentPanel(const WorkbenchRecipeDef& recipe) {
    if (g_game.anchorPanelTab == AnchorPanelTab::Workbench) {
        return recipe.station == g_game.craftingStation && recipe.station != CraftingStation::Workbench;
    }
    if (g_game.anchorPanelTab == AnchorPanelTab::Crafting) {
        if (recipe.station != CraftingStation::Workbench) return false;
        return RecipeIsBuilding(recipe) == (g_game.craftingCategory == CraftingCategory::Buildings);
    }
    return false;
}

int FirstRecipeForStation(CraftingStation station) {
    for (int i = 0; i < static_cast<int>(kWorkbenchRecipes.size()); ++i) {
        if (kWorkbenchRecipes[i].station == station) return i;
    }
    return 0;
}

int FirstRecipeForCraftingCategory(CraftingCategory category) {
    for (int i = 0; i < static_cast<int>(kWorkbenchRecipes.size()); ++i) {
        const WorkbenchRecipeDef& recipe = kWorkbenchRecipes[i];
        if (recipe.station == CraftingStation::Workbench &&
            RecipeIsBuilding(recipe) == (category == CraftingCategory::Buildings)) return i;
    }
    return 0;
}

RECT WorkbenchCraftButtonRect(const RECT& panel) {
    return {panel.left + 360, panel.bottom - 78, panel.left + 520, panel.bottom - 34};
}

int StoredItemCount(std::string_view id) {
    int count = 0;
    for (const ItemStack& item : g_game.inventory.slots) if (item.id == id) count += item.count;
    for (const ItemStack& item : g_game.anchorStorage) if (item.id == id) count += item.count;
    return count;
}

bool CanStoreWorkbenchOutput(std::string_view id, int count = 1) {
    const rpg::ItemDef* def = rpg::FindItemDef(id);
    if (!def) return false;
    int capacity = 0;
    for (const ItemStack& item : g_game.inventory.slots) {
        if (item.id.empty()) capacity += def->maxStack;
        else if (item.id == id && item.count < def->maxStack) capacity += def->maxStack - item.count;
        if (capacity >= count) return true;
    }
    return false;
}

void ConsumeStoredItem(std::string_view id, int count) {
    const auto consume = [id, &count](auto& slots) {
        for (ItemStack& item : slots) {
            if (count <= 0) break;
            if (item.id != id || item.count <= 0) continue;
            const int removed = std::min(count, item.count);
            item.count -= removed;
            count -= removed;
            if (item.count == 0) item = {};
        }
    };
    consume(g_game.inventory.slots);
    consume(g_game.anchorStorage);
}

bool WorkbenchRecipeCraftable(const WorkbenchRecipeDef& recipe) {
    if (!RecipeFacilityAvailable(recipe)) return false;
    if (recipe.requiredBlueprint && !BlueprintUnlocked(recipe.requiredBlueprint)) return false;
    for (int i = 0; i < recipe.ingredientCount; ++i) {
        if (StoredItemCount(recipe.ingredients[i].id) < recipe.ingredients[i].count) return false;
    }
    return RecipeIsBuilding(recipe) || CanStoreWorkbenchOutput(recipe.outputId, recipe.outputCount);
}

int HitCraftingStation(const RECT& client, int x, int y) {
    if (!g_game.anchorPanelOpen || g_game.anchorPanelTab != AnchorPanelTab::Workbench ||
        !AnchorStorageAvailable()) return -1;
    const RECT panel = AnchorPanelRect(client);
    for (int i = 0; i < 2; ++i) {
        const RECT button = CraftingStationButtonRect(panel, i);
        if (PtInRect(&button, POINT{x, y})) {
            return static_cast<int>(i == 0 ? CraftingStation::Decompose : CraftingStation::Production);
        }
    }
    return -1;
}

int HitCraftingCategory(const RECT& client, int x, int y) {
    if (!g_game.anchorPanelOpen || g_game.anchorPanelTab != AnchorPanelTab::Crafting ||
        !AnchorStorageAvailable()) return -1;
    const RECT panel = AnchorPanelRect(client);
    for (int i = 0; i < 2; ++i) {
        const RECT button = CraftingStationButtonRect(panel, i);
        if (PtInRect(&button, POINT{x, y})) return i;
    }
    return -1;
}

int HitWorkbenchRecipe(const RECT& client, int x, int y) {
    if (!g_game.anchorPanelOpen ||
        (g_game.anchorPanelTab != AnchorPanelTab::Workbench && g_game.anchorPanelTab != AnchorPanelTab::Crafting) ||
        !AnchorStorageAvailable()) return -1;
    const RECT panel = AnchorPanelRect(client);
    const RECT button = WorkbenchCraftButtonRect(panel);
    return PtInRect(&button, POINT{x, y}) ? g_game.anchorSelectedWorkbenchRecipe : -1;
}

int HitWorkbenchRecipeRow(const RECT& client, int x, int y) {
    if (!g_game.anchorPanelOpen ||
        (g_game.anchorPanelTab != AnchorPanelTab::Workbench && g_game.anchorPanelTab != AnchorPanelTab::Crafting) ||
        !AnchorStorageAvailable()) return -1;
    const RECT panel = AnchorPanelRect(client);
    int visible = 0;
    for (int i = 0; i < static_cast<int>(kWorkbenchRecipes.size()); ++i) {
        if (!RecipeVisibleInCurrentPanel(kWorkbenchRecipes[i])) continue;
        const RECT row = WorkbenchRecipeRect(panel, visible++);
        if (PtInRect(&row, POINT{x, y})) return i;
    }
    return -1;
}

bool BlueprintUnlocked(const char* id) {
    return id && g_game.unlockedBlueprints.count(id) != 0;
}

bool BlueprintAvailable(const AnchorBlueprintDef& blueprint) {
    return !blueprint.dependency || BlueprintUnlocked(blueprint.dependency);
}

int HitAnchorBlueprint(const RECT& client, int x, int y) {
    if (!g_game.anchorPanelOpen || g_game.anchorPanelTab != AnchorPanelTab::Technology) return -1;
    const RECT panel = AnchorPanelRect(client);
    for (int i = 0; i < static_cast<int>(kAnchorBlueprints.size()); ++i) {
        const RECT node = AnchorBlueprintRect(panel, i);
        if (PtInRect(&node, POINT{x, y})) return i;
    }
    return -1;
}

bool PointInAnchorPanel(const RECT& client, int x, int y) {
    const RECT panel = AnchorPanelRect(client);
    return g_game.anchorPanelOpen && PtInRect(&panel, POINT{x, y});
}

int HitAnchorStorageSlot(const RECT& client, int x, int y) {
    if (!g_game.anchorPanelOpen || g_game.anchorPanelTab != AnchorPanelTab::Storage || !AnchorStorageAvailable()) return -1;
    const RECT panel = AnchorPanelRect(client);
    for (int i = 0; i < kAnchorStorageSlotCount; ++i) {
        const RECT slot = AnchorStorageSlotRect(panel, i);
        if (PtInRect(&slot, POINT{x, y})) return kAnchorStorageSlotBase + i;
    }
    return -1;
}

int HitAnchorInventorySlot(const RECT& client, int x, int y) {
    if (!g_game.anchorPanelOpen || g_game.anchorPanelTab != AnchorPanelTab::Storage || !AnchorStorageAvailable()) return -1;
    const RECT panel = AnchorPanelRect(client);
    for (int i = 0; i < kInventorySlotCount; ++i) {
        const RECT slot = AnchorInventorySlotRect(panel, i);
        if (PtInRect(&slot, POINT{x, y})) return i;
    }
    return -1;
}

int HitAnchorNpc(const RECT& client, int x, int y) {
    if (!g_game.anchorPanelOpen ||
        (g_game.anchorPanelTab != AnchorPanelTab::Residents && g_game.anchorPanelTab != AnchorPanelTab::Work)) return -1;
    const RECT panel = AnchorPanelRect(client);
    for (int i = 0; i < static_cast<int>(g_game.npcs.size()); ++i) {
        const RECT row = AnchorNpcRowRect(panel, i);
        if (PtInRect(&row, POINT{x, y})) return i;
    }
    return -1;
}

int HitAnchorNpcStone(const RECT& client, int x, int y) {
    if (!g_game.anchorPanelOpen ||
        (g_game.anchorPanelTab != AnchorPanelTab::Residents && g_game.anchorPanelTab != AnchorPanelTab::Work) ||
        g_game.anchorSelectedNpc < 0 ||
        g_game.anchorSelectedNpc >= static_cast<int>(g_game.npcs.size())) return -1;
    const RECT panel = AnchorPanelRect(client);
    for (int i = 0; i < kSpiritSlotCount; ++i) {
        const RECT button = g_game.anchorPanelTab == AnchorPanelTab::Work
            ? RECT{panel.left + 342 + i * 156, panel.top + 382, panel.left + 342 + i * 156 + 144, panel.top + 416}
            : AnchorStoneButtonRect(panel, i);
        if (PtInRect(&button, POINT{x, y})) return i;
    }
    return -1;
}

struct NpcFacilityJob {
    std::string objectId;
    std::wstring name;
};

std::vector<NpcFacilityJob> AvailableNpcFacilityJobs() {
    std::vector<NpcFacilityJob> jobs;
    const rpg::SceneObject* firstFarmland = nullptr;
    for (const rpg::SceneObject& object : g_game.scene.objects) {
        if (!ObjectInsideTerritory(object)) continue;
        if (object.type == "farmland" && !firstFarmland) firstFarmland = &object;
    }
    if (firstFarmland) jobs.push_back({firstFarmland->id, L"农田管理员"});
    for (const rpg::SceneObject& object : g_game.scene.objects) {
        if (object.type == "furnace" && ObjectInsideTerritory(object)) jobs.push_back({object.id, L"熔炉工"});
        else if (object.type == "sawmill" && ObjectInsideTerritory(object)) jobs.push_back({object.id, L"锯木工"});
    }
    return jobs;
}

RECT NpcWorkFacilityRect(const RECT& panel, int index) {
    const int top = panel.top + 136 + index * 34;
    return {panel.left + 342, top, panel.left + 742, top + 29};
}

RECT NpcGatherTargetRect(const RECT& panel, int index) {
    const int left = panel.left + 342 + index * 108;
    return {left, panel.top + 264, left + 100, panel.top + 296};
}

RECT NpcStartGatheringRect(const RECT& panel) {
    return {panel.left + 342, panel.top + 312, panel.left + 492, panel.top + 348};
}

RECT NpcReturnHomeRect(const RECT& panel) {
    return {panel.left + 504, panel.top + 312, panel.left + 654, panel.top + 348};
}

RECT NpcStopWorkRect(const RECT& panel) {
    return {panel.left + 666, panel.top + 312, panel.left + 816, panel.top + 348};
}

RECT NpcWorkCargoRect(const RECT& panel, int index) {
    constexpr int cell = 34;
    constexpr int gap = 4;
    const int column = index % 5;
    const int row = index / 5;
    const int left = panel.left + 556 + column * (cell + gap);
    const int top = panel.top + 450 + row * (cell + gap);
    return {left, top, left + cell, top + cell};
}

int HitNpcWorkFacility(const RECT& client, int x, int y) {
    if (!g_game.anchorPanelOpen || g_game.anchorPanelTab != AnchorPanelTab::Work) return -1;
    const RECT panel = AnchorPanelRect(client);
    const auto jobs = AvailableNpcFacilityJobs();
    for (int i = 0; i < static_cast<int>(jobs.size()) && i < 3; ++i) {
        const RECT row = NpcWorkFacilityRect(panel, i);
        if (PtInRect(&row, POINT{x, y})) return i;
    }
    return -1;
}

int HitNpcGatherTarget(const RECT& client, int x, int y) {
    if (!g_game.anchorPanelOpen || g_game.anchorPanelTab != AnchorPanelTab::Work) return -1;
    const RECT panel = AnchorPanelRect(client);
    for (int i = 0; i < 4; ++i) {
        const RECT button = NpcGatherTargetRect(panel, i);
        if (PtInRect(&button, POINT{x, y})) return i;
    }
    return -1;
}

enum class NpcWorkAction { None, StartGathering, ReturnHome, Stop };

NpcWorkAction HitNpcWorkAction(const RECT& client, int x, int y) {
    if (!g_game.anchorPanelOpen || g_game.anchorPanelTab != AnchorPanelTab::Work) return NpcWorkAction::None;
    const RECT panel = AnchorPanelRect(client);
    const POINT point{x, y};
    const RECT start = NpcStartGatheringRect(panel);
    const RECT home = NpcReturnHomeRect(panel);
    const RECT stop = NpcStopWorkRect(panel);
    if (PtInRect(&start, point)) return NpcWorkAction::StartGathering;
    if (PtInRect(&home, point)) return NpcWorkAction::ReturnHome;
    if (PtInRect(&stop, point)) return NpcWorkAction::Stop;
    return NpcWorkAction::None;
}

std::wstring NpcTaskLabel(const Npc& npc) {
    switch (npc.taskMode) {
    case NpcTaskMode::Facility: return L"设施工作";
    case NpcTaskMode::Gathering: return npc.inCombat ? L"外出自卫" : L"外出采集";
    case NpcTaskMode::Returning: return L"返回领地";
    default: return npc.following ? L"跟随" : L"空闲";
    }
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

void DrawTerritoryStatus(HDC hdc) {
    if (!RealityAnchorExists()) return;
    const int residents = static_cast<int>(g_game.npcs.size());
    const int food = StoredFoodPoints();
    const std::wstring foodDays = residents > 0 ? std::to_wstring(food / residents) + L" 天" : L"--";
    const COLORREF stabilityColor = g_game.territoryStability < 25.0f
        ? RGB(236, 126, 116)
        : g_game.territoryStability < 60.0f ? RGB(231, 190, 112) : RGB(164, 221, 173);
    DrawTextLine(hdc, L"承载 " + std::to_wstring(RealityLoad()) + L" / " + std::to_wstring(RealityCapacityLimit()),
                 70, 48, RGB(205, 226, 208));
    DrawTextLine(hdc, L"食物 " + std::to_wstring(food) + L"（" + foodDays + L"）", 70, 68, RGB(222, 204, 145));
    DrawTextLine(hdc, L"稳定 " + std::to_wstring(static_cast<int>(std::lround(g_game.territoryStability))) + L" / 100",
                 70, 88, stabilityColor);
    DrawTextLine(hdc, L"领地 " + std::to_wstring(TerritoryTileCount()) + L" / " + std::to_wstring(TerritoryAreaLimit()),
                 70, 108, RGB(183, 204, 193));
}

void DrawCenteredText(HDC hdc, const std::wstring& text, const RECT& rect, COLORREF color, int fontSize);

void DrawAnchorPanel(HDC hdc, const RECT& client) {
    if (!g_game.anchorPanelOpen) return;
    if (!AnchorStorageAvailable() &&
        (g_game.anchorPanelTab == AnchorPanelTab::Storage || g_game.anchorPanelTab == AnchorPanelTab::Workbench ||
         g_game.anchorPanelTab == AnchorPanelTab::Crafting)) {
        g_game.anchorPanelTab = AnchorPanelTab::Residents;
    }
    const RECT panel = AnchorPanelRect(client);
    FillRectColor(hdc, panel, RGB(28, 35, 34));
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(116, 160, 139));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, panel.left, panel.top, panel.right, panel.bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    DrawTextLine(hdc, L"现实锚点", panel.left + 20, panel.top + 21, RGB(230, 239, 216));
    const struct { AnchorPanelTab tab; const wchar_t* label; } tabs[] = {
        {AnchorPanelTab::Storage, L"锚点仓库"},
        {AnchorPanelTab::Workbench, L"工作台"},
        {AnchorPanelTab::Crafting, L"制作"},
        {AnchorPanelTab::Residents, L"NPC 列表"},
        {AnchorPanelTab::Work, L"工作和采集"},
        {AnchorPanelTab::Technology, L"科技树"},
    };
    for (const auto& tab : tabs) {
        const RECT rect = AnchorTabRect(panel, tab.tab);
        if (rect.right <= rect.left) continue;
        const bool selected = g_game.anchorPanelTab == tab.tab;
        FillRectColor(hdc, rect, selected ? RGB(72, 112, 91) : RGB(43, 52, 49));
        DrawCenteredText(hdc, tab.label, rect, selected ? RGB(235, 243, 222) : RGB(180, 192, 181), 17);
    }

    if (g_game.anchorPanelTab == AnchorPanelTab::Storage) {
        DrawTextLine(hdc, L"锚点仓储  " + std::to_wstring(kAnchorStorageSlotCount) + L" 格", panel.left + 20, panel.top + 70, RGB(172, 205, 182));
        DrawTextLine(hdc, L"主角背包", panel.left + 422, panel.top + 70, RGB(230, 239, 216));
        for (int i = 0; i < kAnchorStorageSlotCount; ++i) {
            DrawInventorySlot(hdc, AnchorStorageSlotRect(panel, i), InventoryItemForDrawing(kAnchorStorageSlotBase + i), false, false);
        }
        for (int i = 0; i < kInventorySlotCount; ++i) {
            DrawInventorySlot(hdc, AnchorInventorySlotRect(panel, i), InventoryItemForDrawing(i), false, false);
        }
        return;
    }

    if (g_game.anchorPanelTab == AnchorPanelTab::Workbench || g_game.anchorPanelTab == AnchorPanelTab::Crafting) {
        constexpr const wchar_t* workbenchLabels[] = {L"种子分解", L"生产"};
        constexpr const wchar_t* craftingLabels[] = {L"物品", L"建筑"};
        for (int option = 0; option < 2; ++option) {
            const RECT optionButton = CraftingStationButtonRect(panel, option);
            const bool selectedOption = g_game.anchorPanelTab == AnchorPanelTab::Workbench
                ? g_game.craftingStation == (option == 0 ? CraftingStation::Decompose : CraftingStation::Production)
                : g_game.craftingCategory == static_cast<CraftingCategory>(option);
            FillRectColor(hdc, optionButton, selectedOption ? RGB(70, 112, 86) : RGB(46, 56, 52));
            DrawCenteredText(hdc,
                             g_game.anchorPanelTab == AnchorPanelTab::Workbench ? workbenchLabels[option] : craftingLabels[option],
                             optionButton, selectedOption ? RGB(232, 244, 224) : RGB(205, 216, 204), 14);
        }
        DrawTextLine(hdc, L"优先消耗背包材料，再消耗锚点仓库材料",
                     panel.left + 326, panel.top + 70, RGB(151, 176, 162));
        int visibleRecipe = 0;
        for (int i = 0; i < static_cast<int>(kWorkbenchRecipes.size()); ++i) {
            const WorkbenchRecipeDef& recipe = kWorkbenchRecipes[i];
            if (!RecipeVisibleInCurrentPanel(recipe)) continue;
            const RECT row = WorkbenchRecipeRect(panel, visibleRecipe++);
            const bool unlocked = !recipe.requiredBlueprint || BlueprintUnlocked(recipe.requiredBlueprint);
            FillRectColor(hdc, row, i == g_game.anchorSelectedWorkbenchRecipe ? RGB(68, 105, 86) : RGB(43, 52, 49));
            DrawTextLine(hdc, recipe.name, row.left + 9, row.top + 5,
                         unlocked ? RGB(232, 239, 220) : RGB(143, 151, 146));
            if (!unlocked) DrawTextLine(hdc, L"未解锁", row.right - 58, row.top + 5, RGB(173, 137, 126));
        }

        const int selected = std::clamp(g_game.anchorSelectedWorkbenchRecipe, 0,
                                        static_cast<int>(kWorkbenchRecipes.size()) - 1);
        const WorkbenchRecipeDef& recipe = kWorkbenchRecipes[selected];
        const bool unlocked = !recipe.requiredBlueprint || BlueprintUnlocked(recipe.requiredBlueprint);
        const bool stationAvailable = RecipeFacilityAvailable(recipe);
        const bool craftable = WorkbenchRecipeCraftable(recipe);
        const int detailX = panel.left + 360;
        DrawTextLine(hdc, recipe.name, detailX, panel.top + 112,
                     unlocked ? RGB(236, 241, 222) : RGB(151, 159, 153));
        ItemStack previewItem = MakeItemStack(recipe.outputId, recipe.outputCount);
        RECT previewRect{panel.right - 132, panel.top + 104, panel.right - 44, panel.top + 192};
        if (!previewItem.id.empty()) DrawItemImage(hdc, previewItem, previewRect, false);
        if (unlocked) {
            DrawTextLine(hdc, L"所需材料", detailX, panel.top + 154, RGB(170, 202, 181));
            for (int i = 0; i < recipe.ingredientCount; ++i) {
                const auto& ingredient = recipe.ingredients[i];
                const int owned = StoredItemCount(ingredient.id);
                DrawTextLine(hdc, std::wstring(ingredient.name) + L"  " + std::to_wstring(owned) + L" / " +
                             std::to_wstring(ingredient.count), detailX, panel.top + 190 + i * 34,
                             owned >= ingredient.count ? RGB(169, 220, 178) : RGB(220, 147, 133));
            }
            const std::wstring outputText = RecipeIsBuilding(recipe)
                ? L"确认后进入建筑放置模式"
                : L"产出 " + std::to_wstring(recipe.outputCount) + L" 个，成品进入主角背包";
            DrawTextLine(hdc, outputText,
                         detailX, panel.bottom - 108, RGB(149, 166, 156));
        } else {
            DrawTextLine(hdc, L"需要先在科技树解锁该建筑蓝图", detailX, panel.top + 160, RGB(174, 148, 137));
        }
        const RECT button = WorkbenchCraftButtonRect(panel);
        FillRectColor(hdc, button, craftable ? RGB(70, 121, 85) : RGB(53, 60, 57));
        std::wstring craftLabel;
        if (!stationAvailable) {
            craftLabel = recipe.requiredFacilityType && std::string_view(recipe.requiredFacilityType) == "sawmill"
                ? L"领地内需要锯木台" : L"领地内需要熔炉";
        } else {
            craftLabel = !unlocked ? L"尚未解锁" :
                (craftable ? (RecipeIsBuilding(recipe) ? L"建造" : L"制作") : L"材料不足或背包已满");
        }
        DrawCenteredText(hdc, craftLabel, button,
                         craftable ? RGB(231, 246, 224) : RGB(154, 163, 157), 16);
        return;
    }

    if (g_game.anchorPanelTab == AnchorPanelTab::Work) {
        DrawTextLine(hdc, L"居民工作分配", panel.left + 20, panel.top + 68, RGB(172, 205, 182));
        DrawTextLine(hdc, L"任务与物资", panel.left + 342, panel.top + 68, RGB(230, 239, 216));
        for (int i = 0; i < static_cast<int>(g_game.npcs.size()); ++i) {
            const Npc& npc = g_game.npcs[i];
            const RECT row = AnchorNpcRowRect(panel, i);
            if (row.bottom > panel.bottom - 18) break;
            FillRectColor(hdc, row, i == g_game.anchorSelectedNpc ? RGB(68, 105, 86) : RGB(43, 52, 49));
            DrawTextLine(hdc, npc.name, row.left + 8, row.top + 5, RGB(239, 240, 226));
            DrawTextLine(hdc, NpcTaskLabel(npc), row.right - 74, row.top + 5, RGB(179, 216, 187));
        }
        if (g_game.anchorSelectedNpc < 0 || g_game.anchorSelectedNpc >= static_cast<int>(g_game.npcs.size())) {
            DrawTextLine(hdc, L"从左侧选择居民", panel.left + 342, panel.top + 108, RGB(172, 185, 175));
            return;
        }

        const Npc& npc = g_game.npcs[g_game.anchorSelectedNpc];
        DrawTextLine(hdc, npc.name + L"    " + NpcTaskLabel(npc), panel.left + 342, panel.top + 94, RGB(235, 239, 222));
        DrawTextLine(hdc, L"设施岗位", panel.left + 342, panel.top + 116, RGB(160, 193, 174));
        const auto jobs = AvailableNpcFacilityJobs();
        if (jobs.empty()) {
            DrawTextLine(hdc, L"当前领地没有可用生产设施", panel.left + 342, panel.top + 142, RGB(169, 151, 141));
        } else {
            for (int i = 0; i < static_cast<int>(jobs.size()) && i < 3; ++i) {
                const RECT row = NpcWorkFacilityRect(panel, i);
                const bool selected = npc.taskMode == NpcTaskMode::Facility && npc.facilityId == jobs[i].objectId;
                const auto occupant = std::find_if(g_game.npcs.begin(), g_game.npcs.end(), [&](const Npc& resident) {
                    return resident.taskMode == NpcTaskMode::Facility && resident.facilityId == jobs[i].objectId;
                });
                FillRectColor(hdc, row, selected ? RGB(64, 112, 80) : RGB(48, 58, 54));
                std::wstring label = jobs[i].name;
                label += L"  ";
                label += occupant == g_game.npcs.end() ? L"空闲" : occupant->name;
                DrawTextLine(hdc, label, row.left + 10, row.top + 6,
                             selected ? RGB(209, 239, 214) : RGB(222, 228, 215));
            }
        }

        DrawTextLine(hdc, L"外出采集", panel.left + 342, panel.top + 238, RGB(160, 193, 174));
        constexpr const char* targetIds[] = {"any", "wood", "exotic_wood", "stone"};
        constexpr const wchar_t* targetNames[] = {L"自由采集", L"木头", L"特异木头", L"石头"};
        for (int i = 0; i < 4; ++i) {
            const RECT button = NpcGatherTargetRect(panel, i);
            const bool selected = npc.gatheringTarget == targetIds[i];
            FillRectColor(hdc, button, selected ? RGB(63, 105, 82) : RGB(48, 58, 54));
            DrawCenteredText(hdc, targetNames[i], button,
                             selected ? RGB(218, 241, 218) : RGB(218, 224, 212), 15);
        }
        const RECT start = NpcStartGatheringRect(panel);
        const RECT home = NpcReturnHomeRect(panel);
        const RECT stop = NpcStopWorkRect(panel);
        FillRectColor(hdc, start, npc.spiritStones[0] ? RGB(57, 108, 76) : RGB(58, 64, 61));
        FillRectColor(hdc, home, RGB(65, 83, 75));
        FillRectColor(hdc, stop, RGB(82, 63, 61));
        DrawCenteredText(hdc, L"开始采集", start, npc.spiritStones[0] ? RGB(225, 244, 221) : RGB(145, 153, 148), 15);
        DrawCenteredText(hdc, L"立即回家", home, RGB(226, 235, 224), 15);
        DrawCenteredText(hdc, L"停止任务", stop, RGB(239, 221, 218), 15);

        DrawTextLine(hdc, L"灵石", panel.left + 342, panel.top + 360, RGB(160, 193, 174));
        constexpr const wchar_t* stoneLabels[] = {L"采集石", L"建筑石", L"战斗石"};
        for (int i = 0; i < kSpiritSlotCount; ++i) {
            const RECT button{panel.left + 342 + i * 156, panel.top + 382,
                              panel.left + 342 + i * 156 + 144, panel.top + 416};
            FillRectColor(hdc, button, npc.spiritStones[i] ? RGB(62, 116, 81) : RGB(50, 59, 56));
            DrawCenteredText(hdc, npc.spiritStones[i] ? std::wstring(stoneLabels[i]) + L"  已装备" : stoneLabels[i], button,
                             npc.spiritStones[i] ? RGB(213, 244, 218) : RGB(222, 226, 210), 15);
        }

        DrawTextLine(hdc, L"随身背包  " + std::to_wstring(NpcCargoUsedSlots(npc)) + L" / 10",
                     panel.left + 342, panel.top + 426, RGB(160, 193, 174));
        for (int i = 0; i < static_cast<int>(npc.cargo.size()); ++i) {
            ItemStack item;
            if (!npc.cargo[i].id.empty()) item = MakeItemStack(npc.cargo[i].id, npc.cargo[i].count);
            DrawInventorySlot(hdc, NpcWorkCargoRect(panel, i), item, false, false);
        }
        return;
    }

    if (g_game.anchorPanelTab == AnchorPanelTab::Residents) {
        DrawTextLine(hdc, L"领地居民  " + std::to_wstring(g_game.npcs.size()) + L"    承载 " +
                     std::to_wstring(RealityLoad()) + L" / " + std::to_wstring(RealityCapacityLimit()) +
                     L"    食物 " + std::to_wstring(StoredFoodPoints()) + L"    稳定 " +
                     std::to_wstring(static_cast<int>(std::lround(g_game.territoryStability))),
                     panel.left + 20, panel.top + 68, RGB(172, 205, 182));
        DrawTextLine(hdc, L"居民详细属性", panel.left + 342, panel.top + 68, RGB(230, 239, 216));
        for (int i = 0; i < static_cast<int>(g_game.npcs.size()); ++i) {
            const Npc& npc = g_game.npcs[i];
            const RECT row = AnchorNpcRowRect(panel, i);
            if (row.bottom > panel.bottom - 18) break;
            FillRectColor(hdc, row, i == g_game.anchorSelectedNpc ? RGB(68, 105, 86) : RGB(43, 52, 49));
            DrawTextLine(hdc, npc.name, row.left + 8, row.top + 5, RGB(239, 240, 226));
            DrawTextLine(hdc, npc.shadowForm ? L"影子" : L"实体", row.right - 46, row.top + 5,
                         npc.shadowForm ? RGB(183, 191, 197) : RGB(179, 224, 185));
        }
        if (g_game.anchorSelectedNpc >= 0 && g_game.anchorSelectedNpc < static_cast<int>(g_game.npcs.size())) {
            const Npc& npc = g_game.npcs[g_game.anchorSelectedNpc];
            const int x = panel.left + 342;
            const int y = panel.top + 108;
            DrawTextLine(hdc, npc.name, x, y, RGB(235, 239, 222));
            DrawTextLine(hdc, L"形态：" + std::wstring(npc.shadowForm ? L"影子" : L"实体"), x, y + 34, RGB(186, 210, 196));
            DrawTextLine(hdc, L"血量：" + std::to_wstring(npc.health) + L" / 60", x, y + 64, RGB(223, 132, 132));
            DrawTextLine(hdc, L"好感度：" + std::to_wstring(npc.affinity) + L" / 100", x + 220, y + 64, RGB(222, 190, 126));
            DrawTextLine(hdc, L"性格：" + npc.personality, x, y + 94, RGB(214, 218, 202));
            DrawTextLine(hdc, L"状态：" + std::wstring(npc.following ? L"跟随中" : L"领地内自由活动"), x + 220, y + 94, RGB(179, 207, 190));
            DrawTextLine(hdc, L"灵石配备", x, panel.top + 270, RGB(172, 205, 182));
            constexpr const wchar_t* labels[] = {L"采集石", L"建筑石", L"战斗石"};
            for (int i = 0; i < kSpiritSlotCount; ++i) {
                const RECT button = AnchorStoneButtonRect(panel, i);
                FillRectColor(hdc, button, npc.spiritStones[i] ? RGB(62, 116, 81) : RGB(50, 59, 56));
                DrawCenteredText(hdc, npc.spiritStones[i] ? std::wstring(labels[i]) + L"  已装备" : labels[i], button,
                                 npc.spiritStones[i] ? RGB(213, 244, 218) : RGB(222, 226, 210), 16);
            }
        } else {
            DrawTextLine(hdc, g_game.npcs.empty() ? L"当前领地没有居民" : L"从左侧选择居民查看资料",
                         panel.left + 342, panel.top + 112, RGB(172, 185, 175));
        }
        return;
    }

    DrawTextLine(hdc, L"建筑蓝图", panel.left + 20, panel.top + 68, RGB(172, 205, 182));
    DrawTextLine(hdc, L"点击可研究的节点完成解锁", panel.left + 600, panel.top + 24, RGB(148, 168, 157));
    HPEN linkPen = CreatePen(PS_SOLID, 2, RGB(84, 112, 98));
    HGDIOBJ oldLinkPen = SelectObject(hdc, linkPen);
    const RECT root = AnchorBlueprintRect(panel, 0);
    for (int i = 1; i < static_cast<int>(kAnchorBlueprints.size()); ++i) {
        const RECT child = AnchorBlueprintRect(panel, i);
        MoveToEx(hdc, root.right, (root.top + root.bottom) / 2, nullptr);
        LineTo(hdc, child.left, (child.top + child.bottom) / 2);
    }
    SelectObject(hdc, oldLinkPen);
    DeleteObject(linkPen);
    for (int i = 0; i < static_cast<int>(kAnchorBlueprints.size()); ++i) {
        const AnchorBlueprintDef& blueprint = kAnchorBlueprints[i];
        const bool unlocked = BlueprintUnlocked(blueprint.id);
        const bool available = BlueprintAvailable(blueprint);
        const RECT node = AnchorBlueprintRect(panel, i);
        FillRectColor(hdc, node, unlocked ? RGB(60, 111, 78) : available ? RGB(57, 67, 62) : RGB(39, 44, 43));
        HPEN nodePen = CreatePen(PS_SOLID, 1, unlocked ? RGB(151, 213, 167) : RGB(102, 119, 109));
        HGDIOBJ oldNodePen = SelectObject(hdc, nodePen);
        HGDIOBJ oldNodeBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, node.left, node.top, node.right, node.bottom);
        SelectObject(hdc, oldNodeBrush);
        SelectObject(hdc, oldNodePen);
        DeleteObject(nodePen);
        DrawTextLine(hdc, blueprint.name, node.left + 12, node.top + 12, unlocked ? RGB(226, 245, 220) : RGB(223, 227, 214));
        DrawTextLine(hdc, unlocked ? L"已解锁" : available ? L"点击解锁" : L"前置未完成",
                     node.left + 12, node.top + 42, unlocked ? RGB(161, 224, 176) : RGB(169, 181, 172));
        DrawTextLine(hdc, blueprint.description, node.right + 18, node.top + 26, RGB(177, 192, 180));
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

RECT DeathPanelRect(const RECT& client) {
    constexpr int width = 760;
    constexpr int height = 500;
    const int left = (client.right - width) / 2;
    const int top = (client.bottom - height) / 2;
    return {left, top, left + width, top + height};
}

RECT DeathNpcRowRect(const RECT& client, int index) {
    const RECT panel = DeathPanelRect(client);
    constexpr int rowsPerColumn = 10;
    constexpr int rowHeight = 38;
    constexpr int columnWidth = 350;
    const int column = index / rowsPerColumn;
    const int row = index % rowsPerColumn;
    const int left = panel.left + 20 + column * (columnWidth + 20);
    const int top = panel.top + 74 + row * 40;
    return {left, top, left + columnWidth, top + rowHeight};
}

int HitDeathNpc(const RECT& client, int x, int y) {
    if (g_game.deathPhase != DeathPhase::AwaitingSacrifice || g_game.deathTime < 0.65f) return -1;
    for (int i = 0; i < static_cast<int>(g_game.npcs.size()) && i < 20; ++i) {
        const RECT row = DeathNpcRowRect(client, i);
        if (PtInRect(&row, POINT{x, y})) return i;
    }
    return -1;
}

void DrawDeathOverlay(HDC hdc, const RECT& client) {
    if (g_game.deathPhase == DeathPhase::None) return;
    float opacity = 0.0f;
    if (g_game.deathPhase == DeathPhase::CompensationIntro || g_game.deathPhase == DeathPhase::FinalEnding) {
        opacity = std::min(1.0f, g_game.deathTime / 1.2f);
    } else if (g_game.deathPhase == DeathPhase::AwaitingSacrifice) {
        opacity = std::max(0.0f, 1.0f - g_game.deathTime / 0.8f);
    } else if (g_game.deathPhase == DeathPhase::RevivalFade) {
        opacity = std::max(0.0f, 1.0f - g_game.deathTime / 1.0f);
    }
    if (opacity > 0.0f && EnsureGdiPlus()) {
        Gdiplus::Graphics graphics(hdc);
        Gdiplus::SolidBrush black(Gdiplus::Color(static_cast<BYTE>(std::lround(opacity * 255.0f)), 0, 0, 0));
        graphics.FillRectangle(&black, static_cast<INT>(client.left), static_cast<INT>(client.top),
                               static_cast<INT>(client.right - client.left), static_cast<INT>(client.bottom - client.top));
    }

    if (g_game.deathPhase == DeathPhase::CompensationIntro && g_game.deathTime >= 0.55f) {
        DrawCenteredText(hdc, L"锚点死亡，影子代偿", client, RGB(235, 235, 226), 30);
    } else if (g_game.deathPhase == DeathPhase::FinalEnding && g_game.deathTime >= 0.55f) {
        DrawCenteredText(hdc, L"你挥霍完所有人的生命，最终无人为你续命", client, RGB(225, 220, 214), 26);
    } else if (g_game.deathPhase == DeathPhase::AwaitingSacrifice) {
        const RECT panel = DeathPanelRect(client);
        if (EnsureGdiPlus()) {
            Gdiplus::Graphics graphics(hdc);
            Gdiplus::SolidBrush panelBrush(Gdiplus::Color(218, 22, 27, 26));
            graphics.FillRectangle(&panelBrush, static_cast<INT>(panel.left), static_cast<INT>(panel.top),
                                   static_cast<INT>(panel.right - panel.left), static_cast<INT>(panel.bottom - panel.top));
        }
        FrameRect(hdc, &panel, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
        RECT title{panel.left + 18, panel.top + 14, panel.right - 18, panel.top + 52};
        DrawCenteredText(hdc, L"选择一名影子代偿锚点死亡", title, RGB(239, 231, 210), 24);
        for (int i = 0; i < static_cast<int>(g_game.npcs.size()) && i < 20; ++i) {
            const Npc& npc = g_game.npcs[i];
            const RECT row = DeathNpcRowRect(client, i);
            FillRectColor(hdc, row, RGB(48, 55, 51));
            const std::wstring label = npc.name + L"    生命 " + std::to_wstring(npc.health) +
                                       L"    好感 " + std::to_wstring(npc.affinity) + L"    " + npc.personality;
            DrawCenteredText(hdc, label, row, RGB(226, 229, 215), 15);
        }
    }
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

    DrawFarmCrops(hdc);

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
        if (!NpcInCurrentScene(npc)) continue;
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
                npc.shadowForm ? L"影子" : (npc.evading ? L"躲避" : (npc.inCombat ? L"战斗" : L"跟随")),
                static_cast<int>(std::round(npc.pos.x * renderScale - g_game.camera.x - 16.0f * renderScale)),
                static_cast<int>(std::round(npc.pos.y * renderScale - g_game.camera.y + 22.0f * renderScale)),
                npc.shadowForm ? RGB(184, 191, 198) :
                    (npc.evading ? RGB(244, 205, 126) : (npc.inCombat ? RGB(244, 126, 126) : RGB(177, 228, 190))));
        } else if (npc.returningHome || npc.taskMode != NpcTaskMode::Idle) {
            DrawTextLine(hdc, NpcTaskLabel(npc),
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

    if (g_game.deathPhase != DeathPhase::AwaitingSacrifice) DrawPlayer(hdc);

    for (const rpg::SceneObject* object : objects) {
        if (!rpg::ObjectIsGroundOverlay(*object) && rpg::ObjectSortY(*object) > playerSortY) {
            rpg::DrawSceneObject(hdc, *object, g_game.camera.x, g_game.camera.y, false, renderScale);
        }
    }

    DrawDebugCollisionOverlay(hdc);
    DrawWorldFog(hdc, client);

    const std::wstring zoomText = L"视角 " + std::to_wstring(static_cast<int>(std::round(renderScale * 100.0f))) + L"%";
    DrawTextLine(hdc, zoomText, 18, 18, RGB(220, 230, 202));
    DrawAnchorButton(hdc);
    DrawTerritoryStatus(hdc);

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
    DrawDeathOverlay(hdc, client);

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
    if (g_game.anchorPanelOpen) {
        g_game.anchorPanelTab = AnchorStorageAvailable() ? AnchorPanelTab::Storage : AnchorPanelTab::Residents;
        if (g_game.anchorSelectedNpc < 0 && !g_game.npcs.empty()) g_game.anchorSelectedNpc = 0;
    }
    g_game.inventory.open = false;
    g_game.inventory.dragging = false;
    g_game.inventory.dragSource = -1;
    g_game.npcContextIndex = -1;
    g_game.showTalk = false;
    if (!g_game.anchorPanelOpen) g_game.anchorSelectedNpc = -1;
}

void SelectAnchorPanelTab(AnchorPanelTab tab) {
    if ((tab == AnchorPanelTab::Storage || tab == AnchorPanelTab::Workbench || tab == AnchorPanelTab::Crafting) &&
        !AnchorStorageAvailable()) return;
    g_game.anchorPanelTab = tab;
    g_game.inventory.dragging = false;
    g_game.inventory.dragSource = -1;
    if (tab == AnchorPanelTab::Workbench) {
        if (g_game.craftingStation == CraftingStation::Workbench) g_game.craftingStation = CraftingStation::Decompose;
        g_game.anchorSelectedWorkbenchRecipe = FirstRecipeForStation(g_game.craftingStation);
    } else if (tab == AnchorPanelTab::Crafting) {
        g_game.anchorSelectedWorkbenchRecipe = FirstRecipeForCraftingCategory(g_game.craftingCategory);
    }
    if ((tab == AnchorPanelTab::Residents || tab == AnchorPanelTab::Work) &&
        g_game.anchorSelectedNpc < 0 && !g_game.npcs.empty()) {
        g_game.anchorSelectedNpc = 0;
    }
}

void AssignSelectedNpcFacility(int index) {
    if (g_game.anchorSelectedNpc < 0 || g_game.anchorSelectedNpc >= static_cast<int>(g_game.npcs.size())) return;
    const auto jobs = AvailableNpcFacilityJobs();
    if (index < 0 || index >= static_cast<int>(jobs.size())) return;
    for (int i = 0; i < static_cast<int>(g_game.npcs.size()); ++i) {
        if (i != g_game.anchorSelectedNpc && g_game.npcs[i].taskMode == NpcTaskMode::Facility &&
            g_game.npcs[i].facilityId == jobs[index].objectId) {
            g_game.pickupNotice = L"该岗位已由 " + g_game.npcs[i].name + L" 负责";
            g_game.pickupNoticeTime = 2.0f;
            return;
        }
    }
    Npc& npc = g_game.npcs[g_game.anchorSelectedNpc];
    npc.taskMode = NpcTaskMode::Facility;
    npc.facilityId = jobs[index].objectId;
    npc.workMap = g_currentScenePath.filename().wstring();
    npc.gatheringObjectId.clear();
    npc.following = false;
    npc.returningHome = false;
    npc.inCombat = false;
    npc.workTimer = 0.0f;
    g_game.pickupNotice = npc.name + L" 已分配至 " + jobs[index].name;
    g_game.pickupNoticeTime = 2.0f;
    SaveCurrentGame();
}

void SelectNpcGatheringTarget(int index) {
    if (g_game.anchorSelectedNpc < 0 || g_game.anchorSelectedNpc >= static_cast<int>(g_game.npcs.size())) return;
    constexpr const char* targets[] = {"any", "wood", "exotic_wood", "stone"};
    if (index < 0 || index >= static_cast<int>(std::size(targets))) return;
    g_game.npcs[g_game.anchorSelectedNpc].gatheringTarget = targets[index];
    SaveCurrentGame();
}

void ExecuteNpcWorkAction(NpcWorkAction action) {
    if (action == NpcWorkAction::None || g_game.anchorSelectedNpc < 0 ||
        g_game.anchorSelectedNpc >= static_cast<int>(g_game.npcs.size())) return;
    Npc& npc = g_game.npcs[g_game.anchorSelectedNpc];
    if (action == NpcWorkAction::StartGathering) {
        if (!npc.spiritStones[0]) {
            g_game.pickupNotice = L"需要先给 " + npc.name + L" 装备采集石";
            g_game.pickupNoticeTime = 2.0f;
            return;
        }
        npc.taskMode = NpcTaskMode::Gathering;
        npc.workMap = g_currentScenePath.filename().wstring();
        npc.facilityId.clear();
        npc.gatheringObjectId.clear();
        npc.following = false;
        npc.returningHome = false;
        npc.inCombat = false;
        npc.workTimer = 0.0f;
        g_game.pickupNotice = npc.name + L" 已开始外出采集";
    } else if (action == NpcWorkAction::ReturnHome) {
        npc.taskMode = NpcTaskMode::Returning;
        npc.gatheringObjectId.clear();
        npc.following = false;
        npc.inCombat = false;
        npc.workTimer = 0.0f;
        g_game.pickupNotice = npc.name + L" 正在立即返回领地";
    } else {
        const bool needsReturn = !ActorProtectedByTerritory(npc.pos) || NpcCargoUsedSlots(npc) > 0;
        npc.taskMode = needsReturn ? NpcTaskMode::Returning : NpcTaskMode::Idle;
        npc.facilityId.clear();
        npc.gatheringObjectId.clear();
        npc.following = false;
        npc.inCombat = false;
        npc.workTimer = 0.0f;
        g_game.pickupNotice = npc.taskMode == NpcTaskMode::Idle ? npc.name + L" 已停止任务"
                                                                : npc.name + L" 已停止任务，正在返程卸货";
    }
    g_game.pickupNoticeTime = 2.0f;
    SaveCurrentGame();
}

void StartCraftedBuildingPlacement(const WorkbenchRecipeDef& recipe) {
    g_game.inventory.reservedBuildingMaterials.clear();
    for (int i = 0; i < recipe.ingredientCount; ++i) {
        const auto& ingredient = recipe.ingredients[i];
        ConsumeStoredItem(ingredient.id, ingredient.count);
        g_game.inventory.reservedBuildingMaterials.push_back(MakeItemStack(ingredient.id, ingredient.count));
    }
    g_game.inventory.placingBuilding = true;
    g_game.inventory.placingAnchor = false;
    g_game.inventory.movingExistingBuilding = false;
    g_game.inventory.placingCraftedBuilding = true;
    g_game.inventory.placementSourceSlot = -1;
    g_game.inventory.placementObjectType = recipe.outputId;
    g_game.anchorPanelOpen = false;
    g_game.inventory.open = false;
    const POINT mouse = g_game.inventory.mousePoint;
    UpdateBuildingPlacementPreview(mouse.x, mouse.y);
    g_game.pickupNotice = std::wstring(L"放置 ") + recipe.name + L"：左键确认，右键取消并退回材料";
    g_game.pickupNoticeTime = 3.0f;
    SaveCurrentGame();
}

void CraftWorkbenchRecipe(int index) {
    if (!AnchorStorageAvailable() || index < 0 || index >= static_cast<int>(kWorkbenchRecipes.size())) return;
    const WorkbenchRecipeDef& recipe = kWorkbenchRecipes[index];
    if (!RecipeFacilityAvailable(recipe)) {
        g_game.pickupNotice = recipe.requiredFacilityType && std::string_view(recipe.requiredFacilityType) == "sawmill"
            ? L"领地内需要先放置锯木台" : L"领地内需要先放置熔炉";
    } else if (recipe.requiredBlueprint && !BlueprintUnlocked(recipe.requiredBlueprint)) {
        g_game.pickupNotice = L"需要先在科技树解锁该蓝图";
    } else {
        for (int i = 0; i < recipe.ingredientCount; ++i) {
            if (StoredItemCount(recipe.ingredients[i].id) < recipe.ingredients[i].count) {
                g_game.pickupNotice = L"制作材料不足";
                g_game.pickupNoticeTime = 2.0f;
                return;
            }
        }
        if (RecipeIsBuilding(recipe)) {
            StartCraftedBuildingPlacement(recipe);
            return;
        }
        if (!CanStoreWorkbenchOutput(recipe.outputId, recipe.outputCount)) {
            g_game.pickupNotice = L"主角背包已满";
            g_game.pickupNoticeTime = 2.0f;
            return;
        }
        ItemStack output = MakeItemStack(recipe.outputId, recipe.outputCount);
        if (output.id.empty()) {
            g_game.pickupNotice = L"物品定义缺失";
        } else {
            for (int i = 0; i < recipe.ingredientCount; ++i) {
                ConsumeStoredItem(recipe.ingredients[i].id, recipe.ingredients[i].count);
            }
            StoreItem(output);
            g_game.pickupNotice = std::wstring(L"制作完成：") + recipe.name;
            SaveCurrentGame();
        }
    }
    g_game.pickupNoticeTime = 2.0f;
}

void UnlockAnchorBlueprint(int index) {
    if (index < 0 || index >= static_cast<int>(kAnchorBlueprints.size())) return;
    const AnchorBlueprintDef& blueprint = kAnchorBlueprints[index];
    if (BlueprintUnlocked(blueprint.id)) {
        g_game.pickupNotice = std::wstring(blueprint.name) + L" 已经解锁";
    } else if (!BlueprintAvailable(blueprint)) {
        g_game.pickupNotice = L"需要先完成前置科技";
    } else {
        g_game.unlockedBlueprints.insert(blueprint.id);
        g_game.pickupNotice = std::wstring(L"已解锁建筑蓝图：") + blueprint.name;
        SaveCurrentGame();
    }
    g_game.pickupNoticeTime = 2.0f;
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
    if (!HeldSpiritSkill(2)) {
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
            SpawnMonsterLoot(monster.pos);
            g_game.activeCombatMonster = -1;
            for (Npc& npc : g_game.npcs) npc.inCombat = false;
        } else {
            EnterFollowerCombat(target);
        }
    }
}

const char* GatheringDropForObject(const rpg::SceneObject& object) {
    if (object.type == "tree_oak") return "wood";
    if (object.type == "exotic_tree_01") return "exotic_wood";
    if (object.type == "stone_round") return "stone";
    return nullptr;
}

void StartGathering() {
    if (!HeldSpiritSkill(0)) {
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
    if (g_screen == AppScreen::Playing && g_game.deathPhase != DeathPhase::None &&
        (msg == WM_MOUSEMOVE || msg == WM_MOUSELEAVE || msg == WM_MOUSEWHEEL ||
         msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP || msg == WM_RBUTTONDOWN ||
         msg == WM_RBUTTONUP || msg == WM_LBUTTONUP || msg == WM_CAPTURECHANGED)) {
        return 0;
    }
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
        if (g_game.deathPhase != DeathPhase::None) return 0;
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
        if (g_screen != AppScreen::Playing || g_game.deathPhase != DeathPhase::None || !g_console.open) {
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
        if (g_game.deathPhase != DeathPhase::None) return 0;
        if (g_console.open) return 0;
        if (g_game.mapOpen) return 0;
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
        if (g_game.deathPhase != DeathPhase::None) {
            if (const int npc = HitDeathNpc(client, x, y); npc >= 0) SacrificeNpc(npc);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (g_console.open || g_game.mapOpen) return 0;
        if (const int tool = HitBuildingToolButton(client, x, y); tool >= 0) {
            if (g_game.inventory.placingBuilding) CancelBuildingPlacement();
            g_game.inventory.selectingTerritory = false;
            g_game.inventory.selectionRemoving = false;
            g_game.inventory.buildingToolMode = static_cast<BuildingToolMode>(tool);
            constexpr const wchar_t* labels[] = {L"移动建筑", L"创建或删除耕地", L"扩张或收回领地"};
            g_game.pickupNotice = labels[tool];
            g_game.pickupNoticeTime = 1.5f;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (g_game.inventory.placingBuilding) {
            UpdateBuildingPlacementPreview(x, y);
            PlaceBuildingAtPreview();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
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
        if (g_game.anchorPanelOpen) {
            if (const int tab = HitAnchorTab(client, x, y); tab >= 0) {
                SelectAnchorPanelTab(static_cast<AnchorPanelTab>(tab));
                InvalidateRect(hwnd, nullptr, FALSE);
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
            if (const int station = HitCraftingStation(client, x, y); station >= 0) {
                g_game.craftingStation = static_cast<CraftingStation>(station);
                g_game.anchorSelectedWorkbenchRecipe = FirstRecipeForStation(g_game.craftingStation);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (const int category = HitCraftingCategory(client, x, y); category >= 0) {
                g_game.craftingCategory = static_cast<CraftingCategory>(category);
                g_game.anchorSelectedWorkbenchRecipe = FirstRecipeForCraftingCategory(g_game.craftingCategory);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (const int recipeRow = HitWorkbenchRecipeRow(client, x, y); recipeRow >= 0) {
                g_game.anchorSelectedWorkbenchRecipe = recipeRow;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (const int recipe = HitWorkbenchRecipe(client, x, y); recipe >= 0) {
                CraftWorkbenchRecipe(recipe);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (const int facility = HitNpcWorkFacility(client, x, y); facility >= 0) {
                AssignSelectedNpcFacility(facility);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (const int target = HitNpcGatherTarget(client, x, y); target >= 0) {
                SelectNpcGatheringTarget(target);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (const NpcWorkAction action = HitNpcWorkAction(client, x, y); action != NpcWorkAction::None) {
                ExecuteNpcWorkAction(action);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (const int blueprint = HitAnchorBlueprint(client, x, y); blueprint >= 0) {
                UnlockAnchorBlueprint(blueprint);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
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
                    std::wstring reason;
                    if (CanAcceptWanderingShadow(&reason)) {
                        shadow.state = WandererState::Accepted;
                        shadow.age = 0.0f;
                        g_game.pickupNotice = L"你收留了流浪影子，它正在走向领地";
                        g_game.shadowDialogIndex = -1;
                    } else {
                        g_game.pickupNotice = std::move(reason);
                    }
                    g_game.pickupNoticeTime = 2.0f;
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
                            npc.evading = false;
                            npc.threatMonster = -1;
                            npc.evadeTime = 0.0f;
                            npc.moving = false;
                            npc.returningHome = !ActorProtectedByTerritory(npc.pos);
                            g_game.pickupNotice = npc.name + (npc.returningHome ? L" 已取消跟随，正在返回领地" : L" 已取消跟随");
                        } else if (FollowerCount() >= kMaximumFollowers) {
                            g_game.pickupNotice = L"最多只能有 4 名跟随者";
                        } else {
                            npc.following = true;
                            npc.taskMode = NpcTaskMode::Idle;
                            npc.facilityId.clear();
                            npc.gatheringObjectId.clear();
                            npc.workMap = g_currentScenePath.filename().wstring();
                            npc.returningHome = false;
                            npc.inCombat = false;
                            npc.evading = false;
                            npc.threatMonster = -1;
                            npc.evadeTime = 0.0f;
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
        if (HeldSpiritSkill(1)) {
            const POINT tile = ScreenToTile(x, y);
            if (g_game.inventory.buildingToolMode == BuildingToolMode::Move) {
                if (BeginMovingBuildingAt(tile)) UpdateBuildingPlacementPreview(x, y);
            } else {
                g_game.inventory.selectingTerritory = true;
                g_game.inventory.selectionRemoving = false;
                g_game.inventory.territoryStartTile = tile;
                g_game.inventory.territoryEndTile = tile;
                SetCapture(hwnd);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (InteractWithFarmland(x, y)) {
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
        if (HeldSpiritSkill(0)) StartGathering();
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
        if (g_game.inventory.selectingTerritory &&
            ((wParam & MK_RBUTTON) || (wParam & MK_LBUTTON))) {
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
        if (g_game.inventory.placingBuilding) {
            CancelBuildingPlacement();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (g_screen == AppScreen::Playing && !g_game.inventory.open && HeldSpiritSkill(1) &&
            g_game.inventory.buildingToolMode != BuildingToolMode::Move) {
            g_game.inventory.selectingTerritory = true;
            g_game.inventory.selectionRemoving = true;
            g_game.inventory.territoryStartTile = ScreenToTile(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            g_game.inventory.territoryEndTile = g_game.inventory.territoryStartTile;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
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
        return 0;
    case WM_RBUTTONUP:
        if (g_game.inventory.selectingTerritory && g_game.inventory.selectionRemoving) {
            g_game.inventory.territoryEndTile = ScreenToTile(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            FinishBuildingToolSelection();
            ReleaseCapture();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_game.inventory.selectingTerritory && !g_game.inventory.selectionRemoving) {
            g_game.inventory.territoryEndTile = ScreenToTile(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            FinishBuildingToolSelection();
            ReleaseCapture();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (g_game.inventory.selectingTerritory) {
            g_game.inventory.selectingTerritory = false;
            g_game.inventory.selectionRemoving = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
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
