// property.h — 시드 무작위 property 층의 최소 하네스.
//
// 용어는 algorithms 시리즈의 property·fuzz 검증 편을 그대로 따른다.
//
//   property   생성된 *모든* 입력이 지켜야 하는 관계. 고정 기대값 표가 아니다.
//   불변식     연산 sequence 중에도 계속 참인 성질. 여기서는 단일 호출의
//              사후조건과 metamorphic 관계 둘 다를 property 로 적는다.
//   oracle     결과가 맞는지 판단하는 근거. 이 층이 쓰는 oracle 은 세 가지다 —
//              (a) 수학적 property, (b) 독립 구현 대조(differential),
//              (c) 입력 변환 전후 관계(metamorphic).
//   생성기     사전조건을 만족하는 입력을 *구성* 한다. 무작위 값을 만들고 버리는
//              rejection 이 아니라, domain 안에서만 값이 나오도록 구간을 준다.
//   shrink     실패를 유지한 채 반례를 단순화한다. 후보가 domain 을 벗어나지
//              않으므로 축소 뒤에도 사전조건이 깨지지 않는다.
//
// 이 층이 *아닌* 것: coverage-guided fuzzing 이 아니다. coverage 되먹임도
// sanitizer corpus 도 없고, 입력은 seed 에서 결정론적으로 생성될 뿐이다.
// 깊은 파서 상태를 여는 일은 이 하네스의 몫이 아니다.
//
// 외부 라이브러리를 쓰지 않는다 — 시드 루프 + 생성기 + 축소기 + 반례 출력이 전부다.

#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace prop {

// 결정론적 LCG. core_tests.cpp 의 고정 사례 쪽 Lcg 와 같은 계수를 쓴다 —
// 두 층이 같은 난수원을 공유해야 "같은 seed" 라는 말이 하나의 뜻을 가진다.
class Rng {
public:
    explicit Rng(uint32_t seed) : s_(seed ? seed : 1u) {}
    uint32_t Next() {
        s_ = s_ * 1664525u + 1013904223u;
        return s_;
    }
    float Unit() { return (float)(Next() >> 8) * (1.0f / 16777216.0f); }
    float Range(float a, float b) { return a + (b - a) * Unit(); }

private:
    uint32_t s_;
};

// 표본 번호 → 그 표본만의 seed. master seed 와 property 이름이 함께 섞이므로
// property 를 하나 추가해도 다른 property 의 입력이 바뀌지 않는다.
inline uint32_t Mix(uint32_t a, uint32_t b) {
    uint32_t h = a ^ (b + 0x9e3779b9u + (a << 6) + (a >> 2));
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

inline uint32_t NameHash(const char* text) {
    uint32_t h = 2166136261u;
    while (*text) {
        h ^= (uint32_t)(uint8_t)*text++;
        h *= 16777619u;
    }
    return h;
}

// ---- 생성기 ----------------------------------------------------------------

// 파라미터 하나의 domain. `simplest` 는 shrink 가 향하는 목표이자 "가장 단순한
// 유효값" 이다. 반드시 [lo, hi] 안이어야 한다 — 축소가 domain 을 벗어나면
// 사전조건이 깨져 실패의 원인이 바뀐다.
struct ParamSpec {
    const char* name;
    float       lo;
    float       hi;
    float       simplest;
};

// 분포 설계. 균등 난수만으로는 경계가 거의 안 나온다.
//   일반 난수 60 / 경계값 15 / 라운드값 10 / 퇴화(경계 바로 옆) 15
// 과거 실패를 담는 corpus 몫은 이 층이 아니라 core_tests.cpp 의 고정 사례가
// 가진다 — 반례가 나오면 그쪽에 fixture 로 승격한다.
// snippet-begin: property-distribution
inline float GenerateParam(Rng& rng, const ParamSpec& spec) {
    const float span = spec.hi - spec.lo;
    const uint32_t bucket = rng.Next() % 100u;

    if (bucket < 60u) return rng.Range(spec.lo, spec.hi);

    const float anchors[4] = {spec.lo, spec.hi, spec.simplest, 0.0f};
    if (bucket < 75u) {
        const float value = anchors[rng.Next() & 3u];
        return (value < spec.lo || value > spec.hi) ? spec.simplest : value;
    }
    if (bucket < 85u) {
        const float step = (span > 4.0f) ? 1.0f : 0.25f;
        float value = std::round(rng.Range(spec.lo, spec.hi) / step) * step;
        if (value < spec.lo) value = spec.lo;
        if (value > spec.hi) value = spec.hi;
        return value;
    }
    float base = anchors[rng.Next() & 3u];
    if (base < spec.lo || base > spec.hi) base = spec.simplest;
    float value = base + span * 1e-4f * (rng.Unit() * 2.0f - 1.0f);
    if (value < spec.lo) value = spec.lo;
    if (value > spec.hi) value = spec.hi;
    return value;
}
// snippet-end: property-distribution

// ---- 관측 ------------------------------------------------------------------

// property 본문이 채우는 관측 기록.
//   Bound()      관측값이 상계 이하인지. 상계 대비 비율을 누적해 최악을 남긴다.
//   interesting  이 표본이 *비퇴화* 인가. 자명하게 통과하는 표본만 모이면
//                property 는 아무것도 강제하지 않는다(part11 의 vacuity).
//   skipped      사전조건 밖이라 판정하지 않는다. 축소기는 skipped 로 바뀌는
//                후보를 거부하므로 반례가 사전조건을 유지한 채 작아진다.
struct Observation {
    float       worst         = 0.0f;
    const char* worstName     = "";
    double      worstObserved = 0.0;
    double      worstBound    = 0.0;
    bool        interesting   = false;
    bool        skipped       = false;

    bool Bound(const char* name, double observed, double bound) {
        const bool ok = !(observed > bound);   // NaN 이면 false 로 떨어진다.
        double ratio;
        if (bound > 0.0) {
            ratio = observed / bound;
        } else {
            ratio = (observed <= 0.0) ? 0.0 : 1e9;
        }
        Record(name, ratio, observed, bound);
        return ok;
    }

    // 하한 계약. 대조군("이 값은 충분히 커야 한다")을 같은 보고 형식에 담는다.
    bool AtLeast(const char* name, double observed, double minimum) {
        const bool ok = !(observed < minimum);
        const double ratio = (observed > 0.0) ? minimum / observed : 1e9;
        Record(name, ratio, observed, minimum);
        return ok;
    }

private:
    // 관측/상계 비를 누적한다. 상계 아래로 얼마나 여유가 있었는지가 그대로
    // "이 계약이 헐거운가" 의 지표다. 음수 관측은 0 으로, NaN 은 최악으로 본다.
    void Record(const char* name, double ratio, double observed, double bound) {
        if (!(ratio == ratio)) ratio = 1e9;   // NaN
        if (ratio < 0.0) ratio = 0.0;
        if (ratio > (double)worst) {
            worst         = (float)ratio;
            worstName     = name;
            worstObserved = observed;
            worstBound    = bound;
        }
    }
};

using CheckFn = bool (*)(const float* params, Observation& obs);

struct Property {
    const char*       step;            // 이 알고리즘을 들여온 step 키
    const char*       name;            // `quat/compose-assoc` 처럼 영역/성질
    const ParamSpec*  params;
    int               paramCount;
    CheckFn           check;
    int               samples;         // 기본 표본 수 (--scale 로 곱해진다)
    int               minInterestPct;  // 비퇴화 표본 하한 (%)
    int               maxInterestPct;  // 상한 (%). 100 이면 검사하지 않는다.
    int               maxSkipPct;      // 사전조건 탈락 상한 (%)
    int               minWorstPct;     // 최악 관측이 상계의 몇 % 이상이어야 하는가.
                                       // 실측 상계를 property 로 승격한 경우에만
                                       // 쓴다 — 상계가 헐거우면 성질이 자명해진다.
    const char*       contract;        // 이 property 가 단언하는 계약 한 줄
};

struct RunConfig {
    uint32_t masterSeed = 20260823u;
    int      scale      = 1;
    int      onlySample = -1;   // >= 0 이면 그 표본 하나만 (재현 모드)
};

struct RunResult {
    int    samples       = 0;
    int    interesting   = 0;
    int    skipped       = 0;
    int    planned       = 0;
    float  worst         = 0.0f;
    const char* worstName = "";
    double worstObserved = 0.0;
    double worstBound    = 0.0;
    bool   failed        = false;
    int    failedSample  = -1;
    int    shrinkSteps   = 0;
    std::vector<float> generated;
    std::vector<float> shrunk;
    Observation failure{};
};

inline void FillParams(const Property& property, uint32_t masterSeed, int sample,
                       std::vector<float>& out) {
    Rng rng(Mix(masterSeed ^ NameHash(property.name), (uint32_t)sample + 1u));
    out.resize((size_t)property.paramCount);
    for (int k = 0; k < property.paramCount; ++k) {
        out[(size_t)k] = GenerateParam(rng, property.params[k]);
    }
}

// 이 파라미터가 *판정 가능한 실패* 인가. 사전조건 밖(skipped)은 실패가 아니다.
inline bool StillFails(const Property& property, const std::vector<float>& values,
                       Observation& obs) {
    obs = Observation{};
    const bool ok = property.check(values.data(), obs);
    return !ok && !obs.skipped;
}

// 탐욕적 축소. 파라미터마다 몇 개의 후보(목표값, 중간값, 반올림)를 시도하고
// 실패가 유지되면 채택한다. 더 줄지 않을 때까지 반복한다.
inline int Shrink(const Property& property, std::vector<float>& values, Observation& obs) {
    int steps   = 0;
    int budget  = 400;
    bool moved  = true;
    while (moved && budget > 0) {
        moved = false;
        for (int k = 0; k < property.paramCount && budget > 0; ++k) {
            const ParamSpec& spec = property.params[k];
            const float current   = values[(size_t)k];
            const float candidates[5] = {
                spec.simplest,
                0.5f * (current + spec.simplest),
                std::round(current),
                std::round(current * 2.0f) * 0.5f,
                std::round(current * 10.0f) * 0.1f,
            };
            // snippet-begin: property-shrink-candidates
            for (int c = 0; c < 5 && budget > 0; ++c) {
                const float candidate = candidates[c];
                // 후보가 domain 안에 있으므로 축소 뒤에도 사전조건이 유지된다.
                if (!(candidate >= spec.lo && candidate <= spec.hi)) continue;
                if (candidate == current) continue;
                // "더 단순하다" 의 정의 — 목표값에 더 가깝다.
                if (std::fabs(candidate - spec.simplest) >
                    std::fabs(current - spec.simplest)) {
                    continue;
                }
                values[(size_t)k] = candidate;
                --budget;
                Observation trial;
                if (StillFails(property, values, trial)) {
                    obs   = trial;      // 실패가 유지됐다 — 축소를 채택한다.
                    moved = true;
                    ++steps;
                    break;
                }
                values[(size_t)k] = current;
            }
            // snippet-end: property-shrink-candidates
        }
    }
    return steps;
}

inline RunResult Run(const Property& property, const RunConfig& config) {
    RunResult result;
    const int planned = (config.onlySample >= 0) ? 1 : property.samples * config.scale;
    const int begin   = (config.onlySample >= 0) ? config.onlySample : 0;
    result.planned    = planned;

    std::vector<float> values;
    for (int index = 0; index < planned; ++index) {
        const int sample = begin + index;
        FillParams(property, config.masterSeed, sample, values);

        Observation obs;
        const bool ok = property.check(values.data(), obs);
        ++result.samples;
        if (obs.skipped) {
            ++result.skipped;
            continue;
        }
        if (obs.interesting) ++result.interesting;
        if (obs.worst > result.worst) {
            result.worst         = obs.worst;
            result.worstName     = obs.worstName;
            result.worstObserved = obs.worstObserved;
            result.worstBound    = obs.worstBound;
        }
        if (ok) continue;

        // 첫 반례에서 멈춘다 — 축소된 하나가 원본 백 개보다 낫다.
        result.failed       = true;
        result.failedSample = sample;
        result.generated    = values;
        Observation shrunk  = obs;
        result.shrinkSteps  = Shrink(property, values, shrunk);
        result.shrunk       = values;
        result.failure      = shrunk;
        break;
    }
    return result;
}

}  // namespace prop
