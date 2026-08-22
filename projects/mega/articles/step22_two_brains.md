# 22. 적을 두 층으로 나눈다

## 들어가며

19편까지 적의 결정은 `Enemy::alive` 플래그 하나였다. 살아 있으면 걷고, 죽으면 사라진다. 상태가 하나뿐이니 전이라는 개념이 없었다.

20편의 격자 항법이 그 하나로 표현되지 않는 사건을 셋 데려온다.

- **앞 칸이 타워로 막혔다.** 메이즈 디펜스의 핵심 조작이 그것이므로, 막힌 적은 되돌아가는 대신 앞의 타워를 때려야 한다.
- **빙결 타워에 맞았다.** 잠시 못 움직인다.
- **오래 살아남았다.** 웨이브 후반의 압박을 위해 가속한다.

셋 다 "지금 무엇을 하는 중인가"를 자료로 들지 않으면 쓸 수 없다.

그리고 보스에게는 그것으로도 부족하다. 후퇴 → 회복은 **여러 틱에 걸친 하나의 행동**이고, 그 사이에 "체력이 낮은가"를 다시 묻지 않아야 한다. 상태를 하나 더 만들어 타이머를 붙이는 것으로도 되지만, 그러면 우선순위(다쳤으면 물러난다 > 슬램이 준비됐으면 내려찍는다 > 전진한다)를 표현할 자리가 없다.

그래서 두 층으로 나눈다. 기준은 **개체 수**다.

| 층 | 수 | 표현 | 왜 |
|---|---|---|---|
| 잡몹 | 수백~수천 | `ai/fsm/state_machine.hpp` | 상태·전이 한 벌을 전원이 돌려 쓴다 |
| 정예 | 한둘 | `ai/bt/{tick,node,builder}.hpp` | 개체마다 트리 한 그루. `Running` 위치가 곧 상태다 |

정예는 같은 행동을 **두 방식으로** 쓸 수 있다. 그 둘이 `elite_brain=plain|fsm|bt` A/B 이고, 이 편의 판정이다. **두 팔이 매치 내내 같은 세계를 도는가.**

결론부터: 처음에는 아니었다. 754틱째에 갈렸다.

---

## 목차
1. [기계 한 벌을 전원이 돌려 쓴다](#1-기계-한-벌을-전원이-돌려-쓴다)
2. [전이 순서가 게임 규칙이다](#2-전이-순서가-게임-규칙이다)
3. [막혔다는 것은 어떻게 아는가](#3-막혔다는-것은-어떻게-아는가)
4. [Running 이 없으면 표현되지 않는 것](#4-running-이-없으면-표현되지-않는-것)
5. [행동 본체는 한 벌이다](#5-행동-본체는-한-벌이다)
6. [두 팔이 어긋난 754틱](#6-두-팔이-어긋난-754틱)
7. [완료 판정이 세계의 함수일 때](#7-완료-판정이-세계의-함수일-때)
8. [끝날 수 있는 상태마다 목록을 다시 적는다](#8-끝날-수-있는-상태마다-목록을-다시-적는다)
9. [트리를 자료로](#9-트리를-자료로)
10. [거부는 파일 단위다](#10-거부는-파일-단위다)
11. [어느 팔도 이기지 못했다](#11-어느-팔도-이기지-못했다)
12. [변이 둘](#12-변이-둘)
13. [실행과 관측](#13-실행과-관측)

---

## 1. 기계 한 벌을 전원이 돌려 쓴다

잡몹은 수천 마리다. 개체마다 `StateMachine<GruntCtx>` 를 하나씩 두면 `std::function` 열아홉 자리(상태 넷 x 진입·갱신·이탈 열둘, 전이 가드 일곱)와 `std::vector` 둘이 개체 수만큼 복제된다. 상태와 전이는 개체 수와 무관한 자료인데 말이다.

그래서 기계는 한 벌이고, 개체는 `current_` 만 든다.

<!-- snippet: include/ai/fsm/state_machine.hpp@fsm-resume -->
```cpp
void Resume(int state) { current_ = state; }
```

`Start` 로 대신할 수 없다. `Start` 는 `onEnter` 를 부르므로, 매 틱 밀어 넣으면 진입 효과가 매 틱 재실행돼 "전이할 때 정확히 한 번"이라는 계약이 깨진다. 그 한 줄짜리 함수가 flyweight 규약 전체다.

호출부는 이렇게 된다.

<!-- snippet: include/game/enemy_brain.hpp@grunt-brain -->
```cpp
class GruntBrain {
public:
    GruntBrain() { Build(); }

    // 개체의 상태를 밀어 넣고, 한 틱 돌리고, 새 상태를 돌려받는다.
    GruntState Tick(GruntState state, GruntCtx& ctx) {
        machine_.Resume(static_cast<int>(state));
        machine_.Update(ctx);
        return static_cast<GruntState>(machine_.Current());
    }
```

개체가 드는 것은 `GruntState` 한 바이트다. 그 바이트는 `BrainRow` 에 있고, `BrainRow` 배열은 적 저장의 행 순서를 그대로 따라간다 — 삽입 순서로 늘고, 압축도 저장과 **같은 규칙**(살아 있는 행을 순서 보존으로 앞으로 당긴다)으로 한다.

<!-- snippet: include/game/game.hpp@brain-compact -->
```cpp
        for (size_t read = 0; read < count && read < brains_.size(); ++read) {
            if (!enemies_.Alive(read)) {
                // 정예 슬롯은 그 자리에서 돌려준다. 반납하지 않으면 정예가
                // 다섯 번 죽는 매치에서 여섯 번째 정예가 트리를 못 받는다.
                if (brains_[read].elite != EliteBrain::kNoSlot)
                    elites_.Release(brains_[read].elite);
                navField_.ClearPath(read);
                continue;
            }
            if (write != read) brains_[write] = brains_[read];
            revealed_[write] = revealed_[read];
            ++write;
        }
```

`BrainRow` 를 `EnemyStore` 안에 넣지 않은 이유는 17편의 저장 A/B 때문이다. 저장 세 팔(AoS·SoA·ECS)의 비교 대상은 "규칙이 매 틱 읽는 열"인데 AI 상태는 AI 시스템만 읽는다. 열을 늘리면 세 팔이 전부 그 열을 나르게 되고, 17편이 잰 수치가 다른 워크로드의 수치가 된다.

## 2. 전이 순서가 게임 규칙이다

`StateMachine::Update` 는 **참인 첫 전이**를 택한다. 그러므로 전이를 등록한 순서가 곧 우선순위다.

```cpp
machine_.AddTransition(-1, [](const GruntCtx& c) { return c.stun > 0.0f; }, stunned);
machine_.AddTransition(stunned, [](const GruntCtx& c) { return c.stun <= 0.0f; }, advance);
machine_.AddTransition(advance, [](const GruntCtx& c) { return c.blocked; }, blocked);
```

첫 줄의 `from = -1` 은 **임의 상태에서** 걸리는 전역 전이다. 빙결이 봉쇄를 이긴다 — 얼어붙은 적이 타워를 때리면 빙결 타워가 아무 일도 하지 않은 것이 되기 때문이다. 이것은 최적화도 구현 세부도 아니라 게임 밸런스이고, 그래서 테스트가 그 순서를 직접 잠근다.

```cpp
ctx.blocked = true;
ctx.stun = mega::game::kFrostStun;
mega::game::GruntState state = brain.Tick(mega::game::GruntState::Advance, ctx);
CHECK(state == mega::game::GruntState::Stunned);
CHECK(!ctx.attack);
```

`Blocked` 에서 풀려날 때 목적지가 둘인 것도 같은 이유다. 나이가 임계를 넘었으면 `Enraged` 로, 아니면 `Advance` 로 — 막혀 있는 동안 늙은 적이 다시 처음부터 걷기 시작하면 그 25초는 아무 뜻이 없다.

## 3. 막혔다는 것은 어떻게 아는가

`blocked` 는 세계가 채워 주는 입력이다. 채우는 쪽은 이렇다.

<!-- snippet: include/game/game.hpp@tick-grunt -->
```cpp
float TickGrunt(size_t i, BrainRow& brain, float dt) {
    GruntCtx ctx;
    ctx.dt = dt;
    ctx.age = brain.age;
    ctx.stun = brain.stun;
    ctx.blocked = !navField_.HasRoute(navGrid_, i, brain.cell);
    brain.state = grunts_.Tick(brain.state, ctx);
    if (ctx.attack) {
        ++melee_;
        AttackBlockingTower(i, brain, dt);
    }
    return ctx.speedScale;
}
```

`HasRoute` 가 20편에서 "다음 칸을 소비하지 않는 질문"으로 만들어진 이유가 여기 있다. 상태 기계는 매 틱 이 질문을 하는데, 그 질문이 커서를 밀면 막히지 않은 적이 한 틱에 두 칸씩 나아간다.

그리고 막은 것이 언제나 타워인 것은 아니다.

```cpp
void AttackBlockingTower(size_t i, BrainRow& brain, float dt) {
    const int target = NearestTowerWithin(enemies_.Pos(i), MeleeReach());
    if (target < 0) {
        brain.cell = OwnCell(i);
        brain.hasCell = false;
        return;
    }
```

바위나 안착한 잔해가 막았으면 때릴 대상이 없다. 그때 목표 칸을 자기 칸으로 되돌려 다음 틱에 길을 다시 찾게 한다. 이 두 줄이 없으면 잔해에 막힌 적이 영원히 그 자리에 서고, 21편의 "교착 없음" 불변식이 처음 무너지는 자리가 정확히 여기다.

## 4. Running 이 없으면 표현되지 않는 것

정예의 후퇴는 1.2초다. 그동안 코어 반대 방향으로 이동하고, 그 뒤 회복한다. 후퇴 중에는 "체력이 낮은가"를 다시 묻지 않는다 — 물으면 매 틱 `arm_retreat` 가 다시 울려 타이머가 영원히 리셋된다.

행동 트리에서 그것을 표현하는 것이 `Running` 이다.

```cpp
Status Tick(Blackboard& bb) override {
    for (; current_ < children_.size(); ++current_) {
        Status s = children_[current_]->Tick(bb);
        if (s == Status::Running) return Status::Running;       // 위치 기억
        if (s == Status::Failure) { current_ = 0; return Status::Failure; }
    }
    current_ = 0;
    return Status::Success;
}
```

`current_` 가 Running 을 낸 자식을 기억하므로, 다음 틱은 조건 노드를 건너뛰고 그 자식부터 재개한다. **memory Composite** 라고 부르는 변형이고, 매 틱 처음부터 재평가하는 reactive 변형과 여기서 갈린다.

이 게임에 memory 쪽이 맞는 이유는 위의 후퇴다. reactive 라면 "체력이 낮은가"가 매 틱 재평가되어, 회복으로 체력이 임계를 넘는 순간 후퇴 시퀀스가 통째로 취소된다. 그것도 하나의 설계지만, 그러면 회복은 절대 끝까지 가지 못한다.

## 5. 행동 본체는 한 벌이다

두 팔이 비교가 되려면 **같은 일**을 해야 한다. 그래서 행동의 본체는 자유 함수 한 벌로 두고, 트리도 상태 기계도 그것을 부른다.

<!-- snippet: include/game/enemy_brain.hpp@elite-actions -->
```cpp
inline void EliteArmRetreat(EliteCtx& c) { c.retreat = kEliteRetreatTime; ++c.retreats; }
inline void EliteArmSlam(EliteCtx& c) { c.slam = kEliteSlamCharge; }
```

본체가 두 벌이 되는 순간 A/B 는 비교가 아니라 서로 다른 두 게임이 된다. 17편에서 저장 세 팔이 `AdvanceEnemyOnPath` 를 공유하는 것과 같은 이유다.

두 팔을 틱 단위로 맞추는 규칙도 여기서 정해진다.

- **자기 Composite 안에 뒤가 있는 노드**(`Retreat` — 뒤에 `Heal` 이 있다)는 **진입 시** 완료를 검사하고, 끝났으면 아무 일도 하지 않고 `Success`. 그러면 BT 는 그 틱에 다음 노드를 돌고, FSM 도 그 틱에 전이해 다음 상태를 돈다.
- **자기 Composite 를 끝내는 노드**(`Heal` · `Slam` · `Advance`)는 **행동한 뒤** 완료를 검사한다. 그러면 BT 는 그 틱을 그 노드로 끝내고, FSM 도 그 틱을 그 상태로 끝낸 뒤 다음 틱에 전이한다.

이 둘까지 맞췄을 때 두 팔은 처음 753틱을 비트 단위로 같이 돌았다. 그리고 754틱에서 갈렸다.

## 6. 두 팔이 어긋난 754틱

진단 출력은 이랬다(`hp` 는 보스 체력, `sp` 는 속도 배수, `heal` 은 그 틱의 회복량, `slam` 은 충전 잔여).

```
   752 fsm hp= 592.4384 sp=0.00 heal=3.240 slam=-0.000 | bt hp= 592.4384 sp=0.00 heal=3.240 slam=-0.000
   753 fsm hp= 589.6783 sp=0.00 heal=3.240 slam=-0.000 | bt hp= 589.6783 sp=0.00 heal=3.240 slam=-0.000
   754 fsm hp= 586.9183 sp=0.00 heal=3.240 slam=-0.000 | bt hp= 583.6783 sp=0.00 heal=0.000 slam= 0.583
```

읽는 법이 중요하다. 752 → 753 에서 체력이 **줄었다.** 회복은 3.24 들어갔는데 결과가 2.76 낮아졌으니, 그 틱의 타워 사격이 6.0 을 깎은 것이다. 즉 **회복 중에도 보스는 계속 맞고 있다.**

목표 체력은 최대치의 55% = 594 다. BT 의 `Heal` 노드는 이렇게 완료를 판정했다.

```cpp
return (c.hp + gain >= c.maxHp * kEliteHealTarget) ? Status::Success : Status::Running;
```

753틱의 입력 체력은 592.4384 였다. `592.4384 + 3.24 = 595.68 >= 594` → **Success.** 시퀀스가 끝나고 다음 틱(754)에 Selector 가 목록을 처음부터 다시 봐서 슬램 가지로 넘어갔다.

FSM 의 전이 가드는 이랬다.

```cpp
m.AddTransition(heal, [](const EliteCtx& c) { return c.hp >= c.maxHp * kEliteHealTarget; }, advance);
```

이 가드는 **다음 틱의 시작에** 평가된다. 754틱의 입력 체력은 589.6783 이다 — 753틱에 3.24 회복하고 6.0 맞았기 때문이다. `589.6783 >= 594` 는 거짓이므로 FSM 은 계속 회복한다.

**같은 식인데 다른 답이 나온다.** 두 팔이 그 식을 서로 다른 시각에 평가하고, 그 사이에 세계가 값을 바꾸기 때문이다.

## 7. 완료 판정이 세계의 함수일 때

후퇴와 슬램에는 이 문제가 없다. 둘의 완료 조건은 `c.retreat <= 0` 과 `c.slam <= 0` 이고, 그 타이머는 행동 자신 말고 아무도 건드리지 않는다. 언제 평가해도 같은 답이 나온다.

회복만 다르다. 완료가 **체력**의 함수이고, 체력은 타워가 계속 깎는다. 그래서 완료를 판정한 그 자리에 결과를 남긴다.

<!-- snippet: include/game/enemy_brain.hpp@elite-actions -->
```cpp
inline ai::bt::Status EliteHealStep(EliteCtx& c) {
    const float gain = kEliteHealRate * c.maxHp * c.dt;
    c.heal = gain;
    c.speedScale = 0.0f;
    c.healed = c.hp + gain >= c.maxHp * kEliteHealTarget;
    return c.healed ? ai::bt::Status::Success : ai::bt::Status::Running;
}
```

`healed` 는 다음 틱까지 사는 한 비트다. 후퇴·슬램에서는 타이머 자체가 그 기록이었고, 회복에는 그런 타이머가 없으니 만들어 준 것이다. 규칙으로 적으면 이렇다.

> 완료 판정이 세계의 함수면, 판정한 자리에 결과를 남긴다. 다시 계산하지 않는다.

## 8. 끝날 수 있는 상태마다 목록을 다시 적는다

`healed` 만으로는 부족했다. 754틱에 BT 는 회복이 끝나자 **슬램**으로 갔는데, 고친 FSM 은 `Advance` 로 갔다. 전이표에 `heal → slam` 이 없었기 때문이다.

여기가 이 A/B 의 진짜 결론이다.

> **memory Selector 는 어느 가지가 끝나는 순간 우선순위 목록 전체를 처음부터 다시 본다. 상태 기계에는 그 장치가 없다.**

트리에서는 Selector 자식 세 개가 목록이고, 어느 가지가 `Success` 를 내면 `current_` 가 0으로 돌아가 다음 틱에 그 목록이 통째로 다시 평가된다. 상태 기계에서 그것에 대응하는 것은 **끝날 수 있는 상태마다 그 목록을 손으로 다시 적는 일**이다.

<!-- snippet: include/game/enemy_brain.hpp@elite-machine -->
```cpp
static void BuildMachine(ai::fsm::StateMachine<EliteCtx>& m) {
```

상태는 넷인데 전이가 여덟이다. 가지가 셋인 트리가 여덟 개의 전이가 된 것이고, 그 배수는 "끝날 수 있는 상태의 수"에서 나온다. 상태가 늘 때마다 이 표는 곱으로 자란다.

전이의 등록 순서도 규칙이다. 같은 출발 상태끼리는 먼저 등록한 것이 이긴다. 그래서 세 덩어리(다침 → 슬램 → 전진)가 트리의 Selector 자식 순서와 같은 순서로 놓여 있다.

## 9. 트리를 자료로

정예 행동을 바꾸려면 C++ 를 고쳐야 하는 상태는 `balance.script` 와의 계약을 깬다. 그래서 트리를 텍스트에서 조립한다.

```
selector
  sequence
    condition hurt
    action arm_retreat
    action retreat
    action heal
  end
  sequence
    condition slam_ready
    action arm_slam
    action slam
  end
  action advance
end
```

토큰 열을 `Builder` 호출로 바꾸는 것이 전부다.

<!-- snippet: include/game/enemy_brain.hpp@elite-build-tree -->
```cpp
ai::bt::NodePtr BuildTree() {
    ai::bt::Builder builder;
    for (Token t : tokens_) {
        switch (t) {
            case Token::Selector: builder.Selector(); break;
            case Token::Sequence: builder.Sequence(); break;
            case Token::End:      builder.End(); break;
```

조건 노드가 읽는 것은 블랙보드뿐이고, 행동 노드가 만지는 것은 개체의 문맥뿐이다. 그 둘을 갈라 두는 것이 자료 기반 정의의 전제다 — 조건이 세계를 직접 질의하면 트리 정의를 파일로 옮겨도 조건마다 코드가 다시 필요해진다.

<!-- snippet: include/game/enemy_brain.hpp@elite-tick -->
```cpp
void Tick(uint8_t slot) {
    Slot& s = slots_[slot];
    s.ctx.speedScale = 1.0f;
```

블랙보드는 `std::unordered_map` 네 벌이다. 시뮬 갱신 경로에 두면 안 되는 자료구조지만, **조회 전용**이라 규약을 어기지 않는다 — 순회하지 않으면 순서가 결과에 새지 않는다.

## 10. 거부는 파일 단위다

`assets/elite.bt` 는 재빌드 없이 교체할 수 있다고 광고된 파일이고, 그 말은 누구든 아무 바이트나 넣을 수 있다는 뜻이다. `level.txt` · `waves.txt` · `balance.script` · `boss.pose` 와 같은 규칙을 그대로 적용한다.

<!-- snippet: include/game/enemy_brain.hpp@elite-configure -->
```cpp
bool Configure(EliteBrainKind kind, size_t slots, const std::string& treeText) {
    kind_ = kind;
    slots_.clear();
    slots_.resize(slots ? slots : 1);
    free_.clear();
    for (size_t i = slots_.size(); i-- > 0;) free_.push_back(static_cast<uint8_t>(i));
    external_ = !treeText.empty() && ParseTree(treeText);
    if (!external_) ParseTree(DefaultEliteTreeText());
```

파싱이 두 단계인 것이 요점이다. 먼저 **토큰 열 전체**를 만들고, 하나라도 실패하면 `tokens_` 를 건드리지 않고 돌아간다. 그 다음 조립한다. 그래서 절반만 저작된 트리라는 상태가 존재할 수 없다.

부분 반영을 허용하지 않는 이유는 진단 가능성이다. 후퇴는 파일이고 슬램은 내장인 트리는 아무도 저작한 적 없는 행동이라, 이상한 것을 본 사람이 원인을 파일에서도 코드에서도 찾을 수 없다.

내장 폴백을 **같은 형식의 텍스트**로 둔 것도 같은 계산이다.

```cpp
inline const char* DefaultEliteTreeText() {
    return "selector\n"
           "  sequence\n"
           "    condition hurt\n"
```

폴백이 다른 형식(예: 코드로 직접 조립한 트리)이면 "파일이 반영됐는가"를 결과로 구분할 수 없다. 같은 형식이면 폴백 경로도 파서를 지나므로, 파서가 깨지면 폴백까지 함께 깨져 조용한 성공이 불가능해진다.

## 11. 어느 팔도 이기지 못했다

A/B 의 판정은 둘이 함께 와야 한다. **결과가 같고, 비용이 다르다.**

앞의 절반은 §6~8 이후 성립한다. 뒤의 절반은 성립하지 않는다.

`EliteBrain::Configure` 를 보면 이유가 명확하다. 슬롯마다 **트리도 상태 기계도 둘 다** 짓는다.

```cpp
for (Slot& slot : slots_) {
    slot.tree = BuildTree();
    BuildMachine(slot.machine);
}
```

팔에 따라 하나만 짓게 할 수는 있다. 그러지 않은 이유는 스위치가 매치 중에 바뀌지 않는다는 보장을 어디에도 적어 두지 않았기 때문이고, 그 보장 없이 조건부로 지으면 팔을 바꾼 순간 빈 포인터를 돌린다. 그래서 **메모리 비용은 두 팔이 같다.**

틱당 비용은 둘 다 작다. 정예는 슬롯 상한이 8이라, 잡몹 수천 마리가 도는 상태 기계와 달리 이 축의 비용은 프로파일에서 유의미한 구간을 만들지 못한다.

**그러므로 기본 팔은 `plain` 으로 남는다.** 이기지 못한 팔을 기본으로 올리지 않는다는 규칙 그대로다. 정예 행동은 켜야 생기는 기능이고, 두 표현 중 어느 쪽을 쓸지는 취향이다 — 다만 **같은 게임**이라는 것이 검사로 잠겨 있다.

층을 나눈 결정 자체는 다르다. 잡몹에 트리를 쓸 수 없는 이유는 비용이 아니라 구조다. BT 의 `Running` 위치는 Composite 안의 `current_` 가 들고, 그것이 곧 개체의 상태다. 즉 **개체마다 트리 한 그루**여야 한다. 정예가 한둘이라 감당되는 것이고, 수천 마리에는 성립하지 않는다.

## 12. 변이 둘

**변이 1 — `Sequence` 의 Running 전파를 없앤다.**

```cpp
Status Tick(Blackboard& bb) override {
    for (; current_ < children_.size(); ++current_) {
        Status s = children_[current_]->Tick(bb);
        if (s == Status::Failure) { current_ = 0; return Status::Failure; }
    }
    ...
```

`Running` 이 다음 자식으로 넘어가 버리므로, 후퇴가 시작된 그 틱에 회복도 함께 실행되고 시퀀스가 한 틱 만에 끝난다. 즉 **여러 틱에 걸친 행동이 사라진다.** `test_boss_behavior` 의 "후퇴 연속 구간이 72틱 근처" 단언이 1로 주저앉아 죽는다. `test_ai_bt` 의 `TestRunningResumesAtSameNode` 도 함께 죽는다.

**변이 2 — `EliteHealStep` 에서 `c.healed` 기록을 지우고 FSM 가드를 식으로 되돌린다.**

이것이 §6 에서 실제로 있었던 상태다. 두 팔 각각은 멀쩡히 돌고, 노드 단위 테스트도 조립기 테스트도 전부 통과한다. 죽는 것은 `test_boss_behavior` 의 팔 대조 하나뿐이고, 그 대조가 없었다면 이 결함은 "정예가 가끔 한 틱 더 회복한다"로 남아 협동 모드의 desync 로만 드러났을 것이다.

## 13. 실행과 관측

**노드와 상태 기계의 단위 계약.**

```
> build\bin\Release\test_ai_bt.exe
[test] ai_bt
[test] ai_bt: 46 checks, 0 failures

> build\bin\Release\test_ai_decision.exe
[test] ai_decision
[test] ai_decision: 78 checks, 0 failures
```

`ai_decision` 이 잠그는 것 넷: `Resume` 가 `onEnter` 를 부르지 않는다, 막히면 `Blocked` 로 가고 풀리면 돌아온다(전이마다 진입·이탈 정확히 한 번), 빙결이 봉쇄를 이긴다, 늙은 적은 막혔다 풀려도 `Advance` 가 아니라 `Enraged` 로 돌아온다.

**조립기와 저작 파일.**

```
> build\bin\Release\test_elite_content.exe
[test] elite_content
  트리 900틱: 후퇴 146틱 · 회복 168틱 · 슬램 3회 · 후퇴횟수 2
  거부 6종: 전부 내장 트리로 통째 폴백
[test] elite_content: 73 checks, 0 failures
```

`후퇴 146틱` 은 72틱짜리 후퇴가 두 번 있었다는 뜻이고(반올림 한 틱 포함), `후퇴횟수 2` 가 상한에 걸린 것이다. 상한이 없으면 이 수가 계속 오르고 정예는 죽지 않는다.

`거부 6종` 은 알 수 없는 행동 이름 · 알 수 없는 조건 이름 · `end` 누락 · `end` 초과 · 잘린 `action` · 공백뿐인 파일이다. 여섯 전부에서 900틱 궤적이 내장 트리와 **비트 단위로** 같았다. 첫 번째 경우가 특히 중요한데, 그 파일은 후퇴 가지가 완전히 정상이고 마지막 줄에만 모르는 이름이 있다 — 부분 반영을 허용하는 파서라면 여기서 절반짜리 트리를 세운다.

같은 파일이 반대 방향도 확인한다. `action advance` 하나만 있는 트리는 후퇴도 슬램도 0이고, 후퇴 가지를 뺀 트리는 슬램만 남는다. **다른 텍스트가 다른 행동을 내지 않으면, "내장 트리와 같다"는 단언은 파서가 입력을 통째로 무시해도 통과한다.**

**제품 안의 정예.**

```
> build\bin\Release\test_boss_behavior.exe
[test] boss_behavior
  plain: 693틱 · 슬램 0 · 후퇴 0틱 · 회복 0틱
  bt: 최저 체력 0.6/1080.0 · 후퇴 연속 73틱(기대 72) · 회복 465틱 · 슬램 5
  bt: 회복 구간 2회 · 문맥 후퇴 2회(상한 2) · 보스 소멸 예 · 1658틱
  fsm=bt 일치 1658틱 · 슬램 5 · 회복 465틱 (plain 은 0 / 0)
  슬램 4회 · 최저 타워 체력 107.0/180.0 · 남은 타워 10/10
[test] boss_behavior: 29 checks, 0 failures
```

읽을 것이 넷 있다.

**`plain: 693틱` 대 `bt: 1658틱`.** 같은 체력, 같은 타워, 같은 맵인데 보스가 2.4배 오래 산다. 후퇴 두 번과 회복 465틱이 그 차이다. 이것이 "켜야 생긴다"의 실물이고, `plain` 팔의 세 카운터가 전부 0인 것이 그 반대편이다.

**`후퇴 연속 73틱(기대 72)`.** `kEliteRetreatTime = 1.2초` x 60Hz = 72틱이고, 부동소수점 누적으로 한 틱이 더 붙었다. Running 전파가 무너지면 이 수가 1이 된다.

**`fsm=bt 일치 1658틱`.** 매치가 끝날 때까지 두 팔의 위치·체력·생존 열이 비트 단위로 같았다. §6~8 을 고치기 전에는 754틱이었다.

**`최저 타워 체력 107.0/180.0`.** 슬램이 카운터만 올린 것이 아니라 실제로 타워를 깎았다. 이 단언이 없으면 위의 `슬램 5` 는 "함수가 불렸다"까지만 말한다.

**제품 — 두 팔.**

```
> build\bin\Release\mega_defense.exe --selftest 25000 --ab map=procgen --ab pathing=flowfield --ab steering=1 --ab stealth=1 --ab elite_brain=bt
[content] elite_tree=external
[selftest] result=CLEAR ticks=4921 towers=10 wave=6 packet=164 protocol=OK render=OK assets=10/10 content=external stomps=3
[selftest] ai field_builds=40 searches=0 melee=0 slams=3 reveal=122

> build\bin\Release\mega_defense.exe --selftest 25000 --ab map=procgen --ab pathing=astar --ab elite_brain=fsm
[selftest] result=CLEAR ticks=5085 towers=6 wave=6 packet=104 protocol=OK render=OK assets=10/10 content=external stomps=6
[selftest] ai field_builds=0 searches=544 melee=0 slams=1 reveal=0
```

`[content] elite_tree=external` — 저장소의 `assets/elite.bt` 가 실행 파일 옆으로 복사되어 실제로 채택됐다. 거부되면 `builtin` 이 되고 표준 오류에 사유가 남는다.

두 실행의 슬램 수가 다른 것(3 대 1)은 팔의 차이가 아니라 맵·타워 배치·조향의 차이다. 두 팔이 같은 게임이라는 판정은 위의 `fsm=bt 일치 1658틱` 이 하는 것이고, 제품 셀프테스트는 **두 팔이 각각 끝까지 돈다**를 확인한다.

`melee=0` 은 정직하게 적어 둘 값이다. 이 절차 맵에서 잡몹이 타워에 막힌 적이 한 번도 없었다는 뜻이고, `Blocked` 상태는 단위 테스트에서만 진입된다. 미로가 충분히 넓어 우회로가 언제나 남기 때문이다.

**변이 확인.**

`Sequence::Tick` 의 Running 전파를 `continue` 로 바꿔 실제로 주입했다.

```
[test] ai_bt
  FAIL tests\test_ai_bt.cpp:66  CHECK(tree->Tick(bb) == Status::Running)
  FAIL tests\test_ai_bt.cpp:67  CHECK(tree->Tick(bb) == Status::Running)
  FAIL tests\test_ai_bt.cpp:69  CHECK_EQ(firstCalls, 1)
[test] ai_bt: 46 checks, 3 failures

[test] boss_behavior
      36 fsm sp=1.00 slam=-0.000 cd=4.983 | bt sp=0.00 slam=0.583 cd=0.000
  FAIL tests\test_boss_behavior.cpp:225  CHECK_EQ(fsm.slams, bt.slams)
  ...
  슬램 0회 · 최저 타워 체력 180.0/180.0 · 남은 타워 10/10
[test] boss_behavior: 29 checks, 12 failures
```

한 변이가 열다섯 단언을 죽인다. 그리고 마지막 줄이 그 결함의 게임 쪽 얼굴이다 — **슬램이 한 번도 터지지 않고 타워가 한 대도 맞지 않는다.** 충전이 여러 틱에 걸치지 못하니 슬램은 시작하자마자 다음 노드로 넘어가고, 실제 발동에는 영원히 닿지 않는다.

두 번째 변이(§12 의 `healed` 제거)는 이 편을 쓰는 동안 실제로 있던 상태다. 그 상태에서 `ai_bt` 도 `ai_decision` 도 `elite_content` 도 전부 초록이었고, 죽은 것은 `boss_behavior` 의 팔 대조 세 줄뿐이었다.

---

## 결론

두 층으로 나눈 기준이 개체 수라는 것은 결과지 출발점이 아니었다. 출발점은 **표현할 것이 있는가**였다. 잡몹에게는 상태 넷과 전이 일곱이 필요했고, 정예에게는 우선순위 있는 조합과 여러 틱짜리 행동이 필요했다. 그 요구가 서로 다르니 표현도 갈렸고, 개체 수는 그 갈림을 감당 가능하게 만드는 조건이었을 뿐이다.

A/B 에서 얻은 것은 성능 수치가 아니라 **결함 하나**다. 754틱의 어긋남은 두 팔을 나란히 돌려 비교하지 않았다면 발견되지 않았다. 각 팔은 혼자서는 완벽히 그럴듯하게 동작했고, 노드 단위 테스트도 조립기 테스트도 전부 초록이었다. 두 표현이 같은 행동이어야 한다는 요구가 있었기 때문에 그 차이가 실패가 됐다.

측정에서 나온 것 셋.

**완료 판정의 시각이 표현마다 다르다.** 트리의 노드는 행동한 직후에, 상태 기계의 가드는 다음 틱의 시작에 판정한다. 완료 조건이 자기 타이머면 두 시각이 같은 답을 내고, 세계의 값이면 다른 답을 낸다.

**Selector 는 목록을 다시 보고 상태 기계는 보지 않는다.** 가지 셋짜리 트리가 전이 여덟 개가 된 배수가 그 차이의 크기다.

**같은 계산을 두 번 하면 두 번째가 틀린다.** `healed` 를 남기는 것과 식을 다시 계산하는 것의 차이가 정확히 그것이었다.

현재 상태의 한계 넷.

**정예 슬롯이 여덟이다.** 아홉 번째 정예는 브레인 없이 스폰되어 걷기만 한다. 슬롯이 개체마다 트리 한 그루를 뜻하므로 상한 자체는 필요하지만, 상한에 걸렸다는 사실이 밖에서 보이지 않는다.

**전이표가 곱으로 자란다.** 상태를 하나 더하면 "끝날 수 있는 상태 x 우선순위 목록"만큼 전이가 는다. 트리 쪽은 자식 하나가 는다. 다섯 번째 상태부터는 이 비대칭이 유지 비용이 된다.

**블랙보드가 문자열 키다.** 틱마다 다섯 번 해싱한다. 정예가 한둘이라 지금은 재지도 못할 비용이지만, 같은 구조를 잡몹에 쓸 수 없는 두 번째 이유이기도 하다.

**`elite.bt` 의 어휘가 고정이다.** 조건은 둘, 행동은 여섯. 새 행동을 저작하려면 여전히 C++ 를 고쳐야 한다. 자료가 된 것은 **조합**이지 어휘가 아니다.
