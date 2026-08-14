cmake_minimum_required(VERSION 3.20)
project(db_lab CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(db_bench      src/db_bench.cpp)
add_executable(db_crash_test src/db_crash_test.cpp)
add_executable(db_tests      src/db_tests.cpp)

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
