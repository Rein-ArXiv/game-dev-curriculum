// switches — 이 제품의 A/B 스위치 레지스트리.
//
// 같은 역할을 하는 구현이 둘 있을 때, 하나를 지우면 비교가 사라진다. 지우지 않고
// 둘 다 제품에 두되 **런타임에 고를 수 있게** 하면 "같은 결과 + 다른 비용"을 매번
// 다시 잴 수 있다. 이 헤더가 그 고르기의 유일한 입구다.
//
// 규약(이 파일이 그 규약의 소유자다):
//   1. 이름은 소문자 스네이크 한 단어(entity_store · id_map · frame_alloc …).
//   2. **기본값은 언제나 스위치가 생기기 전의 동작**이다. 스위치를 추가한다는
//      이유로 기존 실행의 결과가 달라지면 그것은 스위치가 아니라 변경이다.
//   3. 출처는 셋이고 뒤가 앞을 덮는다: 기본값 → 환경 변수 `MEGA_AB` →
//      명령줄 `--ab name=value`. 환경 변수를 두는 이유는 프로세스를 띄우는 쪽이
//      실행 파일의 인자를 손댈 수 없는 경우(테스트 하네스·CI 러너)가 있기 때문이다.
//   4. 알 수 없는 이름·값은 **조용히 무시하지 않고 거절**한다. 오타 난 스위치가
//      무시되면 두 팔이 같은 코드를 돌면서 "차이가 없다"는 거짓 결론이 나온다.
//   5. 판정은 두 단계다 — (a) 두 팔의 결과가 같다는 단언, (b) 두 팔의 비용 수치.
//      (a) 없는 (b)는 의미가 없다. 빨라진 쪽이 다른 답을 냈을 수 있기 때문이다.
//
// 여기 있는 것은 이름·파싱·표시뿐이고, 각 스위치가 무엇을 바꾸는지는 그 스위치를
// 읽는 코드가 소유한다.

#pragma once

#include "live/feature_flag.hpp"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace mega {
namespace game {

// 적 저장 배치. Aos = std::vector<Enemy>, Soa = 필드별 열 배열,
// Ecs = 컴포넌트 조합별 archetype 컬럼(ecs/world.h).
enum class EntityStoreKind : uint8_t { Aos, Soa, Ecs };
// id → 행 대응. Linear = 전수 스캔, Hash = 개방 주소법 해시맵.
enum class IdMapKind : uint8_t { Linear, Hash };
// 프레임 수명 스크래치. Malloc = 요청마다 operator new, Arena = bump + Rewind.
enum class FrameAllocKind : uint8_t { Malloc, Arena };
// 매치 수명 가변 크기 버퍼. Malloc = operator new, Slab = 크기 클래스 freelist.
enum class BlobAllocKind : uint8_t { Malloc, Slab };
// 발사체 저장. Vector = std::vector + 압축, Pool = 고정 블록 풀 + intrusive 목록.
enum class BodyAllocKind : uint8_t { Vector, Pool };
// 잡 스케줄러 내부 잠금. Std = std::mutex/condition_variable, Futex = sync/lock.h.
enum class WorkerLockKind : uint8_t { Std, Futex };
// 에셋 리로드 경로. Sync = 프레임 스레드에서 직접 읽기, Async = io/async_io.h.
enum class AssetIoKind : uint8_t { Sync, Async };
// 규모. Small = 적 상한 64(기본 콘텐츠 그대로), Large = 상한 2048 + 웨이브 증량.
enum class ScaleKind : uint8_t { Small, Large };
// 맵의 출처. Classic = assets/level.txt 의 무분기 회랑, Procgen = 미로 + 밀도장.
enum class MapSource : uint8_t { Classic, Procgen };
// 시점. Ortho2d = 화면 좌표 = 월드 좌표(항등), Tilted3d = 원근 + 기울기 + 픽킹.
enum class CameraKind : uint8_t { Ortho2d, Tilted3d };
// 사거리 안 적 질의. Linear = 전수 스캔, Grid = 균일 격자, Quadtree = 쿼드트리.
enum class TargetIndexKind : uint8_t { Linear, Grid, Quadtree };
// 정적 기하(바위·안착 잔해) 질의. Naive = 전수 셀 순회, Bvh = math/spatial/bvh.
enum class StaticQueryKind : uint8_t { Naive, Bvh };
// 잔해 강체의 광역 탐색. Naive = 전수 짝짓기, Grid = physics/world 의 균일 격자.
enum class BroadphaseKind : uint8_t { Naive, Grid };
// 잔해 적분기. Symplectic = 속도 먼저, Explicit = 갱신 전 속도로 위치를 옮긴다.
enum class IntegratorKind : uint8_t { Symplectic, Explicit };
// 보스 표시. Sprite = enemy_boss.ppm 한 장, Rig = 본 계층 + 클립 + 2본 IK 로
// 그린 다족 개체. **표시만** 바꾼다 — 스톰프는 어느 팔에서도 똑같이 돈다.
enum class BossRenderKind : uint8_t { Sprite, Rig };
// 적이 길을 얻는 방법. Polyline = 맵이 계산해 준 경로 배열 하나를 전원이 밟는다,
// AStar = 개체마다 통행 격자 위에서 경로를 찾아 캐시한다, FlowField = 코어로
// 향하는 벡터장을 한 장 지어 전원이 읽는다.
enum class PathingKind : uint8_t { Polyline, AStar, FlowField };
// 정예(보스)의 행동 표현. Plain = 코어로 걷기만 한다(스위치 이전의 동작),
// Fsm = 상태와 타이머로 쓴 같은 행동, Bt = 우선순위 트리로 쓴 같은 행동.
enum class EliteBrainKind : uint8_t { Plain, Fsm, Bt };
// 스냅샷을 어떻게 싣는가. Full = 매 장 전체(스위치 이전의 동작), Delta =
// 클라이언트가 확인해 준 기준선 대비 차분. **두 팔의 디코드 결과는 비트
// 동일해야 하고**, 다른 것은 바이트 수뿐이다.
enum class SnapshotMode : uint8_t { Full, Delta };
// 보간 지연·재전송 주기의 출처. Fixed = 상수 3.0틱/100ms(스위치 이전의 동작),
// Rtt = net::RttSampler 가 잰 왕복·지터에서 net::AdaptiveTiming 이 계산한 값.
enum class InterpDelayKind : uint8_t { Fixed, Rtt };

// snippet-begin: switch-table
struct Switches {
    EntityStoreKind entityStore = EntityStoreKind::Aos;
    IdMapKind       idMap       = IdMapKind::Linear;
    FrameAllocKind  frameAlloc  = FrameAllocKind::Malloc;
    BlobAllocKind   blobAlloc   = BlobAllocKind::Malloc;
    BodyAllocKind   bodyAlloc   = BodyAllocKind::Vector;
    WorkerLockKind  workerLock  = WorkerLockKind::Std;
    AssetIoKind     assetIo     = AssetIoKind::Sync;
    ScaleKind       scale       = ScaleKind::Small;
    MapSource       map         = MapSource::Classic;
    CameraKind      camera      = CameraKind::Ortho2d;
    TargetIndexKind targetIndex = TargetIndexKind::Linear;
    StaticQueryKind staticQuery = StaticQueryKind::Naive;
    BroadphaseKind  broadphase  = BroadphaseKind::Naive;
    IntegratorKind  integrator  = IntegratorKind::Symplectic;
    BossRenderKind  bossRender  = BossRenderKind::Sprite;
    PathingKind     pathing     = PathingKind::Polyline;
    EliteBrainKind  eliteBrain  = EliteBrainKind::Plain;
    SnapshotMode    snapshotMode = SnapshotMode::Full;
    InterpDelayKind interpDelay  = InterpDelayKind::Fixed;
    bool            steering    = false;  // 격자 방향에 조향을 얹는가
    bool            stealth     = false;  // 은신 규칙(Stalker + 레이더)을 켜는가
    bool            avatarPredict = false;  // 클라가 자기 아바타를 예측·되감는가
    bool            avatarGate    = false;  // 건설이 아바타 사거리 안으로 묶이는가
    int             solverIters = 8;      // 잔해 접촉 솔버 속도 반복 횟수
    int             mapSeed     = 7;      // 절차 맵 시드(map=procgen 일 때만 읽는다)
    int             jobs        = 0;      // 워커 수. 0 = 직렬(기준선)
    bool            affinity    = false;  // 워커를 코어에 고정하는가

    // 명시적으로 지정된 스위치 이름(쉼표로 이어 붙인다). 서버가 내려보낸
    // 플래그가 이것을 덮지 못하게 하는 유일한 장치이고, 그래서 테스트는 언제나
    // 팔을 이긴다 — 팔을 강제로 고를 수 없으면 A/B 는 검사할 수 없다.
    std::string explicitNames;
    bool WasSetExplicitly(std::string_view name) const;
    // CLI·환경 변수 경로 전용. Set 과 같은 일을 하고 이름을 "명시됨"으로 기록한다.
    bool SetExplicit(std::string_view name, std::string_view value, std::string* error = nullptr);
    // 서버가 내려보낸 배정을 적용한다. 명시된 이름은 건드리지 않는다.
    // 반환값은 실제로 적용된 개수, rejected 에는 모르는 이름·값의 사유가 쌓인다.
    int ApplyFlags(const live::FlagSet& flags, std::string* rejected = nullptr);

    // 27개 스위치 **전부**를 `ParseList` 가 그대로 되먹일 수 있는 형태로 적는다.
    // Describe 와 셋이 다르다 — 구분자가 쉼표이고, `map_seed` 가 독립 항목이며,
    // `explicit=` 꼬리가 없다.
    //
    // 왜 전부인가. 리플레이는 팔을 통째로 고정해야 한다. 명시된 것만 적으면
    // 나머지는 재생 시점의 `assets/flags.txt` 배정을 받게 되고, 그러면 운영자가
    // 롤아웃 하나를 바꾸는 순간 어제 기록한 리플레이가 오늘 갈린다 — 그 갈림은
    // 결정성 결함이 아닌데 결정성 게이트가 실패로 보고한다. 전부 적어 두면
    // `ApplyFlags` 의 `WasSetExplicitly` 가 그 배정을 통째로 막는다.
    std::string Assignments() const;

    // 이름 하나에 값 하나. 모르는 이름이나 허용되지 않은 값이면 false 와 사유.
    bool Set(std::string_view name, std::string_view value, std::string* error = nullptr);
    // "name=value" 한 덩어리.
    bool Parse(std::string_view assignment, std::string* error = nullptr);
    // 쉼표로 이어 붙인 여러 덩어리("a=b,c=d"). 빈 문자열은 성공(변경 없음).
    bool ParseList(std::string_view list, std::string* error = nullptr);
    // 환경 변수 MEGA_AB 를 읽어 적용한다.
    bool LoadEnvironment(std::string* error = nullptr);

    // 로그 한 줄. 두 팔의 수치를 나란히 적을 때 어느 팔인지 남기는 용도다.
    std::string Describe() const;
};
// snippet-end: switch-table

inline bool Switches::Set(std::string_view name, std::string_view value, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = std::string(name) + "=" + std::string(value) + ": " + why;
        return false;
    };
    if (name == "entity_store") {
        if (value == "aos") entityStore = EntityStoreKind::Aos;
        else if (value == "soa") entityStore = EntityStoreKind::Soa;
        else if (value == "ecs") entityStore = EntityStoreKind::Ecs;
        else return fail("aos|soa|ecs 만 허용");
        return true;
    }
    if (name == "id_map") {
        if (value == "linear") idMap = IdMapKind::Linear;
        else if (value == "hash") idMap = IdMapKind::Hash;
        else return fail("linear|hash 만 허용");
        return true;
    }
    if (name == "frame_alloc") {
        if (value == "malloc") frameAlloc = FrameAllocKind::Malloc;
        else if (value == "arena") frameAlloc = FrameAllocKind::Arena;
        else return fail("malloc|arena 만 허용");
        return true;
    }
    if (name == "blob_alloc") {
        if (value == "malloc") blobAlloc = BlobAllocKind::Malloc;
        else if (value == "slab") blobAlloc = BlobAllocKind::Slab;
        else return fail("malloc|slab 만 허용");
        return true;
    }
    if (name == "body_alloc") {
        if (value == "vector") bodyAlloc = BodyAllocKind::Vector;
        else if (value == "pool") bodyAlloc = BodyAllocKind::Pool;
        else return fail("vector|pool 만 허용");
        return true;
    }
    if (name == "worker_lock") {
        if (value == "std") workerLock = WorkerLockKind::Std;
        else if (value == "futex") workerLock = WorkerLockKind::Futex;
        else return fail("std|futex 만 허용");
        return true;
    }
    if (name == "asset_io") {
        if (value == "sync") assetIo = AssetIoKind::Sync;
        else if (value == "async") assetIo = AssetIoKind::Async;
        else return fail("sync|async 만 허용");
        return true;
    }
    if (name == "scale") {
        if (value == "small") scale = ScaleKind::Small;
        else if (value == "large") scale = ScaleKind::Large;
        else return fail("small|large 만 허용");
        return true;
    }
    if (name == "map") {
        if (value == "classic") map = MapSource::Classic;
        else if (value == "procgen") map = MapSource::Procgen;
        else return fail("classic|procgen 만 허용");
        return true;
    }
    if (name == "camera") {
        if (value == "ortho2d") camera = CameraKind::Ortho2d;
        else if (value == "tilted3d") camera = CameraKind::Tilted3d;
        else return fail("ortho2d|tilted3d 만 허용");
        return true;
    }
    if (name == "target_index") {
        if (value == "linear") targetIndex = TargetIndexKind::Linear;
        else if (value == "grid") targetIndex = TargetIndexKind::Grid;
        else if (value == "quadtree") targetIndex = TargetIndexKind::Quadtree;
        else return fail("linear|grid|quadtree 만 허용");
        return true;
    }
    if (name == "static_query") {
        if (value == "naive") staticQuery = StaticQueryKind::Naive;
        else if (value == "bvh") staticQuery = StaticQueryKind::Bvh;
        else return fail("naive|bvh 만 허용");
        return true;
    }
    if (name == "broadphase") {
        if (value == "naive") broadphase = BroadphaseKind::Naive;
        else if (value == "grid") broadphase = BroadphaseKind::Grid;
        else return fail("naive|grid 만 허용");
        return true;
    }
    if (name == "integrator") {
        if (value == "symplectic") integrator = IntegratorKind::Symplectic;
        else if (value == "explicit") integrator = IntegratorKind::Explicit;
        else return fail("symplectic|explicit 만 허용");
        return true;
    }
    if (name == "boss_render") {
        if (value == "sprite") bossRender = BossRenderKind::Sprite;
        else if (value == "rig") bossRender = BossRenderKind::Rig;
        else return fail("sprite|rig 만 허용");
        return true;
    }
    if (name == "pathing") {
        if (value == "poly") pathing = PathingKind::Polyline;
        else if (value == "astar") pathing = PathingKind::AStar;
        else if (value == "flowfield") pathing = PathingKind::FlowField;
        else return fail("poly|astar|flowfield 만 허용");
        return true;
    }
    if (name == "elite_brain") {
        if (value == "plain") eliteBrain = EliteBrainKind::Plain;
        else if (value == "fsm") eliteBrain = EliteBrainKind::Fsm;
        else if (value == "bt") eliteBrain = EliteBrainKind::Bt;
        else return fail("plain|fsm|bt 만 허용");
        return true;
    }
    if (name == "snapshot_mode") {
        if (value == "full") snapshotMode = SnapshotMode::Full;
        else if (value == "delta") snapshotMode = SnapshotMode::Delta;
        else return fail("full|delta 만 허용");
        return true;
    }
    if (name == "interp_delay") {
        if (value == "fixed") interpDelay = InterpDelayKind::Fixed;
        else if (value == "rtt") interpDelay = InterpDelayKind::Rtt;
        else return fail("fixed|rtt 만 허용");
        return true;
    }
    if (name == "avatar_predict") {
        if (value == "0") avatarPredict = false;
        else if (value == "1") avatarPredict = true;
        else return fail("0|1 만 허용");
        return true;
    }
    if (name == "avatar_gate") {
        if (value == "0") avatarGate = false;
        else if (value == "1") avatarGate = true;
        else return fail("0|1 만 허용");
        return true;
    }
    if (name == "steering") {
        if (value == "0") steering = false;
        else if (value == "1") steering = true;
        else return fail("0|1 만 허용");
        return true;
    }
    if (name == "stealth") {
        if (value == "0") stealth = false;
        else if (value == "1") stealth = true;
        else return fail("0|1 만 허용");
        return true;
    }
    if (name == "solver_iters") {
        // jobs 와 같은 이유로 자유 정수를 받지 않는다 — "반복을 올리면 관통이
        // 단조 감소한다"는 단언이 어느 세 값에서 재졌는지가 실행마다 달라진다.
        if (value == "4") solverIters = 4;
        else if (value == "8") solverIters = 8;
        else if (value == "16") solverIters = 16;
        else return fail("4|8|16 만 허용");
        return true;
    }
    if (name == "map_seed") {
        if (value.size() == 1 && value[0] >= '1' && value[0] <= '8')
            mapSeed = value[0] - '0';
        else return fail("1..8 만 허용");
        return true;
    }
    if (name == "jobs") {
        // 자유 정수를 받지 않는다. 워커 수가 결과를 바꾸지 않는다는 단언은 이
        // 다섯 값으로 하고, 그 밖의 값은 측정 대상이 아니다 — 허용 집합을 넓히면
        // "어떤 워커 수에서 재 봤는가"가 실행마다 달라진다.
        if (value == "0") jobs = 0;
        else if (value == "1") jobs = 1;
        else if (value == "2") jobs = 2;
        else if (value == "4") jobs = 4;
        else if (value == "8") jobs = 8;
        else return fail("0|1|2|4|8 만 허용");
        return true;
    }
    if (name == "affinity") {
        if (value == "0") affinity = false;
        else if (value == "1") affinity = true;
        else return fail("0|1 만 허용");
        return true;
    }
    if (error) *error = "알 수 없는 스위치 이름: " + std::string(name);
    return false;
}

inline bool Switches::WasSetExplicitly(std::string_view name) const {
    size_t begin = 0;
    while (begin < explicitNames.size()) {
        size_t comma = explicitNames.find(',', begin);
        if (comma == std::string::npos) comma = explicitNames.size();
        if (std::string_view(explicitNames).substr(begin, comma - begin) == name) return true;
        begin = comma + 1;
    }
    return false;
}

inline bool Switches::SetExplicit(std::string_view name, std::string_view value,
                                  std::string* error) {
    if (!Set(name, value, error)) return false;
    if (!WasSetExplicitly(name)) {
        if (!explicitNames.empty()) explicitNames += ',';
        explicitNames.append(name);
    }
    return true;
}

// snippet-begin: flag-source
inline int Switches::ApplyFlags(const live::FlagSet& flags, std::string* rejected) {
    int applied = 0;
    for (const live::FlagAssignment& item : flags.All()) {
        if (WasSetExplicitly(item.name)) continue;   // CLI·환경 변수가 언제나 이긴다
        std::string reason;
        if (Set(item.name, item.arm, &reason)) { ++applied; continue; }
        // 모르는 이름·값은 기본 팔로 남는다. 크래시하지 않는 것이 계약이다 —
        // 운영자가 내려보낸 오타 하나가 붙어 있는 모든 클라이언트를 죽이면
        // 그것은 플래그가 아니라 원격 배포다.
        if (rejected) {
            if (!rejected->empty()) *rejected += " / ";
            *rejected += reason;
        }
    }
    return applied;
}
// snippet-end: flag-source

inline bool Switches::Parse(std::string_view assignment, std::string* error) {
    const size_t equals = assignment.find('=');
    if (equals == std::string_view::npos || equals == 0 || equals + 1 == assignment.size()) {
        if (error) *error = "형식은 name=value 다: " + std::string(assignment);
        return false;
    }
    // SetExplicit 이다. --ab 와 MEGA_AB 는 둘 다 ParseList → Parse 를 지나므로,
    // 이 한 줄이 두 출처를 모두 "명시됨"으로 표시한다.
    return SetExplicit(assignment.substr(0, equals), assignment.substr(equals + 1), error);
}

inline bool Switches::ParseList(std::string_view list, std::string* error) {
    size_t begin = 0;
    while (begin <= list.size()) {
        size_t comma = list.find(',', begin);
        if (comma == std::string_view::npos) comma = list.size();
        std::string_view item = list.substr(begin, comma - begin);
        if (!item.empty() && !Parse(item, error)) return false;
        if (comma == list.size()) break;
        begin = comma + 1;
    }
    return true;
}

inline bool Switches::LoadEnvironment(std::string* error) {
    const char* raw = std::getenv("MEGA_AB");
    if (!raw || !*raw) return true;
    return ParseList(raw, error);
}

inline std::string Switches::Describe() const {
    std::string out;
    out += "entity_store=";
    out += entityStore == EntityStoreKind::Ecs ? "ecs"
         : entityStore == EntityStoreKind::Soa ? "soa" : "aos";
    out += " id_map=";
    out += idMap == IdMapKind::Hash ? "hash" : "linear";
    out += " frame_alloc=";
    out += frameAlloc == FrameAllocKind::Arena ? "arena" : "malloc";
    out += " blob_alloc=";
    out += blobAlloc == BlobAllocKind::Slab ? "slab" : "malloc";
    out += " body_alloc=";
    out += bodyAlloc == BodyAllocKind::Pool ? "pool" : "vector";
    out += " worker_lock=";
    out += workerLock == WorkerLockKind::Futex ? "futex" : "std";
    out += " asset_io=";
    out += assetIo == AssetIoKind::Async ? "async" : "sync";
    out += " scale=";
    out += scale == ScaleKind::Large ? "large" : "small";
    out += " map=";
    out += map == MapSource::Procgen ? "procgen" : "classic";
    if (map == MapSource::Procgen) out += "/" + std::to_string(mapSeed);
    out += " camera=";
    out += camera == CameraKind::Tilted3d ? "tilted3d" : "ortho2d";
    out += " target_index=";
    out += targetIndex == TargetIndexKind::Quadtree ? "quadtree"
         : targetIndex == TargetIndexKind::Grid     ? "grid" : "linear";
    out += " static_query=";
    out += staticQuery == StaticQueryKind::Bvh ? "bvh" : "naive";
    out += " broadphase=";
    out += broadphase == BroadphaseKind::Grid ? "grid" : "naive";
    out += " integrator=";
    out += integrator == IntegratorKind::Explicit ? "explicit" : "symplectic";
    out += " boss_render=";
    out += bossRender == BossRenderKind::Rig ? "rig" : "sprite";
    out += " pathing=";
    out += pathing == PathingKind::FlowField ? "flowfield"
         : pathing == PathingKind::AStar     ? "astar" : "poly";
    out += " elite_brain=";
    out += eliteBrain == EliteBrainKind::Bt ? "bt"
         : eliteBrain == EliteBrainKind::Fsm ? "fsm" : "plain";
    out += " snapshot_mode=";
    out += snapshotMode == SnapshotMode::Delta ? "delta" : "full";
    out += " interp_delay=";
    out += interpDelay == InterpDelayKind::Rtt ? "rtt" : "fixed";
    out += " avatar_predict=";
    out += avatarPredict ? "1" : "0";
    out += " avatar_gate=";
    out += avatarGate ? "1" : "0";
    out += " steering=";
    out += steering ? "1" : "0";
    out += " stealth=";
    out += stealth ? "1" : "0";
    out += " solver_iters=" + std::to_string(solverIters);
    out += " jobs=" + std::to_string(jobs);
    out += " affinity=";
    out += affinity ? "1" : "0";
    // 어느 출처에서 왔는지가 로그에 남아야 두 팔의 수치에 출처가 생긴다.
    if (!explicitNames.empty()) out += " explicit=" + explicitNames;
    return out;
}

// snippet-begin: switch-assignments
inline std::string Switches::Assignments() const {
    std::string out;
    auto put = [&out](const char* name, const std::string& value) {
        if (!out.empty()) out += ',';
        out += name;
        out += '=';
        out += value;
    };
    put("entity_store", entityStore == EntityStoreKind::Ecs ? "ecs"
                      : entityStore == EntityStoreKind::Soa ? "soa" : "aos");
    put("id_map", idMap == IdMapKind::Hash ? "hash" : "linear");
    put("frame_alloc", frameAlloc == FrameAllocKind::Arena ? "arena" : "malloc");
    put("blob_alloc", blobAlloc == BlobAllocKind::Slab ? "slab" : "malloc");
    put("body_alloc", bodyAlloc == BodyAllocKind::Pool ? "pool" : "vector");
    put("worker_lock", workerLock == WorkerLockKind::Futex ? "futex" : "std");
    put("asset_io", assetIo == AssetIoKind::Async ? "async" : "sync");
    put("scale", scale == ScaleKind::Large ? "large" : "small");
    put("map", map == MapSource::Procgen ? "procgen" : "classic");
    put("map_seed", std::to_string(mapSeed));
    put("camera", camera == CameraKind::Tilted3d ? "tilted3d" : "ortho2d");
    put("target_index", targetIndex == TargetIndexKind::Quadtree ? "quadtree"
                      : targetIndex == TargetIndexKind::Grid     ? "grid" : "linear");
    put("static_query", staticQuery == StaticQueryKind::Bvh ? "bvh" : "naive");
    put("broadphase", broadphase == BroadphaseKind::Grid ? "grid" : "naive");
    put("integrator", integrator == IntegratorKind::Explicit ? "explicit" : "symplectic");
    put("solver_iters", std::to_string(solverIters));
    put("boss_render", bossRender == BossRenderKind::Rig ? "rig" : "sprite");
    put("pathing", pathing == PathingKind::FlowField ? "flowfield"
                 : pathing == PathingKind::AStar     ? "astar" : "poly");
    put("elite_brain", eliteBrain == EliteBrainKind::Bt ? "bt"
                     : eliteBrain == EliteBrainKind::Fsm ? "fsm" : "plain");
    put("snapshot_mode", snapshotMode == SnapshotMode::Delta ? "delta" : "full");
    put("interp_delay", interpDelay == InterpDelayKind::Rtt ? "rtt" : "fixed");
    put("steering", steering ? "1" : "0");
    put("stealth", stealth ? "1" : "0");
    put("avatar_predict", avatarPredict ? "1" : "0");
    put("avatar_gate", avatarGate ? "1" : "0");
    put("jobs", std::to_string(jobs));
    put("affinity", affinity ? "1" : "0");
    return out;
}
// snippet-end: switch-assignments

}  // namespace game
}  // namespace mega
