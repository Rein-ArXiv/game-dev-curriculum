// counters.hpp — 알고리즘이 실제로 한 일을 세는 계측기.
//
// 벽시계만 재면 두 가지를 잃는다. 첫째, 시간은 기계마다 달라 재현되지 않는다.
// 둘째, 최적화기가 결과를 쓰지 않는 계산을 통째로 지워도 시간은 그럴듯하게 줄어든다.
// 그래서 이 lab 의 측정은 언제나 두 층이다 —
//   (a) 기계 독립적인 작업량(비교 횟수 · 이동 바이트 · 방문 노드 · probe 길이 …)
//   (b) 그 위에 얹은 시간
// 그리고 (a) 를 세는 카운터가 결과 checksum 을 함께 물고 있어야 (b) 가 정직해진다.
#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace measure {

// snippet-begin: counters-struct
struct Counters {
    uint64_t comparisons = 0;   // 비교자 호출 횟수
    uint64_t moved_bytes = 0;   // 원소 이동으로 옮긴 바이트
    uint64_t visited     = 0;   // 방문한 정점·노드·상태
    uint64_t probes      = 0;   // 해시 탐사 걸음
    uint64_t augments    = 0;   // 증가 경로·phase 횟수
    uint64_t peak_bytes  = 0;   // 임시 메모리 최대치
    uint64_t checksum    = 0;   // 결과를 소비한 흔적

    // 결과 checksum. 정렬 결과나 경로 비용을 여기에 섞어 두면, 그 값을 읽는 코드가
    // 벤치 바깥에 있으므로 컴파일러가 계산 자체를 죽은 코드로 지울 수 없다.
    void Consume(uint64_t value) {
        checksum ^= value + 0x9e3779b97f4a7c15ull + (checksum << 6) + (checksum >> 2);
    }
    void Peak(uint64_t bytes) { peak_bytes = (bytes > peak_bytes) ? bytes : peak_bytes; }
    void Reset() { *this = Counters{}; }
};
// snippet-end: counters-struct

// 반복 측정의 분포. 평균 하나로는 "가끔 튄다" 를 볼 수 없다 — 상각 O(1) 자료구조의
// 재구축 spike 는 정확히 P99 에서만 보인다.
struct Samples {
    std::vector<double> v;

    void Add(double x) { v.push_back(x); }
    size_t Count() const { return v.size(); }

    double Mean() const {
        if (v.empty()) return 0.0;
        double s = 0.0;
        for (double x : v) s += x;
        return s / double(v.size());
    }
    // 최근접 순위법. 표본이 적을 때 보간하면 없는 값을 만들어 낸다.
    double Percentile(double p) const {
        if (v.empty()) return 0.0;
        std::vector<double> sorted = v;
        std::sort(sorted.begin(), sorted.end());
        size_t rank = size_t(std::ceil(p / 100.0 * double(sorted.size())));
        if (rank == 0) rank = 1;
        if (rank > sorted.size()) rank = sorted.size();
        return sorted[rank - 1];
    }
    double Max() const {
        double m = 0.0;
        for (double x : v) m = (x > m) ? x : m;
        return m;
    }
};

// 단조 시계. system_clock 은 시간 동기화로 뒤로 갈 수 있어 측정에 쓰지 않는다.
class Timer {
public:
    Timer() : t0_(std::chrono::steady_clock::now()) {}
    void Restart() { t0_ = std::chrono::steady_clock::now(); }
    double Ms() const {
        return std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now() - t0_).count();
    }
    double Us() const { return Ms() * 1000.0; }

private:
    std::chrono::steady_clock::time_point t0_;
};

}  // namespace measure
