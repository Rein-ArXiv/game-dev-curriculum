// property.h — 시드 무작위 property 층의 최소 하네스.
//
// 용어는 다른 lab 의 property 층과 같게 쓴다.
//   property   생성된 *모든* 입력이 지켜야 하는 관계. 고정 기대값 표가 아니다.
//   oracle     결과가 맞는지 판단하는 근거. 여기서는 (a) 불변식, (b) 느린 독립
//              구현 대조, (c) 입력 변환 전후 관계(metamorphic) 셋을 쓴다.
//   생성기     사전조건을 만족하는 입력을 *구성* 한다. 버리고 다시 뽑는 rejection 이
//              아니라 domain 안에서만 값이 나오게 한다.
//   shrink     실패를 유지한 채 반례를 단순화한다.
//
// 이 층이 *아닌* 것: coverage-guided fuzzing 이 아니다. 되먹임도 sanitizer corpus 도
// 없고 입력은 seed 에서 결정론적으로 생성될 뿐이다.
//
// 알고리즘 입력은 대부분 이산이라 파라미터를 int64_t 로 든다 — 크기 n, 폭 w,
// 밀도 백분율, 시드 같은 것들이다. 축소는 그 정수를 목표값 쪽으로 당긴다.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace prop {

// 결정론적 xorshift. 표본 번호와 property 이름을 섞어 시드를 만든다 —
// property 를 하나 추가해도 다른 property 의 입력이 바뀌지 않는다.
class Rng {
public:
    explicit Rng(uint64_t seed) : s_(seed ? seed : 0x9e3779b97f4a7c15ull) {}
    uint64_t Next() {
        s_ ^= s_ << 13; s_ ^= s_ >> 7; s_ ^= s_ << 17;
        return s_;
    }
    uint32_t Below(uint32_t n) { return n ? uint32_t(Next() % n) : 0; }
    int64_t Between(int64_t lo, int64_t hi) {
        if (hi <= lo) return lo;
        return lo + int64_t(Next() % uint64_t(hi - lo + 1));
    }

private:
    uint64_t s_;
};

inline uint64_t Mix(uint64_t a, uint64_t b) {
    uint64_t h = a ^ (b + 0x9e3779b97f4a7c15ull + (a << 6) + (a >> 2));
    h ^= h >> 33; h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return h;
}

inline uint64_t NameHash(const char* text) {
    uint64_t h = 1469598103934665603ull;
    while (*text) { h ^= uint64_t(uint8_t(*text++)); h *= 1099511628211ull; }
    return h;
}

// snippet-begin: property-param
// 파라미터 하나의 domain. `simplest` 는 축소가 향하는 목표이자 "가장 단순한
// 유효값" 이다. 반드시 [lo, hi] 안이어야 한다 — 축소가 domain 을 벗어나면
// 사전조건이 깨져 실패의 *원인* 이 바뀐다.
struct ParamSpec {
    const char* name;
    int64_t     lo;
    int64_t     hi;
    int64_t     simplest;
};

// 분포 설계. 균등 난수만으로는 경계가 거의 안 나온다.
//   일반 난수 60 / 경계값 25 / 목표값 근처 15
inline int64_t GenerateParam(Rng& rng, const ParamSpec& spec) {
    const uint32_t bucket = rng.Below(100);
    if (bucket < 60) return rng.Between(spec.lo, spec.hi);
    if (bucket < 85) {
        const int64_t anchors[3] = {spec.lo, spec.hi, spec.simplest};
        const int64_t v = anchors[rng.Below(3)];
        return (v < spec.lo || v > spec.hi) ? spec.simplest : v;
    }
    const int64_t near = spec.simplest + int64_t(rng.Below(3));
    return (near > spec.hi) ? spec.hi : near;
}
// snippet-end: property-param

// property 본문이 채우는 관측 기록.
//   interesting  이 표본이 *비퇴화* 인가. 자명하게 통과하는 표본만 모이면
//                property 는 아무것도 강제하지 않는다(vacuity).
//   skipped      사전조건 밖이라 판정하지 않는다.
struct Observation {
    bool        interesting = false;
    bool        skipped     = false;
    const char* failure     = "";
    int64_t     observed    = 0;
    int64_t     expected    = 0;

    bool Equal(const char* what, int64_t got, int64_t want) {
        if (got == want) return true;
        failure = what;
        observed = got;
        expected = want;
        return false;
    }
    bool True(const char* what, bool condition) {
        if (condition) return true;
        failure = what;
        observed = 0;
        expected = 1;
        return false;
    }
};

using CheckFn = bool (*)(const int64_t* params, Observation& obs);

struct Property {
    const char*      step;
    const char*      name;
    const ParamSpec* params;
    int              param_count;
    CheckFn          check;
    int              samples;
    int              min_interest_pct;   // 비퇴화 표본 하한(%)
    const char*      contract;
};

struct RunConfig {
    uint64_t master_seed = 20260824ull;
    int      scale       = 1;
    int      only_sample = -1;    // >= 0 이면 그 표본 하나만 (재현 모드)
};

struct RunResult {
    int samples = 0;
    int interesting = 0;
    int skipped = 0;
    bool failed = false;
    int failed_sample = -1;
    int shrink_steps = 0;
    std::vector<int64_t> generated;
    std::vector<int64_t> shrunk;
    Observation failure{};
};

inline void FillParams(const Property& p, uint64_t master, int sample,
                       std::vector<int64_t>& out) {
    Rng rng(Mix(master ^ NameHash(p.name), uint64_t(sample) + 1u));
    out.resize(size_t(p.param_count));
    for (int k = 0; k < p.param_count; ++k) out[size_t(k)] = GenerateParam(rng, p.params[k]);
}

inline bool StillFails(const Property& p, const std::vector<int64_t>& values,
                       Observation& obs) {
    obs = Observation{};
    const bool ok = p.check(values.data(), obs);
    return !ok && !obs.skipped;
}

// snippet-begin: property-shrink
// 탐욕적 축소. 파라미터마다 목표값 · 중간값 · 한 걸음 감소를 시도하고, 실패가
// 유지되면 채택한다. 후보가 전부 domain 안이므로 축소 뒤에도 사전조건이 유지된다.
inline int Shrink(const Property& p, std::vector<int64_t>& values, Observation& obs) {
    int steps = 0, budget = 300;
    bool moved = true;
    while (moved && budget > 0) {
        moved = false;
        for (int k = 0; k < p.param_count && budget > 0; ++k) {
            const ParamSpec& spec = p.params[k];
            const int64_t current = values[size_t(k)];
            const int64_t candidates[3] = {
                spec.simplest,
                current + (spec.simplest - current) / 2,
                (current > spec.simplest) ? current - 1 : current + 1,
            };
            for (int c = 0; c < 3 && budget > 0; ++c) {
                const int64_t candidate = candidates[c];
                if (candidate < spec.lo || candidate > spec.hi) continue;
                if (candidate == current) continue;
                // "더 단순하다" 의 정의 — 목표값에 더 가깝다.
                const int64_t d_new = (candidate > spec.simplest) ? candidate - spec.simplest
                                                                  : spec.simplest - candidate;
                const int64_t d_old = (current > spec.simplest) ? current - spec.simplest
                                                                : spec.simplest - current;
                if (d_new >= d_old) continue;
                values[size_t(k)] = candidate;
                --budget;
                Observation trial;
                if (StillFails(p, values, trial)) {
                    obs = trial;
                    moved = true;
                    ++steps;
                    break;
                }
                values[size_t(k)] = current;
            }
        }
    }
    return steps;
}
// snippet-end: property-shrink

inline RunResult Run(const Property& p, const RunConfig& config) {
    RunResult r;
    const int planned = (config.only_sample >= 0) ? 1 : p.samples * config.scale;
    const int begin   = (config.only_sample >= 0) ? config.only_sample : 0;

    std::vector<int64_t> values;
    for (int index = 0; index < planned; ++index) {
        const int sample = begin + index;
        FillParams(p, config.master_seed, sample, values);
        Observation obs;
        const bool ok = p.check(values.data(), obs);
        ++r.samples;
        if (obs.skipped) { ++r.skipped; continue; }
        if (obs.interesting) ++r.interesting;
        if (ok) continue;

        // 첫 반례에서 멈춘다 — 축소된 하나가 원본 백 개보다 낫다.
        r.failed = true;
        r.failed_sample = sample;
        r.generated = values;
        Observation shrunk = obs;
        r.shrink_steps = Shrink(p, values, shrunk);
        r.shrunk = values;
        r.failure = shrunk;
        break;
    }
    return r;
}

// snippet-begin: property-report
// 반례 보고. 시드 · 축소 결과 · *그 표본 하나만 다시 도는 명령* 을 함께 낸다.
// 재현 명령이 없으면 반례는 로그일 뿐 디버깅 도구가 아니다.
inline void ReportFailure(const Property& p, const RunResult& r, const RunConfig& config,
                          const char* binary) {
    std::printf("\nPROPERTY 반례 [%s] 표본 %d\n", p.name, r.failed_sample);
    std::printf("  계약: %s\n", p.contract);
    std::printf("  실패: %s (관측 %lld, 기대 %lld)\n", r.failure.failure,
                (long long)r.failure.observed, (long long)r.failure.expected);
    std::printf("  생성: ");
    for (int k = 0; k < p.param_count; ++k)
        std::printf("%s=%lld ", p.params[k].name, (long long)r.generated[size_t(k)]);
    std::printf("\n  축소: ");
    for (int k = 0; k < p.param_count; ++k)
        std::printf("%s=%lld ", p.params[k].name, (long long)r.shrunk[size_t(k)]);
    std::printf("(%d 걸음)\n", r.shrink_steps);
    std::printf("  재현: %s --properties --only=%s --seed=%llu --sample=%d\n\n",
                binary, p.name, (unsigned long long)config.master_seed, r.failed_sample);
}
// snippet-end: property-report

}  // namespace prop
