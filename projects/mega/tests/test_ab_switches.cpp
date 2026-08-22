// A/B 스위치 규약 자체를 지킨다.
//
// 이 파일이 지키는 것은 성능이 아니라 **비교의 유효성**이다. 오타 난 스위치가
// 조용히 무시되면 두 팔이 같은 코드를 돌면서 "차이가 없다"는 거짓 결론이 나오고,
// 기본값이 스위치 이전과 다르면 앞선 편의 실측 출력이 전부 재현되지 않는다.

#include "game/switches.hpp"
#include "test_util.h"

#include <string>

using namespace mega::game;

static void TestDefaultsAreThePreSwitchBehaviour() {
    Switches s;
    CHECK(s.entityStore == EntityStoreKind::Aos);
    CHECK(s.idMap == IdMapKind::Linear);
    CHECK(s.frameAlloc == FrameAllocKind::Malloc);
    CHECK(s.blobAlloc == BlobAllocKind::Malloc);
    CHECK(s.bodyAlloc == BodyAllocKind::Vector);
    CHECK(s.workerLock == WorkerLockKind::Std);
    CHECK(s.assetIo == AssetIoKind::Sync);
    CHECK(s.scale == ScaleKind::Small);
    CHECK(s.map == MapSource::Classic);
    CHECK(s.camera == CameraKind::Ortho2d);
    CHECK(s.targetIndex == TargetIndexKind::Linear);
    CHECK(s.staticQuery == StaticQueryKind::Naive);
    CHECK(s.broadphase == BroadphaseKind::Naive);
    CHECK(s.integrator == IntegratorKind::Symplectic);
    CHECK(s.bossRender == BossRenderKind::Sprite);
    CHECK_EQ(s.solverIters, 8);
    CHECK_EQ(s.mapSeed, 7);
    CHECK_EQ(s.jobs, 0);
    CHECK(!s.affinity);
}

static void TestEveryNameParses() {
    Switches s;
    std::string error;
    CHECK(s.Parse("entity_store=soa", &error));
    CHECK(s.Parse("id_map=hash", &error));
    CHECK(s.Parse("frame_alloc=arena", &error));
    CHECK(s.Parse("blob_alloc=slab", &error));
    CHECK(s.Parse("body_alloc=pool", &error));
    CHECK(s.Parse("worker_lock=futex", &error));
    CHECK(s.Parse("asset_io=async", &error));
    CHECK(s.Parse("scale=large", &error));
    CHECK(s.Parse("map=procgen", &error));
    CHECK(s.Parse("map_seed=3", &error));
    CHECK(s.Parse("camera=tilted3d", &error));
    CHECK(s.Parse("target_index=quadtree", &error));
    CHECK(s.Parse("static_query=bvh", &error));
    CHECK(s.Parse("broadphase=grid", &error));
    CHECK(s.Parse("integrator=explicit", &error));
    CHECK(s.Parse("boss_render=rig", &error));
    CHECK(s.Parse("solver_iters=16", &error));
    CHECK(s.Parse("jobs=8", &error));
    CHECK(s.Parse("affinity=1", &error));
    CHECK(s.entityStore == EntityStoreKind::Soa);
    CHECK(s.idMap == IdMapKind::Hash);
    CHECK(s.frameAlloc == FrameAllocKind::Arena);
    CHECK(s.blobAlloc == BlobAllocKind::Slab);
    CHECK(s.bodyAlloc == BodyAllocKind::Pool);
    CHECK(s.workerLock == WorkerLockKind::Futex);
    CHECK(s.assetIo == AssetIoKind::Async);
    CHECK(s.scale == ScaleKind::Large);
    CHECK(s.map == MapSource::Procgen);
    CHECK_EQ(s.mapSeed, 3);
    CHECK(s.camera == CameraKind::Tilted3d);
    CHECK(s.targetIndex == TargetIndexKind::Quadtree);
    CHECK(s.staticQuery == StaticQueryKind::Bvh);
    CHECK(s.broadphase == BroadphaseKind::Grid);
    CHECK(s.integrator == IntegratorKind::Explicit);
    CHECK(s.bossRender == BossRenderKind::Rig);
    CHECK_EQ(s.solverIters, 16);
    CHECK_EQ(s.jobs, 8);
    CHECK(s.affinity);
    // Describe 는 로그의 출처 표시다. 켠 값이 그대로 보여야 한다.
    const std::string described = s.Describe();
    CHECK(described.find("entity_store=soa") != std::string::npos);
    CHECK(described.find("jobs=8") != std::string::npos);
    CHECK(described.find("target_index=quadtree") != std::string::npos);
    CHECK(described.find("camera=tilted3d") != std::string::npos);
    CHECK(described.find("map=procgen/3") != std::string::npos);
    CHECK(described.find("solver_iters=16") != std::string::npos);
    CHECK(described.find("boss_render=rig") != std::string::npos);
    // 세 번째 팔도 로그에 그대로 남아야 한다.
    Switches ecs;
    CHECK(ecs.Parse("entity_store=ecs", &error));
    CHECK(ecs.entityStore == EntityStoreKind::Ecs);
    CHECK(ecs.Describe().find("entity_store=ecs") != std::string::npos);
}

// 모르는 이름·값은 거절이지 무시가 아니다. 무시하면 오타 난 팔이 기준선과 같은
// 코드를 돌면서 "개선 없음"을 보고한다.
static void TestUnknownIsRejected() {
    Switches s;
    std::string error;
    CHECK(!s.Parse("entity_stroe=soa", &error));
    CHECK(error.find("entity_stroe") != std::string::npos);
    CHECK(!s.Parse("entity_store=column", &error));
    CHECK(error.find("aos|soa|ecs") != std::string::npos);
    CHECK(!s.Parse("jobs=3", &error));        // 허용 집합 밖
    CHECK(!s.Parse("solver_iters=7", &error)); // 세 값만 잰다
    CHECK(!s.Parse("map_seed=0", &error));
    CHECK(!s.Parse("map_seed=9", &error));
    CHECK(!s.Parse("camera=isometric", &error));
    CHECK(!s.Parse("target_index=octree", &error));
    CHECK(!s.Parse("static_query=kdtree", &error));
    CHECK(!s.Parse("broadphase=sap", &error));
    CHECK(!s.Parse("integrator=rk4", &error));
    CHECK(!s.Parse("boss_render=skeleton", &error));
    CHECK(!s.Parse("entity_store", &error));  // 형식 위반
    CHECK(!s.Parse("=soa", &error));
    CHECK(!s.Parse("entity_store=", &error));
    // 거절된 파싱은 상태를 바꾸지 않는다.
    CHECK(s.entityStore == EntityStoreKind::Aos);
    CHECK_EQ(s.jobs, 0);
}

static void TestListAndOverride() {
    Switches s;
    std::string error;
    CHECK(s.ParseList("entity_store=soa,jobs=4,id_map=hash", &error));
    CHECK(s.entityStore == EntityStoreKind::Soa);
    CHECK_EQ(s.jobs, 4);
    CHECK(s.idMap == IdMapKind::Hash);
    CHECK(s.ParseList("", &error));           // 빈 목록은 변경 없는 성공
    CHECK_EQ(s.jobs, 4);
    // 뒤가 앞을 덮는다(환경 변수 → 명령줄 순서가 이 규칙 위에 있다).
    CHECK(s.Parse("jobs=2", &error));
    CHECK_EQ(s.jobs, 2);
    CHECK(!s.ParseList("jobs=4,bogus=1", &error));
    CHECK_EQ(s.jobs, 4);                      // 목록은 앞에서부터 적용되다 멈춘다
}

static void RunTests() {
    TestDefaultsAreThePreSwitchBehaviour();
    TestEveryNameParses();
    TestUnknownIsRejected();
    TestListAndOverride();
}

TEST_MAIN("ab_switches")
