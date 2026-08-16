// step 단위 실행 하네스.
//
// 문제. 한 실행 파일이 여러 step 의 검사를 담고 있으면 `ctest` 는 그 전부를
// 한 덩어리로만 돌린다. step k 를 막 끝낸 독자가 *자기가 방금 추가한 것* 만
// 확인할 방법이 없고, 어느 step 문서에서 실행해도 관측 출력이 똑같아진다.
//
// 그래서 각 실행 파일은 {step, 이름, 함수} 표를 넘기고 argv[1] 로 필터를 받는다.
//   physics_core_tests              # 인자 없음 — 표 전체 (기존 동작 그대로)
//   physics_core_tests step02       # step 태그로
//   physics_core_tests integrator_energy   # 케이스 이름으로
//   physics_core_tests --list       # 표 목록만 출력
//
// 케이스 함수는 실패를 세는 정수를 증가시키기만 한다(각 파일의 CHECK 매크로).
// 하네스는 그 정수를 케이스 전후로 읽어 케이스별 ok / FAILED 를 찍는다.

#pragma once

#include <cstring>
#include <iostream>

namespace physlab::testkit {

struct StepCase {
    const char* step;   // "step04" — 이 검사를 *추가한* step
    const char* name;   // 케이스 이름 (필터로도 쓴다)
    void (*fn)();
};

// 반환값이 그대로 프로세스 종료 코드가 된다. 0 통과 / 1 실패 / 2 필터 오타.
inline int RunStepCases(int argc, char** argv, const StepCase* cases,
                        int count, const char* label, const int* failures) {
    const char* filter = (argc > 1) ? argv[1] : nullptr;
    if (filter != nullptr && std::strcmp(filter, "--list") == 0) {
        for (int i = 0; i < count; ++i) {
            std::cout << cases[i].step << ' ' << cases[i].name << '\n';
        }
        return 0;
    }

    int selected = 0;
    for (int i = 0; i < count; ++i) {
        const StepCase& c = cases[i];
        if (filter != nullptr && std::strcmp(filter, c.step) != 0 &&
            std::strcmp(filter, c.name) != 0) {
            continue;
        }
        const int before = *failures;
        std::cout << "[" << c.step << "] " << c.name << '\n';
        c.fn();
        std::cout << ((*failures == before) ? "  ok\n" : "  FAILED\n");
        ++selected;
    }

    if (selected == 0) {
        // 필터 오타를 조용한 성공으로 넘기면 ctest 는 아무것도 안 돌고 통과한다.
        std::cerr << "no case matches filter `" << (filter ? filter : "")
                  << "` in " << label << '\n';
        return 2;
    }
    if (*failures != 0) {
        std::cerr << *failures << " " << label << " check(s) failed\n";
        return 1;
    }
    std::cout << selected << " case(s) ok: " << label << " ["
              << (filter ? filter : "all") << "]\n";
    return 0;
}

}  // namespace physlab::testkit
