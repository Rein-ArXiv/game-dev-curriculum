## 들어가며

5편의 협동은 전제 하나 위에 서 있다. **플레이어 둘이 서로를 신뢰한다.** 호스트는 Hello 를 보낸 아무나를 상대로 삼고, 누가 접속했는지 묻지 않으며, 정원도 없다. 그리고 호스트가 곧 플레이어이므로 호스트가 나가면 판이 끝난다.

이 편의 도달점은 둘이다.

**첫째, 플레이어 없는 권위 프로세스.** `mega_defense_server` 는 창도 렌더도 없이 게임 규칙만 돌리고, 여러 끝점을 동시에 상대하며, 자격을 증명한 클라이언트만 받는다.

**둘째, 크래시를 견디는 정산.** 한 판이 끝나면 프로필과 매치 기록이 디스크로 간다. 그 쓰기가 도중에 끊겨도 이전 상태가 남거나 새 상태가 남을 뿐, **반쯤 쓰인 상태는 남지 않는다.**

두 주제는 무관해 보이지만 공통점이 있다. 둘 다 **프로세스가 죽는 것을 정상 사건으로 취급한다.** 서버는 신호를 받고 정리하며 끝나야 하고, 저장소는 정리하지 못하고 죽어도 다음 기동에서 회복돼야 한다.

---

## 목차
1. [호스트-플레이어와 전용 서버는 다른 물건이다](#1-호스트-플레이어와-전용-서버는-다른-물건이다)
2. [끝점별 세션: 상대 하나 모델을 버린다](#2-끝점별-세션-상대-하나-모델을-버린다)
3. [서비스 사이클: 수신 → 시뮬 → 방송](#3-서비스-사이클-수신--시뮬--방송)
4. [둘이 모이기 전에는 시간이 흐르지 않는다](#4-둘이-모이기-전에는-시간이-흐르지-않는다)
5. [입장 토큰: 다섯 가지 거부](#5-입장-토큰-다섯-가지-거부)
6. [상수 시간 비교와 오프라인 검증](#6-상수-시간-비교와-오프라인-검증)
7. [드레인 종료](#7-드레인-종료)
8. [WAL: COMMIT 마커 뒤에 fsync 한다](#8-wal-commit-마커-뒤에-fsync-한다)
9. [재기동이 원자성을 넘기게 하는 한 줄](#9-재기동이-원자성을-넘기게-하는-한-줄)
10. [한 트랜잭션에 프로필과 매치 기록을 함께](#10-한-트랜잭션에-프로필과-매치-기록을-함께)
11. [저장 파일은 실행 파일 옆이 아니다](#11-저장-파일은-실행-파일-옆이-아니다)
12. [실행과 관측](#12-실행과-관측)

---

## 1. 호스트-플레이어와 전용 서버는 다른 물건이다

`mega_defense --host` 로도 협동이 된다. 그런데도 별도 실행 파일을 만든 이유가 넷이다.

| | 호스트-플레이어 | 전용 서버 |
|---|---|---|
| 창·렌더 | 있다 | 없다 |
| 상대 | **하나** | 여럿(정원 2) |
| 입장 자격 | 묻지 않는다 | HMAC 서명 토큰 |
| 종료 | 창을 닫는다 | OS 신호 · 실행 시간 상한 |

셋째가 결정적이다. 호스트-플레이어는 친구와 하는 판이라 "Hello 를 보낸 사람"이면 충분하다. 인터넷에 노출된 프로세스에서는 그 규칙이 성립하지 않는다 — 누가 접속했는지, 그 사람이 이 매치에 속하는지, 이미 들어와 있지 않은지를 물어야 한다.

진입점은 클라이언트와 같은 형태다.

<!-- snippet: games/defense/server_main.cpp -->
```cpp
#include "game/defense_server.hpp"

#include <cstdio>

int main(int argc, char** argv) {
    mega::game::DefenseServerOptions options;
    if (!mega::game::ParseDefenseServerOptions(argc, argv, options)) {
        std::fprintf(stderr, "%s", mega::game::DefenseServerUsage());
        return 2;
    }
    return mega::game::RunDefenseServer(options);
}
```

18줄이다. 5편과 같은 이유로 얇다 — 여기가 두꺼우면 여기서만 도달 가능한 코드가 생기고, 그 코드를 검증하는 테스트는 존재할 수 없다.

두 실행 파일이 공유하는 것도 분명하다. **게임 규칙과 와이어 포맷이 같다.** `GameWorld`·`wire::WriteSnapshot`·`wire::ApplyCommand`·콘텐츠 로더가 전부 같은 코드다. 다른 것은 소켓 모델과 입장 정책과 종료 조건뿐이다.

## 2. 끝점별 세션: 상대 하나 모델을 버린다

`net::UdpTransport` 는 상대 하나를 모델링한다. 5편의 호스트에는 그것으로 충분했다 — 협동 인원이 둘이고, 그중 하나가 호스트 자신이므로 상대는 하나다.

전용 서버는 플레이어가 아니다. 상대가 둘이고, 각자 독립적인 입장 상태와 명령 시퀀스를 갖는다.

<!-- snippet: include/net/udp_server.hpp#UdpEndpoint -->
```cpp
struct UdpEndpoint {
    sockaddr_storage address{};
#ifdef _WIN32
    int length = 0;
#else
    socklen_t length = 0;
#endif

    std::string Key() const {
        if (address.ss_family != AF_INET) return {};
        const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(&address);
        char text[INET_ADDRSTRLEN]{};
        if (!inet_ntop(AF_INET, &ipv4->sin_addr, text, sizeof(text))) return {};
        return std::string(text) + ":" + std::to_string(ntohs(ipv4->sin_port));
    }
};
```

`Key()` 가 `"127.0.0.1:52134"` 같은 문자열을 만든다. `sockaddr_storage` 를 그대로 맵 키로 쓰지 않는 이유는 셋이다 — 구조체에 패딩이 있어 비교가 불안정하고, 해시 함수를 직접 써야 하며, 무엇보다 **로그에 찍을 수 있는 형태가 아니다**. 접속 문제를 진단할 때 끝점을 눈으로 읽을 수 있다는 것이 문자열 키의 실질적 값이다.

IPv4 가 아니면 빈 문자열을 돌려준다. 서버는 그 빈 문자열을 거부 사유로 쓴다 — **키가 없는 데이터그램은 세션을 가질 수 없다.**

세션은 이렇게 생겼다.

<!-- snippet: include/game/defense_server.hpp#ClientSession -->
```cpp
struct ClientSession {
    net::UdpEndpoint     endpoint;
    live::GameAdmission  admission;
    // 클라이언트당 신뢰·순서 수신. 클라이언트가 1번부터 센다.
    net::ReliableOrderedReceiver<wire::Command> commands{1};
    // ...
    Clock::time_point    lastSeen{};
};
```

**신뢰 수신 채널이 클라이언트마다 하나씩** 있다. 이것이 없으면 두 클라이언트의 시퀀스가 섞인다 — 둘 다 1부터 세므로, 하나의 채널에 넣으면 두 번째 클라이언트의 1번이 첫 번째의 중복으로 버려진다.

`admission` 을 세션이 들고 있는 이유는 재접속 처리다. 같은 끝점에서 Hello 가 다시 오면 **토큰 id 가 같은지** 확인한다. 다르면 다른 사람이 그 끝점을 물려받았다는 뜻이므로 거부한다.

## 3. 서비스 사이클: 수신 → 시뮬 → 방송

<!-- snippet: include/game/defense_server.hpp#Step -->
```cpp
void Step(Clock::time_point now) {
    net::UdpDatagram datagram;
    while (socket_.Receive(datagram)) HandleDatagram(datagram, now);

    if (!simulationStarted_ && clients_.size() == kServerMaximumPlayers) {
        simulationStarted_ = true;
        nextTick_ = now;
        std::printf("[match] start id=%s capacity=%zu\n", activeMatch_.c_str(),
                    matches_.Capacity(activeMatch_));
    }

    if (simulationStarted_ && now >= nextTick_) {
        int catchUp = 0;
        while (now >= nextTick_ && catchUp < 5) {
            if (!world_.Won() && !world_.Lost()) world_.Step(kServerFixedDt);
            // ...
            ++tick_;
            // ...
            ++catchUp;
            nextTick_ += std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<double>(kServerFixedDt));
        }
        if (catchUp == 5 && now >= nextTick_) nextTick_ = now;
        if (tick_ % 3 == 0) BroadcastSnapshot();
    }
}
```

클라이언트 쪽 프레임 루프와 **같은 구조**다 — 펌프가 먼저, 고정 스텝 시뮬, 3틱마다 방송, 보충 상한 5틱, 초과분 폐기.

한 가지가 다르다. 클라이언트는 누적기(`accumulator_`)를 쓰고 서버는 **목표 시각**(`nextTick_`)을 쓴다. 결과는 같지만 표현이 다르다. 서버는 프레임이라는 개념이 없어 "이번 프레임이 몇 초였는가"를 물을 대상이 없기 때문이다. 대신 "다음 틱이 언제인가"를 절대 시각으로 들고, 현재가 그 시각을 지났으면 돈다.

`while (socket_.Receive(datagram))` 로 소켓을 완전히 비우는 것도 같다. 한 번에 하나만 읽으면 수신 버퍼가 쌓이고 그 지연이 누적된다.

<!-- snippet: include/game/defense_server.hpp#HandleDatagram -->
```cpp
void HandleDatagram(const net::UdpDatagram& datagram, Clock::time_point now) {
    const std::string endpointKey = datagram.sender.Key();
    if (endpointKey.empty()) {
        ++rejectedPackets_;
        return;
    }

    // ...
    std::string token;
    net::PacketHeader framing;
    wire::AdmissionHelloDetail detail;
    if (wire::ReadAdmissionHello(datagram.bytes, token, &framing, &detail)) {
        AdmitClient(datagram, endpointKey, token, now, framing, detail);
        return;
    }

    auto client = clients_.find(endpointKey);
    if (client == clients_.end()) {
        ++rejectedPackets_;
        return;
    }
    client->second.lastSeen = now;
    // ...
    wire::Command command;
    if (!wire::ReadCommand(datagram.bytes, command, &framing) ||
        !client->second.commands.Accept(command.sequence, command)) {
        ++rejectedPackets_;
        return;
    }
    // ...
    while (client->second.commands.Poll(command)) {
        // ...
        if (wire::ApplyCommand(world_, command)) ++acceptedCommands_;
    }
}
```

**세션이 없는 끝점의 명령은 읽지도 않는다.** 입장하지 않은 발신자의 바이트가 디코더에 닿을 이유가 없다.

`rejectedPackets_` 를 세는 자리가 넷이다 — 키 없음, 세션 없음, 디코드 실패, 시퀀스 거부. 하나의 카운터로 합친 것은 이 값이 진단용 총계이기 때문이다. 정상 운영에서 이 수가 계속 오르면 누군가 쏘고 있다는 뜻이고, 그 사실 자체가 신호다.

`ApplyCommand` 가 참을 돌려줄 때만 `acceptedCommands_` 를 올린다. 순서대로 전달된 명령이라도 월드가 거부할 수 있다(골드 부족·건설 불가 칸). 두 수를 나누면 "도착했지만 적용되지 않은 명령"의 비율이 나온다.

## 4. 둘이 모이기 전에는 시간이 흐르지 않는다

```cpp
if (!simulationStarted_ && clients_.size() == kServerMaximumPlayers) {
    simulationStarted_ = true;
    nextTick_ = now;
}
```

정원(2)이 다 차기 전에는 **틱이 하나도 돌지 않는다.**

대안은 서버가 켜지자마자 시뮬을 시작하는 것이었다. 기각한 이유는 협동 게임의 성질이다 — 먼저 들어온 플레이어가 웨이브 3까지 혼자 버티고 나서 두 번째 플레이어가 합류하면, 두 번째 플레이어는 이미 진 판에 들어오게 된다. 골드도 없고 타워도 남의 것이다.

`nextTick_ = now` 로 목표 시각을 재설정하는 것이 함께 있어야 한다. 없으면 `Init` 때 잡은 시각과 실제 시작 사이의 대기 시간이 전부 빚으로 남아, 시작 즉시 보충 상한 5틱이 몰아친다.

`activeMatch_` 는 첫 입장자의 `MatchId` 로 잠긴다. 그 뒤 다른 매치 id 를 들고 온 토큰은 거부된다. **한 프로세스가 한 판만 돌린다**는 규칙이고, 이것이 없으면 두 개의 서로 다른 매치에 배정된 플레이어들이 같은 월드에서 만난다.

<!-- snippet: include/game/defense_server.hpp#BroadcastSnapshot -->
```cpp
void BroadcastSnapshot() {
    for (auto& [key, client] : clients_) {
        (void)key;
        const uint16_t seq = client.seq.Next();
        const net::PacketHeader header = client.window.Header(seq);
        const net::Bytes snapshot =
            options_.switches.snapshotMode == SnapshotMode::Delta
                ? wire::WriteSnapshotPacket(world_, tick_, client.commands.LastDelivered(),
                                            header, client.baselines, seq, nullptr,
                                            avatarFrame_, AvatarChecksum32())
                : wire::WriteSnapshot(world_, tick_, client.commands.LastDelivered(),
                                      nullptr, header, avatarFrame_, AvatarChecksum32());
        if (!snapshot.empty()) {
            socket_.SendTo(client.endpoint, snapshot);
            // ...
        }
    }
}
```

**스냅샷을 클라이언트마다 다시 만든다.** 게임 상태는 같지만 `acknowledgedCommand` 가 다르기 때문이다 — 각 클라이언트는 자기가 보낸 명령이 어디까지 반영됐는지를 알아야 하고, 그 값은 그 클라이언트의 수신 채널에서만 나온다.

인코딩을 한 번만 하고 ACK 필드만 덮어쓰는 최적화도 가능하다. 그러지 않은 이유는 그 최적화가 **바이트 오프셋을 안다는 가정**을 코드에 새기기 때문이다. 정원이 2이므로 절약할 것은 인코딩 한 번뿐이고, 그 대가로 포맷 변경이 조용히 깨지는 자리를 만들 이유가 없다.

## 5. 입장 토큰: 다섯 가지 거부

토큰 형식은 `base64url(json).base64url(hmac_sha256)` 이다. 제어 평면(매치메이커)이 배포 비밀로 서명하고, 게임 서버가 오프라인으로 검증한다.

```json
{"Kind":"game","PlayerId":"alice","MatchId":"process-match",
 "ServerId":"process-server","TokenId":"token-a","ExpiresAt":4102444800}
```

<!-- snippet: include/live/admission_token.hpp#VerifyGameAdmission -->
```cpp
inline bool VerifyGameAdmission(
    std::string_view token,
    std::span<const uint8_t> secret,
    std::string_view expectedServerId,
    int64_t nowUnixSeconds,
    GameAdmission& admission) {
    const size_t separator = token.find('.');
    if (separator == 0 || separator == std::string_view::npos ||
        separator != token.rfind('.')) return false;
    const std::string_view body = token.substr(0, separator);
    const std::string_view signatureText = token.substr(separator + 1);
    const auto signature = detail::DecodeBase64(signatureText, true);
    const auto jsonBytes = detail::DecodeBase64(body, true);
    if (!signature || !jsonBytes || signature->size() != 32) return false;
    const auto expected = detail::Hmac(
        secret,
        std::span(reinterpret_cast<const uint8_t*>(body.data()), body.size()));
    if (!detail::ConstantTimeEqual(*signature, expected)) return false;

    const std::string json(jsonBytes->begin(), jsonBytes->end());
    const auto kind = detail::JsonString(json, "Kind");
    // ... PlayerId · MatchId · ServerId · TokenId · ExpiresAt 을 같은 방식으로 읽는다
    if (!kind || *kind != "game" || !playerId || playerId->empty() ||
        !matchId || matchId->empty() || !serverId || *serverId != expectedServerId ||
        !tokenId || tokenId->empty() || !expiresAt || *expiresAt <= nowUnixSeconds) {
        return false;
    }
    admission = {*playerId, *matchId, *serverId, *tokenId, *expiresAt};
    return true;
}
```

**서명을 먼저 검증하고 그다음 내용을 읽는다.** 순서가 반대면 서명되지 않은 JSON 을 파싱하게 되고, 파서가 공격 표면이 된다.

`separator != token.rfind('.')` 는 점이 정확히 하나여야 한다는 검사다. 점이 둘이면 어디까지가 서명 대상인지가 모호해지고, 그 모호함은 대개 우회로가 된다.

`*serverId != expectedServerId` 가 **audience 검사**다. 이것이 없으면 한 서버용으로 발급된 토큰이 같은 비밀을 쓰는 다른 모든 서버에서 통한다. 서명은 "제어 평면이 발급했다"만 증명하지 "이 서버로 가라"를 증명하지 않는다.

`*expiresAt <= nowUnixSeconds` 가 **만료 검사**다. 이것이 없으면 한 번 유출된 토큰이 영구 통행권이 된다.

나머지 두 거부는 서버 쪽에 있다.

<!-- snippet: include/game/defense_server.hpp#AdmitClient -->
```cpp
void AdmitClient(const net::UdpDatagram& datagram, const std::string& endpointKey,
                 const std::string& token, Clock::time_point now,
                 const net::PacketHeader& framing,
                 const wire::AdmissionHelloDetail& detail) {
    live::GameAdmission admission;
    if (!live::VerifyGameAdmission(token, secret_, options_.serverId, UnixNow(), admission)) {
        ++rejectedPackets_;
        return;
    }
    auto existingClient = clients_.find(endpointKey);
    if (existingClient != clients_.end()) {
        if (existingClient->second.admission.tokenId != admission.tokenId) {
            ++rejectedPackets_;
        } else {
            existingClient->second.endpoint = datagram.sender;
            existingClient->second.lastSeen = now;
            // ...
        }
        return;
    }
    const auto tokenOwner = tokenEndpoints_.find(admission.tokenId);
    const auto playerOwner = playerEndpoints_.find(admission.playerId);
    const bool tokenConflict = tokenOwner != tokenEndpoints_.end() &&
                               tokenOwner->second != endpointKey;
    const bool playerConflict = playerOwner != playerEndpoints_.end() &&
                                playerOwner->second != endpointKey;
    // ...
    if (!matches_.Allows(admission.matchId, wire::PlayerKey(admission.playerId))) {
        ++rejectedPackets_;
        return;
    }
    // ...
    const size_t capacity = matches_.Capacity(admission.matchId);
    const bool full = clients_.size() >= capacity ||
                      clients_.size() >= kServerMaximumPlayers;
    if (tokenConflict || playerConflict || full) {
        ++rejectedPackets_;
        return;
    }
    activeMatch_ = admission.matchId;
    tokenEndpoints_[admission.tokenId] = endpointKey;
    playerEndpoints_[admission.playerId] = endpointKey;
    ClientSession& session = clients_[endpointKey];
    session.endpoint = datagram.sender;
    session.admission = std::move(admission);
    session.lastSeen = now;
    // ...
}
```

거부 목록을 정리하면 이렇다.

| # | 거부 | 막는 것 |
|---|---|---|
| 1 | HMAC 서명 불일치 | 위조 토큰 |
| 2 | `ServerId` 불일치 | 다른 서버용 토큰의 재사용 |
| 3 | 만료 | 유출된 옛 토큰 |
| 4 | 토큰·플레이어의 다중 끝점 결합 | 토큰 하나로 여러 연결을 여는 것 |
| 5 | 정원 초과 · 매치 id 불일치 | 남의 판에 끼어들기 |

네 번째가 이 함수에만 있는 이유는 그것이 **상태에 대한 질문**이기 때문이다. 서명·audience·만료는 토큰만 보면 답할 수 있지만, "이 토큰이 이미 다른 끝점에서 쓰이고 있는가"는 서버의 접속 목록을 봐야 안다.

`tokenEndpoints_` 와 `playerEndpoints_` 를 **따로** 두는 것도 의도다. 한 플레이어가 서로 다른 두 토큰을 발급받는 경우가 있을 수 있고(재시도), 그때도 플레이어 단위 중복은 막아야 한다.

**같은 끝점의 재-Hello 는 갱신으로 처리한다.** 클라이언트는 500ms 마다 Hello 를 보내므로(5편) 정상 운영에서 이 경로가 계속 밟힌다. 그때마다 새 세션을 만들면 명령 시퀀스가 초기화된다.

## 6. 상수 시간 비교와 오프라인 검증

<!-- snippet: include/live/admission_token.hpp#ConstantTimeEqual -->
```cpp
inline bool ConstantTimeEqual(
    std::span<const uint8_t> left, std::span<const uint8_t> right) {
    if (left.size() != right.size()) return false;
    uint8_t difference = 0;
    for (size_t i = 0; i < left.size(); ++i) difference |= left[i] ^ right[i];
    return difference == 0;
}
```

`memcmp` 는 첫 불일치 바이트에서 돌아온다. 그 소요 시간이 **일치한 접두사 길이에 비례**하므로, 공격자는 서명의 첫 바이트를 256번 시도해 가장 오래 걸린 값을 고르고, 그다음 바이트로 넘어가는 식으로 32바이트를 한 바이트씩 복원할 수 있다.

XOR 누적은 항상 전체를 순회한다. 길이가 다를 때만 조기 반환하는데, **길이는 비밀이 아니다**(서명은 항상 32바이트로 고정이고 그 사실이 코드에 적혀 있다).

검증이 **오프라인**이라는 점도 설계다. 게임 서버는 토큰을 확인하려고 어떤 서비스에도 묻지 않는다. 제어 평면과 배포 비밀을 공유할 뿐이다. 그래서 제어 평면이 죽어도 이미 발급된 토큰으로는 계속 입장할 수 있고, 게임 서버의 응답 시간에 네트워크 왕복이 들어가지 않는다. 대가는 **취소가 불가능**하다는 것이고, 그것을 만료 시각으로 제한한다.

비밀이 없을 때의 동작도 결정 사항이다.

```cpp
if (options_.secretBase64.empty()) {
    secret_ = live::LocalDevelopmentSecret();
    std::fprintf(stderr,
        "[security] using the local development token secret; do not expose this server\n");
}
```

두 대안이 있었다. 비밀 없이 뜨기를 거부하는 것과, 개발용 고정 비밀로 뜨되 경고하는 것. 후자를 고른 이유는 로컬 협동 테스트가 매번 비밀을 만들어 환경 변수에 넣게 하면 그 절차가 곧 생략되기 때문이다. 대신 **경고를 표준 오류로 매 기동마다** 찍는다. 실제 배포에서는 `LIVE_SERVICE_TOKEN_SECRET` 환경 변수나 `--secret-base64` 인자로 준다.

`DecodeSecret` 이 32바이트 미만을 거부하는 것은 HMAC-SHA256 의 출력이 32바이트이므로 그보다 짧은 키는 보안 여유를 갖지 못하기 때문이다.

## 7. 드레인 종료

<!-- snippet: include/game/defense_server.hpp#ShouldContinue -->
```cpp
// 종료 조건: 신호 또는 --run-seconds 상한.
bool ShouldContinue(Clock::time_point now) const {
    if (platform::ShutdownRequested()) return false;
    if (options_.runSeconds > 0 &&
        now - started_ >= std::chrono::seconds(options_.runSeconds)) return false;
    return true;
}
```

<!-- snippet: include/game/defense_server.hpp#RunDefenseServer -->
```cpp
inline int RunDefenseServer(const DefenseServerOptions& options) {
    // ...
    platform::EnableUtf8Console();
    DefenseServer server;
    if (!server.Init(options)) return 1;
    platform::InstallShutdownSignals();

    while (true) {
        const DefenseServer::Clock::time_point now = DefenseServer::Clock::now();
        if (!server.ShouldContinue(now)) break;
        server.Step(now);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    server.PrintSummary();
    return 0;
}
```

신호 처리는 `platform/` 이 소유한다.

<!-- snippet: include/platform/process.hpp -->
```cpp
#pragma once

namespace mega {
namespace platform {

// SIGINT/SIGTERM 을 잡아 종료 플래그를 세우도록 설치한다. 여러 번 불러도 안전.
void InstallShutdownSignals();

// 종료 요청이 있었는가. 신호 처리기와 루프 사이의 유일한 통신 수단이다.
bool ShutdownRequested();

// 플래그를 되돌린다(테스트·재시작용).
void ClearShutdownRequest();

// 신호 없이 프로그램적으로 종료를 요청한다(런타임 상한 도달 등).
void RequestShutdown();

}  // namespace platform
}  // namespace mega
```

신호 처리기가 만질 수 있는 것은 async-signal-safe 한 것뿐이다 — 실질적으로 원자 플래그 하나다. 그 제약을 아는 코드가 한 곳에 있어야 하고, 서버는 **"종료 요청이 왔는가"만 물으면 된다.**

이 형태가 사 주는 것이 드레인 종료다. 신호가 오면 루프가 다음 반복에서 빠져나가고, `PrintSummary()` 가 요약을 찍고, 소멸자가 소켓을 닫는다. `exit()` 를 신호 처리기에서 부르면 그 셋 중 어느 것도 일어나지 않는다.

`--run-seconds` 는 테스트를 위한 것이다. 신호를 보내는 방식은 플랫폼마다 다르고(Windows 에는 SIGTERM 이 없다) 그 차이를 테스트가 흡수하기보다, 서버 스스로 끝나게 하는 편이 단순하다.

`sleep_for(1ms)` 는 바쁜 대기를 막는다. 시뮬 간격이 16.7ms 이므로 1ms 폴링은 충분히 촘촘하고, 그 사이 CPU 를 100% 쓰지 않는다.

## 8. WAL: COMMIT 마커 뒤에 fsync 한다

이제 두 번째 주제다. 한 판이 끝나면 결과가 디스크로 간다.

가장 단순한 형태는 프로필 구조체를 파일에 덮어쓰는 것이다. 그 형태의 실패 모드가 이 절의 출발점이다 — **덮어쓰는 도중에 프로세스가 죽으면 앞부분은 새 값, 뒷부분은 옛 값인 파일이 남는다.** 그 파일은 어느 시점에도 실재한 적이 없는 상태이고, 다음 기동에서 읽히면 조용히 잘못된 진행도가 된다.

해법은 append-only 로그다.

<!-- snippet: include/db/kv_store.hpp#CommitBuffer -->
```cpp
bool CommitBuffer() {
    if (!wal_) return false;
    for (const Op& op : txnBuf_) {
        uint8_t t = op.type;
        std::fwrite(&t, 1, 1, wal_);
        PutU32(wal_, (uint32_t)op.key.size());
        std::fwrite(op.key.data(), 1, op.key.size(), wal_);
        if (op.type == kPut) {
            PutU32(wal_, (uint32_t)op.val.size());
            std::fwrite(op.val.data(), 1, op.val.size(), wal_);
        }
    }
    uint8_t commit = kCommit;
    std::fwrite(&commit, 1, 1, wal_);
    std::fflush(wal_);
    Fsync(wal_);                                  // 내구성 보장 지점
    for (const Op& op : txnBuf_) Apply(op);
    txnBuf_.clear();
    return true;
}
```

레코드 형식은 이렇다.

```text
PUT     0x01  keyLen:u32  key  valLen:u32  val
DELETE  0x02  keyLen:u32  key
COMMIT  0x03
```

**순서가 전부다.**

1. 트랜잭션의 모든 레코드를 쓴다.
2. COMMIT 마커 한 바이트를 쓴다.
3. `fflush` + `fsync` — 여기까지가 디스크에 도달했음이 보장되는 지점.
4. **그 다음에** 인메모리 맵에 반영한다.

3번과 4번 사이에서 죽으면 로그는 완전하고 맵만 갱신되지 않았는데, 프로세스가 죽었으므로 맵은 어차피 사라진다. 다음 기동이 로그를 재생하면 같은 상태가 된다.

1번과 2번 사이에서 죽으면 COMMIT 마커가 없는 레코드들이 남는다. **그것들은 재생에서 통째로 버려진다** — 원자성이다.

`fflush` 와 `fsync` 를 둘 다 부르는 것이 필요하다. `fflush` 는 C 라이브러리 버퍼를 OS 로 넘길 뿐이고, OS 의 페이지 캐시에서 물리 매체까지는 `fsync`(Windows 에서는 `_commit`)가 밀어 준다. 앞의 것만 부르면 프로세스 크래시는 견디지만 전원 차단은 견디지 못한다.

## 9. 재기동이 원자성을 넘기게 하는 한 줄

재생은 이렇게 생겼다.

<!-- snippet: include/db/kv_store.hpp#Replay -->
```cpp
void Replay(std::FILE* rf, long& committedEnd, long& total) {
    committedEnd = 0;
    total = 0;
    // ... 파일 크기를 재고 전체를 buf 로 읽는다. 읽지 못하면 total 을 0 으로 남긴다.
    size_t pos = 0;
    std::vector<Op> pending;
    // ...
    while (pos < buf.size()) {
        uint8_t type = buf[pos++];
        if (type == kCommit) {
            for (const Op& op : pending) Apply(op);   // 커밋 → 적용
            pending.clear();
            committedEnd = (long)pos;                 // 여기까지는 영속 확정분
            continue;
        }
        if (type != kPut && type != kDel) break;      // 손상 → 중단
        if (pos + 4 > buf.size()) break;              // 토막난 레코드
        uint32_t kl = u32(pos); pos += 4;
        if (pos + kl > buf.size()) break;
        std::string key((const char*)&buf[pos], kl); pos += kl;
        std::string val;
        if (type == kPut) {
            if (pos + 4 > buf.size()) break;
            uint32_t vl = u32(pos); pos += 4;
            if (pos + vl > buf.size()) break;
            val.assign((const char*)&buf[pos], vl); pos += vl;
        }
        pending.push_back({(RecType)type, key, val});
    }
}
```

모든 읽기가 범위를 확인하고, 벗어나면 그 자리에서 멈춘다. 토막난 마지막 레코드가 정상적인 종료 조건이다.

그런데 재생만으로는 부족하다. 여기가 이 절의 요점이다.

<!-- snippet: include/db/kv_store.hpp#Open -->
```cpp
bool Open(const char* path) {
    Close();
    path_ = path;
    long committedEnd = 0, total = 0;
    if (std::FILE* rf = std::fopen(path, "rb")) {   // 기존 로그 재생
        Replay(rf, committedEnd, total);
        std::fclose(rf);
    }
    // 크래시가 남긴 미커밋 꼬리를 파일에 그대로 두면 안 된다. 로그는 append-only 고
    // COMMIT 마커는 "그 앞에 쌓인 pending 전부"에 걸리므로, 재기동 뒤 첫 트랜잭션이
    // 적는 마커 하나가 그 꼬리까지 커밋해 버린다 — 원자성이 재기동을 못 넘긴다.
    // 재생이 인정한 마지막 커밋 지점까지로 잘라 그 경로를 없앤다.
    if (total > committedEnd) Truncate(path, committedEnd);
    wal_ = std::fopen(path, "ab");                  // 이후 추가 기록용
    return wal_ != nullptr;
}
```

`if (total > committedEnd) Truncate(path, committedEnd);` 한 줄이 원자성을 재기동 너머로 확장한다.

없으면 이렇게 된다. 크래시가 미커밋 꼬리 `[PUT a=1][PUT b=2]` 를 남긴다. 재생은 그것을 무시하고 맵을 올바르게 복원한다 — **여기까지는 맞다.** 그다음 프로세스가 새 트랜잭션 `[PUT c=3][COMMIT]` 를 append 한다. 파일은 이제 `[PUT a=1][PUT b=2][PUT c=3][COMMIT]` 이고, **다음 기동의 재생은 그 COMMIT 을 셋 전부에 적용한다.** 버려졌어야 할 a 와 b 가 부활한다.

잘라 내면 그 경로가 없어진다. 잘라 내지 못해도(파일 잠김 등) 재생 결과는 바뀌지 않으므로 실패는 조용히 넘긴다 — 다음 `Open` 이 같은 지점을 다시 시도한다.

`total` 이 0으로 남는 경우도 처리돼 있다. 파일을 읽지 못했으면 `Replay` 가 `total` 을 0으로 남기고, 그러면 `total > committedEnd` 가 거짓이라 **내용을 못 본 파일을 자르지 않는다.**

## 10. 한 트랜잭션에 프로필과 매치 기록을 함께

<!-- snippet: include/game/defense_progression.hpp#Record -->
```cpp
bool Record(uint32_t wave, uint32_t score, bool cleared, uint64_t endedAt,
            Profile& updated, const live::Wallet* wallet = nullptr) {
    if (!open_) return false;
    Profile profile;
    Load(profile);
    ++profile.totalMatches;
    if (cleared) ++profile.clears;
    profile.bestWave = std::max(profile.bestWave, wave);
    profile.bestScore = std::max(profile.bestScore, score);

    db::MatchResult match;
    match.id = profile.totalMatches;
    match.hostAccount = 1;
    match.waveReached = wave;
    match.score = score;
    match.cleared = cleared ? 1 : 0;
    match.endedAt = endedAt;

    store_.Begin();
    db::Repository<Profile> profiles(store_, "profile:");
    db::Repository<db::MatchResult> matches(store_, "match:");
    profiles.Save(1, profile);
    matches.Save(match.id, match);
    if (wallet) {
        // ...
        live::Wallet copy = *wallet;
        db::Repository<live::Wallet> wallets(store_, "wallet:");
        wallets.Save(1, copy);
    }
    if (!store_.Commit()) return false;
    updated = profile;
    return true;
}
```

`Begin()` 과 `Commit()` 사이에 **두 레포지토리에 대한 쓰기**가 들어 있다. 하나의 COMMIT 마커가 둘 다에 걸린다.

이 원자성이 필요한 이유는 `match.id = profile.totalMatches` 라는 관계 때문이다. 프로필만 저장되고 매치 기록이 빠지면 다음 판의 매치 id 가 기존 것을 덮어쓴다. 매치만 저장되고 프로필이 빠지면 매치 id 가 중복된다. 둘 중 어느 쪽도 파일 손상으로 보이지 않고 **그냥 이상한 기록**으로 보인다.

`endedAt` 이 인자로 들어오는 것도 결정이다. 함수 안에서 시스템 시각을 읽으면 같은 입력이 실행마다 다른 레코드를 만든다. 호출자는 여기에 호스트 틱을 넣는다 — 시뮬 시간이므로 결정적이다.

`Repository` 는 레코드를 이진 아카이브로 바이트화하고 `"profile:1"` 같은 키를 만든다. 접두사가 있어서 한 스토어에 여러 종류가 공존한다.

## 11. 저장 파일은 실행 파일 옆이 아니다

<!-- snippet: include/platform/paths.cpp#UserDataDirectory -->
```cpp
std::string UserDataDirectory(const std::string& application) {
    if (application.empty()) return std::string();
    std::filesystem::path directory;
    std::string root;
#if defined(_WIN32)
    if (EnvironmentVariable("LOCALAPPDATA", root)) directory = root;
    else directory = ExecutableDirectory();
    directory /= application;
#else
    if (EnvironmentVariable("HOME", root)) directory = root;
    else directory = ".";
    directory /= ".local/share";
    directory /= application;
#endif
    const std::string result = directory.string();
    if (!EnsureDirectory(result)) return std::string();
    return result;
}
```

실행 파일 옆에 쓰면 세 가지가 깨진다. 설치 디렉터리는 대개 **쓰기 권한이 없고**, 여러 사용자가 같은 설치를 공유하면 **진행도가 섞이고**, 재설치나 업데이트가 저장 파일을 **지운다**.

플랫폼 관례를 따르는 것이 답이다 — Windows 는 `%LOCALAPPDATA%`, 그 외는 `$HOME/.local/share`.

`ExecutableDirectory()` 로 되돌아가는 경로가 있는 것은 환경 변수가 없는 환경(일부 CI, 최소 컨테이너) 때문이다. 그때도 프로그램은 돌아야 한다.

앱은 이렇게 쓴다.

```cpp
if (options_.mode != RunMode::Client) {
    std::string path = options_.profilePath;
    if (path.empty()) {
        const std::string directory = platform::UserDataDirectory("MegaDefense");
        path = (directory.empty() ? executableDirectory : directory) + "/profile.wal";
    }
    if (progression_.Open(path)) progression_.Load(profile_);
}
```

**클라이언트는 저장소를 열지 않는다.** 권위 진행도를 갖지 않기 때문이다. 클라이언트가 자기 로컬 월드(스텝된 적 없는)를 기준으로 기록하면 전부 0짜리 매치가 쌓인다.

셀프테스트는 데이터 경로만 다르다.

```cpp
if (effective.selftest && effective.profilePath.empty()) {
    effective.profilePath = platform::ExecutableDirectory() + "/selftest_profile.wal";
}
```

**코드 경로가 아니라 데이터 경로만** 바꾼다. 셀프테스트도 진짜 `ProgressionStore` 에 진짜 파일로 커밋하되, 플레이어의 프로필을 덮지 않는다. 조건 분기로 저장을 건너뛰면 그 경로가 검증되지 않는다.

**이 기본 경로는 공용이고 누적한다.** 실행 파일이 하나이므로 모든 셀프테스트가 같은 파일을 데우고, 그 파일은 지워지지 않는다. 여기 커밋되는 것에 지갑이 섞이면서(28편) 그 누적이 다음 판의 시작 골드를 바꾸게 됐고, 같은 명령이 몇 번째 실행이냐에 따라 다른 결과를 내기 시작했다. 그래서 이 기본값은 **아무도 이름을 대지 않았을 때의 마지막 수단**이고, `ctest` 의 셀프테스트는 여기 기대지 않는다 — 검사마다 `--profile` 로 자기 파일을 잡은 뒤 `--fresh-profile` 로 비우고 출발한다. 그 인자와 그것이 고친 결함은 28편 13절에 있다.

## 12. 실행과 관측

전용 서버와 저장소에는 네 개의 검사가 붙어 있다.

```powershell
cmake --build build --config Release -j
ctest --test-dir build -C Release -R "dedicated|admission|db_kv|progression"
```

```text
    Start  2: test_admission
1/4 Test  #2: test_admission ...................   Passed    0.03 sec
    Start  8: test_db_kv
2/4 Test  #8: test_db_kv .......................   Passed    0.11 sec
    Start 10: test_dedicated_process
3/4 Test #10: test_dedicated_process ...........   Passed    3.10 sec
    Start 16: test_defense_progression
4/4 Test #16: test_defense_progression .........   Passed    0.07 sec

100% tests passed out of 4

Total Test time (real) =   3.32 sec
```

```powershell
.\build\bin\Release\test_admission.exe
.\build\bin\Release\test_db_kv.exe
.\build\bin\Release\test_dedicated_process.exe
.\build\bin\Release\test_defense_progression.exe
```

```text
[test] admission
[test] admission: 8 checks, 0 failures
[test] db_kv
[test] db_kv: 73 checks, 0 failures
[test] dedicated_process
[test] dedicated_process: 7 checks, 0 failures
[test] defense_progression
[test] defense_progression: 15 checks, 0 failures
```

`test_dedicated_process` 가 3.1초 걸리는 것은 **진짜 서버 프로세스를 띄우기 때문**이다. 이 테스트는 `mega_defense_server --port N --server-id process-server --run-seconds 3` 을 spawn 하고, 독립적인 UDP 끝점 **둘**을 열어 각각 `alice`/`token-a`, `bob`/`token-b` 토큰으로 입장한다. 두 토큰의 `MatchId` 는 같고(`process-match`), `ServerId` 는 서버가 받은 것과 같다.

그다음 첫 번째 끝점이 배치 명령(시퀀스 1)을 보내고, 두 조건이 성립할 때까지 기다린다 — 첫 번째 끝점이 `acknowledgedCommand == 1 && towers.size() == 1` 인 스냅샷을 받고, 두 번째 끝점도 `towers.size() == 1` 인 스냅샷을 받는 것. **후자가 방송을 증명한다.** 명령을 보내지 않은 클라이언트도 상태를 받는다는 뜻이다. 마지막으로 서버 종료 코드가 0인지 본다.

서버를 직접 띄우면 기동과 종료가 보인다.

```powershell
.\build\bin\Release\mega_defense_server.exe --port 27099 --run-seconds 2
```

```text
[security] using the local development token secret; do not expose this server
[server] id=mega-local-1 udp=0.0.0.0:27099 capacity=2
[content] map=external waves=external balance=external
[server] stop tick=0 players=0 commands=0 rejected=0 result=incomplete
```

`tick=0` 이 4절의 규칙이다. 2초 동안 돌았지만 **아무도 접속하지 않아 틱이 하나도 진행되지 않았다.** `result=incomplete` 는 이기지도 지지도 않았다는 뜻이다. 그리고 개발용 비밀 경고가 매 기동마다 표준 오류로 나간다.

`[content] map=external waves=external balance=external` 은 서버가 클라이언트와 **같은 콘텐츠 로더**로 같은 파일을 읽었다는 뜻이다. 두 프로세스가 다른 게임을 돌리면 스냅샷의 좌표가 서로 다른 맵을 가리킨다.

`test_db_kv` 의 73개 단언은 저장소의 실패 모드를 직접 만든다. 커밋되지 않은 꼬리를 손으로 파일에 붙이고 다시 열어 그것이 적용되지 않는지, 그리고 **그 뒤에 새 트랜잭션을 커밋해도 부활하지 않는지**를 본다. 후자가 9절의 `Truncate` 한 줄을 검증하는 자리다. 커밋된 것과 커밋되지 않은 것이 섞인 로그, 토막난 레코드, 롤백, 많은 키도 함께 다룬다.

정산의 결과는 파일에서 직접 읽을 수 있다. 솔로 셀프테스트가 남긴 WAL 의 첫 트랜잭션을 바이트로 펼치면 이렇다.

```text
01 09 00 00 00 70 72 6f 66 69 6c 65 3a 31 14 00 00 00
01 00 00 00 01 00 00 00 01 00 00 00 06 00 00 00 ee 05 00 00
01 07 00 00 00 6d 61 74 63 68 3a 31 1d 00 00 00
01 00 00 00 01 00 00 00 01 00 00 00 06 00 00 00 ee 05 00 00
01 61 05 00 00 00 00 00 00
03
```

한 줄씩 읽으면 이렇다.

```text
01              PUT
09 00 00 00     키 9바이트
"profile:1"
14 00 00 00     값 20바이트 = schemaVersion·totalMatches·clears·bestWave·bestScore
                1, 1, 1, 6, 0x05ee(1518)

01              PUT
07 00 00 00     키 7바이트
"match:1"
1d 00 00 00     값 29바이트 = schemaVersion 1, id 1, hostAccount 1,
                waveReached 6, score 1518, cleared 1, endedAt 0x0561(1377)

03              COMMIT
```

두 레코드가 **하나의 COMMIT 뒤에** 있다. 10절의 원자성이 파일에서 그대로 보인다.

숫자들도 앞 편들과 맞물린다. `bestWave = 6` 과 `endedAt = 1377` 은 셀프테스트 출력의 `wave=6 ticks=1377` 이다. `score = 1518` 은 `waveReached * 100 + gold + (cleared ? 500 : 0)` 이므로 남은 골드가 418이었다는 뜻이다. `endedAt` 이 시스템 시각이 아니라 시뮬 틱이라 **같은 실행을 반복해도 같은 값**이 나온다.

파일은 append-only 이므로 셀프테스트를 여러 번 돌리면 같은 모양의 트랜잭션이 뒤에 계속 쌓인다. 맵의 최종 상태는 마지막 것이 결정한다.

---

## 결론

이 단계에서 세운 것.

1. **전용 서버 실행 파일** — 게임 규칙과 와이어 포맷은 클라이언트와 같은 코드, 다른 것은 소켓 모델·입장 정책·종료 조건 셋뿐이다. 진입점 18줄.
2. **끝점별 세션** — 문자열 키(`ip:port`)로 맵을 만들고, 신뢰 수신 채널을 클라이언트마다 하나씩 둔다. 하나로 합치면 둘 다 1부터 세는 시퀀스가 섞인다.
3. **서비스 사이클** — 클라이언트 프레임 루프와 같은 구조지만 누적기 대신 목표 시각을 쓴다. 서버에는 프레임이라는 개념이 없기 때문이다.
4. **정원이 차기 전에는 틱이 없다** — 늦게 온 플레이어가 이미 진 판에 들어오지 않게 한다. 첫 입장자의 `MatchId` 가 프로세스를 잠근다.
5. **다섯 가지 거부** — 서명·audience·만료는 토큰만 보면 답할 수 있고, 토큰·플레이어의 다중 끝점 결합과 정원·매치 충돌은 서버 상태를 봐야 답할 수 있다.
6. **상수 시간 비교와 오프라인 검증** — `memcmp` 의 조기 반환이 서명을 한 바이트씩 복원할 길을 연다. 오프라인 검증의 대가는 취소 불가이고 그것을 만료로 제한한다.
7. **드레인 종료** — 신호 처리기는 원자 플래그 하나만 만지고, 루프가 다음 반복에서 빠져나와 요약을 찍고 소켓을 닫는다.
8. **WAL 의 순서** — 레코드 전부 → COMMIT 마커 → fsync → 인메모리 반영. 마커 전 크래시는 트랜잭션을 통째로 버린다.
9. **미커밋 꼬리 잘라내기** — 재생이 무시하는 것만으로는 부족하다. append-only 로그에서 다음 COMMIT 마커가 그 꼬리까지 커밋하기 때문이다. 한 줄이 원자성을 재기동 너머로 확장한다.
10. **한 트랜잭션, 두 레포지토리** — `match.id = profile.totalMatches` 라는 관계가 둘의 원자성을 요구한다. `endedAt` 은 시스템 시각이 아니라 시뮬 틱이다.
11. **사용자 데이터 디렉터리** — 설치 경로는 쓰기 권한도, 사용자 분리도, 업데이트 내성도 없다. 셀프테스트는 코드 경로가 아니라 데이터 경로만 바꾼다.

현재 상태의 한계. 여기까지 세운 것들은 전부 **사람의 규율에 의존한다.** 진입점이 얇아야 한다는 규칙도, `games/` 가 OS API 를 직접 부르면 안 된다는 규칙도, 헤더가 제품에서 실제로 쓰여야 한다는 규칙도 문서에 적혀 있을 뿐 무엇도 강제하지 않는다. 규칙은 사람이 어기고, 어긴 것을 아무도 알아채지 못한다. 그 규칙들을 빌드 실패로 바꾸는 장치는 아직 하나도 없다.
