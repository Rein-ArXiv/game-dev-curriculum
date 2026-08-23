cmake_minimum_required(VERSION 3.20)
project(math_lab CXX)

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
    src/scenes/welcome_scene.cpp
    src/scenes/rotation_scene.cpp
    src/scenes/interpolation_scene.cpp
    src/scenes/noise_scene.cpp
    src/scenes/collision_scene.cpp
    src/scenes/spatial_scene.cpp
    src/scenes/sdf_scene.cpp
    src/scenes/projection_scene.cpp
)

enable_testing()
# GUI 없이 도는 코어 검증. algo/ 는 헤더온리라 소스가 필요 없고, 화면 분할 계산인
# ComputeLayout 만 scene_chrome.cpp 에 있어 함께 컴파일한다(그리기 함수는 부르지 않는다).
add_executable(math_core_tests
    tests/core_tests.cpp
    tests/property.h
    src/ui/scene_chrome.cpp)
target_include_directories(math_core_tests PRIVATE src)
target_link_libraries(math_core_tests PRIVATE raylib)

# 실행 단위를 step 입도로 나눈다. 인자는 core_tests.cpp 의 kCases 표에 있는 step 키고,
# ctest 이름은 그 step 의 문서 이름과 같다 — 어느 step 이 깨졌는지 이름만 보고 안다.
set(MATH_LAB_STEPS
    step01_skeleton
    step02_rotation
    step03_interpolation
    step04_noise
    step05_collision
    step06_spatial
    step07_sdf
    step08_projection)

foreach(step IN LISTS MATH_LAB_STEPS)
    string(SUBSTRING "${step}" 0 6 step_key)
    add_test(NAME math_${step} COMMAND math_core_tests "${step_key}")
endforeach()

# step 필터 없이 표 전체를 도는 타깃. 위 step 타깃의 합집합이다.
add_test(NAME math_core_tests COMMAND math_core_tests)

# property 층만 도는 타깃. 위 step 타깃에 이미 포함되지만, 시드 무작위 검증이
# 깨졌는지를 한 줄로 보기 위한 진입점이다. 표본을 늘린 야간 캠페인은 같은
# 실행 파일에 `--scale=N` 을 주면 된다(ctest 타깃으로 걸지 않는다 — 매 커밋에
# 도는 것과 오래 도는 것을 섞지 않는다).
add_test(NAME math_property_layer COMMAND math_core_tests --properties)

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    ${raygui_SOURCE_DIR}/src)
target_link_libraries(${PROJECT_NAME} PRIVATE raylib)

# 플랫폼 분기.
if(UNIX AND NOT APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE MATH_LAB_LINUX=1)
elseif(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE MATH_LAB_WINDOWS=1)
elseif(APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE MATH_LAB_MACOS=1)
endif()

# 컴파일러 경고
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(math_core_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall -Wextra -Wpedantic)
    target_compile_options(math_core_tests PRIVATE
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
