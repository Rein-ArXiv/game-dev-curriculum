## 들어가며

4편의 도달점은 "바이트가 왕복한다"였다. `WriteSnapshot` 이 만든 벡터가 `ReadSnapshot` 으로 되돌아온다. 그 사이에는 아무것도 없다 — 손실도, 재정렬도, 지연도, 프로세스 경계도.

이 편의 도달점은 **두 프로세스가 한 판을 함께 도는 것**이다. 끝나면 다음이 성립한다.

- 창을 쓰는 실행과 창 없는 셀프테스트가 **완전히 같은 프레임 함수**를 지난다.
- 호스트가 권위를 갖고, 클라이언트는 명령만 보내고 상태를 받는다.
- 명령은 손실돼도 도착하고, 순서대로 정확히 한 번 적용된다.
- 클라이언트는 3틱마다 오는 스냅샷 사이를 보간해 60fps 로 그린다.
- 협동 클라이언트가 제 몫을 했는지를 **프로세스 종료 코드**로 답할 수 있다.

마지막 항목이 이 편에서 가장 오래 걸린 문제였다. 솔로·호스트는 "이겼는가"로 성공을 물을 수 있지만, 클라이언트는 권위 월드를 갖지 않아 그 질문 자체가 성립하지 않는다.

---

## 목차
1. [프레임 루프가 main() 안에 있으면 테스트할 수 없다](#1-프레임-루프가-main-안에-있으면-테스트할-수-없다)
2. [FrameInput: 창과 스크립트가 만나는 자리](#2-frameinput-창과-스크립트가-만나는-자리)
3. [한 프레임의 순서](#3-한-프레임의-순서)
4. [권위는 호스트에 있다](#4-권위는-호스트에-있다)
5. [신뢰 채널: 프레이밍을 갖지 않는 두 코어](#5-신뢰-채널-프레이밍을-갖지-않는-두-코어)
6. [클라이언트 펌프: 세 가지 주기](#6-클라이언트-펌프-세-가지-주기)
7. [스냅샷 사이를 채우는 렌더 시계](#7-스냅샷-사이를-채우는-렌더-시계)
8. [카운터가 한 바퀴 돌 때](#8-카운터가-한-바퀴-돌-때)
9. [클라이언트에게 성공이란 무엇인가](#9-클라이언트에게-성공이란-무엇인가)
10. [헤드리스가 도는 것은 흉내가 아니다](#10-헤드리스가-도는-것은-흉내가-아니다)
11. [실행과 관측](#11-실행과-관측)

---

## 1. 프레임 루프가 main() 안에 있으면 테스트할 수 없다

이 편의 구조 결정 하나가 나머지 전부를 결정했다. 그 결정의 배경은 이렇다.

초기 형태에서 `main()` 은 이렇게 생겼었다.

```
main()
  ├─ 인자 파싱
  ├─ --selftest 면 여기서 조기 반환 (간이 시뮬만 돌림)
  ├─ 창 생성
  ├─ 에셋 로드
  ├─ 소켓 열기
  └─ while (창이 살아 있는 동안)
        입력 폴링 → 넷 펌프 → 시뮬 → 렌더 → 표시
```

이 형태에는 구조적 결함이 있다. **창 없이 저 루프를 돌릴 방법이 없다.** 루프가 `window.PumpAndContinue()` 에 매달려 있고, 그 앞의 초기화가 창 생성과 얽혀 있기 때문이다.

그 결과가 `--selftest` 의 조기 반환이었다. 창을 못 만드는 환경에서 무언가는 검증해야 하니, 셀프테스트는 **루프를 흉내 낸 다른 코드**를 돌렸다. 그 순간 검증과 제품이 다른 코드를 본다. 에셋 로드도, 명령 경로도, 복제도, 진행도 커밋도 셀프테스트를 지나가지 않았다.

해법은 프레임 루프를 창에서 떼어 내는 것 하나뿐이다.

```
DefenseApp::Init(options)              창을 열지 않는다
DefenseApp::Step(FrameInput, seconds)  프레임 하나. 표시도 페이싱도 없다

RunDefenseWindowed(app, options)    창을 만들고 Step 을 부른다
RunDefenseHeadless(app, maxTicks)   스크립트로 Step 을 부른다
```

`Step` 밖에 남은 것은 **표시(`Present`)와 프레임 페이싱(`sleep_until`)** 뿐이다. 그 둘만이 창 실행과 헤드리스 실행의 진짜 차이이기 때문이다.

진입점은 그래서 29줄이 됐다.

<!-- snippet: games/defense/main.cpp -->
```cpp
#include "game/defense_app.hpp"

#include <cstdio>

int main(int argc, char** argv) {
    mega::game::DefenseOptions options;
    if (!mega::game::ParseDefenseOptions(argc, argv, options)) {
        std::fprintf(stderr, "%s", mega::game::DefenseUsage());
        return 2;
    }
    return mega::game::RunDefense(options);
}
```

진입점이 얇아야 하는 이유는 미학이 아니다. **여기가 두꺼우면 여기서만 도달 가능한 코드가 생기고, 그 코드를 검증하는 테스트는 존재할 수 없다.** 그 규칙을 사람의 규율이 아니라 빌드 실패로 강제하는 방법은 7편에서 다룬다.

## 2. FrameInput: 창과 스크립트가 만나는 자리

프레임 루프에 들어가는 것은 구조체 하나다.

<!-- snippet: include/game/defense_app.hpp#FrameInput -->
```cpp
struct FrameInput {
    std::array<bool, kDefenseActionKeys.size()> keyDown{};
    int  mouseX = -100, mouseY = -100;   // 맵 밖 = 배치 프리뷰 없음
    bool mouseClicked = false;           // 이번 프레임의 누름 엣지

    void Press(int virtualKey) {
        for (size_t i = 0; i < kDefenseActionKeys.size(); ++i)
            if (kDefenseActionKeys[i] == virtualKey) keyDown[i] = true;
    }
};
```

세 가지 결정이 있다.

**키는 배열이고 인덱스는 `kDefenseActionKeys` 의 순서다.** 가상 키 코드를 그대로 담으면 구조체가 256바이트가 되고, 무엇보다 헤드리스 스크립트가 "어느 키가 게임에 의미 있는가"를 다시 알아야 한다. 배열 인덱스로 묶으면 그 목록이 한 곳에만 있다.

**마우스 기본값이 `(-100, -100)` 이다.** 화면 밖이다. `DrawGame` 이 `hud.mouseY >= map.OriginY()` 일 때만 배치 프리뷰를 그리므로, 마우스를 설정하지 않은 프레임은 자동으로 "커서 없음"이 된다. 기본값 0이었다면 헤드리스 실행마다 좌상단 셀에 프리뷰가 깜빡였을 것이다.

**`mouseClicked` 는 이미 엣지다.** 키는 눌림 상태를 주고 `InputMap` 이 엣지를 만드는데(3편), 마우스는 반대다. 창의 `MouseClicked()` 가 이미 엣지를 계산하고 있고, 마우스 버튼은 액션에 바인딩되지 않아 `InputMap` 이 관여할 것이 없기 때문이다.

`Press` 헬퍼가 있는 이유는 헤드리스 스크립트가 `frame.Press('U')` 라고 쓸 수 있게 하기 위해서다. 인덱스를 직접 다루면 스크립트가 배열 순서를 알아야 한다.

## 3. 한 프레임의 순서

<!-- snippet: include/game/defense_app.hpp@frame-seam -->
```cpp
input_.BeginFrame();
for (size_t i = 0; i < kDefenseActionKeys.size(); ++i)
    input_.OnKey(kDefenseActionKeys[i], frame.keyDown[i]);

const Clock::time_point now = Clock::now();
frameSeconds = std::clamp(frameSeconds, 0.0, 0.25);
accumulator_ += frameSeconds;

{
    MEGA_PROFILE_SCOPE("net.pump");
    if (options_.mode == RunMode::Host) PumpHost();
    else if (options_.mode == RunMode::Client) PumpClient(now, frameSeconds);
}
```

한 프레임의 전체 순서는 이렇다.

```
1. 입력 매핑        키 눌림 → 액션 엣지
2. 시간 누적        프레임 초를 클램프해 누적기에 더한다
3. 네트워크 펌프    호스트: 명령 수신 · 클라이언트: 송신 + 스냅샷 수신
4. 뷰 선택          클라이언트는 보간 결과, 나머지는 권위 월드 캡처
5. 입력 → 명령      클릭 분기, 선택된 타워에 대한 U/X/T
6. 고정 스텝 시뮬   누적기가 허용하는 만큼(호스트·솔로만)
7. 연결 판정 · 소리 · 진행도 기록
8. 파티클 · 렌더
```

**펌프가 시뮬보다 먼저**인 것이 핵심이다. 호스트는 이번 프레임에 도착한 명령을 이번 프레임 시뮬에 반영해야 한다. 반대면 명령이 한 프레임 늦게 적용되고, 그 지연이 왕복 시간에 그대로 더해진다.

**뷰를 두 번 캡처한다.** 4단계에서 한 번(입력 판정용, 시뮬 이전 상태), 6단계 끝에서 다시 한 번(그리기용, 시뮬 이후 상태). 클릭 판정은 플레이어가 **화면에서 본 상태**를 기준으로 해야 하고, 그 화면은 지난 프레임의 것이다.

**클라이언트는 6단계를 건너뛴다.** `if (options_.mode != RunMode::Client)` 로 통째로 막혀 있다. 클라이언트가 로컬 월드를 굴리면 그것은 예측이고, 예측은 화면에 보이는 상태와 권위 상태가 갈라졌을 때 되감기를 요구한다. 협동 PvE 에서는 그 복잡도를 살 이유가 없다 — 적대적 지연 보상이 필요한 것은 대전이지 협동이 아니다.

## 4. 권위는 호스트에 있다

<!-- snippet: include/game/defense_app.hpp#PumpHost -->
```cpp
void PumpHost() {
    net::Bytes packet;
    while (network_.Receive(packet)) {
        if (wire::ReadHello(packet)) {
            network_.AcceptLastSender();
            continue;
        }
        wire::Command command;
        if (!wire::ReadCommand(packet, command)) continue;
        remoteCommands_.Accept(command.sequence, command);
    }
    wire::Command command;
    while (remoteCommands_.Poll(command)) {   // 순서대로, 정확히 한 번
        wire::ApplyCommand(world_, command);
        metrics_.GetCounter("remote_commands").Add();
    }
}
```

`AcceptLastSender()` 가 이 함수의 보안 경계다. `UdpTransport` 는 상대 하나를 모델링하는데, **첫 데이터그램의 발신자를 자동으로 상대로 삼으면** 아무 포트 스캐너나 한 바이트를 쏴서 그 자리를 차지할 수 있다. 그러면 진짜 클라이언트의 패킷은 "다른 발신자"로 버려진다.

그래서 트랜스포트는 발신자를 *후보*로만 기억하고, 응용 계층이 핸드셰이크를 확인한 뒤에만 승격시킨다. `ReadHello` 는 8바이트 헤더 전체(magic·version·type·길이)를 검사하므로, 우연히 그 8바이트를 맞출 확률은 무시할 만하다.

수신과 적용이 두 단계로 나뉜 것도 설계다. 첫 루프는 **소켓을 비우고** 채널에 넣는다. 둘째 루프는 채널에서 **연속된 것만** 꺼낸다. 이 분리가 없으면 재정렬된 패킷(3번이 2번보다 먼저 도착)을 도착 순서대로 적용하게 된다.

<!-- snippet: include/game/defense_app.hpp#SendSnapshot -->
```cpp
void SendSnapshot() {
    bool truncated = false;
    net::Bytes snapshot = wire::WriteSnapshot(
        world_, hostTick_, remoteCommands_.LastDelivered(), &truncated);
    if (snapshot.empty()) return;
    network_.Send(snapshot);
    metrics_.GetCounter("snapshots_sent").Add();
    if (truncated) metrics_.GetCounter("snapshots_truncated").Add();
    SnapshotBytesHistogram().Record(static_cast<double>(snapshot.size()));
}
```

`remoteCommands_.LastDelivered()` 가 **누적 ACK** 다. 스냅샷은 어차피 매 3틱 나가므로, 거기에 "여기까지 순서대로 반영했다"는 정수 하나를 실으면 별도의 ACK 패킷이 필요 없다. 4편에서 스냅샷 고정부에 `acknowledgedCommand` 4바이트를 잡아 둔 것이 이 자리다.

전송 주기는 `hostTick_ % 3 == 0`, 즉 20Hz 다. 60Hz 로 보내면 대역폭이 세 배인데, 클라이언트는 어차피 보간해 그리므로 화면 품질 차이가 거의 없다. 20Hz 는 보간 버퍼가 감당하는 하한(뒤에서 다룬다)과 대역폭 사이의 타협이다.

## 5. 신뢰 채널: 프레이밍을 갖지 않는 두 코어

명령은 반드시 도착해야 한다. 타워 건설 명령이 유실되면 플레이어는 골드를 냈는데 아무 일도 일어나지 않은 것으로 본다. 반면 스냅샷은 유실돼도 된다 — 다음 것이 오면 최신 상태가 복원된다.

그래서 명령만 신뢰 채널을 지난다. 그런데 이 저장소의 신뢰 채널에는 **자체 DATA/ACK 프레이밍**이 있었고, 이 프로토콜은 시퀀스를 명령 패킷에, ACK 를 스냅샷에 이미 싣고 있다. 프레이밍이 맞지 않는다.

그 상황에서 흔히 일어나는 일이 "제품이 자기 자리에 신뢰성 로직을 다시 구현하는 것"이다. 실제로 그렇게 됐었다. 해법은 채널을 **신뢰성 코어와 프레이밍으로 분리**하는 것이었다.

<!-- snippet: include/net/reliable_channel.hpp@reliable-sender -->
```cpp
template <class Payload = Bytes>
class ReliableOrderedSender {
public:
    explicit ReliableOrderedSender(uint32_t initialSequence = 0)
        : nextSend_(initialSequence) {}

    // 메시지를 큐잉하고 부여된 시퀀스를 돌려준다.
    uint32_t Queue(Payload message) {
        const uint32_t sequence = nextSend_++;
        unacked_.emplace(sequence, std::move(message));
        return sequence;
    }
    // ...
    // 누적 ack(포함): 상대가 sequence 까지 받았다.
    void AckThrough(uint32_t sequence) {
        for (auto it = unacked_.begin(); it != unacked_.end();)
            it = SequenceAtOrBefore(it->first, sequence) ? unacked_.erase(it)
                                                        : std::next(it);
    }

    // fn(sequence, payload) 를 미확인 메시지에 시퀀스 오름차순으로 적용.
    template <class Fn>
    void ForEachUnacked(Fn&& fn) const {
        for (const auto& entry : unacked_) fn(entry.first, entry.second);
    }
    // ...
private:
    uint32_t                    nextSend_ = 0;   // 다음 부여할 시퀀스
    std::map<uint32_t, Payload> unacked_;        // 미확인(재전송 대상)
};
```

이 클래스는 **전송을 하지 않는다.** 시퀀스를 부여하고, 미확인 목록을 관리하고, ACK 로 정리할 뿐이다. 실제로 바이트를 내보내는 것은 `ForEachUnacked` 를 받은 호출자다. 그래서 이 프로토콜은 자기 패킷 형식으로 내보내면서도 신뢰성 로직은 이것을 쓴다.

`std::map` 인 것이 중요하다. `ForEachUnacked` 가 **시퀀스 오름차순**으로 도는 것이 보장돼야 재전송 순서가 안정적이다. `unordered_map` 이면 같은 상태에서 매번 다른 순서로 나간다.

`AckThrough` 는 포함 경계(sequence 까지 받았다)이고, 자매 함수 `AckBefore` 는 배타 경계다. 두 형태를 다 두는 것이 값을 낸다 — 이 프로토콜의 스냅샷은 "마지막으로 반영한 명령 번호"를 실으므로 포함 경계가 맞고, 자체 프레이밍을 쓰는 채널은 "연속 수신 경계"를 보내므로 배타 경계가 맞다.

<!-- snippet: include/net/reliable_channel.hpp#Accept -->
```cpp
bool Accept(uint32_t sequence, Payload payload) {
    if (sequence == nextExpected_) {          // 기대한 다음 → 전달 + 버퍼 드레인
        delivered_.push_back(std::move(payload));
        ++nextExpected_;
        auto it = buffered_.find(nextExpected_);
        while (it != buffered_.end()) {
            delivered_.push_back(std::move(it->second));
            buffered_.erase(it);
            ++nextExpected_;
            it = buffered_.find(nextExpected_);
        }
        return true;
    }
    if (SequenceMoreRecent(sequence, nextExpected_) &&
        static_cast<uint32_t>(sequence - nextExpected_) <=
            kReceiveWindow)                   // 제한된 앞당겨 도착 → 버퍼링
        return buffered_.emplace(sequence, std::move(payload)).second;
    return false;                             // 과거·과도한 미래: 무시
}
```

세 갈래가 전부다.

- **기대한 번호** — 즉시 전달하고, 버퍼에 이어지는 것이 있으면 연쇄로 드레인한다.
- **앞당겨 도착** — 수신창(1024) 안이면 버퍼에 넣는다. 창 상한이 있어야 공격자가 시퀀스 40억을 보내 메모리를 채우지 못한다.
- **이미 받은 과거** — 무시. 재전송의 중복이 여기서 걸러진다.

`SequenceMoreRecent` 는 32비트 순환을 고려한 비교다. 단순 `>` 를 쓰면 카운터가 한 바퀴 돌 때 모든 새 패킷이 "과거"로 판정된다.

## 6. 클라이언트 펌프: 세 가지 주기

<!-- snippet: include/game/defense_app.hpp#PumpClient -->
```cpp
void PumpClient(Clock::time_point now, double frameSeconds) {
    if (now - lastHello_ >= std::chrono::milliseconds(500)) {
        network_.Send(options_.admissionToken.empty()
            ? wire::WriteHello()
            : wire::WriteAdmissionHello(options_.admissionToken));
        lastHello_ = now;
        ++helloPacketsSent_;
    }
    if (!localCommands_.Empty() &&
        now - lastResend_ >= std::chrono::milliseconds(100)) {
        localCommands_.ForEachUnacked(
            [&](uint32_t sequence, const wire::Command& queued) {
                wire::Command outgoing = queued;
                outgoing.sequence = sequence;
                network_.Send(wire::WriteCommand(outgoing));
                ++commandPacketsSent_;
            });
        lastResend_ = now;
    }
    net::Bytes packet;
    // ...
    while (network_.Receive(packet)) {
        // ...
        wire::Snapshot snapshot;
        if (!wire::ReadSnapshot(packet, snapshot)) continue;
        const bool truncated = snapshot.truncated;
        if (replicas_.Add(std::move(snapshot))) {
            lastSnapshot_ = now;
            ++snapshotsReceived_;
            if (truncated) metrics_.GetCounter("snapshots_truncated").Add();
        }
    }
    const wire::Snapshot* newest = replicas_.Newest();
    if (newest && newest->acknowledgedCommand > acknowledgedCommand_)
        acknowledgedCommand_ = newest->acknowledgedCommand;
    localCommands_.AckThrough(newest ? newest->acknowledgedCommand : 0);
    replicas_.Advance(frameSeconds * 60.0);
}
```

세 개의 서로 다른 주기가 한 함수에 있다.

**Hello 500ms.** UDP 에는 연결이 없으므로 호스트는 클라이언트의 존재를 데이터그램으로만 안다. 한 번만 보내면 그 하나가 유실됐을 때 영원히 붙지 못한다. 500ms 는 사람이 기다릴 수 있는 재시도 간격이면서 유휴 트래픽이 무시할 만한 값이다.

**명령 재전송 100ms.** 미확인 명령이 있는 동안만 돈다. 100ms 는 루프백에서는 과하고 인터넷에서는 RTT 근처다. 이 구현이 RTT 추정 대신 고정값을 쓰는 것은 명령이 드물게 발생하기 때문이다 — 초당 수십 개의 스냅샷과 달리 명령은 플레이어가 클릭할 때만 생긴다.

**스냅샷 수신은 매 프레임.** 소켓을 비울 때까지 돈다. 한 프레임에 하나만 읽으면 프레임이 밀렸을 때 수신 버퍼가 쌓이고, 그 지연이 계속 누적된다.

`replicas_.Add` 의 반환값을 보는 것이 미묘하다. 이미 지나간 틱의 스냅샷은 `false` 를 돌려주고, 그때는 `lastSnapshot_` 을 갱신하지 않는다. **살아 있는 링크의 정의가 "패킷이 온다"가 아니라 "새 상태가 온다"** 이기 때문이다. 호스트가 멈춘 채 같은 틱을 반복해 보내면 그것은 살아 있는 링크가 아니다.

`AckThrough(newest ? ... : 0)` 에서 스냅샷이 없을 때 0을 넘기는 것도 의도다. 첫 명령 번호가 1이므로 0은 "아무것도 확인되지 않음"이고, `AckThrough(0)` 는 아무것도 지우지 않는다.

호스트가 알려 준 ACK 를 `acknowledgedCommand_` 에 단조 증가로만 반영하는 것(`>` 비교)은 재정렬된 스냅샷 때문이다. 오래된 스냅샷이 뒤늦게 도착해 ACK 를 되돌리면 이미 확인된 명령이 다시 미확인이 된다.

## 7. 스냅샷 사이를 채우는 렌더 시계

호스트는 20Hz 로 보내고 클라이언트는 60fps 로 그린다. 받은 즉시 그리면 화면이 20Hz 로 끊긴다.

해법은 **약간 과거를 그리는 것**이다. 최신 스냅샷보다 몇 틱 뒤의 시점을 렌더 시각으로 잡으면, 그 시각을 감싸는 두 스냅샷이 항상 버퍼에 있으므로 그 사이를 보간할 수 있다.

<!-- snippet: include/net/session.hpp#InterpolationClock -->
```cpp
struct InterpolationClock {
    double delayTicks = 3.0;  // 새 타임라인에서 시계를 최신보다 이만큼 뒤에 건다
    double leadTicks  = 3.0;  // 시계는 최신에 이보다 가까이 다가가지 않는다
    size_t keep       = 16;   // 버퍼가 보관하는 스냅샷 수
};
```

디펜스가 쓰는 값은 이렇다.

```cpp
wire::SnapshotView replicas_{net::InterpolationClock{3.0, 2.0, 8}};
```

세 숫자에 각각 근거가 있다.

**지연 3틱.** 호스트는 3틱마다 보낸다. 지연이 3틱 미만이면 렌더 시각이 최신 스냅샷보다 앞서게 되어 보간할 미래가 없다. 정확히 3틱이면 스냅샷 하나 분량의 여유다.

**리드 2틱.** 시계가 최신에 2틱보다 가까이 붙지 않는다. 지연(3)보다 작은 이유는 이것이 **상한이지 목표가 아니기** 때문이다. 클라이언트 프레임이 호스트보다 조금 빨리 흐르면 시계가 최신 쪽으로 밀리는데, 2틱 지점에서 멈춰 서서 보간 가능한 구간을 유지한다.

**보관 8개.** 3틱 간격 × 8 = 24틱, 0.4초 분량이다. 이보다 크게 잡을 이유가 없다 — 0.4초를 넘게 밀린 상태는 보간으로 감출 수 있는 상황이 아니다.

<!-- snippet: include/net/session.hpp@interp-advance -->
```cpp
void Advance(double ticks) {
    if (buf_.Empty()) return;
    renderTick_ += ticks;
    const double ceiling = static_cast<double>(buf_.NewestTick()) - clock_.leadTicks;
    if (renderTick_ > ceiling) renderTick_ = ceiling;
    const double floor = static_cast<double>(buf_.OldestTick());
    if (renderTick_ < floor) renderTick_ = floor;
}
```

**클램프 순서가 결정적이다.** 위쪽(최신에 너무 붙음)을 먼저 막고 아래쪽(버퍼에서 밀려남)을 나중에 막는다. 순서가 반대면 버퍼에 스냅샷이 하나뿐일 때 — `OldestTick == NewestTick` 이므로 천장이 바닥보다 2틱 아래다 — 위쪽 클램프가 이겨서 존재하지 않는 과거를 가리킨다. 지금 순서에서는 아래쪽이 이겨서 그 하나를 그대로 보게 된다.

보간 자체는 프로토콜이 갖는다.

<!-- snippet: include/game/defense_protocol.hpp#Lerp -->
```cpp
inline Snapshot Lerp(const Snapshot& a, const Snapshot& b, float t) {
    Snapshot out = b;
    out.coreHp = a.coreHp + (b.coreHp - a.coreHp) * t;
    for (EnemyState& enemy : out.enemies) {
        const EnemyState* previous = FindById(a.enemies, enemy.id);
        if (!previous) continue;
        enemy.pos = previous->pos + (enemy.pos - previous->pos) * t;
        enemy.hp = previous->hp + (enemy.hp - previous->hp) * t;
    }
    for (ProjectileState& projectile : out.projectiles) {
        const ProjectileState* previous = FindById(a.projectiles, projectile.id);
        if (previous)
            projectile.pos = previous->pos + (projectile.pos - previous->pos) * t;
    }
    return out;
}
```

**`out = b` 로 시작한다.** 즉 기본은 최신 스냅샷이고, 보간하는 것은 연속적인 양(위치·체력)뿐이다. 골드·웨이브·승패는 이산이라 보간이 의미 없다 — 골드 420과 490 사이의 455는 어느 순간에도 실제였던 적이 없는 값이다.

**대응이 없으면 `b` 값을 그대로 쓴다.** 방금 등장한 적은 보간할 과거가 없다. `continue` 로 넘어가면 `out` 이 이미 `b` 의 복사본이므로 최신 위치가 남는다.

**타워는 보간하지 않는다.** 타워는 움직이지 않기 때문이다. 순회를 생략하면 타워가 많을 때 프레임당 `FindById` 호출이 그만큼 줄어든다.

`FindById` 가 선형 탐색인 것은 컬렉션 상한이 작기 때문이다(적 64, 발사체 20). 해시맵을 쓰면 매 프레임 맵 두 개를 만들어야 하고, 그 비용이 64개 선형 탐색보다 크다.

## 8. 카운터가 한 바퀴 돌 때

`tick` 은 `uint32_t` 다. 60Hz 로 약 828일이면 한 바퀴 돈다. 실용적으로 도달하지 않는 값이지만, 문제는 다른 데 있다 — **호스트가 재시작하면 틱이 0부터 다시 시작한다.**

렌더 시계는 `double` 이다. 4294967290 근처를 가리키던 시계에 틱 5인 스냅샷이 들어오면, 보간 구간 계산이 무의미해진다.

<!-- snippet: include/net/snapshot.hpp#Add -->
```cpp
bool Add(Snap s) {
    if (!snaps_.Empty()) {
        if (s.tick == snaps_.Newest().tick) {
            snaps_.Newest() = std::move(s);
            return true;
        }
        if (!SequenceMoreRecent(s.tick, snaps_.Newest().tick)) return false;
        // float render ticks cannot interpolate across uint32 wrap. Start a
        // fresh interpolation epoch when the monotonic counter wraps.
        if (s.tick < snaps_.Newest().tick) snaps_.Clear();
    }
    if (snaps_.Empty()) ++epoch_;
    snaps_.Push(std::move(s));
    return true;
}
```

`SequenceMoreRecent` 는 순환을 고려하므로 "4294967290 다음의 5"를 최신으로 인정한다. 그러나 **`double` 렌더 시계는 그 점프를 표현할 수 없다.** 그래서 수치적으로 작아진 틱이 오면 버퍼를 비우고 세대(`epoch_`)를 올린다.

시계를 굴리는 쪽이 그 세대를 본다.

```cpp
bool Add(Snap s) {
    if (!buf_.Add(std::move(s))) return false;
    if (buf_.Epoch() != epoch_) {
        epoch_ = buf_.Epoch();
        renderTick_ = static_cast<double>(buf_.NewestTick()) - clock_.delayTicks;
    }
    return true;
}
```

세대가 바뀌면 시계를 **최신보다 `delayTicks` 뒤에 다시 건다.** 이 재앵커링은 두 상황에서 일어난다 — 첫 스냅샷을 받았을 때(버퍼가 비어 있다 채워짐)와 카운터가 순환했을 때. 두 상황의 처리가 같아야 하는 이유는 둘 다 "이전 타임라인과 이어지지 않는 새 타임라인의 시작"이기 때문이다.

같은 틱이 다시 오면 **덮어쓰고 `true`** 를 돌려준다. 호스트가 같은 틱을 재전송하는 경우이고, 내용이 갱신됐을 수 있으므로 최신 것으로 바꾼다.

## 9. 클라이언트에게 성공이란 무엇인가

여기가 이 편에서 가장 오래 걸린 문제다.

솔로·호스트 셀프테스트의 성공 조건은 명확하다 — "이겼는가". 클라이언트에게 그 질문은 **성립하지 않는다.** 클라이언트는 권위 월드를 갖지 않는다. 로컬 `world_` 는 `Init` 에서 만들어지고 한 번도 `Step` 되지 않으므로, 그것을 읽으면 항상 0이 나온다. 그 0은 실패와 구분되지 않는다.

그래서 클라이언트 셀프테스트는 오랫동안 **항상 1을 반환**했다. 협동 클라이언트를 실행하는 어떤 자동 검사도 결과를 읽을 수 없었다는 뜻이다.

질문을 바꿨다. "이겼는가" 대신 **"왕복이 실제로 일어났는가"** 를 묻는다.

<!-- snippet: include/game/defense_app.hpp#ClientRoundTripProven -->
```cpp
inline bool ClientRoundTripProven(const DefenseApp& app) {
    return app.Connected() && app.SnapshotsReceived() > 0 &&
           app.CommandPacketsSent() > 0 && app.AcknowledgedCommand() > 0;
}
```

네 조건이 왕복의 네 구간에 대응한다.

| 조건 | 증명하는 것 |
|---|---|
| `Connected()` | 살아 있는 스냅샷 스트림을 최근에 봤다 |
| `SnapshotsReceived() > 0` | 호스트 → 클라이언트 방향이 산다 |
| `CommandPacketsSent() > 0` | 클라이언트 → 호스트 방향으로 **바이트가 실제로 나갔다** |
| `AcknowledgedCommand() > 0` | 호스트가 그 명령을 순서대로 반영했다고 통지했다 |

세 번째가 왜 필요한지가 이 절의 요점이다. 명령이 큐에만 쌓이고 소켓으로 나가지 않는 상태 — `PumpClient` 의 재전송 블록이 통째로 죽은 상태 — 가 가능하다. 그 상태에서 클라이언트는 화면상 정상으로 보인다. 스냅샷을 받아 그리고 있으니까. 다만 **타워를 하나도 세우지 못한다.**

그래서 관측점을 넷 만들었다.

```cpp
uint64_t HelloPacketsSent() const { return helloPacketsSent_; }
uint64_t CommandPacketsSent() const { return commandPacketsSent_; }
uint64_t CommandsQueued() const { return commandsQueued_; }
uint64_t SnapshotsReceived() const { return snapshotsReceived_; }
```

`CommandsQueued` 와 `CommandPacketsSent` 를 **따로** 세는 것이 진단의 핵심이다. 전자가 0이면 입력이 명령을 만들지 못한 것이고, 전자는 양수인데 후자가 0이면 송신 배선이 끊긴 것이다. 하나로 합치면 그 구분이 사라진다.

이 게임플레이는 이 카운터들을 읽지 않는다. 순전히 관측용이고, 그것이 이 넷이 존재하는 이유 전부다.

판정도 세 상태로 나눴다.

<!-- snippet: include/game/defense_app.hpp#RunDefenseSelftest -->
```cpp
inline int RunDefenseSelftest(DefenseApp& app, const DefenseOptions& options) {
    const SelftestReport report = RunDefenseHeadless(app, options.selftestTicks);
    const bool contentOk = app.Skin().externalAssets == 10 && app.ExternalWaves() &&
                           app.ExternalBalance() && app.ExternalPoses() &&
                           (app.ExternalMap() || app.Map().Generated());
    // ...
    if (options.mode == RunMode::Client) {
        const char* result = report.commandAck > 0
            ? "LINKED"
            : (report.snapshotsReceived > 0 ? "NO-ACK" : "NO-LINK");
        // ... 카운터를 한 줄로 찍는다
        app.PrintMetrics();
        app.PrintProfile();
        app.DumpTrace();

        const bool ok = report.connected && report.snapshotsReceived > 0 &&
                        report.commandPacketsSent > 0 && report.commandAck > 0 &&
                        report.renderedPixels && report.protocolOk && contentOk;
        return ok ? 0 : 1;
    }
    // ...
}
```

- **LINKED** — 명령이 나갔고 호스트가 반영했다. 성공.
- **NO-ACK** — 상태는 받는데 내 명령이 반영되지 않는다. 송신 방향이 끊겼다.
- **NO-LINK** — 아예 붙지 못했다. 포트·주소·방화벽 문제다.

세 상태를 구분해야 **실패가 어느 방향의 배선인지** 밖에서 읽힌다. 하나의 불린이면 "안 됐다"만 남는다.

## 10. 헤드리스가 도는 것은 흉내가 아니다

<!-- snippet: include/game/defense_app.hpp#RunDefenseHeadless -->
```cpp
inline SelftestReport RunDefenseHeadless(DefenseApp& app, int maxTicks) {
    SelftestReport report;
    const DefenseMap& map = app.Map();
    const std::vector<DefenseMap::Cell> plan = SelftestPlan(map);
    // ...
    for (report.ticks = 0; report.ticks < maxTicks; ++report.ticks) {
        FrameInput frame;

        if (planCursor < plan.size()) {
            frame.Press(static_cast<int>('1' + (planCursor % 4)));
            clickAt(frame, map.CellCenter(plan[planCursor].x, plan[planCursor].y));
            frame.mouseClicked = true;
        } else if (!app.View().towers.empty()) {
            // 남는 골드는 강화에 쓴다. 한 틱은 선택(클릭), 다음 틱은 'U'.
            const auto& towers = app.View().towers;
            const auto& tower = towers[upgradeCursor % towers.size()];
            clickAt(frame, tower.pos);
            if (selecting) {
                frame.mouseClicked = true;
            } else {
                frame.Press('U');
                ++upgradeCursor;
            }
            selecting = !selecting;
        }
        // ...

        app.Step(frame, static_cast<double>(kDefenseFixedDt));

        const int towers = app.World().TowerCount();
        if (planCursor < plan.size()) {
            if (towers > placedTowers) {
                placedTowers = towers;
                ++planCursor;
                stallTicks = 0;
            // ...
            }
        }
        if (app.Connected()) report.connected = true;
        if (app.Finished()) { ++report.ticks; break; }
        if (app.Options().mode == RunMode::Client && ClientRoundTripProven(app)) {
            ++report.ticks;
            break;
        }
    }
    // ...
}
```

이 루프가 하는 일은 `FrameInput` 을 만드는 것뿐이다. 그 뒤로는 창 실행과 **완전히 같은 `app.Step`** 이다.

스크립트의 설계에도 결정이 있다.

**골드가 부족하면 같은 셀을 계속 시도한다.** `planCursor` 는 타워 수가 실제로 늘었을 때만 전진한다. 명령이 거부되면 다음 틱에 같은 클릭이 다시 나가고, 이것이 **실제 플레이와 같은 경로**다 — 사람도 골드가 모일 때까지 같은 자리를 다시 누른다.

**배치가 끝나면 강화로 넘어간다.** 한 틱은 클릭(선택), 다음 틱은 `U`. 두 틱으로 나눈 이유는 3편의 클릭 분기 때문이다 — 같은 프레임에 선택과 강화를 넣으면, 선택이 반영된 `selectedTower_` 를 그 프레임의 강화 분기가 이미 지나쳤을 수 있다.

**종료 조건이 모드마다 다르다.** 솔로·호스트는 `Finished()`(이기거나 졌다), 클라이언트는 `ClientRoundTripProven()`. **정지 조건이 성공 조건과 같은 모양**이어야 한다 — 클라이언트를 `Finished()` 로 돌리면 로컬 월드가 영원히 끝나지 않으므로 항상 최대 틱까지 돌고 타임아웃으로 보고된다.

루프 뒤에는 세 개의 사후 검사가 있다.

```cpp
const net::Bytes encoded = wire::WriteSnapshot(app.World(), app.HostTick(), 0);
wire::Snapshot decoded;
report.protocolOk = !encoded.empty() && wire::ReadSnapshot(encoded, decoded);
report.packetBytes = encoded.size();
```

실제로 돌린 월드를 그대로 전송 포맷으로 왕복시킨다. 4편에서 본 `packet=175` 가 이 줄에서 나온다.

```cpp
const uint32_t background = render::Pack({14, 18, 23, 255});
// ... 프레임버퍼 전체를 훑어 배경색이 아닌 픽셀을 찾는다
```

1편에서 본 `render=OK` 가 이 줄이다.

그리고 클라이언트는 보고할 값의 출처가 다르다.

```cpp
if (app.Options().mode == RunMode::Client) {
    report.towers = static_cast<int>(app.View().towers.size());
    report.wave = static_cast<int>(app.View().wave);
    // ...
}
```

**클라이언트가 아는 세계는 복제된 뷰뿐이다.** 스텝된 적 없는 로컬 월드를 읽으면 언제나 0이 나와 실패와 구분되지 않는다.

창 루프 쪽은 이렇게 생겼다.

<!-- snippet: include/game/defense_app.hpp#RunDefenseWindowed -->
```cpp
Clock::time_point previousFrame = Clock::now();
Clock::time_point nextFrame = previousFrame;
while (window.PumpAndContinue()) {
    FrameInput frame;
    for (size_t i = 0; i < kDefenseActionKeys.size(); ++i)
        frame.keyDown[i] = window.Key(kDefenseActionKeys[i]);
    frame.mouseX = window.MouseX();
    frame.mouseY = window.MouseY();
    frame.mouseClicked = window.MouseClicked();

    const Clock::time_point now = Clock::now();
    const double frameSeconds =
        std::chrono::duration<double>(now - previousFrame).count();
    previousFrame = now;

    app.Step(frame, frameSeconds);
    // ... 표시 백엔드가 프레임버퍼를 화면에 올린다
    nextFrame += std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(kDefenseFixedDt));
    if (nextFrame > Clock::now()) std::this_thread::sleep_until(nextFrame);
    else nextFrame = Clock::now();
    app.RecordFrameMs(std::chrono::duration<double, std::milli>(Clock::now() - now).count());
}
```

프레임 페이싱이 `nextFrame += dt` 형태인 것이 결정이다. `sleep_for(dt - 소요시간)` 으로 쓰면 매 프레임의 오차가 누적돼 장기적으로 60Hz 에서 벗어난다. 절대 시각을 누적하면 개별 프레임이 늦어도 다음 프레임이 따라잡는다.

`else nextFrame = Clock::now();` 는 지나치게 밀렸을 때 목표 시각을 현재로 재설정한다. 없으면 한 번 크게 밀린 뒤 그 빚을 갚느라 여러 프레임을 sleep 없이 연속 실행한다.

## 11. 실행과 관측

협동 배선에는 세 층의 검사가 있다.

```powershell
cmake --build build --config Release -j
ctest --test-dir build -C Release -R "client_loop|coop_process|defense_process"
```

```text
    Start 11: test_defense_client_loop
1/3 Test #11: test_defense_client_loop .........   Passed    0.04 sec
    Start 13: test_defense_coop_process
2/3 Test #13: test_defense_coop_process ........   Passed    0.87 sec
    Start 15: test_defense_process
3/3 Test #15: test_defense_process .............   Passed    0.27 sec

100% tests passed out of 3

Total Test time (real) =   1.19 sec
```

**첫째 층 — 제품 클라이언트를 진짜 소켓 위에서 돌린다.**

```powershell
.\build\bin\Release\test_defense_client_loop.exe
```

```text
[test] defense_client_loop
[assets] loaded 10/10 external sprites from C:\...\build\bin\Release/assets
[content] map=external waves=external (5) balance=script
[network] joining 127.0.0.1:65168
[test] defense_client_loop: frames=2 hello=1 sent=1 queued=2 snapshots=1 ack=1
[test] defense_client_loop: 23 checks, 0 failures
```

이 테스트는 `DefenseApp` 을 `RunMode::Client` 로 구성하고, 반대편 끝을 자기가 `net::UdpTransport` 로 잡는다. 창도 스크립트도 아닌 테스트가 `FrameInput` 을 만들고, 그 뒤로는 제품이 도는 것과 같은 `Step()` 이 돈다.

`frames=2` 가 인상적이다. **두 프레임 만에** 명령이 소켓 밖으로 나갔다. 그리고 그 명령이 시퀀스 1이고, 타입이 `Place` 이고, 인자가 `Cannon` 이고, x·y 가 `EncodePosition(target)` 과 정확히 같은지까지 확인한다 — 3편의 클릭 분기부터 4편의 인코딩까지가 한 줄에 걸린다.

`queued=2` 와 `sent=1` 이 다른 것도 정보다. 두 프레임 동안 클릭이 두 번 들어가 명령이 둘 큐잉됐지만, 재전송 주기(100ms) 때문에 실제로 나간 것은 한 번의 배치뿐이다.

두 번째 단계에서는 호스트 역할이 그 명령을 자기 월드에 적용하고 스냅샷으로 돌려준다. 그 결과 `ack=1` 이 되고, 미확인 목록이 줄고, `app.View().towers` 에 타워가 하나 나타난다 — **복제된 상태가 눈에 보인다.** 마지막으로 `ClientRoundTripProven(app)` 자체를 단언한다. 제품의 판정 기준을 테스트가 검사하는 것이다.

**둘째 층 — 양쪽이 모두 제품 실행 파일이다.**

```powershell
.\build\bin\Release\test_defense_coop_process.exe
```

```text
[test] defense_coop_process
[test] defense_coop_process: attempts=1 host_exit=0 client_exit=0 remote_commands=1 snapshots=414 client_sent=1 client_ack=1
[test] defense_coop_process: 9 checks, 0 failures
```

호스트도 클라이언트도 `mega_defense` 다. 테스트가 대신 구현하는 코드는 한 줄도 없다 — argv 파싱부터 프레임 루프까지 전부 제품이 돈다. 테스트는 프로세스를 띄우고 두 표준 출력을 읽고 왕복을 셀 뿐이다.

`snapshots=414` 는 호스트가 실제로 방송한 스냅샷 수다. `remote_commands=1` 은 호스트가 클라이언트 명령을 순서대로 반영한 수이고, `client_sent=1` 은 클라이언트가 내보낸 명령 패킷 수다. **두 숫자가 양쪽 프로세스의 서로 다른 표준 출력에서 나온다** — 한쪽만 보면 어느 방향이 끊겼는지 알 수 없다.

그리고 클라이언트 로그에서 `result=LINKED` 를 찾는다. 9절에서 정의한 세 상태 중 성공에 해당하는 것이다.

이 테스트의 결정성 설계도 적어 둔다. 포트는 OS 가 고르게 하고(bind 0), 대기는 고정 sleep 이 아니라 **호스트가 `[network] hosting UDP port N` 을 실제로 찍을 때까지 로그를 폴링**한다. 제품이 그 줄에서 `fflush(stdout)` 을 하는 이유가 이것이다 — 표준 출력이 파일로 리다이렉트되면 기본이 전량 버퍼링이라, flush 가 없으면 그 줄은 프로세스가 끝날 때까지 보이지 않고 테스트는 고정 sleep 으로 추측할 수밖에 없다.

재시도 조건도 좁다. **"둘이 아예 만나지 못했다"(호스트 스냅샷 0)일 때만** 다시 돈다. 그것은 환경 문제다. "만났는데 명령이 오지 않았다"는 재시도하지 않는다 — 그것이 정확히 이 테스트가 잡으려는 결함이기 때문이다.

**셋째 층 — 제품 자신의 셀프테스트.**

```powershell
.\build\bin\Release\mega_defense.exe --selftest
```

```text
[selftest] result=CLEAR ticks=1377 towers=11 wave=6 packet=175 protocol=OK render=OK assets=10/10 content=external
[metrics] matches=1 remote_commands=0 snapshots=0 commands_sent=0 command_ack=0 frame_p95=0.0ms snapshot_bytes_p95=0 snapshot_bytes_max=0 snapshots_truncated=0
```

솔로 모드라 네트워크 카운터가 전부 0이다. 그것이 정상이다 — 솔로는 소켓을 열지 않는다. `matches=1` 은 한 판이 끝나 진행도에 기록됐다는 뜻이고, `frame_p95=0.0ms` 는 헤드리스가 프레임 시간을 재지 않기 때문이다(페이싱이 창 루프에만 있다).

같은 실행 파일을 협동으로 띄우면 그 자리가 채워진다. 위 `test_defense_coop_process` 의 `snapshots=414` 가 바로 호스트 쪽 `[metrics]` 줄에서 읽은 값이다.

---

## 결론

이 단계에서 세운 것.

1. **프레임 루프를 창에서 떼어 냈다** — `DefenseApp::Step(FrameInput, seconds)` 하나. 밖에 남은 것은 표시와 페이싱뿐이고, 그래서 헤드리스가 흉내가 아니라 제품의 진짜 루프를 돈다. 진입점은 29줄이다.
2. **`FrameInput`** — 키는 배열 인덱스, 마우스 기본값은 화면 밖, 클릭은 이미 엣지. 창과 스크립트가 만나는 유일한 형태.
3. **프레임 순서** — 펌프가 시뮬보다 먼저, 뷰는 두 번 캡처(판정용/그리기용), 클라이언트는 시뮬을 건너뛴다.
4. **핸드셰이크 후 발신자 승격** — 첫 데이터그램이 자동으로 상대가 되면 아무나 그 자리를 차지한다.
5. **신뢰성 코어와 프레이밍의 분리** — 이 프로토콜은 시퀀스를 명령에, 누적 ACK 를 스냅샷에 실으므로 프레이밍 없이 코어만 쓴다. 그 분리가 없으면 제품이 신뢰성 로직을 자기 자리에 다시 구현한다.
6. **세 가지 주기** — Hello 500ms, 명령 재전송 100ms, 수신은 매 프레임 소켓을 비울 때까지.
7. **보간 시계 (3, 2, 8)** — 지연은 전송 간격만큼, 리드는 그보다 작게, 보관은 0.4초. 클램프 순서가 스냅샷 하나뿐일 때의 동작을 결정한다.
8. **세대 재앵커링** — 카운터 순환과 첫 스냅샷을 같은 규칙으로 처리한다. 둘 다 "이어지지 않는 새 타임라인"이다.
9. **클라이언트의 성공 정의** — "이겼는가"가 불가능하므로 "왕복이 일어났는가"로 바꿨다. 관측점 넷과 상태 셋(LINKED/NO-ACK/NO-LINK)이 실패의 방향을 밖에서 읽히게 한다.

현재 상태의 한계. 이 협동은 **플레이어 둘이 서로를 신뢰한다는 전제** 위에 있다. 호스트는 Hello 를 보낸 아무나 상대로 삼고, 누가 접속했는지 묻지 않으며, 정원 개념도 없다. 그리고 호스트가 곧 플레이어이므로 호스트가 나가면 판이 끝난다. 진행도는 로컬 파일 하나에 쌓이는데 그 파일이 쓰기 도중 크래시를 견디는지는 아직 아무도 묻지 않았다. 플레이어 없는 권위 프로세스, 입장 자격 검증, 그리고 크래시를 견디는 정산이 다음 편의 주제다.
