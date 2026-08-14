// builder — 행동 트리를 fluent 하게 조립한다.
//
//   auto tree = Builder()
//       .Selector()
//           .Sequence()
//               .Condition(IsEnemyVisible)
//               .Action(Attack)
//           .End()
//           .Action(Patrol)
//       .End()
//       .Build();
//
// Composite 는 End() 로 닫는다. Decorator 는 자식 하나를 붙이면 자동으로 닫힌다
// (Inverter().Condition(...) → Condition 이 Inverter 의 자식이 되고 즉시 pop).

#pragma once

#include "ai/bt/node.hpp"

#include <vector>

namespace mega {
namespace ai {
namespace bt {

class Builder {
public:
    Builder& Sequence() { return Open(std::make_unique<bt::Sequence>(), Kind::Composite); }
    Builder& Selector() { return Open(std::make_unique<bt::Selector>(), Kind::Composite); }
    Builder& Parallel(bool requireAll) { return Open(std::make_unique<bt::Parallel>(requireAll), Kind::Composite); }

    Builder& Inverter()  { return Open(std::make_unique<bt::Inverter>(),  Kind::Decorator); }
    Builder& Succeeder() { return Open(std::make_unique<bt::Succeeder>(), Kind::Decorator); }
    Builder& Repeater(int count) { return Open(std::make_unique<bt::Repeater>(count), Kind::Decorator); }

    Builder& Action(std::function<Status(Blackboard&)> fn) {
        return Leaf(std::make_unique<bt::Action>(std::move(fn)));
    }
    Builder& Condition(std::function<bool(Blackboard&)> fn) {
        return Leaf(std::make_unique<bt::Condition>(std::move(fn)));
    }

    Builder& End() {                          // Composite 닫기
        if (!stack_.empty()) stack_.pop_back();
        return *this;
    }

    NodePtr Build() { return std::move(root_); }

private:
    enum class Kind { Composite, Decorator };
    struct Frame { Node* node; Kind kind; };

    // 현재 부모에 붙인다. 부모가 Decorator 면 자식 설정 후 자동으로 닫는다.
    void Attach(NodePtr node) {
        if (stack_.empty()) { root_ = std::move(node); return; }
        Frame top = stack_.back();
        if (top.kind == Kind::Composite) {
            static_cast<Composite*>(top.node)->Add(std::move(node));
        } else {
            static_cast<Decorator*>(top.node)->SetChild(std::move(node));
            stack_.pop_back();                // Decorator 자동 닫기
        }
    }
    Builder& Open(NodePtr node, Kind kind) {
        Node* raw = node.get();
        Attach(std::move(node));
        stack_.push_back({raw, kind});
        return *this;
    }
    Builder& Leaf(NodePtr node) {
        Attach(std::move(node));
        return *this;
    }

    NodePtr            root_;
    std::vector<Frame> stack_;
};

}  // namespace bt
}  // namespace ai
}  // namespace mega
