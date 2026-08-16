// Influence map 의 한 tick — decay → stamp → spread.
//
// InfluenceScene::Step 은 아군/적 두 필드에 이 함수를 각각 적용한다. 두 필드는
// 서로를 읽지 않으므로 필드 하나짜리 순수 함수로 나눌 수 있고, 그래야 raylib
// 없이 결정성·벽 차단을 검증할 수 있다 (ai/part09).
//
// spread 는 자기 값과 "이웃 값 * spread 계수" 중 최댓값을 취하는 max-확산이다.
// 따라서 벽으로 완전히 갈린 영역에는 값이 전파되지 않고, 확산 반복 i 회는
// 거리 i 셀까지만 도달한다.

#pragma once

#include <vector>

namespace ailab {

struct GridPoint {
    int x = 0;
    int y = 0;
};

struct InfluenceParams {
    float decay      = 0.9f;   // 과거 망각 계수 (tick 당)
    float spread     = 0.6f;   // 이웃으로 넘길 때의 감쇠
    int   iterations = 2;      // tick 당 확산 반복
};

// stamps 는 .x/.y 를 가진 셀의 임의 범위 (grid.h 의 ailab::Cell 도 그대로
// 들어간다).
template <typename StampRange, typename Walkable>
void StepInfluenceField(int width, int height,
                        std::vector<float>& field,
                        const StampRange& stamps,
                        const InfluenceParams& params,
                        Walkable&& walkable) {
    if (width <= 0 || height <= 0) return;
    const int n = width * height;
    if ((int)field.size() != n) field.assign((size_t)n, 0.0f);

    // 1) decay (과거 망각).
    for (float& v : field) v *= params.decay;

    // 2) stamp (현재 유닛 강도 주입).
    for (const auto& c : stamps) {
        if (c.x < 0 || c.y < 0 || c.x >= width || c.y >= height) continue;
        field[(size_t)(c.y * width + c.x)] = 1.0f;
    }

    // 3) spread (벽 우회, 거리 감쇠 — 반복 확산).
    static const int dx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int dy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    std::vector<float> next;
    for (int i = 0; i < params.iterations; ++i) {
        next = field;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int idx = y * width + x;
                if (!walkable(x, y)) { next[(size_t)idx] = 0.0f; continue; }
                float best = field[(size_t)idx];
                for (int k = 0; k < 8; ++k) {
                    const int nx = x + dx[k], ny = y + dy[k];
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
                    if (!walkable(nx, ny)) continue;
                    const float in = field[(size_t)(ny * width + nx)] * params.spread;
                    if (in > best) best = in;
                }
                next[(size_t)idx] = best;
            }
        }
        field.swap(next);
    }
}

}  // namespace ailab
