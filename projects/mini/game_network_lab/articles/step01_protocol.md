## 들어가며

게임 서버의 첫 경계는 소켓 API가 아니라 바이트 해석 규칙이다. TCP는 메시지가 아닌 순서 있는
바이트 스트림을 제공하고, UDP는 메시지 경계를 보존하지만 손실·중복·재정렬될 수 있다. 이 둘을
같은 방식으로 읽으면 TCP에서는 패킷 경계를 상상하게 되고, UDP에서는 잘린 데이터그램을 부분
메시지로 오인하게 된다.

이 구현은 TCP control frame과 UDP datagram을 서로 다른 계약으로 정의한다. 두 형식은 magic,
version, type, payload length를 명시하고 모든 정수를 network byte order로 기록한다. 디코더가
검증하지 않은 바이트는 게임 상태에 도달하지 않는다.

---

## 목차

1. 두 전송의 경계
2. TCP frame 형식
3. 점진적 디코더
4. UDP datagram 형식
5. 순환하는 sequence number
6. 검증 기준

---

## 두 전송의 경계

TCP의 한 번의 `recv`는 송신 측 `send` 한 번과 대응하지 않는다. 12바이트 헤더가 3바이트씩
도착할 수도 있고, 여러 frame이 한 번에 합쳐질 수도 있다. 따라서 애플리케이션은 수신 바이트를
누적한 뒤 길이 prefix가 지시하는 완전한 frame만 꺼내야 한다.

UDP의 한 번의 `recvfrom`은 하나의 데이터그램과 대응한다. 길이가 부족한 데이터그램을 버퍼에
보관해 이어 붙이면 서로 무관한 두 데이터그램을 하나의 메시지로 조작하게 된다. UDP 디코더는
`actual_bytes == header_bytes + declared_payload_bytes`를 요구하고 아니면 전체를 버린다.

## TCP frame 형식

control header는 12바이트다.

| offset | bytes | 의미 |
|---:|---:|---|
| 0 | 4 | protocol magic |
| 4 | 2 | protocol version |
| 6 | 2 | control message type |
| 8 | 4 | payload byte count |

직렬화는 C++ 구조체를 그대로 복사하지 않는다. 구조체 padding, host endian, ABI가 wire format에
섞이기 때문이다. 값을 큰 바이트부터 명시적으로 추가한다.

```cpp
void AppendU32(std::vector<std::byte>& out, std::uint32_t value) {
    out.push_back(static_cast<std::byte>((value >> 24u) & 0xffu));
    out.push_back(static_cast<std::byte>((value >> 16u) & 0xffu));
    out.push_back(static_cast<std::byte>((value >> 8u) & 0xffu));
    out.push_back(static_cast<std::byte>(value & 0xffu));
}
```

payload 상한은 64KiB다. 이 상한은 메모리 최적화가 아니라 프로토콜 안전 조건이다. 공격자가
길이 필드에 4GiB를 적더라도 디코더가 그만큼 할당하거나 기다리지 않는다.

## 점진적 디코더

`FrameDecoder`는 아직 소비하지 않은 바이트와 읽기 offset을 보존한다. `Push`는 누적 상한을
먼저 검사하고, `Pop`은 세 상태를 구분한다.

- 헤더도 부족하다: 더 받을 수 있으므로 대기한다.
- 헤더는 있지만 frame 전체가 부족하다: 길이를 기억하고 대기한다.
- magic, version, 길이가 불법이다: 연결을 복구하려 하지 않고 decoder를 실패 상태로 만든다.

```cpp
if (available < kFrameHeaderBytes) return std::nullopt;
if (magic != kProtocolMagic || version != kProtocolVersion ||
    payloadBytes > kMaxControlPayload) {
    failed_ = true;
    return std::nullopt;
}
if (available < kFrameHeaderBytes + payloadBytes) return std::nullopt;
```

불법 헤더 뒤에서 magic을 다시 검색하는 resynchronization을 하지 않는 이유는 TCP 연결 자체가
신뢰 경계이기 때문이다. 공격자가 삽입한 바이트에서 우연히 magic을 찾아 세션을 계속하는 것보다
연결을 닫고 새 handshake를 요구하는 편이 상태 기계를 단순하고 결정적으로 만든다.

소비한 prefix는 매 frame마다 지우지 않는다. offset이 버퍼 절반을 넘었을 때만 compaction해
작은 frame 연속 수신에서 반복적인 `memmove`를 줄인다.

## UDP datagram 형식

UDP header는 connection ID와 sequence를 포함한 24바이트다. payload 최대 크기는 1200바이트로
제한한다. 이는 모든 인터넷 경로의 MTU를 보장한다는 뜻이 아니라, 일반적인 IPv6 최소 MTU
1280바이트 안에서 IP·UDP 헤더와 일부 확장 여유를 남기는 보수적 애플리케이션 예산이다.

```cpp
if (bytes.size() < kDatagramHeaderBytes) return std::nullopt;
if (magic != kProtocolMagic || version != kProtocolVersion) return std::nullopt;
if (payloadBytes > kMaxDatagramPayload) return std::nullopt;
if (bytes.size() != kDatagramHeaderBytes + payloadBytes) return std::nullopt;
```

reserved 필드는 0이어야 한다. 필드를 무시하지 않고 검사하면 확장 버전을 구버전 서버가 우연히
수용하는 일을 막는다. 호환성은 version 협상으로 열어야지 미정 바이트를 묵인해서 만들지 않는다.

## 순환하는 sequence number

32비트 sequence는 약 42억 뒤 0으로 돌아간다. 단순한 `lhs > rhs`는
`0xffffffff -> 0`에서 실패한다. 비교하려는 두 값의 거리가 sequence 공간 절반 미만이라는
윈도우 불변식 아래 signed difference를 사용한다.

```cpp
bool IsNewerSequence(std::uint32_t lhs, std::uint32_t rhs) {
    return lhs != rhs && static_cast<std::int32_t>(lhs - rhs) > 0;
}
```

이 식은 임의로 멀리 떨어진 두 번호의 전체 순서를 정의하지 않는다. 서버가 보관하는 미확인
윈도우를 `2^31`보다 작게 유지할 때만 유효한 serial number arithmetic이다.

## 검증 기준

테스트는 정상 round trip보다 경계 분할을 우선한다. 헤더를 3바이트 단위로 넣고, header와
payload를 나누고, 두 frame을 한 번에 넣는다. UDP는 마지막 바이트를 제거한 데이터그램과 선언
길이가 실제 길이보다 큰 데이터그램을 거부해야 한다. sequence는 최댓값에서 0으로 넘어가는
경우와 같은 번호의 중복을 별도로 확인한다.

---

## 결론

wire format은 자료형 목록이 아니라 신뢰 경계다. TCP에는 누적 가능한 점진적 frame decoder가,
UDP에는 데이터그램 단위의 완전성 검사가 필요하다. 명시적 endian, 길이 상한, version, reserved
검사, 순환 sequence 비교를 고정하면 플랫폼 I/O 방식이 달라도 게임 서버 core는 같은 바이트
계약을 사용할 수 있다.
