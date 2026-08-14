// node — 행동 트리 노드: Composite / Decorator / Leaf.
//
// 세 종류뿐이다. Composite(Sequence/Selector/Parallel)는 자식을 조합하고,
// Decorator(Inverter/Repeater/…)는 자식 하나를 감싸 결과를 변형하며, Leaf
// (Action/Condition)는 실제 일을 한다. 이 단순한 분류로 임의 복잡한 행동을
// 합성한다(ai 시리즈 BT 노드 종류).
//
// Sequence/Selector 는 *memory* 변형이다: Running 이던 자식 위치를 기억해 다음
// tick 에 거기서 이어간다(매 tick 처음부터 재평가하는 reactive 변형과 구분).

#pragma once

#include "ai/bt/tick.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace mega {
namespace ai {
namespace bt {

class Node {
public:
    virtual ~Node() = default;
    virtual Status Tick(Blackboard& bb) = 0;
    virtual void   Reset() {}
};
using NodePtr = std::unique_ptr<Node>;

// --- Composite ---
class Composite : public Node {
public:
    void Add(NodePtr c) { children_.push_back(std::move(c)); }
    void Reset() override { current_ = 0; for (auto& c : children_) c->Reset(); }
protected:
    std::vector<NodePtr> children_;
    size_t               current_ = 0;
};

// Sequence: 자식이 모두 Success 여야 Success(논리 AND). Failure 즉시 중단.
class Sequence : public Composite {
public:
    Status Tick(Blackboard& bb) override {
        for (; current_ < children_.size(); ++current_) {
            Status s = children_[current_]->Tick(bb);
            if (s == Status::Running) return Status::Running;       // 위치 기억
            if (s == Status::Failure) { current_ = 0; return Status::Failure; }
        }
        current_ = 0;
        return Status::Success;
    }
};

// Selector: 자식 중 하나라도 Success 면 Success(논리 OR). 우선순위 폴백.
class Selector : public Composite {
public:
    Status Tick(Blackboard& bb) override {
        for (; current_ < children_.size(); ++current_) {
            Status s = children_[current_]->Tick(bb);
            if (s == Status::Running) return Status::Running;
            if (s == Status::Success) { current_ = 0; return Status::Success; }
        }
        current_ = 0;
        return Status::Failure;
    }
};

// Parallel: 모든 자식을 매 tick 평가. requireAll 이면 전부 Success 라야 Success,
// 아니면 하나라도 Success 면 Success. 하나라도 Failure 면(엄격) Failure.
class Parallel : public Composite {
public:
    explicit Parallel(bool requireAll) : requireAll_(requireAll) {}
    Status Tick(Blackboard& bb) override {
        int succ = 0, fail = 0;
        for (auto& c : children_) {
            Status s = c->Tick(bb);
            if (s == Status::Success) ++succ;
            else if (s == Status::Failure) ++fail;
        }
        int n = (int)children_.size();
        if (requireAll_) {
            if (fail > 0) return Status::Failure;
            if (succ == n) return Status::Success;
        } else {
            if (succ > 0) return Status::Success;
            if (fail == n) return Status::Failure;
        }
        return Status::Running;
    }
private:
    bool requireAll_;
};

// --- Decorator ---
class Decorator : public Node {
public:
    Decorator() = default;
    explicit Decorator(NodePtr c) : child_(std::move(c)) {}
    void SetChild(NodePtr c) { child_ = std::move(c); }      // 빌더용
    void Reset() override { if (child_) child_->Reset(); }
protected:
    NodePtr child_;
};

// Inverter: Success ↔ Failure(Running 유지).
class Inverter : public Decorator {
public:
    using Decorator::Decorator;
    Status Tick(Blackboard& bb) override {
        Status s = child_->Tick(bb);
        if (s == Status::Success) return Status::Failure;
        if (s == Status::Failure) return Status::Success;
        return s;
    }
};

// Succeeder: 자식 결과와 무관히 Success(Running 제외).
class Succeeder : public Decorator {
public:
    using Decorator::Decorator;
    Status Tick(Blackboard& bb) override {
        Status s = child_->Tick(bb);
        return s == Status::Running ? Status::Running : Status::Success;
    }
};

// Repeater: 자식을 count 회 반복(count<0 이면 무한, 매 tick 한 번씩 진행).
class Repeater : public Decorator {
public:
    explicit Repeater(int count) : count_(count) {}                 // 빌더용(자식 나중)
    Repeater(NodePtr c, int count) : Decorator(std::move(c)), count_(count) {}
    Status Tick(Blackboard& bb) override {
        Status s = child_->Tick(bb);
        if (s == Status::Running) return Status::Running;
        child_->Reset();
        if (count_ < 0) return Status::Running;                     // 무한 반복
        if (++done_ >= count_) { done_ = 0; return Status::Success; }
        return Status::Running;
    }
    void Reset() override { Decorator::Reset(); done_ = 0; }
private:
    int count_;
    int done_ = 0;
};

// --- Leaf ---
class Action : public Node {
public:
    explicit Action(std::function<Status(Blackboard&)> fn) : fn_(std::move(fn)) {}
    Status Tick(Blackboard& bb) override { return fn_(bb); }
private:
    std::function<Status(Blackboard&)> fn_;
};

class Condition : public Node {
public:
    explicit Condition(std::function<bool(Blackboard&)> fn) : fn_(std::move(fn)) {}
    Status Tick(Blackboard& bb) override { return fn_(bb) ? Status::Success : Status::Failure; }
private:
    std::function<bool(Blackboard&)> fn_;
};

}  // namespace bt
}  // namespace ai
}  // namespace mega
