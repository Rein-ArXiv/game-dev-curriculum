#include "memory/arena.h"
#include "memory/pool.h"
#include "memory/slab.h"
#include "test_util.h"

#include <cstdint>
#include <set>

using namespace mega;

static void TestArena() {
    Arena a(1024);
    CHECK_EQ(a.Used(), 0u);

    void* p1 = a.Alloc(100, 16);
    CHECK(p1 != nullptr);
    CHECK((reinterpret_cast<uintptr_t>(p1) & 15) == 0);   // 16 정렬
    CHECK(a.Used() >= 100u);

    void* p2 = a.Alloc(200, 8);
    CHECK(p2 != nullptr);
    CHECK(p2 > p1);                                         // bump 전진

    // 용량 초과는 nullptr.
    CHECK(a.Alloc(10000) == nullptr);

    // Scope 되감기.
    size_t before = a.Used();
    {
        Arena::Scope s(a);
        a.Alloc(300);
        CHECK(a.Used() > before);
    }
    CHECK_EQ(a.Used(), before);                            // 스코프 종료 후 복원

    a.Reset();
    CHECK_EQ(a.Used(), 0u);
    CHECK(a.Peak() >= before);                             // peak 는 유지
}

static void TestPool() {
    Pool pool(64, 8);
    CHECK_EQ(pool.Live(), 0u);

    std::set<void*> ptrs;
    for (int i = 0; i < 8; ++i) {
        void* p = pool.Acquire();
        CHECK(p != nullptr);
        CHECK(pool.Owns(p));
        ptrs.insert(p);
    }
    CHECK_EQ(ptrs.size(), 8u);          // 모두 다른 블록
    CHECK_EQ(pool.Live(), 8u);
    CHECK(pool.Acquire() == nullptr);   // 소진

    void* first = *ptrs.begin();
    pool.Release(first);
    CHECK_EQ(pool.Live(), 7u);
    void* re = pool.Acquire();
    CHECK_EQ(re, first);                // 방금 반납한 블록 재사용

    pool.Reset();
    CHECK_EQ(pool.Live(), 0u);
}

static void TestSlab() {
    Slab slab;

    // 클래스 경계 매핑: 16, 17→32, 64, 65→96.
    void* a = slab.Alloc(16);
    void* b = slab.Alloc(17);
    void* c = slab.Alloc(1000);
    CHECK(a && b && c);
    CHECK((reinterpret_cast<uintptr_t>(a) & 15) == 0);    // 16 정렬 헤더

    // free 후 같은 클래스 재할당 → 청크 재사용.
    slab.Free(a);
    void* a2 = slab.Alloc(16);
    CHECK_EQ(a2, a);

    slab.Free(b);
    slab.Free(c);
    slab.Free(a2);

    // large fallback (클래스 최대 초과).
    void* big = slab.Alloc(100000);
    CHECK(big != nullptr);
    // 쓰기 가능한지 확인(헤더 침범 없음).
    uint8_t* bytes = static_cast<uint8_t*>(big);
    for (int i = 0; i < 100000; i += 4096) bytes[i] = (uint8_t)i;
    slab.Free(big);

    // 대량 할당/해제로 freelist 무결성 확인.
    void* arr[256];
    for (int i = 0; i < 256; ++i) { arr[i] = slab.Alloc(48); CHECK(arr[i] != nullptr); }
    std::set<void*> uniq(arr, arr + 256);
    CHECK_EQ(uniq.size(), 256u);                          // 겹침 없음
    for (int i = 0; i < 256; ++i) slab.Free(arr[i]);
}

static void RunTests() {
    TestArena();
    TestPool();
    TestSlab();
}

TEST_MAIN("memory")
