# 21. 한 줄로 겹쳐 걷는 수백 마리

## 들어가며

20편이 폴리라인을 격자로 바꾸면서 적의 이동이 이렇게 됐다. 자기 칸을 알고, 다음 칸을 묻고, 그 칸의 중심을 향해 걷는다.

문제는 **다음 칸이 개체마다 다르지 않다**는 것이다. 플로우 필드는 칸당 방향 하나를 주므로 같은 칸에 있는 개체는 전부 같은 곳을 본다. A* 팔도 마찬가지다 — 목표가 코어 하나고 격자가 같으니 최단 경로도 같다. 그래서 같은 칸에서 출발한 스무 마리는 스무 마리 내내 같은 점을 밟는다. 9편에서 적 상한을 64에서 2048로 올린 것이 여기서 그림으로 드러난다: 수백 마리가 한 픽셀 위에 포개진 채 코어까지 행진한다.

`ai/steering/steering.hpp` 가 그 자리를 가져간다.

| 힘 | 무엇을 원하는가 |
|---|---|
| `Seek` | 격자가 가리키는 다음 칸의 중심으로 |
| `Separation` | 분리 반경 안의 이웃에서 멀어지기 |
| `Arrive` | 코어 근처에서 감속 |

셋을 `PriorityBlender` 로 **힘 예산 안에서** 합성한다. 단순 가중합이 아닌 이유는 예산이라는 개념이 있어야 "무엇을 먼저 포기하는가"를 정할 수 있기 때문이다.

스위치는 `steering=0|1` 이다. 끄면 20편 그대로 격자 방향으로 걷고, 켜면 위치가 힘의 적분이 된다.

이 편의 판정은 둘이다. **겹침이 실제로 걷히는가**, 그리고 **켜도 전원이 유한 시간 안에 코어에 닿는가.** 두 번째가 더 어렵다 — 조향의 실패는 "틀린 답"이 아니라 **끝나지 않음**으로 나타나기 때문이다.

---

## 목차
1. [위치가 경로의 함수인 한 조향은 끼어들 수 없다](#1-위치가-경로의-함수인-한-조향은-끼어들-수-없다)
2. [힘 예산과 우선순위](#2-힘-예산과-우선순위)
3. [Seek 이 먼저 먹는다](#3-seek-이-먼저-먹는다)
4. [이웃 질의 — 같은 클래스, 다른 인스턴스](#4-이웃-질의--같은-클래스-다른-인스턴스)
5. [목표 칸을 놓아야 할 때](#5-목표-칸을-놓아야-할-때)
6. [벽에 붙어 서는 것과 벽 밖에 갇히는 것](#6-벽에-붙어-서는-것과-벽-밖에-갇히는-것)
7. [교착 없음을 어떻게 단언하는가](#7-교착-없음을-어떻게-단언하는가)
8. [결정성 — 이웃 순회 순서가 새는 자리](#8-결정성--이웃-순회-순서가-새는-자리)
9. [변이 둘](#9-변이-둘)
10. [실행과 관측](#10-실행과-관측)

---

## 1. 위치가 경로의 함수인 한 조향은 끼어들 수 없다

격자 팔의 전진은 "다음 칸의 중심까지 남은 거리만큼 이동, 남으면 다음 칸으로" 다. 위치는 **목표 칸과 속도의 함수**이고, 그 사이에 다른 힘이 들어갈 자리가 없다.

<!-- snippet: include/game/game.hpp@step-grid -->
```cpp
void StepGrid(size_t i, BrainRow& brain, float speed, float dt, float& damageOut, bool away) {
    if (away) { StepAway(i, brain, speed, dt); return; }
    math::Vec2 pos = enemies_.Pos(i);
    float remaining = speed * dt;
```

조향 팔은 그 계산을 통째로 다른 것으로 바꾼다. 격자 방향은 **`Seek` 의 목표로만** 쓰이고, 위치는 속도의 적분이 된다.

<!-- snippet: include/game/game.hpp@step-steered -->
```cpp
void StepSteered(size_t i, BrainRow& brain, float speed, float dt, float& damageOut,
                 bool away, float separation) {
    const math::Vec2 pos = enemies_.Pos(i);
```

두 팔을 나란히 두는 이유는 비교 때문만이 아니다. classic 맵의 회랑은 한 칸 폭이라 조향이 표현할 것이 거의 없고, 절차 맵의 개방 지형에서만 이 축이 뜻을 갖는다. 그래서 `steering=0` 은 "느린 팔"이 아니라 **다른 지형의 기본값**이다.

## 2. 힘 예산과 우선순위

`PriorityBlender` 는 최대 힘을 예산으로 들고, 넣는 순서대로 그 예산을 소진한다.

```cpp
bool Add(const math::Vec2& force) {
    float mag = math::Length(force);
    if (mag < 1e-5f) return remaining_ > 1e-5f;
    if (mag <= remaining_) { accum_ += force; remaining_ -= mag; return remaining_ > 1e-5f; }
    accum_ += math::Normalize(force) * remaining_;        // 예산만큼만
    remaining_ = 0;
    return false;
}
```

단순 가중합과의 차이는 **포화**할 때 드러난다. 가중합에서는 이웃이 열 마리든 두 마리든 분리력이 전체 가속에서 같은 비율을 차지한다. 예산 방식에서는 앞에 넣은 힘이 예산을 다 쓰면 뒤의 힘이 **0이 된다** — 그것이 "무엇을 먼저 포기하는가"를 자료로 만든다.

예산의 크기는 속도에 비례시킨다.

```cpp
static constexpr float kSteerForceScale = 6.0f;
...
agent.maxForce = speed * kSteerForceScale;
```

고정 상수로 두면 빠른 적(Fast 는 기본 속도의 1.8배)이 같은 힘으로는 방향을 바꾸지 못해 벽을 따라 미끄러진다. 속도에 비례시키면 "몇 틱 만에 방향을 뒤집을 수 있는가"가 모든 적에게 같아진다.

## 3. Seek 이 먼저 먹는다

순서가 곧 정책이다.

```cpp
ai::steer::PriorityBlender blender(agent.maxForce);
blender.Add(ai::steer::Seek(agent, target));
Neighbors(i, pos, separation);
blender.Add(ai::steer::Separation(agent, neighbors_, separation));
const float slowRadius = BossScale() * 1.5f;
if (math::LengthSq(pos - corePos_) < slowRadius * slowRadius)
    blender.Add(ai::steer::Arrive(agent, corePos_, slowRadius));
```

분리를 먼저 넣으면 빽빽한 군집에서 전진력이 남지 않는다. 그러면 개체들이 제자리에서 서로를 밀며 **영원히 서 있는다** — 실패가 아니라 끝나지 않음이다.

Seek 을 먼저 넣으면 반대 성질을 얻는다. 군집이 빽빽할수록 분리가 잘리므로 겹침이 완전히 사라지지는 않지만, 전진은 언제나 보장된다. 이 축에서는 그쪽이 옳은 절충이다. **겹치는 군집은 보기 나쁘고, 멈춘 군집은 게임이 끝나지 않는다.**

`Arrive` 를 코어 근처에서만 넣는 것도 같은 이유다. 상시로 넣으면 먼 거리에서도 감속 항이 예산을 먹어 행진이 느려진다.

## 4. 이웃 질의 — 같은 클래스, 다른 인스턴스

분리력은 반경 안의 이웃을 알아야 한다. 15편의 조준 색인이 이미 "반경 안의 적"에 답하는 자료구조이므로 그것을 재사용한다 — 다만 **인스턴스는 따로** 둔다.

```cpp
void Neighbors(size_t self, const math::Vec2& at, float radius) {
    neighbors_.clear();
    const size_t rows = enemies_.Size();
    if (rows == 0) return;
    crowdScratch_.resize(rows);
    const size_t found = crowdIndex_.Query(at, radius, crowdScratch_.data(), rows);
    for (size_t k = 0; k < found && neighbors_.size() < kMaxNeighbors; ++k) {
```

질의 반경이 한 자릿수 다르기 때문이다. 조준은 사거리 180px 를 묻고 분리는 21px 를 묻는다. 같은 셀 크기로는 한쪽이 언제나 손해를 본다 — 셀이 크면 분리 질의가 후보를 잔뜩 걸러야 하고, 셀이 작으면 조준 질의가 셀을 잔뜩 훑는다.

상한 `kMaxNeighbors = 16` 이 붙어 있는 이유는 비용이다. 없으면 군집 한가운데의 개체가 수백 마리를 훑고, 그 개체가 수백 마리이므로 조향 비용이 개체 수의 제곱이 된다. 열여섯은 분리 반경 안에 실제로 들어올 수 있는 개체 수보다 넉넉하다.

## 5. 목표 칸을 놓아야 할 때

격자 팔에서는 위치가 목표 칸의 함수라, "목표 칸이 이웃이 아닌 상태"가 생기지 않는다. 조향 팔에서는 생긴다. 위치가 힘의 적분이라 한 틱에 두 칸을 지나거나 분리력에 옆으로 밀려날 수 있다.

```cpp
const DefenseMap::Cell here = map_.WorldToCell(pos);
const bool adjacent = std::abs(here.x - brain.cell.x) <= 1 &&
                      std::abs(here.y - brain.cell.y) <= 1;
if (!adjacent && navGrid_.Walkable(here.x, here.y)) {
    brain.cell = {here.x, here.y};
    brain.hasCell = false;
}
```

이 세 줄이 없으면 개체가 **벽에 최고 속도로 붙어 영원히 선다.** 목표 칸이 멀어져 있으면 그 칸을 향한 직선이 벽을 지나고, `Seek` 은 벽 쪽으로 계속 밀고, 미끄럼 처리가 그 이동을 계속 취소한다. seed=4 의 120마리 중 2마리에서 실제로 관측한 상태다.

## 6. 벽에 붙어 서는 것과 벽 밖에 갇히는 것

조향된 속도로 이동한 결과가 통행 불가 칸일 수 있다. 축 하나씩 미끄러뜨린다.

<!-- snippet: include/game/game.hpp@slide-into-walkable -->
```cpp
math::Vec2 SlideIntoWalkable(const math::Vec2& from, const math::Vec2& to) const {
    if (!WalkableAt(from)) return to;        // 이미 밖이다 — 막으면 갇힌다
    if (WalkableAt(to)) return to;
    const math::Vec2 alongX{to.x, from.y};
    if (WalkableAt(alongX)) return alongX;
    const math::Vec2 alongY{from.x, to.y};
    if (WalkableAt(alongY)) return alongY;
    return from;
}
```

**첫 줄이 이 함수의 전부다.** 나머지 넷은 교과서적인 미끄럼이고, 첫 줄만이 이 게임에서 관측한 것을 담는다.

스폰 지터는 셀 하나만 한 폭이다(절차 맵의 셀은 10px, 지터는 ±10px). 그래서 개체가 **처음부터 통행 불가 칸에 설 수 있고**, 분리력도 개체를 벽 쪽으로 민다. 그 상태에서 "통행 불가로는 못 간다"만 있으면 후보 셋이 전부 통행 불가라 영원히 제자리다.

이미 밖에 있는 개체에게 필요한 것은 금지가 아니라 **탈출**이다. `Seek` 이 경로 쪽을 가리키고 있으므로 한두 틱이면 돌아온다. 규칙을 "언제나 통행 가능한 칸에만 있어야 한다"로 두는 대신 "통행 가능한 칸에서 나가지는 않는다"로 둔 것이고, 그 둘의 차이가 교착과 진행을 가른다.

## 7. 교착 없음을 어떻게 단언하는가

조향이 무너지는 방식은 셋인데, 그중 둘은 실패로 나타나지 않는다.

| 무너지는 방식 | 어떻게 보이는가 |
|---|---|
| 힘이 발산한다 | 개체가 맵 밖으로 날아간다 — 위치 단언이 잡는다 |
| 분리가 전진을 삼킨다 | 군집이 제자리에서 서로를 민다 — **아무 단언도 실패하지 않는다** |
| 개체가 벽에 갇힌다 | 그 한 마리만 영원히 남는다 — **아무 단언도 실패하지 않는다** |

뒤의 둘을 실패로 바꾸는 것은 "유한 시간 안에 전원이 코어에 닿는다"를 **명시적으로** 단언하는 것뿐이다.

```cpp
if (alive == 0 && world.Wave() > 1) { run.arrivedTick = tick; break; }
...
CHECK(steered.arrivedTick > 0);       // -1 = 9000틱 안에 못 끝냈다
```

타워를 하나도 세우지 않는 것이 이 검사의 설계다. 타워가 있으면 "몇 마리가 도착했는가"가 조향이 아니라 사격의 함수가 되고, 갇힌 한 마리도 결국 총에 맞아 사라져 교착이 감춰진다.

겹침 판정도 마찬가지로 조심해야 한다. 최소 거리 하나로 재면 두 개체가 한순간 스치기만 해도 단언이 무너져, 조향의 값이 아니라 운을 재게 된다. 그래서 **겹친 짝의 수**를 세고 두 팔의 비를 본다.

```cpp
CHECK(steered.minSeparation > plain.minSeparation);
CHECK(steered.overlaps * 4 < plain.overlaps);
```

그리고 스폰 직후 400틱은 재지 않는다. 스폰은 한 칸에서 일어나므로 그 순간의 최소 거리는 조향이 아니라 지터의 함수이고, 두 팔에서 같은 값이 나온다.

## 8. 결정성 — 이웃 순회 순서가 새는 자리

조향은 이웃 질의를 지난다. 그 질의가 셀 안의 개체를 어떤 순서로 돌려주는가가 분리력의 부동소수점 합산 순서를 정하고, 합산 순서가 다르면 결과 비트가 다르다.

이 축에서 그것을 잠그는 것은 색인이 삽입 순서(= 행 번호 오름차순)로 후보를 낸다는 성질이고, 검사는 같은 시드로 두 번 돌린 상태 해시를 맞춰 보는 것이다.

```cpp
static void TestDeterminism() {
    const CrowdRun a = RunCrowd(true, 120, 4000, 5);
    const CrowdRun b = RunCrowd(true, 120, 4000, 5);
    CHECK_EQ(a.hash, b.hash);
    CHECK_EQ(a.arrivedTick, b.arrivedTick);
}
```

같은 프로세스 안의 두 실행이라 "해시 자료구조의 순회 순서" 같은 것이 새면 바로 잡힌다. 그것으로 충분한 이유는 이 게임의 이웃 질의가 해시를 쓰지 않기 때문이다 — 쓰는 순간 이 검사는 통과하면서 크로스 프로세스 협동만 갈라지는 종류의 결함을 놓친다.

## 9. 변이 둘

**변이 1 — `Truncate` 의 클램프를 지운다.**

```cpp
inline math::Vec2 Truncate(const math::Vec2& v, float) { return v; }
```

속도 상한이 사라지면 매 틱 더해지는 가속이 그대로 쌓여 속도가 발산한다. 개체가 몇 틱 만에 맵을 가로지르고, 코어를 지나쳐 반대편으로 나간다. 도착 판정(`kCoreReachSq`)은 위치의 근접으로 하므로 그 틱에 코어를 건너뛴 개체는 **영원히 도착하지 않는다.** `test_crowd` 의 `TestNoDeadlock` 이 `arrivedTick = -1` 로 이 변이를 죽인다.

**변이 2 — `SlideIntoWalkable` 의 첫 줄을 지운다.**

```cpp
math::Vec2 SlideIntoWalkable(const math::Vec2& from, const math::Vec2& to) const {
    if (WalkableAt(to)) return to;
    ...
```

스폰 지터로 통행 불가 칸에 선 개체가 세 후보 전부 거절당해 제자리에 굳는다. 죽지도 도착하지도 않으므로 그 웨이브가 끝나지 않고, 같은 검사가 잡는다. 겹침 단언은 이 변이를 **통과시킨다** — 굳은 개체는 오히려 아무와도 겹치지 않기 때문이다.

## 10. 실행과 관측

**힘 합성의 단위 계약.**

```
> build\bin\Release\test_ai_steering.exe
[test] ai_steering
[test] ai_steering: 14 checks, 0 failures
```

**군집 — 겹침 · 교착 · 재현성.**

```
> build\bin\Release\test_crowd.exe
[test] crowd
  격자 방향  최소거리=0.037px 겹침짝=38361 도착틱=1030 최대생존=120
  조향       최소거리=0.052px 겹침짝=1782 도착틱=3621 최대생존=120
  seed=2 조향 도착틱=3730
  seed=4 조향 도착틱=4072
  seed=6 조향 도착틱=3805
[test] crowd: 18 checks, 0 failures
```

읽을 것이 셋 있다.

**`겹침짝 38361 → 1782`, 21.5배.** 이것이 조향의 값이다. 격자 방향 팔에서는 같은 칸을 향하는 개체가 정확히 같은 점을 밟으므로, 분리 반경의 1/4(절차 맵 셀 10px 기준 1.75px)보다 가까운 짝이 400틱 이후에만 3만 8천 번 관측된다.

**`최소거리` 는 거의 안 움직인다(0.037 → 0.052px).** 이 수 하나로 단언을 세웠다면 조향의 값을 재지 못했을 것이다. 두 개체가 한순간 스치는 일은 조향을 켜도 일어나고, 최소값은 그 한 번에 지배된다. **분포를 재야 하는 자리에서 극값을 재면 운을 잰다.**

**`도착틱 1030 → 3621`, 3.5배 느리다.** 조향은 공짜가 아니다. 분리력이 전진력의 예산을 먹고, 개체가 서로를 피해 도는 만큼 경로가 길어진다. 그럼에도 **끝난다**는 것이 이 축의 불변식이고, 서로 다른 세 시드에서 3730 · 4072 · 3805 틱으로 모두 유한하다.

`최대생존=120` 이 두 팔에서 같은 것도 필요한 확인이다. 조향 팔에서 개체가 덜 태어났다면 위의 비교는 다른 규모의 두 군집을 비교한 것이 된다.

**제품.**

```
> build\bin\Release\mega_defense.exe --selftest 25000 --ab map=procgen --ab pathing=flowfield --ab steering=1 --ab stealth=1 --ab elite_brain=bt
[terrain] seed=7 41x41 maze=799 carved=318 rock=564 veins=148 route=79 bvh_prims=564/nodes=359
[selftest] result=CLEAR ticks=4921 towers=10 wave=6 packet=164 protocol=OK render=OK assets=10/10 content=external stomps=3
[selftest] ai field_builds=40 searches=0 melee=0 slams=3 reveal=122
[profile] frame=8090.50/11.117ms(n=4921) sim.step=341.55/0.895ms(n=4921) sim.nav=183.19/0.851ms(n=4920) sim.enemy_advance=28.81/0.201ms(n=4920) sim.cleanup=3.48/0.055ms(n=4921)
```

`result=CLEAR` 는 조향을 켠 채로 제품이 다섯 웨이브를 끝냈다는 뜻이다. 교착이 있었다면 `TIMEOUT` 이 된다.

`sim.nav` 안에 이웃 색인 재빌드가 들어 있다(`steering=1` 일 때만 돈다). 총 183.19ms / 4920틱이고, 그중 색인 재빌드가 필드 재빌드 40회와 섞여 있어 여기서는 분리되지 않는다.

**변이 확인.**

`Truncate` 의 클램프를 지우면 속도가 발산해 개체가 코어를 건너뛰고, `test_crowd` 의 `arrivedTick` 이 -1 이 되어 `TestNoDeadlock` 이 죽는다. `SlideIntoWalkable` 의 첫 줄(`if (!WalkableAt(from)) return to;`)을 지우면 스폰 지터로 벽에 선 개체가 굳어 같은 단언이 죽는다. 겹침 단언은 후자를 **통과시킨다** — 굳은 개체는 아무와도 겹치지 않기 때문이고, 그래서 두 단언이 둘 다 필요하다.

---

## 결론

조향을 넣으면서 바뀐 것은 겉모습만이 아니다. **위치를 무엇의 함수로 두는가**가 바뀌었다. 경로의 함수였을 때 개체의 다음 위치는 계산이었고, 힘의 적분이 된 뒤로는 상태다. 그 차이가 목표 칸 재획득·미끄럼·교착 같은 문제를 한꺼번에 데려왔다.

그 문제들의 공통점은 **실패가 아니라 정지로 나타난다**는 것이다. 잘못된 위치는 단언이 잡지만 멈춘 군집은 아무 단언도 건드리지 않는다. 그래서 이 축의 검사는 "값이 맞는가"가 아니라 "끝나는가"를 묻는 쪽으로 설계됐고, 그것이 타워 없는 하네스와 도착 틱 단언의 이유다.

측정에서 나온 것 둘.

**Seek 을 먼저 넣는 것이 절충이지 최선이 아니다.** 겹침이 완전히 사라지지 않는다. 분리를 먼저 넣으면 겹침은 사라지지만 빽빽한 군집이 멈춘다. 예산 방식은 그 선택을 강제할 뿐 없애 주지 않는다.

**교착의 원인은 조향이 아니라 스폰이었다.** 처음에 벽에 갇힌 개체를 조향의 문제로 보고 분리력과 예산을 여러 번 만졌다. 실제 원인은 스폰 지터가 셀 폭만 해서 개체가 통행 불가 칸에서 태어나는 것이었고, 고친 곳은 미끄럼 함수의 첫 줄이었다.

현재 상태의 한계 셋.

**분리는 이웃 열여섯까지만 본다.** 그보다 빽빽해지면 실제 이웃 일부가 무시되어 그쪽으로 밀려난다. 상한을 올리면 비용이 개체 수의 제곱으로 돌아온다.

**정렬(Alignment)과 응집(Cohesion)은 쓰지 않는다.** 헤더에는 있다. 이 게임의 적은 무리 짓는 생물이 아니라 코어로 밀려드는 개별 개체라 둘이 표현할 것이 없고, 이름을 쓰려고 호출하지는 않는다.

**미끄럼은 축 정렬이다.** 벽에 비스듬히 부딪히면 x 또는 y 로만 미끄러지므로 실제 벽면을 따라가지 않는다. 셀 격자에서는 벽면이 언제나 축 정렬이라 지금은 구분되지 않지만, 잔해가 회전한 채 굳는 순간 그 가정이 깨진다.
