# 27. 스위치는 원래 플래그였다

## 들어가며

8편에서 A/B 스위치 규약을 정할 때 출처를 이렇게 적었다: 기본값 → 환경 변수 `MEGA_AB` → 명령줄 `--ab name=value`. 뒤가 앞을 덮는다. 그 세 줄이 19편까지 충분했던 이유는 하나다 — **팔을 고르는 사람과 프로세스를 띄우는 사람이 같은 사람이었다.**

26편에서 그 전제가 끝났다. 서버가 큐에서 매치를 만들고, 클라이언트가 토큰을 들고 붙는다. 이제 "이 매치는 어느 팔로 도는가"를 정하는 주체와 실행 파일을 띄우는 주체가 다르다. 클라이언트에게 `--ab entity_store=soa` 를 붙여 달라고 부탁할 방법이 없고, 부탁한다 해도 그것은 스위치가 아니라 설득이다.

스위치는 원래 플래그였다. 이름이 있고, 값이 팔 이름이고, 같은 사용자에게는 언제나 같은 값이 나와야 하고, 코드 재배포 없이 바꿀 수 있어야 한다 — 그것이 피처 플래그의 정의 그대로다. 이 편은 그 사실을 인정하고 출처를 옮긴다.

그리고 같은 자리에서 두 번째 축을 넣는다. **시간**이다. 이 게임은 5웨이브 한 판이고, 끝나면 처음부터 다시 시작한다. 오늘의 한 판과 다음 주의 한 판이 완전히 같다 — "언제 플레이했는가"라는 축이 아예 없다. 기간 한정 이벤트가 그 축을 만든다.

| 헤더 | 하는 일 |
|---|---|
| `live/feature_flag.hpp` | 안정 버킷 · 점진 롤아웃 · 팔 이름 · 확정 배정의 인코딩 |
| `live/event_schedule.hpp` | `[start, end)` 창 · 반복 주기 · 수정자 합성 |

도달점의 수치.

- 롤아웃 0/10/30/75/100% 를 2만 명 표본으로 재면 **0.00 / 10.03 / 30.00 / 74.72 / 100.00%**. 0과 100은 근사가 아니라 정확히 맞는다.
- 팔이 셋인 플래그에서 롤아웃에 든 2만 명이 **10003 / 9997** 로 갈린다.
- 팔 개수도 롤아웃도 같고 **이름만 다른** 두 플래그의 사용자 집합 일치율 **50.0%** — 즉 독립이다. 버킷 해시에서 이름 결합을 빼면 이 값이 정확히 100%가 된다.
- 배포되는 `flags.txt` 를 읽으면 variant 5개 + toggle 3개, 한 플레이어에게 확정된 배정이 **121바이트**.
- 겹치는 세 이벤트의 경계: `t=1767571199` 에 활성 2개, **한 초 뒤** `t=1767571200` 에 활성 1개로 완전히 교대한다.

---

## 목차
1. [CLI 가 답하지 못하는 질문](#1-cli-가-답하지-못하는-질문)
2. [안정 버킷 — 같은 사람은 언제나 같은 팔](#2-안정-버킷--같은-사람은-언제나-같은-팔)
3. [플래그마다 독립이어야 하는 이유](#3-플래그마다-독립이어야-하는-이유)
4. [기본 팔은 스위치가 생기기 전의 동작](#4-기본-팔은-스위치가-생기기-전의-동작)
5. [확정 배정 — 판정은 서버에서 한 번만](#5-확정-배정--판정은-서버에서-한-번만)
6. [CLI 오버라이드가 최상단에 남는 이유](#6-cli-오버라이드가-최상단에-남는-이유)
7. [반씩 열린 창](#7-반씩-열린-창)
8. [수정자 합성의 순서](#8-수정자-합성의-순서)
9. [시계를 안에서 읽지 않는다](#9-시계를-안에서-읽지-않는다)
10. [운영자 파일 둘의 원자적 폴백](#10-운영자-파일-둘의-원자적-폴백)
11. [변이 둘](#11-변이-둘)
12. [실행과 관측](#12-실행과-관측)

---

## 1. CLI 가 답하지 못하는 질문

지금 스위치 표는 이렇게 생겼다.

<!-- snippet: include/game/switches.hpp@switch-table -->
```cpp
struct Switches {
    EntityStoreKind entityStore = EntityStoreKind::Aos;
    IdMapKind       idMap       = IdMapKind::Linear;
    FrameAllocKind  frameAlloc  = FrameAllocKind::Malloc;
    // ...
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

이 구조가 답하지 못하는 질문이 셋 있다.

**"플레이어 4821번은 어느 팔로 돌았나."** 답할 수 없다. 스위치는 프로세스 속성이지 플레이어 속성이 아니다. 같은 프로세스가 두 플레이어를 받으면 둘은 같은 팔이고, 다른 날 같은 플레이어가 붙으면 그날의 인자에 따라 다른 팔이다.

**"이 팔을 사용자의 10%에게만 켜라."** 표현할 문법이 없다. `entity_store=soa` 는 전부 아니면 전무다.

**"팔을 바꾸는 데 재배포가 필요한가."** 필요하다 — 정확히는 클라이언트 재실행이 필요하고, 그것은 이미 붙어 있는 세션에는 불가능하다.

셋 다 피처 플래그가 원래 답하는 질문이다.

## 2. 안정 버킷 — 같은 사람은 언제나 같은 팔

버킷의 전부는 이 함수다.

<!-- snippet: include/live/feature_flag.hpp@flag-bucket -->
```cpp
inline uint32_t FlagBucket(std::string_view name, uint32_t userId, uint32_t salt) {
    uint32_t h = 2166136261u;
    for (char c : name) { h ^= static_cast<uint8_t>(c); h *= 16777619u; }
    h ^= salt;   h *= 16777619u;
    h ^= userId; h *= 16777619u;
    h ^= h >> 16;
    return h;
}
```

난수도 시각도 없다. 입력은 (플래그 이름, 사용자 id, salt) 셋뿐이고, 그래서 어느 프로세스에서 몇 번을 물어도 같은 답이 나온다.

**왜 난수가 아니라 해시인가.** 매치마다 팔이 바뀌면 그 사용자가 본 것이 무엇이었는지 사후에 말할 수 없다. "SoA 팔의 p99 가 12% 낮다"는 문장은 어떤 사용자 집합이 SoA 팔이었는지 확정할 수 있을 때만 성립한다. 난수를 쓰면 그 집합이 존재하지 않는다 — 매 프레임 다른 집합이 있을 뿐이다.

`salt` 는 같은 플래그 안의 **서로 독립인 두 질문**을 가른다. "이 사용자가 롤아웃에 드는가"와 "들었다면 어느 대안 팔인가"는 다른 질문인데, 같은 해시로 둘 다 답하면 롤아웃 하위 구간에 특정 팔이 몰린다. 팔이 셋인 `split` 플래그를 2만 명으로 재면 `left=10003 right=9997` — salt 를 나눈 결과다.

## 3. 플래그마다 독립이어야 하는 이유

`for (char c : name)` 한 줄이 이 헤더에서 가장 조용한 방어선이다. 그 줄을 빼도 롤아웃 비율은 정확히 맞고, 안정성도 그대로고, 폴백도 그대로 돈다. 깨지는 것은 **독립성** 하나뿐이다.

독립이 아니면 무슨 일이 일어나는가. `entity_store=soa` 를 받은 사용자 집합과 `pathing=flowfield` 를 받은 사용자 집합이 **정확히 같아진다**. 그러면 "flowfield 팔의 프레임 시간이 낮다"는 관측이 실제로는 SoA 저장의 효과일 수 있고, 그 둘을 분리할 방법이 데이터에 남아 있지 않다. 한 실험의 효과가 다른 실험의 결과로 보고된다.

`tests/test_flag_rollout.cpp` 는 그것을 정면으로 잰다.

<!-- snippet: tests/test_flag_rollout.cpp#TestFlagsAreIndependent -->
```cpp
static void TestFlagsAreIndependent() {
    FeatureFlags flags;
    // 두 플래그의 팔 개수도 롤아웃도 같다. 다른 것은 이름뿐이다.
    CHECK(flags.Define("alpha", {"off", "on"}, 50));
    CHECK(flags.Define("beta", {"off", "on"}, 50));

    int alphaOn = 0, betaOn = 0, agree = 0;
    const int sample = 4000;
    for (uint32_t player = 0; player < (uint32_t)sample; ++player) {
        const bool a = flags.ArmFor("alpha", player) == "on";
        const bool b = flags.ArmFor("beta", player) == "on";
        alphaOn += a ? 1 : 0;
        betaOn += b ? 1 : 0;
        agree += (a == b) ? 1 : 0;
    }
    // 둘 다 대략 절반씩 켜지되,
    CHECK(alphaOn > sample * 45 / 100 && alphaOn < sample * 55 / 100);
    CHECK(betaOn > sample * 45 / 100 && betaOn < sample * 55 / 100);
    // **같은 사용자 집합은 아니다**. 이름이 섞이지 않으면 이 값이 정확히 sample 이 된다.
    std::printf("  alpha_on=%d beta_on=%d 같은 답=%d/%d (%.1f%%)\n", alphaOn, betaOn,
                agree, sample, 100.0 * agree / sample);
    CHECK(agree > sample * 40 / 100 && agree < sample * 60 / 100);
    // ...
}
```

`agree` 가 이 테스트의 전부다. 두 플래그가 같은 사용자에게 같은 답을 준 횟수. 독립이면 4000명 중 절반쯤(측정값 2002, 50.0%), 독립이 아니면 정확히 4000이다.

## 4. 기본 팔은 스위치가 생기기 전의 동작

팔 목록의 **첫 번째**가 기본 팔이다. 그리고 그 기본 팔은 8편의 규약 2 — "기본값은 언제나 스위치가 생기기 전의 동작" — 와 같은 것이어야 한다. `variant entity_store aos soa ecs` 에서 `aos` 가 앞에 오는 이유가 그것이다.

판정은 네 갈래다.

<!-- snippet: include/live/feature_flag.hpp@flag-resolve -->
```cpp
std::string ArmFor(std::string_view name, uint32_t userId) const {
    const VariantSpec* spec = FindVariant(name);
    if (!spec || spec->arms.empty()) return {};
    if (!spec->forced.empty()) {
        for (const std::string& arm : spec->arms)
            if (arm == spec->forced) return arm;      // 운영자가 고른 팔
        return spec->arms.front();                    // 알 수 없는 팔 → 기본 팔
    }
    if (spec->arms.size() == 1) return spec->arms.front();
    if (static_cast<int>(FlagBucket(name, userId, 0u) % 100u) >= spec->rollout)
        return spec->arms.front();                    // 롤아웃 밖 → 기본 팔
    const uint32_t alternatives = static_cast<uint32_t>(spec->arms.size() - 1);
    return spec->arms[1 + FlagBucket(name, userId, 0x9E3779B9u) % alternatives];
}
```

세 갈래가 전부 `arms.front()` 로 떨어진다는 것이 요점이다. 정의가 없으면 빈 문자열이고, 그때 호출자는 자기 기본값을 그대로 쓴다 — 배정이 도착하지 않아도 게임은 스위치 이전 동작으로 돈다.

**알 수 없는 팔에서 크래시하지 않는 이유.** 이 값은 서버가 내려보낸 문자열이고, 서버는 운영자가 적은 파일을 읽는다. 오타가 프로덕션에서 클라이언트를 죽이는 경로가 있으면 그 플래그 시스템은 배포에 쓸 수 없다. 그래서 판정은 언제나 유효한 팔을 낸다.

다만 **운영자 파일 단계에서는 거부한다.** `force=flowfeild` 를 적으면 파일 전체가 거부된다. 런타임 폴백은 와이어를 건너온 값을 위한 것이지, 사람의 오타를 감춰 주는 장치가 아니다. 두 자리의 엄격함이 다른 이유가 그것이다.

## 5. 확정 배정 — 판정은 서버에서 한 번만

클라이언트가 자기 id 로 다시 버킷을 계산하게 두면, 두 프로세스에 같은 해시 함수 사본이 생긴다. 한쪽만 바뀌는 순간 두 프로세스가 다른 게임을 돌게 되고, 그 사고는 desync 로만 드러난다.

그래서 서버가 판정을 끝내고 **결과**를 보낸다.

<!-- snippet: include/live/feature_flag.hpp@flag-wire -->
```cpp
std::string Encode() const {
    std::string out;
    for (const FlagAssignment& item : items_) {
        if (!out.empty()) out += ';';
        out += item.name;
        out += '=';
        out += item.arm;
    }
    return out;
}

// 실패하면 out 을 건드리지 않는다 — 절반만 적용된 배정은 어느 팔로도
// 설명되지 않는 세 번째 상태다.
static bool Decode(std::string_view text, FlagSet& out, std::string* error = nullptr) {
    // ...
}
```

`FlagSet` 은 이름 오름차순을 항상 유지한다. 삽입 순서에 좌우되면 같은 배정이 실행마다 다른 바이트가 되고, 30편이 세우는 골든 패킷 대조가 성립하지 않는다.

문법은 `[a-z0-9_]` 로 좁혔다. 구분자(`=` · `;`)가 값 안에 들어갈 수 없으므로 이스케이프가 필요 없고, 이스케이프가 없으면 디코더에 애매한 상태가 없다. 상한은 이름 24자 · 팔 16자 · 항목 32개 · 전체 512바이트다. 배포되는 `flags.txt` 를 읽어 한 플레이어에게 확정하면 121바이트가 나온다 — 상한의 24%.

배선된 뒤의 실제 크기는 이렇다. 배정은 승인 패킷(`PacketType::AdmissionGrant`)에 실려 내려가고, 그 패킷의 예산은 컴파일 시간에 잠겨 있다.

<!-- snippet: include/game/defense_protocol.hpp@grant-budget -->
```cpp
static constexpr size_t kAdmissionGrantFixedBytes = 4 + 8 + 1 + 1 + 8 + 2;
static constexpr size_t kAdmissionGrantMaxBytes =
    16 + kAdmissionGrantFixedBytes + live::kFlagWireMax;
static_assert(kAdmissionGrantMaxBytes <= kMaxPacketBytes,
              "입장 승인 패킷이 UDP payload 예산을 넘는다");
```

상한은 `16 + 24 + 512 = 552`바이트이고, 실제로 나가는 것은 그보다 훨씬 작다.

```
[admission] player=bob endpoint=127.0.0.1:50461 players=1/2 content=accept grant=161B
            flags=boss_rig_preview=off;camera=ortho2d;entity_store=soa;meta_shop=on;
                  pathing=flowfield;scrap_weekend=on;stealth=0;steering=1
            event_mods=scrap x2.00
```

**161바이트**다. 인코딩된 배정 문자열이 121바이트이고, 합류 헤더 16 + 고정부 24 = 40바이트가 그 앞에 붙는다. 121은 패킷 크기가 아니라 화물의 크기였고, 이제 둘이 구분된다.

승인이 실어 내리는 것이 배정 하나가 아니라는 점도 여기서 드러난다 — 콘텐츠 버전·해시, 서버 플랫폼, 그리고 **매치 시작 시각**이 함께 간다. 시작 시각을 서버가 정하는 이유는 이벤트가 기간이기 때문이다. 두 클라이언트가 각자 시계를 읽으면 한쪽만 주말 이벤트 안에 있는 매치가 생긴다.

## 6. CLI 오버라이드가 최상단에 남는 이유

출처가 넷이 된다: 기본값 → `live::FeatureFlags`(서버가 내려보낸 배정) → 환경 변수 `MEGA_AB` → 명령줄 `--ab`. 뒤가 앞을 덮는 규칙은 그대로다.

플래그가 CLI **아래**에 들어가는 것이 이 편의 유일한 설계 결정이고, 이유는 하나다. **테스트가 팔을 강제로 고를 수 없으면 A/B 는 검사할 수 없다.** 9편의 `test_entity_store_ab` 는 aos·soa·ecs 세 팔을 각각 돌려 상태 해시가 같은지 본다. 그 테스트가 서버의 롤아웃 판정에 좌우되면, 어느 날은 두 팔만 돌고 어느 날은 세 팔이 돌게 된다. 그것은 검사가 아니다.

같은 이유로 `mega_defense_selftest_procgen` 같은 ctest 항목도 `--ab` 로 팔을 못박는다. 플래그가 그것을 이길 수 있으면 게이트가 게이트가 아니게 된다.

배선된 뒤의 실측이다. 같은 실행 파일, 같은 `flags.txt`, 다른 것은 명령줄 하나다.

```
> mega_defense --selftest
[flags] source=file applied=5 meta_shop=1 scrap_event=1 assignment=boss_rig_preview=off;camera=ortho2d;
        entity_store=aos;meta_shop=on;pathing=flowfield;scrap_weekend=on;stealth=0;steering=0
[switches] … pathing=flowfield …

> mega_defense --selftest --ab pathing=poly
[flags] source=file applied=4 meta_shop=1 scrap_event=1 assignment=(위와 동일)
[switches] … pathing=poly … explicit=pathing
```

`applied` 가 5에서 4로 줄고, `explicit=pathing` 이 붙는다. 그 한 이름만 플래그를 통과하지 못했다는 뜻이고, 그것이 CLI 오버라이드가 최상단에 남는다는 문장의 관측 가능한 형태다.

`explicit=` 목록을 `Describe()` 에 넣은 것이 이 배선의 유일한 표시 변경이다. 두 팔의 수치를 나란히 적을 때 어느 이름이 어디서 왔는지가 로그에 없으면, 그 수치는 출처 없는 숫자가 된다.

구현은 `Set` 을 감싸는 한 겹이다. `Parse` 가 `Set` 대신 `SetExplicit` 을 부르고 — `--ab` 와 `MEGA_AB` 는 둘 다 `ParseList → Parse` 를 지나므로 한 줄이 두 출처를 다 표시한다 — `ApplyFlags` 는 그 목록에 있는 이름을 건너뛴다.

<!-- snippet: include/game/switches.hpp@flag-source -->
```cpp
inline int Switches::ApplyFlags(const live::FlagSet& flags, std::string* rejected) {
    int applied = 0;
    for (const live::FlagAssignment& item : flags.All()) {
        if (WasSetExplicitly(item.name)) continue;   // CLI·환경 변수가 언제나 이긴다
        std::string reason;
        if (Set(item.name, item.arm, &reason)) { ++applied; continue; }
        // ...
    }
    return applied;
}
```

`Set` 의 본문은 한 줄도 바뀌지 않았다. 스위치 표는 여전히 이름 하나에 값 하나를 아는 함수이고, 출처가 늘었다는 사실을 모른다.

**배정에 두 종류가 섞여 있다는 것이 배선에서 드러난 것 하나다.** `flags.txt` 는 A/B 스위치 이름(`entity_store` · `pathing`)과 운영 토글(`meta_shop` · `scrap_weekend` · `boss_rig_preview`)을 함께 담는다. 토글을 스위치 표에 그대로 먹이면 전부 "알 수 없는 이름"으로 거절되고, 그 거절 로그가 매 실행마다 나오면 진짜 오타가 그 안에 묻힌다. 규칙은 한 곳에만 둔다.

<!-- snippet: include/game/defense_content.hpp@flag-split -->
```cpp
inline live::FlagSet SplitLiveToggles(const live::FlagSet& assigned, LiveToggles& toggles) {
    live::FlagSet forSwitches;
    for (const live::FlagAssignment& item : assigned.All()) {
        const bool on = item.arm == "on" || item.arm == "1";
        if (item.name == "meta_shop") { toggles.metaShop = on; continue; }
        if (item.name == "scrap_weekend") { toggles.scrapEvent = on; continue; }
        if (item.name == "boss_rig_preview") {
            // ...
            if (on) forSwitches.Assign("boss_render", "rig");
            continue;
        }
        forSwitches.Assign(item.name, item.arm);
    }
    return forSwitches;
}
```

클라이언트와 전용 서버가 같은 함수를 부른다. 서로 다른 규칙을 쓰면 같은 배정이 두 프로세스에서 다른 팔이 된다.

## 7. 반씩 열린 창

이벤트의 활성 판정은 네 줄이다.

<!-- snippet: include/live/event_schedule.hpp@event-active -->
```cpp
bool ActiveAt(int64_t t) const {
    if (t < start) return false;
    const int64_t duration = end - start;
    if (duration <= 0) return false;
    if (period <= 0) return t < end;                 // 1회성
    const int64_t phase = (t - start) % period;      // 주기 안에서의 위상
    return phase < duration;
}
```

`t < end` 의 `<` 가 이 헤더에서 가장 자주 틀리는 자리다. `<=` 로 적으면 종료 시각 그 한 초 동안 이벤트가 여전히 활성이고, 바로 그 시각에 시작하는 다음 이벤트도 활성이다. 두 이벤트가 겹치면 수정자가 두 번 곱해진다 — 스크랩 2배 이벤트가 끝나는 순간 4배가 나오는 종류의 사고다.

배포되는 `events.txt` 로 그 경계를 실제로 관측하면 이렇다.

```
  t=1767484800 active=2 [2,1] scrap=2.00 reward=1.50 hp=1.20 blast=0 boss=1
  t=1767571199 active=2 [2,1] scrap=2.00 reward=1.50 hp=1.20 blast=0 boss=1
  t=1767571200 active=1 [3] scrap=1.00 reward=1.00 hp=1.00 blast=-500 boss=0
```

한 초 사이에 완전히 교대한다. 주말 이벤트 둘이 끝나는 시각이 발파 무료 주간의 시작 시각이고, 그 순간 활성 집합에 겹침이 없다.

## 8. 수정자 합성의 순서

곱하는 값(배율)과 더하는 값(시작 골드)과 불리언(보스 러시)이 섞여 있다. 곱만 있으면 순서가 보이지 않지만 셋이 섞이면 보인다.

<!-- snippet: include/live/event_schedule.hpp@event-compose -->
```cpp
void Compose(const EventModifiers& next) {
    scrapMultiplier   *= next.scrapMultiplier;
    rewardMultiplier  *= next.rewardMultiplier;
    enemyHpMultiplier *= next.enemyHpMultiplier;
    bonusStartGold    += next.bonusStartGold;
    blastCostDelta    += next.blastCostDelta;
    bossRush           = bossRush || next.bossRush;
}
```

합성 순서는 (우선순위 내림차순, 동률은 id 오름차순)으로 고정한다. `stable_sort` 를 쓰는 것도 같은 이유다 — 등록 순서가 결과에 새어 들어오면 안 된다.

<!-- snippet: include/live/event_schedule.hpp@event-modifiers -->
```cpp
EventModifiers ModifiersAt(int64_t t) const {
    EventModifiers out;
    for (const Event* e : ActiveOrdered(t)) out.Compose(e->mods);
    return out.Clamped();
}
```

마지막의 `Clamped()` 는 개별 이벤트가 아니라 **곱**을 자른다. 파서가 각 이벤트의 `scrap` 을 0.25..4 로 검증하지만 4배 이벤트 넷이 겹치면 256배가 된다. 운영자가 실수로 겹쳐 놓은 시즌이 경제를 하루 만에 무너뜨리는 것을 막는 마지막 자물쇠다. 테스트가 그 값을 확인한다 — 4배 이벤트 4개가 겹쳐도 결과는 8.00 이다.

## 9. 시계를 안에서 읽지 않는다

`ActiveAt(int64_t t)` 는 시각을 인자로 받는다. `std::chrono::system_clock::now()` 를 이 헤더 안에서 부르지 않는다.

이유는 셋이다.

**리플레이.** 시계를 안에서 읽으면 같은 시드·같은 입력의 리플레이가 재생 시각에 따라 다른 게임이 된다. 이 시리즈가 세우는 결정성 게이트에 정면으로 어긋난다.

**호스트와 서버의 합의.** 매치 시작 시각을 서버가 정해서 내려보내면 두 프로세스가 같은 이벤트 집합을 본다. 각자 자기 시계를 읽으면 밀리초 차이로 경계에서 갈릴 수 있고, 그 갈라짐은 하루에 한 번만 재현된다.

**테스트.** 경계 시각을 주입할 수 없으면 `[start, end)` 를 검사할 방법이 없다. `t=1767571199` 와 `t=1767571200` 을 나란히 물을 수 있는 것은 시각이 인자이기 때문이다.

`tests/test_event_window.cpp` 는 같은 파일을 두 번 파싱해 "호스트"와 "서버"로 두고, 900부터 2600까지 훑으며 두 관측자의 활성 집합과 수정자가 전부 같은지 본다.

## 10. 운영자 파일 둘의 원자적 폴백

`assets/events.txt` 와 `assets/flags.txt` 는 재빌드 없이 교체할 수 있다고 광고되는 파일이다 = 신뢰 경계 밖 입력이다. 3편의 `balance.script`, 18편의 `boss.pose`, 22편의 `elite.bt` 와 **정확히 같은 규칙**을 따른다.

한 줄이라도 거부되면 파일 전체를 버린다. 절반만 적용된 플래그 표는 "운영자가 의도한 구성"도 "기본 구성"도 아닌 세 번째 상태이고, 세 번째 상태는 아무도 재현할 수 없다.

거부 사유는 표준 오류로 나간다. 폴백은 정상 동작이지만 "왜 폴백인가"까지 조용하면 파일을 교체한 사람은 자기 값이 적용된 줄 안다.

두 파서가 실제로 거부하는 것들.

| 입력 | 거부 사유 |
|---|---|
| `event 1 x 200 100 0 0` | end 는 start 보다 커야 한다 |
| `event 1 x 100 200 50 0` | period 는 지속 시간보다 짧을 수 없다 |
| `event 1 x 100 200 0 0 scrap=9` | scrap 은 0.25..4 |
| `event 1 x 100 200 0 0\nevent 1 y 300 400 0 0` | id 중복 |
| `variant pathing poly astar rollout=250` | rollout 은 0..100 |
| `variant pathing poly astar force=flowfeild` | force 값이 팔 목록에 없다 |
| `toggle beta maybe` | toggle 값은 on\|off |

이벤트 수는 64개, 플래그 정의는 64개, 줄 수는 512·256으로 막았다. 크기 상한이 없는 파서는 파일을 교체할 수 있는 순간부터 서비스 거부 통로다.

## 11. 변이 둘

**변이 1 — 버킷에서 이름 결합을 뺀다.**

```cpp
inline uint32_t FlagBucket(std::string_view name, uint32_t userId, uint32_t salt) {
    uint32_t h = 2166136261u;
    // for (char c : name) { h ^= (uint8_t)c; h *= 16777619u; }   ← 지운다
    h ^= salt;   h *= 16777619u;
    h ^= userId; h *= 16777619u;
    h ^= h >> 16;
    return h;
}
```

```
[test] flag_rollout
  FAIL tests\test_flag_rollout.cpp:68  CHECK(agree > sample * 40 / 100 && agree < sample * 60 / 100)
  FAIL tests\test_flag_rollout.cpp:73  CHECK(FlagBucket("alpha", 12345u, 0u) != FlagBucket("beta", 12345u, 0u))
  rollout=  0% ->   0.00% (0/20000)
  rollout= 10% ->  10.02% (2003/20000)
  rollout= 30% ->  29.98% (5997/20000)
  rollout= 75% ->  75.38% (15075/20000)
  rollout=100% -> 100.00% (20000/20000)
  3-arm split: left=9999 right=10001
[test] flag_rollout: 2926 checks, 2 failures
```

주목할 것은 **실패한 두 줄 말고 전부 그대로라는 것**이다. 롤아웃 비율도, 3팔 분배도, 안정성도, 폴백도 전부 통과한다. 독립성 단언이 없으면 이 변이는 무증상이다.

**변이 2 — 이벤트 창의 경계를 `<=` 로 바꾼다.**

```cpp
if (period <= 0) return t <= end;
// ...
return phase <= duration;
```

```
[test] event_window
  FAIL tests\test_event_window.cpp:27  CHECK(!e.ActiveAt(200))
  FAIL tests\test_event_window.cpp:35  CHECK(schedule.ActiveAt(t).size() <= 1)
  FAIL tests\test_event_window.cpp:38  CHECK_EQ(schedule.ActiveAt(200)[0], 2u)
  FAIL tests\test_event_window.cpp:52  CHECK(!weekly.ActiveAt(1100))
  FAIL tests\test_event_window.cpp:56  CHECK(!weekly.ActiveAt(2100))
[test] event_window: 589 checks, 5 failures
```

세 번째 줄이 특히 읽을 만하다. `ActiveAt(200)[0]` 이 `2` 가 아니라 `1` 이다 — 끝난 이벤트가 우선순위 동률에서 id 가 작다는 이유로 새 이벤트를 밀어냈다.

두 변이 모두 복원했고, 복원 뒤 두 테스트는 각각 2926 / 589 검사에 실패 0이다.

## 12. 실행과 관측

**플래그.**

```
> build\bin\Release\test_flag_rollout.exe
[test] flag_rollout
  alpha_on=1940 beta_on=1950 같은 답=2002/4000 (50.0%)
  rollout=  0% ->   0.00% (0/20000)
  rollout= 10% ->  10.03% (2005/20000)
  rollout= 30% ->  30.00% (6000/20000)
  rollout= 75% ->  74.72% (14943/20000)
  rollout=100% -> 100.00% (20000/20000)
  3-arm split: left=10003 right=9997
  resolve(4242) = entity_store=soa;pathing=astar;steering=0
[test] flag_rollout: 2926 checks, 0 failures
```

`rollout=0` 이 정확히 0이고 `rollout=100` 이 정확히 20000인 것은 근사가 아니다. `bucket % 100 >= rollout` 이라는 비교가 0에서는 언제나 참, 100에서는 언제나 거짓이기 때문이다. 경계를 근사로 두면 "이 플래그를 완전히 껐다"고 말할 수 없다.

**이벤트.**

```
> build\bin\Release\test_event_window.exe
[test] event_window
  t=1800 scrap=2.00 enemy_hp=1.50 blast=-40 boss_rush=1 active=3
[test] event_window: 589 checks, 0 failures
```

**배포되는 두 파일이 실제로 파서를 통과하는가.** 헤더가 아니라 `games/defense/assets/` 에 놓인 파일을 읽어 확인한다.

```
events.txt: 3개 수용
  t=1767000000 active=0 [] scrap=1.00 reward=1.00 hp=1.00 blast=0 boss=0
  t=1767398400 active=1 [1] scrap=2.00 reward=1.00 hp=1.00 blast=0 boss=0
  t=1767484800 active=2 [2,1] scrap=2.00 reward=1.50 hp=1.20 blast=0 boss=1
  t=1767571199 active=2 [2,1] scrap=2.00 reward=1.50 hp=1.20 blast=0 boss=1
  t=1767571200 active=1 [3] scrap=1.00 reward=1.00 hp=1.00 blast=-500 boss=0
  t=1768176000 active=0 [] scrap=1.00 reward=1.00 hp=1.00 blast=0 boss=0
  t=1768003200 active=2 [1,3] scrap=2.00 reward=1.00 hp=1.00 blast=-500 boss=0
flags.txt: variant 5개 toggle 3개
  player 1001 -> boss_rig_preview=off;camera=ortho2d;entity_store=aos;meta_shop=on;pathing=flowfield;scrap_weekend=on;stealth=0;steering=0
  player 1002 -> boss_rig_preview=off;camera=ortho2d;entity_store=soa;meta_shop=on;pathing=flowfield;scrap_weekend=on;stealth=0;steering=0
  player 1003 -> boss_rig_preview=off;camera=ortho2d;entity_store=ecs;meta_shop=on;pathing=flowfield;scrap_weekend=on;stealth=0;steering=1
```

읽을 것 셋.

**`t=1768003200` 의 활성 집합이 `[1,3]` 이다.** 1번은 주 반복 이벤트라 그 주에 다시 열렸고, 3번은 7일짜리 1회성이 아직 안 끝났다. 반복과 1회성이 같은 표에서 공존한다.

**세 플레이어가 `entity_store` 에서 aos / soa / ecs 로 갈렸다.** 롤아웃 50%이고 팔이 셋이라 절반은 aos, 나머지 절반이 soa 와 ecs 로 나뉜다. 우연히 셋이 다 나왔다.

**`pathing` 은 셋 다 `flowfield` 다.** `force=flowfield` 가 롤아웃을 무시하고 전원을 한 팔로 보낸 결과다.

## 13. 제품 실측

**(1) 서버가 파일을 읽었다는 것.**

```
> mega_defense_server --port 27155 --server-id probe-server --run-seconds 6
[platform] Windows/x86-64/LE profile=PC
[patch] version=1 hash=0x51570A985EA2FBD7 assets=18 cooked=17
[flags] source=file variants=5 toggles=3
[events] source=file active=1 scrap=x2.00 reward=x1.00 enemy_hp=x1.00 start_gold=+0 blast_cost=+0 boss_rush=0
```

`variants=5 toggles=3` 이 배포된 `flags.txt` 의 내용과 정확히 같다. `source=file` 이 아니라 `default` 가 찍히면 파일이 거부됐다는 뜻이고, 그때도 서버는 서지만 전원이 기본 팔로 돈다.

**(2) 입장 승인이 그 배정을 내려보낸다.**

```
[admission] player=bob   endpoint=127.0.0.1:50461 players=1/2 content=accept grant=161B
            flags=boss_rig_preview=off;camera=ortho2d;entity_store=soa;meta_shop=on;pathing=flowfield;
                  scrap_weekend=on;stealth=0;steering=1
            event_mods=scrap x2.00
[admission] player=alice endpoint=127.0.0.1:50460 players=2/2 content=accept grant=161B
            flags=(bob 과 동일)
```

두 사람이 **같은** 배정을 받는다. 버킷 키가 플레이어가 아니라 `MatchBucketId(matchId)` 이기 때문이다. 플레이어 단위로 두면 한 매치 안에서 두 사람이 서로 다른 권위 팔(예: `pathing`)을 요구하게 되고, 그 순간 호스트가 둘이 된다.

로컬 실행(`MatchBucketId("local")`)은 다른 버킷이라 `entity_store=aos`·`steering=0` 을 받는다. 같은 파일, 같은 롤아웃, 다른 매치 — 그것이 안정 버킷의 정의다.

**(3) 명령줄이 그 배정을 덮는다.**

```
> mega_defense --selftest                        → [switches] … pathing=flowfield …      (applied=5)
> mega_defense --selftest --ab pathing=poly      → [switches] … pathing=poly … explicit=pathing  (applied=4)
```

`flags.txt` 의 `pathing` 행은 `rollout=100 force=flowfield` 다. 즉 이 파일이 켜져 있는 한 **제품의 기본 실행은 더 이상 폴리라인 팔이 아니다.** 그래서 ctest 에 `mega_defense_selftest_poly` 한 줄을 더했다 — 명령줄이 이긴다는 것과 폴리라인 팔이 여전히 제품 안에서 돈다는 것을 같은 검사가 함께 지킨다.

**(4) 로그가 찍힌다는 것과 배선이 산다는 것은 다르다.**

(1)~(3) 이 보여 준 것은 전부 로그 한 줄이다. `[flags] applied=5` 가 찍힌다는 것과 그 다섯이 실제로 `Switches` 표에 반영됐다는 것 사이에는 함수 하나가 있고 — `Switches::ApplyFlags` — 이 편을 쓸 당시 그 함수를 부르는 테스트는 **하나도 없었다**(`grep -rn ApplyFlags tests/` 가 0행이었다). 같은 공백이 `defense::ApplyEventModifiers` 와 정산의 스크랩 배율에도 있었다.

세 자리를 각각 무력화해 봤다. 제품 동작은 셋 다 확실히 달라진다.

```
ApplyFlags → return 0
  [flags] source=default applied=0 …          (source=file applied=5 였다)
  [switches] … pathing=poly …                 (flowfield 였다 — 다른 탐색 코드가 돈다)

ApplyEventModifiers 본문 건너뜀
  보스 러시 창에서 적 체력 x1.2 · 보상 x1.5 가 웨이브 표에 도달하지 않는다

@settle-scrap 의 배율 → 상수 1.0
  [meta] settle scrap=+208 …                  (+416 이었다 — 정확히 반토막)
```

그런데 그 상태에서 ctest 117개가 **전부 통과했다.** 무증상이 아니라 아무도 안 보고 있었던 것이다. `test_flag_rollout` 과 `test_event_window` 는 `live/` 헤더만 겨냥하고(각각 이 문서 11절과 12절이 그 범위를 적고 있다), 셀프테스트들은 팔을 `--ab` 로 못박아 두어 파일 배정에 애초에 무감각하다.

`tests/test_live_ops_wiring.cpp` 가 그 세 자리를 잇는다. 흉내가 아니라 **제품 `DefenseApp`** 을 헤드리스로 세우고, 실행 파일 옆의 진짜 `flags.txt` · `events.txt` · `balance.script` · `waves.txt` 를 읽게 한 뒤 결과를 묻는다. 시각은 `DefenseOptions::matchUnixOverride` 로 **주입한다** — 벽시계로 판정하면 `weekend_scrap` 이 주 단위로 반복하므로 주말에만 초록인 검사가 된다. 리플레이 재생을 위해 이미 뚫려 있던 주입점이 그대로 검증 가능성이 됐다.

창의 좌표도 손으로 적지 않는다. 일정을 읽어 (a) 아무 이벤트도 없는 시각 (b) 스크랩 배율만 걸리는 시각 (c) 적 체력·보상이 함께 걸리는 시각 셋을 뽑는다. 운영자가 시즌을 옮겨도 검사가 "배선이 죽었다"고 거짓 보고하지 않는다.

```
> build\bin\Release\test_live_ops_wiring.exe
[test] live_ops_wiring
  창: quiet=1767312000 scrap_only=1767398400 content=1767484800 (이벤트 3개)
  flags: source=file applied=5/5 pathing=flowfield meta_shop=1 scrap_event=1
  flags: --ab pathing=poly 가 파일 배정을 이긴다 (applied=4)
  events: t=1767484800 enemy_hp=x1.20 reward=x1.50 boss_rush=1 wave0 hp 12.0->14.4 reward 5->8
  settle: score=1588 base=208  x1.00 -> +208  x2.00 -> +416  (ticks=1377 wave=6 won=1)
[test] live_ops_wiring: 126 checks, 0 failures
```

같은 변이를 다시 걸면 이번에는 죽는다.

```
ApplyFlags → return 0                  16 failures
  FAIL …test_live_ops_wiring.cpp:155  CHECK_EQ(app.Options().switches.Assignments(), expected.Assignments())
  FAIL …test_live_ops_wiring.cpp:162  CHECK_EQ(std::string(app.FlagSource()), std::string("file"))
  flags: source=default applied=0/5 pathing=poly …

ApplyEventModifiers 본문 건너뜀        12 failures
  FAIL …test_live_ops_wiring.cpp:302  CHECK(Near(got.hp, file[i].hp * hpMul))
  events: … wave0 hp 12.0->12.0 reward 5->5
```

두 번째 줄이 이 검사의 요점이다. `Balance()` 만 보면 "밸런스에는 얹혔는데 웨이브에는 안 곱해진" 상태를 밖에서 구분할 수 없다. 그래서 파일에서 읽은 원본 웨이브와 제품이 실제로 굴리는 웨이브를 나란히 두고, 두 **독립한 출처**(밸런스 스크립트를 직접 읽은 값 × 일정이 낸 수정자)의 곱과 대조한다.

**(5) 그리고 변이를 되돌리자 달력이 걸렸다.**

세 번째 변이(`ApplyEventModifiers` 본문 건너뛰기)를 복원한 직후, 손대지 않은 검사 하나가 죽었다.

```
mega_defense_selftest_ai_astar ... ***Failed
  [events] source=file active=2 scrap=x2.00 reward=x1.50 enemy_hp=x1.20 boss_rush=1
  [selftest] result=LOST ticks=7552 towers=12 wave=5
```

원인은 코드가 아니라 **요일**이다. 복원한 날은 2026-08-23, 일요일이었다. 이 편이 지은 `boss_rush` 창은 `period=604800` 으로 **매주 일요일**에 열리고, 그 창은 적 체력 x1.2 에 보스 3배를 건다. A* 팔은 그 편성을 못 버틴다.

즉 이 검사는 **처음부터 일요일마다 빨간** 상태였다. 변이가 이벤트를 통째로 꺼 두는 동안만 우연히 가려져 있었을 뿐이다. 그 주에 코드는 한 줄도 바뀌지 않았다.

이것이 이 편의 진짜 교훈이다. 반복하는 이벤트를 들여오는 순간, **시각을 읽는 모든 검사가 달력에 종속된다.** `test_live_ops_wiring` 은 그 함정을 알고 시각을 주입했지만, 그보다 먼저 있던 셀프테스트 아홉 개는 전부 벽시계를 읽고 있었다.

`DefenseOptions::matchUnixOverride` 에 CLI 표면을 열어 끊었다. 기본값 0 은 "진짜 시계"의 이름이므로 이 인자를 주지 않은 실행은 예전과 한 줄도 다르지 않다.

```cmake
set(MEGA_SELFTEST_UNIX_QUIET 1767312000)   # 활성 이벤트 0개
set(MEGA_SELFTEST_UNIX_EVENT 1767484800)   # boss_rush + weekend_scrap 동시
```

전부 QUIET 로 밀어 두면 이벤트가 걸린 판이 제품 실행 파일 안에서 한 번도 돌지 않는다. 그래서 `mega_defense_selftest_event` 하나를 EVENT 로 못박아 따로 세웠다 — 달력이 우연히 주던 커버리지를 **의도**로 되돌리는 자리다. 두 시각의 틱 수가 실제로 갈린다(1377 대 1488).

**(4)의 반대 방향도 함께 잠갔다.** `--ab` 가 파일 배정을 이긴다는 계약이다. 그것이 깨지면 이 검사를 강화한 대가로 A/B 게이트 전부가 약해진다 — `mega_defense_selftest_poly` 를 포함해 팔을 못박는 모든 항목이 서버 롤아웃에 좌우되기 때문이다.

CTest 는 이 편 시점에 111개 대상을 전수 통과했다. 이 편이 들여온 검사는 `test_flag_rollout`(버킷 안정성 · 롤아웃 수렴 · 팔 독립성)과 `test_event_window`(경계 시각 · 합성 순서)이고, `test_live_ops` 가 그 위에 얹혀 있다. 위 (4)의 `test_live_ops_wiring` 과 `mega_defense_selftest_event` 가 뒤에 더해져 지금은 119개다.

```
> test_flag_rollout.exe
  rollout=  0% ->   0.00% (0/20000)
  rollout= 10% ->  10.03% (2005/20000)
  rollout= 30% ->  30.00% (6000/20000)
  rollout= 75% ->  74.72% (14943/20000)
  rollout=100% -> 100.00% (20000/20000)
  3-arm split: left=10003 right=9997
[test] flag_rollout: 2926 checks, 0 failures

> test_event_window.exe
  t=1800 scrap=2.00 enemy_hp=1.50 blast=-40 boss_rush=1 active=3
[test] event_window: 589 checks, 0 failures
```

---

## 결론

이 편이 한 일은 새 기능을 만든 것이 아니라 **이미 있던 것의 정체를 인정한 것**에 가깝다. `--ab entity_store=soa` 는 처음부터 플래그였다. 값이 팔 이름이고, 코드를 바꾸지 않고 켜고 끄고, 두 구현을 동시에 살려 둔다 — 피처 플래그의 정의 그대로다. 다만 그 플래그의 저장소가 명령줄이었을 뿐이고, 명령줄은 프로세스 하나만큼만 오래 산다.

측정에서 나온 것 셋.

**독립성은 다른 모든 성질이 통과하는 상태에서 혼자 깨질 수 있다.** 이름 결합을 뺀 변이가 롤아웃·안정성·폴백을 전부 통과시킨 것이 그 증거다. 플래그 시스템을 검사할 때 비율만 재면 이 결함을 못 잡는다.

**경계는 근사가 아니라 정확해야 한다.** `rollout=0` 이 "거의 0%"면 그 플래그를 껐다고 말할 수 없고, 껐다고 말할 수 없으면 롤백 수단이 없다.

**겹치는 이벤트의 곱은 개별 이벤트의 범위 검사로 막히지 않는다.** 4배 이벤트 넷이 256배가 되는 것을 파서는 볼 수 없다. 합성 뒤의 클램프가 그 자리를 지킨다.

현재 상태의 한계 넷.

**버킷이 32비트 FNV-1a 다.** 2만 명 표본에서 목표 비율과 최대 0.3%p 어긋난다. 통계적으로 유의한 A/B 를 하려면 이 편차가 실험 효과보다 작아야 하고, 효과가 1% 미만이면 더 나은 해시나 더 큰 표본이 필요하다.

**플래그 배정이 매치 시작에 한 번 고정된다.** 매치 중에 운영자가 플래그를 바꿔도 진행 중인 매치는 옛 배정으로 끝난다. 그것이 옳은 동작이지만("한 매치 안에서 팔이 바뀌면 그 매치의 수치는 어느 팔의 것도 아니다"), 긴급 롤백이 즉시 반영되지 않는다는 뜻이기도 하다.

**이벤트 수정자가 밸런스의 일부만 덮는다.** 스크랩 배율·보상 배율·적 체력·시작 골드·발파 비용·보스 러시 여섯 가지뿐이다. 웨이브 구성 자체를 바꾸는 이벤트는 이 표로 표현할 수 없다.

**`toggle` 과 `variant` 가 같은 이름 공간을 쓰지 않는다.** `Resolve` 는 둘을 같은 `FlagSet` 에 넣지만, 같은 이름을 양쪽에 정의하면 뒤에 삽입된 쪽이 앞을 덮는다. 파서가 그 충돌을 거부하지 않는다.
