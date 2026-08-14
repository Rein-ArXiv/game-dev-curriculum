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
    src/fault/fault_inject.cpp
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

# 테스트 타깃이 공유하는 코어 TU 목록. GUI(SDL2/OpenGL/ImGui) 의존이 없다 —
# 그래서 테스트는 창을 띄우지 않고 backend·sync·sched·workload 를 직접 부른다.
set(SKL_CORE_TEST_SOURCES
    src/fault/fault_inject.cpp
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

add_executable(system_core_tests
    tests/system_core_tests.cpp ${SKL_CORE_TEST_SOURCES})

# 오류 경로 전용. src/fault 의 주입 seam 으로 EINTR 재시도, aio 수확,
# io_uring 링 재초기화, factory 폴백, 스케줄링 권한 실패를 결정적으로 밟는다.
add_executable(system_fault_tests
    tests/system_fault_tests.cpp ${SKL_CORE_TEST_SOURCES})

# PROJECTS_PLAN 이 지정한 I/O 3항목 전용: 부분 읽기(short read) 계약,
# 실제 시그널로 만드는 EINTR(FIFO + pthread_kill), 취소 후 수확(drain) 규약.
add_executable(system_io_path_tests
    tests/system_io_path_tests.cpp ${SKL_CORE_TEST_SOURCES})

set(SKL_TEST_TARGETS system_core_tests system_fault_tests system_io_path_tests)

# 헤드리스 측정 러너. GUI 도구와 *같은 워크로드 · 같은 계측기* 를 창 없이 돌려
# 사이클마다 4채널(cycle ms · MB/s · ctx-sw delta · page fault delta)을 CSV 한
# 줄로 뱉는다. 테스트 타깃이 이미 증명한 분리를 그대로 써서 SDL2/OpenGL/ImGui
# 는 링크하지 않는다. FrameTimer/ProcCounters 는 GUI 의존이 없으므로 코어 TU
# 목록에 두 파일만 더 얹으면 된다.
# snippet-begin: bench-target
add_executable(system_kernel_bench
    tools/bench_main.cpp
    ${SKL_CORE_TEST_SOURCES}
    src/measure/frame_timer.cpp
    src/measure/proc_counters.cpp)

target_include_directories(system_kernel_bench PRIVATE src)
target_link_libraries(system_kernel_bench PRIVATE
    Threads::Threads
    ${CMAKE_DL_LIBS}
)
# snippet-end: bench-target
# 테스트 타이밍 훅은 정의하지 않는다 — 측정 경로에 훅 분기를 넣지 않기 위해서다.
target_compile_options(system_kernel_bench PRIVATE -Wall -Wextra -Wpedantic)

# backend 가용성 정의(SKL_HAS_POSIX_AIO / SKL_HAS_LIBURING)와 링크는 테스트와
# 러너에 똑같이 걸어야 한다. 한쪽만 걸면 측정에 들어가는 backend 집합이 앱과
# 달라진다.
set(SKL_HEADLESS_TARGETS ${SKL_TEST_TARGETS} system_kernel_bench)

foreach(skl_test IN LISTS SKL_TEST_TARGETS)
    target_include_directories(${skl_test} PRIVATE src)
    target_link_libraries(${skl_test} PRIVATE
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )
    # 테스트 전용 타이밍 훅(src/testhook/test_hook.h). 앱 타깃에는 정의하지
    # 않으므로 제품 바이너리에서는 훅 호출이 통째로 사라진다.
    target_compile_definitions(${skl_test} PRIVATE SKL_TEST_HOOKS=1)
    target_compile_options(${skl_test} PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ${skl_test} COMMAND ${skl_test})
    # TIMEOUT 은 작업 큐 인계 회귀(remaining_ 언더플로)가 만드는 hang 을 실패로
    # 바꾸는 안전망이다. system_core_tests 는 자체 워치독으로 먼저 잡는다.
    set_tests_properties(${skl_test} PROPERTIES TIMEOUT 60)
endforeach()

# 폴백을 실패로 만드는 케이스. liburing·POSIX AIO 가 있는 머신에서 io_uring/aio
# 가 조용히 read 로 내려앉으면 여기서 실패한다. 없는 머신도 있으므로 기본
# 케이스와 분리하고 라벨을 붙인다 —
#   ctest -L strict          # 요구
#   ctest -LE strict         # 이식성 모드
add_test(NAME system_core_tests_strict COMMAND system_core_tests)
set_tests_properties(system_core_tests_strict PROPERTIES
    ENVIRONMENT "SKL_REQUIRE_BACKENDS=uring,aio"
    LABELS strict
    TIMEOUT 60)

# 러너 자체의 회귀를 잡는 스모크 케이스. 러너는 사이클마다 총 바이트와 체크섬을
# 검사하고 어긋나면 0 이 아닌 코드로 끝나므로, "돌긴 도는데 엉뚱한 것을 재고
# 있다" 를 ctest 가 잡는다. 픽스처 디렉터리를 케이스마다 따로 두는 이유는
# ctest -j 로 병렬 실행될 때 같은 파일을 동시에 생성하지 않게 하기 위해서다.
add_test(NAME system_kernel_bench_smoke
    COMMAND system_kernel_bench --cycles=3 --out=/dev/null
            --warmup=1 --files=4 --bytes=65536
            --fixture-dir=${CMAKE_CURRENT_BINARY_DIR}/bench_fixture_single)
set_tests_properties(system_kernel_bench_smoke PROPERTIES TIMEOUT 60)

# 단일 스레드 경로만 밟으면 워커풀·코루틴·sync 를 통과하는 인자 처리가
# 검사되지 않는다. 조합 하나를 더 건다.
add_test(NAME system_kernel_bench_smoke_workers
    COMMAND system_kernel_bench --cycles=3 --out=/dev/null
            --warmup=1 --files=4 --bytes=65536
            --backend=pread --sync=futex --ctx=coroutine --workers=4
            --fixture-dir=${CMAKE_CURRENT_BINARY_DIR}/bench_fixture_workers)
set_tests_properties(system_kernel_bench_smoke_workers PROPERTIES TIMEOUT 60)

# 측정 채널이 살아 있는지. 러너가 "돌기만" 하고 카운터 배선이 끊어져 있으면
# 4채널 중 두 개가 통째로 0 이 되는데, CSV 만 보고는 알아채기 어렵다. mmap
# backend 는 사이클마다 새 매핑을 만들어 반드시 minor fault 를 내므로 그
# 성질로 getrusage → FrameTimer → CSV 배선을 못 박는다.
add_test(NAME system_kernel_bench_fault_channel
    COMMAND system_kernel_bench --cycles=5 --out=/dev/null
            --warmup=1 --files=4 --bytes=65536 --backend=mmap --min-faults=1
            --fixture-dir=${CMAKE_CURRENT_BINARY_DIR}/bench_fixture_faults)
set_tests_properties(system_kernel_bench_fault_channel PROPERTIES TIMEOUT 60)

# 잘못된 토글 이름을 조용히 기본값으로 흘려보내면 CSV 는 요청하지도 않은
# 조합의 수치를 그 이름으로 담는다. 거부가 계약이다.
add_test(NAME system_kernel_bench_rejects_bad_toggle
    COMMAND system_kernel_bench --backend=no_such_backend --out=/dev/null)
set_tests_properties(system_kernel_bench_rejects_bad_toggle PROPERTIES
    WILL_FAIL TRUE
    TIMEOUT 60)

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
    if(SKL_RT_LIB)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${SKL_RT_LIB})
    endif()
    foreach(skl_test IN LISTS SKL_HEADLESS_TARGETS)
        target_compile_definitions(${skl_test} PRIVATE SKL_HAS_POSIX_AIO=1)
        if(SKL_RT_LIB)
            target_link_libraries(${skl_test} PRIVATE ${SKL_RT_LIB})
        endif()
    endforeach()
    message(STATUS "POSIX AIO: enabled")
else()
    message(STATUS "POSIX AIO: disabled (headers/symbols unavailable)")
endif()

# io_uring 가드. liburing이 발견되면 SKL_HAS_LIBURING 정의 + 링크.
if(LIBURING_FOUND)
    target_compile_definitions(${PROJECT_NAME} PRIVATE SKL_HAS_LIBURING=1)
    target_link_libraries(${PROJECT_NAME} PRIVATE PkgConfig::LIBURING)
    foreach(skl_test IN LISTS SKL_HEADLESS_TARGETS)
        target_compile_definitions(${skl_test} PRIVATE SKL_HAS_LIBURING=1)
        target_link_libraries(${skl_test} PRIVATE PkgConfig::LIBURING)
    endforeach()
    message(STATUS "io_uring: enabled (liburing ${LIBURING_VERSION})")
else()
    message(STATUS "io_uring: disabled (liburing not found)")
endif()

target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-deprecated-declarations)  # ucontext_t (POSIX deprecated 표시) 사용
