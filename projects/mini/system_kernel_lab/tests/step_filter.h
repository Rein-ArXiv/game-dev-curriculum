#pragma once

// step 별 실행 필터.
//
// 세 테스트 바이너리는 각각 검사 함수를 여럿 갖는다. 인자 없이 부르면 전부
// 돈다 — 기존 동작 그대로다. `--step=N` 을 주면 *그 step 이 추가한 것을 보는
// 검사* 만 돈다. CMake 는 step 마다 add_test 를 따로 걸어, 각 step 문서가
// "지금까지 만든 것" 이 아니라 "이 step 이 만든 것" 을 실행하는 명령을 적을 수
// 있게 한다.
//
// 소속을 정하는 규칙은 하나다 — *그 step 이 무엇을 추가했는가*. pread·mmap
// backend 를 도는 검사는 그 둘을 추가한 step 에 속하고, 같은 계약을 read 로
// 확인하는 검사는 read backend 를 추가한 step 에 속한다. 실행 모델도 마찬가지라
// 워커 풀 조합과 코루틴 풀 조합은 서로 다른 step 이다.
//
// 인자 문법:
//   (없음)      전부 실행
//   --step=N    step N 이 추가한 검사만
//   --list      소속과 이름만 찍고 종료
//
// 선택된 검사가 0개면 종료 코드 2 다. add_test 가 검사 없는 step 을 가리키는
// 배선 오류를 "아무것도 안 돌았지만 초록" 으로 넘기지 않기 위해서다.

#include <cstddef>
#include <iostream>
#include <string>

namespace skl_test {

struct StepCase {
    int         step;   // 이 검사를 추가한 step 번호
    const char* name;   // 사람이 읽는 이름
    void      (*fn)();  // 검사 본체
};

inline std::string StepTag(int step) {
    std::string tag = "step";
    if (step < 10) tag += '0';
    tag += std::to_string(step);
    return tag;
}

// failureCount 는 호출자가 세는 누적 실패 수를 읽는다(파일마다 int 와
// atomic<int> 로 다르다). afterEach 는 검사 사이의 전역 상태 정리 — 주입
// 규칙이 다음 검사로 새지 않게 한다. nullptr 이면 생략한다.
inline int RunStepCases(int argc, char** argv, const char* suite,
                        const StepCase* cases, std::size_t count,
                        int (*failureCount)(), void (*afterEach)()) {
    int  wanted = -1;
    bool list   = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--list") {
            list = true;
            continue;
        }
        if (arg.rfind("--step=", 0) == 0) {
            const std::string value = arg.substr(7);
            if (value.empty() ||
                value.find_first_not_of("0123456789") != std::string::npos) {
                std::cerr << suite << ": bad step number in '" << arg << "'\n";
                return 2;
            }
            wanted = std::stoi(value);
            continue;
        }
        std::cerr << suite << ": unknown argument '" << arg
                  << "' (expected --step=N or --list)\n";
        return 2;
    }

    if (list) {
        for (std::size_t i = 0; i < count; ++i) {
            std::cout << StepTag(cases[i].step) << "  " << cases[i].name << '\n';
        }
        return 0;
    }

    std::size_t selected = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (wanted >= 0 && cases[i].step != wanted) continue;
        ++selected;
        const int before = failureCount();
        std::cout << "[ run  ] " << StepTag(cases[i].step) << "  "
                  << cases[i].name << '\n';
        cases[i].fn();
        if (afterEach != nullptr) afterEach();
        std::cout << (failureCount() == before ? "[  ok  ] " : "[ FAIL ] ")
                  << StepTag(cases[i].step) << "  " << cases[i].name << '\n';
    }

    if (selected == 0) {
        std::cerr << suite << ": --step=" << wanted
                  << " selected no checks — add_test 배선이 어긋났습니다\n";
        return 2;
    }

    const int failed = failureCount();
    std::cout << suite;
    if (wanted >= 0) std::cout << " --step=" << wanted;
    std::cout << ": " << selected << '/' << count << " check(s) run, " << failed
              << " failed\n";
    return failed == 0 ? 0 : 1;
}

}  // namespace skl_test
