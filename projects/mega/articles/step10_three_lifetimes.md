## 들어가며

9편이 규모를 올렸다. 적 2048마리를 세울 수 있고, 그 규모에서 시스템별 시간을 잰다.

그 규모에서 새로 보이는 것이 하나 있다. **버퍼 크기가 개체 수를 따라간다.** 조준 결과 배열은 타워 수, 스플래시 후보 배열은 적 수, 개체별 코어 피해 배열도 적 수다. 적 64마리에서는 전부 수백 바이트라 스택에 둬도 됐지만, 2048마리에서는 프레임마다 16KB 를 잡았다 버린다.

지금 그 버퍼들은 전부 같은 곳에서 온다 — 범용 할당자. 그리고 범용 할당자는 **수명을 묻지 않는다.** 프레임 하나만 사는 배열도, 매치 내내 사는 맵 버퍼도, 초당 수십 번 나고 죽는 발사체도 전부 같은 `operator new` 로 간다.

이 편은 그 하나를 셋으로 가른다.

| 수명 | 예 | 할당자 | 스위치 |
|---|---|---|---|
| 프레임 | 조준 결과 · 스플래시 후보 · 개체별 피해 · 사라진 발사체 좌표 | `memory/arena.h` | `frame_alloc=malloc\|arena` |
| 회전이 빠른 고정 크기 | 발사체 블록 | `memory/pool.h` | `body_alloc=vector\|pool` |
| 매치 · 가변 크기 | 맵 타일 배열 · 경로 배열 | `memory/slab.h` | `blob_alloc=malloc\|slab` |

그리고 풀이 들어오면 새 문제가 하나 생긴다 — **스텝 중에 죽은 객체를 그 스텝이 계속 읽는다.** `containers/intrusive_list.h` 가 그 문제의 답이다.

도달점은 이렇다. 시뮬 한 스텝의 프레임당 힙 할당이 **3회에서 0회**가 되고, 그 사실이 전역 `operator new` 카운터로 단언되며, 세 할당자를 다 켠 팔이 기본 팔과 비트 동일한 상태 해시를 낸다.

---

## 목차
1. [수명을 묻지 않는 할당자](#1-수명을-묻지-않는-할당자)
2. [프레임 스크래치: Mark 하나, Rewind 하나](#2-프레임-스크래치-mark-하나-rewind-하나)
3. [스크래치를 쓰는 네 자리](#3-스크래치를-쓰는-네-자리)
4. [넘치면 죽지 않고 센다](#4-넘치면-죽지-않고-센다)
5. [측정: 셀 수 있는 것을 센다](#5-측정-셀-수-있는-것을-센다)
6. [발사체 풀: 속도가 아니라 주소 안정성](#6-발사체-풀-속도가-아니라-주소-안정성)
7. [죽었지만 아직 읽는다 — deferred-free 목록](#7-죽었지만-아직-읽는다--deferred-free-목록)
8. [매치 수명 가변 버퍼와 크기 클래스](#8-매치-수명-가변-버퍼와-크기-클래스)
9. [스위치가 바뀌는 순간의 함정](#9-스위치가-바뀌는-순간의-함정)
10. [변이: 되감기와 미룸](#10-변이-되감기와-미룸)
11. [실행과 관측](#11-실행과-관측)

---

## 1. 수명을 묻지 않는 할당자

`std::vector<int> candidates;` 한 줄에는 정보가 빠져 있다. 이 배열이 얼마나 사는지가 타입에 없다.

그래서 "이 프레임에 몇 번 할당했는가"라는 질문에 답할 수단이 없다. 답할 수 없으면 줄일 수도 없고, 줄였다고 주장할 수도 없다.

수명별로 가르면 답이 생긴다. 프레임 아레나에서 나온 것은 프레임 끝에 되감기 한 번으로 전부 사라진다 — 개별 해제가 없으므로 "몇 번 해제했는가"라는 질문 자체가 없어진다. 풀에서 나온 것은 개수가 상한으로 묶여 있다. 슬랩에서 나온 것은 같은 크기 클래스로 돌아온다.

`game/allocators.hpp` 가 그 셋을 게임 쪽 표면으로 감싼다. 기존 경로(`operator new`)는 지우지 않고 A/B 의 한쪽 팔로 남는다 — 새 할당자가 낫다는 주장은 옛 할당자를 같은 워크로드에서 돌려 봐야만 성립한다.

## 2. 프레임 스크래치: Mark 하나, Rewind 하나

`FrameScratch` 는 두 팔을 한 표면 뒤에 둔다.

<!-- snippet: include/game/allocators.hpp@scratch-alloc -->
```cpp
template <class T>
T* Alloc(size_t count) {
    static_assert(alignof(T) <= alignof(std::max_align_t), "과정렬 타입은 담지 않는다");
    if (count == 0) return nullptr;
    if (kind_ == FrameAllocKind::Arena) {
        if (T* p = arena_.AllocArray<T>(count)) return p;
        // 아레나가 모자라면 죽지 않고 힙으로 떨어지되 그 사실을 센다. 조용히
        // 넘어가면 "프레임당 할당 0" 이라는 주장이 거짓인 채로 통과한다.
        ++overflows_;
    }
    void* raw = ::operator new(sizeof(T) * count);
    blocks_.push_back(raw);
    return static_cast<T*>(raw);
}
```

malloc 팔이 `std::malloc` 이 아니라 `::operator new` 를 직접 부르는 것이 의도다. 측정 도구가 전역 `operator new` 를 바꿔치기해서 세기 때문에, `malloc` 을 쓰면 두 팔의 차이가 카운터에 잡히지 않는다. **측정할 수 없는 차이는 없는 차이와 같다.**

스텝의 양 끝에 Mark/Rewind 가 붙는다.

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
    WinLoseSystem();
    scratch_.End();
}
```

`Begin` 은 아레나 팔에서 현재 오프셋을 저장하고, `End` 는 그 오프셋으로 되돌린다. malloc 팔에서는 `Begin` 이 아무것도 하지 않고 `End` 가 이번 프레임에 잡은 블록을 전부 반납한다.

`Reset()` 이 아니라 `Mark`/`Rewind` 를 쓰는 이유는 중첩 때문이다. 렌더 경로에도 스크래치가 하나 더 있고(`DefenseApp::frameScratch_`), 시뮬 스크래치와 별개다. 한쪽이 통째로 되감으면 다른 쪽의 유효 범위를 지운다.

아레나 용량은 상한에서 계산한다.

```cpp
scratch_.Init(switches_.frameAlloc, 64 * 1024 + enemyCap_ * 64);
```

적 상한이 정해져 있으므로 최대 소요가 계산 가능하다. 매치 시작에 한 번 잡고, 매치 중에는 다시 잡지 않는다 — 그것이 아레나의 요점이다.

## 3. 스크래치를 쓰는 네 자리

넣을 자리를 고르는 기준은 "개체 수를 따라 커지고, 프레임을 넘어 살지 않는 배열"이다. 지금 게임에 그런 자리가 넷 있다.

1. **개체별 코어 피해**(`sim.enemy_advance`) — `float[적 수]`. 워커가 공유 스칼라를 갱신하지 않게 하려고 도입한 배열이다(11편).
2. **조준 결과**(`sim.tower_fire`) — `int[타워 수]`. 순수 질의의 산출을 담아 두었다가 인덱스 순으로 적용한다.
3. **스플래시 후보**(`sim.projectile`) — `uint32_t[적 수]`. 폭발 반경에 든 행 번호를 모은다.
4. **사라진 발사체 좌표**(렌더) — `math::Vec2[이전 프레임 발사체 수]`. 파티클 방출 순서를 맵 순회에서 떼어내려고 도입했다(9편 §8).

재설계안이 이 자리로 지목했던 다섯 중 셋 — 플로우 필드의 열린/닫힌 집합, 브로드페이즈 후보 쌍, 델타 스냅샷 인코딩 버퍼 — 은 **이 게임에 아직 없다.** 플로우 필드도 브로드페이즈도 델타 스냅샷도 구현되지 않았으므로, 그 자리에 아레나를 배선하는 것은 호출되지 않는 코드를 만드는 일이다. 그래서 넷만 넣었다.

스플래시 후보를 모으는 자리가 대표적이다.

<!-- snippet: include/game/game.hpp@projectile-system -->
```cpp
void ProjectileSystem(float dt) {
    MEGA_PROFILE_SCOPE("sim.projectile");
    const size_t enemyCount = enemies_.Size();
    uint32_t* candidates = enemyCount ? scratch_.Alloc<uint32_t>(enemyCount) : nullptr;

    projs_.ForEach([&](Projectile& p) {
        int tracked = enemies_.FindRow(p.targetId);
        if (tracked >= 0 && !enemies_.Alive(static_cast<size_t>(tracked))) tracked = -1;
        if (tracked >= 0) p.target = enemies_.Pos(static_cast<size_t>(tracked));

        math::Vec2 d = p.target - p.pos;
        float dist = math::Length(d);
        float step = p.speed * dt;
        if (dist > step + 1e-3f) p.pos += d * (step / dist);
        else p.pos = p.target;

        const bool hitTracked =
            tracked >= 0 && physics::CollideCircleCircle(
                p.pos, 2.0f, enemies_.Pos(static_cast<size_t>(tracked)),
                EnemyCollisionRadius(enemies_.Type(static_cast<size_t>(tracked)))).hit;
        if (!hitTracked && dist > step + 1e-3f) return;

        // 여기서 죽지만 바로 다음 줄이 이 객체를 다시 읽는다. 풀 팔에서
        // 그것이 성립하는 이유는 반납이 스텝 경계로 미뤄지기 때문이다.
        projs_.Kill(p);
        const math::Vec2 impact = tracked >= 0
            ? enemies_.Pos(static_cast<size_t>(tracked)) : p.target;
        const float splash = p.splash;
        const float damage = p.damage;

        size_t hits = 0;
        for (size_t i = 0; i < enemyCount; ++i) {
            if (!enemies_.Alive(i)) continue;
            if (!physics::CollideCircleCircle(impact, splash, enemies_.Pos(i),
                                              EnemyCollisionRadius(enemies_.Type(i))).hit)
                continue;
            candidates[hits++] = static_cast<uint32_t>(i);
        }
        for (size_t k = 0; k < hits; ++k) {
            const size_t i = candidates[k];
            const float hp = enemies_.Hp(i) - damage;
            enemies_.SetHp(i, hp);
            if (hp <= 0.0f) {
                enemies_.Kill(i);
                gold_ += enemies_.Reward(i);
                // ...
            }
        }
    });
}
```

후보 배열을 발사체마다가 아니라 **시스템 진입에 한 번** 잡는다. 발사체마다 잡으면 프레임당 할당이 발사체 수만큼 늘어나 malloc 팔의 수치가 극적으로 나빠지겠지만, 그것은 좋은 코드를 나쁘게 만들어 A/B 를 이기는 것이다. 하지 않았다. 그래서 이 편의 측정치는 **프레임당 3회 → 0회**라는 수수한 수가 된다.

## 4. 넘치면 죽지 않고 센다

아레나에는 상한이 있다. 넘으면 `Alloc` 이 `nullptr` 을 돌려준다.

그 상태에서 두 가지 선택지가 있다. 죽거나, 힙으로 떨어지거나. 죽는 쪽은 정직하지만 게임을 멈춘다. 떨어지는 쪽은 게임을 살리지만 **"프레임당 할당 0"이라는 주장을 조용히 거짓으로 만든다.**

그래서 셋째를 골랐다 — 떨어지되 **센다**. `overflows_` 가 그 카운터이고, 테스트가 그것이 0인지 단언한다.

```cpp
CHECK_EQ(arena.overflows, static_cast<size_t>(0));
```

이 한 줄이 "0회 할당"을 실제 주장으로 만든다. 없으면 아레나가 매 프레임 넘쳐 힙으로 떨어지는 상태가 테스트를 통과한다.

## 5. 측정: 셀 수 있는 것을 센다

벽시계 시간은 머신 부하에 흔들린다. 대신 **정수로 셀 수 있는 것**을 센다 — 한 프레임이 전역 `operator new` 를 몇 번 부르는가.

테스트 번역 단위에서 전역 `operator new` 를 바꿔치기한다. 정적 링크된 `mega_core` 를 포함해 프로그램 전체의 할당이 이 카운터를 지난다.

```cpp
void* operator new(size_t size) {
    if (g_counting.load(std::memory_order_relaxed))
        g_newCalls.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(size ? size : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
```

측정 전에 300틱을 워밍업으로 돌린다. 컨테이너 용량이 자라는 동안의 할당은 프레임 비용이 아니라 시작 비용이기 때문이다.

```
[frame_alloc scale=small]
  malloc                 allocations=900 ticks=300 per_tick=3.000 arena_peak=0B overflow=0
  arena                  allocations=0 ticks=300 per_tick=0.000 arena_peak=528B overflow=0
[frame_alloc scale=large]
  malloc                 allocations=900 ticks=300 per_tick=3.000 arena_peak=0B overflow=0
  arena                  allocations=0 ticks=300 per_tick=0.000 arena_peak=16400B overflow=0
```

읽으면 이렇다.

- malloc 팔은 정확히 **틱당 3회**다. 시뮬 스크래치 세 자리(개체별 피해 · 조준 결과 · 스플래시 후보)와 정확히 일치한다.
- arena 팔은 **0회**다. 300틱 동안 전역 할당이 한 번도 없다.
- `arena_peak` 이 규모를 그대로 보여 준다. small 528바이트, large 16,400바이트 — 적이 32배가 되니 스크래치도 31배가 됐다.
- `overflow=0` — 아레나가 한 번도 모자라지 않았다. 그러므로 위의 0은 진짜 0이다.

정직하게 크기를 적으면, 이것은 틱당 세 번의 힙 왕복을 없앤 것이다. 극적인 수가 아니다. 그리고 그 세 번이 프레임 예산에서 차지하는 비중은 8편의 측정으로 이미 알고 있다 — 시뮬 전체가 프레임의 0.2%다.

그럼 왜 하는가. 두 가지 때문이다.

**하나.** 이 세 자리는 규모를 따라 커지는 자리다. large 에서 프레임마다 16KB 를 잡았다 버리는 것은 할당 횟수보다 **단편화**의 문제다. 아레나는 같은 16KB 를 매 프레임 재사용한다.

**둘.** 이 편의 진짜 산출은 숫자가 아니라 **셀 수 있게 된 것** 자체다. 다음에 누가 프레임 경로에 `std::vector` 를 하나 더 넣으면 `per_tick` 이 4가 되고, 그 변화가 테스트 출력에 남는다.

## 6. 발사체 풀: 속도가 아니라 주소 안정성

발사체는 크기가 같고 회전이 빠르다. 고정 블록 풀의 교과서적 대상이다.

그런데 풀을 넣는 진짜 이유는 성능이 아니다. **주소 안정성**이다.

`std::vector<Projectile>` 은 `push_back` 한 번에 전체를 옮길 수 있다. 옮기면 그 벡터를 가리키던 모든 포인터가 매달린다. 지금은 스텝 중에 `push_back` 이 없어서 안전하지만, 그 안전은 코드 배치의 **우연**이다.

풀에서는 우연이 아니라 구조다. 블록은 절대 움직이지 않는다.

<!-- snippet: include/memory/pool.h@pool-freelist -->
```cpp
void BuildFreeList() {
    free_ = nullptr;
    if (!base_) return;
    // 역순으로 이어 0번 블록이 head 가 되도록(지역성).
    for (size_t i = blockCount_; i-- > 0;) {
        void* block = base_ + i * blockSize_;
        *reinterpret_cast<void**>(block) = free_;
        free_ = block;
    }
}
```

빈 블록의 내부 공간을 `next` 포인터로 재활용하므로 메타데이터 오버헤드가 0이다. 이 성질에는 대가가 있다 — **반납한 블록의 첫 8바이트가 즉시 덮인다.** 그 사실이 §7 의 설계를 결정한다.

풀 팔에서는 상한을 넘으면 `nullptr` 을 돌려주고 조용히 늘어나지 않는다. 늘어나면 주소가 움직이기 때문이다. 벡터 팔도 같은 상한에서 거절하게 만들었다 — 그러지 않으면 두 팔이 다른 개수의 발사체를 만들어 상태 해시가 갈린다.

## 7. 죽었지만 아직 읽는다 — deferred-free 목록

`ProjectileSystem` 은 명중한 발사체를 죽인 **직후** 그 객체를 다시 읽는다. `impact` 좌표·`splash` 반경·`damage` 셋이 그 뒤에 필요하다.

풀에서 죽이자마자 반납하면 그 세 줄이 이미 덮인 메모리를 읽는다. `pos` 는 블록의 첫 8바이트이므로 freelist 의 `next` 포인터가 그 자리에 들어간다 — 폭발 좌표가 포인터 비트로 해석된다.

답은 **죽이는 것과 반납하는 것을 분리**하는 것이다. 그리고 그러려면 같은 객체가 두 목록에 순차적으로 들어가야 한다: live 에서 빠지고 deferred-free 로 간다. 훅이 객체 안에 있어야 할당 없이 그 이동이 된다.

```cpp
struct Node {
    Projectile p;
    ListHook   liveHook;
    ListHook   deadHook;
};
```

<!-- snippet: include/game/entity_store.hpp@deferred-kill -->
```cpp
void Kill(Projectile& p) {
    p.alive = 0;
    if (kind_ == BodyAllocKind::Vector) return;
    Node* node = NodeOf(p);
    live_.Remove(node);
    dead_.PushBack(node);
    --liveCount_;
}

// 스텝 경계. 여기서만 메모리가 실제로 돌아간다.
void EndStep() {
    if (kind_ == BodyAllocKind::Vector) {
        vec_.erase(std::remove_if(vec_.begin(), vec_.end(),
                                  [](const Projectile& p) { return !p.alive; }),
                   vec_.end());
        return;
    }
    while (Node* node = dead_.Front()) {
        dead_.Remove(node);
        pool_.Release(node);
    }
}
```

`IntrusiveList::Remove` 가 할당 없이 O(1) 인 것이 이 구조의 전제다.

<!-- snippet: include/containers/intrusive_list.h#Remove -->
```cpp
void Remove(T* obj) {
    ListHook* h = Hk(obj);
    if (!h->Linked()) return;
    h->prev->next = h->next;
    h->next->prev = h->prev;
    h->prev = h->next = nullptr;
    --size_;
}
```

순회 순서가 두 팔에서 같아야 한다는 요구가 여기서도 걸린다. 벡터 팔의 `std::remove_if` 는 순서를 보존하고, intrusive 목록의 `PushBack`/`Remove` 도 보존한다. 그래서 두 팔의 발사체 순회 순서가 같고, 발사체 id 부여 순서가 같고, 스냅샷 바이트가 같다.

한 가지 함정이 있었다. `ForEach` 중에 콜백이 노드를 죽이면 그 노드가 목록에서 빠진다 — 다음 노드를 미리 잡아 두지 않으면 순회가 끊긴 링크를 따라간다.

```cpp
for (Node* n = live_.First(); n; ) {
    Node* next = live_.Next(n);   // fn 이 n 을 죽여 목록에서 뺄 수 있다
    if (n->p.alive) fn(n->p);
    n = next;
}
```

**벡터 팔은 이 성질이 없다.** 그래서 벡터 팔에서 스텝 중에 포인터를 들면 안 된다는 규칙이 남는다. 지금 코드는 그 규칙을 지키지만(`Kill` 뒤에 읽는 것은 참조 하나뿐이고 그 사이에 push 가 없다), 강제하는 장치는 없다.

## 8. 매치 수명 가변 버퍼와 크기 클래스

세 번째 수명은 "매치 동안 살고, 크기가 맵에 따라 달라지는" 버퍼다. 지금 게임에 그런 것이 둘 있다 — `DefenseMap::tiles_`(w×h 바이트)와 경로 배열(경로 길이×8바이트), 그리고 `GameWorld::path_`.

맵이 16×15 로 고정이던 시절에는 한 번 잡고 끝이었다. 지금은 다르다. **매치를 다시 시작할 때마다** 맵이 값으로 복사되고 경로가 다시 만들어진다('R' 키의 `Restart`, 그리고 반복 매치 소크 테스트).

그 자리를 `BlobArray<T>` 로 바꿨다. 값 의미론이라 `DefenseMap` 이 그대로 값으로 오간다.

<!-- snippet: include/game/allocators.hpp@blob-assign -->
```cpp
void Assign(size_t count, const T& fill) {
    Release();
    if (count == 0) return;
    from_ = BlobStore::Instance().Kind();
    data_ = static_cast<T*>(BlobStore::Instance().Alloc(sizeof(T) * count));
    if (!data_) return;
    size_ = count;
    for (size_t i = 0; i < size_; ++i) data_[i] = fill;
}
```

할당자 인스턴스가 프로세스에 하나인 것이 설계 결정이다. 이 버퍼를 들고 있는 `DefenseMap` 은 값으로 복사되고 사본의 수명이 원본과 다르다. 할당자를 맵 안에 넣으면 사본마다 다른 슬랩이 생겨 **재사용이 사라진다** — 슬랩을 쓰는 이유가 없어진다.

판정은 "빨라졌는가"가 아니다. **페이지 수가 수렴하는가**다.

```
[blob_alloc] rounds=100 pages_after_warmup=8 pages_final=8 live=0
```

맵 크기를 바꿔 가며 100회 재생성했고, 35회차 시점의 페이지 수(8)가 100회차에도 8이다. 크기 클래스 재사용이 실제로 돈다는 뜻이다. `Free` 의 클래스 계산이 어긋나면 해제분이 엉뚱한 freelist 로 가고, 그러면 요청마다 새 페이지가 생겨 이 수가 단조 증가한다 — 겉으로는 그냥 메모리가 새는 것으로 보인다.

`live=0` 은 100회가 끝난 뒤 살아 있는 블록이 없다는 뜻이다. 새지 않는다.

## 9. 스위치가 바뀌는 순간의 함정

`BlobArray` 를 처음 썼을 때 힙이 깨졌다. 원인은 이랬다.

`GameWorld::Init` 이 `BlobStore::Instance().Configure(switches_.blobAlloc)` 를 부른다. 그 시점에 이미 살아 있는 `BlobArray` 가 있으면(예: 이전 매치의 맵), 그 배열은 **옛 팔에서 잡힌 것**이다. 그런데 소멸자는 지금 모드로 반납한다 — malloc 포인터가 슬랩의 freelist 로 들어가고, 다음 `Alloc` 이 그것을 내주면서 힙이 무너진다.

답은 각 배열이 **자기가 어디서 왔는지 기억하는** 것이다.

<!-- snippet: include/game/allocators.hpp@blob-free -->
```cpp
void Free(void* p, BlobAllocKind from) {
    if (!p) return;
    --live_;
    if (from == BlobAllocKind::Slab) slab_.Free(p);
    else ::operator delete(p);
}
```

`BlobArray` 가 `from_` 을 들고 있고 반납할 때 그것을 돌려준다. 사본은 **지금 팔**에서 새로 잡으므로 사본의 `from_` 은 사본이 만들어진 시점의 모드다.

이 함정은 A/B 스위치 일반의 함정이다. 스위치가 프로세스 수명 동안 한 번만 바뀐다고 가정하면 이런 코드가 통과한다. 실제로는 테스트 하나가 두 팔을 연달아 돌리고, 그 사이에 이전 팔의 객체가 살아 있다. 그래서 이 상황을 재현하는 테스트를 따로 뒀다.

```cpp
store.Configure(BlobAllocKind::Malloc);
BlobArray<uint32_t> fromMalloc(64, 7u);
store.Configure(BlobAllocKind::Slab);
BlobArray<uint32_t> fromSlab(64, 9u);
// ...
store.Configure(BlobAllocKind::Malloc);     // 소멸은 이 모드에서 일어난다
// 여기서 세 배열이 각자 자기 팔로 돌아간다. 잘못되면 이 스코프를 나가면서 죽는다.
```

## 10. 변이: 되감기와 미룸

두 가지를 실제로 주입했다.

**변이 1 — 아레나의 되감기를 지운다.**

```cpp
void End() {
    if (false) arena_.Rewind(mark_);   // [MUTATION] 되감기 제거
```

```
[frame_alloc scale=small]
  malloc                 allocations=900 ticks=300 per_tick=3.000 arena_peak=0B overflow=0
  arena                  allocations=900 ticks=300 per_tick=3.000 arena_peak=69632B overflow=1393
  FAIL ...\tests\test_frame_alloc_budget.cpp:108  CHECK(heap.allocations > arena.allocations)
  FAIL ...\tests\test_frame_alloc_budget.cpp:109  CHECK(heap.allocations - arena.allocations >= static_cast<uint64_t>(ticks) * 2)
  FAIL ...\tests\test_frame_alloc_budget.cpp:111  CHECK_EQ(arena.overflows, static_cast<size_t>(0))
```

세 줄이 함께 죽는 것이 이 변이의 특징이다. 되감기가 없으면 아레나가 몇 프레임 만에 고갈되고, 고갈되면 전부 힙으로 떨어져 **두 팔의 할당 수가 같아진다**(900 vs 900). `overflow=1393` 이 그 사실을 직접 말한다.

**변이 2 — 반납을 스텝 경계까지 미루지 않는다.**

```cpp
Node* node = NodeOf(p);
live_.Remove(node);
pool_.Release(node);      // [MUTATION] 스텝 경계까지 미루지 않고 즉시 반납
--liveCount_;
```

```
[test] deferred_free
  FAIL ...\tests\test_deferred_free.cpp:46  CHECK_EQ(store.DeferredCount(), static_cast<size_t>(4))
  FAIL ...\tests\test_deferred_free.cpp:47  CHECK_EQ(store.PoolLive(), static_cast<size_t>(8))
  FAIL ...\tests\test_deferred_free.cpp:55  CHECK(spawned[i]->pos.x == 10.0f * static_cast<float>(i))
  FAIL ...\tests\test_deferred_free.cpp:55  CHECK(spawned[i]->pos.x == 10.0f * static_cast<float>(i))
  FAIL ...\tests\test_deferred_free.cpp:55  CHECK(spawned[i]->pos.x == 10.0f * static_cast<float>(i))
  FAIL ...\tests\test_deferred_free.cpp:55  CHECK(spawned[i]->pos.x == 10.0f * static_cast<float>(i))
```

`pos.x` 가 깨진 것이 핵심이다. 처음 이 테스트를 썼을 때는 `id` 와 `damage` 만 봤고, 그 둘은 블록의 앞쪽 8바이트 **바깥**에 있어서 살아남았다 — 변이가 두 줄만 죽였다. `pos` 를 추가해서야 "실제 게임 코드가 죽은 뒤에 읽는 그 필드"가 검사에 들어왔다.

테스트가 무엇을 보는지가 변이가 무엇을 증명하는지를 정한다.

둘 다 복원 후 재통과했다(§11).

## 11. 실행과 관측

### 프레임 할당 예산

```powershell
cd C:/Users/REIN/Documents/VeLog/projects/mega
cmake --build build --config Release
.\build\bin\Release\test_frame_alloc_budget.exe
```

```
[test] frame_alloc_budget
[frame_alloc scale=small]
  malloc                 allocations=900 ticks=300 per_tick=3.000 arena_peak=0B overflow=0
  arena                  allocations=0 ticks=300 per_tick=0.000 arena_peak=528B overflow=0
[frame_alloc scale=large]
  malloc                 allocations=900 ticks=300 per_tick=3.000 arena_peak=0B overflow=0
  arena                  allocations=0 ticks=300 per_tick=0.000 arena_peak=16400B overflow=0
[test] frame_alloc_budget: 12 checks, 0 failures
```

### 발사체 풀과 미룬 반납

```powershell
.\build\bin\Release\test_deferred_free.exe
.\build\bin\Release\test_body_pool.exe
```

```
[test] deferred_free
[body_alloc]
  vector ticks=3000 total=2.0ms projectile_peak=2
  pool   ticks=3000 total=1.0ms projectile_peak=2
[test] deferred_free: 346 checks, 0 failures
[test] body_pool
[test] body_pool: 20102 checks, 0 failures
```

3000틱에 2.0ms 대 1.0ms 다. 절반이지만 둘 다 작다 — 이 규모(동시 발사체 몇 개)에서 발사체 할당은 애초에 병목이 아니다. 풀이 들어간 이유는 §6 에 적은 대로 속도가 아니라 주소 안정성이다.

`test_body_pool` 의 20,102개 검사 중 20,000개는 1만 회 Acquire/Release 를 섞으면서 매번 `Live()` 가 실제 개수와 맞는지를 본 것이다. freelist 의 `next` 연결이 한 칸이라도 어긋나면 같은 블록이 두 번 나가고, 살아 있는 주소의 집합 크기가 개수와 달라진다.

### 크기 클래스 재사용

```powershell
.\build\bin\Release\test_blob_slab.exe
```

```
[test] blob_slab
[blob_alloc] rounds=100 pages_after_warmup=8 pages_final=8 live=0
[test] blob_slab: 221 checks, 0 failures
```

### 세 할당자를 다 켠 팔

```powershell
.\build\bin\Release\mega_defense.exe --ab frame_alloc=arena --ab body_alloc=pool --ab blob_alloc=slab --selftest
```

```
[switches] entity_store=aos id_map=linear frame_alloc=arena blob_alloc=slab body_alloc=pool worker_lock=std asset_io=sync scale=small jobs=0 affinity=0 workers=0 lock=std
[selftest] result=CLEAR ticks=1377 towers=11 wave=6 packet=175 protocol=OK render=OK assets=10/10 content=external
```

`ticks=1377 towers=11 wave=6 packet=175` — 기본 팔과 같다.

### 변이 복원 후

```powershell
.\build\bin\Release\test_frame_alloc_budget.exe
.\build\bin\Release\test_deferred_free.exe
```

```
[test] frame_alloc_budget: 12 checks, 0 failures
[test] deferred_free: 346 checks, 0 failures
```

---

## 결론

할당이 세 수명으로 갈렸다.

**프레임.** 시뮬 한 스텝의 힙 할당이 3회에서 0회가 됐고, 그것이 전역 카운터로 단언된다. 아레나가 넘치지 않았다는 것도 함께 단언되므로 그 0은 진짜 0이다.

**고정 크기·빠른 회전.** 발사체가 주소가 움직이지 않는 블록에 산다. 그 성질 위에서 "스텝 중에 죽었지만 스텝 경계까지 읽을 수 있다"가 구조적으로 보장된다 — intrusive 목록 두 개가 그 보장의 기계다.

**매치·가변 크기.** 맵 버퍼가 크기 클래스 freelist 를 지나고, 100회 재생성 뒤에도 페이지가 8장에서 늘지 않는다.

측정에서 배운 것을 정직하게 적으면, **세 할당자의 시간 이득은 이 규모에서 작다.** 프레임당 세 번의 힙 왕복과 3000틱에 1ms 다. 값어치는 다른 데 있다 — 이제 프레임 할당 횟수가 **정수로 관측되고 회귀가 테스트에 남는다.**

현재 상태의 한계.

**아레나 상한은 손으로 계산한 수다.** `64KB + 적 상한 × 64B` 인데, 그 64바이트는 스크래치 세 자리의 개체당 소요(4+4+4 = 12바이트)에 여유를 곱한 것이다. 자리가 늘면 이 수를 손으로 다시 정해야 하고, 틀리면 `overflow` 카운터가 알려 준다 — 즉 **테스트를 돌린 뒤에야** 알 수 있다.

**벡터 팔의 주소 규칙은 강제되지 않는다.** 풀 팔은 스텝 중 포인터 유지가 구조적으로 안전하지만 벡터 팔은 아니다. 두 팔이 같은 코드를 도는 이상, 벡터 팔에서만 위험한 패턴을 막는 장치가 없다.

**슬랩이 담당하는 자리가 둘뿐이다.** 맵 타일과 경로. 재설계안이 지목한 나머지(플로우 필드 타일, BVH 노드, 행동 트리 노드, 델타 스냅샷 버퍼)는 아직 이 게임에 존재하지 않는다. 그래서 크기 클래스 분포가 실제 워크로드가 아니라 두 종류의 버퍼만으로 이뤄져 있고, 8장이라는 페이지 수도 그 좁은 분포의 수다.

**세 스위치를 켜도 프레임은 빨라지지 않는다.** 8편의 측정대로 시뮬은 프레임의 0.2%이고, 그 0.2%의 일부를 줄인 것이기 때문이다.
