## 들어가며

> 이 실습은 게임 개발의 모든 CS 주제를 하나의 프로그램에 구현하는 과정이 아니다. 같은 상태를 여러 메모리 배치와 연산 방식으로 처리하고, 할당·순회 비용을 나누어 관찰하는 저수준 성능 실습이다. ECS 는 단일 컴포넌트 조합의 저장 배치까지만 다룬다. query 와 구조 변경, 문자열·인코딩·동시성 등은 별도 실습에서 다룬다.
>
> 실행 결과가 특정 순서로 나와야 성공하는 실습은 아니다. 먼저 같은 입력에 대해 같은 의미의 결과를 계산하는지 확인한다. 그다음 작업량·측정 구간·실행 환경을 맞추고 반복 측정한다. 기대와 다른 성능이 나오면 결과를 숨기거나 구현 오류로 단정하지 않고, 이 실험이 어떤 가정을 충족하지 못했는지 확인한다.
>
> 각 장의 코드는 세 종류로 구분한다. `현재 단계 완성 코드`는 해당 단계의 파일 집합과 함께 빌드할 수 있어야 한다. `발췌 코드`는 생략된 선언과 helper 의 실제 위치를 함께 적는다. `설명용 의사코드`는 의미와 제어 흐름을 보여주며 그대로 붙여 넣는 소스가 아니다. 최종 `src/`와 중간 단계의 함수 시그니처가 다르면 변환 표를 제공한다.

현재까지의 결과물은 SoA·Archetype에 SSE·AVX2 경로를 박아 *수동 SIMD*로 처리량을 높인 측정 도구다. SIMD 경로는 경계 갱신을 *마스크 + select* 로 풀어 lane별 분기를 없애지만, 함수에는 dispatch·loop·tail 분기가 남는다. 한편 Scalar 경로에는 여전히 *명시적 if 분기* 가 있어 *분기 예측* 의 영향을 받는다. 분기 패턴이 불규칙하면 분기 예측 미스가 비용을 늘릴 수 있지만, 이 도구는 그 실패율을 직접 측정하지 않는다.

이 단계의 목표는 *분기 처리 방식* 을 토글로 만들어 같은 워크로드에서 두 방식을 비교하는 것이다. 토글 OFF는 명시적 if, 토글 ON은 *predication* (조건을 ternary로 적어 컴파일러가 cmov·blend·마스크 select로 낮추도록 유도). 같은 결과를 두 방식으로 처리한 ms 차이가 그래프에 그려진다. C++의 조건 연산자는 선택된 피연산자 하나만 평가하므로, ms 차이를 “삼항 연산자가 양쪽을 항상 계산한다”는 식으로 단정하지 않는다.

이 단계의 결과물은 다음 셋이다.

1. SoA·Archetype·AoS의 `UpdateScalar` 가 `branchless` 인자를 받아 두 경로(if 분기 / predication)로 갈라진다.
2. SIMD 경로는 이미 마스크 처리라 `branchless` 토글에 영향받지 않는다.
3. HUD의 `branchless` 체크박스가 활성화된다.

---

## 목차
1. [분기 예측의 비용과 측정 의도](#1-분기-예측의-비용과-측정-의도)
2. [Predication: 분기 없이 ternary로](#2-predication-분기-없이-ternary로)
3. [Scalar 경로의 두 변종](#3-scalar-경로의-두-변종)
4. [SIMD 경로는 영향받지 않는다](#4-simd-경로는-영향받지-않는다)
5. [Hud 활성화와 빌드·실행 결과](#5-hud-활성화와-빌드실행-결과)

---

## 1. 분기 예측의 비용과 측정 의도

현대 CPU는 *분기 예측* 으로 파이프라인 stall을 줄인다. 분기 명령이 들어오기 전에 *어느 쪽으로 갈지* 추측하고 미리 명령을 가져온다. 추측이 맞으면 추가 비용이 작고, 틀리면 *파이프라인 flush + 재시작* 비용이 든다.

아래 표는 일반적인 예측기 모델을 설명하기 위한 것이며 이 도구가 측정한 값이 아니다. 미스 비용과 예측기 학습 방식은 마이크로아키텍처마다 다르므로 특정 사이클 수를 정답으로 두지 않는다.

| 분기 패턴(모델) | 예측 경향 | 상대 비용 |
|---|---|---|
| 항상 같은 방향 (true 또는 false 한쪽으로) | 높음 | 작음 |
| 주기적 (T/F/T/F/T/F...) | 학습되면 높음 | 작음 |
| 길게 같은 방향 후 전환 | 전환 시점만 미스 | 작음 |
| 무작위 | 낮음 | 미스 비용 누적 |

이 도구의 경계 반사 분기는 *대부분 false* 다. 1만 개 점 중 매 프레임 *경계에 닿는 점* 은 소수다. 그러나 사건이 드물다는 것과 실제 기계어 분기의 예측 실패율이 낮다는 것은 같은 측정값이 아니다. 사건 빈도는 확률이고, 분기 실패율은 `branch-misses / branches` 로 재는 별개의 양이다. 변동하는 dt 때문에 사건 빈도 자체도 고정되지 않는다. 그래서 이 문서는 “적중률 99%” 같은 값을 이 데모의 결론으로 제시하지 않는다. 먼저 어떤 분기가 생성됐는지 확인하고, 가능한 환경에서는 그 워크로드의 branch 와 branch-miss 를 계측한다. PMU 를 사용할 수 없으면 실패율을 측정하지 않았다고 표시한다.

그렇다면 *predication* 토글은 무엇을 보여 주는가?

- **OFF (if 분기)**: 명시적 분기를 남긴다. 컴파일러가 분기 부분을 자동 벡터화하지 못할 수 있어 SoA scalar의 자동 벡터화가 부분적일 수 있다.
- **ON (predication)**: 조건 선택을 ternary로 적어 *branchless 코드 생성* 을 유도한다. 컴파일러가 cmov, blend, 마스크 select로 변환하기 쉬워질 수 있고, 자동 벡터화가 루프 전체에 적용될 가능성도 커진다.

이 토글의 효과는 *분기 예측 영향* 만이 아니라 *컴파일러 자동 벡터화 가능성의 차이* 일 수 있다. 어느 쪽이든 소스의 조건식과 실제 기계어를 1:1로 대응시키지 않는다. 함수에는 dispatch·loop·tail 분기가 남을 수 있으므로 compiler report 와 disassembly 로 확인한다.

이 도구에서 나올 수 있는 결과는 다음 셋 중 하나다. 이것은 정답이나 합격 조건이 아니라 가설이다.

- predication ON이 OFF보다 *눈에 띄게 빠를 수 있음* — 컴파일러가 자동 벡터화 차이를 만든 경우.
- 거의 같을 수 있음 — 컴파일러가 if 분기도 잘 풀었거나, 분기 빈도가 낮아 차이가 묻힌 경우.
- predication ON이 *살짝 느릴 수 있음* — 현재 ON 경로가 모든 원소에 `clamp` 를 수행해 추가 연산이 생긴 경우. 이것을 “삼항 연산자가 양쪽 피연산자를 항상 평가한다”는 이유로 설명하지 않는다. C++의 조건 연산자는 선택된 피연산자 하나만 평가하며, 컴파일러가 부작용 없는 계산을 select 등으로 낮추는 문제와 언어의 평가 규칙은 별개다.

세 결과 모두 *학습 가치* 가 있다. 도구는 본인 머신의 컴파일러가 어떤 결과를 내는지를 그 자리에서 보여 준다.

### 순서를 바꾸는 trace 실험 — 보충 구현 목표

현재 도구는 입력 분포를 토글로 바꿀 수 없다. 충돌이 드문 예측 쉬운 입력과 참/거짓이 섞인 입력을 독립적으로 선택하려면 별도의 trace 실습이 필요하다. 아직 구현하지 않았다.

- 같은 참/거짓 개수를 유지한 채 순서만 바꾼다: 무작위 순서, 교대 순서, 긴 동일값 구간, 정렬된 순서를 비교한다.
- 참의 비율도 바꾼다: p=0, 0.01, 0.5, 1.
- trace 생성과 정렬은 타이머 밖에 두고, 각 구현의 결과 합계와 방문 원소를 검사한다.
- 정렬 비용까지 평가할 때는 그 비용을 포함하는 별도의 end-to-end 실험을 만든다.

trace 생성기, 순서 변형, PMU 수집, 고정 fixture 는 아직 저장소에 없다. 이 절은 구현 목표이며 완료로 표시하지 않는다.

**학습 질문:** 서로 다른 C++ 문법이 같은 기계어가 됐는가? true 비율이 같아도 순서가 달라지면 결과가 다른가? PMU 가 없다면 어떤 원인까지 말할 수 있고 어떤 것은 가설로 남겨야 하는가?

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

언어 규칙 하나를 분명히 한다. `outX ? -velX_[i] : velX_[i]` 에서 C++은 선택된 피연산자 하나만 평가한다. 양쪽을 모두 계산한 뒤 고르는 것이 아니다. 컴파일러가 부작용 없는 계산을 조건부 이동이나 select로 낮추는 것은 언어 규칙과 별개의 최적화 결정이다. 그러므로 “삼항이라서 양쪽을 계산해 느리다”는 설명은 쓰지 않는다.

어셈블리 수준에서 조건 선택은 여러 형태로 변환될 수 있다.

- **cmov** (조건부 이동): `cmovz`, `cmovnz` 등. 분기 없이 결과를 선택.
- **bitwise mask + andn + or**: SIMD에서 자주 쓰이는 패턴. SSE2 bitwise select와 같은 형태.
- **여전히 분기**: 컴파일러가 분기를 남기는 경우.

실제로 어떤 형태로 변환되는지는 godbolt나 `objdump` 로 확인한다. ternary가 branchless로 내려갈 수도 있고, 컴파일러가 분기를 남길 수도 있다. 소스 문법만으로 생성 분기를 단정하지 않는다.

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

SSE/AVX2 경로는 경계 갱신을 이미 *마스크 + bitwise select* 로 처리한다. `branchless` 토글이 *추가로 영향* 을 줄 부분이 없다. 다만 함수 전체의 dispatch·loop·tail 분기가 사라진 것은 아니므로, 이 서술은 lane별 경계 분기에 한정한다.

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

다음은 예상 시나리오이지 합격 조건이 아니다.

- **Scalar + branchless OFF**: 명시적 if. 컴파일러가 자동 벡터화에 부분 적용될 수 있음.
- **Scalar + branchless ON**: predication. 자동 벡터화가 더 잘 적용될 수 있음.

ON 쪽이 *살짝 빠르거나 거의 같을 수 있다*. 이 데모의 경계 반사는 사건이 드물어 분기 비용 자체가 작을 수 있다. 다만 이것은 “분기 예측 적중률이 99%”라는 측정이 아니다. 반대로 ON이 느릴 수도 있는데, 그 원인을 삼항 연산자 양쪽 계산으로 단정하지 않는다. 자동 벡터화 여부와 생성 분기는 compiler report와 disassembly로 별도 확인한다.

`simd: SSE/AVX2` 일 때는 체크박스 ON/OFF가 그래프에 영향이 없어야 한다. SIMD 경로의 경계 갱신은 마스크 처리라 이 토글과 무관하다.

### 자동 검증: `parts_branch_tests`

predication은 *같은 값* 을 다른 명령으로 내는 변형이다. 결과가 달라졌다면 그건 최적화가 아니라 버그다. 그래서 `branch` 그룹은 성능이 아니라 *등식* 만 본다 — 체크박스를 켰을 때 화면의 점들이 조금이라도 다른 자리에 있으면 ms 비교는 의미를 잃는다.

앞 절에서 말한 "SIMD 경로는 토글 무관" 은 이 등식으로는 확인되지 않는다. 그쪽은 같은 함수가 같은 인자로 도는 구조라 결과가 다를 수 없고, 그래서 허용 오차가 아니라 *0* 이 기준이다.

<!-- snippet: tests/core_tests.cpp#TestBranchlessEquivalence -->
```cpp
// tests/core_tests.cpp — branch 그룹

void TestBranchlessEquivalence() {
    constexpr int   kFrames = 240;
    constexpr float kDt     = 1.0f / 144.0f;

    auto branched   = MakeAndInit(ppl::Layout::SoA);
    auto predicated = MakeAndInit(ppl::Layout::SoA);
    for (int i = 0; i < kFrames; ++i) {
        branched->Update(kDt, ppl::SimdWidth::Scalar, false);
        predicated->Update(kDt, ppl::SimdWidth::Scalar, true);
    }
    std::vector<float> a, b;
    branched->CopyState(a);
    predicated->CopyState(b);
    CHECK(NearStates(a, b));

    // 세 레이아웃 모두에서 같은 성질이 성립해야 한다.
    for (int l = 0; l < ppl::kLayoutCount; ++l) {
        const auto layout = static_cast<ppl::Layout>(l);
        const char* label = ppl::LayoutName(layout);

        auto off = MakeAndInit(layout);
        auto on  = MakeAndInit(layout);
        for (int i = 0; i < kFrames; ++i) {
            off->Update(kDt, ppl::SimdWidth::Scalar, false);
            on->Update(kDt, ppl::SimdWidth::Scalar, true);
        }
        std::vector<float> scalarOff, scalarOn;
        off->CopyState(scalarOff);
        on->CopyState(scalarOn);
        CHECK_AT(NearStates(scalarOff, scalarOn), label);

        // SIMD 경로는 이미 마스크 select라 이 토글에 영향받지 않는다.
        auto simdOff = MakeAndInit(layout);
        auto simdOn  = MakeAndInit(layout);
        for (int i = 0; i < kFrames; ++i) {
            simdOff->Update(kDt, ppl::SimdWidth::SSE, false);
            simdOn->Update(kDt, ppl::SimdWidth::SSE, true);
        }
        std::vector<float> sseOff, sseOn;
        simdOff->CopyState(sseOff);
        simdOn->CopyState(sseOn);
        CHECK_AT(NearStates(sseOff, sseOn, 0.0f), label);
    }
}
```

predication 쪽 루프가 `std::clamp` 를 조건 없이 부르는데도 결과가 같은 이유는 clamp가 범위 안에서 항등이기 때문이다. 그 성질이 깨지는 방향으로 루프를 고치면 `NearStates(scalarOff, scalarOn)` 이 먼저 걸린다.

```bash
cmake -S . -B build -DPERF_LAB_ENABLE_AVX2=ON
cmake --build build --config Release
ctest --test-dir build -C Release -R parts_branch_tests --output-on-failure

./build/parts_core_tests branch              # Linux/macOS
.\build\Release\parts_core_tests.exe branch  # Windows
```

```text
parts_core_tests: group `branch`
  branch/BranchlessEquivalence ... 7 check(s) ok
branch: 1 test(s), 7 check(s), 0 failure(s)
```

`7 check(s)` 는 SoA 한 쌍 + 세 레이아웃 × (scalar 등식, SSE 등식) 이다. 이 수는 빌드 옵션에 영향받지 않는다 — 분기 처리는 x86 인트린식과 무관한 축이라, AVX2를 켜든 끄든 같은 일곱 단언이 돈다.

### 검증 체크리스트

- 체크박스 ON/OFF가 *그래프에 차이를 안 만듦* → simd 토글이 SSE/AVX2일 때 예상되는 결과. Scalar로 바꾼 뒤 다시 토글한다.
- Scalar에서도 차이 미미 → 컴파일러가 *if 분기도 잘 풀었을 수 있음*. godbolt에서 두 함수 어셈블리를 비교해 cmov/마스크 변환 여부를 확인한다.
- predication ON이 *느림* → 추가 연산이나 다른 codegen 때문일 수 있다. 삼항 연산자가 양쪽을 계산한다고 단정하지 말고, disassembly와 PMU로 확인한다.
- 100k 엔티티에서도 차이 미미 → 메모리 대역폭이 병목이거나 사건 빈도가 낮은 경우일 수 있다.
- `branch-misses` 를 수집하지 않았다면 미스율은 *미측정* 으로 표시한다.

### 측정의 정직성

`branchless` 토글의 효과는 워크로드 의존이 매우 크다. 분기가 *불규칙한* 워크로드에서는 predication의 이득이 명확할 수 있지만, 이 도구의 경계 반사처럼 *대부분 false* 인 분기에서는 차이가 작다. 현재 계기는 실행 시간만 기록하므로 *분기 예측 미스율* 자체를 측정하지 않는다. 그 원인을 확인하려면 `branch-misses` 같은 하드웨어 성능 카운터(PMU)를 별도로 수집해야 한다. PMU 를 쓸 수 없는 환경에서는 그 값을 *unavailable* 로 두고, 사건 빈도와 분기 실패율을 같은 값으로 취급하지 않는다.

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
- Bryce Adelstein Lelbach, "Benchmarking C++ Code" (CppCon 2015) — 분기 측정의 함정
