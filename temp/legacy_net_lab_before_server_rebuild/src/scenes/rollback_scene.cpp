#include "scenes/rollback_scene.h"

#include "raygui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace netlab {

namespace {
constexpr float kSimDt = 1.0f / 60.0f;
int Clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
}  // namespace

void RollbackScene::OnEnter() { ResetSim(); }

void RollbackScene::ResetSim() {
    for (int i = 0; i < kHistory; ++i) {
        ax_[i] = 0.5f; bx_[i] = 0.5f; inA_[i] = 0; inB_[i] = 0;
        bConfirmed_[i] = false; bTick_[i] = -1;
    }
    realTick_ = 0; curTick_ = 0; confirmedTick_ = -1; lastConfirmedInput_ = 0;
    inflight_.clear();
    simAccum_ = 0.0;
    rollbacksWin_ = resimDepthWin_ = mispredictWin_ =
        predictedConfirmationsWin_ = 0;
    windowAccum_ = 0.0; rollbacksDisp_ = 0; avgDepthDisp_ = 0; mispredictRateDisp_ = 0;
    rng_ = 12345u;
}

// 원격 입력: 결정론적. ~40 tick 마다 방향 전환 → 전환 지점에서만 예측 실패.
int RollbackScene::RemoteInput(int tick) const {
    return ((tick / 40) % 2 == 0) ? 1 : -1;
}

int RollbackScene::LocalInput() const {
    if (IsKeyDown(KEY_LEFT))  return -1;
    if (IsKeyDown(KEY_RIGHT)) return 1;
    // 키 입력 없으면 자동 패턴 (다른 주기).
    return ((realTick_ / 55) % 2 == 0) ? 1 : -1;
}

bool RollbackScene::HasConfirmedRemoteInput(int tick) const {
    if (tick < 0) return false;
    int idx = tick % kHistory;
    return bConfirmed_[idx] && bTick_[idx] == tick;
}

int RollbackScene::LastConfirmedRemoteInputBefore(int tick) const {
    int maxBack = std::min(tick, kHistory - 1);
    for (int back = 1; back <= maxBack; ++back) {
        int t = tick - back;
        if (HasConfirmedRemoteInput(t)) return inB_[t % kHistory];
    }
    return lastConfirmedInput_;
}

void RollbackScene::SetRemoteInput(int tick, int input, bool confirmed) {
    int idx = tick % kHistory;
    inB_[idx] = input;
    bConfirmed_[idx] = confirmed;
    bTick_[idx] = tick;
}

void RollbackScene::StepSim(int tick) {
    int idx  = tick % kHistory;
    int nidx = (tick + 1) % kHistory;
    float a = ax_[idx] + inA_[idx] * kSpeed;
    float b = bx_[idx] + inB_[idx] * kSpeed;
    ax_[nidx] = std::clamp(a, 0.0f, 1.0f);
    bx_[nidx] = std::clamp(b, 0.0f, 1.0f);
}

void RollbackScene::Update(float dt) {
    if (dt > 0.25f) dt = 0.25f;
    simAccum_ += dt;
    windowAccum_ += dt;

    while (simAccum_ >= kSimDt) {
        simAccum_ -= kSimDt;

        // 1) 원격이 이번 realTick 의 입력을 생성·송신 (지연+지터 후 도착).
        int delay = Clampi((int)latency_ + (int)(NextRand() % ((uint32_t)jitter_ + 1)),
                           0, kHistory - 2);
        inflight_.push_back({realTick_, realTick_ + delay, RemoteInput(realTick_)});

        // 2) 도착한 패킷 처리 → 확정 + 필요 시 롤백.
        for (size_t i = 0; i < inflight_.size();) {
            if (inflight_[i].deliverTick <= realTick_) {
                Packet p = inflight_[i];
                inflight_[i] = inflight_.back();
                inflight_.pop_back();
                int t = p.inputTick;
                if (t >= 0 && t < curTick_ && (realTick_ - t) < kHistory - 1) {
                    int idx = t % kHistory;
                    bool wasPredicted = !HasConfirmedRemoteInput(t);
                    bool mismatch = (bTick_[idx] != t) || (inB_[idx] != p.input);
                    if (wasPredicted) ++predictedConfirmationsWin_;
                    SetRemoteInput(t, p.input, true);
                    if (t > confirmedTick_) confirmedTick_ = t;
                    if (mode_ == 0 && wasPredicted && mismatch) {
                        // 롤백 + 재시뮬 (t..curTick_).
                        ++rollbacksWin_; ++mispredictWin_;
                        resimDepthWin_ += (curTick_ - t);
                        lastRollbackFlash_ = realTick_;
                        lastRollbackTick_  = t;
                        int prediction = LastConfirmedRemoteInputBefore(t);
                        for (int s = t; s < curTick_; ++s) {
                            int sidx = s % kHistory;
                            if (HasConfirmedRemoteInput(s)) {
                                prediction = inB_[sidx];
                            } else {
                                SetRemoteInput(s, prediction, false);
                            }
                            StepSim(s);
                        }
                        lastConfirmedInput_ = LastConfirmedRemoteInputBefore(curTick_);
                    }
                } else if (t > confirmedTick_) {
                    SetRemoteInput(t, p.input, true);
                    confirmedTick_ = t;
                }
            } else {
                ++i;
            }
        }

        // 3) 모드별 진행.
        if (mode_ == 0) AdvanceRollback();
        else            AdvanceDelay();

        ++realTick_;
    }

    // 측정 윈도우 갱신 (1초).
    if (windowAccum_ >= 1.0) {
        rollbacksDisp_ = rollbacksWin_;
        avgDepthDisp_  = rollbacksWin_ > 0 ? (float)resimDepthWin_ / rollbacksWin_ : 0.0f;
        mispredictRateDisp_ = predictedConfirmationsWin_ > 0
            ? (float)mispredictWin_ / predictedConfirmationsWin_
            : 0.0f;
        rollbacksWin_ = resimDepthWin_ = mispredictWin_ =
            predictedConfirmationsWin_ = 0;
        windowAccum_ = 0.0;
    }
}

void RollbackScene::AdvanceRollback() {
    // 목표: realTick - inputDelay 까지 예측으로 진행.
    int target = std::max(0, realTick_ - (int)inputDelay_);
    while (curTick_ < target) {
        int idx = curTick_ % kHistory;
        inA_[idx] = LocalInput();
        if (!HasConfirmedRemoteInput(curTick_)) {
            SetRemoteInput(curTick_, lastConfirmedInput_, false);   // 예측: 마지막 확정 입력 반복
        } else {
            lastConfirmedInput_ = inB_[idx];
        }
        StepSim(curTick_);
        ++curTick_;
    }
}

void RollbackScene::AdvanceDelay() {
    // 다음 tick 의 원격 입력이 실제로 확정된 경우에만 진행 (입력 지연 발생).
    while (HasConfirmedRemoteInput(curTick_)) {
        int idx = curTick_ % kHistory;
        inA_[idx] = LocalInput();
        lastConfirmedInput_ = inB_[idx];
        StepSim(curTick_);
        ++curTick_;
    }
}

void RollbackScene::DrawWorld(int cx, int cy, int cw, int ch) {
    canvas_ = {(float)cx, (float)cy, (float)cw, (float)ch};
    float left = cx + 60.0f, right = cx + cw - 60.0f;
    float laneA = cy + ch * 0.35f, laneB = cy + ch * 0.6f;
    auto X = [&](float t) { return left + (right - left) * t; };

    int curIdx = curTick_ % kHistory;

    // 트랙.
    DrawLineEx({left, laneA}, {right, laneA}, 2.0f, Color{50, 54, 64, 255});
    DrawLineEx({left, laneB}, {right, laneB}, 2.0f, Color{50, 54, 64, 255});
    DrawText("Local (A)",  (int)left - 4, (int)laneA - 28, 14, Color{120, 220, 150, 255});
    DrawText("Remote (B)", (int)left - 4, (int)laneB + 14, 14, Color{120, 180, 235, 255});

    // 예측 ghost (B): 확정 안 된 현재 tick 이면 표시.
    if (mode_ == 0 && curTick_ > 0 && !HasConfirmedRemoteInput(curTick_ - 1)) {
        DrawCircleV({X(bx_[curIdx]), laneB}, 9.0f, Color{120, 180, 235, 70});
        DrawText("predicted", (int)X(bx_[curIdx]) - 28, (int)laneB - 26, 12,
                 Color{120, 180, 235, 150});
    }

    DrawCircleV({X(ax_[curIdx]), laneA}, 10.0f, Color{120, 220, 150, 255});
    DrawCircleV({X(bx_[curIdx]), laneB}, 10.0f, Color{120, 180, 235, 255});

    // 롤백 플래시.
    if (realTick_ - lastRollbackFlash_ < 18) {
        unsigned char a = (unsigned char)(200 - (realTick_ - lastRollbackFlash_) * 10);
        DrawText("ROLLBACK", (int)(cx + cw * 0.5f) - 40, (int)cy + 16, 20, Color{230, 90, 90, a});
    }

    // tick 룰러 — confirmed / cur / real 간격.
    float ry = cy + ch - 40.0f;
    float span = 90.0f;   // 최근 90 tick 윈도우
    auto TX = [&](int t) {
        float rel = (float)(t - (realTick_ - (int)span)) / span;
        return left + (right - left) * std::clamp(rel, 0.0f, 1.0f);
    };
    DrawLineEx({left, ry}, {right, ry}, 2.0f, Color{50, 54, 64, 255});
    DrawCircleV({TX(confirmedTick_), ry}, 5.0f, Color{120, 180, 235, 255});
    DrawCircleV({TX(curTick_), ry}, 5.0f, Color{120, 220, 150, 255});
    DrawCircleV({TX(realTick_), ry}, 5.0f, Color{220, 200, 90, 255});
    DrawText("confirmed", (int)TX(confirmedTick_) - 30, (int)ry + 10, 11, Color{120, 180, 235, 200});
    DrawText("cur",  (int)TX(curTick_) - 10, (int)ry - 22, 11, Color{120, 220, 150, 200});
    DrawText("real", (int)TX(realTick_) - 12, (int)ry + 10, 11, Color{220, 200, 90, 200});

    DrawText("화살표 키: Local 직접 조작 (안 누르면 자동)", (int)left - 4, (int)cy + 8, 13,
             Color{150, 154, 162, 255});
}

void RollbackScene::DrawPanel(int px, int py, int pw, int ph) {
    (void)pw; (void)ph;
    int y = py + 8;
    const float W = 240.0f;
    auto row = [&](int h) { float r = (float)y; y += h; return r; };
    char buf[80];

    GuiLabel(Rectangle{(float)(px + 8), row(22), W, 18}, "Rollback Netcode");
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, "Mode");
    GuiComboBox(Rectangle{(float)(px + 8), row(26), W, 22}, "Rollback;Delay-based", &mode_);

    std::snprintf(buf, sizeof(buf), "latency = %.0f frames (%.0f ms)", latency_, latency_ * 16.7f);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr, &latency_, 0.0f, 30.0f);

    std::snprintf(buf, sizeof(buf), "jitter = %.0f frames", jitter_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr, &jitter_, 0.0f, 10.0f);

    std::snprintf(buf, sizeof(buf), "input delay = %.0f frames", inputDelay_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr, &inputDelay_, 0.0f, 10.0f);

    if (GuiButton(Rectangle{(float)(px + 8), row(30), W, 24}, "Reset")) ResetSim();

    y += 8;
    GuiLabel(Rectangle{(float)(px + 8), row(20), W, 18}, "측정 (최근 1초)");
    if (mode_ == 0) {
        std::snprintf(buf, sizeof(buf), "rollbacks/s = %d", rollbacksDisp_);
        GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
        std::snprintf(buf, sizeof(buf), "avg resim depth = %.1f", avgDepthDisp_);
        GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
        std::snprintf(buf, sizeof(buf), "mispredict rate = %.0f%%", mispredictRateDisp_ * 100.0f);
        GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
        std::snprintf(buf, sizeof(buf), "local input delay = %.0f frames",
                      inputDelay_);
        GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    } else {
        int lag = std::max(0, realTick_ - curTick_);
        std::snprintf(buf, sizeof(buf), "input lag = %d frames (%.0f ms)", lag, lag * 16.7f);
        GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
        GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, "rollback = 0 (예측 안 함)");
    }

    y += 6;
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16},
             mode_ == 0 ? "Rollback: 즉시 진행 + 불일치 시 되감기" : "Delay: 원격 입력 올 때까지 대기");
}

}  // namespace netlab
