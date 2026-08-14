#include "scenes/prediction_scene.h"

#include "raygui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace netlab {

namespace {
constexpr float kSimDt = 1.0f / 60.0f;
constexpr float kWallX = 0.7f;
}  // namespace

void PredictionScene::OnEnter() { Reset(); }

void PredictionScene::Reset() {
    predictedX_ = serverX_ = lastServerX_ = 0.5f;
    pending_.clear(); toServer_.clear(); toClient_.clear();
    serverInputBuffer_.Reset();
    nextSeq_ = 0; serverProcessedSeq_ = -1; realTick_ = 0; accum_ = 0.0;
    clientAckSeq_ = -1;
    lastMode_ = mode_;
    correctionDisp_ = correctionSmooth_ = 0.0f; lastCorrectionFlash_ = -1000;
    rng_ = 31u;
}

int PredictionScene::LocalInput() const {
    if (IsKeyDown(KEY_LEFT))  return -1;
    if (IsKeyDown(KEY_RIGHT)) return 1;
    return ((realTick_ / 50) % 2 == 0) ? 1 : -1;   // 자동 패턴
}

float PredictionScene::ClientApply(float x, int input) const {
    return std::clamp(x + input * kSpeed, 0.0f, 1.0f);   // 벽 모름
}

float PredictionScene::ServerApply(float x, int input) const {
    float nx = std::clamp(x + input * kSpeed, 0.0f, 1.0f);
    if (serverWall_ && nx > kWallX) nx = kWallX;          // 서버 권위 벽
    return nx;
}

void PredictionScene::Update(float dt) {
    if (mode_ != lastMode_) {
        Reset();
    }
    if (dt > 0.25f) dt = 0.25f;
    accum_ += dt;

    while (accum_ >= kSimDt) {
        accum_ -= kSimDt;
        const int outboundDelay =
            SampleDelayFrames(latency_, jitter_, Next());

        // --- 클라이언트: 입력 생성 ---
        int in = LocalInput();
        InputPkt pkt{nextSeq_++, in, realTick_ + outboundDelay};
        toServer_.push_back(pkt);
        if (mode_ == 0) {
            // 예측: 즉시 로컬 적용 + 미확정 버퍼.
            pending_.push_back(pkt);
            predictedX_ = ClientApply(predictedX_, in);
        }

        // --- 서버: 도착한 입력 처리 ---
        for (size_t i = 0; i < toServer_.size();) {
            if (toServer_[i].deliverTick <= realTick_) {
                InputPkt p = toServer_[i];
                toServer_[i] = toServer_.back(); toServer_.pop_back();
                if (p.seq > serverProcessedSeq_)
                    serverInputBuffer_.Receive(p.seq, p.input);
            } else { ++i; }
        }

        // cumulative ACK 가 뜻을 가지려면 모든 입력을 연속 sequence 순서로
        // 적용해야 한다. 재정렬로 생긴 구멍은 해당 패킷이 도착할 때까지 보류한다.
        serverInputBuffer_.Drain([&](int input) {
            serverX_ = ServerApply(serverX_, input);
        });
        serverProcessedSeq_ = serverInputBuffer_.LastApplied();

        // 서버 → 클라 스냅샷 (매 tick).
        const int back = SampleDelayFrames(latency_, jitter_, Next());
        toClient_.push_back({serverX_, serverProcessedSeq_, realTick_ + back});

        // --- 클라이언트: 스냅샷 수신 + 보정 ---
        for (size_t i = 0; i < toClient_.size();) {
            if (toClient_[i].deliverTick <= realTick_) {
                SnapPkt s = toClient_[i];
                toClient_[i] = toClient_.back(); toClient_.pop_back();
                if (!AcceptCumulativeAck(s.lastSeq, clientAckSeq_)) continue;
                lastServerX_ = s.serverX;
                if (mode_ == 0) {
                    float beforeX = predictedX_;
                    predictedX_ = ReconcileScalar(
                        s.serverX, s.lastSeq, pending_,
                        [](const InputPkt& p) { return p.seq; },
                        [](const InputPkt& p) { return p.input; },
                        [&](float x, int input) {
                            return ClientApply(x, input);
                        });
                    float corr = std::fabs(predictedX_ - beforeX);
                    correctionSmooth_ =
                        correctionSmooth_ * 0.9f + corr * 0.1f;
                    if (corr > 0.01f) lastCorrectionFlash_ = realTick_;
                }
            } else { ++i; }
        }

        ++realTick_;
    }
    correctionDisp_ = correctionSmooth_;
}

void PredictionScene::DrawWorld(int cx, int cy, int cw, int ch) {
    canvas_ = {(float)cx, (float)cy, (float)cw, (float)ch};
    float left = cx + 60.0f, right = cx + cw - 60.0f;
    float lane = cy + ch * 0.45f;
    auto X = [&](float t) { return left + (right - left) * t; };

    DrawText(mode_ == 0 ? "예측 ON: green=내 화면(즉시), blue=서버 권위(지연)"
                        : "예측 OFF: 서버 상태만 그림 -> 입력이 RTT 만큼 늦음",
             (int)left - 4, (int)cy + 12, 14, Color{180, 184, 192, 255});

    // 트랙.
    DrawLineEx({left, lane}, {right, lane}, 2.0f, Color{50, 54, 64, 255});

    // 서버 벽 표시.
    if (serverWall_) {
        DrawLineEx({X(kWallX), lane - 40}, {X(kWallX), lane + 40}, 2.0f, Color{200, 120, 90, 200});
        DrawText("server wall (client unaware)", (int)X(kWallX) - 60, (int)lane - 58, 12,
                 Color{200, 120, 90, 220});
    }

    // 서버 권위 위치 (파랑) — 항상 표시.
    DrawCircleV({X(lastServerX_), lane + 22}, 9.0f, Color{120, 180, 235, 255});
    DrawText("server", (int)X(lastServerX_) - 18, (int)lane + 34, 12, Color{120, 180, 235, 220});

    // 클라이언트 렌더 위치.
    float clientX = (mode_ == 0) ? predictedX_ : lastServerX_;
    DrawCircleV({X(clientX), lane - 22}, 10.0f, Color{120, 220, 150, 255});
    DrawText(mode_ == 0 ? "predicted (you see)" : "rendered (laggy)",
             (int)X(clientX) - 50, (int)lane - 52, 12, Color{120, 220, 150, 220});

    // 보정 플래시.
    if (mode_ == 0 && realTick_ - lastCorrectionFlash_ < 24) {
        unsigned char a = (unsigned char)std::max(0, 220 - (realTick_ - lastCorrectionFlash_) * 9);
        DrawText("RECONCILE 보정!", (int)(cx + cw * 0.5f) - 60, (int)cy + 40, 18,
                 Color{230, 90, 90, a});
    }

    DrawText("화살표 키: 직접 조작 (안 누르면 자동)", (int)left - 4, (int)cy + ch - 28, 13,
             Color{150, 154, 162, 255});
}

void PredictionScene::DrawPanel(int px, int py, int pw, int ph) {
    (void)pw; (void)ph;
    int y = py + 8;
    const float W = 240.0f;
    auto row = [&](int h) { float r = (float)y; y += h; return r; };
    char buf[80];

    GuiLabel(Rectangle{(float)(px + 8), row(22), W, 18}, "Client Prediction");
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, "Mode");
    GuiComboBox(Rectangle{(float)(px + 8), row(26), W, 22},
                "예측+보정;예측 없음", &mode_);

    std::snprintf(buf, sizeof(buf), "latency = %.0f frames (%.0f ms)", latency_, latency_ * 16.7f);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr, &latency_, 0.0f, 30.0f);

    std::snprintf(buf, sizeof(buf), "jitter = %.0f frames", jitter_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr, &jitter_, 0.0f, 8.0f);

    GuiCheckBox(Rectangle{(float)(px + 8), row(24), 16, 16},
                " server wall (보정 유발)", &serverWall_);

    if (GuiButton(Rectangle{(float)(px + 8), row(30), W, 24}, "Reset")) Reset();

    y += 8;
    GuiLabel(Rectangle{(float)(px + 8), row(20), W, 18}, "측정");
    int rtt = (int)(latency_ * 2);
    if (mode_ == 0) {
        GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, "input lag = ~0 (즉시 예측)");
        std::snprintf(buf, sizeof(buf), "pending inputs = %d", (int)pending_.size());
        GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
        std::snprintf(buf, sizeof(buf), "avg correction = %.4f", correctionDisp_);
        GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    } else {
        std::snprintf(buf, sizeof(buf), "input lag = ~%d frames (%.0f ms)", rtt, rtt * 16.7f);
        GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
        GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, "(서버 왕복 후에야 반영)");
    }

    y += 6;
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, "예측: 권위 유지 + 즉시 반응.");
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, "벽 켜면 예측 빗나가 보정(되감김).");
}

}  // namespace netlab
