#include <windows.h>
#include <windowsx.h>

#include "scene.h"
#include "scene_render.h"
#include "save_game.h"
#include "terrain_render.h"
#include "world.h"
#include "world_layers.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <commdlg.h>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <gdiplus.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int kWindowWidth = 1180;
constexpr int kWindowHeight = 760;
constexpr int kPaletteWidth = 190;
constexpr int kPaletteTabsY = 52;
constexpr int kPaletteTabsHeight = 32;
constexpr int kPaletteFirstY = 100;
constexpr int kPaletteRowHeight = 56;
constexpr int kPaletteFooterHeight = 350;
constexpr int kMinimumMapViewportWidth = 480;
constexpr int kTileSize = rpg::kTileSize;

#ifndef RPG_ASSET_DIR
#define RPG_ASSET_DIR L"assets"
#endif

enum class EditorMode {
    Maps,
    Objects,
    Natural,
    Built,
};

enum class FooterAction {
    CreateMap,
    DeleteMap,
    LoadScene,
    SaveSceneAs,
    LoadBackground,
    ClearBackground,
    ExportBlankMap,
    SaveScene,
    LinkTeleport,
    DeleteObject,
    ToggleCollision,
};

struct FooterButtonDef {
    FooterAction action;
    const wchar_t* label;
};

constexpr FooterButtonDef kFooterButtons[] = {
    {FooterAction::CreateMap, L"调整当前层尺寸  N"},
    {FooterAction::DeleteMap, L"重新生成资源"},
    {FooterAction::LoadScene, L"加载存档"},
    {FooterAction::SaveSceneAs, L"场景另存为"},
    {FooterAction::LoadBackground, L"载入底图  L"},
    {FooterAction::ClearBackground, L"清除底图  Shift+L"},
    {FooterAction::ExportBlankMap, L"导出空白图  B"},
    {FooterAction::SaveScene, L"保存  Ctrl+S"},
    {FooterAction::LinkTeleport, L"连接传送点  T"},
    {FooterAction::DeleteObject, L"删除对象  Delete"},
    {FooterAction::ToggleCollision, L"碰撞开关  C"},
};

constexpr UINT_PTR kFooterButtonBaseId = 4000;

struct GridSize {
    int columns = rpg::kMapWidth;
    int rows = rpg::kMapHeight;
};

struct BitmapCache {
    std::wstring path;
    std::unique_ptr<Gdiplus::Bitmap> bitmap;
    bool attempted = false;
};

struct MapEntry {
    std::filesystem::path path;
    std::wstring displayName;
    int width = rpg::kMapWidth;
    int height = rpg::kMapHeight;
    bool hasPlayerStart = false;
};

struct EditorState {
    rpg::Scene scene;
    EditorMode mode = EditorMode::Maps;
    std::string selectedType = "tree_oak";
    std::string selectedNatural = "grass";
    std::string selectedBuilt = "stone_floor";
    std::string selectedBuiltObject;
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
    float zoom = 1.0f;
    bool panning = false;
    bool panningWithMiddleButton = false;
    POINT panStartScreen{};
    float panStartCameraX = 0.0f;
    float panStartCameraY = 0.0f;
    std::filesystem::path scenePath;
    std::filesystem::path saveDirectory;
    std::vector<MapEntry> maps;
    int selectedMap = -1;
    bool linkingTeleport = false;
    std::filesystem::path linkSourceScene;
    std::string linkSourceId;
    int paletteScroll = 0;
    rpg::Vec2 dragStartWorld{};
    rpg::Vec2 dragStartObject{};
    bool selectingNaturalArea = false;
    int selectionStartTileX = 0;
    int selectionStartTileY = 0;
    int selectionEndTileX = 0;
    int selectionEndTileY = 0;
    std::wstring status = L"就绪";
};

EditorState g_editor;
BitmapCache g_backgroundCache;
ULONG_PTR g_gdiplusToken = 0;
bool g_gdiplusStarted = false;
HWND g_footerButtons[static_cast<int>(sizeof(kFooterButtons) / sizeof(kFooterButtons[0]))]{};

bool PromptOpenSaveFile(HWND owner, std::filesystem::path& outPath);

std::filesystem::path AssetPath(const wchar_t* relative) {
    return std::filesystem::path(RPG_ASSET_DIR) / relative;
}

std::wstring MapDisplayName(const std::filesystem::path& path) {
    std::wstring name = path.stem().wstring();
    std::replace(name.begin(), name.end(), L'_', L' ');
    return name.empty() ? path.filename().wstring() : name;
}

int MapIndexForPath(const std::filesystem::path& path) {
    const std::filesystem::path normalized = path.lexically_normal();
    for (int i = 0; i < static_cast<int>(g_editor.maps.size()); ++i) {
        if (g_editor.maps[i].path.lexically_normal() == normalized) {
            return i;
        }
    }
    return -1;
}

void RefreshMapCatalog() {
    g_editor.maps.clear();
    const std::filesystem::path root = g_editor.saveDirectory / L"maps";
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
        g_editor.selectedMap = -1;
        return;
    }

    for (const rpg::WorldLayerDef& layer : rpg::WorldLayers()) {
        const std::filesystem::path path = rpg::WorldLayerPath(root, layer);
        MapEntry entry;
        entry.path = path;
        rpg::Scene probe;
        std::string error;
        if (rpg::LoadSceneFromFile(path, probe, &error)) {
            entry.width = probe.mapWidth;
            entry.height = probe.mapHeight;
            entry.hasPlayerStart = probe.hasPlayerStart;
        }
        entry.displayName = std::to_wstring(layer.index) + L". " + layer.displayName + L"  " +
            std::to_wstring(entry.width) + L"x" + std::to_wstring(entry.height);
        g_editor.maps.push_back(std::move(entry));
    }

    g_editor.selectedMap = MapIndexForPath(g_editor.scenePath);
}

float Clamp(float value, float minValue, float maxValue) {
    return std::max(minValue, std::min(maxValue, value));
}

bool EnsureGdiPlus() {
    if (g_gdiplusStarted) {
        return g_gdiplusToken != 0;
    }

    Gdiplus::GdiplusStartupInput startup{};
    if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &startup, nullptr) != Gdiplus::Ok) {
        g_gdiplusToken = 0;
        g_gdiplusStarted = true;
        return false;
    }

    g_gdiplusStarted = true;
    return true;
}

void ReleaseBackgroundResources() {
    g_backgroundCache.path.clear();
    g_backgroundCache.bitmap.reset();
    g_backgroundCache.attempted = false;
}

std::filesystem::path ResolveBackgroundPath(const std::wstring& storedPath) {
    if (storedPath.empty()) {
        return {};
    }

    std::filesystem::path path = std::filesystem::path(storedPath);
    if (path.is_absolute()) {
        return path;
    }

    const std::filesystem::path assetPath = std::filesystem::path(RPG_ASSET_DIR) / path;
    std::error_code ec;
    if (std::filesystem::exists(assetPath, ec)) {
        return assetPath;
    }

    return path;
}

std::wstring StoreBackgroundPath(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::path assetRoot = std::filesystem::path(RPG_ASSET_DIR);
    const std::filesystem::path relative = std::filesystem::relative(path, assetRoot, ec);
    if (!ec && !relative.empty() && relative.native().rfind(L"..", 0) != 0) {
        return relative.generic_wstring();
    }

    return path.generic_wstring();
}

void DebugTrace(const std::wstring& text) {
    OutputDebugStringW((text + L"\n").c_str());
}

void DebugTraceAscii(const std::string& text) {
    std::error_code ec;
    const std::filesystem::path logPath = std::filesystem::temp_directory_path(ec) / L"rpg_game_editor_debug.log";
    if (ec) {
        return;
    }

    std::ofstream log(logPath, std::ios::app);
    if (log) {
        log << text << '\n';
    }
}

void DebugLastError(const char* tag) {
    const DWORD error = GetLastError();
    DebugTraceAscii(std::string(tag) + " error=" + std::to_string(error));
}

std::unique_ptr<Gdiplus::Bitmap> LoadBitmapFromPath(const std::filesystem::path& path) {
    if (path.empty()) {
        return nullptr;
    }

    auto bitmap = std::make_unique<Gdiplus::Bitmap>(path.c_str());
    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
        return nullptr;
    }
    return bitmap;
}

bool EnsureBackgroundBitmap() {
    if (!rpg::SceneSupportsBackgroundImage(g_editor.scene)) {
        ReleaseBackgroundResources();
        return false;
    }
    if (g_backgroundCache.path != g_editor.scene.backgroundImagePath) {
        ReleaseBackgroundResources();
        g_backgroundCache.path = g_editor.scene.backgroundImagePath;
    }

    if (g_backgroundCache.attempted) {
        return g_backgroundCache.bitmap != nullptr;
    }

    g_backgroundCache.attempted = true;
    if (g_backgroundCache.path.empty()) {
        return false;
    }

    g_backgroundCache.bitmap = LoadBitmapFromPath(ResolveBackgroundPath(g_backgroundCache.path));
    return g_backgroundCache.bitmap != nullptr;
}

bool GetPngEncoderClsid(CLSID& clsid) {
    UINT count = 0;
    UINT size = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &size) != Gdiplus::Ok || size == 0) {
        return false;
    }

    std::vector<BYTE> buffer(size);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    if (Gdiplus::GetImageEncoders(count, size, encoders) != Gdiplus::Ok) {
        return false;
    }

    for (UINT i = 0; i < count; ++i) {
        if (std::wstring_view(encoders[i].MimeType) == L"image/png") {
            clsid = encoders[i].Clsid;
            return true;
        }
    }
    return false;
}

bool SaveBitmapAsPng(const std::filesystem::path& path, Gdiplus::Bitmap& bitmap) {
    CLSID pngClsid{};
    if (!GetPngEncoderClsid(pngClsid)) {
        return false;
    }
    return bitmap.Save(path.c_str(), &pngClsid, nullptr) == Gdiplus::Ok;
}

struct GridSizeDialogState {
    HWND owner = nullptr;
    HWND hwnd = nullptr;
    HWND columnsEdit = nullptr;
    HWND rowsEdit = nullptr;
    int columns = rpg::kMapWidth;
    int rows = rpg::kMapHeight;
    bool accepted = false;
    bool done = false;
};

void DebugCreateWindowResult(const char* tag, HWND hwnd) {
    if (hwnd) {
        DebugTraceAscii(std::string(tag) + " ok hwnd=" + std::to_string(static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(hwnd))));
        return;
    }
    DebugLastError(tag);
}

int ReadEditInt(HWND edit, int fallback) {
    wchar_t buffer[64]{};
    GetWindowTextW(edit, buffer, static_cast<int>(sizeof(buffer) / sizeof(buffer[0])));
    try {
        const int value = std::stoi(buffer);
        return value > 0 ? value : fallback;
    } catch (...) {
        return fallback;
    }
}



bool CreateGridSizeControls(HWND hwnd, GridSizeDialogState& state) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    DebugTraceAscii("grid:controls-begin hwnd=" + std::to_string(static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(hwnd))));
    DebugTraceAscii(std::string("grid:controls-parent_is_window=") + (IsWindow(hwnd) ? "1" : "0"));
    DebugTraceAscii(std::string("grid:controls-parent_thread=") + std::to_string(GetWindowThreadProcessId(hwnd, nullptr)));

    auto createControl = [&](const char* tag, DWORD exStyle, const wchar_t* className, const wchar_t* text,
                             DWORD style, int x, int y, int width, int height, int id) -> HWND {
        SetLastError(ERROR_SUCCESS);
        HWND control = CreateWindowExW(
            exStyle,
            className,
            text,
            style,
            x,
            y,
            width,
            height,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            instance,
            nullptr);
        if (control) {
            DebugTraceAscii(std::string(tag) + " ok hwnd=" + std::to_string(static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(control))) +
                            " parent=" + std::to_string(static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(GetParent(control)))));
        } else {
            DebugLastError(tag);
        }
        return control;
    };

    createControl("grid:label-columns", 0, L"STATIC", L"列数", WS_CHILD | WS_VISIBLE, 16, 16, 72, 20, 10);
    createControl("grid:label-rows", 0, L"STATIC", L"行数", WS_CHILD | WS_VISIBLE, 16, 52, 72, 20, 11);

    state.columnsEdit = createControl(
        "grid:edit-columns",
        WS_EX_CLIENTEDGE,
        L"EDIT",
        std::to_wstring(state.columns).c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
        96,
        12,
        120,
        24,
        1);
    state.rowsEdit = createControl(
        "grid:edit-rows",
        WS_EX_CLIENTEDGE,
        L"EDIT",
        std::to_wstring(state.rows).c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
        96,
        48,
        120,
        24,
        2);

    createControl("grid:button-ok", 0, L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 48, 92, 72, 26, IDOK);
    createControl("grid:button-cancel", 0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 132, 92, 72, 26, IDCANCEL);

    const bool controlsCreated = state.columnsEdit && state.rowsEdit && GetDlgItem(hwnd, IDOK) && GetDlgItem(hwnd, IDCANCEL);
    DebugTraceAscii(std::string("grid:controls-result=") + (controlsCreated ? "ok" : "fail"));
    return controlsCreated;
}

void AppendDialogWord(std::vector<BYTE>& data, WORD value) {
    const size_t offset = data.size();
    data.resize(offset + sizeof(WORD));
    *reinterpret_cast<WORD*>(data.data() + offset) = value;
}

void AppendDialogString(std::vector<BYTE>& data, const wchar_t* text) {
    const size_t length = wcslen(text) + 1;
    const size_t offset = data.size();
    data.resize(offset + length * sizeof(wchar_t));
    std::memcpy(data.data() + offset, text, length * sizeof(wchar_t));
}

void AlignDialogData(std::vector<BYTE>& data) {
    data.resize((data.size() + 3u) & ~3u);
}

void AppendDialogItem(std::vector<BYTE>& data, DWORD style, DWORD exStyle, short x, short y, short width, short height,
                      WORD id, WORD classAtom, const wchar_t* title) {
    AlignDialogData(data);
    const size_t offset = data.size();
    data.resize(offset + sizeof(DLGITEMTEMPLATE));
    auto* item = reinterpret_cast<DLGITEMTEMPLATE*>(data.data() + offset);
    item->style = style;
    item->dwExtendedStyle = exStyle;
    item->x = x;
    item->y = y;
    item->cx = width;
    item->cy = height;
    item->id = id;
    AppendDialogWord(data, 0xffff);
    AppendDialogWord(data, classAtom);
    AppendDialogString(data, title);
    AppendDialogWord(data, 0);
}

std::vector<BYTE> BuildGridSizeDialogTemplate(const GridSizeDialogState& state) {
    std::vector<BYTE> data(sizeof(DLGTEMPLATE));
    auto* dialog = reinterpret_cast<DLGTEMPLATE*>(data.data());
    dialog->style = DS_MODALFRAME | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    dialog->dwExtendedStyle = WS_EX_DLGMODALFRAME;
    dialog->cdit = 6;
    dialog->x = 0;
    dialog->y = 0;
    dialog->cx = 180;
    dialog->cy = 110;

    AppendDialogWord(data, 0);
    AppendDialogWord(data, 0);
    AppendDialogString(data, L"地图尺寸（格子）");
    AppendDialogWord(data, 9);
    AppendDialogString(data, L"Segoe UI");

    AppendDialogItem(data, WS_CHILD | WS_VISIBLE, 0, 8, 12, 40, 12, 10, 0x0082, L"宽度");
    AppendDialogItem(data, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
                     WS_EX_CLIENTEDGE, 56, 9, 110, 16, 1001, 0x0081, std::to_wstring(state.columns).c_str());
    AppendDialogItem(data, WS_CHILD | WS_VISIBLE, 0, 8, 38, 40, 12, 11, 0x0082, L"高度");
    AppendDialogItem(data, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
                     WS_EX_CLIENTEDGE, 56, 35, 110, 16, 1002, 0x0081, std::to_wstring(state.rows).c_str());
    AppendDialogItem(data, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                     0, 34, 70, 50, 18, IDOK, 0x0080, L"确定");
    AppendDialogItem(data, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                     0, 92, 70, 50, 18, IDCANCEL, 0x0080, L"取消");
    return data;
}

INT_PTR CALLBACK GridSizeDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<GridSizeDialogState*>(GetWindowLongPtrW(hwnd, DWLP_USER));

    switch (msg) {
    case WM_INITDIALOG: {
        state = reinterpret_cast<GridSizeDialogState*>(lParam);
        SetWindowLongPtrW(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
        if (!state) {
            DebugTraceAscii("grid:init-no-state");
            return FALSE;
        }

        state->hwnd = hwnd;
        state->columnsEdit = GetDlgItem(hwnd, 1001);
        state->rowsEdit = GetDlgItem(hwnd, 1002);
        DebugTraceAscii("grid:init hwnd=" + std::to_string(static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(hwnd))) +
                        " columns_edit=" + std::to_string(static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(state->columnsEdit))) +
                        " rows_edit=" + std::to_string(static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(state->rowsEdit))));

        if (state->owner) {
            RECT ownerRect{};
            RECT dialogRect{};
            if (GetWindowRect(state->owner, &ownerRect) && GetWindowRect(hwnd, &dialogRect)) {
                const int width = dialogRect.right - dialogRect.left;
                const int height = dialogRect.bottom - dialogRect.top;
                const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
                const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
                SetWindowPos(hwnd, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
            }
        }
        SetFocus(state->columnsEdit);
        return TRUE;
    }
    case WM_COMMAND:
        DebugTraceAscii("grid:wm_command id=" + std::to_string(LOWORD(wParam)) + " code=" + std::to_string(HIWORD(wParam)));
        switch (LOWORD(wParam)) {
        case IDOK:
            if (!state) {
                return TRUE;
            }
            state->columns = std::max(1, ReadEditInt(state->columnsEdit, state->columns));
            state->rows = std::max(1, ReadEditInt(state->rowsEdit, state->rows));
            state->accepted = true;
            state->done = true;
            EndDialog(hwnd, IDOK);
            return TRUE;
        case IDCANCEL:
            if (state) {
                state->done = true;
            }
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        default:
            break;
        }
        break;
    case WM_CLOSE:
        DebugTraceAscii("grid:wm_close");
        if (state) {
            state->done = true;
        }
        EndDialog(hwnd, IDCANCEL);
        return TRUE;
    default:
        break;
    }

    return FALSE;
}

bool PromptGridSize(HWND owner, GridSize& size) {
    GridSizeDialogState state{};
    state.columns = size.columns;
    state.rows = size.rows;
    state.owner = owner;

    DebugTraceAscii(std::string("grid:owner_is_window=") + (IsWindow(owner) ? "1" : "0"));
    DebugTraceAscii(std::string("grid:owner_visible=") + (IsWindowVisible(owner) ? "1" : "0"));
    DebugTraceAscii(std::string("grid:owner_enabled=") + (IsWindowEnabled(owner) ? "1" : "0"));
    DWORD ownerPid = 0;
    const DWORD ownerTid = GetWindowThreadProcessId(owner, &ownerPid);
    DebugTraceAscii("grid:owner_tid=" + std::to_string(ownerTid) + " pid=" + std::to_string(ownerPid));
    std::vector<BYTE> dialogTemplate = BuildGridSizeDialogTemplate(state);
    DebugTraceAscii("grid:dialog-template bytes=" + std::to_string(dialogTemplate.size()));
    SetLastError(ERROR_SUCCESS);
    const INT_PTR result = DialogBoxIndirectParamW(
        GetModuleHandleW(nullptr),
        reinterpret_cast<const DLGTEMPLATE*>(dialogTemplate.data()),
        owner,
        GridSizeDialogProc,
        reinterpret_cast<LPARAM>(&state));
    if (result == -1) {
        DebugLastError("grid:dialog-create-fail");
        return false;
    }
    DebugTraceAscii("grid:dialog-result=" + std::to_string(static_cast<long long>(result)));

    if (result == IDOK && state.accepted) {
        size.columns = state.columns;
        size.rows = state.rows;
        return true;
    }
    return false;
}
bool PromptOpenImageFile(HWND owner, std::filesystem::path& outPath) {
    wchar_t buffer[MAX_PATH * 4] = {};
    std::wstring initialDir = AssetPath(L"").wstring();

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"PNG 图片\0*.png\0所有文件\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
    ofn.lpstrInitialDir = initialDir.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER;

    if (!GetOpenFileNameW(&ofn)) {
        return false;
    }

    outPath = buffer;
    return true;
}

bool PromptOpenSceneFile(HWND owner, std::filesystem::path& outPath) {
    wchar_t buffer[MAX_PATH * 4]{};
    std::wstring initialDir = AssetPath(L"maps").wstring();
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Scene JSON (*.json)\0*.json\0All files\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
    ofn.lpstrInitialDir = initialDir.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) {
        return false;
    }
    outPath = buffer;
    return true;
}

bool PromptSaveSceneFile(HWND owner, const std::filesystem::path& current, std::filesystem::path& outPath) {
    wchar_t buffer[MAX_PATH * 4]{};
    const std::wstring currentName = current.empty() ? L"scene.json" : current.filename().wstring();
    wcsncpy_s(buffer, currentName.c_str(), _TRUNCATE);
    std::wstring initialDir = AssetPath(L"maps").wstring();
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Scene JSON (*.json)\0*.json\0All files\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
    ofn.lpstrInitialDir = initialDir.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetSaveFileNameW(&ofn)) {
        return false;
    }
    outPath = buffer;
    if (outPath.extension().empty()) {
        outPath.replace_extension(L".json");
    }
    return true;
}

bool PromptSavePngFile(HWND owner, std::filesystem::path& outPath) {
    wchar_t buffer[MAX_PATH * 4] = L"空白地图.png";
    std::wstring initialDir = AssetPath(L"").wstring();

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"PNG 文件\0*.png\0所有文件\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
    ofn.lpstrInitialDir = initialDir.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (!GetSaveFileNameW(&ofn)) {
        return false;
    }

    outPath = buffer;
    if (outPath.extension().empty()) {
        outPath.replace_extension(L".png");
    }
    return true;
}

bool DrawBackgroundImage(HDC hdc) {
    if (g_editor.scene.backgroundImagePath.empty() || !rpg::SceneSupportsBackgroundImage(g_editor.scene)) {
        return false;
    }
    if (!EnsureGdiPlus() || !EnsureBackgroundBitmap()) {
        return false;
    }

    Gdiplus::Bitmap* bitmap = g_backgroundCache.bitmap.get();
    if (!bitmap) {
        return false;
    }

    Gdiplus::Graphics graphics(hdc);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    const int destX = static_cast<int>(std::round(kPaletteWidth - g_editor.cameraX));
    const int destY = static_cast<int>(std::round(-g_editor.cameraY));
    const int destW = static_cast<int>(std::round(rpg::SceneWorldWidth(g_editor.scene) * g_editor.zoom));
    const int destH = static_cast<int>(std::round(rpg::SceneWorldHeight(g_editor.scene) * g_editor.zoom));

    graphics.DrawImage(
        bitmap,
        Gdiplus::Rect(destX, destY, destW, destH),
        0,
        0,
        static_cast<INT>(bitmap->GetWidth()),
        static_cast<INT>(bitmap->GetHeight()),
        Gdiplus::UnitPixel);
    return true;
}

bool ExportBlankMapTemplate(HWND owner) {
    DebugTrace(L"[ExportBlankMapTemplate] start");
    DebugTraceAscii("export:start");
    g_editor.status = L"正在导出空白图";
    GridSize size{};
    if (!PromptGridSize(owner, size)) {
        DebugTrace(L"[ExportBlankMapTemplate] grid dialog cancelled");
        DebugTraceAscii("export:grid-cancel");
        g_editor.status = L"已取消导出";
        return false;
    }
    DebugTrace(L"[ExportBlankMapTemplate] grid confirmed: " + std::to_wstring(size.columns) + L"x" + std::to_wstring(size.rows));
    DebugTraceAscii("export:grid-ok");

    std::filesystem::path outputPath;
    if (!PromptSavePngFile(owner, outputPath)) {
        DebugTrace(L"[ExportBlankMapTemplate] save dialog cancelled");
        DebugTraceAscii("export:save-cancel");
        g_editor.status = L"已取消导出";
        return false;
    }
    DebugTrace(L"[ExportBlankMapTemplate] output path: " + outputPath.wstring());
    DebugTraceAscii("export:save-ok");

    const int width = size.columns * kTileSize;
    const int height = size.rows * kTileSize;
    auto bitmap = std::make_unique<Gdiplus::Bitmap>(width, height, PixelFormat32bppARGB);
    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
        DebugTrace(L"[ExportBlankMapTemplate] bitmap create failed");
        DebugTraceAscii("export:bitmap-fail");
        g_editor.status = L"创建空白图片失败";
        return false;
    }

    Gdiplus::Graphics graphics(bitmap.get());
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighSpeed);
    graphics.Clear(Gdiplus::Color(255, 255, 255, 255));

    Gdiplus::Pen gridPen(Gdiplus::Color(90, 210, 210, 210), 1.0f);
    Gdiplus::Pen borderPen(Gdiplus::Color(160, 170, 170, 170), 1.0f);
    for (int x = 0; x <= size.columns; ++x) {
        const float px = static_cast<float>(x * kTileSize) + 0.5f;
        graphics.DrawLine(&gridPen, px, 0.0f, px, static_cast<float>(height));
    }
    for (int y = 0; y <= size.rows; ++y) {
        const float py = static_cast<float>(y * kTileSize) + 0.5f;
        graphics.DrawLine(&gridPen, 0.0f, py, static_cast<float>(width), py);
    }
    graphics.DrawRectangle(&borderPen, 0.0f, 0.0f, static_cast<float>(width - 1), static_cast<float>(height - 1));

    if (!SaveBitmapAsPng(outputPath, *bitmap)) {
        DebugTrace(L"[ExportBlankMapTemplate] save png failed");
        DebugTraceAscii("export:png-fail");
        g_editor.status = L"导出空白地图失败";
        return false;
    }

    DebugTrace(L"[ExportBlankMapTemplate] success");
    DebugTraceAscii("export:success");
    g_editor.status = L"已导出空白地图模板";
    return true;
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
    g_editor.cameraX = Clamp(g_editor.cameraX, 0.0f, std::max(0.0f, rpg::SceneWorldWidth(g_editor.scene) * g_editor.zoom - viewportW));
    g_editor.cameraY = Clamp(g_editor.cameraY, 0.0f, std::max(0.0f, rpg::SceneWorldHeight(g_editor.scene) * g_editor.zoom - viewportH));
}

rpg::Vec2 ScreenToWorld(int x, int y) {
    return {
        (static_cast<float>(x - kPaletteWidth) + g_editor.cameraX) / g_editor.zoom,
        (static_cast<float>(y) + g_editor.cameraY) / g_editor.zoom,
    };
}

std::vector<const rpg::SceneObjectDef*> PlaceableObjectDefs() {
    std::vector<const rpg::SceneObjectDef*> defs;
    for (const rpg::SceneObjectDef& def : rpg::ObjectDefs()) {
        if (def.placeable && !def.building) {
            defs.push_back(&def);
        }
    }
    return defs;
}

std::vector<const rpg::SceneObjectDef*> BuildingObjectDefs() {
    std::vector<const rpg::SceneObjectDef*> defs;
    for (const rpg::SceneObjectDef& def : rpg::ObjectDefs()) {
        if (def.placeable && def.building) {
            defs.push_back(&def);
        }
    }
    return defs;
}

int PaletteItemCount() {
    switch (g_editor.mode) {
    case EditorMode::Maps:
        return static_cast<int>(g_editor.maps.size());
    case EditorMode::Natural:
        return static_cast<int>(rpg::NaturalTerrainDefs().size()) + 1;
    case EditorMode::Built:
        return static_cast<int>(rpg::BuiltTerrainDefs().size()) + 1 +
            static_cast<int>(BuildingObjectDefs().size());
    case EditorMode::Objects:
    default:
        return static_cast<int>(PlaceableObjectDefs().size());
    }
}

int PaletteTabHitTest(int x, int y) {
    if (x < 8 || x >= kPaletteWidth - 8 || y < kPaletteTabsY || y >= kPaletteTabsY + kPaletteTabsHeight) {
        return -1;
    }
    const int tabWidth = (kPaletteWidth - 16) / 4;
    return std::min(3, (x - 8) / tabWidth);
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

int FooterButtonCount() {
    return static_cast<int>(sizeof(kFooterButtons) / sizeof(kFooterButtons[0]));
}

RECT FooterButtonRect(const RECT& client, int index) {
    constexpr int kFooterButtonHeight = 24;
    constexpr int kFooterButtonGap = 4;
    const int firstY = client.bottom - kPaletteFooterHeight + 12;
    return RECT{
        14,
        firstY + index * (kFooterButtonHeight + kFooterButtonGap),
        kPaletteWidth - 14,
        firstY + index * (kFooterButtonHeight + kFooterButtonGap) + kFooterButtonHeight,
    };
}

void UpdateFooterButtonLabels() {
    for (int i = 0; i < FooterButtonCount(); ++i) {
        if (!g_footerButtons[i]) {
            continue;
        }
        const wchar_t* text = kFooterButtons[i].label;
        if (kFooterButtons[i].action == FooterAction::ToggleCollision) {
            text = g_editor.showCollision ? L"隐藏碰撞  C" : L"显示碰撞  C";
        }
        SetWindowTextW(g_footerButtons[i], text);
    }
}

void LayoutFooterButtons(HWND hwnd) {
    RECT client{};
    GetClientRect(hwnd, &client);

    HDWP defer = BeginDeferWindowPos(FooterButtonCount());
    for (int i = 0; i < FooterButtonCount(); ++i) {
        const RECT rect = FooterButtonRect(client, i);
        if (g_footerButtons[i]) {
            if (defer) {
                defer = DeferWindowPos(
                    defer,
                    g_footerButtons[i],
                    nullptr,
                    rect.left,
                    rect.top,
                    rect.right - rect.left,
                    rect.bottom - rect.top,
                    SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOREDRAW);
            } else {
                SetWindowPos(
                    g_footerButtons[i],
                    nullptr,
                    rect.left,
                    rect.top,
                    rect.right - rect.left,
                    rect.bottom - rect.top,
                    SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOREDRAW);
            }
        }
    }
    if (defer) {
        EndDeferWindowPos(defer);
    }

    RedrawWindow(
        hwnd,
        nullptr,
        nullptr,
        RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

void CreateFooterButtons(HWND hwnd) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_TEXT;
    for (int i = 0; i < FooterButtonCount(); ++i) {
        const int id = static_cast<int>(kFooterButtonBaseId + i);
        g_footerButtons[i] = CreateWindowW(
            L"BUTTON",
            kFooterButtons[i].label,
            style,
            0,
            0,
            100,
            24,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr),
            nullptr);
    }
    UpdateFooterButtonLabels();
    LayoutFooterButtons(hwnd);
}

int FooterButtonHitTest(HWND hwnd, int x, int y) {
    if (x < 0 || x >= kPaletteWidth) {
        return -1;
    }

    RECT client{};
    GetClientRect(hwnd, &client);
    for (int i = 0; i < FooterButtonCount(); ++i) {
        const RECT button = FooterButtonRect(client, i);
        if (x >= button.left && x < button.right && y >= button.top && y < button.bottom) {
            return i;
        }
    }
    return -1;
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

bool LoadEditorSceneFromPath(const std::filesystem::path& scenePath) {
    rpg::ReleaseSceneRenderResources();
    rpg::ReleaseTerrainRenderResources();
    ReleaseBackgroundResources();
    std::string objectError;
    const bool objectsLoaded = rpg::ReloadObjectDefs(&objectError);
    std::string terrainError;
    const bool terrainLoaded = rpg::ReloadTerrainDefs(&terrainError);

    const auto objectDefs = PlaceableObjectDefs();
    const rpg::SceneObjectDef* selectedDef = rpg::FindObjectDef(g_editor.selectedType);
    if (!objectDefs.empty() && (!selectedDef || !selectedDef->placeable)) {
        g_editor.selectedType = objectDefs.front()->type;
    }
    const auto& naturalDefs = rpg::NaturalTerrainDefs();
    if (g_editor.selectedNatural != "none" && !naturalDefs.empty() &&
        !rpg::FindTerrainDef(g_editor.selectedNatural, rpg::TerrainLayer::Natural)) {
        g_editor.selectedNatural = naturalDefs.front().id;
    }
    const auto& builtDefs = rpg::BuiltTerrainDefs();
    if (!builtDefs.empty() && g_editor.selectedBuilt != "none" &&
        !rpg::FindTerrainDef(g_editor.selectedBuilt, rpg::TerrainLayer::Built)) {
        g_editor.selectedBuilt = builtDefs.front().id;
    }

    std::string error;
    if (!rpg::LoadSceneFromFile(scenePath, g_editor.scene, &error)) {
        g_editor.scene = rpg::MakeDefaultScene();
        rpg::SaveSceneToFile(scenePath, g_editor.scene);
        g_editor.status = L"已创建默认场景";
    } else {
        g_editor.status = L"已载入场景";
    }

    g_editor.selectedObject = -1;
    g_editor.nextObjectId = static_cast<int>(g_editor.scene.objects.size()) + 1;
    g_editor.scenePath = scenePath;
    g_editor.selectedMap = MapIndexForPath(scenePath);
    g_editor.cameraX = 0.0f;
    g_editor.cameraY = 0.0f;
    g_editor.dirty = false;

    if (!objectsLoaded || !terrainLoaded) {
        g_editor.status = L"模块加载失败";
    } else if (!objectError.empty() || !terrainError.empty()) {
        g_editor.status = L"已载入，模块有警告";
    } else {
        g_editor.status = L"已载入场景模块";
    }
    return true;
}

void SetZoomAt(HWND hwnd, int screenX, int screenY, float factor) {
    const rpg::Vec2 anchor = ScreenToWorld(screenX, screenY);
    g_editor.zoom = Clamp(g_editor.zoom * factor, 0.5f, 2.5f);
    g_editor.cameraX = anchor.x * g_editor.zoom - static_cast<float>(screenX - kPaletteWidth);
    g_editor.cameraY = anchor.y * g_editor.zoom - static_cast<float>(screenY);
    ClampCamera(hwnd);
}

void LoadEditorScene(HWND owner = nullptr) {
    rpg::ReloadObjectDefs();
    rpg::ReloadTerrainDefs();
    if (g_editor.saveDirectory.empty()) {
        std::filesystem::path saveFile;
        if (!PromptOpenSaveFile(owner, saveFile)) {
            g_editor.status = L"请点击“加载存档”选择 save.json";
            return;
        }
        rpg::SaveGameInfo save;
        std::string error;
        if (!rpg::LoadSaveGame(saveFile.parent_path(), save, &error)) {
            g_editor.status = L"存档读取失败";
            return;
        }
        g_editor.saveDirectory = saveFile.parent_path();
        g_editor.scenePath.clear();
    }
    RefreshMapCatalog();
    std::filesystem::path path = g_editor.scenePath;
    if (path.empty() || !std::filesystem::is_regular_file(path)) {
        path.clear();
        for (const MapEntry& map : g_editor.maps) {
            if (map.hasPlayerStart) {
                path = map.path;
                break;
            }
        }
        if (path.empty() && !g_editor.maps.empty()) {
            path = g_editor.maps.front().path;
        }
    }
    if (!path.empty()) LoadEditorSceneFromPath(path);
}

void SaveEditorScene() {
    std::string error;
    if (!g_editor.scenePath.empty() && rpg::SaveSceneToFile(g_editor.scenePath, g_editor.scene, &error)) {
        g_editor.status = L"已保存场景";
        g_editor.dirty = false;
    } else {
        g_editor.status = L"保存失败";
    }
}

std::filesystem::path NextMapPath() {
    const std::filesystem::path root = AssetPath(L"maps");
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    for (int index = 1; index < 10000; ++index) {
        const std::filesystem::path candidate = root / (L"new_map_" + std::to_wstring(index) + L".json");
        if (!std::filesystem::exists(candidate, ec)) {
            return candidate;
        }
    }
    return root / L"new_map.json";
}

void CreateMap(HWND hwnd) {
    const rpg::WorldLayerDef* layer = rpg::FindWorldLayer(g_editor.scenePath);
    if (!layer) {
        g_editor.status = L"当前文件不是固定世界层";
        return;
    }

    GridSize size{g_editor.scene.mapWidth, g_editor.scene.mapHeight};
    if (!PromptGridSize(hwnd, size)) {
        return;
    }
    if (size.columns < layer->minSize || size.columns > layer->maxSize ||
        size.rows < layer->minSize || size.rows > layer->maxSize) {
        const std::wstring message = std::wstring(layer->displayName) + L"的宽度和高度必须在 " +
            std::to_wstring(layer->minSize) + L" 到 " + std::to_wstring(layer->maxSize) + L" 格之间。";
        MessageBoxW(hwnd, message.c_str(), L"尺寸超出范围", MB_OK | MB_ICONWARNING);
        g_editor.status = L"尺寸超出当前层允许范围";
        return;
    }

    rpg::ResizeWorldLayer(g_editor.scene, *layer, size.columns, size.rows);
    ReleaseBackgroundResources();
    MarkDirty(L"已调整当前层尺寸并重新生成资源");
    const int mapIndex = MapIndexForPath(g_editor.scenePath);
    if (mapIndex >= 0) {
        g_editor.maps[mapIndex].width = size.columns;
        g_editor.maps[mapIndex].height = size.rows;
        g_editor.maps[mapIndex].displayName = std::to_wstring(layer->index) + L". " + layer->displayName + L"  " +
            std::to_wstring(size.columns) + L"x" + std::to_wstring(size.rows);
    }
}

bool PromptOpenSaveFile(HWND owner, std::filesystem::path& outPath) {
    wchar_t buffer[MAX_PATH * 4] = L"save.json";
    const std::wstring initialDir = rpg::DefaultSavesRoot().wstring();
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"RPG 存档 (save.json)\0save.json\0JSON 文件\0*.json\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = static_cast<DWORD>(std::size(buffer));
    ofn.lpstrInitialDir = initialDir.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return false;
    outPath = buffer;
    return outPath.filename() == L"save.json";
}

void DeleteMap(HWND hwnd) {
    const rpg::WorldLayerDef* layer = rpg::FindWorldLayer(g_editor.scenePath);
    if (!layer) {
        g_editor.status = L"当前文件不是固定世界层";
        return;
    }
    if (MessageBoxW(
            hwnd,
            L"重新生成当前层的树木、灌木、矿石和传送门？\n手工摆放的其他对象与地形会保留。",
            L"重新生成资源",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
        return;
    }
    rpg::RegenerateWorldLayerResources(g_editor.scene, *layer);
    g_editor.selectedObject = -1;
    MarkDirty(L"已重新生成当前层资源和传送门");
}

std::string StoreScenePath(const std::filesystem::path& path) {
    return std::filesystem::path(StoreBackgroundPath(path)).generic_u8string();
}

void BeginTeleportLink() {
    if (g_editor.selectedObject < 0 || g_editor.selectedObject >= static_cast<int>(g_editor.scene.objects.size())) {
        g_editor.status = L"请先选择传送点";
        return;
    }

    const rpg::SceneObject& source = g_editor.scene.objects[g_editor.selectedObject];
    if (!rpg::ObjectIsTeleport(source)) {
        g_editor.status = L"当前对象不是传送点";
        return;
    }

    if (g_editor.dirty) {
        SaveEditorScene();
    }
    g_editor.linkingTeleport = true;
    g_editor.linkSourceScene = g_editor.scenePath;
    g_editor.linkSourceId = source.id;
    g_editor.mode = EditorMode::Maps;
    g_editor.paletteScroll = 0;
    g_editor.status = L"请选择目标地图，再点击目标传送点";
}

bool CompleteTeleportLink(HWND hwnd, int targetIndex) {
    if (!g_editor.linkingTeleport || targetIndex < 0 ||
        targetIndex >= static_cast<int>(g_editor.scene.objects.size())) {
        return false;
    }

    rpg::SceneObject& target = g_editor.scene.objects[targetIndex];
    if (!rpg::ObjectIsTeleport(target)) {
        g_editor.status = L"目标对象不是传送点";
        return false;
    }

    const std::filesystem::path sourceScene = g_editor.linkSourceScene;
    const std::string sourceId = g_editor.linkSourceId;
    if (sourceScene.lexically_normal() == g_editor.scenePath.lexically_normal() && sourceId == target.id) {
        g_editor.status = L"不能连接到传送点自身";
        return false;
    }

    const std::string targetScenePath = g_editor.scenePath.filename().generic_u8string();
    target.targetScene = sourceScene.filename().generic_u8string();
    target.targetId = sourceId;

    bool sourceUpdated = false;
    if (sourceScene.lexically_normal() == g_editor.scenePath.lexically_normal()) {
        for (rpg::SceneObject& source : g_editor.scene.objects) {
            if (source.id == sourceId && rpg::ObjectIsTeleport(source)) {
                source.targetScene = targetScenePath;
                source.targetId = target.id;
                sourceUpdated = true;
                break;
            }
        }
    } else {
        rpg::Scene sourceSceneData;
        std::string error;
        if (rpg::LoadSceneFromFile(sourceScene, sourceSceneData, &error)) {
            for (rpg::SceneObject& source : sourceSceneData.objects) {
                if (source.id == sourceId && rpg::ObjectIsTeleport(source)) {
                    source.targetScene = targetScenePath;
                    source.targetId = target.id;
                    sourceUpdated = rpg::SaveSceneToFile(sourceScene, sourceSceneData, &error);
                    break;
                }
            }
        }
    }

    std::string targetError;
    if (!sourceUpdated || !rpg::SaveSceneToFile(g_editor.scenePath, g_editor.scene, &targetError)) {
        g_editor.status = L"传送点连接保存失败";
        return false;
    }

    g_editor.dirty = false;
    g_editor.linkingTeleport = false;
    g_editor.linkSourceScene.clear();
    g_editor.linkSourceId.clear();
    g_editor.selectedObject = targetIndex;
    g_editor.status = L"传送点已双向连接";
    MessageBoxW(hwnd, L"传送点已双向连接。", L"连接成功", MB_OK | MB_ICONINFORMATION);
    return true;
}

void LoadSceneDialog(HWND hwnd) {
    g_editor.saveDirectory.clear();
    g_editor.scenePath.clear();
    LoadEditorScene(hwnd);
}

void SaveSceneAsDialog(HWND hwnd) {
    std::filesystem::path path;
    if (PromptSaveSceneFile(hwnd, g_editor.scenePath, path)) {
        g_editor.scenePath = path;
        SaveEditorScene();
    }
}

bool LoadBackgroundImage(HWND hwnd) {
    if (!rpg::SceneSupportsBackgroundImage(g_editor.scene)) {
        MessageBoxW(
            hwnd,
            L"大于 128 x 128 格的地图不支持整图底图，请使用地形和场景对象。",
            L"无法加载底图",
            MB_OK | MB_ICONINFORMATION);
        g_editor.status = L"大地图不支持整图底图";
        return false;
    }

    std::filesystem::path path;
    if (!PromptOpenImageFile(hwnd, path)) {
        return false;
    }

    auto bitmap = LoadBitmapFromPath(path);
    if (!bitmap) {
        g_editor.status = L"底图加载失败";
        return false;
    }

    g_editor.scene.backgroundImagePath = StoreBackgroundPath(path);
    g_backgroundCache.path = g_editor.scene.backgroundImagePath;
    g_backgroundCache.bitmap = std::move(bitmap);
    g_backgroundCache.attempted = true;
    MarkDirty(L"已载入底图");
    return true;
}

bool ClearBackgroundImage() {
    if (g_editor.scene.backgroundImagePath.empty()) {
        g_editor.status = L"没有可清除的底图";
        return false;
    }

    g_editor.scene.backgroundImagePath.clear();
    ReleaseBackgroundResources();
    MarkDirty(L"已清除底图");
    return true;
}

void AddObjectAt(rpg::Vec2 point, std::string_view type = {}) {
    const std::string objectType = type.empty() ? g_editor.selectedType : std::string(type);
    const rpg::SceneObjectDef* def = rpg::FindObjectDef(objectType);
    if (!def || !def->placeable) {
        g_editor.status = L"未选择有效对象类型";
        return;
    }

    point.x = Clamp(point.x, 0.0f, rpg::SceneWorldWidth(g_editor.scene));
    point.y = Clamp(point.y, 0.0f, rpg::SceneWorldHeight(g_editor.scene));

    rpg::SceneObject object = rpg::MakeObject(objectType, point, g_editor.nextObjectId++);
    const std::string groupId = object.id;
    if (!def->companionType.empty() && rpg::FindObjectDef(def->companionType)) {
        object.groupId = groupId;
    }

    g_editor.scene.objects.push_back(object);
    g_editor.selectedObject = static_cast<int>(g_editor.scene.objects.size()) - 1;
    if (!def->companionType.empty()) {
        if (rpg::FindObjectDef(def->companionType)) {
            rpg::SceneObject companion = rpg::MakeObject(def->companionType, point, g_editor.nextObjectId++);
            companion.groupId = groupId;
            g_editor.scene.objects.push_back(companion);
        } else {
            g_editor.status = L"伴随对象未找到";
        }
    }
    g_editor.dragging = def->draggable;
    g_editor.dragStartWorld = point;
    g_editor.dragStartObject = point;
    MarkDirty(L"已放置对象");
}

rpg::Vec2 BuildingAnchor(rpg::Vec2 point, const rpg::SceneObjectDef& def) {
    const int maxTileX = std::max(0, g_editor.scene.mapWidth - def.footprintWidth);
    const int maxTileY = std::max(0, g_editor.scene.mapHeight - def.footprintHeight);
    const int tx = std::clamp(static_cast<int>(std::floor(point.x / kTileSize)), 0, maxTileX);
    const int ty = std::clamp(static_cast<int>(std::floor(point.y / kTileSize)), 0, maxTileY);
    return {
        (static_cast<float>(tx) + def.footprintWidth * 0.5f) * kTileSize,
        (static_cast<float>(ty) + static_cast<float>(def.footprintHeight)) * kTileSize,
    };
}

bool ObjectCanDrag(const rpg::SceneObject& object) {
    const rpg::SceneObjectDef* def = rpg::FindObjectDef(object.type);
    return !def || def->draggable;
}

void DeleteSelectedObject() {
    if (g_editor.selectedObject < 0 || g_editor.selectedObject >= static_cast<int>(g_editor.scene.objects.size())) {
        return;
    }
    const std::string groupId = g_editor.scene.objects[g_editor.selectedObject].groupId;
    if (!groupId.empty()) {
        for (int i = static_cast<int>(g_editor.scene.objects.size()) - 1; i >= 0; --i) {
            if (g_editor.scene.objects[i].groupId == groupId) {
                g_editor.scene.objects.erase(g_editor.scene.objects.begin() + i);
            }
        }
    } else {
        g_editor.scene.objects.erase(g_editor.scene.objects.begin() + g_editor.selectedObject);
    }
    g_editor.selectedObject = -1;
    MarkDirty(L"已删除对象");
}

void RunFooterAction(HWND hwnd, FooterAction action) {
    DebugTrace(L"[RunFooterAction] action=" + std::to_wstring(static_cast<int>(action)));
    DebugTraceAscii("footer:action=" + std::to_string(static_cast<int>(action)));
    switch (action) {
    case FooterAction::CreateMap:
        CreateMap(hwnd);
        break;
    case FooterAction::DeleteMap:
        DeleteMap(hwnd);
        break;
    case FooterAction::LoadScene:
        LoadSceneDialog(hwnd);
        break;
    case FooterAction::SaveSceneAs:
        SaveSceneAsDialog(hwnd);
        break;
    case FooterAction::LoadBackground:
        LoadBackgroundImage(hwnd);
        break;
    case FooterAction::ClearBackground:
        ClearBackgroundImage();
        break;
    case FooterAction::ExportBlankMap:
        ExportBlankMapTemplate(hwnd);
        break;
    case FooterAction::SaveScene:
        SaveEditorScene();
        break;
    case FooterAction::LinkTeleport:
        BeginTeleportLink();
        break;
    case FooterAction::DeleteObject:
        DeleteSelectedObject();
        break;
    case FooterAction::ToggleCollision:
        g_editor.showCollision = !g_editor.showCollision;
        g_editor.status = g_editor.showCollision ? L"碰撞已显示" : L"碰撞已隐藏";
        UpdateFooterButtonLabels();
        break;
    }
}

void MoveSelectedObject(rpg::Vec2 point) {
    if (g_editor.selectedObject < 0 || g_editor.selectedObject >= static_cast<int>(g_editor.scene.objects.size())) {
        return;
    }
    if (!ObjectCanDrag(g_editor.scene.objects[g_editor.selectedObject])) {
        g_editor.dragging = false;
        g_editor.status = L"该建筑按地格固定，不能拖动";
        return;
    }

    rpg::Vec2 delta{point.x - g_editor.dragStartWorld.x, point.y - g_editor.dragStartWorld.y};
    rpg::SceneObject& object = g_editor.scene.objects[g_editor.selectedObject];
    const float targetX = Clamp(g_editor.dragStartObject.x + delta.x, 0.0f, rpg::SceneWorldWidth(g_editor.scene));
    const float targetY = Clamp(g_editor.dragStartObject.y + delta.y, 0.0f, rpg::SceneWorldHeight(g_editor.scene));
    const rpg::Vec2 appliedDelta{targetX - object.pos.x, targetY - object.pos.y};
    const std::string groupId = object.groupId;
    if (!groupId.empty()) {
        for (rpg::SceneObject& grouped : g_editor.scene.objects) {
            if (grouped.groupId == groupId) {
                grouped.pos.x = Clamp(grouped.pos.x + appliedDelta.x, 0.0f, rpg::SceneWorldWidth(g_editor.scene));
                grouped.pos.y = Clamp(grouped.pos.y + appliedDelta.y, 0.0f, rpg::SceneWorldHeight(g_editor.scene));
            }
        }
    } else {
        object.pos.x = targetX;
        object.pos.y = targetY;
    }
    MarkDirty(L"已移动对象");
}

void PaintTerrainCell(int tx, int ty) {
    bool changed = false;
    if (g_editor.mode == EditorMode::Natural) {
        changed = rpg::SetNaturalTerrain(g_editor.scene, tx, ty, g_editor.selectedNatural);
    } else if (g_editor.mode == EditorMode::Built) {
        changed = rpg::SetBuiltTerrain(g_editor.scene, tx, ty, g_editor.selectedBuilt);
    }
    if (changed) {
        if (g_editor.mode == EditorMode::Natural) {
            MarkDirty(g_editor.selectedNatural == "none" ? L"已擦除自然地皮" : L"已绘制自然地皮");
        } else {
            MarkDirty(L"已绘制建筑地板");
        }
    }
}

void PaintTerrainAt(rpg::Vec2 point) {
    const int targetX = static_cast<int>(std::floor(point.x / kTileSize));
    const int targetY = static_cast<int>(std::floor(point.y / kTileSize));
    if (targetX < 0 || targetY < 0 || targetX >= g_editor.scene.mapWidth || targetY >= g_editor.scene.mapHeight) {
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

void UpdateNaturalSelection(int screenX, int screenY) {
    const rpg::Vec2 world = ScreenToWorld(screenX, screenY);
    g_editor.selectionEndTileX = std::clamp(
        static_cast<int>(std::floor(world.x / kTileSize)),
        0,
        std::max(0, g_editor.scene.mapWidth - 1));
    g_editor.selectionEndTileY = std::clamp(
        static_cast<int>(std::floor(world.y / kTileSize)),
        0,
        std::max(0, g_editor.scene.mapHeight - 1));
}

void ApplyNaturalSelection() {
    const int left = std::min(g_editor.selectionStartTileX, g_editor.selectionEndTileX);
    const int right = std::max(g_editor.selectionStartTileX, g_editor.selectionEndTileX);
    const int top = std::min(g_editor.selectionStartTileY, g_editor.selectionEndTileY);
    const int bottom = std::max(g_editor.selectionStartTileY, g_editor.selectionEndTileY);
    bool changed = false;
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            changed = rpg::SetNaturalTerrain(g_editor.scene, x, y, g_editor.selectedNatural) || changed;
        }
    }
    if (changed) {
        const int width = right - left + 1;
        const int height = bottom - top + 1;
        MarkDirty(
            (g_editor.selectedNatural == "none" ? L"已批量擦除自然地皮 " : L"已批量铺设自然地皮 ") +
            std::to_wstring(width) + L"x" + std::to_wstring(height));
    }
}

rpg::TerrainDef const* NaturalTerrainForIndex(int index) {
    const auto& defs = rpg::NaturalTerrainDefs();
    if (index <= 0) {
        return nullptr;
    }
    const int defIndex = index - 1;
    return defIndex >= 0 && defIndex < static_cast<int>(defs.size()) ? &defs[defIndex] : nullptr;
}

rpg::TerrainDef const* BuiltTerrainForIndex(int index) {
    const auto& defs = rpg::BuiltTerrainDefs();
    const int defIndex = index - 1;
    return defIndex >= 0 && defIndex < static_cast<int>(defs.size()) ? &defs[defIndex] : nullptr;
}

const rpg::SceneObjectDef* BuiltObjectForIndex(int index) {
    const int firstIndex = static_cast<int>(rpg::BuiltTerrainDefs().size()) + 1;
    const int defIndex = index - firstIndex;
    const auto defs = BuildingObjectDefs();
    return defIndex >= 0 && defIndex < static_cast<int>(defs.size()) ? defs[defIndex] : nullptr;
}

void DrawPalette(HDC hdc, const RECT& client) {
    RECT panel{0, 0, kPaletteWidth, client.bottom};
    FillRectColor(hdc, panel, RGB(38, 44, 48));

    DrawTextLine(hdc, L"场景编辑器", 18, 18, RGB(244, 242, 225));

    constexpr const wchar_t* tabNames[] = {L"地图", L"对象", L"自然", L"建筑"};
    const int tabWidth = (kPaletteWidth - 16) / 4;
    for (int i = 0; i < 4; ++i) {
        const bool selected = static_cast<int>(g_editor.mode) == i;
        RECT tab{
            8 + i * tabWidth,
            kPaletteTabsY,
            i == 3 ? kPaletteWidth - 8 : 8 + (i + 1) * tabWidth,
            kPaletteTabsY + kPaletteTabsHeight,
        };
        FillRectColor(hdc, tab, selected ? RGB(79, 102, 87) : RGB(51, 59, 62));
        DrawTextLine(hdc, tabNames[i], tab.left + 5, tab.top + 8, selected ? RGB(248, 245, 220) : RGB(185, 193, 188));
    }

    const auto objectDefs = PlaceableObjectDefs();
    SaveDC(hdc);
    IntersectClipRect(hdc, 0, kPaletteFirstY, kPaletteWidth, std::max<LONG>(kPaletteFirstY, client.bottom - kPaletteFooterHeight));
    for (int i = 0; i < PaletteItemCount(); ++i) {
        bool selected = false;
        std::wstring label;
        COLORREF swatch = RGB(56, 65, 68);
        bool showSwatch = false;

        if (g_editor.mode == EditorMode::Maps) {
            selected = i == g_editor.selectedMap;
            if (i < static_cast<int>(g_editor.maps.size())) {
                label = g_editor.maps[i].displayName;
                showSwatch = true;
                swatch = g_editor.maps[i].hasPlayerStart ? RGB(91, 169, 208) : RGB(78, 100, 112);
            }
        } else if (g_editor.mode == EditorMode::Objects) {
            selected = objectDefs[i]->type == g_editor.selectedType;
            label = objectDefs[i]->displayName;
        } else if (g_editor.mode == EditorMode::Natural) {
            if (i == 0) {
                selected = g_editor.selectedNatural == "none";
                label = L"擦除";
                swatch = RGB(34, 42, 45);
                showSwatch = true;
            } else if (const rpg::TerrainDef* terrain = NaturalTerrainForIndex(i)) {
                selected = terrain->id == g_editor.selectedNatural;
                label = terrain->displayName;
                swatch = rpg::TerrainFallbackColor(*terrain);
                showSwatch = true;
            }
        } else {
            if (const rpg::SceneObjectDef* object = BuiltObjectForIndex(i)) {
                selected = g_editor.selectedBuiltObject == object->type;
                label = object->displayName;
                swatch = object->teleport ? RGB(185, 185, 185) : RGB(151, 106, 71);
                showSwatch = true;
            } else {
                const rpg::TerrainDef* terrain = BuiltTerrainForIndex(i);
                selected = g_editor.selectedBuiltObject.empty() &&
                    (terrain ? terrain->id == g_editor.selectedBuilt : g_editor.selectedBuilt == "none");
                label = terrain ? terrain->displayName : L"擦除";
                swatch = terrain ? rpg::TerrainFallbackColor(*terrain) : RGB(34, 42, 45);
                showSwatch = true;
            }
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

    std::wstring dirty = g_editor.dirty ? L"未保存" : L"已保存";
    DrawTextLine(hdc, dirty, 18, client.bottom - 52, g_editor.dirty ? RGB(255, 210, 115) : RGB(175, 224, 160));
    DrawTextLine(hdc, g_editor.status, 18, client.bottom - 24, RGB(224, 226, 212));
}

void DrawMap(HDC hdc, const RECT& client) {
    rpg::DrawTerrain(
        hdc,
        g_editor.scene,
        g_editor.cameraX - kPaletteWidth,
        g_editor.cameraY,
        true,
        g_editor.zoom,
        client.right - client.left,
        client.bottom - client.top);
}

void DrawSceneObjects(HDC hdc, const RECT& client) {
    std::vector<int> indices;
    indices.reserve(g_editor.scene.objects.size());
    for (int i = 0; i < static_cast<int>(g_editor.scene.objects.size()); ++i) {
        const rpg::RectF bounds = rpg::ObjectVisualBounds(g_editor.scene.objects[i]);
        constexpr float margin = 64.0f;
        const float cameraX = g_editor.cameraX - kPaletteWidth;
        if (bounds.right * g_editor.zoom - cameraX < kPaletteWidth - margin ||
            bounds.left * g_editor.zoom - cameraX > client.right + margin ||
            bounds.bottom * g_editor.zoom - g_editor.cameraY < -margin ||
            bounds.top * g_editor.zoom - g_editor.cameraY > client.bottom + margin) {
            continue;
        }
        indices.push_back(i);
    }
    std::stable_sort(indices.begin(), indices.end(), [](int a, int b) {
        return rpg::ObjectSortY(g_editor.scene.objects[a]) < rpg::ObjectSortY(g_editor.scene.objects[b]);
    });

    for (int index : indices) {
        if (rpg::ObjectIsGroundOverlay(g_editor.scene.objects[index])) {
            rpg::DrawSceneObject(
                hdc,
                g_editor.scene.objects[index],
                g_editor.cameraX - kPaletteWidth,
                g_editor.cameraY,
                index == g_editor.selectedObject,
                g_editor.zoom);
        }
    }

    for (int index : indices) {
        if (rpg::ObjectIsGroundOverlay(g_editor.scene.objects[index])) {
            continue;
        }
        rpg::DrawSceneObject(
            hdc,
            g_editor.scene.objects[index],
            g_editor.cameraX - kPaletteWidth,
            g_editor.cameraY,
            index == g_editor.selectedObject,
            g_editor.zoom);
    }

    if (g_editor.showCollision) {
        for (int index : indices) {
            rpg::DrawSceneObjectCollision(
                hdc,
                g_editor.scene.objects[index],
                g_editor.cameraX - kPaletteWidth,
                g_editor.cameraY,
                index == g_editor.selectedObject ? RGB(255, 245, 96) : RGB(219, 64, 64),
                g_editor.zoom);
        }
    }
}

void DrawNaturalSelection(HDC hdc) {
    if (!g_editor.selectingNaturalArea) {
        return;
    }
    const int leftTile = std::min(g_editor.selectionStartTileX, g_editor.selectionEndTileX);
    const int rightTile = std::max(g_editor.selectionStartTileX, g_editor.selectionEndTileX) + 1;
    const int topTile = std::min(g_editor.selectionStartTileY, g_editor.selectionEndTileY);
    const int bottomTile = std::max(g_editor.selectionStartTileY, g_editor.selectionEndTileY) + 1;
    RECT selection{
        static_cast<LONG>(std::round(kPaletteWidth + leftTile * kTileSize * g_editor.zoom - g_editor.cameraX)),
        static_cast<LONG>(std::round(topTile * kTileSize * g_editor.zoom - g_editor.cameraY)),
        static_cast<LONG>(std::round(kPaletteWidth + rightTile * kTileSize * g_editor.zoom - g_editor.cameraX)),
        static_cast<LONG>(std::round(bottomTile * kTileSize * g_editor.zoom - g_editor.cameraY)),
    };
    HBRUSH brush = CreateHatchBrush(HS_DIAGCROSS, RGB(105, 180, 210));
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(240, 224, 112));
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    Rectangle(hdc, selection.left, selection.top, selection.right, selection.bottom);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
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
    DrawBackgroundImage(hdc);
    DrawMap(hdc, client);
    DrawSceneObjects(hdc, client);
    DrawNaturalSelection(hdc);
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
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool repaint = true;

    switch (key) {
    case VK_DELETE:
        RunFooterAction(hwnd, shift ? FooterAction::DeleteMap : FooterAction::DeleteObject);
        break;
    case 'C':
        RunFooterAction(hwnd, FooterAction::ToggleCollision);
        break;
    case 'R':
        LoadEditorScene();
        break;
    case 'B':
        RunFooterAction(hwnd, FooterAction::ExportBlankMap);
        break;
    case 'N':
        RunFooterAction(hwnd, FooterAction::CreateMap);
        break;
    case 'L':
        if (shift) {
            RunFooterAction(hwnd, FooterAction::ClearBackground);
        } else {
            RunFooterAction(hwnd, FooterAction::LoadBackground);
        }
        break;
    case 'S':
        if (ctrl) {
            RunFooterAction(hwnd, FooterAction::SaveScene);
        } else {
            g_editor.cameraY += 48.0f;
        }
        break;
    case 'T':
        RunFooterAction(hwnd, FooterAction::LinkTeleport);
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
    case VK_ADD:
        SetZoomAt(hwnd, kPaletteWidth + kMinimumMapViewportWidth / 2, kWindowHeight / 2, 1.1f);
        break;
    case VK_SUBTRACT:
        SetZoomAt(hwnd, kPaletteWidth + kMinimumMapViewportWidth / 2, kWindowHeight / 2, 1.0f / 1.1f);
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
        LoadEditorScene(hwnd);
        CreateFooterButtons(hwnd);
        ClampCamera(hwnd);
        ClampPaletteScroll(hwnd);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        RECT minimumWindow{
            0,
            0,
            kPaletteWidth + kMinimumMapViewportWidth,
            kPaletteFirstY + kPaletteRowHeight + kPaletteFooterHeight,
        };
        constexpr DWORD windowStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
        AdjustWindowRectEx(&minimumWindow, windowStyle, FALSE, 0);
        info->ptMinTrackSize.x = minimumWindow.right - minimumWindow.left;
        info->ptMinTrackSize.y = minimumWindow.bottom - minimumWindow.top;
        return 0;
    }
    case WM_SIZE:
        LayoutFooterButtons(hwnd);
        ClampCamera(hwnd);
        ClampPaletteScroll(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN: {
        SetFocus(hwnd);
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);

        if (const int tab = PaletteTabHitTest(x, y); tab >= 0) {
            g_editor.mode = static_cast<EditorMode>(tab);
            g_editor.selectedObject = -1;
            g_editor.selectingNaturalArea = false;
            g_editor.paletteScroll = 0;
            g_editor.status = tab == 0 ? L"地图列表" :
                (tab == 1 ? L"对象工具" : (tab == 2 ? L"自然地皮" : L"建筑地板"));
            ClampPaletteScroll(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (const int footerButton = FooterButtonHitTest(hwnd, x, y); footerButton >= 0) {
            RunFooterAction(hwnd, kFooterButtons[footerButton].action);
            ClampCamera(hwnd);
            ClampPaletteScroll(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (const int hit = PaletteHitTest(hwnd, x, y); hit >= 0) {
            if (g_editor.mode == EditorMode::Maps) {
                if (hit < static_cast<int>(g_editor.maps.size())) {
                    LoadEditorSceneFromPath(g_editor.maps[hit].path);
                    if (g_editor.linkingTeleport) {
                        g_editor.mode = EditorMode::Built;
                        g_editor.selectedObject = -1;
                        g_editor.paletteScroll = 0;
                        g_editor.status = L"已选择地图，请点击目标传送点";
                    }
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            } else if (g_editor.mode == EditorMode::Objects) {
                const auto objectDefs = PlaceableObjectDefs();
                if (hit < static_cast<int>(objectDefs.size())) {
                    g_editor.selectedType = objectDefs[hit]->type;
                }
            } else if (g_editor.mode == EditorMode::Natural) {
                if (hit == 0) {
                    g_editor.selectedNatural = "none";
                } else if (const rpg::TerrainDef* terrain = NaturalTerrainForIndex(hit)) {
                    g_editor.selectedNatural = terrain->id;
                }
            } else {
                if (const rpg::SceneObjectDef* object = BuiltObjectForIndex(hit)) {
                    g_editor.selectedBuiltObject = object->type;
                    g_editor.selectedType = object->type;
                    g_editor.status = object->teleport
                        ? L"已选择传送点，点击地格放置方形传送点"
                        : L"已选择建筑，点击地格放置";
                } else if (const rpg::TerrainDef* terrain = BuiltTerrainForIndex(hit)) {
                    g_editor.selectedBuiltObject.clear();
                    g_editor.selectedBuilt = terrain->id;
                    g_editor.status = L"已选择建筑地板";
                } else {
                    g_editor.selectedBuiltObject.clear();
                    g_editor.selectedBuilt = "none";
                    g_editor.status = L"已选择建筑层擦除工具";
                }
            }
            g_editor.selectedObject = -1;
            if (g_editor.mode != EditorMode::Built) {
                g_editor.status = L"已选择工具";
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (x >= kPaletteWidth) {
            if (GetKeyState(VK_SPACE) & 0x8000) {
                g_editor.panning = true;
                g_editor.panningWithMiddleButton = false;
                g_editor.panStartScreen = POINT{x, y};
                g_editor.panStartCameraX = g_editor.cameraX;
                g_editor.panStartCameraY = g_editor.cameraY;
                SetCapture(hwnd);
                return 0;
            }
            const rpg::Vec2 world = ScreenToWorld(x, y);
            if (g_editor.linkingTeleport) {
                const int objectIndex = HitObject(world);
                if (objectIndex >= 0) {
                    CompleteTeleportLink(hwnd, objectIndex);
                } else {
                    g_editor.status = L"请点击目标传送点完成连接";
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }

            const rpg::SceneObjectDef* selectedBuilding =
                g_editor.mode == EditorMode::Built
                    ? rpg::FindObjectDef(g_editor.selectedBuiltObject)
                    : nullptr;
            if (selectedBuilding && !selectedBuilding->building) {
                selectedBuilding = nullptr;
            }
            if (g_editor.mode == EditorMode::Built) {
                const int objectIndex = HitObject(world);
                const rpg::SceneObjectDef* hitDef = objectIndex >= 0
                    ? rpg::FindObjectDef(g_editor.scene.objects[objectIndex].type)
                    : nullptr;
                if (objectIndex >= 0 && hitDef && hitDef->building) {
                    g_editor.selectedObject = objectIndex;
                    g_editor.dragging = false;
                    g_editor.status = hitDef->teleport
                        ? L"已选中传送点，可连接或删除"
                        : L"已选中建筑，可删除";
                } else if (selectedBuilding) {
                    AddObjectAt(BuildingAnchor(world, *selectedBuilding), selectedBuilding->type);
                    g_editor.dragging = false;
                    g_editor.status = selectedBuilding->teleport
                        ? L"已放置传送点，请选中后点击“连接传送点”"
                        : L"已放置建筑";
                } else {
                    g_editor.paintingTerrain = true;
                    g_editor.lastPaintTileX = -1;
                    g_editor.lastPaintTileY = -1;
                    PaintTerrainAt(world);
                }
            } else if (g_editor.mode == EditorMode::Objects) {
                const int objectIndex = HitObject(world);
                if (objectIndex >= 0) {
                    g_editor.selectedObject = objectIndex;
                    if (!ObjectCanDrag(g_editor.scene.objects[objectIndex])) {
                        g_editor.dragging = false;
                        g_editor.status = rpg::ObjectIsTeleport(g_editor.scene.objects[objectIndex])
                            ? L"已选中传送点，可连接或删除"
                            : L"已选中固定建筑，可删除";
                    } else {
                        g_editor.dragging = true;
                        g_editor.dragStartWorld = world;
                        g_editor.dragStartObject = g_editor.scene.objects[objectIndex].pos;
                        g_editor.status = L"拖动对象";
                    }
                } else {
                    AddObjectAt(world);
                }
            } else if (g_editor.mode == EditorMode::Natural) {
                g_editor.paintingTerrain = true;
                g_editor.lastPaintTileX = -1;
                g_editor.lastPaintTileY = -1;
                PaintTerrainAt(world);
            } else {
                return 0;
            }
            if (g_editor.dragging || g_editor.paintingTerrain) {
                SetCapture(hwnd);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &point);
        if (point.x >= kPaletteWidth) {
            const int steps = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            if (steps != 0) {
                SetZoomAt(hwnd, point.x, point.y, steps > 0 ? 1.1f : 1.0f / 1.1f);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        } else {
            const int steps = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            g_editor.paletteScroll -= steps * kPaletteRowHeight;
            ClampPaletteScroll(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g_editor.panning &&
            ((g_editor.panningWithMiddleButton && (wParam & MK_MBUTTON)) ||
             (!g_editor.panningWithMiddleButton && (wParam & MK_LBUTTON)))) {
            g_editor.cameraX = g_editor.panStartCameraX - static_cast<float>(GET_X_LPARAM(lParam) - g_editor.panStartScreen.x);
            g_editor.cameraY = g_editor.panStartCameraY - static_cast<float>(GET_Y_LPARAM(lParam) - g_editor.panStartScreen.y);
            ClampCamera(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (g_editor.selectingNaturalArea && (wParam & MK_RBUTTON)) {
            UpdateNaturalSelection(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (g_editor.paintingTerrain && (wParam & MK_LBUTTON)) {
            PaintTerrainAt(ScreenToWorld(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (g_editor.dragging && (wParam & MK_LBUTTON)) {
            MoveSelectedObject(ScreenToWorld(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_MBUTTONDOWN:
        if (GET_X_LPARAM(lParam) < kPaletteWidth) {
            return 0;
        }
        SetFocus(hwnd);
        g_editor.panning = true;
        g_editor.panningWithMiddleButton = true;
        g_editor.panStartScreen = POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        g_editor.panStartCameraX = g_editor.cameraX;
        g_editor.panStartCameraY = g_editor.cameraY;
        SetCapture(hwnd);
        return 0;
    case WM_MBUTTONUP:
        if (g_editor.panning && g_editor.panningWithMiddleButton) {
            g_editor.panning = false;
            g_editor.panningWithMiddleButton = false;
            ReleaseCapture();
        }
        return 0;
    case WM_RBUTTONDOWN: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        if (x < kPaletteWidth || g_editor.mode != EditorMode::Natural ||
            g_editor.scene.mapWidth <= 0 || g_editor.scene.mapHeight <= 0) {
            return 0;
        }
        SetFocus(hwnd);
        const rpg::Vec2 world = ScreenToWorld(x, y);
        g_editor.selectionStartTileX = std::clamp(
            static_cast<int>(std::floor(world.x / kTileSize)), 0, g_editor.scene.mapWidth - 1);
        g_editor.selectionStartTileY = std::clamp(
            static_cast<int>(std::floor(world.y / kTileSize)), 0, g_editor.scene.mapHeight - 1);
        g_editor.selectionEndTileX = g_editor.selectionStartTileX;
        g_editor.selectionEndTileY = g_editor.selectionStartTileY;
        g_editor.selectingNaturalArea = true;
        g_editor.status = g_editor.selectedNatural == "none"
            ? L"右键框选批量擦除区域"
            : L"右键框选批量铺设区域";
        SetCapture(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_RBUTTONUP:
        if (g_editor.selectingNaturalArea) {
            UpdateNaturalSelection(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            ApplyNaturalSelection();
            g_editor.selectingNaturalArea = false;
            ReleaseCapture();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_CONTEXTMENU:
        return 0;
    case WM_LBUTTONUP:
        if ((g_editor.panning && !g_editor.panningWithMiddleButton) ||
            g_editor.dragging || g_editor.paintingTerrain) {
            if (!g_editor.panningWithMiddleButton) {
                g_editor.panning = false;
            }
            g_editor.dragging = false;
            g_editor.paintingTerrain = false;
            g_editor.lastPaintTileX = -1;
            g_editor.lastPaintTileY = -1;
            ReleaseCapture();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_CAPTURECHANGED:
        g_editor.panning = false;
        g_editor.panningWithMiddleButton = false;
        g_editor.dragging = false;
        g_editor.paintingTerrain = false;
        g_editor.selectingNaturalArea = false;
        g_editor.lastPaintTileX = -1;
        g_editor.lastPaintTileY = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_KEYDOWN:
        HandleKey(hwnd, wParam);
        return 0;
    case WM_COMMAND: {
        const UINT id = LOWORD(wParam);
        const UINT code = HIWORD(wParam);
        DebugTrace(L"[WM_COMMAND] id=" + std::to_wstring(id) + L" code=" + std::to_wstring(code));
        DebugTraceAscii("wm_command:id=" + std::to_string(id) + ":code=" + std::to_string(code));
        if (code == BN_CLICKED && id >= kFooterButtonBaseId && id < kFooterButtonBaseId + static_cast<UINT>(FooterButtonCount())) {
            const int index = static_cast<int>(id - kFooterButtonBaseId);
            RunFooterAction(hwnd, kFooterButtons[index].action);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        return 0;
    }
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
        ReleaseBackgroundResources();
        if (g_gdiplusStarted && g_gdiplusToken != 0) {
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

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCmd) {
    // Keep Win32 drawing and mouse coordinates in the same coordinate space on scaled displays.
    SetProcessDPIAware();
    const wchar_t kClassName[] = L"FreeWalkRpgSceneEditorWindow";

    WNDCLASSW wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    RegisterClassW(&wc);

    constexpr DWORD kWindowStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    RECT windowRect{0, 0, kWindowWidth, kWindowHeight};
    AdjustWindowRect(&windowRect, kWindowStyle, FALSE);

    HWND hwnd = CreateWindowExW(
        0,
        kClassName,
        L"自由行走 RPG 场景编辑器",
        kWindowStyle,
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
