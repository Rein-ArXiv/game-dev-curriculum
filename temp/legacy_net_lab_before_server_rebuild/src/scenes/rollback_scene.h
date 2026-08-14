#pragma once

#include "scenes/iscene.h"

#include "raylib.h"

#include <cstdint>
#include <vector>

namespace netlab {

// Rollback Scene — Delay-based vs Rollback 넷코드 비교.
// 결정론 2인 시뮬(좌우 이동)에 인공 지연/지터를 주고, 입력 예측 -> 불일치 시
// 롤백 + 재시뮬을 수행한다. 롤백 횟수 / 평균 재시뮬 깊이 / 입력 지연을 측정.
// multiplayer part01 의 이론을 살아 있는 시각화로 만든다.
class RollbackScene : public IScene {
public:
    void OnEnter() override;
    void Update(float dt) override;
    void DrawWorld(int cx, int cy, int cw, int ch) override;
    void DrawPanel(int px, int py, int pw, int ph) override;
    std::string_view Name() const override { return "Rollback"; }

private:
    static constexpr int   kHistory = 512;
    static constexpr float kSpeed   = 0.012f;

    struct Packet { int inputTick; int deliverTick; int input; };

    int  RemoteInput(int tick) const;   // 결정론적 원격 입력 패턴
    int  LocalInput() const;            // 키보드 또는 자동
    bool HasConfirmedRemoteInput(int tick) const;
    int  LastConfirmedRemoteInputBefore(int tick) const;
    void SetRemoteInput(int tick, int input, bool confirmed);
    void StepSim(int tick);             // tick 의 입력으로 상태 진행
    void AdvanceRollback();
    void AdvanceDelay();
    void ResetSim();

    // 링버퍼 상태/입력 (tick % kHistory).
    float ax_[kHistory] = {};
    float bx_[kHistory] = {};
    int   inA_[kHistory] = {};
    int   inB_[kHistory] = {};
    bool  bConfirmed_[kHistory] = {};
    int   bTick_[kHistory] = {};

    int   realTick_     = 0;    // 실시간 경과 tick (벽시계)
    int   curTick_      = 0;    // 시뮬된 최신 tick
    int   confirmedTick_ = -1;  // 원격 입력 확정된 최고 tick
    int   lastConfirmedInput_ = 0;
    std::vector<Packet> inflight_;

    double simAccum_ = 0.0;

    // 토글.
    int   mode_       = 0;      // 0 Rollback, 1 Delay
    float latency_    = 6.0f;   // 단방향 지연(프레임)
    float jitter_     = 2.0f;   // 지터(프레임)
    float inputDelay_ = 2.0f;   // rollback 의도적 입력 지연(프레임)

    // 측정(최근 1초 윈도우).
    int   rollbacksWin_ = 0;
    int   resimDepthWin_ = 0;
    int   mispredictWin_ = 0;
    int   predictedConfirmationsWin_ = 0;
    double windowAccum_ = 0.0;
    int   rollbacksDisp_ = 0;
    float avgDepthDisp_ = 0.0f;
    float mispredictRateDisp_ = 0.0f;
    int   lastRollbackFlash_ = -1000;
    int   lastRollbackTick_  = 0;

    uint32_t rng_ = 12345u;
    uint32_t NextRand() { rng_ = rng_ * 1664525u + 1013904223u; return rng_; }

    Rectangle canvas_{0, 0, 0, 0};
};

}  // namespace netlab
