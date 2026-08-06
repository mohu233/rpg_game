#include "terrain_render.h"

#include <gdiplus.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rpg {
namespace {

constexpr COLORREF kMissingTerrain = RGB(220, 38, 170);

#ifndef RPG_ASSET_DIR
#define RPG_ASSET_DIR L"assets"
#endif

enum class MaskDirection {
    Left,
    Right,
    Top,
    Bottom,
    Count,
};

struct TerrainBitmap {
    std::string id;
    TerrainLayer layer = TerrainLayer::Natural;
    std::vector<std::unique_ptr<Gdiplus::Bitmap>> bases;
    std::vector<std::array<std::unique_ptr<Gdiplus::Bitmap>, static_cast<size_t>(MaskDirection::Count)>> masked;
    bool attempted = false;
};

ULONG_PTR g_gdiplusToken = 0;
std::array<std::unique_ptr<Gdiplus::Bitmap>, static_cast<size_t>(MaskDirection::Count)> g_masks;
bool g_masksAttempted = false;
std::vector<TerrainBitmap> g_terrainBitmaps;

bool EnsureGdiPlus() {
    if (g_gdiplusToken != 0) {
        return true;
    }
    Gdiplus::GdiplusStartupInput input;
    return Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr) == Gdiplus::Ok;
}

std::filesystem::path AssetPath(const std::wstring& relative) {
    return std::filesystem::path(RPG_ASSET_DIR) / relative;
}

std::unique_ptr<Gdiplus::Bitmap> LoadPng(const std::filesystem::path& path) {
    if (!EnsureGdiPlus()) {
        return nullptr;
    }
    auto bitmap = std::make_unique<Gdiplus::Bitmap>(path.c_str());
    if (bitmap->GetLastStatus() != Gdiplus::Ok || bitmap->GetWidth() != kTileSize || bitmap->GetHeight() != kTileSize) {
        return nullptr;
    }
    return bitmap;
}

bool EnsureMasks() {
    if (g_masksAttempted) {
        for (const auto& mask : g_masks) {
            if (!mask) {
                return false;
            }
        }
        return true;
    }
    g_masksAttempted = true;
    constexpr const wchar_t* names[] = {L"left.png", L"right.png", L"top.png", L"bottom.png"};
    for (size_t i = 0; i < g_masks.size(); ++i) {
        g_masks[i] = LoadPng(AssetPath(std::wstring(L"terrain/masks/") + names[i]));
        if (!g_masks[i]) {
            return false;
        }
    }
    return true;
}

BYTE* BitmapRow(Gdiplus::BitmapData& data, int y) {
    return static_cast<BYTE*>(data.Scan0) + y * data.Stride;
}

std::unique_ptr<Gdiplus::Bitmap> ApplyAlphaMask(Gdiplus::Bitmap& texture, Gdiplus::Bitmap& mask) {
    auto output = std::make_unique<Gdiplus::Bitmap>(kTileSize, kTileSize, PixelFormat32bppARGB);
    Gdiplus::Rect bounds(0, 0, kTileSize, kTileSize);
    Gdiplus::BitmapData textureData{};
    Gdiplus::BitmapData maskData{};
    Gdiplus::BitmapData outputData{};

    if (texture.LockBits(&bounds, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &textureData) != Gdiplus::Ok) {
        return nullptr;
    }
    if (mask.LockBits(&bounds, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &maskData) != Gdiplus::Ok) {
        texture.UnlockBits(&textureData);
        return nullptr;
    }
    if (output->LockBits(&bounds, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &outputData) != Gdiplus::Ok) {
        mask.UnlockBits(&maskData);
        texture.UnlockBits(&textureData);
        return nullptr;
    }

    for (int y = 0; y < kTileSize; ++y) {
        const BYTE* source = BitmapRow(textureData, y);
        const BYTE* alpha = BitmapRow(maskData, y);
        BYTE* target = BitmapRow(outputData, y);
        for (int x = 0; x < kTileSize; ++x) {
            const int offset = x * 4;
            target[offset + 0] = source[offset + 0];
            target[offset + 1] = source[offset + 1];
            target[offset + 2] = source[offset + 2];
            target[offset + 3] = static_cast<BYTE>(
                static_cast<unsigned int>(source[offset + 3]) * alpha[offset + 0] / 255u);
        }
    }

    output->UnlockBits(&outputData);
    mask.UnlockBits(&maskData);
    texture.UnlockBits(&textureData);
    return output;
}

TerrainBitmap& BitmapCacheFor(const TerrainDef& def) {
    for (TerrainBitmap& cached : g_terrainBitmaps) {
        if (cached.id == def.id && cached.layer == def.layer) {
            return cached;
        }
    }
    TerrainBitmap cached;
    cached.id = def.id;
    cached.layer = def.layer;
    g_terrainBitmaps.push_back(std::move(cached));
    return g_terrainBitmaps.back();
}

bool LoadTerrainBitmap(const TerrainDef& def, TerrainBitmap& cached) {
    if (cached.attempted) {
        return !cached.bases.empty();
    }
    cached.attempted = true;

    const std::filesystem::path baseRelative(def.imagePath);
    const bool masksReady = def.layer == TerrainLayer::Natural && EnsureMasks();
    for (int variant = 0; variant < def.variants; ++variant) {
        std::filesystem::path relative = baseRelative;
        if (variant > 0) {
            relative = baseRelative.parent_path() /
                (baseRelative.stem().wstring() + L"_" + std::to_wstring(variant) + baseRelative.extension().wstring());
        }
        auto bitmap = LoadPng(AssetPath(relative.wstring()));
        if (!bitmap) {
            cached.bases.clear();
            cached.masked.clear();
            return false;
        }

        std::array<std::unique_ptr<Gdiplus::Bitmap>, static_cast<size_t>(MaskDirection::Count)> maskedVariant;
        if (masksReady) {
            for (size_t i = 0; i < maskedVariant.size(); ++i) {
                maskedVariant[i] = ApplyAlphaMask(*bitmap, *g_masks[i]);
            }
        }
        cached.bases.push_back(std::move(bitmap));
        cached.masked.push_back(std::move(maskedVariant));
    }
    return true;
}

RECT TileRect(int tx, int ty, float cameraX, float cameraY) {
    return {
        static_cast<LONG>(std::round(tx * kTileSize - cameraX)),
        static_cast<LONG>(std::round(ty * kTileSize - cameraY)),
        static_cast<LONG>(std::round((tx + 1) * kTileSize - cameraX)),
        static_cast<LONG>(std::round((ty + 1) * kTileSize - cameraY)),
    };
}

void FillSolidRect(HDC hdc, const RECT& rect, COLORREF color) {
    SetDCBrushColor(hdc, color);
    FillRect(hdc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

void DrawLine(HDC hdc, int x1, int y1, int x2, int y2, COLORREF color) {
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
    SetDCPenColor(hdc, color);
    MoveToEx(hdc, x1, y1, nullptr);
    LineTo(hdc, x2, y2);
    SelectObject(hdc, oldPen);
}

size_t TerrainVariant(int tx, int ty, size_t count) {
    const unsigned int hash = static_cast<unsigned int>(tx) * 73856093u ^ static_cast<unsigned int>(ty) * 19349663u;
    return count == 0 ? 0 : hash % count;
}

bool DrawTerrainImage(Gdiplus::Graphics& graphics, const TerrainDef& def, const RECT& rect, int tx, int ty) {
    TerrainBitmap& cached = BitmapCacheFor(def);
    if (!LoadTerrainBitmap(def, cached)) {
        return false;
    }
    Gdiplus::Bitmap* bitmap = cached.bases[TerrainVariant(tx, ty, cached.bases.size())].get();
    const Gdiplus::Rect destination(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
    return graphics.DrawImage(bitmap, destination, 0, 0, kTileSize, kTileSize, Gdiplus::UnitPixel) == Gdiplus::Ok;
}

bool DrawMaskedTerrainImage(
    Gdiplus::Graphics& graphics,
    const TerrainDef& def,
    MaskDirection direction,
    const RECT& rect,
    int sourceTileX,
    int sourceTileY) {
    TerrainBitmap& cached = BitmapCacheFor(def);
    if (!LoadTerrainBitmap(def, cached)) {
        return false;
    }
    const size_t variant = TerrainVariant(sourceTileX, sourceTileY, cached.masked.size());
    Gdiplus::Bitmap* bitmap = cached.masked[variant][static_cast<size_t>(direction)].get();
    if (!bitmap) {
        return false;
    }
    const Gdiplus::Rect destination(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
    return graphics.DrawImage(bitmap, destination, 0, 0, kTileSize, kTileSize, Gdiplus::UnitPixel) == Gdiplus::Ok;
}

int TerrainPriority(const TerrainDef* def) {
    return def ? def->priority : -1000000;
}

bool NeighborWins(const TerrainDef* current, const TerrainDef* neighbor) {
    if (!neighbor || neighbor == current) {
        return false;
    }
    if (!current) {
        return true;
    }
    if (neighbor->priority != current->priority) {
        return TerrainPriority(neighbor) > TerrainPriority(current);
    }
    return neighbor->id > current->id;
}

} // namespace

COLORREF TerrainFallbackColor(const TerrainDef& terrain) {
    return RGB(
        (terrain.fallbackRgb >> 16) & 0xff,
        (terrain.fallbackRgb >> 8) & 0xff,
        terrain.fallbackRgb & 0xff);
}

void DrawTerrain(HDC hdc, const Scene& scene, float cameraX, float cameraY, bool showGrid) {
    if (!EnsureGdiPlus()) {
        return;
    }

    Gdiplus::Graphics graphics(hdc);
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighSpeed);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    for (int y = 0; y < kMapHeight; ++y) {
        for (int x = 0; x < kMapWidth; ++x) {
            const RECT rect = TileRect(x, y, cameraX, cameraY);
            const std::string_view currentId = NaturalTerrainAt(scene, x, y);
            if (currentId == "none") {
                continue;
            }
            const TerrainDef* current = FindTerrainDef(currentId, TerrainLayer::Natural);
            if (!current || !DrawTerrainImage(graphics, *current, rect, x, y)) {
                FillSolidRect(hdc, rect, current ? TerrainFallbackColor(*current) : kMissingTerrain);
            }

            struct Neighbor {
                int x;
                int y;
                MaskDirection mask;
            };
            constexpr Neighbor neighbors[] = {
                {-1, 0, MaskDirection::Left},
                {1, 0, MaskDirection::Right},
                {0, -1, MaskDirection::Top},
                {0, 1, MaskDirection::Bottom},
            };
            for (const Neighbor& offset : neighbors) {
                const std::string_view neighborId = NaturalTerrainAt(scene, x + offset.x, y + offset.y);
                if (neighborId == "none" || neighborId == currentId) {
                    continue;
                }
                const TerrainDef* neighbor = FindTerrainDef(neighborId, TerrainLayer::Natural);
                if (NeighborWins(current, neighbor)) {
                    DrawMaskedTerrainImage(graphics, *neighbor, offset.mask, rect, x + offset.x, y + offset.y);
                }
            }
        }
    }

    for (int y = 0; y < kMapHeight; ++y) {
        for (int x = 0; x < kMapWidth; ++x) {
            const std::string_view builtId = BuiltTerrainAt(scene, x, y);
            if (builtId == "none") {
                continue;
            }
            const TerrainDef* built = FindTerrainDef(builtId, TerrainLayer::Built);
            const RECT rect = TileRect(x, y, cameraX, cameraY);
            if (!built || !DrawTerrainImage(graphics, *built, rect, x, y)) {
                FillSolidRect(hdc, rect, built ? TerrainFallbackColor(*built) : kMissingTerrain);
            }
        }
    }

    for (int y = 0; y < kMapHeight; ++y) {
        for (int x = 0; x < kMapWidth; ++x) {
            const RECT rect = TileRect(x, y, cameraX, cameraY);
            if (showGrid) {
                DrawLine(hdc, rect.left, rect.top, rect.right, rect.top, RGB(74, 104, 83));
                DrawLine(hdc, rect.left, rect.top, rect.left, rect.bottom, RGB(74, 104, 83));
            }
        }
    }
}

void ReleaseTerrainRenderResources() {
    g_terrainBitmaps.clear();
    for (auto& mask : g_masks) {
        mask.reset();
    }
    g_masksAttempted = false;
    if (g_gdiplusToken != 0) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
}

} // namespace rpg
