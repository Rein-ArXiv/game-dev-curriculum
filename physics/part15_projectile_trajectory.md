## 들어가며

발사체 (projectile) 는 게임의 *가장 흔한 물리 객체* — 화살, 총알, 마법, 미사일, 던지는 박스. 단순한 운동학으로 보이지만, 실제 게임에서는 *표적 예측, 공기 저항, 충돌 ToI* 등 작은 디테일이 쌓인다.

이 글의 목표.

1. **진공 포물선** — 다수의 게임 발사체가 쓰는 기본 모델.
2. **선형 항력** — 단순 감쇠, 해석해 존재.
3. **이차 항력과 종단 속도** — 현실 공기 저항, 수치 적분 필요.
4. **움직이는 표적 예측** — 적이 미사일을 *미래 위치* 로 발사.
5. **발사각 역산** — 목표 좌표에 도달하는 *발사각 계산*.
6. **충돌 ToI 와 sweep** — 빠른 발사체의 터널링 방지.
7. **궤적 시각화** — 발사 전 예측 궤적 표시.
8. **게임 적용 패턴**.

발사체는 *대부분 게임 코드에서 직접 시뮬* 된다 — 강체 솔버 거치지 않고 *직접 적분*. 단순함과 *예측 가능성* 이 결정적.


---

## 목차
1. [진공 포물선](#1-진공-포물선)
2. [선형 항력 모델](#2-선형-항력-모델)
3. [이차 항력과 종단속도](#3-이차-항력과-종단속도)
4. [움직이는 표적 예측](#4-움직이는-표적-예측)
5. [발사각 역산](#5-발사각-역산)
6. [충돌 ToI와 swept test](#6-충돌-toi와-swept-test)
7. [궤적 시각화](#7-궤적-시각화)
8. [게임 적용 패턴](#8-게임-적용-패턴)


---

## 1. 진공 포물선

가장 단순한 발사체 모델. *중력만* — 공기 저항 무시.

### 1.1 운동 방정식

위치, 속도, 가속도:

$$
\vec{r}(t) = \vec{r}_0 + \vec{v}_0 t + \tfrac{1}{2} \vec{g} t^2
$$
$$
\vec{v}(t) = \vec{v}_0 + \vec{g} t
$$
$$
\vec{a}(t) = \vec{g} = \text{const}
$$

중력 $\vec{g}$ 가 *유일한 외력*. 게임 단위에서 $|\vec{g}| \approx 9.8 \text{ m/s}^2$ — 또는 *게임 톤에 맞춰* 더 큰 값 (15 ~ 30 m/s²) 으로 *빠른 떨어짐*.

### 1.2 2D 포물선

xy 평면에서. 수평 / 수직 분리:

$$
x(t) = x_0 + v_{0x} t
$$
$$
y(t) = y_0 + v_{0y} t - \tfrac{1}{2} g t^2
$$

수평은 *등속*, 수직은 *등가속*.

### 1.3 코드

```cpp
struct Projectile {
    vec3 position;
    vec3 velocity;
};

void StepProjectile(Projectile& p, vec3 gravity, float dt) {
    p.velocity += gravity * dt;
    p.position += p.velocity * dt;
}
```

semi-implicit Euler. 두 줄. 다수의 게임 발사체는 이 정도 모델로도 충분하다.

실행 모델과 예측·조준 모델은 분리해 고정해야 한다. semi-implicit step 으로 실행하면 누적 이산 오차가 생기므로, 닫힌 형식 예측으로 조준한 각도가 실제 적분 궤적과 어긋날 수 있다. 각 실행 모드 옆에 대응하는 predictor/aim 방식을 함께 적는다. 정확한 정지 표적 조준이 목표면 실행과 같은 해석적 모델을 쓰거나 실제 수치 step 과 같은 식으로 역산한다 (7.2, 8.3절).

### 1.4 닫힌 형식

매 step 적분 안 하고 *시간 t* 에서 *직접 평가* 가능.

```cpp
vec3 ProjectilePosAt(vec3 r0, vec3 v0, vec3 g, float t) {
    return r0 + v0 * t + g * (0.5f * t * t);
}
```

*미래 위치 예측* 에 유용. AI 가 *발사 시점에 표적 시간 t 후 위치* 를 한 줄로 계산.

### 1.5 최대 높이, 도달 시간, 사거리

수직 운동의 기본 양들.

```math
\text{최대 높이 시간:} \quad t_{peak} = \frac{v_{0y}}{g}
\text{최대 높이:} \quad h_{max} = \frac{v_{0y}^2}{2g}
\text{공중 체공 시간:} \quad t_{air} = \frac{2 v_{0y}}{g}  (\text{지면 출발 / 도착})
\text{사거리:} \quad R = \frac{v_0^2 \sin(2\theta)}{g}
```

마지막 식이 *발사각 + 초기 속력으로 사거리*. 45° 가 최대 사거리.

### 1.6 게임 단순화

게임은 보통 *진공 포물선만*. 화살 / 총알 / 폭탄. 사용자가 *기대* 하는 단순한 동작 + 예측 가능성. 공기 저항은 짧은 비행·낮은 속도에서는 시각 차이가 작지만, 거리·속도·도달 시간을 바꾸는 모델 파라미터이므로 항상 무시할 수 있는 것은 아니다. 그래서 필요한 정확도에 맞춰 진공 근사로 두는 경우가 많다.

## 2. 선형 항력 모델

공기 저항의 가장 단순한 모델.

### 2.1 식

$$
\vec{F}_d = -k \vec{v}
$$

속도에 비례. 항상 *속도 반대 방향*. 작은 속도에 적합 (느린 객체).

운동 방정식:

$$
m \dot{\vec{v}} = m \vec{g} - k \vec{v}
$$

### 2.2 해석해

선형 ODE — *닫힌 형식 해* 존재.

수직 성분 (낙하):

$$
v_y(t) = -v_{term} + (v_{0y} + v_{term}) e^{-t/\tau}
$$

여기서 $v_{term} = mg/k$ (종단 속도), $\tau = m/k$ (시간 상수).

직관: *시간 흐를수록 종단 속도에 지수적 접근*. $\tau$ 가 *수렴 시간*.

### 2.3 수평 성분

수평은 *중력 없음* — 순수 지수 감쇠.

$$
v_x(t) = v_{0x} e^{-t/\tau}
$$

*시간 흐를수록 0 으로 수렴*. 수평 거리 적분:

$$
x(t) = x_0 + v_{0x} \tau (1 - e^{-t/\tau})
$$

t → ∞ 에서 *수평 거리 $v_{0x} \tau$ 로 수렴*. 무한대 못 감.

### 2.4 코드

```cpp
void StepProjectileLinearDrag(Projectile& p, vec3 gravity,
                                float dragPerMass, float dt) {
    vec3 dragAccel = p.velocity * (-dragPerMass); // = -(k/m) v
    p.velocity += (gravity + dragAccel) * dt;     // 진공보다 한 항 추가
    p.position += p.velocity * dt;
}
```

진공 모델에 *drag acceleration 항 한 줄 추가*. 물리식의 $k$ 를 그대로 쓰려면 `dragPerMass = k / m` 으로 변환한다.

### 2.5 단점

선형 항력은 낮은 Reynolds 수의 Stokes regime에서 물리적 모델이 될 수 있고, 게임에서는 조절하기 쉬운 감쇠 근사로도 쓴다. 일반적인 공기 중 빠른 발사체에는 이차 항력이 더 적합하지만 경계는 속도 하나가 아니라 유체 밀도·점성·물체 길이 scale에 좌우된다.

게임에서 *시각적 부드러움* 위주면 OK. 정확한 시뮬은 이차 항력.

### 2.6 게임 활용

- *깃털, 종이* 같은 *매우 느린* 객체.
- *부드러운 감쇠 효과* — 던진 폭탄이 *너무 멀리 안 나가게*.

대부분 게임이 *진공 또는 이차*. 선형은 *간단한 감쇠* 가 필요할 때.

## 3. 이차 항력과 종단속도

현실 공기 저항의 표준 모델.

### 3.1 식

$$
\vec{F}_d = -\tfrac{1}{2} \rho C_d A |\vec{v}| \vec{v}
$$

$\rho$ = 공기 밀도, $C_d$ = 항력 계수 (drag coefficient, 도형 형태), $A$ = 단면적, $|\vec{v}| \vec{v}$ = 속도 크기 × 속도 벡터.

여기서 $\vec{v}$ 는 주변 유체에 대한 *물체의 상대 속도* 다. 무풍일 때만 world velocity 와 같고, 바람이 있으면 `worldVelocity - windVelocity` 를 써야 한다. 가속도 식으로 옮기면 항력 계수는 $\rho C_d A / (2m)$ 이므로 `dragCoef` 에 질량과 기준 면적을 어떻게 접었는지 명시한다.

크기는 *속도 제곱*, 방향은 *속도 반대*.

### 3.2 종단 속도

수직 낙하의 *평형 속도*:

$$
v_{term} = \sqrt{\frac{2 m g}{\rho C_d A}}
$$

물리적 의미: *항력이 중력과 정확히 균형* — 가속도 0. 충분히 떨어지면 *이 속도로 수렴*.

| 객체 | 종단 속도 (m/s) |
|---|---|
| 사람 (자유 낙하) | 50 |
| 사람 (낙하산 펼침) | 5 |
| 깃털 | 0.4 |
| 골프공 | 32 |
| 빗방울 | 9 |

이 표는 크기·자세·기준 면적·고도(공기 밀도) 조건이 다른 대표값을 모은 *비실측 튜닝 예시* 다. 실제 값은 $C_d$ 와 $A$ 정의에 따라 크게 달라지므로 게임에서는 측정·튜닝 대상으로 둔다.

### 3.3 닫힌 형식 없음

이차 항력은 비선형 ODE다. 일정 중력과 한 축 운동처럼 제한된 경우에는 `tanh` 등을 이용한 닫힌 해가 있지만, 중력에 수직인 속도 성분까지 있는 일반 2D/3D 궤적은 단순 포물선 형태의 닫힌 해가 없어 수치 적분을 주로 사용한다.

```cpp
void StepProjectileQuadDrag(Projectile& p, vec3 gravity,
                                float dragCoef, float dt) {
    float vMag = length(p.velocity);
    vec3  drag = -dragCoef * vMag * p.velocity;
    p.velocity += (gravity + drag) * dt;
    p.position += p.velocity * dt;
}
```

이 한 줄 *비선형* 추가. semi-implicit Euler 의 안정성은 큰 $c|\vec{v}|dt$ 에서 무너지므로, $h c |\vec{v}|$ 가 충분히 작은 동안만 이산화가 신뢰할 만하다. 그 범위를 넘으면 아래 split 이나 substep 을 쓴다.

### 3.4 작은 dt 권장

명시적 update는 큰 $c|\vec{v}|dt$에서 항력만으로 속도 방향을 뒤집을 수 있다. 하지만 전체 `newVel`과 이전 속도의 내적을 보고 0으로 clamp하면 발사체의 정상적인 apex나 중력에 의한 방향 전환까지 지워 버린다.

```cpp
// 1차 operator split 예시:
// 중력을 먼저 적용하고, dv/dt = -c |v| v의 radial 감쇠를
// 한 step 동안 해석적으로 적용해 항력 자체의 방향 반전을 막는다.
vec3 vStar = p.velocity + gravity * dt;
float speed = length(vStar);
p.velocity = vStar / (1.0f + dragCoef * speed * dt);
p.position += p.velocity * dt;
```

이는 결합된 중력+항력 ODE의 정확해가 아니라 first-order splitting이다. 더 높은 정확도가 필요하면 적응형 RK, implicit 방법 또는 충분히 작은 substep을 기준 해와 비교한다.

### 3.5 게임에서의 사용

대부분 게임은 *이차 항력 사용 안 함*. 이유:

- *예측 어려움*: 닫힌 형식 없어 *AI 의 표적 계산* 복잡.
- *시각 차이 미미*: 짧은 발사체 (총알) 는 *공기 저항 효과 안 보임*.
- *게임플레이 단순함* 우선.

긴 거리 발사체 (저격수 시뮬, 항공 시뮬) 만 이차 항력. *Sniper Elite*, *War Thunder* 등.

### 3.6 항력 계수 매개변수

$C_d$ 값.

| 도형 | $C_d$ |
|---|---|
| 구 | 0.47 |
| 큐브 | 1.05 |
| 유선형 | 0.04 |
| Drag 만 (Disc) | 1.2 |

게임은 *단순 곱셈 상수* 로 — $-c \cdot |\vec{v}| \vec{v}$. 디자이너 튜닝. 이 $C_d$ 표 역시 Reynolds/Mach 수와 자세에 따라 변하는 대표값이며 특정 조건의 상수가 아니다.

## 4. 움직이는 표적 예측

AI 가 *움직이는 표적* 을 미사일로 맞추기.

### 4.1 문제 설정

- 발사 위치 $\vec{p}_s$, 발사체 속력 $s$ (스칼라).
- 표적 위치 $\vec{p}_t$, 표적 속도 $\vec{v}_t$ (등속 직선 가정).

*발사체와 표적이 만나는 시간 $t$* 와 *발사 방향* 찾기.

### 4.2 만남 조건

$$
|\vec{p}_t + \vec{v}_t t - \vec{p}_s| = s \cdot t
$$

발사체가 *거리 = 속력 × 시간* 이동. 표적의 *t 후 위치* 와 같음.

### 4.3 이차 방정식

양변 제곱:

$$
|\vec{p}_t - \vec{p}_s + \vec{v}_t t|^2 = s^2 t^2
$$

$\vec{d} = \vec{p}_t - \vec{p}_s$ 두면:

$$
|\vec{d}|^2 + 2 (\vec{d} \cdot \vec{v}_t) t + |\vec{v}_t|^2 t^2 = s^2 t^2
$$

정리:

$$
(|\vec{v}_t|^2 - s^2) t^2 + 2 (\vec{d} \cdot \vec{v}_t) t + |\vec{d}|^2 = 0
$$

표준 이차 방정식 $at^2 + bt + c = 0$.

### 4.4 풀이

```cpp
bool PredictHitTime(vec3 ps, float s, vec3 pt, vec3 vt, float& tOut) {
    vec3 d = pt - ps;
    if (!(s > 0.0f)) return false;                 // 발사체 속력은 양수
    if (!(length_squared(d) > 0.0f)) {             // 동일 위치 — 이미 표적
        tOut = 0.0f;
        return true;
    }

    auto dotD = [](vec3 x, vec3 y) {
        return double(x.x)*y.x + double(x.y)*y.y + double(x.z)*y.z;
    };
    double a = dotD(vt, vt) - double(s) * s;
    double b = 2.0 * dotD(d, vt);
    double c = dotD(d, d);

    if (a == 0.0) {
        // 발사체 속력 = 표적 속력 — 선형
        if (b == 0.0) return false;
        double t = -c / b;
        if (!(t > 0.0)) return false;
        tOut = (float)t;
        return true;
    }

    double disc = b * b - 4.0 * a * c;
    double tol = 64.0 * std::numeric_limits<double>::epsilon()
               * (b * b + std::fabs(4.0 * a * c));
    if (disc < -tol) return false;                 // 해 없음 — 발사체로 못 따라잡음
    disc = std::max(0.0, disc);

    // -b ± sqrtD 가 가까울 때 작은 양근이 0 으로 소실되는 것을 막는 안정형
    double sqrtD = std::sqrt(disc);
    double q = -0.5 * (b + std::copysign(sqrtD, b));
    if (q == 0.0) return false;
    double t1 = q / a;
    double t2 = c / q;

    // 최종 근을 원래 거리식에 대입해 잔차를 확인한다.
    auto valid = [&](double t) {
        if (!(t > 0.0)) return false;
        vec3 future = pt + vt * (float)t;
        double dist = (double)length(future - ps);
        return std::fabs(dist - (double)s * t) <= 1e-4 * std::max(1.0, (double)s * t);
    };

    double best = 1e30;
    if (valid(t1)) best = std::min(best, t1);
    if (valid(t2)) best = std::min(best, t2);
    if (best >= 1e30) return false;
    tOut = (float)best;
    return true;
}
```

`a`, `b`, `c`는 단위가 다르므로 그 크기를 직접 섞어 퇴화를 판정하지 않는다. 위 코드는 double로 계수를 계산하고 정확히 0인 계수만 선형 분기한다. 거의 같은 속력은 안정형 이차식으로 푼다. 유한한 예측 시간 범위에서 근사 선형화를 원한다면 먼저 시간과 거리를 무차원화하고 오차를 그 범위에서 평가해야 한다. 이 풀이는 등속 무중력 가정이므로 `s > 0`, 유한 입력, 최대 예측 시간, 같은 위치(즉시 명중) 정책을 호출부 계약으로 둔다.

### 4.5 발사 방향

$t$ 결정되면 *발사 위치 + 발사체 속력 × t* 후 *표적이 그 자리에 있어야*. 표적의 미래 위치 $\vec{p}_t + \vec{v}_t t$ 가 발사체의 목표.

```cpp
vec3 aimAt = pt + vt * tOut;
vec3 toAim = aimAt - ps;
vec3 fireDir = (length_squared(toAim) > 1e-8f)
             ? normalize(toAim)
             : vec3{1, 0, 0};
vec3 initialVel = fireDir * s;
```

### 4.6 중력 포함

발사체가 *중력 영향* 받으면 위 식이 더 복잡 — *4차 방정식*. 닫힌 형식 풀이 어려움.

해결: *반복 풀이*. 진공 가정으로 t 추정 → 그 t 의 *중력 보정 후 미래 위치* → 새 t. 수렴까지.

또는 *수치 풀이* (Newton's method).

반복이 항상 수렴하는 것은 아니므로 최대 반복 횟수와 실패 시 근사 조준 정책을 함께 둔다.

### 4.7 해 없는 경우

해 없음의 실제 조건은 *양의 실근이 없음* 이다. 표적이 발사체보다 빠르더라도 표적이 발사체 쪽으로 오고 있으면 맞출 수 있다. `disc < 0` 이거나 계산된 두 근이 모두 `t <= 0` 이면 실패다.

AI 처리: *예측 실패 → 가까운 미래 위치 추정 후 발사* (놓치더라도 시도).

## 5. 발사각 역산

목표 좌표 + 초기 속력 → 발사각.

### 5.1 2D 식

발사 위치 (0, 0), 목표 (x, y), 초기 속력 $v_0$, 발사각 $\theta$.

수평·수직 운동:

$$
x = v_0 \cos\theta \cdot t
$$
$$
y = v_0 \sin\theta \cdot t - \tfrac{1}{2} g t^2
$$

t 소거:

$$
y = x \tan\theta - \frac{g x^2}{2 v_0^2 \cos^2\theta} = x \tan\theta - \frac{g x^2 (1 + \tan^2\theta)}{2 v_0^2}
$$

$\tan\theta = u$ 두면 *u 에 대한 이차 방정식*:

$$
\frac{g x^2}{2 v_0^2} u^2 - x u + \left( y + \frac{g x^2}{2 v_0^2} \right) = 0
$$

이 유도는 $x > 0$ 를 가정하고 $t$ 를 소거한 결과다. 목표가 음의 x 방향이거나 3D 이거나 수직 표적인 경우는 각도 대신 비행시간을 직접 푸는 아래 계약을 쓴다.

### 5.2 풀이

정지 표적·일정 가속도 $\vec{a}$·고정 발사 속력 $s$ 에서 변위 $\vec{r}$ 는

$$
\vec{r} = \vec{v}_0 t + \tfrac{1}{2}\vec{a} t^2, \qquad |\vec{v}_0|^2 = s^2
$$

$T = t^2$ 로 두고 $t$ 를 소거하면 $T$ 에 대한 이차 방정식이 된다.

$$
\tfrac{1}{4}|\vec{a}|^2 T^2 - (\vec{r}\cdot\vec{a} + s^2) T + |\vec{r}|^2 = 0
$$

양의 실수 $T$ 만 채택하고 각 $t$ 에 대해

$$
\vec{v}_0 = \frac{\vec{r}}{t} - \frac{\vec{a} t}{2}
$$

로 발사 속도를 복원한다. 이 한 계약이 음의 x, 수직 표적, 무중력, 도달 불가를 모두 다룬다.

```cpp
struct LaunchSolution {
    vec3  velocity;   // 발사 속도 (|velocity| = speed)
    float time;       // 비행 시간
};

std::vector<LaunchSolution> ComputeLaunchVelocity(vec3 r, vec3 a, float speed) {
    std::vector<LaunchSolution> out;
    if (!(speed > 0.0f)) return out;                 // 고정 발사 속력은 양수
    if (!(length_squared(r) > 0.0f)) return out;     // 이 API는 동일 위치 목표를 즉시 명중으로 따로 처리

    double A = 0.25 * (double)dot(a, a);
    double B = -((double)dot(r, a) + (double)speed * speed);
    double C = (double)dot(r, r);

    double T1, T2;
    if (A == 0.0) {
        T1 = T2 = C / ((double)speed * speed);       // 무중력 — 등속 직선
    } else {
        double disc = B * B - 4.0 * A * C;
        double tol = 64.0 * std::numeric_limits<double>::epsilon()
                   * (B * B + std::fabs(4.0 * A * C));
        if (disc < -tol) return out;                 // 양의 실근 없음 — 도달 불가
        disc = std::max(0.0, disc);
        double q = -0.5 * (B + std::copysign(std::sqrt(disc), B));
        if (q == 0.0) return out;
        T1 = q / A;
        T2 = C / q;
    }

    double roots[2] = {T1, T2};
    std::sort(roots, roots + 2);
    for (double T : roots) {
        if (!(T > 0.0)) continue;
        double t = std::sqrt(T);
        vec3 v = r / (float)t - a * (0.5f * (float)t);
        // 최종 속도를 고정 속력 조건에 대입해 잔차를 확인한다.
        if (std::fabs(length(v) - speed) >
            1e-4f * std::max(1.0f, speed)) continue;
        if (!out.empty() && std::fabs((float)t - out.back().time) <= 1e-6f) continue;
        out.push_back({v, (float)t});
    }
    return out;
}
```

돌려준 두 해는 *비행시간 오름차순* 이다. 일반적인 수평 거리 양수·수직 중력 배치에서는 첫 해가 *낮은 각(직사)*, 둘째 해가 *높은 각(곡사)* 에 대응한다. 수직 표적이나 임의 방향 가속도에서는 짧은/긴 비행시간 해로 구분하며 각도 이름을 그대로 적용하지 않는다. 각도 자체가 필요하면 발사 속도를 어느 평면에서 정의하는지 밝히고 `atan2` 로 변환한다.

### 5.3 사거리 부족

위 $T$ 이차 방정식의 판별식이 음수 — 초기 속력이 목표 도달에 *부족함*. AI 처리:

- 더 가까이 이동 후 발사.
- 더 큰 발사체 사용.
- 발사 포기.

### 5.4 최대 사거리

낮은 각과 높은 각이 *같을 때* (disc = 0) — 최대 사거리.

$$
v_0^4 = g (g x^2 + 2 y v_0^2)
$$

목표가 발사점과 같은 높이 ($y = 0$):

$$
x_{max} = \frac{v_0^2}{g}, \quad \theta_{max} = 45°
$$

물리 교과서의 표준 결과.

### 5.5 3D 일반화

3D 도 같은 $T$ 계약을 쓴다. $\vec{r} = \vec{to} - \vec{from}$, $\vec{a} = \vec{g}$ 를 그대로 넘기면 수평면 방향과 수직 발사각이 한 번에 나온다. 별도의 수직 분기나 도달 판정을 두지 않아도 수직 표적과 도달 불가가 같은 경로에서 처리된다.

```cpp
std::vector<LaunchSolution> ComputeLaunchVelocity3D(vec3 from, vec3 to,
                                                    vec3 gravity, float speed) {
    return ComputeLaunchVelocity(to - from, gravity, speed);
}
```

### 5.6 게임 활용

- *AI 미사일*: 적이 *플레이어 위치에 발사각 계산* + 발사.
- *NPC 던지기*: 캐릭터가 *목표 위치에 폭탄 던짐*.
- *Aim assist*: 자동 조준의 *예상 궤적*.

표적이 *움직이면* 4절의 *표적 예측* 으로 *미래 위치* 를 구한 뒤 그 위치에 *발사각 역산* 하는 근사 조합을 쓸 수 있다. 그러나 4절의 예측 시간은 *중력 없는 등속 직선* 가정이고, 5절 발사체의 실제 비행 시간은 중력 때문에 다르다. 그래서 이 한 번의 결합은 정확한 intercept 가 아니라 *근사 조준* 이다. 정확도를 높이려면 예측 시간과 발사각을 함께 수렴시키거나 시간·발사각을 동시에 푼다. 반복은 항상 수렴하지 않으므로 상한·복수근·실패 정책을 함께 둔다.

## 6. 충돌 ToI와 swept test

빠른 발사체의 *터널링* 방지.

### 6.1 터널링 문제

발사체가 *한 프레임 안에 큰 거리 이동* — 벽보다 빠른 속도면 *벽 두께를 한 step 에 통과*. 충돌 검출 못 함.

```mermaid
flowchart LR
    Frame_n[Frame n: 벽 앞] -->|큰 dt × 큰 v| Frame_n1[Frame n+1: 벽 뒤]
    Frame_n1 --> NoHit[충돌 검출 안 됨]
```

총알 (수백 m/s) 이 *얇은 벽 (10 cm)* 을 *60 fps* 에 — 매 프레임 *수 m 이동* > 벽 두께. 터널링.

### 6.2 ToI (Time of Impact)

해결: *연속 충돌 검출 (CCD)*. 발사체의 *경로* 를 광선 / 캡슐로 *sweep*. 처음 닿는 *시각 t* (Time of Impact).

```cpp
struct RaycastHit {
    bool  hit;
    float t;          // 0 ~ 1, 발사체 경로 비율
    vec3  point;
    vec3  normal;
};

RaycastHit Sweep(vec3 from, vec3 to, const World& world);
```

t < 1 이면 *프레임 안에 충돌*. 그 시점에서 *발사체 정지 / 반사 / 폭발*.

### 6.3 광선 (ray) sweep

작은 발사체 (총알) 는 *광선 (점) 으로 모델*. broad phase + ray vs AABB → narrow ray vs 삼각형.

```cpp
RaycastHit RayCastBullet(vec3 from, vec3 dir, float maxDist, const World& world) {
    // ... BVH 트래버설 + ray-triangle test ...
}
```

매 step:

```cpp
vec3 newPos = bullet.position + bullet.velocity * dt;
vec3 segment = newPos - bullet.position;
float segmentLen = length(segment);
RaycastHit hit{};
if (segmentLen > 1e-6f) {
    hit = RayCastBullet(bullet.position,
                         segment / segmentLen,
                         segmentLen, world);
}
if (hit.hit) {
    bullet.position = hit.point;
    bullet.alive = false;
    // 데미지 처리 등
} else {
    bullet.position = newPos;
}
```

### 6.4 캡슐 sweep

큰 발사체 (로켓, 폭탄) 는 *점이 아님* — 부피 가짐. 캡슐 sweep.

```cpp
RaycastHit SweepCapsule(Capsule cap, vec3 disp, const World& world);
```

캡슐 형상이 *경로 따라 휩쓸리며* 처음 만나는 충돌. 비용 ray 보다 큼 — 충돌 형상이 더 복잡.

### 6.5 빠른 발사체의 *연속 다중 충돌*

발사체가 *한 step 에 여러 객체* 통과 가능. 예: 화살이 *적 3 명을 일렬로 관통*.

```cpp
constexpr float kMinToiAdvance = 1e-5f;
constexpr float kSkin = 0.001f;

while (remainingTime > 0) {
    RaycastHit hit = Sweep(bullet.position,
                           bullet.position + bullet.velocity * remainingTime, world);
    if (!hit.hit) {
        bullet.position += bullet.velocity * remainingTime;
        break;
    }
    bullet.position = hit.point;
    OnHit(bullet, hit);
    if (!bullet.canPenetrate) break;
    float usedTime = std::min(remainingTime,
                              std::max(hit.t * remainingTime, kMinToiAdvance));
    remainingTime -= usedTime;
    if (length_squared(bullet.velocity) > 1e-8f) {
        bullet.position += normalize(bullet.velocity) * kSkin; // 같은 표면 재충돌 방지
    }
    // 관통 — 계속
}
```

관통 발사체 (화살, 큰 총알) 의 자연 처리.

`kSkin` 만큼 밀어내는 방식은 얇은 표면에서는 다음 sweep 이 같은 면을 다시 치지 않게 하지만, 두꺼운 collider 내부에서는 같은 표면을 다시 찾거나 다른 표적을 놓칠 수 있다. 시간 최소 차감과 공간 skin 이 서로 맞지 않아 위치와 시간도 어긋난다. 따라서 입사/출사 질의(entry/exit)나 hit shape 임시 제외, penetration event 중복 규약을 둔다. `maximumImpactCount` 와 `usedTime` 에 대응하는 이동을 명시하고, `OnHit` 이 velocity·liveness·world 를 바꾸면 다음 sweep 전에 재검증한다.

### 6.6 산업 패턴

대부분 엔진의 *hit-scan vs projectile* 구분.

- **Hit-scan**: 즉시 ray cast. 무한 속도 가정. 빠른 총 (현대 FPS).
- **Projectile**: 실제 시뮬. 느린 발사체 (RPG, 화살).

hit-scan 은 *총알 비행 시간 0* — 게임플레이 단순. 일부 게임이 *시각 효과만 발사체* (총알이 실제로 보이지만 hit 는 즉시 결정).

## 7. 궤적 시각화

발사 전 *예측 궤적* 표시.

### 7.1 왜 필요한가

플레이어가 *발사 방향 + 강도* 를 정할 때 *결과를 미리 봄* — 정확한 조준 가능. *Worms*, *Angry Birds*, *grenade indicator* 등.

### 7.2 시각화 방법

미리 *시뮬을 마음 속에 돌려* — N step 의 *발사체 위치* 를 점으로 그리기.

```cpp
void DrawTrajectory(vec3 from, vec3 v0, vec3 gravity,
                     int numPoints, float dtPerPoint) {
    vec3 pos = from;
    vec3 v   = v0;
    for (int i = 0; i < numPoints; ++i) {
        DrawSphere(pos, 0.05f);
        v   += gravity * dtPerPoint;
        pos += v * dtPerPoint;
    }
}
```

또는 *닫힌 형식 평가*:

```cpp
for (int i = 0; i < numPoints; ++i) {
    float t = i * dtPerPoint;
    vec3 pos = from + v0 * t + gravity * (0.5f * t * t);
    DrawSphere(pos, 0.05f);
}
```

후자가 *오차 누적 없음* (각 점 독립).

### 7.3 충돌 종료

궤적이 *벽 / 지면에 닿으면 거기서 끊김*. 매 점마다 *이전 점에서 ray cast*.

```cpp
vec3 prev = from;
for (int i = 1; i <= numPoints; ++i) {
    float t = i * dtPerPoint;
    vec3 curr = from + v0 * t + gravity * (0.5f * t * t);
    vec3 segment = curr - prev;
    float segmentLen = length(segment);
    if (segmentLen < 1e-6f) continue;
    RaycastHit hit = RayCast(prev, segment / segmentLen, segmentLen);
    if (hit.hit) {
        DrawSphere(hit.point, 0.1f, /* hit 색 */);
        break;
    }
    DrawSphere(curr, 0.05f);
    prev = curr;
}
```

벽에 닿는 지점 *명확 표시* — 플레이어가 조준 정확성 판단.

### 7.4 점선 / 실선

| 표현 | 효과 |
|---|---|
| 점 (10~30 개) | 가벼움, 발사 가능성 |
| 실선 | 명확하지만 다소 무거움 |
| 짧은 dash | 발사체 속도 감각 |
| 색 그래디언트 | 시작 = 밝음, 끝 = 어둠 (시간 감각) |

게임 톤에 맞춰. 캐주얼 게임은 *밝은 점*, 진지 시뮬은 *얇은 실선*.

### 7.5 발사 매개변수 UI

궤적 시각화 + *발사각 / 강도 슬라이더* 또는 *마우스 드래그*:

- 마우스를 *목표 방향으로 끌면* 발사각 결정.
- *드래그 거리* 가 강도.
- 실시간으로 *궤적 갱신*.

Worms / Angry Birds 의 *조준 UI*.

### 7.6 항력 모델의 시각 비교

진공 vs 선형 vs 이차 항력의 *세 궤적을 동시* — 같은 발사 조건에 *모델별 차이*.

진공: 완벽한 포물선.
선형 항력: 수평 속도가 지수 감쇠하고, 수직 속도는 종단 속도로 접근.
이차 항력: 속력에 따라 감쇠가 강해지고, 수직 낙하에서는 종단 속도로 접근.

항력은 상승 높이와 하강 속도를 모두 바꾸므로 진공보다 항상 더 빨리 착지한다고 단정할 수 없다.

세 곡선이 *서서히 갈라짐* — 어떤 모델 선택할지 사용자가 직접 비교.

## 8. 게임 적용 패턴

발사체의 *전형적 게임 시스템*.

### 8.1 발사체 라이프사이클

```mermaid
flowchart LR
    Spawn[발사 spawn] --> Update[매 frame 적분 + ray cast]
    Update --> Hit{충돌?}
    Hit -->|예| OnHit[데미지 / 효과 / 폭발]
    OnHit --> Despawn[제거]
    Hit -->|아니오| Update
    Update --> Lifetime{수명 초과?}
    Lifetime -->|예| Despawn
    Lifetime -->|아니오| Update
```

### 8.2 발사체 자료구조

```cpp
struct Projectile {
    vec3   position;
    vec3   velocity;
    float  lifetime = 5.0f;       // 5 초 후 자동 제거
    float  damage   = 10.0f;
    int    ownerId;                // 발사자 (자기 자신엔 피해 X)
    bool   alive    = true;
    // 시각 / 효과 매개변수
};

std::vector<Projectile> projectiles;
```

게임의 *수십 ~ 수백 발사체* 가 array.

### 8.3 매 frame 업데이트

```cpp
void UpdateProjectiles(std::vector<Projectile>& projectiles,
                         const World& world, float dt) {
    for (auto& p : projectiles) {
        if (!p.alive) continue;

        // 시뮬레이션
        vec3 newPos = p.position + p.velocity * dt + 0.5f * gravity * dt * dt;
        p.velocity += gravity * dt;

        // 충돌
        vec3 segment = newPos - p.position;
        float segmentLen = length(segment);
        RaycastHit hit{};
        if (segmentLen > 1e-6f) {
            hit = RayCast(p.position, segment / segmentLen,
                          segmentLen, world, p.ownerId);
        }
        if (hit.hit) {
            p.position = hit.point;
            OnProjectileHit(p, hit);
            p.alive = false;
            continue;
        }

        p.position = newPos;
        p.lifetime -= dt;
        if (p.lifetime <= 0) p.alive = false;
    }

    // 죽은 발사체 정리
    projectiles.erase(
        std::remove_if(projectiles.begin(), projectiles.end(),
                        [](const Projectile& p) { return !p.alive; }),
        projectiles.end());
}
```

위 update 는 *직선 step 운동 모형의 근사* 다. `newPos` 는 정확한 포물선 끝점이지만 그 사이 sweep 은 직선 chord 이므로 `hit.t` 는 곡선의 ToI 가 아니다. 곡률 오차는 대략 $|g| h^2 / 8$ 이며, 이 값이 허용치를 넘으면 step 을 subdivide 한다. 데미지·반응에 충돌 시점 속도를 쓰면 전체 `dt` 중력을 다 더한 `p.velocity` 가 아니라 TOI 시점 속도를 써야 한다. 움직이는 표적의 상대 운동과 shape 반경도 같은 query 계약으로 둔다.

`OnProjectileHit` 안에서 `projectiles` 에 spawn/erase 하면 range-for 참조가 무효화될 수 있다. hit 을 값으로 기록해 순회가 끝난 뒤 소비하고, spawn/despawn 은 command queue 로 분리한다.

### 8.4 폭발 (area damage)

발사체가 *폭발* 하면 *주변 적에게 데미지*. 폭발 중심 + 반경.

```cpp
void Explode(vec3 center, float radius, float damage, const World& world) {
    auto victims = SphereCast(center, radius, world);
    for (auto& v : victims) {
        float dist = length(v.position - center);
        float damageFalloff = std::max(0.0f, 1.0f - dist / radius);
        v.health -= damage * damageFalloff;
    }
}
```

거리 따른 *데미지 감쇠* 자연.

`SphereCast` 가 값을 복사한 결과를 돌려주면 `v.health` 수정이 원본에 반영되지 않는다. stable entity handle 을 돌려주고, 한 entity 의 여러 collider 는 dedupe 한 뒤 권위 health 를 바꾼다.

### 8.5 발사체 풀

발사체가 *지속 생성 / 제거* — `new` / `delete` 비용. *오브젝트 풀* 패턴.

```cpp
struct ProjectilePool {
    std::vector<Projectile> data;
    std::vector<int>         freeList;

    explicit ProjectilePool(int capacity) {
        data.resize(capacity);            // 고정 용량을 생성 시 확보
        freeList.reserve(capacity);
        for (int i = capacity - 1; i >= 0; --i) freeList.push_back(i);
    }

    // 성공하면 사용 가능한 live 객체를 돌려준다. 실패 시 -1.
    int AcquireInitialized(vec3 position, vec3 velocity) {
        if (freeList.empty()) return -1;  // 정상 경로에서 컨테이너를 늘리지 않는다
        int idx = freeList.back();
        freeList.pop_back();
        data[idx] = {};
        data[idx].position = position;
        data[idx].velocity = velocity;
        data[idx].alive    = true;
        return idx;
    }

    void Release(int idx) {
        if (idx < 0 || idx >= (int)data.size() || !data[idx].alive) return; // 중복 해제 거부
        data[idx].alive = false;
        freeList.push_back(idx);
    }
};
```

`Acquire` 는 재사용 슬롯을 *초기화하지 않은 raw slot* 으로 돌려줄 수도 있고, 호출자가 모든 필드를 덮어쓰는 계약이면 그대로 유효하다. 위 예제는 `AcquireInitialized` 처럼 *사용 가능한 live 객체를 만든다* 는 계약을 택해 `alive`·`position`·`velocity` 를 초기화한다. 고정 용량을 생성 시 확보하고 free-list 도 reserve 하므로 정상 acquire/release 경로는 이후 컨테이너를 확장하지 않는다. 용량이 없으면 실패를 돌려주며, 이 실패 경로의 런타임 할당까지 *할당 0* 이라고 주장하지 않는다.

### 8.6 발사체 종류별 디자인

| 종류 | 모델 |
|---|---|
| 총알 / hit-scan | 즉시 ray cast |
| 화살 | 진공 포물선 + 광선 sweep |
| 로켓 | 등속 직선 + 캡슐 sweep + 폭발 |
| 마법 / homing missile | 표적 추적 (steering force) |
| 폭탄 / 수류탄 | 진공 포물선 + 바닥 충돌 + 타이머 폭발 |
| 산탄 (shotgun) | 여러 ray cast 동시 |

각 종류가 *기본 모델 + 특수 동작*. 게임 코드의 *Projectile 인터페이스* 위에서.

### 8.7 네트워크 동기화

멀티플레이의 발사체 — *클라이언트가 발사 시점 + 방향만 전송*. 서버가 *권위 시뮬*. 클라이언트는 *예측 표시*.

```cpp
// 클라이언트
SendFireMessage(server, fireDir, fireSpeed);
CreateLocalPredictedProjectile(...);

// 서버
OnFireMessage(client, fireDir, fireSpeed);
CreateAuthoritativeProjectile(...);
BroadcastProjectileState(...);
```

발사체가 *진공 포물선* 같은 *deterministic* 모델이면 동기화 쉽다. 항력 모델이 더 복잡하면 동기화 어려움.


---

## 결론

발사체가 *게임의 가장 흔한 물리 시스템* — 단순한 운동학 위의 *예측·항력·터널링·시각화* 디테일.

이 글에서 정리한 핵심.

1. **진공 포물선** — $\vec{r}(t) = \vec{r}_0 + \vec{v}_0 t + \tfrac{1}{2} \vec{g} t^2$. 가장 흔한 기본 모델.
2. **선형 항력** — 닫힌 해 존재 (지수 감쇠). 단순 시각 효과.
3. **이차 항력** — 종단 속도 + 수치 적분. 정밀 시뮬.
4. **표적 예측** — 등속 표적 + 이차 방정식 풀이.
5. **발사각 역산** — 목표 좌표 + 초기 속력 → 두 발사각 (낮음 / 높음).
6. **ToI + sweep** — 빠른 발사체 터널링 회피. ray / capsule sweep.
7. **궤적 시각화** — 발사 전 예측. UI 친화 게임의 표준.
8. **게임 패턴** — 라이프사이클, pool, 폭발, hit-scan vs projectile.

발사체 물리는 *운동학 + 예측 + 연속 충돌 검출* 의 결합이다. 단순한 포물선도 표적 예측, 항력, sweep, 네트워크 동기화와 만나면 게임플레이 품질을 크게 좌우한다.

## 더 읽을거리

- Kain Shin, "Predictive Aim Mathematics for AI Targeting" (Game Developer/Gamasutra) — 움직이는 표적 예측의 게임 친화적 정리.
- Kyle Pittman, "Math for Game Programmers: Building a Better Jump" (GDC 2016) — 포물선 매개변수 (높이·시간) 역산 설계.
- Halliday, Resnick, Walker, *Fundamentals of Physics*, Ch. 4 — 포물선의 학술 기초.
- Real-Time Rendering, 4th ed., online Ch. 25 "Collision Detection" — ray / sweep 검사 기초.
- Quake 소스 — 미사일 prediction 의 산업 reference.
- Sebastian Lague, "Projectile Prediction" 비디오 — 시각적 설명.
- Eric Lengyel, *Mathematics for 3D Game Programming*, Ch. 13 "Linear Physics" §13.3 "Projectile Motion".
- Erin Catto, GDC 발표 — 강체 + 발사체의 통합.
- Glenn Fiedler, "Networked Physics" — 발사체 네트워크 동기화.
