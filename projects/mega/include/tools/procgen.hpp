// procgen — 시드 기반 절차 생성. 같은 시드는 같은 결과(재현성).
//
// 재귀 백트래커(randomized DFS)로 미로를 판다. 셀을 홀수 좌표에 두고 셀 사이 벽을
// 뚫어, 결과가 항상 *하나의 연결된 트리* 가 된다 — 즉 모든 칸이 도달 가능하고
// 사이클이 없다. 결정적 PRNG 만 써서 시드로 버그 재현·밸런스 테스트·일일 챌린지를
// 만든다(tools 절차 생성 챕터). 출력은 Level(타일 그리드)이다.

#pragma once

#include "tools/level_editor.hpp"

#include <cstdint>
#include <vector>

namespace mega {
namespace tools {

class MazeGen {
public:
    // cw×ch 셀 미로 → (2*cw+1)×(2*ch+1) 타일 Level. 스폰 좌상, 목표 우하.
    static Level Generate(int cw, int ch, uint32_t seed) {
        if (cw < 1) cw = 1; if (ch < 1) ch = 1;
        int W = 2 * cw + 1, H = 2 * ch + 1;
        Level lvl;
        lvl.Init(W, H, '#');                        // 전부 벽에서 시작

        uint32_t rng = seed ? seed : 1;
        auto nx = [&] { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; };

        std::vector<uint8_t> visited((size_t)cw * ch, 0);
        std::vector<int> stack;
        stack.push_back(0);                         // 셀 (0,0)
        visited[0] = 1;
        lvl.Set(1, 1, '.');                         // 셀 (0,0) 의 타일

        static const int dcx[4] = {1, -1, 0, 0}, dcy[4] = {0, 0, 1, -1};
        while (!stack.empty()) {
            int cur = stack.back();
            int ccx = cur % cw, ccy = cur / cw;
            // 미방문 이웃 셀 수집.
            int order[4] = {0, 1, 2, 3};
            for (int i = 3; i > 0; --i) { int j = (int)(nx() % (uint32_t)(i + 1)); int t = order[i]; order[i] = order[j]; order[j] = t; }
            bool moved = false;
            for (int k = 0; k < 4; ++k) {
                int d = order[k];
                int ncx = ccx + dcx[d], ncy = ccy + dcy[d];
                if (ncx < 0 || ncx >= cw || ncy < 0 || ncy >= ch) continue;
                int ni = ncy * cw + ncx;
                if (visited[ni]) continue;
                // 두 셀 사이 벽을 뚫는다.
                lvl.Set(2 * ncx + 1, 2 * ncy + 1, '.');                 // 이웃 셀 타일
                lvl.Set(ccx * 2 + 1 + dcx[d], ccy * 2 + 1 + dcy[d], '.'); // 사이 벽
                visited[ni] = 1;
                stack.push_back(ni);
                moved = true;
                break;
            }
            if (!moved) stack.pop_back();           // 막다른 길 → 백트랙
        }

        lvl.Set(1, 1, 'S');                         // 스폰
        lvl.Set(W - 2, H - 2, 'G');                 // 목표(우하 셀)
        return lvl;
    }
};

}  // namespace tools
}  // namespace mega
