cmake_minimum_required(VERSION 3.20)
project(db_lab CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(db_bench      src/db_bench.cpp)
add_executable(db_crash_test tests/db_crash_test.cpp)
add_executable(db_tests      tests/db_tests.cpp)

foreach(t db_bench db_crash_test db_tests)
  target_include_directories(${t} PRIVATE src)
  if(MSVC)
    # Korean comments are UTF-8. Without /utf-8, a CP949 trail byte can be
    # interpreted as a line-splicing backslash before preprocessing.
    target_compile_options(${t} PRIVATE /W4 /utf-8)
  else()
    target_compile_options(${t} PRIVATE -Wall -Wextra)
  endif()
endforeach()

enable_testing()
add_test(NAME db_tests      COMMAND db_tests)
add_test(NAME db_crash_test COMMAND db_crash_test)
# 축소 실행. 측정용은 아니고, fsync ON 커밋 경로와 벤치의 실패 검사(Die)를
# CI 에서도 매번 지나가게 하는 것이 목적이다.
add_test(NAME db_bench_smoke COMMAND db_bench --smoke)

# ----- step 별 실행 단위 -----
# 위 세 대상은 '전부 돌린다'. 그것만으로는 step k 를 막 끝낸 독자가 *자기가 방금
# 추가한 것* 을 확인할 수 없다 — 통과 출력이 step 마다 같아지기 때문이다.
# 그래서 db_tests 를 소속 step 으로 쪼갠 대상을 더한다. 소속을 정하는 것은
# tests/db_tests.cpp 의 kCases 표이고, 각 대상은 자기 이름과 검사 목록을 찍는다.
#   step01 프레임 형식·CRC·파싱 경계 → db_tests_step01
#   step02 트랜잭션 커밋·복구         → db_tests_step02
#   step03 벤치의 실패 검사(Die)      → db_bench_smoke   (위에 이미 있음)
#   step04 크래시 주입 스윕           → db_crash_test    (위에 이미 있음)
add_test(NAME db_tests_step01 COMMAND db_tests --step=01)
add_test(NAME db_tests_step02 COMMAND db_tests --step=02)
