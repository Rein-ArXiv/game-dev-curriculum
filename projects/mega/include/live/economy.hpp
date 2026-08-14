// economy — 통화 source/sink 와 인플레이션 지표. 경제 건강을 균형으로 본다.
//
// 통화는 들어오고(매치 보상=source) 나간다(강화 구매=sink). 둘의 합이 순환량
// (supply)을 정하고, sink 가 source 를 충분히 흡수하지 못하면 통화가 쌓여 가치가
// 떨어진다(인플레이션). 누적 source/sink 를 추적해 인플레율을 지표로 노출한다
// (live_service 경제 챕터). 가격은 티어가 오를수록 가파르게 올려 sink 를 키운다.

#pragma once

#include <cstdint>
#include <unordered_map>

namespace mega {
namespace live {

class Economy {
public:
    // source: 통화 발행(매치 보상 등). 계정 잔고와 순환량 증가.
    void Grant(uint32_t account, int64_t amount) {
        if (amount <= 0) return;
        balances_[account] += amount;
        supply_ += amount;
        sourced_ += amount;
    }

    // sink: 통화 소각(구매 등). 잔고 부족이면 false.
    bool Spend(uint32_t account, int64_t amount) {
        if (amount <= 0) return false;
        auto it = balances_.find(account);
        if (it == balances_.end() || it->second < amount) return false;
        it->second -= amount;
        supply_ -= amount;
        sunk_ += amount;
        return true;
    }

    int64_t Balance(uint32_t account) const {
        auto it = balances_.find(account);
        return it == balances_.end() ? 0 : it->second;
    }
    int64_t Supply()  const { return supply_; }
    int64_t Sourced() const { return sourced_; }
    int64_t Sunk()    const { return sunk_; }

    // 인플레율: 발행분 중 회수되지 않고 쌓인 비율. 0=완전 흡수, 1=전혀 회수 안 됨.
    double InflationRate() const {
        if (sourced_ == 0) return 0.0;
        return (double)(sourced_ - sunk_) / (double)sourced_;
    }

    // 가격: 티어가 오를수록 가파르게(sink 강화). 기본 50 * 2^tier.
    static int64_t UpgradeCost(int tier) {
        int64_t c = 50;
        for (int i = 0; i < tier; ++i) c *= 2;
        return c;
    }

private:
    std::unordered_map<uint32_t, int64_t> balances_;
    int64_t supply_  = 0;
    int64_t sourced_ = 0;
    int64_t sunk_    = 0;
};

}  // namespace live
}  // namespace mega
