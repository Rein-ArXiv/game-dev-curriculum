#pragma once

#include "alloc/iallocator.h"
#include "ui/toggles.h"

#include <cstddef>
#include <list>
#include <vector>

namespace ppl {

// ContainerBenchmark — 같은 N개 노드의 *순회 비용* 을 컨테이너 종류별로 비교.
//
// 시작 시 한 번 N개를 채우고, 매 프레임 *순회* 하며 dist 합산. 결과 sum은 멤버에
// 남겨 컴파일러가 루프를 dead code로 삭제하기 어렵게 한다.
//
//   - Vector:        std::vector<Node>. 연속 메모리, prefetch 친화. baseline.
//   - StdList:       std::list<Node>. 노드 기반 배치, 포인터 추적, 캐시 미스 증가 가능.
//   - IntrusiveList: 외부 allocator(Pool 등)에서 노드 받아 next 포인터로 연결.
//                    Pool 결합 시 연속에 가까운 배치라 캐시가 vector에 가까울 수 있다.
//
// 캐시 미스의 영향이 ms 차이로 드러난다. 같은 N에 같은 작업을 해도 컨테이너에 따라
// 결과 ms는 수배 차이가 날 수 있다.
class ContainerBenchmark {
public:
    struct Node {
        float dist;
        int   pad[5];   // Node를 32 byte로 맞춰 캐시 라인당 2개가 들어가게 한다.
        Node* next;     // intrusive 노드용. vector/list는 무시.
    };
    static_assert(sizeof(void*) != 8 || sizeof(Node) == 32,
                  "ContainerBenchmark::Node should stay 32 bytes on 64-bit builds.");

    void Init(Container type, IAllocator* intrusiveAlloc, std::size_t count);
    void Shutdown();

    // 매 프레임 호출. 컨테이너를 한 번 순회하며 dist 합산.
    void RunFrame();

    Container Type()         const { return type_; }
    std::size_t NodeCount()  const { return count_; }
    float LastFrameMs()      const { return lastFrameMs_; }
    float LastSum()          const { return lastSum_; }

private:
    Container   type_         = Container::Vector;
    std::size_t count_        = 0;
    float       lastFrameMs_  = 0.0f;
    float       lastSum_      = 0.0f;

    // Vector 경로
    std::vector<Node> vec_;

    // StdList 경로
    std::list<Node> list_;

    // IntrusiveList 경로 — 외부 allocator에서 받아 직접 link.
    Node*       intrusiveHead_  = nullptr;
    IAllocator* intrusiveAlloc_ = nullptr;
};

}  // namespace ppl
