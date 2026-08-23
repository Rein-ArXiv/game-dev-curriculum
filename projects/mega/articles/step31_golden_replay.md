# 31. 갈렸다는 것보다 언제 갈렸는가

## 들어가며

이 저장소는 결정성을 여러 번 주장했다. A/B 두 팔이 같은 세계를 돌았다는 문장은 `entity_store` 편에도 `pathing` 편에도 있고, 그 문장의 근거는 언제나 `GameWorld::StateHash()` 하나가 두 팔에서 같더라는 것이었다.

그 대조에는 두 개의 구멍이 있다.

**첫째, 한 프로세스 안에서 잰다.** 두 팔을 나란히 돌리고 마지막에 수를 비교하면, 프로세스 경계를 넘는 비결정성은 두 팔에 **똑같이** 들어가 서로를 상쇄한다. 벽시계로 정해지는 이벤트 배율, 환경 변수가 고른 팔, 저장된 프로필의 시작 골드 티어, 주소 순서에 좌우되는 순회 — 이 전부가 같은 프로세스 안에서는 두 팔에 공통이라 차이를 만들지 않는다.

**둘째, 마지막 수 하나만 본다.** 18000틱을 돌고 수가 다르다는 사실만 남으면 진단은 손으로 이분 탐색을 다시 도는 일이 된다. 더 나쁜 경우도 있다 — 두 시뮬이 300틱에서 갈렸다가 양쪽 다 벽에 눌려 같은 자리로 수렴하면 마지막 해시는 도로 일치한다. 그 순간 "desync 는 없었다"는 **거짓 green** 이 나온다.

이 편이 세우는 것은 그 둘을 동시에 고치는 장치다. 한 실행이 조작 열과 **틱마다 접은 체인**을 파일로 적고, 다른 프로세스가 그 조작만 가지고 같은 체인을 다시 만든다. 상쇄될 상대가 없고, 갈리면 몇 번째 틱인지가 나온다.

| 헤더 | 하는 일 |
|---|---|
| `mp/golden.hpp` | 틱마다 (권위 해시 · 아바타 체크섬 · 아바타 프레임)을 접는 체인, 구간 마크, 대조 |
| `game/replay.hpp` | 리플레이 파일 포맷 · 인코더/디코더 · `mp::Lockstep` 두 번째 재생기 |

도달점의 수치.

- 세 팔의 기록과 재생이 **프로세스를 갈아 끼우고도 체인이 비트 동일**하다. 솔로 `0xecfa0daff65fe392`, 호스트 `0x1f1a5cd4d501f351`, 아바타 `0xd6a4788b65889b9a`.
- 호스트 팔에서 아바타 서브시뮬은 **1377틱 내내 한 번도 전진하지 않는다**(`avatar_frames=0`). lockstep 재생도 같은 이유로 `stalls=1377` — 두 구현이 "원격 입력이 없으면 이 프레임은 열리지 않는다"에 같은 답을 낸다.
- `GameWorld::Step` 의 시스템 두 줄을 맞바꾸면 세 기록이 전부 갈린다. 솔로·호스트는 **1틱**, 아바타는 **52틱** — 각 기록에서 첫 발이 나간 틱이다. 같은 실험을 세 번 더 했는데 **셋은 갈리지 않았다.** 그 사실이 이 게이트의 사정거리다.
- 조작 한 번(500번째 프레임의 키 하나)을 뒤집으면 **501틱**에서 갈린다. 그 앞 500틱은 마지막 마크까지 같다.
- 정수 서브시뮬의 머신 독립 골든 `0x4c9447828fd222f4` 가 저장소 안에 박혔다. 부동소수가 한 비트도 섞이지 않은 값이다.
- `docs/LAB_ONLY.md` 등재부가 **0행**이 되었다. `include/` 114개 헤더 전부가 제품에서 도달한다.

---

## 목차
1. [마지막 해시가 답하지 못하는 두 질문](#1-마지막-해시가-답하지-못하는-두-질문)
2. [링크에 무엇을 넣는가](#2-링크에-무엇을-넣는가)
3. [구간이라는 맞바꿈](#3-구간이라는-맞바꿈)
4. [리플레이가 고정해야 했던 세 가지](#4-리플레이가-고정해야-했던-세-가지)
5. [파일이 정확히 소비되는가](#5-파일이-정확히-소비되는가)
6. [두 번째 재생기](#6-두-번째-재생기)
7. [다루지 못하는 자취를 다룬 척하지 않기](#7-다루지-못하는-자취를-다룬-척하지-않기)
8. [세 팔이 서로 다른 것을 지킨다](#8-세-팔이-서로-다른-것을-지킨다)
9. [변이 — 시스템 두 줄을 맞바꾼다](#9-변이--시스템-두-줄을-맞바꾼다)
10. [갈리지 않은 셋](#10-갈리지-않은-셋)
11. [등재부가 비었다](#11-등재부가-비었다)
12. [실행과 관측](#12-실행과-관측)

---

## 1. 마지막 해시가 답하지 못하는 두 질문

`GameWorld::StateHash()` 는 한 순간을 접는다. 부동소수는 비트 패턴 그대로 섞고, 적·발사체·타워·AI 결정·지형 변화를 전부 훑는다. 좋은 해시다. 문제는 그것을 **한 번만** 부른다는 것이었다.

한 번만 부르면 두 질문에 답할 수 없다.

**언제 갈렸는가.** 이것은 편의의 문제가 아니다. 결정성 결함의 원인은 거의 언제나 "그 틱에 무슨 시스템이 무엇을 만졌는가"인데, 틱을 모르면 그 질문 자체를 시작할 수 없다.

**갈렸다가 붙지는 않았는가.** 아바타 서브시뮬이 이 함정을 이미 알고 있었다. `mp::DetState` 에는 `trail` 이라는 필드가 있고, 그 주석이 이렇게 적혀 있다 — 위치만 비교하면 갈라졌다가 양쪽 다 같은 벽에 눌려 다시 붙은 divergence 를 놓친다. 그때 체크섬이 도로 일치해 desync 가 없었던 것처럼 보인다.

`trail` 은 아바타 위치 이력에 대해 그 문제를 푼다. 권위 시뮬 전체에 대해서는 아무도 풀지 않았다.

체인이 그것을 푼다. 틱 t 의 링크는 t-1 까지 접은 값 위에 이번 틱의 상태를 얹어 만든다.

<!-- snippet: include/mp/golden.hpp@golden-append -->
```cpp
class GoldenChain {
public:
    // stride 0 은 허용하지 않는다(마크가 하나도 안 남아 진단이 불가능해진다).
    void Reset(uint32_t stride) {
        stride_ = stride == 0 ? 1u : stride;
        chain_ = kGoldenOffsetBasis;
        ticks_ = 0;
        marks_.clear();
        sealed_ = false;
    }

    // 한 틱을 접는다. 순서가 계약이다 — 넷 중 하나라도 자리를 바꾸면 같은 상태가
    // 다른 체인을 내므로, 이 순서는 파일 포맷의 일부다.
    void Append(uint64_t worldHash, uint64_t avatarChecksum, uint32_t avatarFrame) {
        ++ticks_;
        Mix(static_cast<uint64_t>(ticks_));
        Mix(worldHash);
        Mix(avatarChecksum);
        Mix(static_cast<uint64_t>(avatarFrame));
        if (ticks_ % stride_ == 0) marks_.push_back(GoldenMark{ticks_, chain_});
    }

    // 마지막 마크가 마지막 틱과 어긋나 있으면(틱 수가 stride 의 배수가 아니면)
    // 끝을 하나 더 남긴다. 이것이 없으면 두 실행의 **꼬리**가 비교되지 않는다.
    void Seal() {
        if (marks_.empty() || marks_.back().tick != ticks_)
            marks_.push_back(GoldenMark{ticks_, chain_});
        sealed_ = true;
    }
```

접두사에 민감하므로 한 번 갈리면 되돌아오지 않는다. `test_replay_golden` 이 그 성질을 직접 세운다 — 37틱에서 한 번만 다르게 접은 두 체인은 80번째 마크에서도 여전히 다르다.

## 2. 링크에 무엇을 넣는가

넷을 접는다. 각각 들어가는 이유가 다르고, 이유가 없는 것은 넣지 않는다.

| 넣는 것 | 이유 |
|---|---|
| 틱 번호 | 위치 민감성. 이것이 없으면 "같은 상태를 한 틱 늦게 지났다"가 같은 체인을 낸다 |
| 권위 상태 해시 | float 비트 패턴 그대로. 반올림한 값을 섞으면 갈라진 상태가 같은 해시를 낼 수 있다 |
| 아바타 체크섬 | 정수 서브시뮬. 소유자가 다르다 — 권위는 호스트가, 아바타는 양쪽이 각자 굴린다 |
| 아바타 프레임 | 서브시뮬이 **몇 번 전진했는가**. 스톨은 상태를 바꾸지 않으므로 체크섬만으로는 안 보인다 |

마지막 것이 실측으로 정당화된 항목이다. 8편의 비예측 팔에서 원격 입력이 없으면 `mp::Rollback::Tick()` 은 `false` 를 돌려주고 상태를 그대로 둔다. 즉 **10틱 동안 멈춘 실행과 한 번도 안 멈춘 실행이 같은 체크섬을 낸다.** 프레임 번호를 링크에 넣으면 그 둘이 갈린다.

```cpp
    for (uint32_t tick = 1; tick <= 20; ++tick) {
        moving.Append(7u, 0xABCDu, tick);          // 매 틱 서브시뮬이 전진
        stalled.Append(7u, 0xABCDu, tick < 10 ? tick : 9u);   // 9프레임에서 멈춤
    }
```

이 두 체인은 10틱에서 갈린다. 상태는 한 번도 다른 적이 없다.

**넣지 않는 것도 규약이다.** 벽시계·프레임 시간·프로파일러 수치는 링크에 넣지 않는다. 실행마다 다르므로 넣는 순간 이 체인은 언제나 갈리고, 언제나 실패하는 게이트는 며칠 안에 꺼진다.

접는 자리는 고정 스텝 루프 안이고, 권위 시뮬과 아바타 서브시뮬이 **둘 다 끝난 뒤**다.

<!-- snippet: include/game/defense_app.hpp@chain-append -->
```cpp
    void RecordTick() {
        if (!capture_) return;
        chain_.Append(world_.StateHash(), avatars_.Checksum(), avatarFrame_);
        ReplayAvatarTick tick;
        tick.local = lastAvatarInput_;
        tick.advanced = lastAvatarAdvanced_ ? 1 : 0;
        tick.checksum = avatars_.Checksum();
        capturedAvatar_.push_back(tick);
    }
```

`avatarFrame_` 은 8편이 정한 좌표 그대로다 — "틱을 마친 뒤의 프레임 수". 그때 한 칸 어긋나 정상 동작이 전량 desync 로 보고된 적이 있어서, 새 소비자가 생길 때마다 같은 좌표를 쓰는지 확인한다.

## 3. 구간이라는 맞바꿈

마크를 몇 틱마다 남길지가 `stride` 다. 1이면 틱마다 남기므로 갈린 **틱**을 정확히 짚고, N이면 갈린 **구간**만 나온다. 정밀도와 메모리의 맞바꿈이다.

중요한 것은 그 맞바꿈을 감추지 않는 것이다.

<!-- snippet: include/mp/golden.hpp@golden-compare -->
```cpp
inline ChainDiff CompareChains(const GoldenChain& golden, const GoldenChain& fresh) {
    ChainDiff diff;
    diff.goldenTicks = golden.Ticks();
    diff.freshTicks = fresh.Ticks();
    diff.exact = golden.Stride() == 1 && fresh.Stride() == 1;

    const std::vector<GoldenMark>& a = golden.Marks();
    const std::vector<GoldenMark>& b = fresh.Marks();
    const size_t common = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < common; ++i) {
        if (a[i].tick == b[i].tick && a[i].chain == b[i].chain) {
            diff.lastAgreeTick = a[i].tick;
            continue;
        }
        diff.diverged = true;
        // 마크의 틱 번호까지 어긋났다면(한쪽이 stride 를 다르게 잡았다면) 작은
        // 쪽이 의심 구간의 오른쪽 끝이다.
        diff.suspectTick = a[i].tick < b[i].tick ? a[i].tick : b[i].tick;
        return diff;
    }

    // 마크가 겹치는 데까지는 같은데 길이가 다르다 — 한쪽이 먼저 끝났다.
    // 이것도 갈림이다. 조기 종료는 상태가 아니라 **진행**의 차이라서 마지막
    // 해시 비교로는 잡히지 않는 부류다.
    if (a.size() != b.size() || golden.Ticks() != fresh.Ticks() ||
        golden.Chain() != fresh.Chain()) {
        diff.diverged = true;
        diff.suspectTick = diff.lastAgreeTick + 1;
    }
    return diff;
}
```

`exact` 가 false 면 `suspectTick` 은 정확한 틱이 아니라 구간의 오른쪽 끝이다. 짐작으로 정확한 틱을 말하지 않는다 — **틀린 진단은 없는 진단보다 나쁘다.** stride 64 로 137틱에서 갈린 자료를 대조하면 `(128, 192]` 가 나오고, 그 구간을 stride 1 로 다시 돌리면 137 이 나온다. 그 두 걸음이 진단 절차다.

마지막 `if` 는 길이 차이를 갈림으로 센다. 조기 종료는 상태가 아니라 진행의 차이라, 두 실행의 마지막 상태가 우연히 같아도 다른 실행이다.

제품 기록은 stride 1 로 돈다. 1377틱 기록의 마크가 1377개고 파일이 44541바이트니, 정밀도를 포기할 이유가 없다.

## 4. 리플레이가 고정해야 했던 세 가지

여기서부터가 배선하며 발견한 것들이다. **결정성은 조작을 고정한다고 얻어지지 않는다.** 이 제품의 한 프레임에는 조작 말고도 세 개의 외부 입력이 있었고, 셋 다 고정하지 않으면 검증기가 결정성 결함이 아닌 것을 결정성 결함으로 보고한다.

**A/B 팔 전체.** `assets/flags.txt` 는 `pathing` 을 `flowfield` 로 강제하고 `entity_store` 를 절반에게 `soa` 로 준다. 명시된 스위치만 기록하면 나머지는 **재생 시점의** 롤아웃이 정한다. 운영자가 롤아웃 하나를 바꾸는 순간 어제 기록한 리플레이가 오늘 갈리고, 그 갈림은 결정성 결함이 아닌데 게이트는 실패로 보고한다.

그래서 27개 전부를 적는다. 배정이 **끝난 뒤**의 값이다.

<!-- snippet: include/game/switches.hpp@switch-assignments -->
```cpp
inline std::string Switches::Assignments() const {
    std::string out;
    auto put = [&out](const char* name, const std::string& value) {
        if (!out.empty()) out += ',';
        out += name;
        out += '=';
        out += value;
    };
    put("entity_store", entityStore == EntityStoreKind::Ecs ? "ecs"
                      : entityStore == EntityStoreKind::Soa ? "soa" : "aos");
```

전부 적어 두면 `Switches::ApplyFlags` 의 `WasSetExplicitly` 가 재생 쪽 배정을 통째로 막는다. 27편이 세운 "CLI 가 언제나 플래그를 이긴다"는 규약이 여기서 두 번째 소비자를 얻는다 — 리플레이는 CLI 와 같은 자격으로 팔을 고정한다.

**매치 시작 시각.** `assets/events.txt` 의 이벤트는 주 단위로 반복하는 창을 갖고, 그 창 안에서 시작 골드·적 체력·발파 비용·스크랩 배율이 달라진다. 기록과 재생 사이에 창 하나만 열려도 첫 틱부터 갈린다. 그래서 파일이 `matchUnix` 를 들고, 재생은 벽시계 대신 그 값을 쓴다.

```cpp
            // 리플레이 재생은 시계를 못박는다. 이벤트 창은 주 단위로 반복하므로
            // 기록과 재생 사이에 창 하나만 열려도 시작 골드·적 체력이 달라진다.
            matchStartUnix_ = options_.matchUnixOverride != 0 ? options_.matchUnixOverride
                                                              : UnixSeconds();
```

**프로필 저장소.** 28편의 지갑이 `startGoldTier` 를 들고, 그 값이 `balance_.startGold` 에 더해진다. 셀프테스트는 실행 파일 옆의 `selftest_profile.wal` 을 쓰는데, 그 파일에는 `--buy-tier 5` 로 도는 다른 검사가 커밋한 티어가 남아 있다. 기록과 재생이 그 파일을 공유하면 시작 골드가 달라진다.

이것은 가설이 아니라 관측이다. 같은 1377틱을 돌린 두 실행이 타워를 **12개와 10개** 세웠고, 차이는 코드가 아니라 그 `.wal` 파일 하나였다.

고친 방법은 리플레이 파일 옆에 전용 저장소를 두고, 기록과 재생 **양쪽 모두** 그것을 0바이트로 자르고 시작하는 것이다. 두 실행이 같은 빈 상태에서 출발한다.

```cpp
inline std::string ReplayProfilePath(const std::string& replayPath) {
    return replayPath + ".wal";
}
```

세 가지를 한 문장으로 줄이면 이렇게 된다 — **리플레이는 조작이 아니라 프레임의 모든 외부 입력을 기록한다.** 하나라도 빠지면 거짓 실패를 내고, 거짓 실패를 내는 게이트는 꺼진다.

## 5. 파일이 정확히 소비되는가

포맷은 4편의 와이어 규율을 그대로 따른다. 리틀엔디안 고정 폭, 컬렉션마다 개수와 상한, 그리고 **정확한 소비**.

<!-- snippet: include/game/replay.hpp@replay-decode -->
```cpp
inline bool DecodeReplay(const std::vector<uint8_t>& bytes, Replay& out,
                         std::string* error = nullptr) {
    size_t cursor = 0;
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    auto need = [&](size_t n) { return bytes.size() - cursor >= n; };
```

디코더가 거절하는 것은 여덟 가지다 — 매직 불일치, 버전 불일치, 헤더 절단, 스위치 문자열 상한 초과, 프레임/자취/마크 절단, 인원 수 범위 밖, **잉여 바이트**, 그리고 봉인되지 않은 체인.

잉여 바이트를 거절하는 이유는 4편에서 적은 것과 같다. 흘려보내면 두 번째 프레이밍이 생긴다 — 같은 파일을 두 가지로 읽을 수 있게 되고, 그러면 "이 파일이 무엇인가"에 답이 둘이 된다.

기록된 아바타 입력도 신뢰 경계 밖으로 다룬다.

```cpp
        if (!mp::InputInRange(tick.local)) return fail("아바타 입력이 범위 밖이다");
        if (tick.advanced > 1) return fail("전진 플래그가 0|1 이 아니다");
```

`moveX = 7` 인 입력을 그대로 서브시뮬에 넣으면 정수 클램프를 우회하는 통로가 된다. 와이어 디코더가 이미 같은 검사를 하고 있었고, 파일도 같은 급의 입구다.

**봉인 검사**가 이 포맷에만 있는 것이다. 마지막 마크의 틱이 총 틱 수와 같지 않으면 그 파일은 `Seal()` 을 지나지 않았거나 손상된 것이다. 둘 다 재생 대상이 아니고, 재생하면 꼬리가 비교되지 않은 채 MATCH 가 나온다.

조작의 와이어 형태는 17개 액션 키를 3바이트에 담는다. `FrameInput` 자체를 쓰지 않는 이유는 순환 때문이다 — 그 구조체는 프레임 루프 헤더가 소유하고, 그 헤더가 이 포맷 헤더를 인클루드한다.

<!-- snippet: include/game/defense_app.hpp@replay-frame-codec -->
```cpp
    static ReplayFrame ToReplayFrame(const FrameInput& frame) {
        ReplayFrame out;
        for (size_t i = 0; i < kDefenseActionKeys.size(); ++i)
            if (frame.keyDown[i]) out.keys |= (1u << i);
        out.mouseX = static_cast<int16_t>(std::clamp(frame.mouseX, -32768, 32767));
        out.mouseY = static_cast<int16_t>(std::clamp(frame.mouseY, -32768, 32767));
        out.flags = frame.mouseClicked ? kReplayClicked : 0;
        return out;
    }
    static FrameInput FromReplayFrame(const ReplayFrame& record) {
        FrameInput frame;
        for (size_t i = 0; i < kDefenseActionKeys.size(); ++i)
            frame.keyDown[i] = (record.keys & (1u << i)) != 0;
        frame.mouseX = record.mouseX;
        frame.mouseY = record.mouseY;
        frame.mouseClicked = (record.flags & kReplayClicked) != 0;
        return frame;
    }
```

## 6. 두 번째 재생기

여기서 `mp/lockstep.hpp` 가 등재부를 떠난다.

그 헤더는 이 저장소가 만든 첫 주부터 있었고, 한 번도 제품에 없었다. 등재 사유는 언제나 같았다 — 호스트 권위 + 스냅샷 복제 모델과 lockstep 은 같은 문제에 대한 서로 다른 답이라, 둘을 동시에 넣으면 권위의 원본이 둘이 된다.

그 사유는 **여전히 옳다.** lockstep 은 지금도 이 게임의 네트워크 모델이 아니다. 바뀐 것은 lockstep 이 네트워크 모델 말고 다른 자격으로 쓰일 자리가 생겼다는 것이다.

아바타 서브시뮬을 제품에서 굴리는 것은 `mp::Rollback` 이다. 예측하고, 확정이 예측과 다르면 되감아 다시 굴린다. rollback 정확성의 정의는 이렇게 적혀 있다 — 재시뮬 결과는 "처음부터 그 입력을 안 경우"와 비트 동일하다.

**그 정의를 검사하려면 "처음부터 그 입력을 안 경우"를 실제로 굴려 봐야 한다.** 그것이 정확히 lockstep 이다. 예측하지 않고, 전원의 입력이 모일 때까지 멈추고, 모이면 그 입력으로 한 프레임을 민다.

<!-- snippet: include/game/replay.hpp@lockstep-replay -->
```cpp
inline LockstepReport VerifyWithLockstep(const Replay& replay) {
    LockstepReport report;
    mp::Lockstep lockstep;
    lockstep.Init(replay.players, 0);
    uint32_t frame = 0;
    bool sawStall = false;
    for (size_t i = 0; i < replay.avatar.size(); ++i) {
        const ReplayAvatarTick& tick = replay.avatar[i];
        const uint32_t tickNumber = static_cast<uint32_t>(i) + 1;
        lockstep.SubmitInput(frame, replay.localPlayer, tick.local);
        if (replay.prediction) {
            // 예측 팔에서 확정이 오지 않은 자리를 `Rollback` 은 '바닥값'으로
            // 메운다. 헤드리스에는 원격 피어가 없어 그 바닥값이 처음부터 끝까지
            // 기본 입력이다 — 같은 값을 여기 넣어야 두 재생이 같은 것을 민다.
            for (int p = 0; p < replay.players; ++p)
                if (p != replay.localPlayer) lockstep.SubmitInput(frame, p, mp::Input());
        }
        const bool canAdvance = lockstep.CanAdvance();
        if (tick.advanced == 0) {
            // 제품이 멈췄다. lockstep 도 멈춰야 한다 — 전원의 입력이 모이기
            // 전에 전진하면 그것이 lockstep 이 아니다.
            if (canAdvance) {
                report.fault = LockstepFault::Advanced;
                report.faultTick = tickNumber;
                return report;
            }
            sawStall = true;
            ++report.stalls;
            continue;
        }
```

두 재생기는 같은 `mp::DetState` 를 서로 다른 규율로 민다. 확정 입력이 같으면 결과도 같아야 한다는 것이 rollback 정확성의 정의이므로, 이 대조는 그 정의를 실행 가능한 단언으로 바꾼다. **권위의 원본은 여전히 하나다** — 두 번째 재생기는 아무것도 결정하지 않고 대조만 한다.

문지기 자체도 따로 세운다. 2인 lockstep 에 한 명의 입력만 넣으면 `CanAdvance()` 는 거짓이고, `Advance()` 를 불러도 프레임과 체크섬이 그대로다. 나머지 한 명이 오면 전진하고, 다음 프레임에서 다시 닫힌다.

```cpp
    lockstep.SubmitInput(0, 0, ScriptedInput(0, 0));
    CHECK(!lockstep.CanAdvance());          // 한 명만 왔다
    const uint64_t before = lockstep.Checksum();
    lockstep.Advance();                     // 무시돼야 한다
    CHECK_EQ(lockstep.Frame(), 0u);
    CHECK_EQ(lockstep.Checksum(), before);
```

## 7. 다루지 못하는 자취를 다룬 척하지 않기

두 재생기가 정확히 대응하지 **않는** 경우가 하나 있다.

어떤 프레임이 멈췄다가 나중에 전진하면, `mp::Rollback::SetInput` 은 그 사이에 온 로컬 입력으로 슬롯을 덮어쓰므로 **마지막** 값을 쓴다. `mp::Lockstep::SubmitInput` 은 중복 등록을 무시하므로 **첫** 값을 지킨다. 두 규율이 다른 입력을 고르니 대조가 성립하지 않는다.

헤드리스 기록에서는 이 자취가 나오지 않는다. 솔로 팔은 1인이라 언제나 전진하고, 원격 없는 비예측 팔은 언제나 멈춘다. 그 사실에 기대는 대신, 나오면 터지게 뒀다.

```cpp
        if (sawStall) {
            report.fault = LockstepFault::StallThenAdvance;
            report.faultTick = tickNumber;
            return report;
        }
```

"지금은 안 나온다"에 기대는 코드와 "나오면 실패한다"고 적은 코드는 오늘 같은 결과를 낸다. 다른 것은 그 자취가 실제로 나오는 날이다 — 앞의 것은 조용히 틀린 답을 내고, 뒤의 것은 시끄럽게 멈춘다.

`test_replay_golden` 이 그 경로를 실제로 만들어 확인한다. 전부 멈춘 2인 자취의 30번째 틱만 "전진했다"로 바꾸면 검증기가 31틱에서 `stall_then_advance` 로 거절한다.

## 8. 세 팔이 서로 다른 것을 지킨다

기록·재생 짝을 세 팔에 건다. 셋이 서로 다른 것을 지킨다.

| 팔 | 아바타 인원 | 예측 | lockstep 재생이 하는 일 |
|---|---|---|---|
| solo | 1 | 끔 | 매 틱 전진 — 체크섬 1377개를 전량 대조 |
| host | 2 | 끔 | **한 번도 전진하지 않음** — 문지기가 1377틱 내내 닫혀 있다 |
| avatar | 2 | 켬 | 되감기 기계가 돌고, 없는 원격이 바닥값으로 메워진다 |

호스트 팔이 흥미롭다. 원격 피어가 없으므로 `Rollback::Tick()` 이 매 틱 `false` 를 돌려주고 아바타는 첫 프레임에서 굳는다. lockstep 재생도 같은 이유로 한 번도 전진하지 않는다 — 로컬 입력은 매 틱 제출되는데 원격 자리가 비어 있어 `CanAdvance()` 가 거짓이다. **두 구현이 "이 프레임은 열리지 않는다"에 같은 답을 낸다.**

CTest 는 이 여섯을 fixture 로 묶는다. 기록이 먼저 끝나야 재생이 읽을 파일이 있고, `-j` 병렬 실행에서 순서가 뒤집히면 재생은 없는 파일을 읽는다.

<!-- snippet: CMakeLists.txt@replay-pair -->
```cmake
function(mega_add_replay_pair arm ticks port)
    set(path ${MEGA_REPLAY_DIR}/${arm}.mgrp)
    set(shared "")
    if(NOT port EQUAL 0)
        set(shared --host ${port})
    endif()
    add_test(NAME mega_defense_record_${arm}
             COMMAND mega_defense --selftest ${ticks} --record ${path}
                     --match-unix ${MEGA_SELFTEST_UNIX_QUIET} ${shared} ${ARGN})
    set_tests_properties(mega_defense_record_${arm} PROPERTIES
                         TIMEOUT 300 FIXTURES_SETUP replay_${arm})
    add_test(NAME mega_defense_replay_${arm}
             COMMAND mega_defense --verify-replay ${path} ${shared})
    set_tests_properties(mega_defense_replay_${arm} PROPERTIES
                         TIMEOUT 300 FIXTURES_REQUIRED replay_${arm})
endfunction()
```

기록 쪽은 시각을 못박는다. `assets/events.txt` 의 창은 주 단위로 **반복**하므로, 못박지 않으면 같은 명령이 화요일과 일요일에 서로 다른 게임을 기록한다 — 일요일에는 boss_rush 창이 열려 적 체력 x1.2 에 보스 3배가 걸린다. 기록과 재생은 파일이 시각을 나르므로 그때도 서로 일치하지만(`Replay::matchUnix`), 골든 체인의 값 자체가 요일마다 달라지면 "어제와 같은 해시인가"를 물을 수 없다. 실제로 이 저장소는 그 함정을 한 번 밟았다 — 2026-08-23(일)에 `mega_defense_selftest_ai_astar` 가 그 이유로 죽었고, 코드는 그 주에 한 줄도 바뀌지 않았다.

재생 쪽에는 `--ab` 를 넘기지 않는다. 팔은 파일이 소유하고, 재생 실행 파일은 명령줄 스위치를 만나면 무시하지 않고 **거절한다** — 재생 중에 팔 하나를 덮으면 기록과 다른 게임을 돌린 뒤 그 차이를 결정성 결함으로 보고하게 된다.

`FIXTURES_SETUP` 은 순서만 정하는 것이 아니다. 기록이 실패하면 재생은 **실행되지 않고** NOT RUN 으로 남는다. 없는 파일을 읽어 "디코드 실패"로 죽는 것보다 낫다 — 그 실패는 원인을 가린다.

## 9. 변이 — 시스템 두 줄을 맞바꾼다

이 게이트가 실제로 무엇을 잡는지 보려면, 잡아야 할 것을 만들어 넣어야 한다.

`GameWorld::Step` 의 여섯 시스템 순서는 2편이 고정한 것이고, 그 순서에는 이유가 붙어 있다. 두 줄을 맞바꾼다.

```cpp
        EnemyAdvanceSystem(dt);
        ProjectileSystem(dt);          // 원래는 TowerFireSystem 이 먼저다
        TowerFireSystem(dt);
```

원래 순서에서는 이번 틱에 발사된 발사체가 **같은 틱에** 한 걸음 나아간다. 맞바꾸면 다음 틱까지 제자리에 있다. 게임 규칙이 바뀌었고, 어떤 기존 테스트도 그것을 모른다.

기록해 둔 골든 셋으로 재생하면 이렇게 나온다.

```
> mega_defense.exe --verify-replay build/replays/solo.mgrp
[replay] result=DIVERGED ticks=1377/1377 frames=1377
         chain=0xecfa0daff65fe392/0x39fe9eb38805dab4 stride=1
[replay] chain diverged: last_agree=0 suspect=1 exact=yes
[replay] lockstep=OK frames=1377 stalls=0 fault=none tick=0

> mega_defense.exe --verify-replay build/replays/avatar.mgrp --host 47822
[replay] result=DIVERGED ticks=1339/1339 frames=1339
         chain=0xd6a4788b65889b9a/0xd2f808115e4d20ac stride=1
[replay] chain diverged: last_agree=51 suspect=52 exact=yes
[replay] lockstep=OK frames=1339 stalls=0 fault=none tick=0
```

**두 수가 다르다는 것이 이 진단의 값어치다.** 솔로 기록은 1틱, 아바타 기록은 52틱. 둘 다 그 기록에서 **첫 발이 나간 틱**이다. 솔로 스크립트는 0번째 프레임에 곧바로 타워를 세우니 첫 시뮬 틱에 이미 한 발이 나간다. 아바타 팔은 건설이 사거리 안으로 묶여 있어 먼저 목표 칸까지 걸어야 하고, 그 걸음이 51틱이다.

세 번째 줄도 읽어야 한다. `lockstep=OK` 다 — 아바타 서브시뮬은 갈리지 않았다. 시스템 순서는 float 권위 시뮬의 문제이고 정수 서브시뮬은 그 순서를 지나지 않기 때문이다. **한 실행에서 어느 층이 갈렸는지가 두 줄로 갈린다.**

복원하면 셋 다 `MATCH` 로 돌아온다. 체인 값도 원래 값 그대로다.

## 10. 갈리지 않은 셋

같은 실험을 세 번 더 했고, **셋 다 갈리지 않았다.** 이것이 이 편에서 가장 값어치 있는 관측이다.

| 변이 | 결과 |
|---|---|
| `StompSystem` ↔ `CleanupSystem` | MATCH |
| `DebrisSystem` ↔ `WinLoseSystem` | MATCH |
| `ProjectileSystem` ↔ `StompSystem` | MATCH |

셋 다 실제로 게임 규칙을 바꾸는 변경이다. 그런데 이 기록은 그것을 보지 못한다. 이유는 코드가 아니라 **기록의 내용**에 있다.

이 리플레이는 `map=classic` 팔이다. 저작된 무분기 회랑에는 바위가 없고, 바위가 없으면 발파가 없고, 발파가 없으면 잔해가 없다. `DebrisSystem` 은 1377틱 내내 빈 목록을 훑는다. `StompSystem` 은 두 번 돌지만(`stomps=2`) 그 두 번이 정리·발사체와 겹치는 상태를 만들지 않는다.

**게이트의 사정거리는 그 게이트가 도는 코드의 사정거리다.** 도달성 게이트가 "헤더가 제품에서 도달 가능한가"만 묻고 그 코드가 실행되는지는 묻지 못하는 것과 같은 종류의 한계이고, 여기서는 한 걸음 더 안쪽이다 — 실행되기는 하는데 **표현할 상태를 만들지 않는다.**

이 한계를 좁히는 방법은 골든을 늘리는 것이다. 절차 지형·플로우 필드·정예 트리 팔의 기록을 함께 두면 위 셋 중 둘은 잡힌다. 지금 세 팔로 시작한 것은 CI 시간 예산 때문이고, 그 예산이 판단이라는 사실을 적어 둔다.

한 가지 확실한 것은 체인 자체는 그런 갈림을 표현할 수 있다는 것이다. `test_replay_golden` 이 같은 기록의 500번째 프레임에서 키 하나를 뒤집어 그것을 확인한다.

```
  조작 한 번 뒤집기 → 갈린 틱 501 (마지막 일치 500)
```

500틱까지 마크가 전부 같고 501에서 갈린다. 체인은 한 틱의 한 비트에 반응한다. 반응하지 않은 것은 체인이 아니라 그 변이가 이 기록 안에서 아무 상태도 바꾸지 않았기 때문이다.

## 11. 등재부가 비었다

`docs/LAB_ONLY.md` 의 표가 0행이 되었다.

```
  제품 도달 합계 114개 / 랩 전용 등재 1개 / 근거 없는 미도달 0개
  LAB_ONLY.md:42 `mp/lockstep.hpp` - 이제 제품에서 도달 가능하다. 랩 전용 등재를 지워라.
```

게이트가 **등재가 남아 있다는 이유로** 빌드를 실패시켰다. 이 방향의 실패가 있어야 그 표가 현실보다 뒤처질 수 없다. 등재를 지우니 통과한다.

빈 표를 지우지는 않는다. 표가 없어지면 검사기는 등재할 곳이 없다고 판단하고, 그 순간 "도달 불가인데 근거도 없는" 제3의 상태가 다시 합법이 된다. **0행은 규칙이 끝났다는 뜻이 아니라 지금 그 상태에 있는 헤더가 없다는 뜻이다.**

## 12. 실행과 관측

**세 팔의 기록과 재생.** 프로세스가 여섯 번 뜨고 여섯 번 죽는다.

```
> ctest --test-dir build -C Release -R "record_|replay_" -V
110: [record] path=.../build/replays/solo.mgrp frames=1377 ticks=1377 marks=1377
     stride=1 chain=0xecfa0daff65fe392 avatar_frames=1377 mode=0 players=1 predict=0
110: [record] wave=6 towers=10 bytes=44541 switches=entity_store=aos,...,affinity=0
1/6 Test #110: mega_defense_record_solo .........   Passed    1.20 sec
111: [replay] result=MATCH ticks=1377/1377 frames=1377
     chain=0xecfa0daff65fe392/0xecfa0daff65fe392 stride=1
111: [replay] lockstep=OK frames=1377 stalls=0 fault=none tick=0
2/6 Test #111: mega_defense_replay_solo .........   Passed    1.12 sec
112: [record] ... chain=0x1f1a5cd4d501f351 avatar_frames=0 mode=1 players=2 predict=0
3/6 Test #112: mega_defense_record_host .........   Passed    1.15 sec
113: [replay] result=MATCH ticks=1377/1377 frames=1377
     chain=0x1f1a5cd4d501f351/0x1f1a5cd4d501f351 stride=1
113: [replay] lockstep=OK frames=0 stalls=1377 fault=none tick=0
4/6 Test #113: mega_defense_replay_host .........   Passed    1.15 sec
114: [record] ... frames=1339 chain=0xd6a4788b65889b9a avatar_frames=1339 predict=1
5/6 Test #114: mega_defense_record_avatar .......   Passed    0.98 sec
115: [replay] result=MATCH ticks=1339/1339 frames=1339
     chain=0xd6a4788b65889b9a/0xd6a4788b65889b9a stride=1
115: [replay] lockstep=OK frames=1339 stalls=0 fault=none tick=0
6/6 Test #115: mega_defense_replay_avatar .......   Passed    1.17 sec
100% tests passed out of 6
```

세 줄이 요점이다. 솔로는 `frames=1377 stalls=0`, 호스트는 `frames=0 stalls=1377`, 아바타는 `frames=1339 stalls=0`. **같은 검증기가 세 팔에서 정반대 결과를 내고 셋 다 통과한다.** 문지기가 열려 있어야 할 때 열리고 닫혀 있어야 할 때 닫힌다는 뜻이다.

아바타 팔이 1339틱인 것도 관측이다. 건설이 사거리 안으로 묶이면 스크립트가 목표 칸까지 걸어가야 하고, 그 걸음이 판의 길이를 바꾼다.

**단위 검사.**

```
> build\bin\Release\test_replay_golden.exe
[test] replay_golden
  정수 골든 chain=0x4c9447828fd222f4 f60=0x4b0c5329d6a17a62
          f120=0x8737cda3929a25d4 f240=0x7dcf42cf5671fe9c
  제품 리플레이 frames=900 chain=0x12d83bf6fa7a36d2 lockstep_frames=900
  조작 한 번 뒤집기 → 갈린 틱 501 (마지막 일치 500)
[test] replay_golden: 1921 checks, 0 failures
```

첫 줄의 네 수는 저장소 안에 **박아 둔** 값이다. 부동소수가 한 비트도 섞이지 않은 정수 서브시뮬의 결과라, 다른 컴파일러·다른 최적화·다른 머신에서도 같아야 한다. `mp::DetState::Step` 의 연산 순서를 바꾸면 이 네 수가 죽는다. 권위 시뮬(float)의 값은 박지 않는다 — 같은 바이너리·같은 빌드 구성 안에서의 재현만 보장되기 때문이고, 그 사실을 넘어 주장하지 않는다.

**변이.**

```
> (TowerFireSystem 과 ProjectileSystem 을 맞바꾼 뒤 재빌드)
[replay] result=DIVERGED ticks=1377/1377 frames=1377
         chain=0xecfa0daff65fe392/0x39fe9eb38805dab4 stride=1
[replay] chain diverged: last_agree=0 suspect=1 exact=yes
> (복원 뒤 재빌드)
[replay] result=MATCH ticks=1377/1377 frames=1377
         chain=0xecfa0daff65fe392/0xecfa0daff65fe392 stride=1
```

**전체.** CTest 는 117개 대상을 전수 통과한다. 이 편이 들여온 것은 `test_replay_golden` 과 기록·재생 여섯이고, 하나가 줄었다 — `tests/test_zzdiag.cpp` 는 단언이 하나도 없고 언제나 0으로 끝나는 진단용 스크래치였다. 글롭이 그것을 ctest 대상으로 세고 있었으므로 그 항목의 green 은 아무것도 뜻하지 않았다.

---

## 결론

이 편의 장치는 새 알고리즘이 아니다. `StateHash` 도 `DetState::Checksum` 도 이미 있었고, `mp::Lockstep` 은 첫 주부터 저장소에 있었다. 바뀐 것은 **그 셋을 언제 부르는가**뿐이다.

한 번 부르면 상태를 말하고, 틱마다 부르면 역사를 말한다. 그 차이가 "갈렸다"와 "1377틱 중 몇 번째에서 갈렸다"의 차이다.

측정에서 나온 것 셋.

**결정성은 조작을 고정한다고 얻어지지 않는다.** 팔·시계·프로필 저장소 셋을 고정하기 전까지 이 검증기는 갈리지 않아야 할 자리에서 갈렸다. 셋 다 게임 로직이 아니라 게임 **바깥**의 입력이었고, 그래서 아무도 그것을 프레임의 입력으로 세지 않고 있었다.

**같은 파일이 두 실행에 공유되면 그 파일이 입력이다.** 타워 12개와 10개의 차이가 `.wal` 하나였다는 것은, 프로필 저장소가 이 게임의 조용한 전역 상태였다는 뜻이다. 리플레이가 그것을 드러냈다.

**변이가 잡히지 않으면 게이트가 아니라 골든을 보라.** 시스템 순서를 바꾼 변이 넷 중 셋이 통과했다. 체인은 틱 하나의 비트 하나에 반응하는데도 그랬다. 잡히지 않은 이유는 그 기록이 잔해도 발파도 만들지 않기 때문이다.

현재 상태의 한계 넷.

**골든이 `map=classic` 세 팔뿐이다.** 절차 지형·플로우 필드·정예 트리 팔은 기록되지 않고, 그 팔에서만 도는 코드의 결정성은 여전히 A/B 대조로만 검사된다. CI 시간 예산이 이유이고, 그것은 기술적 한계가 아니라 판단이다.

**권위 시뮬의 골든 값은 저장소에 없다.** float 재현은 같은 컴파일러·같은 플래그 안에서만 보장되므로 값을 박으면 다른 컴파일러에서 거짓 실패가 난다. 실측으로는 프로세스 경계와 **계측 경계**까지 넘었다 — AddressSanitizer 로 다시 빌드한 실행 파일이 같은 기록을 재생해 `0xecfa0daff65fe392` 를 그대로 낸다. 그러나 다른 컴파일러는 이 저장소가 돌려 본 적이 없고, 크로스 머신 결정성으로 **검사된** 것은 정수 서브시뮬 쪽 골든 네 수까지다.

**멈췄다가 전진하는 자취를 두 번째 재생기가 다루지 못한다.** 지금 세 팔에서는 나오지 않지만, 실제 원격 피어가 붙은 기록을 남기게 되면 곧바로 나온다. 그때는 `Rollback` 이 실제로 쓴 입력 표를 기록에 함께 넣어야 한다.

**한 `Step` 이 한 시뮬 틱이라는 관계에 기대고 있다.** 헤드리스에서는 참이고 단위 검사가 그것을 단언하지만, 창 실행은 한 프레임에 최대 다섯 틱을 몰아 돌 수 있다. 그 실행을 기록하려면 프레임과 틱을 서로 다른 좌표로 세는 포맷이 필요하다.
