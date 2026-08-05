#pragma once

#include <array>

namespace rpg {

constexpr int kTileSize = 48;
constexpr int kMapWidth = 28;
constexpr int kMapHeight = 18;

inline constexpr std::array<const wchar_t*, kMapHeight> kWorldMap = {
    L"############################",
    L"#..........#...............#",
    L"#..........#...............#",
    L"#..........#.......######..#",
    L"#.........................##",
    L"#.....######...............#",
    L"#.........................##",
    L"#.................##.......#",
    L"#.................##.......#",
    L"#......####................#",
    L"#....................###...#",
    L"#....................###...#",
    L"#............#####.........#",
    L"#..........................#",
    L"#....###...................#",
    L"#..........................#",
    L"#..........................#",
    L"############################",
};

inline bool IsWallTile(int tx, int ty) {
    if (tx < 0 || ty < 0 || tx >= kMapWidth || ty >= kMapHeight) {
        return true;
    }
    return kWorldMap[ty][tx] == L'#';
}

inline float WorldWidth() {
    return static_cast<float>(kMapWidth * kTileSize);
}

inline float WorldHeight() {
    return static_cast<float>(kMapHeight * kTileSize);
}

} // namespace rpg
