cmake_minimum_required(VERSION 3.16)
project(mega CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)

if(MSVC)
    add_compile_options(/permissive- /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra)
    find_package(Threads REQUIRED)
endif()

enable_testing()

# ══ 계층 규칙 검사(재설계안 A3) ══════════════════════════════════════
#
#   games/ 는 OS API(소켓·창·파일·오디오·GPU·신호)를 직접 부를 수 없다.
#   그 권한은 net/ · platform/ · io/ 에만 있다. 게임 진입점은 150줄 이하.
#
# 감사가 찾은 결함 — main.cpp 익명 네임스페이스의 ReplicaBuffer 재구현 — 은
# main.cpp 가 <winsock2.h> 와 <windows.h> 를 손에 쥘 수 있었기 때문에 가능했다.
# 원재료가 닿는 곳에 있으면 누구든 그 자리에서 다시 만든다. 인클루드를 막으면
# 그 물리적 경로 자체가 사라진다.
#
# 규칙 자체는 scripts/check_layers.py 하나가 소유한다(저장소의 다른 검사기와
# 같은 형식: `LEVEL<TAB>경로:줄<TAB>메시지` + 집계 줄, ERROR 가 있으면 종료
# 코드 1). 검사기를 CMake 로 쓰지 않는 이유는 정확성이다 — file(STRINGS) 의
# 결과를 foreach 로 순회하면 빈 줄이 사라져 위반의 줄 번호가 앞의 빈 줄 수만큼
# 밀린다. 위반 위치를 틀리게 알려 주는 게이트는 무시당한다.
#
# 세 번 돈다: 구성 시(즉시 실패) + 빌드마다(mega_layer_check) + ctest.
# 빌드마다 도는 쪽이 본체다 — 구성 시에만 돌면 규칙을 어긴 편집이 재구성
# 전까지 통과한다.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_FOUND)
    message(FATAL_ERROR
        "계층 규칙 검사기(scripts/check_layers.py)를 돌릴 Python 3 인터프리터가 없다.\n"
        "이 검사는 끌 수 있는 옵션이 아니다 — 끌 수 있는 게이트는 게이트가 아니다.\n"
        "Python 3 를 설치하거나 -DPython3_EXECUTABLE=<경로> 로 지정하라.")
endif()

set(MEGA_GAMES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/games)
set(MEGA_ENTRY_LINE_LIMIT 150)
# -X utf8 을 붙이지 않는다. 붙이면 cp949 콘솔의 빌드 로그에서 위반 메시지가
# 깨져 읽을 수 없다 — 무엇을 어겼는지 못 읽는 게이트는 무시당한다.
set(MEGA_LAYER_CHECK_COMMAND
    ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_layers.py
    --games-dir ${MEGA_GAMES_DIR}
    --entry-line-limit ${MEGA_ENTRY_LINE_LIMIT})

execute_process(COMMAND ${MEGA_LAYER_CHECK_COMMAND}
                RESULT_VARIABLE MEGA_LAYER_CHECK_RESULT
                OUTPUT_VARIABLE MEGA_LAYER_CHECK_OUTPUT
                ERROR_VARIABLE MEGA_LAYER_CHECK_OUTPUT)
if(NOT MEGA_LAYER_CHECK_RESULT EQUAL 0)
    message(FATAL_ERROR "계층 규칙 위반(재설계안 A3):\n${MEGA_LAYER_CHECK_OUTPUT}")
endif()
message(STATUS "계층 규칙 OK: games/ 에 OS API 직접 호출 없음, 진입점 ≤ ${MEGA_ENTRY_LINE_LIMIT}줄")

add_custom_target(mega_layer_check ALL
    COMMAND ${MEGA_LAYER_CHECK_COMMAND}
    COMMENT "계층 규칙 검사: games/ 에 OS API 직접 호출이 없는가"
    VERBATIM)
add_test(NAME layer_rules COMMAND ${MEGA_LAYER_CHECK_COMMAND})

# ══ 도달성 게이트 준비(재설계안 A2) ══════════════════════════════════
#
#   include/ 아래 모든 헤더는 제품에서 도달 가능하거나 docs/LAB_ONLY.md 에
#   이유와 함께 등재돼야 한다. 제3의 상태는 빌드 실패다.
#
# 게이트 본체는 scripts/reachability.py 이고, 입력은 제품 타깃이 컴파일될 때
# 컴파일러가 직접 남긴 의존성 출력이다. 여기서는 그 출력을 켜는 일만 한다.
#
# Python 은 위 계층 규칙 검사가 이미 필수로 요구한다. 이 게이트도 같은 인터프리터를
# 쓴다 — 선택 의존으로 두면 없는 머신에서 게이트가 조용히 꺼지고, 그 순간 감사가
# 찾은 "도달 불가인데 근거도 없는" 제3의 상태가 다시 합법이 된다.
set(MEGA_REACHABILITY_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/scripts/reachability.py)
set(MEGA_REACHABILITY_DIR ${CMAKE_CURRENT_BINARY_DIR}/reachability)
set(MEGA_LAB_ONLY_DOC ${CMAKE_CURRENT_SOURCE_DIR}/docs/LAB_ONLY.md)
set(MEGA_REACHABILITY_REPORT ${CMAKE_CURRENT_SOURCE_DIR}/docs/PRODUCT_LAYERS.md)

# 의존성 출력을 담을 디렉터리는 컴파일 전에 있어야 한다(컴파일러가 만들지 않는다).
if(CMAKE_CONFIGURATION_TYPES)
    foreach(cfg ${CMAKE_CONFIGURATION_TYPES})
        file(MAKE_DIRECTORY ${MEGA_REACHABILITY_DIR}/${cfg})
    endforeach()
else()
    file(MAKE_DIRECTORY ${MEGA_REACHABILITY_DIR}/${CMAKE_BUILD_TYPE})
endif()

# 제품 타깃 하나가 자기 진입점을 컴파일하면서 "실제로 연 파일 목록"을 남기게 한다.
# 텍스트로 #include 를 훑는 근사와 달리 전처리기 조건부(#if)가 정확히 반영된다.
define_property(GLOBAL PROPERTY MEGA_PRODUCT_DEPFILES
                BRIEF_DOCS "제품 타깃의 의존성 출력 목록"
                FULL_DOCS "TARGET=경로 형식. 도달성 게이트의 유일한 입력이다.")
function(mega_capture_dependencies target)
    if(MSVC)
        set(depfile ${MEGA_REACHABILITY_DIR}/$<CONFIG>/${target}.json)
        target_compile_options(${target} PRIVATE
                               "SHELL:/sourceDependencies ${depfile}")
    else()
        set(depfile ${MEGA_REACHABILITY_DIR}/$<CONFIG>/${target}.d)
        target_compile_options(${target} PRIVATE -MD -MF ${depfile})
    endif()
    set_property(GLOBAL APPEND PROPERTY MEGA_PRODUCT_DEPFILES
                 "${target}=${depfile}")
endfunction()

# ══ Vulkan(선택) ═════════════════════════════════════════════════════
# mega_core 가 표시 백엔드(platform/vk_present.hpp)를 갖게 되었으므로 탐지가
# 먼저 와야 한다. 없으면 그 헤더는 통째로 비활성이고 소프트웨어 표시만 남는다.
find_package(Vulkan QUIET)

# ══ mega_core — include/ 는 헤더 더미가 아니라 실제 정적 라이브러리 타깃이다 ══
#
# 재설계안 A1: "제품 실행 파일과 모든 테스트가 같은 타깃을 링크한다."
# 감사가 찾은 진짜 결함은 테스트와 제품이 서로 다른 코드를 본다는 것이었다.
# 여기서는 인클루드 경로조차 mega_core 를 통해서만 전파되므로(PUBLIC),
# 이 타깃을 링크하지 않는 TU 는 엔진 헤더를 볼 수조차 없다.
#
# include/ 103개 파일 중 실제 구현 .cpp 는 넷뿐이고 나머지는 헤더온리다.
# 헤더온리를 그대로 두면 "아무도 인클루드하지 않는 헤더는 컴파일조차 되지
# 않는" 상태가 남는다 — 감사가 지적한 64개 미도달 헤더가 정확히 그 상태였다.
# 그래서 헤더 하나당 그 헤더만 인클루드하는 번역 단위를 생성해 mega_core 안에서
# 전부 컴파일한다. 헤더를 추가하면 CONFIGURE_DEPENDS 가 재구성을 걸어 TU 가
# 자동으로 생기므로, "컴파일된 적 없는 헤더"는 구조적으로 존재할 수 없다.
#
# 주의(도달성 게이트 P0-2 를 위한 구분): 이 생성 TU 들은 "컴파일된다"만 보장하지
# "제품에서 도달 가능하다"를 보장하지 않는다. 도달성은 제품 타깃(mega_defense ·
# mega_defense_server)의 의존성 출력에서 계산해야 하며, 그 타깃들은 각각
# main.cpp / server_main.cpp 단 하나만 컴파일하므로 그 계산이 그대로 성립한다.

set(MEGA_CORE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

file(GLOB_RECURSE MEGA_CORE_HEADERS CONFIGURE_DEPENDS
     ${MEGA_CORE_INCLUDE_DIR}/*.h
     ${MEGA_CORE_INCLUDE_DIR}/*.hpp)
file(GLOB_RECURSE MEGA_CORE_IMPL_SOURCES CONFIGURE_DEPENDS
     ${MEGA_CORE_INCLUDE_DIR}/*.cpp)
list(SORT MEGA_CORE_HEADERS)
list(SORT MEGA_CORE_IMPL_SOURCES)

if(NOT MEGA_CORE_HEADERS)
    message(FATAL_ERROR "include/ 아래에서 헤더를 하나도 찾지 못했다 — 글롭이 깨졌다")
endif()

# 헤더온리 파일을 실제 TU 로 승격시키는 생성기.
set(MEGA_CORE_UNIT_DIR ${CMAKE_CURRENT_BINARY_DIR}/mega_core_units)
file(MAKE_DIRECTORY ${MEGA_CORE_UNIT_DIR})
set(MEGA_CORE_UNITS "")
foreach(header ${MEGA_CORE_HEADERS})
    file(RELATIVE_PATH rel ${MEGA_CORE_INCLUDE_DIR} ${header})
    string(REGEX REPLACE "[^A-Za-z0-9]" "_" stem "${rel}")
    set(unit ${MEGA_CORE_UNIT_DIR}/unit_${stem}.cpp)
    set(unit_text "// 생성 파일 — 직접 수정하지 마라. CMakeLists.txt 가 include/${rel} 하나당 하나를 만든다.\n//\n// 왜 존재하는가: 헤더온리 파일은 아무도 인클루드하지 않으면 컴파일조차 되지 않는다.\n// 그 상태에서는 '도달 불가'와 '깨진 코드'를 구분할 수 없어 도달성 게이트가 무의미해진다.\n// 이 TU 는 헤더가 (1) 자족적으로 컴파일되고 (2) 인클루드 가드가 멱등임을 강제한다.\n#include \"${rel}\"\n#include \"${rel}\"\n")
    set(unit_old "")
    if(EXISTS ${unit})
        file(READ ${unit} unit_old)
    endif()
    if(NOT unit_old STREQUAL unit_text)
        file(WRITE ${unit} "${unit_text}")
    endif()
    list(APPEND MEGA_CORE_UNITS ${unit})
endforeach()

# 생성 TU 를 정리하다 남은 고아 파일(헤더가 삭제·이름 변경된 경우)을 제거한다.
file(GLOB MEGA_CORE_STALE_UNITS ${MEGA_CORE_UNIT_DIR}/unit_*.cpp)
foreach(stale ${MEGA_CORE_STALE_UNITS})
    if(NOT stale IN_LIST MEGA_CORE_UNITS)
        file(REMOVE ${stale})
    endif()
endforeach()

add_library(mega_core STATIC ${MEGA_CORE_IMPL_SOURCES} ${MEGA_CORE_UNITS})
# 헤더를 소스로 등록해 IDE 와 의존성 추적에서 1급으로 보이게 한다.
target_sources(mega_core PRIVATE ${MEGA_CORE_HEADERS})
target_include_directories(mega_core PUBLIC ${MEGA_CORE_INCLUDE_DIR})
source_group(TREE ${MEGA_CORE_INCLUDE_DIR} PREFIX include FILES ${MEGA_CORE_HEADERS})
source_group("generated units" FILES ${MEGA_CORE_UNITS})

# mega_core 의 외부 의존은 OS 가 제공하는 것뿐이다(서드파티 없음).
#   Synchronization — sync/futex.h 의 WaitOnAddress/WakeByAddress
#   ws2_32          — net/udp_*.hpp 의 Winsock
#   user32 · gdi32  — platform/window.hpp 의 창 생성과 GDI blit
#   winmm           — platform/audio_device.hpp 의 waveOut
#   Threads         — jobs/scheduler.h 의 std::thread (POSIX pthread)
# 소비자가 그대로 물려받아야 하므로 PUBLIC.
#
# user32/gdi32/winmm 은 P0-2 까지 mega_defense 에만 붙어 있었다. 그때는 창·오디오
# 코드가 games/defense/ 에 있었기 때문이다. 이제 platform/ 이 소유하므로 그
# 의존도 mega_core 로 따라 올라온다 — 계층이 바뀌면 링크도 같이 바뀌어야 한다.
if(WIN32)
    target_link_libraries(mega_core PUBLIC Synchronization ws2_32 user32 gdi32 winmm)
endif()
if(TARGET Threads::Threads)
    target_link_libraries(mega_core PUBLIC Threads::Threads)
endif()

# Vulkan 표시 백엔드도 mega_core 안에 있다(platform/vk_present.hpp). 정의와
# 링크가 PUBLIC 이어야 제품·테스트가 같은 조건으로 같은 코드를 본다 — 제품에만
# 정의를 주면 그 헤더는 다시 "제품만 보는 코드"가 된다.
if(Vulkan_FOUND)
    target_compile_definitions(mega_core PUBLIC MEGA_HAS_VULKAN=1)
    target_link_libraries(mega_core PUBLIC Vulkan::Vulkan)
endif()

add_dependencies(mega_core mega_layer_check)

list(LENGTH MEGA_CORE_HEADERS MEGA_CORE_HEADER_COUNT)
list(LENGTH MEGA_CORE_IMPL_SOURCES MEGA_CORE_IMPL_COUNT)
message(STATUS
    "mega_core: 헤더 ${MEGA_CORE_HEADER_COUNT}개(생성 TU 동수) + 구현 ${MEGA_CORE_IMPL_COUNT}개")

# ── 엔진·게임 단위 및 통합 테스트 ───────────────────────────────────
# 모든 테스트는 제품과 동일한 mega_core 를 링크한다. 테스트가 엔진 .cpp 를
# 개별로 나열하거나 인클루드 경로를 따로 여는 방식은 없앴다 — 그것이 분기의 원인이다.
#
# games/defense 를 인클루드 루트로 열어 주던 줄을 제거했다(P0-3). 그 줄이 있는
# 한 "게임 폴더 안에만 있는 코드를 테스트가 집어 드는" 상태가 계속 가능했다.
# 콘텐츠 로더·진행도·HUD·프레임 루프는 전부 mega_core(include/) 로 올라갔다.
file(GLOB ENGINE_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp)
foreach(t ${ENGINE_TESTS})
    get_filename_component(n ${t} NAME_WE)
    add_executable(${n} ${t})
    target_include_directories(${n} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(${n} PRIVATE mega_core)
    add_test(NAME ${n} COMMAND ${n})
endforeach()
set_tests_properties(test_defense_process PROPERTIES TIMEOUT 15)
set_tests_properties(test_defense_soak PROPERTIES TIMEOUT 30)
# 아래 둘은 "끝나지 않는 스크립트"를 실제로 인터프리터에 먹인다(tools::Script 의
# 전역 실행 예산 · 중첩 깊이 · 파서 전진 보장). 그 방어가 무너지면 테스트는
# 실패하는 게 아니라 **끝나지 않는다** — 그 상태를 실패로 바꿔 주는 것은
# 타임아웃뿐이다. 방어가 살아 있으면 둘 다 1초 안에 끝난다.
set_tests_properties(test_tools_script PROPERTIES TIMEOUT 30)
set_tests_properties(test_defense_content PROPERTIES TIMEOUT 30)
# 제품 실행 파일 둘을 띄우고 협동 왕복을 단언한다. 스폰·핸드셰이크·승리까지
# 도는 시간을 넉넉히 잡되, 무한정 매달리지는 않게 한다.
set_tests_properties(test_defense_coop_process PROPERTIES TIMEOUT 180)

# ── 최종 제품: 하나의 협동 디펜스 실행 파일 ────────────────────────
# 진입점 하나만 컴파일한다. 게임 로직·프레임 루프·창·오디오는 전부 mega_core
# 안에 있고(game/defense_app.hpp), 여기 남은 것은 인자 파싱과 RunDefense 호출뿐이다.
# 인클루드 루트로 games/defense 를 열지 않는다 — 열 것이 남아 있지 않다.
add_executable(mega_defense games/defense/main.cpp)
target_link_libraries(mega_defense PRIVATE mega_core)
mega_capture_dependencies(mega_defense)
add_custom_command(TARGET mega_defense POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/games/defense/assets
            $<TARGET_FILE_DIR:mega_defense>/assets)
# --selftest 는 이제 조기 반환하지 않는다. 창만 없을 뿐 제품의 진짜 프레임 루프를
# N틱 구동한다 — 에셋 로드 · 명령 경로 · 고정 스텝 시뮬 · 소프트웨어 렌더 ·
# 진행도 커밋이 전부 실제로 실행되고, 그 결과를 프로세스 종료 코드로 단언한다.
add_test(NAME mega_defense_selftest COMMAND mega_defense --selftest)
set_tests_properties(mega_defense_selftest PROPERTIES TIMEOUT 120)

# GPU 표시 경로도 ctest 대상이다. 감사 #7·#24 는 README·docs 가 "Vulkan 스왑체인
# 5프레임 present 를 자동 검증한다"고 적으면서 ctest 에도 CI 에도 그 항목이 없는
# 상태를 지적했다. 여기서 등록해 그 문장을 사실로 만든다.
#
# `if(Vulkan_FOUND)` 안에 넣지 않는다. 그러면 테스트 목록 자체가 머신마다 달라져
# "ctest N개"라는 서술이 어느 머신에서는 틀리고, 무엇보다 GPU 가 없다는 사실이
# 결과에 아무 흔적도 남기지 않는다. 대신 항상 등록하고, 실행할 수 없는 환경에서는
# 실행 파일이 kDefenseSkipExitCode(77) 로 끝나 ctest 가 이 검사를 SKIP 으로 센다.
# 실패는 오직 "장치도 창도 있는데 present 가 깨졌을 때"뿐이다.
add_test(NAME mega_defense_gpu_selftest COMMAND mega_defense --gpu-selftest)
set_tests_properties(mega_defense_gpu_selftest PROPERTIES
                     TIMEOUT 120
                     SKIP_RETURN_CODE 77)

# 도달성 게이트는 "헤더가 제품에서 도달 가능한가"만 강제한다. 도달 가능한 코드가
# 실제로 **실행되는가**는 묻지 않으므로, 배선 결함은 무증상으로 통과할 수 있다.
# 그 두 번째 게이트가 아래 둘이다.
#
#   test_defense_client_loop   제품 DefenseApp 을 Client 로 구성해 진짜 소켓 위에서
#                              Step() 을 돈다. PumpClient 를 부르는 첫 테스트다.
#   test_defense_coop_process  호스트도 클라이언트도 mega_defense 실행 파일이다.
#                              main.cpp → RunDefense → 클라이언트 루프까지 이어지는
#                              배선 전체를 프로세스 경계 밖에서 확인한다.
#
# 후자는 mega_defense 실행 파일과 그 옆의 assets 를 실제로 실행하므로 빌드 의존이
# 필요하다. 전자도 같은 assets 를 읽는다(없어도 폴백으로 돌지만, 두 테스트가 같은
# 콘텐츠를 보게 해 두는 편이 진단이 쉽다).
add_dependencies(test_defense_coop_process mega_defense)
add_dependencies(test_defense_client_loop mega_defense)

# Headless authoritative process. It reuses the same game rules and wire
# format, but owns a multi-endpoint UDP socket and validates live-service
# admission tokens before accepting player commands.
add_executable(mega_defense_server games/defense/server_main.cpp)
target_link_libraries(mega_defense_server PRIVATE mega_core)
mega_capture_dependencies(mega_defense_server)
add_custom_command(TARGET mega_defense_server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/games/defense/assets
            $<TARGET_FILE_DIR:mega_defense_server>/assets)

# The final executable can present the same framebuffer through Vulkan. This is
# a backend choice, not a second game or a second source snapshot.
# 정의·링크는 mega_core 가 PUBLIC 으로 갖는다(위). 여기 남은 것은 셰이더 컴파일뿐.
if(Vulkan_FOUND)
    if(DEFINED Vulkan_GLSLC_EXECUTABLE AND EXISTS "${Vulkan_GLSLC_EXECUTABLE}")
        add_custom_command(TARGET mega_defense POST_BUILD
            COMMAND "${Vulkan_GLSLC_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/games/defense/shaders/fullscreen.vert"
                    -o "$<TARGET_FILE_DIR:mega_defense>/fullscreen.vert.spv"
            COMMAND "${Vulkan_GLSLC_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/games/defense/shaders/blit.frag"
                    -o "$<TARGET_FILE_DIR:mega_defense>/blit.frag.spv")
        message(STATUS "mega_defense Vulkan presenter enabled")
    else()
        message(WARNING "Vulkan found but glslc is missing; --vulkan needs precompiled shaders")
    endif()
endif()

# ══ 도달성 게이트 본체(재설계안 A2) ══════════════════════════════════
#
# 제품 타깃이 컴파일된 뒤에 돈다. 입력은 그 컴파일이 남긴 의존성 출력뿐이므로
# 게이트가 보는 도달 집합과 실행 파일이 실제로 링크하는 코드가 어긋날 수 없다.
#
# 왜 mega_core 가 아니라 제품 타깃인가. mega_core 는 헤더 하나당 생성 TU 를 만들어
# include/ 전체를 컴파일한다(위 [A1]). 거기서 재면 "전부 도달 가능"이라는 거짓
# green 이 나온다. 제품 타깃은 진입점 TU 하나씩만 컴파일하므로 그 둘의 의존성
# 출력이 곧 제품 도달 집합이다.
#
# 같은 실행을 빌드(mega_reachability)와 ctest(reachability_gate) 양쪽에 건다.
# 빌드 쪽이 본체다 — 위반을 커밋 전에 잡으려면 ctest 를 기다려서는 늦다.
get_property(MEGA_PRODUCT_DEPFILES GLOBAL PROPERTY MEGA_PRODUCT_DEPFILES)
if(NOT MEGA_PRODUCT_DEPFILES)
    message(FATAL_ERROR
        "도달성 게이트: 의존성 출력을 남기는 제품 타깃이 하나도 없다. "
        "제품 없이 계산한 도달성은 아무 의미가 없으므로 통과시키지 않는다.")
endif()

set(MEGA_REACHABILITY_ARGS "")
foreach(entry ${MEGA_PRODUCT_DEPFILES})
    list(APPEND MEGA_REACHABILITY_ARGS --dep ${entry})
endforeach()

# --readme-tree 는 같은 입력(include/ 하위 구조)을 쓰는 곁다리 대조다. README 의
# 코드 구조 트리에 실재 디렉터리가 빠지거나(감사 #27 의 serialize/ 등 6개), 없는
# 디렉터리가 남아 있으면 빌드가 실패한다. 손으로 맞춰 둔 트리는 다음 디렉터리가
# 생기는 순간 다시 어긋난다 — 어긋날 수 없게 만드는 것은 대조뿐이다.
set(MEGA_REACHABILITY_COMMAND
    ${Python3_EXECUTABLE} -X utf8 ${MEGA_REACHABILITY_SCRIPT}
    --include-dir ${MEGA_CORE_INCLUDE_DIR}
    --lab-only ${MEGA_LAB_ONLY_DOC}
    --report ${MEGA_REACHABILITY_REPORT}
    --json ${MEGA_REACHABILITY_DIR}/$<CONFIG>/reachability.json
    --readme-tree ${CMAKE_CURRENT_SOURCE_DIR}/README.md
    ${MEGA_REACHABILITY_ARGS})

add_custom_target(mega_reachability ALL
    COMMAND ${MEGA_REACHABILITY_COMMAND}
    COMMENT "도달성 게이트: include/ 의 모든 헤더가 제품 도달 또는 LAB_ONLY 등재인가"
    VERBATIM)
add_dependencies(mega_reachability mega_defense mega_defense_server)

add_test(NAME reachability_gate COMMAND ${MEGA_REACHABILITY_COMMAND})
