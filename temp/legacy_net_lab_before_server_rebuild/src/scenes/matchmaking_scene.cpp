#include "scenes/matchmaking_scene.h"

#include "model/network_model.h"
#include "raygui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace netlab {

namespace {
constexpr float kMaxWaitDisp = 30.0f;   // 룰러 하단 = 30초 대기
}

void MatchmakingScene::OnEnter() { Reset(); }

void MatchmakingScene::Reset() {
    queue_.clear(); flashes_.clear();
    spawnAccum_ = 0.0; totalMatches_ = 0; waitSum_ = diffSum_ = 0.0f;
    matchTimer_ = 0.0f; matchesThisWin_ = 0; matchesPerMinDisp_ = 0;
    rng_ = 555u;
}

int MatchmakingScene::RandMMR() {
    // 4개 균등합 ≈ 정규분포 (중심극한). 중심 1500.
    float s = 0;
    for (int i = 0; i < 4; ++i) s += (float)(Next() % 1000);
    int mmr = (int)(s - 2000 + 1500);   // 평균 1500, 분산 적당
    return std::clamp(mmr, kMinMMR, kMaxMMR);
}

void MatchmakingScene::Update(float dt) {
    if (dt > 0.25f) dt = 0.25f;

    // 신규 플레이어 스폰.
    spawnAccum_ += dt * spawnRate_;
    while (spawnAccum_ >= 1.0f) {
        spawnAccum_ -= 1.0f;
        if ((int)queue_.size() < 400) queue_.push_back({RandMMR(), 0.0f});
    }
    for (auto& p : queue_) p.wait += dt;

    // 매칭 시도: 대기 오래된 순으로, 범위 겹치는 첫 상대와 매칭.
    std::sort(queue_.begin(), queue_.end(),
              [](const Player& a, const Player& b) { return a.wait > b.wait; });
    std::vector<bool> matched(queue_.size(), false);
    for (size_t i = 0; i < queue_.size(); ++i) {
        if (matched[i]) continue;
        int bestJ = -1, bestDiff = 1 << 30;
        for (size_t j = i + 1; j < queue_.size(); ++j) {
            if (matched[j]) continue;
            int diff = std::abs(queue_[i].mmr - queue_[j].mmr);
            if (MutuallyCompatibleMatch(
                    diff, queue_[i].wait, queue_[j].wait,
                    baseWindow_, slope_) &&
                diff < bestDiff) {
                bestDiff = diff; bestJ = (int)j;
            }
        }
        if (bestJ >= 0) {
            matched[i] = matched[(size_t)bestJ] = true;
            ++totalMatches_; ++matchesThisWin_;
            waitSum_ += (queue_[i].wait + queue_[(size_t)bestJ].wait) * 0.5f;
            diffSum_ += bestDiff;
            // 플래시 좌표는 DrawWorld 에서 canvas 기준으로 환산하므로 mmr 만 저장.
            flashes_.push_back({(float)queue_[i].mmr, (float)queue_[(size_t)bestJ].mmr,
                                std::max(queue_[i].wait, queue_[(size_t)bestJ].wait), 0.6f});
        }
    }
    // 매칭된 플레이어 제거.
    std::vector<Player> next;
    next.reserve(queue_.size());
    for (size_t i = 0; i < queue_.size(); ++i)
        if (!matched[i]) next.push_back(queue_[i]);
    queue_.swap(next);

    // 플래시 수명.
    for (auto& f : flashes_) f.life -= dt;
    flashes_.erase(std::remove_if(flashes_.begin(), flashes_.end(),
                                  [](const Flash& f) { return f.life <= 0.0f; }),
                   flashes_.end());

    // matches/min.
    matchTimer_ += dt;
    if (matchTimer_ >= 1.0f) {
        matchesPerMinDisp_ = matchesThisWin_ * 60;
        matchesThisWin_ = 0; matchTimer_ = 0.0f;
    }
}

void MatchmakingScene::DrawWorld(int cx, int cy, int cw, int ch) {
    canvas_ = {(float)cx, (float)cy, (float)cw, (float)ch};
    float left = cx + 60.0f, right = cx + cw - 30.0f;
    float top = cy + 50.0f, bot = cy + ch - 40.0f;
    auto X = [&](float mmr) { return left + (right - left) * (mmr - kMinMMR) / (kMaxMMR - kMinMMR); };
    auto Y = [&](float wait) { return top + (bot - top) * std::clamp(wait / kMaxWaitDisp, 0.0f, 1.0f); };
    float pxPerMMR = (right - left) / (kMaxMMR - kMinMMR);

    // MMR 축 눈금.
    DrawText("MMR ->", (int)left, (int)cy + 8, 14, Color{150, 154, 162, 255});
    for (int m = 0; m <= kMaxMMR; m += 500) {
        DrawLine((int)X((float)m), (int)top - 6, (int)X((float)m), (int)bot, Color{38, 40, 48, 255});
        char b[16]; std::snprintf(b, sizeof(b), "%d", m);
        DrawText(b, (int)X((float)m) - 10, (int)top - 22, 11, Color{120, 124, 132, 255});
    }
    DrawText("대기시간 v (위=방금, 아래=오래)", (int)left - 4, (int)bot + 10, 12,
             Color{150, 154, 162, 255});

    // 플레이어 + 허용 범위 바.
    for (const auto& p : queue_) {
        float y = Y(p.wait);
        float half = AllowedDiff(p.wait) * pxPerMMR;
        DrawLineEx({X((float)p.mmr) - half, y}, {X((float)p.mmr) + half, y}, 1.0f,
                   Color{70, 90, 120, 110});
        DrawCircleV({X((float)p.mmr), y}, 3.5f, Color{120, 180, 235, 230});
    }

    // 매치 플래시 (방금 성사된 쌍 연결선).
    for (const auto& f : flashes_) {
        unsigned char a = (unsigned char)std::clamp(f.life / 0.6f * 230.0f, 0.0f, 230.0f);
        DrawLineEx({X(f.ax), Y(f.y)}, {X(f.bx), Y(f.y)}, 2.5f, Color{120, 220, 150, a});
    }
}

void MatchmakingScene::DrawPanel(int px, int py, int pw, int ph) {
    (void)pw; (void)ph;
    int y = py + 8;
    const float W = 240.0f;
    auto row = [&](int h) { float r = (float)y; y += h; return r; };
    char buf[80];

    GuiLabel(Rectangle{(float)(px + 8), row(22), W, 18}, "Matchmaking (1v1)");

    std::snprintf(buf, sizeof(buf), "spawn = %.1f /s", spawnRate_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr, &spawnRate_, 0.5f, 20.0f);

    std::snprintf(buf, sizeof(buf), "widening = %.0f MMR/s", slope_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr, &slope_, 0.0f, 300.0f);

    std::snprintf(buf, sizeof(buf), "base window = %.0f MMR", baseWindow_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiSlider(Rectangle{(float)(px + 8), row(24), W, 16}, nullptr, nullptr, &baseWindow_, 0.0f, 400.0f);

    if (GuiButton(Rectangle{(float)(px + 8), row(30), W, 24}, "Reset")) Reset();

    y += 8;
    GuiLabel(Rectangle{(float)(px + 8), row(20), W, 18}, "측정");
    std::snprintf(buf, sizeof(buf), "queue size = %d", (int)queue_.size());
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    std::snprintf(buf, sizeof(buf), "matches/min = %d", matchesPerMinDisp_);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    float avgWait = totalMatches_ > 0 ? waitSum_ / totalMatches_ : 0.0f;
    std::snprintf(buf, sizeof(buf), "avg wait = %.1f s", avgWait);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    float avgDiff = totalMatches_ > 0 ? diffSum_ / totalMatches_ : 0.0f;
    std::snprintf(buf, sizeof(buf), "avg MMR diff = %.0f", avgDiff);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, buf);
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, "(diff 작을수록 공정 매치)");

    y += 6;
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, "widening 크게 -> 대기 짧고");
    GuiLabel(Rectangle{(float)(px + 8), row(18), W, 16}, "          매치품질(diff) 나빠짐");
}

}  // namespace netlab
