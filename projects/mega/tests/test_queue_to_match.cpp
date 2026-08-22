// 큐에서 매치까지: 매치를 만드는 주체가 처음 생긴다.
//
// 지금까지 전용 서버가 여는 매치의 이름은 "처음 도착한 토큰이 말한 것"이었다.
// 그 상태에서는 서버가 자기가 무슨 매치를 여는지 알지 못하고, 서명만 유효하면
// 어떤 이름의 매치든 열 수 있었다. 형성기가 생기면 그 이름이 **형성된 매치의
// 명단**과 대조 가능해진다.
//
// 재는 것 넷.
//   1. 합성 500명 큐에서 형성된 매치의 레이팅 폭이 창 이하다.
//   2. 같은 사람이 두 매치에 들어가지 않는다.
//   3. 같은 사람들이 **다른 순서로** 큐에 들어와도 같은 매치가 나온다(결정성).
//   4. 형성된 MatchId 와 그 명단으로만 입장이 승인된다.

#include "mp/matchmaking.hpp"
#include "test_util.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace mega::mp;

// 500명의 합성 프로필. 레이팅은 프로필의 최고 점수에서 나온다 — 별도의 레이팅
// 시스템이 없으므로 저장되는 유일한 실력 대리 지표가 그것이다.
static std::vector<Player> SyntheticQueue(size_t count, uint32_t seed) {
    std::vector<Player> players;
    uint32_t rng = seed ? seed : 1;
    auto next = [&] { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; };
    for (size_t i = 0; i < count; ++i) {
        Player player;
        player.id = static_cast<uint32_t>(i + 1);
        // 대부분은 낮은 점수, 소수가 아주 높은 점수 — 실제 분포에 가깝게.
        const uint32_t bestScore = (next() % 40) < 3 ? next() % 200000 : next() % 4000;
        player.rating = RatingFromBestScore(bestScore);
        player.enqueuedAt = next() % 600;      // 서로 다른 시각에 큐에 들어왔다
        players.push_back(player);
    }
    return players;
}

static void TestRatingFromBestScoreIsMonotone() {
    int previous = RatingFromBestScore(0);
    CHECK_EQ(previous, 1000);
    for (uint32_t score = 0; score < 400000; score += 137) {
        const int rating = RatingFromBestScore(score);
        CHECK(rating >= previous);            // 단조 비감소
        CHECK(rating >= 1000 && rating <= 3000);
        previous = rating;
    }
    CHECK(RatingFromBestScore(100) > RatingFromBestScore(99));
    CHECK_EQ(RatingFromBestScore(4000000u), 2600);
    CHECK_EQ(RatingFromBestScore(0xFFFFFFFFu), 3000);   // 상한에서 잘린다
}

struct QueueReport {
    size_t matches = 0;
    size_t leftover = 0;
    int maxSpread = 0;
    uint64_t maxWait = 0;
    size_t duplicated = 0;
};

static QueueReport FormFrom(const std::vector<Player>& players, int teamSize, int window,
                            uint64_t now, std::vector<Match>* formedOut = nullptr) {
    Matchmaker maker;
    maker.Init(teamSize, window);
    for (const Player& player : players) CHECK(maker.Enqueue(player, now));

    const std::vector<Match> formed = maker.FormMatches(now);
    QueueReport report;
    report.matches = formed.size();
    report.leftover = maker.QueueSize();

    std::set<uint32_t> seen;
    std::set<std::string> ids;
    for (const Match& match : formed) {
        CHECK_EQ(match.players.size(), static_cast<size_t>(teamSize));
        CHECK(!match.id.empty());
        CHECK(ids.insert(match.id).second);           // 이름이 겹치지 않는다
        report.maxSpread = match.maxRating - match.minRating > report.maxSpread
                               ? match.maxRating - match.minRating
                               : report.maxSpread;
        report.maxWait = match.maxWait > report.maxWait ? match.maxWait : report.maxWait;
        bool hostInMatch = false;
        for (uint32_t id : match.players) {
            if (!seen.insert(id).second) ++report.duplicated;
            if (id == match.hostId) hostInMatch = true;
        }
        CHECK(hostInMatch);
    }
    if (formedOut) *formedOut = formed;
    return report;
}

static void TestFiveHundredPlayerQueue() {
    const std::vector<Player> players = SyntheticQueue(500, 20260822u);
    const int window = 100;
    std::vector<Match> formed;
    const QueueReport report = FormFrom(players, 2, window, 1000, &formed);

    std::printf("  500명 큐 · 창 %d · 2인: 매치 %zu개 · 잔여 %zu명 · "
                "최대 레이팅 폭 %d · 최대 대기 %llu\n",
                window, report.matches, report.leftover, report.maxSpread,
                static_cast<unsigned long long>(report.maxWait));

    CHECK(report.matches > 200);                  // 대부분이 짝을 찾는다
    CHECK_EQ(report.duplicated, (size_t)0);       // 같은 사람이 두 매치에 없다
    CHECK(report.maxSpread <= window);            // 창 밖의 짝이 없다
    CHECK_EQ(report.matches * 2 + report.leftover, (size_t)500);

    // 창을 좁히면 폭은 줄고 잔여가 는다 — 매칭 품질의 두 축은 서로 대가다.
    const QueueReport tight = FormFrom(players, 2, 0, 1000);
    std::printf("  창 0: 매치 %zu개 · 잔여 %zu명 · 최대 폭 %d\n",
                tight.matches, tight.leftover, tight.maxSpread);
    CHECK_EQ(tight.maxSpread, 0);
    CHECK(tight.leftover > report.leftover);
}

// 같은 사람들이 다른 순서로 큐에 들어와도 같은 매치가 나와야 한다. 정렬 비교가
// 레이팅 하나뿐이면 동점의 순서가 입력 순서에 좌우되고, 같은 큐가 실행마다
// 다른 짝을 낸다 — 그러면 "이 큐에서 이 매치가 나온다"는 문장 자체가 성립하지 않는다.
static void TestOrderIndependence() {
    const std::vector<Player> ascending = SyntheticQueue(200, 31337u);
    std::vector<Player> shuffled = ascending;
    uint32_t rng = 0xC0FFEEu;
    for (size_t i = shuffled.size(); i > 1; --i) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        std::swap(shuffled[i - 1], shuffled[rng % i]);
    }
    bool orderDiffers = false;
    for (size_t i = 0; i < ascending.size(); ++i)
        if (ascending[i].id != shuffled[i].id) orderDiffers = true;
    CHECK(orderDiffers);           // 실제로 섞였다

    std::vector<Match> a, b;
    FormFrom(ascending, 2, 150, 500, &a);
    FormFrom(shuffled, 2, 150, 500, &b);
    CHECK_EQ(a.size(), b.size());
    size_t identical = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        if (a[i].id == b[i].id && a[i].players == b[i].players) ++identical;
    }
    std::printf("  입력 순서만 다른 두 큐: 매치 %zu개 중 %zu개가 완전히 같다\n",
                a.size(), identical);
    CHECK_EQ(identical, a.size());
}

// 형성된 매치만 입장을 허락한다.
static void TestOnlyFormedMatchesAdmit() {
    const std::vector<Player> players = SyntheticQueue(64, 909090u);
    Matchmaker maker;
    maker.Init(2, 200);
    for (const Player& player : players) CHECK(maker.Enqueue(player, 10));
    CHECK(!maker.Enqueue(players[0], 10));        // 중복 등록 거절

    const std::vector<Match> formed = maker.FormMatches(20);
    MatchRegistry registry;
    for (const Match& match : formed) CHECK(registry.Register(match));
    CHECK_EQ(registry.Count(), formed.size());
    CHECK_EQ(registry.AssignedPlayers(), formed.size() * 2);

    // 같은 매치를 두 번 등록하지 못한다.
    if (!formed.empty()) CHECK(!registry.Register(formed[0]));

    // 명단에 있는 사람만 승인.
    for (const Match& match : formed) {
        CHECK(registry.Known(match.id));
        CHECK_EQ(registry.Capacity(match.id), (size_t)2);
        for (uint32_t id : match.players) CHECK(registry.Allows(match.id, id));
        CHECK(!registry.Allows(match.id, 999999u));    // 명단 밖
    }
    // 형성된 적 없는 이름은 통째로 거부. 서명만 유효한 토큰이 열 수 있던 문이다.
    CHECK(!registry.Known("m-0000-00000000"));
    CHECK(!registry.Allows("m-0000-00000000", players[0].id));
    CHECK_EQ(registry.Capacity("m-dead-beefbeef"), (size_t)0);

    // 다른 매치의 이름으로는 들어가지 못한다.
    if (formed.size() >= 2)
        CHECK(!registry.Allows(formed[0].id, formed[1].players[0]));

    // 매치가 끝나면 장부에서 지운다 — 그러면 그 사람은 다시 큐에 설 수 있다.
    if (!formed.empty()) {
        const std::string finished = formed[0].id;
        registry.Forget(finished);
        CHECK(!registry.Known(finished));
        CHECK_EQ(registry.Count(), formed.size() - 1);
    }
}

// 이름은 명단의 함수다. 같은 일련번호라도 명단이 다르면 다른 이름이 나온다.
static void TestMatchIdIsDeterministic() {
    const std::vector<uint32_t> roster{7, 11};
    const std::string first = FormatMatchId(1, roster);
    CHECK_EQ(first, FormatMatchId(1, roster));
    CHECK(first != FormatMatchId(2, roster));
    CHECK(first != FormatMatchId(1, std::vector<uint32_t>{7, 12}));
    CHECK(first != FormatMatchId(1, std::vector<uint32_t>{11, 7}));
    CHECK_EQ(first.size(), (size_t)15);           // "m-" + 4 + "-" + 8
    CHECK_EQ(first.substr(0, 2), std::string("m-"));
}

// 남은 큐가 굶지 않는지: 다음 회차에 비슷한 사람이 오면 묶인다.
static void TestLeftoverPairsUpLater() {
    Matchmaker maker;
    maker.Init(2, 50);
    CHECK(maker.Enqueue(Player{1, 1000, 0}, 0));
    CHECK(maker.Enqueue(Player{2, 1400, 0}, 0));
    CHECK_EQ(maker.FormMatches(10).size(), (size_t)0);
    CHECK_EQ(maker.QueueSize(), (size_t)2);
    CHECK_EQ(maker.LongestWait(30), (uint64_t)30);

    CHECK(maker.Enqueue(Player{3, 1020, 25}, 25));
    const std::vector<Match> formed = maker.FormMatches(30);
    CHECK_EQ(formed.size(), (size_t)1);
    CHECK_EQ(formed[0].players.size(), (size_t)2);
    CHECK_EQ(formed[0].maxWait, (uint64_t)30);     // 가장 오래 기다린 사람 기준
    CHECK_EQ(maker.QueueSize(), (size_t)1);        // 1400 만 남는다
}

static void RunTests() {
    TestRatingFromBestScoreIsMonotone();
    TestFiveHundredPlayerQueue();
    TestOrderIndependence();
    TestOnlyFormedMatchesAdmit();
    TestMatchIdIsDeterministic();
    TestLeftoverPairsUpLater();
}

TEST_MAIN("queue_to_match")
