## 들어가며

실제 소켓이 연결되어도 게임 서버가 자동으로 권위형이 되지는 않는다. 클라이언트가 보낸 위치를
그대로 저장하면 서버는 relay일 뿐이고, I/O thread가 도착 즉시 월드를 갱신하면 packet timing이
게임 규칙이 된다. 권위 서버는 입력 의도만 받고 자기 시계에서 상태를 계산해야 한다.

이 구현은 TCP로 control handshake를 수행하고, 난수 capability로 UDP endpoint를 결합한 뒤,
60Hz 정수 시뮬레이션과 20Hz snapshot을 실행한다.

---

## 목차

1. TCP와 UDP의 역할 분리
2. endpoint binding
3. 입력 검증과 순서
4. 고정 틱 시뮬레이션
5. snapshot과 prediction error
6. 이 모델의 한계
7. 검증 실행

---

## TCP와 UDP의 역할 분리

TCP control channel은 연결 생성과 작은 신뢰 메시지를 담당한다. 클라이언트가 Hello를 보내면
서버는 64비트 connection ID와 128비트 임시 token을 Welcome payload로 반환한다. TCP의 순서와
재전송은 handshake 구현을 단순하게 한다.

실시간 input과 snapshot은 UDP를 사용한다. 오래된 input이 늦게 도착했을 때 TCP head-of-line
blocking으로 최신 input까지 기다릴 이유가 없기 때문이다. 단, UDP 사용 자체가 저지연을
보장하지는 않는다. 중복·순서·크기·신원을 애플리케이션이 처리해야 한다.

## endpoint binding

TCP connection과 UDP source endpoint는 운영체제에서 서로 관련이 없다. packet의 connection ID만
믿으면 다른 송신자가 ID를 추측해 input을 보낼 수 있다. 서버는 Welcome token을 UDP Connect
payload에서 상수 시간으로 비교한다.

<!-- snippet: src/server_runtime.cpp#GameServerRuntime::OnDatagram -->
```cpp
if (message->type == DatagramType::Connect) {
    if (!TokenEquals(message->payload, client.token)) {
        ++metrics_.rejectedPackets;
        return;
    }
    client.endpoint = endpoint;
    client.udpBound = true;
    auto accepted = EncodeDatagram(DatagramType::ConnectAccepted,
                                   message->connection, message->sequence, {});
    backend_.SendDatagram(endpoint, accepted);
    return;
}
```

binding 이후 input은 저장된 IP·port와 실제 송신 endpoint가 같아야 한다. 이 token은 로컬 lab의
capability이며 계정 인증이나 암호화된 session ticket이 아니다. CSPRNG, 만료, audience, replay
방지가 필요한 production admission과 역할을 구분해야 한다.

## 입력 검증과 순서

클라이언트는 절대 위치가 아니라 틱마다 `moveX`, `moveY` 의도를 보낸다. payload는 정확히
4바이트여야 하고 각 값은 signed 16비트다. 서버는 connection 존재, Hello 완료, UDP binding,
endpoint 일치, message type, payload 길이를 모두 통과시킨 뒤 input을 저장한다.

같거나 오래된 sequence는 버린다.

<!-- snippet: src/server_runtime.cpp#GameServerRuntime::OnDatagram -->
```cpp
if (client.hasInput &&
    !IsNewerSequence(message->sequence, client.lastInputSequence)) return;
client.hasInput = true;
client.lastInputSequence = message->sequence;
client.moveX = DecodeI16(message->payload, 0);
client.moveY = DecodeI16(message->payload, 2);
```

현재 모델은 가장 최근 input을 매 server tick에 적용한다. 실제 게임에서는 command가 어느 tick을
대상으로 하는지, 입력이 없을 때 마지막 값을 유지할지 0으로 만들지, 초당 허용 이동량을 어떻게
clamp할지를 게임 규칙으로 명시해야 한다.

## 고정 틱 시뮬레이션

서버 위치는 정수다. float drift를 보여 주려는 물리 lab이 아니라 네트워크 시계와 권위 순서를
검증하는 것이 목적이므로, 한 tick의 상태 전이는 단순하다.

<!-- snippet: src/server_runtime.cpp#GameServerRuntime::RunTick -->
```cpp
for (auto& [id, client] : clients_) {
    static_cast<void>(id);
    client.positionX = static_cast<std::int32_t>(std::clamp<std::int64_t>(
        std::int64_t{client.positionX} + client.moveX, INT32_MIN, INT32_MAX));
    client.positionY = static_cast<std::int32_t>(std::clamp<std::int64_t>(
        std::int64_t{client.positionY} + client.moveY, INT32_MIN, INT32_MAX));
}
```

`Advance(now)`는 16,667마이크로초 간격으로 최대 4틱을 보충한다. event loop가 오래 멈춘 뒤 무한
catch-up에 빠지지 않도록 상한을 넘으면 시계를 현재 이후로 재설정한다. 이는 시간 부채 일부를
버리는 정책이다. 경쟁 게임에서는 tick overrun을 운영 장애로 기록하고 부하를 줄이는 정책도
필요하다.

네트워크 worker는 `Advance`를 호출하지 않는다. 서버 main loop가 backend event를 drain한 뒤
runtime 시계를 전진시켜 같은 스레드에서 상태 전이 순서를 고정한다.

## snapshot과 prediction error

세 server tick마다 connection별 snapshot을 보낸다. payload에는 서버가 마지막으로 수용한 input
sequence와 권위 위치가 들어간다. ACK는 클라이언트가 어떤 입력까지 확정되었는지 판단하는 기준이다.

클라이언트는 input 전송과 동시에 같은 이동량을 로컬 위치에 적용해 prediction을 관측한다. snapshot이
오면 권위 위치와 로컬 위치의 차이를 출력하고 권위 위치로 맞춘다.

<!-- snippet: src/client_main.cpp#main -->
```cpp
std::int32_t errorX = authoritativeX - predictedX;
std::int32_t errorY = authoritativeY - predictedY;
predictedX = authoritativeX;
predictedY = authoritativeY;
```

correction이 0이 아닌 것은 반드시 버그가 아니다. client input tick, server simulation tick,
snapshot tick이 서로 다른 시계이므로 snapshot 시점에 아직 서버가 처리하지 않은 입력이 존재한다.

## 이 모델의 한계

이 클라이언트는 권위 위치로 되돌린 뒤 ACK 이후의 미확정 입력을 replay하지 않는다. 따라서 완전한
server reconciliation 구현이 아니라 prediction error를 측정하는 최소 관측기다. 화면 smoothing,
input history replay, remote entity interpolation, teleport threshold도 없다.

서버도 reliable UDP channel, congestion control, delta baseline, AOI를 구현하지 않는다. 이 기능은
단순히 field를 추가하는 문제가 아니라 별도 window·lifetime·bandwidth 불변식을 요구한다.

## 검증 실행

여기서 만든 판정은 전부 `GameServerRuntime` 안에 있고, 그 객체는 `IServerBackend`만 참조한다.
그래서 socket도 thread도 없이 통째로 돌릴 수 있다. 기록용 fake backend를 주입하고
`NetworkEvent`를 손으로 만들어 넣으며 시각은 `Advance()` 인자로 준다. CTest 이름은
`game_network_step05_authority`다.

```powershell
ctest --test-dir projects/mini/game_network_lab/build -C Release `
      -R game_network_step05 --output-on-failure
```

```text
    Start 9: game_network_step05_authority
1/1 Test #9: game_network_step05_authority ....   Passed    0.06 sec

100% tests passed out of 1

Total Test time (real) =   0.08 sec
```

실행 파일을 직접 부르면 case 다섯 개가 각각 관측한 값이 나온다.

```powershell
projects/mini/game_network_lab/build/Release/game_network_runtime_tests.exe step05
```

```text
[step05] hello_welcome          ok  Welcome payload = 8B id + 16B token, token non-zero; a second Hello -> rejectedPackets=1 disconnects=1
[step05] forged_token           ok  one flipped token byte -> rejectedPackets=2 ConnectAccepted=0 snapshots=0 after 3 ticks
[step05] foreign_endpoint       ok  Input from an unbound endpoint -> rejectedPackets=1 position=(0, 0) ack=0
[step05] stale_input_ack        ok  duplicate and older sequences dropped, ack 0xfffffff0 -> 2 across the wrap, positionX=21
[step05] tick_snapshot_cadence  ok  ticks=12 snapshots=4 ack=7 position=(36, -24)
game_network_runtime_tests: 5 case(s) passed [step05 5]
```

`forged_token`의 reject가 2인 이유는 token 위조 하나와, 결합되지 않은 연결에서 이어 보낸
Input 하나가 각각 세어지기 때문이다. 결합에 실패한 연결은 계속 거부되어야지 조용히 통과해서는
안 된다.

마지막 case가 고정 틱과 snapshot 주기를 함께 못 박는다. 기대값은 `kTickInterval`이나
`kSnapshotEveryTicks`를 다시 계산해서 만들지 않고 숫자로 적는다. 상수를 인용해 기대값을
만들면 상수를 바꾸는 회귀에서 기대값도 같이 움직여 아무것도 잡지 못한다.

<!-- snippet: tests/runtime_tests.cpp#TestAuthoritativeTickAndSnapshotCadence -->
```cpp
AdvanceTicks(runtime, 12);
Require(runtime.Metrics().ticks == 12, "twelve spaced Advance calls run twelve ticks");

auto snapshots = CollectSnapshots(fake);
Require(snapshots.size() == 4, "twelve ticks broadcast four snapshots");
Require(runtime.Metrics().snapshots == 4, "the snapshot metric matches the sends");

for (std::size_t i = 0; i < snapshots.size(); ++i) {
    const std::int32_t tick = static_cast<std::int32_t>(3 * (i + 1));
    Require(snapshots[i].sequence == static_cast<std::uint32_t>(i + 1),
            "snapshot sequence increases by one per broadcast");
    Require(snapshots[i].ack == 7, "every snapshot acks the applied input");
    Require(snapshots[i].positionX == 3 * tick,
            "positionX integrates moveX once per tick");
    Require(snapshots[i].positionY == -2 * tick,
            "positionY integrates moveY once per tick");
}
```

`ack == 7`은 위치보다 먼저 깨지는 검사다. 서버가 마지막으로 *수신한* sequence를 ack에 넣으면
중복 input이 들어온 뒤 값이 달라진다. 권위 위치는 그대로여도 ack가 틀리면 클라이언트는
확정되지 않은 입력을 확정으로 판단한다.

---

## 결론

권위 서버는 TCP·UDP를 선택하는 것보다 상태 소유권을 명시하는 데서 시작한다. TCP는 handshake,
UDP는 최신 input과 snapshot, capability는 두 endpoint의 결합, 고정 틱은 상태 전이 순서를 맡는다.
클라이언트는 예측 오차를 숨기지 않고 ACK와 권위 위치로 관측한다. 이 구조는 구현한 범위와 완전한
상용 netcode 사이의 경계도 분명하게 보여 준다.
