# 20. 따라갈 선이 없다

## 들어가며

13편이 절차 지형을 넣기 전까지 이 게임의 적은 **선 위를 걸었다**. `assets/level.txt` 가 스폰에서 코어까지 무분기 회랑 하나를 저작했고, 맵 로더는 그 회랑이 정말로 분기 없는 한 줄인지 검사했으며, 적은 그 선의 점을 차례로 밟았다. 경로 탐색이 없었던 것이 아니라 **탐색할 것이 없었다.**

13편이 그 전제를 절반 깼다. 절차 맵은 개방 지형이라 저작된 회랑이 없고, 그래서 `DefenseMap::RecomputeRoute` 가 스폰→코어 BFS 로 폴리라인을 **계산**하게 됐다. 계산된 것도 여전히 선 하나다. 그 하나로는 답할 수 없는 질문이 넷 있다.

1. **적마다 다른 곳에 있다.** 경로 배열의 어느 점부터 걸어야 하는지를 매번 최근접 탐색으로 다시 찾아야 한다.
2. **타워가 칸을 막으면 선 전체가 무효가 된다.** 메이즈 디펜스의 배치 규칙이 바로 그 조작인데, 그때마다 전원의 자리를 다시 매긴다.
3. **시야 판정이 "벽이 가로막는가"를 물으려면 셀 단위 통행 가능성이 필요하다.** 폴리라인에는 셀이 없다.
4. **위치가 경로의 함수인 한 다른 힘이 끼어들 자리가 없다.**

세 헤더가 그 자리를 가져간다.

| 헤더 | 하는 일 |
|---|---|
| `ai/pathfinding/grid_map.hpp` | 통행 가능성 격자 — 타일·타워 점유·안착 잔해를 접은 하나의 표현 |
| `ai/pathfinding/astar.hpp` | 개체별 경로 탐색. 동시에 **필드의 오라클** |
| `ai/pathfinding/flow_field.hpp` | 목표 하나를 공유하는 벡터장. 개체는 자기 칸을 읽기만 한다 |

스위치는 `pathing=poly|astar|flowfield` 셋이다. 기존 폴리라인 팔은 지우지 않았다 — 저작된 회랑을 밟는 것이 classic 맵의 계약이고, 그 계약을 지키는 팔이 없으면 두 격자 팔이 "옳은가"를 물을 기준이 사라진다.

이 편의 판정은 둘이다. **같은 격자에서 두 팔이 같은 비용의 경로를 내는가**(정확성), 그리고 **지형이 초당 여러 번 바뀔 때 두 팔의 비용이 어디에서 갈리는가**(비용).

---

## 목차
1. [폴리라인이 답하지 못하는 것](#1-폴리라인이-답하지-못하는-것)
2. [통행 가능성이라는 하나의 질문](#2-통행-가능성이라는-하나의-질문)
3. [두 맵에서 통행 가능성의 정의가 다르다](#3-두-맵에서-통행-가능성의-정의가-다르다)
4. [칸 하나만 고친다](#4-칸-하나만-고친다)
5. [두 팔 — 캐시된 열과 공유 필드](#5-두-팔--캐시된-열과-공유-필드)
6. [A\* 가 필드의 오라클이다](#6-a-가-필드의-오라클이다)
7. [동점 규칙을 고정하지 않으면 결정성이 무너진다](#7-동점-규칙을-고정하지-않으면-결정성이-무너진다)
8. [길이 남아 있는가 — 소비하지 않는 질문](#8-길이-남아-있는가--소비하지-않는-질문)
9. [교차점은 어디인가](#9-교차점은-어디인가)
10. [변이 둘](#10-변이-둘)
11. [실행과 관측](#11-실행과-관측)

---

## 1. 폴리라인이 답하지 못하는 것

폴리라인 팔의 전진은 이런 모양이다. 개체가 `pathIndex` 라는 열을 들고, 그 자리의 점을 향해 걷고, 남은 거리만큼 다음 점으로 넘어간다.

<!-- snippet: include/game/game.hpp@enemy-advance -->
```cpp
void EnemyAdvanceSystem(float dt) {
```

이 구조에서 지형이 바뀌면 무슨 일이 일어나는지가 `RefreshRoute` 에 그대로 적혀 있다. 새 경로를 받고, **살아 있는 모든 개체에 대해** 경로의 모든 점을 훑어 가장 가까운 자리를 다시 찾는다.

<!-- snippet: include/game/game.hpp@refresh-route -->
```cpp
void RefreshRoute() {
    const std::vector<math::Vec2> route = map_.Path();
    path_.AssignFrom(route.data(), route.size());
```

개체 수 x 경로 길이다. 그리고 이 계산이 답하는 질문은 "이 개체는 선의 어디쯤에 있는가" 하나뿐이라, 타워가 막았는지도 벽이 시야를 가리는지도 여기서는 물을 수 없다.

문제는 비용이 아니라 **표현**이다. 선은 "어디로 갈 수 있는가"에 답하는 자료구조가 아니라 "어디로 가라"를 지시하는 자료구조다. 앞의 넷은 전부 앞쪽 질문이다.

## 2. 통행 가능성이라는 하나의 질문

그래서 통행 가능성을 한 자료구조가 소유하게 한다. `ai::path::GridMap` 은 셀당 1바이트짜리 배열이고, 인터페이스는 넷뿐이다 — `Init` · `Walkable` · `SetWalkable` · `InBounds`.

이 단순함이 요점이다. 경로 탐색기도, 필드 빌더도, 시야 레이캐스트도 **같은 하나**를 읽는다. 세 소비자가 각자 "통행 가능"을 다시 정의하면 그 순간 셋이 서로 다른 세계를 본다.

`game/nav.hpp` 의 `NavGrid` 가 그 격자를 게임 좌표에 잇는 어댑터다. 그리고 격자를 **둘** 든다.

```cpp
const ai::path::GridMap& Grid() const { return grid_; }
const ai::path::GridMap& SightGrid() const { return sight_; }
```

같은 자료구조의 두 번째 사본을 두는 이유는 두 격자가 서로 다른 질문에 답하기 때문이다. 타워는 걸어 지나갈 수 없지만 레이더의 **시선**까지 막지는 않는다. 하나로 합쳐 두면 레이더가 자기 자신에게 가려 아무것도 보지 못한다. 실제로 그 상태를 만들었고, 은신 개체가 한 틱도 드러나지 않았다.

## 3. 두 맵에서 통행 가능성의 정의가 다르다

이 파일에서 가장 중요한 열 줄이다.

<!-- snippet: include/game/nav.hpp@nav-walkable -->
```cpp
static bool WalkableIn(const DefenseMap& map, int x, int y) {
    if (!map.InBounds(x, y)) return false;
    if (map.Generated()) return map.Passable(x, y);
    const TileType tile = map.Tile(x, y);
    return tile == TileType::Path || tile == TileType::Spawn || tile == TileType::Core;
}
static bool TransparentIn(const DefenseMap& map, int x, int y) {
    return map.InBounds(x, y) && !map.Rock(x, y);
}
```

절차 맵에서는 `DefenseMap::Passable` 그대로다 — 바위가 아니고 점유되지 않았으면 어디로든 간다. 메이즈 디펜스의 규칙 그 자체다.

Classic 맵에서는 **저작된 회랑만** 통행 가능하다. 개방 칸(`Buildable`)을 통행 가능으로 두면 격자 팔의 적이 회랑을 무시하고 코어로 직진하고, 그러면 같은 맵 파일이 팔마다 다른 게임이 된다.

이 정의 덕분에 classic 회랑에서 최단 경로가 **유일**해지고, 세 팔(poly · astar · flowfield)이 같은 셀 열을 밟는다. 그 성질이 "두 팔이 같은 답을 낸다"를 제품 안에서 물을 수 있게 만드는 유일한 장치다. 라이브러리 단위 테스트로는 "A* 와 필드의 비용이 같다"까지만 물을 수 있고, "게임이 같은 게임인가"는 물을 수 없다.

## 4. 칸 하나만 고친다

매치 중 지형은 초당 여러 번 바뀐다. 타워 배치·판매, 발파, 잔해 안착 — 그런데 그 변경은 **언제나 칸 하나**다. 전체 재생성은 매치 시작과 발파처럼 여러 칸이 한꺼번에 바뀔 때만 쓴다.

<!-- snippet: include/game/nav.hpp@nav-sync-cell -->
```cpp
bool SyncCell(const DefenseMap& map, int x, int y) {
    if (!grid_.InBounds(x, y)) return false;
    sight_.SetWalkable(x, y, TransparentIn(map, x, y));
    const bool want = WalkableIn(map, x, y);
    if (grid_.Walkable(x, y) == want) return false;
    grid_.SetWalkable(x, y, want);
    ++version_;
    return true;
}
```

돌려주는 값이 "무언가 실제로 뒤집혔는가"라는 것이 이 함수의 설계다. 호출자는 그 값으로 "필드를 다시 지어야 하는가"를 판단한다. 바뀌지 않은 갱신에 재빌드를 걸면, 잔해가 **이미 막힌 칸에** 또 안착할 때마다 필드를 헛되이 다시 짓는다.

`version_` 은 격자가 실제로 바뀔 때만 오른다. 두 팔의 무효화가 전부 이 수 하나를 본다.

<!-- snippet: include/game/game.hpp@nav-system -->
```cpp
void NavSystem() {
    if (!GridNav() || !mapEnabled_) return;
    MEGA_PROFILE_SCOPE("sim.nav");
    navField_.Sync(navGrid_);
    if (navVersion_ != navGrid_.Version()) {
        navVersion_ = navGrid_.Version();
        if (navField_.Kind() == PathingKind::AStar) navField_.InvalidatePaths();
    }
```

여기 네 줄에 두 팔의 비용 구조가 전부 있다. 필드 팔은 **한 번** 다시 짓고, A* 팔은 개체 경로를 **전부 버린다** — 버린 경로는 각 개체가 자기 차례에 다시 찾으므로 비용이 개체 수만큼 흩어진다.

## 5. 두 팔 — 캐시된 열과 공유 필드

`NavField::NextCell` 이 두 팔이 갈리는 유일한 함수다.

<!-- snippet: include/game/nav.hpp@nav-next-cell -->
```cpp
bool NextCell(const NavGrid& nav, size_t row, const NavCell& from, NavCell& out) {
    if (from == nav.Core()) return false;                 // 코어에 닿았다
    if (kind_ == PathingKind::FlowField) {
        const math::Vec2 d = field_.Direction(from.x, from.y);
        if (d.x == 0.0f && d.y == 0.0f) return false;     // 도달 불가
        out = {from.x + StepOf(d.x), from.y + StepOf(d.y)};
        return nav.Walkable(out.x, out.y);
    }
```

인자 `from` 이 개체의 **현재 목표 칸**이지 개체가 서 있는 칸이 아니라는 것이 중요하다. 서 있는 칸을 쓰면 스폰 지터로 흔들린 위치가 첫 칸 선택을 바꾸고, classic 회랑에서 두 팔이 다른 열을 낸다. 목표 칸을 쓰면 두 팔의 셀 열이 스폰부터 동일하다.

필드 쪽 갱신은 조건이 둘뿐이다.

<!-- snippet: include/game/nav.hpp@nav-field-sync -->
```cpp
void Sync(const NavGrid& nav) {
    if (kind_ != PathingKind::FlowField) return;
    if (fieldVersion_ == nav.Version() && builds_ > 0) return;
    field_.Build(nav.Grid(), nav.Core().x, nav.Core().y);
    fieldVersion_ = nav.Version();
    ++builds_;
}
```

`builds_ > 0` 이 붙어 있는 이유는 첫 프레임이다. 매치 시작 시 `version_` 은 1이고 `fieldVersion_` 도 0이 아닌 값으로 초기화될 수 있는데, 그때 첫 빌드를 건너뛰면 전원이 영벡터를 읽어 아무도 움직이지 않는다.

## 6. A\* 가 필드의 오라클이다

두 팔은 **서로 다른 최단 경로를 고를 수 있다**. 격자에 같은 비용의 경로가 여러 개 있으면 A* 는 우선순위 큐가 먼저 편 쪽을, 필드는 이웃 순회가 먼저 만난 쪽을 고른다. 그래서 "경로가 같다"는 성립하지 않는 대조다.

성립하는 대조는 **비용**이다. 그 비교를 하려면 경로에서 비용을 다시 뽑을 수 있어야 하고, 그것이 `AStar::PathCost` 가 존재하는 유일한 이유다.

<!-- snippet: include/ai/pathfinding/astar.hpp@path-cost -->
```cpp
static float PathCost(const std::vector<int>& cells, int width) {
    const float kSqrt2 = 1.41421356f;
    float cost = 0;
    for (size_t i = 1; i < cells.size(); ++i) {
        const int px = cells[i - 1] % width, py = cells[i - 1] / width;
        const int cx = cells[i] % width,     cy = cells[i] / width;
        cost += (px != cx && py != cy) ? kSqrt2 : 1.0f;
    }
    return cost;
}
```

두 상수(`1` 과 `sqrt2`)는 `FindPath` 가 `g` 를 쌓을 때 쓰는 것과 **같아야** 한다. 다르면 이 함수는 비교기가 아니라 두 번째 정의가 된다.

오라클 검사는 무작위 격자 200장에서 돈다. 12~25칸 정사각에 전체 칸의 1/4을 벽으로 찍고, 좌상단에서 우하단으로 A* 를 돌린 뒤 그 경로의 비용을 같은 격자의 필드 적분 비용과 맞춘다. 도달 불가 격자도 일부러 섞는다 — 두 팔의 **도달 가능성 판정이 같은가**까지 물어야 오라클이 완성된다.

## 7. 동점 규칙을 고정하지 않으면 결정성이 무너진다

필드의 벡터는 "비용이 가장 낮아지는 이웃"을 가리킨다. 그 이웃이 여럿일 때 어느 쪽을 고르는가는 순회 순서가 정한다.

```cpp
for (int d = 0; d < 8; ++d) {
    ...
    float c = cost_[m.Index(nx, ny)];
    if (c < best) { best = c; bx = nx; by = ny; }
}
```

`<` 이지 `<=` 가 아니다. 동점이면 **먼저 만난 방향이 이긴다**. 그래서 `dx`/`dy` 배열의 순서가 게임 규칙이 된다 — 그 순서를 흔들면 개방 지형의 수많은 동점 칸에서 방향이 갈리고, 같은 시드로 두 번 돌린 매치가 다른 세계가 된다.

이 사실은 협동 모드에서 특히 중요하다. 호스트와 클라이언트는 서로 다른 프로세스에서 같은 격자를 짓는데, 필드 빌드가 결정적이지 않으면 스냅샷 보간으로 덮이지 않는 차이가 생긴다.

그래서 이 순서를 **테스트가 잠근다**. 두 팔로 각각 매치를 돌린 상태 지문이 같아야 하고, 지형을 바꾼 직후 필드를 다시 짓지 않으면 그 지문이 갈려야 한다.

## 8. 길이 남아 있는가 — 소비하지 않는 질문

격자 항법이 들어오면서 새 질문 하나가 생긴다. **앞이 막혔는가.** 타워로 회랑을 닫는 것이 메이즈 디펜스의 핵심 조작이므로, 막힌 적이 되돌아가는 대신 앞의 타워를 때려야 한다.

그 판정은 "다음 칸"과 다르다. 다음 칸을 얻는 것은 커서를 하나 미는 **소비**이고, 막혔는지 묻는 것은 소비 없는 질의여야 한다.

<!-- snippet: include/game/nav.hpp@nav-has-route -->
```cpp
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
```

필드 팔에서 이 질문은 `Reachable` 조회 한 번이다. A* 팔에서는 **이 질문이 곧 탐색**이라, 찾은 결과를 캐시에 남긴다. 같은 틱의 `NextCell` 이 그것을 그대로 쓰므로 한 틱에 두 번 찾는 일은 없다.

두 팔의 비대칭이 여기서 가장 노골적이다. 필드 팔에는 공짜인 질문이 A* 팔에는 전체 탐색이다.

## 9. 교차점은 어디인가

두 팔의 비용 구조는 이렇게 갈린다.

| | 지형이 안 바뀔 때 | 지형이 바뀔 때 |
|---|---|---|
| astar | 개체당 한 번 찾고 캐시. 이후 공짜 | 개체 수만큼의 재탐색 |
| flowfield | 필드 한 장. 개체당 조회 한 번 | 필드 재빌드 **한 번** |

그래서 교차점은 "개체 수"가 아니라 **"개체 수 x 무효화 빈도"** 가 정한다. 개체가 적고 지형이 자주 바뀌면 필드가, 개체가 많고 지형이 고정이면 A* 가 유리할 여지가 있다.

측정은 그 두 축을 실제로 흔들어 본다. 600틱을 돌리면서 60틱마다 발파를 한 번씩 넣고(= 지형이 초당 한 번 바뀐다), 적 수를 바꿔 가며 두 팔의 벽시계 시간과 각각의 재계산 횟수를 나란히 잰다. 결과는 아래 §11 에 있다.

## 10. 변이 둘

**변이 1 — `GridMap::SetWalkable` 의 경계 검사를 지운다.**

```cpp
void SetWalkable(int x, int y, bool v) { cells_[Index(x, y)] = v ? 1 : 0; }
```

`Index(x, y)` 는 `y * w + x` 다. 그래서 `x = -1` 은 **앞 줄의 마지막 칸**을 가리킨다. 경계 밖 쓰기가 오류가 되는 대신 멀쩡한 칸을 조용히 오염시키고, 그 칸은 맵 어디에도 근거가 없는 벽이 된다. `test_ai_pathfinding` 의 `TestOutOfBoundsIsSafe` 가 이 변이를 죽인다.

**변이 2 — `AStar::Heuristic` 을 비허용(inadmissible)으로 만든다.**

```cpp
return (mx + (1.41421356f - 1.0f) * mn) * 1.5f;   // 실제 비용보다 크게
```

A* 는 여전히 경로를 찾고, 그 경로도 여전히 통행 가능한 칸만 지난다. 다만 **최단이 아니다**. 휴리스틱이 실제 비용을 넘기면 최적성 보장이 사라지기 때문이다. 통행 가능성만 확인하는 테스트는 이 변이를 통과시킨다. 죽이는 것은 오라클 단언 — A* 경로 비용과 필드 적분 비용의 일치 — 하나뿐이고, 이것이 "두 팔을 서로의 검사기로 쓴다"의 실질이다.

## 11. 실행과 관측

**격자와 두 알고리즘의 단위 계약.**

```
> build\bin\Release\test_ai_pathfinding.exe
[test] ai_pathfinding
[test] ai_pathfinding: 42 checks, 0 failures
```

**오라클과 두 팔의 비용.**

```
> build\bin\Release\test_pathing_ab.exe
[test] pathing_ab
  무작위 격자 200장: 비교 156 · 도달불가 44 · 최대 비용 차 0.000008
  poly      지문=c73222962df8ced6 타워=6 코어=500.0 2.59ms
  astar     지문=c73222962df8ced6 타워=6 코어=500.0 1.17ms (탐색 59회)
  flowfield 지문=c73222962df8ced6 타워=6 코어=500.0 1.06ms (재빌드 1회)
  필드 재빌드: 시작 0 → 60틱 1 → 타워 배치 뒤 2
  지형이 바뀔 때(600틱, 60틱마다 발파 1회)
    적   64  astar   323.9ms (탐색   2176)  flowfield    99.5ms (재빌드 34)  비 3.25x
    적  256  astar  1028.9ms (탐색   8973)  flowfield   106.7ms (재빌드 36)  비 9.64x
    적 1024  astar  3789.6ms (탐색  25759)  flowfield   227.2ms (재빌드 32)  비 16.68x
[test] pathing_ab: 4494 checks, 0 failures
```

읽을 것이 넷 있다.

**`도달불가 44`.** 무작위로 벽을 1/4 찍으면 다섯 장 중 하나는 좌상단과 우하단이 끊긴다. 그 44장에서도 두 팔의 판정이 갈리지 않았다는 것이 오라클의 절반이다.

**`최대 비용 차 0.000008`.** 156장 전부에서 A* 경로 비용과 필드 적분 비용이 일치했다. 차이는 `sqrt2` 를 float 로 여러 번 더한 데서 오는 것이고, 단언 임계 `1e-3` 에 두 자릿수 여유가 있다.

**`지문=c73222962df8ced6` 셋이 같다.** classic 회랑에서 세 팔이 900틱 동안 비트 단위로 같은 세계를 돌았다. `poly` 가 더 느린 것(2.59ms 대 1.17ms)은 `RefreshRoute` 의 개체 x 경로 재매핑 때문이고, 이 맵에서는 격자 팔이 그 비용을 아예 치르지 않는다.

**교차점은 이 워크로드에 없다.** 지형이 초당 한 번 바뀌면 A* 팔의 비용이 적 수에 정비례해 오르고(64 → 1024 에서 324ms → 3790ms, 11.7배), 필드 팔은 거의 평평하다(99.5ms → 227.2ms). 재빌드 횟수가 32~36 으로 고정인 것이 그 이유다 — 발파 횟수가 같으니 필드가 다시 지어지는 횟수도 같다. 반면 A* 팔의 탐색 횟수는 2176 → 25759 로 개체 수를 따라간다.

한 가지 더. `적 1024` 에서도 재빌드가 32회인데 탐색이 25759회다. 개체당 25회꼴이고, 600틱 동안 발파가 10회 있었으니 개체당 무효화 2.5회 + 초기 탐색이다. **무효화 한 번의 값이 팔마다 세 자릿수 배 다르다**는 것이 이 표의 요약이다.

**필드 재빌드가 조건대로만 일어난다.**

`시작 0 → 60틱 1 → 타워 배치 뒤 2`. 60틱을 돌아도 재빌드는 처음 한 번뿐이고, 타워를 세워 칸 하나가 막힌 뒤에야 두 번째가 일어난다. `SyncCell` 이 "실제로 뒤집혔는가"를 돌려주지 않으면 이 수가 60을 넘는다.

**제품 — A\* 팔.**

```
> build\bin\Release\mega_defense.exe --selftest 25000 --ab map=procgen --ab pathing=astar --ab elite_brain=fsm
[selftest] result=CLEAR ticks=5085 towers=6 wave=6 packet=104 protocol=OK render=OK assets=10/10 content=external stomps=6
[selftest] ai field_builds=0 searches=544 melee=0 slams=1 reveal=0
[selftest] terrain route=79 opened=16 debris_live=0 debris_settled=26 target_rebuilds=0
```

**제품 — 필드 팔.**

```
> build\bin\Release\mega_defense.exe --selftest 25000 --ab map=procgen --ab pathing=flowfield --ab steering=1 --ab stealth=1 --ab elite_brain=bt
[terrain] seed=7 41x41 maze=799 carved=318 rock=564 veins=148 route=79 bvh_prims=564/nodes=359
[selftest] result=CLEAR ticks=4921 towers=10 wave=6 packet=164 protocol=OK render=OK assets=10/10 content=external stomps=3
[selftest] ai field_builds=40 searches=0 melee=0 slams=3 reveal=122
[profile] frame=8090.50/11.117ms(n=4921) render.draw=7648.17/6.605ms(n=4921) sim.step=341.55/0.895ms(n=4921) sim.nav=183.19/0.851ms(n=4920) ...
```

두 줄이 서로의 여집합이다. A* 팔은 `field_builds=0 searches=544`, 필드 팔은 `field_builds=40 searches=0`. 이 두 수가 제품 안에서 그 팔이 실제로 돌았다는 유일한 증거이고, 배선이 끊기면 둘 다 0이 된다.

`sim.nav` 가 총 183.19ms / 4920틱 = 틱당 0.037ms 다. 프레임의 대부분은 `render.draw`(7648ms)이고, 항법은 그 2.4% 다. **경로를 자료구조로 바꾼 대가는 이 게임의 규모에서 렌더 비용에 묻힌다.**

**변이 확인.**

`AStar::Heuristic` 을 1.5배로 부풀리면(비허용) 경로는 여전히 찾아지고 통행 가능한 칸만 지나지만 최단이 아니다. 오라클 단언이 `최대 비용 차` 를 임계 위로 밀어 죽는다. `GridMap::SetWalkable` 의 `InBounds` 를 지우면 `TestOutOfBoundsIsSafe` 의 `CHECK(m.Walkable(4, 1))` 이 즉시 죽는다 — `x = -1` 쓰기가 앞 줄의 마지막 칸을 벽으로 만들기 때문이다.

---

## 결론

폴리라인에서 격자로 옮긴 것의 실질은 **자료구조 하나가 질문 하나를 소유하게 한 것**이다. "어디로 갈 수 있는가"에 답하는 것이 통행 격자 하나뿐이므로, 경로 탐색기와 필드 빌더와 시야 레이캐스트가 서로 다른 세계를 볼 수 없다.

두 팔을 둘 다 남긴 이유는 성능이 아니라 검사다. 필드는 개체 수와 무관하게 한 번 짓고 모두가 읽는 구조라 명백히 유리한 자리가 있지만, 그것이 **옳은지**는 그 자신이 증명하지 못한다. A* 는 노드 단위로 최적성이 논증되는 고전 알고리즘이라 오라클이 될 수 있고, 그래서 이기지 못하는 팔을 남겨 두는 것이 비용이 아니라 장치다.

측정에서 나온 것 둘.

**동점 규칙이 게임 규칙이다.** 필드 빌드의 이웃 순회 순서가 개방 지형의 수많은 동점 칸에서 방향을 정한다. 알고리즘의 정확성에는 아무 영향이 없고 — 어느 방향을 골라도 최단이다 — 결정성에는 전부다.

**"칸 하나만 바꾼다"가 재빌드 정책의 전제다.** `SyncCell` 이 실제 변경 여부를 돌려주지 않으면 잔해가 이미 막힌 칸에 안착할 때마다 필드를 다시 짓는다. 그 낭비는 프로파일에서 `sim.nav` 구간의 튐으로만 보이고, 원인은 격자가 아니라 잔해 쪽에 있어 찾기 어렵다.

현재 상태의 한계 셋.

**A* 팔은 개체마다 `std::vector<int>` 를 하나씩 든다.** 경로 캐시가 적 상한만큼 잡히고, 무효화될 때마다 그 벡터들이 비워진다. 셀 인덱스를 담는 고정 크기 링으로 바꾸면 할당이 사라지지만, 경로 길이 상한을 게임 규칙으로 정해야 한다.

**필드는 목표가 하나일 때만 성립한다.** 코어가 둘이 되거나 적이 서로 다른 목표를 갖게 되면 필드도 그만큼 필요하고, 그 순간 "한 장이면 전원이 읽는다"는 이점이 사라진다. A* 를 남겨 둔 두 번째 이유가 이것이다.

**격자는 셀 단위라 잔해의 실제 모양을 표현하지 못한다.** 파편이 칸의 귀퉁이만 걸쳐도 그 칸 전체가 막힌다. 통행 가능성을 부분 점유율로 두면 표현은 정확해지지만, 그 순간 "통행 가능한가"가 참·거짓이 아니게 되어 A* 와 필드가 모두 실수 비용을 다시 정의해야 한다.
