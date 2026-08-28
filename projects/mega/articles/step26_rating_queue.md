# 26. 매치에 이름이 생긴다

## 들어가며

전용 서버가 매치 하나를 여는 방식은 이렇다.

```cpp
if (activeMatch_.empty()) activeMatch_ = admission.matchId;
```

**처음 도착한 입장 토큰이 말한 이름**이 그대로 이 서버가 여는 매치의 이름이 된다. 두 번째 토큰은 같은 이름을 말해야 하고, 다르면 거부된다.

이 코드가 틀린 것은 아니다. 서명·발급처·만료·서버 id·중복 결합·정원을 전부 검증하고 나서 남은 마지막 항목이 matchId 이고, 그 시점에 서버가 대조할 수 있는 것은 "먼저 온 쪽이 말한 것"뿐이었다. **매치를 만드는 주체가 없었기 때문이다.**

그 상태에서 "큐"라는 개념은 성립하지 않는다. 대기열도, 실력 기준도, 정원 배정도 전부 매치를 만드는 쪽의 일인데 그 쪽이 없다. 서명만 유효한 토큰 하나가 아무 이름의 매치나 열 수 있고, 서버는 그것이 정당한 이름인지 물을 방법이 없다.

이 편은 그 주체를 만든다.

| 헤더 | 하는 일 |
|---|---|
| `mp/matchmaking.hpp` | 큐 · 레이팅 창 · 매치 형성 · 결정적 MatchId · 형성 장부 |

도달점의 수치.

- 합성 **500명** 큐, 창 100, 2인 매치: **250개** 형성 · 잔여 **0명** · 최대 레이팅 폭 **100** · 최대 대기 998.
- 창을 0으로 좁히면 매치 **247개** · 잔여 **6명** · 폭 **0** — 품질의 두 축이 서로 대가라는 것이 수로 나온다.
- 같은 사람들이 **순서만 다르게** 큐에 들어와도 형성된 매치 99개가 **99개 모두** 완전히 같다.
- 같은 사람이 두 매치에 들어간 횟수 **0**.
- 형성된 적 없는 MatchId 는 정원 0, 명단 조회 거부.

---

## 목차
1. [이름을 정하는 주체가 없었다](#1-이름을-정하는-주체가-없었다)
2. [레이팅은 어디서 오는가](#2-레이팅은-어디서-오는가)
3. [정렬은 전순서여야 한다](#3-정렬은-전순서여야-한다)
4. [창과 대기 — 품질의 두 축](#4-창과-대기--품질의-두-축)
5. [MatchId 는 명단의 함수다](#5-matchid-는-명단의-함수다)
6. [장부 — 서명만으로는 부족하다](#6-장부--서명만으로는-부족하다)
7. [설계에서 조정한 것](#7-설계에서-조정한-것)
8. [변이 둘](#8-변이-둘)
9. [실행과 관측](#9-실행과-관측)

---

## 1. 이름을 정하는 주체가 없었다

입장 승인의 검사 목록은 이미 길다. HMAC-SHA256 서명, 종류가 `game` 인가, `ServerId` 가 이 서버인가, 만료가 지나지 않았는가, 같은 토큰이 다른 끝점에서 이미 쓰이지 않았는가, 같은 playerId 가 다른 끝점에 있지 않은가, 정원이 남았는가.

일곱 개를 전부 통과한 토큰이 남기는 마지막 질문이 matchId 다. 그리고 그 질문에는 답할 근거가 없었다. 서버는 자기가 어떤 매치를 열기로 되어 있는지 모른다.

이것이 왜 실제 결함인지는 두 가지로 나뉜다.

**하나. 정원이 이름과 묶여 있지 않다.** 서버의 정원 2는 서버의 상수이지 그 매치의 정원이 아니다. 3인 매치와 2인 매치를 같은 서버 풀에서 섬기려는 순간 그 구분을 표현할 자리가 없다.

**둘. 명단이 없다.** 서명이 유효한 토큰 두 개가 같은 이름을 말하기만 하면 들어온다. 매치메이커가 짝지은 두 사람인지, 서로 모르는 두 사람이 같은 이름을 우연히(또는 고의로) 말한 것인지 구분되지 않는다.

형성기가 생기면 둘 다 답이 생긴다. 매치는 **명단과 정원과 이름을 함께** 갖고 태어난다.

## 2. 레이팅은 어디서 오는가

별도의 레이팅 시스템을 두지 않았다. 이유는 그것을 움직일 신호가 아직 없기 때문이다 — 이 게임은 협동 PvE 라 승패가 상대적이지 않고, 지금 저장되는 유일한 실력 대리 지표는 프로필의 `bestScore` 다.

그 사실을 이름으로 적어 둔다.

<!-- snippet: include/mp/matchmaking.hpp#RatingFromBestScore -->
```cpp
inline int RatingFromBestScore(uint32_t bestScore) {
    int      rating    = 1000;   // 아직 한 판도 끝내지 못한 사람의 자리
    uint32_t threshold = 100;
    while (bestScore >= threshold && rating < 3000) {
        rating += 100;
        threshold *= 2;
    }
    return rating;
}
```

계단이 배로 넓어지는 것은 점수 분포 때문이다. 점수는 도달한 웨이브에 대해 대략 지수로 자라므로, 선형으로 매핑하면 상위 몇 명이 레이팅 축 전체를 차지하고 나머지 전부가 한 칸에 뭉친다. 로그 계단은 그 뭉침을 편다.

성질 둘을 검사가 잠근다. **단조 비감소** — 점수가 오르면 레이팅이 내려가지 않는다. **유계** — 1000 이상 3000 이하. 상한이 없으면 극단값 하나가 창 계산을 무의미하게 만든다.

```cpp
CHECK_EQ(RatingFromBestScore(0), 1000);
CHECK_EQ(RatingFromBestScore(4000000u), 2600);
CHECK_EQ(RatingFromBestScore(0xFFFFFFFFu), 3000);   // 상한에서 잘린다
```

## 3. 정렬은 전순서여야 한다

형성 자체는 단순하다. 레이팅 순으로 늘어놓고 앞에서부터 `teamSize` 명씩 끊되, 그 묶음의 폭이 창 이내일 때만 매치로 만든다.

<!-- snippet: include/mp/matchmaking.hpp@form-matches mode=partial -->
```cpp
    std::vector<Match> FormMatches(uint64_t now = 0) {
        std::vector<Match> out;
        std::sort(queue_.begin(), queue_.end(), [](const Player& a, const Player& b) {
            if (a.rating != b.rating) return a.rating < b.rating;
            return a.id < b.id;                     // 전순서: 동점의 순서를 고정한다
        });
        // ...
    }
```

비교 함수의 둘째 줄이 이 절의 전부다.

`a.rating < b.rating` 하나만으로 비교하면 레이팅이 같은 플레이어들 사이의 순서가 **정해지지 않는다**. `std::sort` 는 안정 정렬이 아니므로 같은 큐가 입력 순서에 따라 다른 짝을 낼 수 있다.

그것이 왜 결함인지는 짚어야 한다. 폭 단언은 통과한다 — 동점끼리 어떻게 섞이든 폭은 여전히 창 이내다. 매치 수도 같다. 무너지는 것은 **"이 큐에서 이 매치가 나온다"는 문장 자체**이고, 그 문장이 없으면 매칭 품질을 재는 어떤 수치도 재현되지 않는다. 다음 실행에서 다른 수가 나올 때 그것이 코드 변경 때문인지 정렬의 우연 때문인지 구분할 수 없다.

레이팅이 100 단위 계단이라 동점이 아주 흔하다는 것도 이 문제를 키운다. 500명 큐에서 서로 다른 레이팅 값은 스물 몇 개뿐이다.

검사는 같은 사람들을 **순서만 섞어** 두 번 넣고 결과가 같은지 본다.

```cpp
std::vector<Match> a, b;
FormFrom(ascending, 2, 150, 500, &a);
FormFrom(shuffled, 2, 150, 500, &b);
CHECK_EQ(a.size(), b.size());
...
CHECK_EQ(identical, a.size());
```

## 4. 창과 대기 — 품질의 두 축

매칭 품질은 하나가 아니다. 레이팅 폭이 좁을수록 매치가 공정하고, 대기가 짧을수록 플레이어가 덜 떠난다. 둘은 서로 대가다.

그 대가를 수로 본다.

```
500명 큐 · 창 100 · 2인: 매치 250개 · 잔여 0명 · 최대 레이팅 폭 100 · 최대 대기 998
창 0: 매치 247개 · 잔여 6명 · 최대 폭 0
```

창을 100에서 0으로 — 즉 "레이팅이 정확히 같은 사람끼리만" — 좁히면 폭은 0이 되지만 6명이 짝을 찾지 못하고 큐에 남는다. 500명 규모에서 3개 매치를 잃는 대가로 폭을 100에서 0으로 줄이는 것이고, 이 게임이 협동 PvE 라는 점을 생각하면 그 거래는 좋지 않다. 레이팅 100 차이가 협동에서 만드는 차이는 작다.

큐에 남은 사람이 굶지 않는다는 것도 성질이다. 다음 회차에 비슷한 사람이 오면 묶인다.

```cpp
CHECK(maker.Enqueue(Player{1, 1000, 0}, 0));
CHECK(maker.Enqueue(Player{2, 1400, 0}, 0));
CHECK_EQ(maker.FormMatches(10).size(), (size_t)0);   // 차 400 > 창 50
CHECK_EQ(maker.LongestWait(30), (uint64_t)30);

CHECK(maker.Enqueue(Player{3, 1020, 25}, 25));
const std::vector<Match> formed = maker.FormMatches(30);
CHECK_EQ(formed.size(), (size_t)1);
CHECK_EQ(formed[0].maxWait, (uint64_t)30);     // 가장 오래 기다린 사람 기준
```

`maxWait` 를 매치에 실어 두는 이유는 운영 지표다. "형성된 매치의 대기 분포"는 창 폭을 조정할 때 보는 수이고, 그것을 나중에 재구성하려면 큐 이력을 따로 남겨야 한다.

지금 이 형성기가 하지 않는 것도 분명하다. **대기가 길어졌다고 창을 넓히지 않는다.** 실전 매치메이커의 표준 동작이지만, 그 정책은 "얼마나 기다렸을 때 얼마나 넓히는가"라는 튜닝 값을 요구하고 그 값을 정할 실측이 없다. 지금 있는 것은 그 정책을 얹을 자리(`LongestWait`, `Match::maxWait`)까지다.

## 5. MatchId 는 명단의 함수다

이름은 결정적이어야 한다. 실행마다 달라지면 토큰과 서버가 맞춰 볼 기준이 사라진다.

<!-- snippet: include/mp/matchmaking.hpp#FormatMatchId -->
```cpp
inline std::string FormatMatchId(uint32_t serial, const std::vector<uint32_t>& players) {
    uint32_t hash = 2166136261u;
    for (uint32_t id : players) {
        for (int shift = 0; shift < 32; shift += 8)
            hash = (hash ^ ((id >> shift) & 0xFFu)) * 16777619u;
    }
    static const char* kHex = "0123456789abcdef";
    std::string out = "m-";
    for (int shift = 12; shift >= 0; shift -= 4) out += kHex[(serial >> shift) & 0xF];
    out += '-';
    for (int shift = 28; shift >= 0; shift -= 4) out += kHex[(hash >> shift) & 0xF];
    return out;
}
```

15자다. `m-` + 일련번호 4자리 + `-` + 명단 해시 8자리.

두 부분이 다른 일을 한다. **일련번호**는 같은 명단이 다시 만나도 다른 매치가 되게 한다 — 어제 같이 한 두 사람이 오늘 다시 만나면 그것은 다른 매치다. **명단 해시**는 이름 하나가 명단 하나에 묶이게 한다.

명단 해시가 순서에 민감한 것도 의도다. `{7, 11}` 과 `{11, 7}` 은 다른 이름이 된다. 형성기가 명단을 항상 정렬된 순서로 만들기 때문에 실제로는 후자가 나오지 않지만, 두 값이 우연히 같아지지 않는 편이 나중에 명단 순서에 의미가 생겼을 때 안전하다.

이름을 UUID 로 두는 안을 기각했다. 난수원이 필요해지고, 그 난수원이 결정성 규약의 예외를 하나 만든다. 형성기가 결정적이면 같은 큐가 같은 매치와 같은 이름을 내고, 그것이 재현 가능한 매칭 품질 측정의 전제다.

## 6. 장부 — 서명만으로는 부족하다

형성된 매치를 기억하는 자리가 `MatchRegistry` 다.

<!-- snippet: include/mp/matchmaking.hpp#MatchRegistry mode=partial -->
```cpp
class MatchRegistry {
public:
    bool Register(const Match& match) {
        if (match.id.empty() || match.players.empty()) return false;
        if (matches_.count(match.id)) return false;
        for (uint32_t player : match.players)
            if (assigned_.count(player)) return false;   // 이미 다른 매치의 멤버다
        matches_[match.id] = match.players;
        for (uint32_t player : match.players) assigned_[player] = match.id;
        return true;
    }
    // ...
};
```

세 가지를 막는다. 같은 이름의 두 번째 등록, 빈 명단, 그리고 **한 사람이 두 매치의 멤버가 되는 것**.

마지막 것이 큐 쪽의 중복 등록 거절과 짝을 이룬다. `Matchmaker::Enqueue` 는 이미 큐에 있는 id 를 거절하고, `Register` 는 이미 배정된 id 를 거절한다. 두 방어가 다른 층에 있는 이유는 큐와 장부가 서로를 모르기 때문이다 — 큐는 매치가 끝났는지 모르고, 장부는 누가 대기 중인지 모른다.

입장 승인이 물을 수 있게 된 질문은 둘이다.

```cpp
CHECK(registry.Known(match.id));
CHECK_EQ(registry.Capacity(match.id), (size_t)2);
for (uint32_t id : match.players) CHECK(registry.Allows(match.id, id));
CHECK(!registry.Allows(match.id, 999999u));    // 명단 밖

// 형성된 적 없는 이름은 통째로 거부. 서명만 유효한 토큰이 열 수 있던 문이다.
CHECK(!registry.Known("m-0000-00000000"));
CHECK_EQ(registry.Capacity("m-dead-beefbeef"), (size_t)0);
```

정렬 컨테이너를 쓴 것도 이유가 있다. 순회 순서가 삽입 순서에 좌우되면 같은 장부가 실행마다 다른 순서로 읽히고, 그 위에서 세는 어떤 수치도 재현되지 않는다.

`Forget` 은 매치가 끝났을 때 부른다. 그래야 그 사람들이 다시 큐에 설 수 있다. 지우지 않으면 장부가 한 방향으로만 자라고, 한 번 플레이한 사람은 영원히 배정된 상태로 남는다.

와이어에 종류 둘이 는다. `QueueRequest = 7` 과 `QueueResponse = 8`, `kVersion` 4→5.

```cpp
inline net::Bytes WriteQueueRequest(std::string_view playerId, int rating,
                                    const net::PacketHeader& framing = {});
inline net::Bytes WriteQueueResponse(std::string_view matchId,
                                     const net::PacketHeader& framing = {});
```

`matchId` 는 **정확히 15자만** 받는다. `FormatMatchId` 가 내는 폭이 그것이고, 길이를 느슨하게 두면 형성되지 않은 이름이 문법만 맞춰 들어올 통로가 생긴다. 레이팅은 클라이언트가 말하는 값이라 신뢰 경계 밖이므로 3000에서 자른다 — 여기서 안 자르면 그 위의 수 하나가 큐 정렬을 통째로 왜곡한다.

문자열 `playerId` 와 매치메이커의 정수 id 를 잇는 함수도 하나뿐이어야 한다.

<!-- snippet: include/game/defense_protocol.hpp#PlayerKey -->
```cpp
inline uint32_t PlayerKey(std::string_view playerId) {
    uint32_t hash = 2166136261u;
    for (char c : playerId) { hash ^= static_cast<uint8_t>(c); hash *= 16777619u; }
    return hash;
}
```

큐 요청과 입장 토큰이 같은 함수를 지나야 "줄 선 사람"과 "들어온 사람"이 같은 사람인지 물을 수 있다.

**`activeMatch_` 의 정체가 바뀌었다.** 예전 코드는 이랬다.

```cpp
    const bool matchConflict = !activeMatch_.empty() && activeMatch_ != admission.matchId;
    // ...
    if (activeMatch_.empty()) activeMatch_ = admission.matchId;
```

먼저 도착한 토큰이 이름을 정하고, 두 번째 토큰이 그 이름과 같은지만 봤다. 서명만 유효하면 아무 이름의 매치나 열 수 있었다는 뜻이다. 지금은 장부에 묻는다.

```cpp
    if (!matches_.Allows(admission.matchId, wire::PlayerKey(admission.playerId))) {
        ++rejectedPackets_;
        return;
    }
    const size_t capacity = matches_.Capacity(admission.matchId);
    const bool full = clients_.size() >= capacity ||
                      clients_.size() >= kServerMaximumPlayers;
```

정원을 둘 다 검사하는 것이 의도다. `kServerMaximumPlayers` 는 이 프로세스가 감당하는 수이고 `Capacity` 는 이 판이 요구하는 수다 — 지금 둘 다 2인 것은 우연이다. `activeMatch_` 는 남았지만 이제 "지금 도는 판의 이름"일 뿐이고, 승인 판정에는 쓰이지 않는다. 매치가 끝나면 `matches_.Forget` 과 함께 비워진다.

**실측 왕복.** 서버를 띄우고 두 소켓이 줄을 선다.

```
[queue] formed id=m-0001-bd20bd80 players=2 rating=1200..1250
[admission] reject reason=content_mismatch player=alice client=0x51570A985EA2FBD6 server=0x51570A985EA2FBD7
[admission] player=bob   endpoint=127.0.0.1:50461 players=1/2 content=accept grant=161B ...
[admission] player=alice endpoint=127.0.0.1:50460 players=2/2 content=accept grant=161B ...
[match] start id=m-0001-bd20bd80 capacity=2
[server] queue requests=2 responses=2 duplicates=0 matches=1
[server] stop tick=317 players=2 commands=0 rejected=1 ...
```

`m-0001-bd20bd80` 은 두 소켓이 **각자** 받은 이름이고 둘이 같다. 형성이 없으면 이 이름은 존재하지 않는다.

레이팅은 별도 시스템이 아니라 이미 저장되고 있던 값에서 나온다.

```cpp
    int QueueRating() const {
        return mp::RatingFromBestScore(profile_.bestScore);
    }
```

`defense::Profile::bestScore` 는 `ProgressionStore` 가 매치마다 갱신하는 값이고, 그 저장소는 이 제품이 이미 갖고 있었다. 협동 PvE 라 승패가 상대적이지 않으므로 지금 저장되는 유일한 실력 대리 지표가 그것이다 — 그 사실을 이름으로 적어 두는 편이 가짜 Elo 를 하나 더 만드는 것보다 정직하다.

배선에서 한 가지를 바꿔야 했다. 클라이언트는 권위 진행도를 갖지 않으므로 기본 저장소를 열지 않는 것이 기존 동작이었는데, 그러면 모든 클라이언트가 레이팅 1000을 말하고 매칭 창은 있지만 그 창이 아무것도 가르지 않는다. 그래서 **명시된** 프로필(`--profile PATH`)이 있을 때만 읽는다. 기본 경로는 여전히 열지 않는다.

로그 스케일이 만드는 계단은 이렇다(`RatingFromBestScore`).

```
bestScore      0     99    100    199    200    399    400    799  …  52428800+
rating      1000   1000   1100   1100   1200   1200   1300   1300  …      3000
```

한 판을 끝낸 프로필이 `bestScore` 로 얻는 값이 셀프테스트 기준 400~450 근처이므로, 실제 프로필로 만든 큐의 레이팅은 지금 1300 언저리에 몰린다. 계단이 배로 넓어지므로 초반 몇 판의 차이가 창(100) 하나를 넘지 못하고, 그것이 협동 게임에서 원하는 성질이다 — 실력 차이로 사람을 못 만나게 하는 것은 매칭이 아니라 벽이다.

큐 요청에 실리는 값은 `--queue PLAYER_ID` 로 켜지고, 받은 이름은 로그에 남는다.

```
[queue] player=alice rating=1300 match=m-0001-bd20bd80
```

## 7. 설계에서 조정한 것

**`Matchmaker` 와 `MatchRegistry` 를 나눴다.** 하나로 합치면 코드가 짧아지지만 수명이 다르다. 큐는 매치가 형성되면 비워지고, 장부는 매치가 끝날 때까지 남는다. 전용 서버가 여러 매치를 섬기게 되면 큐는 하나이고 장부는 그 위에서 계속 자란다.

**host 선정을 id 최솟값으로 남겼다.** 실전은 RTT 가 낮은 피어를 고르지만, 그러려면 큐에 들어올 때 각 피어의 링크 특성을 알아야 하고 지금은 그 표본이 없다. 전용 서버 토폴로지에서는 host 가 서버이므로 이 필드가 쓰이지 않는다는 점도 있다 — 리슨 호스트 모드에만 의미가 있다.

**`enqueuedAt` 을 호출자 단위로 뒀다.** 시계를 내부에 두면 이 헤더가 시간을 부르게 되고, 그러면 같은 큐가 실행마다 다른 대기 값을 낸다. 단위를 정하지 않은 것도 의도다 — 서버는 초를, 검사는 임의의 눈금을 쓴다.

**정원을 `Match::players.size()` 로 뒀다.** 별도 필드를 두면 명단과 정원이 갈라질 수 있는 자리가 생긴다.

## 8. 변이 둘

**하나. `FormMatches` 의 정렬을 통째로 지웠다.**

```
> test_queue_to_match.exe
  500명 큐 · 창 100 · 2인: 매치 219개 · 잔여 62명 · 최대 레이팅 폭 100 · 최대 대기 998
  창 0: 매치 202개 · 잔여 96명 · 최대 폭 0
  FAIL test_queue_to_match.cpp:142  CHECK_EQ(a.size(), b.size())
  입력 순서만 다른 두 큐: 매치 87개 중 0개가 완전히 같다
  FAIL test_queue_to_match.cpp:149  CHECK_EQ(identical, a.size())
[test] queue_to_match: 9889 checks, 2 failures
```

여기서 배운 것이 있다. **폭 단언이 죽지 않는다.** 정렬 없이도 우연히 인접한 두 사람의 레이팅 차가 창 이내이면 매치가 만들어지고, 그 매치의 폭은 여전히 창 이내다. 죽는 것은 재현성 단언이고, 퇴행은 **매치 수**로만 보인다 — 250개에서 219개로. 31개의 짝이 사라졌다.

계획 단계에서 예상한 것은 "정렬을 지우면 레이팅 폭 단언이 실패한다"였다. 틀렸다. 폭 단언은 형성 규칙이 `if (폭 <= 창)` 을 직접 검사하므로 정렬과 무관하게 성립한다. 정렬이 지키는 것은 폭이 아니라 **효율**(비슷한 사람이 실제로 인접하게 되는 것)과 **재현성**이다.

**둘. 정렬 비교에서 id 두 번째 키만 지웠다.**

```
> test_queue_to_match.exe
  500명 큐 · 창 100 · 2인: 매치 250개 · 잔여 0명 · 최대 레이팅 폭 100 · 최대 대기 998
  창 0: 매치 247개 · 잔여 6명 · 최대 폭 0
  입력 순서만 다른 두 큐: 매치 99개 중 1개가 완전히 같다
  FAIL test_queue_to_match.cpp:149  CHECK_EQ(identical, a.size())
[test] queue_to_match: 10315 checks, 1 failures
```

첫 두 줄이 **깨끗한 빌드와 완전히 같다.** 매치 수도, 잔여도, 폭도, 대기도. 유일하게 다른 것이 순서만 섞은 두 큐의 일치 개수 99 → 1 이다.

이것이 이 변이의 값이다. 전순서가 없는 상태는 어떤 결과 지표로도 관측되지 않고, **오직 같은 입력을 두 번 다르게 넣어 보는 검사만이** 그것을 본다. 이런 종류의 결함이 오래 살아남는 이유이기도 하다.

## 9. 실행과 관측

```
> test_queue_to_match.exe
[test] queue_to_match
  500명 큐 · 창 100 · 2인: 매치 250개 · 잔여 0명 · 최대 레이팅 폭 100 · 최대 대기 998
  창 0: 매치 247개 · 잔여 6명 · 최대 폭 0
  입력 순서만 다른 두 큐: 매치 99개 중 99개가 완전히 같다
[test] queue_to_match: 10315 checks, 0 failures
```

첫 줄의 `잔여 0명` 은 500이 짝수이고 레이팅이 100 계단이라 나온 값이다. 계단이 굵어서 같은 레이팅 무리가 크고, 무리 안에서 전부 짝이 지어진다. 실제 분포가 더 고르면 잔여가 는다.

`최대 대기 998` 은 합성 큐의 입장 시각이 0~599 눈금에 흩어져 있고 형성 시각이 1000이라 나온 수다. 이 수 자체는 하네스의 값이지 링크의 값이 아니다.

둘째 줄의 `잔여 6명` 이 홀수 크기 레이팅 무리의 개수다. 창 0에서는 무리를 넘어 묶일 수 없으므로 각 무리의 홀수 한 명이 남는다.

셋째 줄이 이 편에서 가장 얇은 얼음 위에 있는 수치다. 99개가 전부 같다는 것은 형성기가 큐의 **집합**만 보고 **순서**를 보지 않는다는 뜻이고, 그 성질은 비교 함수 한 줄에 매달려 있다.

## 8. 제품 실측

전용 서버를 띄우고 두 소켓이 줄을 선 뒤 그 이름으로 입장한다.

```
> mega_defense_server --port 27155 --server-id probe-server --run-seconds 6
[security] using the local development token secret; do not expose this server
[server] id=probe-server udp=0.0.0.0:27155 capacity=2
[platform] Windows/x86-64/LE profile=PC
[patch] version=1 hash=0x51570A985EA2FBD7 assets=18 cooked=17
[queue] formed id=m-0001-bd20bd80 players=2 rating=1200..1250
[admission] player=bob   endpoint=127.0.0.1:50461 players=1/2 content=accept grant=161B
[admission] player=alice endpoint=127.0.0.1:50460 players=2/2 content=accept grant=161B
[match] start id=m-0001-bd20bd80 capacity=2
[server] queue requests=2 responses=2 duplicates=0 matches=1
[server] stop tick=317 players=2 commands=0 rejected=1 snapshots=210 snapshot_bytes=29214 avg=139.1 result=incomplete
```

`rejected=1` 이 이 편의 핵심 수치다. 같은 실행에서 형성되지 않은 이름(`m-0000-00000000`)으로 한 번 두드렸고, 서명이 유효한 토큰이었는데도 승인되지 않았다. 배선 전이라면 그 토큰이 매치를 **열었을** 것이다 — 먼저 온 쪽이 이름을 정하는 규칙이었으므로.

같은 왕복을 프로세스 경계 밖에서 자동으로 확인하는 것이 `test_dedicated_process` 다.

```
> test_dedicated_process.exe
[test] dedicated_process: queue -> match id=m-0001-bd20bd80
[test] dedicated_process: 19 checks, 0 failures
```

세 단계를 순서대로 밟는다. (0) 형성되지 않은 MatchId 로는 스냅샷이 한 장도 오지 않는다. (1) 큐 요청 두 개가 같은 15자 이름을 받아 온다. (2) 그 이름으로만 입장이 승인되고 명령·스냅샷이 흐른다. 순서를 뒤집으면 "이름을 받아 왔다"만 보이고 그 이름이 **필요했다**는 것은 보이지 않는다.

CTest 는 111개 대상을 전수 통과한다. 이 편이 들여온 검사는 `test_queue_to_match`(합성 큐에서 형성·장부·정원) 이고, `test_mp_matchmaking` 에는 이름과 중복 거절 케이스가 하나 늘었으며, `test_dedicated_process` 는 위 세 단계로 다시 쓰였다.

---

## 결론

이 편의 변경은 코드 양으로는 작다. 정렬 비교 한 줄, 문자열 만드는 함수 하나, 그리고 `std::map` 두 개짜리 장부.

크기에 비해 바꾸는 것이 큰 이유는 **없던 주체가 생기기 때문**이다. 지금까지 매치는 "토큰이 말하는 것"이었다. 그것은 사실 매치가 존재하지 않았다는 뜻이다 — 이름만 있고 명단도 정원도 없는 것을 매치라고 부를 수 없다. 형성기가 생기고 나서야 서버가 "이 매치"라고 말할 때 그것이 무엇을 가리키는지가 정해진다.

정렬 한 줄에서 배운 것이 그다음이다. 전순서가 아닌 비교는 **어떤 결과 지표로도 보이지 않는다.** 매치 수도 폭도 대기도 전부 정상이고, 오직 같은 입력을 두 번 다르게 넣었을 때만 갈라진다. 결정성 규약이 "순회 순서가 삽입에 좌우되는 자료구조를 시뮬 경로에 두지 않는다"라고 말할 때 막으려는 것이 정확히 이런 종류의 결함이고, 그것을 잡는 검사의 모양도 정해져 있다 — **같은 것을 다른 순서로 두 번**.

측정에서 나온 것 둘.

**창을 100에서 0으로 좁히는 대가는 500명당 3개 매치다.** 이 수를 보기 전에는 "폭을 좁히면 대기가 는다"가 방향만 있는 문장이었다.

**정렬을 지우면 매치가 250 → 219 개로 준다.** 정렬이 지키는 것이 폭이 아니라 효율이라는 사실이 이 수로 드러났고, 그 전까지는 정렬의 역할을 잘못 알고 있었다.

현재 상태의 한계 넷.

**창이 대기에 따라 넓어지지 않는다.** 오래 기다린 사람의 창을 넓히는 것이 표준 동작이지만, 그 정책은 튜닝 값을 요구하고 그 값을 정할 실측이 없다. 지금 있는 것은 정책을 얹을 자리까지다.

**레이팅이 계단이라 창이 거칠다.** 100 단위 계단 위에서 창 100은 "한 칸 옆까지"이고 창 0은 "같은 칸만"이다. 그 사이가 없다. 점수를 연속 값으로 매핑하면 창이 세밀해지지만, 그러면 같은 레이팅이 거의 없어져 전순서 문제가 사라지는 대신 대기가 는다.

**장부가 프로세스 메모리에만 있다.** 서버가 재시작하면 형성된 매치가 전부 사라지고, 이미 발급된 토큰은 갈 곳이 없어진다. 장부를 영속화하려면 db 계층을 지나야 하고, 그것은 매치 형성이 서버와 다른 프로세스로 갈라진 뒤의 문제다.

**형성기와 서버가 같은 프로세스에 있다고 가정한다.** `MatchRegistry` 를 서버가 직접 소유하므로 매치 형성도 그 서버에서 일어나야 한다. 실전은 형성이 중앙에서 일어나고 서버는 그 결과를 받는 구조이며, 그때 필요한 것은 장부가 아니라 토큰 안에 명단을 싣는 것이다.
