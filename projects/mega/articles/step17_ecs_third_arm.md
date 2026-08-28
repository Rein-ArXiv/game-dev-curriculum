# 17. 선택적 컴포넌트가 생기는 순간

## 들어가며

9편에서 적 저장이 둘로 갈렸다. `EnemyStore` 는 `std::vector<Enemy>`(AoS)와 필드별 열 배열(SoA)을 함께 들고, `entity_store=aos|soa` 가 런타임에 고른다. 판정은 하나였다 — 3000틱 뒤 상태 해시가 비트 동일한가.

그 두 팔이 성립했던 조건이 있다. **모든 적이 같은 필드를 갖는다.** 위치·체력·속도·경로 인덱스가 종류와 무관하게 전부 있었고, 그래서 열 하나에 빈 자리가 생기지 않았다.

이 편에서 그 조건이 깨진다. 18편이 보스에게 리그를 준다 — 걸음 위상과 마지막으로 디딘 발의 좌표를. 그리고 **그것은 보스만 갖는다.**

고정 열 SoA 에서 그 말은 이런 뜻이다.

```
scale=large 의 적 상한 2048.
리그 열(math::Vec2 + uint32_t = 12바이트) x 2048행 = 24,576바이트.
그 열을 실제로 쓰는 행: 보스 하나 또는 둘.
```

24KB 가 한두 행을 위해 놓인다. AoS 도 같다 — `Enemy` 구조체가 12바이트 커지고, 그 12바이트가 2048개 전부에 붙어 전진 루프의 캐시 라인을 밀어낸다.

archetype ECS 는 그 자리를 다르게 잡는다. 같은 컴포넌트 조합을 가진 엔티티만 한 덩어리에 모으고, 그 덩어리만 그 컬럼을 갖는다. 리그가 없는 2044행에는 리그 컬럼이 **존재하지 않는다**.

`ecs/world.h` 는 3편 무렵부터 저장소에 있었고 `tests/test_ecs.cpp` 가 검증했지만 제품은 부르지 않았다. `docs/LAB_ONLY.md` 의 등재 사유는 "군집 10,000의 컴포넌트 조합별 컬럼 순회. 현 제품은 타워·적·발사체 세 종류를 고정 배열로 들고 있고 그 규모에서는 archetype 이동 비용이 이득을 넘는다"였다. 그 문장의 전제가 지금 바뀐다 — 규모가 아니라 **조합**이 바뀌기 때문이다.

도달점의 수치.

- 세 저장(`aos`·`soa`·`ecs`)이 3000틱 뒤 **같은 상태 해시**. 보스가 실제로 3번 발을 디딘 매치에서.
- 리그 열: SoA **24,576B**(상한 2048행) vs ECS **48B**(보스 4마리) — 512배.
- "리그를 가진 행이 있는가" 질의 2000회: aos **2.43ms** / soa **2.19ms** / ecs **0.03ms** — 약 **80배**.
- 반대 방향: 적 2048에서 600틱 조준(`sim.tower_fire`) aos **11.65ms** / soa **10.00ms** / ecs **56.08ms** — **5.6배 손해**.
- 순회 순서 고정 래퍼를 벗기면 같은 엔티티 집합이 실행 경로에 따라 **다른 누적값**을 낸다(`416d7000` vs `41287000`).

---

## 목차
1. [세 번째 팔이 답하는 질문](#1-세-번째-팔이-답하는-질문)
2. [컴포넌트를 어디서 자를 것인가](#2-컴포넌트를-어디서-자를-것인가)
3. [archetype 이 표현하지 못하는 것 — 행 번호](#3-archetype-이-표현하지-못하는-것--행-번호)
4. [순회 순서는 도달 경로에 의존한다](#4-순회-순서는-도달-경로에-의존한다)
5. [두 역사, 같은 순서 — 결정성을 테스트가 잡는 법](#5-두-역사-같은-순서--결정성을-테스트가-잡는-법)
6. [전진 루프의 세 번째 가지](#6-전진-루프의-세-번째-가지)
7. [압축 — 순서를 아는 것은 저장이 아니다](#7-압축--순서를-아는-것은-저장이-아니다)
8. [값: 리그 열과 조회](#8-값-리그-열과-조회)
9. [대가: 랜덤 접근](#9-대가-랜덤-접근)
10. [두 축의 측정](#10-두-축의-측정)
11. [설계에서 조정한 것](#11-설계에서-조정한-것)
12. [변이](#12-변이)
13. [실행과 관측](#13-실행과-관측)

---

## 1. 세 번째 팔이 답하는 질문

9편의 A/B 는 **배치**를 물었다. 같은 필드 집합을 행으로 묶을 것인가, 열로 펼 것인가. 답은 워크로드가 어느 필드를 읽는가에 달려 있었고, 전진 루프가 10개 열 중 5개만 읽으므로 SoA 가 이겼다.

이 편의 A/B 는 **존재**를 묻는다. 어떤 필드는 어떤 개체에만 있다. 그때 고정 열은 두 가지를 낭비한다.

- **공간.** 없는 개체의 자리도 잡는다.
- **시간.** "이 필드를 가진 개체가 있는가"를 물으려면 전수 스캔뿐이다. 열은 있는데 그 열이 유효한지를 열 자신이 모른다.

archetype 저장은 둘 다 다르게 답한다. 조합이 곧 자료구조이므로 없는 컴포넌트는 컬럼이 없고, "이 조합이 있는가"는 조합 목록(수 개)만 보면 된다.

대신 값을 치른다. 컴포넌트가 붙거나 떨어질 때마다 행이 다른 덩어리로 **이동**하고(memcpy), 개별 엔티티에 접근하려면 엔티티 → 레코드 → archetype → 컬럼 두 단계를 지난다. §9가 그 대가의 수치다.

## 2. 컴포넌트를 어디서 자를 것인가

`Enemy` 하나를 컴포넌트 셋으로 나눴다. 기준은 "누가 읽는가"다.

<!-- snippet: include/game/entity_store.hpp@ecs-components -->
```cpp
struct EnemyCore {
    math::Vec2 pos{0, 0};
    float      speed = 20;
    float      damage = 10;
    uint32_t   pathIndex = 0;
    uint32_t   id = 0;
    uint32_t   row = 0;
    EnemyType  type = EnemyType::Normal;
    uint8_t    alive = 1;
};
struct EnemyVitals {
    float hp = 10, maxHp = 10;
    int   reward = 5;
};
struct EnemyRig {
    math::Vec2 lastStomp{0, 0};
    uint32_t   plants = 0;
};
```

- **`EnemyCore`** — 전진 루프가 읽는 전부. 9편에서 SoA 팔이 읽던 다섯 열(`pos`·`speed`·`damage`·`pathIndex`·`alive`)에 `id`·`type`·`row` 가 더해진 것이다. 이 셋이 왜 들어왔는지가 이 구조체의 성격을 말한다 — `id` 는 조회가, `type` 은 충돌 반지름과 리그 판정이, `row` 는 §3이 쓴다.
- **`EnemyVitals`** — 피해 처리와 스냅샷이 읽는다. 전진 루프는 건드리지 않는다.
- **`EnemyRig`** — **보스만 갖는다.** 이 구조체 하나 때문에 세 번째 팔이 있다.

`hp` 와 `maxHp` 를 코어에서 뺀 것은 SoA 팔의 논리와 같다. 전진 루프가 매 틱 2048행을 훑을 때 체력을 캐시 라인에 끌고 오지 않는다.

리그가 붙는 조건은 한 줄이다.

```cpp
static bool HasRig(EnemyType type) { return type == EnemyType::Boss; }
```

이 함수가 세 팔 모두의 유일한 판정이다. AoS·SoA 는 "이 행이 리그 열을 의미 있게 쓰는가"를 이것으로 묻고, ECS 는 "이 엔티티에 컴포넌트를 붙일 것인가"를 이것으로 정한다. 판정이 두 벌이 되면 세 팔의 해시가 그 자리에서 갈린다.

## 3. archetype 이 표현하지 못하는 것 — 행 번호

`EnemyStore` 의 공개 계약은 9편에 이렇게 적혀 있다.

> 어느 저장이든 살아 있는 행의 상대 순서는 **삽입 순서**다. 조준 동점(같은 거리)의 승자와 스냅샷에 실리는 순서가 그 순서로 결정된다.

archetype 저장은 그 계약을 **표현할 수 없다.** 이유가 둘이다.

- 엔티티는 조합별로 나뉘어 있다. 보스와 잡몹은 다른 덩어리에 있고, 두 덩어리 사이에 "누가 먼저 들어왔나"라는 순서가 없다.
- 덩어리 **안의** 자리도 안정적이지 않다. 컴포넌트를 붙이면 행이 이동하고, 원래 있던 자리는 마지막 행이 swap-remove 로 채운다. 그 순간 그 archetype 안의 순서가 흐트러진다.

그래서 순서는 저장 바깥에 둔다.

```cpp
mutable World          ecs_;
std::vector<EntityId>  order_;   // 행 번호 → 엔티티(삽입 순서)
```

`order_[i]` 가 행 `i` 다. 그리고 반대 방향 — 엔티티에서 행 번호로 — 이 필요해서 `EnemyCore::row` 가 있다. 청크 순회가 컬럼 포인터만 주므로, 그 안에서 "지금 몇 번 행인가"를 물을 다른 방법이 없다.

이 `order_` 배열이 세 번째 팔의 가장 큰 비용이다. 엔티티당 8바이트가 더 들고, 모든 개별 접근이 그 배열을 한 번 더 지난다. 그것을 감수한 이유는 **비교 가능성**이다 — 순서를 바꾸는 순간 세 팔은 서로 다른 게임이 되고, A/B 라는 말이 성립하지 않는다.

## 4. 순회 순서는 도달 경로에 의존한다

`World::ForEachChunk` 는 `archetypes_` 를 배열 순서로 훑는다. 그 배열은 **어느 조합이 먼저 나타났는가** 순으로 늘어난다.

이것이 왜 문제인가. 웨이브 1에 잡몹만 나오면 `(Core, Vitals)` archetype 이 먼저 생기고, 웨이브 5의 보스가 `(Core, Vitals, Rig)` 를 만든다. 반대로 보스가 먼저 나오는 콘텐츠라면 순서가 뒤집힌다. **같은 엔티티 집합인데 배열 순서가 다르다.**

그 순서로 부동소수를 누적하면 결과가 갈린다. 결합법칙이 성립하지 않기 때문이다. 그리고 이 게임의 시뮬은 코어 체력을 누적으로 깎고, 그 값이 상태 해시에 들어간다.

그래서 시뮬 갱신 경로가 쓰는 순회를 따로 뒀다.

<!-- snippet: include/ecs/world.h@archetype-order -->
```cpp
template <class... Ts, class Fn>
void ForEachChunkOrdered(Fn&& fn) {
    uint64_t qmask = (Bit(ComponentTypeId<Ts>()) | ...);
    visit_.clear();
    for (uint32_t i = 0; i < (uint32_t)archetypes_.size(); ++i) {
        const Archetype& a = archetypes_[i];
        if ((a.mask & qmask) != qmask || a.entities.empty()) continue;
        visit_.push_back(i);
    }
    std::sort(visit_.begin(), visit_.end(), [this](uint32_t l, uint32_t r) {
        return archetypes_[l].mask < archetypes_[r].mask;
    });
    for (uint32_t i : visit_) {
        Archetype& a = archetypes_[i];
        fn(a.entities.size(), ColPtr<Ts>(a)...);
    }
}
```

정렬 기준이 **archetype 인덱스가 아니라 조합 마스크**인 것이 요점이다. 인덱스는 생성 순서 그 자체라 정렬해도 아무것도 바뀌지 않는다. 마스크는 컴포넌트 집합의 순수 함수라 도달 경로와 무관하다.

정렬 비용은 archetype 개수(제품에서 3~5개, 합성 워크로드에서 9개)에 대한 것이고, 청크 하나의 원소 수(최대 2048)에 비하면 없다.

빈 archetype 을 건너뛰는 `a.entities.empty()` 도 계약이다. 컴포넌트 이동으로 텅 빈 archetype 은 배열에 그대로 남으므로, 걸러 주지 않으면 원소 0개짜리 청크가 콜백에 실려 온다. `count > 0` 을 전제로 쓴 루프가 그 자리에서 깨진다.

**한 가지 남은 의존.** `ComponentTypeId<T>()` 는 프로세스 전역 카운터를 **최초 사용 순서**로 나눠 준다. 그래서 마스크의 절대값은 어떤 컴포넌트가 먼저 인스턴스화되었는가에 달려 있다. 한 실행 파일 안에서는 그 순서가 코드로 고정되므로 실행마다 같지만, 서로 다른 바이너리 사이에서는 같다는 보장이 없다. 지금 이 저장소에서 그것이 문제가 되지 않는 이유는 호스트와 클라이언트가 같은 실행 파일이기 때문이고, 그 전제가 깨지는 날에는 컴포넌트에 명시적 id 를 주어야 한다.

## 5. 두 역사, 같은 순서 — 결정성을 테스트가 잡는 법

"순서가 고정됐다"를 어떻게 단언하는가. 한 번 돌려서 나온 순서를 상수로 박으면 그것은 순서를 검사하는 게 아니라 기록하는 것이다.

`tests/test_ecs_order.cpp` 는 **같은 최종 집합에 서로 다른 경로로 도달한 두 월드**를 만든다.

```cpp
const std::vector<Recipe> kForward{
    {0, 1.5f}, {1, 2.25f}, {2, 3.125f}, {4, 4.0625f},
    {3, 5.5f}, {5, 6.75f}, {6, 7.875f}, {7, 8.125f},
};
```

`extras` 비트가 세 컴포넌트(Brain·Cloak·Rig)를 켠다. 조합 하나에 엔티티 하나씩 넣으므로 archetype 안의 자리 순서가 결과에 섞이지 않고, **방문 순서가 곧 값의 순서**가 된다.

한 월드는 이 순서대로, 다른 월드는 뒤집어서 만든다. 그리고 방문 순서에 민감한 누적을 돌린다.

```cpp
uint32_t FoldBits(const std::vector<float>& values) {
    float accumulator = 1.0f;
    for (float v : values) accumulator = accumulator * 0.5f + v;
    uint32_t bits = 0;
    std::memcpy(&bits, &accumulator, sizeof(bits));
    return bits;
}
```

곱셈-덧셈은 결합법칙을 지키지 않으므로 순서가 다르면 비트가 다르다. 시뮬이 코어 체력을 누적하는 방식의 축소판이다.

단언은 둘이다.

```cpp
CHECK_EQ(FoldBits(a), FoldBits(b));                       // 정렬 순회는 같다
CHECK(FoldBits(rawForward) != FoldBits(rawBackward));     // 생성 순서 순회는 다르다
```

두 번째 줄이 첫 번째 줄을 **공짜가 아니게** 만든다. 정렬 래퍼가 없어도 우연히 같은 순서가 나오는 워크로드라면 첫 단언은 아무것도 증명하지 않는다.

실측:

```
  ordered fold=416c0000/416c0000  raw fold=416d7000/41287000
```

정렬 순회는 두 역사에서 같고, 생성 순서 순회는 다르다.

세 번째 검사는 **archetype 이동을 겪은 월드**다. 컴포넌트를 붙였다 떼는 여정을 강제로 넣어 행이 덩어리 사이를 옮겨 다니게 한 뒤, 최종 조합만 같으면 순서도 같다는 것을 본다. 이동이 순서를 흐트러뜨리지 않는다는 것이 §3의 `order_` 가 필요한 이유이자, 그 배열이 제 몫을 한다는 증거다.

## 6. 전진 루프의 세 번째 가지

`EnemyAdvanceSystem` 은 9편까지 두 가지였다 — SoA 는 열 포인터를 잡고, AoS 는 행 배열을 훑는다. ECS 가지는 컬럼을 하나씩 받지 않는다. **조합별 청크**를 받는다.

<!-- snippet: include/game/entity_store.hpp@ecs-chunks -->
```cpp
template <class Fn>
void ForEachCoreChunk(Fn&& fn) {
    ecs_.ForEachChunkOrdered<EnemyCore>(
        [&fn](size_t count, EnemyCore* core) { fn(count, core); });
}
```

그리고 규칙 쪽:

<!-- snippet: include/game/game.hpp@enemy-advance -->
```cpp
void EnemyAdvanceSystem(float dt) {
    MEGA_PROFILE_SCOPE("sim.enemy_advance");
    const size_t count = enemies_.Size();
    if (count == 0) return;
    // ...
    float* damage = scratch_.Alloc<float>(count);
    for (size_t i = 0; i < count; ++i) damage[i] = 0.0f;
    // ...
    if (enemies_.Kind() == EntityStoreKind::Ecs) {
        enemies_.ForEachCoreChunk([&](size_t chunk, EnemyCore* cells) {
            for (size_t k = 0; k < chunk; ++k) {
                EnemyCore& row = cells[k];
                if (!row.alive) continue;
                if (onPath)
                    AdvanceEnemyOnPath(row.pos, row.pathIndex, row.alive, row.speed,
                                       row.damage, path, pathCount, dt, damage[row.row]);
                else
                    AdvanceEnemyTowardCore(row.pos, row.alive, row.speed, row.damage,
                                           core, dt, damage[row.row]);
            }
        });
    } else if (enemies_.Kind() == EntityStoreKind::Soa) {
    // ...
    for (size_t i = 0; i < count; ++i)
        if (damage[i] != 0.0f) coreHp_ -= damage[i];
}
```

`damage[row.row]` 가 §3의 `row` 필드가 존재하는 이유다. 코어 피해는 **행 번호 오름차순으로** 합쳐져야 직렬 실행과 비트 동일한데(9편·11편), 청크 순회는 행 번호 순으로 오지 않는다. 개체별로 받아 두고 마지막에 인덱스 순으로 합치면 순회 순서가 결과를 바꾸지 못한다.

**워커에 나누지 않았다.** 11편의 `RunChunks` 는 인덱스 구간을 자르는데, ECS 팔의 청크 경계는 개체 수가 아니라 **조합**이 정한다. 보스 하나짜리 청크와 2047개짜리 청크를 `kParallelGrain` 으로 자르면 그 자름이 조합 구성마다 달라지고, "워커 수가 결과를 바꾸지 않는다"는 11편의 단언이 기대는 전제 — 자름이 인덱스만의 함수라는 것 — 이 흔들린다. 지금은 직렬로 두고, 그 사실을 한계로 적는다.

## 7. 압축 — 순서를 아는 것은 저장이 아니다

<!-- snippet: include/game/entity_store.hpp@compact-dead -->
```cpp
void CompactDead() {
    if (kind_ == EntityStoreKind::Ecs) {
        size_t write = 0;
        for (size_t read = 0; read < order_.size(); ++read) {
            const EntityId entity = order_[read];
            EnemyCore* core = ecs_.Get<EnemyCore>(entity);
            if (!core || !core->alive) { ecs_.Destroy(entity); continue; }
            order_[write] = entity;
            core->row = static_cast<uint32_t>(write);
            ++write;
        }
        order_.resize(write);
        RebuildIndex();
        return;
    }
    // ...
}
```

세 팔이 하는 일이 같아 보이지만 대상이 다르다.

- **AoS** 는 `std::remove_if` 로 행을 당긴다.
- **SoA** 는 열마다 쓰기 커서를 당긴다.
- **ECS** 는 `order_` 배열을 당긴다. 저장 자체는 손대지 않는다 — `Destroy` 가 archetype 안에서 swap-remove 를 하지만 그 자리 바뀜은 `order_` 에 아무 영향이 없다. 살아남은 엔티티의 `row` 만 새로 매긴다.

`ecs_.Destroy` 가 다른 엔티티의 데이터를 **행 통째로 memcpy** 해 옮긴다는 점이 여기서 미묘하다. `row` 필드는 그 memcpy 에 실려 함께 움직이므로, 방금 쓴 값이 이동 뒤에도 유효하다. 만약 행 번호를 `EnemyCore` 바깥의 병렬 배열에 뒀다면 이동마다 그 배열을 따로 고쳐야 했고, 그 고침을 잊는 것이 정확히 이 부류의 버그다.

## 8. 값: 리그 열과 조회

**공간.** `EnemyStore::RigBytes()` 가 세 팔의 리그 열 크기를 답한다. SoA·AoS 는 상한만큼, ECS 는 그 컴포넌트를 실제로 가진 엔티티만큼.

```
> build_verify\bin\Release\test_entity_store_ab.exe
  rig columns: soa=24576B(상한 2048행) ecs=48B(보스 4) ecs_swarm=0B
```

보스가 **하나도 없는** 군집에서 ECS 는 0바이트다. 컬럼이 존재하지 않기 때문이다. SoA 는 같은 상황에서도 24,576바이트를 들고 있다.

**시간.** 스톰프 시스템이 매 틱 처음 하는 질문은 "리그를 가진 행이 있는가"다.

<!-- snippet: include/game/entity_store.hpp@rig-rows -->
```cpp
size_t RigRows() const {
    if (kind_ == EntityStoreKind::Ecs) {
        size_t rows = 0;
        ForEachRigChunk([&rows](size_t count, EnemyCore*, EnemyRig*) { rows += count; });
        return rows;
    }
    size_t rows = 0;
    const size_t count = Size();
    for (size_t i = 0; i < count; ++i) if (HasRig(Type(i))) ++rows;
    return rows;
}
```

같은 답을 내지만 하는 일이 다르다. ECS 는 조합 목록(3~9개)을 훑고 해당 청크의 원소 수를 더한다. 나머지 둘은 행을 전수로 훑으며 `type` 을 비교한다.

```
  RigRows() x2000 (2048행 중 보스 4): aos=2.43ms soa=2.19ms ecs=0.03ms
```

**약 80배.** 이 수는 개체 수에 비례해 벌어진다 — ECS 쪽은 개체 수와 무관하고, 나머지 둘은 선형이기 때문이다.

제품 안에서 그 차이는 이렇게 나타난다. `sim.stomp` 스코프는 `RigRows() > 0` 일 때만 열리므로, 스코프의 호출 수가 곧 "보스가 살아 있던 틱 수"다.

```
> build_verify\bin\Release\mega_defense.exe --selftest --trace-out trace_aos.tsv
sim.stomp	432	0.047	0.014	0.0001
```

1377틱 중 432틱. 나머지 945틱은 조기 반환한다.

## 9. 대가: 랜덤 접근

세 번째 팔이 이기기만 한다면 스위치를 둘 이유가 없다. 지는 자리가 훨씬 크다.

```
> build_verify\bin\Release\test_entity_store_ab.exe
[entity_store scale=large]
  aos  enemies=2048 ticks=600 total=20.3ms  advance=3.86ms fire=11.65ms projectile=3.46ms cleanup=0.37ms
  soa  enemies=2048 ticks=600 total=18.7ms  advance=3.98ms fire=10.00ms projectile=3.19ms cleanup=0.66ms
  ecs  enemies=2048 ticks=600 total=76.4ms  advance=4.01ms fire=56.08ms projectile=11.77ms cleanup=4.21ms
```

읽는 법이 시스템마다 다르다.

- **`advance` 3.86 / 3.98 / 4.01ms — 사실상 같다.** 세 팔 모두 연속 메모리를 순차로 훑는다. ECS 의 청크도 컬럼이라 SoA 와 같은 접근 패턴이다. archetype 저장의 이론적 강점이 그대로 나타나는 자리이고, 실제로 손해가 없다.
- **`fire` 11.65 / 10.00 / **56.08ms** — 5.6배 손해.** 조준(`SelectTargetIn`)은 행 번호로 개체를 하나씩 묻는다. ECS 팔에서 그 한 번은 `order_[i]` → 레코드 조회 → archetype 의 `ColumnOf` 선형 탐색 → 컬럼 오프셋 계산이다. 타워 8개 x 적 2048개 x 600틱이면 그 경로를 천만 번 지난다.
- **`cleanup` 0.37 / 0.66 / 4.21ms — 11배.** 압축이 `Destroy` 마다 행 memcpy 를 유발한다.

**결론은 "ECS 가 느리다"가 아니다.** 순차 순회에서는 동등하고, 조합 질의에서는 80배 빠르며, 개별 랜덤 접근에서는 5배 느리다. 이 게임의 기본 규모(적 64)에서는 세 수치 전부 무시할 만하고, `scale=large` 에서는 조준이 지배적이라 SoA 가 옳다. 그래서 **기본값은 `aos`** 로 두었다 — 스위치가 생기기 전의 동작이라는 규약이 먼저이기도 하고, 측정이 그 규약과 어긋나지 않기 때문이다.

## 10. 두 축의 측정

설계는 "개체 수와 조합 다양성 두 축으로 교차점을 잰다"고 적었다. 개체 수 축은 §9의 표다. 조합 다양성 축은 `tests/test_ecs_order.cpp` 가 잰다 — 엔티티 8192개를 몇 개의 조합으로 쪼개는가만 바꾼다.

```
> build_verify\bin\Release\test_ecs_order.exe
  [조합 다양성 축] 엔티티 8192 고정
    조합  1개(archetype  3) 구축=0.60ms 순회x400=1.58ms 이동(추가+제거 16384회)=0.59ms
    조합  2개(archetype  5) 구축=0.53ms 순회x400=1.60ms 이동(추가+제거 16384회)=0.75ms
    조합  4개(archetype  9) 구축=0.81ms 순회x400=1.60ms 이동(추가+제거 16384회)=0.75ms
    조합  8개(archetype  9) 구축=0.92ms 순회x400=1.60ms 이동(추가+제거 16384회)=0.69ms
```

**예상과 다른 결과가 하나 있다.** 조합을 8배로 늘려도 순회 비용이 움직이지 않는다(1.58 → 1.60ms). 조합이 늘면 청크가 잘게 쪼개져 순회가 느려질 것이라 예상했는데, 8192개를 8조각으로 나눠도 조각 하나가 1024개다 — 캐시 라인 관점에서 여전히 큰 덩어리라 경계 비용이 묻힌다. **조합 다양성이 순회를 해치려면 조합 수가 개체 수에 근접해야 한다**는 것이 이 표의 실제 내용이고, 그 지점은 이 게임의 어떤 규모에서도 오지 않는다.

`archetype` 개수가 조합 수보다 큰 것(조합 4개에 archetype 9개)은 **중간 조합** 때문이다. `Add` 는 컴포넌트를 하나씩 붙이므로 `(Core)` → `(Core, Brain)` → `(Core, Brain, Cloak)` 처럼 거쳐 간 조합이 전부 배열에 남는다. 그 빈 archetype 들은 `ForEachChunkOrdered` 가 건너뛰지만 메모리는 잡고 있다 — archetype 저장의 조용한 비용이다.

이동 비용은 16,384회에 0.6~0.8ms, 즉 한 번에 40~50나노초다. 컴포넌트가 붙고 떨어지는 일이 매 틱 수천 번 일어나는 게임이라면 이 수가 지배적이 되지만, 이 게임에서 리그가 붙는 순간은 보스 스폰 한 번뿐이다.

## 11. 설계에서 조정한 것

**1. 순회 정렬 기준을 archetype id 가 아니라 조합 마스크로.** 설계는 "archetype id 오름차순으로 고정하는 래퍼"라고 적었는데, `archetypes_` 의 인덱스가 곧 id 이므로 그 정렬은 항등 함수다 — 래퍼를 벗겨도 아무것도 달라지지 않아 변이가 성립하지 않는다. 마스크로 정렬해야 도달 경로 의존성이 실제로 사라진다(§4).

**2. 컴포넌트 조합을 넷이 아니라 하나로.** 설계는 "정예는 Brain, 스텔스는 Cloak, 보스는 Rig, 넉백 대상은 Body" 를 들었다. 그중 제품에 실재하는 것은 Rig 하나뿐이다 — 나머지 셋은 행동 트리·인지·강체가 적에게 붙는 단계(`docs/LAB_ONLY.md` 의 P2·P4)의 것이고, 지금 넣으면 아무도 읽지 않는 컴포넌트가 된다. 조합 다양성 축은 그래서 제품이 아니라 `test_ecs_order.cpp` 의 합성 워크로드에서 잰다(§10). 그 테스트의 컴포넌트 이름을 `Brain`·`Cloak`·`Rig` 로 둔 것은 그 자리에 무엇이 올지를 남기기 위해서다.

**3. 리그를 청크 순회로 처리하지 않는다.** `ForEachRigChunk` 는 존재하지만 스톰프 시스템의 **판정**에만 쓰고(§8), 실제 피해 처리는 세 팔 모두 행 번호 오름차순 스캔이다. 청크 순회로 처리하면 archetype 안의 자리 순서가 그대로 피해 적용 순서가 되는데, 그 순서는 §3에서 본 대로 안정적이지 않다. 타워 체력은 부동소수 감산으로 깎이므로 순서가 결과를 바꾼다.

**4. `RigBytes()` 를 AoS 에서는 `capacity()` 기준으로.** AoS 는 열이라는 것이 없어 "리그 열의 크기"가 성립하지 않는다. 대신 구조체에 박힌 12바이트 x 예약 용량으로 셌다 — 실제로 그만큼의 메모리가 리그 때문에 늘어난 것이 맞기 때문이다.

## 12. 변이

**정렬 래퍼를 벗긴다.** `ForEachChunkOrdered` 의 `std::sort` 세 줄을 지웠다.

```
> build\bin\Release\test_ecs_order.exe
[test] ecs_order
  FAIL tests\test_ecs_order.cpp:95  CHECK(a[i] == b[i])          (x6)
  FAIL tests\test_ecs_order.cpp:96  CHECK_EQ(FoldBits(a), FoldBits(b))
  ordered fold=416d7000/41287000  raw fold=416d7000/41287000
  FAIL tests\test_ecs_order.cpp:137  CHECK(a[i] == b[i])         (x7)
  FAIL tests\test_ecs_order.cpp:138  CHECK_EQ(FoldBits(a), FoldBits(b))
[test] ecs_order: 28 checks, 15 failures
```

출력 한 줄이 변이를 그대로 그린다. `ordered fold` 가 `raw fold` 와 **같은 값**이 되었다 — 정렬을 지나지 않는 순회와 구분되지 않는다는 뜻이고, 그래서 두 역사가 다른 누적값을 낸다.

복원 후 재통과:

```
> build\bin\Release\test_ecs_order.exe
  ordered fold=416c0000/416c0000  raw fold=416d7000/41287000
[test] ecs_order: 28 checks, 0 failures
```

## 13. 실행과 관측

깨끗한 빌드에서.

```
> cmake -S . -B build_verify
-- 계층 규칙 OK: games/ 에 OS API 직접 호출 없음, 진입점 ≤ 150줄
-- mega_core: 헤더 110개(생성 TU 동수) + 구현 4개
> cmake --build build_verify --config Release
> ctest -C Release -j 8
100% tests passed out of 84
```

**세 저장이 같은 세계를 돈다.**

```
> build_verify\bin\Release\test_entity_store_ab.exe
[test] entity_store_ab
  rig columns: soa=24576B(상한 2048행) ecs=48B(보스 4) ecs_swarm=0B
  RigRows() x2000 (2048행 중 보스 4): aos=2.43ms soa=2.19ms ecs=0.03ms
  match rig columns: soa=768B ecs=0B stomps=3
[entity_store scale=large]
  aos  enemies=2048 ticks=600 total=20.3ms  advance=3.86ms fire=11.65ms projectile=3.46ms cleanup=0.37ms
  soa  enemies=2048 ticks=600 total=18.7ms  advance=3.98ms fire=10.00ms projectile=3.19ms cleanup=0.66ms
  ecs  enemies=2048 ticks=600 total=76.4ms  advance=4.01ms fire=56.08ms projectile=11.77ms cleanup=4.21ms
[test] entity_store_ab: 121 checks, 0 failures
```

`stomps=3` 이 이 표를 읽을 수 있게 만든다. 보스가 실제로 발을 디딘 매치에서 세 해시가 같았다는 뜻이고, 그 수가 0이면 위의 해시 일치는 "리그 열을 아무도 건드리지 않았다"는 말과 구분되지 않는다.

`match rig columns: soa=768B ecs=0B` — 매치가 끝나는 시점에 SoA 는 여전히 768바이트(상한 64행 x 12)를 들고 있고, ECS 는 보스가 죽었으므로 0이다.

**제품 안에서.** 세 번째 팔로 한 판을 끝까지 돈다.

```
> build_verify\bin\Release\mega_defense.exe --selftest --ab entity_store=ecs --ab boss_render=rig --ab camera=tilted3d
[switches] entity_store=ecs ... camera=tilted3d ... boss_render=rig ...
[selftest] result=CLEAR ticks=1377 towers=11 wave=6 packet=175 protocol=OK render=OK assets=10/10 content=external stomps=2
```

기본 팔과 같은 틱 수(1377), 같은 타워 수(11), 같은 웨이브(6)로 끝난다.

시뮬 시스템별 비용(같은 매치, 저장만 바꿔서):

| 스코프 | `entity_store=aos` | `entity_store=ecs` |
|---|---|---|
| `sim.step` 합계 | 2.060ms | 2.375ms |
| `sim.enemy_advance` | 0.276ms | 0.345ms |
| `sim.cleanup` | 0.153ms | 0.188ms |
| `sim.stomp`(n=432) | 0.047ms | 0.072ms |

기본 규모(적 최대 26)에서는 세 팔의 차이가 프레임 예산의 0.02% 안에 있다. §9의 5.6배가 나타나려면 `scale=large` 가 필요하고, 그 규모는 이 게임의 콘텐츠가 만들지 않는다.

**순서 검사.**

```
> build_verify\bin\Release\test_ecs_order.exe
[test] ecs_order
  ordered fold=416c0000/416c0000  raw fold=416d7000/41287000
  [조합 다양성 축] 엔티티 8192 고정
    조합  1개(archetype  3) 구축=0.60ms 순회x400=1.58ms 이동(추가+제거 16384회)=0.59ms
    조합  8개(archetype  9) 구축=0.92ms 순회x400=1.60ms 이동(추가+제거 16384회)=0.69ms
[test] ecs_order: 28 checks, 0 failures
```

---

## 결론

세 번째 팔이 답하는 질문은 앞의 둘과 다르다. AoS↔SoA 는 **같은 필드 집합을 어떻게 놓을 것인가**를 물었고, ECS 는 **필드가 개체마다 다를 때 무엇이 달라지는가**를 묻는다. 그 질문이 성립하려면 선택적 컴포넌트가 실재해야 했고, 그것을 만든 것이 보스의 리그다.

측정에서 나온 것 셋.

**순차 순회는 동등하다.** 전진 루프에서 세 팔의 차이가 4% 안이다(3.86 / 3.98 / 4.01ms). archetype 컬럼도 결국 연속 메모리이므로 SoA 와 같은 접근을 한다.

**조합 질의는 80배 빠르고 랜덤 접근은 5.6배 느리다.** 어느 쪽이 지배적인가는 워크로드가 정하고, 이 게임에서는 조준(랜덤 접근)이 지배적이다. 그래서 기본값은 바뀌지 않았다.

**조합 다양성은 순회를 해치지 않았다.** 조합을 8배로 늘려도 순회 시간이 1.58 → 1.60ms 였다. 조각 하나가 1024개면 경계 비용이 묻힌다는 것이고, 조합 수가 개체 수에 근접해야 그 비용이 보인다.

현재 상태의 한계 넷.

**ECS 팔은 워커에 나뉘지 않는다.** 청크 경계가 조합이 정하는 것이라 인덱스 기반 분할과 맞물리지 않고, 그 상태로 나누면 "워커 수가 결과를 바꾸지 않는다"는 11편의 단언이 기대는 전제가 흔들린다. `jobs>0` 과 `entity_store=ecs` 를 함께 켜면 전진 루프만 직렬로 돈다.

**행 번호가 저장 밖에 있다.** `order_` 배열과 `EnemyCore::row` 가 서로를 가리키는 이중 장부이고, 둘이 어긋나면 조용히 다른 개체의 피해가 합쳐진다. 지금 그것을 지키는 것은 `CompactDead` 한 곳뿐이라는 사실이 이 구조의 약한 지점이다.

**컴포넌트 id 가 최초 사용 순서로 정해진다.** 마스크 정렬은 그 id 위에 서 있으므로, 서로 다른 실행 파일이 같은 순서를 낸다는 보장이 없다. 호스트와 클라이언트가 같은 바이너리인 지금은 문제가 되지 않는다.

**중간 archetype 이 회수되지 않는다.** `Add` 를 하나씩 하면 거쳐 간 조합이 전부 배열에 남고, 비어도 지워지지 않는다. 조합 4개짜리 워크로드에서 archetype 이 9개가 된 것이 그 흔적이다.
