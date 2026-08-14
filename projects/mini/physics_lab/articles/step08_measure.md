## 들어가며

`physics_lab` 의 마지막 단계. *baseline determinism sanity check* + 종합 측정. 작은 N-body 시뮬 (원형 강체 + Sequential Impulse + Semi-implicit Euler) 을 *살아있는 시뮬* 로 돌리며 *Live 에너지 / 평균 침투* 를 그래프로 보고, *Run determinism* 버튼이 *같은 seed 로 두 번 시뮬을 동기적으로 돌려 max delta* 를 측정.

**"Sanity check" 인 이유**. 이 테스트는 *같은 프로세스 / 같은 빌드 / 같은 컴파일러 / 같은 CPU* 에서 *동일 코드 경로를 두 번 실행했을 때 결과가 같은가* 만 확인한다. 즉 *최소 결정성 기준* — 이마저도 깨진다면 *멀티플레이어 lockstep* / *replay* / *리그레션 테스트* 같은 *진짜 결정성 요구* 는 시작도 못 한다는 의미.

진짜 cross-machine deterministic 인지 검증하려면 *다른 CPU / 다른 컴파일러 / 다른 OS* 에서 같은 시뮬을 돌려 비교해야 한다. 이 Scene 은 그 *상위 검증의 baseline* 만 다룬다.

토글 `shuffle contacts` 가 *결정성을 의도적으로 깨는 실험* — SI 의 *Gauss-Seidel 풀이* 가 *순서 의존* 임을 직접 보여준다.

결정성의 세 단계.

| 결과 | max delta | 의미 |
|---|---|---|
| BIT-EXACT REPEATABLE | 정확히 0.0 | 이 프로세스의 두 실행이 일치. lockstep의 필요조건일 뿐 충분조건은 아님. |
| tolerantly deterministic | $\varepsilon$ 이하 (예: < 0.001 px) | SIMD reduce 순서 / floating-point arithmetic 의 영향. 게임 *실행* 은 문제 없지만 lockstep 은 위험. |
| NON-DETERMINISTIC | $\varepsilon$ 초과 | 시뮬이 갈라짐. 멀티스레딩의 race / 부동소수 결합법칙 / RNG 순서 변경. |

대부분의 게임 엔진은 *기본적으로 non-deterministic*. *결정성을 보장하려면* 명시적 노력 필요 — fixed dt + 동일 순서 reduce + cross-platform float consistency + 결정적 RNG.

---

## 목차
1. [Live 시뮬 = 결정성 테스트 시뮬 (코드 공유)](#1-live-시뮬--결정성-테스트-시뮬-코드-공유)
2. [StepSim — 모든 시뮬의 공통 단위](#2-stepsim--모든-시뮬의-공통-단위)
3. [RunDeterminismTest](#3-rundeterminismtest)
4. [세 단계 분류 (bit-exact / tolerant / non-deterministic)](#4-세-단계-분류-bit-exact--tolerant--non-deterministic)
5. [무엇이 결정성을 깨는가](#5-무엇이-결정성을-깨는가)

---

## 1. Live 시뮬 = 결정성 테스트 시뮬 (코드 공유)

가장 큰 함정은 *결정성 테스트가 Live 시뮬과 다른 코드를 돌면 답이 무의미* 함. 따라서 *동일한 `StepSim` 함수* 가 두 케이스에 모두 쓰여야 한다.

```cpp
class MeasureScene : public IScene {
    std::vector<MeasureBody> live_;     // Live 시뮬

    void Step(float dt) override {
        StepSim(live_, dt);              // 매 frame 1 step
    }

    void RunDeterminismTest() {
        auto runOnce = [&]() {
            std::vector<MeasureBody> sim;
            InitBodies(sim, N_, seed_);
            for (int s = 0; s < testFrames_; ++s) StepSim(sim, 1/120);
            return sim;
        };
        auto a = runOnce();
        auto b = runOnce();
        // a, b 비교 → max delta
    }
};
```

같은 `StepSim` → 같은 *부동소수 연산 순서* → *bit-exact* 가능성. `InitBodies` 도 *같은 seed* 면 *같은 RNG 순서* → *같은 초기 배치*.

## 2. StepSim — 모든 시뮬의 공통 단위

```cpp
void StepSim(std::vector<MeasureBody>& sim, float dt) const {
    int n = (int)sim.size();
    // 1. semi-implicit Euler 적분.
    for (auto& b : sim) {
        b.vel.y += kSimGravity * dt;
        b.pos.x += b.vel.x * dt;
        b.pos.y += b.vel.y * dt;
    }
    // 2. contact 수집 (brute O(n²)).
    std::vector<Cn> contacts;
    // body-body + walls ...
    // 3. Sequential Impulse iter 반복.
    for (int iter = 0; iter < iterations_; ++iter) {
        for (auto& c : contacts) {
            // velocity impulse with accumulated lambda clamp
        }
    }
    // 4. position correction iter 반복.
    for (int iter = 0; iter < iterations_/2; ++iter) {
        // 새로 contact 검사 + 위치 보정
    }
}
```

*고정 순서*:
- body 는 *벡터의 index 순서* 로 처리.
- contact 는 *(i, j) 쌍을 i < j* 로 생성 → *고정 순서*.
- iter 안에서도 contact 가 *순차 처리*.

이 두 조건이 *bit-exact* 의 전제. *iter 가 parallel for* 면 reduce 순서가 frame 마다 변할 수 있다 → non-deterministic.

`mass = 1`, `invMass = 1` 로 고정. 매 frame 에 *RNG 호출 없음* — 시뮬 진행 중에는 random 이 안 쓰임. RNG 는 *InitBodies* 때만.

## 3. RunDeterminismTest

버튼 클릭 (또는 T 키) 시:

```cpp
void RunDeterminismTest() {
    auto runOnce = [&]() {
        std::vector<MeasureBody> sim;
        InitBodies(sim, N_, seed_);
        for (int s = 0; s < testFrames_; ++s) StepSim(sim, 1/120);
        return sim;
    };
    auto a = runOnce();
    auto b = runOnce();

    float maxD = 0, sumD = 0;
    for (int i = 0; i < N_; ++i) {
        float dx = a[i].pos.x - b[i].pos.x;
        float dy = a[i].pos.y - b[i].pos.y;
        float d  = sqrt(dx*dx + dy*dy);
        maxD = max(maxD, d);
        sumD += d;
    }
    lastTestMaxDelta_      = maxD;
    lastTestDeterministic_ = (maxD == 0.0f);
    testRan_               = true;
}
```

`testFrames_` 가 600 (5 초 @ 120 Hz) 이 기본이다. 더 긴 실행은 작은 차이가 누적되는지 관찰하기 쉽지만, 모든 물리계의 오차가 지수적으로 발산하는 것은 아니다. 감쇠된 계에서는 차이가 줄 수도 있고, 접촉 분기에서 불연속적으로 커질 수도 있다.

## 4. 세 단계 분류 (bit-exact / tolerant / non-deterministic)

```cpp
if (maxD == 0.0f) {                         // 정확히 같음
    DrawRectangle(... GREEN ...);
    Label("BIT-EXACT DETERMINISTIC");
} else if (maxD < 1e-3f) {                  // 0.001 px 이하
    DrawRectangle(... YELLOW ...);
    Label("tolerantly deterministic");
} else {
    DrawRectangle(... RED ...);
    Label("NON-DETERMINISTIC");
}
```

이 코드는 *같은 머신 / 같은 빌드 / 같은 컴파일러 옵션* 에서는 *기본 토글 (shuffle off)* 일 때 *항상 bit-exact* 다 — 시뮬 안의 *모든 연산이 순차적 + RNG 호출 없음* 이라서. *cross-compiler / cross-CPU* 는 다를 수 있다 (예: x87 80-bit vs SSE2 32-bit, FMA 사용 여부) — 그 검증은 이 sanity check 의 범위 밖.

`shuffle contacts` 토글은 두 비교 실행 사이에 shuffle RNG 상태를 의도적으로 공유·진행시켜 서로 다른 접촉 순서를 주입한다. 이는 “같은 완전한 입력 상태인데 저절로 비결정적”인 테스트가 아니라, 스케줄 순서가 입력 계약에서 빠졌을 때 Gauss-Seidel 결과가 달라진다는 실패 주입이다. 결정적 replay라면 RNG와 작업 순서도 상태 또는 입력으로 기록해야 한다.

게임 엔진의 결정성 실패 케이스 대부분이 이 패턴 — *contact / island / pair 의 처리 순서가 thread / frame 마다 미세하게 다름*. 우리 코드는 *single-thread + brute O(n²) + 고정 순서* 라 *기본 토글에서는* 결정성 위반의 여지가 없음.

## 5. 무엇이 결정성을 깨는가

깰 수 있는 변경들 (실험 가능).

1. **iter 안의 contact 순서를 frame 마다 바꿈** — 예: contact 를 `std::random_shuffle` 로 섞기. SI 의 *Gauss-Seidel 풀이* 는 *순서 의존* 이라 결과가 다르다.
2. **multithread 로 contact 병렬 처리** — `parallel_for(contacts, ...)`. 같은 contact 에 대한 갱신이 *경쟁* → race condition. 정확히 *같은 reduce* 순서가 보장 안 됨.
3. **RNG 를 시뮬 도중 사용** — 예: penalty 의 noise. `std::mt19937` 의 *호출 순서* 가 *thread 마다* 다르면 깨짐.
4. **floating-point optimization flags** — `-ffast-math` (GCC), `/fp:fast` (MSVC)는 재결합과 contraction을 허용한다. 같은 바이너리가 프레임마다 다른 최적화 패스를 택하는 것은 아니지만, 빌드·아키텍처·벡터화 폭이 달라지면 연산 순서와 반올림이 달라질 수 있다.
5. **cross-platform float** — x87 80-bit, SSE 32-bit, ARM NEON 의 *반올림 mode 차이*. 진짜로 cross-platform deterministic 하려면 *fixed-point* 또는 *softfloat lib*.

이 Scene 은 *기본 케이스가 bit-exact* — 우리가 *위 5 가지를 모두 피했기 때문*. 게임 엔진을 만들 때 *결정성이 필요한 부분* 은 이 5 가지를 의식적으로 피해야 한다.

```mermaid
flowchart LR
    A[Same seed] --> B[Run1: InitBodies + 600 StepSim]
    A --> C[Run2: InitBodies + 600 StepSim]
    B --> D[final positions array A]
    C --> E[final positions array B]
    D & E --> F[per-body delta = |A.pos - B.pos|]
    F --> G[max, avg]
    G --> H{maxD == 0?}
    H -->|yes| I[BIT-EXACT]
    H -->|< 1e-3| J[tolerant]
    H -->|else| K[NON-DET]
```

---

## 결론

Step 8 에서 추가한 것 — 그리고 lab 전체의 마무리.

1. **MeasureScene 의 Live 시뮬 + Run determinism 버튼** — 같은 `StepSim` 코드를 *Live 한 step* 과 *동기적 결정성 테스트* 양쪽에 재사용.
2. **결정성 3 분류** — bit-exact / tolerantly deterministic / non-deterministic. UI 가 색띠로 즉시 분류.
3. **종합 측정** — Live 의 *avg penetration* + *total KE* 시간 그래프. seed / N / iter 토글이 바뀌면 그래프가 재시작.
4. **physics_lab 전체** — Step 1 (skeleton + fixed dt accumulator) → Step 2 (integrators) → Step 3 (forces) → Step 4 (broad phase) → Step 5 (narrow phase) → Step 6 (response) → Step 7 (constraints) → Step 8 (measure). *physics 시리즈의 part01·02 (운동학·적분기), part05 (Broad Phase), part06·07 (SAT / GJK+EPA), part08 (법선 응답), part10·11·12 (제약·SI·PBD)* 를 인터랙티브하게 검증하는 *측정 도구* 완성. 마찰 (part09), 회전 강체 동역학 (part03·04), 조인트 (part13), part14~20 은 대응 구현이 없다.

전체 실행:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
ctest --test-dir build -C Release --output-on-failure
```

```text
1/6 Test #1: physics_core_tests ...............   Passed    0.06 sec
2/6 Test #2: physics_collision_tests ..........   Passed    0.07 sec
3/6 Test #3: physics_broad_tests ..............   Passed    0.14 sec
4/6 Test #4: physics_response_tests ...........   Passed    0.10 sec
5/6 Test #5: physics_constraints_tests ........   Passed    0.08 sec
6/6 Test #6: physics_determinism_tests ........   Passed    0.46 sec

100% tests passed out of 6
```

ctest 가 도는 것은 여섯 개다 — core 헤더 (적분기 오차 차수와 에너지 거동 / restitution target / 누적 impulse 클램프 / AABB / 거리 projection), narrow phase (SAT·GJK+EPA 의 hit·깊이·법선), broad phase (overlap 쌍 집합 대조), response (접촉 수집·SI 수렴·위치 보정량), constraints (chain stiffness·Baumgarte drift), 그리고 이 Scene 의 결정성이다.

`physics_determinism_tests` 는 `MeasureScene` 의 `InitBodies` / `StepSim` / `RunDeterminismTest` 를 창 없이 부른다. GUI 가 `DrawWorld` 에서 넘겨주던 캔버스 좌표만 `SetCanvas` 로 주입하면 `T` 키가 밟는 것과 같은 경로다.

- 같은 seed 로 600 스텝을 두 번 돌리면 max delta 가 정확히 `0.0f` — 패널의 *bit-exact* 색띠와 같은 판정.
- `SetShuffleContacts(true)` 면 두 실행이 갈라진다 (실측 max delta 0.700). Gauss-Seidel 풀이의 순서 의존이 자동 검사로 남는다.
- 반복성만 보면 *아무것도 하지 않는 `StepSim`* 도 통과하므로 절대 기준을 함께 건다 — 900 스텝 뒤 모든 body 가 벽과 바닥 안에 있고 (최저점 316.6, 바닥 316), 운동 에너지가 0 에 가깝고 (3.3e-4), 평균 침투가 slop 근처 (0.69) 인지.

바닥 접촉 생성을 지우면 이 절대 기준 다섯 줄이 한꺼번에 실패한다 — 최저점 347, KE 24300, 평균 침투 6.83.

physics_lab 은 *시리즈의 주장이 측정으로 검증되는 도구*. *글의 그래프* 가 lab 의 *실시간 곡선* 으로 재현되고, *발산 / 수렴 / drift / 결정성 위반* 의 순간을 *직접 눈으로* 본다. 이게 *물리 lab* 이 *시각화 데모* 와 다른 결정적 차이.

## 더 읽을거리

- Glenn Fiedler, ["Floating Point Determinism"](https://gafferongames.com/post/floating_point_determinism/) — 결정성 3 분류의 배경이 되는 부동소수점 재현성 논의.
- Bruce Dawson, ["Floating-Point Determinism"](https://randomascii.wordpress.com/2013/07/16/floating-point-determinism/) — 컴파일러·플랫폼별 차이의 정밀 분석.
- Erin Catto, [Box2D publications](https://box2d.org/publications/) — solver 구조와 측정 항목의 프로덕션 기준.
