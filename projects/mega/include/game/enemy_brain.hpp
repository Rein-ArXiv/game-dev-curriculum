// enemy_brain — 적의 결정. 잡몹은 상태 기계로, 정예는 상태 기계 **또는** 행동
// 트리로 쓴 같은 행동으로.
//
// 19편까지 적의 결정은 `Enemy::alive` 플래그 하나였다. 살아 있으면 걷고, 죽으면
// 사라진다 — 상태가 하나뿐이니 전이라는 개념이 없었다. 격자 항법이 들어오면서
// 그 하나로 표현되지 않는 사건이 셋 생긴다. 앞 칸이 타워로 막혔다(되돌아가는
// 대신 때려야 한다), 빙결 타워에 맞았다(잠시 못 움직인다), 오래 살아남았다
// (가속한다). 셋 다 "지금 무엇을 하는 중인가"를 자료로 들지 않으면 쓸 수 없다.
//
// 두 층으로 나눈 기준은 **개체 수**다.
//
//   잡몹   수백~수천. 상태 기계 하나를 전원이 돌려 쓴다(flyweight). 상태·전이는
//          한 벌이고 `current_` 만 개체가 든다 — 개체마다 StateMachine 을 하나씩
//          두면 std::function 여섯 개와 vector 둘이 개체 수만큼 복제된다.
//   정예   한둘. 우선순위 있는 조합(후퇴 → 회복, 슬램, 전진)이 필요하고, 그
//          조합을 두 가지 방식으로 쓸 수 있다. 그 둘이 이 파일의 A/B 다.
//
// ── 두 팔이 틱 단위로 같은 결과를 내게 하는 규칙 ────────────────────
//
// FSM 은 "전이를 먼저 보고, 그 다음 현재 상태를 돈다". BT 의 memory Composite 는
// "Running 이던 자식부터 잇고, 그 자식이 Success 를 내면 **같은 틱에** 다음
// 자식으로 넘어간다". 이 차이 때문에 아무렇게나 쓰면 두 팔이 한 틱씩 어긋난다.
// 어긋나지 않게 하는 규칙은 하나다 — **완료를 어디서 검사하는가**.
//
//   자기 Composite 안에 뒤가 있는 노드(Retreat) → **진입 시** 완료를 검사하고
//       끝났으면 아무 일도 하지 않고 Success. 그러면 BT 는 그 틱에 다음 노드를
//       돌고, FSM 도 그 틱에 전이해 다음 상태를 돈다.
//   자기 Composite 를 끝내는 노드(Heal · Slam · Advance) → **행동한 뒤** 완료를
//       검사한다. 그러면 BT 는 그 틱을 그 노드로 끝내고, FSM 도 그 틱을 그
//       상태로 끝낸 뒤 다음 틱에 전이한다.
//
// 이 규칙을 지키면 두 팔의 상태 해시가 매치 내내 비트 동일하다. 행동 본체를
// 자유 함수로 한 벌만 두는 것(EliteRetreatStep 등)은 저장 세 팔이
// AdvanceEnemyOnPath 를 공유하는 것과 같은 이유다 — 본체가 두 벌이 되는 순간
// A/B 는 비교가 아니라 서로 다른 두 게임이 된다.

#pragma once

#include "ai/bt/builder.hpp"
#include "ai/bt/node.hpp"
#include "ai/bt/tick.hpp"
#include "ai/fsm/state_machine.hpp"
#include "ai/perception/perception.hpp"
#include "ai/steering/steering.hpp"
#include "game/nav.hpp"
#include "math/vec.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace mega {
namespace game {

// ── 잡몹 ───────────────────────────────────────────────────────────
enum class GruntState : uint8_t { Advance, Blocked, Stunned, Enraged, Count };

// 오래 살아남은 적이 가속하기 시작하는 시각과 그 배수. 이 둘이 있어야 Enraged 가
// 표현할 것을 갖는다.
inline constexpr float kEnrageAfter = 25.0f;
inline constexpr float kEnrageSpeed = 1.30f;
// 막힌 잡몹이 앞의 타워를 때리는 초당 피해. 타워 체력(kTowerBaseHp = 180)에
// 대해 7초쯤이라 플레이어가 대응할 시간이 남는다.
inline constexpr float kMeleeDps = 26.0f;
// 빙결 타워 한 발이 재우는 시간.
inline constexpr float kFrostStun = 0.45f;

// 적 한 마리에 대한 상태 기계의 뷰. 입력은 호스트가 매 틱 채우고, 출력은
// 호스트가 매 틱 읽는다. 상태 기계는 이 구조체 말고는 아무것도 모른다.
// snippet-begin: grunt-ctx
struct GruntCtx {
    float dt = 0;
    float age = 0;          // 스폰 후 경과
    float stun = 0;         // 남은 빙결 시간
    bool  blocked = false;  // 다음 걸음이 통행 불가 칸으로 들어간다

    float speedScale = 1.0f;   // 출력: 이번 틱 속도 배수
    bool  attack = false;      // 출력: 앞의 타워를 때린다
    uint32_t enters = 0, exits = 0;   // 전이가 실제로 몇 번 울렸는가
};
// snippet-end: grunt-ctx

// 상태 기계 한 벌. 개체마다 만들지 않는다 — 상태와 전이는 개체 수와 무관하다.
// snippet-begin: grunt-brain
class GruntBrain {
public:
    GruntBrain() { Build(); }

    // 개체의 상태를 밀어 넣고, 한 틱 돌리고, 새 상태를 돌려받는다.
    GruntState Tick(GruntState state, GruntCtx& ctx) {
        machine_.Resume(static_cast<int>(state));
        machine_.Update(ctx);
        return static_cast<GruntState>(machine_.Current());
    }
    const char* NameOf(GruntState state) {
        machine_.Resume(static_cast<int>(state));
        return machine_.CurrentName().c_str();
    }

private:
    void Build() {
        auto enter = [](GruntCtx& c) { ++c.enters; };
        auto exit  = [](GruntCtx& c) { ++c.exits; };
        const int advance = machine_.AddState("Advance", enter,
            [](GruntCtx& c) { c.speedScale = 1.0f; }, exit);
        const int blocked = machine_.AddState("Blocked", enter,
            [](GruntCtx& c) { c.speedScale = 0.0f; c.attack = true; }, exit);
        const int stunned = machine_.AddState("Stunned", enter,
            [](GruntCtx& c) { c.speedScale = 0.0f; }, exit);
        const int enraged = machine_.AddState("Enraged", enter,
            [](GruntCtx& c) { c.speedScale = kEnrageSpeed; }, exit);

        // **순서가 규칙이다.** StateMachine::Update 는 참인 첫 전이를 택하므로,
        // 빙결과 봉쇄가 동시에 참일 때 어느 쪽이 이기는지를 이 배열의 순서가
        // 정한다. 빙결이 먼저다 — 얼어붙은 적이 타워를 때리면 빙결 타워가
        // 아무 일도 하지 않은 것이 된다.
        machine_.AddTransition(-1, [](const GruntCtx& c) { return c.stun > 0.0f; }, stunned);
        machine_.AddTransition(stunned, [](const GruntCtx& c) { return c.stun <= 0.0f; }, advance);
        machine_.AddTransition(advance, [](const GruntCtx& c) { return c.blocked; }, blocked);
        machine_.AddTransition(enraged, [](const GruntCtx& c) { return c.blocked; }, blocked);
        machine_.AddTransition(blocked,
            [](const GruntCtx& c) { return !c.blocked && c.age < kEnrageAfter; }, advance);
        machine_.AddTransition(blocked,
            [](const GruntCtx& c) { return !c.blocked && c.age >= kEnrageAfter; }, enraged);
        machine_.AddTransition(advance,
            [](const GruntCtx& c) { return c.age >= kEnrageAfter; }, enraged);
    }

    ai::fsm::StateMachine<GruntCtx> machine_;
};
// snippet-end: grunt-brain

// ── 정예 ───────────────────────────────────────────────────────────
enum class EliteState : uint8_t { Advance, Retreat, Heal, Slam, Count };

inline constexpr float kEliteHurtFraction  = 0.30f;   // 이 아래로 떨어지면 후퇴
inline constexpr float kEliteHealTarget    = 0.55f;   // 이만큼 회복하면 복귀
inline constexpr float kEliteHealRate      = 0.18f;   // 초당 최대 체력의 몇 배
inline constexpr float kEliteRetreatTime   = 1.20f;
inline constexpr float kEliteRetreatSpeed  = 1.15f;
inline constexpr float kEliteSlamCharge    = 0.60f;
inline constexpr float kEliteSlamCooldown  = 5.00f;
inline constexpr float kEliteSlamDamage    = 45.0f;
inline constexpr float kEliteSlamCells     = 1.60f;   // 반경(칸)
// 후퇴+회복을 몇 번까지 허용하는가. 상한이 없으면 정예는 **죽지 않는다** —
// 30%에서 물러나 55%까지 회복하는 고리가 끝나지 않기 때문이다. 실제로
// elite_brain=fsm 팔이 그 상태로 웨이브 5에서 매치를 끝냈다.
inline constexpr int   kEliteMaxRetreats   = 2;

// 정예 한 마리의 뷰. FSM 팔과 BT 팔이 **같은 구조체**를 쓴다.
// snippet-begin: elite-ctx
struct EliteCtx {
    float dt = 0;
    float hp = 1, maxHp = 1;
    float coreDist = 0;
    bool  towerNear = false;

    float retreat = 0;         // 남은 후퇴 시간(두 팔이 공유하는 타이머)
    float slam = 0;            // 남은 충전 시간
    float slamCooldown = 0;
    int   retreats = 0;        // 이번 생에 물러난 횟수

    float speedScale = 1.0f;   // 출력
    bool  away = false;        // 출력: 코어 반대 방향으로
    float heal = 0;            // 출력: 이번 틱 회복량
    bool  slamNow = false;     // 출력: 이번 틱 슬램 발동
};
// snippet-end: elite-ctx

// ── 행동 본체: 두 팔이 공유하는 유일한 사본 ────────────────────────
// snippet-begin: elite-actions
inline void EliteArmRetreat(EliteCtx& c) { c.retreat = kEliteRetreatTime; ++c.retreats; }
inline void EliteArmSlam(EliteCtx& c) { c.slam = kEliteSlamCharge; }

// 뒤에 Heal 이 있는 노드 → **진입 시** 완료 검사.
inline ai::bt::Status EliteRetreatStep(EliteCtx& c) {
    if (c.retreat <= 0.0f) return ai::bt::Status::Success;
    c.retreat -= c.dt;
    c.speedScale = kEliteRetreatSpeed;
    c.away = true;
    return ai::bt::Status::Running;
}

// Sequence 를 끝내는 노드 → **행동한 뒤** 완료 검사.
inline ai::bt::Status EliteHealStep(EliteCtx& c) {
    const float gain = kEliteHealRate * c.maxHp * c.dt;
    c.heal = gain;
    c.speedScale = 0.0f;
    return (c.hp + gain >= c.maxHp * kEliteHealTarget) ? ai::bt::Status::Success
                                                       : ai::bt::Status::Running;
}

inline ai::bt::Status EliteSlamStep(EliteCtx& c) {
    c.slam -= c.dt;
    c.speedScale = 0.0f;
    if (c.slam > 0.0f) return ai::bt::Status::Running;
    c.slamNow = true;
    c.slamCooldown = kEliteSlamCooldown;
    return ai::bt::Status::Success;
}

inline ai::bt::Status EliteAdvanceStep(EliteCtx& c) {
    c.speedScale = 1.0f;
    return ai::bt::Status::Success;
}

inline bool EliteHurt(const EliteCtx& c) {
    return c.hp < c.maxHp * kEliteHurtFraction && c.retreats < kEliteMaxRetreats;
}
inline bool EliteSlamReady(const EliteCtx& c) { return c.slamCooldown <= 0.0f && c.towerNear; }
// snippet-end: elite-actions

// 내장 기본 트리의 텍스트. `assets/elite.bt` 가 없거나 거부되면 이것을 읽는다 —
// 폴백이 **다른 형식**이면 "파일이 반영됐는가"를 결과로 구분할 수 없다.
inline const char* DefaultEliteTreeText() {
    return "selector\n"
           "  sequence\n"
           "    condition hurt\n"
           "    action arm_retreat\n"
           "    action retreat\n"
           "    action heal\n"
           "  end\n"
           "  sequence\n"
           "    condition slam_ready\n"
           "    action arm_slam\n"
           "    action slam\n"
           "  end\n"
           "  action advance\n"
           "end\n";
}

// ── 정예 브레인: 두 팔 ─────────────────────────────────────────────
// 트리도 상태 기계도 **개체마다 하나**다. BT 의 Running 위치는 Composite 안의
// current_ 가 들고, 그것이 곧 개체의 상태이기 때문이다. 정예가 한둘이라 감당된다 —
// 잡몹에 같은 구조를 쓸 수 없는 이유가 정확히 그것이고, 그래서 층이 갈렸다.
class EliteBrain {
public:
    // 트리 정의를 텍스트에서 조립한다. 거부되면 내장 텍스트로 다시 시도하고,
    // 그 사실을 돌려준다. **부분 반영은 없다** — 절반만 저작된 트리는 아무도
    // 저작한 적 없는 행동이라 진단이 더 어려워진다.
    // snippet-begin: elite-configure
    bool Configure(EliteBrainKind kind, size_t slots, const std::string& treeText) {
        kind_ = kind;
        slots_.clear();
        slots_.resize(slots ? slots : 1);
        free_.clear();
        for (size_t i = slots_.size(); i-- > 0;) free_.push_back(static_cast<uint8_t>(i));
        external_ = !treeText.empty() && ParseTree(treeText);
        if (!external_) ParseTree(DefaultEliteTreeText());
        for (Slot& slot : slots_) {
            slot.tree = BuildTree();
            BuildMachine(slot.machine);
        }
        return external_;
    }
    // snippet-end: elite-configure

    bool ExternalTree() const { return external_; }
    EliteBrainKind Kind() const { return kind_; }
    size_t SlotCount() const { return slots_.size(); }

    // 슬롯 배정은 LIFO 다. 순서가 정해져 있지 않으면 같은 매치를 두 번 돌렸을 때
    // 정예마다 다른 트리를 물려받아 결정성이 깨진다.
    uint8_t Acquire() {
        if (free_.empty()) return kNoSlot;
        const uint8_t slot = free_.back();
        free_.pop_back();
        Slot& s = slots_[slot];
        s.ctx = EliteCtx{};
        s.state = EliteState::Advance;
        if (s.tree) s.tree->Reset();
        return slot;
    }
    void Release(uint8_t slot) {
        if (slot >= slots_.size()) return;
        free_.push_back(slot);
    }

    EliteCtx& Context(uint8_t slot) { return slots_[slot].ctx; }
    EliteState State(uint8_t slot) const { return slots_[slot].state; }
    const ai::bt::Blackboard& Board(uint8_t slot) const { return slots_[slot].board; }

    // 한 틱. 팔에 따라 트리를 돌리거나 상태 기계를 돌린다.
    // snippet-begin: elite-tick
    void Tick(uint8_t slot) {
        Slot& s = slots_[slot];
        s.ctx.speedScale = 1.0f;
        s.ctx.away = false;
        s.ctx.heal = 0.0f;
        s.ctx.slamNow = false;
        if (kind_ == EliteBrainKind::Bt) {
            // 조건 노드가 읽는 것은 블랙보드뿐이다. 세계에 대한 질문은 자료로
            // 오고, 행동은 개체의 타이머를 만진다 — 그 둘을 갈라 두면 트리
            // 정의를 파일로 옮겨도 조건이 코드를 다시 필요로 하지 않는다.
            s.board.SetFloat("hp_frac", s.ctx.maxHp > 0 ? s.ctx.hp / s.ctx.maxHp : 0.0f);
            s.board.SetInt("retreats_left", kEliteMaxRetreats - s.ctx.retreats);
            s.board.SetFloat("core_dist", s.ctx.coreDist);
            s.board.SetBool("tower_near", s.ctx.towerNear);
            s.board.SetBool("slam_ready", EliteSlamReady(s.ctx));
            active_ = &s.ctx;
            if (s.tree) s.tree->Tick(s.board);
            active_ = nullptr;
            return;
        }
        s.machine.Resume(static_cast<int>(s.state));
        s.machine.Update(s.ctx);
        s.state = static_cast<EliteState>(s.machine.Current());
    }
    // snippet-end: elite-tick

    static constexpr uint8_t kNoSlot = 0xFF;

private:
    enum class Token : uint8_t { Selector, Sequence, End, CondHurt, CondSlam,
                                 ArmRetreat, ArmSlam, Retreat, Heal, Slam, Advance };

    // 텍스트를 토큰 열로. 알 수 없는 이름 하나에 전체가 거부된다.
    bool ParseTree(const std::string& text) {
        std::vector<Token> tokens;
        std::string word;
        std::vector<std::string> words;
        for (char c : text) {
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                if (!word.empty()) { words.push_back(word); word.clear(); }
                if (c == '\n') continue;
            } else if (c == '#') {
                word += c;      // 주석은 쓰지 않는다 — 알 수 없는 이름으로 거부된다
            } else {
                word += c;
            }
        }
        if (!word.empty()) words.push_back(word);

        int depth = 0;
        for (size_t i = 0; i < words.size(); ++i) {
            const std::string& w = words[i];
            if (w == "selector") { tokens.push_back(Token::Selector); ++depth; continue; }
            if (w == "sequence") { tokens.push_back(Token::Sequence); ++depth; continue; }
            if (w == "end") {
                if (--depth < 0) return false;      // 짝이 안 맞는다
                tokens.push_back(Token::End);
                continue;
            }
            if ((w == "condition" || w == "action") && i + 1 < words.size()) {
                const std::string& name = words[++i];
                if (w == "condition") {
                    if (name == "hurt") tokens.push_back(Token::CondHurt);
                    else if (name == "slam_ready") tokens.push_back(Token::CondSlam);
                    else return false;
                } else {
                    if (name == "arm_retreat") tokens.push_back(Token::ArmRetreat);
                    else if (name == "arm_slam") tokens.push_back(Token::ArmSlam);
                    else if (name == "retreat") tokens.push_back(Token::Retreat);
                    else if (name == "heal") tokens.push_back(Token::Heal);
                    else if (name == "slam") tokens.push_back(Token::Slam);
                    else if (name == "advance") tokens.push_back(Token::Advance);
                    else return false;
                }
                continue;
            }
            return false;
        }
        if (depth != 0 || tokens.empty()) return false;
        tokens_ = std::move(tokens);
        return true;
    }

    // 토큰 열을 Builder 호출로. 조립 DSL 이 없으면 자료 기반 정의가 성립하지 않는다.
    // snippet-begin: elite-build-tree
    ai::bt::NodePtr BuildTree() {
        ai::bt::Builder builder;
        for (Token t : tokens_) {
            switch (t) {
                case Token::Selector: builder.Selector(); break;
                case Token::Sequence: builder.Sequence(); break;
                case Token::End:      builder.End(); break;
                case Token::CondHurt:
                    builder.Condition([](ai::bt::Blackboard& bb) {
                        return bb.GetFloat("hp_frac", 1.0f) < kEliteHurtFraction &&
                               bb.GetInt("retreats_left", 0) > 0;
                    });
                    break;
                case Token::CondSlam:
                    builder.Condition([](ai::bt::Blackboard& bb) {
                        return bb.GetBool("slam_ready", false);
                    });
                    break;
                case Token::ArmRetreat:
                    builder.Action([this](ai::bt::Blackboard&) {
                        EliteArmRetreat(*active_);
                        return ai::bt::Status::Success;
                    });
                    break;
                case Token::ArmSlam:
                    builder.Action([this](ai::bt::Blackboard&) {
                        EliteArmSlam(*active_);
                        return ai::bt::Status::Success;
                    });
                    break;
                case Token::Retreat:
                    builder.Action([this](ai::bt::Blackboard&) { return EliteRetreatStep(*active_); });
                    break;
                case Token::Heal:
                    builder.Action([this](ai::bt::Blackboard&) { return EliteHealStep(*active_); });
                    break;
                case Token::Slam:
                    builder.Action([this](ai::bt::Blackboard&) { return EliteSlamStep(*active_); });
                    break;
                case Token::Advance:
                    builder.Action([this](ai::bt::Blackboard&) { return EliteAdvanceStep(*active_); });
                    break;
            }
        }
        return builder.Build();
    }
    // snippet-end: elite-build-tree

    // 같은 행동을 상태와 전이로. 여기 있는 타이머 셋(retreat · slam ·
    // slamCooldown)이 BT 의 Running 이 대신하던 것이다.
    // snippet-begin: elite-machine
    static void BuildMachine(ai::fsm::StateMachine<EliteCtx>& m) {
        const int advance = m.AddState("Advance", {},
            [](EliteCtx& c) { EliteAdvanceStep(c); });
        const int retreat = m.AddState("Retreat", [](EliteCtx& c) { EliteArmRetreat(c); },
            [](EliteCtx& c) { EliteRetreatStep(c); });
        const int heal = m.AddState("Heal", {}, [](EliteCtx& c) { EliteHealStep(c); });
        const int slam = m.AddState("Slam", [](EliteCtx& c) { EliteArmSlam(c); },
            [](EliteCtx& c) { EliteSlamStep(c); });

        m.AddTransition(advance, [](const EliteCtx& c) { return EliteHurt(c); }, retreat);
        m.AddTransition(advance, [](const EliteCtx& c) { return EliteSlamReady(c); }, slam);
        m.AddTransition(retreat, [](const EliteCtx& c) { return c.retreat <= 0.0f; }, heal);
        m.AddTransition(heal,
            [](const EliteCtx& c) { return c.hp >= c.maxHp * kEliteHealTarget; }, advance);
        m.AddTransition(slam, [](const EliteCtx& c) { return c.slam <= 0.0f; }, advance);
    }
    // snippet-end: elite-machine

    struct Slot {
        EliteCtx                        ctx;
        EliteState                      state = EliteState::Advance;
        ai::bt::NodePtr                 tree;
        ai::bt::Blackboard              board;
        ai::fsm::StateMachine<EliteCtx> machine;
    };

    std::vector<Slot>    slots_;
    std::vector<uint8_t> free_;
    std::vector<Token>   tokens_;
    EliteCtx*            active_ = nullptr;
    EliteBrainKind       kind_ = EliteBrainKind::Plain;
    bool                 external_ = false;
};

// ── 인지 ───────────────────────────────────────────────────────────
// 레이더 타워의 시야와 청각. 단위는 **칸**이다 — 격자 레이캐스트가 칸 단위로
// 걷기 때문이고, 픽셀로 두면 맵 셀 크기가 바뀔 때마다 규칙이 따라 바뀐다.
inline constexpr float kRadarRangeCells = 12.0f;
inline constexpr float kRadarFovDegrees = 140.0f;
inline constexpr float kRadarHearCells  = 2.5f;
inline constexpr float kStalkerForget   = 1.10f;   // 놓친 뒤 계속 조준되는 시간

inline ai::perceive::VisionCone RadarCone() {
    return ai::perceive::VisionCone::FromDegrees(kRadarRangeCells, kRadarFovDegrees);
}

// ── 개체별 AI 상태 ─────────────────────────────────────────────────
// EnemyStore 안에 두지 않았다. 저장 세 팔(AoS·SoA·ECS)의 비교 대상은 "규칙이
// 매 틱 읽는 열"인데 이 값들은 AI 시스템만 읽는다 — 열을 늘리면 세 팔이 전부
// 그 열을 나르게 되고, 17편이 잰 수치가 다른 워크로드의 수치가 된다. 대신
// 삽입 순서를 그대로 따라가는 병렬 배열로 두고, 압축을 저장과 **같은 규칙**으로
// 한다(살아 있는 행을 순서 보존으로 앞으로 당긴다).
struct BrainRow {
    GruntState state = GruntState::Advance;
    uint8_t    elite = EliteBrain::kNoSlot;
    uint8_t    revealed = 1;      // 은신 규칙이 꺼져 있거나 지금 드러났다
    bool       hasCell = false;   // 목표 칸을 잡았는가
    NavCell    cell{0, 0};        // 지금 걸어가고 있는 목표 칸
    float      stun = 0;
    float      age = 0;
    math::Vec2 velocity{0, 0};    // 조향 팔의 속도
    ai::perceive::Memory memory;  // 은신 개체의 목격 기억
};

}  // namespace game
}  // namespace mega
