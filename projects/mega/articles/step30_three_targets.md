# 30. 한 타깃만 빌드하면 이식성은 주장이다

## 들어가며

`platform/` 아래 헤더 넷이 등재부에 남아 있다. 등재 사유가 넷 다 같은 문장으로 끝난다 — "단일 타깃만 빌드하는 현재는", "같은 아키텍처 두 프로세스 사이만 오가고", "현 제품은 규모가 고정이라".

이 축은 관측 장치가 없으면 검사할 수 없다. 리틀엔디안 머신 하나에서 빌드하고 실행하는 한, 직렬화가 옳은 이유가 "바이트 순서를 명시했기 때문"인지 "머신이 마침 리틀엔디안이기 때문"인지 구분되지 않는다. 워커 수가 결과를 바꾸지 않는다는 문장도, 워커 수가 언제나 같으면 검사되지 않는다.

**CI 를 Windows · Linux · WASM 셋으로 늘리는 것이 그 관측 장치다.** 그리고 그 셋에서 무엇을 봐야 하는지를 정하는 것이 이 편의 일이다.

| 헤더 | 하는 일 |
|---|---|
| `platform/detect.hpp` | OS · 아키텍처 · 엔디안을 상위 코드가 질의할 수 있는 값으로 |
| `platform/byteorder.hpp` | 직렬화를 머신 엔디안과 무관하게 |
| `platform/capability.hpp` | 능력이 정하는 것과 **정하지 않는 것**의 경계 |
| `platform/console.hpp` | 한국어 진단이 깨지지 않게 |

도달점의 수치.

- 고정 값 표 18개를 인코딩한 **74바이트가 손으로 적은 골든 벡터와 정확히 일치**한다.
- `wire::Writer` 의 페이로드 47바이트가 `AppendLE*` 의 출력과 **바이트 동일** — 배선이 시프트를 갈아 끼워도 나가는 바이트가 안 변한다는 계약.
- PC · Mobile · Console · Web 네 프로파일로 2000틱씩 돌린 권위 해시가 전부 `0x75e3d3b91beb7537`. **워커 수는 4/4/4/0 으로 다르다.**
- 같은 해시가 `scale` 을 바꾸면 갈라진다: `0xeaaf3af9fbd365f8` 대 `0x4c34a02237ac6ba3`.
- 콘솔 코드페이지를 437로 떨어뜨린 뒤 `EnableUtf8Console()` 이 65001로 되돌린다. 그 호출을 지우면 437에 머물고 검사 4건이 죽는다.

---

## 목차
1. [#ifdef 안에 갇힌 사실](#1-ifdef-안에-갇힌-사실)
2. [열거 값이 곧 와이어 코드다](#2-열거-값이-곧-와이어-코드다)
3. [시프트가 옳은 이유와 머신이 옳은 이유](#3-시프트가-옳은-이유와-머신이-옳은-이유)
4. [골든 벡터 — 손으로 적은 74바이트](#4-골든-벡터--손으로-적은-74바이트)
5. [교체가 무증상이 되지 않게 하는 단언](#5-교체가-무증상이-되지-않게-하는-단언)
6. [능력이 정하는 것과 정하지 않는 것](#6-능력이-정하는-것과-정하지-않는-것)
7. [능력이 예산을 이긴다](#7-능력이-예산을-이긴다)
8. [해시가 무엇에 민감해야 하는가](#8-해시가-무엇에-민감해야-하는가)
9. [읽히지 않는 진단은 없는 진단이다](#9-읽히지-않는-진단은-없는-진단이다)
10. [변이 하나](#10-변이-하나)
11. [실행과 관측](#11-실행과-관측)

---

## 1. #ifdef 안에 갇힌 사실

지금 OS 분기는 `platform/` 구현 파일 안의 `#ifdef` 로만 존재한다. `platform/paths.cpp` 는 `%LOCALAPPDATA%` 와 `$XDG_DATA_HOME` 을 안다. `platform/window.hpp` 는 GDI 를 안다. 그런데 그 사실이 그 파일 밖으로 나오지 않는다.

그래서 상위 코드가 "나는 지금 어느 OS 에 있는가"를 말할 수 없다. 6편의 서버가 시작 로그에 `[server] id=… udp=…` 를 찍지만 그 로그에 OS 가 없고, 입장 승인 로그에도 없다. Windows 클라이언트가 Linux 서버에 붙었다는 사실이 어디에도 남지 않는다.

`platform/detect.hpp` 는 그 사실을 값으로 꺼낸다. 전처리기가 아는 것을 `constexpr` 함수가 돌려주고, 상위 코드는 `#ifdef` 대신 그 값을 읽는다.

```
  this machine: Windows/x86-64/LE
```

## 2. 열거 값이 곧 와이어 코드다

이 문자열이 로그에만 있으면 값을 꺼낼 이유가 절반뿐이다. 나머지 절반은 **핸드셰이크에 싣는 것**이다.

싣는 순간 열거는 프로토콜의 일부가 된다. `OS::Windows` 가 0이라는 사실이 와이어에 나가고, 누군가 `enum class OS` 에 새 값을 **앞에** 끼워 넣으면 그 순간 모든 옛 클라이언트가 자기를 다른 OS 라고 말하게 된다.

그 사실을 컴파일 시간에 못박는다.

<!-- snippet: include/platform/detect.hpp@os-wire-code -->
```cpp
static_assert(static_cast<uint8_t>(OS::Windows) == 0 && static_cast<uint8_t>(OS::Unknown) == 6,
              "OS 열거 값은 입장 핸드셰이크의 와이어 코드다");
static_assert(static_cast<uint8_t>(Arch::X86_64) == 0 && static_cast<uint8_t>(Arch::Unknown) == 5,
              "Arch 열거 값은 입장 핸드셰이크의 와이어 코드다");

constexpr uint8_t OSCode(OS os) { return static_cast<uint8_t>(os); }
constexpr OS OSFromCode(uint8_t code) {
    return code <= static_cast<uint8_t>(OS::Unknown) ? static_cast<OS>(code) : OS::Unknown;
}
constexpr uint8_t ArchCode(Arch a) { return static_cast<uint8_t>(a); }
constexpr Arch ArchFromCode(uint8_t code) {
    return code <= static_cast<uint8_t>(Arch::Unknown) ? static_cast<Arch>(code) : Arch::Unknown;
}
```

`static_assert` 가 양 끝을 잡는다. 앞에 끼우면 `Windows == 0` 이 깨지고, 중간에 끼우면 `Unknown == 6` 이 깨진다. 새 플랫폼은 `Unknown` 앞에 붙이면 되고 — 그러면 `Unknown` 의 값이 오르므로 단언도 함께 고쳐야 한다. 고쳐야 한다는 사실이 컴파일 에러로 보이는 것이 요점이다.

`OSFromCode` 는 모르는 코드를 `Unknown` 으로 흡수한다. 와이어를 건너온 바이트를 그대로 `static_cast` 하면 열거 범위 밖 값이 만들어지고, 그것을 `switch` 에 넣는 순간 정의되지 않은 동작이다.

## 3. 시프트가 옳은 이유와 머신이 옳은 이유

4편의 `wire::Writer` 는 이렇게 쓴다.

```cpp
void U16(uint16_t value) {
    bytes_.push_back(static_cast<uint8_t>(value));
    bytes_.push_back(static_cast<uint8_t>(value >> 8));
}
```

**이 코드는 옳다.** 시프트는 값의 산술이지 메모리 배치가 아니므로, 빅엔디안 머신에서도 같은 바이트가 나온다. 문제는 그 옳음이 검사되지 않는다는 것이다. 리틀엔디안 머신 하나에서는 이 코드와 `memcpy(buffer, &value, 2)` 가 구분되지 않는다.

구분하는 방법은 둘이다. 빅엔디안 머신에서 돌리거나, **결과 바이트를 미리 적어 두고 대조하거나.** 앞의 것은 CI 를 늘리는 일이고 뒤의 것은 지금 할 수 있다.

`platform/byteorder.hpp` 가 그 자리를 소유한다. 이미 있던 `WriteLE*`/`ReadLE*` 는 포인터를 받는데, 직렬화기는 미리 크기를 아는 버퍼가 아니라 **자라는 바이트 열**에 쓴다. 그래서 이어 붙이는 형태를 준다.

<!-- snippet: include/platform/byteorder.hpp@append-le -->
```cpp
inline void AppendLE8(std::vector<uint8_t>& out, uint8_t v) { out.push_back(v); }
inline void AppendLE16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}
inline void AppendLE32(std::vector<uint8_t>& out, uint32_t v) {
    AppendLE16(out, static_cast<uint16_t>(v));
    AppendLE16(out, static_cast<uint16_t>(v >> 16));
}
inline void AppendLE64(std::vector<uint8_t>& out, uint64_t v) {
    AppendLE32(out, static_cast<uint32_t>(v));
    AppendLE32(out, static_cast<uint32_t>(v >> 32));
}
```

포인터 산술을 호출부에 시키지 않는 것이 요점이다. `WriteLE32(bytes.data() + offset, v)` 를 여기저기 적게 하면 그 자리마다 오프셋 계산이 생기고, 오프셋 계산이 있는 곳에는 범위 실수가 있다.

## 4. 골든 벡터 — 손으로 적은 74바이트

`tests/test_golden_packet.cpp` 의 골든은 코드가 만든 것이 아니라 손으로 적은 배열이다.

```cpp
static const uint8_t kGolden[] = {
    // uint16
    0x00, 0x00,  0x01, 0x00,  0xFF, 0x00,  0x00, 0x01,
    0x34, 0x12,  0xEF, 0xBE,  0xFF, 0xFF,
    // uint32
    0x00, 0x00, 0x00, 0x00,  0x01, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00,  0x00, 0xFF, 0x00, 0x00,
    0x78, 0x56, 0x34, 0x12,  0xEF, 0xBE, 0xAD, 0xDE,
    0xFF, 0xFF, 0xFF, 0xFF,
    // uint64
    // ...
};
```

**코드로 기대값을 만들면 안 되는 이유.** `for (v : values) expected.push_back(v & 0xFF), push_back(v >> 8)` 로 기대값을 만들면 그것은 구현의 사본이고, 구현과 기대가 같은 방식으로 틀릴 수 있다. 손으로 적은 바이트는 사람이 "리틀엔디안이란 이런 것이다"를 한 번 선언한 것이다.

값 표는 경계를 고른다. `0x0000` 과 `0xFFFF`, `0x00FF` 와 `0x0100` (바이트 경계를 넘는 최소 쌍), `0x1234` 와 `0xBEEF` (양쪽 니블이 다른 값). `0xFFFFFFFF` 는 부호 확장 사고가 있으면 여기서 걸린다.

읽기도 같은 표로 왕복한다. 그리고 **범위를 벗어난 읽기가 값을 건드리지 않는지**까지 본다 — `ReadLE32At` 가 실패했는데 `value` 가 반쯤 채워져 있으면 호출자는 실패를 무시해도 되는 것처럼 착각한다.

## 5. 교체가 무증상이 되지 않게 하는 단언

배선이 할 일은 `wire::Writer::U16` 안의 두 줄을 `AppendLE16(bytes_, value)` 한 줄로 바꾸는 것이다. 나가는 바이트는 하나도 안 변해야 한다.

그 계약을 미리 못박아 둔다.

```cpp
wire::Writer writer(wire::PacketType::Hello);
for (uint16_t v : kValues16) writer.U16(v);
for (uint32_t v : kValues32) writer.U32(v);
writer.U8(0x5A);
writer.Raw("mega");
const std::vector<uint8_t> packet = writer.Finish();

// 헤더 8바이트(magic · version · type · payload 길이) 뒤가 페이로드다.
CHECK(packet.size() > 8);
std::vector<uint8_t> expected;
for (uint16_t v : kValues16) AppendLE16(expected, v);
// ...
```

헤더 8바이트를 건너뛰고 페이로드만 비교하는 이유는 **버전 바이트** 때문이다. `wire::kVersion` 은 프로토콜이 바뀔 때마다 오르므로, 전체 패킷을 골든으로 잡으면 이 검사가 프로토콜 변경마다 깨진다. 깨지는 검사는 곧 지워지는 검사다. 그래서 값이 변하는 자리는 **구조로** 확인한다 — magic 이 리틀엔디안으로 읽히는가, `packet[5]` 가 타입인가, `packet[6..7]` 의 길이가 실제 페이로드 크기와 같은가.

```
  wire::Writer 페이로드 47바이트가 AppendLE 와 동일
```

## 6. 능력이 정하는 것과 정하지 않는 것

여기가 이 편에서 가장 중요한 경계다.

플랫폼 프로파일은 규모 파라미터를 정한다. 그런데 **어떤 규모**를 정하는지가 문제다.

| 능력이 정한다 | 능력이 정하지 않는다 |
|---|---|
| 잡 워커 수 | 적 상한 |
| 프레임 아레나 크기 | 웨이브 구성 |
| 슬랩 크기 | 타워 사거리·피해 |
| 파티클 상한 | 발사체 상한 |
| 오디오 보이스 수 | 물리 솔버 반복 횟수 |
| 렌더 LOD | 경로 탐색 방식 |

오른쪽 열을 능력이 건드리는 순간 모바일 클라이언트와 PC 호스트가 서로 다른 게임을 돌게 된다. 그러면 "같은 시드 + 같은 입력 = 같은 상태 해시"가 거짓이 되고, desync 탐지도 리플레이 검증도 성립하지 않는다. 성능 이야기가 아니라 **정의**의 문제다.

<!-- snippet: include/platform/capability.hpp@capability-budget -->
```cpp
inline RuntimeBudget BudgetFor(const PlatformProfile& profile) {
    RuntimeBudget budget;
    // 스레드 능력이 없으면 워커는 0이다. maxThreads 값이 무엇이든 상관없다 —
    // 능력이 예산을 이긴다.
    budget.workerCap = profile.Has(Capability::Threads) ? profile.maxThreads : 0;
    if (budget.workerCap < 0) budget.workerCap = 0;

    // 나머지는 메모리 예산에서 비례로 끌어온다. 절대값을 프로파일마다 손으로
    // 적으면 새 플랫폼이 생길 때마다 다섯 줄을 더 적게 된다.
    const int budgetMB = profile.memoryBudgetMB > 0 ? profile.memoryBudgetMB : 512;
    budget.frameArenaKB  = budgetMB >= 4096 ? 1024 : (budgetMB >= 1024 ? 512 : 256);
    budget.blobSlabKB    = budgetMB >= 4096 ? 4096 : (budgetMB >= 1024 ? 2048 : 512);
    budget.particleCap   = budgetMB >= 4096 ? 2048 : (budgetMB >= 1024 ? 1024 : 256);
    budget.audioVoiceCap = budgetMB >= 1024 ? 32 : 8;
    budget.renderLod     = budgetMB >= 4096 ? 0 : (budgetMB >= 1024 ? 1 : 2);
    if (!profile.Has(Capability::GPU)) budget.renderLod = 2;
    return budget;
}
```

`RuntimeBudget` 이라는 별도 구조체를 두는 것 자체가 경계다. 프로파일에서 값을 직접 꺼내 쓰게 두면 누군가 `profile.memoryBudgetMB` 로 적 상한을 계산하는 코드를 쓸 수 있고, 그것을 막을 방법이 없다. 예산 구조체에는 권위 규모에 해당하는 필드가 **아예 없다**.

## 7. 능력이 예산을 이긴다

Web 프로파일이 자기모순이었다. `Capability::Threads` 를 갖지 않는데 `maxThreads = 4` 라고 적혀 있었다. 두 값 중 어느 쪽을 믿어야 하는지가 호출부마다 달라진다.

`maxThreads` 를 1로 고치고, 파생을 능력에 종속시켰다. `Has(Threads)` 가 거짓이면 `workerCap` 은 0이다 — `maxThreads` 값이 무엇이든.

그리고 요청과 능력을 합치는 규칙을 명시한다.

```cpp
// 요청한 워커 수를 능력 안으로 자른다. 자르기만 하고 올리지는 않는다 —
// `--ab jobs=8` 로 명시한 사람이 8보다 많은 워커를 얻는 일은 없어야 한다.
inline int ClampWorkers(int requested, const PlatformProfile& profile) {
    const int cap = BudgetFor(profile).workerCap;
    if (requested < 0) return 0;
    return requested < cap ? requested : cap;
}
```

**자르기만 하고 올리지 않는다.** `--ab jobs=2` 로 명시한 사람이 16코어 머신에서 16개 워커를 얻으면, 11편이 세운 A/B 가 무너진다. 능력은 상한이지 목표가 아니다.

네 프로파일의 예산이 실제로 갈린다.

```
  프로파일별 예산 (workers/arenaKB/slabKB/particles/voices/lod)
    PC       16  1024  4096  2048  32 0
    Mobile    8   512  2048  1024  32 1
    Console   8  1024  4096  2048  32 0
    Web       0   256   512   256   8 2
```

## 8. 해시가 무엇에 민감해야 하는가

"네 프로파일의 권위 해시가 같다"는 단언은 그 자체로는 아무것도 증명하지 못한다. 해시가 아무것도 안 보면 언제나 같기 때문이다.

그래서 `tests/test_capability_invariant.cpp` 는 두 방향을 함께 잰다.

**같아야 하는 것.** 네 프로파일로 `ClampWorkers(4, profile)` 만큼 워커를 띄우고 2000틱씩 돌린 `GameWorld::StateHash()`. Web 은 워커 0(직렬)으로, 나머지는 4로 돈다.

**달라야 하는 것.** 같은 프로파일에서 `scale` 만 바꾸면 해시가 갈라진다. 이 단언이 있어야 위의 "전부 같다"가 의미를 갖는다.

```
  PC       workers= 4  hash=0x75e3d3b91beb7537
  Mobile   workers= 4  hash=0x75e3d3b91beb7537
  Console  workers= 4  hash=0x75e3d3b91beb7537
  Web      workers= 0  hash=0x75e3d3b91beb7537
  scale=large 0xeaaf3af9fbd365f8  vs  scale=small 0x4c34a02237ac6ba3
```

윗 네 줄이 11편의 워커 결정성 위에 서 있다는 점도 읽을 만하다. `jobs=0` 과 `jobs=4` 가 같은 세계를 낸다는 것은 이미 검사된 사실이고, 이 편은 그것을 **능력 프로파일이라는 경로로** 다시 지난다. 능력이 워커 수만 바꾸는 한 이 해시는 안전하다.

## 9. 읽히지 않는 진단은 없는 진단이다

이 저장소의 실패 메시지는 전부 한국어다.

```
[content] balance.script 거부, 기본 구성으로 진행: …
[content] boss.pose 거부, 내장 클립으로 진행: …
[admission] player=… endpoint=…
```

소스도 UTF-8 이고 프로그램이 내보내는 바이트도 UTF-8 인데, Windows 콘솔의 기본 출력 코드페이지가 CP949 나 437 이면 이 줄들이 깨진다. 깨진 실패 메시지는 읽히지 않고, 읽히지 않는 진단은 없는 진단이다. 그래서 이것은 표시 취향이 아니라 **진단의 도달성** 문제다.

<!-- snippet: include/platform/console.hpp@utf8-console -->
```cpp
inline bool EnableUtf8Console() {
    if (GetConsoleOutputCP() == 0u) return true;     // 콘솔 없음 = 할 일 없음
    SetConsoleOutputCP(kUtf8CodePage);
    return GetConsoleOutputCP() == kUtf8CodePage;
}
```

콘솔이 없는 프로세스(파이프로 리다이렉트된 CI 러너, GUI 서브시스템)에서는 설정할 대상이 없다. 그 상태는 실패가 아니므로 참을 돌려준다.

검사는 세 층으로 나눈다.

**컴파일러.** 소스의 `"거부"` 가 실행 파일에 UTF-8 6바이트(`EA B1 B0 EB B6 80`)로 들어갔는가. CP949 로 좁혀졌다면 4바이트가 되어 이 비교가 깨진다. MSVC 에서 `/utf-8` 이 빠지면 정확히 그 일이 일어난다.

**런타임.** 코드페이지를 437로 **일부러 떨어뜨린 뒤** 호출한다. 이미 65001인 콘솔에서만 재면 "아무것도 안 하는 구현"과 구분되지 않는다.

**내용.** 제품 코드가 만들어 내는 진단이 유효한 UTF-8 인가. 리터럴은 첫 층이 확인했지만, 진단은 리터럴과 런타임 값(파일명·줄번호·계정 id)을 이어 붙여 만들어진다 — 그 이음매에서 잘린 바이트가 섞일 수 있다. 27·28편의 파서에서 실제 진단 7건을 뽑아 전부 검사한다.

배선은 두 줄이다. `RunDefense` 와 `RunDefenseServer` 의 첫 줄에 `platform::EnableUtf8Console()` 을 놓는다. 다른 자리가 아니라 진입점인 이유는, 그 뒤의 어떤 코드도 자기가 첫 진단을 찍는 코드인지 모르기 때문이다.

**리다이렉트한 실행에서의 실측.** 밸런스 값을 범위 밖으로 고친 에셋을 서버에 물리고, 표준 출력을 파이프에 흘린다.

```
> mega_defense_server --port 27193 --run-seconds 1 --assets ./badassets 2>&1 | cat -v
[security] using the local development token secret; do not expose this server
[content] balance.script M-jM-1M-0M-kM-6M-^@, M-jM-8M-0M-kM-3M-8 ... (바이트 그대로)
[platform] Windows/x86-64/LE profile=PC
[patch] version=1 hash=0x5099B2438EC0C639 assets=4 cooked=4
```

`cat -v` 가 보여 주는 바이트가 `EA B1 B0` — UTF-8 의 "거"다. CP949 였다면 두 바이트(`B0 C5`)로 나왔을 것이고, 이 저장소의 진단은 전부 그 두 상태 중 하나로 갈린다. 파이프에는 콘솔이 없으므로 `EnableUtf8Console` 자체는 아무것도 하지 않는다 — 여기서 확인되는 것은 **컴파일러가 소스를 UTF-8 로 넣었다**는 층이다. 세 층 중 나머지 둘(런타임 코드페이지 전환, 진단 내용의 유효성)은 `test_console_encoding` 이 본다.

```
> test_console_encoding.exe
  console cp: 949 -> (437) -> 65001
  진단 7건 전부 UTF-8 유효. 예: events:1: end 는 start 보다 커야 한다
[test] console_encoding: 40 checks, 0 failures
```

첫 줄의 `949` 가 이 머신 콘솔의 기본값이다. 배선이 없으면 위 진단이 저 코드페이지로 나가 깨진다.

**플랫폼 한 줄도 진입에서 찍는다.**

```
[platform] Windows/x86-64/LE profile=PC
```

이 줄이 서버와 클라이언트 양쪽 시작 로그에 있고, 같은 세 값(OS·아키텍처·엔디안)이 입장 인사와 승인 패킷에도 실린다. 그래서 "어느 OS 클라이언트가 어느 OS 서버에 붙었는가"가 로그 한 줄로 답해진다 — 클라이언트 쪽은 `[admission] grant … server=Windows/x86-64` 로 상대를 적는다.

배선하면서 하나를 고쳤다. 에셋 디렉터리가 통째로 없을 때도 파이프라인이 배포 목록 하나만 등록해 **유효한 콘텐츠 해시를 냈다.** 에셋이 빠진 배포가 발행 가능한 버전이 되면 그 버전에 대한 입장 검사는 아무것도 지키지 않는다. 원본이 0개면 0(미발행)을 돌려주도록 바꿨다.

배선 뒤의 시작 로그다.

```
> mega_defense --selftest
[platform] Windows/x86-64/LE profile=PC workers=16 arena=1024KB voices=32 lod=0
```

네 수가 전부 `BudgetFor(ProfileFor(CurrentOS()))` 에서 나온다. `workers=16` 은 **상한**이지 실제 워커 수가 아니다 — `--ab jobs=N` 이 그 아래에서만 유효하고, 자르기만 하며 올리지는 않는다.

```cpp
        const int requestedWorkers = platform::ClampWorkers(options_.switches.jobs,
                                                            platformProfile_);
```

`arena=1024KB` 는 프레임 스크래치 아레나의 용량이고, 예전에는 `256 * 1024` 라는 리터럴이었다. 리터럴이 프로파일에서 오는 값으로 바뀌었지만 **권위 결과는 바뀌지 않아야 한다** — 아레나는 프레임 수명 스크래치이고, 넘치면 malloc 으로 떨어지도록 이미 되어 있다.

그 성질이 이 편의 경계식이다.

```
> test_capability_invariant.exe
  PC       workers= 4  hash=0x75e3d3b91beb7537
  Mobile   workers= 4  hash=0x75e3d3b91beb7537
  Console  workers= 4  hash=0x75e3d3b91beb7537
  Web      workers= 0  hash=0x75e3d3b91beb7537
  scale=large 0xeaaf3af9fbd365f8  vs  scale=small 0x4c34a02237ac6ba3
```

네 프로파일의 권위 해시가 같고, Web 은 워커가 0이다(Threads 능력이 없으므로 `maxThreads` 값과 무관하게 0). 마지막 줄이 대조군이다 — **규모를 바꾸는 스위치는 해시를 실제로 바꾼다.** 그 줄이 없으면 "해시가 같다"는 단언은 해시 함수가 상수를 돌려주는 구현에서도 통과한다.

## 10. 변이 하나

**`EnableUtf8Console` 에서 `SetConsoleOutputCP` 호출을 지운다.**

```cpp
inline bool EnableUtf8Console() {
    if (GetConsoleOutputCP() == 0u) return true;
    return GetConsoleOutputCP() == kUtf8CodePage;
}
```

```
[test] console_encoding
  FAIL tests\test_console_encoding.cpp:71  CHECK(EnableUtf8Console())
  console cp: 65001 -> (437) -> 437
  FAIL tests\test_console_encoding.cpp:79  CHECK_EQ(after, kUtf8CodePage)
  FAIL tests\test_console_encoding.cpp:81  CHECK(EnableUtf8Console())
  FAIL tests\test_console_encoding.cpp:82  CHECK_EQ(ConsoleOutputCodePage(), kUtf8CodePage)
[test] console_encoding: 40 checks, 4 failures
```

`console cp: 65001 -> (437) -> 437` 이 그 자체로 진단이다. 테스트가 일부러 437로 떨어뜨렸고, 함수가 되돌리지 못했다.

이 변이를 처음에 시도했을 때 **잡히지 않았다.** 개발 콘솔이 이미 65001 이라 "떨어뜨린다"는 단계가 없었고, 함수가 아무것도 안 해도 결과가 65001 이었기 때문이다. 437로 떨어뜨리는 두 줄을 넣고 나서야 변이가 죽었다. 검사가 잡을 수 있는 상태를 **만들어 놓고** 재야 한다는 것의 실례다.

복원 뒤 40 검사에 실패 0.

## 11. 실행과 관측

**골든 패킷.**

```
> build\bin\Release\test_golden_packet.exe
[test] golden_packet
  골든 벡터 74바이트 일치
  wire::Writer 페이로드 47바이트가 AppendLE 와 동일
  CurrentEndian=LE  probe[0]=0x04  Windows/x86-64/LE
[test] golden_packet: 99 checks, 0 failures
```

세 번째 줄이 `CurrentEndian()` 과 실제 바이트 배치의 대조다. `union { uint32_t word; uint8_t byte[4]; }` 에 `0x01020304` 를 넣고 `byte[0]` 을 본다. `constexpr` 판정(`std::endian::native`)과 런타임 관측이 같아야 한다 — 컴파일러가 틀릴 일은 없지만, 이 대조가 없으면 `CurrentEndian()` 을 상수로 고정한 구현이 통과한다.

같은 테스트가 `WriteLE32` 와 `memcpy` 의 차이도 명시한다.

```cpp
uint8_t raw[4];
const uint32_t value = 0x01020304u;
std::memcpy(raw, &value, sizeof(raw));
CHECK_EQ(raw[0] == buffer[0], littleOnThisMachine);
```

이 머신에서는 둘이 같다. 빅엔디안 타깃에서는 다르다. 그 차이가 `platform/byteorder.hpp` 가 존재하는 이유 전부다.

**능력 불변식.**

```
> build\bin\Release\test_capability_invariant.exe
[test] capability_invariant
  프로파일별 예산 (workers/arenaKB/slabKB/particles/voices/lod)
    PC       16  1024  4096  2048  32 0
    Mobile    8   512  2048  1024  32 1
    Console   8  1024  4096  2048  32 0
    Web       0   256   512   256   8 2
  this machine: Windows/x86-64/LE
  PC       workers= 4  hash=0x75e3d3b91beb7537
  Mobile   workers= 4  hash=0x75e3d3b91beb7537
  Console  workers= 4  hash=0x75e3d3b91beb7537
  Web      workers= 0  hash=0x75e3d3b91beb7537
  scale=large 0xeaaf3af9fbd365f8  vs  scale=small 0x4c34a02237ac6ba3
[test] capability_invariant: 31 checks, 0 failures
```

**콘솔 인코딩.**

```
> build\bin\Release\test_console_encoding.exe
[test] console_encoding
  console cp: 65001 -> (437) -> 65001
  진단 7건 전부 UTF-8 유효. 예: events:1: end 는 start 보다 커야 한다
[test] console_encoding: 40 checks, 0 failures
```

마지막 줄의 진단이 27편 파서가 만든 것이라는 점이 요점이다. 인코딩 검사가 인위적인 리터럴이 아니라 **제품이 실제로 찍는 문자열**을 본다.

## 12. 제품 실측과 남은 절반

**골든 74바이트.**

```
> test_golden_packet.exe
  골든 벡터 74바이트 일치
  wire::Writer 페이로드 47바이트가 AppendLE 와 동일
  CurrentEndian=LE  probe[0]=0x04  Windows/x86-64/LE
[test] golden_packet: 99 checks, 0 failures
```

둘째 줄이 이 편의 배선 계약이다. `wire::Writer` 안의 손으로 쓴 시프트를 `platform::AppendLE*` 로 갈아 끼웠고, **나가는 바이트가 하나도 변하지 않았다.** 그 동치가 검사되지 않았다면 그 교체는 무증상 회귀가 될 수 있었다.

```cpp
    void U8(uint8_t value) { platform::AppendLE8(bytes_, value); }
    void U16(uint16_t value) { platform::AppendLE16(bytes_, value); }
    void U32(uint32_t value) { platform::AppendLE32(bytes_, value); }
    void U64(uint64_t value) { platform::AppendLE64(bytes_, value); }
```

읽기 쪽은 형태가 다르다. 경계 검사를 **앞에 남긴 채** 읽기만 옮겼다 — 검사를 잃으면 잘린 패킷이 버퍼 밖을 읽는다.

**남은 절반은 여전히 남아 있다.** 이 저장소가 지금 돌 수 있는 타깃은 하나(Windows/x86-64/LE)이고, 위 74바이트는 그 한 타깃의 결과다. 빅엔디안 타깃에서 같은 바이트가 나온다는 것은 코드의 성질(자리마다 시프트한다)에서 따라 나오지만, 그것을 **측정한** 것은 아니다.

측정과 논증의 차이를 이 편이 계속 말해 온 이유가 여기 있다. `memcpy` 구현과 `WriteLE32` 구현은 이 머신에서 같은 74바이트를 낸다. 둘을 가르는 것은 다음 타깃이지 이 타깃이 아니다. 그래서 검사는 그 차이를 **간접적으로** 잡아 둔다.

```cpp
    uint8_t raw[4];
    const uint32_t value = 0x01020304u;
    std::memcpy(raw, &value, sizeof(raw));
    CHECK_EQ(raw[0] == buffer[0], littleOnThisMachine);
```

`memcpy` 결과가 `WriteLE32` 결과와 같은 것은 **이 머신이 리틀엔디안일 때에만**이라는 관계 자체를 단언한다. 빅엔디안 CI 가 들어오는 날 이 줄은 그대로 통과하고, 골든 벡터 쪽이 진짜 답을 낸다.

CTest 는 111개 대상을 전수 통과한다. 이 편이 들여온 검사는 `test_golden_packet`(골든 벡터 · Writer 동치 · 엔디안 대조) · `test_capability_invariant`(네 프로파일 권위 해시 동일) · `test_console_encoding`(세 층)이고, `test_platform` · `test_platform_caps` 가 그 아래에 있다.

---

## 결론

이 편의 헤더 넷은 성격이 다르지만 결함의 모양이 같다. **한 타깃에서는 옳고 그른 구현이 같은 결과를 낸다.** 시프트와 `memcpy`, 능력 프로파일 넷과 하나, 65001 로 바꾸는 구현과 아무것도 안 하는 구현 — 리틀엔디안 Windows 머신 하나에서는 셋 다 구분되지 않는다.

구분하는 방법이 셋 있고, 이 편은 그중 둘을 썼다.

**결과를 미리 적어 두기.** 골든 벡터는 "리틀엔디안이란 이런 것이다"를 사람이 한 번 선언한 것이고, 구현이 그 선언과 다르면 어느 머신에서든 즉시 걸린다.

**차이를 만들어 놓고 재기.** 콘솔을 437로 떨어뜨리고, 프로파일 넷을 실제로 돌린다. 차이가 없는 상태에서 재면 무엇도 증명되지 않는다.

세 번째 — **다른 머신에서 돌리기** — 는 CI 를 늘려야 한다. 그때까지 이 편의 주장 중 "빅엔디안에서도 같다"는 절반은 검사가 아니라 논증이다. 그 사실을 적어 두는 것이 지금 할 수 있는 최선이다.

측정에서 나온 것 셋.

**변이가 잡히지 않으면 테스트가 아니라 조건을 고쳐야 한다.** 콘솔 변이는 처음에 통과했다. 함수가 옳아서가 아니라 이미 목표 상태였기 때문이다.

**"전부 같다"는 단언은 "달라야 할 때 다르다"와 짝이어야 한다.** 네 프로파일의 해시가 같다는 문장은 `scale` 로 갈라진다는 문장이 옆에 있을 때만 의미가 있다.

**자기모순인 데이터는 조용히 산다.** Web 프로파일이 `Threads` 없이 `maxThreads = 4` 를 들고 있던 것은 아무 테스트도 깨지 않았다. 그 값을 실제로 읽는 코드가 없었기 때문이다.

현재 상태의 한계 넷.

**빅엔디안 타깃이 없다.** WASM 도 리틀엔디안이므로 3타깃 CI 가 생겨도 엔디안 축은 여전히 논증이다. 골든 벡터가 그 자리를 대신하지만, 그것은 "우리가 옳다고 믿는 바이트"이지 "다른 배치에서 관측된 바이트"가 아니다.

**능력 프로파일이 실제 하드웨어를 보지 않는다.** `ProfileFor(CurrentOS())` 는 OS 만 본다. 코어 수도 실제 메모리도 조회하지 않아, 2코어 노트북과 16코어 워크스테이션이 같은 `PC` 프로파일을 받는다.

**`RuntimeBudget` 의 여섯 필드 중 `workerCap` 만 실제로 검사된다.** 나머지 다섯은 값이 갈리는지만 확인하고, 그 값이 실제로 아레나 크기나 보이스 수를 정하는지는 배선 뒤의 일이다.

**`platform/console.hpp` 의 비-Windows 경로가 이 머신에서 컴파일되지 않는다.** `#else` 가지는 Linux CI 가 생기기 전까지 컴파일조차 되지 않고, 컴파일되지 않는 코드는 옳다고 말할 수 없다.
