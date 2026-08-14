cmake_minimum_required(VERSION 3.20)
project(ai_lab CXX)

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
    src/ui/scene_chrome.cpp
    src/grid/grid.cpp
    src/scenes/welcome_scene.cpp
    src/scenes/pathfind_scene.cpp
    src/scenes/flowfield_scene.cpp
    src/scenes/steering_scene.cpp
    src/scenes/boids_scene.cpp
    src/scenes/influence_scene.cpp
)

enable_testing()
add_executable(ai_pathfinder_tests
    tests/pathfinder_tests.cpp
    src/grid/grid.cpp
)
target_include_directories(ai_pathfinder_tests PRIVATE src)
target_link_libraries(ai_pathfinder_tests PRIVATE raylib)
add_test(NAME ai_pathfinder_tests COMMAND ai_pathfinder_tests)

add_executable(ai_model_tests tests/ai_model_tests.cpp)
target_include_directories(ai_model_tests PRIVATE src)
add_test(NAME ai_model_tests COMMAND ai_model_tests)

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    ${raygui_SOURCE_DIR}/src)
target_link_libraries(${PROJECT_NAME} PRIVATE raylib)

# 플랫폼 분기.
if(UNIX AND NOT APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE AI_LAB_LINUX=1)
    target_link_libraries(${PROJECT_NAME} PRIVATE m)
elseif(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE AI_LAB_WINDOWS=1)
elseif(APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE AI_LAB_MACOS=1)
endif()

# 컴파일러 경고.
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(ai_pathfinder_tests PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(ai_model_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(ai_pathfinder_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(ai_model_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
