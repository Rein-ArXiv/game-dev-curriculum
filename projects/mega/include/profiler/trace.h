// Profiler — 핫 패스에 박는 경량 스코프 타이머 + 락프리 링 트레이스 버퍼.
//
// 프레임 루프 안에서 "어느 시스템이 몇 ms 먹었나"를 알려면, 매 스코프 진입/이탈을
// 기록하되 그 기록 자체가 측정을 왜곡하지 않아야 한다. ScopeTimer 는 RAII 로
// 진입 시각을 잡고 소멸 시 [이름, 스레드, 시작, 지속]을 링 버퍼에 한 번의 원자
// 증가로 적는다 — 락이 없어 워커들이 경합 없이 동시에 기록한다.
//
// 링이 가득 차면 가장 오래된 이벤트를 덮어쓴다(최근 N개만 보존). 오프라인
// 분석은 Snapshot 으로 뜬다.
//
// Linux 에서는 같은 지점에 USDT(SystemTap) 마커도 노출해 perf/bpftrace 가 잡을
// 수 있게 한다. 비활성 시 컴파일러가 nop 만 남겨 비용이 사실상 0이다.

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__) && defined(MEGA_USE_USDT)
#include <sys/sdt.h>
#define MEGA_USDT2(provider, probe) DTRACE_PROBE(provider, probe)
#else
#define MEGA_USDT2(provider, probe) ((void)0)
#endif

namespace mega {
namespace profiler {

struct TraceEvent {
    const char* name    = nullptr;
    uint32_t    tid     = 0;
    double      startMs = 0;   // 프로파일러 epoch 기준
    double      durMs   = 0;
};

class Profiler {
public:
    static constexpr size_t kCap = 1u << 16;   // 2^16 이벤트 보존(pow2)

    static Profiler& Get() {
        static Profiler p;
        return p;
    }

    double NowMs() const {
        return std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now() - epoch_).count();
    }

    void Record(const TraceEvent& e) {
        uint64_t i = head_.fetch_add(1, std::memory_order_relaxed);
        ring_[i & (kCap - 1)] = e;             // 덮어쓰기(오래된 것부터)
    }

    uint64_t TotalRecorded() const { return head_.load(std::memory_order_relaxed); }

    void Reset() { head_.store(0, std::memory_order_relaxed); }

    // 보존된 이벤트를 오래된→최신 순으로 복사.
    std::vector<TraceEvent> Snapshot() const {
        uint64_t c = head_.load(std::memory_order_acquire);
        uint64_t n = c < kCap ? c : kCap;
        uint64_t start = c - n;
        std::vector<TraceEvent> out;
        out.reserve(n);
        for (uint64_t i = 0; i < n; ++i) out.push_back(ring_[(start + i) & (kCap - 1)]);
        return out;
    }

private:
    Profiler() : epoch_(std::chrono::steady_clock::now()) {}
    std::array<TraceEvent, kCap>          ring_{};
    std::atomic<uint64_t>                 head_{0};
    std::chrono::steady_clock::time_point epoch_;
};

// 현재 스레드 ID 를 32비트로 축약(표시·그룹핑용).
inline uint32_t ThreadTag() {
    return static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

// RAII 스코프 타이머: 소멸 시 한 이벤트를 기록.
class ScopeTimer {
public:
    explicit ScopeTimer(const char* name)
        : name_(name), startMs_(Profiler::Get().NowMs()) {
        MEGA_USDT2(mega, scope_enter);
    }
    ~ScopeTimer() {
        double end = Profiler::Get().NowMs();
        Profiler::Get().Record(TraceEvent{name_, ThreadTag(), startMs_, end - startMs_});
        MEGA_USDT2(mega, scope_exit);
    }
    ScopeTimer(const ScopeTimer&) = delete;
    ScopeTimer& operator=(const ScopeTimer&) = delete;
private:
    const char* name_;
    double      startMs_;
};

// 이름별 집계: 호출 수·총 시간·최대 시간.
struct AggEntry {
    std::string name;
    uint64_t    count   = 0;
    double      totalMs = 0;
    double      maxMs   = 0;
};

inline std::vector<AggEntry> AggregateByName(const std::vector<TraceEvent>& events) {
    std::vector<AggEntry> out;
    for (const TraceEvent& e : events) {
        const char* nm = e.name ? e.name : "(null)";
        AggEntry* slot = nullptr;
        for (AggEntry& a : out)
            if (a.name == nm) { slot = &a; break; }
        if (!slot) { out.push_back(AggEntry{nm, 0, 0, 0}); slot = &out.back(); }
        slot->count   += 1;
        slot->totalMs += e.durMs;
        if (e.durMs > slot->maxMs) slot->maxMs = e.durMs;
    }
    return out;
}

}  // namespace profiler
}  // namespace mega

// 고유 변수명으로 스코프 타이머를 박는 매크로.
#define MEGA_PROFILE_CONCAT2(a, b) a##b
#define MEGA_PROFILE_CONCAT(a, b)  MEGA_PROFILE_CONCAT2(a, b)
#define MEGA_PROFILE_SCOPE(name) \
    ::mega::profiler::ScopeTimer MEGA_PROFILE_CONCAT(megaScope_, __LINE__)(name)
