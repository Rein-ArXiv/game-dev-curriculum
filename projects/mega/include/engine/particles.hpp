// particles — VFX 파티클 시스템. 이미터·수명·페이드.
//
// 이미터가 파티클을 뿜고, 각 파티클은 속도로 움직이며 수명이 줄고, 수명이 다하면
// 사라진다. 알파는 남은 수명 비율로 페이드아웃한다. 폭발·먼지·발사 궤적 같은
// 효과의 기본 단위다. 갱신은 죽은 파티클을 swap-remove 로 압축해 O(n).

#pragma once

#include "math/vec.hpp"

#include <cstddef>
#include <vector>

namespace mega {
namespace engine {

struct Particle {
    math::Vec3 pos{0, 0, 0};
    math::Vec3 vel{0, 0, 0};
    float      life = 0;      // 남은 수명(초)
    float      maxLife = 1;
    float      size = 1;
};

class ParticleSystem {
public:
    void Emit(const math::Vec3& pos, const math::Vec3& vel, float life, float size = 1.0f) {
        Particle p; p.pos = pos; p.vel = vel; p.life = life; p.maxLife = life; p.size = size;
        particles_.push_back(p);
    }

    void Update(float dt, const math::Vec3& gravity = {0, 0, 0}) {
        for (size_t i = 0; i < particles_.size();) {
            Particle& p = particles_[i];
            p.vel += gravity * dt;
            p.pos += p.vel * dt;
            p.life -= dt;
            if (p.life <= 0.0f) {
                particles_[i] = particles_.back();   // swap-remove
                particles_.pop_back();
            } else {
                ++i;
            }
        }
    }

    // 알파(0~1): 남은 수명 비율. 수명 끝으로 갈수록 0.
    static float Alpha(const Particle& p) { return p.maxLife > 0 ? p.life / p.maxLife : 0; }

    size_t AliveCount() const { return particles_.size(); }
    const std::vector<Particle>& Particles() const { return particles_; }
    void Clear() { particles_.clear(); }

private:
    std::vector<Particle> particles_;
};

}  // namespace engine
}  // namespace mega
