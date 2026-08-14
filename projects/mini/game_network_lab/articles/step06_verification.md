## 들어가며

네트워크 코드는 정상 실행 한 번으로 검증되지 않는다. 로컬 loopback에서는 데이터가 빠르고 손실이
거의 없으며, TCP frame도 우연히 한 덩어리로 도착하기 쉽다. 서버와 클라이언트가 실행되었다는
사실만 확인하면 framing, partial send, shutdown lifetime 같은 결함이 숨어 남는다.

검증은 순수 protocol, runtime 상태 기계, 플랫폼 backend, 실제 두 프로세스의 경계를 분리한다.
각 계층은 자기 실패를 관측 가능한 exit code와 metric으로 남긴다.

---

## 목차

1. 결정적 protocol test
2. runtime 상태 기계 test
3. bounded send queue test
4. 실제 socket smoke
5. 종료 검증
6. metric 해석
7. 교차 플랫폼 검증 범위
8. 재현 명령

---

## 결정적 protocol test

`game_network_tests`는 OS scheduler에 의존하지 않고 바이트 경계를 직접 만든다. control frame을
3 / 7 / 나머지 세 조각으로 `Push`해 header 분할과 payload 분할을 만들고, 조각 사이에서 `Pop`이
아직 비어 있는지까지 확인한다. 두 frame을 이어 붙여 coalescing도 만든다. 실패 시 즉시 종료하는
helper는 `Require`다.

```cpp
FrameDecoder decoder;
Require(decoder.Push(std::span(frame).first(3)), "push first fragment");
Require(!decoder.Pop().has_value(), "partial header must wait");
Require(decoder.Push(std::span(frame).subspan(3, 7)), "push second fragment");
Require(!decoder.Pop().has_value(), "partial payload must wait");
Require(decoder.Push(std::span(frame).subspan(10)), "push final fragment");
auto decodedFrame = decoder.Pop();
```

UDP는 마지막 byte 제거, payload 길이 위조, reserved field 변경을 각각 거부해야 한다. sequence
검사는 `0xffffffff` 뒤의 0이 더 새롭고, 같은 값은 새롭지 않다는 경계를 확인한다. TCP header도
같은 방식으로 magic 반전과 version 2를 각각 거부하고, 선언 길이에 65537을 적은 frame은 그만큼
buffer를 늘리지 않고 decoder를 실패 상태로 만든다. 실패 상태가 된 decoder는 이후 `Push`와 `Pop`을
계속 거부해야 한다. 재동기화가 가능한 척하면 공격자가 frame 경계를 고를 수 있게 된다.

정상 round trip만 테스트하면 encoder와 decoder가 같은 잘못을 공유해도 통과할 수 있다. 그래서
알려진 byte 배열과 손상 입력을 함께 사용해야 한다. golden 배열은 encoder 출력을 복사한 것이
아니라 wire format 표를 보고 손으로 적은 값이다.

```cpp
const std::array<std::byte, 17> expected{
    std::byte{0x47}, std::byte{0x4e}, std::byte{0x4c}, std::byte{0x31}, // magic
    std::byte{0x00}, std::byte{0x01},                                   // version 1
    std::byte{0x00}, std::byte{0x01},                                   // Hello
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x05}, // length 5
    std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5},
};
```

## runtime 상태 기계 test

`GameServerRuntime`은 `IServerBackend`만 참조하므로 socket도 thread도 없이 통째로 돌릴 수 있다.
`game_network_runtime_tests`는 `SendStream`·`SendDatagram`·`Disconnect` 호출을 기록하는 fake
backend를 주입하고, `NetworkEvent`를 손으로 만들어 넣는다. 시계는 `Advance()` 인자로 주입한다.

```cpp
// Advance() runs a tick only when now has reached nextTick_, and it caps
// catch-up at 4, so N ticks need N calls spaced one interval apart.
for (int i = 0; i < count; ++i) runtime.Advance(kBase + gnl::kTickInterval * i);
```

tick 간격과 snapshot 주기가 구현 파일의 익명 namespace에 있으면 테스트가 같은 숫자를 따로 적어야
한다. 그러면 상수를 바꾸는 회귀에서 테스트도 함께 바뀌어 아무것도 잡지 못한다. 두 상수는 헤더의
`gnl::kTickInterval`, `gnl::kSnapshotEveryTicks` 한 곳에만 있고, 기대값은 상수를 다시 쓰지 않고
숫자로 못 박는다: 12 tick 뒤 위치는 (36, -24)이고 snapshot은 정확히 4개다.

이 계층이 확인하는 것은 network가 아니라 판정이다. Welcome payload가 8byte id와 16byte token인지,
1byte만 뒤집은 token이 `ConnectAccepted`를 받지 못하는지, 결합된 endpoint가 아닌 곳에서 온 Input이
권위 위치를 움직이지 않는지, 같은 sequence와 더 오래된 sequence가 폐기되는지, snapshot의 ack가
마지막으로 *적용된* sequence인지, 10 tick 정지가 4 tick catch-up으로 잘리는지다. 이들은 정직한
client 한 명만 띄우는 smoke test로는 전부 관측되지 않는다. 서버가 token을 검사하지 않아도 그
client는 여전히 snapshot을 받기 때문이다.

## bounded send queue test

연결별 queued send byte 상한은 순수 함수로 확인할 수 없다. 상한이 걸리려면 kernel socket buffer가
실제로 막혀야 하기 때문이다. `game_network_send_queue_tests`는 `CreatePlatformServer()`로 backend를
직접 띄우고, 수신 buffer를 작게 잡은 뒤 `recv`를 한 번도 호출하지 않는 loopback TCP 피어를 붙인다.
상한을 8KiB로 낮추고 4KiB씩 계속 보내면 상한이 있는 구현은 몇 번 만에 그 connection을 닫는다.

두 backend는 포기 방식이 다르다. IOCP는 `SendStream`에서 바로 false를 돌려주고, epoll은 명령을
queue에 넣은 뒤 event loop에서 닫는다. 그래서 공통 판정 기준은 둘 다 약속하는 관측값 하나다:
그 connection에 대한 `Disconnected` 이벤트가 정확히 1건이고 `error`가 `kSendQueueOverflowError`다.
0건이면 상한이 없어 메모리가 무한히 늘어난 것이고, 2건 이상이면 session 해제가 멱등이 아니다.

## 실제 socket smoke

단위 테스트는 `AcceptEx`, `epoll_wait`, kernel socket buffer를 통과하지 않는다. smoke test는
`game_server`를 제한 시간으로 별도 프로세스에서 실행하고 `game_client`가 실제 TCP와 UDP socket을
연다.

성공 조건은 단순 connect가 아니다. tick 수는 절차마다 다르다. 자동 process test는 `--ticks 60`,
아래 재현 절의 수동 관측은 `--ticks 120`이다.

- TCP Hello와 Welcome을 완성한다.
- Welcome token으로 UDP endpoint를 결합한다.
- 지정한 수의 input tick을 전송한다: 자동 test는 60개.
- snapshot을 하나 이상 decode한다.
- 서버 ACK와 권위 위치를 출력한다.
- 두 프로세스가 제한 시간 안에 정상 exit한다.

자동 process test는 정상 client를 시작하기 전에 IOCP의 preposted UDP receive 수보다 많은 4KiB
데이터그램을 보낸다. 각 completion은 `WSAEMSGSIZE`로 실패하지만 서버는 오류 payload를 버리고
receive를 재게시해야 한다. 그 뒤 정상 UDP Connect와 snapshot이 성공하므로 오류 입력이 receive
capacity를 영구 감소시키지 않았음을 확인한다.

클라이언트 snapshot 수가 0인데 exit 0을 반환하면 smoke가 거짓 양성이 된다. 구현은 snapshot이
없을 때 별도 실패 code를 반환한다.

## 종료 검증

IOCP shutdown bug는 기능 테스트 끝에서만 드러나는 경우가 많다. 서버 `--duration`은 정해진 시간
뒤 listener와 session을 닫고 outstanding completion을 drain한다. process가 timeout에 걸리면
테스트 실패다.

정상 종료에서 확인할 불변식은 다음과 같다.

- 새 operation이 제출되지 않는다.
- 취소된 operation도 completion handler를 통과한다.
- outstanding counter가 음수나 nonzero로 남지 않는다.
- worker 종료 packet은 I/O drain 뒤 처리된다.
- session destructor가 마지막 operation보다 먼저 실행되지 않는다.

도구가 process를 강제 종료한 결과는 성공으로 세지 않는다. 강제 종료는 lifetime 결함을 감춘다.

## metric 해석

서버는 control frame, datagram, rejected packet, snapshot, tick 수를 출력한다. metric은 성능 숫자
장식이 아니라 상태 기계가 어떤 경로를 통과했는지 확인하는 증거다.

예를 들어 client가 보낸 input tick 수보다 datagram 수가 많아야 하는 이유는 UDP Connect가
포함되기 때문이다. snapshot은 60Hz tick의 1/3 부근이어야 하지만 process 시작·종료 경계 때문에
정확히 초당 20개일 필요는 없다. rejected packet이 증가했다면 malformed input인지 종료 중
race인지 로그와 함께 구분한다.

snapshot metric이 무엇을 세는지도 함께 봐야 한다. 이 값은 `SendDatagram` 제출이 접수된 횟수이며
완료 시점의 전송 실패는 여기서 차감되지 않는다. 즉 "권위 상태가 20Hz로 만들어졌다"는 증거이지
"클라이언트에 도달했다"는 증거가 아니다. 도달은 client가 decode한 snapshot 수로 따로 본다.

## 교차 플랫폼 검증 범위

Windows Release 빌드는 raw IOCP 경로와 실제 process smoke를 검증한다. Linux build는 epoll,
`eventfd`, `accept4`를 컴파일하고 같은 protocol test와 process smoke를 실행해야 한다. 한 플랫폼
통과를 다른 backend 통과로 간주하지 않는다.

WSL 명령이 존재해도 distribution이 설치되지 않았다면 Linux backend는 미검증 상태다. 문서에는
"지원하도록 작성됨"과 "해당 환경에서 실행 검증됨"을 구분해 기록한다.

## 재현 명령

Windows에서는 설치된 Visual Studio generator를 사용한다.

```powershell
cmake -S projects/mini/game_network_lab `
      -B projects/mini/game_network_lab/build-vs18 `
      -G "Visual Studio 18 2026" -A x64
cmake --build projects/mini/game_network_lab/build-vs18 --config Release
ctest --test-dir projects/mini/game_network_lab/build-vs18 `
      -C Release --output-on-failure
```

실제 동작을 별도로 관측할 때는 서버를 숨김 프로세스로 실행하고 클라이언트 exit code를 보존한다.

```powershell
$server = Start-Process `
  projects/mini/game_network_lab/build-vs18/Release/game_server.exe `
  -ArgumentList '--duration','8' -WindowStyle Hidden -PassThru
Start-Sleep -Milliseconds 500
projects/mini/game_network_lab/build-vs18/Release/game_client.exe --ticks 120
Wait-Process -Id $server.Id
```

---

## 결론

네트워크 검증은 protocol byte 경계, runtime 상태 전이, OS backend, process 수명으로 층을 나눠야
한다. fragmentation·coalescing·truncation·sequence wrap은 결정적으로 만들고, 실제 TCP·UDP와
shutdown은 별도 프로세스로 확인한다. 실행하지 않은 플랫폼은 지원 주장과 분리해 기록해야 검증
결과가 구현 범위를 과장하지 않는다.
