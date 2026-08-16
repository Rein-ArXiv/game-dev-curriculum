## 들어가며

두 번째 단계는 같은 그리드에서 A*, Dijkstra, Greedy Best-First, JPS를 비교한다. 단순히 경로 선만 그리면 알고리즘 차이가 보이지 않는다. open·closed 셀, 확장 수, 최대 open 크기, 최종 비용을 함께 보여 줘야 탐색 전략의 비용을 읽을 수 있다.

---

## 목차
1. [공정한 비교 조건](#1-공정한-비교-조건)
2. [한 노드씩 진행하는 Pathfinder](#2-한-노드씩-진행하는-pathfinder)
3. [우선순위와 휴리스틱](#3-우선순위와-휴리스틱)
4. [JPS 비교의 경계](#4-jps-비교의-경계)
5. [검증 실험](#5-검증-실험)

---

## 1. 공정한 비교 조건

모든 알고리즘은 같은 이동 그래프를 사용해야 한다.

- 8방향 이동
- 직선 비용 1, 대각선 비용 $\sqrt{2}$
- 코너 컷 허용 여부 통일
- 같은 시작점·목표점·벽

Octile·Euclidean·Chebyshev처럼 이 8방향 비용 모델에서 admissible한 휴리스틱을 사용하면 A*, Dijkstra, JPS는 같은 최단 비용을 내야 한다. Manhattan은 대각 이동의 실제 비용보다 크게 평가할 수 있어 이 동등성 검증에서 제외한다. Greedy는 휴리스틱만 따라가므로 더 긴 경로를 낼 수 있다. 이 차이는 버그가 아니라 비교 대상이다.

## 2. 한 노드씩 진행하는 Pathfinder

`Pathfinder::Reset`은 알고리즘과 휴리스틱을 고르고 시작점을 open에 넣는다. `Step`은 한 번에 노드 하나, JPS에서는 jump point 하나를 확장한다.

Scene 은 `Reset` 을 한 곳에서만 부른다. 알고리즘·휴리스틱·격자 크기·벽이 바뀌면 `needRestart_` 만 세우고, 실제 초기화는 Update 안에서 한 번 일어난다.

<!-- snippet: src/scenes/pathfind_scene.cpp#PathfindScene::Restart -->
```cpp
void PathfindScene::Restart() {
    pf_.Reset(grid_, (Algo)algoIdx_, (Heur)heurIdx_);
    stepAcc_     = 0.0f;
    needRestart_ = false;
    pendingStep_ = false;
    pendingRun_  = false;
}
```

탐색 진행도 한 곳에 모은다. 패널 버튼은 요청 플래그만 세우고 Update 가 그것을 소비하므로, Draw 를 몇 번 호출해도 탐색이 앞으로 가지 않는다. 루프를 한 프레임에 전부 돌리지도 않는다 — `step/s`에 맞춰 일부만 실행해 탐색 과정이 보이게 한다. Run은 자동 진행이고 Step은 정확히 한 번만 확장한다.

<!-- snippet: src/scenes/pathfind_scene.cpp#PathfindScene::Update -->
```cpp
if (needRestart_) Restart();

// 패널 버튼 요청 소비 — 탐색 진행은 이 지점에서만 일어난다.
if (pendingRun_)  { pf_.RunToCompletion(grid_); pendingRun_ = false; }
if (pendingStep_) { pf_.Step(grid_); pendingStep_ = false; }

if (autoRun_ && !pf_.Stats().done) {
    stepAcc_ += stepRate_ * dt;
    int steps = (int)stepAcc_;
    stepAcc_ -= (float)steps;
    for (int i = 0; i < steps && pf_.Step(grid_); ++i) {}
}
```

## 3. 우선순위와 휴리스틱

알고리즘 차이는 open list의 우선순위로 정리된다.

| 알고리즘 | 우선순위 |
|---|---|
| Dijkstra | $g(n)$ |
| Greedy | $h(n)$ |
| A* | $g(n)+h(n)$ |
| JPS | A*와 같지만 대칭 경로를 건너뜀 |

표의 네 줄은 `Priority` 한 함수에 그대로 들어 있다. 알고리즘을 바꾸는 것은 자료구조가 아니라 이 우선순위 식 하나를 바꾸는 일이다.

<!-- snippet: src/pathfind/pathfinder.h#Priority -->
```cpp
float Priority(int idx, Cell c, const Grid& g) const {
    float gv = gScore_[(size_t)idx];
    float hv = Heuristic(c, g.Goal());
    switch (algo_) {
        case Algo::Dijkstra: return gv;
        case Algo::Greedy:   return hv;
        case Algo::AStar:
        case Algo::JPS:
        default:             return gv + hv;
    }
}
```

8방향 비용에서 Octile 휴리스틱은 이동 모델과 잘 맞는다. Manhattan은 대각 이동을 허용하는 그래프에서 비용을 과대평가할 수 있어 A*의 최적성 조건을 깨뜨린다. UI에서 휴리스틱을 바꿀 수 있게 한 이유는 이 차이를 직접 확인하기 위해서다.

## 4. JPS 비교의 경계

JPS의 장점은 균일 비용 그리드의 대칭 경로를 건너뛰는 데 있다. 가중치가 셀마다 다르거나 이동 규칙이 복잡하면 표준 JPS의 가정이 깨진다. 따라서 이 Scene은 균일 비용과 통일된 코너 규칙을 유지한다.

JPS가 맞게 동작하면:

- A*와 경로 비용이 같다.
- 열린 공간에서 expand 수가 크게 줄어든다.
- 복잡한 장애물에서는 감소 폭이 작아질 수 있다.

## 5. 검증 실험

1. 빈 맵에서 A*와 JPS의 경로 비용을 비교한다.
2. 미로형 맵에서 Dijkstra와 A*의 expand 수를 비교한다.
3. Greedy가 빠르지만 더 긴 경로를 고르는 지형을 만든다.
4. Manhattan과 Octile을 바꿔 최적 비용 유지 여부를 본다.
5. 벽을 그린 뒤 Reset 없이 낡은 경로가 남지 않는지 확인한다.

**실행과 관측**

```bash
cmake --build build --config Release
ctest --test-dir build -C Release -R ai_step02_pathfinding --output-on-failure
./build/Release/ai_pathfinder_tests step02   # 같은 열 케이스를 직접 실행
```

ctest 는 통과 여부만, 실행 파일은 어떤 케이스가 돌았는지까지 알려 준다.

```text
1/1 Test #2: ai_step02_pathfinding ............   Passed
100% tests passed out of 1

ai_pathfinder_tests [step02] 10 case(s)
  ok  open-grid/heuristic-equivalence
  ok  forced-neighbors/cardinal
  ok  forced-neighbors/diagonal
  ok  jps-equals-dijkstra/200-maps
  ok  cost-model/absolute
  ok  search-effort/inequalities
  ok  suboptimality/greedy-manhattan
  ok  gui-defaults/enum-order
  ok  unreachable/no-path
  ok  degenerate/start-equals-goal
pathfinder tests passed
```

이 테스트가 고정하는 것은 다섯 가지다.

- **절대 비용 모델.** 빈 24x16 격자의 기본 배치는 순수 수평 19칸이라 네 알고리즘 모두 `pathCost == 19.0`, goal 을 (21,15) 로 옮기면 대각 7 + 직선 12 라 $7\sqrt{2}+12 = 21.899494$ 다. 한 칸 대각 이동만 시키면 $\sqrt{2}$ 그대로다. 알고리즘끼리만 비교하면 대각 비용 상수를 통째로 바꿔도 세 알고리즘이 나란히 틀려 통과하므로, 절대값을 못 박아야 이동 모델이 실제로 검증된다.
- **경로 자체의 계약.** `Path()` 를 직접 읽어 첫 셀 == start, 끝 셀 == goal, 연속 셀이 8-인접(`|dx|<=1 && |dy|<=1`), 모든 셀이 통행 가능, 그리고 경로를 따라 다시 더한 비용이 보고된 `pathCost` 와 1e-3 이내로 같은지 본다. JPS 는 jump point 사이를 직선 보간으로 채우므로 이 검사가 없으면 보간이 깨져도 `pathLen > 0` 만으로 통과한다.
- **JPS ≡ Dijkstra.** 30x22 격자에 밀도 0.28 로 시드 1..200 의 벽을 깔고, 두 알고리즘의 `found` 와 `pathCost` 가 모두 일치하는지 본다. 대각 forced neighbor 규칙을 지우면 시드 1 에서 27.48528 대 28.31371 로 즉시 갈라진다. 대각 통로 옆에 벽 한 칸을 붙인 16x10 고정 맵도 같은 규칙을 겨냥한다.
- **탐색 비용의 부등식.** 빈 48x48 에서 `jps.expanded < astar.expanded <= dijkstra.expanded` 이고 세 알고리즘의 `maxOpen` 이 모두 양수다. 32x20 수직벽 맵에서도 같다.
- **비허용 휴리스틱의 비최적성.** 48x48 시드 1385 맵에서 Greedy 49.62742 > Dijkstra 46.31371, 30x22 시드 7 맵에서 A*/Manhattan 29.97056 > Dijkstra 29.14213 이다. 빈 격자에서는 Greedy 도 최적이므로 그때는 `>=` 만 요구한다.

두 번째 항목 — 경로 자체의 계약 — 이 실제로 어떻게 적히는지는 볼 만하다. 위 열 케이스가
전부 이 검사를 거친다.

<!-- snippet: tests/pathfinder_tests.cpp@path-contract -->
```cpp
CHECK_CTX((int)path.size() == stats.pathLen, context);
CHECK_CTX(path.front() == grid.Start(), context);
CHECK_CTX(path.back() == grid.Goal(), context);

float traced = 0.0f;
for (size_t i = 0; i < path.size(); ++i) {
    CHECK_CTX(grid.Walkable(path[i].x, path[i].y), context);
    if (i == 0) continue;
    const int dx = std::abs(path[i].x - path[i - 1].x);
    const int dy = std::abs(path[i].y - path[i - 1].y);
    CHECK_CTX(dx <= 1 && dy <= 1, context);   // 연속 셀은 8-인접
    CHECK_CTX(dx + dy > 0, context);          // 제자리 반복 금지
    traced += (dx != 0 && dy != 0) ? kDiagonalCost : 1.0f;
}
CHECK_CTX(Near(traced, stats.pathCost, 1e-3f), context);
```

마지막 줄이 이 블록의 값이다. `pathCost` 를 보고서 그대로 믿는 대신 셀을 하나씩 밟아
다시 더한 값과 맞춘다. 탐색기가 비용을 잘못 세거나 JPS 의 jump point 사이 보간이 깨지면
두 숫자가 갈라지고, `context` 에 맵 이름과 알고리즘 이름이 붙어 있어 200 개 맵 중
어느 것이 깨졌는지 바로 나온다.

GUI 는 48x48 격자에 Heuristic 을 Octile 로 두고 읽는다. 벽이 없는 초기 상태에서 Algorithm 만 바꾸면 `expanded` 가 Dijkstra 1946, A* 44, Greedy 44, JPS 2 로 갈라지고 `path cost` 는 넷 다 43.00 으로 같다. `Random walls` 는 격자 크기로 정해지는 고정 시드를 쓰므로 48x48 에서는 몇 번을 눌러도 같은 맵이 나오고, 그 맵에서는 `expanded` 가 Dijkstra 1535, A* 186, JPS 109, Greedy 44 이며 `path cost` 는 Dijkstra·A*·JPS 가 46.31 로 같은 반면 Greedy 만 49.63 으로 크다. 확장 수가 가장 적은 설정이 가장 짧은 경로를 주지 않는다는 사실이 한 화면에서 읽힌다.

이 단계의 핵심은 “A*가 빠르다”가 아니라, *휴리스틱과 그래프 가정이 속도와 최적성을 어떻게 교환하는지* 수치로 확인하는 것이다.

---

## 결론

동일한 그리드와 이동 규칙에서 Dijkstra, A*, Greedy, JPS를 한 노드씩 진행하며 경로 비용과 확장 노드 수를 비교할 수 있게 했다. 휴리스틱의 허용성과 일관성이 최적 경로 보장에 어떤 조건으로 연결되는지도 실험 기준에 포함했다.

현재 비교는 정적 격자와 고정 비용 모델을 전제로 한다. 동적 장애물, 비균일 이동 비용에서의 JPS 제약, 시간에 따라 변하는 그래프는 이 결과만으로 평가할 수 없다.
