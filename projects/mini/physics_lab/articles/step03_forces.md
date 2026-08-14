## 들어가며

`physics_lab` 의 세 번째 단계. *단일 mass 에 여러 force 를 켜고 끄며 운동 / 위치 / 총 에너지 그래프* 를 본다. 적분기는 Step 2 의 *Semi-implicit Euler* 로 고정 — 적분기 비교는 끝났고, 이 Scene 의 변수는 *어떤 외력이 작용하는가*.

세 종류 force.

| 종류 | 식 | 보존성 | 효과 |
|---|---|---|---|
| 중력 | $F_g = m g \hat{y}$ | 보존 (potential $U_g = mgh$) | 일정한 아래 방향 가속 |
| 스프링 | $F_s = -k (\|d\| - L_0) \hat{d}$ | 보존 (potential $U_s = \tfrac{1}{2} k (\|d\| - L_0)^2$) | anchor 와의 거리 차이에 비례한 복원력 |
| 드래그 (linear) | $F_d = -c_1 v$ | 비보존 (에너지 소산) | 속도에 비례한 저항 (저속) |
| 드래그 (quadratic) | $F_d = -c_2 \|v\| v$ | 비보존 | 속도 제곱에 비례한 저항 (고속) |

핵심 관찰. *보존력 (gravity, spring) 만 있으면 총 에너지가 일정하게 유지* — Semi-implicit 의 약 보존성 안에서. *드래그가 켜지면 단조 감소* — 마찰의 정의 자체.

이 단계의 그래프는 *세 시리즈* — KE (운동 에너지), PE (위치 에너지), Total. 토글이 어떻게 두 곡선의 패턴을 바꾸는지가 학습 목적.

---

## 목차
1. [네 force 의 식과 코드](#1-네-force-의-식과-코드)
2. [에너지 정의와 단위 변환](#2-에너지-정의와-단위-변환)
3. [Scene 구조: 두 핸들 + pulse](#3-scene-구조-두-핸들--pulse)
4. [세 가지 시나리오](#4-세-가지-시나리오)

---

## 1. 네 force 의 식과 코드

모든 force 가 *현재 상태 $(x, v)$ 만* 으로 결정. mass 한 개의 net force 를 한 함수에서 계산.

```cpp
Vector2 ForcesScene::ComputeForce(Vector2 pos, Vector2 vel) const {
    Vector2 f = {0, 0};

    if (gravityOn_) {
        f.y += mass_ * gravityG_;          // 아래 방향 (화면 좌표에서 y+)
    }
    if (springOn_) {
        Vector2 a   = anchorHandle_.Position();
        Vector2 d   = {pos.x - a.x, pos.y - a.y};
        float   len = std::sqrt(d.x * d.x + d.y * d.y);
        if (len > 1e-4f) {
            float scale = -springK_ * (len - springRest_) / len;
            f.x += scale * d.x;             // anchor 방향 (rest 보다 멀면 당겨옴)
            f.y += scale * d.y;
        }
    }
    if (dragLinear_ > 0.0f) {
        f.x += -dragLinear_ * vel.x;
        f.y += -dragLinear_ * vel.y;
    }
    if (dragQuad_ > 0.0f) {
        float speed = std::sqrt(vel.x * vel.x + vel.y * vel.y);
        f.x += -dragQuad_ * speed * vel.x;
        f.y += -dragQuad_ * speed * vel.y;
    }
    return f;
}
```

스프링의 *부호*. `d = pos - anchor` 가 anchor 에서 mass 로 향하는 벡터. `len - rest > 0` 이면 *과늘어남* → force 는 anchor 쪽 (`-d` 방향) → 계수 `-k(len - rest) / len`. 이 식이 *spring 의 1 D Hooke 의 법칙을 그대로 2 D 로 확장* — `d` 방향에 따라 잡아당기거나 밀어냄.

`len < 1e-4f` 분기는 *anchor 와 mass 가 정확히 일치할 때의 0 / 0* 방지. 그런 경우 force 0 (정의되지 않음).

linear vs quadratic drag.

- linear $F = -c_1 v$: 저속 (Reynolds 수 작음) 에서 점성 유체 / Stokes drag. 작은 입자가 점성 유체에서 떨어질 때.
- quadratic $F = -c_2 \|v\| v$: 고속 (Reynolds 수 큼) 에서 압력 drag. 큰 물체 / 공기 저항. 자동차, 공.

게임에서는 둘 다 쓰임. terminal velocity 는 *둘 다 유한* — 중력과 drag 이 평형이 되는 속도. 차이는 *식의 형태와 접근 곡선*:

$$
\text{linear:} \quad m g = c_1 v_t \;\Rightarrow\; v_t = \frac{m g}{c_1}, \quad v(t) = v_t \bigl(1 - e^{-(c_1/m) t}\bigr)
$$

$$
\text{quadratic:} \quad m g = c_2 v_t^2 \;\Rightarrow\; v_t = \sqrt{\frac{m g}{c_2}}, \quad v(t) = v_t \tanh\!\bigl((g/v_t) t\bigr)
$$

둘 다 *asymptotic* — 유한한 $v_t$ 에 *지수적 / 쌍곡탄젠트적* 으로 접근하지만 정확히 도달하진 못함. 차이는 *고속에서의 감속 강도*. quadratic 은 $v$ 가 커질수록 *제곱* 으로 저항이 커지므로 *공기 중 낙하* 처럼 빠르게 $v_t$ 근처로 수렴한다. linear 는 *지수 접근* — 천천히 수렴하지만 부드럽다. 낙하 시뮬에 *어떤 게 현실적인가* 는 물체 크기 / 속도 / 매질에 달림.

## 2. 에너지 정의와 단위 변환

화면 좌표는 픽셀, 시뮬은 *임시로 픽셀 단위* 로 진행. 에너지를 *m, s 단위* 로 보려면 *100 px = 1 m* 변환을 그래프 push 시점에 한 번 적용.

```cpp
float floorRef = (float)(lastCanvasY_ + lastCanvasH_);
float h        = (floorRef - pos_.y) / 100.0f;        // m
float vMs2     = (vel_.x*vel_.x + vel_.y*vel_.y) / (100.0f * 100.0f);   // m²/s²

float KE       = 0.5f * mass_ * vMs2;
float PE_grav  = gravityOn_ ? (mass_ * (gravityG_ / 100.0f) * h) : 0.0f;
float PE_spring = 0.0f;
if (springOn_) {
    Vector2 a   = anchorHandle_.Position();
    Vector2 d   = {pos_.x - a.x, pos_.y - a.y};
    float   len = std::sqrt(d.x*d.x + d.y*d.y);
    float   dx  = (len - springRest_) / 100.0f;        // m
    PE_spring   = 0.5f * springK_ * dx * dx;
}
float PE = PE_grav + PE_spring;
keHist_.Push(KE);
peHist_.Push(PE);
totalHist_.Push(KE + PE);
```

PE_grav 의 *기준선* 은 캔버스 바닥. mass 가 바닥에 있으면 $h = 0$, PE_grav = 0. *기준선 선택은 자유*. 다만 같은 시뮬에서 *고정* 해야 그래프가 의미 있다.

drag 가 한 work 는 *명시적으로 표현 안 함* — 그냥 Total 이 감소하는 것을 보면 된다. 더 정확히 측정하려면 *각 step 의 $F_d \cdot v\,\Delta t$* 를 누적하면 되지만, total energy 시계열만으로도 *drag 가 얼마나 빠르게 에너지를 빼는가* 가 보임.

## 3. Scene 구조: 두 핸들 + pulse

핸들 두 개 — *mass 위치 리셋* 핸들 + *spring anchor* 핸들. DragHandle 의 *한 번에 한 핸들만 잡힘* 정책으로 *겹쳐도 가까운 한 개만* 잡힘.

```cpp
DragHandle massHandle_;     // 빨강 — mass 위에 떠 있음
DragHandle anchorHandle_;   // 초록 — anchor 위에
```

`Step(dt)`:

```cpp
if (massHandle_.IsDragging()) {
    pos_ = massHandle_.Position();
    vel_ = {0, 0};
    keHist_.Clear(); peHist_.Clear(); totalHist_.Clear();
    return;
}
// pending impulse 적용 (P 키 — pulse 버튼)
if (pulsePending_) { vel_ += pulseDv_; pulsePending_ = false; }

// Semi-implicit Euler
Vector2 f = ComputeForce(pos_, vel_);
vel_ += (f / mass_) * dt;
pos_ += vel_ * dt;
// ... 벽 / 바닥 충돌 (단순 응답) ...
```

`P` 키 / Pulse 버튼이 *위 방향 임펄스* (`-400 px/s` $v_y$). 정지 상태에서 *튀어오르게* 만드는 트리거. spring 이 켜져 있으면 *튀어올랐다가 매달려 진동*. drag 가 있으면 *진동이 점점 감쇠*.

벽 / 바닥 충돌은 *이 Scene 자체의 단순 응답* — `forces_scene.cpp` 안에 `0.5f` 로 하드코딩된 반발 계수 + 위치 교정 + 속도 반전. Step 1 의 Welcome Scene 은 `restitution_ = 0.6f` 를 쓰므로 두 값은 서로 다르다. 이 Scene 에서는 *force 차이* 가 주제이므로 충돌은 *최소한*.

## 4. 세 가지 시나리오

**시나리오 A — `gravity = on, spring = off, drag = 0`. P 누름.**

mass 가 *위로 튀어올랐다가 자유낙하 → 바닥 충돌 → 점점 작아지는 bounce*. KE 와 PE 가 *서로 교대로 증감*. 단순 응답의 *restitution $e = 0.5$* 때문에 매 충돌마다 *법선 속도가 절반 → 법선 운동에너지가 $e^2 = 0.25$ 배만 남음 → 75% 손실*. 수직 충돌이라 *법선 = 전체 속도* 라서 한 번 튕길 때마다 KE 가 *1/4 로 감소* → Total 이 *계단식으로 4 배씩 작아짐*. (충돌이 *비탄성* 이라 보존력만 있어도 손실 발생 — 충돌 응답이 비보존인 셈)

**시나리오 B — `gravity = on, spring = on, anchor 가 mass 위`. P 누름.**

mass 가 *위로 튀어올랐다가 → spring 이 잡아당김 → 평형점 (gravity / spring 균형) 주위 진동*. KE 와 PE_total 이 *주기적으로 교대*. drag 가 0 이면 *Total 거의 일정* — Semi-implicit 의 약 보존성 안에서 *약하게 진동* (energy 가 1.001, 0.999 사이를 오감).

**시나리오 C — `gravity = on, spring = on, drag_linear = 1.0`. P 누름.**

진동이 *지수 감쇠*. 평형점 근처에서 천천히 정지. Total 이 *exponential decay 형태* 로 감소. drag_quad 만 켜면 *고속에서 빠르게 감속, 저속에선 거의 안 줄어듦* — 곡선 모양이 다름. 둘 다 켜면 두 mechanism 의 합.

```mermaid
flowchart TB
    A[ComputeForce] -->|gravity| B[F_g = mg]
    A -->|spring| C[F_s = -k(|d|-L0) ĥ_d]
    A -->|linear drag| D[F_dl = -c1 v]
    A -->|quad drag| E[F_dq = -c2 |v| v]
    B & C & D & E --> F[Sum F]
    F --> G[Semi-impl Euler]
    G --> H[pos, vel update]
    H --> I[Energy update KE / PE / Total]
    I --> J[Graph push]
```

---

## 결론

Step 3 에서 추가한 것.

1. **4 종 force 의 식 + 단일 함수 구현** — `ComputeForce(pos, vel)` 한 함수에 토글 분기. 추가 force 가 필요해지면 *조건 한 줄 + 식 한 줄*.
2. **에너지 정의: KE / PE_grav / PE_spring** — 그래프로 *보존 / 비보존* 의 시각적 차이.
3. **두 핸들 + pulse 입력** — DragHandle 의 한 번에 한 핸들 정책으로 겹쳐도 안 충돌. impulse 는 *Step 안에서 한 번 적용 후 flag clear*.
4. **단위 변환 (100 px = 1 m)** — 그래프 라벨이 *m / s 기준* — 시리즈 본문과 동일한 단위.

이 Scene 이 다루는 것은 mass 한 개에 걸리는 force 뿐이다. 물체가 여러 개일 때의 충돌 후보 추출·접촉 판정·응답은 여기에 없고, 자동 테스트도 늘지 않는다.

## 더 읽을거리

- David Baraff, Andrew Witkin, ["Physically Based Modeling": Particle System Dynamics](https://www.cs.cmu.edu/~baraff/sigcourse/) — 힘 누적·스프링·감쇠의 표준 정리.
- Erin Catto, [Box2D 소스의 b2 World Step](https://github.com/erincatto/box2d) — 프로덕션에서의 힘/임펄스 적용 순서.
