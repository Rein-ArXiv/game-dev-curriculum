#include "scenes/spatial_scene.h"

#include "algo/noise.h"   // Hash2 재활용 (결정론적 분포)
#include "raygui.h"

#include <cstdio>

namespace mathlab {

void SpatialScene::OnEnter() {
    initialized_ = false;
}

void SpatialScene::Update(float dt) {
    rayStart_.Update();
    rayEnd_.Update();
    (void)dt;
}

void SpatialScene::Regenerate(int cx, int cy, int cw, int ch) {
    boxes_.clear();
    boxes_.reserve((size_t)numObjects_);
    for (int i = 0; i < numObjects_; ++i) {
        float u  = nz::HashTo01(i, 0, (uint32_t)seed_);
        float v  = nz::HashTo01(i, 1, (uint32_t)seed_);
        float wf = nz::HashTo01(i, 2, (uint32_t)seed_);
        float hf = nz::HashTo01(i, 3, (uint32_t)seed_);
        float w  = 8.0f + wf * 30.0f;
        float h  = 8.0f + hf * 30.0f;
        float xMin = (float)cx + 30 + u * (cw - 60 - w);
        float yMin = (float)cy + 30 + v * (ch - 60 - h);
        boxes_.push_back({{xMin, yMin}, {xMin + w, yMin + h}});
    }
    bvh_.Build(boxes_, leafThreshold_);

    // maxDepth_ 가 0 이면 빌드 직후 모든 노드 표시.
    int maxD = 0;
    for (auto& n : bvh_.Nodes()) maxD = std::max(maxD, n.depth);
    if (maxDepth_ <= 0 || maxDepth_ > maxD) maxDepth_ = maxD;
}

void SpatialScene::DrawWorld(int cx, int cy, int cw, int ch) {
    bool needRegen = !initialized_
                    || seed_ != lastSeed_
                    || numObjects_ != lastN_
                    || leafThreshold_ != lastLeaf_
                    || cx != lastCx_ || cy != lastCy_
                    || cw != lastCw_ || ch != lastCh_;
    if (needRegen) {
        Regenerate(cx, cy, cw, ch);
        if (!initialized_) {
            rayStart_.SetPosition({(float)(cx + 40), (float)(cy + ch * 0.7f)});
            rayEnd_.SetPosition({(float)(cx + cw - 40), (float)(cy + ch * 0.3f)});
            initialized_ = true;
        }
        lastSeed_ = seed_;
        lastN_    = numObjects_;
        lastLeaf_ = leafThreshold_;
        lastCx_ = cx; lastCy_ = cy; lastCw_ = cw; lastCh_ = ch;
    }

    // 광선 트래버설 — visited 노드 set 만들기.
    // snippet-begin: bvh-traversal-viz
    Vector2 ro = rayStart_.Position();
    Vector2 rd = {rayEnd_.Position().x - ro.x, rayEnd_.Position().y - ro.y};
    float L = std::sqrt(rd.x * rd.x + rd.y * rd.y);
    Vector2 rdN = (L > 1e-6f) ? Vector2{rd.x / L, rd.y / L} : Vector2{1, 0};
    auto trav = bvh_.Trace(boxes_, ro, rdN, L);
    std::vector<bool> visited((size_t)bvh_.NumNodes(), false);
    for (int i : trav.visitedNodes) visited[(size_t)i] = true;

    // BVH 노드 그리기 (depth 제한).
    for (size_t i = 0; i < bvh_.Nodes().size(); ++i) {
        const BVHNode2D& n = bvh_.Nodes()[i];
        if (n.depth > maxDepth_) continue;
        if (showOnlyVisited_ && !visited[i]) continue;

        Color c{120, 130, 150, 80};
        if (visited[i]) c = Color{120, 220, 255, 160};
        if (n.leftChild < 0) {
            // leaf — 더 강조
            c = visited[i] ? Color{120, 255, 180, 200}
                            : Color{180, 200, 220, 120};
        }
        DrawRectangleLinesEx(
            Rectangle{n.bounds.min.x, n.bounds.min.y,
                       n.bounds.Width(), n.bounds.Height()},
            1.0f, c);
    }

    // 객체 그리기 (hit 된 객체는 빨강).
    for (size_t i = 0; i < boxes_.size(); ++i) {
        const AABB2D& b = boxes_[i];
        Color c = ((int)i == trav.hitObjIdx)
                    ? Color{255, 100, 100, 220}
                    : Color{255, 200, 80, 140};
        DrawRectangleRec(
            Rectangle{b.min.x, b.min.y, b.Width(), b.Height()}, c);
    }
    // snippet-end: bvh-traversal-viz

    // 광선
    if (showRay_) {
        Vector2 hitPt = (trav.hit && trav.hitT < L)
                          ? Vector2{ro.x + rdN.x * trav.hitT,
                                     ro.y + rdN.y * trav.hitT}
                          : rayEnd_.Position();
        DrawLineEx(ro, hitPt, 2.0f, Color{255, 240, 120, 255});
        if (trav.hit && trav.hitT < L) {
            DrawCircleV(hitPt, 5.0f, Color{255, 80, 80, 255});
        }
    }

    rayStart_.Draw(Color{120, 220, 120, 255}, "ro");
    rayEnd_.Draw(Color{220, 120, 220, 255}, "rd");

    char buf[160];
    std::snprintf(buf, sizeof(buf),
                   "objects %d   nodes %d   visited %d   %s",
                   numObjects_,
                   bvh_.NumNodes(),
                   (int)trav.visitedNodes.size(),
                   trav.hit ? "HIT" : "miss");
    DrawText(buf, cx + 10, cy + 10, 14, Color{200, 200, 200, 255});
}

void SpatialScene::DrawPanel(int px, int py, int pw, int ph) {
    (void)pw; (void)ph;
    int y = py + 8;

    GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, "Spatial / BVH");
    y += 22;

    char buf[64];
    float n = (float)numObjects_;
    std::snprintf(buf, sizeof(buf), "objects = %d", numObjects_);
    GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
    GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
               nullptr, nullptr, &n, 4.0f, 500.0f);
    numObjects_ = (int)n;
    y += 42;

    float seedF = (float)seed_;
    std::snprintf(buf, sizeof(buf), "seed = %d", seed_);
    GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
    GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
               nullptr, nullptr, &seedF, 1.0f, 999.0f);
    seed_ = (int)seedF;
    y += 42;

    float leaf = (float)leafThreshold_;
    std::snprintf(buf, sizeof(buf), "leaf threshold = %d", leafThreshold_);
    GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
    GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
               nullptr, nullptr, &leaf, 1.0f, 16.0f);
    leafThreshold_ = (int)leaf;
    y += 42;

    int maxD = 0;
    for (auto& nn : bvh_.Nodes()) maxD = std::max(maxD, nn.depth);
    float md = (float)maxDepth_;
    std::snprintf(buf, sizeof(buf), "show depth ≤ %d / %d", maxDepth_, maxD);
    GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
    GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
               nullptr, nullptr, &md, 0.0f, (float)std::max(maxD, 1));
    maxDepth_ = (int)md;
    y += 42;

    GuiCheckBox(Rectangle{(float)(px + 8), (float)y, 16, 16},
                 " only visited nodes", &showOnlyVisited_);
    y += 22;
    GuiCheckBox(Rectangle{(float)(px + 8), (float)y, 16, 16},
                 " show ray", &showRay_);
    y += 22;
}

}  // namespace mathlab
