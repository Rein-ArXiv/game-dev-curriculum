// skeleton — 본 계층과 로컬↔월드 변환. 애니메이션의 골격.
//
// 각 본은 부모 기준 로컬 변환(Transform)을 갖는다. 포즈(본별 로컬 변환)가 주어지면
// 부모→자식 순서로 누적해 월드 변환을 만든다(world = parentWorld ∘ local). 본은
// 부모가 자식보다 먼저 추가된다고 가정한다(위상 정렬). 수학 모듈의 Transform 합성을
// 그대로 쓴다.

#pragma once

#include "math/transform.hpp"

#include <vector>

namespace mega {
namespace engine {

using Pose = std::vector<math::Transform>;   // 본별 로컬 변환

class Skeleton {
public:
    // 본 추가. parent=-1 이면 루트. 바인드 포즈 로컬 변환을 함께 준다.
    int AddBone(int parent, const math::Transform& bindLocal) {
        parents_.push_back(parent);
        bind_.push_back(bindLocal);
        return (int)parents_.size() - 1;
    }

    int  BoneCount() const { return (int)parents_.size(); }
    int  Parent(int i) const { return parents_[i]; }
    const Pose& BindPose() const { return bind_; }

    // 로컬 포즈 → 월드 변환. 부모가 먼저 계산됐다고 가정.
    Pose ComputeWorld(const Pose& localPose) const {
        Pose world(parents_.size());
        for (size_t i = 0; i < parents_.size(); ++i) {
            int p = parents_[i];
            world[i] = (p < 0) ? localPose[i] : world[p] * localPose[i];   // 부모 ∘ 로컬
        }
        return world;
    }

private:
    std::vector<int>            parents_;
    Pose                        bind_;
};

}  // namespace engine
}  // namespace mega
