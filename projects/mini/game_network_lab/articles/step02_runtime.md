## 들어가며

소켓 worker가 곧 게임 서버가 되면 I/O 완료 순서가 게임 규칙의 실행 순서를 결정한다. worker
수에 따라 결과가 달라지고, 느린 클라이언트의 송신 대기가 시뮬레이션을 막으며, 연결 종료와
게임 객체 제거가 한 함수에 얽힌다. 플랫폼 I/O와 권위 시뮬레이션 사이에 이벤트 경계를 두어야
하는 이유다.

이 구현의 backend는 연결·바이트·데이터그램·종료를 `NetworkEvent`로 전달하고, 게임 runtime은
고정 틱 스레드에서 그 이벤트를 소비한다. 반대 방향은 bounded send 명령이다.

---

## 목차

1. backend 계약
2. 연결과 게임 세션의 차이
3. TCP 송신 backpressure
4. buffer 소유권
5. 고정 틱과 네트워크 시계
6. 종료 불변식

---

## backend 계약

게임 core가 알아야 할 것은 IOCP completion key나 epoll file descriptor가 아니다. 안정적인
`ConnectionId`, 받은 바이트, UDP 송신자 endpoint, 오류 코드다.

```cpp
struct NetworkEvent {
    NetworkEventKind kind{};
    ConnectionId connection = 0;
    Endpoint endpoint{};
    std::vector<std::byte> payload;
    int error = 0;
};
```

backend는 OS 이벤트를 이 값 객체로 바꾼다. runtime은 `OnNetworkEvent`에서 TCP 바이트를
connection별 `FrameDecoder`에 넣고, 완성된 control message만 상태 기계에 전달한다. UDP는
데이터그램 decoder를 통과한 뒤 connection ID와 endpoint 결합을 확인한다.

이 경계 덕분에 IOCP는 완료 기반, epoll은 준비 기반이어도 handshake와 시뮬레이션 코드는 같다.
추상화 대상은 시스템 호출 이름이 아니라 "연결에서 어떤 바이트가 관측되었다"는 의미다.

## 연결과 게임 세션의 차이

TCP accept 직후에는 네트워크 연결만 존재한다. Hello가 검증되고 서버가 Welcome을 보낸 뒤에도
UDP endpoint는 아직 모른다. runtime은 상태를 분리한다.

```cpp
struct ClientState {
    FrameDecoder decoder;
    std::array<std::byte, 16> token{};
    Endpoint endpoint{};
    bool helloComplete = false;
    bool udpBound = false;
    std::uint32_t lastInputSequence = 0;
};
```

Welcome의 난수 token은 TCP에서 검증한 주체가 보낸 첫 UDP Connect를 같은 connection에 결합하는
capability다. 암호학적 계정 자격 증명은 아니지만, 임의의 첫 UDP 데이터그램이 세션을 선점하는
것은 막는다. UDP input은 `udpBound` 이후에만 처리한다.

TCP close는 control channel의 종료다. backend가 session 객체를 바로 해제해도 된다는 뜻은
아니다. OS에 제출된 receive/send가 참조하는 메모리는 completion 또는 취소 완료가 회수될 때까지
살아 있어야 한다.

## TCP 송신 backpressure

`send`가 즉시 끝나지 않는 것은 정상이다. 수신자가 느리거나 커널 송신 버퍼가 차면 남은 바이트를
보관하고 writable 사건에서 이어 보내야 한다. 여기서 queue를 무한히 두면 느린 한 연결이 서버
메모리를 소비한다.

송신 queue의 불변식은 다음과 같다.

- 연결마다 queued byte 상한이 있다.
- 한 frame의 일부만 전송되면 offset을 증가시킨다.
- frame 전체가 완료된 뒤에만 queue에서 제거한다.
- 상한을 넘는 연결은 새 데이터를 무시하지 않고 종료시킨다.

```cpp
if (bytes.size() > config_.maxQueuedSendBytesPerConnection -
                       std::min(session->queuedSendBytes,
                                config_.maxQueuedSendBytesPerConnection)) {
    overflow = true;
}
// sendMutex를 놓은 뒤에 종료시킨다.
if (overflow) {
    CloseSession(session, kSendQueueOverflowError);
    return false;
}
```

비교를 `queuedSendBytes + bytes.size() > 상한` 으로 적으면 합이 넘칠 수 있으므로 남은 여유를
빼는 형태로 쓴다. 두 backend는 같은 상한값과 같은 종료 코드를 쓴다. `Disconnected` 이벤트의
`error`는 `network_types.h`의 `kSendQueueOverflowError`이며 IOCP backend와 epoll backend에서
동일하다.

메모리를 더 주는 방식은 backpressure가 아니다. 생성 속도가 소비 속도보다 계속 빠르면 queue
크기와 관계없이 언젠가 고갈된다. 제한은 실패를 조기에 국소화한다.

## buffer 소유권

비동기 송신 함수에 넘긴 span은 호출 동안만 필요한 것이 아니다. OS가 완료를 보고할 때까지
주소와 내용이 유효해야 한다. 따라서 송신 명령은 payload를 소유하고, IOCP operation도 실제
제출된 payload와 `WSABUF`를 함께 소유한다.

수신도 같은 원칙이다. stack buffer 주소를 overlapped operation에 저장하면 함수 반환 뒤 use
after free가 된다. operation 객체가 소유하는 buffer를 제출하고 completion handler에서만 읽는다.

## 고정 틱과 네트워크 시계

runtime의 `Advance(now)`는 60Hz tick 경계를 따라 정수 위치를 갱신한다. 네트워크 사건이 도착한
worker 시각에 곧바로 월드를 전진시키지 않는다. 입력은 최신 의도로 저장되고 tick이 이를
소비한다.

```cpp
while (now >= nextTick && catchUpTicks < kMaxCatchUpTicks) {
    RunTick();
    nextTick += tickDuration;
    ++catchUpTicks;
}
```

snapshot은 세 tick마다 20Hz로 보낸다. input, simulation, snapshot은 서로 다른 주기를 가지므로
클라이언트의 예측 위치와 마지막 권위 위치 사이에 작은 차이가 생기는 것이 정상이다.

## 종료 불변식

정상 종료는 소켓 handle을 닫는 한 줄이 아니다.

1. 새 accept와 receive 제출을 중단한다.
2. listener, UDP socket, session socket을 닫아 pending I/O를 취소한다.
3. 이미 queue에 들어온 completion을 worker가 회수한다.
4. outstanding operation 수가 0이 된 뒤 worker 종료 사건을 보낸다.
5. worker를 join하고 completion queue를 해제한다.

이 순서를 뒤집으면 completion이 파괴된 session이나 queue를 참조한다. 종료 경로도 정상 I/O와
같은 소유권 규칙을 따라야 한다.

---

## 결론

게임 서버 core는 소켓 callback 집합이 아니다. 플랫폼 backend가 값 이벤트와 bounded send
명령을 제공하고, 권위 runtime이 한 고정 틱 순서에서 상태를 바꿔야 한다. 연결 상태, UDP 결합,
송신 backpressure, 비동기 buffer 수명, 종료 drain을 계약으로 만들면 I/O 병렬성과 게임 상태의
결정성을 함께 유지할 수 있다.
