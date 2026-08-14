## 들어가며

현재까지의 결과물은 SoA·Archetype에 SSE·AVX2 경로를 박아 *수동 SIMD*로 처리량을 높인 측정 도구다. SIMD 경로는 분기를 *마스크 + select* 로 풀어 분기 자체를 제거했다. 한편 Scalar 경로에는 여전히 *명시적 if 분기* 가 있어 *분기 예측* 의 영향을 그대로 받는다. 분기 패턴이 불규칙하면 분기 예측 미스가 ms를 늘리고, 패턴이 규칙적이면 거의 비용이 없다.

이 단계의 목표는 *분기 자체* 를 토글로 만들어 같은 워크로드에서 분기 처리 방식을 비교하는 것이다. 토글 OFF는 명시적 if, 토글 ON은 *predication* (조건을 ternary로 풀어 컴파일러가 cmov 또는 마스크로 변환하도록). 같은 결과를 두 방식으로 처리한 ms 차이가 그래프에 그려진다.

이 단계의 결과물은 다음 셋이다.

1. SoA·Archetype·AoS의 `UpdateScalar` 가 `branchless` 인자를 받아 두 경로(if 분기 / predication)로 갈라진다.
2. SIMD 경로는 이미 마스크 처리라 `branchless` 토글에 영향받지 않는다.
3. HUD의 `branchless` 체크박스가 활성화된다.

이 글의 코드 스니펫은 *해당 단계의 스냅샷* 이다. 디스크의 통합 코드는 누적되어 더 진행된 상태일 수 있다.

---

## 목차
1. [분기 예측의 비용과 측정 의도](#1-분기-예측의-비용과-측정-의도)
2. [Predication: 분기 없이 ternary로](#2-predication-분기-없이-ternary로)
3. [Scalar 경로의 두 변종](#3-scalar-경로의-두-변종)
4. [SIMD 경로는 영향받지 않는다](#4-simd-경로는-영향받지-않는다)
5. [Hud 활성화와 빌드·실행 결과](#5-hud-활성화와-빌드실행-결과)

---

## 1. 분기 예측의 비용과 측정 의도

현대 CPU는 *분기 예측* 으로 파이프라인 stall을 줄인다. 분기 명령이 들어오기 전에 *어느 쪽으로 갈지* 추측하고 미리 명령을 가져온다. 추측이 맞으면 추가 비용이 작고, 틀리면 *파이프라인 flush + 재시작* 으로 보통 15~20 사이클 손해다.

분기 예측의 *적중률* 은 분기 패턴에 의존한다.

| 분기 패턴 | 예측 결과 | 비용 |
|---|---|---|
| 항상 같은 방향 (true 또는 false 한쪽으로) | 매우 높은 적중률 | 작음 |
| 주기적 (T/F/T/F/T/F...) | 예측기가 학습하면 높음 | 작음 |
| 길게 같은 방향 후 전환 | 전환 시점만 미스 | 작음 |
| 무작위 | 50% 미스 | 미스 × 15~20 cycle |

이 도구의 경계 반사 분기는 *대부분 false* 다. 1만 개 점 중 매 프레임 *경계에 닿는 점* 은 전체의 1% 미만. CPU는 거의 항상 false로 예측해 적중률 99% 이상이 나오고, 분기 비용은 측정에서 거의 안 드러난다.

그렇다면 *predication* 토글은 무엇을 보여 주는가?

- **OFF (if 분기)**: 분기 예측 적중 → 빠름. 다만 컴파일러가 *분기 부분을 자동 벡터화* 못 할 수 있어 SoA scalar의 자동 벡터화가 부분적이다.
- **ON (predication)**: 분기 자체를 ternary로 풀어 *branchless 코드 생성* 을 유도한다. 컴파일러가 cmov, blend, 마스크 select로 변환하기 쉬워진다. 자동 벡터화가 *전체 루프에 적용* 될 가능성이 커진다.

즉 이 토글의 진짜 효과는 *분기 예측 영향* 만이 아니라 *컴파일러 자동 벡터화 가능성의 차이* 다. predication 형태가 branchless 코드로 내려가면 scalar 토글이지만 *내부적으로 SIMD에 가까운* 처리가 된다.

이 도구의 측정 결과로 다음 셋 중 하나가 보인다.

- predication ON이 OFF보다 *눈에 띄게 빠름* — 컴파일러가 자동 벡터화 차이를 만든 케이스.
- 거의 같음 — 컴파일러가 if 분기도 잘 풀었거나, 분기 빈도가 낮아 차이가 묻힌 케이스.
- predication ON이 *살짝 느림* — 자동 벡터화에 차이가 없고 ternary의 *양쪽 모두 계산* 비용만 보인 케이스.

세 결과 모두 *학습 가치* 가 있다. 도구는 본인 머신의 컴파일러가 어떤 결과를 내는지를 그 자리에서 보여 준다.

## 2. Predication: 분기 없이 ternary로

같은 동작을 두 방식으로 적는다.

명시적 분기:

```cpp
if (posX_[i] < 0.0f || posX_[i] > maxX) {
    velX_[i] = -velX_[i];
    posX_[i] = std::clamp(posX_[i], 0.0f, maxX);
}
```

Predication (branchless 코드 생성을 유도하는 형태):

```cpp
const bool outX = (posX_[i] < 0.0f) || (posX_[i] > maxX);
velX_[i] = outX ? -velX_[i] : velX_[i];   // ternary → cmov/blend/마스크 후보
posX_[i] = std::clamp(posX_[i], 0.0f, maxX);  // 항상 실행
```

두 차이.

1. **분기 제거 유도**: predication 코드는 컴파일러가 분기 대신 조건부 이동, blend, 마스크 select를 쓰도록 만들기 쉽다. 실제 분기 명령이 사라지는지는 컴파일러와 최적화 옵션을 확인해야 한다.
2. **clamp 항상 실행**: 명시적 분기에서는 경계 안일 때 `clamp` 가 호출되지 않지만, predication에서는 항상 호출된다. 컴파일러가 `std::clamp` 를 `min(max(...))` 형태로 잘 낮추면 비용은 작지만, 이것도 구현과 옵션에 의존한다.

어셈블리 수준에서 ternary는 보통 다음 중 하나로 변환된다.

- **cmov** (조건부 이동): `cmovz`, `cmovnz` 등. 분기 없이 결과를 선택.
- **bitwise mask + andn + or**: SIMD에서 자주 쓰이는 패턴. SSE2 bitwise select와 같은 형태.
- **여전히 분기**: 컴파일러가 분기가 더 빠르다고 판단한 경우 (드뭄).

실제로 어떤 형태로 변환되는지는 godbolt에서 확인 가능하다. ternary가 branchless로 내려갈 수도 있고, 컴파일러가 분기가 더 낫다고 판단하면 여전히 분기를 남길 수도 있다.

## 3. Scalar 경로의 두 변종

`UpdateScalar` 가 `branchless` 인자를 받아 두 변종을 갈라낸다.

```cpp
void SoASimulation::UpdateScalar(float dt, bool branchless) {
    const float w = static_cast<float>(viewportW_);
    const float h = static_cast<float>(viewportH_);
    const float maxX = w - 1.0f;
    const float maxY = h - 1.0f;

    if (branchless) {
        // Predication 경로
        for (std::size_t i = 0; i < count_; ++i) {
            posX_[i] += velX_[i] * dt;
            posY_[i] += velY_[i] * dt;

            const bool outX = (posX_[i] < 0.0f) || (posX_[i] > maxX);
            const bool outY = (posY_[i] < 0.0f) || (posY_[i] > maxY);

            velX_[i] = outX ? -velX_[i] : velX_[i];
            velY_[i] = outY ? -velY_[i] : velY_[i];
            posX_[i] = std::clamp(posX_[i], 0.0f, maxX);
            posY_[i] = std::clamp(posY_[i], 0.0f, maxY);
        }
    } else {
        // 명시적 분기 경로
        for (std::size_t i = 0; i < count_; ++i) {
            posX_[i] += velX_[i] * dt;
            posY_[i] += velY_[i] * dt;

            if (posX_[i] < 0.0f || posX_[i] > maxX) {
                velX_[i] = -velX_[i];
                posX_[i] = std::clamp(posX_[i], 0.0f, maxX);
            }
            if (posY_[i] < 0.0f || posY_[i] > maxY) {
                velY_[i] = -velY_[i];
                posY_[i] = std::clamp(posY_[i], 0.0f, maxY);
            }
        }
    }
}
```

`if (branchless)` 자체는 함수 진입 시 *한 번만* 평가되는 조건이라 핫 루프 안의 분기 비용에 영향을 주지 않는다. 함수 진입 후 두 루프 중 하나만 끝까지 실행된다.

Archetype과 AoS도 같은 패턴이다. 차이는 핫 루프 본체가 SoA 대신 chunk 외부 루프 + chunk 내부 루프(Archetype) 또는 struct 배열 루프(AoS)로 바뀌는 것뿐이다.

### 외부 분기의 의도

`if (branchless)` 를 함수 *바깥* 에 두지 않고 안에 두는 이유: 호출자(`Update`)에서 이미 `simd` 분기를 처리하고 있어 추가 분기를 호출자로 옮기면 dispatch 코드가 복잡해진다. Update가 매 프레임 한 번 호출되므로 안에서 한 번 더 분기해도 비용은 무시 수준이다.

## 4. SIMD 경로는 영향받지 않는다

SSE/AVX2 경로는 이미 *마스크 + bitwise select* 로 분기를 제거한 형태다. `branchless` 토글이 *추가로 영향* 을 줄 부분이 없다.

```cpp
// SSE 경로의 마스크 처리 (기존 코드에서 이미 작성됨)
const __m128 mX = _mm_or_ps(_mm_cmplt_ps(px, zero), _mm_cmpgt_ps(px, wMax));
vx = SseSelect(vx, _mm_sub_ps(zero, vx), mX);
```

이게 본질적으로 ternary predication을 SIMD lane으로 펼친 형태다. `branchless == true` 와 동등한 처리를 *항상* 하고 있다.

호출 측에서는 `branchless` 인자를 SIMD 경로에 *전달하지 않는다*. 시그니처 일관성을 위해 받아둔 인자를 명시적으로 무시하는 게 코드의 의도를 분명히 한다.

```cpp
case SimdWidth::SSE:
#if defined(PERF_LAB_X86_SIMD)
    UpdateSSE(dt);                  // SIMD는 이미 마스크 처리, branchless 무관
#else
    UpdateScalar(dt, branchless);   // 비-x86 폴백은 scalar이므로 branchless 적용
#endif
    break;
```

폴백 경로에서는 `branchless` 가 다시 의미를 갖는다. Apple Silicon에서 SSE 토글을 선택하면 scalar로 떨어지고, 그 안에서 predication 토글이 효과를 본다.

## 5. Hud 활성화와 빌드·실행 결과

기존 코드에서 branchless 체크박스는 grayed out이었다. `GuiSetState(STATE_DISABLED)` 두 줄을 빼서 활성화한다.

```cpp
// src/ui/hud.cpp 의 branchless 체크박스 부분

bool branchless = toggles.branchless;
GuiCheckBox(Rect(x2 + kLabelW, rowY2(row2), kRowH, kRowH),
            "branchless", &branchless);
toggles.branchless = branchless;
row2++;
```

이제 체크박스 클릭으로 즉시 두 변종 사이를 오갈 수 있다.

### 빌드와 동작

```bash
cmake -S . -B build -DPERF_LAB_ENABLE_AVX2=ON
cmake --build build --config Release
```

실행 후 `simd: Scalar` 로 두고 `branchless` 체크박스를 켜고/끄며 그래프를 본다.

### 측정 시점에서 보일 수 있는 경향

- **Scalar + branchless OFF**: 명시적 if. 컴파일러가 자동 벡터화에 부분 적용.
- **Scalar + branchless ON**: predication. 컴파일러가 자동 벡터화에 더 잘 적용될 수 있음.

ON 쪽이 *살짝 빠르거나 거의 같음* 이 흔한 결과다. 분기 빈도가 낮은 워크로드라 분기 예측 적중률이 이미 99%대라 분기 비용 자체가 작다. 그래도 *컴파일러 자동 벡터화 차이* 가 ms로 드러나는 케이스가 있다.

`simd: SSE/AVX2` 일 때는 체크박스 ON/OFF가 그래프에 영향이 없다. SIMD 경로는 이미 마스크 처리라 토글의 영향이 없는 게 *옳은* 결과다.

### 검증 체크리스트

- 체크박스 ON/OFF가 *그래프에 차이를 안 만듦* → simd 토글이 SSE/AVX2일 때 정상. Scalar로 바꾼 뒤 다시 토글.
- Scalar에서도 차이 미미 → 컴파일러가 *if 분기도 잘 풀고 있음*. godbolt에서 두 함수 어셈블리를 비교해 cmov/마스크 변환 여부 확인.
- predication ON이 *느림* → 양쪽 분기 모두 계산하는 비용이 분기 예측 이득보다 큰 케이스. 분기 빈도가 매우 낮은 워크로드에서 가능.
- 100k 엔티티에서도 차이 미미 → 메모리 대역폭이 병목이라 분기 처리 차이가 묻힌 경우.

### 측정의 정직성

`branchless` 토글의 효과는 워크로드 의존이 매우 크다. 분기가 *불규칙한* 워크로드에서는 predication의 이득이 명확할 수 있지만, 이 도구의 경계 반사처럼 *대부분 false* 인 분기에서는 차이가 작다. 현재 계기는 실행 시간만 기록하므로 *분기 예측 미스율* 자체를 설명하지 못한다. 그 원인을 확인하려면 `branch-misses` 같은 하드웨어 성능 카운터를 별도로 수집해야 한다.

이 토글이 *항상* 빠르다거나 *항상* 느리다는 단정은 위험하다. *내 머신, 내 컴파일러, 내 워크로드* 에 대한 답은 도구가 그 자리에서 그려 준다.

---

## 결론

이 단계의 결과물은 같은 Scalar 경로 안에서 명시적 분기와 predication 두 처리 방식을 체크박스 한 번에 토글할 수 있는 도구다. 약 80줄의 코드(`UpdateScalar` 의 두 변종 + Update 인자 전달)가 기존 코드 위에 얹혔다.

세 가지 결정이 이 단계의 토대를 만들었다.

1. **predication = ternary** — 분기 대신 cmov, blend, 마스크 select로 변환되도록 유도한다. 컴파일러 자동 벡터화 가능성이 함께 높아질 수 있다.
2. **SIMD 경로는 토글 무관** — 이미 마스크 처리라 추가 영향 없음. 시그니처는 받지만 명시적으로 무시.
3. **함수 안 if 분기로 두 변종 분리** — `UpdateScalar(dt, branchless)` 한 함수가 분기로 두 루프 중 하나만 실행. dispatch가 단순.

여기까지의 코드는 layout / SIMD / 분기 처리 세 축의 비교가 가능하다. 다만 *메모리 할당* 자체는 시뮬 시작 시 한 번 일어나고 매 프레임은 정적이다. 매 프레임 일어나는 *동적 할당 패턴* 을 재는 워크로드도, 토글된 할당자를 주입할 지점도 아직 없어 allocator 토글은 어떤 측정값에도 영향을 주지 못한다.

## 더 읽을거리

- Daniel Lemire, "Making your code faster by taming branches" — 분기 예측과 predication
- Dan Luu, "Branch prediction" — 현대 CPU 분기 예측기 동작
- [godbolt.org](https://godbolt.org/) — `if` vs ternary의 어셈블리 차이 직접 확인
- Agner Fog, "The microarchitecture of Intel, AMD and VIA CPUs" — 분기 예측기 세부
- [Andrei Alexandrescu, "Optimization Tips" (CppCon 2014)](https://www.youtube.com/watch?v=Qq_WaiwzOtI) — branchless 패턴 모음
- Bryce Adelstein-Lelbach, "Benchmarking C++ Code" (CppCon 2015) — 분기 측정의 함정
