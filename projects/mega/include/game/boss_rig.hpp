// boss_rig — 보스의 본 계층 · 저작 클립 · 2본 IK, 그리고 그 발이 규칙에 닿는 자리.
//
// 15편까지 보스는 `enemy_boss.ppm` 한 장이었다. 스프라이트는 위치를 갖지만
// **자세**를 갖지 못하고, 자세가 없으면 "발이 어디를 디뎠는가"라는 질문이
// 성립하지 않는다. 이 파일이 그 질문을 만든다.
//
// 세 소스가 한 포즈에서 합쳐진다.
//
//   engine/skeleton  몸통 → 고관절 → 무릎 → 발 체인 네 벌. 로컬 TRS 를 월드로 합성.
//   engine/anim      assets/boss.pose 의 걷기·공격 클립을 샘플링하고 블렌딩.
//   engine/ik        고관절에서 목표까지 2본 IK. 무릎을 놓고 발을 착지시킨다.
//
// 클립이 정하는 것은 몸통의 들썩임과 **다리가 어느 쪽을 가리키는가**(고관절 요)
// 이고, 코드가 정하는 것은 **얼마나 내딛는가**(보폭)다. 목표점은 고관절 로컬
// 공간에 놓이므로 클립의 회전이 그대로 목표를 돌린다 — 두 소스가 곱해진다.
//
// 그리고 이 계산은 표시로 끝나지 않는다. 디딘 발의 월드 좌표가 스톰프 공격의
// 원점이 되어 권위 시뮬로 들어간다. 그러려면 호스트와 클라이언트가 같은 값을
// 얻어야 하고, 그래서 **입력이 전부 스냅샷 안에 있는 것들뿐**이다: 보스의 위치,
// 코어의 위치, 칸 크기. 걸음 위상조차 위치의 함수다(BossPhase) — 틱 카운터나
// 시뮬 전용 타이머를 쓰면 클라이언트가 재현할 수 없다.

#pragma once

#include "engine/anim.hpp"
#include "engine/ik.hpp"
#include "engine/skeleton.hpp"
#include "math/interp.hpp"
#include "math/quat.hpp"
#include "math/scalar.hpp"
#include "math/transform.hpp"
#include "math/vec.hpp"

#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace mega {
namespace game {

// ── 골격 상수 ──────────────────────────────────────────────────────
inline constexpr int kBossLegCount  = 4;
// 몸통 1 + 다리마다 고관절·무릎·발 셋.
inline constexpr int kBossBoneCount = 1 + kBossLegCount * 3;

// 다리 마디 길이와 보폭(칸 크기 배수).
inline constexpr float kBossThigh   = 0.42f;
inline constexpr float kBossShin    = 0.42f;
inline constexpr float kBossStance  = 0.55f;   // 고관절에서 발까지 기본 거리
inline constexpr float kBossSwing   = 0.32f;   // 그 거리의 진폭

// 고관절 위치(보스 로컬. +x = 오른쪽, +y = 진행 방향).
inline constexpr float kBossHipX[kBossLegCount] = {-0.50f, 0.50f, -0.46f, 0.46f};
inline constexpr float kBossHipY[kBossLegCount] = { 0.34f, 0.34f, -0.34f, -0.34f};

// 걸음 위상 하나(= 발 한 번 디딤)에 해당하는 이동량(px). 위상은 위치의 함수라
// 스냅샷만으로 재현된다 — 그것이 이 상수가 존재하는 유일한 이유다.
inline constexpr float kBossStride = 42.0f;

// 스톰프 한 번의 반경(칸 배수)과 피해.
inline constexpr float kStompRadiusCells = 1.5f;
inline constexpr float kStompDamage      = 14.0f;

// 걸음 위상. 바닥값이 곧 "지금까지 몇 번 디뎠는가"이고, 소수부가 그 걸음 안의
// 진행도다. (x + y) 를 쓰는 이유는 맵 어느 방향으로 걸어도 단조롭게 변하는
// 스칼라가 필요하고, 그것이 스냅샷 안에 있어야 하기 때문이다.
// snippet-begin: boss-phase
inline float BossPhase(const math::Vec2& pos) {
    return (pos.x + pos.y) * (1.0f / kBossStride);
}
// snippet-end: boss-phase

// 다리 하나의 이번 프레임 보폭. order 0 = 방금 디딘 다리.
inline float BossLegSwing(float frac, int leg, uint32_t plants) {
    const int planted = static_cast<int>(plants % static_cast<uint32_t>(kBossLegCount));
    const int order = (leg - planted + kBossLegCount) % kBossLegCount;
    const float t = (frac + static_cast<float>(order)) * (1.0f / kBossLegCount);
    return kBossSwing * std::cos(6.2831853f * t);
}

// 공격(내려찍기) 블렌드 가중치. 디딤 직전에 감아올렸다가 디디는 순간 0 이 된다.
inline float BossAttackWeight(float frac) {
    if (frac <= 0.7f) return 0.0f;
    return math::SmootherStep((frac - 0.7f) * (1.0f / 0.3f));
}

// ── 클립 ───────────────────────────────────────────────────────────
// 파일이 저작하는 자유도는 여섯이다: 몸통 들림 · 몸통 롤 · 고관절 요 넷.
// 나머지(마디 길이·부모 사슬)는 골격이 소유하고 파일이 건드릴 수 없다.
struct BossPoseKey {
    float time = 0.0f;
    float lift = 0.0f;                     // 몸통 들림(칸 배수)
    float roll = 0.0f;                     // 몸통 롤(도)
    float hip[kBossLegCount] = {0, 0, 0, 0};   // 고관절 요(도)
};

inline engine::Pose BossPoseFromKey(const BossPoseKey& key) {
    engine::Pose pose(kBossBoneCount);
    pose[0].position = {0.0f, key.lift, 0.0f};
    pose[0].rotation = math::Quat::FromAxisAngle({0.0f, 0.0f, 1.0f}, math::Radians(key.roll));
    for (int leg = 0; leg < kBossLegCount; ++leg) {
        const int hip = 1 + leg * 3;
        pose[hip].position = {kBossHipX[leg], kBossHipY[leg], 0.0f};
        pose[hip].rotation =
            math::Quat::FromAxisAngle({0.0f, 0.0f, 1.0f}, math::Radians(key.hip[leg]));
        pose[hip + 1].position = {0.0f, -kBossThigh, 0.0f};   // 무릎
        pose[hip + 2].position = {0.0f, -kBossShin, 0.0f};    // 발
    }
    return pose;
}

// 부모가 언제나 자식보다 앞선다. Skeleton::AddBone 이 그 순서를 강제한다.
// snippet-begin: boss-skeleton
inline engine::Skeleton MakeBossSkeleton() {
    engine::Skeleton skeleton;
    const int body = skeleton.AddBone(-1, math::Transform{});
    for (int leg = 0; leg < kBossLegCount; ++leg) {
        math::Transform hipBind;
        hipBind.position = {kBossHipX[leg], kBossHipY[leg], 0.0f};
        const int hip = skeleton.AddBone(body, hipBind);
        math::Transform thigh;
        thigh.position = {0.0f, -kBossThigh, 0.0f};
        const int knee = skeleton.AddBone(hip, thigh);
        math::Transform shin;
        shin.position = {0.0f, -kBossShin, 0.0f};
        skeleton.AddBone(knee, shin);
    }
    return skeleton;
}
// snippet-end: boss-skeleton

// 내장 기본 클립. 파일이 없거나 거부되면 **통째로** 이것으로 돌아간다.
inline std::vector<BossPoseKey> DefaultWalkKeys() {
    return {
        {0.00f,  0.00f,  0.0f, {-35.0f,  35.0f, -35.0f,  35.0f}},
        {0.50f, -0.05f,  4.0f, {-21.0f,  49.0f, -49.0f,  21.0f}},
        {1.00f,  0.00f,  0.0f, {-35.0f,  35.0f, -35.0f,  35.0f}},
    };
}
inline std::vector<BossPoseKey> DefaultAttackKeys() {
    return {
        {0.00f,  0.00f,  0.0f, {-35.0f,  35.0f, -35.0f,  35.0f}},
        {0.50f, -0.16f, -9.0f, {-52.0f,  52.0f, -28.0f,  28.0f}},
        {1.00f,  0.00f,  0.0f, {-35.0f,  35.0f, -35.0f,  35.0f}},
    };
}

inline engine::AnimClip ClipFromKeys(const std::vector<BossPoseKey>& keys) {
    engine::AnimClip clip;
    for (const BossPoseKey& key : keys) clip.AddKey(key.time, BossPoseFromKey(key));
    return clip;
}

// 골격 + 두 클립. 값 타입이라 월드가 사본을 들고, 클라이언트도 같은 사본을 든다.
struct BossPoses {
    engine::Skeleton skeleton = MakeBossSkeleton();
    engine::AnimClip walk     = ClipFromKeys(DefaultWalkKeys());
    engine::AnimClip attack   = ClipFromKeys(DefaultAttackKeys());

    // 걷기와 공격을 가중 블렌딩한 로컬 포즈. 두 클립이 한 포즈로 합쳐지는 지점.
    // snippet-begin: boss-sample
    engine::Pose SampleLocal(float frac, float attackWeight) const {
        const engine::Pose stride = walk.Sample(frac * walk.Duration());
        const engine::Pose slam   = attack.Sample(frac * attack.Duration());
        return engine::BlendPoses(stride, slam, math::Saturate(attackWeight));
    }
    // snippet-end: boss-sample
};

// ── 저작 파일 ──────────────────────────────────────────────────────
// 형식(공백 구분, `#` 이후는 주석):
//
//   bones 13
//   clip walk
//   key <시각> <들림> <롤도> <고관절0> <고관절1> <고관절2> <고관절3>
//   clip attack
//   key ...
//
// 신뢰 경계 밖 입력이므로 level/waves/balance 와 같은 규칙을 따른다: 한 줄이라도
// 거부되면 **파일 전체**를 버리고 내장 기본 클립으로 돌아간다. 절반만 반영하면
// 걷기는 파일이고 공격은 내장인 포즈가 나오고, 그 조합은 아무도 저작한 적이 없다.
// snippet-begin: pose-parse
inline bool ParseBossPoses(const std::string& text, BossPoses& out, std::string* diagnostic) {
    auto fail = [&](const std::string& why) {
        if (diagnostic) *diagnostic = why;
        return false;
    };
    std::vector<BossPoseKey> walk, attack;
    std::vector<BossPoseKey>* current = nullptr;
    bool bonesSeen = false;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        const size_t comment = line.find('#');
        if (comment != std::string::npos) line.erase(comment);
        std::istringstream parser(line);
        std::string word;
        if (!(parser >> word)) continue;
        if (word == "bones") {
            int count = 0;
            if (!(parser >> count) || count != kBossBoneCount)
                return fail("bones 선언이 골격의 본 수와 다르다");
            bonesSeen = true;
        } else if (word == "clip") {
            std::string name;
            if (!(parser >> name)) return fail("clip 이름이 없다");
            if (name == "walk") current = &walk;
            else if (name == "attack") current = &attack;
            else return fail("알 수 없는 클립 이름: " + name);
        } else if (word == "key") {
            if (!current) return fail("clip 선언보다 먼저 나온 key");
            BossPoseKey key;
            if (!(parser >> key.time >> key.lift >> key.roll)) return fail("key 필드가 모자라다");
            for (int leg = 0; leg < kBossLegCount; ++leg)
                if (!(parser >> key.hip[leg])) return fail("고관절 각이 모자라다");
            std::string trailing;
            if (parser >> trailing) return fail("key 뒤에 남은 토큰: " + trailing);
            // 시각은 단조 증가여야 한다. AnimClip::Sample 은 times_ 가 오름차순
            // 이라고 믿고 앞에서부터 훑으므로, 비단조 키는 조용히 건너뛰어진다.
            if (!current->empty() && !(key.time > current->back().time))
                return fail("key 시각이 단조 증가가 아니다");
            if (!(key.time >= 0.0f && key.time <= 60.0f)) return fail("key 시각이 범위 밖이다");
            if (!(key.lift >= -1.0f && key.lift <= 1.0f)) return fail("몸통 들림이 범위 밖이다");
            if (!(key.roll >= -90.0f && key.roll <= 90.0f)) return fail("몸통 롤이 범위 밖이다");
            for (int leg = 0; leg < kBossLegCount; ++leg)
                if (!(key.hip[leg] >= -180.0f && key.hip[leg] <= 180.0f))
                    return fail("고관절 각이 범위 밖이다");
            current->push_back(key);
        } else {
            return fail("알 수 없는 지시자: " + word);
        }
    }
    if (!bonesSeen) return fail("bones 선언이 없다");
    if (walk.size() < 2 || attack.size() < 2) return fail("클립마다 키가 둘 이상이어야 한다");
    if (walk.back().time <= 0.0f || attack.back().time <= 0.0f)
        return fail("클립 길이가 0이다");
    out.skeleton = MakeBossSkeleton();
    out.walk = ClipFromKeys(walk);
    out.attack = ClipFromKeys(attack);
    return true;
}
// snippet-end: pose-parse

// ── 한 프레임의 자세 ────────────────────────────────────────────────
struct BossLeg {
    math::Vec2 hip{0, 0}, knee{0, 0}, foot{0, 0};
    bool       reachable = false;
};
struct BossFrame {
    math::Vec2 body{0, 0};
    math::Vec2 forward{0, 1};
    BossLeg    legs[kBossLegCount];
    uint32_t   plants = 0;
    int        plantedLeg = 0;
    float      frac = 0.0f;
    float      attackWeight = 0.0f;
};

// snippet-begin: boss-frame
inline BossFrame SolveBossFrame(const BossPoses& poses, const math::Vec2& pos,
                                const math::Vec2& corePos, float scale) {
    BossFrame out;
    const float phase = BossPhase(pos);
    const float base = std::floor(phase);
    out.plants = base > 0.0f ? static_cast<uint32_t>(base) : 0u;
    out.frac = math::Saturate(phase - base);
    out.plantedLeg = static_cast<int>(out.plants % static_cast<uint32_t>(kBossLegCount));
    out.attackWeight = BossAttackWeight(out.frac);

    const engine::Pose local = poses.SampleLocal(out.frac, out.attackWeight);
    const engine::Pose world = poses.skeleton.ComputeWorld(local);

    math::Vec2 forward = corePos - pos;
    const float span = math::Length(forward);
    out.forward = span > 1e-4f ? forward * (1.0f / span) : math::Vec2{0.0f, 1.0f};
    const math::Vec2 right{out.forward.y, -out.forward.x};
    const math::Vec2 origin = pos;
    auto toWorld = [&](const math::Vec2& p) {
        return origin + right * (p.x * scale) + out.forward * (p.y * scale);
    };

    out.body = toWorld({world[0].position.x, world[0].position.y});
    for (int leg = 0; leg < kBossLegCount; ++leg) {
        const int hipBone = 1 + leg * 3;
        const math::Transform& hip = world[static_cast<size_t>(hipBone)];
        const math::Vec2 hipLocal{hip.position.x, hip.position.y};
        // 목표는 **고관절 로컬 공간**에 놓는다. 클립이 정한 고관절 회전이 그대로
        // 목표를 돌리므로, 저작 포즈가 다리의 방향을, 코드가 보폭을 정한다.
        const float swing = BossLegSwing(out.frac, leg, out.plants);
        const math::Vec3 targetLocal{0.0f, -(kBossStance + swing), 0.0f};
        const math::Vec3 target = hip.TransformPoint(targetLocal);
        const engine::IkResult solved = engine::SolveTwoBone2D(
            hipLocal, kBossThigh, kBossShin, {target.x, target.y}, hipLocal.x < 0.0f);
        out.legs[leg].hip = toWorld(hipLocal);
        out.legs[leg].knee = toWorld(solved.joint);
        out.legs[leg].foot = toWorld(solved.end);
        out.legs[leg].reachable = solved.reachable;
    }
    return out;
}

// 스톰프의 원점. 호스트는 시뮬에서, 클라이언트는 스냅샷에서 이 함수를 부른다.
inline math::Vec2 BossPlantedFoot(const BossPoses& poses, const math::Vec2& pos,
                                  const math::Vec2& corePos, float scale) {
    const BossFrame frame = SolveBossFrame(poses, pos, corePos, scale);
    return frame.legs[frame.plantedLeg].foot;
}
// snippet-end: boss-frame

}  // namespace game
}  // namespace mega
