#pragma once

#include "scenes/iscene.h"
#include "model/network_model.h"

#include "raylib.h"

#include <cstdint>
#include <deque>
#include <vector>

namespace netlab {

// Prediction Scene — 권위 서버 모델의 클라이언트 예측 + 서버 보정.
// 로컬 입력을 서버 왕복 없이 즉시 예측 적용하고, 서버 권위 스냅샷이 오면
// 그 상태로 리셋 + 미확정 입력 재적용(reconciliation). "예측 끄기"와 비교하면
// 입력 지연이 RTT 만큼 생긴다. 서버 전용 제약(클라 모름)으로 보정도 관찰.
// multiplayer part07 의 이론을 살아 있는 시각화로 만든다.
class PredictionScene : public IScene {
public:
    void OnEnter() override;
    void Update(float dt) override;
    void DrawWorld(int cx, int cy, int cw, int ch) override;
    void DrawPanel(int px, int py, int pw, int ph) override;
    std::string_view Name() const override { return "Prediction"; }

private:
    static constexpr float kSpeed = 0.012f;

    struct InputPkt { int seq; int input; int deliverTick; };
    struct SnapPkt  { float serverX; int lastSeq; int deliverTick; };

    int   LocalInput() const;
    float ClientApply(float x, int input) const;   // 클라 모델(벽 모름)
    float ServerApply(float x, int input) const;   // 서버 모델(벽 권위)
    void  Reset();

    // 클라이언트 측.
    float predictedX_ = 0.5f;
    std::deque<InputPkt> pending_;
    int   nextSeq_ = 0;
    float lastServerX_ = 0.5f;
    int   clientAckSeq_ = -1;

    // 서버 측.
    float serverX_ = 0.5f;
    int   serverProcessedSeq_ = -1;
    OrderedInputBuffer serverInputBuffer_;

    // 네트워크.
    std::vector<InputPkt> toServer_;
    std::vector<SnapPkt>  toClient_;
    int   realTick_ = 0;
    double accum_ = 0.0;

    // 토글.
    int   mode_     = 0;       // 0 예측+보정, 1 예측 없음
    int   lastMode_ = 0;
    float latency_  = 6.0f;    // 단방향 지연(프레임)
    float jitter_   = 1.0f;
    bool  serverWall_ = false; // 서버만 아는 벽(x>=0.7 차단) → 보정 유발

    // 측정.
    float correctionDisp_ = 0.0f;
    float correctionSmooth_ = 0.0f;
    int   lastCorrectionFlash_ = -1000;

    uint32_t rng_ = 31u;
    uint32_t Next() { rng_ = rng_ * 1664525u + 1013904223u; return rng_; }

    Rectangle canvas_{0, 0, 0, 0};
};

}  // namespace netlab
