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

<!-- snippet: src/protocol.cpp#AppendU32 -->
```cpp
void AppendU32(std::vector<std::byte>& output, std::uint32_t value) {
    output.push_back(static_cast<std::byte>((value >> 24u) & 0xffu));
    output.push_back(static_cast<std::byte>((value >> 16u) & 0xffu));
    output.push_back(static_cast<std::byte>((value >> 8u) & 0xffu));
    output.push_back(static_cast<std::byte>(value & 0xffu));
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

<!-- snippet: src/protocol.cpp#FrameDecoder::Pop -->
```cpp
if (available < kFrameHeaderBytes) return std::nullopt;
// ... 헤더 필드를 읽는다
if (magic != kProtocolMagic || version != kProtocolVersion ||
    payloadBytes > kMaxControlPayload) {
    failed_ = true;
    return std::nullopt;
}

std::size_t frameBytes = kFrameHeaderBytes + payloadBytes;
if (available < frameBytes) return std::nullopt;
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

디코더는 header 크기부터 확인하고, 일곱 필드를 모두 읽은 뒤 값 검사와 길이 일치 검사를 한다.
필드 읽기가 하나라도 실패하면 값을 쓰지 않는다.

<!-- snippet: src/protocol.cpp#DecodeDatagram -->
```cpp
if (bytes.size() < kDatagramHeaderBytes) return std::nullopt;
// ... magic, version, type, connection, sequence, payloadBytes, reserved 를 읽는다
if (!magic || !version || !type || !connection || !sequence ||
    !payloadBytes || !reserved) return std::nullopt;
if (*magic != kProtocolMagic || *version != kProtocolVersion ||
    *reserved != 0 || *payloadBytes > kMaxDatagramPayload) return std::nullopt;
if (bytes.size() != kDatagramHeaderBytes + *payloadBytes) return std::nullopt;
```

reserved 필드는 0이어야 한다. 필드를 무시하지 않고 검사하면 확장 버전을 구버전 서버가 우연히
수용하는 일을 막는다. 호환성은 version 협상으로 열어야지 미정 바이트를 묵인해서 만들지 않는다.

## 순환하는 sequence number

32비트 sequence는 약 42억 뒤 0으로 돌아간다. 단순한 `lhs > rhs`는
`0xffffffff -> 0`에서 실패한다. 비교하려는 두 값의 거리가 sequence 공간 절반 미만이라는
윈도우 불변식 아래 signed difference를 사용한다.

<!-- snippet: src/protocol.cpp#IsNewerSequence -->
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

여기까지의 case는 `game_network_tests` 안에 `step01` 태그로 모여 있고, CTest 이름은
`game_network_step01_protocol`이다.

```powershell
ctest --test-dir projects/mini/game_network_lab/build -C Release `
      -R game_network_step01 --output-on-failure
```

```text
    Start 6: game_network_step01_protocol
1/1 Test #6: game_network_step01_protocol .....   Passed    0.03 sec

100% tests passed out of 1

Total Test time (real) =   0.04 sec
```

통과 여부는 이 정도만 말해 준다. 무엇을 봤는지는 실행 파일을 직접 부르면 case마다 나온다.

```powershell
projects/mini/game_network_lab/build/Release/game_network_tests.exe step01
```

```text
[step01] golden_control_frame    ok  EncodeControl(Hello, 5B) = 17B, header 12B matched byte for byte
[step01] golden_datagram         ok  EncodeDatagram(Input, 42, 99, 5B) = 29B, header 24B matched byte for byte
[step01] framing                 ok  3+7+7 byte pushes -> 1 frame, one 29B push -> 2 frames
[step01] datagram_round_trip     ok  connection=42 sequence=99 payload=5B
[step01] datagram_rejection      ok  rejected: truncated, length +1 and -1, 2 reserved bytes, magic, version, short header; the original still decodes
[step01] frame_header_rejection  ok  magic, version and a 65537 byte declared length each latch Failed(); BufferedBytes stayed 17
[step01] frame_buffer_cap        ok  accumulation cap 65548B accepted, cap+1 refused in one push and in two
[step01] sequence_wrap           ok  0 is newer than 0xffffffff, 0xffffffff is not newer than 0, 17 is not newer than 17
game_network_tests: 8 case(s) passed [step01 8]
```

`frame_buffer_cap`이 그 상한을 어떻게 판정하는지가 이 형식의 대표다. 상한만큼은 받아들이고,
한 바이트를 더하면 거부하며, 그 뒤로는 decoder가 아무것도 받지 않는다. 상한을 한 번에 넘기는
push와 두 번에 나눠 넘기는 push를 따로 확인하는 이유는 누적 검사와 단발 검사가 다른 코드
경로이기 때문이다.

<!-- snippet: tests/protocol_tests.cpp#TestFrameBufferCap -->
```cpp
constexpr std::size_t kCap = kMaxControlPayload + kFrameHeaderBytes;

{
    FrameDecoder decoder;
    Require(decoder.Push(std::vector<std::byte>(kCap)), "the cap itself fits");
    Require(decoder.BufferedBytes() == kCap, "all of it is buffered");
    Require(!decoder.Push(std::vector<std::byte>(1)), "one byte past the cap is refused");
    Require(decoder.Failed(), "exceeding the cap fails the decoder");
    // ...
}
{
    FrameDecoder decoder;
    Require(!decoder.Push(std::vector<std::byte>(kCap + 1)),
            "a single oversized push is refused");
    Require(decoder.Failed(), "a single oversized push fails the decoder");
}
```

---

## 결론

wire format은 자료형 목록이 아니라 신뢰 경계다. TCP에는 누적 가능한 점진적 frame decoder가,
UDP에는 데이터그램 단위의 완전성 검사가 필요하다. 명시적 endian, 길이 상한, version, reserved
검사, 순환 sequence 비교를 고정하면 플랫폼 I/O 방식이 달라도 게임 서버 core는 같은 바이트
계약을 사용할 수 있다.
