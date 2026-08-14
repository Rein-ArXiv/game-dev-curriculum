cmake_minimum_required(VERSION 3.20)
project(net_lab CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
set(SUPPORT_MODULE_RAUDIO OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)

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
    src/scenes/welcome_scene.cpp
    src/scenes/rollback_scene.cpp
    src/scenes/prediction_scene.cpp
    src/scenes/lockstep_scene.cpp
    src/scenes/matchmaking_scene.cpp
    src/scenes/lagsim_scene.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    ${raygui_SOURCE_DIR}/src)
target_link_libraries(${PROJECT_NAME} PRIVATE raylib)

add_executable(net_model_tests tests/network_model_tests.cpp)
target_include_directories(net_model_tests PRIVATE src)
enable_testing()
add_test(NAME net_model_tests COMMAND net_model_tests)

if(UNIX AND NOT APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE NET_LAB_LINUX=1)
    target_link_libraries(${PROJECT_NAME} PRIVATE m)
elseif(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE NET_LAB_WINDOWS=1)
elseif(APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE NET_LAB_MACOS=1)
endif()

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(net_model_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(net_model_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
