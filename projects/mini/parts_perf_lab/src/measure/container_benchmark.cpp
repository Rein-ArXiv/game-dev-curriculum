#include "measure/container_benchmark.h"

#include <chrono>
#include <random>

namespace ppl {

void ContainerBenchmark::Init(Container type, IAllocator* intrusiveAlloc,
                              std::size_t count) {
    Shutdown();

    type_           = type;
    count_          = 0;
    intrusiveAlloc_ = intrusiveAlloc;

    // 결정론적 시드. 같은 측정을 재현 가능하게 한다.
    std::mt19937 rng(0xBADCAFEu);
    std::uniform_real_distribution<float> dist(0.0f, 100.0f);

    switch (type_) {
        case Container::Vector: {
            vec_.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                Node n{};
                n.dist = dist(rng);
                vec_.push_back(n);
            }
            count_ = vec_.size();
            break;
        }
        case Container::StdList: {
            // 일반적인 std::list 구현은 기본 allocator로 노드를 개별 힙 할당한다.
            // 원소 주소 연속성이 보장되지 않아 캐시 미스가 늘기 쉽다.
            for (std::size_t i = 0; i < count; ++i) {
                Node n{};
                n.dist = dist(rng);
                list_.push_back(n);
            }
            count_ = list_.size();
            break;
        }
        case Container::IntrusiveList: {
            // 외부 allocator(Pool 권장)에서 노드 N개 받아 next로 link.
            // Pool과 결합하면 연속 메모리에 가깝게 배치되어 캐시 친화적.
            if (!intrusiveAlloc_) break;
            intrusiveHead_ = nullptr;
            for (std::size_t i = 0; i < count; ++i) {
                void* p = intrusiveAlloc_->Allocate(sizeof(Node), alignof(Node));
                if (!p) break;
                Node* node = static_cast<Node*>(p);
                node->dist = dist(rng);
                node->next = intrusiveHead_;
                intrusiveHead_ = node;
                ++count_;
            }
            break;
        }
    }
}

void ContainerBenchmark::Shutdown() {
    vec_.clear();
    list_.clear();

    // Intrusive list 정리. Pool은 Reset으로 일괄 회수도 가능하지만
    // 인터페이스 일관성을 위해 노드별 Deallocate.
    if (intrusiveHead_ && intrusiveAlloc_) {
        Node* cur = intrusiveHead_;
        while (cur) {
            Node* next = cur->next;
            intrusiveAlloc_->Deallocate(cur, sizeof(Node));
            cur = next;
        }
    }
    intrusiveHead_  = nullptr;
    intrusiveAlloc_ = nullptr;
    count_          = 0;
}

void ContainerBenchmark::RunFrame() {
    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();

    float sum = 0.0f;
    switch (type_) {
        case Container::Vector: {
            for (const auto& n : vec_) sum += n.dist;
            break;
        }
        case Container::StdList: {
            for (const auto& n : list_) sum += n.dist;
            break;
        }
        case Container::IntrusiveList: {
            for (Node* p = intrusiveHead_; p; p = p->next) sum += p->dist;
            break;
        }
    }

    lastSum_ = sum;

    const auto t1 = Clock::now();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    lastFrameMs_ = static_cast<float>(ns) / 1.0e6f;
}

}  // namespace ppl
