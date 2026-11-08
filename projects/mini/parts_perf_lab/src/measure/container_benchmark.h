#pragma once

#include "alloc/iallocator.h"
#include "measure/bench_status.h"
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
//   - StdList:       std::list<Node>. 노드 기반 배치, 포인터 추적.
//   - IntrusiveList: 외부 allocator(Pool 등)에서 노드 받아 next 포인터로 연결.
//
// 이 비교는 저장 형태뿐 아니라 allocation 방식·노드 크기·포인터 의존성·순회 순서가
// 함께 바뀐다. 따라서 시간 차이는 "이 세 구현의 순회 비용 차이"로 기록하고,
// 시간만으로 cache miss가 원인이라고 확정하지 않는다.
//
// 소유권: Intrusive 경로는 이 클래스가 노드를 할당하고 직접 반환하므로 노드 수명에
// 대한 소유 책임이 있다. 그래서 복사/이동을 금지하고 소멸자에서 Shutdown한다.
// 주입한 allocator는 borrowed이며 이 객체보다 오래 살아야 한다. 정리 중 호출되는
// Deallocate는 예외를 던지지 않아야 한다(소멸자에서 호출되므로).
class ContainerBenchmark {
public:
    ContainerBenchmark() = default;
    ~ContainerBenchmark() { Shutdown(); }

    ContainerBenchmark(const ContainerBenchmark&) = delete;
    ContainerBenchmark& operator=(const ContainerBenchmark&) = delete;
    ContainerBenchmark(ContainerBenchmark&&) = delete;
    ContainerBenchmark& operator=(ContainerBenchmark&&) = delete;

    struct Node {
        float dist;
        int   pad[5];   // Node를 32 byte로 맞춰 캐시 라인당 2개가 들어가게 한다.
        Node* next;     // intrusive 노드용. vector/list는 무시.
    };
    static_assert(sizeof(void*) != 8 || sizeof(Node) == 32,
                  "ContainerBenchmark::Node should stay 32 bytes on 64-bit builds.");

    // Shutdown을 먼저 호출하므로 이전 설정/측정이 남지 않는다.
    void Init(Container type, IAllocator* intrusiveAlloc, std::size_t count);
    void Shutdown();

    // 매 프레임 호출. 컨테이너를 한 번 순회하며 dist 합산.
    void RunFrame();

    Container Type()             const { return type_; }
    std::size_t NodeCount()      const { return count_; }        // 완성된 노드 수
    std::size_t RequestedCount() const { return requested_; }
    BenchStatus Status()         const { return status_; }
    float LastFrameMs()          const { return lastFrameMs_; }
    float LastSum()              const { return lastSum_; }

private:
    Container   type_         = Container::Vector;
    std::size_t requested_    = 0;
    std::size_t count_        = 0;
    BenchStatus status_       = BenchStatus::Skipped;
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
