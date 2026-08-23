// articulation — low-link DFS 로 "이 칸을 막으면 두 지점이 끊기는가"를 전수 판정.
//
// 메이즈 디펜스의 배치 규칙은 칸 하나짜리 질문이다: 여기에 타워를 세워도 스폰에서
// 코어까지 갈 수 있는가. 그 질문에 답하는 가장 짧은 코드는 후보 격자를 만들어
// flood fill 을 한 번 도는 것이고, 실제로 이 저장소가 그렇게 해 왔다.
//
// 그 방식이 답하지 못하는 것은 **다음 질문**이다. 격자 전체에서 막으면 안 되는
// 칸이 어디인가. flood fill 로 답하려면 칸마다 한 번씩, 1,681번 돌아야 한다.
// low-link DFS 는 한 번 돌아 전부 표시한다 — 그리고 그 표를 얻는 순간 HUD 는
// 커서 아래 한 칸의 가부가 아니라 **격자 전체를 칠할 수 있다.** 성능이 아니라
// 그것이 흡수의 이유다.
//
// ── 판정의 근거 ───────────────────────────────────────────────────
// 스폰을 뿌리로 DFS 를 돌면 각 정점에 발견 시각 tin 과, 자기 부분트리에서 역방향
// 간선으로 닿을 수 있는 가장 이른 시각 low 가 붙는다. 정점 v 가 스폰과 코어를
// 가르는 조건은 정확히 둘이다.
//
//   1. v 가 DFS 트리에서 스폰→코어 경로 위에 있다(뿌리도 코어도 아니다).
//   2. 그 경로에서 v 의 자식 c 에 대해 low[c] >= tin[v] 다.
//
// 2 는 "c 의 부분트리가 v 를 거치지 않고 v 위로 올라갈 수 없다"는 문장이고,
// 코어는 그 부분트리 안에 있으므로 v 를 지우면 코어가 뿌리에서 떨어진다. 반대로
// low[c] < tin[v] 면 부분트리 어딘가에서 v 의 진짜 조상으로 가는 역간선이 있고,
// 코어는 그 간선을 타고 v 를 피해 뿌리로 갈 수 있다.
//
// 그래서 트리 경로 위만 보면 된다 — 가르는 정점은 **모든** 스폰-코어 경로 위에
// 있어야 하고, DFS 트리 경로도 그중 하나이기 때문이다.
//
// ── 왜 격자 전용인가 ──────────────────────────────────────────────
// 일반 그래프 판이 아니라 (width x height, 4방향) 판이다. 인접 리스트를 만들지
// 않으므로 41x41 격자 하나에 배열 넷(tin·low·parent·seen)만 쓰고, 그 넷은 격자
// 버전이 바뀔 때만 다시 채워진다. 일반화는 여기서 값을 만들지 못한다 — 이 게임에
// 격자가 아닌 통행 그래프는 없다.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mega {
namespace algo {

// 4방향 격자 위에서 스폰-코어 분리 정점을 한 번에 전부 찾는다.
//
// 재귀가 아니라 명시 스택이다. 41x41 은 재귀로도 안전하지만 깊이가 입력에
// 좌우되는 재귀를 판정기 안에 두면 그 안전이 호출자의 격자 크기 선택에 딸린다.
class GridSeparators {
public:
    // passable[y*width + x] != 0 이면 통행 가능. 스폰·코어 칸은 호출자가 언제나
    // 통행 가능으로 넘긴다(게임 규칙이 그 둘을 막지 못하게 한다).
    //
    // 반환값은 "지금 스폰에서 코어까지 통해 있는가"다. 거짓이면 분리 표는 전부
    // 0 이고, 그 상태에서의 배치 판정은 어느 칸이든 거짓이어야 한다 — 이미
    // 끊긴 격자는 무엇을 막아도 끊긴 채다.
    // snippet-begin: artic-build
    bool Build(const uint8_t* passable, int width, int height,
               int spawnX, int spawnY, int coreX, int coreY) {
        width_ = width;
        height_ = height;
        const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
        tin_.assign(count, 0);
        low_.assign(count, 0);
        parent_.assign(count, kNoParent);
        separator_.assign(count, 0);
        connected_ = false;
        if (width <= 0 || height <= 0) return false;
        if (!InBounds(spawnX, spawnY) || !InBounds(coreX, coreY)) return false;

        const int32_t root = spawnY * width_ + spawnX;
        const int32_t goal = coreY * width_ + coreX;
        if (!passable[static_cast<size_t>(root)] || !passable[static_cast<size_t>(goal)])
            return false;

        // ── low-link DFS ────────────────────────────────────────────
        uint32_t timer = 1;
        stack_.clear();
        stack_.push_back({root, 0});
        tin_[static_cast<size_t>(root)] = timer;
        low_[static_cast<size_t>(root)] = timer;
        ++timer;
        static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        while (!stack_.empty()) {
            Frame& frame = stack_.back();
            if (frame.next < 4) {
                const int d = frame.next++;
                const int cx = frame.at % width_, cy = frame.at / width_;
                const int nx = cx + dx[d], ny = cy + dy[d];
                if (!InBounds(nx, ny)) continue;
                const size_t next = static_cast<size_t>(ny * width_ + nx);
                if (!passable[next]) continue;
                if (tin_[next] != 0) {
                    // 역방향 간선. 부모로 되돌아가는 간선은 세지 않는다 — 격자에는
                    // 같은 두 칸을 잇는 간선이 하나뿐이라 그 한 번만 건너뛰면 된다.
                    //
                    // 여기서 low_[next] 를 쓰면 안 된다. 무방향 DFS 에서 이미
                    // 방문된 이웃의 low 는 자기 부분트리 바깥의 정보를 담고 있어,
                    // 그것을 끌어오면 실제로는 가르는 정점이 안 가르는 것으로
                    // 보고된다. 역간선이 주는 정보는 **그 정점의 발견 시각**뿐이다.
                    if (static_cast<int32_t>(next) != parent_[static_cast<size_t>(frame.at)])
                        low_[static_cast<size_t>(frame.at)] =
                            Min(low_[static_cast<size_t>(frame.at)], tin_[next]);
                    continue;
                }
                parent_[next] = frame.at;
                tin_[next] = timer;
                low_[next] = timer;
                ++timer;
                stack_.push_back({static_cast<int32_t>(next), 0});
                continue;
            }
            const int32_t done = frame.at;
            stack_.pop_back();
            if (!stack_.empty()) {
                const int32_t up = stack_.back().at;
                low_[static_cast<size_t>(up)] =
                    Min(low_[static_cast<size_t>(up)], low_[static_cast<size_t>(done)]);
            }
        }

        if (tin_[static_cast<size_t>(goal)] == 0) return false;   // 코어 미도달
        connected_ = true;

        // ── 트리 경로를 코어에서 뿌리로 거슬러 표시 ──────────────────
        // 뿌리(스폰)는 표시하지 않는다. 게임 규칙이 스폰 칸을 막지 못하게 하므로
        // 뿌리의 분리 여부는 답이 쓰이지 않는 질문이다.
        for (int32_t child = goal; parent_[static_cast<size_t>(child)] != kNoParent;) {
            const int32_t up = parent_[static_cast<size_t>(child)];
            if (up != root &&
                low_[static_cast<size_t>(child)] >= tin_[static_cast<size_t>(up)])
                separator_[static_cast<size_t>(up)] = 1;
            child = up;
        }
        return true;
    }
    // snippet-end: artic-build

    bool Connected() const { return connected_; }
    bool InBounds(int x, int y) const {
        return x >= 0 && y >= 0 && x < width_ && y < height_;
    }
    // 이 칸을 막으면 스폰-코어가 끊기는가. 격자 밖이면 거짓(막을 칸이 없다).
    bool Separates(int x, int y) const {
        if (!InBounds(x, y)) return false;
        return separator_[static_cast<size_t>(y * width_ + x)] != 0;
    }
    // DFS 가 스폰에서 닿은 칸인가. 닿지 않은 칸은 어떤 스폰-코어 경로에도 없다.
    bool Reached(int x, int y) const {
        if (!InBounds(x, y)) return false;
        return tin_[static_cast<size_t>(y * width_ + x)] != 0;
    }
    size_t SeparatorCount() const {
        size_t total = 0;
        for (uint8_t flag : separator_) total += flag ? 1u : 0u;
        return total;
    }

private:
    struct Frame { int32_t at; int next; };
    static constexpr int32_t kNoParent = -1;
    static uint32_t Min(uint32_t a, uint32_t b) { return a < b ? a : b; }

    int                   width_ = 0, height_ = 0;
    bool                  connected_ = false;
    std::vector<uint32_t> tin_, low_;
    std::vector<int32_t>  parent_;
    std::vector<uint8_t>  separator_;
    std::vector<Frame>    stack_;
};

}  // namespace algo
}  // namespace mega
