// sfx_bus — 시뮬이 낸 소리를 카메라 기준의 좌·우 게인으로 옮기는 자리.
//
// 14편이 카메라를 들여오면서 화면은 팬·줌하게 됐지만 소리는 그대로였다. 맵
// 반대편에서 터진 발파와 눈앞의 총성이 같은 크기로, 같은 방향에서 났다 —
// `AudioDevice::Play(Sfx)` 가 낼 수 있는 것이 그것뿐이었기 때문이다.
//
// 여기서 그 자리를 셋으로 나눈다.
//
//   시뮬     소리가 **어디서** 났는지를 안다(game::SoundEvent). 어떻게 들리는지는
//            모른다 — 카메라를 모르기 때문이고, 알아서도 안 된다(결정성).
//   이 버스  카메라를 청취자로 삼아 거리 감쇠와 좌우 패닝을 계산한다.
//   장치     이미 게인이 정해진 보이스들을 한 블록으로 합성해 밀어 넣는다.
//
// 합성이 프레임당 한 번인 이유는 waveOut 의 성질이다 — 버퍼를 여러 개 줄 세우면
// 섞이지 않고 차례로 재생된다. 한 프레임에 총 넷이 나가면 총성 넷이 겹치는 대신
// 줄을 서고, 그 지연이 다음 프레임까지 밀린다.

#pragma once

#include "engine/audio.hpp"
#include "game/camera.hpp"
#include "game/game.hpp"
#include "math/vec.hpp"
#include "platform/audio_device.hpp"

#include <cstdint>
#include <vector>

namespace mega {
namespace game {

// 감쇠가 시작되는 거리(픽셀). 기본 맵의 칸이 30px 이므로 세 칸 안은 감쇠가 없다.
inline constexpr float kAudioRefDistance = 90.0f;
// ortho2d 팔의 청취자 높이. 항등 카메라에는 눈 위치라는 것이 없으므로, 화면
// 중심 위에 떠 있다고 본다 — 그래야 맵 양 끝의 소리가 다른 크기로 들린다.
inline constexpr float kAudioListenerHeight = 260.0f;

inline platform::Sfx SfxFor(SoundKind kind) {
    switch (kind) {
        case SoundKind::Fire:  return platform::Sfx::Hit;
        case SoundKind::Kill:  return platform::Sfx::Death;
        case SoundKind::Blast: return platform::Sfx::Death;
        case SoundKind::Stomp: return platform::Sfx::Step;
    }
    return platform::Sfx::Hit;
}

// 카메라 → 청취자. 기울인 카메라에서는 눈과 우측 기저가 그대로 청취자가 된다.
// snippet-begin: listener-from-camera
inline engine::Listener ListenerFrom(const DefenseCamera& camera) {
    engine::Listener listener;
    if (!camera.Tilted()) {
        const math::Vec2 focus = camera.Focus();
        listener.pos = {focus.x, focus.y, kAudioListenerHeight};
        listener.right = {1.0f, 0.0f, 0.0f};
        return listener;
    }
    listener.pos = camera.Eye();
    // 우측 = 상방 x 전방. 순서를 뒤집으면 좌우가 통째로 바뀐다.
    const math::Vec3 up{0.0f, 0.0f, 1.0f};
    const math::Vec3 right = math::Cross(up, camera.Forward());
    listener.right = math::Length(right) > 1e-4f ? math::Normalize(right)
                                                 : math::Vec3{1.0f, 0.0f, 0.0f};
    return listener;
}
// snippet-end: listener-from-camera

struct SfxVoice {
    math::Vec2    at{0, 0};
    platform::Sfx sfx = platform::Sfx::Hit;
    float         gainL = 0.0f, gainR = 0.0f;
};

class SfxBus {
public:
    // 이번 프레임의 소리를 보이스로 바꾼다. 장치가 없어도 여기까지는 똑같이
    // 돈다 — 헤드리스 검증이 제품과 같은 코드를 재게 하는 것이 그 이유다.
    // snippet-begin: sfx-collect
    void Collect(const std::vector<SoundEvent>& events, const DefenseCamera& camera,
                 const platform::SfxTones& tones) {
        voices_.clear();
        if (events.empty()) return;
        const engine::Listener listener = ListenerFrom(camera);
        for (const SoundEvent& event : events) {
            if (voices_.size() >= engine::kMaxVoices) { ++dropped_; continue; }
            SfxVoice voice;
            voice.at = event.at;
            voice.sfx = SfxFor(event.kind);
            engine::Source source;
            source.pos = {event.at.x, event.at.y, 0.0f};
            source.refDist = kAudioRefDistance;
            source.samples = tones.Samples(voice.sfx, source.count);
            engine::SourceGains(listener, source, voice.gainL, voice.gainR);
            voices_.push_back(voice);
        }
    }
    // snippet-end: sfx-collect

    // 보이스들을 한 스테레오 블록으로 합성해 장치에 넘긴다. 장치가 닫혀 있으면
    // 합성까지만 하고 제출이 no-op 이 된다.
    // snippet-begin: sfx-submit
    void Submit(platform::AudioDevice& device) {
        if (voices_.empty()) return;
        const platform::SfxTones& tones = device.Tones();
        mix_.clear();
        size_t frames = 0;
        for (const SfxVoice& voice : voices_) {
            engine::Voice mixed;
            mixed.samples = tones.Samples(voice.sfx, mixed.count);
            mixed.gainL = voice.gainL;
            mixed.gainR = voice.gainR;
            if (mixed.count > frames) frames = mixed.count;
            mix_.push_back(mixed);
        }
        if (frames == 0) return;
        left_.assign(frames, 0.0f);
        right_.assign(frames, 0.0f);
        engine::MixVoices(mix_.data(), mix_.size(), left_.data(), right_.data(), frames);
        block_.resize(frames * 2);
        for (size_t i = 0; i < frames; ++i) {
            block_[i * 2]     = Quantize(left_[i]);
            block_[i * 2 + 1] = Quantize(right_[i]);
        }
        device.PlaySamples(block_.data(), frames);
        ++blocks_;
    }
    // snippet-end: sfx-submit

    const std::vector<SfxVoice>& Voices() const { return voices_; }
    uint64_t Blocks() const { return blocks_; }
    uint64_t Dropped() const { return dropped_; }
    void Clear() { voices_.clear(); }

private:
    // 동시에 32보이스가 몰리면 합이 1을 크게 넘는다. 클리핑은 소리를 뭉개지만
    // 감싸는 것(wrap)은 폭발음을 낸다 — 둘 중에는 뭉개는 쪽이 옳다.
    static int16_t Quantize(float sample) {
        const float scaled = sample * 8000.0f;
        if (scaled >= 32767.0f) return 32767;
        if (scaled <= -32768.0f) return -32768;
        return static_cast<int16_t>(scaled);
    }

    std::vector<SfxVoice>     voices_;
    std::vector<engine::Voice> mix_;
    std::vector<float>        left_, right_;
    std::vector<int16_t>      block_;
    uint64_t                  blocks_ = 0;
    uint64_t                  dropped_ = 0;
};

}  // namespace game
}  // namespace mega
