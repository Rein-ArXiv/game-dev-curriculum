cmake_minimum_required(VERSION 3.20)
project(parts_perf_lab CXX)

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

# GUI 앱 타깃을 만들지 여부. OFF 로 두면 raylib/raygui FetchContent(네트워크·GLFW·OpenGL)를
# 아예 건너뛰고 parts_core_tests 만 구성한다. 계산 코어/문서 검증은 GUI 의존성 없이 돌린다.
option(PERF_LAB_BUILD_GUI
       "Build the raylib GUI application (fetches raylib/raygui)" ON)

# SIMD(AVX2) 활성화 옵션. SIMD step에서 켠다.
# 켜지 않은 채로 빌드해도 C++ Auto 경로는 정상 동작한다.
option(PERF_LAB_ENABLE_AVX2
       "Enable AVX2 compiler flags for SIMD modules" OFF)

# FMA(Fused Multiply-Add) 사용 여부. AVX2 경로에서 pos += vel*dt를 한 번 반올림으로
# 계산할지(fmadd), 곱셈·덧셈으로 나눌지 정한다. 폭(AVX2)과 FMA를 독립 축으로 두어
# 결과 차이를 폭 때문으로 오해하지 않게 한다. OFF면 -mfma를 넘기지 않는다.
option(PERF_LAB_ENABLE_FMA
       "Use FMA in the AVX2 SIMD path (requires FMA-capable CPU)" ON)

if(PERF_LAB_BUILD_GUI)
    # raylib을 FetchContent로 가져온다. 별도 시스템 설치 불필요.
    include(FetchContent)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
    set(SUPPORT_MODULE_RAUDIO OFF CACHE BOOL "" FORCE)  # 오디오 불필요(다만 raylib 5.0 은 CUSTOMIZE_BUILD=ON 일 때만 이 옵션을 읽으므로 raudio.c 는 그대로 빌드된다)

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
        src/sim/soa.cpp
        src/sim/aos.cpp
        src/sim/archetype.cpp
        # Draw()만 담은 렌더링 TU. 계산 코어와 분리되어 테스트 타깃에는 들어가지 않는다.
        src/sim/soa_draw.cpp
        src/sim/aos_draw.cpp
        src/sim/archetype_draw.cpp
        src/sim/factory.cpp
        src/measure/frame_timer.cpp
        src/measure/event_benchmark.cpp
        src/measure/container_benchmark.cpp
        src/ui/raygui_impl.cpp
        src/ui/hud.cpp
        src/alloc/malloc_allocator.cpp
        src/alloc/arena_allocator.cpp
        src/alloc/pool_allocator.cpp
        src/alloc/slab_allocator.cpp
    )
endif()

enable_testing()
# headless 계산 코어 테스트. *_draw.cpp 를 빼면 raylib 심볼이 하나도 남지 않으므로
# 이 타깃은 raylib/GLFW/OpenGL 을 링크하지 않고 빌드된다. GUI OFF 구성에서도 항상 만든다.
add_executable(parts_core_tests
    tests/core_tests.cpp
    tests/headless_draw.cpp
    src/sim/soa.cpp
    src/sim/aos.cpp
    src/sim/archetype.cpp
    src/sim/factory.cpp
    src/measure/container_benchmark.cpp
    src/measure/event_benchmark.cpp
    src/measure/frame_timer.cpp
    src/alloc/malloc_allocator.cpp
    src/alloc/arena_allocator.cpp
    src/alloc/pool_allocator.cpp
    src/alloc/slab_allocator.cpp
)
target_include_directories(parts_core_tests PRIVATE src)
# 전체 실행. 한 번에 다 돌리고 싶을 때의 진입점이다.
add_test(NAME parts_core_tests COMMAND parts_core_tests)

# 그룹별 실행 단위. 같은 실행 파일에 그룹 이름을 넘기면 그 그룹만 돈다.
# 이름은 그 검사를 *추가한* step 의 주제를 딴다 — ctest 이름 하나가 문서 한 편에
# 대응하므로, 어느 step 을 따라가다 깨졌는지가 ctest 출력에서 바로 보인다.
# snippet-begin: group-tests
function(parts_add_group_test name group)
    add_test(NAME ${name} COMMAND parts_core_tests ${group})
endfunction()

parts_add_group_test(parts_skeleton_tests  skeleton)
parts_add_group_test(parts_toggles_tests   toggles)
parts_add_group_test(parts_layout_tests    layout)
parts_add_group_test(parts_simd_tests      simd)
parts_add_group_test(parts_branch_tests    branch)
parts_add_group_test(parts_allocator_tests allocator)
parts_add_group_test(parts_container_tests container)
parts_add_group_test(parts_graph_tests     graph)
# snippet-end: group-tests

set(PERF_LAB_TARGET_X86 OFF)
if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "^(X86|x86|x64|X64)$"
   OR CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i[3-6]86)$")
    set(PERF_LAB_TARGET_X86 ON)
endif()

# 앱 타깃에만 붙는 GUI 의존성과 플랫폼 매크로.
if(PERF_LAB_BUILD_GUI)
    target_include_directories(${PROJECT_NAME} PRIVATE
        src
        ${raygui_SOURCE_DIR}/src)
    target_link_libraries(${PROJECT_NAME} PRIVATE raylib)

    # 플랫폼 분기. OS별 실험 코드를 추가할 때 이 매크로로 분기한다.
    if(UNIX AND NOT APPLE)
        target_compile_definitions(${PROJECT_NAME} PRIVATE PERF_LAB_LINUX=1)
    elseif(WIN32)
        target_compile_definitions(${PROJECT_NAME} PRIVATE PERF_LAB_WINDOWS=1)
    elseif(APPLE)
        target_compile_definitions(${PROJECT_NAME} PRIVATE PERF_LAB_MACOS=1)
    endif()
endif()

# 컴파일러 경고
if(MSVC)
    target_compile_options(parts_core_tests PRIVATE /W4 /permissive- /utf-8)
    if(PERF_LAB_BUILD_GUI)
        target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive- /utf-8)
    endif()
else()
    # -ffp-contract=off: 일반 C++(C++ Auto) 경로에서 컴파일러가 곱셈·덧셈을 FMA로
    # 합치는 것을 막는다. 그래야 "C++ Auto"와 "SSE" 경로가 같은 반올림 계약을 쓴다.
    # AVX2 경로의 FMA는 인트린식으로 명시하므로 이 옵션과 별개다.
    target_compile_options(parts_core_tests PRIVATE
        -Wall -Wextra -Wpedantic -ffp-contract=off)
    if(PERF_LAB_BUILD_GUI)
        target_compile_options(${PROJECT_NAME} PRIVATE
            -Wall -Wextra -Wpedantic -ffp-contract=off)
    endif()
endif()

if(PERF_LAB_ENABLE_AVX2)
    if(PERF_LAB_TARGET_X86)
        # 앱 타깃과 테스트 타깃에 동일하게 부여한다. 테스트에 주지 않으면
        # UpdateAVX2 가 `#if defined(PERF_LAB_AVX2)` 가드 때문에 테스트 바이너리에서
        # 아예 컴파일되지 않아 AVX2 경로가 무검증으로 남는다.
        target_compile_definitions(parts_core_tests PRIVATE PERF_LAB_AVX2=1)
        if(PERF_LAB_BUILD_GUI)
            target_compile_definitions(${PROJECT_NAME} PRIVATE PERF_LAB_AVX2=1)
        endif()
        if(MSVC)
            target_compile_options(parts_core_tests PRIVATE /arch:AVX2)
            if(PERF_LAB_BUILD_GUI)
                target_compile_options(${PROJECT_NAME} PRIVATE /arch:AVX2)
            endif()
            if(PERF_LAB_ENABLE_FMA)
                target_compile_definitions(parts_core_tests PRIVATE PERF_LAB_FMA=1)
                if(PERF_LAB_BUILD_GUI)
                    target_compile_definitions(${PROJECT_NAME} PRIVATE PERF_LAB_FMA=1)
                endif()
            endif()
        else()
            target_compile_options(parts_core_tests PRIVATE -mavx2)
            if(PERF_LAB_BUILD_GUI)
                target_compile_options(${PROJECT_NAME} PRIVATE -mavx2)
            endif()
            if(PERF_LAB_ENABLE_FMA)
                target_compile_options(parts_core_tests PRIVATE -mfma)
                target_compile_definitions(parts_core_tests PRIVATE PERF_LAB_FMA=1)
                if(PERF_LAB_BUILD_GUI)
                    target_compile_options(${PROJECT_NAME} PRIVATE -mfma)
                    target_compile_definitions(${PROJECT_NAME} PRIVATE PERF_LAB_FMA=1)
                endif()
            endif()
        endif()
    else()
        message(WARNING
            "PERF_LAB_ENABLE_AVX2 is ON, but the target is not x86/x64. "
            "AVX2 code is disabled and runtime SIMD selections fall back.")
    endif()
endif()

# Release 빌드는 LTO와 강한 최적화. Debug는 기호 보존.
if(PERF_LAB_BUILD_GUI)
    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE
            $<$<CONFIG:Release>:/O2>
            $<$<CONFIG:Release>:/Oi>
            $<$<CONFIG:Release>:/GL>)
        target_link_options(${PROJECT_NAME} PRIVATE
            $<$<CONFIG:Release>:/LTCG>)
    else()
        target_compile_options(${PROJECT_NAME} PRIVATE
            $<$<CONFIG:Release>:-O3>
            $<$<CONFIG:Release>:-fno-omit-frame-pointer>)
        # -flto 는 환경 따라 켠다. 빌드 시간 영향 큼.
    endif()
endif()
