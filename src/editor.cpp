#include <windows.h>
#include <windowsx.h>

#include "scene.h"
#include "scene_render.h"
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
constexpr int kTileSize = rpg::kTileSize;

#ifndef RPG_ASSET_DIR
#define RPG_ASSET_DIR L"assets"
#endif

struct EditorState {
    rpg::Scene scene;
    std::string selectedType = "tree_oak";
    int selectedObject = -1;
    int nextObjectId = 1;
    bool dragging = false;
    bool dirty = false;
    bool showCollision = true;
    float cameraX = 0.0f;
    float cameraY = 0.0f;
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

int PaletteHitTest(int x, int y) {
    if (x < 0 || x >= kPaletteWidth) {
        return -1;
    }

    constexpr int firstY = 58;
    constexpr int rowH = 56;
    const int index = (y - firstY) / rowH;
    if (y < firstY || index < 0 || index >= static_cast<int>(rpg::ObjectDefs().size())) {
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

void DrawPalette(HDC hdc, const RECT& client) {
    RECT panel{0, 0, kPaletteWidth, client.bottom};
    FillRectColor(hdc, panel, RGB(38, 44, 48));

    DrawTextLine(hdc, L"Scene Editor", 18, 18, RGB(244, 242, 225));

    const auto& defs = rpg::ObjectDefs();
    constexpr int firstY = 58;
    constexpr int rowH = 56;
    for (int i = 0; i < static_cast<int>(defs.size()); ++i) {
        const bool selected = defs[i].type == g_editor.selectedType;
        RECT button{14, firstY + i * rowH, kPaletteWidth - 14, firstY + i * rowH + 42};
        FillRectColor(hdc, button, selected ? RGB(80, 116, 88) : RGB(56, 65, 68));

        HPEN pen = CreatePen(PS_SOLID, 1, selected ? RGB(226, 232, 158) : RGB(88, 98, 102));
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, button.left, button.top, button.right, button.bottom);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(pen);

        DrawTextLine(hdc, defs[i].displayName, button.left + 12, button.top + 12, RGB(245, 244, 226));
    }

    std::wstring dirty = g_editor.dirty ? L"Unsaved" : L"Saved";
    DrawTextLine(hdc, L"Ctrl+S save", 18, client.bottom - 126, RGB(214, 220, 201));
    DrawTextLine(hdc, L"Delete remove", 18, client.bottom - 102, RGB(214, 220, 201));
    DrawTextLine(hdc, L"C collision", 18, client.bottom - 78, RGB(214, 220, 201));
    DrawTextLine(hdc, dirty, 18, client.bottom - 48, g_editor.dirty ? RGB(255, 210, 115) : RGB(175, 224, 160));
    DrawTextLine(hdc, g_editor.status, 18, client.bottom - 24, RGB(224, 226, 212));
}

void DrawMap(HDC hdc) {
    for (int y = 0; y < rpg::kMapHeight; ++y) {
        for (int x = 0; x < rpg::kMapWidth; ++x) {
            const bool wall = rpg::kWorldMap[y][x] == L'#';
            RECT rect{
                kPaletteWidth + static_cast<LONG>(std::round(x * kTileSize - g_editor.cameraX)),
                static_cast<LONG>(std::round(y * kTileSize - g_editor.cameraY)),
                kPaletteWidth + static_cast<LONG>(std::round((x + 1) * kTileSize - g_editor.cameraX)),
                static_cast<LONG>(std::round((y + 1) * kTileSize - g_editor.cameraY)),
            };
            FillRectColor(hdc, rect, wall ? RGB(220, 189, 126) : RGB(111, 189, 132));

            HPEN pen = CreatePen(PS_SOLID, 1, wall ? RGB(198, 165, 105) : RGB(92, 165, 115));
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, rect.left, rect.top, rect.right, rect.bottom);
            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(pen);
        }
    }
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
    if (repaint) {
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        LoadEditorScene();
        ClampCamera(hwnd);
        return 0;
    case WM_SIZE:
        ClampCamera(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        SetFocus(hwnd);
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        if (const int hit = PaletteHitTest(x, y); hit >= 0) {
            g_editor.selectedType = rpg::ObjectDefs()[hit].type;
            g_editor.selectedObject = -1;
            g_editor.status = L"Selected tool";
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (x >= kPaletteWidth) {
            const rpg::Vec2 world = ScreenToWorld(x, y);
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
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g_editor.dragging && (wParam & MK_LBUTTON)) {
            MoveSelectedObject(ScreenToWorld(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_editor.dragging) {
            g_editor.dragging = false;
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
