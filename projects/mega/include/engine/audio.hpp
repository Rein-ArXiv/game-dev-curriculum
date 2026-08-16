// audio — 3D 거리 감쇠·스테레오 패닝·다중 소스 믹싱.
//
// 각 소스를 청취자 기준으로 거리에 따라 줄이고(감쇠), 좌우 방향에 따라 패닝해
// 스테레오 버퍼에 누적한다. 여러 소스가 한 출력 버퍼로 모이는 것이 믹서다. 실제
// 사운드 디바이스 대신 *샘플 버퍼* 로 믹싱해 파형을 단언 가능하게 검증한다.
// 이 저장소의 디바이스 출력은 platform/audio_device.hpp(winmm waveOut)이고, 그쪽은
// 미리 합성한 톤을 직접 재생하므로 이 믹서를 거치지 않는다(docs/LAB_ONLY.md 참조).

#pragma once

#include "math/vec.hpp"

#include <cmath>
#include <cstddef>
#include <vector>

namespace mega {
namespace engine {

// 거리 감쇠(역거리 모델). refDist 안은 1, 멀수록 1/(1+거리/ref).
inline float Attenuation(float dist, float refDist = 1.0f) {
    if (dist <= refDist) return 1.0f;
    return refDist / (refDist + (dist - refDist));
}

// 패닝 게인(equal-power). pan ∈ [-1(좌), +1(우)] → (left, right), left²+right²≈1.
inline void PanGains(float pan, float& left, float& right) {
    if (pan < -1) pan = -1; if (pan > 1) pan = 1;
    float angle = (pan + 1.0f) * 0.25f * 3.14159265f;   // 0..π/2
    left  = std::cos(angle);
    right = std::sin(angle);
}

struct Listener {
    math::Vec3 pos{0, 0, 0};
    math::Vec3 right{1, 0, 0};   // 청취자 우측 방향(패닝 기준)
};

struct Source {
    math::Vec3          pos{0, 0, 0};
    const float*        samples = nullptr;
    size_t              count = 0;
    float               gain = 1.0f;
};

// 소스 하나의 좌/우 최종 게인(감쇠 × 패닝 × 소스 게인).
inline void SourceGains(const Listener& l, const Source& s, float& outL, float& outR) {
    math::Vec3 d = s.pos - l.pos;
    float dist = math::Length(d);
    float att = Attenuation(dist) * s.gain;
    float pan = 0.0f;
    if (dist > 1e-5f) pan = math::Dot(d * (1.0f / dist), math::Normalize(l.right));
    float pl, pr;
    PanGains(pan, pl, pr);
    outL = att * pl;
    outR = att * pr;
}

// 여러 소스를 스테레오 버퍼(outL/outR, 길이 n)에 믹싱.
inline void MixStereo(const Listener& listener, const std::vector<Source>& sources,
                      float* outL, float* outR, size_t n) {
    for (size_t i = 0; i < n; ++i) { outL[i] = 0; outR[i] = 0; }
    for (const Source& s : sources) {
        float gl, gr;
        SourceGains(listener, s, gl, gr);
        size_t m = s.count < n ? s.count : n;
        for (size_t i = 0; i < m; ++i) {
            outL[i] += s.samples[i] * gl;
            outR[i] += s.samples[i] * gr;
        }
    }
}

}  // namespace engine
}  // namespace mega
