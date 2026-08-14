// 레벨 에디터(검증·연결성) + 절차 생성(재현성·연결성) 검증.
#include "tools/level_editor.hpp"
#include "tools/procgen.hpp"
#include "test_util.h"

#include <string>

using namespace mega::tools;

static void TestValidReachable() {
    Level lvl;
    lvl.FromAscii({
        "S...",
        ".##.",
        "...G",
    });
    auto r = lvl.Validate();
    CHECK(r.hasSpawn);
    CHECK(r.hasGoal);
    CHECK(r.reachable);          // 우회로로 도달 가능
    CHECK(r.Valid());
    CHECK_EQ(r.sx, 0); CHECK_EQ(r.sy, 0);
    CHECK_EQ(r.gx, 3); CHECK_EQ(r.gy, 2);
}

static void TestUnreachable() {
    Level lvl;
    lvl.FromAscii({
        "S#G",
        "#.#",
        "...",   // S 는 벽으로 완전 격리
    });
    auto r = lvl.Validate();
    CHECK(r.hasSpawn);
    CHECK(r.hasGoal);
    CHECK(!r.reachable);         // 도달 불가 → 빌드 차단 대상
    CHECK(!r.Valid());
}

static void TestMissingSpawnGoal() {
    Level a; a.FromAscii({"....", "..G."});      // 스폰 없음
    CHECK(!a.Validate().hasSpawn);
    CHECK(!a.Validate().Valid());

    Level b; b.FromAscii({"S...", "...."});      // 목표 없음
    CHECK(!b.Validate().hasGoal);
    CHECK(!b.Validate().Valid());
}

static void TestEditAndCount() {
    Level lvl;
    lvl.Init(5, 5, '.');
    lvl.Set(0, 0, 'S');
    lvl.Set(4, 4, 'G');
    lvl.Set(2, 2, '#');
    auto r = lvl.Validate();
    CHECK_EQ(r.walls, 1);
    CHECK_EQ(r.floors, 24);      // 25 - 1
    CHECK(r.reachable);          // 벽 하나는 우회 가능
    // 라운드트립.
    auto rows = lvl.ToAscii();
    CHECK_EQ((int)rows.size(), 5);
    CHECK_EQ(rows[0][0], 'S');
    CHECK_EQ(rows[2][2], '#');
}

static void TestMazeReproducible() {
    // 같은 시드 → 같은 미로.
    Level m1 = MazeGen::Generate(8, 8, 1337);
    Level m2 = MazeGen::Generate(8, 8, 1337);
    CHECK_EQ(m1.Width(), 17); CHECK_EQ(m1.Height(), 17);   // 2*8+1
    bool same = (m1.ToAscii() == m2.ToAscii());
    CHECK(same);

    // 다른 시드 → 다른 미로(대체로).
    Level m3 = MazeGen::Generate(8, 8, 9999);
    CHECK(m1.ToAscii() != m3.ToAscii());
}

static void TestMazeAlwaysSolvable() {
    // 미로는 트리라 항상 스폰→목표 도달 가능. 여러 시드로 확인.
    for (uint32_t seed : {1u, 42u, 1337u, 99999u, 7u}) {
        Level m = MazeGen::Generate(10, 10, seed);
        auto r = m.Validate();
        CHECK(r.hasSpawn);
        CHECK(r.hasGoal);
        CHECK(r.reachable);      // 항상 풀 수 있다
    }
}

static void RunTests() {
    TestValidReachable();
    TestUnreachable();
    TestMissingSpawnGoal();
    TestEditAndCount();
    TestMazeReproducible();
    TestMazeAlwaysSolvable();
}

TEST_MAIN("tools_level")
