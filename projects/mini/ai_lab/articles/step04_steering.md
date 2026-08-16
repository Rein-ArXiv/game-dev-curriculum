## 들어가며

경로 탐색이 “어디로 갈지”를 정한다면 Steering은 “이번 프레임에 어떤 가속도를 줄지”를 정한다. Seek, Arrive, Wander, Pursue는 모두 원하는 속도와 현재 속도의 차이를 제한된 힘으로 바꾸는 변형이다.

---

## 목차
1. [공통 운동 모델](#1-공통-운동-모델)
2. [Seek와 Arrive](#2-seek와-arrive)
3. [Wander와 Pursue](#3-wander와-pursue)
4. [제한과 시간 적분](#4-제한과-시간-적분)
5. [검증 실험](#5-검증-실험)

---

## 1. 공통 운동 모델

네 행동은 목표 속도(desired)를 만드는 방식만 다르고, 그 뒤의 절단과 적분은 하나로 공유한다. 목표 속도와 현재 속도의 차이를 `maxForce`로 자르고, 그 힘을 적분한 속도를 다시 `maxSpeed`로 자른다.

<!-- snippet: src/model/steering.h#StepSteering -->
```cpp
const ModelVector2 desired =
    DesiredVelocity(behavior, state, goal, targetVelocity, params);

const ModelVector2 steer =
    VecTruncate(VecSub(desired, state.vel), params.maxForce);
state.vel = VecTruncate(VecAdd(state.vel, VecMul(steer, dt)), params.maxSpeed);
state.pos = VecAdd(state.pos, VecMul(state.vel, dt));
```

`maxForce`는 방향을 얼마나 빨리 바꿀 수 있는지, `maxSpeed`는 최종 속도 상한을 정한다. 둘을 구분하지 않으면 순간 회전하거나 목표 주변에서 심하게 진동한다.

## 2. Seek와 Arrive

Seek는 목표 방향으로 항상 최대 속도를 원한다.

$$
\mathbf{v}_{desired}=v_{max}\frac{\mathbf{x}_{target}-\mathbf{x}}{\|\mathbf{x}_{target}-\mathbf{x}\|}
$$

목표 근처에서도 최대 속도이므로 지나쳤다가 되돌아오는 진동이 생긴다. Arrive는 `slowRadius` 안에서 원하는 속도를 거리에 비례해 줄여 부드럽게 멈춘다.

## 3. Wander와 Pursue

Wander는 매 프레임 완전 무작위 방향을 고르는 것이 아니다. 현재 진행 방향 앞의 원 위 목표를 조금씩 흔들어 연속적인 방황을 만든다. 무작위 각도 변화가 너무 크면 떨림이 된다.

Pursue는 현재 위치가 아니라 목표의 미래 위치를 향한다.

$$
\mathbf{x}_{future}=\mathbf{x}_{target}+\mathbf{v}_{target}t_{lookahead}
$$

예측 시간은 거리에 비례하고 두 속도의 합에 반비례한다. 분모의 `+ 1.0f`는 두 속도가 모두 0일 때의 0 나눗셈 방지항이다.

<!-- snippet: src/model/steering.h#PursueLookahead -->
```cpp
inline float PursueLookahead(float distance, float maxSpeed, float targetSpeed) {
    return distance / (maxSpeed + targetSpeed + 1.0f);
}
```

lookahead가 너무 길면 급회전하는 목표를 크게 빗나가고, 너무 짧으면 단순 Seek와 비슷해진다.

## 4. 제한과 시간 적분

Scene의 목적은 “행동 이름”보다 제한값의 효과를 보는 것이다.

- maxForce가 작으면 관성이 큰 움직임
- maxForce가 크면 즉각적인 방향 전환
- slowRadius가 작으면 목표 주변 진동
- dt가 커지면 적분 오차와 경계 통과 증가

궤적과 속도 벡터를 함께 그려 위치만 봐서는 알 수 없는 원인을 드러낸다.

## 5. 검증 실험

1. Seek에서 목표를 에이전트 뒤로 옮겨 회전 반경을 본다.
2. Arrive의 slowRadius를 줄여 overshoot가 생기는 지점을 찾는다.
3. Pursue와 Seek를 움직이는 목표에 적용해 오차를 비교한다.
4. maxForce를 극단값으로 바꿔 조작감 변화를 관찰한다.
5. 속도가 항상 maxSpeed 이하인지 확인한다.

**실행과 관측**

```bash
cmake --build build --config Release
ctest --test-dir build -C Release -R ai_step04_steering --output-on-failure
./build/Release/ai_model_tests step04   # 같은 네 케이스를 직접 실행
```

```text
1/1 Test #4: ai_step04_steering ...............   Passed
100% tests passed out of 1

ai_model_tests [step04] 4 case(s)
  ok  steering/speed-cap
  ok  steering/arrive-slowdown
  ok  steering/pursue-lookahead
  ok  steering/bounds
AI model tests passed
```

한 스텝의 계산은 `src/model/steering.h` 의 `StepSteering` 에 있고 Scene 은 마우스 위치·원운동 타깃·무작위 wander 델타만 만들어 넘긴다. 그래서 네 행동 전부가 창 없이 검증된다.

- **속도 상한.** 네 행동 각각을 시드 고정 난수 목표로 1000 스텝 돌려 `|vel| <= maxSpeed` 를 확인한다. 이때 dt 범위가 결정적이다. $dt \le 1$ 이면 $v' = (1-dt)v + dt\,v_{desired}$ 가 볼록결합이라 절단 없이도 상한이 성립하므로, 60fps 프레임에 1~2초짜리 스톨을 섞어야 `VecTruncate` 의 maxSpeed 절단이 실제로 일하는 구간을 밟는다. 최고 속도로 달리다 목표가 정반대로 뒤집히고 프레임이 1.5초 걸리는 결정적 케이스도 함께 둔다 — 절단이 없으면 그 한 스텝에서 260 이 520 으로 튄다.
- **Arrive 의 감속.** slowRadius(120) 안에서 목표 거리를 120 에서 0 까지 줄이며 desired 속력이 매번 엄격히 감소하는지, 거리 60 에서 정확히 maxSpeed 의 절반(130)인지, slowRadius 밖에서는 전부 maxSpeed 로 포화하는지 본다.
- **Pursue 의 lookahead.** $t = d / (v_{max} + |v_{target}| + 1)$ 을 값으로 못 박고(예: $d=600$, $v_{max}=200$, $|v_{target}|=99$ 면 정확히 2), desired 가 현재 위치가 아니라 외삽한 미래 위치를 향하는지 확인한다. 분모의 $+1$ 은 두 속도가 모두 0 일 때의 0 나눗셈 방지항이라 값 검사로 함께 고정된다.
- **경계 처리.** 캔버스 밖으로 나가면 위치가 경계로 clamp 되고 해당 축 속도가 $-0.5$ 배가 되어 부호가 뒤집히는지 본다. 오른쪽·위쪽 두 방향을 각각 검사한다.

`steering/speed-cap` 의 결정적 케이스는 이렇게 생겼다. 난수 1000 스텝은 절단이 일하는
구간을 *밟을 확률* 만 주지만, 이 네 줄은 그 구간을 반드시 밟는다.

<!-- snippet: tests/ai_model_tests.cpp@steering-speed-cap -->
```cpp
const ailab::SteerBounds wide{0.0f, 0.0f, 10000.0f, 10000.0f};
ailab::SteerState state{{5000.0f, 5000.0f}, {params.maxSpeed, 0.0f}, 0.0f};
state = ailab::StepSteering(state, ailab::SteerBehavior::Seek, {-100000.0f, 5000.0f},
                            {}, params, 1.5f, wide);
CHECK(ailab::VecLen(state.vel) <= params.maxSpeed + 1e-4f);
CHECK(state.vel.x < 0.0f);
```

최고 속도(260)로 오른쪽으로 달리는 중에 목표가 왼쪽 10만 픽셀 밖으로 뒤집히고 프레임이
1.5 초 걸린다. `maxForce`(520)를 1.5 초 적분하면 속도 변화량이 780 이라 `VecTruncate` 의
maxSpeed 절단이 없으면 그 한 스텝에서 $|v|$ 가 520 으로 튄다. 두 번째 CHECK 는 절단이
방향까지 죽이지 않았는지 — 여전히 목표 쪽으로 돌아섰는지 — 를 함께 본다. 캔버스를 넓게
잡은 것은 경계 반사가 이 결과를 가리지 않게 하기 위해서다.

`LimitSpeed` 의 방향비 유지와 반복 반사는 군집 Scene 이 쓰는 다른 경계 모델이라 그쪽
케이스에 있다. 국소 제어는 위 `steering/bounds` 의 감쇠 반사를 정본으로 삼는다.

Steering 탭의 기본값은 max speed 260, max force 520, slow radius 120 이다. Seek 에서 마우스를 캔버스 반대편으로 옮기면 궤적이 꺾이지 않고 호를 그리는데, max force 를 40 으로 내리면 호가 눈에 띄게 커지고 1200 으로 올리면 거의 제자리에서 방향이 바뀐다. Arrive 로 바꾸면 목표 둘레에 slow radius 원이 그려지고, 그 원 안으로 들어갈수록 초록 속도 벡터가 짧아진다. slow radius 를 20 까지 줄이면 감속 구간이 짧아 목표를 지나쳤다 돌아오는 왕복이 궤적에 남는다.

Steering은 완벽한 경로가 아니라 *제약된 물체가 자연스럽게 목표를 따라가는 국소 제어* 다.

---

## 결론

Seek, Arrive, Wander, Pursue를 같은 가속도 제한 모델 위에 놓고 `maxForce`, `maxSpeed`, `slowRadius`, `dt`가 궤적과 오버슈트에 미치는 영향을 관찰할 수 있게 했다. 위치뿐 아니라 속도와 조향 벡터를 함께 표시해 결과의 원인을 분리한다.

Steering은 국소 운동 제어일 뿐 전역 경로의 도달 가능성이나 다수 에이전트의 충돌 회피를 보장하지 않는다. 장애물과 동역학 제약이 강한 환경에서는 별도의 계획 및 제약 처리와 결합해야 한다.
