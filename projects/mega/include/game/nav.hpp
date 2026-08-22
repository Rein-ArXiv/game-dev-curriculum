// nav — 통행 격자와 그 위의 두 경로 팔. 규칙이 "어디로 갈 수 있는가"를 묻는 곳.
//
// 13편의 절차 맵이 들어오기 전까지 이 질문은 존재하지 않았다. 경로는
// `assets/level.txt` 가 저작한 한 줄짜리 회랑이었고, 적은 그 폴리라인의 점을
// 차례로 밟았다. 절차 맵이 개방 지형을 만들면서 `DefenseMap::RecomputeRoute` 가
// 스폰→코어 BFS 로 폴리라인을 **계산**하게 됐지만, 계산된 것도 여전히 경로 하나다.
//
// 경로 하나로는 답할 수 없는 질문이 넷 있다.
//
//   1. 적마다 다른 곳에 있다. 경로 배열의 어느 점부터 걷는지를 매번 최근접
//      탐색으로 다시 찾아야 한다(game.hpp 의 RefreshRoute 가 하는 일이다).
//   2. 타워가 칸을 막으면 그 하나짜리 경로가 통째로 무효가 된다.
//   3. 시야 판정이 "벽이 가로막는가"를 물으려면 통행 가능성을 셀 단위로 읽어야
//      하는데, 폴리라인에는 셀이 없다.
//   4. 조향이 붙으면 위치가 경로의 함수가 아니게 된다 — 폴리라인은 위치를 정하는
//      장치라 조향이 끼어들 자리를 주지 않는다.
//
// 그래서 통행 가능성이라는 **한 가지 질문**을 한 자료구조가 답하게 한다. 그것이
// `ai/pathfinding/grid_map.hpp` 의 GridMap 이고, NavGrid 는 DefenseMap 의 타일 ·
// 타워 점유 · 안착 잔해를 그 표현으로 접는 어댑터다. A* · 플로우 필드 · 시야
// 레이캐스트가 전부 이 하나를 읽는다.
//
// 두 맵 종류에서 통행 가능성의 **정의가 다르다**. 그 차이가 이 파일의 핵심이다.
//
//   Classic    저작된 회랑만 통행 가능하다(Path·Spawn·Core 타일). 개방 칸을
//              통행 가능으로 두면 적이 회랑을 무시하고 코어로 직진해 버려,
//              같은 맵이 격자 팔에서 다른 게임이 된다.
//   Generated  DefenseMap::Passable 그대로. 바위가 아니고 점유되지 않은 칸이면
//              어디로든 간다 — 메이즈 디펜스의 규칙 그 자체다.
//
// 이 정의 덕분에 Classic 맵에서 격자 팔의 셀 열은 폴리라인과 **같은 열**이 되고,
// 세 팔(poly · astar · flowfield)의 상태 해시가 비트 동일해진다. 그것이
// "두 팔이 같은 답을 낸다"를 제품 안에서 묻는 유일한 방법이다.

#pragma once

#include "ai/pathfinding/astar.hpp"
#include "ai/pathfinding/flow_field.hpp"
#include "ai/pathfinding/grid_map.hpp"
#include "game/defense_map.hpp"
#include "game/switches.hpp"
#include "math/vec.hpp"

#include <cstdint>
#include <vector>

namespace mega {
namespace game {

// 격자 좌표에서 본 셀 하나. DefenseMap::Cell 과 같은 모양이지만 의미가 다르다 —
// 저쪽은 타일 좌표이고 이쪽은 통행 격자 좌표다. 지금은 1:1 이다.
struct NavCell {
    int x = 0, y = 0;
    bool operator==(const NavCell& o) const { return x == o.x && y == o.y; }
};

// ── 통행 격자 ──────────────────────────────────────────────────────
// 격자를 통째로 다시 짓는 것과 셀 하나만 고치는 것을 둘 다 갖는다. 매치 중
// 지형은 초당 여러 번 바뀌는데(타워 배치·판매·발파·잔해 안착) 그 변경은 언제나
// **칸 하나**다. 전체 재생성은 매치 시작과 발파처럼 여러 칸이 한꺼번에 바뀔 때만 쓴다.
class NavGrid {
public:
    void Rebuild(const DefenseMap& map) {
        grid_.Init(map.Columns(), map.Rows(), true);
        sight_.Init(map.Columns(), map.Rows(), true);
        generated_ = map.Generated();
        for (int y = 0; y < map.Rows(); ++y)
            for (int x = 0; x < map.Columns(); ++x) {
                grid_.SetWalkable(x, y, WalkableIn(map, x, y));
                sight_.SetWalkable(x, y, TransparentIn(map, x, y));
            }
        spawn_ = {map.SpawnCell().x, map.SpawnCell().y};
        core_  = {map.CoreCell().x, map.CoreCell().y};
        ++version_;
    }

    // 칸 하나의 통행 가능성만 맵과 다시 맞춘다. 통행 가능성이 실제로 뒤집힌
    // 경우에만 참을 돌려준다 — 호출자가 "필드를 다시 지어야 하는가"를 그것으로
    // 판단한다. 바뀌지 않은 갱신에 재빌드를 걸면 잔해가 이미 막힌 칸에 또
    // 안착할 때마다 필드를 헛되이 다시 짓는다.
    // snippet-begin: nav-sync-cell
    bool SyncCell(const DefenseMap& map, int x, int y) {
        if (!grid_.InBounds(x, y)) return false;
        sight_.SetWalkable(x, y, TransparentIn(map, x, y));
        const bool want = WalkableIn(map, x, y);
        if (grid_.Walkable(x, y) == want) return false;
        grid_.SetWalkable(x, y, want);
        ++version_;
        return true;
    }
    // snippet-end: nav-sync-cell

    const ai::path::GridMap& Grid() const { return grid_; }
    // 시야 격자. 통행 격자와 **다른 질문에 답한다** — 타워는 걸어 지나갈 수
    // 없지만 레이더의 시야까지 막지는 않는다. 하나로 합쳐 두면 레이더가 자기
    // 자신에게 가려 아무것도 보지 못한다(실제로 그 상태를 만들었고, 은신
    // 개체가 한 틱도 드러나지 않았다). 시야를 막는 것은 바위뿐이다.
    const ai::path::GridMap& SightGrid() const { return sight_; }
    bool     Walkable(int x, int y) const { return grid_.Walkable(x, y); }
    int      Width() const { return grid_.Width(); }
    int      Height() const { return grid_.Height(); }
    int      Index(int x, int y) const { return grid_.Index(x, y); }
    NavCell  Spawn() const { return spawn_; }
    NavCell  Core() const { return core_; }
    // 격자가 바뀔 때마다 오른다. 경로 캐시의 무효화 기준이다.
    uint32_t Version() const { return version_; }

    // 통행 가능성의 정의. 두 맵 종류가 갈리는 유일한 지점이다.
    // snippet-begin: nav-walkable
    static bool WalkableIn(const DefenseMap& map, int x, int y) {
        if (!map.InBounds(x, y)) return false;
        if (map.Generated()) return map.Passable(x, y);
        const TileType tile = map.Tile(x, y);
        return tile == TileType::Path || tile == TileType::Spawn || tile == TileType::Core;
    }
    // 시야가 지나가는가. 바위만 막는다 — 타워도 잔해도 통행은 막지만 레이더
    // 접시의 시선은 막지 않는다.
    static bool TransparentIn(const DefenseMap& map, int x, int y) {
        return map.InBounds(x, y) && !map.Rock(x, y);
    }
    // snippet-end: nav-walkable

private:
    ai::path::GridMap grid_;
    ai::path::GridMap sight_;
    NavCell           spawn_, core_;
    uint32_t          version_ = 0;
    bool              generated_ = false;
};

// ── 두 경로 팔 ─────────────────────────────────────────────────────
// 목표는 코어 하나로 공유되므로 필드 한 장이 모든 개체를 덮는다. 그럼에도 A* 를
// 남기는 이유는 둘이다 — 개체마다 다른 목표를 갖게 되는 날의 자리이기도 하고,
// 지금은 **필드의 오라클**이다. 같은 격자에서 두 방식이 같은 비용의 경로를 내야
// 한다는 것이 이 그룹의 판정이다.
class NavField {
public:
    void Configure(PathingKind kind, size_t capacity) {
        kind_ = kind;
        paths_.assign(capacity ? capacity : 1, std::vector<int>{});
        fieldVersion_ = 0;
        builds_ = 0;
        searches_ = 0;
    }

    PathingKind Kind() const { return kind_; }
    // 필드를 통째로 다시 지은 횟수 / 개체 경로를 찾은 횟수. 두 팔의 비용이
    // 갈리는 지점을 재는 수다.
    size_t Builds() const { return builds_; }
    size_t Searches() const { return searches_; }
    const ai::path::FlowField& Field() const { return field_; }

    // 격자가 바뀌었으면 공유 필드를 한 번 다시 짓는다. A* 팔은 여기서 아무것도
    // 하지 않는다 — 그 팔의 비용은 개체마다 흩어져 발생한다.
    // snippet-begin: nav-field-sync
    void Sync(const NavGrid& nav) {
        if (kind_ != PathingKind::FlowField) return;
        if (fieldVersion_ == nav.Version() && builds_ > 0) return;
        field_.Build(nav.Grid(), nav.Core().x, nav.Core().y);
        fieldVersion_ = nav.Version();
        ++builds_;
    }
    // snippet-end: nav-field-sync

    // 개체 하나의 다음 칸. 두 팔이 갈리는 유일한 함수다.
    //
    //   flowfield  현재 칸의 벡터를 읽어 이웃 칸을 고른다. 지형이 바뀌어도
    //              개체는 아무것도 다시 계산하지 않는다 — 필드가 이미 새것이다.
    //   astar      캐시된 셀 열의 커서를 하나 민다. 격자 버전이 바뀌었으면
    //              그 자리에서 다시 찾는다.
    //
    // `from` 은 개체의 **현재 목표 칸**이지 개체가 서 있는 칸이 아니다. 그래야
    // 스폰 지터로 흔들린 위치가 첫 칸 선택을 바꾸지 못하고, 두 팔이 Classic
    // 회랑에서 같은 열을 낸다.
    // snippet-begin: nav-next-cell
    bool NextCell(const NavGrid& nav, size_t row, const NavCell& from, NavCell& out) {
        if (from == nav.Core()) return false;                 // 코어에 닿았다
        if (kind_ == PathingKind::FlowField) {
            const math::Vec2 d = field_.Direction(from.x, from.y);
            if (d.x == 0.0f && d.y == 0.0f) return false;     // 도달 불가
            out = {from.x + StepOf(d.x), from.y + StepOf(d.y)};
            return nav.Walkable(out.x, out.y);
        }
        if (row >= paths_.size()) return false;
        std::vector<int>& path = paths_[row];
        if (path.empty() || path.front() != nav.Index(from.x, from.y)) {
            ++searches_;
            if (!ai::path::AStar::FindPath(nav.Grid(), from.x, from.y,
                                           nav.Core().x, nav.Core().y, path))
                return false;
        }
        if (path.size() < 2) return false;
        const int next = path[1];
        path.erase(path.begin());                             // 커서를 하나 민다
        out = {next % nav.Width(), next / nav.Width()};
        return nav.Walkable(out.x, out.y);
    }
    // snippet-end: nav-next-cell

    // 이 칸에서 코어로 가는 길이 남아 있는가. 상태 기계의 Blocked 판정이
    // 묻는 유일한 질문이고, **다음 칸을 소비하지 않는다**. A* 팔에서는 이
    // 질문이 곧 탐색이라 결과를 캐시에 남긴다 — 같은 틱의 NextCell 이 그것을
    // 그대로 쓰므로 한 틱에 두 번 찾는 일은 없다.
    // snippet-begin: nav-has-route
    bool HasRoute(const NavGrid& nav, size_t row, const NavCell& from) {
        if (from == nav.Core()) return true;
        if (!nav.Walkable(from.x, from.y)) return false;
        if (kind_ == PathingKind::FlowField) return field_.Reachable(from.x, from.y);
        if (row >= paths_.size()) return false;
        std::vector<int>& path = paths_[row];
        if (!path.empty() && path.front() == nav.Index(from.x, from.y)) return true;
        ++searches_;
        return ai::path::AStar::FindPath(nav.Grid(), from.x, from.y,
                                         nav.Core().x, nav.Core().y, path);
    }
    // snippet-end: nav-has-route

    // 격자가 바뀌면 개체 경로는 전부 버린다. 필드 팔이 한 번의 재빌드로 끝내는
    // 일을 A* 팔은 개체 수만큼 나눠 치른다는 것이 이 두 줄의 뜻이다.
    void InvalidatePaths() {
        for (std::vector<int>& path : paths_) path.clear();
    }
    void ClearPath(size_t row) { if (row < paths_.size()) paths_[row].clear(); }
    size_t CachedPathCells(size_t row) const {
        return row < paths_.size() ? paths_[row].size() : 0;
    }

private:
    static int StepOf(float v) { return v > 0.3f ? 1 : (v < -0.3f ? -1 : 0); }

    ai::path::FlowField           field_;
    std::vector<std::vector<int>> paths_;
    PathingKind kind_ = PathingKind::Polyline;
    uint32_t    fieldVersion_ = 0;
    size_t      builds_ = 0;
    size_t      searches_ = 0;
};

// 월드 좌표를 격자 **실수** 좌표로. 시야 레이캐스트가 셀 사이를 걷기 때문에
// 정수 셀 좌표로는 표현할 수 없다.
inline math::Vec2 NavSpace(const DefenseMap& map, const math::Vec2& world) {
    const float cell = static_cast<float>(map.CellSize());
    return {world.x / cell, (world.y - static_cast<float>(map.OriginY())) / cell};
}

}  // namespace game
}  // namespace mega
