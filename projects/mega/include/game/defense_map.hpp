// defense_map — 격자 맵. 타일 · 점유 · 경로 · 연결성.
//
// game/game.hpp 안에 있던 DefenseMap 을 여기로 옮겼다. 옮긴 이유는 순환이다:
// 절차 생성(game/map_gen.hpp) · 정적 기하 색인(game/terrain.hpp) · 잔해
// (game/debris.hpp)가 전부 맵을 알아야 하는데, 그 셋을 game.hpp 가 다시
// 인클루드한다. 맵이 game.hpp 안에 남아 있으면 그 셋 중 어느 것도 맵을 볼 수 없다.
//
// 맵에는 두 종류가 있고 **경로 모델이 다르다**.
//
//   Classic    assets/level.txt 가 저작한 무분기 회랑. 경로는 파일이 정하고
//              매치 내내 바뀌지 않는다. 타워는 경로를 막지 않는다(경로 타일은
//              건설 불가다). 이것이 스위치 이전의 동작이고 기본값이다.
//   Generated  미로 골격 + 밀도장이 만든 개방 지형. 통행 가능한 칸이면 어디로든
//             갈 수 있으므로 **경로는 계산 결과**다(스폰→코어 BFS). 타워와
//             안착 잔해가 칸을 점유해 그 계산의 입력을 바꾼다 — 메이즈 디펜스다.
//
// 두 모델이 공유하는 것은 `pathCells_` 라는 출력 하나다. 규칙(game/game.hpp)은
// 그 배열만 읽으므로 맵 종류를 몰라도 된다.

// ── 배치 판정을 칸 질의에서 격자 전수로 ──────────────────────────
// `WouldRemainConnected` 는 오랫동안 칸 하나짜리 질문이었다. 후보 격자를 통째로
// 짓고(41x41 char 할당 + 복사) flood fill 을 한 번 돈다. 답은 옳지만 **한 칸에
// 대해서만** 옳다.
//
// 그래서 화면이 말할 수 있는 것도 한 칸뿐이었다 — 커서 아래가 되는지 안 되는지.
// low-link DFS 는 격자 하나에 한 번 돌아 **모든 칸**을 미리 표시하므로, 화면이
// "막으면 안 되는 칸"을 격자 전체에 칠할 수 있다. 그것이 이 팔이 들여온 기능이고,
// 질의가 O(1) 이 되는 것은 그 부산물이다.
//
// 두 팔의 판정은 **같아야 한다.** 다르면 최적화가 아니라 규칙 변경이다.

#pragma once

#include "algo/articulation.hpp"
#include "game/allocators.hpp"
#include "math/vec.hpp"
#include "tools/level_editor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace mega {
namespace game {

enum class TileType : uint8_t { Buildable, Path, Spawn, Core, Blocked };

// 게임 규칙이 소유하는 격자 맵. 렌더러는 이 데이터를 그릴 뿐이며, 배치 명령은
// 항상 이 맵으로 다시 검증된다. 기본 맵은 한 셀 폭의 굴곡 경로를 갖는다.
//
// 타일 배열과 경로 배열은 매치 수명이고 크기가 맵에 따라 달라진다 — 크기 클래스
// 할당자(game/allocators.hpp 의 BlobArray)가 담당하는 정확히 그 부류다.
class DefenseMap {
public:
    struct Cell { int x = 0, y = 0; };

    DefenseMap(int columns = 16, int rows = 15, int cellSize = 30, int originY = 30)
        : columns_(columns), rows_(rows), cellSize_(cellSize), originY_(originY),
          tiles_(static_cast<size_t>(columns * rows), TileType::Buildable) {}

    static DefenseMap CreateDefault() {
        DefenseMap map;
        std::vector<Cell> cells;
        auto append = [&map, &cells](int x, int y) {
            if (!cells.empty()) {
                const Cell& last = cells.back();
                if (last.x == x && last.y == y) return;
            }
            cells.push_back({x, y});
            map.SetTile(x, y, TileType::Path);
        };
        auto segment = [&append](int x0, int y0, int x1, int y1) {
            int dx = (x1 > x0) - (x1 < x0);
            int dy = (y1 > y0) - (y1 < y0);
            for (;;) {
                append(x0, y0);
                if (x0 == x1 && y0 == y1) break;
                x0 += dx;
                y0 += dy;
            }
        };

        segment(7, 0, 7, 3);
        segment(7, 3, 12, 3);
        segment(12, 3, 12, 7);
        segment(12, 7, 4, 7);
        segment(4, 7, 4, 11);
        segment(4, 11, 8, 11);
        segment(8, 11, 8, 14);
        map.SetTile(7, 0, TileType::Spawn);
        map.SetTile(8, 14, TileType::Core);
        map.spawn_ = {7, 0};
        map.core_  = {8, 14};
        map.pathCells_.AssignFrom(cells.data(), cells.size());

        // 가장자리 일부는 장식/지형용 비배치 셀이다.
        for (Cell c : std::array<Cell, 8>{{{0, 0}, {1, 0}, {14, 0}, {15, 0},
                                           {0, 14}, {1, 14}, {14, 14}, {15, 14}}})
            map.SetTile(c.x, c.y, TileType::Blocked);
        return map;
    }

    // '.', '#', 'P', 'S', 'C' rows -> validated map. The path must be one
    // continuous, non-branching chain from S to C and consume every path tile.
    static bool FromAscii(const std::vector<std::string>& rows, int cellSize,
                          int originY, DefenseMap& out) {
        if (rows.empty() || rows.size() > 64 || rows.front().empty() || rows.front().size() > 64)
            return false;
        size_t width = rows.front().size();
        for (const std::string& row : rows) if (row.size() != width) return false;

        DefenseMap map(static_cast<int>(width), static_cast<int>(rows.size()), cellSize, originY);
        Cell spawn{-1, -1}, core{-1, -1};
        int pathTileCount = 0;
        for (int y = 0; y < map.rows_; ++y) {
            for (int x = 0; x < map.columns_; ++x) {
                char tile = rows[static_cast<size_t>(y)][static_cast<size_t>(x)];
                switch (tile) {
                    case '.': map.SetTile(x, y, TileType::Buildable); break;
                    case '#': map.SetTile(x, y, TileType::Blocked); break;
                    case 'P': map.SetTile(x, y, TileType::Path); ++pathTileCount; break;
                    case 'S':
                        if (spawn.x >= 0) return false;
                        spawn = {x, y}; map.SetTile(x, y, TileType::Spawn); ++pathTileCount; break;
                    case 'C':
                        if (core.x >= 0) return false;
                        core = {x, y}; map.SetTile(x, y, TileType::Core); ++pathTileCount; break;
                    default: return false;
                }
            }
        }
        if (spawn.x < 0 || core.x < 0 || pathTileCount < 2) return false;

        std::vector<uint8_t> visited(static_cast<size_t>(map.columns_ * map.rows_), 0);
        std::vector<Cell> cells;
        Cell current = spawn;
        cells.push_back(current);
        visited[static_cast<size_t>(current.y * map.columns_ + current.x)] = 1;
        const int directions[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        while (!(current.x == core.x && current.y == core.y)) {
            std::array<Cell, 4> candidates{};
            int candidateCount = 0;
            for (const auto& direction : directions) {
                int nx = current.x + direction[0];
                int ny = current.y + direction[1];
                if (!map.InBounds(nx, ny)) continue;
                TileType tile = map.Tile(nx, ny);
                bool isPath = tile == TileType::Path || tile == TileType::Spawn || tile == TileType::Core;
                size_t index = static_cast<size_t>(ny * map.columns_ + nx);
                if (isPath && !visited[index]) candidates[static_cast<size_t>(candidateCount++)] = {nx, ny};
            }
            if (candidateCount != 1) return false;
            current = candidates[0];
            visited[static_cast<size_t>(current.y * map.columns_ + current.x)] = 1;
            cells.push_back(current);
        }
        if (static_cast<int>(cells.size()) != pathTileCount) return false;
        map.spawn_ = spawn;
        map.core_  = core;
        map.pathCells_.AssignFrom(cells.data(), cells.size());
        out = std::move(map);
        return true;
    }

    int Columns() const { return columns_; }
    int Rows() const { return rows_; }
    int CellSize() const { return cellSize_; }
    int OriginY() const { return originY_; }
    int PixelWidth() const { return columns_ * cellSize_; }
    int PixelHeight() const { return originY_ + rows_ * cellSize_; }
    MapSource Source() const { return source_; }
    bool Generated() const { return source_ == MapSource::Procgen; }
    Cell SpawnCell() const { return spawn_; }
    Cell CoreCell() const { return core_; }

    bool InBounds(int x, int y) const {
        return x >= 0 && y >= 0 && x < columns_ && y < rows_;
    }
    TileType Tile(int x, int y) const {
        return InBounds(x, y) ? tiles_[static_cast<size_t>(y * columns_ + x)] : TileType::Blocked;
    }
    Cell WorldToCell(const math::Vec2& p) const {
        return {static_cast<int>(std::floor(p.x / static_cast<float>(cellSize_))),
                static_cast<int>(std::floor((p.y - originY_) / static_cast<float>(cellSize_)))};
    }
    math::Vec2 CellCenter(int x, int y) const {
        return {x * static_cast<float>(cellSize_) + cellSize_ * 0.5f,
                originY_ + y * static_cast<float>(cellSize_) + cellSize_ * 0.5f};
    }
    math::Vec2 Snap(const math::Vec2& p) const {
        Cell c = WorldToCell(p);
        return CellCenter(c.x, c.y);
    }
    bool IsBuildable(const math::Vec2& p) const {
        Cell c = WorldToCell(p);
        return InBounds(c.x, c.y) && Tile(c.x, c.y) == TileType::Buildable && !Occupied(c.x, c.y);
    }
    std::vector<math::Vec2> Path() const {
        std::vector<math::Vec2> out;
        out.reserve(pathCells_.Size());
        for (Cell c : pathCells_) out.push_back(CellCenter(c.x, c.y));
        return out;
    }
    size_t RouteLength() const { return pathCells_.Size(); }
    Cell RouteCell(size_t i) const { return pathCells_[i]; }

    // ── 절차 지형이 심는 두 장(場) ─────────────────────────────────
    // 광맥은 그 칸에 세운 타워의 데미지를 올리고, 명암은 타일 색을 흔든다.
    // 둘 다 같은 밀도장에서 나오므로 시드 하나가 지형·보상·외관을 함께 정한다.
    bool  Vein(int x, int y) const {
        return InBounds(x, y) && !vein_.Empty() && vein_[static_cast<size_t>(y * columns_ + x)] != 0;
    }
    uint8_t Shade(int x, int y) const {
        return (InBounds(x, y) && !shade_.Empty()) ? shade_[static_cast<size_t>(y * columns_ + x)] : 128;
    }

    // ── 점유와 통행 ────────────────────────────────────────────────
    // 점유는 타워와 안착 잔해가 만든다. Classic 맵에서는 통행 판정에 들어가지
    // 않는다 — 경로가 파일이 정한 고정 회랑이기 때문이다.
    bool Occupied(int x, int y) const {
        if (occupied_.Empty() || !InBounds(x, y)) return false;
        return occupied_[static_cast<size_t>(y * columns_ + x)] != 0;
    }
    // 매치를 다시 시작하면 타워도 잔해도 사라진다. 점유만 남아 있으면 유령 벽이
    // 경로 계산에 그대로 반영된다.
    void ClearOccupancy() {
        if (!occupied_.Empty()) occupied_.Assign(static_cast<size_t>(columns_ * rows_), uint8_t{0});
        ++gridVersion_;
    }
    void SetOccupied(int x, int y, bool value) {
        if (!InBounds(x, y)) return;
        if (occupied_.Empty()) occupied_.Assign(static_cast<size_t>(columns_ * rows_), uint8_t{0});
        occupied_[static_cast<size_t>(y * columns_ + x)] = value ? uint8_t{1} : uint8_t{0};
        ++gridVersion_;
    }
    bool Passable(int x, int y) const {
        if (!InBounds(x, y)) return false;
        if (Tile(x, y) == TileType::Blocked) return false;
        if ((x == spawn_.x && y == spawn_.y) || (x == core_.x && y == core_.y)) return true;
        return !Occupied(x, y);
    }
    bool Rock(int x, int y) const { return Tile(x, y) == TileType::Blocked; }

    // 발파가 부르는 유일한 변경 지점. 바위 칸을 개방한다. 반대 방향은 없다 —
    // 감산만 하는 연산이라 코어가 봉쇄되는 발파는 구조적으로 존재할 수 없다.
    // snippet-begin: open-cell
    bool OpenCell(int x, int y) {
        if (!InBounds(x, y) || Tile(x, y) != TileType::Blocked) return false;
        SetTile(x, y, TileType::Buildable);
        return true;
    }
    // snippet-end: open-cell

    // 후보 격자의 연결성 검사. `blockX/blockY` 를 추가로 막았다고 가정한다
    // (-1 이면 현재 격자 그대로). 검사기는 tools::Level 의 flood fill 이다 —
    // 같은 검사를 두 벌 갖지 않기 위해 맵을 그 표현으로 내려보낸다.
    // snippet-begin: to-level
    tools::Level ToLevel(int blockX = -1, int blockY = -1) const {
        tools::Level level;
        level.Init(columns_, rows_, '#');
        for (int y = 0; y < rows_; ++y)
            for (int x = 0; x < columns_; ++x)
                level.Set(x, y, Passable(x, y) ? '.' : '#');
        if (InBounds(blockX, blockY)) level.Set(blockX, blockY, '#');
        level.Set(spawn_.x, spawn_.y, 'S');
        level.Set(core_.x, core_.y, 'G');
        return level;
    }

    // 이 칸을 막아도 스폰→코어가 살아 있는가. 메이즈 디펜스의 배치 규칙이다.
    // 팔이 둘이고 **두 팔은 언제나 같은 답을 내야 한다**.
    bool WouldRemainConnected(int blockX, int blockY) const {
        if (buildCheck_ == BuildCheckKind::Articulation) return ArticConnected(blockX, blockY);
        return ToLevel(blockX, blockY).Validate().Valid();
    }
    // snippet-end: to-level

    // ── 전수 판정 ──────────────────────────────────────────────────
    void SetBuildCheck(BuildCheckKind kind) {
        if (buildCheck_ == kind) return;
        buildCheck_ = kind;
        chokeVersion_ = kNoVersion;
    }
    BuildCheckKind BuildCheck() const { return buildCheck_; }
    // 통행 격자가 바뀔 때마다 오른다. 판정 캐시의 무효화 기준이고, 항법 격자가
    // 이미 같은 기준을 쓰고 있다(NavGrid::Version).
    uint32_t GridVersion() const { return gridVersion_; }

    // snippet-begin: choke-mask
    // 막으면 스폰↔코어가 끊기는 칸에 1. artic 팔에서만 채워지고, floodfill 팔에서는
    // 빈 배열이다 — 그 팔에서 이 표를 채우려면 칸마다 flood fill 을 돌아야 하고
    // 그것이 바로 이 팔이 대신하는 일이기 때문이다.
    //
    // 이 표가 곧 화면 기능이다. 커서 아래 한 칸의 가부 대신 격자 전체를 칠할 수
    // 있게 되는 것이 low-link 를 들인 이유다.
    const std::vector<uint8_t>& ChokeMask() const {
        EnsureChoke();
        return chokeMask_;
    }
    bool Choke(int x, int y) const {
        if (buildCheck_ != BuildCheckKind::Articulation || !InBounds(x, y)) return false;
        EnsureChoke();
        return !chokeMask_.empty() && chokeMask_[static_cast<size_t>(y * columns_ + x)] != 0;
    }
    size_t ChokeCount() const {
        EnsureChoke();
        size_t total = 0;
        for (uint8_t flag : chokeMask_) total += flag ? 1u : 0u;
        return total;
    }
    // snippet-end: choke-mask

    // 스폰→코어 최단 경로를 다시 계산한다. Classic 맵은 저작된 회랑을 그대로
    // 두고 참을 반환한다. 도달 불가면 경로를 건드리지 않고 거짓이다 — 호출자가
    // 방금 한 변경을 되돌릴 수 있어야 하기 때문이다.
    // snippet-begin: recompute-route
    bool RecomputeRoute() {
        if (source_ != MapSource::Procgen) return true;
        const size_t count = static_cast<size_t>(columns_ * rows_);
        std::vector<int32_t> previous(count, kUnvisited);
        std::vector<int32_t> queue;
        queue.reserve(count);
        const int32_t start = spawn_.y * columns_ + spawn_.x;
        const int32_t goal  = core_.y * columns_ + core_.x;
        previous[static_cast<size_t>(start)] = -1;
        queue.push_back(start);
        static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (size_t head = 0; head < queue.size(); ++head) {
            const int32_t current = queue[head];
            if (current == goal) break;
            const int cx = current % columns_, cy = current / columns_;
            for (int d = 0; d < 4; ++d) {
                const int nx = cx + dx[d], ny = cy + dy[d];
                if (!Passable(nx, ny)) continue;
                const size_t next = static_cast<size_t>(ny * columns_ + nx);
                if (previous[next] != kUnvisited) continue;
                previous[next] = current;
                queue.push_back(static_cast<int32_t>(next));
            }
        }
        if (previous[static_cast<size_t>(goal)] == kUnvisited) return false;
        std::vector<Cell> cells;
        for (int32_t at = goal; at != -1; at = previous[static_cast<size_t>(at)])
            cells.push_back({at % columns_, at / columns_});
        std::reverse(cells.begin(), cells.end());
        pathCells_.AssignFrom(cells.data(), cells.size());
        return true;
    }
    // snippet-end: recompute-route

    // 절차 생성기가 완성된 격자를 밀어 넣는 입구. 생성기는 game/map_gen.hpp 가
    // 소유하고, 맵은 그 결과를 받아 들기만 한다.
    void AdoptGenerated(std::vector<TileType>&& tiles, std::vector<uint8_t>&& vein,
                        std::vector<uint8_t>&& shade, Cell spawn, Cell core) {
        tiles_.AssignFrom(tiles.data(), tiles.size());
        vein_.AssignFrom(vein.data(), vein.size());
        shade_.AssignFrom(shade.data(), shade.size());
        occupied_.Assign(tiles.size(), uint8_t{0});
        spawn_ = spawn;
        core_  = core;
        source_ = MapSource::Procgen;
        ++gridVersion_;
    }

    void SetTile(int x, int y, TileType type) {
        if (InBounds(x, y)) tiles_[static_cast<size_t>(y * columns_ + x)] = type;
        ++gridVersion_;
    }

private:
    static constexpr int32_t  kUnvisited = -2;
    static constexpr uint32_t kNoVersion = 0xFFFFFFFFu;

    // snippet-begin: artic-connected
    // artic 팔의 판정. flood fill 팔과 **같은 답**을 내야 하므로 그 팔이 암묵적으로
    // 갖고 있던 네 규칙을 여기서 명시한다.
    //
    //   격자 밖을 막는다      후보 격자가 그대로다 → 지금 통해 있는가가 곧 답이다.
    //   스폰·코어를 막는다    ToLevel 이 그 두 칸을 'S'/'G' 로 덮어써 막기가 무효다.
    //   이미 못 가는 칸       막아도 아무것도 바뀌지 않는다.
    //   그 밖의 통행 칸       지금 통해 있고 그 칸이 분리 정점이 아니면 참.
    bool ArticConnected(int blockX, int blockY) const {
        EnsureChoke();
        if (!articConnected_) return false;
        if (!InBounds(blockX, blockY)) return true;
        if ((blockX == spawn_.x && blockY == spawn_.y) ||
            (blockX == core_.x && blockY == core_.y)) return true;
        if (!Passable(blockX, blockY)) return true;
        return chokeMask_[static_cast<size_t>(blockY * columns_ + blockX)] == 0;
    }

    void EnsureChoke() const {
        if (buildCheck_ != BuildCheckKind::Articulation) {
            chokeMask_.clear();
            articConnected_ = false;
            chokeVersion_ = kNoVersion;
            return;
        }
        if (chokeVersion_ == gridVersion_) return;
        const size_t count = static_cast<size_t>(columns_) * static_cast<size_t>(rows_);
        passableScratch_.assign(count, 0);
        for (int y = 0; y < rows_; ++y)
            for (int x = 0; x < columns_; ++x)
                passableScratch_[static_cast<size_t>(y * columns_ + x)] =
                    Passable(x, y) ? uint8_t{1} : uint8_t{0};
        articConnected_ = separators_.Build(passableScratch_.data(), columns_, rows_,
                                            spawn_.x, spawn_.y, core_.x, core_.y);
        chokeMask_.assign(count, 0);
        if (articConnected_) {
            for (int y = 0; y < rows_; ++y)
                for (int x = 0; x < columns_; ++x)
                    if (separators_.Separates(x, y))
                        chokeMask_[static_cast<size_t>(y * columns_ + x)] = 1;
        }
        chokeVersion_ = gridVersion_;
    }
    // snippet-end: artic-connected

    int columns_ = 0, rows_ = 0, cellSize_ = 0, originY_ = 0;
    MapSource             source_ = MapSource::Classic;
    Cell                  spawn_{0, 0}, core_{0, 0};
    BlobArray<TileType>   tiles_;
    BlobArray<Cell>       pathCells_;
    BlobArray<uint8_t>    occupied_;
    BlobArray<uint8_t>    vein_;
    BlobArray<uint8_t>    shade_;

    BuildCheckKind        buildCheck_ = BuildCheckKind::FloodFill;
    uint32_t              gridVersion_ = 1;
    // 캐시. 판정은 논리적으로 const 이므로 저장만 mutable 이다 — 호출부가
    // `const DefenseMap&` 를 들고 묻는다는 사실이 이 팔 때문에 바뀌면 안 된다.
    mutable uint32_t              chokeVersion_ = kNoVersion;
    mutable bool                  articConnected_ = false;
    mutable std::vector<uint8_t>  chokeMask_;
    mutable std::vector<uint8_t>  passableScratch_;
    mutable algo::GridSeparators  separators_;
};

}  // namespace game
}  // namespace mega
