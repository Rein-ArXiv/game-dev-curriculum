## 들어가며

일곱 편까지의 도달점은 "규칙이 규칙으로 강제된다"였다. 계층 규칙과 도달성 게이트가 빌드를 세우고, 제품의 진짜 프레임 루프가 헤드리스로 돈다.

그 상태에서 다음으로 할 일은 정해져 있었다. `include/` 아래 헤더 89개 중 64개가 제품에서 도달조차 하지 않고, 그중 상당수는 "더 빠른 자료구조·할당자·워커 풀"이다. 그것들을 게임에 넣는 것이 남은 일이다.

그런데 그 일을 지금 시작하면 안 된다. **무엇이 느린지 모르는 상태에서 자료구조를 바꾸면, 바꾼 뒤에도 무엇이 나아졌는지 모른다.** 게다가 바꾼 것이 게임을 조용히 다른 게임으로 만들었는지도 알 수 없다.

이 편은 그래서 코드를 빠르게 만들지 않는다. **잴 수 있게** 만든다. 끝나면 두 가지가 생긴다.

1. **시스템별 귀속.** 프레임 하나가 어디서 시간을 썼는지가 `sim.enemy_advance` 같은 이름 단위로 나온다. 파일로도 덤프된다.
2. **A/B 스위치 규약.** 앞으로 들어올 모든 "같은 결과 + 다른 비용" 구현이 어떤 이름으로 켜지고, 어떤 순서로 값을 받고, 무엇을 단언해야 판정으로 인정되는지가 한 파일에 못박힌다.

시작 상태에서 지표는 하나뿐이었다 — `live::Histogram` 의 `frame_ms`. 그것이 답하지 못하는 질문에서 출발한다.

---

## 목차
1. [frame_ms 가 답하지 못하는 질문](#1-frame_ms-가-답하지-못하는-질문)
2. [ScopeTimer 를 어디에 박는가](#2-scopetimer-를-어디에-박는가)
3. [측정 장치가 측정을 왜곡하지 않으려면](#3-측정-장치가-측정을-왜곡하지-않으려면)
4. [첫 측정: 예상이 틀렸다](#4-첫-측정-예상이-틀렸다)
5. [파일로 덤프한다 — 그리고 그것이 쓰기 경로의 첫 호출자다](#5-파일로-덤프한다--그리고-그것이-쓰기-경로의-첫-호출자다)
6. [A/B 스위치 규약: 이름·기본값·출처](#6-ab-스위치-규약-이름기본값출처)
7. [기본값은 언제나 스위치 이전의 동작이다](#7-기본값은-언제나-스위치-이전의-동작이다)
8. [모르는 이름을 무시하면 비교가 거짓말이 된다](#8-모르는-이름을-무시하면-비교가-거짓말이-된다)
9. [판정 절차: (a) 같은 결과, 그다음에 (b) 수치](#9-판정-절차-a-같은-결과-그다음에-b-수치)
10. [계측 자체를 테스트한다](#10-계측-자체를-테스트한다)
11. [실행과 관측](#11-실행과-관측)

---

## 1. frame_ms 가 답하지 못하는 질문

지금 있는 지표는 이것 하나다.

```
[metrics] matches=1 remote_commands=0 snapshots=0 commands_sent=0 command_ack=0
          frame_p95=0.0ms snapshot_bytes_p95=0 snapshot_bytes_max=0 snapshots_truncated=0
```

`frame_p95` 는 "프레임이 몇 ms 였나"에 답한다. 그것으로 답할 수 있는 질문은 하나뿐이다 — **예산을 넘었는가.**

답할 수 없는 질문은 이렇다.

- 넘었다면 **어디서** 넘었는가?
- 적을 64마리에서 2048마리로 올리면 어느 시스템이 먼저 무너지는가?
- 자료구조를 바꿨을 때 좋아진 것이 정말 그 자료구조인가, 아니면 옆의 다른 변화인가?

세 번째가 특히 위험하다. 총 프레임 시간만 보고 최적화를 고르면, 실제로는 무관한 변경이 캐시 배치를 우연히 바꿔서 생긴 차이를 "이 자료구조가 빠르다"로 기록하게 된다. 그런 기록은 다음 사람을 잘못된 방향으로 보낸다.

필요한 것은 **귀속**이다. 프레임 안의 시간을 이름 붙은 구간에 나눠 담는 장치.

`profiler/trace.h` 가 그 장치이고, P0 감사 시점에는 제품 어디에서도 도달하지 않는 헤더였다. 그 파일에는 이미 필요한 것이 다 있다 — RAII 스코프 타이머, 락프리 링 버퍼, 이름별 집계.

<!-- snippet: include/profiler/trace.h#ScopeTimer -->
```cpp
class ScopeTimer {
public:
    explicit ScopeTimer(const char* name)
        : name_(name), startMs_(Profiler::Get().NowMs()) {
        MEGA_USDT2(mega, scope_enter);
    }
    ~ScopeTimer() {
        double end = Profiler::Get().NowMs();
        Profiler::Get().Record(TraceEvent{name_, ThreadTag(), startMs_, end - startMs_});
        MEGA_USDT2(mega, scope_exit);
    }
    ScopeTimer(const ScopeTimer&) = delete;
    ScopeTimer& operator=(const ScopeTimer&) = delete;
private:
    const char* name_;
    double      startMs_;
};
```

## 2. ScopeTimer 를 어디에 박는가

박을 자리를 고르는 기준은 하나다. **다음에 바꿀 것의 경계.**

시뮬 한 스텝은 이미 여섯 시스템으로 쪼개져 있다(2편). 그 여섯이 그대로 경계다 — 앞으로 저장을 바꾸든 할당자를 바꾸든 워커를 붙이든, 영향은 이 여섯 중 하나 이상에 나타난다.

<!-- snippet: include/game/game.hpp@world-step -->
```cpp
void Step(float dt) {
    if (won_ || lost_) return;
    MEGA_PROFILE_SCOPE("sim.step");
    scratch_.Begin();
    SpawnSystem(dt);
    // ...
    EnemyAdvanceSystem(dt);
    TowerFireSystem(dt);
    ProjectileSystem(dt);
    // ...
    WinLoseSystem();
    scratch_.End();
}
```

그리고 시스템 각각의 첫 줄에 자기 이름의 스코프가 하나씩 들어간다(`sim.spawn` · `sim.enemy_advance` · `sim.tower_fire` · `sim.projectile` · `sim.cleanup` · `sim.winlose`).

시뮬 밖에도 셋을 박았다. 프레임 전체(`frame`), 네트워크 펌프(`net.pump`), 소프트웨어 렌더(`render.draw`). 넷 펌프를 중괄호로 감싼 이유는 그 두 줄만 재기 위해서다 — 스코프는 블록 수명이라 재고 싶은 범위를 블록으로 만들어야 한다.

<!-- snippet: include/game/defense_app.hpp@frame-seam -->
```cpp
input_.BeginFrame();
for (size_t i = 0; i < kDefenseActionKeys.size(); ++i)
    input_.OnKey(kDefenseActionKeys[i], frame.keyDown[i]);

const Clock::time_point now = Clock::now();
frameSeconds = std::clamp(frameSeconds, 0.0, 0.25);
accumulator_ += frameSeconds;

{
    MEGA_PROFILE_SCOPE("net.pump");
    if (options_.mode == RunMode::Host) PumpHost();
    else if (options_.mode == RunMode::Client) PumpClient(now, frameSeconds);
}
```

## 3. 측정 장치가 측정을 왜곡하지 않으려면

계측의 첫 번째 실패 모드는 **계측이 측정 대상보다 비싼 것**이다. 여기서 그것을 피하는 장치는 셋이다.

**이름은 포인터로만 저장한다.** `TraceEvent::name` 은 `const char*` 이고 문자열 복사가 없다. 매크로에 들어오는 것은 전부 리터럴이라 수명 걱정이 없다.

**기록은 원자 증가 한 번이다.** `Record` 는 `head_.fetch_add` 로 자리를 잡고 그 자리에 구조체를 쓴다. 락이 없어 워커 스레드가 동시에 기록해도 경합하지 않는다(11편에서 워커가 들어오면 이 성질이 필요해진다).

**링이 가득 차면 덮어쓴다.** 용량은 2^16 이벤트다. 한 프레임에 10개를 남기면 6553프레임 — 109초 분량이 항상 보존된다. 무한히 쌓지 않으므로 긴 실행에서도 메모리가 늘지 않는다.

측정된 오버헤드는 아래 §11 에 있다. 스코프 하나가 30ns 남짓이고, 프레임당 10개면 0.3µs — 16.7ms 예산의 0.002%다.

한 가지 함정이 있었다. `AggregateByName` 이 `count` 를 세는데, 링이 한 바퀴 돌면 앞쪽 이벤트가 덮여 그 수가 줄어든다. 그래서 계측을 검증하는 테스트는 남긴 이벤트 수가 링 용량을 넘지 않는다는 것도 함께 단언한다 — 그러지 않으면 `count == ticks` 단언이 긴 실행에서만 조용히 깨진다.

## 4. 첫 측정: 예상이 틀렸다

셀프테스트 1377틱을 돌리고 집계를 찍었다.

```
[profile] recorded=13770 frame=845.67/2.745ms(n=1377) render.draw=837.10/1.516ms(n=1377)
          sim.step=1.61/0.045ms(n=1377) sim.projectile=0.30/0.042ms(n=1377)
          sim.tower_fire=0.27/0.001ms(n=1377) sim.enemy_advance=0.25/0.003ms(n=1377)
          sim.cleanup=0.15/0.001ms(n=1377) sim.spawn=0.08/0.001ms(n=1377)
```

숫자는 `총합/최댓값(호출 수)` 이다. 읽으면 이렇다.

| 구간 | 총합 | 프레임 대비 |
|---|---|---|
| `frame` | 845.67ms | 100% |
| `render.draw` | 837.10ms | **99.0%** |
| `sim.step` | 1.61ms | 0.19% |

시뮬 전체가 프레임의 **0.2%** 다. 여섯 시스템을 다 합쳐도 렌더 한 번의 1/500 이다.

이 수를 보기 전에 나는 시뮬을 먼저 최적화할 계획이었다. 그것이 틀렸다는 것을 이 한 줄이 말한다 — 현재 규모(적 최대 26마리)에서 시뮬을 두 배 빠르게 만들어도 프레임은 0.1% 빨라진다.

동시에 이 수는 앞으로의 계획을 다시 세워 준다. 시뮬을 재는 것이 의미를 가지려면 **규모가 올라가야 한다.** 그래서 9편의 첫 작업이 자료구조 교체가 아니라 "적 상한을 64에서 2048로 올릴 수 있게 만들기"가 된다. 상한을 올리기 전에는 SoA 도, 워커도, 아레나도 잴 것이 없다.

측정이 다음 할 일의 순서를 바꿨다. 그것이 이 편이 먼저 있는 이유다.

## 5. 파일로 덤프한다 — 그리고 그것이 쓰기 경로의 첫 호출자다

표준 출력 한 줄은 상위 8개까지만 담는다. 전량은 `--trace-out PATH` 로 TSV 를 남긴다.

<!-- snippet: include/game/asset_reload.hpp#FormatTraceReport -->
```cpp
inline std::string FormatTraceReport() {
    const std::vector<profiler::TraceEvent> events = profiler::Profiler::Get().Snapshot();
    std::vector<profiler::AggEntry> aggregate = profiler::AggregateByName(events);
    std::sort(aggregate.begin(), aggregate.end(),
              [](const profiler::AggEntry& a, const profiler::AggEntry& b) {
                  return a.totalMs > b.totalMs;
              });
    std::string out = "scope\tcount\ttotal_ms\tmax_ms\tmean_ms\n";
    char line[256];
    for (const profiler::AggEntry& entry : aggregate) {
        const double mean = entry.count ? entry.totalMs / static_cast<double>(entry.count) : 0.0;
        std::snprintf(line, sizeof(line), "%s\t%llu\t%.3f\t%.3f\t%.4f\n",
                      entry.name.c_str(),
                      static_cast<unsigned long long>(entry.count),
                      entry.totalMs, entry.maxMs, mean);
        out += line;
    }
    return out;
}
```

TSV 를 고른 이유는 두 가지다. 스프레드시트와 `awk` 가 둘 다 그대로 먹고, 값에 쉼표가 들어갈 여지가 없다(스코프 이름은 리터럴이다).

여기서 부수적으로 하나가 해결됐다. P0 감사는 `io/file.cpp` 가 "링크만 되고 호출 0" 이라고 판정했는데, 정확히는 **읽기만 쓰이고 쓰기는 아무도 부르지 않는** 상태였다. 에셋 로드가 `io::ReadTextFile` 을 부르지만 `io::File::WriteAt` 의 호출자는 없었다 — 진행도 저장은 `db/kv_store.hpp` 가 자기 `FILE*` 를 쓴다. 트레이스 덤프가 그 첫 호출자다.

<!-- snippet: include/game/defense_app.hpp#DumpTrace -->
```cpp
bool DumpTrace() {
    if (options_.traceOut.empty()) return false;
    const bool ok = WriteTraceReport(options_.traceOut, options_.switches.assetIo,
                                     scheduler_.WorkerCount() > 0 ? &scheduler_ : nullptr);
    std::printf("[trace] %s %s\n", ok ? "wrote" : "FAILED", options_.traceOut.c_str());
    std::fflush(stdout);
    return ok;
}
```

## 6. A/B 스위치 규약: 이름·기본값·출처

이제 규약이다. 앞으로 네 편에 걸쳐 열 개의 스위치가 들어온다. 그 열 개가 제각각의 방식으로 켜지면, 두 팔을 비교하는 절차 자체가 매번 달라져 결과를 서로 비교할 수 없게 된다.

그래서 여기서 한 파일에 못박는다.

<!-- snippet: include/game/switches.hpp@switch-table -->
```cpp
struct Switches {
    EntityStoreKind entityStore = EntityStoreKind::Aos;
    IdMapKind       idMap       = IdMapKind::Linear;
    FrameAllocKind  frameAlloc  = FrameAllocKind::Malloc;
    BlobAllocKind   blobAlloc   = BlobAllocKind::Malloc;
    BodyAllocKind   bodyAlloc   = BodyAllocKind::Vector;
    WorkerLockKind  workerLock  = WorkerLockKind::Std;
    AssetIoKind     assetIo     = AssetIoKind::Sync;
    ScaleKind       scale       = ScaleKind::Small;
    // ...
    int             jobs        = 0;      // 워커 수. 0 = 직렬(기준선)
    bool            affinity    = false;  // 워커를 코어에 고정하는가
    // ...
    // 이름 하나에 값 하나. 모르는 이름이나 허용되지 않은 값이면 false 와 사유.
    bool Set(std::string_view name, std::string_view value, std::string* error = nullptr);
    // "name=value" 한 덩어리.
    bool Parse(std::string_view assignment, std::string* error = nullptr);
    // 쉼표로 이어 붙인 여러 덩어리("a=b,c=d"). 빈 문자열은 성공(변경 없음).
    bool ParseList(std::string_view list, std::string* error = nullptr);
    // 환경 변수 MEGA_AB 를 읽어 적용한다.
    bool LoadEnvironment(std::string* error = nullptr);

    // 로그 한 줄. 두 팔의 수치를 나란히 적을 때 어느 팔인지 남기는 용도다.
    std::string Describe() const;
};
```

**이름**은 소문자 스네이크 한 단어다. **출처는 셋이고 뒤가 앞을 덮는다** — 기본값 → 환경 변수 `MEGA_AB` → 명령줄 `--ab name=value`.

환경 변수를 둔 이유는 실용이다. 프로세스를 띄우는 쪽이 실행 파일의 인자를 손댈 수 없는 경우가 있다(테스트 하네스, CI 러너, 다른 프로세스가 띄우는 협동 클라이언트). 그때 `MEGA_AB="entity_store=soa,jobs=4"` 하나로 팔을 고를 수 있어야 한다.

<!-- snippet: include/game/defense_app.hpp#ParseDefenseOptions -->
```cpp
inline bool ParseDefenseOptions(int argc, char** argv, DefenseOptions& options) {
    // 순서가 규약이다: 기본값 → 환경 변수 → 명령줄. 뒤가 앞을 덮는다.
    std::string switchError;
    if (!options.switches.LoadEnvironment(&switchError)) {
        std::fprintf(stderr, "MEGA_AB: %s\n", switchError.c_str());
        return false;
    }
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--ab") == 0) {
            if (i + 1 >= argc) return false;
            if (!options.switches.Parse(argv[++i], &switchError)) {
                std::fprintf(stderr, "--ab %s\n", switchError.c_str());
                return false;
            }
            continue;
        }
        // ...
    }
    return true;
}
```

그리고 어느 팔로 돌았는지가 **모든 실행의 로그 첫머리에** 찍힌다. 이것이 없으면 뒤에 나오는 모든 수치가 출처 없는 숫자가 된다.

```
[switches] entity_store=aos id_map=linear frame_alloc=malloc blob_alloc=malloc
           body_alloc=vector worker_lock=std asset_io=sync scale=small jobs=0 affinity=0
           workers=0 lock=std
```

## 7. 기본값은 언제나 스위치 이전의 동작이다

규약의 두 번째 조항이 가장 중요하다.

> 기본값은 언제나 그 스위치가 생기기 전의 동작이다.

이유는 재현성이다. 앞선 편들의 "실행과 관측"에 적힌 수는 전부 스위치가 없던 시절의 실행에서 나왔다. 스위치를 추가하면서 기본값을 새 구현으로 두면, 그 수들이 조용히 재현되지 않게 된다 — 그리고 재현되지 않는 기록은 기록이 아니다.

그래서 열 개 전부의 기본값이 "예전 그대로"다. `entity_store=aos` 는 `std::vector<Enemy>`, `frame_alloc=malloc` 은 함수 지역 배열, `jobs=0` 은 스레드를 하나도 만들지 않는다.

이 조항이 지켜졌다는 증거는 셀프테스트의 숫자다. G1 의 모든 통합이 끝난 뒤에도 기본 팔의 결과가 6편과 같은 값을 낸다.

```
[selftest] result=CLEAR ticks=1377 towers=11 wave=6 packet=175 protocol=OK render=OK
           assets=10/10 content=external
```

`ticks=1377` · `packet=175` — 2편과 4편에 적힌 그 수 그대로다.

## 8. 모르는 이름을 무시하면 비교가 거짓말이 된다

세 번째 조항: **알 수 없는 이름·값은 거절이지 무시가 아니다.**

이 조항이 없으면 어떻게 되는지가 이 규약을 만든 이유다. `--ab entity_stroe=soa` 라고 오타를 내면, 무시하는 구현에서는 프로그램이 조용히 **기본 팔로 돈다**. 그리고 그 실행의 수치를 "soa 팔"이라고 기록한다. 두 팔이 같은 코드를 돌면서 "차이가 없다"는 결론이 나오고, 그 결론은 완전히 틀렸는데 아무 신호도 없다.

<!-- snippet: include/game/switches.hpp#Switches::Set -->
```cpp
inline bool Switches::Set(std::string_view name, std::string_view value, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = std::string(name) + "=" + std::string(value) + ": " + why;
        return false;
    };
    if (name == "entity_store") {
        if (value == "aos") entityStore = EntityStoreKind::Aos;
        // ...
        else return fail("aos|soa|ecs 만 허용");
        return true;
    }
    // ...
    if (error) *error = "알 수 없는 스위치 이름: " + std::string(name);
    return false;
}
```

`jobs` 가 자유 정수를 받지 않는 것도 같은 층위의 결정이다. 허용 집합은 `0|1|2|4|8` 다섯 개뿐이다. 넓히면 "어떤 워커 수에서 재 봤는가"가 실행마다 달라져, 다섯 값의 해시가 같다는 단언이 무엇을 덮는지 흐려진다.

## 9. 판정 절차: (a) 같은 결과, 그다음에 (b) 수치

네 번째 조항이 규약의 핵심이다.

> 판정은 두 단계다. (a) 두 팔의 결과가 같다는 단언, (b) 두 팔의 비용 수치.
> **(a) 없는 (b)는 의미가 없다.**

빨라진 쪽이 다른 답을 냈을 수 있기 때문이다. 그리고 이 순서를 지키지 않으면 가장 흔한 자기기만이 생긴다 — 일을 덜 해서 빨라진 것을 최적화로 기록하는 것.

(a)를 물으려면 "결과가 같다"를 한 수로 물을 수단이 있어야 한다. 그것이 상태 해시다.

<!-- snippet: include/game/game.hpp@state-hash -->
```cpp
uint64_t StateHash() const {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](uint64_t v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    auto mixFloat = [&mix](float f) {
        uint32_t bits = 0;
        std::memcpy(&bits, &f, sizeof(bits));
        mix(bits);
    };
    mixFloat(coreHp_);
    mix(static_cast<uint64_t>(static_cast<int64_t>(gold_)));
    mix(static_cast<uint64_t>(waveIdx_));
    mix(static_cast<uint64_t>(nextEntityId_));
    mix(won_ ? 2u : lost_ ? 3u : 1u);
    const size_t enemyCount = enemies_.Size();
    mix(enemyCount);
    for (size_t i = 0; i < enemyCount; ++i) {
        mix(enemies_.Id(i));
        mixFloat(enemies_.Pos(i).x);
        mixFloat(enemies_.Pos(i).y);
        mixFloat(enemies_.Hp(i));
        mix(enemies_.PathIndex(i));
        mix(enemies_.Alive(i) ? 1u : 0u);
        // ...
    }
    // ...
    return h;
}
```

두 가지가 의도적이다.

**부동소수는 비트 그대로 섞는다.** 반올림한 값이나 정수로 바꾼 값을 섞으면, 미세하게 갈라진 두 세계가 같은 해시를 낸다. 그러면 이 해시는 "같다"를 증명하지 못하고 "비슷하다"만 말한다 — 그리고 비슷한 두 시뮬은 3000틱 뒤에 완전히 다른 결과를 낸다.

**개수를 먼저 섞는다.** 컬렉션마다 크기를 먼저 섞으므로, 원소가 하나 빠진 상태와 원소가 하나 다른 상태가 구분된다.

`nextEntityId_` 가 들어간 것도 의도다. 이 값은 화면에 나타나지 않지만, 발사체가 몇 개 만들어졌는지를 누적한 수다. 두 팔이 같은 위치·체력에 도달했더라도 그 사이에 다른 개수의 발사체를 만들었다면 그것은 다른 세계다.

## 10. 계측 자체를 테스트한다

계측 장치의 특이한 위험이 하나 있다. **계측이 죽어도 게임은 정상으로 보인다.** 스코프를 지우면 그냥 그 이름이 집계에서 사라질 뿐이고, 어떤 테스트도 실패하지 않는다.

그래서 계측 자체에 테스트를 붙였다. 제품의 진짜 프레임 루프를 240틱 돌린 뒤, 이름이 다 있는지와 호출 수가 틱 수와 맞는지를 본다.

<!-- snippet: tests/test_frame_trace.cpp#TestFrameLoopIsInstrumented -->
```cpp
void TestFrameLoopIsInstrumented() {
    profiler::Profiler::Get().Reset();

    DefenseOptions options;
    options.selftest = true;
    options.profilePath = platform::ExecutableDirectory() + "/trace_probe.wal";
    // 매 실행 같은 자리에서 출발한다 — 이 파일은 지워지지 않아 ctest 를 거듭
    // 돌리면 지갑이 누적한다(`test_profile_isolation` 이 그 누적을 잰다).
    CHECK(TruncateFile(options.profilePath));
    DefenseApp app;
    CHECK(app.Init(options));

    const int ticks = 240;
    for (int i = 0; i < ticks; ++i) app.Step(FrameInput{}, kDefenseFixedDt);

    const std::vector<profiler::AggEntry> aggregate =
        profiler::AggregateByName(profiler::Profiler::Get().Snapshot());
    CHECK(!aggregate.empty());

    // 프레임 루프의 이음매들.
    const char* frameScopes[3] = {"frame", "net.pump", "render.draw"};
    for (const char* name : frameScopes) {
        const profiler::AggEntry* entry = Find(aggregate, name);
        CHECK(entry != nullptr);
        if (entry) CHECK_EQ(entry->count, static_cast<uint64_t>(ticks));
    }
    // ...
}
```

시뮬 여섯 시스템은 `count == ticks` 로 단언할 수 없다. 승패가 나면 `Step` 이 조기 반환하기 때문이다. 그래서 `0 < count <= ticks` 로 묶는다 — 0이 아니라는 것이 "이 시스템이 한 번도 안 돌았다"를 배제하고, 틱 수 이하라는 것이 "중복 기록"을 배제한다.

중첩도 함께 지킨다. 바깥 스코프의 총합이 안쪽 스코프의 총합보다 크거나 같아야 한다 — 이것이 깨지면 타이머의 시작·종료 시각이 뒤집힌 것이다.

## 11. 실행과 관측

### 기본 팔이 예전 그대로인가

```powershell
cd C:/Users/REIN/Documents/VeLog/projects/mega
cmake --build build --config Release
.\build\bin\Release\mega_defense.exe --selftest
```

```
[assets] loaded 10/10 external sprites from ...\build\bin\Release/assets
[content] map=external waves=external (5) balance=script
[switches] entity_store=aos id_map=linear frame_alloc=malloc blob_alloc=malloc body_alloc=vector worker_lock=std asset_io=sync scale=small jobs=0 affinity=0 workers=0 lock=std
[selftest] result=CLEAR ticks=1377 towers=11 wave=6 packet=175 protocol=OK render=OK assets=10/10 content=external
[metrics] matches=1 remote_commands=0 snapshots=0 commands_sent=0 command_ack=0 frame_p95=0.0ms snapshot_bytes_p95=0 snapshot_bytes_max=0 snapshots_truncated=0
[profile] recorded=13770 frame=845.67/2.745ms(n=1377) render.draw=837.10/1.516ms(n=1377) sim.step=1.61/0.045ms(n=1377) sim.projectile=0.30/0.042ms(n=1377) sim.tower_fire=0.27/0.001ms(n=1377) sim.enemy_advance=0.25/0.003ms(n=1377) sim.cleanup=0.15/0.001ms(n=1377) sim.spawn=0.08/0.001ms(n=1377)
```

`ticks=1377 towers=11 wave=6 packet=175` — 앞선 편의 값과 같다.

### 트레이스 파일

```powershell
.\build\bin\Release\mega_defense.exe --selftest 200 --trace-out trace_demo.tsv
```

```
[trace] wrote trace_demo.tsv
```

```
scope   count   total_ms        max_ms  mean_ms
frame   200     123.537 1.178   0.6177
render.draw     200     122.690 1.171   0.6135
sim.step        200     0.223   0.005   0.0011
sim.projectile  200     0.039   0.001   0.0002
sim.enemy_advance       200     0.036   0.001   0.0002
sim.tower_fire  200     0.035   0.001   0.0002
sim.cleanup     200     0.022   0.001   0.0001
sim.spawn       200     0.012   0.001   0.0001
net.pump        200     0.007   0.000   0.0000
sim.winlose     200     0.006   0.000   0.0000
```

`sim.winlose` 는 `mean_ms=0.0000` 이다. 이것이 스코프 하나의 바닥 비용에 가깝다 — 그 함수는 비교 두 번이 전부다. 200회에 0.006ms 이므로 스코프 하나가 **약 30ns**, 프레임당 10개면 0.3µs 다. 16.7ms 예산의 0.002%다.

`net.pump` 가 Solo 모드에서도 200회 기록되는 것에 주목할 만하다. 그 안의 분기가 전부 거짓이라 아무 일도 하지 않지만, **스코프는 돈다** — 그래서 "이 프레임에 넷 펌프가 불렸는가"와 "그것이 무엇을 했는가"가 구분된다.

### 계측 테스트

```powershell
ctest --test-dir build -C Release -R "frame_trace|ab_switches"
```

```
    Start  2: test_ab_switches
1/2 Test  #2: test_ab_switches .................   Passed    0.03 sec
    Start 30: test_frame_trace
2/2 Test #30: test_frame_trace .................   Passed    0.16 sec

100% tests passed out of 2
```

### 스위치가 거절하는가

```powershell
.\build\bin\Release\mega_defense.exe --ab entity_stroe=soa --selftest
```

```
--ab 알 수 없는 스위치 이름: entity_stroe
usage: mega_defense [--selftest [ticks] | --gpu-selftest | --host [port] | --join <ipv4> [port] [--token TOKEN]] [--profile PATH] [--vulkan]
                    [--ab name=value]... [--trace-out PATH]
  --ab   entity_store=aos|soa  id_map=linear|hash  frame_alloc=malloc|arena
         blob_alloc=malloc|slab  body_alloc=vector|pool  worker_lock=std|futex
         asset_io=sync|async  scale=small|large  jobs=0|1|2|4|8  affinity=0|1
         (환경 변수 MEGA_AB="name=value,..." 도 같은 이름을 받는다)
```

종료 코드는 2다. 오타 난 스위치로 돌린 측정은 시작조차 하지 못한다.

---

## 결론

이 편이 만든 것은 두 개의 계량기다.

**시스템별 귀속.** 프레임 시간이 이름 붙은 구간으로 나뉜다. 그리고 그 첫 측정이 곧바로 계획을 바꿨다 — 시뮬은 프레임의 0.2%이고, 그것을 재는 일이 의미를 가지려면 규모가 먼저 올라가야 한다.

**A/B 규약.** 이름·기본값·출처·판정 절차가 한 파일에 있다. 앞으로 들어올 열 개의 스위치가 전부 같은 문법으로 켜지고, 전부 "기본값 = 예전 동작"을 지키고, 전부 (a) 같은 결과 → (b) 수치 순서로 판정된다.

지금 상태의 한계는 분명하다.

**귀속은 시스템 단위까지다.** `sim.projectile` 이 느리다는 것은 알 수 있어도 그 안의 어느 줄인지는 모른다. 더 잘게 쪼개는 방법은 스코프를 더 박는 것뿐이고, 그러면 계측 비용이 측정 대상에 가까워진다.

**총합과 최댓값만 있고 분포가 없다.** `max_ms` 는 최악의 프레임 하나를 말하지만 그것이 얼마나 자주 일어나는지는 말하지 않는다. p99 히스토그램은 `live::Histogram` 이 프레임 전체에 대해서만 갖고 있고, 시스템별로는 없다.

**스위치는 프로세스 시작에만 읽힌다.** 매치 중에 팔을 바꿀 수 없으므로, 두 팔의 비교는 언제나 두 번의 실행이다. 같은 실행 안에서 번갈아 재는 방식(같은 캐시 상태·같은 열 상태에서 비교하는 것)은 지금 구조로는 불가능하다.

그리고 가장 큰 한계 — **재는 것은 아직 아무것도 바꾸지 않았다.** LAB_ONLY 등재부는 58행 그대로이고, 제품은 여전히 `std::vector` 와 `new` 위에서 돈다.
