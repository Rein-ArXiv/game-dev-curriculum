## 들어가며

이 게임은 한 스레드로 돈다. 10편까지 그것을 바꾸지 않았다.

바꿀 때가 됐는지는 측정이 말한다. 9편에서 적 2048마리를 세우고 재 보니 시뮬 한 스텝의 두 시스템이 나머지를 합친 것보다 비싸다 — 적 전진(`sim.enemy_advance`)과 조준 질의(`sim.tower_fire`). 둘 다 개체마다 독립적인 계산이라 나눌 수 있는 모양이다.

그런데 병렬 시뮬의 어려운 부분은 속도가 아니다. **재현성**이다.

워커가 공유 스칼라를 갱신하거나, 결과를 완료 순서대로 이어 붙이면, 같은 입력에 워커 수마다 다른 세계가 나온다. 그러면 골든 해시도, 리플레이도, 협동 권위도 성립하지 않는다 — 호스트가 워커 4개로 돌고 검증기가 8개로 돌면 두 시뮬이 갈린다.

그래서 이 편은 규칙을 먼저 정하고 그다음에 나눈다.

1. **청크는 인덱스로만 나누고, 청크는 자기 구간 바깥을 쓰지 않는다.**
2. **합치는 것은 인덱스 오름차순으로 한다** — 완료 순서가 아니라.

그다음 스케줄러 내부로 내려간다. `jobs/scheduler.h` 는 지금 `std::mutex` 와 `std::condition_variable` 을 쓴다. 그것을 `sync/lock.h` 의 futex 기반 `Mutex`·`CondVar` 로 갈아탈 수 있게 하고(`worker_lock=std|futex`), 워커를 코어에 고정한다(`affinity=0|1`).

도달점은 이렇다. 워커 0·1·2·4·8 다섯 구성이 **비트 동일한 상태 해시**를 내고, 그 다섯이 정말 병렬 경로를 돌았다는 것이 테스트에 단언되어 있으며, 두 잠금 구현이 같은 작업 집합을 정확히 한 번씩 실행한다.

그리고 이 편의 측정 결과는 예상과 반대다. 미리 적어 둔다 — **이 워크로드에서 병렬화는 손해다.**

---

## 목차
1. [나눌 수 있는 것과 나눌 수 없는 것](#1-나눌-수-있는-것과-나눌-수-없는-것)
2. [규칙 1: 청크는 자기 구간만 쓴다](#2-규칙-1-청크는-자기-구간만-쓴다)
3. [규칙 2: 합치기는 인덱스 오름차순](#3-규칙-2-합치기는-인덱스-오름차순)
4. [조준은 질의와 적용으로 쪼갠다](#4-조준은-질의와-적용으로-쪼갠다)
5. [규모가 작으면 직렬로 떨어진다](#5-규모가-작으면-직렬로-떨어진다)
6. [잠금 두 벌을 한 표면 뒤에](#6-잠금-두-벌을-한-표면-뒤에)
7. [시한 없는 대기는 워커를 영영 재운다](#7-시한-없는-대기는-워커를-영영-재운다)
8. [워커를 코어에 고정한다](#8-워커를-코어에-고정한다)
9. [측정: 병렬화가 손해인 이유](#9-측정-병렬화가-손해인-이유)
10. [변이: 테스트가 병렬 경로를 정말 돌고 있었는가](#10-변이-테스트가-병렬-경로를-정말-돌고-있었는가)
11. [실행과 관측](#11-실행과-관측)

---

## 1. 나눌 수 있는 것과 나눌 수 없는 것

시뮬 여섯 시스템을 하나씩 본다.

| 시스템 | 나눌 수 있는가 | 왜 |
|---|---|---|
| `sim.spawn` | 아니오 | `nextEntityId_` 를 순서대로 소비한다 |
| `sim.enemy_advance` | **예** | 개체가 서로를 읽지 않는다 |
| `sim.tower_fire` | **부분** | 질의는 순수, 사격은 순서 의존 |
| `sim.projectile` | 아니오 | 여러 발사체가 같은 적의 체력을 깎는다 |
| `sim.cleanup` | 아니오 | 압축은 순차 쓰기다 |
| `sim.winlose` | 아니오 | 비교 두 번이다 |

나누는 것은 둘뿐이다. 나머지는 직렬로 남으므로 암달의 법칙이 그대로 걸린다 — 그 사실이 §9 의 측정에 그대로 나온다.

`sim.projectile` 을 나누지 않은 이유가 미묘하다. 발사체 두 개가 같은 프레임에 같은 적을 맞히면 체력 감산이 두 번 일어나는데, 그 순서가 결과를 바꾼다(체력이 0을 지나는 시점이 보상 지급을 정한다). 나누려면 감산을 개체별로 모았다가 순서대로 적용해야 하고, 그것은 이 편이 다루는 것보다 큰 재구성이다.

## 2. 규칙 1: 청크는 자기 구간만 쓴다

`ParallelFor` 는 `[0, count)` 를 grain 크기로 쪼개 워커에 뿌린다. 워커가 어느 청크를 언제 집을지는 모른다. 그러므로 청크는 **자기 인덱스 구간 안의 자리에만 쓸 수 있다.**

이 규칙을 지키기 위해 `EnemyAdvanceSystem` 이 바뀌었다. 원래 코드는 적이 코어에 닿으면 그 자리에서 `coreHp_ -= e.damage` 를 했다. 그것은 공유 스칼라 갱신이다.

<!-- snippet: include/game/game.hpp@enemy-advance -->
```cpp
void EnemyAdvanceSystem(float dt) {
    MEGA_PROFILE_SCOPE("sim.enemy_advance");
    const size_t count = enemies_.Size();
    if (count == 0) return;
    // ...

    // 개체별 코어 피해를 따로 받아 둔다. 워커가 coreHp_ 를 직접 깎으면 감산
    // 순서가 실행마다 달라져 부동소수 결과가 갈린다.
    float* damage = scratch_.Alloc<float>(count);
    for (size_t i = 0; i < count; ++i) damage[i] = 0.0f;
    // ...
    // 병합은 인덱스 오름차순 — 직렬 실행의 감산 순서와 정확히 같다.
    for (size_t i = 0; i < count; ++i)
        if (damage[i] != 0.0f) coreHp_ -= damage[i];
}
```

개체별 피해를 배열에 받아 두고, 병합은 프레임 스레드가 인덱스 순으로 한다. 그 배열이 10편의 프레임 스크래치를 쓰는 네 자리 중 하나다 — 이 배열은 병렬화 때문에 생겼고, 병렬화가 없었다면 필요 없었을 것이다.

전진 산술 자체는 9편에서 이미 자유 함수로 빠져 있었다(`AdvanceEnemyOnPath`). 그 함수가 `damageOut` 참조를 받는 형태였던 것이 여기서 값을 한다 — 병렬화를 위해 시그니처를 바꿀 필요가 없었다.

## 3. 규칙 2: 합치기는 인덱스 오름차순

부동소수 덧셈은 결합법칙이 성립하지 않는다. `(a+b)+c` 와 `a+(b+c)` 가 다를 수 있다. 그래서 청크별 부분합을 완료 순서대로 더하면 워커 수마다 다른 값이 나온다.

인덱스 오름차순 병합은 **직렬 실행과 정확히 같은 연산 순서**다. 그러므로 비트 동일이 보장된다.

정직하게 덧붙일 것이 있다. **이 게임의 현재 수치로는 순서가 결과를 바꾸지 않는다.** 코어 피해는 적 종류별로 10·20·50 같은 정수이고, 코어 체력도 정수 범위에 있어 float 반올림이 일어나지 않는다. 그래서 이 규칙을 어긴 변이(병합을 역순으로)를 넣어도 해시가 갈리지 않았다.

그래도 규칙으로 둔다. 밸런스 한 줄이 `damage` 를 소수로 만드는 순간, 이 규칙이 없으면 워커 수마다 다른 세계가 된다. 그리고 그때 나타나는 증상은 "가끔 협동 클라이언트의 코어 체력이 호스트와 1 다르다"이고, 그 원인을 병합 순서까지 거슬러 올라가는 데는 며칠이 든다.

변이로 증명할 수 있는 것은 인접한 규칙 — **병렬 경로가 정말 전 구간을 덮는가** — 였고, 그것을 §10 에서 증명한다.

## 4. 조준은 질의와 적용으로 쪼갠다

`TowerFireSystem` 은 반쪽만 나눌 수 있다.

조준 질의는 순수하다. 타워 위치·사거리·모드와 적 상태를 읽어 대상 인덱스 하나를 낸다. 아무것도 바꾸지 않는다.

사격은 순수하지 않다. 발사체를 만들고(`nextEntityId_` 소비), 타이머를 재장전한다. 그리고 발사체 id 부여 순서는 상태 해시에 들어간다.

<!-- snippet: include/game/game.hpp@tower-fire -->
```cpp
void TowerFireSystem(float dt) {
    MEGA_PROFILE_SCOPE("sim.tower_fire");
    // ...
    const size_t towerCount = towers_.size();
    if (towerCount == 0) return;
    // ...
    // 조준 질의는 순수하다 — 적을 읽기만 하고 아무것도 바꾸지 않는다. 그래서
    // 워커에 나눌 수 있다. 바꾸는 쪽(발사체 생성·타이머)은 타워 인덱스
    // 오름차순으로 직렬 적용해 발사체 id 부여 순서를 고정한다.
    int* targets = scratch_.Alloc<int>(towerCount);
    const Tower* towers = towers_.data();
    const EnemyStore* enemies = &enemies_;
    // ...
    const math::Vec2 core = corePos_;
    // ...
    RunChunks(towerCount, [=](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            const Tower& t = towers[i];
            // ...
            if (t.timer - dt > 0.0f) { targets[i] = -1; continue; }
            // ...
        }
    });

    for (size_t i = 0; i < towerCount; ++i) {
        Tower& t = towers_[i];
        // ...
        t.timer -= dt;
        if (t.timer > 0.0f) continue;
        const int best = targets[i];
        if (best < 0) continue;
        Projectile projectile;
        projectile.pos = t.pos;
        projectile.target = enemies_.Pos(static_cast<size_t>(best));
        projectile.speed = 200.0f;
        projectile.damage = t.damage;
        projectile.splash = t.splash;
        // ...
        projectile.id = nextEntityId_++;
        projectile.targetId = enemies_.Id(static_cast<size_t>(best));
        if (!projs_.Push(projectile)) {
            --nextEntityId_;    // 상한에 막혔다 — id 를 태우지 않는다
            continue;
        }
        // ...
        t.timer = t.fireInterval;
    }
}
```

질의 단계가 재장전 조건(`t.timer - dt > 0.0f`)을 미리 계산해 `-1` 을 넣는 것이 미묘한 부분이다. 그러지 않으면 쏘지 않을 타워까지 전수 스캔을 돌아 낭비가 된다. 그리고 적 상태는 이 시스템 안에서 바뀌지 않으므로, 질의를 앞당겨도 결과가 같다.

`--nextEntityId_` 한 줄도 의도다. 발사체 상한에 막혀 생성이 실패하면 소비한 id 를 되돌린다. 되돌리지 않으면 상한에 닿았는지 여부가 이후 모든 엔티티의 id 를 밀어 두 팔의 해시가 갈린다.

## 5. 규모가 작으면 직렬로 떨어진다

나누는 판단은 한 곳에 있다.

<!-- snippet: include/game/game.hpp@run-chunks -->
```cpp
template <class Fn>
void RunChunks(size_t count, Fn&& fn) {
    if (count == 0) return;
    if (!scheduler_ || scheduler_->WorkerCount() == 0 || count < kParallelGrain * 2) {
        fn(0, count);
        return;
    }
    scheduler_->ParallelFor(count, kParallelGrain, fn);
}
```

`kParallelGrain` 은 256이다. 개체가 512 미만이면 나누지 않고 그 자리에서 부른다 — 워커가 있어도 그렇다.

기준이 두 겹인 것에 주의할 만하다. 워커가 없으면(`jobs=0`) 당연히 직렬이고, 워커가 있어도 규모가 작으면 직렬이다. 후자가 없으면 콘텐츠 규모(적 26마리)에서 청크 하나짜리 병렬 호출이 매 틱 두 번씩 일어나고, 그 오버헤드가 순이익을 음수로 만든다.

**이 분기가 테스트를 조용히 무력화할 수 있다.** 실제로 그랬다 — §10 에서 다룬다.

## 6. 잠금 두 벌을 한 표면 뒤에

스케줄러 내부에는 잠금이 두 종류 있다. 워커 큐마다 하나, 그리고 잠들기·깨우기용 조건 변수 하나.

지금은 `std::mutex` 와 `std::condition_variable` 이다. `sync/lock.h` 에는 같은 것을 futex 위에 직접 쌓은 구현이 있고, 무경합 경로가 원자 연산만으로 끝난다(커널 진입 0회).

둘 중 어느 쪽이 나은지는 워크로드가 정한다. 그래서 하나를 지우지 않고 런타임에 고른다.

<!-- snippet: include/jobs/scheduler.h#HybridMutex -->
```cpp
class HybridMutex {
public:
    void SetKind(WorkerLock kind) { kind_ = kind; }
    WorkerLock Kind() const { return kind_; }

    void Lock() {
        if (kind_ == WorkerLock::Futex) futex_.Lock();
        else standard_.lock();
    }
    void Unlock() {
        if (kind_ == WorkerLock::Futex) futex_.Unlock();
        else standard_.unlock();
    }

private:
    friend class HybridCondVar;
    std::mutex   standard_;
    sync::Mutex  futex_;
    WorkerLock   kind_ = WorkerLock::Std;
};
```

두 뮤텍스를 다 들고 다니는 것이 낭비로 보인다. `std::mutex` 는 Windows 에서 80바이트, `sync::Mutex` 는 4바이트다. 큐 하나당 84바이트를 쓰고 그중 하나만 쓴다.

대안 둘을 검토하고 기각했다.

**템플릿으로 두 벌 인스턴스화.** `SchedulerT<StdPolicy>` 와 `SchedulerT<FutexPolicy>` 를 만들고 파사드가 둘 중 하나를 들게 하는 방식. 메모리는 아끼지만 `Scheduler` 의 공개 표면이 가상 함수나 `unique_ptr` 뒤로 들어가고, `ParallelFor` 호출마다 간접 분기가 하나 늘어난다. 잠금보다 자주 불리는 자리에 간접을 넣는 것은 방향이 반대다.

**컴파일 시간 선택.** `#ifdef` 로 하나만 남기면 두 구현을 같은 빌드에서 비교할 수 없다. A/B 스위치의 목적이 사라진다.

분기 자체의 비용은 잠금 비용보다 두 자릿수 작다. 예측 가능한 멤버 비교 하나이고, 같은 스케줄러 안에서 값이 바뀌지 않으므로 분기 예측기가 100% 맞춘다. 84바이트 × 큐 9개 = 756바이트를 그 대가로 낸다.

## 7. 시한 없는 대기는 워커를 영영 재운다

`sync::CondVar` 에는 시한 대기가 없었다. `Wait(Mutex&)` 하나뿐이다.

워커 루프에 그것을 그대로 넣으면 잠재적으로 매달린다. 루프의 모양이 이렇기 때문이다.

```
큐가 비었는가?  →  비었다  →  잠근다  →  잔다
                      ↑
                 여기서 일이 들어오면?
```

일을 넣는 쪽은 `NotifyAll` 을 부르지만, 그것이 "큐가 비었다"고 판정한 순간과 "잠들었다" 사이에 오면 아무도 다시 깨워 주지 않는다. `std::condition_variable` 버전은 `wait_for(1ms)` 로 그 창을 닫고 있었다.

`sync::CondVar` 의 시퀀스 카운터가 이 경쟁을 **대부분** 닫는다 — `Wait` 이 seq 를 먼저 읽고 그 값으로 `FutexWait` 하므로, 그 사이에 통지가 오면 값이 달라져 즉시 반환한다. 하지만 술어를 보는 자리(작업 큐)가 뮤텍스 밖이라 완전히 닫히지는 않는다.

그래서 시한 대기를 `sync/futex.h` 와 `sync/lock.h` 에 추가했다.

<!-- snippet: include/sync/futex.h#FutexWaitFor -->
```cpp
inline void FutexWaitFor(std::atomic<uint32_t>* addr, uint32_t expected, uint32_t milliseconds) {
#if defined(_WIN32)
    uint32_t cmp = expected;
    WaitOnAddress(reinterpret_cast<volatile void*>(addr), &cmp, sizeof(cmp),
                  static_cast<unsigned long>(milliseconds));
#elif defined(__linux__)
    struct timespec ts;
    ts.tv_sec = static_cast<long>(milliseconds / 1000u);
    ts.tv_nsec = static_cast<long>((milliseconds % 1000u) * 1000000u);
    syscall(SYS_futex, reinterpret_cast<uint32_t*>(addr), FUTEX_WAIT_PRIVATE, expected, &ts, nullptr, 0);
#else
    (void)addr; (void)expected; (void)milliseconds;
#endif
}
```

<!-- snippet: include/sync/lock.h#WaitFor -->
```cpp
void WaitFor(Mutex& m, uint32_t milliseconds) {
    uint32_t s = seq_.load(std::memory_order_acquire);
    m.Unlock();
    FutexWaitFor(&seq_, s, milliseconds);
    m.Lock();
}
```

두 플랫폼의 시한 의미가 다르다는 것이 여기서 드러난다. Windows 의 `WaitOnAddress` 는 밀리초를 받고, Linux 의 `SYS_futex` 는 `timespec` 을 받는다. 그리고 Linux 의 `FUTEX_WAIT` 는 **상대 시간**으로 해석한다(`FUTEX_WAIT_BITSET` 이라야 절대 시간). 여기서는 상대 시간이 맞다.

워커 루프는 이제 두 팔 모두 1ms 시한으로 잔다.

<!-- snippet: include/jobs/scheduler.h@worker-loop -->
```cpp
void WorkerLoop(int idx) {
    // 코어 고정은 워커 스레드의 수명 전체에 걸린다. 소멸자가 이전 마스크를
    // 되돌리므로, 실패(권한 없음)해도 이 스레드는 그냥 OS 배치로 돈다.
    sched::ScopedAffinity pin(pinToCores_
        ? static_cast<int>(static_cast<unsigned>(idx) % sched::HardwareThreads())
        : -1);
    if (pin.Ok()) {
        pinnedWorkers_.fetch_add(1, std::memory_order_relaxed);
        sched::SetThreadPriorityHint(sched::Priority::High);
    }
    if (idx >= 0 && static_cast<size_t>(idx) < workerCores_.size())
        workerCores_[static_cast<size_t>(idx)] = sched::CurrentCore();

    Task t;
    while (true) {
        if (PopOwn(idx, t) || StealAny(idx, t)) { t(); continue; }
        // 할 일 없음 — 깨어날 때까지 대기(주기적 재확인).
        detail::HybridGuard guard(cvMutex_);
        if (stop_) return;
        cv_.WaitFor(cvMutex_, 1);
        if (stop_) return;
    }
}
```

## 8. 워커를 코어에 고정한다

`sched/affinity.h` 는 P0 감사에서 "구현이 컴파일돼 있으나 호출자가 없다"고 지목된 헤더다. 워커 루프가 그 첫 호출자다.

<!-- snippet: include/sched/affinity.cpp@scoped-affinity -->
```cpp
ScopedAffinity::ScopedAffinity(int core) {
#if defined(_WIN32)
    if (core >= 0 && core < 64) {
        DWORD_PTR prev = SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(uint64_t(1) << core));
        if (prev != 0) { prevMask_ = static_cast<uint64_t>(prev); ok_ = true; }
    }
#elif defined(__linux__)
    cpu_set_t prev;
    CPU_ZERO(&prev);
    if (sched_getaffinity(0, sizeof(prev), &prev) == 0) {
        uint64_t m = 0;
        for (int i = 0; i < 64; ++i) if (CPU_ISSET(i, &prev)) m |= (uint64_t(1) << i);
        prevMask_ = m;
        ok_ = PinThreadToCore(core);
    }
#else
    (void)core;
#endif
}

ScopedAffinity::~ScopedAffinity() {
    if (ok_) SetAffinityMask(prevMask_);   // 이전 마스크 복원
}
```

세 가지가 설계 결정이다.

**RAII 로 스레드 수명 전체에 건다.** `WorkerLoop` 의 첫 줄에 두면 스코프가 스레드 함수 전체다. 스레드가 끝나면 소멸자가 마스크를 복원하는데, 그 스레드는 곧 사라지므로 복원의 효과는 없다 — 그래도 RAII 로 두는 이유는 §10 의 변이가 보여 준다. 이 클래스는 워커 밖에서도 쓰이고, 그때는 복원이 실제로 필요하다.

**실패는 조용히 폴백한다.** `core >= 64` 이거나 권한이 없으면 `ok_` 가 거짓이 되고 스레드는 그냥 OS 배치로 돈다. 고정할 수 없는 환경에서 게임이 멈추면 안 된다.

**우선순위는 고정에 성공했을 때만 올린다.** `Priority::High` 는 Windows 에서 `ABOVE_NORMAL`(권한 불필요)이지만 Linux 에서는 `SCHED_FIFO` 라 `CAP_SYS_NICE` 가 필요하다. 실패해도 무시한다.

재설계안은 오디오 믹싱 스레드에 `Priority::Realtime` 을 주라고 했다. **주지 않았다.** 이 게임의 오디오는 `platform/audio_device.hpp` 의 `waveOut` 이고 전용 스레드가 없다 — `Play()` 가 버퍼를 큐에 넣으면 OS 가 재생한다. 우선순위를 올릴 스레드가 존재하지 않는다.

## 9. 측정: 병렬화가 손해인 이유

적 2048마리·게이틀링 16대·600틱. 워커 0·1·2·4·8.

```
[jobs scale=large towers=16]
  jobs=0 enemies=2048 ticks=600 total=20.7ms advance=4.18ms fire=7.13ms
  jobs=1 enemies=2048 ticks=600 total=22.9ms advance=5.71ms fire=7.34ms
  jobs=2 enemies=2048 ticks=600 total=25.7ms advance=7.08ms fire=8.26ms
  jobs=4 enemies=2048 ticks=600 total=25.9ms advance=8.05ms fire=7.89ms
  jobs=8 enemies=2048 ticks=600 total=30.3ms advance=12.58ms fire=7.55ms
```

**워커를 늘릴수록 느려진다.** 16코어 머신에서 8워커가 직렬보다 1.5배 느리다.

산수를 해 보면 원인이 명확하다. 2048개체를 grain 256으로 나누면 청크 8개다. 직렬 전진은 600틱에 4.18ms 이므로 **틱당 6.97µs**, 청크 하나당 0.87µs 의 일이다.

8워커에서 전진은 12.58ms/600 = 틱당 20.97µs 다. 늘어난 14µs 를 청크 8개로 나누면 **청크 하나를 워커에 보내고 회수하는 데 1.75µs** 가 든다. 청크 안의 일(0.87µs)의 두 배다.

그 1.75µs 가 어디서 오는지도 코드를 보면 보인다. `ParallelFor` 는 청크마다

- 람다를 `std::function` 으로 감싼다 → **힙 할당 하나**
- 큐 뮤텍스를 잡고 `push_back` 한다
- 끝나고 `cv_.NotifyAll()` 로 워커 전부를 깨운다
- 메인 스레드는 `remaining` 을 원자 로드로 스핀한다

10편에서 프레임당 힙 할당을 3회에서 0회로 줄여 놓고, 여기서 틱당 16회(청크 8개 × 시스템 2개)를 다시 넣은 셈이다. 정직하게 적으면 그렇다.

**그래서 기본값은 `jobs=0` 이다.** 이 워크로드에서 병렬화는 켜면 안 된다.

그럼 왜 넣었는가. 세 가지 때문이다.

**하나.** 결정성 규칙이 실제로 지켜지는지를 지금 증명해 두는 것이 나중에 증명하는 것보다 훨씬 싸다. 워커가 들어온 뒤에 결정성을 되찾는 작업은 시뮬 전체를 다시 읽는 일이다.

**둘.** 교차점이 어디인지 알게 됐다. 개체당 일이 지금의 3.4ns(6.97µs / 2048)에서 **20배쯤** 늘거나, grain 을 키워 청크 수를 줄이면 이 표가 뒤집힌다. 지금 게임에 그런 시스템은 없지만, 개체당 계산이 무거운 시스템(경로 재계산·인지 판정·물리 솔버)이 들어오면 생긴다.

**셋.** `ParallelFor` 의 오버헤드 자체가 개선 대상으로 드러났다. 청크당 `std::function` 할당을 없애는 것(고정 크기 작업 구조체), `NotifyAll` 을 `NotifyOne` × n 으로 바꾸는 것이 후보다. 그 개선의 효과는 이 표를 다시 만들어 재면 된다.

잠금 A/B 는 다른 그림이다. 20,000개의 짧은 작업을 워커 4개에 뿌려 큐 경합을 만들면:

```
[worker_lock] tasks=20000 workers=4  std=3.47ms  futex=3.41ms
[worker_lock] tasks=20000 workers=4  std=4.04ms  futex=3.59ms
[worker_lock] tasks=20000 workers=4  std=3.47ms  futex=3.44ms
```

세 번 다 futex 팔이 앞선다. 차이는 2~11%다. 두 팔이 같은 작업 집합을 정확히 한 번씩 실행했다는 것(20,000개 각각 `executed[i] == 1`)이 함께 단언되어 있다.

## 10. 변이: 테스트가 병렬 경로를 정말 돌고 있었는가

**이 절이 이 편에서 가장 중요하다.**

병렬 경로가 전 구간을 덮는지 확인하려고 변이를 넣었다. 마지막 인덱스를 빠뜨리게 만든 것이다.

```cpp
scheduler_->ParallelFor(count - 1, kParallelGrain, fn);   // [MUTATION] 마지막 인덱스 누락
```

그리고 **테스트가 통과했다.**

```
[test] jobs_determinism
[jobs] serial hash=2b69bc550be3a09d
[jobs] workers=1 hash=2b69bc550be3a09d
[jobs] workers=2 hash=2b69bc550be3a09d
[jobs] workers=4 hash=2b69bc550be3a09d
[jobs] workers=8 hash=2b69bc550be3a09d
```

원인은 §5 의 분기였다. 테스트는 `scale=large` 로 상한을 2048로 올렸지만, **웨이브 콘텐츠는 그대로였다.** `GenerateWaves(8)` 은 한 웨이브에 적을 최대 26마리 만든다. 상한을 올려도 실제 개체 수가 512 미만이라 `RunChunks` 가 언제나 직렬로 떨어졌고, 다섯 워커 수의 해시가 같은 것은 **다섯 다 직렬로 돌았기 때문**이었다.

"워커 수와 무관하다"는 단언이 아무것도 증명하지 않는 상태로 통과하고 있었다.

고친 것은 둘이다. 웨이브를 상한을 채우는 것으로 바꾸고, **개체 수가 실제로 512를 넘었는지를 테스트가 단언하게** 했다.

```cpp
// 이 단언이 없으면 위의 해시 비교가 "둘 다 직렬로 돌았다"를 증명하는 것으로
// 조용히 퇴화한다.
if (scale == ScaleKind::Large) CHECK(world.Enemies().Size() >= 512);
```

그 뒤 같은 변이를 다시 넣으니 죽었다.

```
[test] jobs_determinism
[jobs] serial hash=cc327c88298da8a7
[jobs] workers=1 hash=fb5a59ac77e20e3d
  FAIL ...\tests\test_jobs_determinism.cpp:69  CHECK_EQ(serial, parallel)
[jobs] workers=2 hash=fb5a59ac77e20e3d
  FAIL ...\tests\test_jobs_determinism.cpp:69  CHECK_EQ(serial, parallel)
[jobs] workers=4 hash=fb5a59ac77e20e3d
  FAIL ...\tests\test_jobs_determinism.cpp:69  CHECK_EQ(serial, parallel)
```

부수적으로 얻은 것이 하나 있다. 변이 상태에서도 **워커 1·2·4·8 의 해시는 서로 같다.** 병합 규칙 자체는 결정적이라는 뜻이다 — 틀린 결과를 워커 수와 무관하게 재현한다.

**변이 2 — 코어 고정 복원을 지운다.**

```cpp
ScopedAffinity::~ScopedAffinity() {
    if (false) SetAffinityMask(prevMask_);   // [MUTATION] 복원 제거
}
```

```
[test] sched
  FAIL ...\tests\test_sched.cpp:89  CHECK(LandsOnCore(0))
```

이 변이를 잡으려면 테스트가 **중첩**을 봐야 한다. 코어 0에 고정한 바깥 스코프 안에서 코어 1에 고정한 안쪽 스코프를 열고 닫은 뒤, 스레드가 코어 0으로 돌아왔는지를 본다. 단일 스코프만 보는 테스트로는 복원 여부를 구분할 수 없다 — 다음 고정이 마스크를 다시 덮어쓰기 때문이다.

둘 다 복원 후 재통과했다(§11).

## 11. 실행과 관측

### 워커 수와 무관한가

```powershell
cd C:/Users/REIN/Documents/VeLog/projects/mega
cmake --build build --config Release
.\build\bin\Release\test_jobs_determinism.exe
```

```
[test] jobs_determinism
[jobs] serial hash=cc327c88298da8a7
[jobs] workers=1 hash=cc327c88298da8a7
[jobs] workers=2 hash=cc327c88298da8a7
[jobs] workers=4 hash=cc327c88298da8a7
[jobs] workers=8 hash=cc327c88298da8a7
[jobs scale=large towers=16]
  jobs=0 enemies=2048 ticks=600 total=20.7ms advance=4.18ms fire=7.13ms
  jobs=1 enemies=2048 ticks=600 total=22.9ms advance=5.71ms fire=7.34ms
  jobs=2 enemies=2048 ticks=600 total=25.7ms advance=7.08ms fire=8.26ms
  jobs=4 enemies=2048 ticks=600 total=25.9ms advance=8.05ms fire=7.89ms
  jobs=8 enemies=2048 ticks=600 total=30.3ms advance=12.58ms fire=7.55ms
[test] jobs_determinism: 20022 checks, 0 failures
```

20,022개 검사 중 20,000개는 `ParallelFor` 의 grain 을 1·7·64·4096 으로 바꿔 가며 5000개 인덱스가 각각 정확히 한 번 처리됐는지 본 것이다. grain 1은 청크가 5000개 — 극단적으로 잘게 쪼갠 경우에도 중복도 누락도 없어야 한다.

### 두 잠금 구현

```powershell
.\build\bin\Release\test_scheduler_lock_ab.exe
```

```
[test] scheduler_lock_ab
[worker_lock] tasks=20000 workers=4  std=3.47ms  futex=3.41ms
[test] scheduler_lock_ab: 40026 checks, 0 failures
```

이 테스트가 **매달릴 수 있는** 테스트라는 점이 중요하다. 깨우기가 죽으면 `Stop()` 이 워커를 join 하지 못하고 영영 기다린다 — 실패가 아니라 정지다. 그 상태를 실패로 바꿔 주는 것은 CMake 의 `TIMEOUT 120` 하나뿐이다.

### 동기화 프리미티브

```powershell
.\build\bin\Release\test_sync.exe
.\build\bin\Release\test_sched.exe
```

```
[test] sync
[test] sync: 28 checks, 0 failures
[test] sched
[test] sched: 28 checks, 0 failures
```

`test_sync` 에 넷을 더했다 — 값이 이미 다르면 `FutexWait` 이 자지 않는다는 것, 시한 대기가 아무도 깨우지 않아도 돌아온다는 것, 술어가 거짓인 동안 `WakeOne` 이 스레드를 빠져나가게 하지 않는다는 것, `CondVar::WaitFor` 가 통지 없이도 대기자를 여러 번 깨워 술어를 다시 보게 한다는 것.

`test_sched` 에는 중첩 `ScopedAffinity` 복원과 스케줄러의 코어 고정을 더했다. 워커 4개가 서로 다른 코어에서 돌았는지를 집합 크기로 본다.

### 코어 고정을 켠 제품

```powershell
.\build\bin\Release\mega_defense.exe --ab jobs=4 --ab affinity=1 --ab worker_lock=futex --selftest
```

```
[switches] entity_store=aos id_map=linear frame_alloc=malloc blob_alloc=malloc body_alloc=vector worker_lock=futex asset_io=sync scale=small jobs=4 affinity=1 workers=4 lock=futex
[selftest] result=CLEAR ticks=1377 towers=11 wave=6 packet=175 protocol=OK render=OK assets=10/10 content=external
```

`workers=4 lock=futex` 가 로그에 남고, 게임 결과는 기본 팔과 같다.

### 변이 복원 후

```powershell
.\build\bin\Release\test_jobs_determinism.exe
.\build\bin\Release\test_sched.exe
```

```
[test] jobs_determinism: 20022 checks, 0 failures
[test] sched: 28 checks, 0 failures
```

---

## 결론

시뮬의 두 시스템이 워커에 나뉜다. 그리고 나눈 결과가 **워커 수와 무관하게 비트 동일**하다는 것이 다섯 구성의 해시로 단언된다.

규칙 둘이 그 성질의 전부다. 청크는 자기 인덱스 구간만 쓰고, 합치기는 인덱스 오름차순이다. 그 규칙을 지키느라 개체별 피해 배열과 조준 결과 배열이 생겼고, 그 둘이 10편의 프레임 스크래치를 쓰는 자리가 됐다.

스케줄러 내부의 잠금이 두 구현을 갖는다. `std::mutex`/`condition_variable` 과 futex 위에 직접 쌓은 것. 경합 워크로드에서 futex 팔이 2~11% 앞선다.

`sched/affinity` 가 첫 제품 호출자를 얻었다. 워커가 코어에 고정되고, 고정에 성공하면 우선순위가 올라간다.

그리고 이 편의 측정 결과를 정직하게 적으면: **이 워크로드에서 병렬화는 손해다.** 청크 하나의 일(0.87µs)이 청크 하나의 배송비(1.75µs)보다 작다. 기본값은 `jobs=0` 이고 그렇게 남긴다.

현재 상태의 한계.

**`ParallelFor` 가 청크마다 힙을 할당한다.** `std::function` 이 그 원인이고, 10편이 프레임 할당을 0으로 만든 바로 그 경로에 틱당 16회를 다시 넣는다. 고정 크기 작업 구조체로 바꾸면 사라지지만, 그러면 `ParallelFor` 의 서명이 `std::function` 을 받지 않게 되어 호출부가 전부 바뀐다.

**grain 이 상수다.** 256으로 못박혀 있고, 개체당 일의 양에 따라 조정되지 않는다. 지금 워크로드에는 너무 작다 — 청크 수를 줄이면 배송비 총합이 줄지만, 그 조정을 자동으로 하는 장치가 없다.

**나뉜 것은 두 시스템뿐이다.** 발사체·압축·스폰은 직렬로 남았고, 그 셋이 시뮬 시간의 절반 가까이를 쓴다. 암달의 법칙이 상한을 정한다.

**코어 고정의 효과는 재지 않았다.** `affinity=1` 이 워커를 실제로 고정한다는 것은 단언되지만, 그것이 프레임 p99 를 바꾸는지는 이 편에서 재지 않았다 — 병렬화 자체가 손해인 상태에서 배치를 최적화하는 것은 순서가 틀렸다.

**오디오 스레드 우선순위는 넣을 자리가 없었다.** 현재 오디오 백엔드에 전용 스레드가 없다.
