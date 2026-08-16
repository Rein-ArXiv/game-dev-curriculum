#pragma once

namespace lab::lang::bench {

// 측정 루프와 정확성 테스트가 *같은* 소스를 쓰도록 한 곳에 둔다. 벤치마크가 자기
// 파일 안에 문자열을 들고 있으면 "두 경로가 같은 프로그램을 돌린다"는 전제를
// 테스트가 확인할 방법이 없다.
// snippet-begin: bench-source
constexpr const char* kSource =
    "let x = 1; let sum = 0;"
    "while (x <= 100) { sum = sum + x; x = x + 1; }";
// snippet-end: bench-source

// 측정 루프는 결과를 읽지 않는다. 전역 조회 비용이 시간에 섞이기 때문이다.
// 그래서 이 프로그램의 고정 결과는 테스트가 대신 못 박는다.
constexpr int kIterations = 10000;

}  // namespace lab::lang::bench
