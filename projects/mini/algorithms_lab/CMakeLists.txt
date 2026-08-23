cmake_minimum_required(VERSION 3.20)
project(algorithms_lab CXX)

# 외부 의존성이 없다. 이 lab 의 학습 대상이 알고리즘 그 자체이므로 라이브러리를
# 끌어오면 검증할 것이 사라진다. std:: 는 오라클(느린 기준 구현)로만 쓴다.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(algo_bench src/algo_bench.cpp)
add_executable(algo_tests tests/algo_tests.cpp)

foreach(t algo_bench algo_tests)
  target_include_directories(${t} PRIVATE src tests)
  if(MSVC)
    # 주석이 UTF-8 한국어다. /utf-8 이 없으면 CP949 하위 바이트가 줄 이음 역슬래시로
    # 해석되어 전처리 전에 다음 줄이 먹힌다.
    target_compile_options(${t} PRIVATE /W4 /utf-8 /permissive-)
  else()
    target_compile_options(${t} PRIVATE -Wall -Wextra)
  endif()
endforeach()

enable_testing()

# 전체 실행. 고정 사례 전부를 한 덩어리로 돈다.
add_test(NAME algo_tests COMMAND algo_tests)

# ----- step 별 실행 단위 -----
# 위 대상만으로는 step k 를 막 끝낸 독자가 *자기가 방금 추가한 것* 을 확인할 수
# 없다 — 통과 출력이 step 마다 같아지기 때문이다. 그래서 소속 step 으로 쪼갠
# 대상을 더한다. 소속을 정하는 것은 tests/algo_tests.cpp 의 kCases 표이고,
# 각 대상은 자기가 돌린 검사 이름을 함께 찍는다.
add_test(NAME algo_tests_step01 COMMAND algo_tests --step=01)   # 순서와 선택
add_test(NAME algo_tests_step02 COMMAND algo_tests --step=02)   # 온라인·구간·스트림
add_test(NAME algo_tests_step03 COMMAND algo_tests --step=03)   # 사전과 문자열
add_test(NAME algo_tests_step04 COMMAND algo_tests --step=04)   # 그래프 구조
add_test(NAME algo_tests_step05 COMMAND algo_tests --step=05)   # 최단 경로와 휴리스틱
add_test(NAME algo_tests_step06 COMMAND algo_tests --step=06)   # 탐욕과 동적 계획
add_test(NAME algo_tests_step07 COMMAND algo_tests --step=07)   # 유량·매칭·탐색
add_test(NAME algo_tests_step08 COMMAND algo_tests --step=08)   # 무손실 압축

# 시드 무작위 property 층. 고정 사례가 못 덮는 입력 공간을 훑고, 실패하면
# 반례를 축소해 시드·재현 명령과 함께 찍는다.
add_test(NAME algo_properties COMMAND algo_tests --properties)

# 축소 실행. 측정용이 아니라, 벤치가 각 팔에 걸어 둔 실패 검사(Die 경로)를
# 매 ctest 실행에서 한 번씩 지나가게 하는 것이 목적이다.
add_test(NAME algo_bench_smoke COMMAND algo_bench --smoke)

# property 층은 표본이 많아 다른 대상보다 오래 걸린다. 무한 루프로 도는 회귀를
# 실패로 판정하려면 상한이 필요하다.
set_tests_properties(algo_properties PROPERTIES TIMEOUT 300)
set_tests_properties(algo_bench_smoke PROPERTIES TIMEOUT 300)
