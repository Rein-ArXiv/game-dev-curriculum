## 들어가며

3편이 끝난 자리에 `wire::Command` 구조체가 있다. 클라이언트 모드에서 그것은 큐에 들어가기만 하고 아무 데도 가지 않는다. 그리고 반대 방향 — 호스트가 자기 월드를 클라이언트에게 보여 주는 수단 — 은 아직 이름조차 없다.

이 편의 도달점은 **바이트 레이아웃 하나**다. 끝나면 다음이 성립한다.

- `GameWorld` 하나가 **한 장의 UDP 데이터그램**으로 압축되고, 되돌아온다.
- 가득 찬 스냅샷이 1200바이트를 넘지 않는다는 사실이 **컴파일 시간에** 증명된다.
- 인코더가 자르는 개수와 디코더가 거부하는 개수가 **같은 상수 하나**에서 온다.
- 어떤 잉여 바이트도, 어떤 범위 밖 열거값도 디코더를 통과하지 못한다.

소켓은 여기 없다. 이 편의 코드는 `std::vector<uint8_t>` 를 만들고 읽을 뿐이고, 그 바이트를 누가 실어 나르는지는 모른다. 그 분리 덕분에 이 편 전체가 프로세스 하나 안에서 검증된다.

---

## 목차
1. [왜 자체 포맷인가](#1-왜-자체-포맷인가)
2. [헤더 8바이트](#2-헤더-8바이트)
3. [Writer 와 Reader: 봉인과 소진](#3-writer-와-reader-봉인과-소진)
4. [양자화: 위치 1/8픽셀, 체력 1/4단위](#4-양자화-위치-18픽셀-체력-14단위)
5. [상한은 하나만 존재한다](#5-상한은-하나만-존재한다)
6. [1200바이트를 컴파일러가 지킨다](#6-1200바이트를-컴파일러가-지킨다)
7. [도달 불가가 된 마지막 자물쇠](#7-도달-불가가-된-마지막-자물쇠)
8. [잘림은 양쪽에서 세어야 한다](#8-잘림은-양쪽에서-세어야-한다)
9. [디코더가 거부하는 것들](#9-디코더가-거부하는-것들)
10. [기대 시퀀스를 태우는 공격](#10-기대-시퀀스를-태우는-공격)
11. [실행과 관측](#11-실행과-관측)

---

## 1. 왜 자체 포맷인가

후보가 셋 있었다.

| 방식 | 크기 | 의존 | 스키마 변경 | 기각 사유 |
|---|---|---|---|---|
| JSON | 매우 큼 | 파서 필요 | 쉬움 | 타워 16기 스냅샷이 UDP 한 장에 안 들어간다 |
| 범용 직렬화 라이브러리 | 중간 | 서드파티 | 도구가 관리 | 이 저장소는 서드파티 의존이 0이다 |
| 저장소 안의 `BinaryArchive` | 작음 | 없음 | 수동 | (아래) |
| 손으로 쓴 바이트 레이아웃 | 가장 작음 | 없음 | 수동 | (선택) |

세 번째가 실질적인 경쟁자였다. 이 저장소에는 이미 범용 이진 아카이브가 있고, 엔진 기본 스냅샷이 그것을 쓴다. 그런데도 게임 스냅샷은 손으로 썼다. 이유는 **양자화** 다.

범용 아카이브는 `float` 을 4바이트로 그대로 적는다. 스냅샷 한 장에 위치가 (타워 16 + 적 64 + 발사체 20) × 2 = 200개 있고, 체력이 128개 있다. 전부 `float` 이면 그것만 1312바이트다 — 헤더를 더하기도 전에 예산을 넘는다. 위치를 16비트로 줄이면 같은 자리가 656바이트가 된다.

즉 이 포맷의 존재 이유는 "직렬화"가 아니라 **"1200바이트 안에 들어가기"** 다. 그 목표가 없으면 범용 아카이브를 썼을 것이다.

1200이라는 숫자는 이더넷 MTU 1500에서 IP·UDP 헤더와 터널링 오버헤드를 뺀 보수적인 값이다. 이보다 크면 데이터그램이 IP 단편화되고, 단편 하나가 유실되면 **패킷 전체가 사라진다** — 유실 확률이 조각 수만큼 곱해진다.

## 2. 헤더 8바이트

모든 패킷이 같은 8바이트로 시작한다.

```text
오프셋  크기  필드
  0      4    magic    0x3146444D  ("MDF1", 리틀엔디안 바이트 순서로 M D F 1)
  4      1    version  1
  5      1    type     1=Hello  2=Command  3=Snapshot  4=AdmissionHello
  6      2    payload  헤더 뒤 바이트 수
```

네 필드가 각각 다른 실패를 막는다.

**`magic`** — 이 소켓에 도착하는 모든 데이터그램이 우리 것은 아니다. UDP 포트는 누구나 쏠 수 있고, 스캐너·다른 프로토콜의 오배송이 실재한다. 4바이트 상수가 그 대부분을 첫 검사에서 떨군다.

**`version`** — 포맷이 바뀌면 이 값이 오른다. 구버전 클라이언트가 신버전 호스트에 붙었을 때, 필드 하나가 밀린 채로 "성공적으로" 디코드되는 것보다 거부되는 편이 낫다.

**`type`** — 하나의 소켓이 네 종류를 나른다. 타입 바이트가 없으면 수신 측이 길이나 내용을 보고 추측해야 하고, 추측은 틀릴 수 있다.

**`payload`** — 헤더 뒤 바이트 수. 이 필드가 있어서 디코더가 **본문을 읽기 전에** 길이 정합성을 검사할 수 있다.

## 3. Writer 와 Reader: 봉인과 소진

<!-- snippet: include/game/defense_protocol.hpp#Writer -->
```cpp
class Writer {
public:
    explicit Writer(PacketType type, const net::PacketHeader& framing = net::PacketHeader()) {
        U32(kMagic);
        U8(kVersion);
        U8(static_cast<uint8_t>(type));
        U16(0);  // payload size, finalized by Finish()
        U16(framing.seq);
        U16(framing.ack);
        U32(framing.ackBits);
    }

    void U8(uint8_t value) { platform::AppendLE8(bytes_, value); }
    void U16(uint16_t value) { platform::AppendLE16(bytes_, value); }
    void U32(uint32_t value) { platform::AppendLE32(bytes_, value); }
    void U64(uint64_t value) { platform::AppendLE64(bytes_, value); }
    void Raw(std::string_view value) {
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    net::Bytes Finish() {
        uint16_t payload = static_cast<uint16_t>(bytes_.size() - 16);
        platform::WriteLE16(bytes_.data() + 6, payload);
        return bytes_;
    }

private:
    net::Bytes bytes_;
};
```

**생성자가 헤더를 쓰고 `Finish` 가 길이를 봉인한다.** 길이를 미리 계산해 넘기는 방식도 있었지만, 그러면 호출자가 계산을 틀릴 수 있고 그 틀림은 수신 측에서야 드러난다. 자리를 0으로 비워 두고 마지막에 실제 크기로 덮어쓰면 **틀릴 방법이 없다**.

바이트 순서는 리틀엔디안으로 손수 쓴다. `memcpy` 로 구조체를 통째로 복사하는 방식을 쓰지 않은 이유는 셋이다 — 패딩이 플랫폼마다 다르고, 엔디안이 아키텍처마다 다르고, 무엇보다 그 방식은 **수신 측이 신뢰할 수 없는 바이트를 구조체로 재해석**하게 만든다. 바이트 단위 조립은 느리지만 이 크기(최대 1198바이트)에서는 의미 없는 비용이다.

<!-- snippet: include/game/defense_protocol.hpp#Reader -->
```cpp
class Reader {
public:
    explicit Reader(const net::Bytes& bytes) : bytes_(bytes) {}

    bool Header(PacketType expected, net::PacketHeader* framing = nullptr) {
        if (bytes_.size() < 16) return false;
        uint32_t magic = U32();
        uint8_t version = U8();
        uint8_t type = U8();
        uint16_t payload = U16();
        net::PacketHeader decoded;
        decoded.seq = U16();
        decoded.ack = U16();
        decoded.ackBits = U32();
        if (!ok_ || magic != kMagic || version != kVersion ||
            type != static_cast<uint8_t>(expected)) return false;
        if (payload != bytes_.size() - 16) return false;
        if (framing) *framing = decoded;
        return true;
    }

    uint8_t U8() {
        if (offset_ + 1 > bytes_.size()) { ok_ = false; return 0; }
        return bytes_[offset_++];
    }
    // ... U16 · U32 는 U8 을 조합하고, Raw 는 범위를 확인한 뒤 복사한다
    bool Done() const { return ok_ && offset_ == bytes_.size(); }

private:
    const net::Bytes& bytes_;
    size_t offset_ = 0;
    bool ok_ = true;
};
```

디코더의 규율이 세 가지다.

**모든 읽기가 범위를 본다.** `U8` 이 유일한 실제 읽기이고 `U16`·`U32` 가 그것을 조합하므로, 범위 검사가 한 곳에만 있다. 범위를 벗어나면 `ok_` 를 내리고 0을 돌려준다 — 예외를 던지지 않는 이유는 디코더가 네트워크 수신 루프 안에서 초당 수십 번 불리고, 잘못된 패킷이 **정상적인 입력**이기 때문이다.

**`payload != bytes_.size() - 8` 를 헤더에서 본다.** 선언한 길이와 실제 길이가 다르면 본문을 읽기 전에 끝난다.

**`Done()` 을 반드시 확인한다.** 모든 필드를 읽은 뒤 오프셋이 정확히 끝에 있어야 한다. 이 검사가 없으면 선언한 개수만큼 읽은 뒤 남는 바이트가 조용히 무시된다. 남는 바이트를 무시하는 디코더는 **같은 의미를 갖는 패킷이 무한히 많은 프로토콜**을 만들고, 그것은 다음 버전에서 그 자리에 의미를 주는 순간 호환성 폭탄이 된다.

두 검사가 겹쳐 있다는 점이 중요하다. 잉여 바이트를 그냥 붙이면 헤더의 길이 검사에 먼저 걸린다. 길이 필드까지 실제 크기로 다시 봉인하면 헤더를 통과하고, 그때 남는 것이 `Done()` 하나다. **두 번째 경우가 진짜 검사**이고, 그것을 자극하지 않는 테스트는 `Done()` 을 한 번도 검증하지 못한다.

## 4. 양자화: 위치 1/8픽셀, 체력 1/4단위

<!-- snippet: include/game/defense_protocol.hpp#EncodePosition -->
```cpp
inline uint16_t EncodePosition(float value) {
    float clamped = std::clamp(value, 0.0f, 8191.875f);
    return static_cast<uint16_t>(std::lround(clamped * 8.0f));
}
```

<!-- snippet: include/game/defense_protocol.hpp#EncodeHp -->
```cpp
inline uint16_t EncodeHp(float value) {
    float clamped = std::clamp(value, 0.0f, 16383.75f);
    return static_cast<uint16_t>(std::lround(clamped * 4.0f));
}
```

정밀도와 범위는 하나의 예산을 나눠 쓴다. 16비트 = 65536단계다.

| | 배율 | 최댓값 | 근거 |
|---|---|---|---|
| 위치 | ×8 (1/8픽셀) | 8191.875 | 화면은 480픽셀. 1/8픽셀은 눈에 보이지 않는다 |
| 체력 | ×4 (1/4단위) | 16383.75 | 최대 체력은 보스의 12배 배수라도 1000 미만 |

두 상한 모두 실제 필요보다 한참 크다. 위치 상한 8191은 화면의 17배이고, 체력 상한 16383은 보스 체력의 20배 이상이다. 그 여유가 의도적인 이유는 **클램프가 조용한 손실**이기 때문이다 — 값이 상한에 붙으면 화면에서 엔티티가 모서리에 뭉치는데, 그 모양은 "버그"가 아니라 "이상한 게임"으로 보인다.

`std::lround` 를 쓰는 것은 `static_cast<uint16_t>(x * 8.0f)` 가 **버림**이기 때문이다. 버림은 계통 오차를 만든다 — 모든 좌표가 평균 1/16픽셀씩 왼쪽으로 밀리고, 그 편향이 보간을 거치면서 누적된다. 반올림은 오차의 기댓값을 0으로 만든다.

`clamp` 가 `lround` 앞에 있는 것도 필요하다. 음수나 거대한 값을 `uint16_t` 로 캐스트하면 결과가 구현 정의이고, 실제로는 감싸 돌아 **화면 반대편에 나타난다**.

디코드는 곱셈의 역이다.

<!-- snippet: include/game/defense_protocol.hpp#DecodePosition -->
```cpp
inline float DecodePosition(uint16_t value) {
    return static_cast<float>(value) * 0.125f;
}
```

`0.125f` 는 2의 거듭제곱의 역수라 **정확히 표현된다**. `/ 8.0f` 로 써도 같은 결과지만, 곱셈으로 쓰면 배율과 역배율이 같은 정확도를 갖는다는 것이 코드에서 눈에 보인다.

3편에서 봤듯 로컬 입력도 이 왕복을 지난다. 그래서 솔로와 협동에서 클릭이 같은 셀로 간다.

## 5. 상한은 하나만 존재한다

인코더는 컬렉션이 너무 크면 자른다. 디코더는 개수가 너무 크면 거부한다. **이 두 개수가 다르면 프로토콜이 깨진다.**

- 인코더 상한 > 디코더 상한: 호스트가 보낸 정상 패킷을 클라이언트가 거부한다. 엔티티가 많아지는 후반 웨이브에서만 링크가 끊긴다.
- 인코더 상한 < 디코더 상한: 디코더가 오지 않을 크기를 허용한다. 방어가 느슨해진다.

그래서 상수는 **하나씩만** 존재하고, `WriteSnapshot` 과 `ReadSnapshot` 이 같은 것을 읽는다.

```cpp
static constexpr size_t kSnapshotMaxTowers = 16;
static constexpr size_t kSnapshotMaxEnemies = 64;
static constexpr size_t kSnapshotMaxProjectiles = 20;
```

이 셋은 **게임플레이 규칙이 아니라 와이어 포맷의 일부**다. 값을 바꾸면 프로토콜이 바뀌므로 `kVersion` 도 함께 올려야 한다.

값의 근거는 이렇다. 적 64는 무리 웨이브(26마리)와 그 뒤 웨이브가 겹치는 최악을 덮는다. 발사체 20은 개틀링 4기가 동시 사격하는 상황(발사 간격 0.12초, 비행 시간 약 0.5초 → 기당 4~5발)을 덮는다. 타워 16은 게임플레이 상한과 같다.

그 마지막이 미묘하다. `GameWorld::kMaxTowers` 도 16이고 `kSnapshotMaxTowers` 도 16이지만, **이유가 다르다.** 하나는 밸런스(한 판에 세울 수 있는 타워 수)이고 하나는 패킷 예산이다. 우연히 같은 값이므로 하나로 합치지 않았다. 대신 묶여야 할 방향을 단언한다.

<!-- snippet: include/game/defense_protocol.hpp@snapshot-budget -->
```cpp
static constexpr size_t kSnapshotFixedBytes = 16 + 1 + 4 + 4 + 4 + 2 + 2 + 2 + 1 + 3 + 4 + 4;
static constexpr size_t kSnapshotTowerBytes = 4 + 2 + 2 + 1 + 1 + 1;
static constexpr size_t kSnapshotEnemyBytes = 4 + 2 + 2 + 2 + 2 + 1;
static constexpr size_t kSnapshotProjectileBytes = 4 + 2 + 2;
static constexpr size_t kSnapshotMaxBytes =
    kSnapshotFixedBytes + kSnapshotMaxTowers * kSnapshotTowerBytes +
    kSnapshotMaxEnemies * kSnapshotEnemyBytes +
    kSnapshotMaxProjectiles * kSnapshotProjectileBytes;
static_assert(kSnapshotMaxBytes <= kMaxPacketBytes,
              "가득 찬 스냅샷이 UDP payload 예산을 넘는다");
static_assert(kSnapshotMaxTowers <= 255 && kSnapshotMaxEnemies <= 255 &&
                  kSnapshotMaxProjectiles <= 255,
              "컬렉션 개수는 U8 한 바이트로 실린다");

static_assert(GameWorld::kMaxTowers <= kSnapshotMaxTowers,
              "게임플레이 타워 상한이 스냅샷 상한을 넘으면 초과 타워가 모든 "
              "클라이언트에서 사라진다");
```

마지막 단언이 그 방향이다. 월드가 프로토콜보다 많은 타워를 담을 수 있게 되면, 초과분은 `std::min` 에 잘려 **모든 스냅샷에서 조용히 사라진다.** 밸런스를 조정하다 `kMaxTowers` 를 24로 올린 사람은 자기가 프로토콜을 건드렸다는 사실을 모른다. 그 발산을 런타임이 아니라 빌드가 잡는다.

## 6. 1200바이트를 컴파일러가 지킨다

크기 산수는 정직하다.

```
고정부      8 + 4 + 4 + 4 + 2 + 2 + 2 + 1 + 3               =   30
            헤더 + tick + ack + gold + coreHp + coreMaxHp
            + wave + flags + 개수 3바이트

타워 1기    id 4 + x 2 + y 2 + type 1 + tier 1 + target 1   =   11
적 1마리    id 4 + x 2 + y 2 + hp 2 + maxHp 2 + type 1      =   13
발사체 1개  id 4 + x 2 + y 2                                 =    8

최대        30 + 16×11 + 64×13 + 20×8
          = 30 + 176 + 832 + 160                             = 1198
```

1198 ≤ 1200. **2바이트 남는다.**

이 계산을 주석에 적어 두는 것과 `static_assert` 로 적어 두는 것의 차이가 이 절의 전부다. 주석은 상수를 바꿀 때 함께 바뀌지 않는다. `static_assert` 는 상수에서 계산되므로 **바꾸는 순간 다시 계산된다**.

`kSnapshotMaxEnemies` 를 64에서 70으로 올려 보면 1198 + 78 = 1276 이 되어 빌드가 그 자리에서 멈춘다. 런타임 검사였다면 적이 70마리가 되는 후반 웨이브에 처음 드러났을 것이고, 그때는 이미 패킷이 나가지 않는다.

`U8 <= 255` 단언은 다른 종류의 실수를 막는다. 개수 필드가 한 바이트이므로 상한이 256 이상이면 `static_cast<uint8_t>(count)` 가 감싸 돌아 **300개를 44개로 선언**한다. 그 패킷은 길이 검사에 걸려 거부되지만, 디버깅하는 사람에게는 "왜 스냅샷이 전부 버려지는가"로만 보인다.

## 7. 도달 불가가 된 마지막 자물쇠

`WriteSnapshot` 의 마지막 줄이 이렇다.

```cpp
net::Bytes bytes = writer.Finish();
return bytes.size() <= kMaxPacketBytes ? bytes : net::Bytes{};
```

**이 분기의 `else` 는 도달할 수 없다.** 세 컬렉션이 전부 상한이어도 1198바이트이고, 상한을 올리면 `static_assert` 가 먼저 빌드를 멈추기 때문이다.

이 사실이 흥미로운 이유는 검증 방법론에 있다. 이 줄을 지우는 변이를 넣고 전체 테스트를 돌리면 **아무 테스트도 실패하지 않는다.** 보통 그것은 테스트의 구멍이라는 신호다 — 커버되지 않은 분기가 있다는 뜻이니까.

여기서는 다르다. 그것은 구멍이 아니라 **도달 불가라는 사실의 반영**이다. 테스트를 추가해 이 분기를 덮으려면 프로토콜 상수를 위반하는 상태를 인위적으로 만들어야 하는데, 그 상태는 `static_assert` 때문에 컴파일되지 않는다.

그래서 이 줄을 지우지 않았다. 이유는 둘이다. 첫째, 도달 불가를 보장하는 것은 컴파일 시간 단언이고 그 단언은 **이 파일의 다른 곳**에 있다 — 누군가 그 단언을 지우면 이 줄이 마지막 방어선이 된다. 둘째, 빈 벡터를 돌려주는 것이 예산 초과의 명시적 표현이고, 호출자는 이미 `if (snapshot.empty()) return;` 으로 그것을 처리한다.

같은 상황을 마주치는 사람을 위해 그 판단을 코드 주석에 남겨 두었다. 변이 테스트가 잡지 못하는 줄이 전부 죽은 줄은 아니다.

## 8. 잘림은 양쪽에서 세어야 한다

<!-- snippet: include/game/defense_protocol.hpp#WriteSnapshot -->
```cpp
inline net::Bytes WriteSnapshot(const GameWorld& world, uint32_t tick,
                                uint32_t acknowledgedCommand,
                                bool* truncatedOut = nullptr,
                                const net::PacketHeader& framing = net::PacketHeader(),
                                uint32_t avatarFrame = 0,
                                uint32_t avatarChecksum = 0) {
    size_t aliveEnemies = 0;
    const size_t enemyRows = world.Enemies().Size();
    for (size_t i = 0; i < enemyRows; ++i)
        if (world.Enemies().Alive(i)) ++aliveEnemies;
    const size_t liveProjectiles = world.Projectiles().Size();

    size_t towerCount = std::min(world.Towers().size(), kSnapshotMaxTowers);
    size_t enemyCount = std::min(aliveEnemies, kSnapshotMaxEnemies);
    size_t projectileCount = std::min(liveProjectiles, kSnapshotMaxProjectiles);
    bool truncated = towerCount != world.Towers().size() ||
                     enemyCount != aliveEnemies ||
                     projectileCount != liveProjectiles;
    if (truncatedOut) *truncatedOut = truncated;

    Writer writer(PacketType::Snapshot, framing);
    writer.U8(kSnapshotFull);
    writer.U32(tick);
    writer.U32(acknowledgedCommand);
    writer.U32(avatarFrame);
    writer.U32(avatarChecksum);
    writer.U32(static_cast<uint32_t>(std::max(world.Gold(), 0)));
    writer.U16(EncodeHp(world.CoreHp()));
    writer.U16(EncodeHp(world.CoreMaxHp()));
    writer.U16(static_cast<uint16_t>(std::clamp(world.Wave(), 0, 65535)));
    uint8_t flags = static_cast<uint8_t>((world.Won() ? 1 : 0) |
                                         (world.Lost() ? 2 : 0) |
                                         (truncated ? 4 : 0));
    writer.U8(flags);
    writer.U8(static_cast<uint8_t>(towerCount));
    writer.U8(static_cast<uint8_t>(enemyCount));
    writer.U8(static_cast<uint8_t>(projectileCount));
    // ... 세 컬렉션을 각각 towerCount · enemyCount · projectileCount 만큼 쓴다
    net::Bytes bytes = writer.Finish();
    return bytes.size() <= kMaxPacketBytes ? bytes : net::Bytes{};
}
```

잘림 정보가 **두 경로**로 나간다. 와이어의 `flags & 4` 와 출력 인자 `truncatedOut` 이다. 중복처럼 보이지만 답하는 질문이 다르다.

- **`flags`** — "내가 보는 세계에 빠진 것이 있는가"를 받는 쪽이 안다. 클라이언트가 화면과 지표에 반영한다.
- **`truncatedOut`** — "내가 방금 무엇을 버렸는가"를 보내는 쪽이 안다.

와이어 플래그만 있으면 잘림은 **클라이언트가 붙어 있을 때만 관측된다.** 그런데 잘림은 호스트에서 일어나는 손실이다. 아무도 접속하지 않은 호스트도, 전용 서버도 자기가 무엇을 버렸는지 셀 수 있어야 한다.

기본값이 `nullptr` 인 것은 관심 없는 호출자를 위한 것이다. 셀프테스트의 프로토콜 왕복 검사처럼 잘림을 신경 쓰지 않는 자리는 인자를 생략한다.

`std::max(world.Gold(), 0)` 은 골드가 음수가 될 수 없다는 규칙을 인코더가 다시 확인하는 것이다. `Gold()` 는 `int` 이고 `U32` 로 나가므로, 음수가 오면 40억 언저리의 수로 나타난다. 규칙상 음수는 생기지 않지만 **인코더가 그 가정에 의존하지 않게** 하는 편이 싸다.

`flags` 를 비트마스크로 쓴 것은 세 개의 불린이 `U8` 세 개가 되면 2바이트를 낭비하기 때문이다. 예산이 2바이트 남는 포맷에서 그것은 의미 있는 절약이다.

## 9. 디코더가 거부하는 것들

<!-- snippet: include/game/defense_protocol.hpp#ReadSnapshot -->
```cpp
inline bool ReadSnapshot(const net::Bytes& bytes, Snapshot& snapshot,
                         net::PacketHeader* framing = nullptr) {
    Reader reader(bytes);
    if (!reader.Header(PacketType::Snapshot, framing)) return false;
    // ...
    if (reader.U8() != kSnapshotFull) return false;
    Snapshot decoded;
    decoded.tick = reader.U32();
    // ... 고정부를 지역 구조체에 푼다
    uint8_t towerCount = reader.U8();
    uint8_t enemyCount = reader.U8();
    uint8_t projectileCount = reader.U8();
    // 디코더의 상한은 인코더가 자른 상한과 같은 상수여야 한다. 둘이 갈라지면
    // 한쪽은 보내고 한쪽은 거부하는 패킷이 생긴다.
    if (static_cast<size_t>(towerCount) > kSnapshotMaxTowers ||
        static_cast<size_t>(enemyCount) > kSnapshotMaxEnemies ||
        static_cast<size_t>(projectileCount) > kSnapshotMaxProjectiles) return false;

    decoded.towers.reserve(towerCount);
    for (uint8_t i = 0; i < towerCount; ++i) {
        TowerState tower;
        tower.id = reader.U32();
        tower.pos = {DecodePosition(reader.U16()), DecodePosition(reader.U16())};
        tower.type = static_cast<TowerType>(reader.U8());
        tower.tier = reader.U8();
        tower.targetMode = static_cast<TargetMode>(reader.U8());
        if (tower.type >= TowerType::Count || tower.tier < 1 || tower.tier > 3 ||
            tower.targetMode > TargetMode::Strongest) return false;
        decoded.towers.push_back(tower);
    }
    // ... 적은 type 범위와 hp <= maxHp + 0.25f 를, 발사체는 위치만 읽는다
    if (!reader.Done()) return false;
    snapshot = std::move(decoded);
    return true;
}
```

네 가지 규율이 있다.

**지역 구조체에 풀고, 전부 검증된 뒤에만 내보낸다.** `decoded` 를 다 채우고 마지막에 `snapshot = std::move(decoded)` 다. 출력 인자에 먼저 쓰면 **거부된 패킷의 범위 밖 값이 호출자 구조체에 남는다.** 호출자가 반환값을 확인하더라도, 그 구조체는 이전 프레임의 유효한 상태였을 수 있고 이제는 반쯤 덮어써진 쓰레기다.

**개수 상한을 본문 읽기 전에 본다.** `reserve(towerCount)` 가 상한 검사 뒤에 오는 것이 중요하다. 앞에 두면 255개 분량을 예약한 뒤 거부하게 된다.

**모든 열거값의 범위를 본다.** `tower.type >= TowerType::Count` 가 그것이다. 2편에서 `TargetMode` 의 기반 타입을 `uint8_t` 로 고정한 이유가 여기서 실현된다 — 기반 타입이 없으면 열거 범위가 `{0..3}` 이라 200을 캐스트한 결과가 unspecified 이고, 그 뒤의 비교는 아무것도 보장하지 못한다.

**필드 간 관계도 본다.** `enemy.hp > enemy.maxHp + 0.25f` 면 거부다. 각 필드가 개별적으로 유효해도 조합이 불가능할 수 있다. 여유 0.25f 는 체력 양자화 단위 그 자체다 — 반올림으로 `hp` 가 `maxHp` 보다 한 단계 크게 나오는 경우를 정상으로 본다.

`tier` 검사가 `1..3` 인 것도 같은 종류다. `tier` 는 `U8` 이라 0..255 를 실을 수 있지만 게임 규칙상 1..3 뿐이고, 0이 오면 화면에서 선택 테두리 크기가 `7 + 0` 이 되어 이상해진다.

명령 디코더도 같은 규율을 따른다.

<!-- snippet: include/game/defense_protocol.hpp#CommandArgumentInRange -->
```cpp
inline bool CommandArgumentInRange(CommandType type, uint8_t argument) {
    switch (type) {
        case CommandType::Place:
            return argument < static_cast<uint8_t>(TowerType::Count);
        case CommandType::Target:
            return argument <= static_cast<uint8_t>(TargetMode::Strongest);
        // ...
        case CommandType::Upgrade:
        case CommandType::Sell:
            // 이 둘은 argument 를 읽지 않는다. 읽지 않는 필드는 0 이어야 한다 —
            // 아니면 같은 동작을 뜻하는 패킷이 256가지가 되고, 다음 버전이 이
            // 바이트에 의미를 주는 순간 옛 클라이언트의 쓰레기가 명령이 된다.
            return argument == 0;
    }
    return false;
}
```

`Upgrade`/`Sell` 이 `argument == 0` 을 요구하는 것이 이 함수의 핵심이다. 읽지 않는 필드를 방치하면 **같은 동작을 뜻하는 패킷이 256가지**가 된다. 프로토콜 버전 2가 그 바이트에 "강화 목표 티어"를 넣는 순간, 버전 1 클라이언트가 우연히 채워 보낸 쓰레기가 유효한 명령이 된다.

## 10. 기대 시퀀스를 태우는 공격

인자 검사를 어디에 둘지에는 실질적인 차이가 있다. 디코더에서 거부하는 것과 `ApplyCommand` 에서 거부하는 것은 결과가 다르다.

<!-- snippet: include/game/defense_protocol.hpp#ReadCommand -->
```cpp
inline bool ReadCommand(const net::Bytes& bytes, Command& command,
                        net::PacketHeader* framing = nullptr) {
    Reader reader(bytes);
    if (!reader.Header(PacketType::Command, framing)) return false;

    // ReadSnapshot 과 같은 규율: 지역에 풀고, 전부 검증된 뒤에만 내보낸다.
    Command decoded;
    decoded.sequence = reader.U32();
    decoded.type = static_cast<CommandType>(reader.U8());
    decoded.x = reader.U16();
    decoded.y = reader.U16();
    decoded.argument = reader.U8();
    if (!reader.Done()) return false;
    if (decoded.type < CommandType::Place || decoded.type > CommandType::Blast) return false;
    if (!CommandArgumentInRange(decoded.type, decoded.argument)) return false;
    command = decoded;
    return true;
}
```

호스트의 수신 단계는 이렇게 생겼다.

```cpp
wire::Command command;
if (!wire::ReadCommand(packet, command)) continue;
remoteCommands_.Accept(command.sequence, command);
```

`Accept` 는 **그 자체로 상태 변경이다.** 기대 시퀀스와 일치하면 그 번호를 소비하고 다음 번호를 기대한다.

여기서 공격이 성립한다. 공격자가 기대 시퀀스에 얹힌 위조 명령 하나를 보낸다. 인자가 범위 밖이라 `ApplyCommand` 는 거부할 것이다 — 그러나 그 전에 `Accept` 가 이미 그 자리를 태웠다. 정직한 클라이언트가 같은 시퀀스로 보낼 진짜 명령은 **영원히 중복 취급되어 사라진다.** 플레이어에게는 "요청한 타워가 조용히 안 서는" 것으로 보인다.

그래서 인자 검사가 `Accept` 앞, 즉 디코더 안에 있어야 한다. `ApplyCommand` 의 같은 검사는 3편에서 본 로컬 입구를 위한 것이지 이 문제를 막지 못한다.

두 검사의 역할 분담이 정리하면 이렇다.

| 검사 | 위치 | 막는 것 |
|---|---|---|
| 타입·인자 범위 | `ReadCommand`(디코더) | 위조 패킷이 신뢰 채널의 시퀀스를 태우는 것 |
| 인자 범위 | `ApplyCommand` | 디코더를 지나지 않는 로컬 입력의 범위 밖 값 |
| 월드 상태 | `ApplyCommand` | 골드 부족·건설 불가 칸·존재하지 않는 타워 |

## 11. 실행과 관측

프로토콜에는 두 개의 검사가 붙어 있다.

```powershell
cmake --build build --config Release -j
ctest --test-dir build -C Release -R "protocol|defense_network"
```

```text
    Start 14: test_defense_network
1/2 Test #14: test_defense_network .............   Passed    0.04 sec
    Start 17: test_defense_protocol
2/2 Test #17: test_defense_protocol ............   Passed    0.03 sec

100% tests passed out of 2

Total Test time (real) =   0.08 sec
```

```powershell
.\build\bin\Release\test_defense_protocol.exe
```

```text
[test] defense_protocol
[test] defense_protocol: 101 checks, 0 failures
```

101개 단언이 무엇을 묻는지가 이 편의 요약이다.

**왕복.** 실제 월드를 10틱 돌린 뒤 인코드·디코드해 `tick`·`ack`·`gold`·컬렉션 크기가 보존되는지 본다. 그다음 마지막 바이트 하나를 떼면 디코드가 실패한다.

**상한.** 기대값을 전부 리터럴로 적는다 — `kSnapshotMaxTowers == 16`, `kSnapshotMaxEnemies == 64`, `kSnapshotMaxProjectiles == 20`. 구현이 쓰는 상수와 비교하면 **사본이 함께 바뀔 때도 통과하는 항등식**이 되어 아무것도 지키지 못하기 때문이다. 그리고 게임플레이 상한이 실제로 작동하는지도 확인한다 — 골드 10만과 빈 셀이 남았는데 17번째 타워가 서지 않는다.

**잘림.** 적 100마리짜리 월드를 만들어 `truncated` 가 보내는 쪽(`bool*`)과 받는 쪽(`flags`) 양쪽에서 켜지는지, 디코드된 적 수가 정확히 상한 64인지 본다. 그리고 반대 방향 — 적 3마리 월드에서는 양쪽 다 꺼져 있어야 한다. **항상 켜지는 플래그는 켜지지 않는 플래그와 똑같이 아무 말도 하지 않기 때문이다.**

**잉여 바이트.** 여기가 `Done()` 을 검증하는 자리다. 잉여를 붙이고 **길이 필드까지 다시 봉인**해 헤더 검사를 통과시킨 뒤 거부되는지를 본다. 형태를 넷으로 바꿔 가며 확인한다 — 바이트 하나, 타워 하나 분량, 패킷 통째로 복제, 그리고 명령·Hello·입장 토큰 패킷에도 같은 규칙이 적용되는지.

**시퀀스 태우기.** 호스트 수신 단계와 같은 모양(`ReadCommand` → `Accept`)을 만들어 놓고 `argument = 200` 짜리 위조 명령을 시퀀스 1로 보낸다. 그다음 정직한 명령을 같은 시퀀스 1로 보내면 **여전히 받아들여져야** 하고, 그것을 꺼내 적용하면 타워가 실제로 선다.

제품 전체에서 스냅샷 크기를 볼 수도 있다.

```powershell
.\build\bin\Release\mega_defense.exe --selftest
```

```text
[selftest] result=CLEAR ticks=1377 towers=11 wave=6 packet=175 protocol=OK render=OK assets=10/10 content=external
```

`packet=175` 는 셀프테스트가 끝난 시점의 실제 월드를 그대로 인코드한 크기다. 6절의 표로 되짚으면 이렇게 나온다.

```
고정부 30 + 타워 11기 × 11 + 적 0마리 × 13 + 발사체 3개 × 8
= 30 + 121 + 0 + 24
= 175
```

승리 직후라 적은 전부 정리됐고 발사체 3개가 아직 날고 있었다. 예산 1200 대비 15% 다. `protocol=OK` 는 그 바이트를 다시 디코드해 성공했다는 뜻이다.

---

## 결론

이 단계에서 세운 것.

1. **자체 바이트 레이아웃** — 범용 직렬화가 아니라 양자화가 목적이다. `float` 위치 200개는 그것만으로 예산을 넘는다.
2. **헤더 8바이트** — magic·version·type·payload. 네 필드가 각각 다른 실패(오배송·버전 불일치·타입 추측·길이 불일치)를 막는다.
3. **봉인과 소진** — `Finish()` 가 길이를 사후에 덮어써 계산 실수를 없애고, `Done()` 이 잉여 바이트를 거부해 "같은 뜻을 갖는 패킷이 여럿"인 상태를 없앤다.
4. **양자화** — 위치 1/8픽셀, 체력 1/4단위. 상한은 실제 필요의 20배 — 클램프가 조용한 손실이기 때문이다. 반올림은 계통 편향을 없앤다.
5. **상한 상수 하나** — 인코더와 디코더가 같은 것을 읽는다. 갈라지면 한쪽은 보내고 한쪽은 거부하는 패킷이 생긴다.
6. **컴파일 시간 예산 검사** — 1198 ≤ 1200 을 `static_assert` 가 상수에서 계산한다. 게임플레이 상한이 프로토콜 상한을 넘는 순간에도 빌드가 멈춘다.
7. **도달 불가 분기를 남긴 판단** — 변이가 잡히지 않는 줄이 전부 죽은 줄은 아니다. 그 근거를 코드에 남겼다.
8. **잘림 신호 두 방향** — 와이어 플래그는 받는 쪽, 출력 인자는 보내는 쪽. 손실은 호스트에서 일어나므로 호스트도 셀 수 있어야 한다.
9. **디코더 규율 넷** — 지역에 풀고 나중에 내보내기, 개수 상한 선검사, 열거 범위 검사, 필드 간 관계 검사.
10. **검사 위치의 실질적 차이** — 인자 검사가 `Accept` 앞에 있어야 위조 패킷이 정직한 클라이언트의 시퀀스를 태우지 못한다.

현재 상태의 한계. 이 바이트들을 **아무도 나르지 않는다.** `WriteSnapshot` 이 만든 벡터는 테스트 안에서 곧바로 `ReadSnapshot` 으로 들어가고, 그 사이에 손실도 재정렬도 지연도 없다. 명령을 몇 번 재전송할지, 스냅샷 사이의 빈 시간을 어떻게 채울지, 두 프로세스가 같은 프레임 루프를 어떻게 나눠 도는지는 어느 것도 정해져 있지 않다.
