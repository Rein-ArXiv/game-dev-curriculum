// assets/elite.bt — 신뢰 경계 밖 입력으로서의 **행동 정의**.
//
// 이 파일은 재빌드 없이 교체할 수 있다고 광고된 파일이고, 그 말은 곧 누구든
// 아무 바이트나 넣을 수 있다는 뜻이다. level·waves·balance·boss.pose 와 같은
// 규칙을 그대로 적용한다: 한 토막이라도 거부되면 파일 전체를 버리고 내장 트리로
// 돌아간다. 절반만 저작된 트리는 아무도 저작한 적 없는 행동이라, 화면에서
// 이상한 것을 본 사람이 원인을 파일에서도 코드에서도 찾을 수 없다.
//
// 그리고 조립 DSL 자체의 계약 둘.
//   1. **텍스트로 조립한 트리가 손으로 조립한 트리와 같은 결과를 낸다.** 이것이
//      성립하지 않으면 "행동을 자료로 옮겼다"는 문장이 거짓이다.
//   2. **다른 텍스트는 다른 결과를 낸다.** 1번만 있으면 파서가 입력을 통째로
//      무시하고 언제나 내장 트리를 세워도 통과한다 — 그 상태에서 저작 파일은
//      장식이다.

#include "ai/bt/builder.hpp"
#include "ai/bt/node.hpp"
#include "ai/bt/tick.hpp"
#include "game/defense_content.hpp"
#include "game/enemy_brain.hpp"
#include "io/file.h"
#include "platform/paths.hpp"
#include "test_util.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace mega;
using namespace mega::game;

namespace {

// ── 합성 호스트 ────────────────────────────────────────────────────
// game.hpp 의 TickElite 와 **같은 순서**로 입력을 채우고 출력을 되먹인다.
// 정예 하나를 세계 없이 돌리기 위한 최소 하네스다.
constexpr float kDt      = 1.0f / 60.0f;
constexpr float kMaxHp   = 1000.0f;
constexpr float kIncoming = 260.0f;      // 전진 중에만 받는 초당 피해

// 타워가 옆에 있는가. 결정적인 일정으로 흔들어 슬램 분기를 실제로 통과시킨다.
bool TowerNearAt(int tick) { return (tick / 40) % 3 != 0; }

// 한 틱의 관측. 두 트리가 같은지는 이 일곱 값으로 판정한다.
struct Sample {
    float speed = 0;
    bool  away = false;
    float heal = 0;
    bool  slam = false;
    float retreat = 0;
    int   retreats = 0;
    float hp = 0;

    bool operator==(const Sample& o) const {
        return speed == o.speed && away == o.away && heal == o.heal && slam == o.slam &&
               retreat == o.retreat && retreats == o.retreats && hp == o.hp;
    }
};

// 호스트 루프. `tick` 은 브레인 한 틱을 돌리는 일만 한다.
template <class TickFn>
std::vector<Sample> Drive(EliteCtx& ctx, TickFn tick, int ticks) {
    std::vector<Sample> out;
    out.reserve(static_cast<size_t>(ticks));
    float hp = kMaxHp;
    for (int t = 0; t < ticks; ++t) {
        // 전진 중일 때만 맞는다. 물러나면 사거리를 벗어나고, 회복 중에는 이미
        // 벗어나 있다 — 그러지 않으면 회복률(초당 180)이 피해율에 눌려
        // Heal 이 영원히 Running 이 되고, 트리는 첫 후퇴에서 멈춘 채 끝난다.
        if (!ctx.away && ctx.speedScale > 0.0f) {
            hp -= kIncoming * kDt;
            if (hp < 1.0f) hp = 1.0f;
        }
        ctx.dt = kDt;
        ctx.hp = hp;
        ctx.maxHp = kMaxHp;
        ctx.coreDist = 300.0f - static_cast<float>(t) * 0.5f;
        ctx.towerNear = TowerNearAt(t);
        // 쿨다운은 브레인 밖에서 깎는다(제품과 같은 자리).
        ctx.slamCooldown = ctx.slamCooldown > kDt ? ctx.slamCooldown - kDt : 0.0f;
        tick();
        if (ctx.heal > 0.0f) {
            hp += ctx.heal;
            if (hp > kMaxHp) hp = kMaxHp;
        }
        out.push_back(Sample{ctx.speedScale, ctx.away, ctx.heal, ctx.slamNow,
                             ctx.retreat, ctx.retreats, hp});
    }
    return out;
}

// 텍스트에서 조립한 트리를 도는 팔.
std::vector<Sample> RunAuthored(const std::string& text, int ticks, bool* external) {
    EliteBrain brain;
    const bool ext = brain.Configure(EliteBrainKind::Bt, 1, text);
    if (external) *external = ext;
    const uint8_t slot = brain.Acquire();
    CHECK(slot != EliteBrain::kNoSlot);
    return Drive(brain.Context(slot), [&brain, slot]() { brain.Tick(slot); }, ticks);
}

// 손으로 조립한 트리를 도는 팔. 노드 목록도 순서도 DefaultEliteTreeText 와
// 같지만, 여기서는 Builder 를 **직접** 부른다.
struct HandTree {
    EliteCtx           ctx;
    ai::bt::Blackboard board;
    ai::bt::NodePtr    tree;

    HandTree() {
        EliteCtx* c = &ctx;
        tree = ai::bt::Builder()
            .Selector()
                .Sequence()
                    .Condition([](ai::bt::Blackboard& bb) {
                        return bb.GetFloat("hp_frac", 1.0f) < kEliteHurtFraction &&
                               bb.GetInt("retreats_left", 0) > 0;
                    })
                    .Action([c](ai::bt::Blackboard&) {
                        EliteArmRetreat(*c);
                        return ai::bt::Status::Success;
                    })
                    .Action([c](ai::bt::Blackboard&) { return EliteRetreatStep(*c); })
                    .Action([c](ai::bt::Blackboard&) { return EliteHealStep(*c); })
                .End()
                .Sequence()
                    .Condition([](ai::bt::Blackboard& bb) {
                        return bb.GetBool("slam_ready", false);
                    })
                    .Action([c](ai::bt::Blackboard&) {
                        EliteArmSlam(*c);
                        return ai::bt::Status::Success;
                    })
                    .Action([c](ai::bt::Blackboard&) { return EliteSlamStep(*c); })
                .End()
                .Action([c](ai::bt::Blackboard&) { return EliteAdvanceStep(*c); })
            .End()
            .Build();
    }
    HandTree(const HandTree&) = delete;
    HandTree& operator=(const HandTree&) = delete;

    // EliteBrain::Tick 의 Bt 갈래와 같은 순서.
    void Tick() {
        ctx.speedScale = 1.0f;
        ctx.away = false;
        ctx.heal = 0.0f;
        ctx.slamNow = false;
        board.SetFloat("hp_frac", ctx.maxHp > 0 ? ctx.hp / ctx.maxHp : 0.0f);
        board.SetInt("retreats_left", kEliteMaxRetreats - ctx.retreats);
        board.SetFloat("core_dist", ctx.coreDist);
        board.SetBool("tower_near", ctx.towerNear);
        board.SetBool("slam_ready", EliteSlamReady(ctx));
        tree->Tick(board);
    }
};

// 관측 열이 실제로 무언가를 관측했는가. 전부 "전진"뿐인 열을 서로 비교해 봐야
// 아무것도 지켜지지 않는다.
int CountRetreatTicks(const std::vector<Sample>& run) {
    int n = 0;
    for (const Sample& s : run)
        if (s.away) ++n;
    return n;
}
int CountHealTicks(const std::vector<Sample>& run) {
    int n = 0;
    for (const Sample& s : run)
        if (s.heal > 0.0f) ++n;
    return n;
}
int CountSlams(const std::vector<Sample>& run) {
    int n = 0;
    for (const Sample& s : run)
        if (s.slam) ++n;
    return n;
}

void CheckSameRun(const std::vector<Sample>& a, const std::vector<Sample>& b, const char* what) {
    CHECK_EQ(a.size(), b.size());
    size_t firstDiff = a.size();
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
        if (!(a[i] == b[i])) { firstDiff = i; break; }
    CHECK_EQ(firstDiff, a.size());
    if (firstDiff != a.size())
        std::printf("  %s: 틱 %zu 에서 갈렸다 (speed %.3f/%.3f away %d/%d heal %.4f/%.4f)\n",
                    what, firstDiff, a[firstDiff].speed, b[firstDiff].speed,
                    a[firstDiff].away ? 1 : 0, b[firstDiff].away ? 1 : 0,
                    a[firstDiff].heal, b[firstDiff].heal);
}

std::string TempPath(const char* name) {
    return platform::ExecutableDirectory() + "/" + name;
}

bool WriteText(const std::string& path, const std::string& text) {
    io::File file;
    if (!file.Open(path.c_str(), io::FileMode::Write)) return false;
    return file.WriteAt(0, text.data(), text.size()).ok;
}

constexpr int kTicks = 900;

// ── 1. 자료로 쓴 트리 = 코드로 쓴 트리 ──────────────────────────────
void TestAuthoredTreeMatchesHandBuiltTree() {
    bool external = false;
    const std::vector<Sample> authored = RunAuthored(DefaultEliteTreeText(), kTicks, &external);
    CHECK(external);          // 내장 텍스트도 파서를 지나야 한다

    HandTree hand;
    const std::vector<Sample> byHand = Drive(hand.ctx, [&hand]() { hand.Tick(); }, kTicks);

    CheckSameRun(authored, byHand, "authored vs hand");

    // 열이 실제로 무언가를 관측했는가. 셋 다 0 이면 위 비교는 "둘 다 전진만
    // 했다"를 확인한 것이라 아무 계약도 지키지 못한다.
    CHECK(CountRetreatTicks(byHand) > 0);
    CHECK(CountHealTicks(byHand) > 0);
    CHECK(CountSlams(byHand) > 0);
    // 후퇴 상한이 실제로 걸린다 — 걸리지 않으면 정예는 죽지 않는다.
    CHECK_EQ(byHand.back().retreats, kEliteMaxRetreats);
    std::printf("  트리 %d틱: 후퇴 %d틱 · 회복 %d틱 · 슬램 %d회 · 후퇴횟수 %d\n",
                kTicks, CountRetreatTicks(byHand), CountHealTicks(byHand),
                CountSlams(byHand), byHand.back().retreats);
}

// ── 2. 다른 텍스트는 다른 행동 ──────────────────────────────────────
// 파서가 입력을 통째로 무시해도 1번은 통과한다. 그 상태를 여기서 배제한다.
void TestDifferentTextIsADifferentBehaviour() {
    bool external = false;
    const std::vector<Sample> baseline = RunAuthored(DefaultEliteTreeText(), kTicks, &external);
    CHECK(external);

    // 후퇴 가지를 뺀 트리. 같은 노드 어휘, 다른 트리, 다른 게임.
    const char* noRetreat =
        "selector\n"
        "  sequence\n"
        "    condition slam_ready\n"
        "    action arm_slam\n"
        "    action slam\n"
        "  end\n"
        "  action advance\n"
        "end\n";
    const std::vector<Sample> stubborn = RunAuthored(noRetreat, kTicks, &external);
    CHECK(external);
    CHECK_EQ(CountRetreatTicks(stubborn), 0);
    CHECK_EQ(CountHealTicks(stubborn), 0);
    CHECK(CountSlams(stubborn) > 0);            // 슬램 가지는 그대로 산다
    CHECK(CountRetreatTicks(baseline) > 0);     // 기준선은 물러난다

    // 전진만 하는 트리. 슬램조차 사라진다.
    const char* onlyAdvance = "selector\n  action advance\nend\n";
    const std::vector<Sample> plain = RunAuthored(onlyAdvance, kTicks, &external);
    CHECK(external);
    CHECK_EQ(CountRetreatTicks(plain), 0);
    CHECK_EQ(CountSlams(plain), 0);
}

// ── 3. 거부는 파일 단위다 ───────────────────────────────────────────
// 절반만 반영하지 않는다. 거부된 트리의 행동은 내장 트리와 **비트 단위로** 같다.
void TestRejectedTreeFallsBackWhole() {
    bool external = false;
    const std::vector<Sample> builtin = RunAuthored(DefaultEliteTreeText(), kTicks, &external);
    CHECK(external);

    struct Case { const char* name; const char* text; };
    const Case cases[] = {
        // 알 수 없는 행동 이름이 **뒤쪽에** 있다. 앞의 후퇴 가지는 완전히
        // 정상이므로, 부분 반영을 허용하는 파서라면 여기서 절반짜리 트리를 세운다.
        {"unknown-action",
         "selector\n"
         "  sequence\n"
         "    condition hurt\n"
         "    action arm_retreat\n"
         "    action retreat\n"
         "    action heal\n"
         "  end\n"
         "  action teleport\n"
         "end\n"},
        // 알 수 없는 조건 이름.
        {"unknown-condition",
         "selector\n"
         "  sequence\n"
         "    condition is_tuesday\n"
         "    action advance\n"
         "  end\n"
         "end\n"},
        // end 가 하나 남는다(닫히지 않은 Composite).
        {"missing-end",
         "selector\n"
         "  sequence\n"
         "    condition hurt\n"
         "    action retreat\n"
         "end\n"},
        // end 가 하나 더 있다(음수 깊이).
        {"extra-end",
         "selector\n"
         "  action advance\n"
         "end\n"
         "end\n"},
        // 인자 없는 action 이 파일 끝에서 잘렸다.
        {"dangling-action",
         "selector\n"
         "  action advance\n"
         "  action\n"
         "end\n"},
        // 노드가 하나도 없다(공백뿐).
        {"whitespace-only", "   \n\t\n"},
    };

    for (const Case& c : cases) {
        bool ext = true;
        const std::vector<Sample> run = RunAuthored(c.text, kTicks, &ext);
        CHECK(!ext);                                  // 거부됐다
        CheckSameRun(run, builtin, c.name);           // 그리고 통째로 내장 트리다
    }
    std::printf("  거부 %zu종: 전부 내장 트리로 통째 폴백\n",
                sizeof(cases) / sizeof(cases[0]));
}

// ── 4. 로더의 계약 ──────────────────────────────────────────────────
// LoadEliteTree 는 바이트를 읽어 넘기기만 한다. 없는 파일과 너무 큰 파일은
// 빈 문자열이 되고, 빈 문자열은 내장 트리를 뜻한다.
void TestLoaderContract() {
    bool found = true;
    const std::string missing =
        defense::LoadEliteTree(TempPath("no_such_elite_tree.bt"), &found);
    CHECK(!found);
    CHECK(missing.empty());

    // 상한(16KiB)을 넘는 파일은 읽히지 않는다. 저작 파일이 커질 수 있다는 것은
    // 곧 조립 시간이 커질 수 있다는 뜻이라, 상한은 파서가 아니라 로더에 있다.
    std::string huge;
    huge.reserve(20 * 1024);
    while (huge.size() < 20 * 1024) huge += "action advance\n";
    const std::string hugePath = TempPath("elite_huge.bt");
    CHECK(WriteText(hugePath, huge));
    found = true;
    const std::string loaded = defense::LoadEliteTree(hugePath, &found);
    CHECK(!found);
    CHECK(loaded.empty());

    // 빈 텍스트 = 저작 없음. Configure 는 external=false 를 돌려주고 내장 트리를 쓴다.
    EliteBrain brain;
    CHECK(!brain.Configure(EliteBrainKind::Bt, 1, std::string{}));
    CHECK(!brain.ExternalTree());
}

// ── 5. 배포되는 그 파일 ─────────────────────────────────────────────
// 인라인 문자열만 검사하면 "저장소에 있는 elite.bt 가 조용히 거부되고 있다"를
// 아무도 알아채지 못한다. 실행 파일 옆에 복사된 진짜 파일을 읽는다.
void TestShippedAssetIsAccepted() {
    bool found = false;
    const std::string text =
        defense::LoadEliteTree(TempPath("assets/elite.bt"), &found);
    CHECK(found);
    CHECK(!text.empty());

    EliteBrain brain;
    CHECK(brain.Configure(EliteBrainKind::Bt, 4, text));
    CHECK(brain.ExternalTree());
    CHECK_EQ(brain.SlotCount(), size_t{4});

    // 그리고 그 파일의 행동이 내장 트리와 같다는 것까지 본다. 저장소의 elite.bt
    // 는 내장 텍스트와 같은 트리를 저작한 것이므로 결과도 같아야 한다 — 다르면
    // 둘 중 하나가 조용히 바뀐 것이다.
    bool external = false;
    const std::vector<Sample> shipped = RunAuthored(text, kTicks, &external);
    CHECK(external);
    const std::vector<Sample> builtin = RunAuthored(DefaultEliteTreeText(), kTicks, &external);
    CheckSameRun(shipped, builtin, "shipped elite.bt");
}

// ── 6. 슬롯 배정은 결정적이다 ───────────────────────────────────────
// 정예가 여럿 살아 있다가 죽고 다시 스폰될 때 어느 슬롯을 물려받는지가
// 실행마다 달라지면 같은 매치를 두 번 돌린 결과가 갈린다.
void TestSlotAssignmentIsDeterministic() {
    EliteBrain brain;
    brain.Configure(EliteBrainKind::Bt, 3, DefaultEliteTreeText());
    const uint8_t a = brain.Acquire();
    const uint8_t b = brain.Acquire();
    const uint8_t c = brain.Acquire();
    CHECK_EQ(a, uint8_t{0});
    CHECK_EQ(b, uint8_t{1});
    CHECK_EQ(c, uint8_t{2});
    CHECK_EQ(brain.Acquire(), EliteBrain::kNoSlot);    // 상한을 넘기면 슬롯 없음

    brain.Release(b);
    CHECK_EQ(brain.Acquire(), b);                      // LIFO
    brain.Release(a);
    brain.Release(c);
    CHECK_EQ(brain.Acquire(), c);
    CHECK_EQ(brain.Acquire(), a);

    // 되돌려받은 슬롯의 문맥은 비어 있어야 한다 — 앞 개체의 후퇴 횟수를
    // 물려받으면 새 정예가 태어나자마자 물러날 수 없는 몸이 된다.
    brain.Release(a);
    EliteCtx& reused = brain.Context(brain.Acquire());
    CHECK_EQ(reused.retreats, 0);
    CHECK_EQ(reused.retreat, 0.0f);
    CHECK_EQ(reused.slamCooldown, 0.0f);
}

void RunTests() {
    TestAuthoredTreeMatchesHandBuiltTree();
    TestDifferentTextIsADifferentBehaviour();
    TestRejectedTreeFallsBackWhole();
    TestLoaderContract();
    TestShippedAssetIsAccepted();
    TestSlotAssignmentIsDeterministic();
}

}  // namespace

TEST_MAIN("elite_content")
