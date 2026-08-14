#include "scene_render.h"

#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rpg {
namespace {

#ifndef RPG_ASSET_DIR
#define RPG_ASSET_DIR L"assets"
#endif

constexpr COLORREF kTransparentKey = RGB(255, 0, 255);

struct ObjectBitmap {
    std::string type;
    std::unique_ptr<Gdiplus::Bitmap> alphaBitmap;
    HBITMAP bitmap = nullptr;
    HDC dc = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    int width = 0;
    int height = 0;
    bool attempted = false;
};

std::vector<ObjectBitmap> g_objectBitmaps;
ULONG_PTR g_gdiplusToken = 0;

bool EnsureGdiPlus() {
    if (g_gdiplusToken != 0) {
        return true;
    }
    Gdiplus::GdiplusStartupInput input{};
    return Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr) == Gdiplus::Ok;
}

RECT ToScreenRect(RectF rect, float cameraX, float cameraY, float zoom) {
    return RECT{
        static_cast<LONG>(std::round(rect.left * zoom - cameraX)),
        static_cast<LONG>(std::round(rect.top * zoom - cameraY)),
        static_cast<LONG>(std::round(rect.right * zoom - cameraX)),
        static_cast<LONG>(std::round(rect.bottom * zoom - cameraY)),
    };
}

void FillRectColor(HDC hdc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
}

void DrawEllipseRaw(HDC hdc, int left, int top, int right, int bottom, COLORREF fill, COLORREF outline, int penWidth = 2) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, penWidth, outline);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    Ellipse(hdc, left, top, right, bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void DrawRoundRectRaw(HDC hdc, int left, int top, int right, int bottom, int roundW, int roundH, COLORREF fill, COLORREF outline) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 2, outline);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, left, top, right, bottom, roundW, roundH);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

std::filesystem::path AssetPath(const std::wstring& relative) {
    return std::filesystem::path(RPG_ASSET_DIR) / relative;
}

ObjectBitmap& BitmapCacheFor(std::string_view type) {
    for (ObjectBitmap& cache : g_objectBitmaps) {
        if (cache.type == type) {
            return cache;
        }
    }

    ObjectBitmap cache;
    cache.type = std::string(type);
    g_objectBitmaps.push_back(std::move(cache));
    return g_objectBitmaps.back();
}

bool LoadObjectBitmap(HDC hdc, const SceneObjectDef& def, ObjectBitmap& cache) {
    if (cache.attempted) {
        return cache.alphaBitmap || (cache.bitmap && cache.dc);
    }
    cache.attempted = true;

    if (def.bitmapPath.empty()) {
        return false;
    }

    const std::filesystem::path imagePath = AssetPath(def.bitmapPath);
    if (imagePath.extension() == L".png") {
        if (!EnsureGdiPlus()) {
            return false;
        }
        cache.alphaBitmap = std::make_unique<Gdiplus::Bitmap>(imagePath.c_str());
        if (!cache.alphaBitmap || cache.alphaBitmap->GetLastStatus() != Gdiplus::Ok) {
            cache.alphaBitmap.reset();
            return false;
        }
        cache.width = static_cast<int>(cache.alphaBitmap->GetWidth());
        cache.height = static_cast<int>(cache.alphaBitmap->GetHeight());
        return cache.width > 0 && cache.height > 0;
    }

    const std::wstring path = imagePath.wstring();
    cache.bitmap = static_cast<HBITMAP>(LoadImageW(
        nullptr,
        path.c_str(),
        IMAGE_BITMAP,
        0,
        0,
        LR_LOADFROMFILE | LR_CREATEDIBSECTION));

    if (!cache.bitmap) {
        return false;
    }

    BITMAP info{};
    GetObjectW(cache.bitmap, sizeof(info), &info);
    cache.width = info.bmWidth;
    cache.height = info.bmHeight;
    cache.dc = CreateCompatibleDC(hdc);
    if (!cache.dc) {
        DeleteObject(cache.bitmap);
        cache.bitmap = nullptr;
        return false;
    }
    cache.oldBitmap = SelectObject(cache.dc, cache.bitmap);
    return cache.width > 0 && cache.height > 0;
}

bool DrawObjectBitmap(HDC hdc, const SceneObject& object, float cameraX, float cameraY, float zoom, BYTE opacity) {
    const SceneObjectDef* def = FindObjectDef(object.type);
    if (!def) {
        return false;
    }

    ObjectBitmap& cache = BitmapCacheFor(object.type);
    if (!LoadObjectBitmap(hdc, *def, cache)) {
        return false;
    }

    const RECT bounds = ToScreenRect(ObjectVisualBounds(object), cameraX, cameraY, zoom);
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0) {
        return false;
    }

    if (cache.alphaBitmap) {
        Gdiplus::Graphics graphics(hdc);
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
        graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighSpeed);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        Gdiplus::ImageAttributes attributes;
        Gdiplus::ColorMatrix matrix = {
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, static_cast<float>(opacity) / 255.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
        };
        attributes.SetColorMatrix(&matrix, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);
        return graphics.DrawImage(
                   cache.alphaBitmap.get(),
                   Gdiplus::Rect(bounds.left, bounds.top, width, height),
                   0,
                   0,
                   cache.width,
                   cache.height,
                   Gdiplus::UnitPixel, &attributes) == Gdiplus::Ok;
    }

    if (opacity == 255) {
        TransparentBlt(
        hdc,
        bounds.left,
        bounds.top,
        width,
        height,
        cache.dc,
        0,
        0,
        cache.width,
        cache.height,
        kTransparentKey);
    } else {
        HDC preview = CreateCompatibleDC(hdc);
        HBITMAP bitmap = CreateCompatibleBitmap(hdc, width, height);
        HGDIOBJ oldBitmap = SelectObject(preview, bitmap);
        BitBlt(preview, 0, 0, width, height, hdc, bounds.left, bounds.top, SRCCOPY);
        TransparentBlt(preview, 0, 0, width, height, cache.dc, 0, 0, cache.width, cache.height, kTransparentKey);
        BLENDFUNCTION blend{AC_SRC_OVER, 0, opacity, 0};
        AlphaBlend(hdc, bounds.left, bounds.top, width, height, preview, 0, 0, width, height, blend);
        SelectObject(preview, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(preview);
    }

    return true;
}

void DrawTree(HDC hdc, int sx, int sy) {
    DrawRoundRectRaw(hdc, sx - 12, sy - 64, sx + 12, sy - 9, 10, 10, RGB(126, 89, 51), RGB(65, 49, 32));
    DrawEllipseRaw(hdc, sx - 47, sy - 114, sx + 17, sy - 50, RGB(60, 128, 78), RGB(31, 82, 50), 3);
    DrawEllipseRaw(hdc, sx - 18, sy - 124, sx + 50, sy - 54, RGB(74, 151, 88), RGB(31, 82, 50), 3);
    DrawEllipseRaw(hdc, sx - 55, sy - 94, sx + 56, sy - 20, RGB(83, 164, 95), RGB(31, 82, 50), 3);
    DrawEllipseRaw(hdc, sx - 22, sy - 82, sx + 64, sy - 18, RGB(91, 178, 104), RGB(31, 82, 50), 2);
}

void DrawStone(HDC hdc, int sx, int sy) {
    DrawEllipseRaw(hdc, sx - 31, sy - 35, sx + 31, sy - 2, RGB(124, 139, 146), RGB(62, 77, 83), 2);
    DrawEllipseRaw(hdc, sx - 16, sy - 31, sx + 11, sy - 13, RGB(151, 165, 171), RGB(151, 165, 171), 1);
}

void DrawBush(HDC hdc, int sx, int sy) {
    DrawEllipseRaw(hdc, sx - 35, sy - 39, sx + 5, sy - 3, RGB(64, 134, 77), RGB(34, 83, 49), 2);
    DrawEllipseRaw(hdc, sx - 6, sy - 45, sx + 37, sy - 3, RGB(77, 154, 86), RGB(34, 83, 49), 2);
    DrawEllipseRaw(hdc, sx - 28, sy - 54, sx + 28, sy - 10, RGB(91, 174, 95), RGB(34, 83, 49), 2);
}

ObjectVisual VisualFor(const SceneObject& object) {
    if (const SceneObjectDef* def = FindObjectDef(object.type)) {
        if (def->teleport) {
            return ObjectVisual::TeleportPoint;
        }
        return def->visual;
    }
    return ObjectVisual::StoneRound;
}

} // namespace

void DrawSceneObject(HDC hdc, const SceneObject& object, float cameraX, float cameraY, bool selected, float zoom, BYTE opacity) {
    const int sx = static_cast<int>(std::round(object.pos.x * zoom - cameraX));
    const int sy = static_cast<int>(std::round(object.pos.y * zoom - cameraY));

    if (!DrawObjectBitmap(hdc, object, cameraX, cameraY, zoom, opacity)) {
        switch (VisualFor(object)) {
        case ObjectVisual::TreeOak:
            DrawTree(hdc, sx, sy);
            break;
        case ObjectVisual::Bush:
            DrawBush(hdc, sx, sy);
            break;
        case ObjectVisual::TeleportPoint: {
            const RECT bounds = ToScreenRect(ObjectVisualBounds(object), cameraX, cameraY, zoom);
            FillRectColor(hdc, bounds, RGB(185, 185, 185));
            break;
        }
        case ObjectVisual::StoneRound:
        default:
            DrawStone(hdc, sx, sy);
            break;
        }
    }

    if (selected) {
        RECT bounds = ToScreenRect(ObjectVisualBounds(object), cameraX, cameraY, zoom);
        HPEN pen = CreatePen(PS_DOT, 1, RGB(255, 247, 122));
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, bounds.left, bounds.top, bounds.right, bounds.bottom);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(pen);
    }
}

void DrawSceneObjectCollision(HDC hdc, const SceneObject& object, float cameraX, float cameraY, COLORREF color, float zoom) {
    if (!object.collision.blocks || object.collision.shape == CollisionShape::None) {
        return;
    }

    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));

    if (object.collision.shape == CollisionShape::Rect) {
        RECT rect = ToScreenRect(ObjectCollisionRect(object), cameraX, cameraY, zoom);
        Rectangle(hdc, rect.left, rect.top, rect.right, rect.bottom);
    } else if (object.collision.shape == CollisionShape::Circle) {
        const int cx = static_cast<int>(std::round((object.pos.x + object.collision.x) * zoom - cameraX));
        const int cy = static_cast<int>(std::round((object.pos.y + object.collision.y) * zoom - cameraY));
        const int r = static_cast<int>(std::round(object.collision.radius * zoom));
        Ellipse(hdc, cx - r, cy - r, cx + r, cy + r);
    }

    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
}

void ReleaseSceneRenderResources() {
    for (ObjectBitmap& cache : g_objectBitmaps) {
        cache.alphaBitmap.reset();
        if (cache.dc) {
            if (cache.oldBitmap) {
                SelectObject(cache.dc, cache.oldBitmap);
            }
            DeleteDC(cache.dc);
            cache.dc = nullptr;
        }
        if (cache.bitmap) {
            DeleteObject(cache.bitmap);
            cache.bitmap = nullptr;
        }
        cache.oldBitmap = nullptr;
    }
    g_objectBitmaps.clear();
    if (g_gdiplusToken != 0) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
}

} // namespace rpg
