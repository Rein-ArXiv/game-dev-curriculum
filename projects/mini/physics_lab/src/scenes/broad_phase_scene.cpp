#include "scenes/broad_phase_scene.h"

#include "core/physics_core.h"
#include "raygui.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>

namespace physlab {

namespace {

constexpr int kGraphCapacity = 600;

// 단순 LCG — 시각화 reproducibility 용. std::mt19937 은 과한 비용.
uint32_t Lcg(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return s;
}

float Rand01(uint32_t& s) {
    return (float)(Lcg(s) >> 8) / (float)(1u << 24);
}

inline bool AabbOverlap(const AabbBody& a, const AabbBody& b) {
    return core::AabbOverlap(
        {a.pos.x, a.pos.y, a.half.x, a.half.y},
        {b.pos.x, b.pos.y, b.half.x, b.half.y});
}

}  // namespace

BroadPhaseScene::BroadPhaseScene() {
    candHist_.Reset(kGraphCapacity);
    msHist_.Reset(kGraphCapacity);
    algoOverlapHist_.Reset(kGraphCapacity);
    gtOverlapHist_.Reset(kGraphCapacity);
}

void BroadPhaseScene::OnEnter() {
    initialized_ = false;
    candHist_.Clear();
    msHist_.Clear();
    algoOverlapHist_.Clear();
    gtOverlapHist_.Clear();
}

void BroadPhaseScene::ResetBodies(int n) {
    bodies_.assign((size_t)n, AabbBody{});
    rngState_ = 1u;
    if (lastCanvasW_ <= 0) return;
    for (auto& b : bodies_) {
        b.half = {bodyHalf_, bodyHalf_};
        b.pos.x = lastCanvasX_ + 16.0f
                   + Rand01(rngState_) * (lastCanvasW_ - 32.0f);
        b.pos.y = lastCanvasY_ + 16.0f
                   + Rand01(rngState_) * (lastCanvasH_ - 32.0f);
        float angle = Rand01(rngState_) * 2.0f * 3.14159265f;
        b.vel = {std::cos(angle) * speed_, std::sin(angle) * speed_};
    }
}

void BroadPhaseScene::HandleInput() {
    if (IsKeyPressed(KEY_R)) ResetBodies(N_);
}

void BroadPhaseScene::RunBrute(int& candidates, int& trueOverlap,
                                 std::vector<OverlapPair>* outPairs) {
    candidates  = 0;
    trueOverlap = 0;
    if (outPairs) outPairs->clear();
    int n = (int)bodies_.size();
    // brute 의 "후보" 는 모든 i < j 쌍 — narrow 가 따로 없는 단순 구조.
    candidates = n * (n - 1) / 2;
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (AabbOverlap(bodies_[(size_t)i], bodies_[(size_t)j])) {
                ++trueOverlap;
                if (outPairs) outPairs->push_back({i, j});
            }
        }
    }
}

void BroadPhaseScene::RunSAP(int& candidates, int& trueOverlap,
                               std::vector<OverlapPair>* outPairs) {
    candidates  = 0;
    trueOverlap = 0;
    if (outPairs) outPairs->clear();

    struct EndPoint { float v; int idx; bool isMin; };
    int n = (int)bodies_.size();
    std::vector<EndPoint> eps;
    eps.reserve((size_t)n * 2);
    for (int i = 0; i < n; ++i) {
        eps.push_back({bodies_[(size_t)i].pos.x - bodies_[(size_t)i].half.x,
                        i, true});
        eps.push_back({bodies_[(size_t)i].pos.x + bodies_[(size_t)i].half.x,
                        i, false});
    }
    // tie-break: 같은 좌표면 min 먼저, max 나중. 경계 접촉을 overlap 으로
    // 보기 위해 min 이 active list 에 먼저 들어가야 한다.
    std::sort(eps.begin(), eps.end(),
               [](const EndPoint& a, const EndPoint& b){
                   if (a.v != b.v) return a.v < b.v;
                   return a.isMin && !b.isMin;
               });

    std::vector<int> active;
    active.reserve(64);
    for (const auto& e : eps) {
        if (e.isMin) {
            // x-overlap 인 모든 active 쌍이 후보.
            for (int idx : active) {
                ++candidates;
                if (AabbOverlap(bodies_[(size_t)e.idx],
                                bodies_[(size_t)idx])) {
                    ++trueOverlap;
                    if (outPairs) {
                        outPairs->push_back(
                            {std::min(e.idx, idx), std::max(e.idx, idx)});
                    }
                }
            }
            active.push_back(e.idx);
        } else {
            auto it = std::find(active.begin(), active.end(), e.idx);
            if (it != active.end()) active.erase(it);
        }
    }
}

void BroadPhaseScene::RunSpatialHash(int& candidates, int& trueOverlap,
                                       std::vector<OverlapPair>* outPairs) {
    candidates  = 0;
    trueOverlap = 0;
    if (outPairs) outPairs->clear();

    // cell coordinate → bucket. uint64_t key 로 hash.
    struct Bucket { std::vector<int> items; };
    std::unordered_map<uint64_t, Bucket> grid;
    grid.reserve(bodies_.size() * 2);

    // unsigned shift 로 작성 — 음수 셀에서도 UB 없음.
    auto cellKey = [](int cx, int cy) -> uint64_t {
        return ((uint64_t)(uint32_t)cx) << 32 | (uint32_t)cy;
    };

    int n   = (int)bodies_.size();
    float c = cellSize_ > 1.0f ? cellSize_ : 1.0f;

    // Phase 1: 등록.
    for (int i = 0; i < n; ++i) {
        const auto& b = bodies_[(size_t)i];
        int minCx = (int)std::floor((b.pos.x - b.half.x) / c);
        int maxCx = (int)std::floor((b.pos.x + b.half.x) / c);
        int minCy = (int)std::floor((b.pos.y - b.half.y) / c);
        int maxCy = (int)std::floor((b.pos.y + b.half.y) / c);
        for (int cy = minCy; cy <= maxCy; ++cy) {
            for (int cx = minCx; cx <= maxCx; ++cx) {
                grid[cellKey(cx, cy)].items.push_back(i);
            }
        }
    }

    // Phase 2: 셀 안 쌍 검사 + 중복 제거.
    std::unordered_set<uint64_t> seen;
    seen.reserve(bodies_.size() * 4);
    for (auto& [key, bucket] : grid) {
        (void)key;
        auto& v = bucket.items;
        for (size_t a = 0; a < v.size(); ++a) {
            for (size_t b = a + 1; b < v.size(); ++b) {
                int i = v[a], j = v[b];
                if (i > j) std::swap(i, j);
                uint64_t pk = ((uint64_t)i << 32) | (uint32_t)j;
                if (seen.insert(pk).second) {
                    ++candidates;
                    if (AabbOverlap(bodies_[(size_t)i], bodies_[(size_t)j])) {
                        ++trueOverlap;
                        if (outPairs) outPairs->push_back({i, j});
                    }
                }
            }
        }
    }
}

void BroadPhaseScene::Step(float dt) {
    // 캔버스 size 가 아직 모를 때 (첫 프레임) skip.
    if (lastCanvasW_ <= 0) return;
    if (!initialized_) {
        ResetBodies(N_);
        initialized_ = true;
    }
    if ((int)bodies_.size() != N_) ResetBodies(N_);

    float left   = (float)lastCanvasX_ + 4.0f;
    float right  = (float)(lastCanvasX_ + lastCanvasW_) - 4.0f;
    float top    = (float)lastCanvasY_ + 4.0f;
    float bottom = (float)(lastCanvasY_ + lastCanvasH_) - 4.0f;

    for (auto& b : bodies_) {
        b.pos.x += b.vel.x * dt;
        b.pos.y += b.vel.y * dt;
        if (b.pos.x - b.half.x < left)   { b.pos.x = left  + b.half.x; b.vel.x = -b.vel.x; }
        if (b.pos.x + b.half.x > right)  { b.pos.x = right - b.half.x; b.vel.x = -b.vel.x; }
        if (b.pos.y - b.half.y < top)    { b.pos.y = top   + b.half.y; b.vel.y = -b.vel.y; }
        if (b.pos.y + b.half.y > bottom) { b.pos.y = bottom - b.half.y; b.vel.y = -b.vel.y; }
    }

    // Ground truth — 알고리즘 결과 검증용. brute 가 항상 정답이라는 전제.
    int gtCandidates = 0, gtOverlap = 0;
    int algoCandidates = 0, algoOverlap = 0;
    double algoUs = 0.0;

    if (algo_ == BroadAlgo::Brute) {
        auto t0 = std::chrono::high_resolution_clock::now();
        RunBrute(gtCandidates, gtOverlap);
        auto t1 = std::chrono::high_resolution_clock::now();
        algoUs = std::chrono::duration<double, std::micro>(t1 - t0).count();
        algoCandidates = gtCandidates;
        algoOverlap    = gtOverlap;
    } else {
        // brute 는 ground truth 전용 — 시간 측정 제외.
        RunBrute(gtCandidates, gtOverlap);

        auto t0 = std::chrono::high_resolution_clock::now();
        switch (algo_) {
        case BroadAlgo::SAP:
            RunSAP(algoCandidates, algoOverlap);
            break;
        case BroadAlgo::SpatialHash:
            RunSpatialHash(algoCandidates, algoOverlap);
            break;
        default:
            break;
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        algoUs = std::chrono::duration<double, std::micro>(t1 - t0).count();
    }

    lastCandidates_  = algoCandidates;
    lastAlgoOverlap_ = algoOverlap;
    lastGroundTruth_ = gtOverlap;
    lastMissed_      = gtOverlap - algoOverlap;
    lastAlgoUs_      = algoUs;

    candHist_.Push((float)algoCandidates);
    msHist_.Push((float)algoUs);
    algoOverlapHist_.Push((float)algoOverlap);
    gtOverlapHist_.Push((float)gtOverlap);
}

void BroadPhaseScene::DrawWorld(int cx, int cy, int cw, int ch) {
    lastCanvasX_ = cx; lastCanvasY_ = cy;
    lastCanvasW_ = cw; lastCanvasH_ = ch;

    DrawRectangle(cx, cy, cw, ch, Color{20, 22, 28, 255});
    DrawRectangleLines(cx, cy, cw, ch, Color{60, 62, 70, 255});

    // 알고리즘별 색.
    switch (algo_) {
    case BroadAlgo::Brute:       algoColor_ = {255, 120, 120, 255}; break;
    case BroadAlgo::SAP:         algoColor_ = {120, 200, 255, 255}; break;
    case BroadAlgo::SpatialHash: algoColor_ = {120, 230, 130, 255}; break;
    }

    // body 들 그리기.
    for (const auto& b : bodies_) {
        DrawRectangle((int)(b.pos.x - b.half.x), (int)(b.pos.y - b.half.y),
                       (int)(b.half.x * 2.0f), (int)(b.half.y * 2.0f),
                       Color{algoColor_.r, algoColor_.g, algoColor_.b, 100});
        DrawRectangleLines((int)(b.pos.x - b.half.x),
                            (int)(b.pos.y - b.half.y),
                            (int)(b.half.x * 2.0f),
                            (int)(b.half.y * 2.0f),
                            algoColor_);
    }

    // SpatialHash 모드면 격자 점선 표시.
    if (algo_ == BroadAlgo::SpatialHash) {
        Color grid = {80, 84, 92, 100};
        for (float x = (float)cx; x < (float)(cx + cw); x += cellSize_) {
            DrawLine((int)x, cy, (int)x, cy + ch, grid);
        }
        for (float y = (float)cy; y < (float)(cy + ch); y += cellSize_) {
            DrawLine(cx, (int)y, cx + cw, (int)y, grid);
        }
    }

    const char* names[3] = {"Brute O(n^2)", "SAP", "Spatial Hash"};
    DrawText(TextFormat("Step 4: BroadPhase = %s  N=%d", names[(int)algo_], N_),
              cx + 10, cy + 10, 14, Color{200, 200, 200, 255});

    // 그래프 — 위: candidates / 알고리즘 overlap / ground truth overlap.
    //          아래: 알고리즘 us.
    Rectangle gr1 = {(float)(cx + cw - 320), (float)(cy + 36), 300.0f, 110.0f};
    Rectangle gr2 = {(float)(cx + cw - 320), (float)(cy + 156), 300.0f, 110.0f};
    GraphInput in1[3] = {
        {&candHist_,        algoColor_,                "candidates"},
        {&algoOverlapHist_, Color{255, 200, 100, 255}, "algo overlaps"},
        {&gtOverlapHist_,   Color{200, 200, 200, 200}, "ground truth"},
    };
    DrawLineGraph(gr1, in1, 3, "Pairs / frame", 0.0f, 1.0f, true);

    GraphInput in2[1] = {
        {&msHist_, algoColor_, "algo us"},
    };
    DrawLineGraph(gr2, in2, 1, "Algorithm time (us)", 0.0f, 1.0f, true);
}

void BroadPhaseScene::DrawPanel(int panelX, int panelY, int panelW, int panelH) {
    (void)panelW; (void)panelH;
    int  y = panelY + 8;
    char buf[160];

    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, "BroadPhase");
    y += 24;

    const char* names[3] = {"Brute O(n^2)", "SAP", "Spatial Hash"};
    for (int i = 0; i < 3; ++i) {
        Rectangle r = {(float)(panelX + 8), (float)y, 240, 22};
        bool selected = ((int)algo_ == i);
        if (selected) DrawRectangleRec(r, Color{60, 100, 160, 255});
        if (GuiButton(r, names[i])) {
            algo_ = (BroadAlgo)i;
            candHist_.Clear();
            msHist_.Clear();
            algoOverlapHist_.Clear();
            gtOverlapHist_.Clear();
        }
        y += 26;
    }
    y += 6;

    std::snprintf(buf, sizeof(buf), "N: %d", N_);
    float nF = (float)N_;
    GuiSliderBar(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                  nullptr, buf, &nF, 10.0f, 2000.0f);
    int newN = (int)nF;
    if (newN != N_) {
        N_ = newN; ResetBodies(N_);
        candHist_.Clear(); msHist_.Clear();
        algoOverlapHist_.Clear(); gtOverlapHist_.Clear();
    }
    y += 22;

    std::snprintf(buf, sizeof(buf), "cell size: %.0f px", cellSize_);
    GuiSliderBar(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                  nullptr, buf, &cellSize_, 8.0f, 200.0f);
    y += 22;

    std::snprintf(buf, sizeof(buf), "body half: %.1f px", bodyHalf_);
    float bhPrev = bodyHalf_;
    GuiSliderBar(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                  nullptr, buf, &bodyHalf_, 2.0f, 30.0f);
    if (bodyHalf_ != bhPrev) {
        for (auto& b : bodies_) b.half = {bodyHalf_, bodyHalf_};
    }
    y += 22;

    std::snprintf(buf, sizeof(buf), "speed: %.0f px/s", speed_);
    float spPrev = speed_;
    GuiSliderBar(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                  nullptr, buf, &speed_, 0.0f, 600.0f);
    if (speed_ != spPrev) {
        // 속도 크기만 갱신, 방향은 유지.
        for (auto& b : bodies_) {
            float s = std::sqrt(b.vel.x * b.vel.x + b.vel.y * b.vel.y);
            if (s > 1e-3f) {
                b.vel.x *= speed_ / s;
                b.vel.y *= speed_ / s;
            }
        }
    }
    y += 26;

    if (GuiButton(Rectangle{(float)(panelX + 8), (float)y, 100, 22},
                   "Reset (R)")) {
        ResetBodies(N_);
        candHist_.Clear();
        msHist_.Clear();
        algoOverlapHist_.Clear();
        gtOverlapHist_.Clear();
    }
    y += 28;

    // 라벨.
    std::snprintf(buf, sizeof(buf), "candidates: %d", lastCandidates_);
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, buf); y += 18;
    std::snprintf(buf, sizeof(buf), "algo overlaps: %d", lastAlgoOverlap_);
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, buf); y += 18;
    std::snprintf(buf, sizeof(buf), "ground truth:  %d", lastGroundTruth_);
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, buf); y += 18;
    if (lastMissed_ != 0) {
        std::snprintf(buf, sizeof(buf), "MISSED: %d (false negative!)",
                       lastMissed_);
        DrawRectangle(panelX + 6, y, 248, 18, Color{120, 30, 30, 200});
        GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, buf);
    } else {
        GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                  "missed: 0  (algo == ground truth)");
    }
    y += 22;
    std::snprintf(buf, sizeof(buf), "algo time: %.1f us", lastAlgoUs_);
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, buf); y += 24;

    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
              "Brute always runs as ground truth.");
    y += 16;
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
              "If 'missed' > 0, algo has a bug.");
    y += 16;
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
              "Compare us per algo across N.");
}

}  // namespace physlab
