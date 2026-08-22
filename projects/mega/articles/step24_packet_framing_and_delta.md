# 24. 클라이언트가 확실히 가진 것

## 들어가며

스냅샷 한 장의 예산은 1200바이트다. 지금 가득 찬 스냅샷은 1198바이트를 쓴다 — 타워 16 + 적 64 + 발사체 20. 그 계산은 `static_assert` 가 컴파일 시간에 지키고 있어서, 적 상한을 65로 올리는 순간 빌드가 멈춘다.

규모 축은 계속 위를 향한다. 적 64는 이 게임이 감당할 수 있는 수가 아니라 **와이어가 감당할 수 있는 수**이고, 그 벽을 넘는 방법은 스냅샷을 더 잘게 자르는 것이 아니라 **보내지 않는 것**이다. 프레임 사이에 안 바뀐 것은 이미 클라이언트에 있다. 다시 보낼 이유가 없다.

그러려면 호스트가 한 가지를 알아야 한다. **이 클라이언트가 지금 확실히 갖고 있는 가장 최신 스냅샷은 무엇인가.** 지금 이 프로토콜은 그것을 모른다. 명령에는 누적 ACK 가 붙어 있지만 그것은 반대 방향이고, 스냅샷 쪽에는 아무 확인도 없다.

이 편은 그 답을 내는 헤더 하나를 세우고, 그 위에 차분을 얹는다.

| 헤더 | 하는 일 |
|---|---|
| `net/packet.hpp` | seq/ack/ackBits 프레이밍 · 시퀀스 색인 저장소 · 델타 코덱 · 기준선 |
| `net/rtt.hpp` | ack 시각에서 뽑는 왕복 표본과, 그것이 대체하는 두 상수 |

도달점의 수치.

- 손실 35% · 재정렬 25% 링크에서 스냅샷 400장: **델타 395 / 전체 5**, 재구성 불가 **0**, 복원 불일치 **0**.
- 같은 구간의 바이트: 원본 55,035 B → 와이어 **16,288 B (29.6%)**.
- 손실 없는 연속 프레임 199장만 보면 185.8 B → **34.1 B (18.4%)**.
- 기준선 보관을 4칸으로 줄이고 ack 의 85%를 잃히면 **전체 229 / 델타 71** — 자동으로 풀 스냅샷으로 되돌아간다.
- 시퀀스가 65536에서 한 바퀴 돈 **뒤에도** 델타 213장이 계속 나간다.
- RTT 추정: 편도 20 / 80 / 200 ms 링크에서 **40.0 / 160.0 / 400.0 ms** 로 수렴(표본 399 / 393 / 381).
- 재전송 주기: 지터 없는 링크 160 ms, 흔들리는 링크 **420 ms**. 12초 동안 재전송 **147회**.

---

## 목차
1. [누적 ACK 가 대답하지 못하는 질문](#1-누적-ack-가-대답하지-못하는-질문)
2. [헤더 하나 — seq · ack · ackBits](#2-헤더-하나--seq--ack--ackbits)
3. [65536마다 한 바퀴](#3-65536마다-한-바퀴)
4. [밀려나는 저장소가 안전장치다](#4-밀려나는-저장소가-안전장치다)
5. [차분은 바이트를 본다](#5-차분은-바이트를-본다)
6. [기준선은 뒤로 가지 않는다](#6-기준선은-뒤로-가지-않는다)
7. [프레이밍이 둘이었다](#7-프레이밍이-둘이었다)
8. [왕복 표본은 게임 트래픽에서 나온다](#8-왕복-표본은-게임-트래픽에서-나온다)
9. [두 상수, 서로 다른 대체 규칙](#9-두-상수-서로-다른-대체-규칙)
10. [설계에서 조정한 것](#10-설계에서-조정한-것)
11. [변이 셋](#11-변이-셋)
12. [실행과 관측](#12-실행과-관측)

---

## 1. 누적 ACK 가 대답하지 못하는 질문

지금 이 프로토콜에는 신뢰성이 한 방향으로만 있다. 클라이언트가 보내는 명령에 단조 시퀀스가 붙고, 호스트가 스냅샷의 `acknowledgedCommand` 에 "여기까지 연속으로 반영했다"를 실어 돌려준다. 누적 ACK 다.

누적 ACK 는 **연속 경계**를 말한다. 5번이 빠지고 6·7·8이 도착했으면 경계는 5에 멈춘다. 명령에는 그것이 정확히 맞는 답이다 — 명령은 순서대로 정확히 한 번 반영돼야 하고, 5번을 건너뛰고 6번을 적용하는 것은 애초에 허용되지 않는다.

스냅샷은 정반대다. 스냅샷에는 순서가 필요 없다. 최신 하나만 의미가 있고, 중간이 빠져도 다음 것이 그 자리를 메운다. 그래서 스냅샷은 비신뢰로 보내고 ACK 도 붙이지 않았다.

차분을 실으려는 순간 그 결정이 문제가 된다. 차분은 **기준선**을 요구하고, 기준선은 받는 쪽이 실제로 갖고 있는 것이어야 한다. 없는 것에 대한 차분은 복원할 수 없다.

여기서 "몇 번까지 연속으로 받았는가"는 쓸모가 없다. 손실률 35%짜리 링크에서 연속 경계는 거의 자라지 않는데, 그 사이에도 클라이언트는 스냅샷을 **많이** 갖고 있다 — 구멍이 뚫린 채로. 필요한 것은 경계가 아니라 **집합**이다.

`net/packet.hpp` 의 헤더가 그 집합을 32비트로 압축한다.

## 2. 헤더 하나 — seq · ack · ackBits

<!-- snippet: include/net/packet.hpp#PacketHeader -->
```cpp
struct PacketHeader {
    uint16_t seq     = 0;          // 이 패킷의 시퀀스
    uint16_t ack     = 0;          // 마지막으로 받은 상대 시퀀스
    uint32_t ackBits = 0;          // ack-1 .. ack-32 수신 여부 비트필드
    // ...
};
```

8바이트다. `ack` 는 지금까지 본 것 중 가장 최신 상대 시퀀스이고, `ackBits` 의 비트 *i* 는 `ack - 1 - i` 를 받았는지를 말한다. 구멍이 뚫린 수신 이력이 그대로 실린다.

이 헤더 하나를 읽는 함수가 판정의 전부다.

<!-- snippet: include/net/packet.hpp@header-acknowledges -->
```cpp
inline bool HeaderAcknowledges(const PacketHeader& header, uint16_t seq) {
    if (seq == header.ack) return true;
    const int back = SeqDiff(header.ack, seq);
    if (back < 1 || back > kAckBitCount) return false;
    return (header.ackBits & (1u << (back - 1))) != 0;
}
```

주의할 점이 하나 있고, 그것이 이 함수의 주석에 적혀 있다. `false` 는 "받지 못했다"가 **아니다**. 창보다 오래된 과거와 아직 오지 않은 미래가 둘 다 `false` 로 나온다. 그래서 이 함수의 결과를 손실의 증거로 쓰면 안 되고, 오직 "아직 확인되지 않았다"로만 써야 한다. 델타 기준선은 이 구분에 정확히 의존한다 — 확인되지 않은 것을 기준선으로 삼지만 않으면 되고, 확인되지 않은 이유가 손실인지 지연인지는 알 필요가 없다.

받는 쪽은 `AckWindow` 가 담당한다. 새 최신이 오면 비트필드를 그만큼 밀고, 재정렬된 과거가 오면 자기 비트를 켠다.

```cpp
window.OnReceived(11);
window.OnReceived(13);            // 12 는 아직 안 왔다
CHECK(!window.Seen(12));
window.OnReceived(12);            // 재정렬 도착 → 구멍이 메워진다
CHECK(window.Seen(12));
CHECK_EQ(window.Ack(), (uint16_t)13);   // 과거 도착은 최신을 되돌리지 않는다
```

마지막 줄이 중요하다. 12가 늦게 도착했다고 `ack` 를 12로 되돌리면 13은 다시 미확인이 되고, 그것을 기준선으로 삼고 있던 차분이 통째로 무효가 된다. **최신은 단조 증가한다**는 규칙을 여기서 한 번 세우고 나면 위쪽 코드가 그것을 다시 걱정하지 않아도 된다.

## 3. 65536마다 한 바퀴

`seq` 가 16비트인 것은 대역폭 때문이다. 20Hz 스냅샷이면 한 바퀴에 55분이 걸리고, 32비트로 늘리면 매 패킷이 2바이트씩 비싸진다. 55분짜리 세션이 있을 수 있으니 순환은 실제로 일어나는 일이다.

순환이 있으면 `<` 로 시각을 비교할 수 없다. 65535 다음은 0이고, 0은 65535보다 **최신**이다.

<!-- snippet: include/net/packet.hpp@seq-wrap -->
```cpp
inline bool SeqGreater(uint16_t a, uint16_t b) {
    return ((a > b) && (a - b <= 32768)) || ((a < b) && (b - a > 32768));
}
inline int SeqDiff(uint16_t a, uint16_t b) {           // a - b (부호 있는 순환 거리)
    int d = (int)a - (int)b;
    if (d > 32768) d -= 65536;
    if (d < -32768) d += 65536;
    return d;
}
```

두 함수는 "두 시퀀스가 시퀀스 공간의 절반보다 가깝다"를 전제한다. 그 전제는 이 프로토콜에서 구조적으로 참이다 — 기준선 보관이 32칸이고 재정렬 창이 32칸이라, 한 번에 32768칸을 건너뛸 경로가 없다.

순환이 실제로 작동하는지는 단위 단언만으로는 부족하다. `SeqGreater` 가 틀려도 **패킷은 계속 오간다** — 델타가 조용히 멈추고 전부 풀 스냅샷이 될 뿐이다. 그것은 기능 정지가 아니라 성능 퇴행이라, 기능 단언으로는 잡히지 않는다. 그래서 검사는 순환을 실제로 통과시키고 **그 뒤에도 델타가 계속 쓰이는지**를 센다.

```cpp
// 시퀀스가 반드시 한 바퀴 돌게 시작점을 끝 가까이 둔다.
RunReplication(report, 0.10f, 0.10f, 0.15f, 300, 65450, 32, 64);
CHECK_EQ(report.unreconstructable, 0);
CHECK(report.deltaAfterWrap > 150);
```

## 4. 밀려나는 저장소가 안전장치다

호스트는 보낸 스냅샷을 보관해야 한다. 클라이언트가 3번을 ack 하면 3번의 바이트를 꺼내 차분의 기준선으로 써야 하기 때문이다.

무한히 보관하면 안 된다는 것은 명백하지만, 이유가 메모리만은 아니다. 더 중요한 이유는 **클라이언트도 무한히 보관하지 않는다**는 것이다. 클라이언트가 이미 버린 스냅샷을 호스트가 기준선으로 지목하면 그 패킷은 복원 불가가 되고, 그것은 손실보다 나쁘다 — 손실은 다음 장이 메우지만 복원 불가는 그 뒤의 모든 차분을 무효로 만든다.

그래서 보관소를 **고정 용량의 시퀀스 색인 표**로 둔다. `seq` 의 하위 비트가 슬롯 번호이고, 같은 슬롯에 있던 다른 `seq` 는 조용히 밀려난다.

```cpp
net::SequenceBuffer<int> buffer(8);
for (int i = 0; i < 8; ++i) buffer.Insert(static_cast<uint16_t>(i), i);
buffer.Insert(8, 8);                       // 슬롯 0 을 덮는다
CHECK_EQ(buffer.Evicted(), (uint64_t)1);
CHECK(buffer.Find(0) == nullptr);          // 밀려났다
```

밀려남이 결함이 아니라 **신호**다. `Baseline()` 이 `nullptr` 을 돌려주는 순간이 곧 "차분을 만들 수 없다"이고, 그때 호스트는 풀 스냅샷을 보낸다. 별도의 만료 정책도, 타임아웃도 필요 없다.

호스트의 보관 칸이 클라이언트의 보관 칸보다 많으면 안 된다는 관계는 이 구조가 표현하지 못한다. 그것은 두 프로세스 사이의 계약이라 한쪽 자료구조가 지킬 수 없고, 지금은 호스트 32칸 · 클라이언트 64칸으로 검사가 그 관계를 고정하고 있다.

`ForEach` 는 슬롯 번호 오름차순이다. 삽입 순서 순회를 제공하지 않는 것은 의도인데, 그것을 제공하면 결정성 규약을 어길 수 있는 자리가 하나 늘기 때문이다.

## 5. 차분은 바이트를 본다

델타 인코더가 게임 스냅샷의 구조를 아느냐 모르느냐는 계층 문제다.

필드 단위로 하면 이득이 크다. "적 3번의 x만 바뀌었다"를 비트 하나로 말할 수 있다. 대신 인코더가 `wire::Snapshot` 의 필드 구성을 알아야 하고, 그러면 `net/` 이 `game/` 을 향해 거꾸로 의존한다. 프로토콜이 필드를 하나 늘릴 때마다 네트워크 계층이 따라 바뀐다.

바이트 단위로 하면 그 의존이 없다. **"무엇이 안 바뀌었는가"는 무엇이 무엇인지 몰라도 셀 수 있다.**

<!-- snippet: include/net/packet.hpp@delta-codec mode=partial -->
```cpp
inline Bytes EncodeDelta(const Bytes& base, const Bytes& current) {
    Bytes out;
    PutVarint(out, static_cast<uint32_t>(current.size()));
    size_t cursor = 0;
    while (cursor < current.size()) {
        size_t same = cursor;
        while (same < current.size() && current[same] == DeltaBaseByte(base, same)) ++same;
        size_t diff = same;
        while (diff < current.size() && current[diff] != DeltaBaseByte(base, diff)) ++diff;
        PutVarint(out, static_cast<uint32_t>(same - cursor));
        PutVarint(out, static_cast<uint32_t>(diff - same));
        out.insert(out.end(),
                   current.begin() + static_cast<std::ptrdiff_t>(same),
                   current.begin() + static_cast<std::ptrdiff_t>(diff));
        cursor = diff;
    }
    return out;
}
```

세그먼트 하나는 `[같은 바이트 수][다른 바이트 수][다른 바이트들]` 이다. 기준선보다 뒤쪽 자리는 0으로 보므로 스냅샷이 자라도 같은 코덱으로 다룰 수 있다.

이 선택의 이득이 어디서 나오는지는 인코더가 필드를 늘어놓은 순서가 정한다. 적 한 마리는 13바이트이고 배치는 `id(4) x(2) y(2) hp(2) maxHp(2) type(1)` 이다. 프레임 사이에 바뀌는 것은 x·y·hp 여섯 바이트뿐이고, id 넉 바이트와 maxHp·type 석 바이트는 그대로다. 그래서 한 마리마다 `건너뜀 7 · 복사 6` 이 되고, 변하지 않은 꼬리가 다음 마리의 머리와 이어져 한 세그먼트로 합쳐진다. 타워 블록은 통째로 건너뛴다 — 타워는 건설·강화·판매 때만 바뀐다.

실측: 손실 없는 연속 프레임 199장에서 **185.8 B → 34.1 B (18.4%)**.

대가도 분명하고, 그것을 코덱의 주석이 요구 조건으로 적어 둔다. **엔티티의 직렬화 순서가 프레임마다 흔들리면 이득이 사라진다.** 같은 값이 다른 자리에 놓이는 순간 전부 "다름"이 된다. 지금 인코더는 적을 저장소의 행 번호 오름차순으로 쓰고 타워를 배열 순서로 쓰므로 그 조건을 만족한다. 그 성질은 우연이 아니라 결정성 규약이 이미 요구하고 있던 것이고, 델타가 그 규약의 두 번째 소비자가 된다.

복원 쪽은 대칭이지만 규율이 하나 더 있다.

```cpp
    if (cursor != end) return false;                          // 잉여 바이트 거부
    out = std::move(decoded);
    return true;
```

선언한 길이를 다 채운 뒤 남는 바이트가 있으면 거부한다. 이 프로토콜의 다른 모든 디코더와 같은 규칙이다 — 읽지 않은 바이트를 통과시키면 같은 상태를 뜻하는 패킷이 무한히 많아지고, 다음 버전이 그 자리에 의미를 주는 순간 옛 클라이언트의 쓰레기가 데이터가 된다.

## 6. 기준선은 뒤로 가지 않는다

`BaselineStore` 가 하는 일은 두 줄로 요약된다.

```cpp
    bool Acknowledge(const PacketHeader& remote) {
        bool advanced = false;
        sent_.ForEach([&](uint16_t seq, const Entry&) {
            if (!HeaderAcknowledges(remote, seq)) return;
            if (confirmed_ && !SeqGreater(seq, confirmedSeq_)) return;
            confirmedSeq_ = seq;
            confirmed_ = true;
            advanced = true;
        });
        return advanced;
    }
```

`SeqGreater` 가 없는 두 번째 줄을 상상해 보면 왜 그것이 있어야 하는지가 보인다. 재정렬된 옛 ack 패킷 하나가 늦게 도착하면 기준선이 과거로 끌려 내려간다. 그 과거는 클라이언트가 이미 버렸을 수 있고, 무엇보다 그 시점에 클라이언트가 **훨씬 최신을** 갖고 있는데도 오래된 것과의 차분을 보내게 된다. 크기 이득이 줄고 재구성 실패 위험이 생긴다. 둘 다 조용히.

검사는 재구성 실패를 직접 센다. 클라이언트 쪽 하네스가 자기가 실제로 보관 중인 스냅샷만으로 복원을 시도하고, 호스트가 없는 기준선을 가리킨 횟수를 `unreconstructable` 로 기록한다. 손실 35% · 재정렬 25% · 400장에서 **0**이다.

## 7. 프레이밍이 둘이었다

여기서 정직하게 적어야 할 것이 있다. `net::PacketHeader` 를 들이면 이 제품에는 프레이밍이 **둘**이 된다.

게임 프로토콜은 이미 자기 머리를 갖고 있다 — `magic(4) version(1) type(1) payloadLength(2)`. 그 여덟 바이트가 "이 패킷이 우리 것인가"와 "본문이 몇 바이트인가"를 말한다. `net::PacketHeader` 는 "이 패킷이 몇 번째이고 나는 무엇을 받았는가"를 말한다. 둘은 다른 질문에 답하므로 하나가 다른 하나를 대체하지 않는다.

대체가 아니라 **합류**가 답이다. 게임 헤더의 payload 길이 바로 뒤에 `net::PacketHeader` 8바이트를 밀어 넣어 머리를 하나로 만들고, 모든 패킷 종류가 같은 자리에서 그것을 읽는다. 패킷이 8바이트 커지므로 예산 단언이 그만큼 내려간다.

이 변경은 와이어 포맷을 바꾼다. 규약에 따라 `wire::kVersion` 을 한 단계 올리고, 옛 버전 패킷은 헤더 검사에서 거부된다.

합류한 머리는 16바이트다.

```
 0      4    5    6        8      10     12          16
 +------+----+----+--------+------+------+-----------+
 |magic |ver |type|payload |seq   |ack   | ackBits   |  본문 …
 +------+----+----+--------+------+------+-----------+
   u32   u8   u8    u16      u16    u16      u32
```

앞 여덟 바이트는 그대로다 — 자리를 옮기지 않았다. 옮기면 옛 클라이언트가 보낸 패킷의 `payload` 필드가 새 서버에서 `seq` 로 읽히고, 그때 나오는 거부 사유는 "길이가 안 맞는다"가 아니라 "매직이 안 맞는다"처럼 엉뚱한 자리를 가리키게 된다.

여덟 바이트가 늘었으니 예산이 그만큼 줄어든다. 산수는 이렇다.

```
전 (v1)  8 + 22 + 16×11 + 64×13 + 20×8 = 1198  (≤ 1200)
후 (v2) 16 + 22 + 16×11 + 64×13 + 20×8 = 1206  (> 1200 → 빌드 실패)
```

`static_assert(kSnapshotMaxBytes <= kMaxPacketBytes)` 가 컴파일 시간에 멈춘다. **그것이 게이트가 제대로 도는 증거이므로 예산 상수 1200 을 올려서 해결하지 않았다.** 1200 은 UDP 경로가 정한 수이지 우리가 정한 수가 아니다.

대신 발사체 상한을 20에서 19로 내렸다.

```
16 + 22 + 16×11 + 64×13 + 19×8 = 1198
```

옛 값과 정확히 같아진다. 세 컬렉션 중 발사체 행만이 순수 표시용이고 매 틱 새로 생겼다 사라지기 때문이다 — 타워는 플레이어가 세운 것이고 적은 권위 상태라, 한 줄 잃는 비용이 셋 중 가장 싸다. 대안(적 64→63)도 산수는 맞지만 사거리 안 적 하나가 화면에서 사라지는 쪽이 더 비싸다.

배선은 얇다. 전체 인코더 `WriteSnapshot` 은 **그대로 두고**, 그 위에 두 함수를 얹었다.

<!-- snippet: include/game/defense_protocol.hpp@delta-write -->
```cpp
    uint16_t baselineSeq = 0;
    const net::BaselineStore::Entry* baseline = baselines.Baseline(&baselineSeq);
    net::Bytes packet;
    if (baseline) {
        const net::Bytes delta = net::EncodeDelta(baseline->bytes, full);
        Writer writer(PacketType::Snapshot, framing);
        writer.U8(kSnapshotDelta);
        writer.U16(baselineSeq);
        // ...
        packet = writer.Finish();
        if (packet.size() > kMaxPacketBytes) packet.clear();
    }
    if (packet.empty()) packet = full;
    baselines.Sent(seq, tick, std::move(full));
    return packet;
```

`DefenseApp::SendSnapshot` 과 `DefenseServer::BroadcastSnapshot` 이 그 함수를 부르고, `DefenseApp::PumpClient` 가 `ReadSnapshotPacket` 으로 받는다. 보관하는 것은 **언제나 전체 바이트**다 — 차분을 보관하면 다음 차분이 차분의 차분이 되어, 한 장만 잃어도 그 뒤가 전부 복원 불가가 된다. 그리고 완성된 차분 패킷이 예산을 넘으면 전체로 되돌린다. 바뀐 구간이 흩어져 있으면 세그먼트 머리값이 절약분을 먹어 차분이 원본보다 커지는 일이 이론상 가능하다.

기준선 칸과 클라이언트 보관 칸은 한 자리에 나란히 두고 잠갔다.

<!-- snippet: include/game/defense_protocol.hpp@baseline-slots -->
```cpp
inline constexpr size_t kSnapshotBaselineSlots = 32;   // 호스트: 보낸 전체 스냅샷
inline constexpr size_t kSnapshotHeldSlots     = 64;   // 클라이언트: 복원해 둔 것
static_assert(kSnapshotHeldSlots > kSnapshotBaselineSlots,
              "클라이언트 보관 칸이 호스트 기준선 칸보다 적으면 호스트가 "
              "클라이언트에 없는 기준선을 가리킨다");
```

세 번째 항목이 여기서 결정됐다. 전체 스냅샷도 본문 첫 바이트에 `kSnapshotFull` 을 적는다. 플래그를 델타 패킷에만 두면 디코더가 "무엇을 읽을지 정하기 전에" 알아야 할 것을 패킷 종류에서 유추해야 하고, 그 유추가 곧 두 번째 프레이밍이다. 1바이트가 더 늘어 예산은 1199가 됐다.

**두 팔은 제품 안에서 이렇게 갈린다.**

```
> test_avatar_wiring.exe          (호스트도 클라이언트도 제품 DefenseApp, 루프백 UDP)
  snapshot_mode: full 평균 167.1B 최대 177B (400장) / delta 평균 71.2B 최대 95B (400장) = 42.6%
```

같은 1200틱, 같은 400장이다. 클라이언트가 본 세계도 같다 — 타워 수·웨이브·골드가 두 팔에서 일치한다는 것이 (a) 결과 동일이고, 평균 167.1B → 71.2B 가 (b) 비용 상이다. 그리고 `snapshot_baseline_miss` 는 두 팔 모두 0이다. 호스트가 클라이언트에 없는 기준선을 가리킨 적이 한 번도 없다는 뜻이다.

전용 서버 쪽 실측은 두 클라이언트가 실제로 붙은 판에서 나온다.

```
[server] stop tick=317 players=2 commands=0 rejected=1 snapshots=210 snapshot_bytes=29214 avg=139.1 result=incomplete
```

## 8. 왕복 표본은 게임 트래픽에서 나온다

같은 헤더가 두 번째 일을 한다. 보낸 패킷의 `seq` 와 송신 시각을 작은 표에 남겨 두고, 상대 헤더가 그 `seq` 를 덮는 순간 `지금 - 보낸 시각` 이 왕복 표본이 된다.

별도의 ping/pong 패킷을 추가하는 안을 기각했다. 이유는 절약이 아니라 정확성이다. 전용 ping 은 게임 트래픽이 없는 순간에도 오가므로 **유휴 경로**를 재게 되는데, 정작 알고 싶은 것은 스냅샷 20Hz + 명령 재전송이 함께 밀려 나가는 혼잡 구간의 왕복 시간이다. 두 수는 다르고, 타임아웃을 정하는 데 쓰이는 것은 후자여야 한다.

대가는 "트래픽이 없으면 표본도 없다"이고, 그래서 추정기가 `Ready()` 로 "아직 아무것도 모른다"를 말할 수 있어야 한다.

표본 하나를 버리는 규칙이 있다. 같은 `seq` 가 두 번 나갔으면 도착한 ack 가 어느 쪽 것인지 알 수 없다. 첫 송신 시각으로 재면 실제보다 크고, 두 번째로 재면 실제보다 작다. 둘 다 타임아웃을 틀린 방향으로 민다.

```cpp
    void OnSent(uint16_t seq, double sendMs) {
        Pending* existing = sent_.Find(seq);
        if (existing) { existing->resent = true; return; }   // 재전송: 표본 자격 상실
        ...
    }
```

TCP 가 Karn's algorithm 이라 부르는 것과 같은 규칙이다. 검사가 그것을 직접 짚는다 — 같은 seq 를 두 번 보내고 ack 를 받으면 새 표본은 **0개**다.

수렴은 실측으로 확인한다. 편도 20 / 80 / 200 ms 링크에서 추정 RTT 가 각각 40.0 / 160.0 / 400.0 ms 로 붙는다. 오차 1 ms 미만이다.

여기서 하네스에 관해 하나 배웠다. 처음에 하네스의 스텝을 16 ms 로 두었더니 편도 20 ms 링크가 **64 ms** 로 나왔다. 지연선이 스텝 경계에서만 배달하므로 20 ms 가 32 ms 로 올림된 것이고, 재고 있던 것은 링크가 아니라 하네스의 반올림이었다. 스텝을 20 ms 로 바꾸자 세 값이 전부 정확히 맞았다. 측정 장치가 측정 대상보다 거칠면 그 거칠기가 결과로 나온다.

## 9. 두 상수, 서로 다른 대체 규칙

바꿔야 할 상수가 둘이었다. `InterpolationClock::delayTicks = 3.0` 과 명령 재전송 주기 `100ms`. 둘 다 링크를 보고 고른 값이 아니다.

처음 계획은 둘 다 RTT 로 대체하는 것이었다. 그중 하나만 맞았다.

**재전송 주기는 RTT 가 하한이다.** 왕복이 끝나기 전에 다시 보내면 그것은 복구가 아니라 중복이다. 그래서 `rtt + 4·jitter` 를 그대로 쓴다.

**보간 지연에는 RTT 가 들어가지 않는다.** 스냅샷은 재전송되지 않는 연속 스트림이라, 링크가 아무리 멀어도 도착 *간격*은 호스트가 보낸 간격 그대로다. 늘려야 하는 것은 그 간격이 흔들리는 폭뿐이다.

측정이 그것을 말한다.

```
보간 지연: 400ms 무지터 3.00틱 · 400ms 지터 8.05틱 (rtt 400/396)
```

왕복 400 ms 인데 흔들리지 않는 링크의 필요 지연은 **3틱**, 즉 상수 시절과 같다. 같은 400 ms 인데 표본이 320/480 사이를 오가는 링크는 **8.05틱**을 요구한다. RTT 를 지연에 넣었다면 앞의 링크에서 24틱(400 ms)을 기다렸을 것이고, 그것은 아무 이득 없는 400 ms 의 반응 지연이다.

<!-- snippet: include/net/rtt.hpp#AdaptiveTiming mode=partial -->
```cpp
struct AdaptiveTiming {
    // ...
    double DelayTicks(const RttEstimator& estimator) const {
        if (!estimator.Ready()) return fallbackDelayTicks;
        const double oneWayJitterMs = static_cast<double>(estimator.Jitter()) * 0.5;
        const double ticks =
            snapshotIntervalTicks + jitterSafety * oneWayJitterMs * tickHz / 1000.0;
        return std::clamp(ticks, fallbackDelayTicks, maxDelayTicks);
    }
    // ...
    double ResendMs(const RttEstimator& estimator) const {
        if (!estimator.Ready()) return fallbackResendMs;
        return std::clamp(static_cast<double>(estimator.Timeout()), minResendMs, maxResendMs);
    }
};
```

두 함수의 하한이 **옛 상수 그 자체**인 것은 A/B 규약이다. 측정이 아직 없는 동안에는 스위치 이전의 동작이 나와야 하고, 그러지 않으면 앞선 편의 실측 출력이 재현되지 않는다.

배선은 세 자리다. 나가는 패킷마다 `rtt_.OnSent(seq, NowMs())`, 들어오는 헤더마다 `rtt_.OnRemoteHeader(framing, NowMs())`, 그리고 `PumpClient` 의 재전송 상수 자리에 `AppliedResendMs()`.

<!-- snippet: include/game/defense_app.hpp@applied-timing -->
```cpp
    int AppliedResendMs() const {
        return options_.switches.interpDelay == InterpDelayKind::Rtt
            ? static_cast<int>(timing_.ResendMs(rtt_.Estimator()))
            : static_cast<int>(timing_.fallbackResendMs);
    }
```

로그가 **측정값 그 자체가 아니라 지금 팔이 쓰는 값**을 찍는 것이 중요하다. 측정값을 찍으면 `fixed` 팔의 로그도 "링크를 봤다"고 말하게 되고, 그러면 두 팔의 출력이 구분되지 않는다.

보간 시계도 같은 규칙이다. `net::InterpolatedView` 는 생성자에서 시계를 한 번 받는 구조라 매 프레임 갱신할 방법이 없었다 — 지연이 측정값에서 나오면 그 값은 링크가 변할 때마다 변하므로, `SetClock` 을 추가해 `delayTicks`·`leadTicks` 만 갈아 끼운다. `keep`(버퍼 용량)은 건드리지 않는다. 용량을 줄이면 이미 들고 있는 스냅샷을 버려야 하고, 그것은 시계의 일이 아니다.

두 팔을 같은 루프백 링크에서 재면 이렇다.

```
> mega_defense --host 27110 --selftest / --join 127.0.0.1 27110 --selftest  (interp_delay=fixed)
[selftest] result=LINKED ticks=5 snapshots=1 commands_queued=4 commands_sent=1 ack=1 towers=9 wave=5
[metrics] ... rtt_ms=1.3 resend_ms=100 delay_ticks=3.00

> 같은 쌍, --ab interp_delay=rtt
[selftest] result=LINKED ticks=3 snapshots=1 commands_queued=2 commands_sent=1 ack=1 towers=9 wave=5
[metrics] ... rtt_ms=1.7 resend_ms=20 delay_ticks=3.01
```

`fixed` 팔은 측정을 하고도 쓰지 않는다 — `rtt_ms=1.3` 이 찍히는데 주기는 100ms 그대로다. `rtt` 팔은 그 1.7ms 를 받아 `rtt + 4·jitter` 를 계산하고, 하한 20ms 에 걸린다. 루프백이라 하한이 이긴 것이고, 그 하한이 없으면 주기가 한 자릿수 밀리초로 내려가 재전송이 복구가 아니라 홍수가 된다.

보간 지연은 3.00 → 3.01 로 거의 움직이지 않았다. 지터가 0에 가까운 링크에서 그것이 옳은 답이다 — `DelayTicks` 에 rtt 가 들어가지 않는다는 설계가 여기서 관측된다. 400ms 링크라도 지터가 없으면 지연은 3틱이어야 하고, 그 사실은 가상 시계 검사가 재고 있다(`보간 지연: 400ms 무지터 3.00틱 · 400ms 지터 8.05틱`).

## 10. 설계에서 조정한 것

**델타를 별도 패킷 종류로 두지 않았다.** 스냅샷 패킷 본문 앞에 1바이트 플래그와 기준선 seq 2바이트를 붙이는 쪽을 택했다. 종류를 나누면 "델타인데 기준선이 없다" 같은 상태가 프로토콜 수준에서 표현 가능해지고, 그것을 막는 검사가 하나 더 필요해진다.

**기준선을 클라이언트가 고르게 하는 안을 기각했다.** 클라이언트가 "나는 7번을 갖고 있으니 그것으로 보내 줘"라고 말하는 설계는 왕복이 한 번 더 들고, 그 요청 자체가 손실될 수 있다. ack 비트필드는 이미 매 패킷에 실려 있으므로 추가 왕복이 0이다.

**`kAckBitCount` 를 32로 고정했다.** 64로 늘리면 창이 넓어지지만 헤더가 4바이트 커진다. 20Hz 스냅샷에서 32칸은 1.6초이고, 그보다 오래 전 스냅샷을 기준선으로 쓸 일은 없다 — 보관소가 32칸이라 이미 밀려나 있다. 두 수를 같게 맞춘 것은 우연이 아니다.

**`SequenceBuffer` 에 삽입 순서 순회를 두지 않았다.** 필요할 것 같아 보였지만, 결정성 규약이 금지하는 순회를 제공하는 자료구조는 언젠가 시뮬 경로에서 쓰인다. 슬롯 번호 오름차순 하나만 남겼다.

## 11. 변이 셋

**하나. `SeqGreater` 의 순환 분기를 지웠다** (`return a > b;`).

```
> test_delta_snapshot.exe
  FAIL test_delta_snapshot.cpp:139  CHECK(net::SeqGreater(0, 65535))
  FAIL test_delta_snapshot.cpp:170  CHECK_EQ(wrapped.Ack(), (uint16_t)5)
  FAIL test_delta_snapshot.cpp:172  CHECK(wrapped.Seen(0))
  FAIL test_delta_snapshot.cpp:178  CHECK(net::HeaderAcknowledges(header, 5))
  순환 후 델타 31 / 전체 델타 114 / 재구성불가 0
  FAIL test_delta_snapshot.cpp:407  CHECK(report.deltaAfterWrap > 150)
[test] delta_snapshot: 5643 checks, 6 failures
```

단위 단언 넷과 배선 단언 하나가 함께 죽는 것이 이 변이의 값이다. 앞의 넷은 함수가 틀렸다고 말하고, 마지막 하나는 그 함수가 실제로 델타 경로를 굴리고 있었다고 말한다. **재구성 불가는 0으로 남는다** — 순환이 깨져도 복원은 계속 성공한다. 그것이 이 결함이 조용한 이유이고, 성능 퇴행을 세는 단언이 없으면 아무도 눈치채지 못한다.

`순환 후 델타 213 → 31` 이 그 퇴행의 크기다. 65536을 지난 순간 기준선이 굳고, 32장 뒤에 보관소에서 밀려나고, 그 뒤로는 전부 풀 스냅샷이다.

**둘. `ApplyDelta` 의 잉여 바이트 거부를 지웠다.**

```
> test_delta_snapshot.exe
  FAIL test_delta_snapshot.cpp:111  CHECK(!net::ApplyDelta(base, surplus, out))
[test] delta_snapshot: 5807 checks, 1 failures
```

한 줄만 죽는다. 정상 경로는 전부 통과하고 그것이 맞다 — 이 줄은 정상 트래픽에서 한 번도 발동하지 않는 신뢰 경계 검사다.

**셋. `RttEstimator::Sample` 의 지터 갱신을 지웠다.**

```
> test_rtt_adaptive.exe
  지터 있음: rtt 286.5 지터 0.00 타임아웃 286.5
  FAIL test_rtt_adaptive.cpp:124  CHECK(jittery.jitter > 10.0f)
  보간 지연: 400ms 무지터 3.00틱 · 400ms 지터 3.00틱 (rtt 400/396)
  FAIL test_rtt_adaptive.cpp:183  CHECK(jitteryDelay > farDelay)
  FAIL test_rtt_adaptive.cpp:190  CHECK(timing.ResendMs(jitteryEstimator) > ...)
  재전송: 메시지 60개 · 12초 · 전송 170회 · 전달 60개 · rtt 266 지터 0 최종 주기 266ms
  FAIL test_rtt_adaptive.cpp:304  CHECK(report.timeoutMs > 400.0)
  FAIL test_rtt_adaptive.cpp:305  CHECK(report.sends < 160)
[test] rtt_adaptive: 36 checks, 5 failures
```

지터 항이 사라지면 보간 지연이 무지터 링크와 같은 3틱으로 돌아가고(흔들리는 링크에서 화면이 끊긴다), 재전송 주기가 646 → **266 ms** 로 내려앉는다. 그 결과가 마지막 줄이다 — 12초 동안 전송 **147 → 170회**. 아직 날아가고 있는 패킷을 다시 보내기 시작한 것이고, 손실이 심한 구간일수록 이 비율이 나빠진다.

## 12. 실행과 관측

**복제 왕복.**

```
> test_delta_snapshot.exe
[test] delta_snapshot
  손실 35%: 전송 400 (full 5 / delta 395) 수신 274 재구성불가 0 불일치 0
  스냅샷 원본 55035 B → 와이어 16288 B (29.6%)
  순환 후 델타 213 / 전체 델타 296 / 재구성불가 0
  기준선 4칸 · ack 손실 85%: full 229 / delta 71 / 밀려남 296
  연속 프레임 델타: 199장 · 원본 평균 185.8 B → 델타 평균 34.1 B (18.4%) · 최대 적 8
[test] delta_snapshot: 5807 checks, 0 failures
```

읽는 법. 첫 줄의 `full 5` 는 매치 시작 직후와 ack 가 오래 끊긴 구간에서 나간 전체 스냅샷이다. 나머지 395장이 차분이고, 그중 클라이언트가 복원하지 못한 것은 **0**이다.

둘째 줄과 다섯째 줄의 비율이 다른 것(29.6% vs 18.4%)은 손실 때문이다. 손실이 있는 링크에서는 기준선이 여러 프레임 전 것이 되므로 차이가 그만큼 커진다. 손실이 없으면 항상 직전 프레임이 기준선이라 18.4%다. **차분의 이득은 링크 품질의 함수**이고, 이것이 델타가 손실 구간에서 자동으로 "덜 아끼는" 방향으로 동작한다는 뜻이다 — 아낄 수 없을 때 억지로 아끼지 않는다.

넷째 줄이 폴백 경로다. 보관을 4칸으로 줄이고 ack 의 85%를 잃히면 확인된 기준선이 살아남기 전에 밀려나므로, 300장 중 229장이 전체 스냅샷으로 나간다. 재구성 불가는 여전히 0이다.

**적응 타이밍.**

```
> test_rtt_adaptive.exe
[test] rtt_adaptive
  편도 20ms → 추정 RTT 40.0ms (기대 40) 지터 0.00 표본 399
  편도 80ms → 추정 RTT 160.0ms (기대 160) 지터 0.00 표본 393
  편도 200ms → 추정 RTT 400.0ms (기대 400) 지터 0.00 표본 381
  지터 없음: rtt 160.0 지터 0.00 타임아웃 160.0
  지터 있음: rtt 286.5 지터 33.37 타임아웃 420.0
  스파이크 800ms 한 번: rtt 160.0 → 224.0 (타임아웃 480.0)
  보간 지연: 400ms 무지터 3.00틱 · 400ms 지터 8.05틱 (rtt 400/396)
  재전송: 메시지 60개 · 12초 · 전송 147회 · 전달 60개 · rtt 294 지터 88 최종 주기 646ms
```

여섯째 줄이 EWMA 를 쓴 이유다. 160 ms 로 수렴한 링크에 800 ms 짜리 스파이크가 한 번 오면 추정은 224 ms 로만 움직인다. 한 표본을 그대로 믿었다면 재전송 주기가 다섯 배로 뛰었다가 되돌아왔을 것이고, 그 사이의 손실은 전부 복구가 늦는다.

## 11. 제품 실측

와이어 변경 셋(합류 프레이밍 · 델타 · 아바타 입력)을 거친 지금 제품은 이렇게 돈다.

```
> mega_defense --selftest
[platform] Windows/x86-64/LE profile=PC workers=16 arena=1024KB voices=32 lod=0
[selftest] result=CLEAR ticks=1377 towers=10 wave=6 packet=181 protocol=OK render=OK assets=10/10 content=external stomps=2
[metrics] matches=1 ... snapshots_truncated=0 baseline_miss=0 rtt_ms=0.0 resend_ms=100 delay_ticks=3.00
```

`packet=181` 은 매치 끝 상태를 전체 인코더로 한 번 왕복시킨 실제 바이트 수다. 상한이 1199 이므로 여유가 1018바이트 남는다 — 이 값이 상한에 가까워지는 것은 적이 64에 붙을 때뿐이고, 그때 잘림 카운터가 먼저 말한다(`snapshots_truncated=0`).

협동 왕복은 두 **프로세스** 경계 밖에서 확인된다.

```
> mega_defense --host 27103 --selftest 8000   (백그라운드)
> mega_defense --join 127.0.0.1 27103 --selftest 8000
[selftest] result=LINKED ticks=5 snapshots=2 commands_queued=2 commands_sent=1 ack=1 towers=9 wave=5 render=OK
[selftest] avatar frame=2 cell=136 checksum=0x165D7FC6 stalls=3 rollbacks=0 input_packets=4 input_bytes=102 desync=0/0

  (호스트 쪽)
[selftest] result=CLEAR ticks=1377 towers=10 wave=6 packet=181 protocol=OK
[selftest] avatar frame=2 cell=136 checksum=0x165D7FC6 stalls=1375 rollbacks=0 input_packets=145 input_bytes=4335 desync=0/0
[metrics] matches=1 remote_commands=1 snapshots=49 ... rtt_ms=1.2 resend_ms=20 delay_ticks=3.01
```

`result=LINKED` 는 명령이 나갔고 호스트가 그것을 순서대로 반영했다는 뜻이다. 그리고 두 프로세스의 아바타 체크섬이 `0x165D7FC6` 로 **같다** — 델타로 복원한 상태가 원본과 다르지 않다는 것을 프로세스 경계 밖에서 한 번 더 확인해 주는 값이다.

CTest 는 111개 대상을 전수 통과한다. 이 편이 들여온 검사는 `test_delta_snapshot`(코덱과 손실·재정렬·순환) · `test_rtt_adaptive`(가상 시계) · `test_avatar_wiring` 의 `snapshot_mode` 두 팔(제품 호스트↔클라이언트)이고, 기존 검사 중 `test_defense_protocol` 은 합류 헤더 때문에 여섯 자리가 바뀌었다 — 그중 하나가 `ResealPayloadLength` 의 `- 8` 이다. 그 한 줄을 안 고쳤다면 "길이는 맞지만 남는 바이트가 있다" 쪽 단언이 헤더 길이 검사에 먼저 걸려, **통과는 하지만 검사하려던 것을 검사하지 않는** 상태가 됐을 것이다.

---

## 결론

이 편의 변경은 세 겹인데 아래에서 위로 읽어야 이유가 보인다.

맨 아래는 **질문**이다. "이 클라이언트가 확실히 가진 가장 최신 스냅샷은 무엇인가." 누적 ACK 는 이 질문에 답하지 못하고, 답하지 못한다는 사실 자체가 오랫동안 보이지 않았다 — 그 질문을 할 이유가 없었기 때문이다. 차분을 보내려는 순간 질문이 생겼고, 질문이 생기고 나서야 기존 구조의 구멍이 구멍으로 보였다.

가운데는 **자료구조**다. 답이 32비트 비트필드와 고정 용량 시퀀스 표로 표현되고 나면, 그 위의 정책은 놀랄 만큼 적어진다. 만료도 타임아웃도 재요청도 없다. 밀려나면 전체를 보낸다, 한 줄이다.

맨 위는 **측정**이다. 두 상수를 측정값으로 바꾸면서 배운 것은 "둘 다 RTT 로 바꾸면 된다"가 틀렸다는 것이다. 재전송에는 RTT 가 하한이고 보간 지연에는 지터만 들어간다. 그 구분은 추측으로는 나오지 않았고, 왕복 400 ms 무지터 링크의 필요 지연이 3틱이라는 수를 보고 나서야 나왔다.

현재 상태의 한계 넷.

**바이트 델타는 순서 안정성에 매달려 있다.** 적 저장소가 행을 압축하거나 재정렬하면 — 지금은 하지 않지만 — 차분이 그 프레임에 부풀어 오른다. 그 사실을 컴파일 시간에 지키는 장치는 없고, 검사가 실측 비율로 간접적으로 지킬 뿐이다.

**기준선은 클라이언트당 하나다.** 전용 서버가 두 클라이언트를 서로 다른 기준선으로 섬기려면 스냅샷 인코딩이 클라이언트마다 한 번씩 돌아야 한다. 2인 협동에서는 무시할 만하지만, 정원이 늘면 이 비용이 선형으로 는다.

**`kMaxDeltaPayload` 는 코덱의 상한이지 프로토콜의 상한이 아니다.** 8192바이트는 "복원 중 메모리 폭주를 막는다" 정도의 값이고, 실제 예산은 게임 프로토콜의 1200바이트가 별도로 지킨다. 두 수가 서로를 모르는 상태는 정리되지 않았다.

**RTT 표본은 링크 하나에 대한 것이다.** 전용 서버가 두 클라이언트를 섬길 때 각 링크의 왕복 시간은 다르고, 지금 구조는 그 둘을 구분해 담을 자리가 클라이언트 세션 안에 있어야 한다는 것만 말하지 그것을 강제하지는 않는다.
