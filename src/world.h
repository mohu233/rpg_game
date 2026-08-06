#pragma once

namespace rpg {

constexpr int kTileSize = 48;
constexpr int kMapWidth = 28;
constexpr int kMapHeight = 18;

inline float WorldWidth() {
    return static_cast<float>(kMapWidth * kTileSize);
}

inline float WorldHeight() {
    return static_cast<float>(kMapHeight * kTileSize);
}

} // namespace rpg
