// map_gen — 맵을 파일 한 장이 아니라 시드 하나에서 만든다.
//
// 세 조각이 순서대로 돈다.
//
//   1. tools/procgen.hpp   재귀 백트래커가 완전 미로(신장 트리)를 판다. 트리라서
//                          스폰↔코어 사이에 단순 경로가 정확히 하나 있고, 모든
//                          칸이 도달 가능하다 — 연결성이 구조적으로 보장된다.
//   2. math/noise.hpp      그 위에 밀도장을 얹어 임계 이상인 벽을 깎는다. 순수
//                          미로는 1칸 회랑이라 타워를 세울 자리가 없다. 깎아야
//                          방과 회랑이 생기고, 그때 비로소 "어디에 세울까"라는
//                          질문이 성립한다. 같은 밀도장이 광맥과 타일 명암도 정한다.
//   3. tools/level_editor  깎은 결과를 flood fill 로 검증한다. 미로가 연결성을
//                          보장하고 감산만 하므로 실패할 수 없지만, "할 수 없다"와
//                          "확인했다"는 다른 문장이다. 확인하지 않은 불변식은
//                          다음 편집에서 조용히 깨진다.
//
// 시드는 A/B 스위치 `map_seed` 가 들고 있고 호스트·클라이언트가 같은 값을 받는다.
// 같은 시드 → 같은 격자이므로 맵 자체는 와이어를 건너지 않는다.

#pragma once

#include "game/defense_map.hpp"
#include "math/noise.hpp"
#include "tools/procgen.hpp"

#include <cstdint>
#include <vector>

namespace mega {
namespace game {
namespace mapgen {

struct TerrainParams {
    int      mazeCellsX = 20;      // 타일 폭 = 2*mazeCellsX + 1 = 41
    int      mazeCellsY = 20;
    uint32_t seed       = 7;
    float    frequency  = 0.17f;   // 밀도장 샘플 주기(칸당)
    int      octaves    = 4;
    float    carve      = 0.02f;   // 밀도가 이보다 크면 벽을 깎는다
    float    vein       = 0.20f;   // Worley F1 이 이보다 작으면 광맥
    int      cellSize   = 10;
    int      originY    = 30;
};

struct TerrainStats {
    int  tiles = 0;      // 전체 칸
    int  maze = 0;       // 미로가 판 개방 칸
    int  carved = 0;     // 밀도장이 추가로 깎은 칸
    int  rock = 0;       // 남은 바위
    int  veins = 0;      // 광맥 칸
    int  route = 0;      // 스폰→코어 경로 길이
    bool valid = false;  // flood fill 검증 결과
};

// 밀도장. FBM(Simplex) 이 방·회랑의 큰 덩어리를 만들고, Worley 가 그 안에 결을
// 넣는다. 반환은 대략 [-1.1, 1.1] 이고, 임계를 낮출수록 더 많이 깎인다.
// snippet-begin: density-field
inline float Density(const math::Noise& noise, int x, int y, const TerrainParams& p) {
    const float fx = static_cast<float>(x) * p.frequency;
    const float fy = static_cast<float>(y) * p.frequency;
    const float shape = math::FBM(
        [&noise](float a, float b) { return noise.Simplex2(a, b); }, fx, fy, p.octaves);
    const float grain = noise.Worley2(fx * 0.5f, fy * 0.5f);   // ≥ 0
    return shape * 0.75f + (grain - 0.5f) * 0.5f;
}
// snippet-end: density-field

// 절차 맵 하나. 실패는 검증 실패뿐이고, 그때는 out 을 건드리지 않는다.
// snippet-begin: generate-map
inline bool Generate(const TerrainParams& p, DefenseMap& out, TerrainStats* statsOut = nullptr) {
    tools::Level level = tools::MazeGen::Generate(p.mazeCellsX, p.mazeCellsY, p.seed);
    const int width = level.Width(), height = level.Height();

    TerrainStats stats;
    stats.tiles = width * height;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (level.At(x, y) != '#') ++stats.maze;

    // 밀도장으로 벽을 깎는다. 테두리는 건드리지 않는다 — 아레나의 틀이다.
    const math::Noise noise(p.seed * 2654435761u + 1u);
    std::vector<uint8_t> shade(static_cast<size_t>(width * height), 128);
    std::vector<uint8_t> vein(static_cast<size_t>(width * height), 0);
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            const float d = Density(noise, x, y, p);
            const float unit = math::Saturate(d * 0.5f + 0.5f);
            shade[static_cast<size_t>(y * width + x)] =
                static_cast<uint8_t>(64.0f + unit * 160.0f);
            if (level.At(x, y) == '#' && d > p.carve) {
                level.Set(x, y, '.');
                ++stats.carved;
            }
        }
    }

    // 검증은 감산이 끝난 뒤 한 번. 여기서 거짓이면 맵을 채택하지 않는다.
    const tools::Level::Report report = level.Validate();
    stats.valid = report.Valid();
    if (!stats.valid) {
        if (statsOut) *statsOut = stats;
        return false;
    }

    // 광맥은 개방 칸 위에만 심는다 — 바위 속 광맥은 세울 수 없어 의미가 없다.
    for (int y = 1; y < height - 1; ++y)
        for (int x = 1; x < width - 1; ++x) {
            if (level.At(x, y) != '.') continue;
            const float grain = noise.Worley2(static_cast<float>(x) * p.frequency * 1.7f,
                                              static_cast<float>(y) * p.frequency * 1.7f);
            if (grain < p.vein) {
                vein[static_cast<size_t>(y * width + x)] = 1;
                ++stats.veins;
            }
        }

    std::vector<TileType> tiles(static_cast<size_t>(width * height), TileType::Blocked);
    DefenseMap::Cell spawn{-1, -1}, core{-1, -1};
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const size_t i = static_cast<size_t>(y * width + x);
            switch (level.At(x, y)) {
                case '.': tiles[i] = TileType::Buildable; break;
                case 'S': tiles[i] = TileType::Spawn; spawn = {x, y}; break;
                case 'G': tiles[i] = TileType::Core;  core  = {x, y}; break;
                default:  tiles[i] = TileType::Blocked; ++stats.rock; break;
            }
        }
    if (spawn.x < 0 || core.x < 0) return false;

    DefenseMap map(width, height, p.cellSize, p.originY);
    map.AdoptGenerated(std::move(tiles), std::move(vein), std::move(shade), spawn, core);
    if (!map.RecomputeRoute()) return false;
    stats.route = static_cast<int>(map.RouteLength());

    out = std::move(map);
    if (statsOut) *statsOut = stats;
    return true;
}
// snippet-end: generate-map

}  // namespace mapgen
}  // namespace game
}  // namespace mega
