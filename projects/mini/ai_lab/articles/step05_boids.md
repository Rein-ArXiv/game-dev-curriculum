## 들어가며

Boids는 중앙 지휘자 없이 세 가지 국소 규칙만으로 무리 행동을 만든다. Separation은 가까운 이웃에게서 멀어지고, Alignment는 이웃 속도에 맞추며, Cohesion은 이웃 중심으로 향한다. 이 단계는 규칙의 조합과 이웃 탐색 비용을 함께 관찰한다.

---

## 목차
1. [세 가지 규칙](#1-세-가지-규칙)
2. [가중 합성과 제한](#2-가중-합성과-제한)
3. [이웃 탐색 비용](#3-이웃-탐색-비용)
4. [경계 처리](#4-경계-처리)
5. [검증 실험](#5-검증-실험)

---

## 1. 세 가지 규칙

각 개체는 perception 반경 안의 이웃만 본다.

- Separation: 거리의 역수에 비례해 밀어냄
- Alignment: 이웃 평균 속도로 맞춤
- Cohesion: 이웃 평균 위치로 이동

이웃이 없으면 세 벡터는 0이어야 한다. 자기 자신을 이웃에 포함하면 평균이 왜곡되므로 반드시 제외한다.

## 2. 가중 합성과 제한

세 규칙은 가중 합으로 steering에 들어간다.

$$
\mathbf{f}=w_s\mathbf{f_s}+w_a\mathbf{f_a}+w_c\mathbf{f_c}
$$

개별 규칙을 먼저 과도하게 clamp하면 가중치의 의미가 달라지므로, 이 구현은 합성한 가속도를 그대로 적분하고 절단은 속도 한 단계에서만 한다. 절단은 상한 maxSpeed와 하한 maxSpeed*0.3의 두 값을 함께 쓴다. 하한이 있으면 이웃이 없거나 세 규칙이 서로 상쇄돼도 개체가 멈추지 않고 무리가 계속 흐른다. 대신 힘 자체에는 상한이 없으므로 가중치를 크게 올리면 한 프레임 안에서 방향이 급격히 꺾인다.

<!-- snippet: src/scenes/boids_scene.cpp#BoidsScene::Update -->
```cpp
Vector2 v = Add(b.vel, Mul(acc, dt));
const auto limited =
    LimitSpeed({v.x, v.y}, maxSpeed_ * 0.3f, maxSpeed_);
v = {limited.x, limited.y};
```

## 3. 이웃 탐색 비용

모든 쌍을 검사하면 개체 수 $n$에서 $O(n^2)$이다. 이 구현은 매 프레임 spatial hash를 다시 만들고 셀 크기를 perception 반경으로 둔다. 그러면 한 Boid는 자신의 셀과 인접한 8개 셀만 조회하면 된다.

<!-- snippet: src/scenes/boids_scene.cpp#BoidsScene::Update -->
```cpp
float cell = std::max(8.0f, perception_);
hash_.clear();
for (int i = 0; i < (int)boids_.size(); ++i) {
    int cx = (int)std::floor(boids_[(size_t)i].pos.x / cell);
    int cy = (int)std::floor(boids_[(size_t)i].pos.y / cell);
    hash_[HashCell(cx, cy)].push_back(i);
}
// ... 개체마다 자기 셀 (cx, cy) 를 구한 뒤
for (int oy = -1; oy <= 1; ++oy)
    for (int ox = -1; ox <= 1; ++ox) {
        auto it = hash_.find(HashCell(cx + ox, cy + oy));
        if (it == hash_.end()) continue;
        for (int j : it->second) {
            // ... 거리 제곱으로 perception 안의 실제 이웃만 거른다.
        }
    }
```

균등 분포이고 셀당 개체 수가 제한되면 구축은 $O(n)$이고 질의는 $O(n+k)$에 가깝다. 모든 개체가 한 셀에 몰리면 여전히 $O(n^2)$이므로 spatial hash가 최악 복잡도를 없애는 것은 아니다. count와 perception을 늘리면 이웃 밀도와 프레임 비용이 함께 커지는 것을 확인할 수 있다.

## 4. 경계 처리

화면 경계에서는 위치를 반사하고 해당 축의 속도 부호를 뒤집는다. 이웃 거리도 비주기 유클리드 공간에서 계산하므로 경계 표현과 질의 공간이 일치한다. 큰 `dt`로 경계를 여러 번 넘을 수 있어 `ReflectAxis`는 한 번 clamp하는 대신 구간 안으로 반복 반사한다.

## 5. 검증 실험

1. separation만 켜서 개체가 균일하게 퍼지는지 본다.
2. alignment만 켜서 방향은 맞지만 무리가 모이지 않는지 본다.
3. cohesion만 켜서 한 점으로 붕괴하려는지 본다.
4. 세 가중치를 조합해 안정된 무리를 만든다.
5. count와 perception을 올려 프레임 비용의 증가를 측정한다.

**실행과 관측**

```bash
cmake --build build --config Release
ctest --test-dir build -C Release -R ai_step05_boids --output-on-failure
./build/Release/ai_model_tests step05   # 같은 두 케이스를 직접 실행
```

```text
1/1 Test #5: ai_step05_boids ..................   Passed
100% tests passed out of 1

ai_model_tests [step05] 2 case(s)
  ok  boids/reflect-axis
  ok  boids/speed-limits
AI model tests passed
```

두 케이스가 이 단계 소속인 이유는 호출부에 있다. `ReflectAxis` 와 하한 인자를 받는
`LimitSpeed` 는 `BoidsScene::Update` 만 부른다 — 국소 제어 Scene 은 자기 감쇠 반사를
따로 쓰고 힘 단계에서 절단한다. 이 단계가 요구해서 생긴 두 함수이므로 검증도 여기 있다.

경계 조건은 `boids/reflect-axis` 가 고정한다.

<!-- snippet: tests/ai_model_tests.cpp#TestReflectAxis -->
```cpp
void TestReflectAxis() {
    float position = -2.0f;
    float velocity = -4.0f;
    ailab::ReflectAxis(position, velocity, 0.0f, 10.0f);
    CHECK(Near(position, 2.0f));
    CHECK(Near(velocity, 4.0f));

    position = 27.0f;
    velocity = 4.0f;
    ailab::ReflectAxis(position, velocity, 0.0f, 10.0f);
    CHECK(Near(position, 7.0f));
    CHECK(Near(velocity, 4.0f));

    position = 5.0f;
    velocity = 3.0f;
    ailab::ReflectAxis(position, velocity, 2.0f, 2.0f);
    CHECK(Near(position, 2.0f));
    CHECK(Near(velocity, 0.0f));
}
```

두 번째 블록이 반복 반사를 겨냥한다. 27 은 구간 [0, 10] 을 두 번 넘은 위치라 한 번만
clamp 하는 구현은 10 을 돌려주지만, 반복 반사는 27 → -7 → 7 로 접어 넣는다. 큰 `dt` 로
개체가 화면 밖 멀리 나가는 것이 이 lab 에서 실제로 일어나는 일이므로 이 차이가 화면에
그대로 나타난다. 세 번째 블록은 구간 폭이 0 인 퇴화 입력에서 무한 루프에 빠지지 않고
속도를 죽이는지 본다. `boids/speed-limits` 는 `LimitSpeed({3,4}, 2, 4)` 가 방향비 3:4 를
유지한 채 크기만 4 로 줄이고, 하한 2 가 느린 개체를 다시 밀어 올리는지 확인한다.

세 규칙의 합성 자체는 Scene 안에 있어 자동 검증 대상이 아니다.

Boids 탭의 기본값은 count 300, perception 36, separation 1.5, alignment 1.0, cohesion 1.0, max speed 140 이다. alignment 와 cohesion 을 0 으로 내리면 개체가 서로 밀어내며 화면 전체로 흩어지고, separation 만 0 으로 내리면 한 점으로 뭉친다. count 를 2000 까지 올리면 하단 푸터의 `ms` 가 뚜렷하게 커지는데, 이때 perception 을 12 로 내리면 개체 수를 그대로 둔 채 다시 내려간다 — 비용을 지배하는 것이 개체 수 자체가 아니라 셀당 이웃 후보 수라는 뜻이다.

Boids의 교훈은 단순 규칙의 아름다움만이 아니다. *행동 규칙보다 이웃을 찾는 자료구조가 먼저 병목이 될 수 있다* 는 점까지 함께 봐야 한다.

---

## 결론

분리, 정렬, 응집을 독립적으로 켜고 가중 합성해 각 규칙의 효과와 상호작용을 관찰할 수 있게 했다. 개체 수와 지각 반경을 바꾸면서 행동 계산보다 이웃 질의가 먼저 지배 비용이 되는 구간도 측정할 수 있다.

현재 공간 해시는 매 프레임 전체를 재구축하고 셀별 벡터 할당을 재사용하지 않는다. 따라서 대규모 군집의 최종 자료구조가 아니며, 높은 밀도에서는 최악의 전쌍 비교에 가까워질 수 있다.
