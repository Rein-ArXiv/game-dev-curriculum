cmake_minimum_required(VERSION 3.20)
project(game_network_lab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(game_network_core
    src/protocol.cpp
    src/server_runtime.cpp
)
target_include_directories(game_network_core PUBLIC src)

if(WIN32)
    add_library(game_network_platform src/iocp_server.cpp)
    target_compile_definitions(game_network_platform PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(game_network_platform PUBLIC game_network_core ws2_32 mswsock)
elseif(UNIX AND NOT APPLE)
    add_library(game_network_platform src/epoll_server.cpp)
    target_link_libraries(game_network_platform PUBLIC game_network_core pthread)
else()
    message(FATAL_ERROR "game_network_lab supports Windows and Linux")
endif()
target_include_directories(game_network_platform PUBLIC src)

add_executable(game_server src/server_main.cpp)
target_link_libraries(game_server PRIVATE game_network_platform)

add_executable(game_client src/client_main.cpp)
target_link_libraries(game_client PRIVATE game_network_core)
if(WIN32)
    target_compile_definitions(game_client PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(game_client PRIVATE ws2_32)
endif()

add_executable(game_network_tests tests/protocol_tests.cpp)
target_link_libraries(game_network_tests PRIVATE game_network_core)

add_executable(game_network_runtime_tests tests/runtime_tests.cpp)
target_link_libraries(game_network_runtime_tests PRIVATE game_network_core)

add_executable(game_network_send_queue_tests tests/send_queue_tests.cpp)
target_link_libraries(game_network_send_queue_tests PRIVATE game_network_platform)
if(WIN32)
    target_compile_definitions(game_network_send_queue_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(game_network_send_queue_tests PRIVATE ws2_32)
endif()

add_executable(game_network_lifecycle_tests tests/lifecycle_tests.cpp)
target_link_libraries(game_network_lifecycle_tests PRIVATE game_network_platform)
if(WIN32)
    target_compile_definitions(game_network_lifecycle_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(game_network_lifecycle_tests PRIVATE ws2_32)
endif()

add_executable(game_network_process_tests tests/process_tests.cpp)
add_dependencies(game_network_process_tests game_server game_client)
if(WIN32)
    target_compile_definitions(game_network_process_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(game_network_process_tests PRIVATE ws2_32)
endif()

foreach(target game_network_core game_network_platform game_server game_client
               game_network_tests game_network_runtime_tests
               game_network_send_queue_tests game_network_lifecycle_tests
               game_network_process_tests)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /EHsc)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    endif()
endforeach()

enable_testing()

# 실행 파일 전체를 도는 타깃. 한 번에 모든 계층을 확인할 때 쓴다.
add_test(NAME game_network_protocol COMMAND game_network_tests)
add_test(NAME game_network_runtime COMMAND game_network_runtime_tests)
add_test(NAME game_network_send_queue COMMAND game_network_send_queue_tests)
add_test(NAME game_network_lifecycle COMMAND game_network_lifecycle_tests)
add_test(NAME game_network_process COMMAND game_network_process_tests)

# step 단위 타깃. 같은 실행 파일에 step 태그를 넘겨 그 step이 추가한 case만 돈다.
# 어느 case가 어느 step에 속하는지는 tests/step_cases.h 의 표에 있다.
add_test(NAME game_network_step01_protocol COMMAND game_network_tests step01)
add_test(NAME game_network_step02_tick_clock COMMAND game_network_runtime_tests step02)
add_test(NAME game_network_step02_send_queue COMMAND game_network_send_queue_tests step02)
add_test(NAME game_network_step05_authority COMMAND game_network_runtime_tests step05)
add_test(NAME game_network_step06_process_smoke COMMAND game_network_process_tests step06)

# backend는 플랫폼마다 하나만 컴파일된다. 같은 계약을 확인하는 두 case는 지금 빌드되는
# backend를 쓴 step에 속한다 — Windows는 IOCP, Linux는 epoll.
if(WIN32)
    add_test(NAME game_network_step03_backend_restart
             COMMAND game_network_lifecycle_tests step03)
    add_test(NAME game_network_step03_datagram_recovery
             COMMAND game_network_process_tests step03)
else()
    add_test(NAME game_network_step04_backend_restart
             COMMAND game_network_lifecycle_tests step04)
    add_test(NAME game_network_step04_datagram_recovery
             COMMAND game_network_process_tests step04)
endif()

foreach(socket_test game_network_send_queue game_network_step02_send_queue
                    game_network_lifecycle)
    set_tests_properties(${socket_test} PROPERTIES TIMEOUT 30)
endforeach()

if(WIN32)
    set_tests_properties(game_network_step03_backend_restart PROPERTIES TIMEOUT 30)
    set_tests_properties(game_network_step03_datagram_recovery PROPERTIES TIMEOUT 20)
else()
    set_tests_properties(game_network_step04_backend_restart PROPERTIES TIMEOUT 30)
    set_tests_properties(game_network_step04_datagram_recovery PROPERTIES TIMEOUT 20)
endif()

# 두 프로세스를 띄우는 case가 둘이므로 전체 타깃은 그 두 배 시간을 잡는다.
set_tests_properties(game_network_process PROPERTIES TIMEOUT 24)
set_tests_properties(game_network_step06_process_smoke PROPERTIES TIMEOUT 20)
