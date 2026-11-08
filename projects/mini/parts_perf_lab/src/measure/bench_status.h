#pragma once

namespace ppl {

// 벤치마크 한 회의 결과 상태.
//
// 시간만 비교하면 "덜 일한 경로가 더 빠른 것"처럼 보일 수 있다. target/completed를
// 분리하고, 완료 수가 요청보다 적으면 그 표본을 순위 비교에서 제외한다.
enum class BenchStatus : int {
    Valid = 0,        // 요청한 작업량을 모두 완료
    Skipped,          // allocator 없음·target 0 등으로 실행하지 않음
    InvalidCapacity,  // 용량/고갈로 요청량을 다 하지 못함 — 비교 무효
};

inline const char* BenchStatusName(BenchStatus s) {
    switch (s) {
        case BenchStatus::Valid:           return "valid";
        case BenchStatus::Skipped:         return "skipped";
        case BenchStatus::InvalidCapacity: return "invalid-capacity";
    }
    return "?";
}

}  // namespace ppl
