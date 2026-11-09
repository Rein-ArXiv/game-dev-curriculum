## 들어가며

QUIC은 TCP의 기능을 UDP 위에 다시 구현한 프로토콜이라고만 설명하기 쉽다. 그러나 중요한 차이는 “UDP를 쓴다”가 아니라 전송 상태 기계, TLS 1.3, 다중 스트림, 경로 변경을 사용자 공간의 하나의 암호화된 연결 계약으로 묶었다는 점이다.

게임 개발자가 QUIC을 볼 때도 두 극단을 피해야 한다.

- 표준 구현이 있으니 모든 실시간 게임 트래픽에 자동으로 적합하다는 주장
- 자체 UDP만이 지연과 패킷 제어를 보장한다는 주장

판단하려면 QUIC이 무엇을 보장하고 무엇을 보장하지 않는지 알아야 한다. 스트림별 순서가 전송 계층 HoL의 범위를 줄이지만 연결 전체의 혼잡을 없애지는 않는다. DATAGRAM frame은 재전송되지 않지만 혼잡 제어 밖의 무제한 송신 통로가 아니다. 0-RTT는 왕복을 줄이지만 replay될 수 있다. Connection ID는 주소 변경을 흡수하지만 새 경로를 검증하기 전 증폭 제한을 받는다.

이 글은 QUIC의 packet·frame·stream·connection 상태를 분리하고, loss detection, flow·congestion control, migration, DATAGRAM, WebTransport·WebRTC·WebSocket의 의미를 게임 데이터의 수명과 연결한다.

---

## 목차

1. [사용자 공간 전송과 ossification](#1-사용자-공간-전송과-ossification)
2. [Packet과 frame의 분리](#2-packet과-frame의-분리)
3. [ACK range와 loss detection](#3-ack-range와-loss-detection)
4. [Stream과 flow control](#4-stream과-flow-control)
5. [TLS 1.3·0-RTT·증폭 제한](#5-tls-130-rtt증폭-제한)
6. [Connection ID와 경로 이동](#6-connection-id와-경로-이동)
7. [DATAGRAM과 게임 메시지](#7-datagram과-게임-메시지)
8. [혼잡 제어·pacing·MTU](#8-혼잡-제어pacingmtu)
9. [브라우저 전송 API의 의미](#9-브라우저-전송-api의-의미)
10. [게임 적용과 검증](#10-게임-적용과-검증)

---

## 1. 사용자 공간 전송과 ossification

TCP는 운영체제 커널과 네트워크 중간 장비에 깊게 배포되어 있다. 새로운 TCP option이나 동작이 표준화되어도 NAT, firewall, load balancer가 예상하지 못한 패킷을 버리거나 수정하면 실제 배포가 어렵다. 이를 protocol ossification이라고 한다.

새 IP protocol number를 사용하는 전송은 더 강한 배포 장벽을 만난다. 많은 NAT가 TCP와 UDP만 이해하기 때문이다. QUIC은 다음 선택을 결합한다.

- UDP payload로 운반해 기존 NAT 경로를 사용
- 전송 상태 기계를 사용자 공간에서 구현해 응용 배포 속도로 갱신
- 대부분의 transport metadata를 인증·암호화해 중간 장비의 의미 의존을 줄임
- version negotiation과 transport parameter로 기능 진화를 명시

이 구조는 UDP가 방화벽을 항상 통과한다는 뜻이 아니다. UDP가 차단된 경로, 유휴 NAT mapping timeout, UDP 443 rate limit은 여전히 존재한다. 연결 시도는 timeout과 fallback 정책을 가져야 한다.

### UDP가 제공하는 것과 QUIC이 추가하는 것

UDP가 제공하는 핵심은 port multiplexing, datagram 경계, checksum이다. QUIC은 그 payload 안에서 다음을 만든다.

```text
UDP datagram
  QUIC packet
    protected header
    encrypted frames
      STREAM / ACK / CRYPTO / DATAGRAM / ...
```

QUIC library가 UDP socket을 사용한다고 응용이 UDP packet을 임의로 조립하는 것은 아니다. packetization, retransmission unit, pacing 시점은 transport implementation이 소유할 수 있다. 게임이 한 tick의 여러 메시지를 어떤 packet에 함께 넣을지 정밀 제어해야 한다면 library API가 그 요구를 제공하는지 확인해야 한다.

## 2. Packet과 frame의 분리

QUIC packet은 암호화와 전달의 단위이고, frame은 transport 의미의 단위다. 하나의 UDP datagram에 여러 QUIC packet이 들어갈 수 있고, 하나의 QUIC packet에는 여러 frame이 들어갈 수 있다.

대표 frame:

| frame | 의미 | 재전송 대상 |
|---|---|---|
| `CRYPTO` | handshake byte range | 손실 시 다시 전송 |
| `STREAM` | 특정 stream의 byte range | 손실 시 다시 전송 |
| `ACK` | 받은 packet number range | ACK 자체는 동일 frame 재전송 대상 아님 |
| `MAX_DATA` | connection flow-control credit | 최신 상태가 필요 |
| `MAX_STREAM_DATA` | stream별 credit | 최신 상태가 필요 |
| `PATH_CHALLENGE` | 새 경로 검증 | 필요 시 다시 전송 |
| `CONNECTION_CLOSE` | 연결 종료 | 종료 절차에 따라 반복 가능 |
| `DATAGRAM` | 비신뢰 응용 메시지 | 재전송하지 않음 |

손실된 것은 packet이지만 재전송하는 것은 그 packet의 바이트 복사본이 아니다. 손실 packet 안에서 아직 의미가 남은 `STREAM`·`CRYPTO` data를 새 packet number의 packet에 다시 싣는다.

이 구분이 nonce 안전성과 ACK 해석에 중요하다. 같은 packet number를 재사용하지 않고, ACK는 원래 packet 전달 여부를 말한다. 응용 데이터가 재전송되어 도착했는지는 stream offset 상태로 판단한다.

### Packet number space

handshake 단계는 Initial, Handshake, Application Data packet number space를 분리한다. 각 공간의 ACK와 loss detection도 독립적이다. handshake key가 폐기되면 해당 공간의 상태를 더 이상 application packet처럼 다루지 않는다.

```cpp
struct PacketSpaceState {
    PacketNumber nextPacketNumber;
    AckRanges received;
    SentPacketMap outstanding;
    TimePoint lossTime;
    Duration pto;
};
```

실제 QUIC 구현은 packet number를 wire에 일부 비트만 보내고 수신자가 예상 범위에서 전체 번호를 복원한다. 응용이 그 절단된 값을 session sequence로 재사용해서는 안 된다.

## 3. ACK range와 loss detection

QUIC ACK는 받은 packet number들의 range를 표현한다.

```text
received: 100 101 102 104 105 108
ACK ranges: [108,108], [104,105], [100,102]
```

누적 ACK 하나보다 재정렬과 여러 손실 구멍을 정밀하게 표현한다. 그러나 ACK frame이 모든 packet마다 즉시 전송된다는 뜻은 아니다. ACK delay와 ACK frequency는 packet overhead와 feedback 지연을 교환한다.

### RTT 표본

RTT 표본은 두 조건을 모두 만족할 때만 만든다 — ACK frame의 largest acknowledged가 *새로* ACK된 번호여야 하고, 그 ACK로 새로 확인된 패킷들 중 ack-eliciting이 하나 이상 있어야 한다. 둘은 서로를 대체하지 않으며, 이미 ACK했던 largest PN을 반복 표본으로 쓰면 안 된다. 이때 latest RTT의 송신 시각은 largest acknowledged의 송신 시각이다(largest 자체가 ack-eliciting일 필요는 없다).

$$
\operatorname{latestRTT}=t_{\text{ackReceived}}-t_{\text{sent}}
$$

Application Data 공간에서는 peer가 보고한 ACK delay를 제한된 범위에서 뺄 수 있다. handshake RTT에서는 공격이나 잘못된 delay가 handshake를 왜곡하지 않도록 다르게 처리한다.

smoothed RTT와 변동은 지수 이동 평균으로 갱신한다.

$$
\operatorname{rttvar}\leftarrow
\frac34\operatorname{rttvar}
+\frac14|\operatorname{smoothedRTT}-\operatorname{adjustedRTT}|
$$

$$
\operatorname{smoothedRTT}\leftarrow
\frac78\operatorname{smoothedRTT}
+\frac18\operatorname{adjustedRTT}
$$

위 순서는 RFC 6298 계열처럼 편차 갱신에 갱신 전 `smoothedRTT`를 쓰는 변형이다. RFC 9002에 인쇄된 순서는 `smoothedRTT`를 먼저 갱신한 뒤 그 값으로 variance 표본을 계산하며, 같은 입력에서 두 식은 다른 값을 낸다. 따라서 순서를 자동으로 바꾸지 않고, 어떤 RFC 판본·errata·실제 library 구현을 따르는지 먼저 정한 뒤 그 규약에 맞춰 고정한다 — "TCP와 QUIC에서 완전히 동일하다"는 귀속만으로는 순서를 확정할 수 없다.

### 손실 판정

packet threshold와 time threshold를 함께 사용한다. 둘 모두 해당 packet number space에서 아직 ACK되지 않은(in-flight) packet에 적용되고, 공통으로 "그 packet보다 뒤에 전송한 packet이 ACK되었다"는 조건이 필요하다.

- packet threshold: 같은 공간에서 번호가 threshold(기본 3) 이상 앞선 packet이 ACK되었으면 이전 packet을 손실로 판정
- time threshold: 같은 공간에서 그 packet보다 뒤에 전송한 packet이 ACK되었고, 그 packet의 전송 시각으로부터 RTT에 비례한 loss_delay가 지났으면 손실로 판정

즉 time threshold는 전송 후 timeout만으로 발동하지 않는다 — ACK가 전혀 없으면 번호·시간 threshold 어느 쪽도 packet을 손실로 만들지 않는다.

재정렬이 심한 경로에서 너무 작은 threshold는 불필요한 재전송을 만든다. 너무 큰 threshold는 recovery를 늦춘다.

### PTO

명확한 손실 증거가 없어도 ACK가 오지 않을 수 있다. Probe Timeout은 ack-eliciting probe를 보내 연결이 멈춘 상태에서 빠져나오게 한다.

$$
\operatorname{PTO}
=\operatorname{smoothedRTT}
+\max(4\operatorname{rttvar}, kGranularity)
+\operatorname{maxAckDelay}
$$

이 식의 `maxAckDelay`는 Application Data 공간에서 확인된 handshake 이후 적용하는 항이다. Initial·Handshake 공간에서는 더하지 않는다. 실제 timer는 packet number space, handshake 상태와 outstanding ack-eliciting packet을 함께 고려한다.

연속 PTO에서 timer를 지수적으로 늘려 네트워크를 더 압박하지 않는다. PTO가 만료되어 ack-eliciting probe를 보내는 것과 outstanding packet을 lost로 선언하는 것은 다르다 — probe는 새 번호로 보내고, probe나 뒤 packet에 대한 ACK가 돌아온 뒤에야 packet threshold·time threshold로 손실을 판정한다. PTO 하나로 모든 outstanding packet을 즉시 손실 처리하지 않는다.

### 응용 RTT와 transport RTT

transport RTT가 낮아도 응용 response는 server queue와 tick gate를 기다릴 수 있다.

$$
T_{\text{input-to-authority}}
=T_{\text{sendQueue}}+T_{\text{network}}
+T_{\text{serverTickWait}}+T_{\text{simulation}}
$$

QUIC library의 RTT 하나로 게임 입력 지연을 설명하지 않는다.

## 4. Stream과 flow control

QUIC stream은 독립적인 ordered byte stream이다. stream $s$의 offset 1000~1099가 손실되면 그 stream은 그 구간 뒤의 바이트를 응용에 순서대로 넘길 수 없다. 다른 stream의 연속 바이트는 전달할 수 있다.

```text
stream 4: [0..999][missing][1100..]  -> stream 4 대기
stream 8: [0..500 complete]          -> 응용 전달 가능
```

이것이 transport-level HoL 범위를 줄인다는 의미다. 다음 제한은 남는다.

- 두 stream frame이 같은 packet에 있었고 packet이 손실되면 둘 다 손실
- 연결의 congestion window가 줄면 모든 stream 송신 속도가 영향을 받음
- connection-level flow control이 막히면 모든 stream이 정지할 수 있음
- 응용이 stream들을 다시 하나의 순서로 기다리면 application HoL이 생김

### 두 단계 flow control

수신자는 connection 전체 `MAX_DATA`와 각 stream의 `MAX_STREAM_DATA` credit를 알린다. 여기서 사용량은 실제로 도착한 payload byte의 합이 아니라, 각 stream이 어디까지 도달했는지 — 최고 수신 end offset 또는 reset/종료 시의 final size — 로 회계한다.

$$
\sum_s \operatorname{highestOffset}_s
\le \operatorname{connectionCredit}
$$

$$
\operatorname{highestOffset}_s
\le \operatorname{streamCredit}_s
$$

따라서 `offset=1023, length=1`인 STREAM은 새 byte가 $1$개뿐이어도 그 stream이 소모한 flow-control 범위는 $1024$다. 중간에 hole이 있어도 최고 offset 기준으로 세고, 재전송·중복 수신 byte를 다시 가산하지 않는다. 수신 응용이 데이터를 읽지 않아 credit를 갱신하지 않으면 송신자는 막힌다. “네트워크가 느리다”가 아니라 소비자가 backpressure를 건 상태다.

### Stream 수명

stream ID는 initiator와 방향을 표현한다. stream을 무제한 만들 수 없으며 peer가 허용한 최대 stream 수를 지켜야 한다. 요청마다 stream을 만들고 닫지 않으면 stream limit과 메모리를 소모한다.

신뢰 메시지를 stream 하나에 계속 이어 붙이면 이전 메시지 손실이 뒤 메시지를 막는다. 메시지 수명과 순서 도메인에 따라 stream을 분리하되, stream 수와 scheduling overhead를 함께 측정한다.

## 5. TLS 1.3·0-RTT·증폭 제한

QUIC은 TLS 1.3 handshake를 transport 안에 통합한다. TLS record를 TCP 위에 싣는 형태가 아니라 `CRYPTO` frame으로 handshake byte를 전달하고 packet protection key를 단계별로 만든다.

### 1-RTT

정상 신규 연결은 server identity를 검증하고 application key를 확립한 뒤 1-RTT application data를 안전하게 교환한다. handshake 완료와 transport parameter 검증 전에는 사용할 수 있는 기능과 신뢰 수준이 다르다.

### 0-RTT replay

재접속 클라이언트는 이전 세션 정보로 0-RTT early data를 보낼 수 있다. 이 데이터는 forward secrecy와 replay 보장에서 1-RTT data와 같지 않다. 공격자가 유효한 early packet을 다시 보낼 수 있다고 가정한다.

0-RTT에 적합한 예:

- 읽기 전용 상태 질의
- 같은 idempotency key로 중복 제거되는 요청
- replay되어도 권한·재화 상태가 변하지 않는 hint

부적합한 예:

- 아이템 구매
- 보상 수령
- 매치 참가 확정
- nonce 없이 상태를 증가시키는 command

“UDP라 중복될 수 있다”와 “암호학적으로 유효한 요청이 replay될 수 있다”는 다른 문제다. application transaction ID와 replay policy가 필요하다.

### 주소 검증과 anti-amplification

서버는 source address가 위조된 작은 요청에 큰 응답을 보내 DDoS 반사기가 될 수 있다. 주소를 검증하기 전 server가 보낼 수 있는 양을 받은 양의 일정 배수로 제한한다. QUIC의 기본 제한은 3배다.

```text
client -> server: 1200 bytes
unvalidated server budget: at most 3600 bytes
```

큰 인증서 chain이나 handshake data가 이 예산을 넘으면 client의 추가 packet을 기다려야 할 수 있다. 첫 연결 지연을 분석할 때 RTT뿐 아니라 amplification budget도 본다.

## 6. Connection ID와 경로 이동

TCP 연결은 4-tuple에 강하게 묶인다. QUIC은 Connection ID로 연결을 식별해 NAT rebinding이나 Wi-Fi·cellular 전환에서 주소가 바뀌어도 같은 연결 상태를 찾을 수 있다.

Connection ID가 있다고 새 주소를 즉시 신뢰하는 것은 아니다.

1. 새 source address에서 유효하게 보호된 packet을 받는다.
2. `PATH_CHALLENGE`를 보낸다.
3. peer가 같은 data를 `PATH_RESPONSE`로 돌려준다.
4. 새 경로의 reachability를 확인한다.
5. RTT, MTU, congestion 상태를 경로 정책에 맞게 갱신한다.

경로 검증 전에는 anti-amplification 제한이 적용될 수 있다.

### Connection ID의 개인정보

고정 ID를 오래 노출하면 주소가 바뀌어도 같은 사용자를 추적할 수 있다. peer가 여러 Connection ID를 발급하고 endpoint가 적절히 교체하며 retired ID를 재사용하지 않는 이유다.

### Stateless reset

서버가 연결 상태를 잃었지만 endpoint가 계속 packet을 보내면, 서버는 상태를 복구하지 않고 stateless reset token으로 연결이 사라졌음을 알릴 수 있다. token은 추측되면 연결 종료 공격에 쓰이므로 충분히 비밀스럽고 예측 불가능해야 한다.

### Migration이 해결하지 않는 것

- 새 경로의 RTT와 bandwidth 변화
- cellular 전환의 과금·전력 비용
- server process 자체의 이동
- connection state를 공유하지 않는 load balancer
- application session의 권한 만료

transport 연결 유지와 게임 세션 권위는 별도 계약이다.

## 7. DATAGRAM과 게임 메시지

QUIC DATAGRAM extension은 같은 보안·연결·혼잡 제어 문맥 안에서 비신뢰 메시지를 보낸다.

보장하지 않는 것:

- 도착
- 중복 제거
- 순서
- 재전송
- application fragmentation 복구

제공하는 것:

- QUIC 연결의 인증·암호화
- congestion control과 pacing 적용
- stream과 같은 경로·Connection ID 사용
- datagram 경계

### 크기 제한

DATAGRAM frame은 하나의 QUIC packet에 들어가야 한다. peer가 알린 maximum datagram frame size와 현재 path MTU를 넘지 않아야 한다.

```cpp
const size_t payloadBudget =
    path.maxUdpPayload -
    quicPacketOverhead -
    datagramFrameOverhead;
```

큰 snapshot을 응용에서 여러 datagram으로 나누면 message ID, fragment index/count, total length, timeout, memory cap이 필요하다. 실시간 상태는 오래된 fragment를 재조립하기보다 더 최신 snapshot으로 대체하는 편이 나을 수 있다.

### 최신값 우선

위치 snapshot처럼 수명이 짧은 데이터는 send queue에서 coalescing한다.

```cpp
pendingTransforms[entity] = newestTransform;
```

queue가 밀렸을 때 오래된 10개 transform을 모두 보내면 bandwidth를 소비하고 수신자는 이미 낡은 상태를 처리한다. 반면 fire command나 inventory delta는 같은 방식으로 덮어쓰면 안 된다.

### Stream과 DATAGRAM 배치

| 데이터 | 적합한 의미 |
|---|---|
| 로그인·권한 결과 | 신뢰 stream |
| 채팅 | 신뢰·순서 stream |
| 월드 baseline | 신뢰 stream 또는 chunked transfer |
| 주기 snapshot | DATAGRAM |
| 반복 가능한 input sample | sequence를 가진 DATAGRAM |
| 경제 transaction | 신뢰 stream + application idempotency |

신뢰 stream을 쓴다고 application transaction이 exactly-once가 되는 것은 아니다. 연결이 끊긴 뒤 응답을 못 받은 요청의 결과는 여전히 불확실할 수 있다.

## 8. 혼잡 제어·pacing·MTU

QUIC도 공유 네트워크에서 혼잡을 유발할 책임이 있다. UDP 위라는 이유로 congestion control을 생략하지 않는다.

송신 가능량은 대략 congestion window와 bytes in flight의 차이다.

$$
B_{\text{congestion}}\le
\max(0,\operatorname{cwnd}-\operatorname{bytesInFlight})
$$

이는 일반 송신의 근사 예산이다. PTO probe처럼 명세가 congestion window를 일시적으로 초과해 보낼 수 있게 하는 예외는 따로 처리한다.

STREAM frame은 여기에 stream·connection flow-control credit까지 추가로 지켜야 한다.

$$
B_{\text{stream}}\le
\min(B_{\text{congestion}},B_{\text{flow}})
$$

그러나 이 min은 STREAM에만 적용되는 상한이다. QUIC DATAGRAM은 stream/connection flow-control limit에 포함되지 않으므로, 모든 frame에 하나의 `min(cwnd, flowCredit)`을 공통 송신 예산으로 쓰면 STREAM credit이 $0$일 때 DATAGRAM까지 막힌다. DATAGRAM에는 congestion·pacing 공통 예산에 더해 peer가 광고한 최대 크기, API queue·size·expiry 한도를 별도로 적용한다.

### Pacing

cwnd만 지키면서 한꺼번에 burst하면 access link queue를 채운다. pacing은 packet 간격을 나눠 전송한다.

$$
\operatorname{pacingRate}
\approx \frac{\operatorname{cwnd}}{\operatorname{smoothedRTT}}
$$

게임 tick 직후 모든 client packet을 동시에 보내는 서버는 transport pacing과 별개로 application burst를 만들 수 있다. client별 send phase를 분산하거나 우선순위 scheduler를 사용한다.

### Stream 우선순위

QUIC transport가 여러 stream을 제공해도 어떤 stream을 먼저 packetize할지는 API와 구현 정책에 달려 있다. 큰 asset stream이 작은 control stream의 CPU queue와 congestion budget을 독점하지 않도록 priority와 bandwidth partition을 확인한다.

### PMTU와 1200바이트

QUIC은 Initial datagram에 최소 1200바이트 규칙을 사용하고 endpoint는 그 크기를 처리할 수 있어야 한다. 1200은 모든 application packet을 영원히 고정해야 하는 숫자가 아니다. Datagram Packetization Layer PMTU Discovery로 더 큰 path payload를 검증할 수 있다.

검증되지 않은 큰 packet 손실을 congestion loss와 혼동하지 않도록 probe 크기와 상태를 분리한다. ICMP가 막힐 수 있으므로 black-hole recovery도 필요하다.

## 9. 브라우저 전송 API의 의미

브라우저는 native UDP socket을 직접 노출하지 않는다. 선택지는 API가 제공하는 의미로 비교한다.

| API | 기반 | 신뢰·순서 | 상대 구조 |
|---|---|---|---|
| WebSocket | TCP (+ TLS, wss) | 신뢰·순서 message delivery; 연결 전체 HoL | client-server |
| WebRTC DataChannel | SCTP + DTLS + ICE/UDP | channel별 ordered·reliability 설정 | peer 또는 server |
| WebTransport | HTTP/3/QUIC 계열 | HTTP/3 위의 stream + datagram mapping | client-server |

지원 여부는 브라우저·플랫폼·배포 시점에 따라 달라질 수 있으므로 런타임 capability detection과 실제 target matrix로 확인한다.

### WebSocket

API가 단순하고 폭넓게 배포되어 있다. 하부는 TCP byte stream이지만 WebSocket 자체는 frame fragmentation을 재조립한 data message 단위 의미를 제공한다. 연결 전체의 ordered delivery와 한 손실 구멍의 HoL을 가진다. 로비, 채팅, 턴제, 낮은 빈도의 control에는 충분할 수 있다.

### WebRTC DataChannel

unordered와 제한된 retransmission을 설정할 수 있어 실시간 의미를 표현할 수 있다. 대신 signaling, ICE, DTLS, SCTP 상태가 결합되고 NAT traversal·relay 구성이 필요하다. “P2P API”로만 보지 말고 dedicated server가 WebRTC endpoint가 되는 구조도 가능하다.

### WebTransport

신뢰 stream과 datagram을 하나의 client-server session에 제공한다 — 이는 HTTP/3/QUIC 기능의 브라우저 mapping이다(W3C WebTransport 스냅샷 2026-07-30 기준). 브라우저의 보안·origin·certificate 정책과 server implementation을 함께 충족해야 한다. native QUIC library API와 WebTransport API는 같은 추상화가 아니므로 공통 message layer 아래 adapter를 둔다.

```cpp
class GameTransport {
public:
    virtual void SendReliable(Channel, Bytes) = 0;
    virtual void SendUnreliable(Channel, Bytes) = 0;
    virtual TransportEvent Poll() = 0;
};
```

이 인터페이스도 너무 약할 수 있다. queue occupancy, deadline, drop reason, path state를 숨기면 backpressure를 제어할 수 없다. 최소 공통분모만 노출하는 것과 필요한 계측을 잃는 것을 구분한다.

## 10. 게임 적용과 검증

QUIC 채택 여부는 장르 이름보다 데이터 의미와 운영 조건으로 결정한다.

### 적합성이 높은 경우

- 로비·매치메이킹·채팅·control과 상태 datagram을 한 보안 session에 묶음
- 모바일 경로 변경이 잦음
- 브라우저 client-server 실시간 통신
- transport 보안·loss recovery를 자체 구현할 인력이 부족함
- 경쟁 액션보다 개발·운영 단순성이 중요함

### 별도 검토가 필요한 경우

- packet 조립과 정확한 tick 송신 시점을 응용이 직접 제어해야 함
- 수만 connection에서 library의 stream·crypto·timer 비용이 지배적임
- 매우 짧은 수명의 상태가 transport queue에 쌓이지 않도록 특수 scheduler가 필요함
- 지원 플랫폼의 QUIC stack과 API가 서로 크게 다름
- 서버 infrastructure가 Connection ID routing과 migration을 지원하지 않음

### 검증 행렬

| 축 | 주입 값 |
|---|---|
| RTT | 0, 지역, 대륙간 |
| jitter | 정상분포보다 burst·상관 지연 포함 |
| loss | 독립 손실과 연속 burst |
| reorder | 작은 역전과 긴 지연 packet |
| path | NAT rebinding, Wi-Fi·cellular 전환 |
| MTU | 1200 근처, 큰 payload black hole |
| server | event-loop stall, queue saturation |
| security | replayed 0-RTT, spoofed address |

관찰 항목:

- handshake와 0-RTT accept/reject 시간
- packet loss, PTO, spurious retransmission
- stream별 blocked time과 connection flow-control blocked time
- DATAGRAM drop·queue age·coalescing 수
- cwnd, bytes in flight, pacing delay
- migration 성공 시간과 path validation failure
- CPU/connection, memory/connection, timer 수

### 불변식

1. 0-RTT replay가 재화·권한 상태를 중복 변경하지 않는다.
2. 오래된 DATAGRAM이 최신 snapshot을 되돌리지 않는다.
3. stream reset이 다른 stream의 application 상태를 부분 commit하지 않는다.
4. 주소 검증 전 송신량이 amplification 한도를 넘지 않는다.
5. queue overflow가 데이터 종류별 정책을 따른다.
6. 연결 이동이 application identity를 바꾸지 않는다.
7. transport close와 session logout을 같은 사건으로 오해하지 않는다.

QUIC 명세 적합성 테스트와 게임 message semantics 테스트를 분리한다. 표준 library를 써도 응용의 sequence, baseline, idempotency, authority 불변식은 자동으로 검증되지 않는다.

---

## 결론

현대 전송의 핵심은 UDP 위에 TCP를 복제한 것이 아니라 packet, frame, stream, connection, path의 상태를 분리한 데 있다. QUIC은 stream별 순서, TLS 1.3, ACK range와 loss detection, connection migration을 제공하고 DATAGRAM으로 비신뢰 메시지를 같은 보안·혼잡 문맥에 넣는다.

그 대가로 응용은 0-RTT replay, 두 단계 flow control, 연결 전체 congestion budget, path validation, library queue와 packetization을 이해해야 한다. 게임 데이터의 수명과 권위 의미를 transport 기능 이름에 맞추지 말고, 각 메시지의 신뢰·순서·deadline·중복·overflow 정책을 먼저 정의한 뒤 전송을 선택해야 한다.

## 더 읽을거리

- [RFC 9000: QUIC: A UDP-Based Multiplexed and Secure Transport](https://www.rfc-editor.org/rfc/rfc9000) — packet, frame, stream, connection migration
- [RFC 9001: Using TLS to Secure QUIC](https://www.rfc-editor.org/rfc/rfc9001) — key 단계와 TLS 1.3 통합
- [RFC 9002: QUIC Loss Detection and Congestion Control](https://www.rfc-editor.org/rfc/rfc9002) — RTT, loss threshold, PTO
- [RFC 9221: An Unreliable Datagram Extension to QUIC](https://www.rfc-editor.org/rfc/rfc9221) — DATAGRAM 의미
- [RFC 9368: Compatible Version Negotiation for QUIC](https://www.rfc-editor.org/rfc/rfc9368) — version 진화
- [RFC 9308: Applicability of the QUIC Transport Protocol](https://www.rfc-editor.org/rfc/rfc9308) — 적용 경계
- Adam Langley et al., “The QUIC Transport Protocol: Design and Internet-Scale Deployment,” SIGCOMM 2017 — 배포와 설계 동기
- [W3C WebTransport](https://www.w3.org/TR/webtransport/) — 브라우저 stream·datagram API 계약
- [WebRTC for the Curious](https://webrtcforthecurious.com/) — ICE, DTLS, SCTP DataChannel 구조
