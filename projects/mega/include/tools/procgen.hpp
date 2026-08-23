// procgen — 시드 기반 절차 생성. 같은 시드는 같은 결과(재현성).
//
// 골격 생성기가 둘이고, **둘 다 신장 트리를 낸다**. 신장 트리라는 것은 모든 칸이
// 도달 가능하고 사이클이 하나도 없다는 뜻이라, 연결성이 구조적으로 보장된다.
// 다른 것은 그 트리의 **형질**이다.
//
//   Generate          재귀 백트래커(randomized DFS). 한 방향으로 끝까지 파고
//                     들어갔다 되돌아오므로 긴 단일 회랑 하나가 생기고 막다른
//                     길이 길다. 분기점이 적다.
//   GenerateKruskal   셀 사이 간선에 시드 기반 가중치를 주고 오름차순으로 훑으며
//                     서로소 집합으로 사이클을 막는다. 채택 순서가 공간적으로
//                     흩어져 있어 분기가 많고 막다른 길이 짧다.
//
// 형질 차이가 왜 게임에 중요한가. 이 골격 위에 밀도장을 얹어 만든 지형에서
// 메이즈 디펜스가 돌고, 거기서 "어디를 막을까"의 선택지 수는 곧 골격의 분기
// 수다. 시드를 아무리 바꿔도 DFS 미로는 언제나 DFS 미로라, 시드 스위치만으로는
// 그 손잡이가 없었다.
//
// 결정적 PRNG 만 쓴다. 시드로 버그 재현·밸런스 테스트·일일 챌린지를 만든다.
// 출력은 Level(타일 그리드)이다.

#pragma once

#include "algo/dsu.hpp"
#include "tools/level_editor.hpp"

#include <algorithm>
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

    // snippet-begin: maze-kruskal
    // 두 번째 팔. 같은 크기·같은 스폰·같은 목표를 내고 결과도 신장 트리다.
    //
    // 정확성을 지키는 것은 `dsu.Union(a, b)` 가 돌려주는 불리언 하나다. 그것을
    // 무시하고 무조건 벽을 뚫으면 채택 간선이 셀 수 - 1 을 넘고 사이클이 생긴다 —
    // 그 순간 이것은 미로가 아니라 그냥 뚫린 방이다. 경로 압축과 크기 합치기는
    // 그 판정에 기여하지 않는다(빠르게 만들 뿐이다).
    static Level GenerateKruskal(int cw, int ch, uint32_t seed) {
        if (cw < 1) cw = 1; if (ch < 1) ch = 1;
        int W = 2 * cw + 1, H = 2 * ch + 1;
        Level lvl;
        lvl.Init(W, H, '#');
        for (int cy = 0; cy < ch; ++cy)
            for (int cx = 0; cx < cw; ++cx)
                lvl.Set(2 * cx + 1, 2 * cy + 1, '.');   // 셀 타일은 전부 열려 있다

        uint32_t rng = seed ? seed : 1;
        auto nx = [&] { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; };

        struct WeightedEdge {
            uint32_t weight, order, a, b;
            int wallX, wallY;
        };
        std::vector<WeightedEdge> edges;
        edges.reserve(static_cast<size_t>(cw - 1) * ch + static_cast<size_t>(cw) * (ch - 1));
        uint32_t order = 0;
        for (int cy = 0; cy < ch; ++cy) {
            for (int cx = 0; cx < cw; ++cx) {
                const uint32_t here = static_cast<uint32_t>(cy * cw + cx);
                if (cx + 1 < cw)
                    edges.push_back({nx(), order++, here, here + 1, 2 * cx + 2, 2 * cy + 1});
                if (cy + 1 < ch)
                    edges.push_back({nx(), order++, here, here + static_cast<uint32_t>(cw),
                                     2 * cx + 1, 2 * cy + 2});
            }
        }
        // 동점은 생성 순서로 가른다. 가중치가 32비트라 충돌은 드물지만, 드문 것을
        // 정하지 않고 두면 정렬 구현이 순서를 정하게 되고 그러면 같은 시드가
        // 표준 라이브러리에 따라 다른 미로를 낸다.
        std::sort(edges.begin(), edges.end(),
                  [](const WeightedEdge& l, const WeightedEdge& r) {
                      if (l.weight != r.weight) return l.weight < r.weight;
                      return l.order < r.order;
                  });

        algo::DisjointSet dsu(static_cast<size_t>(cw) * static_cast<size_t>(ch));
        for (const WeightedEdge& edge : edges) {
            if (!dsu.Union(edge.a, edge.b)) continue;   // 사이클이 생긴다 — 벽을 남긴다
            lvl.Set(edge.wallX, edge.wallY, '.');
        }

        lvl.Set(1, 1, 'S');
        lvl.Set(W - 2, H - 2, 'G');
        return lvl;
    }
    // snippet-end: maze-kruskal

    // 팔 하나를 고르는 유일한 자리. 호출부가 분기를 손으로 적으면 팔이 늘 때마다
    // 그 분기가 흩어진다.
    static Level GenerateWith(bool kruskal, int cw, int ch, uint32_t seed) {
        return kruskal ? GenerateKruskal(cw, ch, seed) : Generate(cw, ch, seed);
    }
};

}  // namespace tools
}  // namespace mega
