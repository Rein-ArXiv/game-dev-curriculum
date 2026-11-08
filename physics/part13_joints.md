## 들어가며

Sequential Impulse나 PBD 솔버는 수식으로 표현된 *제약* 을 반복적으로 푼다. *관절(joint)* 은 두 강체를 물리적으로 연결해 특정 자유도를 묶는 대표적인 제약이다.

게임에서 joint 가 등장하는 곳.

- *Ragdoll*: 캐릭터의 어깨·팔꿈치·무릎.
- *차량*: 차체와 바퀴를 잇는 서스펜션.
- *문*: 경첩 (hinge).
- *밧줄 / 체인*: 노드 간 distance joint.
- *기계*: 슬라이더, 기어, 모터.

각 joint 가 *위치 제약 식* + *Jacobian* 으로 표현. Lagrangian 미정승수법으로 *제약력* 결정. *같은 솔버에서 contact 와 함께* 풀린다.

이 글의 목표.

1. **Distance joint** — 가장 단순. 두 점 사이 거리 일정.
2. **Ball joint (point-to-point)** — 위치만 일치. 자유 회전.
3. **Hinge joint** — Ball + 회전축 정렬.
4. **Slider joint (prismatic)** — 한 축 이동 + 회전 고정.
5. **Motor 추가** — 자유 자유도에 *목표 속도 driver*.
6. **Limit 추가** — 자유 자유도의 *한도* (각도, 거리).
7. **Soft constraint** — spring-damper 형태의 *부드러운 제약*.
8. **엔진별 joint 종류**.

이 글이 마치면 *네 가지 기본 joint* 가 *같은 솔버에 plug-in* 가능. 게임의 대부분 기계 시스템이 이 조합으로 표현.


---

## 목차
1. [Distance joint](#1-distance-joint)
2. [Ball joint (point-to-point)](#2-ball-joint-point-to-point)
3. [Hinge joint](#3-hinge-joint)
4. [Slider joint (prismatic)](#4-slider-joint-prismatic)
5. [Motor 추가](#5-motor-추가)
6. [Limit 추가](#6-limit-추가)
7. [Soft constraint](#7-soft-constraint)
8. [엔진별 joint 종류](#8-엔진별-joint-종류)


---

## 1. Distance joint

가장 단순한 joint. 두 점 사이 거리 일정.

### 1.1 제약 식

두 강체 A, B 의 *anchor 점* $\vec{a}, \vec{b}$ 사이 거리:

$$
C(\vec{q}) = |\vec{a}^{world} - \vec{b}^{world}| - L = 0
$$

$L$ = 목표 거리. anchor 는 *각 강체의 로컬 좌표* — 강체 회전에 따라 world 좌표가 변함.

```cpp
struct DistanceJoint {
    Body* bodyA;
    Body* bodyB;
    vec2  localA;          // bodyA 로컬 anchor
    vec2  localB;          // bodyB 로컬 anchor
    float restLength;
    float gamma = 0.0f;    // soft 모드의 완화 계수. hard 모드는 0
    vec2  rA, rB;          // Prepare 에서 계산해 Solve 에서 사용
    // ...
};
```

### 1.2 Jacobian

C 의 시간 미분. world anchor 의 속도 = body cm 속도 + 회전 효과.

$$
\dot{C} = \hat{u} \cdot (\vec{v}_B^{anchor} - \vec{v}_A^{anchor})
$$

$\hat{u} = (\vec{b} - \vec{a}) / |\vec{b} - \vec{a}|$ — A 에서 B 방향. anchor 속도:

$$
\vec{v}_A^{anchor} = \vec{v}_A + \omega_A \times \vec{r}_A
$$

(2D 에서 $\omega \times \vec{r} = \omega \cdot \vec{r}^{\perp}$.)

Jacobian:

$$
J = \begin{pmatrix} -\hat{u} & -(\vec{r}_A \times \hat{u}) & \hat{u} & (\vec{r}_B \times \hat{u}) \end{pmatrix}
$$

1 행 — 1 자유도 제약.

### 1.3 Effective mass

$$
\text{effM}^{-1} = \frac{1}{M_A} + \frac{1}{M_B} + \frac{(\vec{r}_A \times \hat{u})^2}{I_A} + \frac{(\vec{r}_B \times \hat{u})^2}{I_B}
$$

normal impulse 식과 같은 형태. *방향 = $\hat{u}$*.

### 1.4 Bias (Baumgarte)

위치 drift 보정.

$$
b = \frac{\beta}{\Delta t} (|\vec{a} - \vec{b}| - L)
$$

$\beta \approx 0.1 \sim 0.3$. 거리 위반량을 *작은 분리 속도* 로 흘려넣음.

### 1.5 구현

```cpp
void DistanceJoint::PrepareVelocityConstraint(float dt) {
    rA = Rotate(localA, bodyA->angle);
    rB = Rotate(localB, bodyB->angle);
    vec2 d  = (bodyB->position + rB) - (bodyA->position + rA);
    float L = length(d);
    u = (L > 1e-6f) ? d / L : vec2{0, 0};

    float rAcrossU = Cross2D(rA, u);
    float rBcrossU = Cross2D(rB, u);
    effMass = 1.0f / (bodyA->invMass + bodyB->invMass
                      + rAcrossU * rAcrossU * bodyA->invInertia
                      + rBcrossU * rBcrossU * bodyB->invInertia);

    bias = (kBaumgarte / dt) * (L - restLength);
}

void DistanceJoint::SolveVelocityConstraint() {
    vec2 vA = bodyA->linearVelocity + PerpV(rA, bodyA->angularVelocity);
    vec2 vB = bodyB->linearVelocity + PerpV(rB, bodyB->angularVelocity);
    float Cdot = dot(u, vB - vA);
    // gamma 항이 누적 임펄스 잔차에 걸려야 soft 해로 수렴한다.
    // gamma = 0 이면 hard 제약과 같은 식이다.
    float lambda = -effMass * (Cdot + bias + gamma * impulse_acc);
    impulse_acc += lambda;

    vec2 P = u * lambda;
    bodyA->linearVelocity  -= P * bodyA->invMass;
    bodyA->angularVelocity -= bodyA->invInertia * Cross2D(rA, P);
    bodyB->linearVelocity  += P * bodyB->invMass;
    bodyB->angularVelocity += bodyB->invInertia * Cross2D(rB, P);
}
```

normal impulse 와 거의 같은 코드. equality 제약이라 *clamp 없음*.

### 1.6 게임 활용

- *Rope / 체인*: 노드 간 distance joint.
- *Spring 효과*: distance joint에 compliance와 damping을 넣은 soft constraint.
- *Tether*: 캐릭터를 한 점에 묶기.

## 2. Ball joint (point-to-point)

두 강체의 *한 점이 일치*. 위치 제약 + 자유 회전.

### 2.1 제약 식

$$
C(\vec{q}) = \vec{b}^{world} - \vec{a}^{world} = \vec{0}
$$

벡터 식 — 2D 에서 2 자유도, 3D 에서 3 자유도.

회전 자유: 두 강체가 *한 점 주위로 자유 회전* — 회전축 제한 없음.

### 2.2 Jacobian (2D)

$$
J = \begin{pmatrix}
-1 & 0 & r_{A,y} & 1 & 0 & -r_{B,y} \\
 0 &-1 & -r_{A,x} & 0 & 1 & r_{B,x}
\end{pmatrix}
$$

자유도 = $(\vec{v}_A, \omega_A, \vec{v}_B, \omega_B)$. 2 행 — 2 자유도 제약.

### 2.3 Effective mass (2D)

$K = J M^{-1} J^T$ 가 2×2.

$$
K = (\tfrac{1}{M_A} + \tfrac{1}{M_B}) I_2 + \frac{1}{I_A} R(\vec{r}_A) + \frac{1}{I_B} R(\vec{r}_B)
$$

$R(\vec{r}) = \vec{r}^{\perp} (\vec{r}^{\perp})^T$ — 회전 효과 행렬. 2×2 inverse 계산.

```cpp
mat2 K;
K[0][0] = bodyA->invMass + bodyB->invMass
        + rA.y*rA.y * bodyA->invInertia + rB.y*rB.y * bodyB->invInertia;
K[0][1] = -rA.x*rA.y * bodyA->invInertia - rB.x*rB.y * bodyB->invInertia;
K[1][0] = K[0][1];
K[1][1] = bodyA->invMass + bodyB->invMass
        + rA.x*rA.x * bodyA->invInertia + rB.x*rB.x * bodyB->invInertia;

mat2 effMass = inverse(K);
```

### 2.4 3D

3D 는 $J$ 가 3×12 (두 강체 6+6 자유도), $K$ 가 3×3. 같은 패턴, 차원만 늘림.

### 2.5 게임 활용

- *Ragdoll 의 어깨 / 고관절*: 위치 일치 + 자유 회전.
- *볼 베어링* 시뮬.
- *간단한 매달림*.

ragdoll 의 *모든 관절* 이 ball joint 의 변형 (+ 회전 한도).

### 2.6 코드 패턴

distance joint 와 비슷하지만 *2D 또는 3D 벡터 보정*. 매 iteration 마다 *벡터 lambda* + *벡터 impulse*.

## 3. Hinge joint

Ball joint + *회전축 정렬* 제약. 2D 의 *revolute joint* 와 같음.

### 3.1 제약 식 (3D)

- 위치 일치 (ball joint 부분): 3 자유도.
- 회전축 정렬: 2 자유도 (한 축 정렬 = 평면 2 회전 제거).

총 5 자유도 제약. 6 자유도 강체에서 *1 자유도 (한 축 회전) 만 남음*.

```math
C_{position}: \vec{a}^{world} - \vec{b}^{world} = \vec{0}
C_{axis}: \begin{pmatrix} \hat{a}_1 \cdot \hat{n}_B \\ \hat{a}_2 \cdot \hat{n}_B \end{pmatrix} = \vec{0}
```

$\hat{n}_B$ = body B 의 hinge 축 (월드), $\hat{a}_1, \hat{a}_2$ = body A 기준 hinge 축에 수직인 두 벡터.

축 정렬 = $\hat{n}_A$ 와 $\hat{n}_B$ 가 평행 ⇔ $\hat{a}_1 \cdot \hat{n}_B = 0$ 이고 $\hat{a}_2 \cdot \hat{n}_B = 0$.

### 3.2 2D 의 단순화

2D 에서 *회전이 한 축 (z)* 만 — 정렬할 축 자체가 없음. *Ball joint 그대로* 가 hinge.

```cpp
// 2D RevoluteJoint = 2D BallJoint
struct RevoluteJoint2D {
    Body* bodyA;
    Body* bodyB;
    vec2  localA, localB;
    // ...
};
```

Box2D 의 `b2RevoluteJoint` 가 2D hinge.

### 3.3 3D Jacobian

위치 부분 (3 행) + 회전 정렬 부분 (2 행). 총 5×12.

자세한 식은 길어 — Box2D 의 `b2RevoluteJoint::InitVelocityConstraints` (2D) + Bullet 의 `btHingeConstraint::buildJacobian` (3D) 참조.

### 3.4 회전축의 표현

각 body 가 *로컬 hinge 축* 보유. world 축 = body 회전 적용.

```cpp
struct HingeJoint3D {
    Body* bodyA;
    Body* bodyB;
    vec3  localA, localB;       // anchor
    vec3  axisA;                 // body A 로컬 hinge 축
    vec3  axisB;                 // body B 로컬 hinge 축
};
```

joint 생성 시 같은 월드 축을 각 body의 로컬 공간으로 변환해 `axisA`, `axisB`에 저장한다. 한 body의 로컬 축을 다른 body의 회전으로 직접 유도하면 초기 상대 자세가 사라지고, 이후 두 월드 축의 정렬 오차도 올바르게 계산할 수 없다.

### 3.5 게임 활용

- *문 경첩*: 한 축 회전.
- *Ragdoll 의 무릎 / 팔꿈치*: hinge + 각도 한도.
- *기계 회전축*: 풍차, 톱니바퀴.
- *차량 휠*: 휠과 axle 의 연결.

### 3.6 한도와 모터

hinge 의 *자유 회전* 에 *각도 한도* (limit, 6절) 또는 *모터* (5절) 를 추가하면 *완전한 기계 관절*.

## 4. Slider joint (prismatic)

한 축으로만 *병진 가능* + 회전 고정.

### 4.1 제약 식

3D 에서:

- *2 축 병진 제거*: 2 자유도.
- *3 회전 제거*: 3 자유도.

총 5 자유도 제약. *1 자유도 (한 축 이동) 만 남음*.

```math
C_{lateral}: \hat{a}_1 \cdot (\vec{p}_B - \vec{p}_A) = 0
C_{lateral2}: \hat{a}_2 \cdot (\vec{p}_B - \vec{p}_A) = 0
C_{rotation}: \text{회전 정렬 식 (3 개)}
```

$\hat{a}_1, \hat{a}_2$ = body A 기준 slider 축에 수직인 두 벡터.

### 4.2 2D 의 prismatic

2D 에서 *회전 1 자유도* — 회전 제약 1 개 (각도 일치). 측면 1 자유도. 총 2 자유도 제약.

남는 자유도: *한 축 이동*. Box2D 의 `b2PrismaticJoint`.

### 4.3 코드 구조

```cpp
struct PrismaticJoint2D {
    Body* bodyA;
    Body* bodyB;
    vec2  localA, localB;
    vec2  localAxisA;          // body A 로컬 slider 축
    float referenceAngle;       // 두 body 의 회전 정렬

    // Jacobian rows: lateral (1) + angular (1) = 2 rows
};
```

### 4.4 게임 활용

- *서랍 / 미닫이문*: 한 축 이동.
- *피스톤*: 엔진 시뮬.
- *엘리베이터 (kinematic 대신 dynamic)*.
- *Hydraulic ram* (motor 와 결합).

### 4.5 모터와 결합

slider 의 *자유 이동 자유도* 에 motor — 일정 속도로 이동. *엘리베이터 driving* 표준.

```cpp
struct PrismaticJoint2D {
    bool  motorEnabled = false;
    float motorSpeed   = 0.0f;
    float maxMotorForce = 1000.0f;
    // ...
};
```

motor impulse 는 *추가 제약 + clamp* — 5절.

### 4.6 한도

이동 거리의 최소·최대. 한도 도달 시 *inequality 제약* 활성화. 6절.

## 5. Motor 추가

joint 의 *자유 자유도에 driving force*.

### 5.1 모터 제약

motor 가 *목표 속도 제약*. hinge motor:

$$
C_{motor}: \omega_B - \omega_A = \omega_{target}
$$

(2D). 항상 *equality* 처럼 보이지만, *한도 (maxTorque)* 가 있어 *부등식*.

### 5.2 한도 클램프

motor impulse $\lambda$ 의 한도:

$$
|\lambda| \le \tau_{max} \cdot \Delta t
$$

힌지의 $\lambda$ 는 각임펄스이므로 한도는 최대 토크 $\tau_{max}$ 에서 얻는다. 직선 축을 구동하는 slider motor 라면 같은 자리에 최대 힘 $F_{max}$ 를 사용한다. 매 step 에 적용할 수 있는 임펄스를 제한하므로, 목표 속도 오차가 남아 있어도 모터가 무한한 힘을 만들지 않는다.

```cpp
void HingeMotor::SolveVelocityConstraint() {
    float Cdot = bodyB->angularVelocity - bodyA->angularVelocity - targetSpeed;
    float lambdaDelta = -effMass * Cdot;

    // 누적 임펄스 한도 클램프
    float oldImpulse = impulse_acc;
    float maxImpulse = maxTorque * dt;
    impulse_acc = std::clamp(impulse_acc + lambdaDelta, -maxImpulse, +maxImpulse);
    lambdaDelta = impulse_acc - oldImpulse;

    // 회전 임펄스 적용
    bodyA->angularVelocity -= lambdaDelta * bodyA->invInertia;
    bodyB->angularVelocity += lambdaDelta * bodyB->invInertia;
}
```

### 5.3 게임 활용

- *차량 휠 driving*: 휠 hinge + motor. 목표 속도 = 입력에 따라.
- *문 자동 열림*: hinge + motor (각도 → 목표 각도).
- *기계 회전*: 풍차, 컨베이어 벨트.
- *Animation override*: ragdoll 의 한 관절을 *애니메이션 각도* 로 motor 운전.

### 5.4 Position-based motor

목표 속도 대신 *목표 위치 / 각도* 에서 목표 속도를 만든다. 아래 식은 적분 항이 없는 비례 제어이며 PID 제어가 아니다.

```cpp
float angleError = WrapAngle(targetAngle - currentAngle);
float targetSpeed = std::clamp(kp * angleError,
                               -maxTargetSpeed, maxTargetSpeed);
```

속도 솔버가 이 목표를 추종하고 `maxTorque` 가 실제 응답을 제한한다. 감쇠를 더 직접 제어하려면 상대 각속도 항을 넣어 PD 목표 토크를 만들 수 있지만, 그 경우에도 토크 포화와 시간 간격에 따른 이득 변화를 함께 다뤄야 한다.

### 5.5 Motor 가 깨질 때

motor 가 *과도한 force* 로 *제약 위반* 가능. maxForce 한도가 그 안전망. 한도 안에선 *원하는 속도 도달 시도*, 한도 초과 외력엔 *밀려남*.

이게 "*힘은 부드러운데 가둘 수 있는*" motor 의 매력. *움직이는 플랫폼* 이 *위에 무거운 박스 있으면* 느려짐 — *현실적 동작*.

### 5.6 Power 모델

직선 구동기의 순간 출력은 $P = \vec{F}\cdot\vec{v}$, 회전 구동기는 $P = \vec{\tau}\cdot\vec{\omega}$ 다. 단순 모터 제약은 목표 속도와 최대 힘 또는 토크만 제한하므로 전압, 토크-속도 곡선, 열, 에너지 저장량을 자동으로 보존하지 않는다. 차량이나 로봇처럼 출력 한계가 게임플레이에 관여하면 속도에 따른 가용 토크와 에너지 소비를 별도 모델로 정의해야 한다.

## 6. Limit 추가

joint 자유 자유도의 *한도*.

### 6.1 한도 제약

hinge 의 각도 한도:

$$
\theta_{min} \le \theta \le \theta_{max}
$$

위반 시:

- $\theta < \theta_{min}$: lower limit 활성. *증가 방향만 허용* — inequality 제약.
- $\theta > \theta_{max}$: upper limit 활성. *감소 방향만*.
- 안: 제약 비활성 — 자유 회전.

### 6.2 상태 분기

```cpp
enum class LimitState { Inactive, AtLower, AtUpper, Equal };

void HingeJoint::PrepareLimit(float dt) {
    float angle = bodyB->angle - bodyA->angle - referenceAngle;
    if (lowerAngle == upperAngle) {
        // 두 한도가 같음 — equality 제약
        limitState = LimitState::Equal;
    } else if (angle <= lowerAngle) {
        limitState = LimitState::AtLower;
    } else if (angle >= upperAngle) {
        limitState = LimitState::AtUpper;
    } else {
        limitState = LimitState::Inactive;
    }
}
```

limit 상태가 바뀌면 누적 임펄스의 허용 부호도 바뀐다. Inactive 였다가 AtLower 로 전환되거나 그 반대가 되면 이전 상태의 `impulseLimit_acc` 는 새 feasible set 밖일 수 있다. 이전/현재 limit state 를 저장하고, 상태 전환·`referenceAngle`·한도 수정 시 누적 임펄스를 reset 하거나 새 허용 구간으로 투영한다. Inactive limit 의 stale impulse 는 warm start 에 적용하지 않는다.

hinge 의 `angle = bodyB->angle - bodyA->angle - referenceAngle` 계산은 각 body 의 `angle` 이 연속값일 때만 한도를 올바르게 분류한다. Part04 처럼 `angle` 을 $[-\pi, \pi]$ 로 wrap 하면 연속 185° 가 -175° 가 되어 upper 방향이 lower 위반으로 오인된다. `WrapAngle` 을 한 번 더 적용하는 것으로는 multi-turn joint 가 해결되지 않는다. 물리 각도는 연속값(누적 joint angle)으로 유지하거나 상대 회전에서 branch 를 추적하고, shortest-arc motor 와 연속 회전 limit 를 서로 다른 계약으로 둔다.

### 6.3 활성 한도의 임펄스

상태에 따라 *clamp 방향* 다름.

```cpp
void HingeJoint::SolveLimitVelocityConstraint() {
    if (limitState == LimitState::Inactive) return;

    float Cdot = bodyB->angularVelocity - bodyA->angularVelocity;
    float lambdaDelta = -effMass * Cdot;

    if (limitState == LimitState::AtLower) {
        // 증가 방향 (λ ≥ 0) 만 허용 — 누적 λ 가 음수 되지 않게
        float oldImp = impulseLimit_acc;
        impulseLimit_acc = std::max(0.0f, impulseLimit_acc + lambdaDelta);
        lambdaDelta = impulseLimit_acc - oldImp;
    } else if (limitState == LimitState::AtUpper) {
        // 감소 방향 (λ ≤ 0) 만
        float oldImp = impulseLimit_acc;
        impulseLimit_acc = std::min(0.0f, impulseLimit_acc + lambdaDelta);
        lambdaDelta = impulseLimit_acc - oldImp;
    } else {
        // Equal은 equality 제약이다. 양방향 임펄스를 누적한다.
        impulseLimit_acc += lambdaDelta;
    }

    bodyA->angularVelocity -= lambdaDelta * bodyA->invInertia;
    bodyB->angularVelocity += lambdaDelta * bodyB->invInertia;
}
```

### 6.4 Position correction 의 한도

velocity iteration 으로 한도 미달 시, position iteration 에서 *직접 각도 보정*.

```cpp
bool HingeJoint::SolveLimitPositionConstraint() {
    float angle = bodyB->angle - bodyA->angle - referenceAngle;
    float C = 0;
    if (angle < lowerAngle) C = angle - lowerAngle;
    else if (angle > upperAngle) C = angle - upperAngle;
    else return true;

    float correction = -kBaumgarte * std::clamp(C, -kMaxAngularCorrection,
                                                   kMaxAngularCorrection);
    float lambdaP = effMass * correction;
    bodyA->angle -= lambdaP * bodyA->invInertia;
    bodyB->angle += lambdaP * bodyB->invInertia;
    return std::fabs(C) < kAngularSlop;
}
```

### 6.5 게임 활용

- *Ragdoll 의 무릎*: 한 방향 굽힘만 허용 — lower 를 0° 근처에 두어 *역방향 안 굽힘*.
- *팔꿈치*: 비슷한 한도.
- *Slider 끝점*: 미닫이문이 *벽까지만* 이동.
- *Cone twist* (특수): 어깨의 *swing + twist 한도*.

자연스러운 ragdoll 의 *해부학적 한도* — limit 가 모든 관절에.

### 6.6 Soft limit

부드러운 한도 — 한도 *접근 시 점진적 저항*. spring-damper 와 결합.

```cpp
if (angle > upperAngle - softZone) {
    float error = angle - (upperAngle - softZone);
    float approachingSpeed = std::max(relativeAngularVelocity, 0.0f);
    float targetTorque = -kLimit * error - cLimit * approachingSpeed;
    targetTorque = std::clamp(targetTorque, -maxLimitTorque, 0.0f);
}
```

`kLimit` 의 단위는 torque/radian, `cLimit` 의 단위는 torque/(radian/s) 다. 위 코드는 upper limit 쪽으로 접근할 때만 저항하는 예이며, 실제 솔버에서는 `targetTorque * dt` 를 각임펄스로 바꾸어 적용한다. hard limit 은 최종 안전 경계로 함께 유지한다.

## 7. Soft constraint

equality 제약의 *spring-damper 변형*. 부드러운 joint.

### 7.1 동기

기본 hard 제약은 $C=0$ 을 목표로 하지만, 유한한 반복 횟수와 부동소수점 오차 때문에 실제 프레임에서는 잔여 오차가 남는다. compliance 가 없는 만큼 soft constraint 보다 단단하게 보인다.

soft constraint — 약간 *늘어남 허용* + spring 같은 *복원력*. 자연스러운 *유연성*.

### 7.2 Spring-damper 모델

제약 식 $C = 0$ 대신:

$$
\dot{C} + \frac{\beta}{\Delta t} C + \gamma \lambda = 0
$$

$\beta$ = 위치 오차 보정 계수 (ERP), $\gamma$ = 제약력 완화 계수 (CFM). $\gamma > 0$ 이면 제약이 *완전히 만족되지 않아도 되는* — soft constraint 가 *spring-damper 시스템* 처럼 동작.

CFM (Constraint Force Mixing) + ERP (Error Reduction Parameter) 으로 매개변수.

### 7.3 게임 표현

```cpp
struct DistanceJointSoft : DistanceJoint {
    float frequency = 0.0f;      // Hz — spring 진동수
    float damping   = 0.5f;       // 0~1 — 임계 감쇠 비율
};

void DistanceJointSoft::PrepareVelocityConstraint(float dt) {
    // ... 기존 코드 ...
    if (frequency > 0) {
        // soft 모드 — gamma 계산
        float mass = effMass;
        float omega = 2 * kPi * frequency;
        float k = mass * omega * omega;          // spring constant
        float c = 2 * mass * damping * omega;    // damping
        gamma = dt * (c + dt * k);
        gamma = (gamma != 0) ? 1.0f / gamma : 0;

        // 변형된 effective mass + bias
        effMass = 1.0f / (1.0f / mass + gamma);
        bias    = (L - restLength) * dt * k * gamma;
    }
}
```

기본값 `frequency = 0` 은 위 게이트에 걸려 soft 모드가 꺼진 상태 — 즉 *rigid 모드* 다. 이때 `gamma = 0` 이므로 base solve 식이 hard 제약과 정확히 일치한다. soft 로 쓰려면 `frequency` 에 양수를 준다. 예를 들어 *진동수 1 Hz + 감쇠 0.5* 로 두면 distance joint 의 위반량이 *고무처럼 응답*. soft 로 둔 row 는 hard position projection 으로 다시 조이지 않는다. hard 투영을 덧대면 compliance 가 사라진다.

### 7.4 API 로 노출할 값

soft distance joint 는 강성 $k$ 와 감쇠 $c$ 를 직접 받을 수도 있고, 고유 진동수 $f$ 와 감쇠비 $\zeta$ 를 받아 유효 질량으로 변환할 수도 있다. 후자는 질량이 달라도 디자이너가 비슷한 응답 시간을 지정하기 쉽다. 엔진과 버전에 따라 `hertz`, `frequency`, `dampingRatio`, `stiffness` 같은 이름과 hard mode 전환 조건이 다르므로, 직렬화 데이터에는 단위와 변환식을 함께 고정해야 한다.

### 7.5 게임 활용

- *Spring suspension*: 차량 휠과 차체의 부드러운 연결.
- *부드러운 rope*: 약간 늘어나는 체인.
- *Fabric joint*: cloth 의 연결점이 약간 휨.

### 7.6 Soft vs PBD

soft constraint 가 *SI 솔버 안의 spring 효과* 면, PBD 의 *XPBD compliance* 가 *PBD 안의 같은 개념*. 다른 솔버에서 같은 시각 효과 달성.

## 8. 엔진별 joint 종류

산업 엔진의 joint 라이브러리.

### 8.1 Box2D

Box2D 2.4.1 기준의 2D joint 종류.

| Joint | solver 행 / 목표 운동 | 추가 기능 |
|---|---|---|
| Revolute (hinge) | 2 (위치만) | motor, limit |
| Prismatic (slider) | 2 (측면 + 회전) | motor, limit |
| Distance | 1 | soft |
| Weld | 3 (완전 고정) | - |
| Pulley | 1 (도르래 비율) | - |
| Gear | 1 (기어 비율) | - |
| Wheel | 1 (측면) | spring + motor |
| Mouse | 2 (마우스 위치로 끌기) | - |
| Friction | 2 + 1 (병진 + 회전 마찰) | - |

표의 둘째 열은 configuration DOF 를 강제로 제거하는 holonomic rank 가 아니라 solver 가 만드는 행(row)과 목표 운동을 뜻한다. hard ideal constraint 만 rank 만큼 DOF 가 줄고, Mouse·Friction·compliant(soft) joint 는 유한 힘을 내는 행이지 DOF 를 제거하지 않는다. 목록과 이름은 Box2D 2.4.1 기준이며 3.x API 와는 다르다.

다양한 *2D 게임플레이* 를 위한 풍부한 라이브러리.

### 8.2 Bullet

3D 의 표준 joint.

- `btPoint2PointConstraint` — ball joint.
- `btHingeConstraint` — hinge + motor + limit.
- `btSliderConstraint` — slider + motor + limit.
- `btGeneric6DofConstraint` — 6 DOF 일반 (어느 자유도든 제거 / 한도).
- `btConeTwistConstraint` — 어깨 (swing + twist).

`btGeneric6DofConstraint` 는 여섯 자유도를 각각 제한할 수 있는 *범용 제약*이다. Bullet의 hinge, slider, point-to-point 제약이 이 클래스에서 상속되거나 그 위에 구현되는 것은 아니며, 모두 공통 제약 기반 클래스 아래에 놓인 별도 구현이다.

### 8.3 PhysX

비슷한 라인업 + *D6 joint* (6 DOF 일반).

- `PxFixedJoint`
- `PxRevoluteJoint`
- `PxPrismaticJoint`
- `PxSphericalJoint` (cone limit)
- `PxDistanceJoint`
- `PxD6Joint` (가장 유연 — 6 DOF 각각 free / locked / limited)

*Articulation* — multi-body system 의 *reduced coordinate* 시뮬 (Featherstone).

### 8.4 Jolt

비슷한 라이브러리 + *path constraint* (객체가 정해진 경로 따라가기).

- Fixed, Point, Hinge, Slider, Distance, Cone, Swing-twist.
- Path constraint (특수).
- *Constraint priority* — 어느 joint 부터 풀지 결정.

### 8.5 게임 패턴

| 게임 시스템 | 필요한 joint |
|---|---|
| Ragdoll (사람) | 어깨 (ball 또는 cone-twist), 팔꿈치 / 무릎 (hinge with limit), 손목 / 발목 (cone) |
| 차량 | 휠 (wheel joint, 서스펜션 + motor), 차체 연결 (fixed) |
| 문 / 서랍 | hinge / slider with limit |
| 밧줄 / 체인 | distance 노드 체인 |
| 기계 (풍차, 펌프) | hinge + motor + (slider) |
| 도르래 | pulley (2D) 또는 distance + 회전 |

### 8.6 일반 6 DOF 의 매력

`btGeneric6DofConstraint`, `PxD6Joint` 같은 *6 DOF 일반 joint* 는 자유도별로 free / locked / limited를 설정해 여러 특수 관절의 운동 범위를 표현할 수 있다. 그러나 클래스 상속 관계에서 모든 특수 joint의 부모인 것은 아니며, cone-twist·gear처럼 전용 좌표와 한도 모델이 필요한 동작까지 동일한 매개변수와 솔버 경로로 대체하는 것도 아니다.

대신 *복잡 + 다소 비효율*. 특수 joint (hinge, slider) 가 *전용 최적화* 로 더 빠름. 산업 엔진은 *둘 다 제공*.


---

## 결론

joint 가 *강체의 자유도를 묶어* 게임의 *물리적 연결* 을 표현한다. 모든 joint 는 *위치 제약 + Jacobian* 형식으로 볼 수 있고, contact 와 같은 solver 틀에서 풀 수 있다.

이 글에서 정리한 핵심.

1. **Distance joint** — 거리 일정. 1 자유도. rope / spring 의 토대.
2. **Ball joint** — 위치 일치. 자유 회전. ragdoll 의 어깨.
3. **Hinge joint** — Ball + 회전축 정렬. 한 축 회전만 남음. 경첩.
4. **Slider joint** — 한 축 이동만. 회전 고정. 미닫이 문.
5. **Motor** — 자유 자유도에 driving. maxForce 한도.
6. **Limit** — 자유 자유도의 한도. inequality 제약 활성/비활성.
7. **Soft constraint** — spring-damper 형식. 부드러운 joint.
8. **엔진별 라이브러리** — Box2D 2D 풍부, Bullet/PhysX/Jolt 3D + 6 DOF 일반.

joint 의 핵심은 *자유도를 제거하거나 제한하는 행(row)을 추가하는 것* 이다. distance, ball, hinge, slider, motor, limit 은 모두 이 관점에서 같은 구조로 읽힌다.

## 더 읽을거리

- Erin Catto, "Soft Constraints: Reinventing the Spring" (GDC 2011) — soft joint 의 게임 친화적 정리.
- Box2D 소스 (`b2RevoluteJoint.cpp`, `b2DistanceJoint.cpp`, `b2PrismaticJoint.cpp`) — 2D joint reference.
- Bullet 소스 (`btHingeConstraint.cpp`, `btGeneric6DofConstraint.cpp`) — 3D joint 산업 구현.
- PhysX SDK Guide, "Joints" — D6 joint 의 자세한 매개변수.
- Bullet manual, "Constraints" — joint 종류와 활용 패턴.
- David Eberly, *Game Physics*, 2nd ed. (2010), §6.6–6.7 — constrained motion 과 joint 의 학술 정리.
- Jakobsen, "Advanced Character Physics" (GDC 2001) — ragdoll 의 joint 디자인.
- Wikipedia, "Kinematic pair" — 기계 joint 의 학술 분류.
