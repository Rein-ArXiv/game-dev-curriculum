#include "scenes/narrow_phase_scene.h"

#include "raygui.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>

namespace physlab {

namespace {

constexpr int   kGraphCapacity = 600;
constexpr float kPi            = 3.14159265358979f;

inline float Dot(Vector2 a, Vector2 b) { return a.x*b.x + a.y*b.y; }
inline float Cross(Vector2 a, Vector2 b) { return a.x*b.y - a.y*b.x; }
inline Vector2 Sub(Vector2 a, Vector2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vector2 Neg(Vector2 v) { return {-v.x, -v.y}; }

// Triple product (a × b) × c in 2D, returns vector perpendicular to c in plane.
// Identity: (a × b) × c = b(a·c) − a(b·c)  (Lagrange).
inline Vector2 Triple(Vector2 a, Vector2 b, Vector2 c) {
    float ac = Dot(a, c);
    float bc = Dot(b, c);
    return {b.x * ac - a.x * bc, b.y * ac - a.y * bc};
}

inline Vector2 SupportCSO(const Polygon2D& a, const Polygon2D& b, Vector2 dir) {
    Vector2 sa = a.Support(dir);
    Vector2 sb = b.Support(Neg(dir));
    return Sub(sa, sb);
}

}  // namespace

// ===== Polygon2D =====

Vector2 Polygon2D::ToWorld(Vector2 lp) const {
    float c = std::cos(angle), s = std::sin(angle);
    return {pos.x + c * lp.x - s * lp.y,
             pos.y + s * lp.x + c * lp.y};
}

std::vector<Vector2> Polygon2D::WorldVerts() const {
    std::vector<Vector2> r;
    r.reserve(local.size());
    for (const auto& v : local) r.push_back(ToWorld(v));
    return r;
}

Vector2 Polygon2D::Support(Vector2 dir) const {
    // dir 을 local frame 으로 회전.
    float c = std::cos(-angle), s = std::sin(-angle);
    Vector2 dLocal = {c * dir.x - s * dir.y, s * dir.x + c * dir.y};

    int   best    = 0;
    float bestDot = local[0].x * dLocal.x + local[0].y * dLocal.y;
    for (size_t i = 1; i < local.size(); ++i) {
        float d = local[i].x * dLocal.x + local[i].y * dLocal.y;
        if (d > bestDot) { bestDot = d; best = (int)i; }
    }
    return ToWorld(local[(size_t)best]);
}

// ===== SAT =====

bool SATCollide(const Polygon2D& a, const Polygon2D& b,
                  Vector2& outNormal, float& outDepth) {
    auto va = a.WorldVerts();
    auto vb = b.WorldVerts();

    float   minDepth = std::numeric_limits<float>::infinity();
    Vector2 minAxis  = {1, 0};

    auto project = [](const std::vector<Vector2>& v, Vector2 axis,
                       float& mn, float& mx) {
        mn = mx = Dot(v[0], axis);
        for (size_t i = 1; i < v.size(); ++i) {
            float d = Dot(v[i], axis);
            mn = std::min(mn, d);
            mx = std::max(mx, d);
        }
    };

    auto runAxes = [&](const std::vector<Vector2>& v) -> bool {
        int n = (int)v.size();
        for (int i = 0; i < n; ++i) {
            Vector2 e    = Sub(v[(size_t)((i + 1) % n)], v[(size_t)i]);
            Vector2 axis = {-e.y, e.x};
            float   len  = std::sqrt(axis.x * axis.x + axis.y * axis.y);
            if (len < 1e-9f) continue;
            axis = {axis.x / len, axis.y / len};

            float amn, amx, bmn, bmx;
            project(va, axis, amn, amx);
            project(vb, axis, bmn, bmx);

            // 이 축에서 b 를 떼어놓는 데 필요한 이동 거리 — 두 방향 각각.
            //   pushPos: b 를 +axis 로 밀 때  (a 의 최대가 b 의 최소를 넘은 만큼)
            //   pushNeg: b 를 -axis 로 밀 때
            // 음수면 이미 분리돼 있다는 뜻 = 분리축 발견.
            float pushPos = amx - bmn;
            float pushNeg = bmx - amn;
            if (pushPos < 0 || pushNeg < 0) return false;   // separation

            // 침투 깊이는 두 밀어내기 중 *짧은* 쪽 = MTD.
            // 축 위 두 구간의 교집합 길이 min(amx,bmx) - max(amn,bmn) 이 아니다 —
            // 한 구간이 다른 구간을 품는 깊은 겹침에서 교집합 길이는 짧은 구간의
            // 길이로 잘려 깊이를 과소평가한다 (같은 중심 45도 정사각형: 2.0 vs
            // 참값 1 + √2 ≈ 2.414).
            float overlap = std::min(pushPos, pushNeg);
            if (overlap < minDepth) {
                minDepth = overlap;
                // 법선은 *깊이로 고른 그 밀어내기 방향* 이어야 "normal 로 depth
                // 만큼 밀면 분리된다" 는 계약이 성립한다. 중심간 벡터로 부호를
                // 정하면 깊이와 방향이 서로 다른 축 정보를 쓰게 된다.
                // 중심 대칭 도형(투영 구간의 중점 = 중심의 투영)에서는 두 규칙이
                // 항상 같은 값을 내므로 a → b 규약은 그대로 유지된다.
                minAxis = (pushPos <= pushNeg) ? axis : Neg(axis);
            }
        }
        return true;
    };

    if (!runAxes(va)) return false;
    if (!runAxes(vb)) return false;

    outNormal = minAxis;
    outDepth  = minDepth;
    return true;
}

// ===== GJK + EPA =====

namespace {

bool DoSimplex(std::vector<Vector2>& s, Vector2& dir) {
    if (s.size() == 2) {
        Vector2 ax = s[1];                    // 가장 최근에 추가된 점
        Vector2 bx = s[0];
        Vector2 ab = Sub(bx, ax);
        Vector2 ao = Neg(ax);
        // dir = perpendicular to ab, towards origin.
        Vector2 d = Triple(ab, ao, ab);
        if (Dot(d, d) < 1e-9f) {
            // origin lies on ab — pick any perpendicular.
            d = {-ab.y, ab.x};
        }
        dir = d;
        return false;
    }
    // size == 3 — triangle.
    Vector2 ax = s[2];
    Vector2 bx = s[1];
    Vector2 cx = s[0];
    Vector2 ab = Sub(bx, ax);
    Vector2 ac = Sub(cx, ax);
    Vector2 ao = Neg(ax);

    Vector2 abPerp = Triple(ac, ab, ab);   // perpendicular to ab, away from c
    Vector2 acPerp = Triple(ab, ac, ac);   // perpendicular to ac, away from b

    if (Dot(abPerp, ao) > 0) {
        // origin outside AB region — remove c.
        s.erase(s.begin());
        dir = abPerp;
        return false;
    }
    if (Dot(acPerp, ao) > 0) {
        // origin outside AC region — remove b.
        s.erase(s.begin() + 1);
        dir = acPerp;
        return false;
    }
    return true;  // origin inside triangle → collision.
}

bool GJK(const Polygon2D& a, const Polygon2D& b,
          std::vector<Vector2>& simplex) {
    Vector2 dir = Sub(b.pos, a.pos);
    if (Dot(dir, dir) < 1e-9f) dir = {1, 0};

    Vector2 s = SupportCSO(a, b, dir);
    simplex  = {s};
    dir      = Neg(s);

    for (int iter = 0; iter < 64; ++iter) {
        Vector2 next = SupportCSO(a, b, dir);
        if (Dot(next, dir) <= 0) return false;  // 원점이 CSO 밖.
        simplex.push_back(next);

        if (DoSimplex(simplex, dir)) return true;
    }
    return false;
}

void EPA(const Polygon2D& a, const Polygon2D& b,
          std::vector<Vector2> simplex,
          Vector2& outNormal, float& outDepth) {
    // simplex 의 winding 을 ccw 로 정렬 (origin 이 안쪽이라는 전제).
    if (Cross(Sub(simplex[1], simplex[0]),
               Sub(simplex[2], simplex[0])) < 0) {
        std::swap(simplex[1], simplex[2]);
    }

    for (int iter = 0; iter < 64; ++iter) {
        int    bestIdx  = 0;
        float  bestDist = std::numeric_limits<float>::infinity();
        Vector2 bestN   = {0, 0};

        for (size_t i = 0; i < simplex.size(); ++i) {
            size_t  j = (i + 1) % simplex.size();
            Vector2 e = Sub(simplex[j], simplex[i]);
            // ccw 일 때 바깥 normal 은 (e.y, -e.x).
            Vector2 n   = {e.y, -e.x};
            float   len = std::sqrt(n.x * n.x + n.y * n.y);
            if (len < 1e-9f) continue;
            n = {n.x / len, n.y / len};
            float d = Dot(n, simplex[i]);
            if (d < bestDist) {
                bestDist = d;
                bestN    = n;
                bestIdx  = (int)j;   // i 와 j 사이에 새 점 삽입.
            }
        }

        Vector2 s = SupportCSO(a, b, bestN);
        float   d = Dot(s, bestN);
        if (d - bestDist < 1e-4f) {
            outNormal = bestN;
            outDepth  = bestDist;
            return;
        }
        simplex.insert(simplex.begin() + bestIdx, s);
    }
    outNormal = {0, 0};
    outDepth  = 0;
}

}  // namespace

bool GjkEpaCollide(const Polygon2D& a, const Polygon2D& b,
                     Vector2& outNormal, float& outDepth) {
    std::vector<Vector2> simplex;
    if (!GJK(a, b, simplex)) {
        outNormal = {0, 0};
        outDepth  = 0;
        return false;
    }
    if (simplex.size() < 3) {
        // 드물게 degenerate — collision 이지만 EPA 불가.
        outNormal = {0, 0};
        outDepth  = 0;
        return true;
    }
    EPA(a, b, simplex, outNormal, outDepth);
    return true;
}

// ===== NarrowPhaseScene =====

NarrowPhaseScene::NarrowPhaseScene() {
    depthHist_.Reset(kGraphCapacity);
    usHist_.Reset(kGraphCapacity);
    MakeRegularPolygon(a_, vertsA_, radiusA_);
    MakeRegularPolygon(b_, vertsB_, radiusB_);
}

void NarrowPhaseScene::MakeRegularPolygon(Polygon2D& p, int vertices,
                                              float radius) {
    p.local.clear();
    int n = std::max(3, vertices);
    for (int i = 0; i < n; ++i) {
        float t = (float)i / (float)n * 2.0f * kPi;
        p.local.push_back({std::cos(t) * radius, std::sin(t) * radius});
    }
}

void NarrowPhaseScene::OnEnter() {
    initialized_ = false;
    depthHist_.Clear();
    usHist_.Clear();
}

void NarrowPhaseScene::UpdatePolygonFromHandles(Polygon2D& p,
                                                    DragHandle& center,
                                                    DragHandle& rotation,
                                                    float rotRadius) {
    p.pos = center.Position();
    Vector2 d = {rotation.Position().x - p.pos.x,
                  rotation.Position().y - p.pos.y};
    float len = std::sqrt(d.x * d.x + d.y * d.y);
    if (len > 1e-3f) {
        p.angle = std::atan2(d.y, d.x);
        // rotation 핸들을 항상 rotRadius 위에 고정.
        rotation.SetPosition({p.pos.x + d.x / len * rotRadius,
                                p.pos.y + d.y / len * rotRadius});
    } else {
        rotation.SetPosition({p.pos.x + rotRadius, p.pos.y});
    }
}

void NarrowPhaseScene::HandleInput() {
    aCenter_.Update();
    aRot_.Update();
    bCenter_.Update();
    bRot_.Update();
}

void NarrowPhaseScene::Step(float dt) {
    (void)dt;
    if (!initialized_) return;

    UpdatePolygonFromHandles(a_, aCenter_, aRot_, rotRadius_);
    UpdatePolygonFromHandles(b_, bCenter_, bRot_, rotRadius_);

    // 둘 다 실행 — 결과 비교용. 선택된 알고리즘만 시간 측정에 사용.
    Vector2 satN, gjkN;
    float   satD = 0, gjkD = 0;
    bool    satHit = false, gjkHit = false;
    double  us = 0.0;

    if (algo_ == NarrowAlgo::SAT) {
        auto t0 = std::chrono::high_resolution_clock::now();
        satHit  = SATCollide(a_, b_, satN, satD);
        auto t1 = std::chrono::high_resolution_clock::now();
        us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        gjkHit = GjkEpaCollide(a_, b_, gjkN, gjkD);
    } else {
        auto t0 = std::chrono::high_resolution_clock::now();
        gjkHit  = GjkEpaCollide(a_, b_, gjkN, gjkD);
        auto t1 = std::chrono::high_resolution_clock::now();
        us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        satHit = SATCollide(a_, b_, satN, satD);
    }

    satCollision_ = satHit;
    gjkCollision_ = gjkHit;
    satDepth_     = satD;
    gjkDepth_     = gjkD;
    satNormal_    = satN;
    gjkNormal_    = gjkN;

    bool collide   = (algo_ == NarrowAlgo::SAT) ? satHit : gjkHit;
    Vector2 normal = (algo_ == NarrowAlgo::SAT) ? satN   : gjkN;
    float depth    = (algo_ == NarrowAlgo::SAT) ? satD   : gjkD;

    lastCollision_ = collide;
    lastNormal_    = normal;
    lastDepth_     = depth;
    lastUs_        = us;

    // 두 알고리즘 일치 여부 — collision 동의 + depth 차이 작음.
    bool agreeHit = (satHit == gjkHit);
    float depthDiff = std::fabs(satD - gjkD);
    bool agreeDepth = (!satHit && !gjkHit) || depthDiff < 1.5f;
    bothAgree_ = agreeHit && agreeDepth;

    depthHist_.Push(collide ? depth : 0.0f);
    usHist_.Push((float)us);
}

void NarrowPhaseScene::DrawWorld(int cx, int cy, int cw, int ch) {
    lastCanvasX_ = cx; lastCanvasY_ = cy;
    lastCanvasW_ = cw; lastCanvasH_ = ch;

    if (!initialized_) {
        a_.pos = {(float)(cx + cw / 3),     (float)(cy + ch / 2)};
        b_.pos = {(float)(cx + 2 * cw / 3), (float)(cy + ch / 2)};
        a_.angle = 0; b_.angle = 0;
        aCenter_.SetPosition(a_.pos);
        bCenter_.SetPosition(b_.pos);
        aRot_.SetPosition({a_.pos.x + rotRadius_, a_.pos.y});
        bRot_.SetPosition({b_.pos.x + rotRadius_, b_.pos.y});
        initialized_ = true;
    }

    DrawRectangle(cx, cy, cw, ch, Color{20, 22, 28, 255});
    DrawRectangleLines(cx, cy, cw, ch, Color{60, 62, 70, 255});

    auto drawPoly = [&](const Polygon2D& p, Color c) {
        auto v = p.WorldVerts();
        int  n = (int)v.size();
        for (int i = 0; i < n; ++i) {
            DrawLineEx(v[(size_t)i], v[(size_t)((i + 1) % n)], 2.0f, c);
        }
        DrawCircleV(p.pos, 3.0f, c);
    };

    Color colA = {120, 200, 255, 255};
    Color colB = {255, 200, 100, 255};
    if (lastCollision_) {
        colA = {255, 120, 120, 255};
        colB = {255, 120, 120, 255};
    }
    drawPoly(a_, colA);
    drawPoly(b_, colB);

    // contact normal + depth 시각화.
    if (lastCollision_) {
        Vector2 c = {(a_.pos.x + b_.pos.x) * 0.5f,
                      (a_.pos.y + b_.pos.y) * 0.5f};
        Vector2 tip = {c.x + lastNormal_.x * lastDepth_ * 4.0f,
                        c.y + lastNormal_.y * lastDepth_ * 4.0f};
        DrawLineEx(c, tip, 3.0f, Color{255, 255, 255, 220});
        DrawCircleV(tip, 4.0f, Color{255, 255, 255, 220});
        char buf[64];
        std::snprintf(buf, sizeof(buf), "depth %.2f", lastDepth_);
        DrawText(buf, (int)tip.x + 6, (int)tip.y - 6, 12,
                  Color{220, 220, 220, 255});
    }

    aCenter_.Draw(Color{120, 200, 255, 200}, "A");
    aRot_.Draw(Color{120, 200, 255, 200}, nullptr);
    bCenter_.Draw(Color{255, 200, 100, 200}, "B");
    bRot_.Draw(Color{255, 200, 100, 200}, nullptr);

    const char* algoName = (algo_ == NarrowAlgo::SAT) ? "SAT" : "GJK + EPA";
    DrawText(TextFormat("Step 5: NarrowPhase = %s", algoName),
              cx + 10, cy + 10, 14, Color{200, 200, 200, 255});

    if (!bothAgree_) {
        DrawText("Boundary / numerical convention mismatch (SAT vs GJK+EPA)",
                  cx + 10, cy + 30, 14, Color{255, 200, 120, 255});
    }

    // 그래프 — 우상단.
    Rectangle gr1 = {(float)(cx + cw - 320), (float)(cy + 50), 300.0f, 110.0f};
    Rectangle gr2 = {(float)(cx + cw - 320), (float)(cy + 170), 300.0f, 110.0f};
    GraphInput in1[1] = {{&depthHist_, Color{255, 200, 100, 255}, "depth"}};
    DrawLineGraph(gr1, in1, 1, "Penetration depth (px)", 0, 1, true);

    GraphInput in2[1] = {{&usHist_, Color{120, 200, 255, 255}, "algo us"}};
    DrawLineGraph(gr2, in2, 1, "Algorithm time (us)", 0, 1, true);
}

void NarrowPhaseScene::DrawPanel(int panelX, int panelY, int panelW, int panelH) {
    (void)panelW; (void)panelH;
    int  y = panelY + 8;
    char buf[160];

    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, "NarrowPhase");
    y += 24;

    const char* names[2] = {"SAT", "GJK + EPA"};
    for (int i = 0; i < 2; ++i) {
        Rectangle r = {(float)(panelX + 8), (float)y, 240, 22};
        bool selected = ((int)algo_ == i);
        if (selected) DrawRectangleRec(r, Color{60, 100, 160, 255});
        if (GuiButton(r, names[i])) {
            algo_ = (NarrowAlgo)i;
            depthHist_.Clear();
            usHist_.Clear();
        }
        y += 26;
    }
    y += 6;

    // vertices / radius 슬라이더 (a / b).
    auto vertSlider = [&](const char* label, int& v, int minV, int maxV,
                            Polygon2D& p, float radius) {
        std::snprintf(buf, sizeof(buf), "%s: %d", label, v);
        float fv = (float)v;
        GuiSliderBar(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                      nullptr, buf, &fv, (float)minV, (float)maxV);
        int nv = (int)fv;
        if (nv != v) {
            v = nv;
            MakeRegularPolygon(p, v, radius);
        }
        y += 22;
    };
    auto radSlider = [&](const char* label, float& r,
                           Polygon2D& p, int v) {
        std::snprintf(buf, sizeof(buf), "%s: %.0f", label, r);
        float prev = r;
        GuiSliderBar(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                      nullptr, buf, &r, 10.0f, 100.0f);
        if (r != prev) MakeRegularPolygon(p, v, r);
        y += 22;
    };

    vertSlider("A vertices", vertsA_, 3, 12, a_, radiusA_);
    radSlider("A radius",    radiusA_, a_, vertsA_);
    vertSlider("B vertices", vertsB_, 3, 12, b_, radiusB_);
    radSlider("B radius",    radiusB_, b_, vertsB_);
    y += 8;

    // 결과 라벨.
    std::snprintf(buf, sizeof(buf), "collision: %s",
                   lastCollision_ ? "YES" : "no");
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, buf); y += 18;
    if (lastCollision_) {
        std::snprintf(buf, sizeof(buf), "depth: %.2f px", lastDepth_);
        GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, buf); y += 18;
        std::snprintf(buf, sizeof(buf), "normal: (%.2f, %.2f)",
                       lastNormal_.x, lastNormal_.y);
        GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, buf); y += 18;
    } else {
        y += 36;
    }
    std::snprintf(buf, sizeof(buf), "algo us: %.2f", lastUs_);
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, buf); y += 24;

    // 두 알고리즘 비교.
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
              "Cross-check (both run every frame):");
    y += 18;
    std::snprintf(buf, sizeof(buf), "SAT     hit=%d depth=%.2f",
                   satCollision_ ? 1 : 0, satDepth_);
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, buf); y += 18;
    std::snprintf(buf, sizeof(buf), "GJK+EPA hit=%d depth=%.2f",
                   gjkCollision_ ? 1 : 0, gjkDepth_);
    GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18}, buf); y += 18;
    if (bothAgree_) {
        GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                  "agree (depth diff < 1.5 px)");
    } else {
        DrawRectangle(panelX + 6, y, 248, 18, Color{200, 60, 60, 220});
        GuiLabel(Rectangle{(float)(panelX + 8), (float)y, 240, 18},
                  "DISAGREE: boundary / numerical mismatch");
    }
}

}  // namespace physlab
