#include "containers/hash_map.h"
#include "containers/intrusive_list.h"
#include "containers/ring_buffer.h"
#include "containers/soa_vector.h"
#include "test_util.h"

#include <cstdint>
#include <set>
#include <string>

using namespace mega;

struct Node {
    int      value;
    ListHook hook;
};

static void TestIntrusiveList() {
    IntrusiveList<Node, &Node::hook> list;
    CHECK(list.Empty());

    Node a{1, {}}, b{2, {}}, c{3, {}};
    list.PushBack(&a);
    list.PushBack(&b);
    list.PushFront(&c);     // [c, a, b]
    CHECK_EQ(list.Size(), 3u);
    CHECK_EQ(list.Front()->value, 3);
    CHECK_EQ(list.Back()->value, 2);

    // 순회 순서 확인.
    int seq[3]; int n = 0;
    for (Node* e = list.First(); e; e = list.Next(e)) seq[n++] = e->value;
    CHECK_EQ(n, 3);
    CHECK(seq[0] == 3 && seq[1] == 1 && seq[2] == 2);

    list.Remove(&a);        // [c, b]
    CHECK_EQ(list.Size(), 2u);
    CHECK_EQ(list.Front()->value, 3);
    CHECK_EQ(list.Next(list.First())->value, 2);

    list.Clear();
    CHECK(list.Empty());
}

static void TestRingBuffer() {
    RingBuffer<int> rb(4);   // pow2
    CHECK(rb.Empty());
    CHECK_EQ(rb.Capacity(), 4u);

    rb.Push(1); rb.Push(2); rb.Push(3);
    CHECK_EQ(rb.Size(), 3u);
    CHECK_EQ(rb[0], 1);
    CHECK_EQ(rb.Newest(), 3);

    int out;
    CHECK(rb.Pop(out)); CHECK_EQ(out, 1);
    CHECK_EQ(rb.Size(), 2u);

    // 가득 차면 오래된 것 덮어쓰기.
    rb.Push(4); rb.Push(5); rb.Push(6);   // size 4 도달 후 덮어씀
    CHECK_EQ(rb.Size(), 4u);
    CHECK_EQ(rb.Newest(), 6);
}

static void TestHashMap() {
    OpenAddressingHashMap<uint32_t, int> map;
    CHECK(map.Empty());

    for (uint32_t i = 0; i < 1000; ++i) CHECK(map.Insert(i, (int)(i * 2)));
    CHECK_EQ(map.Size(), 1000u);

    for (uint32_t i = 0; i < 1000; ++i) {
        int* v = map.Find(i);
        CHECK(v != nullptr);
        if (v) CHECK_EQ(*v, (int)(i * 2));
    }
    CHECK(map.Find(99999) == nullptr);

    // 갱신은 새 삽입 아님.
    CHECK(!map.Insert(5, 999));
    CHECK_EQ(*map.Find(5), 999);

    // backward-shift 삭제 무결성: 절반 삭제 후 나머지 모두 찾기.
    for (uint32_t i = 0; i < 1000; i += 2) CHECK(map.Erase(i));
    CHECK_EQ(map.Size(), 500u);
    for (uint32_t i = 1; i < 1000; i += 2) CHECK(map.Contains(i));
    for (uint32_t i = 0; i < 1000; i += 2) CHECK(!map.Contains(i));
}

static void TestSoAVector() {
    // (위치x, 위치y, hp) 3 컬럼.
    SoAVector<float, float, int> ents;
    CHECK(ents.Empty());

    for (int i = 0; i < 100; ++i) ents.PushBack((float)i, (float)(i * 2), 100 - i);
    CHECK_EQ(ents.Size(), 100u);

    // 컬럼 0(x)만 순회 — SoA 핫 패스.
    float* xs = ents.Data<0>();
    int*   hp = ents.Data<2>();
    float sum = 0; for (size_t i = 0; i < ents.Size(); ++i) sum += xs[i];
    CHECK(sum > 4949.0f && sum < 4951.0f);     // 0..99 합 4950
    CHECK_EQ(hp[0], 100);

    // swap-remove: 인덱스 0 제거 → 마지막(99)이 0으로.
    ents.SwapRemove(0);
    CHECK_EQ(ents.Size(), 99u);
    CHECK_EQ(ents.Data<2>()[0], 100 - 99);     // hp of former last
}

static void RunTests() {
    TestIntrusiveList();
    TestRingBuffer();
    TestHashMap();
    TestSoAVector();
}

TEST_MAIN("containers")
