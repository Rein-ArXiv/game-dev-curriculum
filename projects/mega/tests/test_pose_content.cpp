// assets/boss.pose — 신뢰 경계 밖 입력으로서의 애니메이션 클립.
//
// 이 파일은 재빌드 없이 교체할 수 있다고 광고된 파일이고, 그 말은 곧 **누구든
// 아무 바이트나 넣을 수 있다**는 뜻이다. level·waves·balance 와 같은 규칙을
// 그대로 적용한다: 한 줄이라도 거부되면 파일 전체를 버리고 내장 클립으로 돌아간다.
//
// 부분 반영을 허용하지 않는 이유는 진단 가능성이다. 걷기는 파일이고 공격은
// 내장인 포즈는 아무도 저작한 적이 없는 조합이라, 화면에서 이상한 것을 본 사람이
// 그 원인을 파일에서도 코드에서도 찾을 수 없다.
//
// 그리고 클립 자체의 계약 둘: Sample(t) 는 키 시각에서 그 키를 내고,
// BlendPoses 는 가중치를 실제로 쓴다.

#include "engine/anim.hpp"
#include "game/boss_rig.hpp"
#include "game/defense_content.hpp"
#include "io/file.h"
#include "platform/paths.hpp"
#include "test_util.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace mega;
using namespace mega::game;

namespace {

bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

const char* kValid =
    "bones 13\n"
    "clip walk\n"
    "key 0.00  0.000  0.0 -35 35 -35 35\n"
    "key 0.60 -0.040  3.0 -21 49 -49 21\n"
    "key 1.20  0.000  0.0 -35 35 -35 35\n"
    "clip attack\n"
    "key 0.00  0.000  0.0 -35 35 -35 35\n"
    "key 0.40 -0.150 -8.0 -52 52 -28 28\n";

std::string TempPath(const char* name) {
    return platform::ExecutableDirectory() + "/" + name;
}

bool WriteText(const std::string& path, const std::string& text) {
    io::File file;
    if (!file.Open(path.c_str(), io::FileMode::Write)) return false;
    return file.WriteAt(0, text.data(), text.size()).ok;
}

// 파일 전체가 버려졌는가. 내장 클립의 키 수(3)와 진단 유무로 판정한다.
void CheckWholeFileRejected(const char* name, const std::string& text) {
    const std::string path = TempPath(name);
    CHECK(WriteText(path, text));
    bool external = true;
    std::string diagnostic;
    const BossPoses poses = defense::LoadBossPoses(path, &external, &diagnostic);
    CHECK(!external);
    CHECK(!diagnostic.empty());
    // 걷기도 공격도 내장으로 돌아갔다. 한쪽만 반영되는 상태가 없다.
    CHECK_EQ(poses.walk.KeyCount(), DefaultWalkKeys().size());
    CHECK_EQ(poses.attack.KeyCount(), DefaultAttackKeys().size());
    CHECK_EQ(poses.skeleton.BoneCount(), kBossBoneCount);
    std::printf("  거부(%s): %s\n", name, diagnostic.c_str());
}

// ── 1. 정상 파일 ───────────────────────────────────────────────────
void TestValidTextLoads() {
    BossPoses poses;
    std::string diagnostic;
    CHECK(ParseBossPoses(kValid, poses, &diagnostic));
    CHECK(diagnostic.empty());
    CHECK_EQ(poses.walk.KeyCount(), static_cast<size_t>(3));
    CHECK_EQ(poses.attack.KeyCount(), static_cast<size_t>(2));
    CHECK(Near(poses.walk.Duration(), 1.20f));
    CHECK(Near(poses.attack.Duration(), 0.40f));
    CHECK_EQ(poses.skeleton.BoneCount(), kBossBoneCount);
}

// 제품이 실제로 읽는 파일. 실행 파일 옆의 assets/ 에서 온다.
void TestShippedFileLoads() {
    bool external = false;
    std::string diagnostic;
    const BossPoses poses = defense::LoadBossPoses(
        platform::ExecutableDirectory() + "/assets/boss.pose", &external, &diagnostic);
    CHECK(external);
    CHECK(diagnostic.empty());
    // 파일이 내장 클립과 **다르다**. 같으면 이 로더가 실제로 쓰이는지 알 수 없다.
    CHECK(poses.walk.KeyCount() != DefaultWalkKeys().size());
    CHECK(poses.walk.KeyCount() >= 3);
    std::printf("  배포 파일: walk %zu키 attack %zu키\n",
                poses.walk.KeyCount(), poses.attack.KeyCount());
}

// 파일이 없는 것은 거부가 아니다 — 기본 구성으로 도는 정상 상태다.
void TestMissingFileIsSilent() {
    bool external = true;
    std::string diagnostic = "이전 값";
    const BossPoses poses = defense::LoadBossPoses(
        platform::ExecutableDirectory() + "/assets/no_such_boss.pose", &external, &diagnostic);
    CHECK(!external);
    CHECK(diagnostic.empty());
    CHECK_EQ(poses.walk.KeyCount(), DefaultWalkKeys().size());
}

// ── 2. 세 가지 거부 ────────────────────────────────────────────────
void TestRejections() {
    // (a) 키 시각이 단조 증가가 아니다. AnimClip::Sample 은 오름차순을 믿고
    //     앞에서부터 훑으므로, 이런 클립은 뒤 키가 조용히 도달 불가가 된다.
    CheckWholeFileRejected("pose_nonmonotonic.pose",
        "bones 13\n"
        "clip walk\n"
        "key 0.00 0 0 -35 35 -35 35\n"
        "key 0.80 0 0 -21 49 -49 21\n"
        "key 0.40 0 0 -35 35 -35 35\n"
        "clip attack\n"
        "key 0.00 0 0 -35 35 -35 35\n"
        "key 0.40 0 0 -52 52 -28 28\n");

    // (b) 본 수가 골격과 다르다. 파일이 다른 골격을 전제로 저작됐다는 뜻이라
    //     각 값이 어느 관절의 것인지 알 수 없다.
    CheckWholeFileRejected("pose_bones.pose",
        "bones 9\n"
        "clip walk\n"
        "key 0.00 0 0 -35 35 -35 35\n"
        "key 0.50 0 0 -21 49 -49 21\n"
        "clip attack\n"
        "key 0.00 0 0 -35 35 -35 35\n"
        "key 0.40 0 0 -52 52 -28 28\n");

    // (c) 값이 범위 밖. 몸통 들림 12는 보스를 화면 밖으로 띄운다.
    CheckWholeFileRejected("pose_range.pose",
        "bones 13\n"
        "clip walk\n"
        "key 0.00  0 0 -35 35 -35 35\n"
        "key 0.50 12 0 -21 49 -49 21\n"
        "clip attack\n"
        "key 0.00 0 0 -35 35 -35 35\n"
        "key 0.40 0 0 -52 52 -28 28\n");

    // (d) 필드가 모자라거나 남는다.
    CheckWholeFileRejected("pose_short.pose",
        "bones 13\nclip walk\nkey 0.00 0 0 -35 35 -35\n");
    CheckWholeFileRejected("pose_long.pose",
        "bones 13\nclip walk\nkey 0.00 0 0 -35 35 -35 35 99\n"
        "key 0.5 0 0 -35 35 -35 35\nclip attack\n"
        "key 0.0 0 0 -35 35 -35 35\nkey 0.4 0 0 -35 35 -35 35\n");

    // (e) 구조가 깨졌다.
    CheckWholeFileRejected("pose_nobones.pose",
        "clip walk\nkey 0.00 0 0 -35 35 -35 35\nkey 0.5 0 0 -35 35 -35 35\n");
    CheckWholeFileRejected("pose_orphan.pose",
        "bones 13\nkey 0.00 0 0 -35 35 -35 35\n");
    CheckWholeFileRejected("pose_unknown.pose",
        "bones 13\nspine 4\nclip walk\nkey 0.00 0 0 -35 35 -35 35\n");
    CheckWholeFileRejected("pose_oneclip.pose",
        "bones 13\nclip walk\nkey 0.00 0 0 -35 35 -35 35\nkey 0.5 0 0 -35 35 -35 35\n");
}

// ── 3. 클립의 계약 ─────────────────────────────────────────────────
void TestSampleHitsKeysExactly() {
    BossPoses poses;
    CHECK(ParseBossPoses(kValid, poses, nullptr));
    // 0.60 은 두 번째 키의 시각이다. 그 시각의 표본은 그 키여야 한다.
    BossPoseKey middle;
    middle.time = 0.60f; middle.lift = -0.040f; middle.roll = 3.0f;
    middle.hip[0] = -21.0f; middle.hip[1] = 49.0f; middle.hip[2] = -49.0f; middle.hip[3] = 21.0f;
    const engine::Pose expected = BossPoseFromKey(middle);
    const engine::Pose sampled = poses.walk.Sample(0.60f);
    CHECK_EQ(sampled.size(), expected.size());
    for (size_t bone = 0; bone < expected.size() && bone < sampled.size(); ++bone) {
        CHECK(Near(sampled[bone].position.x, expected[bone].position.x, 1e-5f));
        CHECK(Near(sampled[bone].position.y, expected[bone].position.y, 1e-5f));
        CHECK(Near(sampled[bone].rotation.z, expected[bone].rotation.z, 1e-5f));
        CHECK(Near(sampled[bone].rotation.w, expected[bone].rotation.w, 1e-5f));
    }
    // 범위 밖은 클램프한다(끝 키 유지).
    const engine::Pose past = poses.walk.Sample(9.0f);
    const engine::Pose last = poses.walk.Sample(1.20f);
    for (size_t bone = 0; bone < past.size(); ++bone)
        CHECK(Near(past[bone].position.y, last[bone].position.y, 1e-6f));
}

void TestBlendUsesWeight() {
    BossPoseKey low, high;
    low.lift = 0.0f;
    high.lift = -0.40f;
    high.roll = 40.0f;
    const engine::Pose a = BossPoseFromKey(low);
    const engine::Pose b = BossPoseFromKey(high);

    const engine::Pose zero = engine::BlendPoses(a, b, 0.0f);
    const engine::Pose one  = engine::BlendPoses(a, b, 1.0f);
    const engine::Pose half = engine::BlendPoses(a, b, 0.5f);
    CHECK(Near(zero[0].position.y, a[0].position.y, 1e-6f));
    CHECK(Near(one[0].position.y, b[0].position.y, 1e-6f));
    // 가중치를 무시하고 b 를 그대로 내면 이 단언이 실패한다.
    CHECK(Near(half[0].position.y, -0.20f, 1e-5f));
    CHECK(!Near(half[0].position.y, b[0].position.y, 1e-3f));
    CHECK(!Near(half[0].position.y, a[0].position.y, 1e-3f));
    // 회전도 섞인다(중간 각이 양 끝 어느 쪽과도 다르다).
    CHECK(!Near(half[0].rotation.z, a[0].rotation.z, 1e-4f));
    CHECK(!Near(half[0].rotation.z, b[0].rotation.z, 1e-4f));

    // 그리고 그 블렌딩이 보스 자세까지 도달한다.
    BossPoses poses;
    const engine::Pose calm = poses.SampleLocal(0.5f, 0.0f);
    const engine::Pose slam = poses.SampleLocal(0.5f, 1.0f);
    CHECK(!Near(calm[0].position.y, slam[0].position.y, 1e-3f));
}

}  // namespace

static void RunTests() {
    TestValidTextLoads();
    TestShippedFileLoads();
    TestMissingFileIsSilent();
    TestRejections();
    TestSampleHitsKeysExactly();
    TestBlendUsesWeight();
}

TEST_MAIN("pose_content")
