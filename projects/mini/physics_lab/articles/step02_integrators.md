## 들어가며

`physics_lab` 의 두 번째 단계. *적분기 (integrator) 네 종류를 같은 시뮬에 동시에 돌려놓고 발산 / 수렴을 그래프로 본다*. 시리즈 `physics/01~02` 가 *왜 게임 엔진이 Explicit Euler 가 아니라 Semi-implicit 이나 Verlet 을 쓰는가* 를 글로 설명했다면, 이 Scene 은 *그 차이를 사용자가 토글 한 번으로 직접 확인* 한다.

비교 대상.

| 적분기 | 차수 | 보존성 | 비용 / step | 비고 |
|---|---|---|---|---|
| Explicit Euler | 1 | × (발산) | 1 evaluation | 가장 단순 / 가장 약함 |
| Semi-implicit Euler | 1 | symplectic (약 보존) | 1 evaluation | 게임 엔진 표준 |
| Velocity Verlet | 2 | symplectic | 2 evaluation | cloth / 입자 표준 |
| RK4 | 4 | × (그러나 매우 작은 drift) | 4 evaluation | 정밀하지만 비싸고 보존성 없음 |

시스템은 *단순 조화 진동자* (Simple Harmonic Oscillator):

$$
m\,\ddot{x} = -k x, \quad \omega = \sqrt{k/m}, \quad x(t) = A\cos(\omega t + \varphi)
$$

이 시스템을 고른 이유는 *분석해와 에너지 보존이 둘 다 알려져 있어* 적분기의 정확성을 *두 가지 기준* 으로 동시에 검증할 수 있기 때문. 총 에너지

$$
E = \tfrac{1}{2} v^2 + \tfrac{1}{2} \omega^2 x^2
$$

는 시간에 무관하게 일정해야 한다 — 적분기가 *수치적으로 추가하거나 빼는 양* 이 그래프에 그대로 드러난다.

---

## 목차
1. [네 적분기 식](#1-네-적분기-식)
2. [시스템: SHO 와 분석해](#2-시스템-sho-와-분석해)
3. [Scene 자료구조 + 누적기 (fixed dt + integDt)](#3-scene-자료구조--누적기-fixed-dt--integdt)
4. [에너지 그래프 + 진동자 시각화](#4-에너지-그래프--진동자-시각화)
5. [무엇을 보는가: 발산 / 보존 / drift](#5-무엇을-보는가-발산--보존--drift)
6. [적분기 코드의 위치와 헤드리스 테스트](#6-적분기-코드의-위치와-헤드리스-테스트)

---

## 1. 네 적분기 식

상태가 $(x, v)$ 일 때 가속도는 $a(x) = -\omega^2 x$. 각 적분기가 $(x_n, v_n) \to (x_{n+1}, v_{n+1})$ 를 어떻게 계산하는가:

**Explicit Euler (Forward Euler)** — *옛 v 로 x 갱신*:

$$
v_{n+1} = v_n + a(x_n)\,\Delta t, \qquad
x_{n+1} = x_n + v_n\,\Delta t
$$

```cpp
float a    = -w2 * o.x;
float newV = o.v + a * dt;
float newX = o.x + o.v * dt;   // 옛 v
o.v = newV;
o.x = newX;
```

핵심 함정. *순서를 의식 안 하면 newV 가 x 갱신에 끼어듦*. 그러면 그건 더 이상 Explicit 이 아니라 Semi-implicit. 가장 흔한 implementation bug.

**Semi-implicit Euler (Symplectic Euler)** — *새 v 로 x 갱신*:

$$
v_{n+1} = v_n + a(x_n)\,\Delta t, \qquad
x_{n+1} = x_n + v_{n+1}\,\Delta t
$$

```cpp
float a = -w2 * o.x;
o.v    += a * dt;
o.x    += o.v * dt;   // 새 v
```

코드 두 줄. 한 줄 순서가 바뀌었을 뿐인데 *수치 안정성 분류가 발산에서 symplectic 으로 점프*. 보존량을 정확히 보존하진 못하지만 *bounded 범위에서 진동* — 게임 시뮬에서는 충분.

**Velocity Verlet** — *위치는 옛 가속도로, 속도는 옛 + 새 가속도 평균*:

$$
\begin{aligned}
x_{n+1} &= x_n + v_n\,\Delta t + \tfrac{1}{2} a_n\,\Delta t^2 \\
a_{n+1} &= -\omega^2 x_{n+1} \\
v_{n+1} &= v_n + \tfrac{1}{2}(a_n + a_{n+1})\,\Delta t
\end{aligned}
$$

```cpp
float aOld = -w2 * o.x;
o.x       += o.v * dt + 0.5f * aOld * dt * dt;
float aNew = -w2 * o.x;
o.v       += 0.5f * (aOld + aNew) * dt;
```

evaluation 이 2 번 (`aOld` / `aNew`). 비용이 Semi-implicit 의 2 배지만 *2 차 정확도 + symplectic* — cloth / 입자 / 분자 동역학에서 표준. position-only Verlet (`x_{n+1} = 2x_n - x_{n-1} + a\,\Delta t^2`) 도 있지만 *velocity 가 명시적이지 않아* 충돌 / drag 적용이 까다롭다. velocity Verlet 이 게임에서 흔히 쓰임.

**RK4 (4 차 Runge-Kutta)** — 한 step 안에서 *4 번 evaluation* 후 가중 평균:

```cpp
auto f = [w2](float x, float v) {
    return std::pair<float, float>{v, -w2 * x};   // .first = dx/dt, .second = dv/dt
};
auto k1 = f(o.x, o.v);
auto k2 = f(o.x + 0.5f * dt * k1.first, o.v + 0.5f * dt * k1.second);
auto k3 = f(o.x + 0.5f * dt * k2.first, o.v + 0.5f * dt * k2.second);
auto k4 = f(o.x + dt * k3.first,         o.v + dt * k3.second);
o.x += dt / 6.0f * (k1.first  + 2*k2.first  + 2*k3.first  + k4.first);
o.v += dt / 6.0f * (k1.second + 2*k2.second + 2*k3.second + k4.second);
```

evaluation 4 번. *국소 오차가 $O(\Delta t^5)$* 로 가장 정밀하지만 *symplectic 이 아니다* — 매우 긴 시뮬에서는 *작은 drift 가 쌓임*. 게임에서 잘 안 쓰이는 이유는 *비용 4 배 + symplectic 이 아님*. 큰 dt 에서는 RK4 > Verlet 이지만 *작은 dt + 보존성* 이 중요한 게임에선 Verlet 의 가성비가 압도적.

## 2. 시스템: SHO 와 분석해

진폭 $A$, 주파수 $\omega$. 초기 조건 $x(0) = A$, $v(0) = 0$.

분석해:

$$
x(t) = A \cos(\omega t), \qquad v(t) = -A\omega \sin(\omega t)
$$

총 에너지:

$$
E(t) = \tfrac{1}{2} v^2 + \tfrac{1}{2} \omega^2 x^2 = \tfrac{1}{2} \omega^2 A^2 = E_0
$$

따라서 *모든 시점의 $E(t) / E_0 = 1$*. 적분기가 *어긋난 만큼* 그 비율이 1 에서 벗어난다. 그래프의 y 축이 $E/E_0$, 1.0 이 *이상적 보존*. 1.0 보다 위면 *에너지 추가* (Explicit Euler 의 폭주), 아래면 *에너지 손실* (드물게 발생, 일부 implicit scheme).

## 3. Scene 자료구조 + 누적기 (fixed dt + integDt)

Scene 안에 *4 개의 oscillator* 가 *각자의 상태* 를 갖는다. 모두 같은 $\omega, A$ 로 시작.

```cpp
struct Oscillator {
    float       x = 1.0f, v = 0.0f;
    bool        enabled = true;
    Color       color;
    GraphSeries energyHist;   // E / E_0 시간 추이
    GraphSeries posHist;
};

class IntegratorsScene : public IScene {
    Oscillator osc_[(int)IntegratorKind::COUNT];
    GraphSeries analyticalEnergy_;   // 항상 1.0
    GraphSeries analyticalPos_;      // A cos(ωt)

    float amplitude_     = 1.0f;
    float omega_         = 2π;       // 1 Hz
    float integDt_       = 1/120;    // 적분기 dt — 슬라이더로 변경
    float internalAccum_ = 0;        // main fixed dt 누적
    float timeElapsed_   = 0;
};
```

`main` 의 fixed dt 는 1/120 (= 8.33 ms). 적분기 dt 슬라이더가 1/240, 1/120, 1/60, 1/30 토글 가능. *시뮬레이션 wall time 은 같게 유지*:

```cpp
void Step(float dt) override {   // dt = main fixed dt = 1/120
    internalAccum_ += dt;
    while (internalAccum_ >= integDt_) {
        for (each enabled oscillator) {
            StepOne(kind, osc_[i], integDt_);
            osc_[i].energyHist.Push(Energy(osc_[i]) / E0_);
        }
        timeElapsed_ += integDt_;
        analyticalEnergy_.Push(1.0f);
        internalAccum_ -= integDt_;
    }
}
```

- integDt_ = 1/240 이면 main 의 한 frame step 마다 *substep 2 번* → 적분이 *더 정밀*.
- integDt_ = 1/120 이면 substep 1 번.
- integDt_ = 1/60 이면 main step *2 번 모이면* substep 1 번 → 적분이 *덜 정밀*.
- integDt_ = 1/30 이면 main step *4 번 모이면* substep 1 번 → *대부분 적분기에서 발산 시작*.

`while` 의 `guard < 32` 는 spiral of death 방지 — kFixedDt + integDt_ 조합이 잘못되어 substep 이 끝없이 도는 경우를 차단.

## 4. 에너지 그래프 + 진동자 시각화

캔버스 상단은 *5 개 트랙* (4 적분기 + 분석해). 각 트랙은 *수평선 + 그 위 마우스 처럼 흔들리는 점*. 색은 적분기별.

```cpp
auto drawTrack = [&](int idx, const char* name, Color c, float xVal) {
    int ty = trackTop + idx * trackHeight + trackHeight / 2;
    DrawLine(trackXStart, ty, trackXEnd, ty, ...);   // 가이드
    DrawCircle(trackXMid,  ty, 2.0f, ...);            // 원점 마커

    float t = xVal / amplitude_;
    int px = trackXMid + (int)(t * trackXSpan);
    DrawLineEx({trackXMid, ty}, {px, ty}, 2.0f, c);   // 변위 화살표
    DrawCircle(px, ty, 7.0f, c);                       // mass
};
```

Explicit Euler 의 점이 *시간이 지날수록 양옆으로 점점 멀어짐* — 진폭이 발산하는 모습. RK4 / Verlet / 분석해의 점이 *거의 같은 위치에서 흔들리는* 반면.

캔버스 하단은 *에너지 그래프* — y 축이 $E / E_0$. 5 개 시리즈 (4 적분기 + 분석해 1.0 점선).

```cpp
GraphInput inputs[5] = {
    {&osc_[0].energyHist, red,    "Explicit Euler"},
    {&osc_[1].energyHist, blue,   "Semi-impl. Euler"},
    {&osc_[2].energyHist, green,  "Velocity Verlet"},
    {&osc_[3].energyHist, yellow, "RK4"},
    {&analyticalEnergy_,  grey,   "E_0 (ideal)"},
};
DrawLineGraph(rect, inputs, 5, "Energy / E_0", 0.5f, 1.5f, true);
```

`autoY = true` 라 *Explicit 이 폭주하면 y 축이 자동 확장* — 분석해의 1.0 이 그래프 바닥 근처로 압축되어 보임. *시각적 임팩트* 가 큰 표현.

## 5. 무엇을 보는가: 발산 / 보존 / drift

세 시나리오를 토글로 만들어 본다.

**시나리오 A — `dt = 1/30`, $\omega = 6.28$ (1 Hz).**

- Explicit Euler (빨강): $E/E_0$ 가 시간에 따라 *지수적으로 증가*. 5 ~ 10 초 만에 그래프 천장을 넘어감. 진동자 화면에서 *진폭이 점점 커져 화면 밖으로*.
- Semi-implicit Euler (파랑): $E/E_0$ 가 *1.0 주위로 진동*. 진폭은 안정. *완벽 보존은 아니지만 bounded*.
- Verlet (초록): $E/E_0$ 가 *1.0 에 거의 붙어 약하게 진동*. Semi-implicit 보다 진동 폭이 작음.
- RK4 (노랑): $E/E_0$ 가 *1.0 근처 + 매우 느린 drift*. 짧은 시뮬에서는 가장 정확.

**시나리오 B — `dt = 1/240`, 같은 $\omega$.**

네 적분기 모두 *거의 1.0 에 붙음*. Explicit 도 *수초 동안은* 발산이 보이지 않음. *dt 가 충분히 작으면 모든 적분기가 잘 동작한다* 는 사실. 하지만 step 이 4 배.

**시나리오 C — `dt = 1/30`, $\omega = 20$ (3.18 Hz, 더 빠른 진동).**

- Explicit: 1 초 만에 폭주.
- Semi-implicit: *약간의 폭주 보임* — $\omega \Delta t = 20 \times 1/30 \approx 0.67$. symplectic 의 안정 한계 $\omega \Delta t < 2$ 근처지만 잘 동작. 더 큰 $\omega$ 면 *Semi-impl 도 폭주*.
- Verlet: 정상.
- RK4: 정상.

이 셋을 직접 토글해 보면 시리즈 글의 *"Euler 가 발산"* 같은 추상적 주장이 *그래프 곡선 한 개로 압축*. 글에서 *왜 게임 엔진이 Semi-impl 또는 Verlet 을 쓰는가* 가 *이 한 그래프 안에 있다*.

## 6. 적분기 코드의 위치와 헤드리스 테스트

위 네 식은 Scene 안에 흩어져 있지 않다. 이 단계에서 `src/core/physics_core.h` 라는 *Scene 공용 계산 코어* 를 새로 만들고, 네 적분기를 그 안의 `StepHarmonic` 한 함수로 승격한다. Scene 은 렌더링·UI 만 갖고 계산은 core 가 갖는다 — 그래야 *raylib 창 없이* 적분기를 테스트할 수 있다.

```cpp
// src/core/physics_core.h
namespace physlab::core {

enum class HarmonicIntegrator {
    ExplicitEuler, SemiImplicitEuler, VelocityVerlet, RK4,
};

inline void StepHarmonic(HarmonicIntegrator kind, float omega,
                         float dt, float& x, float& v);

}  // namespace physlab::core
```

`IntegratorsScene` 쪽은 위임 한 줄:

```cpp
void IntegratorsScene::StepOne(IntegratorKind kind,
                                  Oscillator& o,
                                  float dt) const {
    core::StepHarmonic(
        static_cast<core::HarmonicIntegrator>((int)kind),
        omega_, dt, o.x, o.v);
}
```

core 가 헤더 하나라 테스트 타깃은 실행 파일 한 개면 된다. `CMakeLists.txt` 에 추가:

```cmake
add_executable(physics_core_tests tests/physics_core_tests.cpp)
target_include_directories(physics_core_tests PRIVATE src)
enable_testing()
add_test(NAME physics_core_tests COMMAND physics_core_tests)
```

`tests/physics_core_tests.cpp` 는 dt 를 절반으로 줄였을 때 위치 오차가 줄어드는 *수렴비* 를 본다 — Explicit Euler 는 1 차 (비 > 1.7), Velocity Verlet 은 2 차 (> 3.5), RK4 는 4 차 (> 10).

실행:

```bash
ctest --test-dir build -C Release --output-on-failure
```

기대 출력:

```text
    Start 1: physics_core_tests
1/6 Test #1: physics_core_tests ...............   Passed    0.06 sec
```

수렴비만으로는 "Explicit 은 에너지가 발산하고 Semi-implicit 은 유계" 라는 이 Scene 의 간판 주장을 잡지 못한다. 둘 다 조화 진동자에서 전역 오차가 1 차라 비가 비슷하게 나오고, 두 분기를 서로 바꿔치기해도 통과한다. 그래서 에너지 `E = ½v² + ½ω²x²` 를 직접 재는 검사를 같은 파일에 둔다 (ω = 1, x₀ = 1, dt = 0.1, 200 스텝).

```cpp
// Explicit Euler 는 매 스텝 (1 + w^2 dt^2) 배로 커진다.
CHECK(monotone);              // e 가 직전보다 줄지 않는다
CHECK(prevE / e0 > 1.5f);     // 실측 약 7.3 배
// Semi-implicit / Verlet 은 유계 진동.
CHECK(worst < 0.15f);         // |e/e0 - 1| 의 최댓값
CHECK(minX < -0.9f);          // 실제로 진동하는가
```

한 스텝만 돌려 두 적분기의 위치가 갈라지는 것도 리터럴로 못 박는다 — Explicit 은 옛 속도로 옮겨 `x = 1.0`, Semi-implicit 은 갱신된 속도로 옮겨 `x = 0.99`. 이 두 줄이 있으면 `ExplicitEuler` 분기를 semi-implicit 코드로 바꾸거나 `SemiImplicitEuler` 분기를 통째로 지우는 변형이 모두 실패한다.

```mermaid
flowchart LR
    A[dt 슬라이더] --> B[integDt_ 갱신]
    B --> C[Reset E0_, history]
    C --> D[main loop: Step(1/120)]
    D --> E["while accum >= integDt: substep"]
    E --> F[oscillator 4개 적분]
    F --> G[Energy / E_0 history push]
    G --> H[Draw: 5 tracks + energy graph]
```

---

## 결론

Step 2 에서 추가한 것.

1. **4 적분기 구현 + core 모듈 승격** — Explicit / Semi-impl / Verlet / RK4. 각자 식이 명료한 4~10 줄. 네 분기는 `src/core/physics_core.h` 의 `core::StepHarmonic` 에 모이고 Scene 은 위임만 한다.
2. **SHO 시스템 + 분석해 기준선** — 적분기 정확성을 *분석해 위치* + *에너지 보존* 두 가지로 동시 검증.
3. **integDt 누적기** — main 의 fixed dt 와 분리. 사용자가 *시뮬레이션 wall time 은 그대로 두고 적분 dt 만 바꿔* 발산 임계를 탐색.
4. **GraphSeries / LineGraph** — 시간 시리즈 (원형 버퍼) + 라인 차트. 두 위젯 모두 Scene 에 독립적인 공용 UI.
5. **5 track 시각화 + 에너지 그래프** — 발산이 *진동자 진폭이 화면 밖으로 + 에너지 곡선이 천장을 뚫음* 두 표현 동시에.
6. **`physics_core_tests` 타깃** — `ctest --test-dir build -C Release` 로 적분기 오차 차수를 GUI 없이 검사.

이 단계에서 자동으로 검증되는 것은 *오차 차수* 뿐이다. 에너지 유계성·발산은 그래프로만 확인할 수 있고, `SemiImplicitEuler` 분기는 어떤 테스트도 호출하지 않는다.

## 더 읽을거리

- Glenn Fiedler, ["Integration Basics"](https://gafferongames.com/post/integration_basics/) — Euler→RK4 비교의 표준 입문.
- David Baraff, Andrew Witkin, ["Physically Based Modeling" 강의 노트](https://www.cs.cmu.edu/~baraff/sigcourse/) (SIGGRAPH Course) — 적분·제약의 이론적 기초.
- Ernst Hairer 외, *Geometric Numerical Integration* — symplectic 적분기가 에너지를 보존하는 이유의 수학.
