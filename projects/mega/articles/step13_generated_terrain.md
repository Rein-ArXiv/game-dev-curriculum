# 13. 맵이 파일 한 장에서 생성물로 바뀐다

## 들어가며

12편까지 이 게임의 맵은 `assets/level.txt` 한 장이었다. 16×15 격자에 한 칸 폭의 굴곡 경로가 그려져 있고, `DefenseMap::FromAscii` 가 그 파일을 한 번 걸어 보며 네 가지를 거부한다 — 끊긴 경로, 분기, 순환, 떠 있는 조각. 그 검사를 통과한 맵은 **스폰에서 코어까지의 단일 사슬**이고, 적은 그 배열을 순서대로 따라간다.

그 모델의 값은 명확하다. 경로가 데이터라서 시뮬이 경로 탐색을 하지 않고, 그래서 결정성이 공짜다. 한계도 명확하다. **맵이 하나뿐이다.** 그리고 그 하나를 사람이 손으로 그려야 한다.

이 편에서 맵은 시드 하나에서 나오는 것이 된다. 조각 셋이 순서대로 돈다.

| 조각 | 헤더 | 하는 일 |
|---|---|---|
| 미로 골격 | `tools/procgen.hpp` | 재귀 백트래커가 완전 미로(신장 트리)를 판다 |
| 밀도장 | `math/noise.hpp` | 임계 이상인 벽을 깎아 방과 회랑을 만든다 |
| 검증 | `tools/level_editor.hpp` | flood fill 로 스폰→코어 도달을 확인한다 |

그리고 네 번째가 붙는다. `math/sdf.hpp` 의 브러시를 바위 밀도장에서 **감산**하는 발파 — 지형이 매치 중에 바뀐다.

도달점은 이렇다. `--ab map=procgen` 을 주면 41×41 칸 아레나가 시드에서 생성되고(미로 개방 799칸 + 밀도장이 깎은 318칸, 남은 바위 564칸, 광맥 148칸), 스폰에서 코어까지 79칸 경로가 계산으로 나오며, 매치 중 발파가 그 바위에서 16칸을 실제로 뚫는다. 기본값 `map=classic` 은 한 비트도 달라지지 않는다 — 셀프테스트가 여전히 `ticks=1377 towers=11 wave=6 packet=175` 로 끝난다.

---

## 목차
1. [순수 미로에는 타워를 세울 자리가 없다](#1-순수-미로에는-타워를-세울-자리가-없다)
2. [밀도장이 벽을 깎는다](#2-밀도장이-벽을-깎는다)
3. [검증은 감산이 끝난 뒤 한 번](#3-검증은-감산이-끝난-뒤-한-번)
4. [경로가 데이터에서 계산으로 바뀐다](#4-경로가-데이터에서-계산으로-바뀐다)
5. [flood fill 이 들어가는 두 번째 자리 — 매 Place 명령](#5-flood-fill-이-들어가는-두-번째-자리--매-place-명령)
6. [발파: 감산만 하는 연산](#6-발파-감산만-하는-연산)
7. [두 맵 모델을 공존시킨 방법](#7-두-맵-모델을-공존시킨-방법)
8. [광맥과 명암 — 같은 장에서 나오는 세 가지](#8-광맥과-명암--같은-장에서-나오는-세-가지)
9. [지형이 와이어를 건너는 방법](#9-지형이-와이어를-건너는-방법)
10. [변이: 감산을 합집합으로](#10-변이-감산을-합집합으로)
11. [실행과 관측](#11-실행과-관측)

---

## 1. 순수 미로에는 타워를 세울 자리가 없다

`tools/procgen.hpp` 의 `MazeGen::Generate(cw, ch, seed)` 는 재귀 백트래커로 미로를 판다. 셀을 홀수 좌표에 두고 셀 사이 벽을 뚫는 방식이라, 결과가 항상 **하나의 연결된 트리**다. 즉 모든 칸이 도달 가능하고 사이클이 없다.

그 성질이 이 게임에 주는 것이 하나 있다. **연결성이 구조적으로 보장된다** — 검증이 실패할 수 없는 입력이 나온다.

그리고 하나를 뺏는다. 미로의 회랑은 **한 칸 폭**이다. 20×20 셀 미로는 41×41 타일이 되고, 그중 개방 칸은 799칸인데 전부 폭 1의 통로다. 타워 디펜스에서 폭 1의 통로가 뜻하는 것은 명확하다 — **타워를 세울 칸이 없다.** 통로에 세우면 길이 막히고, 벽에는 못 세운다.

그래서 미로는 골격이지 결과가 아니다. 깎아야 한다.

## 2. 밀도장이 벽을 깎는다

`math/noise.hpp` 는 세 종류의 장을 준다. Perlin, Simplex, Worley(셀룰러). 그리고 `FBM` 이 옥타브를 누적한다.

이 게임이 쓰는 밀도장은 둘의 합성이다.

<!-- snippet: include/game/map_gen.hpp@density-field -->
```cpp
inline float Density(const math::Noise& noise, int x, int y, const TerrainParams& p) {
    const float fx = static_cast<float>(x) * p.frequency;
    const float fy = static_cast<float>(y) * p.frequency;
    const float shape = math::FBM(
        [&noise](float a, float b) { return noise.Simplex2(a, b); }, fx, fy, p.octaves);
    const float grain = noise.Worley2(fx * 0.5f, fy * 0.5f);   // ≥ 0
    return shape * 0.75f + (grain - 0.5f) * 0.5f;
}
```

역할이 다르다. **FBM(Simplex) 이 큰 덩어리를 만든다** — 4옥타브를 누적하면 방만 한 크기의 저주파 구조 위에 잔가지가 얹힌다. **Worley 는 그 안에 결을 넣는다** — 셀룰러 거리장이라 특징점 주변에 동심원 무늬가 생기고, 그것이 방 가장자리를 울퉁불퉁하게 만든다.

Perlin 을 쓰지 않은 이유는 헤더 주석이 이미 적어 둔 것과 같다. Perlin 은 격자 방향 아티팩트가 두드러진다. 지형에서 그 아티팩트는 "방이 전부 축에 정렬돼 보인다"로 나타난다.

깎기는 이 값 하나와 임계의 비교다.

<!-- snippet: include/game/map_gen.hpp@generate-map -->
```cpp
inline bool Generate(const TerrainParams& p, DefenseMap& out, TerrainStats* statsOut = nullptr) {
    tools::Level level = tools::MazeGen::Generate(p.mazeCellsX, p.mazeCellsY, p.seed);
    const int width = level.Width(), height = level.Height();
    // ...
    // 밀도장으로 벽을 깎는다. 테두리는 건드리지 않는다 — 아레나의 틀이다.
    const math::Noise noise(p.seed * 2654435761u + 1u);
    // ...
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            const float d = Density(noise, x, y, p);
            // ...
            if (level.At(x, y) == '#' && d > p.carve) {
                level.Set(x, y, '.');
                ++stats.carved;
            }
        }
    }
    // ...
}
```

테두리(`y=0`, `x=0`, 마지막 행·열)를 건드리지 않는 것이 유일한 예외다. 그 한 칸이 아레나의 틀이고, 뚫리면 잔해가 맵 밖으로 흘러 나간다(16편).

임계가 실제로 밀도장을 읽고 있는지는 **단조성**으로 확인한다. 임계를 낮출수록 더 많이 깎여야 한다.

| `carve` 임계 | 개방 칸(41×41 = 1681 중) |
|---|---|
| 0.60 | 적다 |
| 0.00 | 중간 |
| −0.40 | 많다 |

`tests/test_map_procgen.cpp` 의 `TestCarveMonotonic` 이 여섯 단계를 훑으며 개방 칸 수가 단조 증가하는지 보고, 양 끝의 차이가 200칸을 넘는지도 함께 본다. 단조성만 보면 **임계를 읽지 않는 구현도 통과한다**(상수 함수는 단조다). 양 끝의 차이가 그 구멍을 막는다.

밀도장이 축에 정렬되지 않았는지도 잰다. `Noise::Hash2` 가 두 좌표를 섞지 않으면 x 를 바꿔도 값이 거의 그대로여서 행 단위 줄무늬가 생긴다. `TestDensityNotAxisAligned` 는 행 방향 분산과 열 방향 분산을 각각 재고 그 비가 0.25~4.0 안에 있는지 본다.

## 3. 검증은 감산이 끝난 뒤 한 번

`tools/level_editor.hpp` 의 `Level::Validate()` 는 스폰(`S`)에서 목표(`G`)까지 4방향 flood fill 로 도달을 확인하고, 벽·바닥 수를 함께 센다.

미로가 연결성을 보장하고 깎기는 감산만 하므로, 이 검사는 **실패할 수 없다.** 그래도 부른다.

> "할 수 없다"와 "확인했다"는 다른 문장이다. 확인하지 않은 불변식은 다음 편집에서 조용히 깨진다.

실패하면 맵을 채택하지 않고 `Generate` 가 거짓을 반환한다. 그때 `defense::ChooseMap` 이 classic 으로 떨어진다.

<!-- snippet: include/game/defense_content.hpp@choose-map -->
```cpp
inline game::DefenseMap ChooseMap(const game::Switches& switches, const std::string& path,
                                  bool* loadedExternal = nullptr,
                                  game::mapgen::TerrainStats* statsOut = nullptr) {
    if (switches.map == game::MapSource::Procgen) {
        game::mapgen::TerrainParams params;
        params.seed = static_cast<uint32_t>(switches.mapSeed);
        game::DefenseMap generated;
        if (game::mapgen::Generate(params, generated, statsOut)) {
            if (loadedExternal) *loadedExternal = true;
            return generated;
        }
    }
    return LoadMapFile(path, loadedExternal);
}
```

플레이할 수 없는 맵으로 매치를 시작하는 것보다 나은 실패는 없다.

## 4. 경로가 데이터에서 계산으로 바뀐다

깎고 나면 개방 칸이 1117칸(799 + 318)이 되고, 스폰에서 코어까지 가는 길이 하나가 아니다. 그러면 "적이 따라갈 배열"이 파일에 없다.

경로를 **계산**한다. 스폰에서 시작하는 너비 우선 탐색이 코어에 닿으면, 선행 칸 배열을 거슬러 올라가 사슬을 만든다.

<!-- snippet: include/game/defense_map.hpp@recompute-route -->
```cpp
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
```

세 가지가 의도적이다.

**이웃 순서가 고정이다.** `{1,0}, {-1,0}, {0,1}, {0,-1}` — 같은 격자는 언제나 같은 경로를 낸다. 그 결정성이 호스트와 클라이언트가 같은 세계를 도는 근거의 절반이다(나머지 절반은 같은 시드).

**실패하면 아무것도 바꾸지 않는다.** 도달 불가면 `pathCells_` 를 건드리지 않고 거짓을 반환한다. 호출자가 방금 한 변경(타워 배치·잔해 안착)을 되돌릴 수 있어야 하기 때문이다.

**결과 형식이 예전과 같다.** `pathCells_` 라는 배열 하나. 규칙(`game/game.hpp`)은 그 배열만 읽으므로 맵이 파일에서 왔는지 시드에서 왔는지 모른다. `AdvanceEnemyOnPath` 는 12편과 같은 코드다.

경로가 계산이 되면 새 문제가 하나 생긴다. **경로가 매치 중에 바뀐다.** 살아 있는 적은 옛 배열의 인덱스를 들고 있다.

<!-- snippet: include/game/game.hpp@refresh-route -->
```cpp
void RefreshRoute() {
    const std::vector<math::Vec2> route = map_.Path();
    path_.AssignFrom(route.data(), route.size());
    // ...
    const size_t count = enemies_.Size();
    const size_t length = path_.Size();
    if (length == 0) return;
    for (size_t i = 0; i < count; ++i) {
        if (!enemies_.Alive(i)) continue;
        const math::Vec2 at = enemies_.Pos(i);
        size_t best = 0;
        float bestDistance = math::LengthSq(path_[0] - at);
        for (size_t k = 1; k < length; ++k) {
            const float d = math::LengthSq(path_[k] - at);
            if (d < bestDistance) { bestDistance = d; best = k; }   // 동점은 앞선 칸
        }
        enemies_.SetPathIndex(i, static_cast<uint32_t>(best));
    }
}
```

새 배열에서 자기 위치와 가장 가까운 칸을 찾아 거기서부터 다시 걷는다. 순회는 인덱스 오름차순이고 동점은 앞선 칸이 이긴다 — 재배치가 실행마다 같은 결과를 내야 하기 때문이다.

이 재배치가 O(적 × 경로)라는 것은 사실이고, 경로 변경이 드물기 때문에 감당한다. 적 64 × 경로 79 = 5056 번의 거리 계산이 배치 명령 하나당 한 번 돈다.

## 5. flood fill 이 들어가는 두 번째 자리 — 매 Place 명령

경로가 계산이 되는 순간 타워가 경로를 막을 수 있게 된다. 그것이 **메이즈 디펜스**다 — 타워를 벽으로 써서 적의 길을 늘린다.

그리고 그 규칙에는 반드시 짝이 되는 제약이 있다. **코어를 완전히 봉쇄하는 배치는 거절해야 한다.** 봉쇄되면 적이 갈 곳이 없고, 갈 곳이 없으면 게임이 아니다.

판정은 `tools::Level` 의 같은 flood fill 이다. 맵을 그 표현으로 내려보내되, **후보 칸이 이미 막혔다고 가정한** 격자를 만든다.

<!-- snippet: include/game/defense_map.hpp@to-level -->
```cpp
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
bool WouldRemainConnected(int blockX, int blockY) const {
    return ToLevel(blockX, blockY).Validate().Valid();
}
```

배치 규칙이 그것을 부른다.

<!-- snippet: include/game/game.hpp@can-place -->
```cpp
bool CanPlaceTower(const math::Vec2& pos) const {
    if (towers_.size() >= kMaxTowers) return false;
    if (!mapEnabled_) return true;
    if (!map_.IsBuildable(pos)) return false;
    math::Vec2 snapped = map_.Snap(pos);
    float minDistance = map_.CellSize() * 0.75f;
    float minDistanceSq = minDistance * minDistance;
    for (const Tower& tower : towers_)
        if (math::LengthSq(tower.pos - snapped) < minDistanceSq) return false;
    // 메이즈 디펜스의 배치 규칙: 이 칸을 막아도 스폰에서 코어까지 갈 수
    // 있어야 한다. 판정은 tools::Level 의 flood fill 이고, 후보 격자
    // (이 칸이 이미 막혔다고 가정한 격자)로 한 번 돈다.
    if (map_.Generated()) {
        const DefenseMap::Cell cell = map_.WorldToCell(snapped);
        if (!map_.WouldRemainConnected(cell.x, cell.y)) return false;
    }
    return true;
}
```

**같은 검사가 두 곳에서 도는 이유.** 채택 전 검사(§3)는 "이 맵이 애초에 플레이 가능한가"를 묻고, 배치 검사는 "이 수가 그것을 깨는가"를 묻는다. 질문이 다르므로 시점도 다르다. 검사기는 하나다 — `tools::Level::Validate()` 를 두 벌로 만들면 한쪽만 고쳐졌을 때 채택은 통과하는데 배치는 거절하는(혹은 반대인) 맵이 생긴다.

비용은 배치 명령 하나당 1681칸 flood fill 한 번이다. 명령은 사람이 클릭할 때만 온다.

`감사 434행이 지적한 중복` — `DefenseMap::FromAscii` 안의 연결성 검사와 `Level::Validate` 가 같은 일을 두 벌로 한다는 지적 — 은 여기서 절반만 해소된다. 두 검사는 이제 **다른 질문**에 답한다: `FromAscii` 는 "무분기 사슬인가"(classic 맵의 계약)를, `Validate` 는 "도달 가능한가"(생성 맵의 계약)를 본다. 무분기 사슬 검사는 생성 맵에 적용할 수 없고(생성 맵은 분기 투성이다), 도달 검사는 classic 맵의 계약을 표현하지 못한다. 그래서 합치지 않았다.

## 6. 발파: 감산만 하는 연산

새 능력이 생긴다. `B` 키를 누르면 커서 아래 바위가 뚫린다. 골드 40.

구현은 `math/sdf.hpp` 의 브러시를 바위 밀도장에서 감산하는 것이다.

<!-- snippet: include/game/terrain.hpp@apply-blast -->
```cpp
inline BlastResult ApplyBlast(DefenseMap& map, StaticGeometry& geometry,
                              const BlastBrush& brush, StaticQueryKind kind,
                              std::vector<uint32_t>& scratch,
                              std::vector<DefenseMap::Cell>* openedOut = nullptr) {
    BlastResult result;
    geometry.QueryRegion(brush.Bounds(), kind, scratch);
    result.candidates = static_cast<int>(scratch.size());
    for (uint32_t prim : scratch) {
        const DefenseMap::Cell c = geometry.CellOf(prim);
        const math::Vec2 center = map.CellCenter(c.x, c.y);
        const math::Vec3 p{center.x, center.y, 0.5f};
        // 바위 안이라는 사실을 -1 로 적는다(SDF 규약: 안이 음수). 브러시를 그
        // 장에서 감산하면 브러시 안쪽만 부호가 뒤집혀 열린다.
        const float after = math::OpSubtract(brush.Sd(p), -1.0f);
        if (after < 0.0f) continue;
        if (!map.OpenCell(c.x, c.y)) continue;
        ++result.opened;
        if (openedOut) openedOut->push_back(c);
    }
    if (result.opened > 0) geometry.Rebuild(map);
    return result;
}
```

`OpSubtract(a, b) = max(-a, b)` 이고 "b 에서 a 를 깎는다"는 뜻이다. 여기서 `b` 는 바위 장 — 바위 칸 안이므로 상수 −1 이다. `a` 는 브러시 SDF. 칸 중심이 브러시 안이면 `Sd ≤ 0` 이라 `-Sd ≥ 0` 이고, `max(≥0, -1) ≥ 0` 이므로 **열린다**. 밖이면 `Sd > 0` 이라 `-Sd < 0` 이고, `max(-Sd, -1) < 0` 이므로 그대로 바위다.

`after >= 0` 이 정확히 `Sd <= 0` 과 같다는 것을 그 두 줄이 말한다.

브러시는 두 형태다.

<!-- snippet: include/game/terrain.hpp#BlastBrush -->
```cpp
struct BlastBrush {
    math::Vec3 a{0, 0, 0.5f}, b{0, 0, 0.5f};
    float      radius = 26.0f;

    bool Capsule() const { return !(a.x == b.x && a.y == b.y && a.z == b.z); }
    float Sd(const math::Vec3& p) const {
        return Capsule() ? math::SdCapsule(p, a, b, radius) : math::SdSphere(p - a, radius);
    }
    // ...
};
```

제품은 지금 구 브러시만 쓴다(`BlastBrush::Sphere`). 캡슐은 축을 실제로 읽는지 확인하기 위해 테스트가 쓴다 — 같은 반지름의 구보다 더 많은 칸을 열어야 한다.

**감산만 하는 연산의 성질 하나가 게임 규칙 하나를 공짜로 준다.** 발파는 칸을 열기만 하고 닫지 않는다. 그러므로 발파가 코어를 봉쇄하는 일은 **구조적으로** 불가능하다 — 배치와 달리 연결성 검사가 필요 없다. `TestBlastNeverSeals` 가 무작위 발파 60회를 퍼부으며 매번 `Validate().Valid()` 와 `RecomputeRoute()` 를 확인한다.

월드 쪽은 이렇다.

<!-- snippet: include/game/game.hpp@world-blast -->
```cpp
int Blast(const math::Vec2& at) {
    if (won_ || lost_ || !mapEnabled_ || !map_.Generated()) return 0;
    if (gold_ < blastCost_) return 0;
    const math::Vec2 center = map_.Snap(at);
    const BlastBrush brush =
        BlastBrush::Sphere(center, static_cast<float>(map_.CellSize()) * 1.8f);
    blastOpened_.clear();
    const BlastResult result = ApplyBlast(map_, geometry_, brush, switches_.staticQuery,
                                          blastScratch_, &blastOpened_);
    if (result.opened == 0) return 0;
    gold_ -= blastCost_;
    // ...
    for (const DefenseMap::Cell& cell : blastOpened_) {
        if (openedCells_.size() >= kMaxOpenedCells) break;
        openedCells_.push_back(static_cast<uint16_t>(cell.y * map_.Columns() + cell.x));
    }
    map_.RecomputeRoute();
    RefreshRoute();
    debris_.SpawnBurst(center, result.opened * 2, rng_);
    debrisDirty_ = true;
    // ...
    return result.opened;
}
```

하나도 열리지 않으면 골드를 쓰지 않는다. 이미 열린 자리를 다시 눌러도 손해가 없어야 하기 때문이다.

## 7. 두 맵 모델을 공존시킨 방법

경로 모델이 둘이다. classic 은 저작된 무분기 사슬, procgen 은 계산된 최단 경로. 이 둘을 한 클래스에 넣되 **규칙이 구분을 모르게** 하는 것이 설계였다.

방법은 출력 형식을 하나로 고정하는 것이다. 두 모델 모두 `pathCells_` 라는 배열을 낸다. `Path()` 는 그 배열의 셀 중심을 돌려주고, `GameWorld` 는 그것을 `path_` 에 복사해 전진 루프에 넣는다. 전진 산술은 한 벌이다.

구분이 남는 자리는 셋뿐이고 전부 `map_.Generated()` 로 묻는다.

| 자리 | classic | procgen |
|---|---|---|
| `Passable` 에 점유가 들어가는가 | 아니오 | 예 |
| 배치가 연결성 검사를 받는가 | 아니오 | 예 |
| 사격선·잔해가 도는가 | 아니오 | 예 |

세 자리 모두 "아니오" 가 **스위치 이전의 동작**이다. 그래서 `map=classic` 은 12편과 비트 동일하게 돈다. 셀프테스트가 그 사실의 증거다 — `ticks=1377 towers=11 wave=6 packet=175` 는 12편의 값 그대로다.

`DefenseMap` 안에 점유 배열이 하나 늘었다. classic 맵에서는 그 배열이 **비어 있다** — `Occupied()` 가 빈 배열을 먼저 보고 거짓을 돌려주므로, 메모리도 비교도 없다.

`Init(map, ...)` 에는 함정이 하나 있었다. 매치를 다시 시작하면 타워도 잔해도 사라지는데, 맵 사본에 점유가 남아 있으면 유령 벽이 경로 계산에 그대로 반영된다.

<!-- snippet: include/game/defense_map.hpp#ClearOccupancy -->
```cpp
void ClearOccupancy() {
    if (!occupied_.Empty()) occupied_.Assign(static_cast<size_t>(columns_ * rows_), uint8_t{0});
}
```

## 8. 광맥과 명암 — 같은 장에서 나오는 세 가지

밀도장은 지형만 정하지 않는다. 같은 노이즈에서 두 가지가 더 나온다.

**광맥.** Worley F1 이 임계보다 작은 개방 칸(특징점 근처)에 광맥을 심는다. 그 칸에 세운 타워는 데미지 배율 1.25배를 받는다. 41×41 맵 시드 7에서 148칸이 광맥이다.

```cpp
if (mapEnabled_ && map_.Generated()) {
    const DefenseMap::Cell cell = map_.WorldToCell(t.pos);
    if (map_.Vein(cell.x, cell.y)) { t.damage *= kVeinDamageBonus; t.onVein = true; }
}
```

설계안은 광맥을 "보상 배율 칸"으로 적었다. 그대로 실장하지 않았고 이유가 있다 — **보상은 적이 들고 있고 발사체는 자기 타워를 모른다.** 킬 크레딧을 추적하려면 발사체에 타워 id 열을 하나 더 붙여야 하고, 그 열은 매 틱 전수로 복사된다. 같은 "광맥이 이득이다"를 발사체가 이미 들고 있는 값(`damage`)으로 표현했다.

**명암.** 밀도값을 0~255로 사상해 칸마다 저장하고, 타일 색을 그 값으로 흔든다. 격자가 균일한 초록 바둑판이 아니라 결이 있는 지면으로 보인다.

**시드.** 셋 다 같은 시드에서 나오므로 `map_seed=1..8` 하나가 지형·보상·외관을 함께 바꾼다. 그리고 같은 시드는 같은 격자다 — `TestSeedReproducible` 이 타일·광맥·명암 세 배열 전부를 비교한다.

## 9. 지형이 와이어를 건너는 방법

맵 자체는 와이어를 건너지 않는다. 시드와 파라미터가 같으면 같은 격자가 나오기 때문이다.

발파는 다르다. 매치 중에 호스트가 격자를 바꾸므로, 클라이언트가 그 사실을 모르면 뚫린 벽을 계속 그린다.

스냅샷에 실을 수는 없었다. 가득 찬 스냅샷이 이미 1198바이트(`kSnapshotMaxBytes`)라 칸 목록을 얹으면 `static_assert(kSnapshotMaxBytes <= kMaxPacketBytes)` 가 **컴파일 시간에** 깨진다. 그래서 별도 패킷을 만들었다.

<!-- snippet: include/game/defense_protocol.hpp@terrain-packet -->
```cpp
inline net::Bytes WriteTerrain(const std::vector<uint16_t>& cells,
                               const net::PacketHeader& framing = net::PacketHeader()) {
    Writer writer(PacketType::Terrain, framing);
    const size_t count = std::min(cells.size(), kMaxTerrainCells);
    writer.U8(static_cast<uint8_t>(count));
    for (size_t i = 0; i < count; ++i) writer.U16(cells[i]);
    return writer.Finish();
}

inline bool ReadTerrain(const net::Bytes& bytes, std::vector<uint16_t>& cells,
                        net::PacketHeader* framing = nullptr) {
    Reader reader(bytes);
    if (!reader.Header(PacketType::Terrain, framing)) return false;
    const uint8_t count = reader.U8();
    if (static_cast<size_t>(count) > kMaxTerrainCells) return false;
    std::vector<uint16_t> decoded;
    decoded.reserve(count);
    for (uint8_t i = 0; i < count; ++i) decoded.push_back(reader.U16());
    if (!reader.Done()) return false;
    cells = std::move(decoded);
    return true;
}
```

내용은 **누적 전체 목록**이다. 그래서 멱등이고, 유실은 다음 전송이 저절로 메운다 — 신뢰 채널이 필요 없는 이유가 그것이다. 전송 조건은 둘이다.

<!-- snippet: include/game/defense_app.hpp@send-terrain -->
```cpp
void MaybeSendTerrain() {
    const std::vector<uint16_t>& cells = world_.OpenedCells();
    if (cells.empty()) return;
    const bool grew = cells.size() != sentTerrainCells_;
    if (!grew && hostTick_ - lastTerrainTick_ < 180) return;
    network_.Send(wire::WriteTerrain(cells, ackWindow_.Header(outgoingSeq_.Next())));
    sentTerrainCells_ = cells.size();
    lastTerrainTick_ = hostTick_;
    ++terrainPacketsSent_;
}
```

목록이 자란 순간 한 번, 그 뒤로는 3초마다 한 번. 96칸 상한에서 최대 193바이트다.

명령 쪽에는 `CommandType::Blast = 5` 가 늘었다. 인자를 읽지 않는 명령이므로 `CommandArgumentInRange` 는 `argument == 0` 을 요구한다 — 4편이 `Upgrade`/`Sell` 에 세운 규칙 그대로다.

## 10. 변이: 감산을 합집합으로

이 편의 배선을 무력화하는 변이는 하나면 된다. `OpSubtract` 를 `OpUnion` 으로 바꾼다.

```cpp
// const float after = math::OpSubtract(brush.Sd(p), -1.0f);
const float after = math::OpUnion(brush.Sd(p), -1.0f);
```

`OpUnion(a, b) = min(a, b)` 이고 `b` 는 항상 −1 이므로 결과는 언제나 −1 이다. 즉 **어떤 브러시도 아무것도 열지 못한다.** 컴파일은 되고, 발파 명령은 여전히 흘러가고, 아무 일도 일어나지 않는다.

```
[test] terrain_blast
  FAIL tests\test_terrain_blast.cpp:59  CHECK_EQ(result.opened, expected)
  FAIL tests\test_terrain_blast.cpp:60  CHECK_EQ(static_cast<int>(opened.size()), expected)
  FAIL tests\test_terrain_blast.cpp:61  CHECK_EQ(RockCount(map), rockBefore - expected)
  FAIL tests\test_terrain_blast.cpp:68  CHECK_EQ(static_cast<int>(geometry.PrimCount()), rockBefore - expected)
  FAIL tests\test_terrain_blast.cpp:128  CHECK(capsuleResult.opened > sphereResult.opened)
[test] terrain_blast: 201 checks, 5 failures
```

오라클이 전수 순회라는 점이 중요하다. 테스트는 "브러시 SDF 가 음수인 바위 칸"을 직접 세고, 그 수와 `ApplyBlast` 의 반환값이 같은지 본다. 색인(BVH)이 후보를 어떻게 추리든 답은 그 수여야 한다.

두 번째 변이는 배치 규칙 쪽이다. `CanPlaceTower` 의 연결성 검사를 지운다.

```
[test] map_procgen
  FAIL tests\test_map_procgen.cpp:221  CHECK(rejected > 0)
[test] map_procgen: 132 checks, 1 failures
```

이 변이는 처음에 **아무 테스트도 죽이지 못했다.** `TestPlacementConnectivityGate` 가 `DefenseMap::WouldRemainConnected` 를 직접 부르고 있었기 때문이다 — 규칙이 그것을 부르지 않아도 통과한다. `TestWorldRejectsSealingPlacement` 를 추가해 실제 배치 명령을 내리게 하고 나서야 변이가 잡혔다. 헤더의 함수를 시험하는 것과 **제품이 그 함수를 부르는지** 시험하는 것은 다른 일이다.

## 11. 실행과 관측

```
> cmake --build build --config Release --parallel
> ctest -C Release
...
100% tests passed out of 77
```

12편의 68에서 77로 늘었다. 새로 생긴 것은 `test_map_procgen` · `test_terrain_blast` · `test_camera_pick` · `test_camera_rail` · `test_transform_chain` · `test_target_index_ab` · `test_static_bvh_ab` · `test_debris` 와, 제품 실행 파일을 절차 지형으로 한 번 더 도는 `mega_defense_selftest_procgen` 이다.

**기본 팔(classic)이 바뀌지 않았다는 것.**

```
> build\bin\Release\mega_defense.exe --selftest
[content] map=external waves=external (5) balance=script
[selftest] result=CLEAR ticks=1377 towers=11 wave=6 packet=175 protocol=OK render=OK assets=10/10 content=external
```

`ticks=1377 towers=11 wave=6 packet=175` — 12편의 출력과 같은 값이다.

**절차 지형 팔.**

```
> build\bin\Release\mega_defense.exe --selftest 25000 --ab map=procgen
[terrain] seed=7 41x41 maze=799 carved=318 rock=564 veins=148 route=79 bvh_prims=564/nodes=359
[selftest] result=CLEAR ticks=2971 towers=7 wave=6 packet=115 protocol=OK render=OK assets=10/10 content=external
[selftest] terrain route=79 opened=16 debris_live=0 debris_settled=27 target_rebuilds=0
```

읽을 것이 여럿이다.

- `maze=799 carved=318` — 미로가 799칸을 열었고 밀도장이 318칸을 더 깎았다. 개방 칸의 28%가 밀도장에서 왔다.
- `rock=564` — 1681칸 중 3분의 1이 바위로 남았다. 그것이 발파의 대상이고 사격선의 장애물이다.
- `route=79` — 스폰에서 코어까지 79칸. classic 맵(assets/level.txt)의 32칸보다 두 배 이상 길다.
- `opened=16` — 셀프테스트 스크립트가 발파를 실제로 눌렀고 16칸이 뚫렸다.
- `towers=7` — classic 의 11보다 적다. 칸이 10픽셀이라 같은 골드로 세울 수 있는 수가 다르지 않은데도 적은 이유는, 배치 계획이 경로 옆 칸을 훑어 만드는 것이라 경로가 바뀔 때마다 다음 후보가 달라지기 때문이다.

**맵 생성 자체의 성질.**

```
> build\bin\Release\test_map_procgen.exe
[test] map_procgen: 1160 checks, 0 failures
```

1160개의 단언은 대부분 8개 시드 × 41×41 격자를 훑는 것이다 — 시드 재현성(타일·광맥·명암 전부), 경로가 개방 칸만 지나는지, 인접 칸끼리 이어지는지, 테두리가 바위인지.

**발파.**

```
> build\bin\Release\test_terrain_blast.exe
[test] terrain_blast: 201 checks, 0 failures
```

여기에는 두 색인 팔(naive·BVH)이 **같은 칸을 연다**는 단언과, 같은 명령열에서 두 팔의 상태 해시가 같다는 단언이 들어 있다. 발파가 색인을 거치는 연산이므로 그 대조가 없으면 색인이 후보를 놓쳐도 알 수 없다.

---

## 결론

맵이 파일에서 시드로 옮겨 갔고, 그 과정에서 세 헤더가 제품에 들어왔다 — `tools/procgen.hpp`(미로 골격), `math/noise.hpp`(밀도장), `tools/level_editor.hpp`(연결성 검사). 네 번째로 `math/sdf.hpp` 가 발파를 만들었다.

바뀐 것 중 가장 큰 것은 지형이 아니라 **경로의 지위**다. 12편까지 경로는 데이터였다 — 파일이 정하고 매치 내내 불변인 배열. 지금 경로는 계산 결과이고, 타워·잔해·발파가 그 계산의 입력을 바꾼다. 그래서 배치 명령 하나가 맵 전체의 flood fill 을 부르고, 살아 있는 적 전부가 새 배열로 재배치된다.

그 대가로 얻은 것은 메이즈 디펜스라는 게임 규칙이다. 타워가 사거리 안의 적을 쏘는 물건이 아니라 **길을 만드는 물건**이 됐다.

현재 상태의 한계 셋을 적어 둔다.

**경로는 무가중 BFS 다.** 지형에 이동 비용이 없으므로 모든 개방 칸이 같은 비용이고, 그래서 최단 경로는 칸 수 최소다. 진흙·얕은 물 같은 비용 차이는 표현되지 않는다.

**경로가 하나다.** 모든 적이 같은 배열을 걷는다. 적마다 다른 길을 가거나, 개체 수가 수천이 되어 개체별 경로가 비용이 되는 상황은 이 구현에 없다.

**재배치는 최근접 칸이다.** 경로가 바뀌면 적은 새 배열에서 자기 위치와 가장 가까운 칸으로 옮겨 간다. 새 경로가 옛 경로와 크게 다르면 적이 진행 방향을 거스르는 칸으로 배정될 수 있고, 그때 눈에 보이는 것은 "적이 잠깐 뒤로 간다"이다. 진행도를 보존하는 재배치(옛 인덱스의 비율을 새 배열에 사상하는 방식)는 경로 길이가 크게 바뀔 때 더 나쁜 결과를 내서 고르지 않았다.
