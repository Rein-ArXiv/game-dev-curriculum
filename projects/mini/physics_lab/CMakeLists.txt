cmake_minimum_required(VERSION 3.20)
project(physics_lab CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# raylib 5.0 still declares an old CMake compatibility range. CMake 4.x
# requires an explicit minimum policy version for such dependencies.
if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# raylib을 FetchContent로 가져온다. 별도 시스템 설치 불필요.
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
set(SUPPORT_MODULE_RAUDIO OFF CACHE BOOL "" FORCE)  # 오디오 불필요

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)

# raygui는 단일 헤더 라이브러리. 헤더만 가져와 include path에 추가.
FetchContent_Declare(
    raygui
    GIT_REPOSITORY https://github.com/raysan5/raygui.git
    GIT_TAG 4.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR none
)
FetchContent_MakeAvailable(raygui)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/measure/frame_timer.cpp
    src/ui/raygui_impl.cpp
    src/ui/handle.cpp
    src/ui/scene_chrome.cpp
    src/ui/line_graph.cpp
    src/scenes/welcome_scene.cpp
    src/scenes/integrators_scene.cpp
    src/scenes/forces_scene.cpp
    src/scenes/broad_phase_scene.cpp
    src/scenes/narrow_phase_scene.cpp
    src/scenes/response_scene.cpp
    src/scenes/constraints_scene.cpp
    src/scenes/measure_scene.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    ${raygui_SOURCE_DIR}/src)
target_link_libraries(${PROJECT_NAME} PRIVATE raylib)

add_executable(physics_core_tests tests/physics_core_tests.cpp)
target_include_directories(physics_core_tests PRIVATE src)
enable_testing()
add_test(NAME physics_core_tests COMMAND physics_core_tests)

# Scene 계산부를 검증하는 타깃들. 제품 TU 를 그대로 컴파일해 링크한다 —
# 테스트가 제품 코드를 링크하지 않으면 무엇을 추가해도 제품을 검증하지 못한다.
# GUI 진입점(DrawWorld / DrawPanel)은 부르지 않으므로 창 없이 돈다.
function(physlab_add_scene_test target)
    add_executable(${target}
        tests/${target}.cpp
        ${ARGN}
        src/ui/handle.cpp
        src/ui/line_graph.cpp
        src/ui/raygui_impl.cpp
    )
    target_include_directories(${target} PRIVATE
        src
        ${raygui_SOURCE_DIR}/src)
    target_link_libraries(${target} PRIVATE raylib)
    add_test(NAME ${target} COMMAND ${target})
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

physlab_add_scene_test(physics_collision_tests   src/scenes/narrow_phase_scene.cpp)
physlab_add_scene_test(physics_broad_tests       src/scenes/broad_phase_scene.cpp)
physlab_add_scene_test(physics_response_tests    src/scenes/response_scene.cpp)
physlab_add_scene_test(physics_constraints_tests src/scenes/constraints_scene.cpp)
# Measure 는 Response 와 *같은* 접촉 솔버를 돌아야 한다. 두 TU 를 함께 링크해
# 그 동치를 ctest 가 직접 확인한다.
physlab_add_scene_test(physics_determinism_tests src/scenes/measure_scene.cpp
                                                 src/scenes/response_scene.cpp)

# 플랫폼 분기.
if(UNIX AND NOT APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE PHYSICS_LAB_LINUX=1)
elseif(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE PHYSICS_LAB_WINDOWS=1)
elseif(APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE PHYSICS_LAB_MACOS=1)
endif()

# 컴파일러 경고 (Scene 테스트 타깃은 physlab_add_scene_test 안에서 지정한다)
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(physics_core_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall -Wextra -Wpedantic)
    target_compile_options(physics_core_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

# Release 빌드 최적화.
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/Oi>)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-fno-omit-frame-pointer>)
endif()
