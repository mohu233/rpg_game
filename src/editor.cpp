#include <windows.h>
#include <windowsx.h>

#include "scene.h"
#include "scene_render.h"
#include "terrain_render.h"
#include "world.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr int kWindowWidth = 1180;
constexpr int kWindowHeight = 760;
constexpr int kPaletteWidth = 190;
constexpr int kPaletteTabsY = 52;
constexpr int kPaletteTabsHeight = 32;
constexpr int kPaletteFirstY = 100;
constexpr int kPaletteRowHeight = 56;
constexpr int kPaletteFooterHeight = 150;
constexpr int kTileSize = rpg::kTileSize;

#ifndef RPG_ASSET_DIR
#define RPG_ASSET_DIR L"assets"
#endif

enum class EditorMode {
    Objects,
    Natural,
    Built,
};

struct EditorState {
    rpg::Scene scene;
    EditorMode mode = EditorMode::Objects;
    std::string selectedType = "tree_oak";
    std::string selectedNatural = "grass";
    std::string selectedBuilt = "stone_floor";
    int selectedObject = -1;
    int nextObjectId = 1;
    bool dragging = false;
    bool paintingTerrain = false;
    int lastPaintTileX = -1;
    int lastPaintTileY = -1;
    bool dirty = false;
    bool showCollision = true;
    float cameraX = 0.0f;
    float cameraY = 0.0f;
    int paletteScroll = 0;
    rpg::Vec2 dragStartWorld{};
    rpg::Vec2 dragStartObject{};
    std::wstring status = L"Ready";
};

EditorState g_editor;

std::filesystem::path AssetPath(const wchar_t* relative) {
    return std::filesystem::path(RPG_ASSET_DIR) / relative;
}

float Clamp(float value, float minValue, float maxValue) {
    return std::max(minValue, std::min(maxValue, value));
}

void FillRectColor(HDC hdc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
}

void DrawTextLine(HDC hdc, const std::wstring& text, int x, int y, COLORREF color) {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    TextOutW(hdc, x, y, text.c_str(), static_cast<int>(text.size()));
}

void ClampCamera(HWND hwnd) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const float viewportW = static_cast<float>(std::max<LONG>(1, client.right - kPaletteWidth));
    const float viewportH = static_cast<float>(std::max<LONG>(1, client.bottom));
    g_editor.cameraX = Clamp(g_editor.cameraX, 0.0f, std::max(0.0f, rpg::WorldWidth() - viewportW));
    g_editor.cameraY = Clamp(g_editor.cameraY, 0.0f, std::max(0.0f, rpg::WorldHeight() - viewportH));
}

rpg::Vec2 ScreenToWorld(int x, int y) {
    return {
        static_cast<float>(x - kPaletteWidth) + g_editor.cameraX,
        static_cast<float>(y) + g_editor.cameraY,
    };
}

int PaletteItemCount() {
    switch (g_editor.mode) {
    case EditorMode::Natural:
        return static_cast<int>(rpg::NaturalTerrainDefs().size());
    case EditorMode::Built:
        return static_cast<int>(rpg::BuiltTerrainDefs().size()) + 1;
    case EditorMode::Objects:
    default:
        return static_cast<int>(rpg::ObjectDefs().size());
    }
}

int PaletteTabHitTest(int x, int y) {
    if (x < 8 || x >= kPaletteWidth - 8 || y < kPaletteTabsY || y >= kPaletteTabsY + kPaletteTabsHeight) {
        return -1;
    }
    const int tabWidth = (kPaletteWidth - 16) / 3;
    return std::min(2, (x - 8) / tabWidth);
}

void ClampPaletteScroll(HWND hwnd) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const int availableHeight = static_cast<int>(std::max<LONG>(0, client.bottom - kPaletteFooterHeight - kPaletteFirstY));
    const int contentHeight = PaletteItemCount() * kPaletteRowHeight;
    g_editor.paletteScroll = std::max(0, std::min(g_editor.paletteScroll, std::max(0, contentHeight - availableHeight)));
}

int PaletteHitTest(HWND hwnd, int x, int y) {
    if (x < 0 || x >= kPaletteWidth) {
        return -1;
    }

    RECT client{};
    GetClientRect(hwnd, &client);
    if (y < kPaletteFirstY || y >= client.bottom - kPaletteFooterHeight) {
        return -1;
    }

    const int index = (y - kPaletteFirstY + g_editor.paletteScroll) / kPaletteRowHeight;
    if (index < 0 || index >= PaletteItemCount()) {
        return -1;
    }
    return index;
}

int HitObject(rpg::Vec2 point) {
    std::vector<int> indices;
    indices.reserve(g_editor.scene.objects.size());
    for (int i = 0; i < static_cast<int>(g_editor.scene.objects.size()); ++i) {
        indices.push_back(i);
    }

    std::stable_sort(indices.begin(), indices.end(), [](int a, int b) {
        return rpg::ObjectSortY(g_editor.scene.objects[a]) < rpg::ObjectSortY(g_editor.scene.objects[b]);
    });

    for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
        if (rpg::PointInObjectVisual(g_editor.scene.objects[*it], point)) {
            return *it;
        }
    }
    return -1;
}

void MarkDirty(const std::wstring& status) {
    g_editor.dirty = true;
    g_editor.status = status;
}

void LoadEditorScene() {
    rpg::ReleaseSceneRenderResources();
    rpg::ReleaseTerrainRenderResources();
    std::string objectError;
    const bool objectsLoaded = rpg::ReloadObjectDefs(&objectError);
    std::string terrainError;
    const bool terrainLoaded = rpg::ReloadTerrainDefs(&terrainError);

    const auto& defs = rpg::ObjectDefs();
    if (!defs.empty() && !rpg::FindObjectDef(g_editor.selectedType)) {
        g_editor.selectedType = defs.front().type;
    }
    const auto& naturalDefs = rpg::NaturalTerrainDefs();
    if (!naturalDefs.empty() && !rpg::FindTerrainDef(g_editor.selectedNatural, rpg::TerrainLayer::Natural)) {
        g_editor.selectedNatural = naturalDefs.front().id;
    }
    const auto& builtDefs = rpg::BuiltTerrainDefs();
    if (!builtDefs.empty() && g_editor.selectedBuilt != "none" &&
        !rpg::FindTerrainDef(g_editor.selectedBuilt, rpg::TerrainLayer::Built)) {
        g_editor.selectedBuilt = builtDefs.front().id;
    }

    std::string error;
    const std::filesystem::path scenePath = AssetPath(L"scenes/demo_scene.json");
    if (!rpg::LoadSceneFromFile(scenePath, g_editor.scene, &error)) {
        g_editor.scene = rpg::MakeDefaultScene();
        rpg::SaveSceneToFile(scenePath, g_editor.scene);
        g_editor.status = L"Created default scene";
    } else {
        g_editor.status = L"Loaded scene";
    }

    g_editor.selectedObject = -1;
    g_editor.nextObjectId = static_cast<int>(g_editor.scene.objects.size()) + 1;
    g_editor.dirty = false;

    if (!objectsLoaded || !terrainLoaded) {
        g_editor.status = L"Module loading failed";
    } else if (!objectError.empty() || !terrainError.empty()) {
        g_editor.status = L"Loaded with module warnings";
    } else {
        g_editor.status = L"Loaded scene modules";
    }
}

void SaveEditorScene() {
    std::string error;
    if (rpg::SaveSceneToFile(AssetPath(L"scenes/demo_scene.json"), g_editor.scene, &error)) {
        g_editor.status = L"Saved scene";
        g_editor.dirty = false;
    } else {
        g_editor.status = L"Save failed";
    }
}

void AddObjectAt(rpg::Vec2 point) {
    if (!rpg::FindObjectDef(g_editor.selectedType)) {
        g_editor.status = L"No valid object type selected";
        return;
    }

    point.x = Clamp(point.x, 0.0f, rpg::WorldWidth());
    point.y = Clamp(point.y, 0.0f, rpg::WorldHeight());

    rpg::SceneObject object = rpg::MakeObject(g_editor.selectedType, point, g_editor.nextObjectId++);
    g_editor.scene.objects.push_back(object);
    g_editor.selectedObject = static_cast<int>(g_editor.scene.objects.size()) - 1;
    g_editor.dragging = true;
    g_editor.dragStartWorld = point;
    g_editor.dragStartObject = point;
    MarkDirty(L"Placed object");
}

void DeleteSelectedObject() {
    if (g_editor.selectedObject < 0 || g_editor.selectedObject >= static_cast<int>(g_editor.scene.objects.size())) {
        return;
    }
    g_editor.scene.objects.erase(g_editor.scene.objects.begin() + g_editor.selectedObject);
    g_editor.selectedObject = -1;
    MarkDirty(L"Deleted object");
}

void MoveSelectedObject(rpg::Vec2 point) {
    if (g_editor.selectedObject < 0 || g_editor.selectedObject >= static_cast<int>(g_editor.scene.objects.size())) {
        return;
    }

    rpg::Vec2 delta{point.x - g_editor.dragStartWorld.x, point.y - g_editor.dragStartWorld.y};
    rpg::SceneObject& object = g_editor.scene.objects[g_editor.selectedObject];
    object.pos.x = Clamp(g_editor.dragStartObject.x + delta.x, 0.0f, rpg::WorldWidth());
    object.pos.y = Clamp(g_editor.dragStartObject.y + delta.y, 0.0f, rpg::WorldHeight());
    MarkDirty(L"Moved object");
}

void PaintTerrainCell(int tx, int ty) {
    if (rpg::IsWallTile(tx, ty)) {
        return;
    }

    bool changed = false;
    if (g_editor.mode == EditorMode::Natural) {
        changed = rpg::SetNaturalTerrain(g_editor.scene, tx, ty, g_editor.selectedNatural);
    } else if (g_editor.mode == EditorMode::Built) {
        changed = rpg::SetBuiltTerrain(g_editor.scene, tx, ty, g_editor.selectedBuilt);
    }
    if (changed) {
        MarkDirty(g_editor.mode == EditorMode::Natural ? L"Painted natural terrain" : L"Painted built floor");
    }
}

void PaintTerrainAt(rpg::Vec2 point) {
    const int targetX = static_cast<int>(std::floor(point.x / kTileSize));
    const int targetY = static_cast<int>(std::floor(point.y / kTileSize));
    if (targetX < 0 || targetY < 0 || targetX >= rpg::kMapWidth || targetY >= rpg::kMapHeight) {
        return;
    }

    int x = g_editor.lastPaintTileX >= 0 ? g_editor.lastPaintTileX : targetX;
    int y = g_editor.lastPaintTileY >= 0 ? g_editor.lastPaintTileY : targetY;
    const int dx = std::abs(targetX - x);
    const int sx = x < targetX ? 1 : -1;
    const int dy = -std::abs(targetY - y);
    const int sy = y < targetY ? 1 : -1;
    int error = dx + dy;

    for (;;) {
        PaintTerrainCell(x, y);
        if (x == targetX && y == targetY) {
            break;
        }
        const int twiceError = error * 2;
        if (twiceError >= dy) {
            error += dy;
            x += sx;
        }
        if (twiceError <= dx) {
            error += dx;
            y += sy;
        }
    }

    g_editor.lastPaintTileX = targetX;
    g_editor.lastPaintTileY = targetY;
}

rpg::TerrainDef const* NaturalTerrainForIndex(int index) {
    const auto& defs = rpg::NaturalTerrainDefs();
    return index >= 0 && index < static_cast<int>(defs.size()) ? &defs[index] : nullptr;
}

rpg::TerrainDef const* BuiltTerrainForIndex(int index) {
    const auto& defs = rpg::BuiltTerrainDefs();
    const int defIndex = index - 1;
    return defIndex >= 0 && defIndex < static_cast<int>(defs.size()) ? &defs[defIndex] : nullptr;
}

void DrawPalette(HDC hdc, const RECT& client) {
    RECT panel{0, 0, kPaletteWidth, client.bottom};
    FillRectColor(hdc, panel, RGB(38, 44, 48));

    DrawTextLine(hdc, L"Scene Editor", 18, 18, RGB(244, 242, 225));

    constexpr const wchar_t* tabNames[] = {L"Objects", L"Natural", L"Built"};
    const int tabWidth = (kPaletteWidth - 16) / 3;
    for (int i = 0; i < 3; ++i) {
        const bool selected = static_cast<int>(g_editor.mode) == i;
        RECT tab{
            8 + i * tabWidth,
            kPaletteTabsY,
            i == 2 ? kPaletteWidth - 8 : 8 + (i + 1) * tabWidth,
            kPaletteTabsY + kPaletteTabsHeight,
        };
        FillRectColor(hdc, tab, selected ? RGB(79, 102, 87) : RGB(51, 59, 62));
        DrawTextLine(hdc, tabNames[i], tab.left + 5, tab.top + 8, selected ? RGB(248, 245, 220) : RGB(185, 193, 188));
    }

    const auto& defs = rpg::ObjectDefs();
    SaveDC(hdc);
    IntersectClipRect(hdc, 0, kPaletteFirstY, kPaletteWidth, std::max<LONG>(kPaletteFirstY, client.bottom - kPaletteFooterHeight));
    for (int i = 0; i < PaletteItemCount(); ++i) {
        bool selected = false;
        std::wstring label;
        COLORREF swatch = RGB(56, 65, 68);
        bool showSwatch = false;

        if (g_editor.mode == EditorMode::Objects) {
            selected = defs[i].type == g_editor.selectedType;
            label = defs[i].displayName;
        } else if (g_editor.mode == EditorMode::Natural) {
            if (const rpg::TerrainDef* terrain = NaturalTerrainForIndex(i)) {
                selected = terrain->id == g_editor.selectedNatural;
                label = terrain->displayName;
                swatch = rpg::TerrainFallbackColor(*terrain);
                showSwatch = true;
            }
        } else {
            const rpg::TerrainDef* terrain = BuiltTerrainForIndex(i);
            selected = terrain ? terrain->id == g_editor.selectedBuilt : g_editor.selectedBuilt == "none";
            label = terrain ? terrain->displayName : L"Erase";
            swatch = terrain ? rpg::TerrainFallbackColor(*terrain) : RGB(34, 42, 45);
            showSwatch = true;
        }

        const int rowY = kPaletteFirstY + i * kPaletteRowHeight - g_editor.paletteScroll;
        RECT button{14, rowY, kPaletteWidth - 14, rowY + 42};
        FillRectColor(hdc, button, selected ? RGB(80, 116, 88) : RGB(56, 65, 68));

        HPEN pen = CreatePen(PS_SOLID, 1, selected ? RGB(226, 232, 158) : RGB(88, 98, 102));
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, button.left, button.top, button.right, button.bottom);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(pen);

        int textX = button.left + 12;
        if (showSwatch) {
            RECT swatchRect{button.left + 9, button.top + 9, button.left + 33, button.bottom - 9};
            FillRectColor(hdc, swatchRect, swatch);
            textX = button.left + 42;
        }
        DrawTextLine(hdc, label, textX, button.top + 12, RGB(245, 244, 226));
    }
    RestoreDC(hdc, -1);

    std::wstring dirty = g_editor.dirty ? L"Unsaved" : L"Saved";
    DrawTextLine(hdc, L"Ctrl+S save", 18, client.bottom - 126, RGB(214, 220, 201));
    DrawTextLine(hdc, L"Delete remove", 18, client.bottom - 102, RGB(214, 220, 201));
    DrawTextLine(hdc, L"C collision", 18, client.bottom - 78, RGB(214, 220, 201));
    DrawTextLine(hdc, dirty, 18, client.bottom - 48, g_editor.dirty ? RGB(255, 210, 115) : RGB(175, 224, 160));
    DrawTextLine(hdc, g_editor.status, 18, client.bottom - 24, RGB(224, 226, 212));
}

void DrawMap(HDC hdc) {
    rpg::DrawTerrain(hdc, g_editor.scene, g_editor.cameraX - kPaletteWidth, g_editor.cameraY, true);
}

void DrawSceneObjects(HDC hdc) {
    std::vector<int> indices;
    indices.reserve(g_editor.scene.objects.size());
    for (int i = 0; i < static_cast<int>(g_editor.scene.objects.size()); ++i) {
        indices.push_back(i);
    }
    std::stable_sort(indices.begin(), indices.end(), [](int a, int b) {
        return rpg::ObjectSortY(g_editor.scene.objects[a]) < rpg::ObjectSortY(g_editor.scene.objects[b]);
    });

    for (int index : indices) {
        rpg::DrawSceneObject(
            hdc,
            g_editor.scene.objects[index],
            g_editor.cameraX - kPaletteWidth,
            g_editor.cameraY,
            index == g_editor.selectedObject);
    }

    if (g_editor.showCollision) {
        for (int index : indices) {
            rpg::DrawSceneObjectCollision(
                hdc,
                g_editor.scene.objects[index],
                g_editor.cameraX - kPaletteWidth,
                g_editor.cameraY,
                index == g_editor.selectedObject ? RGB(255, 245, 96) : RGB(219, 64, 64));
        }
    }
}

void RenderEditor(HWND hwnd, HDC target) {
    RECT client{};
    GetClientRect(hwnd, &client);

    HDC hdc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
    HGDIOBJ oldBitmap = SelectObject(hdc, bitmap);

    FillRectColor(hdc, client, RGB(31, 36, 38));

    SaveDC(hdc);
    IntersectClipRect(hdc, kPaletteWidth, 0, client.right, client.bottom);
    DrawMap(hdc);
    DrawSceneObjects(hdc);
    RestoreDC(hdc, -1);

    DrawPalette(hdc, client);

    HPEN borderPen = CreatePen(PS_SOLID, 2, RGB(24, 28, 30));
    HGDIOBJ oldPen = SelectObject(hdc, borderPen);
    MoveToEx(hdc, kPaletteWidth, 0, nullptr);
    LineTo(hdc, kPaletteWidth, client.bottom);
    SelectObject(hdc, oldPen);
    DeleteObject(borderPen);

    BitBlt(target, 0, 0, client.right, client.bottom, hdc, 0, 0, SRCCOPY);

    SelectObject(hdc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(hdc);
}

void HandleKey(HWND hwnd, WPARAM key) {
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool repaint = true;

    switch (key) {
    case VK_DELETE:
        DeleteSelectedObject();
        break;
    case 'C':
        g_editor.showCollision = !g_editor.showCollision;
        g_editor.status = g_editor.showCollision ? L"Collision visible" : L"Collision hidden";
        break;
    case 'R':
        LoadEditorScene();
        break;
    case 'S':
        if (ctrl) {
            SaveEditorScene();
        } else {
            g_editor.cameraY += 48.0f;
        }
        break;
    case 'W':
    case VK_UP:
        g_editor.cameraY -= 48.0f;
        break;
    case 'A':
    case VK_LEFT:
        g_editor.cameraX -= 48.0f;
        break;
    case 'D':
    case VK_RIGHT:
        g_editor.cameraX += 48.0f;
        break;
    default:
        repaint = false;
        break;
    }

    ClampCamera(hwnd);
    ClampPaletteScroll(hwnd);
    if (repaint) {
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        LoadEditorScene();
        ClampCamera(hwnd);
        ClampPaletteScroll(hwnd);
        return 0;
    case WM_SIZE:
        ClampCamera(hwnd);
        ClampPaletteScroll(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        SetFocus(hwnd);
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);

        if (const int tab = PaletteTabHitTest(x, y); tab >= 0) {
            g_editor.mode = static_cast<EditorMode>(tab);
            g_editor.selectedObject = -1;
            g_editor.paletteScroll = 0;
            g_editor.status = tab == 0 ? L"Object tools" : (tab == 1 ? L"Natural terrain" : L"Built flooring");
            ClampPaletteScroll(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (const int hit = PaletteHitTest(hwnd, x, y); hit >= 0) {
            if (g_editor.mode == EditorMode::Objects) {
                g_editor.selectedType = rpg::ObjectDefs()[hit].type;
            } else if (g_editor.mode == EditorMode::Natural) {
                if (const rpg::TerrainDef* terrain = NaturalTerrainForIndex(hit)) {
                    g_editor.selectedNatural = terrain->id;
                }
            } else {
                if (const rpg::TerrainDef* terrain = BuiltTerrainForIndex(hit)) {
                    g_editor.selectedBuilt = terrain->id;
                } else {
                    g_editor.selectedBuilt = "none";
                }
            }
            g_editor.selectedObject = -1;
            g_editor.status = L"Selected tool";
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (x >= kPaletteWidth) {
            const rpg::Vec2 world = ScreenToWorld(x, y);
            if (g_editor.mode == EditorMode::Objects) {
                const int objectIndex = HitObject(world);
                if (objectIndex >= 0) {
                    g_editor.selectedObject = objectIndex;
                    g_editor.dragging = true;
                    g_editor.dragStartWorld = world;
                    g_editor.dragStartObject = g_editor.scene.objects[objectIndex].pos;
                    g_editor.status = L"Dragging object";
                } else {
                    AddObjectAt(world);
                }
            } else {
                g_editor.paintingTerrain = true;
                g_editor.lastPaintTileX = -1;
                g_editor.lastPaintTileY = -1;
                PaintTerrainAt(world);
            }
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &point);
        if (point.x >= 0 && point.x < kPaletteWidth) {
            const int steps = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            g_editor.paletteScroll -= steps * kPaletteRowHeight;
            ClampPaletteScroll(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g_editor.paintingTerrain && (wParam & MK_LBUTTON)) {
            PaintTerrainAt(ScreenToWorld(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (g_editor.dragging && (wParam & MK_LBUTTON)) {
            MoveSelectedObject(ScreenToWorld(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_editor.dragging || g_editor.paintingTerrain) {
            g_editor.dragging = false;
            g_editor.paintingTerrain = false;
            g_editor.lastPaintTileX = -1;
            g_editor.lastPaintTileY = -1;
            ReleaseCapture();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        HandleKey(hwnd, wParam);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        RenderEditor(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        rpg::ReleaseSceneRenderResources();
        rpg::ReleaseTerrainRenderResources();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCmd) {
    const wchar_t kClassName[] = L"FreeWalkRpgSceneEditorWindow";

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
        L"Free Walk RPG Scene Editor",
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
