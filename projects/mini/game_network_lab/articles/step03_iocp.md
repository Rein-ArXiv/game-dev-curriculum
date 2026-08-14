## 들어가며

IOCP는 "많은 소켓을 감시하는 Windows API"보다 더 구체적이다. 애플리케이션이 overlapped
operation을 먼저 제출하고, 커널이 완료 결과를 completion port에 넣으며, 제한된 worker가 이를
회수한다. 준비된 소켓을 알려 주는 readiness 모델과 달리 completion 한 건은 제출한 I/O 한 건의
수명과 직접 연결된다.

핵심 문제는 함수 호출이 아니라 소유권이다. `OVERLAPPED`, `WSABUF`, payload, socket session은
완료가 회수될 때까지 함께 살아 있어야 한다.

---

## 목차

1. completion port의 역할
2. operation 객체
3. accept와 receive 선제 제출
4. 송신 직렬화
5. 완료 오류 해석
6. 안전한 종료

---

## completion port의 역할

서버는 `CreateIoCompletionPort(INVALID_HANDLE_VALUE, ..., concurrency)`로 port를 만들고 listener,
accepted TCP socket, UDP socket을 같은 port에 연결한다. concurrency 값은 worker thread 개수와
같은 의미가 아니다. 커널이 동시에 runnable하게 둘 completion worker 수에 대한 힌트다.

worker는 `GetQueuedCompletionStatus`로 세 값을 받는다.

- transferred byte count
- completion key
- 제출할 때 사용한 `OVERLAPPED*`

completion key는 socket이나 서버 역할을 식별할 수 있고, `OVERLAPPED*`는 구체적인 operation을
식별한다. 구현은 `OVERLAPPED`를 operation의 첫 멤버로 두고 원래 객체로 복원한다.

```cpp
struct IoOperation {
    // IOCP returns the same pointer passed to Winsock. Keep this first.
    OVERLAPPED overlapped{};
    OperationKind kind{};
    std::shared_ptr<Session> session;
    SOCKET acceptedSocket = INVALID_SOCKET;
    std::vector<std::byte> buffer;
    WSABUF wsabuf{};
    sockaddr_storage remote{};
    int remoteLength = sizeof(sockaddr_storage);
    DWORD flags = 0;
    std::size_t offset = 0;
};
```

buffer는 operation이 소유하되 크기는 용도별로 다르다. TCP receive는 `kReceiveBytes = 16 * 1024`,
UDP receive는 `kDatagramBytes = 2048`, accept는 주소 두 개를 담을 `2 * (sizeof(sockaddr_storage) + 16)`
바이트다. `remote`와 `remoteLength`는 UDP 송신자 endpoint를 추출하고 receive를 다시 게시할 때,
`offset`은 부분 송신을 이어 보낼 때 필요한 operation 상태다.

`OVERLAPPED*`가 null이면 일반 I/O completion이 아니다. 이 구현에서는 worker 종료를 알리는
사용자 completion으로 구분한다.

## operation 객체

overlapped 호출이 `WSA_IO_PENDING`을 반환하면 실패가 아니다. 작업이 접수되었고 결과가 port로
온다는 뜻이다. 함수가 즉시 성공해도 completion routine을 사용하지 않는 IOCP 연결 소켓은 완료
packet이 queue된다. 그러므로 두 경우 모두 operation 소유권을 worker에 넘긴다.

```cpp
AddOutstanding();
int rc = WSARecv(socket, &op->wsabuf, 1, &bytes, &op->flags,
                 &op->overlapped, nullptr);
if (rc == 0 || WSAGetLastError() == WSA_IO_PENDING) return true;
RemoveOutstanding();
delete op;
return false;
```

즉시 제출 실패에서만 operation을 호출 측이 제거한다. 접수된 operation을 socket close 직후
지우면 취소 completion이 같은 주소로 돌아와 use after free가 된다. `shared_ptr<Session>`은
session map에서 연결이 제거된 뒤에도 in-flight operation이 session 수명을 보존하게 한다.

## accept와 receive 선제 제출

`AcceptEx`는 일반 `accept`처럼 준비된 연결을 꺼내는 호출이 아니다. accepted socket과 주소
buffer를 미리 만들어 listener에 제출한다. 서버는 여러 accept operation을 선제 게시해 연결
burst가 worker의 새 allocation을 기다리지 않게 한다.

accept 완료 뒤에는 세 작업이 필요하다.

1. `SO_UPDATE_ACCEPT_CONTEXT`로 accepted socket의 context를 listener에 연결한다.
2. accepted socket을 completion port에 연결한다.
3. 첫 `WSARecv`를 게시하고 accept 슬롯을 보충한다.

UDP도 `WSARecvFrom` operation을 여러 개 게시한다. 각 operation은 독립 buffer와 remote address
storage를 가져야 한다. 완료 handler는 데이터그램을 값 이벤트로 옮긴 뒤 receive를 다시 게시한다.
`WSAEMSGSIZE`처럼 데이터그램 하나에만 해당하는 오류도 payload는 버리되 receive는 다시 게시한다.
그렇지 않으면 큰 데이터그램을 반복해서 보내는 것만으로 preposted receive 깊이를 0까지 줄일 수 있다.

## 송신 직렬화

TCP는 순서 있는 스트림이지만 여러 thread가 같은 socket에 `WSASend`를 무제한 제출한다고
애플리케이션 frame 단위가 자동 보존되는 것은 아니다. 이 구현은 session별 send queue에서 한
operation만 in flight로 유지한다.

완료 byte가 payload보다 작으면 나머지 span을 같은 순서로 다시 제출한다. 전체 frame이 끝난 뒤
queue byte를 차감하고 뒤 frame을 시작한다. bounded queue를 넘으면 느린 receiver를 종료한다.

UDP `WSASendTo`는 데이터그램 하나를 통째로 제출한다. 이 구현은 제출이 접수되었는지까지만
확인하고 완료 결과의 전송 byte 수는 검사하지 않는다. 따라서 runtime의 snapshot metric은 전송
완료 수가 아니라 제출 성공 수다.

## 완료 오류 해석

`GetQueuedCompletionStatus`가 false라는 사실만 보고 loop를 끝내면 안 된다. overlapped가 null인지,
operation이 있는 실패 completion인지 구분해야 한다. operation이 있다면 그 객체는 여전히 회수와
outstanding 차감이 필요하다.

TCP receive의 transferred bytes가 0이면 orderly shutdown이다. 실패 error와 같은 disconnect
경로로 합치되, 이미 session이 closing인지 검사해 중복 `Disconnected` 사건을 만들지 않는다.

## 안전한 종료

서버는 outstanding operation counter를 제출 직전에 증가시키고 모든 completion 또는 제출 실패에서
정확히 한 번 감소시킨다. 종료 시 socket을 닫아 pending I/O를 취소한 뒤 counter가 0이 될 때까지
기다린다.

```cpp
void RemoveOutstanding() {
    if (outstanding.fetch_sub(1) != 1) return;
    { std::lock_guard lock(outstandingMutex); }  // 대기자 등록 구간과 배타
    outstandingCv.notify_all();
}

std::unique_lock lock(outstandingMutex);
outstandingCv.wait(lock, [&] { return outstanding.load() == 0; });
for (std::uint32_t i = 0; i < workerCount; ++i)
    PostQueuedCompletionStatus(port, 0, shutdownKey, nullptr);
```

counter가 atomic이어도 감소와 통지는 대기 mutex 밖에서 하면 안 된다. 종료 스레드가 predicate를
false로 평가한 뒤 대기자로 등록되기 전에 마지막 completion이 0을 만들고 통지하면 그 통지는
유실되고, 종료 뒤에는 새 operation이 제출되지 않으므로 counter가 다시 움직이지 않는다.

worker 종료 packet은 drain 뒤에 보낸다. 먼저 worker를 멈추면 취소 completion을 회수할 주체가
사라져 counter가 0이 되지 않는다.

---

## 결론

IOCP의 성능은 completion queue 이름에서 나오지 않는다. I/O를 충분히 선제 제출하고, operation이
buffer와 session 수명을 소유하며, session별 송신 순서를 제한하고, 취소 completion까지 drain하는
상태 기계에서 나온다. 이 불변식이 지켜지면 worker 수와 연결 수가 달라도 게임 runtime에는 같은
`NetworkEvent` 계약만 노출된다.
