#include "scenes/lagsim_scene.h"

#include "raygui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace netlab {

namespace {
float Dist(Vector2 a, Vector2 b) {
    float dx = a.x - b.x, dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}
}  // namespace

void LagSimScene::OnEnter() { Reset(); }

void LagSimScene::Reset() {
    time_ = 0.0; sendAccum_ = 0.0;
    inflight_.clear(); received_.clear();
    lostCount_ = reorderCount_ = 0;
    errAccum_ = 0.0; interpErrSum_ = extrapErrSum_ = 0.0f; errSamples_ = 0;
    interpErrDisp_ = extrapErrDisp_ = 0.0f;
    rng_ = 99u;
}

// 원본 움직임: 리사주(Lissajous) 곡선. 방향 전환이 잦아 외삽 오차가 잘 보인다.
Vector2 LagSimScene::TruePos(double t) const {
    return { 0.5f + 0.38f * (float)std::sin(t * 1.3),
             0.5f + 0.30f * (float)std::sin(t * 2.1 + 0.7) };
}

Vector2 LagSimScene::Interpolated(double renderTime) const {
    if (received_.empty()) return TruePos(renderTime);
    // renderTime 을 감싸는 두 스냅샷 사이 lerp.
    for (size_t i = received_.size(); i-- > 0;) {
        if (received_[i].sendTime <= renderTime) {
            if (i + 1 < received_.size()) {
                const Snapshot& a = received_[i];
                const Snapshot& b = received_[i + 1];
                double span = b.sendTime - a.sendTime;
                float u = span > 1e-6 ? (float)((renderTime - a.sendTime) / span) : 0.0f;
                u = std::clamp(u, 0.0f, 1.0f);
                return { a.pos.x + (b.pos.x - a.pos.x) * u,
                         a.pos.y + (b.pos.y - a.pos.y) * u };
            }
            return received_[i].pos;   // 최신 이후 = 마지막 값 유지
        }
    }
    return received_.front().pos;
}

Vector2 LagSimScene::Extrapolated(double now) const {
    if (received_.size() < 2) return received_.empty() ? TruePos(now) : received_.back().pos;
    const Snapshot& a = received_[received_.size() - 2];
    const Snapshot& b = received_[received_.size() - 1];
    double span = b.sendTime - a.sendTime;
    if (span < 1e-6) return b.pos;
    float vx = (b.pos.x - a.pos.x) / (float)span;
    float vy = (b.pos.y - a.pos.y) / (float)span;
    float ahead = (float)(now - b.sendTime);
    return { b.pos.x + vx * ahead, b.pos.y + vy * ahead };
}

void LagSimScene::Update(float dt) {
    if (dt > 0.1f) dt = 0.1f;
    time_ += dt;

    // 스냅샷 송신.
    sendAccum_ += dt * sendRate_;
    while (sendAccum_ >= 1.0f) {
        sendAccum_ -= 1.0f;
        bool lost = (Frand() * 100.0f) < lossPct_;
        float jit = (Frand() * 2.0f - 1.0f) * jitter_;
        double arrive = std::max(time_, time_ + latency_ + jit);
        inflight_.push_back({arrive, {time_, TruePos(time_)}, lost});
        if (lost) ++lostCount_;
    }

    // 도착 처리 (재정렬 가능).
    for (size_t i = 0; i < inflight_.size();) {
        if (inflight_[i].arriveTime <= time_) {
            Packet p = inflight_[i];
            inflight_[i] = inflight_.back();
            inflight_.pop_back();
            if (!p.lost) {
                // 정렬 삽입 — 재정렬(늦게 보낸 게 먼저 도착) 감지.
                if (!received_.empty() && p.snap.sendTime < received_.back().sendTime)
                    ++reorderCount_;
                auto it = std::lower_bound(received_.begin(), received_.end(), p.snap.sendTime,
                    [](const Snapshot& s, double t) { return s.sendTime < t; });
                received_.insert(it, p.snap);
                if (received_.size() > 256) received_.erase(received_.begin());
            }
        } else {
            ++i;
        }
    }

    // 오차 측정 (보간은 renderTime 기준, 외삽은 now 기준).
    double renderTime = time_ - interpDelay_;
    interpErrSum_ += Dist(Interpolated(renderTime), TruePos(renderTime));
    extrapErrSum_ += Dist(Extrapolated(time_), TruePos(time_));
    ++errSamples_;
    errAccum_ += dt;
    if (errAccum_ >= 0.5) {
        interpErrDisp_ = errSamples_ ? interpErrSum_ / errSamples_ : 0.0f;
        extrapErrDisp_ = errSamples_ ? extrapErrSum_ / errSamples_ : 0.0f;
        interpErrSum_ = extrapErrSum_ = 0.0f; errSamples_ = 0; errAccum_ = 0.0;
    }
}

void LagSimScene::DrawWorld(int cx, int cy, int cw, int ch) {
    canvas_ = {(float)cx, (float)cy, (float)cw, (float)ch};
    float pad = 30.0f;
    float box = std::min(cw - 2 * pad, ch - 2 * pad - 70.0f);
    Rectangle b{cx + (cw - box) * 0.5f, cy + 40.0f, box, box};
    auto P = [&](Vector2 p) { return Vector2{b.x + p.x * b.width, b.y + p.y * b.height}; };

    DrawRectangleLinesEx(b, 1.5f, Color{60, 64, 74, 255});

    // 원본 경로(최근 궤적) faint.
    Vector2 prev = P(TruePos(time_ - 2.0));
    for (int i = 1; i <= 60; ++i) {
        double t = time_ - 2.0 + 2.0 * i / 60.0;
        Vector2 cur = P(TruePos(t));
        DrawLineEx(prev, cur, 1.0f, Color{50, 54, 64, 255});
        prev = cur;
    }

    double renderTime = time_ - interpDelay_;
    Vector2 truth  = P(TruePos(time_));
    Vector2 raw    = received_.empty() ? truth : P(received_.back().pos);
    Vector2 interp = P(Interpolated(renderTime));
    Vector2 extrap = P(Extrapolated(time_));

    DrawCircleV(truth,  8.0f, Color{235, 235, 235, 255});           // 흰: 원본
    DrawCircleV(raw,    5.0f, Color{200, 120, 90, 230});            // 주황: raw
    DrawCircleV(interp, 5.0f, Color{120, 180, 235, 240});          // 파랑: 보간
    DrawCircleV(extrap, 5.0f, Color{120, 220, 150, 240});          // 초록: 외삽

    // 범례.
    int lx = cx + 16, ly = cy + 8;
    DrawText("● truth", lx, ly, 14, Color{235, 235, 235, 255});
    DrawText("● raw (snap)", lx + 80, ly, 14, Color{200, 120, 90, 255});
    DrawText("● interpolate", lx + 200, ly, 14, Color{120, 180, 235, 255});
    DrawText("● extrapolate", lx + 330, ly, 14, Color{120, 220, 150, 255});

    // 패킷 타임라인 (최근 2초): 도착 스냅샷 + 손실 표시.
    float ty = cy + ch - 24.0f;
    float tl = b.x, tr = b.x + b.width;
    DrawLineEx({tl, ty}, {tr, ty}, 2.0f, Color{50, 54, 64, 255});
    auto TX = [&](double st) {
        float rel = (float)((st - (time_ - 2.0)) / 2.0);
        return tl + (tr - tl) * std::clamp(rel, 0.0f, 1.0f);
    };
    for (const auto& s : received_)
        if (s.sendTime > time_ - 2.0)
            DrawLine((int)TX(s.sendTime), (int)ty - 8, (int)TX(s.sendTime), (int)ty + 8,
                     Color{120, 180, 235, 200});
    DrawText("packet timeline (수신=파랑 마커)", (int)tl, (int)ty - 26, 12,
             Color{150, 154, 162, 255});
}

void LagSimScene::DrawPanel(int px, int py, int pw, int ph) {
    (void)pw; (void)ph;
    int y = py + 8;
    const float W = 240.0f;
    auto row = [&](int h) { float r = (float)y; y += h; return r; };
    char buf[80];

    GuiLabel(Rectangle{(float)(px + 8), row(22), W, 18}, "Lag Simulation");

    std::snprintf(buf, sizeof(buf), "latency = %.0f ms", latency_ * 1000.0f);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr, &latency_, 0.0f, 0.4f);

    std::snprintf(buf, sizeof(buf), "jitter = %.0f ms", jitter_ * 1000.0f);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr, &jitter_, 0.0f, 0.15f);

    std::snprintf(buf, sizeof(buf), "loss = %.0f %%", lossPct_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr, &lossPct_, 0.0f, 40.0f);

    std::snprintf(buf, sizeof(buf), "send rate = %.0f /s", sendRate_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr, &sendRate_, 2.0f, 30.0f);

    std::snprintf(buf, sizeof(buf), "interp delay = %.0f ms", interpDelay_ * 1000.0f);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr, &interpDelay_, 0.0f, 0.3f);

    if (GuiButton(Rectangle{(float)(px + 8), row(30), W, 24}, "Reset")) Reset();

    y += 8;
    GuiLabel(Rectangle{(float)(px + 8), row(20), W, 18}, "측정 (원본과의 평균 오차)");
    std::snprintf(buf, sizeof(buf), "interpolate err = %.3f", interpErrDisp_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    std::snprintf(buf, sizeof(buf), "extrapolate err = %.3f", extrapErrDisp_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    std::snprintf(buf, sizeof(buf), "lost = %d   reorder = %d", lostCount_, reorderCount_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);

    y += 6;
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, "보간: 부드럽되 과거를 그림(지연)");
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, "외삽: 현재 추정하되 전환서 오버슈트");
}

}  // namespace netlab
