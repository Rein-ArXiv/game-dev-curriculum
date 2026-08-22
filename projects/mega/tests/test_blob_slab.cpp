// blob_alloc=malloc|slab — 매치 수명 가변 크기 버퍼의 크기 클래스 재사용.
//
// 맵 타일 배열과 경로 배열은 맵마다 크기가 다르고 매치 동안 산다. 매치를 다시
// 시작하거나 맵을 바꾸면 같은 크기대의 버퍼가 해제되고 다시 잡힌다. 슬랩은 그
// 해제분을 같은 클래스의 freelist 에 넣어 두었다가 다음 요청에 그대로 준다.
//
// 판정은 "빨라졌는가"가 아니라 **페이지 수가 수렴하는가**다. 클래스 계산이
// 어긋나면 해제분이 엉뚱한 freelist 로 가고, 그러면 요청마다 새 페이지가 생겨
// 페이지 수가 단조 증가한다 — 겉으로는 그냥 메모리가 새는 것으로 보인다.

#include "game/allocators.hpp"
#include "game/game.hpp"
#include "test_util.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace mega;
using namespace mega::game;

namespace {

// w x h 격자의 유효한 맵 하나(한 줄 폭 경로: 위에서 내려와 아래로).
DefenseMap MakeMap(int w, int h) {
    std::vector<std::string> rows;
    for (int y = 0; y < h; ++y) {
        std::string row(static_cast<size_t>(w), '.');
        row[1] = 'P';
        rows.push_back(row);
    }
    rows.front()[1] = 'S';
    rows.back()[1] = 'C';
    DefenseMap map;
    const bool ok = DefenseMap::FromAscii(rows, 30, 30, map);
    CHECK(ok);
    return map;
}

void TestSizeClassReuseConverges() {
    BlobStore& store = BlobStore::Instance();
    store.Configure(BlobAllocKind::Slab);
    const size_t liveAtStart = store.LiveBlocks();

    // 맵 크기를 바꿔 가며 100회 재생성한다. 타일 배열은 w*h 바이트, 경로 배열은
    // h*8 바이트라 요청 크기가 매번 다른 클래스에 떨어진다.
    size_t pagesAfterWarmup = 0;
    for (int round = 0; round < 100; ++round) {
        const int w = 8 + (round % 5) * 4;      // 8·12·16·20·24
        const int h = 6 + (round % 7) * 3;      // 6 … 24
        {
            DefenseMap map = MakeMap(w, h);
            GameWorld world;
            Switches switches;
            switches.blobAlloc = BlobAllocKind::Slab;
            world.Configure(switches);
            world.Init(map, GenerateWaves(2), 100, 100.0f);
            CHECK(world.Map() != nullptr);
        }
        if (round == 34) pagesAfterWarmup = store.PageCount();
    }

    std::printf("[blob_alloc] rounds=100 pages_after_warmup=%zu pages_final=%zu live=%zu\n",
                pagesAfterWarmup, store.PageCount(), store.LiveBlocks());
    // 워밍업 뒤로는 페이지가 더 늘지 않아야 한다 — 늘면 재사용이 안 되는 것이다.
    CHECK(pagesAfterWarmup > 0);
    CHECK_EQ(store.PageCount(), pagesAfterWarmup);
    // 살아 있는 블록은 시작 시점으로 돌아온다(맵·월드가 전부 소멸했다).
    CHECK_EQ(store.LiveBlocks(), liveAtStart);
    for (int c = 0; c < Slab::NumClasses(); ++c) CHECK_EQ(store.LiveInClass(c), static_cast<size_t>(0));

    store.Configure(BlobAllocKind::Malloc);
}

// 스위치를 매치 사이에 바꿔도 이미 살아 있는 버퍼가 옛 팔로 반납되는가.
// 반납 경로를 지금 모드로 고르면 malloc 포인터가 슬랩으로 들어가 힙이 깨진다.
void TestBuffersReturnToTheArmThatMadeThem() {
    BlobStore& store = BlobStore::Instance();
    store.Configure(BlobAllocKind::Malloc);
    BlobArray<uint32_t> fromMalloc(64, 7u);
    store.Configure(BlobAllocKind::Slab);
    BlobArray<uint32_t> fromSlab(64, 9u);
    CHECK_EQ(fromMalloc[0], 7u);
    CHECK_EQ(fromSlab[0], 9u);
    BlobArray<uint32_t> copy = fromMalloc;      // 사본은 지금 팔(슬랩)에서 잡힌다
    CHECK_EQ(copy[63], 7u);
    store.Configure(BlobAllocKind::Malloc);     // 소멸은 이 모드에서 일어난다
    // 여기서 세 배열이 각자 자기 팔로 돌아간다. 잘못되면 이 스코프를 나가면서 죽는다.
}

// 두 팔이 같은 게임을 돌린다.
void TestBothArmsAgree() {
    auto run = [](BlobAllocKind kind) {
        Switches switches;
        switches.blobAlloc = kind;
        GameWorld world;
        world.Configure(switches);
        const DefenseMap map = DefenseMap::CreateDefault();
        world.Init(map, GenerateWaves(5), 4000, 100000.0f);
        world.PlaceTowerType(map.CellCenter(6, 1), TowerType::Gatling);
        world.PlaceTowerType(map.CellCenter(11, 4), TowerType::Cannon);
        for (int i = 0; i < 2000; ++i) world.Step(1.0f / 60.0f);
        return world.StateHash();
    };
    const uint64_t viaMalloc = run(BlobAllocKind::Malloc);
    const uint64_t viaSlab = run(BlobAllocKind::Slab);
    BlobStore::Instance().Configure(BlobAllocKind::Malloc);
    CHECK_EQ(viaMalloc, viaSlab);
}

}  // namespace

static void RunTests() {
    TestSizeClassReuseConverges();
    TestBuffersReturnToTheArmThatMadeThem();
    TestBothArmsAgree();
}

TEST_MAIN("blob_slab")
