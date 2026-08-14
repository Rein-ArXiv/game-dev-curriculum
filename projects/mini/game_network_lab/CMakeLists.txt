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

add_executable(game_network_process_tests tests/process_tests.cpp)
add_dependencies(game_network_process_tests game_server game_client)
if(WIN32)
    target_compile_definitions(game_network_process_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(game_network_process_tests PRIVATE ws2_32)
endif()

foreach(target game_network_core game_network_platform game_server game_client
               game_network_tests game_network_runtime_tests
               game_network_send_queue_tests game_network_process_tests)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /EHsc)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    endif()
endforeach()

enable_testing()
add_test(NAME game_network_protocol COMMAND game_network_tests)
add_test(NAME game_network_runtime COMMAND game_network_runtime_tests)
add_test(NAME game_network_send_queue COMMAND game_network_send_queue_tests)
add_test(NAME game_network_process COMMAND game_network_process_tests)
set_tests_properties(game_network_send_queue PROPERTIES TIMEOUT 30)
set_tests_properties(game_network_process PROPERTIES TIMEOUT 12)
