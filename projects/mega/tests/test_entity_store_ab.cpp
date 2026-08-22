// entity_store=aos|soa|ecs — 세 저장이 같은 세계를 돌리는가.
//
// SoA 로 갈아타는 이유는 캐시다. 그 이득은 "적 전진 루프가 hp·reward·type 열을
// 건드리지 않는다"에서 나오고, 그 문장이 참이려면 **결과가 같아야** 한다. 결과가
// 다르면 빨라진 것이 아니라 다른 게임이 된 것이다.
//
// 세 번째 팔(ecs)이 붙는 이유는 다른 축이다. 보스만 리그를 갖게 되면서 고정 열
// SoA 는 **없는 컴포넌트에 자리를 낭비**하기 시작했다. archetype 저장은 그 조합을
// 가진 엔티티만 컬럼을 갖는 대신, 행 번호라는 개념을 저장 안에 두지 못하고
// 컴포넌트 접근마다 엔티티 → archetype → 컬럼 두 단계를 지난다.
//
// 그래서 이 파일의 본 단언은 하나다: 같은 시드·같은 웨이브로 3000틱 돌린 세 저장의
// 상태 해시가 비트 동일하다. 해시는 위치·체력·경로 인덱스·골드·코어 체력·발사체·
// 타워·리그 열을 부동소수 비트 그대로 섞는다(GameWorld::StateHash).

#include "game/game.hpp"
#include "profiler/trace.h"
#include "test_util.h"

#include <chrono>
#include <cstdio>
#include <vector>

using namespace mega;
using namespace mega::game;

namespace {

// 프로파일러가 이름별로 접어 둔 총 시간(ms). step08 에서 프레임에 박은 그
// ScopeTimer 들이 여기서 A/B 의 계량기가 된다 — 벽시계 총합은 "달랐다"까지만
// 말하고, 어느 시스템에서 달랐는지는 이 수가 말한다.
double ScopeTotal(const char* name) {
    for (const profiler::AggEntry& entry :
         profiler::AggregateByName(profiler::Profiler::Get().Snapshot()))
        if (entry.name == name) return entry.totalMs;
    return 0.0;
}

// 셀프테스트 스크립트와 같은 자리에 타워를 세운다 — 실제로 플레이되는 배치다.
const DefenseMap::Cell kPlan[8] = {
    {6, 1}, {8, 2}, {10, 2}, {11, 4}, {13, 5}, {10, 6}, {8, 8}, {5, 8},
};

struct MatchResult {
    uint64_t hash = 0;
    int      wave = 0;
    int      gold = 0;
    float    coreHp = 0;
    size_t   scratchOverflows = 0;
    size_t   arenaPeak = 0;
    size_t   enemies = 0;
    uint32_t stomps = 0;
    size_t   rigBytes = 0;
};

MatchResult RunMatch(const Switches& switches, int ticks, Scheduler* scheduler = nullptr) {
    GameWorld world;
    world.Configure(switches, scheduler);
    const DefenseMap map = DefenseMap::CreateDefault();
    // scale=large 는 상한만 올린다. 그 상한까지 실제로 채우려면 웨이브도 커야 한다.
    std::vector<WaveDef> crowd(4, WaveDef{9000, 900.0f, 30.0f, 1, 0.0f, EnemyType::Normal});
    world.Init(map, switches.scale == ScaleKind::Large ? crowd : GenerateWaves(6),
               4000, 100000.0f);
    for (const DefenseMap::Cell& cell : kPlan)
        world.PlaceTowerType(map.CellCenter(cell.x, cell.y), TowerType::Gatling);
    for (int i = 0; i < ticks; ++i) world.Step(1.0f / 60.0f);

    MatchResult out;
    out.hash = world.StateHash();
    out.wave = world.Wave();
    out.gold = world.Gold();
    out.coreHp = world.CoreHp();
    out.scratchOverflows = world.Scratch().Overflows();
    out.arenaPeak = world.Scratch().ArenaPeak();
    out.enemies = world.Enemies().Size();
    out.stomps = world.Stomps();
    out.rigBytes = world.Enemies().RigBytes();
    return out;
}

// 저장을 바꿔도 조준 결과가 흔들리지 않는지. std::vector<Enemy> 위의 SelectTarget
// 이 오라클이고, EnemyStore 위의 SelectTargetIn 이 시험 대상이다.
void TestTargetingAgreesWithOracle() {
    std::vector<Enemy> rows(6);
    for (size_t i = 0; i < rows.size(); ++i) {
        rows[i].pos = {10.0f * static_cast<float>(i), 20.0f};
        rows[i].hp = 10.0f + static_cast<float>(i) * 3.0f;
        rows[i].maxHp = rows[i].hp;
        rows[i].pathIndex = static_cast<uint32_t>(i % 3);
        rows[i].id = static_cast<uint32_t>(i + 1);
        rows[i].alive = (i == 2) ? 0 : 1;      // 죽은 행 하나를 섞는다
    }
    const math::Vec2 tower{0, 0}, core{0, 200};
    const TargetMode modes[3] = {TargetMode::Nearest, TargetMode::First, TargetMode::Strongest};

    for (EntityStoreKind kind :
         {EntityStoreKind::Aos, EntityStoreKind::Soa, EntityStoreKind::Ecs}) {
        for (IdMapKind idMap : {IdMapKind::Linear, IdMapKind::Hash}) {
            EnemyStore store;
            store.Configure(kind, idMap, 64);
            for (const Enemy& e : rows) store.Push(e);
            for (TargetMode mode : modes)
                CHECK_EQ(SelectTargetIn(tower, core, store, 1000.0f, mode),
                         SelectTarget(tower, core, rows, 1000.0f, mode));
            // 죽은 행을 걷어내도 남은 행의 **순서**가 보존되어야 한다. 순서가
            // 바뀌면 동점 조준의 승자와 스냅샷 순서가 함께 흔들린다.
            store.CompactDead();
            CHECK_EQ(store.Size(), static_cast<size_t>(5));
            const uint32_t expected[5] = {1, 2, 4, 5, 6};
            for (size_t i = 0; i < 5; ++i) {
                CHECK_EQ(store.Id(i), expected[i]);
                CHECK_EQ(store.FindRow(expected[i]), static_cast<int>(i));
            }
            CHECK_EQ(store.FindRow(3), -1);    // 걷어낸 id 는 없다
        }
    }
}

// 상한이 실제로 스폰을 막는가. 막지 않으면 scale 스위치는 이름만 있는 것이다.
// 리그 열의 비용. SoA 는 상한만큼, ECS 는 그 컴포넌트를 실제로 가진 개체만큼.
// 이 수가 세 번째 팔이 존재하는 이유 그 자체다.
void TestRigColumnCost() {
    auto rigBytes = [](EntityStoreKind kind, EnemyType type, size_t count) {
        EnemyStore store;
        store.Configure(kind, IdMapKind::Linear, GameWorld::kEnemyCapLarge);
        for (size_t i = 0; i < count; ++i) {
            Enemy e;
            e.id = static_cast<uint32_t>(i + 1);
            e.type = type;
            store.Push(e);
        }
        return store.RigBytes();
    };
    // 보스가 하나도 없는 군집: SoA 는 여전히 상한만큼 리그 열을 들고, ECS 는 0.
    const size_t soaEmpty = rigBytes(EntityStoreKind::Soa, EnemyType::Swarm, 1024);
    const size_t ecsEmpty = rigBytes(EntityStoreKind::Ecs, EnemyType::Swarm, 1024);
    CHECK_EQ(ecsEmpty, static_cast<size_t>(0));
    CHECK(soaEmpty >= GameWorld::kEnemyCapLarge * (sizeof(math::Vec2) + sizeof(uint32_t)));
    // 보스 넷: ECS 는 넷만큼만.
    const size_t ecsBoss = rigBytes(EntityStoreKind::Ecs, EnemyType::Boss, 4);
    CHECK_EQ(ecsBoss, 4 * sizeof(EnemyRig));
    CHECK(ecsBoss * 100 < soaEmpty);
    std::printf("  rig columns: soa=%zuB(상한 %zu행) ecs=%zuB(보스 4) ecs_swarm=%zuB\n",
                soaEmpty, GameWorld::kEnemyCapLarge, ecsBoss, ecsEmpty);

    // 조합 다양성: 보스가 섞이면 archetype 이 둘로 갈린다(코어+바이탈, +리그).
    EnemyStore mixed;
    mixed.Configure(EntityStoreKind::Ecs, IdMapKind::Linear, 64);
    for (int i = 0; i < 8; ++i) {
        Enemy e;
        e.id = static_cast<uint32_t>(i + 1);
        e.type = (i % 4 == 0) ? EnemyType::Boss : EnemyType::Fast;
        mixed.Push(e);
    }
    // 빈 archetype(0) + 생성 도중 거쳐 간 중간 조합들 + 최종 둘.
    CHECK(mixed.ArchetypeCount() >= 3);
    size_t rigRows = 0;
    mixed.ForEachRigChunk([&](size_t count, EnemyCore*, EnemyRig*) { rigRows += count; });
    CHECK_EQ(rigRows, static_cast<size_t>(2));   // 보스 둘만 리그 청크에 든다

    // 그리고 "리그를 가진 행이 있는가"를 묻는 비용. 스톰프 시스템이 매 틱 처음
    // 하는 질문이고, 세 팔의 답이 같아야 하지만 답하는 데 드는 일은 다르다.
    auto probe = [](EntityStoreKind kind) {
        EnemyStore store;
        store.Configure(kind, IdMapKind::Linear, GameWorld::kEnemyCapLarge);
        for (size_t i = 0; i < GameWorld::kEnemyCapLarge; ++i) {
            Enemy e;
            e.id = static_cast<uint32_t>(i + 1);
            e.type = (i % 512 == 0) ? EnemyType::Boss : EnemyType::Swarm;
            store.Push(e);
        }
        const auto begin = std::chrono::steady_clock::now();
        size_t sink = 0;
        for (int repeat = 0; repeat < 2000; ++repeat) sink += store.RigRows();
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - begin).count();
        CHECK_EQ(sink, static_cast<size_t>(2000 * 4));
        return ms;
    };
    const double aosMs = probe(EntityStoreKind::Aos);
    const double soaMs = probe(EntityStoreKind::Soa);
    const double ecsMs = probe(EntityStoreKind::Ecs);
    std::printf("  RigRows() x2000 (2048행 중 보스 4): aos=%.2fms soa=%.2fms ecs=%.2fms\n",
                aosMs, soaMs, ecsMs);
}

void TestCapacityBounds() {
    for (ScaleKind scale : {ScaleKind::Small, ScaleKind::Large}) {
        Switches switches;
        switches.scale = scale;
        GameWorld world;
        world.Configure(switches);
        world.Init(math::Vec2{400, 400}, math::Vec2{40, 40},
                   {WaveDef{5000, 1000.0f, 0.0f, 1, 0.0f, EnemyType::Normal}}, 0, 1e9f);
        world.Step(1.0f / 60.0f);
        CHECK_EQ(world.Enemies().Size(), world.EnemyCap());
        CHECK_EQ(world.EnemyCap(), scale == ScaleKind::Large
                                       ? GameWorld::kEnemyCapLarge
                                       : GameWorld::kEnemyCapSmall);
    }
}

void TestStoresAgreeOverALongMatch() {
    Switches aos;
    Switches soa;
    soa.entityStore = EntityStoreKind::Soa;
    Switches ecs;
    ecs.entityStore = EntityStoreKind::Ecs;

    const MatchResult a = RunMatch(aos, 3000);
    const MatchResult b = RunMatch(soa, 3000);
    const MatchResult c = RunMatch(ecs, 3000);
    CHECK_EQ(a.hash, b.hash);
    CHECK_EQ(a.hash, c.hash);
    CHECK_EQ(a.wave, b.wave);
    CHECK_EQ(a.wave, c.wave);
    CHECK_EQ(a.gold, b.gold);
    CHECK_EQ(a.gold, c.gold);
    CHECK(a.coreHp == b.coreHp);
    CHECK(a.coreHp == c.coreHp);
    CHECK(a.wave > 1);                 // 실제로 진행된 매치여야 의미가 있다
    // 보스가 실제로 발을 디뎠다. 이 수가 0 이면 위 해시 일치는 리그 열이 셋 다
    // 손대지 않은 채 같다는 뜻일 뿐이라 아무것도 증명하지 못한다.
    CHECK(a.stomps > 0);
    CHECK_EQ(a.stomps, b.stomps);
    CHECK_EQ(a.stomps, c.stomps);
    // 같은 매치인데 리그 열의 비용은 다르다 — 그것이 세 번째 팔의 요점이다.
    CHECK(c.rigBytes < b.rigBytes);
    std::printf("  match rig columns: soa=%zuB ecs=%zuB stomps=%u\n",
                b.rigBytes, c.rigBytes, a.stomps);

    // 규모를 올려도 같아야 한다 — 세 저장의 차이는 규모에서 커지고, 결함도 그렇다.
    Switches aosLarge = aos;
    Switches soaLarge = soa;
    Switches ecsLarge = ecs;
    aosLarge.scale = soaLarge.scale = ecsLarge.scale = ScaleKind::Large;
    const MatchResult aosBig = RunMatch(aosLarge, 1500);
    const MatchResult soaBig = RunMatch(soaLarge, 1500);
    const MatchResult ecsBig = RunMatch(ecsLarge, 1500);
    CHECK_EQ(aosBig.hash, soaBig.hash);
    CHECK_EQ(aosBig.hash, ecsBig.hash);
    CHECK(aosBig.enemies >= 512);      // 규모가 실제로 올라갔는지

    // 다른 스위치와 섞어도 같은 세계다(스위치끼리 간섭하지 않는다).
    Switches mixed = ecs;
    mixed.idMap = IdMapKind::Hash;
    mixed.frameAlloc = FrameAllocKind::Arena;
    mixed.blobAlloc = BlobAllocKind::Slab;
    mixed.bodyAlloc = BodyAllocKind::Pool;
    const MatchResult m = RunMatch(mixed, 3000);
    CHECK_EQ(a.hash, m.hash);
    CHECK_EQ(m.scratchOverflows, static_cast<size_t>(0));
    CHECK(m.arenaPeak > 0);            // 아레나가 실제로 쓰였다
}


// 두 저장의 비용. 단언하지 않고 출력만 한다 — 벽시계 시간은 머신 부하에 흔들리는
// 수라서 게이트로 쓰면 게이트가 먼저 무너진다. 판정은 위의 해시 동일성이 하고,
// 이 숫자는 "그래서 얼마나 달랐나"에 답한다.
//
// 워크로드는 상한을 꽉 채운 군집이다. 콘텐츠 웨이브(최대 26마리)에서는 두 저장이
// 같은 캐시 라인 안에 들어가 차이가 나타나지 않는다.
void ReportStoreCost() {
    auto run = [](EntityStoreKind kind, int ticks) {
        Switches switches;
        switches.entityStore = kind;
        switches.scale = ScaleKind::Large;
        GameWorld world;
        world.Configure(switches);
        const DefenseMap map = DefenseMap::CreateDefault();
        std::vector<WaveDef> waves(4, WaveDef{9000, 900.0f, 4.0f, 1, 0.0f, EnemyType::Normal});
        world.Init(map, waves, 4000, 1e9f);
        for (const DefenseMap::Cell& cell : kPlan)
            world.PlaceTowerType(map.CellCenter(cell.x, cell.y), TowerType::Gatling);
        for (int i = 0; i < 120; ++i) world.Step(1.0f / 60.0f);   // 상한까지 채운다
        const size_t alive = world.Enemies().Size();
        // 계측 구간만 남긴다 — 워밍업까지 섞으면 시스템별 귀속이 흐려진다.
        profiler::Profiler::Get().Reset();
        const auto begin = std::chrono::steady_clock::now();
        for (int i = 0; i < ticks; ++i) world.Step(1.0f / 60.0f);
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - begin).count();
        const char* label = kind == EntityStoreKind::Ecs ? "ecs"
                          : kind == EntityStoreKind::Soa ? "soa" : "aos";
        std::printf("  %-4s enemies=%zu ticks=%d total=%.1fms"
                    "  advance=%.2fms fire=%.2fms projectile=%.2fms cleanup=%.2fms\n",
                    label, alive, ticks, ms,
                    ScopeTotal("sim.enemy_advance"), ScopeTotal("sim.tower_fire"),
                    ScopeTotal("sim.projectile"), ScopeTotal("sim.cleanup"));
        return world.StateHash();
    };
    const int ticks = 600;
    std::printf("[entity_store scale=large]\n");
    const uint64_t aosHash = run(EntityStoreKind::Aos, ticks);
    const uint64_t soaHash = run(EntityStoreKind::Soa, ticks);
    const uint64_t ecsHash = run(EntityStoreKind::Ecs, ticks);
    CHECK_EQ(aosHash, soaHash);        // 잰 세 실행이 같은 세계였다
    CHECK_EQ(aosHash, ecsHash);
}

}  // namespace

static void RunTests() {
    TestTargetingAgreesWithOracle();
    TestRigColumnCost();
    TestCapacityBounds();
    TestStoresAgreeOverALongMatch();
    ReportStoreCost();
}

TEST_MAIN("entity_store_ab")
