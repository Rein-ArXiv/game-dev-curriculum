#pragma once

#include "scenes/iscene.h"

#include "raylib.h"

#include <cstdint>
#include <vector>

namespace netlab {

// LagSim Scene — 패킷 지연/지터/손실/재정렬 + 수신측 재구성 비교.
// 원본(ground truth) 움직임을 샘플해 패킷으로 보내고, 수신측이 raw / 보간 /
// 외삽(예측)으로 위치를 복원한다. 보간은 부드럽되 지연, 외삽은 빠르되
// 방향 전환에서 오버슈트. multiplayer part01/04 의 지연 보상.
class LagSimScene : public IScene {
public:
    void OnEnter() override;
    void Update(float dt) override;
    void DrawWorld(int cx, int cy, int cw, int ch) override;
    void DrawPanel(int px, int py, int pw, int ph) override;
    std::string_view Name() const override { return "LagSim"; }

private:
    struct Snapshot { double sendTime; Vector2 pos; };
    struct Packet   { double arriveTime; Snapshot snap; bool lost; };

    void Reset();
    Vector2 TruePos(double t) const;
    Vector2 Interpolated(double renderTime) const;
    Vector2 Extrapolated(double now) const;

    double time_ = 0.0;
    double sendAccum_ = 0.0;
    std::vector<Packet>   inflight_;
    std::vector<Snapshot> received_;     // sendTime 정렬 유지

    float latency_   = 0.10f;   // 초
    float jitter_    = 0.04f;   // 초
    float lossPct_   = 5.0f;    // %
    float sendRate_  = 10.0f;   // 초당 스냅샷
    float interpDelay_ = 0.12f; // 보간 지연(초)

    // 측정.
    float interpErrDisp_ = 0.0f;
    float extrapErrDisp_ = 0.0f;
    int   lostCount_ = 0;
    int   reorderCount_ = 0;
    double errAccum_ = 0.0;
    float interpErrSum_ = 0.0f, extrapErrSum_ = 0.0f;
    int   errSamples_ = 0;

    uint32_t rng_ = 99u;
    uint32_t Next() { rng_ = rng_ * 1664525u + 1013904223u; return rng_; }
    float Frand() { return (float)(Next() >> 8) / (float)(1u << 24); }

    Rectangle canvas_{0, 0, 0, 0};
};

}  // namespace netlab
