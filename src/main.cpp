#include <windows.h>

#include "scene.h"
#include "scene_render.h"
#include "terrain_render.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <gdiplus.h>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr int kWindowWidth = 960;
constexpr int kWindowHeight = 640;
constexpr int kTileSize = rpg::kTileSize;
constexpr int kMapWidth = rpg::kMapWidth;
constexpr int kMapHeight = rpg::kMapHeight;
constexpr float kPlayerRadius = 16.0f;
constexpr float kPlayerSpeed = 190.0f;
constexpr int kSpriteFrameSize = 96;
constexpr int kSpriteDrawSize = 96;
constexpr int kSideWalkFrames = 8;
constexpr int kCardinalWalkFrames = 4;
constexpr UINT_PTR kFrameTimer = 1;
constexpr UINT kFrameMs = 16;

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
    HBITMAP bitmap = nullptr;
    HDC dc = nullptr;
    bool loaded = false;
};

struct Npc {
    Vec2 pos;
    const wchar_t* name;
    const wchar_t* text;
};

struct Game {
    Player player;
    rpg::Scene scene;
    Vec2 camera{};
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool interact = false;
    bool showTalk = false;
    int nearbyNpc = -1;
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
        static_cast<INT>(std::round(rpg::WorldWidth())),
        static_cast<INT>(std::round(rpg::WorldHeight())));
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
        pos.x + radius > rpg::WorldWidth() ||
        pos.y + radius > rpg::WorldHeight()) {
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

void LoadPlayerSprite(HDC target) {
    if (g_playerSprite.loaded) {
        return;
    }

    const std::wstring spritePath = AssetPath(L"player_walk.bmp").wstring();
    g_playerSprite.bitmap = static_cast<HBITMAP>(LoadImageW(
        nullptr,
        spritePath.c_str(),
        IMAGE_BITMAP,
        0,
        0,
        LR_LOADFROMFILE));

    if (!g_playerSprite.bitmap) {
        g_playerSprite.loaded = true;
        return;
    }

    g_playerSprite.dc = CreateCompatibleDC(target);
    SelectObject(g_playerSprite.dc, g_playerSprite.bitmap);
    g_playerSprite.loaded = true;
}

void LoadGameScene() {
    rpg::ReloadObjectDefs();
    rpg::ReloadTerrainDefs();
    std::string error;
    const std::filesystem::path scenePath = g_requestedScenePath.empty()
        ? AssetPath(L"scenes/demo_scene.json")
        : g_requestedScenePath;
    if (!rpg::LoadSceneFromFile(scenePath, g_game.scene, &error)) {
        g_game.scene = rpg::MakeDefaultScene();
        rpg::SaveSceneToFile(scenePath, g_game.scene);
    }
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

    UpdateNearbyNpc();
    if (g_game.interact && g_game.nearbyNpc >= 0) {
        g_game.showTalk = !g_game.showTalk;
    }
    if (g_game.nearbyNpc < 0) {
        g_game.showTalk = false;
    }
    g_game.interact = false;

    const float worldW = static_cast<float>(kMapWidth * kTileSize);
    const float worldH = static_cast<float>(kMapHeight * kTileSize);
    g_game.camera.x = Clamp(g_game.player.pos.x - kWindowWidth * 0.5f, 0.0f, std::max(0.0f, worldW - kWindowWidth));
    g_game.camera.y = Clamp(g_game.player.pos.y - kWindowHeight * 0.5f, 0.0f, std::max(0.0f, worldH - kWindowHeight));
}

RECT WorldRect(float x, float y, float w, float h) {
    return RECT{
        static_cast<LONG>(std::round(x - g_game.camera.x)),
        static_cast<LONG>(std::round(y - g_game.camera.y)),
        static_cast<LONG>(std::round(x + w - g_game.camera.x)),
        static_cast<LONG>(std::round(y + h - g_game.camera.y)),
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
        static_cast<int>(center.x - rx - g_game.camera.x),
        static_cast<int>(center.y - ry - g_game.camera.y),
        static_cast<int>(center.x + rx - g_game.camera.x),
        static_cast<int>(center.y + ry - g_game.camera.y));
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void DrawPlayer(HDC hdc) {
    LoadPlayerSprite(hdc);
    if (!g_playerSprite.bitmap || !g_playerSprite.dc) {
        DrawEllipse(hdc, g_game.player.pos, kPlayerRadius, 20.0f, RGB(93, 176, 219), RGB(22, 73, 96));
        return;
    }

    const bool moving = std::fabs(g_game.player.vel.x) + std::fabs(g_game.player.vel.y) > 1.0f;
    const bool sideWalk = g_game.player.dir == 1 || g_game.player.dir == 2;
    const int frameCount = sideWalk ? kSideWalkFrames : kCardinalWalkFrames;
    const int frame = moving ? (static_cast<int>(g_game.player.animTime * 10.0f) % frameCount) : 0;
    const bool mirror = g_game.player.dir == 1;

    int sx = 0;
    int sy = 0;
    if (sideWalk) {
        sx = (frame % 4) * kSpriteFrameSize;
        sy = (frame / 4) * kSpriteFrameSize;
    } else if (g_game.player.dir == 0) {
        sx = frame * kSpriteFrameSize;
        sy = 2 * kSpriteFrameSize;
    } else {
        sx = frame * kSpriteFrameSize;
        sy = 3 * kSpriteFrameSize;
    }

    const int dx = static_cast<int>(std::round(g_game.player.pos.x - g_game.camera.x - kSpriteDrawSize * 0.5f));
    const int dy = static_cast<int>(std::round(g_game.player.pos.y - g_game.camera.y - kSpriteDrawSize + 12));

    if (mirror) {
        HDC mirrorDc = CreateCompatibleDC(hdc);
        HBITMAP mirrorBitmap = CreateCompatibleBitmap(hdc, kSpriteFrameSize, kSpriteFrameSize);
        HGDIOBJ oldBitmap = SelectObject(mirrorDc, mirrorBitmap);
        RECT clearRect{0, 0, kSpriteFrameSize, kSpriteFrameSize};
        FillRectColor(mirrorDc, clearRect, RGB(255, 0, 255));
        SetStretchBltMode(mirrorDc, COLORONCOLOR);
        StretchBlt(
            mirrorDc,
            0,
            0,
            kSpriteFrameSize,
            kSpriteFrameSize,
            g_playerSprite.dc,
            sx + kSpriteFrameSize,
            sy,
            -kSpriteFrameSize,
            kSpriteFrameSize,
            SRCCOPY);

        TransparentBlt(
            hdc,
            dx,
            dy,
            kSpriteDrawSize,
            kSpriteDrawSize,
            mirrorDc,
            0,
            0,
            kSpriteFrameSize,
            kSpriteFrameSize,
            RGB(255, 0, 255));

        SelectObject(mirrorDc, oldBitmap);
        DeleteObject(mirrorBitmap);
        DeleteDC(mirrorDc);
        return;
    }

    TransparentBlt(
        hdc,
        dx,
        dy,
        kSpriteDrawSize,
        kSpriteDrawSize,
        g_playerSprite.dc,
        sx,
        sy,
        kSpriteFrameSize,
        kSpriteFrameSize,
        RGB(255, 0, 255));
}

void DrawTextLine(HDC hdc, const std::wstring& text, int x, int y, COLORREF color) {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    TextOutW(hdc, x, y, text.c_str(), static_cast<int>(text.size()));
}

void RenderGame(HWND hwnd, HDC target) {
    RECT client{};
    GetClientRect(hwnd, &client);

    HDC hdc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
    HGDIOBJ oldBitmap = SelectObject(hdc, bitmap);

    FillRectColor(hdc, client, RGB(41, 49, 47));
    DrawBackgroundImage(hdc);

    rpg::DrawTerrain(hdc, g_game.scene, g_game.camera.x, g_game.camera.y);

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
            rpg::DrawSceneObject(hdc, *object, g_game.camera.x, g_game.camera.y);
        }
    }

    for (const Npc& npc : kNpcs) {
        DrawEllipse(hdc, npc.pos, 15.0f, 19.0f, RGB(222, 185, 94), RGB(76, 55, 32));
        DrawTextLine(hdc, npc.name, static_cast<int>(npc.pos.x - g_game.camera.x - 18), static_cast<int>(npc.pos.y - g_game.camera.y - 38), RGB(245, 244, 230));
    }

    for (const rpg::SceneObject* object : objects) {
        if (!rpg::ObjectIsGroundOverlay(*object) && rpg::ObjectSortY(*object) <= playerSortY) {
            rpg::DrawSceneObject(hdc, *object, g_game.camera.x, g_game.camera.y);
        }
    }

    DrawPlayer(hdc);

    for (const rpg::SceneObject* object : objects) {
        if (!rpg::ObjectIsGroundOverlay(*object) && rpg::ObjectSortY(*object) > playerSortY) {
            rpg::DrawSceneObject(hdc, *object, g_game.camera.x, g_game.camera.y);
        }
    }

    DrawTextLine(hdc, L"WASD / arrow keys to move, E to talk near an NPC", 18, 18, RGB(248, 248, 235));

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

    BitBlt(target, 0, 0, client.right, client.bottom, hdc, 0, 0, SRCCOPY);

    SelectObject(hdc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(hdc);
}

void SetKey(WPARAM key, bool pressed) {
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
    default:
        break;
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
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
        SetKey(wParam, true);
        return 0;
    case WM_KEYUP:
        SetKey(wParam, false);
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
        if (g_gdiplusToken != 0) {
            Gdiplus::GdiplusShutdown(g_gdiplusToken);
            g_gdiplusToken = 0;
        }
        rpg::ReleaseSceneRenderResources();
        rpg::ReleaseTerrainRenderResources();
        if (g_playerSprite.dc) {
            DeleteDC(g_playerSprite.dc);
            g_playerSprite.dc = nullptr;
        }
        if (g_playerSprite.bitmap) {
            DeleteObject(g_playerSprite.bitmap);
            g_playerSprite.bitmap = nullptr;
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
