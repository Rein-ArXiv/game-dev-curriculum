// state_machine — 일반 유한 상태 기계 FSM<Ctx>.
//
// BT 가 고수준 의사결정("추적/공격/후퇴")을 합성한다면, FSM 은 짧고 명확한
// 상태 사이클("공격 모션 → 쿨다운 → 재공격")에 맞는다(ai 시리즈 BT vs FSM).
// 각 상태는 onEnter/onUpdate/onExit 를 갖고, 전이는 (from, 가드, to)로 둔다.
// from = -1 은 *임의 상태에서* 전이(글로벌 전이, 예: 사망).
//
// Update 순서: 먼저 전이 가드를 검사해 상태를 바꾸고(즉시 onExit/onEnter), 그
// 다음 현재 상태의 onUpdate 를 돈다.

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace mega {
namespace ai {
namespace fsm {

template <class Ctx>
class StateMachine {
public:
    using Fn    = std::function<void(Ctx&)>;
    using Guard = std::function<bool(const Ctx&)>;

    int AddState(std::string name, Fn onEnter = {}, Fn onUpdate = {}, Fn onExit = {}) {
        states_.push_back({std::move(name), std::move(onEnter), std::move(onUpdate), std::move(onExit)});
        return (int)states_.size() - 1;
    }
    void AddTransition(int from, Guard cond, int to) {
        transitions_.push_back({from, std::move(cond), to});
    }

    void Start(int state, Ctx& ctx) {
        current_ = state;
        if (Valid() && states_[current_].onEnter) states_[current_].onEnter(ctx);
    }

    void Update(Ctx& ctx) {
        for (const Transition& t : transitions_) {
            if ((t.from == -1 || t.from == current_) && t.to != current_) {
                if (t.cond(ctx)) { ChangeState(t.to, ctx); break; }   // 첫 참 전이
            }
        }
        if (Valid() && states_[current_].onUpdate) states_[current_].onUpdate(ctx);
    }

    int Current() const { return current_; }
    const std::string& CurrentName() const {
        static const std::string none = "(none)";
        return Valid() ? states_[current_].name : none;
    }

private:
    struct State      { std::string name; Fn onEnter, onUpdate, onExit; };
    struct Transition { int from; Guard cond; int to; };

    bool Valid() const { return current_ >= 0 && current_ < (int)states_.size(); }

    void ChangeState(int to, Ctx& ctx) {
        if (to == current_) return;
        if (Valid() && states_[current_].onExit) states_[current_].onExit(ctx);
        current_ = to;
        if (Valid() && states_[current_].onEnter) states_[current_].onEnter(ctx);
    }

    std::vector<State>      states_;
    std::vector<Transition> transitions_;
    int                     current_ = -1;
};

}  // namespace fsm
}  // namespace ai
}  // namespace mega
