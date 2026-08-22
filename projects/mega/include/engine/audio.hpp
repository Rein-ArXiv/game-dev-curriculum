// audio — 3D 거리 감쇠·스테레오 패닝·다중 소스 믹싱.
//
// 각 소스를 청취자 기준으로 거리에 따라 줄이고(감쇠), 좌우 방향에 따라 패닝해
// 스테레오 버퍼에 누적한다. 여러 소스가 한 출력 버퍼로 모이는 것이 믹서다. 실제
// 사운드 디바이스 대신 *샘플 버퍼* 로 믹싱해 파형을 단언 가능하게 검증한다.
//
// 제품에서는 이 파일의 두 단계가 **계층 경계를 사이에 두고** 나뉘어 돈다.
//
//   SourceGains  게인은 카메라가 있는 곳에서 계산된다(game/sfx_bus.hpp).
//                청취자가 곧 카메라이므로, 카메라를 모르는 층에서는 계산할 수 없다.
//   MixVoices    합성은 파형이 있는 곳에서 일어난다. 파형은 장치가 합성한 톤이라
//                platform/ 에 있고, 게인만 받아 한 버퍼로 접는다.
//
// MixStereo 는 그 둘을 이어 붙인 기준 구현이다. 위치를 넣으면 파형이 나오므로
// 테스트가 파형 수준에서 감쇠·패닝·합성을 한 번에 단언할 수 있다.

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
//
// 마지막 두 줄의 클램프는 장식이 아니다. 3.14159265f 를 float 로 반올림하면
// π 보다 약간 크고, 그래서 pan=+1 에서 angle 이 π/2 를 넘어 cos 가 -4e-8 이 된다.
// 크기로는 무시할 수 있지만 부호가 음수라 그 채널의 **위상이 뒤집힌다** — 같은
// 소리가 좌우로 동시에 나면 서로를 지운다. 0 으로 잘라 그 경로를 없앤다.
inline void PanGains(float pan, float& left, float& right) {
    if (pan < -1) pan = -1; if (pan > 1) pan = 1;
    float angle = (pan + 1.0f) * 0.25f * 3.14159265f;   // 0..π/2
    left  = std::cos(angle);
    right = std::sin(angle);
    if (left < 0.0f) left = 0.0f;
    if (right < 0.0f) right = 0.0f;
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
    // 이 거리 안에서는 감쇠가 없다. 단위가 픽셀인 게임에서 1 로 두면 두 칸만
    // 떨어져도 소리가 사라지므로, 도메인이 이 값을 정해 주어야 한다.
    float               refDist = 1.0f;
};

// 소스 하나의 좌/우 최종 게인(감쇠 × 패닝 × 소스 게인).
// snippet-begin: source-gains
inline void SourceGains(const Listener& l, const Source& s, float& outL, float& outR) {
    math::Vec3 d = s.pos - l.pos;
    float dist = math::Length(d);
    float att = Attenuation(dist, s.refDist) * s.gain;
    float pan = 0.0f;
    if (dist > 1e-5f) pan = math::Dot(d * (1.0f / dist), math::Normalize(l.right));
    float pl, pr;
    PanGains(pan, pl, pr);
    outL = att * pl;
    outR = att * pr;
}
// snippet-end: source-gains

// 동시에 살아 있을 수 있는 보이스 수. 상한이 없는 믹서는 동시 발사가 몰릴 때
// 프레임 하나를 통째로 먹는다 — 소리는 예산을 지켜야 하는 시스템이다.
inline constexpr size_t kMaxVoices = 32;

// 게인이 이미 정해진 보이스. 파형이 있는 층(장치)이 이 형태로 받는다.
struct Voice {
    const float* samples = nullptr;
    size_t       count = 0;
    float        gainL = 0.0f, gainR = 0.0f;
};

// 보이스들을 스테레오 버퍼(outL/outR, 길이 n)에 합성한다. 반환값은 실제로
// 섞은 보이스 수 — 상한에 걸려 버려진 것이 있으면 count 보다 작다.
// snippet-begin: mix-voices
inline size_t MixVoices(const Voice* voices, size_t count, float* outL, float* outR, size_t n) {
    for (size_t i = 0; i < n; ++i) { outL[i] = 0; outR[i] = 0; }
    const size_t mixed = count < kMaxVoices ? count : kMaxVoices;
    for (size_t v = 0; v < mixed; ++v) {
        const Voice& voice = voices[v];
        if (!voice.samples) continue;
        const size_t m = voice.count < n ? voice.count : n;
        for (size_t i = 0; i < m; ++i) {
            outL[i] += voice.samples[i] * voice.gainL;
            outR[i] += voice.samples[i] * voice.gainR;
        }
    }
    return mixed;
}
// snippet-end: mix-voices

// 위치에서 파형까지 한 번에. 두 단계(게인 계산 · 합성)를 이어 붙인 기준 구현이다.
inline void MixStereo(const Listener& listener, const std::vector<Source>& sources,
                      float* outL, float* outR, size_t n) {
    std::vector<Voice> voices;
    voices.reserve(sources.size());
    for (const Source& s : sources) {
        Voice voice;
        voice.samples = s.samples;
        voice.count = s.count;
        SourceGains(listener, s, voice.gainL, voice.gainR);
        voices.push_back(voice);
    }
    MixVoices(voices.data(), voices.size(), outL, outR, n);
}

}  // namespace engine
}  // namespace mega
