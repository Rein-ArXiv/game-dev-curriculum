## 들어가며

epoll은 완료된 I/O를 반환하지 않는다. file descriptor가 현재 block 없이 수행할 수 있는 작업을
알린다. `EPOLLIN`을 받았다는 것은 한 메시지가 준비되었다는 뜻이 아니라, `read`나 `accept`를
시도할 이유가 생겼다는 뜻이다. 애플리케이션은 nonblocking 시스템 호출을 반복하고 `EAGAIN`에서
멈춰야 한다.

이 구현은 level-triggered epoll, nonblocking socket, `eventfd` command wakeup을 사용한다. IOCP와
OS 동작은 다르지만 backend 밖에는 같은 이벤트와 송신 계약을 제공한다.

---

## 목차

1. readiness 의미
2. accept와 read drain
3. partial write와 EPOLLOUT
4. eventfd command queue
5. descriptor 수명
6. IOCP와의 의미 정렬

---

## readiness 의미

listener, UDP socket, accepted TCP socket, wake `eventfd`를 하나의 epoll instance에 등록한다.
모든 descriptor는 `O_NONBLOCK`이며 생성 시 가능한 경우 `CLOEXEC`도 설정한다. block 가능한
descriptor가 event loop에 섞이면 한 연결의 시스템 호출이 전체 서버를 멈춘다.

level-triggered 모드에서는 읽을 데이터가 남아 있는 동안 `EPOLLIN`이 반복된다. 구현은 사건 한
건마다 한 번만 읽는 대신 즉시 drain해 event loop 왕복과 syscall wakeup을 줄인다.

## accept와 read drain

listener 사건은 `accept4`를 반복한다.

```cpp
for (;;) {
    int fd = accept4(listener, reinterpret_cast<sockaddr*>(&address), &length,
                     SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd >= 0) {
        RegisterSession(fd);
        continue;
    }
    if (errno == EINTR) continue;
    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
    HandleListenerError(errno);
    break;
}
```

TCP `recv`와 UDP `recvfrom`도 같은 패턴이다. `EINTR`은 호출을 반복하고, `EAGAIN`은 현재 drain이
끝났다는 정상 종료다. TCP `recv == 0`은 peer의 orderly close이므로 연결 종료 사건을 만든다.

UDP 데이터그램 하나는 하나의 receive 결과다. buffer보다 큰 데이터그램이 잘려 들어오는 문제를
피하려면 protocol 최대 크기보다 충분한 buffer를 두고, decoder가 정확한 header·payload 길이를
검사해야 한다.

## partial write와 EPOLLOUT

nonblocking `send`는 일부만 쓸 수 있다. queue front에는 payload와 offset을 보존한다. 즉시 보낼
수 있는 동안 반복하고 `EAGAIN`이면 해당 session의 관심 집합에 `EPOLLOUT`을 추가한다.

```cpp
if (errno == EAGAIN || errno == EWOULDBLOCK) {
    ModifyEpoll(session.fd, EPOLLIN | EPOLLOUT | EPOLLRDHUP);
    return;
}
```

queue가 비면 `EPOLLOUT`을 제거한다. writable은 대부분의 시간 참이므로 항상 감시하면 epoll이
계속 같은 descriptor를 반환해 busy loop를 만들 수 있다. 송신할 데이터가 있을 때만 관심을 켠다.

## eventfd command queue

게임 runtime thread가 직접 epoll session map과 socket을 조작하면 event loop thread와 data race가
생긴다. 반대 방향의 송신·종료 요청은 mutex로 보호한 command queue에 넣고 `eventfd` counter를
증가시켜 event loop를 깨운다.

event loop는 wake descriptor를 drain한 뒤 command를 local queue로 옮겨 처리한다. socket map,
epoll registration, send queue 변경은 한 thread에 직렬화된다. mutex는 command 전달에만 있고
socket 상태 기계 전체를 둘러싸지 않는다.

`eventfd`는 byte stream pipe와 달리 64비트 counter다. 여러 wake가 합쳐져도 "command queue를
확인하라"는 의미는 보존된다. wake 횟수와 command 개수를 일대일로 맞출 필요는 없다.

## descriptor 수명

epoll event는 integer file descriptor를 돌려준다. close 직후 OS가 같은 숫자를 새 socket에
재사용할 수 있으므로, 오래된 event가 새 session으로 오인되지 않게 event loop 한 thread에서
등록·삭제·close 순서를 관리한다.

`EPOLLERR`, `EPOLLHUP`, `EPOLLRDHUP`는 read/write 결과와 함께 해석한다. error flag가 있다고 즉시
map을 지우기 전에 처리 중인 session 참조의 유효성을 확인하고 종료 사건을 한 번만 방출한다.

## IOCP와의 의미 정렬

IOCP operation은 제출 시점에 buffer를 소유하고 completion이 수명을 끝낸다. epoll은 준비 사건
뒤 애플리케이션이 syscall을 실행하므로 send queue가 buffer를 소유한다. 내부 소유 모델은 다르다.

공통 경계는 다음과 같다.

- 연결에는 안정적인 `ConnectionId`가 있다.
- 받은 TCP bytes와 UDP datagram은 값 이벤트가 된다.
- 송신 payload는 backend가 소비 완료할 때까지 소유한다.
- 느린 TCP 연결에는 같은 byte 상한을 적용한다.
- 게임 상태는 event loop가 아니라 고정 틱 runtime만 바꾼다.

플랫폼 추상화는 두 API를 같은 모양으로 흉내 내는 것이 아니라, 서로 다른 메커니즘이 같은 상위
불변식을 만족하는지 확인하는 일이다.

---

## 결론

epoll의 핵심은 사건을 받은 뒤 `EAGAIN`까지 nonblocking 작업을 drain하는 것이다. partial send에는
조건부 `EPOLLOUT`, 다른 thread의 명령에는 `eventfd`, descriptor 상태에는 단일 event loop 소유권을
사용한다. readiness와 completion의 차이를 backend 안에 가두면 권위 runtime과 protocol decoder는
Windows와 Linux에서 동일하게 유지된다.
