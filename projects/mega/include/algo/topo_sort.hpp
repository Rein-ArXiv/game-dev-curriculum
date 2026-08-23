// topo_sort — 선행 관계를 순서로 펴는 Kahn, 그리고 펴지지 않을 때의 진단.
//
// 저작 파일이 "A 를 해금해야 B 를 살 수 있다"를 적을 수 있게 되면 두 가지가 함께
// 필요해진다.
//
//   순서   상점이 무엇을 먼저 보여 줄 것인가. 위상 순서면 선행이 언제나 앞에 온다.
//   거부   A→B→A 를 적은 파일은 통째로 거부해야 한다. 순환이 있는 선행 관계는
//          어느 항목도 살 수 없다는 뜻이고, 그 상태를 절반만 받아들이면 상점에
//          영원히 회색인 항목이 남는다.
//
// Kahn 은 그 둘을 한 번에 준다. 진입 차수 0 인 정점을 꺼내며 순서를 만들고, 다
// 꺼냈는데 순서의 길이가 정점 수보다 적으면 남은 것이 곧 순환에 걸린 정점들이다.
//
// ── 동점을 무엇으로 가르는가 ──────────────────────────────────────
// 진입 차수가 동시에 0 이 되는 정점이 여럿이면 Kahn 은 아무나 꺼내도 옳다. 옳지만
// **재현되지 않는다.** 삽입 순서로 꺼내면 파일의 줄 순서가 상점 순서가 되고, 그
// 파일을 한 줄만 옮겨 적은 사람은 자기가 순서를 바꿨다는 것을 모른다. 스크린샷도
// 골든 검사도 그 순간 갈린다. 그래서 꺼내는 자리를 **id 오름차순 최소 힙**으로
// 고정한다 — 같은 간선 집합이면 줄 순서와 무관하게 같은 한 줄이 나온다.
//
// ── 순환을 어떻게 지목하는가 ──────────────────────────────────────
// "순환이 있다"는 진단은 저작자에게 아무것도 알려 주지 않는다. 어느 항목들이
// 서로를 물고 있는지를 적어야 고칠 수 있다. Tarjan 의 강결합 요소가 그 목록을
// 준다 — 크기 2 이상인 요소가 곧 순환 하나이고, 그 요소의 id 를 정렬해 내보낸다.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mega {
namespace algo {

struct Edge {
    uint32_t from = 0;   // 선행
    uint32_t to   = 0;   // 후행
};

struct TopoResult {
    bool                  ok = false;
    std::vector<uint32_t> order;   // ok 면 정점 전부, 위상 순서
    std::vector<uint32_t> cycle;   // ok 가 아니면 순환 하나의 정점 id(오름차순)
};

// snippet-begin: topo-kahn
// 정점은 [0, count) 의 정수다. 같은 간선이 두 번 들어오면 진입 차수도 두 번
// 오르므로 호출자가 중복을 걸러야 한다 — 이 함수는 중복을 조용히 흡수하지 않는다.
inline TopoResult TopologicalOrder(size_t count, const std::vector<Edge>& edges) {
    TopoResult result;
    std::vector<uint32_t> indegree(count, 0);
    std::vector<std::vector<uint32_t>> next(count);
    for (const Edge& edge : edges) {
        if (edge.from >= count || edge.to >= count) return result;   // 범위 밖 = 거부
        next[edge.from].push_back(edge.to);
        ++indegree[edge.to];
    }

    // 최소 힙. std::vector + push_heap/pop_heap 에 greater 비교를 주면 최솟값이
    // 앞에 온다. 별도 자료구조를 들이지 않는 이유는 정점 수가 수십이기 때문이다.
    auto greater = [](uint32_t a, uint32_t b) { return a > b; };
    std::vector<uint32_t> ready;
    for (size_t i = 0; i < count; ++i)
        if (indegree[i] == 0) ready.push_back(static_cast<uint32_t>(i));
    std::make_heap(ready.begin(), ready.end(), greater);

    result.order.reserve(count);
    while (!ready.empty()) {
        std::pop_heap(ready.begin(), ready.end(), greater);
        const uint32_t at = ready.back();
        ready.pop_back();
        result.order.push_back(at);
        for (uint32_t to : next[at]) {
            // 감소와 검사가 한 식에 있어야 한다. `--indegree[to]` 를 하고 나서
            // `indegree[to] == 0` 을 따로 묻는 것과 `indegree[to] == 0` 만 묻는
            // 것은 다르다 — 후자는 차수가 2 이상인 정점을 영원히 꺼내지 못해
            // 순환이 없는 그래프를 순환이라고 보고한다.
            if (--indegree[to] == 0) {
                ready.push_back(to);
                std::push_heap(ready.begin(), ready.end(), greater);
            }
        }
    }
    if (result.order.size() == count) {
        result.ok = true;
        return result;
    }
    result.order.clear();
    return result;
}
// snippet-end: topo-kahn

// snippet-begin: topo-scc
// Tarjan. 반환은 크기 2 이상인 강결합 요소들(각 요소는 id 오름차순, 요소 사이는
// 첫 id 오름차순). 자기 자신을 가리키는 간선은 크기 1 짜리 순환이므로 따로 센다.
inline std::vector<std::vector<uint32_t>> CyclicComponents(
    size_t count, const std::vector<Edge>& edges) {
    std::vector<std::vector<uint32_t>> next(count);
    std::vector<uint8_t> selfLoop(count, 0);
    for (const Edge& edge : edges) {
        if (edge.from >= count || edge.to >= count) continue;
        if (edge.from == edge.to) selfLoop[edge.from] = 1;
        next[edge.from].push_back(edge.to);
    }

    std::vector<uint32_t> index(count, 0), low(count, 0);
    std::vector<uint8_t> onStack(count, 0);
    std::vector<uint32_t> component;
    std::vector<std::vector<uint32_t>> found;
    uint32_t counter = 1;

    struct Frame { uint32_t at; size_t edge; };
    std::vector<Frame> call;
    for (size_t start = 0; start < count; ++start) {
        if (index[start] != 0) continue;
        call.push_back({static_cast<uint32_t>(start), 0});
        index[start] = low[start] = counter++;
        component.push_back(static_cast<uint32_t>(start));
        onStack[start] = 1;
        while (!call.empty()) {
            Frame& frame = call.back();
            if (frame.edge < next[frame.at].size()) {
                const uint32_t to = next[frame.at][frame.edge++];
                if (index[to] == 0) {
                    index[to] = low[to] = counter++;
                    component.push_back(to);
                    onStack[to] = 1;
                    call.push_back({to, 0});
                } else if (onStack[to]) {
                    low[frame.at] = std::min(low[frame.at], index[to]);
                }
                continue;
            }
            const uint32_t done = frame.at;
            call.pop_back();
            if (!call.empty()) {
                const uint32_t up = call.back().at;
                low[up] = std::min(low[up], low[done]);
            }
            if (low[done] != index[done]) continue;
            std::vector<uint32_t> members;
            for (;;) {
                const uint32_t member = component.back();
                component.pop_back();
                onStack[member] = 0;
                members.push_back(member);
                if (member == done) break;
            }
            if (members.size() > 1 || selfLoop[done]) {
                std::sort(members.begin(), members.end());
                found.push_back(std::move(members));
            }
        }
    }
    std::sort(found.begin(), found.end(),
              [](const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
                  return a.front() < b.front();
              });
    return found;
}
// snippet-end: topo-scc

// 순서와 진단을 함께. 순환이면 ok=false 이고 cycle 에 **가장 작은 id 로 시작하는**
// 순환 요소가 들어간다.
inline TopoResult TopologicalOrderWithDiagnosis(size_t count, const std::vector<Edge>& edges) {
    TopoResult result = TopologicalOrder(count, edges);
    if (result.ok) return result;
    const std::vector<std::vector<uint32_t>> cycles = CyclicComponents(count, edges);
    if (!cycles.empty()) result.cycle = cycles.front();
    return result;
}

}  // namespace algo
}  // namespace mega
