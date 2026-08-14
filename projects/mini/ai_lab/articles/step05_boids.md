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

## 3. 이웃 탐색 비용

모든 쌍을 검사하면 개체 수 $n$에서 $O(n^2)$이다. 이 구현은 매 프레임 spatial hash를 다시 만들고 셀 크기를 perception 반경으로 둔다. 그러면 한 Boid는 자신의 셀과 인접한 8개 셀만 조회하면 된다.

```cpp
float cell = std::max(8.0f, perception_);
hash_.clear();
for (int i = 0; i < (int)boids_.size(); ++i) {
    int cx = (int)std::floor(boids_[(size_t)i].pos.x / cell);
    int cy = (int)std::floor(boids_[(size_t)i].pos.y / cell);
    hash_[HashCell(cx, cy)].push_back(i);
}

for (int oy = -1; oy <= 1; ++oy)
    for (int ox = -1; ox <= 1; ++ox) {
        auto it = hash_.find(HashCell(cx + ox, cy + oy));
        if (it == hash_.end()) continue;
        for (int j : it->second) {
            // 거리 제곱으로 perception 안의 실제 이웃만 거른다.
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
ctest --test-dir build -C Release -R ai_model_tests --output-on-failure
./build/Release/ai_model_tests    # 직접 실행하면 "AI model tests passed"
```

```text
1/1 Test #1: ai_model_tests ...................   Passed
100% tests passed out of 1
```

경계 조건 쪽은 이 테스트가 고정한다. 구간을 두 번 이상 넘는 위치(-2 → 2, 27 → 7)도 반사가 구간 안으로 되돌리고 해당 축 속도의 부호를 뒤집으며, 속도 절단은 상한뿐 아니라 하한도 적용한다. 세 규칙의 합성 자체는 Scene 안에 있어 자동 검증 대상이 아니다.

Boids 탭의 기본값은 count 300, perception 36, separation 1.5, alignment 1.0, cohesion 1.0, max speed 140 이다. alignment 와 cohesion 을 0 으로 내리면 개체가 서로 밀어내며 화면 전체로 흩어지고, separation 만 0 으로 내리면 한 점으로 뭉친다. count 를 2000 까지 올리면 하단 푸터의 `ms` 가 뚜렷하게 커지는데, 이때 perception 을 12 로 내리면 개체 수를 그대로 둔 채 다시 내려간다 — 비용을 지배하는 것이 개체 수 자체가 아니라 셀당 이웃 후보 수라는 뜻이다.

Boids의 교훈은 단순 규칙의 아름다움만이 아니다. *행동 규칙보다 이웃을 찾는 자료구조가 먼저 병목이 될 수 있다* 는 점까지 함께 봐야 한다.

---

## 결론

분리, 정렬, 응집을 독립적으로 켜고 가중 합성해 각 규칙의 효과와 상호작용을 관찰할 수 있게 했다. 개체 수와 지각 반경을 바꾸면서 행동 계산보다 이웃 질의가 먼저 지배 비용이 되는 구간도 측정할 수 있다.

현재 공간 해시는 매 프레임 전체를 재구축하고 셀별 벡터 할당을 재사용하지 않는다. 따라서 대규모 군집의 최종 자료구조가 아니며, 높은 밀도에서는 최악의 전쌍 비교에 가까워질 수 있다.
