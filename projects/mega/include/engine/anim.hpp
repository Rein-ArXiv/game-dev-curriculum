// anim — 클립 샘플링과 포즈 블렌딩. 여러 애니메이션을 합성한다.
//
// 클립은 시각별 포즈 키프레임이다. Sample(t) 는 t 를 감싸는 두 키프레임을 보간
// (위치/스케일은 lerp, 회전은 slerp)한다. 두 포즈를 BlendPoses(a,b,w) 로 섞어
// "걷기→달리기" 같은 전이나 상·하체 분리 블렌딩을 만든다. 회전을 쿼터니언으로
// 들어 김벌락 없이 부드럽게 섞인다.

#pragma once

#include "engine/skeleton.hpp"
#include "math/quat.hpp"
#include "math/vec.hpp"

#include <vector>

namespace mega {
namespace engine {

// 두 포즈를 가중치 w(0=a,1=b)로 본별 블렌딩.
inline Pose BlendPoses(const Pose& a, const Pose& b, float w) {
    size_t n = a.size() < b.size() ? a.size() : b.size();
    Pose out(n);
    for (size_t i = 0; i < n; ++i) {
        out[i].position = a[i].position + (b[i].position - a[i].position) * w;
        out[i].rotation = math::Slerp(a[i].rotation, b[i].rotation, w);
        out[i].scale    = a[i].scale + (b[i].scale - a[i].scale) * w;
    }
    return out;
}

// 시각별 포즈 키프레임 클립.
class AnimClip {
public:
    void AddKey(float time, const Pose& pose) { times_.push_back(time); frames_.push_back(pose); }
    float Duration() const { return times_.empty() ? 0 : times_.back(); }
    size_t KeyCount() const { return times_.size(); }

    // 시각 t 의 포즈를 보간해 반환(범위 밖은 클램프).
    Pose Sample(float t) const {
        if (frames_.empty()) return {};
        if (t <= times_.front()) return frames_.front();
        if (t >= times_.back())  return frames_.back();
        size_t i = 1;
        while (i < times_.size() && times_[i] < t) ++i;
        float span = times_[i] - times_[i - 1];
        float w = span > 0 ? (t - times_[i - 1]) / span : 0.0f;
        return BlendPoses(frames_[i - 1], frames_[i], w);
    }

private:
    std::vector<float> times_;
    std::vector<Pose>  frames_;
};

}  // namespace engine
}  // namespace mega
