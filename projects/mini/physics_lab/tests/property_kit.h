// property 층의 실행 하네스 — 생성기 · oracle · shrink · 반례 보고.
//
// 고정 사례 테스트는 *알려진* 배치를 못 박는다. property 테스트는 계약을 적고
// 생성기가 만든 입력 공간 전체에서 그 계약을 깨는 반례를 찾는다. 둘은 대체
// 관계가 아니다 — 고정 사례는 과거 실패를 보존하고, property 는 아직 손으로
// 만들어 본 적 없는 배치를 친다.
//
// 이 헤더가 책임지는 것은 네 가지다.
//
//   1. 결정적 난수원. 벽시계 · 프로세스 시간 · 주소값은 어디에도 들어가지
//      않는다. 같은 시드는 어느 실행 · 어느 기계에서도 같은 수열이다.
//   2. 표본별 시드 파생. 마스터 시드 하나에서 표본 N 개의 시드를 만든다.
//      실패한 표본은 *그 표본의 시드 하나* 로 다시 돌릴 수 있다.
//   3. shrink. 실패를 유지하면서 입력을 단순화한다. "표본 811 번에서 실패" 를
//      "삼각형 두 개, 오프셋 0" 으로 줄이지 못하면 반례는 읽을 수 없다.
//   4. 반례 보고. 시드 · 생성기 버전 · 축소된 입력 · 재현 명령 한 줄.
//      생성기가 바뀌면 같은 시드가 다른 입력을 만들므로 *입력 자체* 도 찍는다.
//
// 외부 라이브러리는 쓰지 않는다. 시드 루프 + 생성기 + 반례 출력이면 이 저장소의
// property 층에 필요한 것은 다 있다.

#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace physlab::testkit {

// 생성기 버전. 생성기를 고치면 올린다 — 그러면 예전 시드가 다른 입력을 만든다는
// 사실이 보고서에 남는다.
inline constexpr int kGeneratorVersion = 1;

// splitmix64. 상태 64비트, 통과 강도는 이 용도에 충분하고 무엇보다 *짧고
// 이식 가능* 하다 — 같은 시드가 어느 컴파일러에서도 같은 수열을 낸다.
class Rng {
public:
    explicit Rng(uint64_t seed) : state_(seed) {}

    uint64_t NextU64() {
        state_ += 0x9E3779B97F4A7C15ull;
        uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    // [0, 1). 상위 24비트만 쓴다 — float 의 유효 비트 수와 같아 반올림 규칙에
    // 기대지 않고 정확히 표현된다.
    float Unit() {
        return (float)(NextU64() >> 40) * (1.0f / 16777216.0f);
    }

    float Range(float lo, float hi) { return lo + (hi - lo) * Unit(); }

    int Int(int lo, int hi) {
        if (hi <= lo) return lo;
        return lo + (int)(NextU64() % (uint64_t)(hi - lo + 1));
    }

    bool Chance(float probability) { return Unit() < probability; }

private:
    uint64_t state_;
};

// 마스터 시드 + 표본 번호 → 표본 시드. 표본끼리 상관이 생기지 않도록 한 번 더
// 섞는다.
inline uint64_t DeriveSeed(uint64_t master, int sample) {
    Rng mixer(master ^ (0x9E3779B97F4A7C15ull * (uint64_t)(sample + 1)));
    return mixer.NextU64();
}

// argv[2] 로 들어온 단일 시드. 0 이면 없음. 값이 있으면 property 는 표본 하나만
// — *바로 그 시드의 입력* 만 — 돌린다. 반례 보고의 재현 명령이 이 슬롯을 쓴다.
inline uint64_t& SeedOverrideSlot() {
    static uint64_t slot = 0;
    return slot;
}
inline void SetSeedOverride(uint64_t seed) { SeedOverrideSlot() = seed; }

// 단일 시드 재현 모드. 표본이 하나뿐이므로 배터리 단위 커버리지 단언(버킷이
// 전부 채워졌는가 · hit/miss 가 한쪽으로 쏠리지 않았는가)은 건너뛴다. 반례
// 하나를 들여다보러 들어온 사람에게 "버킷이 비었다" 는 실패는 잡음이다.
inline bool SingleSeedMode() { return SeedOverrideSlot() != 0; }

inline std::string Format(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (written < 0) return std::string();
    return std::string(buffer);
}

// property 한 개의 실행 결과. 호출부가 버킷 히스토그램 같은 관측을 덧붙인다.
struct PropertyResult {
    int samples   = 0;   // 실제로 돌린 표본 수
    int failures  = 0;   // 계약을 깬 표본 수
};

// property 실행기.
//
//   generate(Rng&)          -> Input          생성기
//   check(const Input&)     -> std::string    빈 문자열이면 통과, 아니면 위반 서술
//   candidates(const Input&)-> vector<Input>  shrink 후보 (단순한 것부터)
//   dump(const Input&)      -> std::string    입력을 사람이 읽을 수 있게
//
// check 가 실패를 돌려주면 shrink 루프가 후보를 하나씩 시험한다. 실패가 유지되는
// 후보를 찾으면 그것을 새 입력으로 삼고 다시 시작한다 (탐욕 + 재시작). 어떤 후보도
// 실패를 유지하지 못하면 그 입력이 지역 최소 반례다.
template <class Input, class Generate, class Check, class Candidates, class Dump>
PropertyResult RunProperty(const char* caseName, const char* property,
                           uint64_t master, int samples, Generate&& generate,
                           Check&& check, Candidates&& candidates, Dump&& dump) {
    PropertyResult result;
    const uint64_t forced = SeedOverrideSlot();
    const int count = (forced != 0) ? 1 : samples;

    for (int index = 0; index < count; ++index) {
        const uint64_t seed = (forced != 0) ? forced : DeriveSeed(master, index);
        Rng rng(seed);
        Input input = generate(rng);
        ++result.samples;

        std::string violation = check(input);
        if (violation.empty()) continue;
        ++result.failures;

        const std::string original         = dump(input);
        const std::string originalViolation = violation;

        int shrinkSteps = 0;
        for (int round = 0; round < 128; ++round) {
            bool improved = false;
            const std::vector<Input> pool = candidates(input);
            for (const Input& candidate : pool) {
                const std::string next = check(candidate);
                if (next.empty()) continue;   // 실패가 사라졌다 — 이 축소는 버린다
                input     = candidate;
                violation = next;
                improved  = true;
                ++shrinkSteps;
                break;
            }
            if (!improved) break;
        }

        std::cerr << "\n[property FAILED] " << property << '\n'
                  << "  시드      : " << seed << "  (생성기 v"
                  << kGeneratorVersion << ", 표본 " << index << ")\n"
                  << "  위반      : " << violation << '\n'
                  << "  최초 위반 : " << originalViolation << '\n'
                  << "  원본 입력 : " << original << '\n'
                  << "  축소 입력 : " << dump(input) << "   (" << shrinkSteps
                  << " step)\n"
                  << "  재현      : physics_property_tests " << caseName << ' '
                  << seed << '\n';

        // 같은 결함이 표본마다 반복되면 보고가 로그를 덮는다. 셋까지만 찍고
        // 나머지는 세기만 한다.
        if (result.failures >= 3) {
            std::cerr << "  (같은 property 에서 반례 3 개를 찍었다 — 나머지 표본은"
                         " 세기만 한다)\n";
            break;
        }
    }
    return result;
}

}  // namespace physlab::testkit
