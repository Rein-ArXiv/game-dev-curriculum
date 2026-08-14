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
add_test(NAME languages_tests COMMAND languages_tests)
