// memory/pool.h — 고정 블록 풀의 불변식.
//
// 풀이 제품에 들어온 이유는 속도가 아니라 주소 안정성이다(발사체 블록을 스텝 중에
// 참조한다). 그 성질은 freelist 가 정확할 때만 성립한다 — next 연결이 한 칸이라도
// 어긋나면 같은 블록이 두 번 나가고, 그때 나타나는 증상은 "발사체가 서로의 표적을
// 덮어쓴다"처럼 할당자와 무관해 보이는 것이다.

#include "memory/pool.h"
#include "test_util.h"

#include <cstdint>
#include <set>
#include <vector>

using namespace mega;

namespace {

struct Block {
    uint64_t tag;
    uint64_t payload[5];
};

// 1만 회 Acquire/Release 를 섞어도 소유 판정·개수가 맞는가.
void TestChurn() {
    const size_t count = 256;
    Pool pool(sizeof(Block), count);
    std::vector<void*> live;
    uint32_t rng = 0x2545F491u;
    auto next = [&rng] { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; };

    size_t peakSeen = 0;
    for (int step = 0; step < 10000; ++step) {
        const bool take = live.empty() || (live.size() < count && (next() & 1u));
        if (take) {
            void* p = pool.Acquire();
            CHECK(p != nullptr);
            CHECK(pool.Owns(p));
            live.push_back(p);
            if (live.size() > peakSeen) peakSeen = live.size();
        } else {
            const size_t index = next() % live.size();
            pool.Release(live[index]);
            live[index] = live.back();
            live.pop_back();
        }
        CHECK_EQ(pool.Live(), live.size());
    }
    CHECK_EQ(pool.Peak(), peakSeen);

    // 살아 있는 블록은 전부 서로 다른 주소여야 한다 — 같은 블록이 두 번 나가면
    // 여기서 잡힌다.
    std::set<void*> unique(live.begin(), live.end());
    CHECK_EQ(unique.size(), live.size());
}

// 풀을 다 쓰면 nullptr 을 돌려준다(늘어나지 않는다 — 늘어나면 주소가 움직인다).
void TestExhaustion() {
    Pool pool(sizeof(Block), 8);
    std::vector<void*> all;
    for (int i = 0; i < 8; ++i) {
        void* p = pool.Acquire();
        CHECK(p != nullptr);
        all.push_back(p);
    }
    CHECK(pool.Acquire() == nullptr);
    CHECK_EQ(pool.Live(), static_cast<size_t>(8));
    pool.Release(all[3]);
    void* again = pool.Acquire();
    CHECK_EQ(again, all[3]);                 // 방금 반납한 블록이 head 로 돌아온다
    CHECK(!pool.Owns(&all));                 // 풀 밖 주소는 소유하지 않는다
}

// 블록 안에 값을 써 두고 다른 블록을 마구 회전시켜도 그 값이 그대로다.
void TestAddressesAreStable() {
    Pool pool(sizeof(Block), 64);
    Block* kept = static_cast<Block*>(pool.Acquire());
    CHECK(kept != nullptr);
    kept->tag = 0xDEADBEEFCAFEF00Dull;

    for (int round = 0; round < 200; ++round) {
        std::vector<void*> temp;
        for (int i = 0; i < 32; ++i) {
            void* p = pool.Acquire();
            if (p) temp.push_back(p);
        }
        for (void* p : temp) pool.Release(p);
    }
    CHECK_EQ(kept->tag, 0xDEADBEEFCAFEF00Dull);
    CHECK(pool.Owns(kept));
    CHECK_EQ(pool.Live(), static_cast<size_t>(1));

    pool.Reset();
    CHECK_EQ(pool.Live(), static_cast<size_t>(0));
    CHECK_EQ(pool.Acquire(), static_cast<void*>(kept));   // 0번 블록이 다시 head
}

}  // namespace

static void RunTests() {
    TestChurn();
    TestExhaustion();
    TestAddressesAreStable();
}

TEST_MAIN("body_pool")
