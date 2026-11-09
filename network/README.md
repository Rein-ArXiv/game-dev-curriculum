# 게임 네트워킹 시리즈

인터넷의 packet 전달에서 시작해 실제 서버 socket runtime, 게임용 전송 protocol, 권위 simulation, 상태 복제와 지연 은폐까지 하나의 데이터 경로로 이해하기 위한 시리즈다. 전송 계층의 동작이 게임의 session·queue·tick 계약에 어떻게 이어지는지 살펴본다.

## 공통 핵심

| # | 문서 | 다루는 내용 |
|---|---|---|
| 01 | [인터넷과 경로](part01_internetworking.md) | 계층·IP·routing·latency·NAT·IPv6 |
| 02 | [전송 계층](part02_transport.md) | UDP·TCP·신뢰성·흐름 제어·혼잡 제어 |
| 03 | [현대 전송](part03_modern_transport.md) | QUIC·stream·DATAGRAM·TLS 1.3·경로 이동 |
| 04 | [Socket 서버 런타임](part04_socket_server_runtime.md) | listener·session·framing·I/O backend·buffer·backpressure |
| 05 | [인터넷 서비스와 보안](part05_services_security.md) | DNS·HTTP·TLS·deadline·retry·service discovery |
| 06 | [게임 전송 프로토콜](part06_game_transport_protocol.md) | sequence·ack·channel·retransmission·pacing |
| 07 | [연결과 세션 보안](part07_connectivity_session_security.md) | topology·NAT traversal·relay·token·packet protection |
| 08 | [권위와 지연 은폐](part08_authority_prediction.md) | fixed tick·authority·prediction·reconciliation·interpolation·lag compensation |
| 09 | [상태 복제](part09_state_replication.md) | snapshot·baseline·delta·AOI·priority·bandwidth budget |
| 10 | [결정론과 Rollback](part10_rollback.md) | input history·resimulation·desync·hybrid rollback |
| 11 | [측정과 진단](part11_measurement_diagnosis.md) | packet capture·경로 진단·분포·clock·production telemetry |
| 12 | [테스트와 재현](part12_network_testing.md) | latency·jitter·loss 주입·trace replay·load·soak |

## 전문 경로

| # | 문서 | 필요한 경우 |
|---|---|---|
| 13 | [Replay와 관전](part13_replay_spectating.md) | 입력·상태 기록, 결정론적 재생, 관전 지연을 설계할 때 |
| 14 | [음성 통신](part14_voice.md) | codec·jitter buffer·SFU·안전·품질 운영을 다룰 때 |
| 15 | [신뢰 경계와 Anti-Cheat](part15_anticheat_trust.md) | 서버 검증·행동 탐지·오탐 예산·제재 정책을 설계할 때 |

## 함께 읽을 내용

로그인, 계정, 영속 경제, 파티, 매치메이킹, 서버 할당과 fleet 운영은 `live_service`에서 이어서 다룬다. 매치메이커가 server endpoint와 token을 발급하고 게임 서버가 token을 검증해 실시간 session을 만드는 흐름으로 연결된다.

운영체제별 I/O의 내부 의미는 `system`에서 확인할 수 있다. 이 시리즈의 socket runtime에서 다루는 readiness와 completion의 수명 계약을 Linux `epoll`·`io_uring`과 Windows IOCP의 동작과 연결해 보자.

## 완료 조건

다음 질문에 코드와 측정값으로 답할 수 있어야 한다.

- TCP stream에서 분할·병합된 frame을 bounded memory로 복원할 수 있는가?
- UDP 위 reliable·unreliable channel이 wrap, loss, duplication, reordering에서 불변식을 지키는가?
- socket close와 in-flight I/O completion 사이에서 session 수명이 안전한가?
- 권위 server tick과 client render clock을 분리하고 prediction 오차를 재현할 수 있는가?
- snapshot byte budget과 AOI 전이가 congestion과 정보 노출을 함께 통제하는가?
- packet 내용과 적용 순서, 초기 상태, 빌드·에셋과 외부 사건을 보존해 실패 session을 재현할 수 있는가?

