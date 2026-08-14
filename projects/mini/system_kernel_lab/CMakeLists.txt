cmake_minimum_required(VERSION 3.20)
project(system_kernel_lab CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# 이 lab은 Linux 전용. WSL2 또는 native Linux에서 빌드한다.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
        "system_kernel_lab is Linux-only. Build under WSL2 or native Linux.")
endif()

# SDL2 + OpenGL: GUI 기반 측정 도구.
find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig)

# POSIX AIO는 glibc 버전에 따라 librt 또는 libc에서 제공된다. 파일 존재가
# 아니라 실제 헤더+심볼 링크로 가용성을 판정한다.
find_library(SKL_RT_LIB rt)
include(CheckCXXSourceCompiles)
set(_SKL_REQUIRED_LIBRARIES_SAVE "${CMAKE_REQUIRED_LIBRARIES}")
if(SKL_RT_LIB)
    set(CMAKE_REQUIRED_LIBRARIES "${SKL_RT_LIB}")
endif()
check_cxx_source_compiles(
    "#include <aio.h>
     int main() { aiocb cb{}; (void)aio_error(&cb); return 0; }"
    SKL_POSIX_AIO_AVAILABLE)
set(CMAKE_REQUIRED_LIBRARIES "${_SKL_REQUIRED_LIBRARIES_SAVE}")
unset(_SKL_REQUIRED_LIBRARIES_SAVE)

# liburing: 시스템에 있을 때만 io_uring 백엔드 컴파일.
if(PKG_CONFIG_FOUND)
    pkg_check_modules(LIBURING IMPORTED_TARGET liburing)
endif()

# Dear ImGui: FetchContent로 가져와 직접 컴파일.
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90.4
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR none
)
FetchContent_MakeAvailable(imgui)

set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/io/aio_backend.cpp
    src/io/io_backend_factory.cpp
    src/io/mmap_backend.cpp
    src/io/pread_backend.cpp
    src/io/read_backend.cpp
    src/io/uring_backend.cpp
    src/measure/frame_timer.cpp
    src/measure/proc_counters.cpp
    src/sched/sched_policy.cpp
    src/sync/sync_factory.cpp
    src/workload/file_io_workload.cpp
    src/ui/hud.cpp
    ${IMGUI_SOURCES}
)

enable_testing()
add_executable(system_core_tests
    tests/system_core_tests.cpp
    src/io/aio_backend.cpp
    src/io/io_backend_factory.cpp
    src/io/mmap_backend.cpp
    src/io/pread_backend.cpp
    src/io/read_backend.cpp
    src/io/uring_backend.cpp
    src/sched/sched_policy.cpp
    src/sync/sync_factory.cpp
    src/workload/file_io_workload.cpp
)
target_include_directories(system_core_tests PRIVATE src)
target_link_libraries(system_core_tests PRIVATE
    Threads::Threads
    ${CMAKE_DL_LIBS}
)
add_test(NAME system_core_tests COMMAND system_core_tests)
set_tests_properties(system_core_tests PROPERTIES TIMEOUT 30)

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    SDL2::SDL2
    OpenGL::GL
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

# POSIX AIO 가드. librt가 발견되면 SKL_HAS_POSIX_AIO 정의 + 링크.
if(SKL_POSIX_AIO_AVAILABLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE SKL_HAS_POSIX_AIO=1)
    target_compile_definitions(system_core_tests PRIVATE SKL_HAS_POSIX_AIO=1)
    if(SKL_RT_LIB)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${SKL_RT_LIB})
        target_link_libraries(system_core_tests PRIVATE ${SKL_RT_LIB})
    endif()
    message(STATUS "POSIX AIO: enabled")
else()
    message(STATUS "POSIX AIO: disabled (headers/symbols unavailable)")
endif()

# io_uring 가드. liburing이 발견되면 SKL_HAS_LIBURING 정의 + 링크.
if(LIBURING_FOUND)
    target_compile_definitions(${PROJECT_NAME} PRIVATE SKL_HAS_LIBURING=1)
    target_compile_definitions(system_core_tests PRIVATE SKL_HAS_LIBURING=1)
    target_link_libraries(${PROJECT_NAME} PRIVATE PkgConfig::LIBURING)
    target_link_libraries(system_core_tests PRIVATE PkgConfig::LIBURING)
    message(STATUS "io_uring: enabled (liburing ${LIBURING_VERSION})")
else()
    message(STATUS "io_uring: disabled (liburing not found)")
endif()

target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-deprecated-declarations)  # ucontext_t (POSIX deprecated 표시) 사용
target_compile_options(system_core_tests PRIVATE
    -Wall -Wextra -Wpedantic)
