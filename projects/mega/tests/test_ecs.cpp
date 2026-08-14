// Archetype ECS 검증.
#include "ecs/world.h"
#include "test_util.h"

using namespace mega;

namespace {
struct Position { float x, y; };
struct Velocity { float dx, dy; };
struct Health   { int hp; };
struct Tag      { int kind; };
}

static void RunTests() {
    // --- 생성/소멸/생존 ---
    {
        World w;
        EntityId a = w.Create();
        EntityId b = w.Create();
        CHECK(w.Alive(a));
        CHECK(w.Alive(b));
        CHECK_EQ(w.EntityCount(), (size_t)2);
        w.Destroy(a);
        CHECK(!w.Alive(a));
        CHECK(w.Alive(b));
        CHECK_EQ(w.EntityCount(), (size_t)1);
        // index 재사용 시 generation 이 올라 옛 핸들은 무효.
        EntityId c = w.Create();
        CHECK_EQ(c.index, a.index);          // 같은 슬롯 재사용
        CHECK(c.generation != a.generation); // 다른 세대
        CHECK(!w.Alive(a));
        CHECK(w.Alive(c));
    }

    // --- 컴포넌트 추가/조회/덮어쓰기 ---
    {
        World w;
        EntityId e = w.Create();
        w.Add(e, Position{1, 2});
        w.Add(e, Velocity{3, 4});
        CHECK(w.Has<Position>(e));
        CHECK(w.Has<Velocity>(e));
        CHECK(!w.Has<Health>(e));
        CHECK_EQ(w.Get<Position>(e)->x, 1.0f);
        CHECK_EQ(w.Get<Velocity>(e)->dy, 4.0f);
        w.Add(e, Position{9, 9});             // 덮어쓰기(이동 없음)
        CHECK_EQ(w.Get<Position>(e)->x, 9.0f);
        CHECK_EQ(w.Get<Velocity>(e)->dx, 3.0f); // 기존 컴포넌트 보존
    }

    // --- archetype 이동: 추가가 컴포넌트를 보존하는가 ---
    {
        World w;
        EntityId e = w.Create();
        w.Add(e, Position{5, 6});
        w.Add(e, Health{100});
        w.Add(e, Velocity{7, 8});             // Position+Health → +Velocity 이동
        CHECK_EQ(w.Get<Position>(e)->x, 5.0f);
        CHECK_EQ(w.Get<Position>(e)->y, 6.0f);
        CHECK_EQ(w.Get<Health>(e)->hp, 100);
        CHECK_EQ(w.Get<Velocity>(e)->dx, 7.0f);
        w.Remove<Health>(e);                  // 제거 후에도 나머지 보존
        CHECK(!w.Has<Health>(e));
        CHECK_EQ(w.Get<Position>(e)->x, 5.0f);
        CHECK_EQ(w.Get<Velocity>(e)->dx, 7.0f);
    }

    // --- swap-remove 후 record 정합성 ---
    // 같은 archetype 에 여러 엔티티를 두고 중간을 지웠을 때, 마지막이 당겨오며
    // 그 엔티티의 row 가 올바르게 갱신되어야 한다.
    {
        World w;
        EntityId es[5];
        for (int i = 0; i < 5; ++i) {
            es[i] = w.Create();
            w.Add(es[i], Position{(float)i, 0});
            w.Add(es[i], Health{i * 10});
        }
        w.Destroy(es[1]);                     // 가운데 제거 → es[4] 가 그 자리로
        CHECK(!w.Alive(es[1]));
        for (int i = 0; i < 5; ++i) {
            if (i == 1) continue;
            CHECK(w.Alive(es[i]));
            CHECK_EQ(w.Get<Position>(es[i])->x, (float)i); // 값이 섞이지 않았는가
            CHECK_EQ(w.Get<Health>(es[i])->hp, i * 10);
        }
    }

    // --- 청크/엔티티 쿼리 ---
    {
        World w;
        // Position+Velocity 엔티티 3, Position 만 2, Health 만 1.
        for (int i = 0; i < 3; ++i) { auto e = w.Create(); w.Add(e, Position{1,1}); w.Add(e, Velocity{2,2}); }
        for (int i = 0; i < 2; ++i) { auto e = w.Create(); w.Add(e, Position{1,1}); }
        { auto e = w.Create(); w.Add(e, Health{1}); }

        int pvCount = 0;
        w.ForEachChunk<Position, Velocity>([&](size_t n, Position* p, Velocity* v) {
            for (size_t i = 0; i < n; ++i) { p[i].x += v[i].dx; }   // 통합 갱신
            pvCount += (int)n;
        });
        CHECK_EQ(pvCount, 3);                 // Velocity 가진 것만

        int pCount = 0;
        w.ForEach<Position>([&](EntityId, Position& p) { (void)p; ++pCount; });
        CHECK_EQ(pCount, 5);                  // Position 가진 모든 것(P+V 와 P)

        // 청크에서 p.x 가 1+2=3 으로 갱신됐는지 한 번 더 쿼리로 확인.
        bool allThree = true;
        w.ForEachChunk<Position, Velocity>([&](size_t n, Position* p, Velocity*) {
            for (size_t i = 0; i < n; ++i) if (p[i].x != 3.0f) allThree = false;
        });
        CHECK(allThree);
    }

    // --- 스모크: 1만 엔티티 생성 + 쿼리 적분 ---
    {
        World w;
        const int N = 10000;
        std::vector<EntityId> ents;
        ents.reserve(N);
        for (int i = 0; i < N; ++i) {
            EntityId e = w.Create();
            w.Add(e, Position{0, 0});
            w.Add(e, Velocity{1, 2});
            if (i % 3 == 0) w.Add(e, Health{50});   // archetype 분기 유발
            ents.push_back(e);
        }
        // 60 프레임 적분.
        for (int f = 0; f < 60; ++f) {
            w.ForEachChunk<Position, Velocity>([](size_t n, Position* p, Velocity* v) {
                for (size_t i = 0; i < n; ++i) { p[i].x += v[i].dx; p[i].y += v[i].dy; }
            });
        }
        // 모든 엔티티가 60*(1,2) 만큼 이동했는가(archetype 무관).
        bool ok = true;
        for (EntityId e : ents) {
            Position* p = w.Get<Position>(e);
            if (!p || p->x != 60.0f || p->y != 120.0f) { ok = false; break; }
        }
        CHECK(ok);
        CHECK_EQ(w.EntityCount(), (size_t)N);
    }
}

TEST_MAIN("ecs")
