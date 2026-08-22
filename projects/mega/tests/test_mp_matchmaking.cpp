// 매치메이킹 검증: 레이팅 윈도우 내 세션 형성·host 선정·잔여 큐.
#include "mp/matchmaking.hpp"
#include "test_util.h"

#include <vector>

using namespace mega::mp;

static void TestBasicForm() {
    Matchmaker mm;
    mm.Init(/*teamSize*/ 2, /*window*/ 100);
    mm.Enqueue({1, 1000});
    mm.Enqueue({2, 1050});      // 1,2 → 매치(차 50)
    mm.Enqueue({3, 1500});
    mm.Enqueue({4, 1520});      // 3,4 → 매치(차 20)
    auto matches = mm.FormMatches();
    CHECK_EQ(matches.size(), (size_t)2);
    for (const auto& m : matches) {
        CHECK_EQ(m.players.size(), (size_t)2);
        CHECK(m.maxRating - m.minRating <= 100);        // 윈도우 내
        // host 는 매치 멤버 중 하나.
        bool hostInMatch = false;
        for (auto id : m.players) if (id == m.hostId) hostInMatch = true;
        CHECK(hostInMatch);
    }
    CHECK_EQ(mm.QueueSize(), (size_t)0);                // 전부 매칭됨
}

static void TestOutsideWindowStays() {
    Matchmaker mm;
    mm.Init(2, 100);
    mm.Enqueue({1, 1000});
    mm.Enqueue({2, 1300});      // 차 300 > 100 → 못 묶임
    auto matches = mm.FormMatches();
    CHECK_EQ(matches.size(), (size_t)0);
    CHECK_EQ(mm.QueueSize(), (size_t)2);                // 둘 다 큐 유지

    // 비슷한 레이팅이 더 오면 묶인다.
    mm.Enqueue({3, 1020});      // 1(1000),3(1020) → 매치
    auto m2 = mm.FormMatches();
    CHECK_EQ(m2.size(), (size_t)1);
    CHECK_EQ(m2[0].players.size(), (size_t)2);
    CHECK_EQ(mm.QueueSize(), (size_t)1);                // 1300 만 남음
}

static void TestInsufficientQueue() {
    Matchmaker mm;
    mm.Init(4, 500);
    mm.Enqueue({1, 1000});
    mm.Enqueue({2, 1100});      // 4명 필요한데 2명 → 매치 없음
    auto matches = mm.FormMatches();
    CHECK_EQ(matches.size(), (size_t)0);
    CHECK_EQ(mm.QueueSize(), (size_t)2);
}

static void TestTeamOfFour() {
    Matchmaker mm;
    mm.Init(4, 300);
    for (uint32_t i = 0; i < 8; ++i) mm.Enqueue({i + 1, 1000 + (int)i * 30});   // 1000..1210
    auto matches = mm.FormMatches();
    CHECK_EQ(matches.size(), (size_t)2);                // 8명 → 4인 매치 2개
    for (const auto& m : matches) {
        CHECK_EQ(m.players.size(), (size_t)4);
        CHECK(m.maxRating - m.minRating <= 300);
    }
    CHECK_EQ(mm.QueueSize(), (size_t)0);
}

// 매치에 **이름**이 있어야 입장 토큰과 맞춰 볼 수 있고, 같은 사람이 두 자리를
// 차지하면 정원과 명단이 동시에 거짓이 된다.
static void TestMatchIdAndDuplicateEnqueue() {
    Matchmaker maker;
    maker.Init(2, 100);
    CHECK(maker.Enqueue({1, 1000, 0}));
    CHECK(!maker.Enqueue({1, 1500, 0}));      // 같은 id 는 두 번 줄 서지 못한다
    CHECK_EQ(maker.QueueSize(), (size_t)1);
    CHECK(maker.Enqueue({2, 1050, 0}));

    const std::vector<Match> formed = maker.FormMatches(0);
    CHECK_EQ(formed.size(), (size_t)1);
    CHECK(!formed[0].id.empty());             // 이름이 비면 장부에 올릴 수 없다
    CHECK_EQ(formed[0].id.size(), (size_t)15);

    MatchRegistry registry;
    CHECK(registry.Register(formed[0]));
    CHECK(registry.Allows(formed[0].id, 1));
    CHECK(registry.Allows(formed[0].id, 2));
    CHECK(!registry.Allows(formed[0].id, 3));         // 명단 밖
    CHECK(!registry.Allows("m-0000-00000000", 1));    // 형성되지 않은 이름
    CHECK_EQ(registry.Capacity(formed[0].id), (size_t)2);
    registry.Forget(formed[0].id);
    CHECK(!registry.Allows(formed[0].id, 1));         // 끝난 매치는 사라진다
}

static void RunTests() {
    TestMatchIdAndDuplicateEnqueue();
    TestBasicForm();
    TestOutsideWindowStays();
    TestInsufficientQueue();
    TestTeamOfFour();
}

TEST_MAIN("mp_matchmaking")
