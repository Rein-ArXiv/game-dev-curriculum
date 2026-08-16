## 들어가며

개별 A*는 에이전트마다 경로를 찾는다. 목표가 하나이고 에이전트가 수백 명이라면 같은 계산을 반복한다. Flow Field는 목표에서 모든 셀까지의 비용을 한 번 계산하고, 각 셀에 가장 낮은 비용 방향을 저장해 다수가 공유한다.

---

## 목차
1. [통합 비용장](#1-통합-비용장)
2. [방향장 생성](#2-방향장-생성)
3. [에이전트 이동](#3-에이전트-이동)
4. [비용 모델과 재빌드](#4-비용-모델과-재빌드)
5. [검증 실험](#5-검증-실험)

---

## 1. 통합 비용장

`FlowFieldScene::Rebuild`는 목표 셀에서 시작하는 Dijkstra를 수행한다. 각 셀의 integration 값은 목표까지의 누적 최소 비용이다.

```text
goal = 0
이웃 = 이동 비용
그 이웃 = 이전 최소 비용 + 이동 비용
```

Dijkstra 자체는 raylib을 모르는 순수 함수 `BuildIntegrationField`에 있다. Scene은 격자 크기와 통행 판정, goal 좌표만 넘긴다 — 그래서 같은 필드 생성 코드를 창 없이 검사할 수 있다.

<!-- snippet: src/scenes/flowfield_scene.cpp#FlowFieldScene::Rebuild -->
```cpp
Cell goal = grid_.Goal();
integration_ = BuildIntegrationField(
    W, H, [&](int nx, int ny) { return grid_.Walkable(nx, ny); }, goal.x, goal.y);
```

도달 불가능한 셀은 무한대로 남는다. 이 값을 히트맵으로 그리면 장애물 뒤에서 비용이 우회하며 커지는 모습이 보인다.

## 2. 방향장 생성

각 셀은 이웃의 integration 값만 비교하지 않는다. 현재 셀에서 이웃으로 이동하는 간선 비용까지 더한 값이 가장 작은 방향을 고른다.

$$
\mathbf{d}(c)=\operatorname{normalize}\left(
\operatorname{center}\left(\arg\min_{n\in N(c)}
\left[w(c,n)+I(n)\right]\right)-\operatorname{center}(c)\right)
$$

대각선 비용이 $\sqrt{2}$이고 직교 이동 비용이 1이라면 이 항을 생략했을 때 더 낮은 이웃 값만 보고 실제로는 비싼 대각선을 선택할 수 있다. 구현은 선택한 후보가 현재 비용보다 커지는 낡은 장에서도 0 방향을 반환한다.

<!-- snippet: src/scenes/flowfield_scene.cpp#FlowFieldScene::Rebuild -->
```cpp
const GridDirection step = SelectFlowDirection(
    x, y,
    [&](int nx, int ny) { return grid_.Walkable(nx, ny); },
    [&](int nx, int ny) {
        return integration_[(size_t)grid_.Index(nx, ny)];
    });
Vector2 dir{(float)step.dx, (float)step.dy};
flow_[(size_t)idx] = Norm(dir);
```

목표에서는 방향이 0이고, 도달 불가능한 셀도 이동 벡터를 만들지 않는다. 화살표 표시를 켜면 비용장의 기울기를 직접 볼 수 있다.

## 3. 에이전트 이동

에이전트는 현재 위치가 속한 셀의 방향을 읽고 속도를 갱신한다. 여기에 separation을 더해 같은 셀로 몰린 개체가 겹치지 않게 한다.

Flow Field는 전역 경로 방향을 제공하지만 국소 충돌 회피까지 해결하지는 않는다. separation을 너무 높이면 목표 방향보다 밀어내기가 강해져 진동하고, 너무 낮으면 에이전트가 겹친다.

## 4. 비용 모델과 재빌드

벽·목표·그리드 크기가 바뀌면 `dirty_`를 세우고 다음 Update 또는 Draw 전에 Rebuild한다. 매 프레임 무조건 재빌드하면 동적 월드에서 비용이 폭증한다.

실전에서는 다음 전략을 고른다.

- 목표와 지형이 정적이면 한 번 빌드
- 작은 변경은 영향 영역만 점진 갱신
- 다수 목표는 목표 그룹별 field 공유

## 5. 검증 실험

1. 좁은 통로를 만들고 integration 히트맵이 우회 비용을 반영하는지 본다.
2. 에이전트 수를 늘려도 field 빌드 횟수가 늘지 않는지 확인한다.
3. separation을 0과 큰 값으로 바꿔 겹침과 진동을 비교한다.
4. 목표를 막아 도달 불가능 셀의 방향이 0인지 확인한다.
5. 벽 편집 직후 낡은 방향장을 따라 벽으로 들어가지 않는지 확인한다.

**실행과 관측**

```bash
cmake --build build --config Release
ctest --test-dir build -C Release -R ai_step03_flowfield --output-on-failure
./build/Release/ai_model_tests step03   # 같은 두 케이스를 직접 실행
```

```text
1/1 Test #3: ai_step03_flowfield ..............   Passed
100% tests passed out of 1

ai_model_tests [step03] 2 case(s)
  ok  flow/direction-edge-cost
  ok  flow/integration-field
AI model tests passed
```

두 케이스는 이 단계가 들여온 `model/flow_field.h` 와 `SelectFlowDirection` 만 겨냥한다.
같은 실행 파일에 다른 단계의 케이스도 들어 있지만 `step03` 필터가 그것들을 뺀다 — 그래서
여기서 초록불이 뜨면 그 초록불은 방금 만든 것에 대한 초록불이다.

이 테스트가 고정하는 것은 방향 선택 규칙과 필드 생성 둘 다다.

방향 선택은 `SelectFlowDirection` 이다. 대각 이웃의 integration 값이 더 작아도 $\sqrt{2}$ 간선 비용을 더하면 직교 이웃이 싸지는 배치에서 `(dx,dy)=(1,0)` 이 나와야 하고, 목표 셀과 값이 무한대인 셀에서는 `(0,0)` 이 나와야 한다.

필드 생성은 `BuildIntegrationField` 다. Scene 이 raylib 없이 부를 수 있는 순수 함수로 두었기 때문에 헤드리스로 검사할 수 있다. 빈 8x6 격자에서 goal (0,0) 기준 값이 octile 거리와 정확히 같아야 하고((2,2) 는 $2\sqrt{2}$, (7,5) 는 $2+5\sqrt{2}$), x=4 열을 통째로 막으면 벽 셀과 벽 너머가 전부 무한대로 남아야 하며, x=4 열에 (4,5) 한 칸만 뚫으면 (7,0) 의 비용이 우회를 반영해 $3+7\sqrt{2}=12.899$ 가 되어야 한다.

우회 검사가 왜 부등호로 끝나면 안 되는지가 이 케이스의 요점이다.

<!-- snippet: tests/ai_model_tests.cpp@integration-field-detour -->
```cpp
const float detour = at(corridor, 7, 0);
CHECK(std::isfinite(detour));
CHECK(detour > at(open, 7, 0) + 1e-3f);
// 구멍 (4,5) 를 반드시 지난다. 해석해:
//   (0,0)→(4,5) = 1 + 4√2,  (4,5)→(7,0) = 2 + 3√2,  합 = 3 + 7√2.
CHECK(Near(at(corridor, 4, 5), 1.0f + 4.0f * 1.41421356f, 1e-4f));
CHECK(Near(detour, 3.0f + 7.0f * 1.41421356f, 1e-3f));
```

세 번째 줄만 있으면 "우회가 직선보다 비싸다"는 사실만 잡히고, 대각 비용 상수를 1 로
바꿔도 통과한다. 뒤 두 줄이 해석해를 그대로 못 박아 이동 비용 모델 자체를 검증 범위에
넣는다.

GUI 는 FlowField 탭에서 40x40 격자, 에이전트 200으로 시작한다. `show arrows` 를 켜면 도달 가능한 셀마다 화살표가 하나씩 보이고, 히트맵은 goal 에서 멀수록 붉어진다. `Random walls` 로 벽을 깔면 벽 뒤 셀의 색이 직선 거리보다 더 먼 쪽으로 밀리고(우회 비용), 벽으로 완전히 둘러싸인 영역은 색도 화살표도 없다(무한대). 에이전트 슬라이더를 0에서 1000까지 올려도 필드는 다시 계산되지 않는다 — 재빌드는 벽·격자 크기가 바뀔 때만 일어난다.

Flow Field의 이득은 “경로를 더 잘 찾는다”가 아니라, *동일 목표를 향하는 다수가 탐색 결과를 공유한다* 는 데 있다.

---

## 결론

역방향 비용 전파로 통합 비용장을 만들고, 그 기울기에서 방향장을 얻어 다수 에이전트가 하나의 탐색 결과를 공유하도록 구성했다. 에이전트 수와 장 재빌드 횟수를 분리해 측정하므로 공유 계산이 유리해지는 조건을 직접 확인할 수 있다.

현재 구현은 지형 변화 때 전체 장을 다시 계산하며, 국소 충돌 회피도 단순 separation에 의존한다. 따라서 잦은 동적 변경이나 좁은 통로의 교착에서 안정적인 해를 보장하지 않는다.
