# 16. 물리 결과가 AI 입력이 되는 경계

## 들어가며

이 게임의 물리는 15편까지 함수 하나였다. `physics::CollideCircleCircle` — 발사체가 적에 닿았는가. 원 둘의 중심 거리를 반지름 합과 비교하는 열 줄이다.

그것으로 충분했던 이유는 이 게임에 **응답할 접촉이 없었기 때문**이다. 적은 규칙이 좌표를 옮기고, 발사체는 목표를 향해 직진한다. 서로 밀어내는 물체가 하나도 없다.

이 편에서 그것이 생긴다. 타워를 철거하면 파편이 떨어지고, 발파가 바위를 뚫으면 그 자리에서도 파편이 나온다. 파편은 굴러가다가 바위와 서로에게 걸려 멈추고, **멈춘 파편은 그 칸을 막는다.**

그 마지막 문장이 이 편의 전부다.

> 물리 결과가 AI 입력이 된다.

13편이 만든 것 — 경로가 데이터가 아니라 통행 격자의 계산 결과라는 구조 — 위에서만 그 문장이 성립한다. 경로가 파일에 적힌 배열이면 파편이 어디 눌러앉든 적은 같은 길을 걷는다.

세 헤더가 들어온다.

| 헤더 | 하는 일 |
|---|---|
| `physics/world.hpp` | 적분 → 광역 → 협역 → 솔버 → 적분 파이프라인 |
| `physics/integrate.hpp` | Symplectic Euler(와 A/B 팔인 Explicit Euler) |
| `physics/solver/sequential_impulse.hpp` | 비관통·반발·마찰의 반복 수렴 |

그리고 스위치 셋이 붙는다 — `broadphase=naive|grid`, `integrator=symplectic|explicit`, `solver_iters=4|8|16`.

도달점의 수치.

- 파편 96개가 **678틱 만에 전부 정지**하고, 그중 25개가 통행 격자를 막는다.
- 광역 탐색 최대 쌍 수 **naive 47,400 vs grid 518**(91배), 두 팔의 상태가 **비트 동일**.
- 5단 적층의 충격 관통 **Symplectic 0.293 vs Explicit 0.690**(2.4배).
- 솔버 반복별 정지 관통 **4회 0.0277 · 8회 0.0140 · 16회 0.0078** — 단조 감소.
- 제품 셀프테스트에서 `sim.debris` 합계 **naive 45.05ms vs grid 12.49ms**.

---

## 목차
1. [왜 top-down 맵에 중력이 있는가](#1-왜-top-down-맵에-중력이-있는가)
2. [일곱 번째 시스템](#2-일곱-번째-시스템)
3. [정지 판정 — 임계 하나가 기능을 죽였다](#3-정지-판정--임계-하나가-기능을-죽였다)
4. [안착을 규칙이 승인한다](#4-안착을-규칙이-승인한다)
5. [광역 탐색: 스위치가 곧 호출 여부](#5-광역-탐색-스위치가-곧-호출-여부)
6. [두 팔이 같은 제약 집합을 같은 순서로](#6-두-팔이-같은-제약-집합을-같은-순서로)
7. [적분기 A/B — 한 줄의 차이](#7-적분기-ab--한-줄의-차이)
8. [솔버 반복과 slop](#8-솔버-반복과-slop)
9. [바디를 골라 지울 수 없다는 제약](#9-바디를-골라-지울-수-없다는-제약)
10. [ContactConstraint 의 포인터](#10-contactconstraint-의-포인터)
11. [변이 둘](#11-변이-둘)
12. [실행과 관측](#12-실행과-관측)

---

## 1. 왜 top-down 맵에 중력이 있는가

`PhysicsWorld` 는 `gravity` 를 갖고, 잔해는 그것을 받는다. 위에서 내려다보는 맵에서 그것이 무슨 뜻인가.

**이 아레나는 경사면이다.** 14편의 기울인 카메라가 보여 주는 지면은 평면이 아니라 코어 쪽으로 기운 비탈이고, 파편은 그 비탈을 따라 +y(화면 아래)로 미끄러져 바위와 서로에게 걸려 쌓인다.

이 해석을 고른 이유는 대안이 전부 물리를 없애기 때문이다.

- **중력 0.** 파편이 생긴 자리에 그대로 멈춘다. 접촉 응답이 필요 없고, 그러면 솔버도 적분기도 도는 자리가 없다.
- **감쇠만.** 초기 임펄스로 흩어졌다가 마찰로 멈춘다. 적층이 생기지 않으므로 5단 적층의 관통을 잴 수 없다.
- **코어 방향 중력.** 파편이 전부 코어로 모인다. 물리적으로 일관되지만 게임에서 읽히지 않는다.

비탈 해석은 물리적으로 일관되고, 적층을 만들고, 화면에서 읽힌다 — 파편이 아래로 흘러 벽에 걸린다. 가속도는 340 px/s² 로 잡았다(칸 10픽셀 기준).

```cpp
static constexpr float kGravity = 340.0f;   // px/s². 칸 10px 기준의 비탈 가속
```

## 2. 일곱 번째 시스템

`GameWorld::Step` 은 12편까지 여섯 시스템이었다. 잔해가 일곱 번째로 들어간다.

<!-- snippet: include/game/game.hpp@world-step -->
```cpp
void Step(float dt) {
    if (won_ || lost_) return;
    MEGA_PROFILE_SCOPE("sim.step");
    scratch_.Begin();
    SpawnSystem(dt);
    EnemyAdvanceSystem(dt);
    TowerFireSystem(dt);
    ProjectileSystem(dt);
    // ...
    CleanupSystem();
    DebrisSystem(dt);
    WinLoseSystem();
    scratch_.End();
}
```

**정리 뒤, 승패 앞.** 두 이웃 모두 이유가 있다.

정리(`CleanupSystem`) 뒤인 것은 잔해가 죽은 엔티티와 무관하기 때문이 아니라, 잔해가 **경로를 바꿀 수 있기 때문**이다. 경로가 바뀌면 살아 있는 적이 재배치된다(13편 §4). 그 재배치는 압축이 끝난 배열 위에서 도는 편이 단순하다 — 죽은 행이 섞여 있으면 재배치 루프가 `Alive` 를 한 번 더 물어야 한다.

승패(`WinLoseSystem`) 앞인 것은 잔해가 코어 체력을 건드리지 않기 때문에 순서가 자유롭고, 그렇다면 "판정이 마지막"이라는 2편의 규칙을 지키는 편이 낫기 때문이다.

시스템 본체는 이렇다.

<!-- snippet: include/game/game.hpp@debris-system -->
```cpp
void DebrisSystem(float dt) {
    if (!mapEnabled_ || !map_.Generated()) return;
    MEGA_PROFILE_SCOPE("sim.debris");
    // 정적 기하가 바뀌었으면 다시 짓는다. 다만 날아다니는 파편이 없을 때만 —
    // PhysicsWorld 는 바디를 골라 지우지 못하므로 재구성이 곧 전량 폐기다.
    if (debrisDirty_ && debris_.Live() == 0) {
        debris_.Reset(map_);
        debrisDirty_ = false;
    }
    if (debris_.Live() == 0) return;
    debris_.Step(dt, map_, settledCells_);
    bool changed = false;
    for (const DefenseMap::Cell& cell : settledCells_) {
        if (!map_.Passable(cell.x, cell.y)) continue;
        if (!map_.WouldRemainConnected(cell.x, cell.y)) continue;
        map_.SetOccupied(cell.x, cell.y, true);
        ++debrisSettled_;
        changed = true;
    }
    if (changed) {
        map_.RecomputeRoute();
        RefreshRoute();
        debrisDirty_ = true;
    }
}
```

첫 줄이 규약이다. **절차 맵에서만 돈다.** classic 맵은 경로가 파일이 정한 고정 회랑이라 막을 칸이라는 개념이 없고, 무엇보다 기본값이 스위치 이전의 동작이어야 한다.

두 번째 줄도 중요하다. `Live() == 0` 이면 즉시 반환한다 — 파편이 없는 프레임에서 물리 비용이 **정확히 0** 이다. 셀프테스트 2971틱 중 파편이 도는 것은 철거·발파 직후의 수백 틱뿐이다.

## 3. 정지 판정 — 임계 하나가 기능을 죽였다

파편이 "멈췄다"를 언제 선언하는가. 속도가 임계보다 작은 상태가 일정 틱 이어질 때다.

<!-- snippet: include/game/debris.hpp@debris-step -->
```cpp
int Step(float dt, const DefenseMap& map, std::vector<DefenseMap::Cell>& settledOut) {
    settledOut.clear();
    if (live_.empty()) return 0;          // 살아 있는 파편이 없으면 물리도 없다
    world_.Step(dt);

    size_t write = 0;
    for (size_t i = 0; i < live_.size(); ++i) {
        const int index = live_[i];
        const physics::RigidBody2D& body = world_.Body(index);
        const float speed = math::Length(body.velocity);
        int& ticks = settleTicks_[i];
        ticks = speed < kSettleSpeed ? ticks + 1 : 0;
        if (ticks < kSettleTicks) {
            live_[write] = index;
            settleTicks_[write] = ticks;
            ++write;
            continue;
        }
        world_.Freeze(index);             // 굳은 파편은 이제 정적 기하다
        const DefenseMap::Cell cell = map.WorldToCell(body.position);
        if (map.InBounds(cell.x, cell.y)) settledOut.push_back(cell);
    }
    live_.resize(write);
    settleTicks_.resize(write);
    return static_cast<int>(settledOut.size());
}
```

임계를 처음에 3 px/s 로 잡았다. 그 값으로는 **파편이 영원히 멈추지 않았다.** 테스트가 그것을 즉시 잡았다 — 2000틱을 돌려도 `Live()` 가 0이 되지 않는다.

이유는 산수다. 중력 340 px/s² · 스텝 1/60초면 접촉을 유지한 바디도 매 스텝 **5.7 px/s** 를 새로 받고, 솔버가 그것을 되돌린다. 그 되돌림이 완벽하지 않으므로 잔여 속도가 남고, 그 잔여가 3 px/s 를 넘나든다.

```cpp
// 임계가 왜 8 px/s 인가. 중력 340 px/s^2 · 1/60초 스텝이면 접촉을 유지한 바디도
// 매 스텝 5.7 px/s 를 새로 받고 솔버가 그것을 되돌린다. 그 잔여 속도가 임계보다
// 크면 "멈춘 파편"이 영원히 멈추지 않은 것으로 판정돼 잔해가 지형이 되지 못한다.
// 8 px/s 는 한 스텝에 0.13 픽셀 — 화면에서 정지와 구분되지 않는 속도다.
static constexpr float  kSettleSpeed = 8.0f;
static constexpr int    kSettleTicks = 20;
```

임계의 정당화가 "적당히 큰 값"이 아니라 **스텝당 중력 증분**이라는 점이 중요하다. dt 나 중력이 바뀌면 이 값도 같이 바뀌어야 하고, 그 관계가 주석에 적혀 있어야 다음 사람이 값을 그냥 늘리지 않는다.

`kSettleTicks = 20`(1/3초)은 한 번 튀는 순간을 정지로 오독하지 않게 하는 값이다.

`Freeze` 는 굳은 파편을 정적 바디로 만든다 — 이제 다른 파편이 그 위에 쌓일 수 있다. 그것이 적층의 출처다.

## 4. 안착을 규칙이 승인한다

`DebrisField::Step` 은 칸을 **막지 않는다.** 안착한 칸 목록을 돌려줄 뿐이다.

그 분리에는 이유가 있다. **코어를 봉쇄하는 안착을 되돌릴 주체가 필요하다.** 물리가 직접 막으면 봉쇄가 이미 일어난 뒤이고, 그 시점에서 되돌리려면 파편을 다시 굴려야 한다.

규칙(`DebrisSystem`)이 세 조건을 본다.

1. 그 칸이 지금 통행 가능한가(이미 타워나 다른 파편이 있으면 건너뛴다).
2. 그 칸을 막아도 스폰→코어가 살아 있는가 — **13편의 배치 규칙과 같은 flood fill.**
3. 통과하면 점유로 표시하고, 하나라도 표시했으면 경로를 다시 푼다.

조건 2에서 거절된 파편은 **물리적으로는 그 자리에 있지만 통행을 막지 않는다.** 화면에서는 파편이 보이는데 적이 그 위를 지나간다 — 게임적으로 어색하지만, 대안(파편이 코어를 봉쇄해 매치가 멈추는 것)보다 낫다.

`debrisSettled_` 는 누적 수를 규칙이 센다. `DebrisField::Settled()` 로는 이 질문에 답할 수 없다 — 정적 기하를 다시 지을 때(§9) 그 값이 0으로 돌아가기 때문이다. 테스트가 처음에 그 값을 읽다가 실패했고, 그 실패가 누적 카운터를 만들게 했다.

## 5. 광역 탐색: 스위치가 곧 호출 여부

`PhysicsWorld::SetBroadphase` 는 원래 이렇게 생겼었다.

```cpp
if (haveBounds_ && dynIdx_.size() >= 64) { /* 격자 */ }
else { /* 전수 */ }
```

규모 문턱이 안에 있었다. 그 상태로 A/B 를 하면 **스위치를 grid 로 놓고도 실제로는 naive 가 돈다** — 동적 바디가 64개 미만이면. 그러면 두 팔의 수치가 같아지고, 그 상태가 "차이가 없다"로 읽힌다.

문턱을 걷어냈다.

```cpp
// 격자 broad-phase 설정. **이 호출 여부가 곧 broadphase=grid|naive 스위치다** —
// 부르지 않으면 전수 짝짓기, 부르면 격자다. 규모 문턱(예전의 "동적 64개
// 이상")을 안에 두지 않는다: 문턱이 있으면 스위치를 grid 로 놓고도 실제로는
// naive 가 돌아 두 팔의 수치가 같아지고, 그 상태가 "차이가 없다"로 읽힌다.
void SetBroadphase(const math::Aabb2& bounds, float cell) {
    bounds_ = bounds; cell_ = cell; haveBounds_ = true;
}
```

잔해 쪽에서 스위치가 그 호출을 정한다.

<!-- snippet: include/game/debris.hpp@debris-reset -->
```cpp
void Reset(const DefenseMap& map) {
    world_.Clear();
    live_.clear();
    settleTicks_.clear();
    cellSize_ = static_cast<float>(map.CellSize());
    world_.gravity = {0.0f, kGravity};
    const float half = cellSize_ * 0.5f;
    for (int y = 0; y < map.Rows(); ++y)
        for (int x = 0; x < map.Columns(); ++x) {
            if (!map.Rock(x, y)) continue;
            physics::RigidBody2D rock;
            rock.shape = physics::Shape::Box({half, half});
            rock.SetStatic();
            rock.position = map.CellCenter(x, y);
            rock.friction = 0.7f;
            rock.restitution = 0.0f;
            world_.AddBody(rock);
        }
    staticCount_ = world_.Count();
    if (broadphase_ == BroadphaseKind::Grid) {
        const math::Aabb2 bounds{{0.0f, static_cast<float>(map.OriginY())},
                                 {static_cast<float>(map.PixelWidth()),
                                  static_cast<float>(map.PixelHeight())}};
        world_.SetBroadphase(bounds, cellSize_ * 2.0f);
    }
}
```

**바위를 칸 크기 상자로 쪼갠 것이 우연이 아니다.** 격자의 질의 반경은 전역 최대 경계 반지름으로 정해진다 — 아주 큰 정적 바디가 하나 섞이면 그 반경이 맵 전체를 덮어 격자가 전수 탐색으로 퇴화한다. `tests/test_physics_solver.cpp` 의 광역 탐색 테스트가 처음에 바닥을 커다란 상자 하나로 깔았다가 그 퇴화를 그대로 봤다(naive 3240 = grid 3240). 바닥을 13조각으로 나누자 4200 vs 1524 가 됐다.

## 6. 두 팔이 같은 제약 집합을 같은 순서로

격자 팔은 naive 팔의 **부분집합**을 낸다. 질의 반경 안에 없는 쌍은 어차피 접촉이 없으므로 제약이 만들어지지 않고, 결과가 같다.

순서는 다르다. 격자는 셀 순회 순서로 쌍을 낸다.

솔버는 반복적이므로 **순서가 결과를 바꾼다.** 그래서 정렬한다.

<!-- snippet: include/physics/world.hpp@build-pairs -->
```cpp
void BuildPairs() {
    pairs_.clear();
    dynIdx_.clear();
    float maxR = 0.0f;
    for (int i = 0; i < (int)bodies_.size(); ++i) {
        float r = BoundingRadius(bodies_[i]);
        if (r > maxR) maxR = r;
        if (!bodies_[i].IsStatic()) dynIdx_.push_back(i);
    }

    if (haveBounds_) {
        const float cell = cell_ > 0 ? cell_ : maxR * 2.0f;
        grid_.Init(bounds_, cell);
        for (int i = 0; i < (int)bodies_.size(); ++i)
            grid_.Insert(bodies_[i].position, i);           // 동적·정적 모두
        const float queryR = maxR * 2.0f + 1e-3f;
        for (int i : dynIdx_)
            grid_.QueryRadius(bodies_[i].position, queryR, [&](int j) {
                if (j == i) return;
                if (bodies_[j].IsStatic()) pairs_.emplace_back(i, j);
                else if (j > i)            pairs_.emplace_back(i, j);
            });
        std::sort(pairs_.begin(), pairs_.end());
        pairs_.erase(std::unique(pairs_.begin(), pairs_.end()), pairs_.end());
        return;
    }

    for (size_t a = 0; a < dynIdx_.size(); ++a)
        for (size_t b = a + 1; b < dynIdx_.size(); ++b)
            pairs_.emplace_back(dynIdx_[a], dynIdx_[b]);
    for (int i : dynIdx_)
        for (int s : staticIdx_)
            pairs_.emplace_back(i, s);
    std::sort(pairs_.begin(), pairs_.end());
}
```

15편의 후보 정렬과 같은 이야기다. **정렬 비용은 격자 팔이 치르는 값이고, 그 값을 치르지 않으면 잴 수 있는 것이 없다.**

바뀐 것이 하나 더 있다. 정적 바디도 격자에 넣는다. 예전에는 동적-정적을 언제나 전수로 짝지었는데, 바위 564개 × 파편 96개 = 54,144 쌍이 매 스텝 만들어진다. 정적을 격자에 넣으면 그 수가 518까지 떨어진다(§12).

파이프라인 자체는 그대로다.

<!-- snippet: include/physics/world.hpp@physics-step -->
```cpp
void Step(float dt) {
    for (auto& b : bodies_) IntegrateForces(b, gravity, dt);   // 1) 힘 → 속도
    BuildPairs();                                              // 2) broad-phase
    constraints_.clear();                                      // 3) narrow-phase
    for (auto& pr : pairs_) {
        RigidBody2D& A = bodies_[pr.first];
        RigidBody2D& B = bodies_[pr.second];
        if (A.IsStatic() && B.IsStatic()) continue;
        Contact c = Collide(A, B);
        if (c.hit) constraints_.push_back(MakeConstraint(A, B, c, dt, solverParams));
    }
    for (int it = 0; it < velocityIterations; ++it)            // 4) 솔버 반복
        for (auto& k : constraints_) SolveVelocity(k);
    for (auto& b : bodies_) IntegrateVelocity(b, dt, integrator);  // 5) 속도 → 위치
}
```

## 7. 적분기 A/B — 한 줄의 차이

Symplectic Euler 와 Explicit Euler 의 차이는 **위치를 어느 시점의 속도로 옮기는가** 하나다.

같은 파이프라인 자리(힘 → 솔버 → 위치)에 두 적분기를 꽂으려면 그 "시점"을 남겨야 한다.

<!-- snippet: include/physics/integrate.hpp@integrate-velocity -->
```cpp
inline void IntegrateVelocity(RigidBody2D& b, float dt,
                              Integrator kind = Integrator::Symplectic) {
    if (b.IsStatic()) return;
    // Symplectic 은 *지금* 속도로 옮긴다 — 이번 스텝의 중력과 접촉 임펄스가 이미
    // 반영된 속도다. Explicit 은 힘을 더하기 **전** 속도로 옮기므로, 이번 스텝에
    // 솔버가 밀어낸 결과가 위치에 들어가지 않는다. 적층이 부푸는 원인이 그것이다.
    const math::Vec2 v = kind == Integrator::Explicit ? b.prevVelocity : b.velocity;
    const float      w = kind == Integrator::Explicit ? b.prevAngularVel : b.angularVel;
    b.position += v * dt;
    b.angle    += w * dt;
    b.force = {0, 0};
    b.torque = 0;
}
```

`prevVelocity` 는 `IntegrateForces` 가 힘을 더하기 직전에 남긴다.

**측정에서 배운 것 하나.** 처음에는 "에너지가 발산한다"를 판정으로 삼으려 했다. 반발 계수 1의 공을 바닥에 튀겨 최고점을 재는 테스트를 썼는데, **Symplectic 팔의 최고점이 8에서 65로 올라갔다.** 이유는 적분기가 아니라 Baumgarte 위치 보정이다 — 관통을 밀어내는 바이어스가 속도에 섞여 에너지를 넣는다. 완전 탄성 접촉에서 그 주입이 누적된다.

즉 그 테스트는 적분기가 아니라 솔버의 성질을 재고 있었다. 판정을 바꿨다 — **정지 상태의 에너지가 아니라 충격 순간의 관통**을 잰다.

```
impact penetration: symplectic=0.29272 explicit=0.69034
```

5단 적층을 떨어뜨려 600스텝을 돌리며 최대 관통을 기록한다. Explicit 이 **2.4배 깊이** 파고든다. 원인은 명확하다 — 충돌 순간 솔버가 속도를 뒤집어도 Explicit 은 **뒤집기 전 속도**로 위치를 옮기므로 한 스텝만큼 더 들어간다.

정지 상태에서는 두 팔이 같다(둘 다 속도가 0에 가까우므로). 그것이 처음 테스트가 `0.01402 == 0.01402` 로 나온 이유였고, 판정이 전이 상태를 봐야 한다는 것을 알려 준 신호였다.

잔해 필드 수준의 에너지 측정도 남겨 뒀다.

```
energy peak/initial: symplectic=1.010 explicit=1.021
```

차이가 1%다. 약한 신호지만 방향은 맞고, 이 워크로드(짧은 낙하 + 즉시 접촉)에서 적분기 차이가 에너지로는 거의 드러나지 않는다는 사실 자체가 관측이다.

## 8. 솔버 반복과 slop

Sequential Impulse 는 접촉마다 임펄스를 풀고 그것을 `velocityIterations` 회 반복해 수렴시킨다. 반복이 많을수록 연립 제약이 더 잘 풀린다.

`solver_iters=4|8|16` 이 그 값을 노출한다.

```
stack penetration: iters4=0.02765 iters8=0.01402 iters16=0.00779 (slop=0.005)
```

**정확히 절반씩 준다.** 5단 적층이 정지한 뒤의 최대 관통이 반복을 두 배로 하면 절반이 된다.

`slop = 0.005` 는 무시하는 관통이다. 그 아래로는 임펄스를 넣지 않는다 — 미세 진동을 막기 위한 값이고, 16회 반복의 0.0078 이 그 값의 1.6배까지 내려온다.

`SolverParams` 의 다른 값 `baumgarte = 0.2` 는 남은 관통을 속도 바이어스로 천천히 밀어내는 비율이다. §7이 보인 에너지 주입의 출처가 그것이다.

`ContactConstraint` 에 `penetration` 필드를 하나 추가했다. 제약 생성 시점의 관통을 기록해 두면 `PhysicsWorld::MaxPenetration()` 이 밖에서 그것을 읽을 수 있다. 그 관측점이 없으면 위 표를 만들 수 없다.

**주의: 이 스위치는 결과를 바꾼다.** `target_index` 나 `broadphase` 와 달리 반복 횟수는 물리 결과 자체를 바꾸므로 "두 팔이 같은 결과"라는 단언을 세울 수 없다. 그런 종류의 스위치임을 분명히 해 두어야 한다 — A/B 등가 대조의 대상이 아니라 **파라미터 노브**다.

## 9. 바디를 골라 지울 수 없다는 제약

`PhysicsWorld` 는 바디를 `std::vector` 에 넣고 인덱스로 참조한다. 지우는 API 가 없다. 넣으면 매치가 끝날 때까지 있다.

그 제약이 두 곳에서 설계를 정했다.

**(1) 지형이 바뀌면 통째로 다시 짓는다.** 발파가 바위를 뚫으면 그 자리의 정적 상자를 지워야 하는데 지울 방법이 없다. 그래서 `Reset` 이 `Clear` 후 전부 다시 넣는다. 그리고 그것은 **날아다니는 파편도 함께 버린다.**

그래서 재구성을 미룬다.

```cpp
if (debrisDirty_ && debris_.Live() == 0) {
    debris_.Reset(map_);
    debrisDirty_ = false;
}
```

파편이 전부 멈춘 뒤에만 다시 짓는다. 그 사이(발파 직후 파편이 날아다니는 동안) 물리 세계는 **뚫리기 전 바위**를 들고 있다 — 파편이 이미 사라진 벽에 부딪힌다. 몇 백 밀리초의 불일치이고, 대안(파편을 즉시 버리기)보다 눈에 덜 띈다.

**(2) 굳은 파편이 정적 바디로 남는다.** `Freeze` 는 `invMass = 0` 으로 만들고 `staticIdx_` 에 넣는다. 지우는 대신 성격을 바꾼다. 그래서 동적 바디 수가 자연히 줄고(정지 상한 96은 **동시 동적** 파편 수다), 굳은 파편 위에 새 파편이 쌓인다.

이 자리에 `memory/pool.h` 가 어울린다는 것은 사실이다. 고정 블록 풀이면 주소가 안정적이라 인덱스 대신 포인터를 쓸 수 있고, 개별 반납이 가능하다. 이 편에서 넣지 않은 이유는 `PhysicsWorld` 의 공개 계약(`Body(int)`)이 인덱스 기반이고, 그것을 바꾸면 기존 물리 테스트 전부가 함께 움직여야 하기 때문이다.

## 10. ContactConstraint 의 포인터

`ContactConstraint` 는 두 바디를 **포인터로** 든다.

```cpp
struct ContactConstraint {
    RigidBody2D* a = nullptr;
    RigidBody2D* b = nullptr;
    // ...
};
```

`bodies_` 는 `std::vector` 다. `push_back` 이 재할당하면 그 포인터가 전부 죽는다.

지금 안전한 이유는 **한 스텝 안에서 바디를 추가하지 않기 때문**이다. 제약은 `Step` 안에서 만들어지고 같은 `Step` 안에서 소비된다. `AddBody` 는 `SpawnBurst` 에서만 불리고, 그것은 `Step` 밖이다.

그 안전은 우연이 아니라 계약이지만, **컴파일러가 강제하지 않는 계약**이다. 8편의 발사체 풀에서 같은 문제를 만났을 때는 답이 명확했다 — 주소가 절대 움직이지 않는 고정 블록 풀. 여기서는 그 답을 미뤘고, 그 사실을 여기 적어 둔다.

관측 가능한 형태로 바꿔 두는 방법이 하나 있다. `Reserve(n)` 을 추가해 두어 호출자가 상한을 미리 잡을 수 있게 했다.

```cpp
void Reserve(size_t n) { bodies_.reserve(n); }
```

지금은 아무도 부르지 않는다.

## 11. 변이 둘

**[1] 법선 임펄스의 0 클램프를 지운다.**

```cpp
// if (newI < 0) newI = 0;   // 임펄스는 당기지 않는다(밀기만)
```

누적 임펄스가 음수가 될 수 있게 되면 접촉이 **당기기** 시작한다 — 분리하려는 두 물체를 붙잡는다.

```
16/77 Test #16: test_debris ......................***Failed
56/77 Test #56: test_physics_solver ..............***Failed
97% tests passed, 2 tests failed out of 77
```

`test_physics_solver` 는 탄성 충돌과 마찰 한계에서, `test_debris` 는 정지 판정에서 죽는다. 당기는 접촉은 파편을 서로 붙잡아 진동시키므로 속도가 임계 아래로 내려가지 않는다.

**[2] 안착이 통행 격자를 막지 못하게 한다.** `DebrisSystem` 의 `map_.SetOccupied(...)` 와 `changed = true` 를 지운다. 물리는 그대로 돌고, 파편은 여전히 멈추고, `debrisSettled_` 도 여전히 오른다. **막지만 않는다.**

```
16/77 Test #16: test_debris ......................***Failed
99% tests passed, 1 tests failed out of 77
```

이것이 이 편의 경계를 지키는 변이다. 물리가 도는 것과 물리 결과가 게임에 반영되는 것은 다른 일이고, 후자를 무력화해도 전자는 멀쩡히 돈다.

## 12. 실행과 관측

```
> ctest -C Release
100% tests passed out of 77
```

**잔해 필드.**

```
> build\bin\Release\test_debris.exe
[test] debris
  settled 96 chunks in 678 ticks
  energy peak/initial: symplectic=1.010 explicit=1.021
  broadphase peak pairs: naive=47400 grid=518 (state diffs=0)
  settled debris blocked 25 cells; route 77 -> 77
[test] debris: 681 checks, 0 failures
```

읽을 것이 넷이다.

- **96개가 678틱(11초) 만에 전부 정지한다.** 유한 시간 종료가 이 시스템의 첫 요구다 — 멈추지 않는 파편은 매 틱 물리 비용을 내면서 지형도 되지 못한다.
- **광역 탐색 최대 쌍 수가 47,400 에서 518 로.** 91배다. 바위 564개 × 동적 파편의 곱이 전수 팔의 지배항이고, 격자가 그것을 지운다.
- **`state diffs=0`.** 300틱 동안 매 틱 모든 바디의 좌표를 기록해 두 팔을 비교했고, 부동소수 비트가 하나도 다르지 않다.
- **25칸이 막혔고 경로 길이는 77 그대로다.** 파편이 경로를 우회시켰지만 우연히 같은 길이의 우회로가 나왔다. 봉쇄는 없었다(`Validate().Valid()` 가 참).

**물리 코어.**

```
> build\bin\Release\test_physics_solver.exe
[test] physics_solver
  stack penetration: iters4=0.02765 iters8=0.01402 iters16=0.00779 (slop=0.005)
  impact penetration: symplectic=0.29272 explicit=0.69034
  broadphase pairs: naive=4200 grid=1524 (position diffs=0)
[test] physics_solver: 84 checks, 0 failures
```

**제품 안에서.** 절차 맵 셀프테스트, 광역 탐색만 바꿔서.

| | `broadphase=naive` | `broadphase=grid` |
|---|---|---|
| `sim.debris` 합계 | 45.05ms | 12.49ms |
| `sim.debris` 최댓값 | 0.280ms | 0.135ms |
| `sim.step` 합계 | 47.87ms | 15.65ms |
| 시뮬 결과 | `CLEAR ticks=2971 towers=7` | `CLEAR ticks=2971 towers=7` |

**3.6배.** 최댓값도 절반으로 줄었다(0.280 → 0.135ms). 최댓값 프레임은 파편이 가장 많이 살아 있는 순간이고, 그 순간에는 동적-동적 쌍이 지배적이라 격자의 이득이 작다. 격자가 지우는 것은 **대부분의 프레임에 있는 동적-정적 쌍**이다.

시뮬 결과가 같다는 것이 이 표를 읽을 수 있게 만든다.

```
> build_verify\bin\Release\mega_defense.exe --selftest 25000 --ab map=procgen --ab camera=tilted3d
      --ab target_index=grid --ab static_query=bvh --ab broadphase=grid
[selftest] result=CLEAR ticks=2971 towers=7 wave=6 packet=115 protocol=OK render=OK
[selftest] terrain route=79 opened=16 debris_live=0 debris_settled=27 target_rebuilds=1100
```

`debris_settled=27` — 매치가 끝날 때 파편 27개가 지형이 되어 있다. `debris_live=0` — 남아서 도는 파편이 없다.

---

## 결론

물리가 이 게임에 처음으로 **응답할 접촉**을 갖게 됐다. 그리고 그 접촉의 결과가 화면에만 머물지 않는다 — 멈춘 파편이 통행 격자를 막고, 막힌 격자가 다음 경로 계산의 입력이 되고, 그 경로를 적이 걷는다.

그 고리가 이 편의 값이다. 13편이 경로를 계산으로 만들지 않았다면 물리는 파티클과 다를 바 없었을 것이다.

측정에서 나온 것 셋.

**격자 광역 탐색은 크게 이긴다**(3.6배). 정적 바디를 격자에 함께 넣은 것이 그 대부분이다. 그리고 그 이득은 정적 바디를 **칸 단위로 쪼갠 덕분**이다 — 큰 상자 하나면 질의 반경이 맵을 덮어 격자가 퇴화한다.

**적분기 차이는 에너지가 아니라 관통으로 드러난다.** 에너지를 판정으로 삼으려던 첫 시도는 Baumgarte 바이어스의 에너지 주입에 가려 실패했고, 그 실패가 "무엇을 재고 있는가"를 다시 묻게 했다.

**솔버 반복은 관통을 절반씩 줄인다**(4→8→16 에서 0.0277→0.0140→0.0078). 비용은 선형이고 이득도 대략 선형이라, 기본값 8은 slop 의 3배 안쪽에 드는 가장 싼 값이다.

현재 상태의 한계 다섯.

**바디를 골라 지울 수 없다.** `PhysicsWorld` 가 인덱스 기반이라 제거 API 가 없고, 그래서 지형이 바뀌면 세계를 통째로 다시 짓는다. 그 재구성이 날아다니는 파편을 버리므로, 파편이 전부 멈출 때까지 미룬다 — 그 사이 물리 세계는 낡은 지형을 들고 있다.

**`ContactConstraint` 가 벡터 원소를 포인터로 든다.** 한 스텝 안에서 바디를 추가하지 않는다는 계약에 기대고 있고, 그 계약은 컴파일러가 강제하지 않는다.

**휴면(sleeping)이 없다.** 굳은 파편은 `Freeze` 로 정적이 되지만, 그 전까지는 거의 멈춘 파편도 매 스텝 전체 파이프라인을 돈다. 20틱의 정지 판정이 사실상의 휴면 역할을 한다.

**warm starting 이 없다.** 프레임 간 임펄스 캐싱이 없어 매 스텝 0에서 수렴을 시작한다. 그래서 반복 횟수가 그대로 품질이 된다(§8의 표가 그 관계다).

**안착 파편이 사격선을 막지 않는다.** 15편의 `StaticGeometry` 는 `map.Rock()` 만 보므로, 파편이 쌓인 칸은 통행은 막지만 총알은 통과시킨다.
