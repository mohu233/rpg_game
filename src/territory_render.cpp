#include "territory_render.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <gdiplus.h>
#include <memory>
#include <vector>

#ifndef RPG_ASSET_DIR
#define RPG_ASSET_DIR L"assets"
#endif

namespace rpg {
namespace {

constexpr int kFlameFrameCount = 8;
constexpr int kAirflowFrameCount = 8;

struct BoundarySegment {
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;
    float normalX = 0.0f;
    float normalY = 0.0f;
};

struct BoundaryCache {
    const Scene* scene = nullptr;
    int width = 0;
    int height = 0;
    bool dirty = true;
    std::vector<BoundarySegment> segments;
};

BoundaryCache g_boundary;
std::unique_ptr<Gdiplus::Bitmap> g_flameStrip;
bool g_flameLoadAttempted = false;
std::unique_ptr<Gdiplus::Bitmap> g_airflowStrip;
bool g_airflowLoadAttempted = false;

void AddBoundarySegment(float x1, float y1, float x2, float y2, float normalX, float normalY) {
    g_boundary.segments.push_back({x1, y1, x2, y2, normalX, normalY});
}

void RebuildBoundary(const Scene& scene) {
    g_boundary.scene = &scene;
    g_boundary.width = scene.mapWidth;
    g_boundary.height = scene.mapHeight;
    g_boundary.dirty = false;
    g_boundary.segments.clear();
    g_boundary.segments.reserve(scene.territory.size());

    for (int y = 0; y < scene.mapHeight; ++y) {
        for (int x = 0; x < scene.mapWidth; ++x) {
            if (!TerritoryAt(scene, x, y)) continue;
            const float left = static_cast<float>(x * kTileSize);
            const float top = static_cast<float>(y * kTileSize);
            const float right = left + kTileSize;
            const float bottom = top + kTileSize;
            if (!TerritoryAt(scene, x, y - 1)) AddBoundarySegment(left, top, right, top, 0.0f, -1.0f);
            if (!TerritoryAt(scene, x + 1, y)) AddBoundarySegment(right, top, right, bottom, 1.0f, 0.0f);
            if (!TerritoryAt(scene, x, y + 1)) AddBoundarySegment(right, bottom, left, bottom, 0.0f, 1.0f);
            if (!TerritoryAt(scene, x - 1, y)) AddBoundarySegment(left, bottom, left, top, -1.0f, 0.0f);
        }
    }
}

bool SegmentVisible(const BoundarySegment& segment, float cameraX, float cameraY, float scale, int width, int height) {
    constexpr float margin = 48.0f;
    const float x1 = segment.x1 * scale - cameraX;
    const float y1 = segment.y1 * scale - cameraY;
    const float x2 = segment.x2 * scale - cameraX;
    const float y2 = segment.y2 * scale - cameraY;
    return std::max(x1, x2) >= -margin && std::min(x1, x2) <= width + margin &&
           std::max(y1, y2) >= -margin && std::min(y1, y2) <= height + margin;
}

Gdiplus::Bitmap* FlameStrip() {
    if (!g_flameLoadAttempted) {
        g_flameLoadAttempted = true;
        const std::filesystem::path path = std::filesystem::path(RPG_ASSET_DIR) / L"effects" / L"territory_flame.png";
        auto bitmap = std::make_unique<Gdiplus::Bitmap>(path.c_str());
        if (bitmap->GetLastStatus() == Gdiplus::Ok &&
            bitmap->GetWidth() >= kFlameFrameCount && bitmap->GetHeight() > 0) {
            g_flameStrip = std::move(bitmap);
        }
    }
    return g_flameStrip.get();
}

Gdiplus::Bitmap* AirflowStrip() {
    if (!g_airflowLoadAttempted) {
        g_airflowLoadAttempted = true;
        const std::filesystem::path path = std::filesystem::path(RPG_ASSET_DIR) / L"effects" / L"territory_airflow.png";
        auto bitmap = std::make_unique<Gdiplus::Bitmap>(path.c_str());
        if (bitmap->GetLastStatus() == Gdiplus::Ok &&
            bitmap->GetWidth() >= kAirflowFrameCount && bitmap->GetHeight() > 0) {
            g_airflowStrip = std::move(bitmap);
        }
    }
    return g_airflowStrip.get();
}

unsigned SegmentHash(const BoundarySegment& segment) {
    const auto x = static_cast<unsigned>(segment.x1);
    const auto y = static_cast<unsigned>(segment.y1);
    return (x * 73856093u) ^ (y * 19349663u) ^
           (segment.x1 == segment.x2 ? 83492791u : 2654435761u);
}

void SetFlameOpacity(Gdiplus::ImageAttributes& attributes, float alpha) {
    Gdiplus::ColorMatrix matrix = {{
        {1.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, alpha, 0.0f},
        {0.0f, 0.0f, 0.0f, 0.0f, 1.0f},
    }};
    attributes.SetColorMatrix(&matrix);
}

void DrawFlameSprite(
    Gdiplus::Graphics& graphics,
    Gdiplus::Bitmap& strip,
    Gdiplus::ImageAttributes& opacity,
    int sourceFrameWidth,
    int sourceFrameHeight,
    int frame,
    float x,
    float y,
    float drawWidth,
    float drawHeight,
    float baseInset,
    float angle) {
    const Gdiplus::GraphicsState state = graphics.Save();
    graphics.TranslateTransform(x, y);
    graphics.RotateTransform(angle);
    const Gdiplus::RectF destination(-drawWidth * 0.5f, -drawHeight + baseInset, drawWidth, drawHeight);
    graphics.DrawImage(
        &strip,
        destination,
        frame * sourceFrameWidth,
        0,
        sourceFrameWidth,
        sourceFrameHeight,
        Gdiplus::UnitPixel,
        &opacity);
    graphics.Restore(state);
}

void DrawVerticalFlameLines(
    Gdiplus::Graphics& graphics,
    const std::vector<const BoundarySegment*>& visible,
    float cameraX,
    float cameraY,
    float scale) {
    const float time = static_cast<float>(GetTickCount64() % 3000) / 3000.0f * 6.2831853f;
    Gdiplus::Pen glow(Gdiplus::Color(15, 20, 22, 23), 6.0f);
    Gdiplus::Pen core(Gdiplus::Color(38, 68, 71, 70), 1.5f);
    glow.SetStartCap(Gdiplus::LineCapRound);
    glow.SetEndCap(Gdiplus::LineCapRound);
    core.SetStartCap(Gdiplus::LineCapRound);
    core.SetEndCap(Gdiplus::LineCapRound);

    for (const BoundarySegment* segment : visible) {
        if (segment->x1 != segment->x2) continue;
        const float x = segment->x1 * scale - cameraX;
        const float y1 = segment->y1 * scale - cameraY;
        const float y2 = segment->y2 * scale - cameraY;
        constexpr int steps = 6;
        Gdiplus::PointF points[steps + 1];
        Gdiplus::GraphicsPath line;
        for (int i = 0; i <= steps; ++i) {
            const float t = static_cast<float>(i) / steps;
            const float y = y1 + (y2 - y1) * t;
            const float wave = std::sin(time + y * 0.085f + SegmentHash(*segment) * 0.001f) * 1.8f;
            points[i] = {x + wave, y};
        }
        line.AddLines(points, steps + 1);
        graphics.DrawPath(&glow, &line);
        graphics.DrawPath(&core, &line);
    }
}

void DrawAirflow(
    Gdiplus::Graphics& graphics,
    const std::vector<const BoundarySegment*>& visible,
    float cameraX,
    float cameraY,
    float scale) {
    Gdiplus::Bitmap* strip = AirflowStrip();
    if (!strip) return;
    const int sourceFrameWidth = static_cast<int>(strip->GetWidth()) / kAirflowFrameCount;
    const int sourceFrameHeight = static_cast<int>(strip->GetHeight());
    if (sourceFrameWidth <= 0 || sourceFrameHeight <= 0) return;

    constexpr float worldSpacing = 20.0f;
    const float seconds = static_cast<float>(GetTickCount64() % 1000000) / 1000.0f;
    for (const BoundarySegment* segment : visible) {
        const float worldDx = segment->x2 - segment->x1;
        const float worldDy = segment->y2 - segment->y1;
        const float worldLength = std::sqrt(worldDx * worldDx + worldDy * worldDy);
        const int sampleCount = std::max(1, static_cast<int>(std::ceil(worldLength / worldSpacing)));
        const unsigned hash = SegmentHash(*segment);
        for (int i = 0; i < sampleCount; ++i) {
            const float t = (static_cast<float>(i) + 0.5f) / sampleCount;
            const float baseX = (segment->x1 + worldDx * t) * scale - cameraX;
            const float baseY = (segment->y1 + worldDy * t) * scale - cameraY;
            const unsigned seed = hash ^ (static_cast<unsigned>(i) * 2246822519u);
            const float phase = static_cast<float>(seed % 1000) / 1000.0f;
            const float speed = 0.18f + static_cast<float>((seed >> 3) % 8) * 0.006f;
            const float progress = std::fmod(seconds * speed + phase, 1.0f);
            const float riseWorld = (2.0f + static_cast<float>((seed >> 7) % 101) / 100.0f) * kTileSize;
            const float riseScreen = progress * riseWorld * scale;
            const float sidewaysWorld = std::sin(progress * 7.0f + phase * 6.2831853f) *
                                        (3.0f + static_cast<float>((seed >> 12) % 5));
            const float worldWidth = 12.0f + static_cast<float>((seed >> 4) % 15);
            const float worldHeight = 10.0f + static_cast<float>((seed >> 9) % 18);
            const float sideways = sidewaysWorld * scale;
            const float width = worldWidth * scale;
            const float height = worldHeight * scale;
            const int frame = static_cast<int>((seed >> 15) % kAirflowFrameCount);

            Gdiplus::ImageAttributes frameOpacity;
            SetFlameOpacity(frameOpacity, (1.0f - progress) * 0.16f);
            const Gdiplus::RectF destination(
                baseX + sideways - width * 0.5f,
                baseY - riseScreen - height * 0.5f,
                width,
                height);
            graphics.DrawImage(
                strip,
                destination,
                frame * sourceFrameWidth,
                0,
                sourceFrameWidth,
                sourceFrameHeight,
                Gdiplus::UnitPixel,
                &frameOpacity);
        }
    }
}

void DrawFlames(Gdiplus::Graphics& graphics, const std::vector<const BoundarySegment*>& visible, float cameraX, float cameraY, float scale) {
    Gdiplus::Bitmap* strip = FlameStrip();
    if (!strip) return;

    const int sourceFrameWidth = static_cast<int>(strip->GetWidth()) / kFlameFrameCount;
    const int sourceFrameHeight = static_cast<int>(strip->GetHeight());
    if (sourceFrameWidth <= 0 || sourceFrameHeight <= 0) return;

    const unsigned globalFrame = static_cast<unsigned>(GetTickCount64() / 95);
    constexpr float worldSpacing = 16.0f;
    Gdiplus::ImageAttributes uprightOpacity;
    Gdiplus::ImageAttributes directionalOpacity;
    SetFlameOpacity(uprightOpacity, 0.27f);
    SetFlameOpacity(directionalOpacity, 0.19f);
    for (const BoundarySegment* segment : visible) {
        const float worldDx = segment->x2 - segment->x1;
        const float worldDy = segment->y2 - segment->y1;
        const float worldLength = std::sqrt(worldDx * worldDx + worldDy * worldDy);
        const int sampleCount = std::max(1, static_cast<int>(std::ceil(worldLength / worldSpacing)));
        const unsigned hash = SegmentHash(*segment);
        for (int i = 0; i < sampleCount; ++i) {
            const float t = (static_cast<float>(i) + 0.5f) / sampleCount;
            const float x = (segment->x1 + worldDx * t) * scale - cameraX;
            const float y = (segment->y1 + worldDy * t) * scale - cameraY;
            const unsigned phase = hash + static_cast<unsigned>(i * 3);
            const int frame = static_cast<int>((globalFrame + phase) % kFlameFrameCount);
            const float worldWidth = 20.0f + static_cast<float>((phase >> 3) % 4);
            const float worldHeight = 26.0f + static_cast<float>((phase >> 5) % 7);
            const float drawWidth = worldWidth * scale;
            const float drawHeight = worldHeight * scale;
            const float baseInset = worldHeight * (7.0f / sourceFrameHeight) * scale;

            // The upright layer gives horizontal edges a wall-like vertical silhouette.
            if (segment->y1 == segment->y2) {
                DrawFlameSprite(graphics, *strip, uprightOpacity, sourceFrameWidth, sourceFrameHeight,
                                frame, x, y, drawWidth, drawHeight, baseInset, 0.0f);
            }

            const float angle = std::atan2(segment->normalY, segment->normalX) * 180.0f / 3.14159265f + 90.0f;
            DrawFlameSprite(graphics, *strip, directionalOpacity, sourceFrameWidth, sourceFrameHeight,
                            frame, x, y, drawWidth * 0.82f, drawHeight * 0.78f, baseInset * 0.78f, angle);
        }
    }
}

} // namespace

void InvalidateTerritoryRenderCache() {
    g_boundary.dirty = true;
}

void DrawTerritoryBoundary(
    HDC hdc,
    const Scene& scene,
    float cameraX,
    float cameraY,
    float scale,
    int viewportWidth,
    int viewportHeight) {
    if (g_boundary.dirty || g_boundary.scene != &scene ||
        g_boundary.width != scene.mapWidth || g_boundary.height != scene.mapHeight) {
        RebuildBoundary(scene);
    }
    if (g_boundary.segments.empty()) return;

    std::vector<const BoundarySegment*> visible;
    visible.reserve(g_boundary.segments.size());
    for (const BoundarySegment& segment : g_boundary.segments) {
        if (SegmentVisible(segment, cameraX, cameraY, scale, viewportWidth, viewportHeight)) {
            visible.push_back(&segment);
        }
    }
    if (visible.empty()) return;

    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    Gdiplus::GraphicsPath path;
    for (const BoundarySegment* segment : visible) {
        path.StartFigure();
        path.AddLine(
            segment->x1 * scale - cameraX,
            segment->y1 * scale - cameraY,
            segment->x2 * scale - cameraX,
            segment->y2 * scale - cameraY);
    }

    Gdiplus::Pen outerFog(Gdiplus::Color(20, 10, 12, 13), 13.0f);
    outerFog.SetStartCap(Gdiplus::LineCapRound);
    outerFog.SetEndCap(Gdiplus::LineCapRound);
    outerFog.SetLineJoin(Gdiplus::LineJoinRound);
    graphics.DrawPath(&outerFog, &path);

    Gdiplus::Pen innerFog(Gdiplus::Color(46, 42, 45, 46), 6.0f);
    innerFog.SetStartCap(Gdiplus::LineCapRound);
    innerFog.SetEndCap(Gdiplus::LineCapRound);
    innerFog.SetLineJoin(Gdiplus::LineJoinRound);
    graphics.DrawPath(&innerFog, &path);

    Gdiplus::Pen core(Gdiplus::Color(68, 91, 94, 92), 1.5f);
    core.SetStartCap(Gdiplus::LineCapRound);
    core.SetEndCap(Gdiplus::LineCapRound);
    graphics.DrawPath(&core, &path);
    DrawVerticalFlameLines(graphics, visible, cameraX, cameraY, scale);
    DrawFlames(graphics, visible, cameraX, cameraY, scale);
    DrawAirflow(graphics, visible, cameraX, cameraY, scale);
}

void ReleaseTerritoryRenderResources() {
    g_flameStrip.reset();
    g_flameLoadAttempted = false;
    g_airflowStrip.reset();
    g_airflowLoadAttempted = false;
    g_boundary = {};
}

} // namespace rpg
