// games/defense/progression.hpp → include/game/defense_progression.hpp (P0-3).
#include "game/defense_progression.hpp"
#include "test_util.h"

#include <cstdio>

using mega::defense::Profile;
using mega::defense::ProgressionStore;

static constexpr const char* kPath = "defense_progression_test.wal";

static void TestTransactionalProgressionPersists() {
    std::remove(kPath);
    {
        ProgressionStore store;
        CHECK(store.Open(kPath));
        Profile profile;
        CHECK(store.Record(5, 900, true, 100, profile));
        CHECK_EQ(profile.totalMatches, 1u);
        CHECK_EQ(profile.clears, 1u);
        CHECK_EQ(profile.bestScore, 900u);
        CHECK(store.Record(3, 400, false, 200, profile));
        CHECK_EQ(profile.totalMatches, 2u);
        CHECK_EQ(profile.clears, 1u);
        CHECK_EQ(profile.bestWave, 5u);
        CHECK_EQ(profile.bestScore, 900u);
    }
    {
        ProgressionStore reopened;
        CHECK(reopened.Open(kPath));
        Profile profile;
        CHECK(reopened.Load(profile));
        CHECK_EQ(profile.totalMatches, 2u);
        CHECK_EQ(profile.clears, 1u);
        CHECK_EQ(profile.bestScore, 900u);
    }
    std::remove(kPath);
}

static void RunTests() {
    TestTransactionalProgressionPersists();
}

TEST_MAIN("defense_progression")
