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
1. [측정 대상이 교재의 솔버와 같은 코드인가](#1-측정-대상이-교재의-솔버와-같은-코드인가)
2. [StepSim — 모든 시뮬의 공통 단위](#2-stepsim--모든-시뮬의-공통-단위)
3. [RunDeterminismTest](#3-rundeterminismtest)
4. [세 단계 분류 (bit-exact / tolerant / non-deterministic)](#4-세-단계-분류-bit-exact--tolerant--non-deterministic)
5. [무엇이 결정성을 깨는가](#5-무엇이-결정성을-깨는가)

---

## 1. 측정 대상이 교재의 솔버와 같은 코드인가

함정은 두 겹이다.

**첫 겹** — *결정성 테스트가 Live 시뮬과 다른 코드를 돌면 답이 무의미* 하다. 따라서 *동일한 `StepSim` 함수* 가 두 케이스에 모두 쓰여야 한다.

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

**둘째 겹, 훨씬 조용한 쪽** — `StepSim` 이 *자기만의 Sequential Impulse 사본* 을 들고 있으면 어떻게 되는가. 첫 겹은 여전히 만족한다. Live 도 테스트도 같은 `StepSim` 을 부르니 max delta 는 깨끗한 `0.0` 이 나온다. 그런데 그 `0.0` 은 *사본이 재현 가능하다* 는 뜻이지 *Response 가 가르친 SI 가 재현 가능하다* 는 뜻이 아니다. 측정과 교재가 다른 코드를 보는 순간, 초록 색띠는 아무것도 보증하지 않는 장식이 된다.

그래서 접촉 파이프라인의 세 단계 — 접촉 수집 / Sequential Impulse / 위치 보정 — 는 `src/core/contact_solver.h` 에 *한 벌만* 둔다. Response 의 SI 분기도, 여기 `StepSim` 도 같은 템플릿 함수를 부른다. body 타입만 템플릿 파라미터로 다르고 (`pos` / `vel` / `radius` / `invMass` 네 멤버만 요구한다), 풀이 코드는 문자 그대로 하나다.

특히 *유효질량* 을 리터럴로 적어 두는 것이 이 부류의 전형적인 사고다. 예전 `StepSim` 은 body-body 에 `2.0f`, 벽에 `1.0f` 을 박아 두었다 — `invMass` 가 1 인 워크로드에서는 정확히 맞는 값이라 끝까지 들키지 않는다. 공유 구현은 언제나 `bi.invMass + bj.invMass` 를 계산한다.

## 2. StepSim — 모든 시뮬의 공통 단위

<!-- snippet: src/scenes/measure_scene.cpp#StepSim -->
```cpp
void MeasureScene::StepSim(std::vector<MeasureBody>& sim, float dt) const {
    const WorldBounds bounds = Bounds();

    // 1. semi-implicit Euler 적분 (중력만).
    core::IntegrateSemiImplicitEuler(sim, kDefaultGravity, dt);

    // 2. contact 수집 (body-body brute O(n²) + 벽 / 바닥).
    std::vector<Contact> contacts;
    core::GatherCircleContacts(sim, bounds, contacts);

    // 3. Sequential Impulse — velocity 수렴.
    if (shuffleContacts_) {
        static thread_local uint32_t shuffleRng = 0xDEADBEEFu;
        core::SolveSequentialImpulse(sim, contacts, solver_, iterations_,
                                     ShuffleContactOrder{&shuffleRng});
    } else {
        core::SolveSequentialImpulse(sim, contacts, solver_, iterations_);
    }

    // 4. position correction (split impulse 단순화).
    core::SolvePositionCorrection(sim, bounds, solver_, iterations_ / 2);
}
```

네 줄이 전부다. 시나리오(중력, 캔버스 경계, 반복 수, shuffle 토글)만 이 Scene 이 정하고 *푸는 일* 은 전부 공유 솔버가 한다.

임펄스 한 번의 실제 계산은 이렇다 — 유효질량이 어디서 오는지 보라.

<!-- snippet: src/core/contact_solver.h@si-effective-mass -->
```cpp
template <class Body>
void ApplyContactImpulse(std::vector<Body>& bodies, Contact& c,
                         float targetVelocity) {
    Body& bi = bodies[(size_t)c.i];
    if (c.j >= 0) {
        Body&       bj = bodies[(size_t)c.j];
        const float vn = DotV(SubV(bj.vel, bi.vel), c.normal);
        const float applied = AccumulateNormalImpulse(
            vn, targetVelocity, bi.invMass + bj.invMass, c.lambda);
        bi.vel.x -= applied * bi.invMass * c.normal.x;
        bi.vel.y -= applied * bi.invMass * c.normal.y;
        bj.vel.x += applied * bj.invMass * c.normal.x;
        bj.vel.y += applied * bj.invMass * c.normal.y;
    } else {
        // 벽은 무한 질량 — 유효질량은 body 쪽 invMass 하나뿐이다.
        const float vn = DotV(bi.vel, c.normal);
        const float applied = AccumulateNormalImpulse(
            vn, targetVelocity, bi.invMass, c.lambda);
        bi.vel.x += applied * bi.invMass * c.normal.x;
        bi.vel.y += applied * bi.invMass * c.normal.y;
    }
}
```

*고정 순서*:
- body 는 *벡터의 index 순서* 로 처리.
- contact 는 *(i, j) 쌍을 i < j* 로 생성한 뒤 body 순서대로 바닥 / 좌벽 / 우벽 → *고정 순서*.
- iter 안에서도 contact 가 *순차 처리*.

이 세 조건이 *bit-exact* 의 전제. *iter 가 parallel for* 면 reduce 순서가 frame 마다 변할 수 있다 → non-deterministic.

`invMass = 1` 이 이 Scene 의 기본값이지만 *가정으로 남아 있지는 않다* — 위 코드가 매번 body 에서 읽어 계산한다. 매 frame 에 *RNG 호출 없음* — 시뮬 진행 중에는 random 이 안 쓰임. RNG 는 *InitBodies* 때만 (그리고 shuffle 토글을 켰을 때만 예외).

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

1. **MeasureScene 의 Live 시뮬 + Run determinism 버튼** — 같은 `StepSim` 코드를 *Live 한 step* 과 *동기적 결정성 테스트* 양쪽에 재사용. 그리고 그 `StepSim` 은 Response 와 *같은* 접촉 솔버(`src/core/contact_solver.h`)를 돈다 — 측정 대상이 교재의 솔버 그 자체다.
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
 1/23 Test  #1: physics_core_tests ...............   Passed    0.02 sec
 2/23 Test  #2: physics_chrome_tests .............   Passed    0.03 sec
 3/23 Test  #3: physics_forces_tests .............   Passed    0.03 sec
 4/23 Test  #4: physics_collision_tests ..........   Passed    0.03 sec
 5/23 Test  #5: physics_broad_tests ..............   Passed    0.08 sec
 6/23 Test  #6: physics_response_tests ...........   Passed    0.04 sec
 7/23 Test  #7: physics_constraints_tests ........   Passed    0.03 sec
 8/23 Test  #8: physics_determinism_tests ........   Passed    0.08 sec
 9/23 Test  #9: physics_property_tests ...........   Passed    0.93 sec
10/23 Test #10: step01.layout ....................   Passed    0.02 sec
11/23 Test #11: step02.integrators ...............   Passed    0.02 sec
12/23 Test #12: step03.forces ....................   Passed    0.02 sec
13/23 Test #13: step04.aabb_overlap ..............   Passed    0.02 sec
14/23 Test #14: step04.broad_phase ...............   Passed    0.06 sec
15/23 Test #15: step05.narrow_phase ..............   Passed    0.02 sec
16/23 Test #16: step06.contact_impulse ...........   Passed    0.02 sec
17/23 Test #17: step06.response ..................   Passed    0.03 sec
18/23 Test #18: step07.distance_projection .......   Passed    0.02 sec
19/23 Test #19: step07.constraints ...............   Passed    0.02 sec
20/23 Test #20: step08.determinism ...............   Passed    0.05 sec
21/23 Test #21: step05.property ..................   Passed    0.03 sec
22/23 Test #22: step06.property ..................   Passed    0.24 sec
23/23 Test #23: step08.property ..................   Passed    0.08 sec

100% tests passed out of 23
```

등록된 이름이 두 층이다. 앞의 아홉 개는 *실행 파일 하나를 통째로* 도는 이름이고 — chrome (레이아웃 분할), core 헤더 (적분기 오차 차수와 에너지 거동 / restitution target / 누적 impulse 클램프 / AABB / 거리 projection), forces (force 항·종단 속도·에너지 소산), narrow phase (SAT·GJK+EPA 의 hit·깊이·법선), broad phase (overlap 쌍 집합 대조), response (접촉 수집·SI 수렴·위치 보정량), constraints (chain stiffness·Baumgarte drift), 이 Scene 의 결정성, 그리고 입력을 손으로 고르지 않는 property 배터리 (무작위 볼록쌍 · 무작위 강체 장면) — 뒤의 열네 개는 *그 실행 파일들을 step 단위로 자른* 이름이다. 각 실행 파일이 `argv[1]` 로 케이스 필터를 받으므로 (`tests/step_cases.h` 의 {step, 이름, 함수} 표), Step k 를 막 끝낸 독자는 `ctest -R stepNN` 으로 그 시점에 존재하는 검사만 돌릴 수 있다. `physics_core_tests` 처럼 한 파일이 네 step 에 걸친 경우 — core 헤더는 여러 주제가 모이는 자리다 — 그 파일은 `step02` / `step04` / `step06` / `step07` 네 이름으로 나뉜다.

`physics_determinism_tests` 는 `MeasureScene` 의 `InitBodies` / `StepSim` / `RunDeterminismTest` 를 창 없이 부른다. GUI 가 `DrawWorld` 에서 넘겨주던 캔버스 좌표만 `SetCanvas` 로 주입하면 `T` 키가 밟는 것과 같은 경로다.

- 같은 seed 로 600 스텝을 두 번 돌리면 max delta 가 정확히 `0.0f` — 패널의 *bit-exact* 색띠와 같은 판정.
- `SetShuffleContacts(true)` 면 두 실행이 갈라진다 (실측 max delta 0.400). Gauss-Seidel 풀이의 순서 의존이 자동 검사로 남는다.
- 반복성만 보면 *아무것도 하지 않는 `StepSim`* 도 통과하므로 절대 기준을 함께 건다 — 900 스텝 뒤 모든 body 가 벽과 바닥 안에 있고 (최저점 316.6, 바닥 316), 운동 에너지가 0 에 가깝고 (3.2e-4), 평균 침투가 slop 근처 (0.70) 인지.
- **같은 워크로드를 Response Scene 과 나란히 240 스텝 돌려 위치·속도가 bit-exact 인지** — 두 Scene 이 정말 같은 솔버를 도는지를 ctest 가 직접 확인한다 (실측 max pos delta `0`, max vel delta `0`).

마지막 줄이 이 Scene 전체의 전제를 떠받치는 자리다. `== 0.0f` 로 못 박는다 — 허용 오차를 두면 "거의 같은 사본" 이 통과한다.

<!-- snippet: tests/physics_determinism_tests.cpp@same-solver -->
```cpp
// 두 Scene 이 정말 같은 솔버를 도는가. 갈라지면 측정은 교재의 솔버가 아니라
// 사본을 재고 있는 것이고, 그때의 '결정적' 판정은 아무것도 보증하지 않는다.
CHECK(n == 24);
CHECK(worstPos == 0.0f);
CHECK(worstVel == 0.0f);
```

이 Scene 이 추가한 것만 돌리려면:

```powershell
ctest --test-dir build -C Release -R step08 --output-on-failure
.\build\Release\physics_determinism_tests.exe step08   # 같은 것을 직접 — 관측값까지 본다
```

```text
[step08] bit_exact_repeat
  ordered maxDelta=0 after 600 steps
  ok
[step08] settles_in_canvas
  settled: lowest=316.583 left=3.31975 right=396.781 ke=0.000323441
  settled avgPen=0.696468
  ok
[step08] shuffle_diverges
  shuffled maxDelta=0.399962
  ok
[step08] scene_report
  ok
[step08] metrics
  metrics: overlapping avgPen=16 separated avgPen=0 one body at 100 px/s KE=0.5
  ok
[step08] same_solver_as_response
  measure vs response: maxPosDelta=0 maxVelDelta=0
  ok
6 case(s) ok: physics determinism [step08]
```

위 네 줄의 목록이 그대로 관측값으로 나온다 — 반복 실행의 델타 `0`, shuffle 의 델타 `0.400`, 900 스텝 뒤 최저점 `316.583` 과 KE `3.23e-4`, 그리고 두 Scene 의 델타 `0` / `0`.

**통합으로 측정값이 움직였고, 그게 정상이다.** 예전 `StepSim` 은 위치 보정을 자체 구현하면서 iteration 안에서 쌍을 하나 밀 때마다 *그 자리의 최신 위치로* 다음 쌍의 침투를 다시 쟀고, 벽·바닥은 body-body 보정이 전부 끝난 *뒤* 의 위치로 계산했다. 공유 구현은 Response 가 하던 대로 iteration 시작 시점에 접촉을 한 번 수집해 *그 스냅숏* 으로 모든 보정을 적용한다. 같은 물리, 다른 갱신 순서다 — 그리고 Gauss-Seidel 계열 풀이에서 갱신 순서는 결과다. 그래서 900 스텝 뒤 평균 침투가 0.6946 → 0.6965, 최저점이 316.572 → 316.583, KE 가 3.26e-4 → 3.23e-4 로 움직였고 shuffle 실험의 max delta 는 0.700 → 0.400 이 됐다. 반대로 유효질량 리터럴(`2.0f` / `1.0f`)은 이 워크로드의 `invMass` 가 1 이라 *숫자로는* 아무 차이도 만들지 않았다 — 값이 맞았기 때문이 아니라 가정이 우연히 성립했기 때문이고, 그래서 더 오래 숨어 있었다. 중요한 것은 어느 쪽이 더 예쁜 숫자인가가 아니라 **이전 숫자가 무엇을 재고 있었는가** 다. 이전 값들은 Response 가 가르친 위치 보정이 아니라 이 Scene 안에만 존재하던 사본의 거동이었다. 통합 직전에 두 Scene 을 같은 조건으로 240 스텝 돌려 보면 위치가 0.557 px, 속도가 4.77 px/s 벌어져 있었다. 지금은 둘 다 정확히 0 이고, 그 사실이 위 목록의 마지막 줄로 ctest 에 남는다.

접촉 수집에서 바닥 접촉을 지우면 body 가 세계 밖으로 떨어져 절대 기준의 최저점(16974)과 KE(24300) 두 줄이 실패하고, 접촉이 아예 사라지므로 shuffle 이 섞을 것도 없어져 순서 의존 검사 세 줄이 함께 무너진다. 통합 전에는 같은 실험이 최저점 347 / 평균 침투 6.83 을 냈다 — `StepSim` 의 위치 보정 단계가 바닥을 *두 번째로* 처리하고 있어서, 접촉 수집을 지워도 바닥이 절반만 사라졌기 때문이다. 그 중복이 사라진 지금은 바닥이 한 곳에만 있다.

### 하나의 배치가 아니라 배치 공간에서의 결정성

위의 판정은 전부 *seed 1, body 24 개, 같은 초기 배치* 하나에서 나온다. "이 워크로드가 재현된다" 와 "이 솔버가 재현된다" 는 다른 주장이다. 접촉이 몇 개 생기느냐, 정적 body 가 섞이느냐, 질량이 얼마나 벌어지느냐에 따라 도는 코드 경로가 달라지기 때문이다.

그래서 무작위 장면 배터리로 같은 계약을 넓힌다.

```powershell
ctest --test-dir build -C Release -R step08.property --output-on-failure
.\build\Release\physics_property_tests.exe step08   # 같은 것을 직접 — 관측값까지 본다
```

```text
[step08] determinism
  48 장면 x 300 프레임 x 2 회: bit 동일
  접촉이 둘 이상 얽힌 장면 48 개 중 순서를 돌렸을 때 갈라진 것 46 개 (최대 60.2678 px)
  ok
1 case(s) ok: physics property [step08]
```

무작위 장면 48 개를 각각 300 프레임씩 두 번 돌려 위치·속도의 *바이트* 를 비교한다. `== 0.0f` 가 아니라 `memcmp` 다 — 부동소수점 비교는 `-0.0f == 0.0f` 를 참으로 만들지만 바이트는 다르고, 재현성 주장은 값이 아니라 비트에 대한 것이다.

**음성 대조군이 붙어 있다.** 반복 재현성만 보면 아무 일도 일어나지 않는 시뮬도 통과한다. 그래서 같은 장면을 contact 처리 순서만 매 iter 한 칸씩 돌려 다시 돌리고, 갈라지는지 본다. 접촉이 둘 이상 얽힌 48 개 장면 중 46 개가 갈라졌고 최대 편차는 60.3 px 다. Gauss-Seidel 풀이의 순서 의존이 이 배터리에서도 그대로 재현된다는 뜻이고, 동시에 위의 bit 동일이 공허하지 않다는 증거다.

<!-- snippet: tests/physics_property_tests.cpp@property-negative-control -->
```cpp
const physlab::WorldBounds bounds = BoundsOf(in);
std::vector<physlab::Contact> contacts;
physlab::core::GatherCircleContacts(first, bounds, contacts);
if (contacts.size() >= 2) {
    ++determinismContended;
    const auto rotated = RunFrames(in, kDeterminismFrames, true);
    const float delta = WorstDelta(first, rotated);
    determinismWorstShuffleDelta =
        std::max(determinismWorstShuffleDelta, delta);
    if (delta > 1e-4f) ++determinismDiverged;
}
```

`SolveSequentialImpulse` 에 프로세스 전역 카운터로 처리 시작 위치를 미는 변이를 넣어 확인했다. 반복 실행의 위치 델타는 `0` 인 채로 속도 비트만 갈라졌고, 그래서 바이트 비교가 잡아냈다 — 위치만 봤다면 통과했을 변이다.

이 배터리가 넓히지 *못하는* 것도 그대로다. 여전히 같은 프로세스, 같은 빌드, 같은 CPU 안의 반복성이다. 배치 공간은 넓어졌지만 실행 환경은 하나뿐이므로, cross-compiler / cross-CPU lockstep 은 이 lab 이 답하지 않는 질문으로 남는다.

physics_lab 은 *시리즈의 주장이 측정으로 검증되는 도구*. *글의 그래프* 가 lab 의 *실시간 곡선* 으로 재현되고, *발산 / 수렴 / drift / 결정성 위반* 의 순간을 *직접 눈으로* 본다. 이게 *물리 lab* 이 *시각화 데모* 와 다른 결정적 차이.

## 더 읽을거리

- Glenn Fiedler, ["Floating Point Determinism"](https://gafferongames.com/post/floating_point_determinism/) — 결정성 3 분류의 배경이 되는 부동소수점 재현성 논의.
- Bruce Dawson, ["Floating-Point Determinism"](https://randomascii.wordpress.com/2013/07/16/floating-point-determinism/) — 컴파일러·플랫폼별 차이의 정밀 분석.
- Erin Catto, [Box2D publications](https://box2d.org/publications/) — solver 구조와 측정 항목의 프로덕션 기준.
