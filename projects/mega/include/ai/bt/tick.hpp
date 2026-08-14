// tick — 행동 트리의 실행 결과 상태와 Blackboard(노드 간 공유 메모리).
//
// 모든 노드는 매 tick 마다 Success/Failure/Running 중 하나를 반환한다. Running 은
// "이 행동이 여러 프레임에 걸친다"는 뜻으로, 상위 Composite 가 다음 tick 에 같은
// 자식부터 이어가게 한다. Blackboard 는 조건·행동 노드가 읽고 쓰는 공유 상태다
// (적 위치, 체력, 타이머 등) — 트리 구조와 데이터를 분리한다(ai 시리즈 BT 챕터).

#pragma once

#include "math/vec.hpp"

#include <string>
#include <unordered_map>

namespace mega {
namespace ai {
namespace bt {

enum class Status { Success, Failure, Running };

class Blackboard {
public:
    void  SetFloat(const std::string& k, float v) { floats_[k] = v; }
    float GetFloat(const std::string& k, float def = 0) const {
        auto it = floats_.find(k); return it == floats_.end() ? def : it->second;
    }
    void SetInt(const std::string& k, int v) { ints_[k] = v; }
    int  GetInt(const std::string& k, int def = 0) const {
        auto it = ints_.find(k); return it == ints_.end() ? def : it->second;
    }
    void SetBool(const std::string& k, bool v) { bools_[k] = v; }
    bool GetBool(const std::string& k, bool def = false) const {
        auto it = bools_.find(k); return it == bools_.end() ? def : it->second;
    }
    void       SetVec2(const std::string& k, math::Vec2 v) { vecs_[k] = v; }
    math::Vec2 GetVec2(const std::string& k, math::Vec2 def = {}) const {
        auto it = vecs_.find(k); return it == vecs_.end() ? def : it->second;
    }
    bool Has(const std::string& k) const {
        return floats_.count(k) || ints_.count(k) || bools_.count(k) || vecs_.count(k);
    }

private:
    std::unordered_map<std::string, float>      floats_;
    std::unordered_map<std::string, int>        ints_;
    std::unordered_map<std::string, bool>       bools_;
    std::unordered_map<std::string, math::Vec2> vecs_;
};

}  // namespace bt
}  // namespace ai
}  // namespace mega
