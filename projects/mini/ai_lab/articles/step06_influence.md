## 들어가며

Influence Map은 월드를 “누가 어디에 있는가”가 아니라 “어디가 얼마나 유리하거나 위험한가”라는 연속 값으로 바꾼다. 이 단계에서는 stamp, spread, decay를 반복하고 Control·Tension·Vulnerability 뷰를 비교한다.

---

## 목차
1. [값의 의미](#1-값의-의미)
2. [Stamp·Spread·Decay](#2-stampspreaddecay)
3. [파생 뷰](#3-파생-뷰)
4. [업데이트 예산](#4-업데이트-예산)
5. [검증 실험](#5-검증-실험)

---

## 1. 값의 의미

아군은 양의 영향력, 적군은 음의 영향력을 찍는다. 셀의 부호는 우세한 진영을, 절댓값은 영향력 크기를 나타낸다. 값 자체가 명령은 아니다. AI는 이 장을 읽어 안전한 경로, 공격할 약점, 방어할 지역을 고른다.

## 2. Stamp·Spread·Decay

`InfluenceScene::Step`은 아군과 적, 두 필드에 같은 순수 함수를 각각 적용한다. 두 필드는 서로를 읽지 않으므로 한 장짜리 함수로 나눌 수 있고, 그래야 창 없이 검사할 수 있다.

<!-- snippet: src/scenes/influence_scene.cpp#InfluenceScene::Step -->
```cpp
void InfluenceScene::Step() {
    const int W = grid_.Width(), H = grid_.Height();
    const InfluenceParams params{decay_, spread_, iters_};
    const auto walkable = [&](int x, int y) { return grid_.Walkable(x, y); };

    StepInfluenceField(W, H, fField_, friendly_, params, walkable);
    StepInfluenceField(W, H, eField_, enemy_, params, walkable);
}
```

`StepInfluenceField`는 필드 하나를 세 단계로 진행한다.

1. **Decay**: 오래된 영향력을 줄인다.
2. **Stamp**: 현재 유닛 위치에 새 값을 주입한다.
3. **Spread**: 이웃 셀로 값을 확산한다.

순서를 고정해야 같은 입력에서 같은 결과가 나온다. spread를 제자리 갱신하면 순회 방향에 따라 결과가 달라지므로, 읽기 버퍼와 쓰기 버퍼를 분리한다.

<!-- snippet: src/model/influence.h#StepInfluenceField -->
```cpp
// 1) decay (과거 망각).
for (float& v : field) v *= params.decay;

// 2) stamp (현재 유닛 강도 주입).
for (const auto& c : stamps) {
    if (c.x < 0 || c.y < 0 || c.x >= width || c.y >= height) continue;
    field[(size_t)(c.y * width + c.x)] = 1.0f;
}

// 3) spread (벽 우회, 거리 감쇠 — 반복 확산).
static const int dx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
static const int dy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
std::vector<float> next;
for (int i = 0; i < params.iterations; ++i) {
    next = field;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int idx = y * width + x;
            // 벽 셀은 값을 보존하지 않는다. 이 분기가 없으면 벽이
            // stamp/decay 로 얻은 값을 다시 뿌려 "벽 우회"가 깨진다.
            if (!walkable(x, y)) { next[(size_t)idx] = 0.0f; continue; }
            float best = field[(size_t)idx];
            for (int k = 0; k < 8; ++k) {
                const int nx = x + dx[k], ny = y + dy[k];
                if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
                if (!walkable(nx, ny)) continue;
                const float in = field[(size_t)(ny * width + nx)] * params.spread;
                if (in > best) best = in;
            }
            next[(size_t)idx] = best;
        }
    }
    field.swap(next);
}
```

## 3. 파생 뷰

- **Control**: $C=f-e$
- **Tension**: $T=2\min(f,e)$. 양쪽 영향력이 동시에 큰 접경 지역
- **Vulnerability**: $V=e(1-f)$. 적 영향력이 크고 아군 영향력이 약한 지역

같은 원본 장에서 목적별 파생 값을 만들 수 있다는 점이 핵심이다. 한 숫자를 모든 의사결정에 그대로 쓰기보다 질문에 맞는 함수를 정의한다.

<!-- snippet: src/model/ai_model.h#CombineInfluence -->
```cpp
switch (view) {
case InfluenceView::Control:
    return f - e;
case InfluenceView::Tension:
    return 2.0f * std::min(f, e);
case InfluenceView::Vulnerability:
    return e * (1.0f - f);
}
```

## 4. 업데이트 예산

큰 맵을 매 프레임 여러 번 확산하면 비용이 크다. 실전에서는 낮은 빈도로 갱신하거나, 변경된 지역만 처리하거나, 여러 프레임에 분산한다. `iters` 슬라이더는 한 번의 갱신에서 확산을 얼마나 멀리 보낼지와 비용을 동시에 바꾼다.

## 5. 검증 실험

1. 아군과 적군 하나씩 배치해 control의 부호를 확인한다.
2. 두 세력이 맞닿는 곳에서 tension이 커지는지 본다.
3. decay를 높여 사라진 유닛의 영향력이 얼마나 빨리 지워지는지 본다.
4. spread 반복 수와 프레임 비용을 비교한다.
5. 같은 배치에서 Step 순서와 버퍼 방식이 결정론적인지 확인한다.

**실행과 관측**

```bash
cmake --build build --config Release
ctest --test-dir build -C Release -R ai_step06_influence --output-on-failure
./build/Release/ai_model_tests step06   # 같은 두 케이스를 직접 실행
```

```text
1/1 Test #6: ai_step06_influence ..............   Passed
100% tests passed out of 1

ai_model_tests [step06] 2 case(s)
  ok  influence/derived-views
  ok  influence/field-step
AI model tests passed
```

필터를 빼고 `./build/Release/ai_model_tests` 를 그냥 부르면 이 실행 파일에 쌓인 열 개
케이스가 전부 돈다. 지금까지 만든 계산 코어가 한 번에 초록불인지 보고 싶을 때 쓴다.

이 테스트는 파생 뷰 세 개의 값을 고정한다. $f=0.8$, $e=0.2$ 에서 control 0.6, tension 0.4, vulnerability 0.04 이고, $f=0$·$e=1$ 이면 vulnerability 가 1.0 로 최대가 된다.

decay → stamp → spread 루프도 함께 검증한다. 두 세력의 장은 서로를 읽지 않으므로 한 장짜리 순수 함수 `StepInfluenceField` 로 나누어 두었고, Scene 은 그것을 아군·적에 각각 적용한다. 검사 항목은 네 가지다.

- **확산 거리와 감쇠.** decay 1.0, spread 0.5 로 (3,3) 에 유닛 하나를 찍으면 iters 1 에서 이웃이 정확히 0.5, 거리 2 는 0 이고 iters 2 에서 거리 2 가 0.25 다. 반복 횟수가 곧 도달 거리이고 감쇠는 $spread^{거리}$ 다. 이 항목은 이렇게 적힌다.

<!-- snippet: tests/ai_model_tests.cpp@influence-spread-distance -->
```cpp
{
    std::vector<float> field((size_t)(W * H), 0.0f);
    const std::vector<ailab::GridPoint> stamps{{3, 3}};
    ailab::StepInfluenceField(W, H, field, stamps,
                              ailab::InfluenceParams{1.0f, 0.5f, 1}, openGrid);
    CHECK(Near(at(field, 3, 3), 1.0f));
    CHECK(Near(at(field, 4, 3), 0.5f));
    CHECK(Near(at(field, 4, 4), 0.5f));
    CHECK(Near(at(field, 5, 3), 0.0f));
}
{
    std::vector<float> field((size_t)(W * H), 0.0f);
    const std::vector<ailab::GridPoint> stamps{{3, 3}};
    ailab::StepInfluenceField(W, H, field, stamps,
                              ailab::InfluenceParams{1.0f, 0.5f, 2}, openGrid);
    CHECK(Near(at(field, 4, 3), 0.5f));
    CHECK(Near(at(field, 5, 3), 0.25f));
    CHECK(Near(at(field, 6, 3), 0.0f));
}
```

  `at(field, 5, 3)` 이 첫 블록에서 0 이고 둘째 블록에서 0.25 라는 두 줄이 붙어 있어야
  `iterations` 가 실제로 소비되는지 잡힌다. 한 블록만 보면 반복 수를 무시하고 항상 한 번만
  확산하는 구현도, 항상 격자 전체를 채우는 구현도 통과한다. 대각 이웃 (4,4) 가 직교 이웃과
  같은 0.5 인 것은 이 확산이 8방향 등방성이라는 뜻이다 — 거리 가중이 없다.
- **감쇠 계수.** 유닛을 걷어낸 다음 틱에 값이 정확히 decay 배로 줄어든다.
- **벽.** x=3 열을 벽으로 막고 (1,3) 에 유닛을 둔 채 10틱을 돌리면 벽 셀은 전부 0, 벽 너머도 전부 0 이다. stamp 는 통행 가능 여부를 보지 않으므로 벽 위 (3,2) 에 유닛을 놓아도 확산 단계의 0 초기화가 그 값을 지워야 한다.
- **결정론.** 같은 배치·설정으로 12틱을 두 번 돌린 결과 벡터가 완전히 같고, 모든 값이 $[0, 1]$ 안에 있다.

Influence 탭의 기본값은 40x40 격자, decay 0.92, spread 0.62, iters 2 이고 갱신은 30Hz 고정 틱이다. 좌클릭으로 아군, 우클릭으로 적을 하나씩 놓으면 Control 뷰에서 아군 주변이 초록, 적 주변이 붉은색으로 칠해지고 두 세력 사이에 색이 사라지는 경계선이 생긴다. 그 상태에서 Tension 뷰로 바꾸면 바로 그 경계 띠만 밝게 남는다. `Walls rnd` 로 벽을 깔면 벽 셀은 값이 0 으로 유지되어 영향력이 벽을 통과하지 못하고 열린 통로로만 번진다. iters 를 1 에서 6 으로 올리면 같은 유닛 배치에서 색이 번지는 반경이 넓어진다.

이 단계로 `ai_lab`은 탐색, 다수 이동, 국소 제어, 군집, 전술 장을 한 도구 안에서 비교한다. 공통 질문은 하나다. *AI가 결정을 내리기 전에 어떤 상태를 계산하고, 그 계산 비용을 어떻게 관찰할 것인가?*

---

## 결론

세력의 stamp, 확산, 감쇠를 분리하고 같은 원본 장에서 control과 tension 같은 목적별 파생 값을 계산하도록 구성했다. 이중 버퍼와 고정 순서를 사용해 같은 배치와 설정에서 결과가 재현되는지도 검증한다.

현재 모델은 단일 스칼라 격자의 등방성 확산이며 가시성, 지형 차폐, 이동 시간 같은 의미론을 포함하지 않는다. 따라서 색이 그럴듯하다는 이유만으로 전술 판단의 정확성을 주장할 수 없고, 장의 값이 표현하는 가정을 먼저 고정해야 한다.
