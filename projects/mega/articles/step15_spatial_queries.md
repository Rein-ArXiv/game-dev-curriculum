# 15. 같은 질의를 세 구현으로 갈라 잰다

## 들어가며

이 게임의 조준은 12편까지 이렇게 돌았다.

```cpp
for (size_t i = 0; i < count; ++i) {
    if (!enemies.Alive(i)) continue;
    float d2 = math::LengthSq(enemies.Pos(i) - towerPos);
    if (d2 > r2) continue;
    // ... 점수를 매기고 최선을 고른다
}
```

타워마다 적 전부를 훑는다. 타워 T, 적 E 면 틱당 T×E 다. 적 64·타워 16 이면 1024번 — 아무 문제가 없다.

그 문제가 없다는 사실이 이 편의 출발점이다. **"공간 색인이 전수 스캔보다 빠르다"는 문장은 규모 없이는 참도 거짓도 아니다.** 그래서 지우는 대신 나란히 두고, 규모를 올려 교차점을 찾는다.

세 팔을 세운다.

| `target_index` | 구현 | 헤더 |
|---|---|---|
| `linear` | 전수 스캔(기본값) | 없음 |
| `grid` | 균일 격자 공간 해시 | `math/spatial/uniform_grid.hpp` |
| `quadtree` | 적응 사분 분할 | `math/spatial/quadtree.hpp` |

그리고 **다른 종류의 질의**가 하나 더 생겼다. 13편의 바위 564칸은 정적이고, 두 가지를 묻는다 — 발파 브러시가 어느 칸을 덮는가(영역), 타워와 적 사이에 바위가 있는가(선분). 그 위에 `math/spatial/bvh.hpp` 를 세우고 `static_query=naive|bvh` 로 고른다.

도달점의 수치를 먼저 적는다.

- **조준 색인.** 적 64에서는 세 팔이 측정 잡음 안에 있다. 적 2048·타워 16·600틱에서 **linear 23.7ms · grid 14.5ms · quadtree 65.9ms** 다. 격자가 이기고 쿼드트리가 2.8배 진다.
- **정적 질의.** 영역 질의 20000회에 **naive 23.8ms · bvh 3.4ms**(7.0배), 선분 질의 20000회에 **naive 16.9ms · bvh 5.5ms**(3.1배).
- **판정 순서.** 세 팔이 매 틱 같은 타워에 같은 대상 행 번호를 고르고, 3000틱 상태 해시가 전부 같다. 그 뒤에야 위 수치가 의미를 갖는다.

---

## 목차
1. [(a) 없는 (b)는 거짓말이다](#1-a-없는-b는-거짓말이다)
2. [점수 함수를 한 벌로](#2-점수-함수를-한-벌로)
3. [동점의 승자를 지키는 정렬](#3-동점의-승자를-지키는-정렬)
4. [색인은 틱마다 통째로 다시 짓는다](#4-색인은-틱마다-통째로-다시-짓는다)
5. [사격선 — 다른 종류의 질의](#5-사격선--다른-종류의-질의)
6. [2D 게임에 3D BVH 를 쓴 이유](#6-2d-게임에-3d-bvh-를-쓴-이유)
7. [무효화 규칙은 검사로만 살아 있다](#7-무효화-규칙은-검사로만-살아-있다)
8. [측정: 교차점은 어디인가](#8-측정-교차점은-어디인가)
9. [BVH 가 이기는 폭이 질의 종류마다 다르다](#9-bvh-가-이기는-폭이-질의-종류마다-다르다)
10. [변이 셋](#10-변이-셋)
11. [실행과 관측](#11-실행과-관측)

---

## 1. (a) 없는 (b)는 거짓말이다

A/B 스위치의 규약 5번은 이렇게 적혀 있다.

> 판정은 두 단계다 — (a) 두 팔의 결과가 같다는 단언, (b) 두 팔의 비용 수치. (a) 없는 (b)는 의미가 없다. 빨라진 쪽이 다른 답을 냈을 수 있기 때문이다.

공간 색인에서 이 규약은 장식이 아니다. 색인이 후보를 **덜** 돌려주면 언제나 빨라진다. 그리고 그 상태는 화면에서 잘 보이지 않는다 — 타워가 가끔 다른 적을 쏠 뿐이다.

이 편을 쓰면서 실제로 그 상태를 만들었다. `UniformGrid::CellRange` 의 상한 클램프를 한 칸 줄이는 변이를 넣었더니(§10) 격자 팔이 **13.9ms 에서 12.7ms 로 빨라졌다.** 후보를 덜 찾았기 때문이다. 등가 단언이 없었다면 그 수치는 "격자가 더 빠르다"는 증거로 기록됐을 것이다.

그래서 이 편의 테스트는 순서가 고정돼 있다.

```cpp
CHECK_EQ(gridDiff, size_t{0});     // (a1) 매 틱 같은 대상
CHECK_EQ(hashDiff, size_t{0});     // (a2) 매 틱 같은 상태
CHECK_EQ(linear.finalHash, grid.finalHash);
std::printf("... linear=%.1fms grid=%.1fms quadtree=%.1fms\n", ...);   // (b)
```

## 2. 점수 함수를 한 벌로

세 팔이 같은 답을 내려면 **점수 계산이 한 벌**이어야 한다. 9편이 두 저장(AoS·SoA)에 대해 전진 산술을 한 함수로 모은 것과 같은 이유다.

<!-- snippet: include/game/game.hpp@target-score -->
```cpp
inline float TargetScore(const EnemyStore& enemies, size_t i, const math::Vec2& towerPos,
                         const math::Vec2& corePos, TargetMode mode) {
    switch (mode) {
        case TargetMode::First:
            return enemies.PathIndex(i) > 0
                ? -static_cast<float>(enemies.PathIndex(i)) * 1000000.0f
                : math::LengthSq(enemies.Pos(i) - corePos);
        case TargetMode::Strongest: return -enemies.Hp(i);
        case TargetMode::Nearest:
        default:                    return math::LengthSq(enemies.Pos(i) - towerPos);
    }
}
```

전수 스캔 팔과 후보 목록 팔은 이 함수를 부르는 방식만 다르다.

<!-- snippet: include/game/game.hpp@select-target-among -->
```cpp
inline int SelectTargetAmong(const math::Vec2& towerPos, const math::Vec2& corePos,
                             const EnemyStore& enemies, float range, TargetMode mode,
                             const uint32_t* candidates, size_t candidateCount,
                             const SightTest& sight = SightTest{}) {
    int best = -1;
    float bestScore = 0;
    const float r2 = range * range;
    for (size_t k = 0; k < candidateCount; ++k) {
        const size_t i = candidates[k];
        if (!enemies.Alive(i)) continue;
        if (!sight.Targetable(i)) continue;
        if (math::LengthSq(enemies.Pos(i) - towerPos) > r2) continue;
        const float score = TargetScore(enemies, i, towerPos, corePos, mode);
        if (best >= 0 && score >= bestScore) continue;
        if (!sight.Visible(towerPos, enemies.Pos(i))) continue;
        bestScore = score;
        best = static_cast<int>(i);
    }
    return best;
}
```

**사거리 재검사가 남아 있는 이유.** 색인의 `QueryRadius` 가 이미 반경으로 걸렀는데도 한 번 더 본다. 두 필터의 부동소수 연산이 같아야 경계에 정확히 걸친 적의 운명이 같아지기 때문이다. `UniformGrid::QueryRadius` 는 `dx*dx + dy*dy <= r2` 를, 전수 스캔은 `LengthSq(pos - towerPos) > r2` 를 본다 — 같은 연산이지만 다른 자리에서 계산한다. 후보 팔에서 전수 스캔과 **같은 식**을 한 번 더 돌리면 그 차이가 사라진다.

## 3. 동점의 승자를 지키는 정렬

전수 스캔은 행 번호 오름차순으로 돌면서 `score < bestScore` 일 때만 갱신한다. 즉 **동점의 승자는 행 번호가 낮은 쪽**이다.

같은 좌표에 적 다섯이 겹쳐 있으면 세 모드 전부에서 점수가 같고, 승자는 0번이다. 격자와 쿼드트리는 후보를 셀/노드 순회 순서로 돌려주므로 그 순서가 다르다.

그래서 정렬한다.

<!-- snippet: include/game/game.hpp@target-index -->
```cpp
class TargetIndex {
public:
    void Configure(TargetIndexKind kind, const math::Aabb2& bounds, float cell) {
        kind_ = kind;
        bounds_ = bounds;
        cell_ = cell > 1.0f ? cell : 1.0f;
        rebuilds_ = 0;
    }
    TargetIndexKind Kind() const { return kind_; }
    size_t Rebuilds() const { return rebuilds_; }
    size_t NodeCount() const { return tree_.NodeCount(); }

    void Rebuild(const EnemyStore& enemies) {
        if (kind_ == TargetIndexKind::Linear) return;
        ++rebuilds_;
        const size_t count = enemies.Size();
        if (kind_ == TargetIndexKind::Grid) {
            grid_.Init(bounds_, cell_);
            for (size_t i = 0; i < count; ++i)
                if (enemies.Alive(i)) grid_.Insert(enemies.Pos(i), static_cast<uint32_t>(i));
            return;
        }
        tree_.Init(bounds_, 8, 8);
        for (size_t i = 0; i < count; ++i)
            if (enemies.Alive(i)) tree_.Insert(enemies.Pos(i), static_cast<uint32_t>(i));
    }

    // 후보를 오름차순으로 out 에 채우고 개수를 돌려준다. 정렬 비용은 색인 팔이
    // 치르는 값이다 — 순서를 맞추지 않으면 동점이 갈려 비교가 성립하지 않는다.
    size_t Query(const math::Vec2& at, float radius, uint32_t* out, size_t capacity) const {
        size_t count = 0;
        auto push = [&](uint32_t row) { if (count < capacity) out[count++] = row; };
        if (kind_ == TargetIndexKind::Grid) grid_.QueryRadius(at, radius, push);
        else                                tree_.QueryRadius(at, radius, push);
        std::sort(out, out + count);
        return count;
    }
    // ...
};
```

`std::sort` 가 색인 팔의 순이익을 갉아먹는다는 것은 사실이고, 그것을 숨기지 않는다. **순서를 맞추지 않으면 잴 것이 없다** — 두 팔이 다른 적을 쏘기 시작하면 그 뒤의 모든 상태가 갈라지고, 비교는 두 게임의 비교가 된다.

정렬을 지운 변이의 출력이 §10에 있다.

## 4. 색인은 틱마다 통째로 다시 짓는다

적은 매 틱 움직인다. 그러면 색인 갱신이 재구축보다 싼지 물어야 한다.

균일 격자에서 항목 하나를 옮기는 것은 옛 셀에서 지우고 새 셀에 넣는 것이다. 옛 셀에서 지우려면 그 셀의 벡터를 훑어야 하고(선형), 옮기지 않은 항목도 셀이 바뀌었는지 확인해야 한다. 전부 움직이는 워크로드에서 그것은 재구축과 같은 일을 더 복잡하게 하는 것이다.

쿼드트리는 더 나쁘다. 항목이 옮겨 가면 노드가 비거나 넘치고, 합치기·쪼개기가 연쇄한다.

그래서 둘 다 `Clear` → `Insert × N` 이다. 그리고 그 재구축이 결정적이다 — 삽입 순서가 행 번호 오름차순으로 고정돼 있다.

프레임 루프 쪽:

<!-- snippet: include/game/game.hpp@tower-fire -->
```cpp
void TowerFireSystem(float dt) {
    MEGA_PROFILE_SCOPE("sim.tower_fire");
    // ...
    const size_t towerCount = towers_.size();
    if (towerCount == 0) return;

    // 색인 팔은 틱당 한 번 통째로 다시 짓는다. 적이 매 틱 움직이므로 갱신이
    // 재구축보다 싸지 않고, 재구축은 순서가 고정돼 결정성이 유지된다.
    const bool indexed = switches_.targetIndex != TargetIndexKind::Linear;
    const size_t rows = enemies_.Size();
    uint32_t* candidates = nullptr;
    if (indexed && rows > 0) {
        MEGA_PROFILE_SCOPE("sim.target_index");
        targetIndex_.Rebuild(enemies_);
        candidates = scratch_.Alloc<uint32_t>(towerCount * rows);
    }
    // ...
    int* targets = scratch_.Alloc<int>(towerCount);
    // ...
    RunChunks(towerCount, [=](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            const Tower& t = towers[i];
            // ...
            if (t.timer - dt > 0.0f) { targets[i] = -1; continue; }
            if (!useIndex) {
                targets[i] = SelectTargetIn(t.pos, core, *enemies, t.range, t.targetMode, sight);
                continue;
            }
            uint32_t* slot = candidates + i * rows;
            const size_t found = index->Query(t.pos, t.range, slot, rows);
            targets[i] = SelectTargetAmong(t.pos, core, *enemies, t.range, t.targetMode,
                                           slot, found, sight);
        }
    });
    // ...
}
```

후보 배열이 **타워마다 따로**인 이유는 11편의 병렬화다. `RunChunks` 가 타워 구간을 워커에 나누므로 두 워커가 같은 버퍼에 쓰면 안 된다. 타워당 `rows` 칸을 잡고 `candidates + i * rows` 로 슬라이스한다.

그 배열의 크기는 최악의 경우(모든 적이 사거리 안)를 담아야 하므로 타워 16 × 적 2048 × 4바이트 = 128KB 다. 10편의 프레임 아레나에서 잡는다.

<!-- snippet: include/game/game.hpp#Init -->
```cpp
void Init(const math::Vec2& corePos, const math::Vec2& spawnPos,
          const std::vector<WaveDef>& waves, int startGold = 100, float coreHp = 100) {
    // ...
    // 색인 팔은 타워마다 후보 배열을 프레임 스크래치에서 잡는다. 기본값
    // (linear)에서는 그 배열이 없으므로 아레나 크기도 예전 그대로다.
    const size_t candidateBytes = switches_.targetIndex == TargetIndexKind::Linear
        ? 0 : kMaxTowers * enemyCap_ * sizeof(uint32_t) + 4096;
    scratch_.Init(switches_.frameAlloc, 64 * 1024 + enemyCap_ * 64 + candidateBytes);
    // ...
}
```

기본 팔에서 아레나 크기가 **한 바이트도 달라지지 않는다.** 10편이 잰 `peak small 528B / large 16,400B / overflow 0` 이 그대로 재현된다.

## 5. 사격선 — 다른 종류의 질의

13편이 만든 바위 564칸이 게임 규칙에 들어온다. **타워는 바위를 뚫고 쏘지 못한다.**

이 규칙은 절차 맵에서만 산다. classic 맵에도 `Blocked` 칸이 8개 있지만(모서리 장식), 거기에 사격선을 켜면 12편까지의 조준 결과가 달라진다 — 기본값이 바뀌는 변경이다.

그래서 판정 객체가 맵 종류를 들고 다닌다.

```cpp
struct SightTest {
    const StaticGeometry* geometry = nullptr;
    StaticQueryKind       kind = StaticQueryKind::Naive;
    bool Visible(const math::Vec2& from, const math::Vec2& to) const {
        return !geometry || !geometry->SegmentBlocked(from, to, kind);
    }
};
```

`geometry == nullptr` 이면 언제나 보인다. classic 맵의 `Sight()` 가 그 상태를 만든다.

**가림 판정을 어디에 두는가** 가 이 절의 설계 결정이다. 자연스러운 자리는 후보를 거를 때다 — 사거리 안 적 전부에 대해 사격선을 보고, 보이는 것만 남긴 뒤 점수를 매긴다.

그러지 않았다.

```cpp
const float score = TargetScore(enemies, i, towerPos, corePos, mode);
if (best >= 0 && score >= bestScore) continue;
// 가림 판정은 **이길 후보에만** 건다. 먼저 걸러도 결과는 같지만, 사격선
// 질의는 후보 하나당 BVH 순회 한 번이라 전수로 걸면 색인의 이득을 전부
// 되돌려준다.
if (!sight.Visible(towerPos, enemies.Pos(i))) continue;
```

점수가 현재 최선을 이기지 못하면 사격선을 아예 묻지 않는다. 결과가 같은 이유는 단순하다 — 우리가 원하는 것은 **보이는 것 중 최선** 하나뿐이고, 최선을 이기지 못하는 후보의 가시성은 답에 영향을 주지 않는다.

절차 맵 셀프테스트에서 사거리 안 적은 보통 수 마리이고, 그중 최선을 갱신하는 후보는 한둘이다. 전수로 걸었다면 질의가 몇 배 늘었을 것이다.

## 6. 2D 게임에 3D BVH 를 쓴 이유

`math/spatial/bvh.hpp` 는 `Aabb3` 를 담는다. 이 게임은 2D 다.

2D 사본을 만들 수도 있었다. 만들지 않은 이유는 **같은 알고리즘이 두 벌이 되는 것**이 이 저장소가 반복해서 피해 온 상태이기 때문이다(9편의 전진 산술, 15편의 점수 함수). 사본은 언제나 한쪽만 고쳐진다.

대신 z 를 매립한다. 칸 하나는 z∈[0,1] 인 상자이고, 질의점의 z 는 늘 0.5 다.

<!-- snippet: include/game/terrain.hpp#Rebuild -->
```cpp
void Rebuild(const DefenseMap& map) {
    prims_.clear();
    cells_.clear();
    const float cell = static_cast<float>(map.CellSize());
    for (int y = 0; y < map.Rows(); ++y)
        for (int x = 0; x < map.Columns(); ++x) {
            if (!map.Rock(x, y)) continue;
            const float px = static_cast<float>(x) * cell;
            const float py = static_cast<float>(map.OriginY()) + static_cast<float>(y) * cell;
            prims_.push_back(math::Aabb3{{px, py, 0.0f}, {px + cell, py + cell, 1.0f}});
            cells_.push_back({x, y});
        }
    bvh_.Build(prims_, 4);
    ++version_;
}
```

비용은 z 축의 slab 테스트 한 번과 `Aabb3` 하나당 8바이트다. 564칸이면 4.5KB — 사본 하나의 유지 비용보다 싸다.

그리고 14편의 카메라와 좌표 규약이 같다(Z-up, 지면 z=0). 두 코드가 같은 좌표를 말한다.

두 질의는 이렇다.

<!-- snippet: include/game/terrain.hpp@query-region -->
```cpp
void QueryRegion(const math::Aabb3& region, StaticQueryKind kind,
                 std::vector<uint32_t>& out) const {
    out.clear();
    if (kind == StaticQueryKind::Bvh) {
        bvh_.Query(region, [&out](int prim) { out.push_back(static_cast<uint32_t>(prim)); });
        std::sort(out.begin(), out.end());
        return;
    }
    for (size_t i = 0; i < prims_.size(); ++i)
        if (prims_[i].Intersects(region)) out.push_back(static_cast<uint32_t>(i));
}
```

<!-- snippet: include/game/terrain.hpp@segment-blocked -->
```cpp
bool SegmentBlocked(const math::Vec2& from, const math::Vec2& to,
                    StaticQueryKind kind) const {
    if (prims_.empty()) return false;
    const math::Vec3 a{from.x, from.y, 0.5f};
    const math::Vec3 b{to.x, to.y, 0.5f};
    if (kind == StaticQueryKind::Naive) {
        for (const math::Aabb3& box : prims_)
            if (SegmentHitsAabb(a, b, box)) return true;
        return false;
    }
    // BVH 팔: 선분의 AABB 로 가지를 쳐 낸 뒤 후보에만 slab 테스트를 건다.
    math::Aabb3 span = math::Aabb3::Empty();
    span.Expand(a);
    span.Expand(b);
    bool blocked = false;
    bvh_.Query(span, [&](int prim) {
        if (!blocked && SegmentHitsAabb(a, b, prims_[static_cast<size_t>(prim)]))
            blocked = true;
    });
    return blocked;
}
```

선분-AABB 판정은 표준 slab 테스트다.

<!-- snippet: include/game/terrain.hpp@segment-aabb -->
```cpp
inline bool SegmentHitsAabb(const math::Vec3& from, const math::Vec3& to,
                            const math::Aabb3& box) {
    float tMin = 0.0f, tMax = 1.0f;
    const math::Vec3 d = to - from;
    for (int axis = 0; axis < 3; ++axis) {
        const float origin = from[axis], delta = d[axis];
        const float lo = box.min[axis], hi = box.max[axis];
        if (std::fabs(delta) < 1e-9f) {
            if (origin < lo || origin > hi) return false;   // 축에 평행하고 밖이다
            continue;
        }
        float t0 = (lo - origin) / delta;
        float t1 = (hi - origin) / delta;
        if (t0 > t1) { const float swap = t0; t0 = t1; t1 = swap; }
        if (t0 > tMin) tMin = t0;
        if (t1 < tMax) tMax = t1;
        if (tMin > tMax) return false;
    }
    return true;
}
```

z 축에서 `delta` 가 0 이다(선분이 지면과 평행). 그 경우 원점 z=0.5 가 [0,1] 안에 있으므로 `continue` 로 넘어간다 — 축 정렬 slab 테스트의 표준 예외 처리가 여기서 실제로 도는 유일한 자리다.

## 7. 무효화 규칙은 검사로만 살아 있다

BVH 는 정적 기하 위에 선다. 발파는 그 기하를 바꾼다.

규칙은 한 줄이다. **지형이 바뀌면 다시 짓는다.** `ApplyBlast` 가 열린 칸이 있을 때만 `Rebuild` 를 부른다.

문제는 이런 규칙이 문서에만 있으면 지키지 않아도 아무 일이 일어나지 않는다는 것이다. 낡은 색인은 크래시하지 않는다 — 이미 열린 칸을 여전히 바위로 보고하고, 사격선 질의가 없는 벽에 막힌다.

그래서 **낡은 색인이 실제로 갈라진다는 것**을 테스트가 보인다.

```cpp
fresh.Rebuild(map);
CHECK(fresh.Version() > versionBefore);
CHECK(stale.Version() == versionBefore);
CHECK(fresh.PrimCount() + static_cast<size_t>(opened) == stale.PrimCount());
// ... 열린 칸을 정확히 덮는 질의에서 두 색인이 갈라진다
CHECK_EQ(divergent, static_cast<size_t>(opened));
```

25칸을 열고, 격자 전체를 칸 단위로 훑으며 두 색인의 히트 수가 다른 칸을 센다. 그 수가 **정확히 25** 여야 한다. 더 적으면 무효화가 부분적으로만 필요한 것이고, 더 많으면 색인이 다른 이유로도 틀린 것이다.

`Version()` 은 그 규칙을 밖에서 관측하는 유일한 창이다. `Rebuild` 마다 오르고, 아무도 그것을 읽지 않으면 무효화를 잊었는지 확인할 방법이 없다.

## 8. 측정: 교차점은 어디인가

조준 색인부터.

```
[classic seed=1 ticks=3000]  linear=2.5ms grid=2.2ms quadtree=2.1ms
[procgen seed=3 ticks=2000]  linear=6.9ms grid=7.3ms quadtree=7.1ms
[procgen seed=5 ticks=2000]  linear=6.3ms grid=6.6ms quadtree=6.6ms
[scale=large towers=16 enemies=2048 ticks=600] linear=23.7ms grid=14.5ms quadtree=65.9ms
```

앞의 세 줄은 **차이를 말하지 않는다.** 적 상한 64, 타워 6~12 인 워크로드에서 세 팔의 차이는 실행 간 변동(±20%)보다 작다. 그 규모에서 "격자가 빠르다"고 적는 것은 잡음을 결론으로 쓰는 것이다.

마지막 줄이 교차점 너머다. 적 2048·타워 16 에서

- **grid 가 linear 를 39% 앞선다**(23.7 → 14.5ms). 타워 하나가 훑는 적이 2048에서 사거리 안 몇 마리로 줄고, 그 절약이 틱당 재구축 한 번의 비용을 넘는다.
- **quadtree 가 linear 에 2.8배 진다**(65.9ms). 이유는 재구축이다. `Quadtree` 는 노드마다 `std::vector<Item>` 을 갖고, 2048개를 넣으면 분할이 연쇄하면서 노드 벡터와 항목 벡터가 계속 자란다. 균일 격자는 셀 배열을 `assign` 으로 한 번 비우고 `push_back` 만 한다.

**설계안이 예상한 것과 반대다.** 설계는 "적이 경로 주변에 몰리는 이 게임의 분포에서는 균일 격자가 빈 셀을 많이 만들고 쿼드트리의 적응 분할이 유리할 수 있다"고 적었다. 실측은 그 가정을 기각한다 — 적응 분할의 이득(질의당 방문 노드 감소)보다 재구축 비용의 손해가 훨씬 크다. 매 틱 통째로 다시 짓는 워크로드에서 트리는 격자와 겨루지 못한다.

이 결론에는 조건이 붙는다. **색인을 재구축하지 않아도 되는 워크로드**(정적이거나 드물게 변하는 점 집합)라면 쿼드트리의 질의 이득만 남는다. 그런 워크로드가 이 게임에는 없다.

제품 안에서 본 값도 같은 이야기를 한다. 절차 맵 셀프테스트(적 상한 64):

| | `sim.tower_fire` 합계 | `sim.target_index` 합계 |
|---|---|---|
| `linear` | 0.49ms | — |
| `grid` | 0.97ms | 0.40ms |
| `quadtree` | 0.90ms | 0.27ms |

작은 규모에서 색인은 **손해**다. 기본값이 `linear` 인 이유가 그것이고, 그 기본값은 규약("기본값은 스위치 이전의 동작")과도 우연히 일치한다.

정적 질의는 반대다.

```
20000 region queries (prims=558): naive=23.8ms bvh=3.4ms  hits=69972
20000 segment queries:            naive=16.9ms bvh=5.5ms  blocked=16978
```

**7.0배와 3.1배.** 여기서는 규모가 이미 교차점 너머다 — 프리미티브 558개는 전수 순회에 충분히 많고, 정적이라 재구축이 발파 때만 일어난다.

## 9. BVH 가 이기는 폭이 질의 종류마다 다르다

같은 BVH 가 영역 질의에서 7.0배 이기고 선분 질의에서 3.1배 이긴다. 제품 안에서는 그 폭이 더 좁다 — `sim.tower_fire` 합계가 naive 0.48~0.49ms, bvh 0.40~0.42ms(3회 실행)로 15% 남짓이다.

폭이 좁아지는 이유 하나는 코드에 있다. **전수 순회는 조기 종료를 하고 BVH 는 하지 못한다.**

```cpp
if (kind == StaticQueryKind::Naive) {
    for (const math::Aabb3& box : prims_)
        if (SegmentHitsAabb(a, b, box)) return true;   // 첫 히트에서 멈춘다
    return false;
}
```

전수 순회는 **첫 번째 막는 바위에서 멈춘다.** 바위가 564개고 절차 맵의 시야가 대체로 막혀 있으므로, 평균적으로 몇 개만 보고 끝난다.

BVH 는 멈출 수 없다. `math::Bvh::Query` 의 콜백은 반환값이 없고, 순회를 중단할 방법이 없다. 그래서 `blocked` 플래그를 세우고 **나머지 후보를 전부 훑는다.**

```cpp
bvh_.Query(span, [&](int prim) {
    if (!blocked && SegmentHitsAabb(a, b, prims_[static_cast<size_t>(prim)]))
        blocked = true;
});
```

즉 이 질의에서 naive 팔은 "후보 전부"가 아니라 "첫 히트까지"만 본다. 절차 맵의 시야는 대체로 막혀 있으므로(무작위 짧은 선분 20000개 중 16978개가 막혔다) 그 조기 종료가 자주 걸린다. BVH 는 가지치기로 후보를 줄이지만 남은 후보를 끝까지 훑으므로, 두 이득이 상쇄되는 만큼 폭이 좁아진다.

고치려면 `Bvh::Query` 가 콜백 반환값으로 중단을 받아야 한다. 이 편에서는 고치지 않았다 — 헤더의 계약을 바꾸는 일이고, 그 계약을 쓰는 다른 소비자(영역 질의)는 조기 종료가 필요 없다.

**한 번 잘못 읽었던 기록도 남긴다.** 중간 측정에서 BVH 팔의 `sim.tower_fire` 가 naive 보다 컸던 적이 있고(0.60 vs 0.50ms) 그것을 "BVH 가 제품에서 진다"로 적을 뻔했다. 클린 빌드에서 3회 반복하니 방향이 반대였다. 단발 측정과 결론 사이에 반복이 없으면 잡음이 그대로 서술이 된다.

## 10. 변이 셋

**[1] 격자 상한 클램프를 한 칸 줄인다.** `UniformGrid::CellRange` 의 `x1`, `y1` 을 −1 한다. 경계 셀에 있는 적이 후보에서 빠진다.

```
=== baseline ===
  [scale=large towers=16 enemies=2048 ticks=600] linear=23.7ms grid=13.9ms quadtree=66.2ms
[test] target_index_ab: 69 checks, 0 failures
=== mutated (CellRange upper clamp -1) ===
  [scale=large towers=16 enemies=2048 ticks=600] linear=23.3ms grid=12.7ms quadtree=70.1ms
  FAIL tests\test_target_index_ab.cpp:259  CHECK_EQ(linear.second, grid.second)
  FAIL tests\test_target_index_ab.cpp:151  CHECK_EQ(chosen, SelectTargetIn(...))
  FAIL tests\test_target_index_ab.cpp:152  CHECK_EQ(chosen, 0)
[test] target_index_ab: 69 checks, 3 failures
```

주목할 것은 **격자 팔이 빨라졌다**는 점이다(13.9 → 12.7ms). 후보를 덜 찾았기 때문이다. 폭이 9%뿐인 이유는 빠진 것이 맵 가장자리 셀뿐이라서인데, **작은 이득이라도 그것이 잘못된 답에서 왔다는 사실은 달라지지 않는다.** 등가 단언이 없으면 이 수치는 개선으로 기록된다.

**[2] 후보 정렬을 지운다.** `TargetIndex::Query` 의 `std::sort` 를 뺀다. 후보 집합은 여전히 정확하고, 순서만 셀 순회 순서가 된다.

```
[test] target_index_ab
  FAIL tests\test_target_index_ab.cpp:103  CHECK_EQ(gridDiff, size_t{0})
  FAIL tests\test_target_index_ab.cpp:109  CHECK_EQ(hashDiff, size_t{0})
  FAIL tests\test_target_index_ab.cpp:110  CHECK_EQ(linear.finalHash, grid.finalHash)
  FAIL tests\test_target_index_ab.cpp:259  CHECK_EQ(linear.second, grid.second)
  FAIL tests\test_target_index_ab.cpp:260  CHECK_EQ(linear.second, tree.second)
```

동점이 갈리고, 그 순간부터 두 팔이 다른 적을 쏘고, 상태 해시가 통째로 갈라진다. **집합이 같아도 순서가 다르면 다른 게임이다.**

**[3] 쿼드트리 하강이 사분면을 무시한다.** `InsertInto` 의 `child[Quadrant(c, p)]` 를 `child[0]` 으로 바꾼다. 점이 엉뚱한 가지에 실리고, 질의가 그 가지를 경계로 쳐 낸다.

```
66/77 Test #66: test_spatial ...........***Failed
69/77 Test #69: test_target_index_ab ...***Failed
97% tests passed, 2 tests failed out of 77
```

**설계안이 제시한 변이는 죽지 않았다.** 설계는 "`Subdivide` 의 사분면 경계 비교를 `<` 에서 `<=` 로 바꾸면 경계 점이 두 번 나와 집합 비교가 실패"한다고 적었다. 실제로 `Quadrant` 의 `>=` 를 `>` 로 바꿔 보면 **77개 테스트가 전부 통과한다.**

이유는 구현에 있다. `Quadrant` 는 삽입과 하강 **양쪽에서 같은 함수**로 쓰이므로, 경계에 걸친 점은 어느 쪽으로 가든 일관되게 한 자식에만 들어간다. 그리고 `Aabb2::Contains` 가 경계를 포함(`<=`)하므로 두 자식의 경계 상자가 그 점을 모두 담고, 질의는 두 자식을 다 방문하지만 점은 한 곳에만 있으므로 중복이 생기지 않는다. 설계가 예상한 실패 모드가 이 코드에는 없다.

그래서 같은 자리를 무력화하는 다른 변이([3])를 찾아 걸었다.

## 11. 실행과 관측

```
> ctest -C Release
100% tests passed out of 77
```

**조준 색인 A/B.**

```
> build_verify\bin\Release\test_target_index_ab.exe
[test] target_index_ab
  [classic seed=1 ticks=3000] linear=2.5ms grid=2.2ms quadtree=2.1ms
  [procgen seed=3 ticks=2000] linear=6.9ms grid=7.3ms quadtree=7.1ms
  [procgen seed=5 ticks=2000] linear=6.3ms grid=6.6ms quadtree=6.6ms
  [scale=large towers=16 enemies=2048 ticks=600] linear=23.7ms grid=14.5ms quadtree=65.9ms
[test] target_index_ab: 69 checks, 0 failures
```

69개 단언 안에는 규모 측정만 있지 않다. 경계에 정확히 걸린 적(거리² == 사거리²), 같은 좌표에 겹친 적 다섯의 동점 처리, 세 타게팅 모드 각각에서의 일치, 그리고 바위 뒤 적이 세 팔 모두에서 똑같이 제외되는지.

**정적 질의 A/B.**

```
> build_verify\bin\Release\test_static_bvh_ab.exe
[test] static_bvh_ab
  20000 region queries (prims=558): naive=23.8ms bvh=3.4ms  hits=69972
  20000 segment queries: naive=16.9ms bvh=5.5ms  blocked=16978
[test] static_bvh_ab: 1118 checks, 0 failures
```

`hits=69972` 와 `blocked=16978` 이 두 팔에서 같다는 것이 (a) 이고, 시간이 (b) 다.

1118개 단언 중 대부분은 무효화 규칙 테스트(41×41 칸을 훑으며 갈라진 칸 수를 세는 것)와 트리 모양 불변성(프리미티브 순서를 뒤집고 리프 크기를 1로 바꿔도 같은 답이 나오는지)이다.

**제품 안에서.**

```
> build_verify\bin\Release\mega_defense.exe --selftest 25000 --ab map=procgen --ab target_index=grid
[selftest] result=CLEAR ticks=2971 towers=7 wave=6 packet=115 ...
[selftest] terrain route=79 opened=16 debris_live=0 debris_settled=27 target_rebuilds=1100
```

`target_rebuilds=1100` — 색인이 실제로 1100번 다시 지어졌다. `linear` 팔에서 이 값은 0이다. 그 숫자가 없으면 "색인 팔로 돌렸다"와 "색인 팔로 돌렸다고 생각했다"를 구분할 수 없다.

그리고 `ticks=2971 towers=7 wave=6` 과 `route=79 opened=16 debris_settled=27` 이 `target_index=linear` · `grid` · `quadtree` · `static_query=bvh` · `broadphase=grid` · `camera=tilted3d` 여섯 실행에서 전부 같다.

---

## 결론

같은 질의에 세 구현을 붙여 재 봤고, 결론은 **규모가 답을 정한다**는 것이었다. 적 64에서는 세 팔이 구분되지 않고, 적 2048에서는 격자가 42% 이기고 쿼드트리가 3배 진다. "공간 색인은 빠르다"는 문장에 규모를 붙이지 않으면 그 문장은 검증 가능한 주장이 아니다.

두 번째 결론은 방법에 관한 것이다. **등가 단언이 성능 측정의 전제다.** 이 편에서 넣은 변이 하나가 격자 팔을 13.9ms 에서 12.7ms 로 만들었고, 그 개선의 정체는 후보를 덜 찾은 것이었다. 상태 해시 비교가 그것을 즉시 잡았다.

세 번째는 정직성에 관한 것이다. 설계안이 예측한 쿼드트리 변이는 이 구현에서 아무 테스트도 죽이지 못했다 — 예측한 실패 모드가 코드에 없었기 때문이다. 그 사실을 적고 같은 자리를 무력화하는 다른 변이를 찾았다. 그리고 한 번은 단발 측정을 잘못 읽어 "BVH 가 제품에서 진다"고 적을 뻔했고, 반복 측정이 그것을 뒤집었다 — 그 기록도 §9에 남겼다.

현재 상태의 한계 넷.

**BVH 질의는 중단할 수 없다.** `Bvh::Query` 의 콜백이 반환값을 갖지 않아 첫 히트에서 멈추지 못한다. 사격선처럼 "있는지만 묻는" 질의에서 그것이 전수 순회에 지는 원인이다.

**격자 셀 크기가 고정이다.** `Configure` 에서 64픽셀로 잡고 바꾸지 않는다. 사거리가 70~180픽셀로 다양하므로 스나이퍼(180)의 질의는 여러 셀을 훑고 스플래시(70)는 한두 셀만 본다. 타워 종류별로 다른 셀 크기를 쓰는 색인은 여기 없다.

**쿼드트리 파라미터를 스위치로 노출하지 않았다.** `maxItems=8`, `maxDepth=8` 로 고정이다. 설계안은 이 둘을 스위치로 열어 교차점을 옮겨 보라고 했는데, 재구축 비용이 지배적이라 그 파라미터로 결론이 뒤집히지 않는다고 판단해 넣지 않았다.

**정적 기하에 안착 잔해가 아직 들어가지 않는다.** `StaticGeometry::Rebuild` 는 `map.Rock(x, y)` 만 본다. 16편의 잔해는 통행 격자를 막지만 사격선은 막지 않는다.
