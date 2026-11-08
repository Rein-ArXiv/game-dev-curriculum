#include "measure/container_benchmark.h"

#include <chrono>
#include <random>

namespace ppl {

void ContainerBenchmark::Init(Container type, IAllocator* intrusiveAlloc,
                              std::size_t count) {
    // 이전 설정의 컨테이너 내용과 마지막 측정값을 모두 비운다. 이걸 빼면 새 설정의
    // 결과와 옛 표본이 같은 조회 창에 섞인다.
    Shutdown();

    type_           = type;
    requested_      = count;
    count_          = 0;
    status_         = BenchStatus::Skipped;
    intrusiveAlloc_ = intrusiveAlloc;

    if (count == 0) return;

    // 결정론적 시드. 같은 바이너리·같은 표준 라이브러리에서 같은 초기 상태를 만든다.
    // 표준 라이브러리가 다르면 uniform_real_distribution의 실수 매핑까지 같다는
    // 보장은 없으므로, 교차 환경 비교에는 생성된 fixture와 hash를 쓴다.
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
            // 원소 주소 연속성을 보장하지 않는다(항상 흩어진다는 뜻은 아니다).
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
            if (!intrusiveAlloc_) {
                status_ = BenchStatus::Skipped;
                return;
            }
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

    status_ = (count_ == requested_) ? BenchStatus::Valid
                                     : BenchStatus::InvalidCapacity;
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
    requested_      = 0;
    status_         = BenchStatus::Skipped;
    // 마지막 측정값도 지운다. count==0 상태에서 옛 sum/ms가 조회되면 안 된다.
    lastFrameMs_ = 0.0f;
    lastSum_     = 0.0f;
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
