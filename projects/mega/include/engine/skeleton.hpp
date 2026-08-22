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
    //
    // 부모는 **자기보다 앞선 인덱스**여야 한다. ComputeWorld 가 한 번의 전방
    // 순회로 world[p] 를 읽기 때문이고, 그 전제가 깨지면 아직 계산되지 않은
    // (기본 구성인) 부모 변환으로 자식이 합성돼 조용히 틀린 포즈가 나온다.
    // 위상 정렬을 "가정"으로 두는 대신 여기서 거절한다 — 거절된 본은 추가되지
    // 않고 -1 이 돌아온다.
    // snippet-begin: add-bone
    int AddBone(int parent, const math::Transform& bindLocal) {
        if (parent >= (int)parents_.size()) return -1;   // 아직 없는 본을 부모로
        if (parent < -1) return -1;
        parents_.push_back(parent);
        bind_.push_back(bindLocal);
        return (int)parents_.size() - 1;
    }
    // snippet-end: add-bone

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
