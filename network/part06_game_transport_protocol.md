## 들어가며

실시간 멀티플레이 전송은 UDP를 선택하는 것으로 끝나지 않는다. 메시지마다 필요한 신뢰성·순서·수명을 표현하고, 손실과 혼잡 아래에서 재전송과 전송률을 통제하는 응용 프로토콜이 필요하다.

이 글은 패킷 헤더와 sequence·ack bitfield, reliable/unreliable 채널, MTU와 재전송에서 출발해 혼잡 신호, pacing, 무선 링크의 손실 구분까지 하나의 전송 계약으로 연결한다.

---

## 목차

1. 왜 TCP가 아닌가
2. UDP 위에 얹는 것
3. 패킷 헤더
4. Sequence와 Ack Bitfield
5. Reliable과 Unreliable Channel
6. Ordered와 Unordered Delivery
7. 송신 루프: 패킷 한 장의 조립
8. MTU와 재전송 정책
9. 흔한 실패 모드
10. TCP의 답을 못 쓰는 이유
11. 혼잡 신호: 무엇을 읽는가
12. 조절 손잡이: 무엇을 줄이는가
13. 이진 모드 정책: 단순한 것부터
14. 대역폭 추정과 페이싱
15. 무선 링크의 함정
16. 검증: 혼잡 제어를 테스트하는 법

---


## 1. 왜 TCP가 아닌가

TCP는 애플리케이션에 중복 없는 순서형 바이트 스트림을 제공한다. 다만 연결이 끊겼을 때 원격 애플리케이션이 특정 요청을 처리했는지까지 알려 주는 exactly-once 실행 보장은 아니다. 이 순서형 전달을 지키는 메커니즘이 실시간 상태 전송에는 정확히 독이 된다.

**Head-of-Line Blocking.** TCP는 앞선 byte가 유실되면 그 뒤 byte가 수신 buffer에 도착해 있어도 순서가 복구될 때까지 애플리케이션에 넘기지 않는다. 손실 복구는 SACK, fast retransmit, RACK/TLP와 RTO 등 stack·상황별 메커니즘을 사용한다. 지연이 한 RTT 부근에서 끝날 수도 있고 timeout까지 갈 수도 있으므로 “손실 1%면 항상 수백 ms 정지”라고 환산할 수는 없다. 핵심은 최신 snapshot이 도착했더라도 앞선 byte gap과 같은 stream에 있으면 사용할 수 없다는 의미 제약이다.

**의미 없는 재전송.** 100ms 전의 위치 스냅샷이 유실됐다면 올바른 대응은 "버리고 다음 것을 쓴다"이다. TCP는 그 선택지가 없다 — 모든 바이트가 평등하게 보장되므로, 가치가 소멸한 데이터도 끝까지 재전송한다. 대역폭과 시간이 이미 낡은 정보에 쓰인다.

**혼잡 제어의 목표 불일치.** 전통적인 TCP 혼잡 제어는 처리량을 높이는 과정에서 라우터 큐를 채울 수 있다. 큐가 차면 지연이 오른다 — 게임이 가장 지키고 싶은 자산이 먼저 희생된다. UDP 기반 게임 프로토콜은 전송률을 직접 제어해야 하며, 최소 RTT 대비 큐잉 지연과 손실을 함께 관찰하는 방식이 흔하다.

그래서 실시간 게임은 UDP 위에 *필요한 만큼의 신뢰성만* 직접 쌓는다. 단, 모든 게임이 그런 것은 아니다 — 턴제·카드·퍼즐처럼 수백 ms의 지연 스파이크가 체감되지 않는 장르는 TCP(또는 WebSocket)로 충분하고, 그 편이 구현·운영이 싸다. 이하의 설계는 "수십 ms가 체감되는" 게임을 전제한다.

## 2. UDP 위에 얹는 것

UDP는 애플리케이션에 datagram을 전달한다. 각 datagram은 독립적이다. 도착하지 않을 수 있고, 순서가 바뀔 수 있고, 같은 내용이 중복될 수 있다. 이 불완전함은 실시간 게임에서 오히려 유리하다. 낡은 위치 상태는 재전송할 가치가 없기 때문이다.

문제는 게임 데이터가 모두 같은 성격이 아니라는 점이다.

| 데이터 | 요구 | 전송 정책 |
|---|---|---|
| 위치/회전 스냅샷 | 최신성 | unreliable, 최신 패킷이 덮어씀 |
| 플레이어 입력 | 낮은 지연, 높은 도착 확률 | unreliable + 최근 입력 반복 포함 |
| 발사/피격 이벤트 | 도착 보장 | reliable |
| 인벤토리/상점/채팅 | 도착 보장, 순서 보장 | reliable ordered |
| 음성 | 낮은 지연 | unreliable, jitter buffer |

따라서 게임 프로토콜은 하나의 연결 안에 여러 정책을 둔다. transport는 UDP 하나지만, 그 위의 channel은 데이터별로 다르게 동작한다.

표의 둘째 줄처럼 최근 입력을 반복하면 별도 retransmission timer 없이 여러 도착 기회를 만들 수 있다. 독립이고 동일한 packet loss 확률을 $p$라고 가정할 때만 특정 입력을 실은 $N$개 packet이 모두 사라질 확률을 $p^N$으로 계산할 수 있다. 실제 무선·queue overflow는 burst loss와 상관을 가지므로 연속 손실 길이와 trace replay로 반복 깊이를 정한다.

```cpp
// 입력 패킷: 최근 입력을 중복 포함한다. 서버는 이미 처리한 틱(<= lastProcessed)은 무시한다.
struct InputPacket {
    uint32_t firstTick;          // inputs[0] 이 어느 틱의 입력인가
    uint8_t  count;              // 보통 3~10
    PlayerInput inputs[kMaxRedundantInputs];
};
```

## 3. 패킷 헤더

최소 패킷 헤더는 보통 다음 정보를 가진다.

```cpp
struct PacketHeader {
    uint32_t protocolId;   // 다른 게임/버전 패킷 차단
    uint32_t sequence;     // 내가 보내는 패킷 번호
    uint32_t ack;          // 내가 받은 상대의 최신 패킷 번호
    uint32_t ackBits;      // ack 이전 32개 패킷 수신 여부
    uint16_t channelMask;  // 포함된 channel 정보
    uint16_t payloadBytes;
};
```

`protocolId`는 포트가 같거나 잘못된 endpoint에서 온 패킷을 빠르게 버리기 위한 값이다. 게임 이름과 프로토콜 버전의 해시를 쓰면, 구버전 클라이언트의 패킷이 신버전 서버에서 첫 4바이트 비교만으로 떨어진다 — 파싱 코드에 닿기 전에 버리는 것이 보안의 첫 분기이기도 하다. 실전에서는 protocolId를 평문 전송하는 대신 패킷 CRC의 시드로 섞어 넣어 4바이트를 아끼는 변형도 흔하다.

`sequence`는 송신자가 보낸 패킷 번호다. `ack`와 `ackBits`는 반대 방향으로 흐르는 수신 확인 정보다. UDP에는 연결 상태가 없으므로, 양방향 패킷마다 서로의 수신 상태를 실어 보낸다. 별도 ack 패킷이 없다는 점이 요점이다 — 게임은 어차피 양방향으로 초당 수십 패킷이 흐르므로, 수신 확인은 그 흐름에 무임승차한다(piggyback). 한쪽이 보낼 게임 데이터가 없을 때만 keepalive 격의 빈 패킷이 ack 운반을 맡는다.

패킷 헤더는 작아야 한다. 20바이트 헤더를 60Hz로 100명에게 보내면 헤더만 초당 120KB다. sequence를 `uint16_t`로 줄이고(wraparound 처리는 4절과 동일), payloadBytes를 빼고(UDP 길이에서 유도), channelMask를 페이로드 쪽 메시지 프리픽스로 옮기면 14바이트가 되고, ack도 `uint16_t`로 줄이고 위의 protocolId→CRC 시드 변형까지 더하면 헤더는 8바이트로 10바이트 안쪽에 들어온다. 다만 줄이기 경쟁보다 중요한 것은 헤더가 손실 복구와 통계(손실률·RTT 측정이 전부 이 헤더에서 나온다)를 가능하게 하는 충분한 정보를 갖는 것이다.

## 4. Sequence와 Ack Bitfield

`sequence`는 패킷마다 증가한다. 수신자는 자신이 받은 가장 큰 sequence를 `ack`로 되돌려 보낸다. 하지만 `ack` 하나만 있으면 그보다 앞의 패킷들이 실제로 도착했는지 알 수 없다. 그래서 `ackBits`를 함께 보낸다.

```text
ack = 100
ackBits bit 0 = packet 99 received
ackBits bit 1 = packet 98 received
ackBits bit 2 = packet 97 received
...
ackBits bit 31 = packet 68 received
```

이 방식은 한 패킷이 손실되어도 다음 패킷들에 같은 수신 확인 정보가 반복해서 실리므로 견고하다. ack 정보 자체가 2절의 "반복 포함"과 같은 원리로 보호되는 것이다 — ack를 나르는 패킷이 32개 연속으로 사라져야 수신 확인이 유실된다.

```cpp
template<typename T>
bool IsMoreRecent(T a, T b) {
    static_assert(std::is_unsigned_v<T>);
    constexpr T halfRange =
        T(std::numeric_limits<T>::max() / 2 + 1);
    T forwardDistance = T(a - b);   // unsigned modular subtraction
    return a != b && forwardDistance < halfRange;
}

bool WasAcked(uint32_t seq, uint32_t ack, uint32_t ackBits) {
    if (seq == ack) return true;
    if (IsMoreRecent(seq, ack)) return false;

    uint32_t diff = ack - seq;
    if (diff == 0 || diff > 32) return false;
    return (ackBits & (1u << (diff - 1))) != 0;
}
```

sequence는 wraparound를 고려해야 한다. `uint16_t`나 `uint32_t`가 끝까지 증가한 뒤 0으로 돌아가도, 두 번호의 차이가 sequence 공간의 절반보다 작다는 가정으로 더 최신인지 판단한다. `IsMoreRecent`가 그 비교다 — 65535 다음의 0은 "더 최신"으로 옳게 판정된다. 타입마다 half-range가 다르므로 32비트 상수 `0x80000000`을 고정한 함수를 `uint16_t`에 재사용하면 wrap 판정이 깨진다. 정확히 half-range만큼 떨어진 두 번호는 어느 쪽이 최신인지 결정할 수 없으므로, 송신 윈도우가 그 거리까지 벌어지지 않게 제한해야 한다. 이 비교를 단순 `a > b`로 쓰는 순간 wraparound마다 연결이 한 번씩 미쳐 돌아가는 버그가 생기고, uint16 sequence를 60Hz로 쓰면 그 순간이 18분마다 온다.

수신 측은 ack를 만들기 위해 최근 수신 sequence들의 비트 윈도우를 유지한다.

```cpp
// 수신 기록: 슬라이딩 비트 윈도우. 중복 패킷 검출도 여기서 공짜로 얻는다.
class ReceivedPackets {
public:
    bool OnReceive(uint16_t seq) {              // 중복이면 false
        if (!initialized_) {
            latest_ = seq;
            initialized_ = true;
            return true;
        }
        if (seq == latest_) return false;        // 최신 패킷 자체의 중복
        if (IsMoreRecent(seq, latest_)) {
            uint16_t shift = seq - latest_;
            window_ = (shift >= 32) ? 0 : (window_ << shift);
            if (shift <= 32) window_ |= (1u << (shift - 1));   // 옛 latest 를 기록
            latest_ = seq;
            return true;
        }
        uint16_t diff = latest_ - seq;
        if (diff > 32) return false;            // 너무 낡음 — 통계만 남기고 버림
        uint32_t bit = 1u << (diff - 1);
        if (window_ & bit) return false;        // 중복
        window_ |= bit;
        return true;
    }
    uint16_t Ack() const { return latest_; }
    uint32_t AckBits() const { return window_; }
private:
    bool initialized_ = false;
    uint16_t latest_ = 0;
    uint32_t window_ = 0;
};
```

`initialized_`는 초기 상태를 실제 패킷 0과 구분한다. 또 `seq == latest_`를 `diff - 1` 비트 계산 전에 별도로 거르지 않으면, 최신 패킷의 중복 수신에서 `diff - 1`이 int로 승격되어 -1이 되고, 음수 시프트라는 미정의 동작이 발생한다. sequence 윈도우 코드는 첫 패킷·최신 중복·윈도우 경계·정확히 절반 거리·wrap 직전과 직후를 단위 테스트해야 한다.

같은 자료구조가 세 가지 일을 동시에 한다 — ack 생성, 중복 패킷 검출(재전송·네트워크 중복 모두), 그리고 손실률 측정(윈도우의 0 비트 비율). 송신 측은 돌아온 ack/ackBits로 "어떤 패킷이 도착했는가"를 알고, 그 패킷에 실었던 reliable 메시지들을 큐에서 지우며, `ack 받은 시각 - 보낸 시각`으로 RTT 표본까지 얻는다. 헤더 12바이트가 프로토콜 계측의 전부를 떠받치는 셈이다.

## 5. Reliable과 Unreliable Channel

Reliable channel은 메시지를 resend queue에 넣고, 상대가 ack할 때까지 보관한다. 일정 시간 동안 ack가 없으면 다시 보낸다. Unreliable channel은 resend queue에 넣지 않는다.

```cpp
struct ReliableMessage {
    uint16_t id;
    uint8_t channel;
    double firstSentTime;
    double lastSentTime;
    uint32_t sendCount;
    ByteBuffer payload;
};

void QueueReliable(uint8_t channel, ByteBuffer payload) {
    ReliableMessage msg;
    msg.id = nextReliableId_++;
    msg.channel = channel;
    msg.firstSentTime = -1.0;
    msg.lastSentTime = -1.0;
    msg.sendCount = 0;
    msg.payload = std::move(payload);
    resendQueue_.push_back(std::move(msg));
}
```

이 코드는 "즉시 보냈다"가 아니라 reliable payload를 재전송 관리 대상에 등록했다는 뜻이다. 실제 송신 루프는 congestion/window 정책, MTU 여유, 마지막 송신 시각을 보고 일부 메시지만 패킷에 싣는다. ack가 돌아오면 해당 메시지를 큐에서 제거하고, 오래 ack되지 않으면 재전송하거나 연결을 끊는다.

여기서 주의할 구분 — ack는 *패킷* 단위인데 신뢰성은 *메시지* 단위다. 한 패킷에 reliable 메시지 여러 개가 실리고, 같은 메시지가 (재전송으로) 여러 패킷에 실릴 수 있다. 그래서 송신 측은 "패킷 N에 메시지 {a, b, c}를 실었다"는 매핑을 유지하고, 패킷 N의 ack가 오면 a·b·c를 완료 처리한다. 메시지에 자체 `id`가 있는 이유이기도 하다 — 수신 측은 메시지 id로 중복 수신(서로 다른 패킷에 같은 메시지)을 걸러낸다.

상태 스냅샷을 reliable로 보내면 손실된 낡은 상태가 재전송되어 최신 상태 앞을 막는다. 실시간 상태는 unreliable로 보내고, reliable은 이벤트나 세션 상태에 제한해야 한다.

Reliable 데이터도 무제한 재전송하면 안 된다. 상대가 느려졌거나 끊겼을 때 resend queue가 계속 커진다. 큐 크기, 재전송 횟수, 연결 timeout, 중요도 정책이 필요하다. 실전 기준 몇 가지: 큐가 일정 크기(예: 수백 KB)를 넘으면 연결 품질의 문제이지 큐의 문제가 아니므로 연결을 끊는 쪽이 옳다. 재전송 횟수 상한(예: 10회)을 넘긴 메시지가 있다는 것도 같은 신호다. "reliable인데 버려도 되는가"라는 질문이 나오는 데이터가 있다면, 그것은 애초에 reliable이 아니라 *최신값만 의미 있는 상태* 였을 가능성이 높다 — 채널 분류를 다시 본다.

## 6. Ordered와 Unordered Delivery

Reliable과 ordered는 다른 개념이다. Reliable은 도착 보장이고, ordered는 애플리케이션에 넘기는 순서 보장이다.

| 정책 | 의미 | 예 |
|---|---|---|
| unreliable unordered | 도착한 것만 즉시 사용 | 위치 스냅샷 |
| unreliable sequenced | 더 최신 sequence만 사용 | 애니메이션 상태 |
| reliable unordered | 반드시 도착, 순서 무관 | 업적 알림 |
| reliable ordered | 반드시 도착, 순서 유지 | 인벤토리 변경, 채팅 |

`unreliable sequenced`는 구현이 한 줄이라 놓치기 쉽지만 쓸모가 많다 — "마지막으로 적용한 sequence보다 낡은 메시지는 버린다". 재정렬된 네트워크에서 낡은 애니메이션 상태가 최신 상태를 덮어쓰는 사고를 막는다.

`reliable ordered`의 수신 측은 작은 재조립 버퍼다.

```cpp
// ordered channel 수신: 다음에 넘길 id 가 오기 전까지 뒤 메시지를 대기시킨다.
void OrderedChannel::OnMessage(uint16_t id, ByteBuffer payload) {
    if (IsMoreRecent(nextDeliverId_, id)) return;       // 이미 전달분 — 중복
    pending_[id] = std::move(payload);
    while (pending_.count(nextDeliverId_)) {            // 연속 구간을 한꺼번에 전달
        Deliver(std::move(pending_[nextDeliverId_]));
        pending_.erase(nextDeliverId_++);
    }
}
```

Ordered channel은 빠진 메시지가 있으면 뒤 메시지를 대기시킨다. 이 대기 자체가 작은 Head-of-Line Blocking이다 — TCP를 버리고 왔는데 채널 안에 TCP의 축소판을 다시 만든 셈이다. 그래서 두 가지 규율이 따라온다. 첫째, ordered가 필요한 데이터만 별도 channel에 넣는다. 채팅 순서 때문에 위치 스냅샷이 막히면 설계가 잘못된 것이다. 둘째, ordered channel을 *여러 개* 둔다 — 채팅 채널의 유실이 인벤토리 채널을 막을 이유가 없다. 채널은 독립된 순서 도메인이고, ENet·Steam Networking 같은 라이브러리가 채널 수를 1급 파라미터로 두는 이유다.

## 7. 송신 루프: 패킷 한 장의 조립

지금까지의 부품 — 헤더, ack, 채널, resend queue — 이 실제로 만나는 곳은 틱마다 도는 송신 루프다. 패킷 한 장이 조립되는 순서가 곧 우선순위 정책이다.

```cpp
void Connection::SendPacket(double now) {
    PacketWriter w(buffer_, kMaxPayload);                // 예: 1200 바이트
    PacketHeader header = MakeHeader();                  // sequence/ack/ackBits
    w.WriteHeader(header);

    // 1) 재전송 기한이 지난 reliable 부터 — 가장 오래 기다린 것 우선
    for (auto& m : resendQueue_) {
        if (now - m.lastSentTime < ResendDelay(rtt_.Smoothed(), rtt_.Variance())) continue;
        if (!w.TryWriteMessage(m)) break;                // MTU 여유 없으면 다음 패킷으로
        m.lastSentTime = now; ++m.sendCount;
        sentRecord_[header.sequence].push_back(m.id);    // 패킷→메시지 매핑(5절)
    }
    // 2) 새 reliable 메시지
    // 3) 남은 공간에 unreliable 상태 (스냅샷 — 우선순위 누적기가 고른 엔티티들)
    FillSnapshot(w);

    socket_.Send(remote_, w.Data(), w.Size());
    bytesSentWindow_ += w.Size();                        // 대역폭 계측
}
```

순서의 논리: 재전송분은 이미 한 번 늦은 데이터라 가장 먼저, 새 reliable이 다음, 그리고 *남는 공간을 스냅샷이 채운다*. 스냅샷은 "가능한 한 많이"가 아니라 "남은 예산만큼"이다 — 어차피 다음 틱에 더 새 것이 온다. 이 구조 덕에 reliable 트래픽이 일시적으로 몰려도(예: 매치 시작 시 초기화 이벤트들) 스냅샷이 자연스럽게 양보하고, 평시에는 스냅샷이 패킷을 채운다.

한 틱에 한 패킷이 기본형이다. reliable이 넘쳐 한 패킷에 다 안 들어가면 다음 틱을 기다린다 — 틱당 여러 패킷을 쏘는 버스트는 큐 스파이크를 만들므로, 정말 필요하면 송신 간격을 나누는 페이싱과 함께 도입한다.

## 8. MTU와 재전송 정책

인터넷 경로의 안전한 UDP payload는 보수적으로 잡아야 한다. IPv4에서 1200바이트 안팎을 목표로 하면 다양한 경로에서 IP fragmentation 위험을 줄일 수 있다. fragmentation은 조각 하나만 사라져도 전체 datagram이 사라지므로 실시간 게임에서 피하는 편이 낫다. 1500(이더넷 MTU)이 아니라 1200인 이유는 경로 어딘가의 터널(VPN·PPPoE·모바일 캐리어)이 오버헤드를 먹기 때문이다 — QUIC도 같은 이유로 1200을 초기 가정으로 쓴다.

큰 데이터는 세 가지 중 하나로 처리한다.

- 처음부터 보내지 않는다. 실시간 상태에 큰 blob을 넣지 않는다.
- 여러 패킷으로 애플리케이션 레벨 분할을 한다. 청크에 (메시지 id, 청크 번호, 총 청크 수)를 붙이고 reliable로 보내면, 수신 측이 재조립한다. IP 단편화와 달리 청크 단위 재전송이 되고 진행률을 알 수 있다.
- TCP/HTTP 같은 별도 경로로 보낸다. 맵 다운로드·에셋 패치가 여기 속한다 — 실시간 채널의 일이 아니다.

재전송 간격은 RTT 기반으로 잡는다. 너무 빠르면 중복 패킷이 폭증하고, 너무 느리면 reliable 이벤트가 늦게 도착한다.

```cpp
double ResendDelay(double smoothedRtt, double rttVar) {
    return std::clamp(smoothedRtt + 4.0 * rttVar, 0.05, 0.50);
}
```

`SRTT + 4·RTTVAR`는 TCP RTO와 같은 발상이다. 여기서 RTTVAR는 통계학의 분산이나 표준편차가 아니라 RTT 편차를 평활한 추정치다. 하한 50ms는 LAN에서 ack가 다음 틱에 실려 오는 것을 기다리는 시간이고, 상한 500ms는 게임 정책의 예시일 뿐이다. 상한 때문에 나쁜 회선에서 재전송이 계속 발생할 수 있으므로, 실제 값은 혼잡 제어·재전송 횟수·연결 timeout과 함께 부하 테스트로 정해야 한다.

좋은 UDP 프로토콜은 손실을 없애지 않는다. 손실을 관측하고, 데이터별로 손실을 받아들이거나 복구한다. 그 경계가 게임의 반응성과 안정성을 결정한다.

## 9. 흔한 실패 모드

이 계층의 버그는 평시에 숨고 악조건에서 터진다. 자주 만나는 것들을 증상 기준으로 정리한다.

| 증상 | 원인 | 위치 |
|---|---|---|
| 18분(uint16)·약 2.3년(uint32)마다 연결 폭주 | wraparound 비교를 `a > b`로 씀 | 4절 IsMoreRecent |
| 손실률 오를수록 트래픽 폭증 | 재전송 간격이 고정·과소, 또는 스냅샷을 reliable로 | 5·8절 |
| 채팅이 밀리면 게임도 밀림 | ordered 채널 하나에 모든 데이터 | 6절 채널 분리 |
| 가끔 같은 이벤트 두 번 적용 | 메시지 중복 검출 누락(패킷 중복·재전송) | 4·5절 id 검사 |
| 특정 회선에서만 큰 패킷 전멸 | MTU 초과 → 단편화 → 방화벽이 단편 폐기 | 8절 1200B |
| 한쪽 일방 전송 시 reliable 영구 미달 | ack 운반 패킷 없음 — keepalive 미구현 | 3절 piggyback |

공통 교훈은 *나쁜 네트워크를 상시 켜고 개발하라* 는 것이다. 손실 5%·재정렬 1%·지연 100ms를 주입한 환경에서만 이 표의 버그들이 개발 중에 보인다.



## 10. TCP의 답을 못 쓰는 이유

Reno와 CUBIC 같은 loss-based congestion control은 drop이나 ECN 이전까지 sending window를 늘릴 수 있어, 큰 drop-tail buffer에서는 persistent queue와 bufferbloat를 만들 수 있다. 그러나 AQM, ECN, pacing, receive/application limit와 cross traffic에 따라 queue가 항상 가득 차는 것은 아니다. TCP의 목표도 단순히 “파일 처리량만 최대화”가 아니라 안정적이고 공정한 congestion control이며, 문제는 게임 snapshot의 수명·부분 신뢰성 요구를 byte-stream API가 표현하지 못한다는 데 있다.

게임 혼잡 제어의 목표는 거꾸로다.

| | loss-based byte stream | 게임 상태 전송 |
|---|---|---|
| 전달 의미 | 모든 byte를 순서대로 전달 | channel별 수명·신뢰성·순서 |
| 주요 품질 | goodput, fairness, completion | tail latency, freshness, correction |
| 혼잡 신호 | loss·ECN·RTT 등 알고리즘별 | loss·ECN·RTT·application backlog |
| 부하 감소 | congestion window·pacing | 전송률과 보내는 *내용* 을 함께 조정 |

마지막 줄이 게임의 고유한 무기다. TCP는 바이트 스트림이라 "덜 보내기 = 더 늦게 보내기"뿐이지만, 게임은 스냅샷 레이트를 낮추고, 우선순위 낮은 엔티티를 빼고, quantization을 거칠게 하는 식으로 *품질을 낮춰서* 바이트를 줄일 수 있다. 혼잡 제어가 전송 계층이 아니라 게임 계층까지 올라와야 하는 이유다.

## 11. 혼잡 신호: 무엇을 읽는가

회선이 나빠지는 순서는 보통 이렇다 — 큐가 차기 시작하면 *RTT가 먼저 오르고*, 큐가 넘치면 *손실이 난다*. 그래서 지연에 민감한 프로토콜은 손실보다 RTT 추세를 1차 신호로 삼는다.

```cpp
// 신호 추출 — RTT 추정기 위에 두 가지를 더 얹는다.
struct CongestionSignals {
    float rttBase;     // 관측된 최소 RTT (큐가 빈 상태의 기준선, 슬라이딩 윈도우 최솟값)
    float rttNow;      // 평활 RTT (EWMA)
    float lossRate;    // 최근 N초 손실률 (ack 비트필드로 계산)
    float QueueDelay() const { return rttNow - rttBase; }   // 큐잉 지연 추정
};
```

- **큐잉 지연** `rttNow - rttBase`: 가장 이른 경고. 기준선(rttBase)은 "큐가 비었을 때의 RTT"이므로 최근 수십 초의 최솟값으로 추정한다. 이 차이가 수십 ms를 넘으면 내가(또는 누군가) 큐를 채우고 있는 것이다.
- **손실률**: ack 비트필드에서 공짜로 얻는다. 단, [무선 링크](#15-무선-링크의-함정)는 혼잡 없이도 손실을 내므로 손실 단독으로 판단하면 오판한다.
- **지터**: 큐 길이의 변동. 급증하면 경로가 불안정하다는 보조 신호다.

신호는 모두 *추정* 이고 노이즈가 크다. 그래서 [이진 모드 정책](#13-이진-모드-정책-단순한-것부터)은 신호 하나에 즉각 반응하지 않고, 임계 초과가 *지속* 될 때만 모드를 바꾸는 히스테리시스를 둔다.

## 12. 조절 손잡이: 무엇을 줄이는가

줄일 수 있는 축은 셋이고, 각각 효과와 부작용이 다르다.

1. **패킷 레이트** (초당 패킷 수): 60Hz 송신을 30Hz, 20Hz, 10Hz로. 헤더·암호화 오버헤드까지 함께 줄어 효과가 크다. 부작용은 스냅샷 간격이 벌어져 보간 지연이 늘어나는 것 — 수신 측 보간 버퍼 깊이를 함께 키워야 한다.
2. **패킷 크기**: 우선순위 누적기에서 이번 패킷에 싣는 엔티티 수를 줄인다. 멀리 있는 것·안 보이는 것부터 빠지므로 체감 품질 저하가 가장 완만하다.
3. **인코딩 정밀도**: quantization을 거칠게(위치 1cm→5cm), 갱신 빈도가 낮아도 되는 필드(체력 등)를 더 듬성하게. 효과는 작지만 누적된다.

세 손잡이의 적용 순서는 장르와 encoder에 따라 다르다. packet rate를 낮추면 per-packet overhead는 줄지만 interpolation age가 늘고, entity를 빼면 AOI 경계가 보이며, quantization을 낮추면 가까운 대상의 오차가 커진다. quality ladder별 체감과 byte 절감량을 측정해 정책을 정한다. 입력은 보통 작고 latency-sensitive하므로 우선 보호하지만 client upload가 항상 무시할 수 있는 것은 아니다.

## 13. 이진 모드 정책: 단순한 것부터

가장 검증된 출발점은 good/bad 두 모드뿐인 정책이다.

```cpp
// 이진 혼잡 정책 — 단순하지만 프로덕션에서 검증된 형태.
void CongestionPolicy::Update(const CongestionSignals& s, double now) {
    const bool congested =
        s.QueueDelay() > configuredQueueDelay ||
        s.lossRate > configuredLossThreshold;

    if (mode_ == Mode::Good) {
        if (congested) {
            mode_ = Mode::Bad;             // 즉시 강등 — 나빠질 땐 빠르게
            badEnteredAt_ = now;
            penaltyTime_ = Clamp(penaltyTime_ * 2.0, 1.0, 60.0);  // 재발마다 배로
        }
    } else {
        // 복귀는 신중하게: penaltyTime 동안 깨끗해야 Good 으로.
        if (congested) badEnteredAt_ = now;                      // 카운터 리셋
        else if (now - badEnteredAt_ > penaltyTime_) {
            mode_ = Mode::Good;
            lastGoodAt_ = now;
        }
        // 오래 Good 을 유지하면 penalty 를 서서히 감면한다(생략).
    }
}
// Good: 60Hz·전체 예산.  Bad: 20~30Hz·예산 절반.
```

설계의 요점은 비대칭이다 — *강등은 즉시, 복귀는 천천히, 재발할수록 더 천천히*. 회선이 경계 근처에서 진동할 때 모드가 펄럭이는 것(flapping)을 penaltyTime의 지수 증가가 막는다. 이 단순한 정책의 가치는 예측 가능성이다 — 두 모드 각각에서의 트래픽 형태가 고정이라 테스트와 디버깅이 쉽다.

모드를 3~4단(60/30/20/10Hz)으로 늘린 사다리형이 그 다음 단계다. 원리는 같고, 단 사이의 히스테리시스만 단마다 둔다.

## 14. 대역폭 추정과 페이싱

이진 정책은 "나쁘다/좋다"만 안다. 더 나아가면 *지금 회선이 초당 몇 바이트를 받아 주는가* 를 직접 추정해 송신 예산을 연속적으로 맞출 수 있다.

수신 측 delivered-byte feedback은 delivery rate 표본을 만드는 한 재료다. application-limited 구간, ACK compression, loss와 feedback interval을 구분하지 않으면 수신량이 곧 bottleneck capacity라는 결론은 틀린다. BBR도 delivery rate와 minimum RTT를 model에 쓰지만 단순한 최근 1초 수신량 비교와 동일한 알고리즘은 아니다. 게임용 구현은 quality SLO를 지킬 headroom을 두고 제한된 probe로 회복을 탐색할 수 있다. headroom은 70~80% 같은 보편 상수가 아니라 estimator bias, cross traffic와 queue-delay 목표로 정한다.

**페이싱**은 추정과 짝을 이루는 송신 습관이다. 틱마다 모인 패킷을 한 번에 버스트로 쏘면 짧은 큐 스파이크가 생긴다 — 같은 바이트라도 틱 간격에 고르게 흩뿌리면(예: 60Hz 틱에 3패킷이면 약 5.6ms 간격) 큐 점유가 낮아진다. 소켓 송신을 타이머로 미루는 한 겹의 큐만 있으면 된다.

### 운전점의 정량 — BDP와 페이싱의 큐 비용

BDP 모델은 왜 inflight와 pacing을 함께 봐야 하는지 설명한다. 다만 다음 식은 고정 capacity의 단일 work-conserving bottleneck, cross traffic 없음, 정확한 propagation RTT, 충분한 receiver·application 처리라는 fluid-model 가정 위에 있다.

전제는 두 양이다(전제: 11절의 신호 — `rttBase`가 곧 아래 $\text{RTprop}$의 추정이다). 큐가 비었을 때의 최소 왕복 전파 지연 $\text{RTprop}$과 경로 병목 대역폭 $\text{BtlBw}$. 파이프의 "부피"는 둘의 곱, 대역폭-지연 곱(BDP)이다.

$$\text{BDP}=\text{BtlBw}\times\text{RTprop}$$

이 이상화된 모델에서 inflight를 늘리면 BDP 부근을 경계로 다음 관계가 나타난다.

- $\text{inflight}\le\text{BDP}$: sender 자체의 inflight가 만든 persistent queue는 없고, delivery rate는 대략 $\text{inflight}/\text{RTprop}$까지 증가한다.
- $\text{inflight}>\text{BDP}$: 초과분이 같은 bottleneck queue에 쌓인다는 가정 아래 delivery rate는 $\text{BtlBw}$ 부근에서 포화되고, 추가 queue delay는 대략 $(\text{inflight}-\text{BDP})/\text{BtlBw}$다.

이상화된 steady state에서는 BDP 부근이 bottleneck을 idle시키지 않으면서 sender가 만든 persistent queue를 최소화하는 기준점이다. 실제 경로에서는 estimator error, ACK aggregation, receiver window, AQM과 cross traffic 때문에 단일 “유일한 점”이 아니다. BBR 계열도 버전별 gain cycle과 inflight bound를 가진 model-based controller이며 항상 정확히 1 BDP에 머무는 알고리즘이 아니다.

설명용 fluid model에서 $\text{BtlBw}=1$ Mbit/s, $\text{RTprop}=100$ms이면 BDP는 12.5kB다. sender가 만든 inflight만 25kB이고 다른 제약이 없다면 초과 12.5kB가 약 100ms의 queue를 만들 수 있다. 그러나 pacing rate를 추정 capacity 아래로 둔 것만으로 queue가 항상 비어 있거나 inflight가 자동으로 BDP 이하가 되지는 않는다. 이미 쌓인 inflight, burst, cross traffic, 낮아진 실제 capacity와 estimator error를 함께 제어해야 한다.

페이싱이 별도의 손잡이인 이유는 *평균* 전송률이 병목 이하라도 큐가 생길 수 있기 때문이다. 한 틱의 묶음 $S$바이트를 회선 라인레이트 $L(\gg$ 병목 $C)$로 한 번에 쏟으면, 병목이 비우는 속도보다 빨리 도착해 일시 큐가 쌓인다. 버스트가 도착하는 동안 큐는 $(L-C)$의 속도로 차올라 최대

$$Q_{\text{peak}}=S\left(1-\frac{C}{L}\right)\approx S\quad(L\gg C)$$

에 이르고, 그 burst 뒤 packet에 최대 약 $S/C$ 규모의 serialization delay를 더할 수 있다. 같은 $S$를 $T$ 동안 $S/T\le C$로 이상적으로 흩뿌리면 **이 송신자가 새로 만드는** queue를 줄일 수 있다. cross traffic과 OS/NIC batching이 있으면 $Q_{\text{peak}}=0$은 보장되지 않는다. 핵심은 평균 이용률뿐 아니라 순간 arrival rate와 burst size도 queue를 결정한다는 점이다.

실패 경계는 추정의 정합성에 있다. $\text{BtlBw}$·$\text{RTprop}$ 추정이 틀리면 BDP 목표도 틀린다. 경로가 바뀌면 오래된 minimum RTT를 재검증해야 하고, ACK compression은 순간 delivery-rate 표본을 부풀릴 수 있다. headroom, sample filtering과 probe 정책은 이 오차의 실측 분포에서 정한다.

## 15. 무선 링크의 함정

유선 가정으로 만든 혼잡 제어는 무선에서 오판한다.

- **비혼잡 손실 가능성**: 와이파이 간섭·무선 link 변화는 end-to-end queue overflow가 아닌 손실을 만들 수 있다. 반대로 AQM은 queue가 크게 오르기 전에 drop·mark할 수 있다. RTT와 loss의 조합은 분류 신호이지 원인을 완벽히 식별하는 증명이 아니다.
- **버스트 지연**: 셀룰러는 수백 ms 동안 아무것도 안 보내다가 몰아서 배달하는 버스트 패턴을 보인다(스케줄러·절전 때문). 이 동안 RTT 표본이 한꺼번에 튀므로, 단발 스파이크가 아니라 *지속 추세* 에만 반응해야 한다.
- **핸드오버**: 기지국 전환·와이파이↔셀룰러 전환은 짧은 지터부터 연결 단절까지 다양한 결과를 만든다. 주소·경로가 바뀌면 migration 또는 reconnect와 새 기준선 추정이 필요하다.

모바일 비중이 큰 게임이라면 혼잡 정책의 기본 상수 자체를 무선 기준으로 잡는 편이 낫다 — 임계는 더 너그럽게, 히스테리시스는 더 길게.

## 16. 검증: 혼잡 제어를 테스트하는 법

혼잡 제어는 평균 환경에서는 아무 일도 안 하는 코드다. 검증은 나쁜 회선의 재현에서 시작한다.

```bash
# HTB class로 1Mbit/s 병목을 만들고 그 아래 netem으로 지연·지터·손실 주입
tc qdisc add dev eth0 root handle 1: htb default 10
tc class add dev eth0 parent 1: classid 1:10 htb rate 1mbit ceil 1mbit
tc qdisc add dev eth0 parent 1:10 handle 10: netem delay 50ms 10ms loss 1%
```

봐야 할 그래프는 셋이다. (1) 송신 레이트와 모드 전이 — 병목보다 빨리 보내기 시작하면 Bad로 강등되는가, 회선이 회복되면 penaltyTime 후 복귀하는가. (2) RTT 시계열 — 정책이 동작할 때 큐잉 지연이 실제로 내려가는가(이게 존재 이유다). (3) 체감 지표 — 보간 끊김 횟수, 스냅샷 적용 지연. 송신량을 줄였는데 체감이 오히려 나아지는 구간이 보이면 정책이 옳게 동작하는 것이다.

흔한 버그 둘. 모드 전이 조건에 히스테리시스가 없어 1초에 수십 번 펄럭이는 것, 그리고 레이트를 낮출 때 수신 측 보간 버퍼를 함께 못 키워 끊김이 생기는 것 — 혼잡 제어는 송신만의 일이 아니라 송수신이 함께 바뀌는 모드 전환이다.


---

## 결론

게임 전송 프로토콜의 목표는 모든 데이터를 반드시 보내는 것이 아니라, 각 메시지가 유효한 시간 안에 필요한 확률과 순서로 도착하게 만드는 것이다. 신뢰성·순서·혼잡 제어를 분리하고 실제 네트워크 조건에서 검증해야 한다.

## 더 읽을거리

- Glenn Fiedler, [Building a Game Network Protocol](https://gafferongames.com/categories/building-a-game-network-protocol/) — sequence/ack bitfield/패킷 단편화를 코드 수준으로 다루는 시리즈.
- Valve Developer Wiki, [Source Multiplayer Networking](https://developer.valvesoftware.com/wiki/Source_Multiplayer_Networking) — 신뢰성 정책 분리의 실전 레퍼런스 구현.
- [ENet](http://enet.bespin.org/) — 채널·신뢰성 플래그를 가진 게임용 UDP 라이브러리(이 글 모델의 실물).
- Fabien Sanglard, [Quake 3 Network Model](https://fabiensanglard.net/quake3/network.php) — unreliable 스냅샷 중심 프로토콜의 원형 분석.
- [Valve GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets) — 채널·신뢰성·암호화를 갖춘 현대적 구현체.
- Glenn Fiedler, ["Reliability and Congestion Avoidance over UDP"](https://gafferongames.com/post/reliability_ordering_and_congestion_avoidance_over_udp/) — 이진 모드 정책의 원형.
- [RFC 8085: UDP Usage Guidelines](https://www.rfc-editor.org/rfc/rfc8085) — UDP 응용이 지켜야 할 혼잡 제어 의무의 표준 정리.
- Neal Cardwell 외, ["BBR: Congestion-Based Congestion Control"](https://queue.acm.org/detail.cfm?id=3022184) (ACM Queue 2016) — 대역폭·RTT 직접 추정 모델의 원전. BDP 운전점·ProbeBW 페이싱 게인의 출처.
- Leonard Kleinrock, "Power and Deterministic Rules of Thumb for Probabilistic Problems in Computer Communications" (Proc. ICC 1979) — 처리량/지연 power 최대점이 BDP에서 성립함을 보인 원전.
- Jim Gettys, Kathleen Nichols, "Bufferbloat: Dark Buffers in the Internet" (ACM Queue 2011) — 큐잉 지연 문제의 명명과 분석.
