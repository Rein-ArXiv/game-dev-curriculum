// transport — 데이터그램 전송 추상 + 결정적 loopback 시뮬레이터.
//
// 소켓 통합 테스트는 비결정적이라 CI 에서 흔들린다. 그래서 ITransport 의 검증
// 백엔드로 인메모리 LoopbackNet 을 둔다 — 손실·재정렬·지연을 시드 PRNG 로 주입해
// 같은 입력이 항상 같은 결과를 낸다. 신뢰 채널·스냅샷 보간을 이 위에서 결정적으로
// 검증한다. 제품 경로의 실제 UDP 소켓은 udp_transport.hpp가 같은 계약으로 구현한다.

#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace mega {
namespace net {

using Bytes = std::vector<uint8_t>;

class ITransport {
public:
    virtual ~ITransport() = default;
    virtual void Send(const Bytes& data) = 0;     // 비신뢰 데이터그램 전송
    virtual bool Receive(Bytes& out) = 0;          // 도착분 1개 꺼냄(없으면 false)
};

struct NetConditions {
    float    loss         = 0.0f;   // 패킷 손실 확률 [0,1]
    int      latencyTicks = 0;      // 전달 지연(Pump 횟수)
    float    reorderProb  = 0.0f;   // 추가 지연으로 순서 뒤섞기 확률
    uint32_t seed         = 0x9e3779b9u;
};

// 양방향 loopback 네트워크. 두 끝점(A/B)이 서로에게 보낸다.
class LoopbackNet {
public:
    explicit LoopbackNet(NetConditions cond = {}) : cond_(cond), rng_(cond.seed ? cond.seed : 1) {}

    void SendAToB(const Bytes& d) { Enqueue(aToB_, d); }
    void SendBToA(const Bytes& d) { Enqueue(bToA_, d); }

    // 한 틱 진행: 도착 예정 패킷을 수신함으로 옮긴다.
    void Pump() {
        ++tick_;
        Deliver(aToB_, bInbox_);
        Deliver(bToA_, aInbox_);
    }

    bool RecvB(Bytes& out) { return Pop(bInbox_, out); }
    bool RecvA(Bytes& out) { return Pop(aInbox_, out); }

    void SetConditions(NetConditions c) { cond_ = c; rng_ = c.seed ? c.seed : 1; }
    int  Tick() const { return tick_; }

private:
    struct InFlight { Bytes data; int deliverTick; };

    float Rand01() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return (rng_ >> 8) * (1.0f / 16777216.0f); }

    void Enqueue(std::vector<InFlight>& pipe, const Bytes& d) {
        if (cond_.loss > 0.0f && Rand01() < cond_.loss) return;     // 손실
        int extra = 0;
        if (cond_.reorderProb > 0.0f && Rand01() < cond_.reorderProb)
            extra = 1 + (int)(Rand01() * 3.0f);                     // 추가 지연 → 순서 뒤섞임
        pipe.push_back({d, tick_ + cond_.latencyTicks + extra});
    }
    void Deliver(std::vector<InFlight>& pipe, std::vector<Bytes>& inbox) {
        // deliverTick 이 지난 것을 deliverTick 오름차순으로 전달.
        std::vector<InFlight> remain;
        // 먼저 도착분만 모아 정렬.
        std::vector<InFlight*> due;
        for (auto& f : pipe) if (f.deliverTick <= tick_) due.push_back(&f);
        for (size_t i = 0; i < due.size(); ++i)
            for (size_t j = i + 1; j < due.size(); ++j)
                if (due[j]->deliverTick < due[i]->deliverTick) std::swap(due[i], due[j]);
        for (auto* f : due) inbox.push_back(std::move(f->data));
        for (auto& f : pipe) if (f.deliverTick > tick_) remain.push_back(std::move(f));
        pipe.swap(remain);
    }
    bool Pop(std::vector<Bytes>& inbox, Bytes& out) {
        if (inbox.empty()) return false;
        out = std::move(inbox.front());
        inbox.erase(inbox.begin());
        return true;
    }

    NetConditions          cond_;
    uint32_t               rng_;
    int                    tick_ = 0;
    std::vector<InFlight>  aToB_, bToA_;
    std::vector<Bytes>     aInbox_, bInbox_;
};

// 한 끝점을 ITransport 로 노출(A 측 / B 측).
class LoopbackEndpoint : public ITransport {
public:
    LoopbackEndpoint(LoopbackNet& net, bool isA) : net_(net), isA_(isA) {}
    void Send(const Bytes& d) override { isA_ ? net_.SendAToB(d) : net_.SendBToA(d); }
    bool Receive(Bytes& out) override { return isA_ ? net_.RecvA(out) : net_.RecvB(out); }
private:
    LoopbackNet& net_;
    bool         isA_;
};

}  // namespace net
}  // namespace mega
