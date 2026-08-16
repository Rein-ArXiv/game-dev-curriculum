cmake_minimum_required(VERSION 3.20)
project(languages_lab CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(languages_demo src/main.cpp)
add_executable(languages_tests src/tests.cpp)
add_executable(languages_bench src/bench.cpp)

foreach(target languages_demo languages_tests languages_bench)
    target_include_directories(${target} PRIVATE src)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

enable_testing()

# 전체 실행 타깃. step별 타깃과 별개로 남긴다 — 한 번의 실행으로 전부 통과했다고
# 말할 수 있는 자리가 있어야 한다.
add_test(NAME languages_tests COMMAND languages_tests)

# step별 타깃. 인자는 src/tests.cpp 등록표의 첫 칸(step 라벨)이며, 각 step 문서는
# 자기 타깃만 돌려 그 step이 추가한 계약만 확인한다. 라벨이 등록표에 없으면
# 실행 파일이 "선택된 테스트가 없다"로 실패하므로 오타가 통과로 보이지 않는다.
function(languages_add_step_test step title)
    add_test(NAME languages_${step}_${title} COMMAND languages_tests ${step})
endfunction()

languages_add_step_test(step01 lexical_contract)
languages_add_step_test(step02 precedence_compiler)
languages_add_step_test(step03 bytecode_verifier)
languages_add_step_test(step04 vm_runtime)
languages_add_step_test(step05 host_binding)
languages_add_step_test(step06 measurement)
