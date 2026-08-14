// analytics — 운영 지표 집계: 카운터·게이지·히스토그램(퍼센타일).
//
// 원시 이벤트를 다 저장하면 무한히 커진다. 대신 집계한다: 단조 증가 카운터(매치
// 수), 순간값 게이지(동접), 분포는 버킷 히스토그램으로 모아 퍼센타일(p50/p95/p99)을
// 추정한다. 운영 대시보드가 보는 것은 이 집계다 — 정확한 원시값이 아니라 버킷
// 해상도의 요약이다(live_service 모니터링 챕터).

#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace mega {
namespace live {

class Counter {
public:
    void Add(uint64_t n = 1) { v_ += n; }
    uint64_t Value() const { return v_; }
    void Reset() { v_ = 0; }
private:
    uint64_t v_ = 0;
};

class Gauge {
public:
    void Set(double v) { v_ = v; }
    void Add(double d) { v_ += d; }
    double Value() const { return v_; }
private:
    double v_ = 0;
};

// 버킷 히스토그램. bounds_ 는 각 버킷의 상한(오름차순), 마지막은 오버플로.
class Histogram {
public:
    // 기본: 0,1,2,...,99,100,200,...,1000,2000... 식 가변 버킷 대신 선형 버킷 생성.
    static Histogram Linear(double maxBound, int buckets) {
        Histogram h;
        for (int i = 1; i <= buckets; ++i) h.bounds_.push_back(maxBound * i / buckets);
        h.counts_.assign(h.bounds_.size() + 1, 0);   // +1 = 오버플로 버킷
        return h;
    }

    void Record(double v) {
        size_t idx = (size_t)(std::upper_bound(bounds_.begin(), bounds_.end(), v) - bounds_.begin());
        ++counts_[idx];
        ++total_;
        sum_ += v;
        if (total_ == 1) { min_ = max_ = v; }
        else { if (v < min_) min_ = v; if (v > max_) max_ = v; }
    }

    uint64_t Count() const { return total_; }
    double   Min()  const { return total_ ? min_ : 0; }
    double   Max()  const { return total_ ? max_ : 0; }
    double   Mean() const { return total_ ? sum_ / total_ : 0; }

    // p ∈ [0,1] 퍼센타일 추정(버킷 상한 해상도).
    double Percentile(double p) const {
        if (total_ == 0) return 0;
        uint64_t target = (uint64_t)(p * total_);
        if (target >= total_) target = total_ - 1;
        uint64_t cum = 0;
        for (size_t i = 0; i < counts_.size(); ++i) {
            cum += counts_[i];
            if (cum > target)
                return i < bounds_.size() ? bounds_[i] : max_;   // 그 버킷 상한
        }
        return max_;
    }

private:
    std::vector<double>   bounds_;
    std::vector<uint64_t> counts_;
    uint64_t              total_ = 0;
    double                min_ = 0, max_ = 0, sum_ = 0;
};

// 이름으로 지표를 관리하는 레지스트리.
class Registry {
public:
    Counter&   GetCounter(const std::string& name) { return counters_[name]; }
    Gauge&     GetGauge(const std::string& name) { return gauges_[name]; }
    Histogram& GetHistogram(const std::string& name, double maxBound = 1000, int buckets = 100) {
        auto it = hists_.find(name);
        if (it == hists_.end()) it = hists_.emplace(name, Histogram::Linear(maxBound, buckets)).first;
        return it->second;
    }
    size_t MetricCount() const { return counters_.size() + gauges_.size() + hists_.size(); }

private:
    std::unordered_map<std::string, Counter>   counters_;
    std::unordered_map<std::string, Gauge>     gauges_;
    std::unordered_map<std::string, Histogram> hists_;
};

}  // namespace live
}  // namespace mega
