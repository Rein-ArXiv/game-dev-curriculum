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

# ══ 새니타이저(선택) ═════════════════════════════════════════════════
#
#   cmake -S . -B build_asan -DMEGA_SANITIZE=address
#   cmake --build build_asan --config Release
#   ctest --test-dir build_asan -C Release --output-on-failure
#
# 왜 별도 빌드 디렉터리인가. ASan 은 컴파일 옵션이자 런타임이다 — 같은 트리에서
# 켰다 껐다 하면 주석이 붙은 TU 와 안 붙은 TU 가 섞여 **없는 결함을 보고한다**.
# 옵션을 캐시 변수로 두고 디렉터리를 나누면 그 혼합이 구조적으로 불가능하다.
#
# 왜 기본값이 꺼짐인가. ASan 은 이 저장소의 3000틱 셀프테스트를 몇 배 느리게
# 만든다. 늘 켜 두면 개발 루프가 느려져 사람이 검사를 덜 돌리고, 그러면 켜 둔
# 이유가 사라진다. 켜는 것은 CI 와 "지금 이 결함을 쫓는 중" 두 자리다.
#
# MSVC 는 `/fsanitize=address` 하나만 지원한다(UBSan 이 없다). 그 사실을 조용히
# 무시하지 않고 구성 단계에서 실패시킨다 — `-DMEGA_SANITIZE=undefined` 를 주고
# green 을 받은 사람은 자기가 검사하지 않은 것을 검사했다고 믿게 된다.
# snippet-begin: sanitize-option
set(MEGA_SANITIZE "" CACHE STRING
    "address | undefined | address,undefined (빈 값 = 끔). MSVC 는 address 만.")
if(MEGA_SANITIZE)
    if(MSVC)
        if(NOT MEGA_SANITIZE STREQUAL "address")
            message(FATAL_ERROR
                "MSVC 가 지원하는 새니타이저는 address 하나다(요청: ${MEGA_SANITIZE}).\n"
                "UndefinedBehaviorSanitizer 가 필요하면 clang-cl 또는 GCC/Clang 으로 빌드하라.")
        endif()
        # /Zi 는 보고서에 파일·줄을 넣는다. 주소만 나오는 ASan 보고서는 읽는
        # 데 드는 비용이 커서 무시당하고, 무시당하는 진단은 없는 진단이다.
        add_compile_options(/fsanitize=address /Zi)
        # ASan 은 증분 링크와 함께 쓸 수 없다. CMake 의 MSVC 기본 Debug 구성은
        # /INCREMENTAL 을 켜므로 여기서 꺼 둔다.
        add_link_options(/INCREMENTAL:NO /DEBUG)
    else()
        add_compile_options(-fsanitize=${MEGA_SANITIZE} -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=${MEGA_SANITIZE})
    endif()
    add_compile_definitions(MEGA_SANITIZER_BUILD=1)
    message(STATUS "새니타이저 빌드: ${MEGA_SANITIZE}")
    # snippet-end: sanitize-option

    # MSVC 의 ASan 은 정적이 아니라 DLL 이다. 그 DLL 은 컴파일러 옆에 있고 보통
    # 개발자 프롬프트의 PATH 에만 있으므로, 평범한 셸이나 CTest 에서 실행하면
    # 모든 실행 파일이 0xC0000135(DLL not found)로 죽는다. **테스트가 하나도
    # 돌지 않았는데 ctest 는 전부 실패로 보고**하는 상태라, 원인을 찾는 데
    # 드는 시간이 통째로 낭비된다. 실행 파일 옆에 복사해 그 상태를 없앤다.
    # snippet-begin: asan-runtime
    if(MSVC)
        get_filename_component(MEGA_MSVC_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)
        find_file(MEGA_ASAN_RUNTIME_DLL
                  NAMES clang_rt.asan_dynamic-x86_64.dll clang_rt.asan_dynamic-i386.dll
                  HINTS ${MEGA_MSVC_BIN_DIR}
                  NO_DEFAULT_PATH)
        if(MEGA_ASAN_RUNTIME_DLL)
            file(COPY ${MEGA_ASAN_RUNTIME_DLL}
                 DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
            if(CMAKE_CONFIGURATION_TYPES)
                foreach(cfg ${CMAKE_CONFIGURATION_TYPES})
                    file(COPY ${MEGA_ASAN_RUNTIME_DLL}
                         DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${cfg})
                endforeach()
            endif()
            message(STATUS "ASan 런타임 복사: ${MEGA_ASAN_RUNTIME_DLL}")
        else()
            message(WARNING
                "ASan 런타임 DLL 을 ${MEGA_MSVC_BIN_DIR} 에서 찾지 못했다.\n"
                "실행 파일이 0xC0000135 로 죽으면 Visual Studio 의 Developer "
                "PowerShell 에서 돌리거나 그 DLL 을 PATH 에 넣어라.")
        endif()
    endif()
    # snippet-end: asan-runtime
endif()

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

# snippet-begin: layer-gate
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
# snippet-end: layer-gate

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
# snippet-begin: core-units
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
# snippet-end: core-units

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
# ── 셀프테스트의 시계를 못박는다 ────────────────────────────────────
#
# `assets/events.txt` 의 창은 주 단위로 **반복**한다. 그래서 시각을 못박지 않으면
# 셀프테스트의 승패가 그 판을 돌린 **요일**에 좌우된다. 가설이 아니라 실측이다 —
# 2026-08-23(일)에 `mega_defense_selftest_ai_astar` 가 `result=LOST` 로 죽었고,
# 원인은 그날 boss_rush 창(매주 일요일)이 열려 적 체력 x1.2 에 보스 3배가 걸린
# 것이었다. 코드는 그 주에 한 줄도 바뀌지 않았다.
#
# 달력에 좌우되는 검사는 검사가 아니다. 아래 두 상수가 그 의존을 끊는다.
#
#   QUIET  가장 이른 이벤트 시작 하루 전 — 활성 이벤트 0개. 이벤트가 없는 상태는
#          결함이 아니라 정상 경로이므로 이것이 기준선이다.
#   EVENT  boss_rush 창의 시작 — weekend_scrap 과 겹쳐 scrap x2 · reward x1.5 ·
#          enemy_hp x1.2 · 보스 3배가 **동시에** 걸린다.
#
# 전부 QUIET 로 밀어 두면 이벤트가 걸린 판은 제품 안에서 한 번도 돌지 않는다.
# 그래서 아래 `mega_defense_selftest_event` 하나를 EVENT 로 못박아 따로 세운다 —
# 달력이 주던 커버리지를 우연이 아니라 **의도**로 되돌리는 자리다. 두 시각의
# 틱 수가 실제로 갈린다(1377 대 1488): 이벤트가 시뮬을 바꾼다는 증거다.
set(MEGA_SELFTEST_UNIX_QUIET 1767312000)   # 2026-01-02 00:00 UTC
set(MEGA_SELFTEST_UNIX_EVENT 1767484800)   # 2026-01-04 00:00 UTC

# ── 셀프테스트의 프로필도 못박는다 ──────────────────────────────────
#
# 시계와 **같은 종류의 결함**이 하나 더 있었다. `--selftest` 는 진행도를 실제로
# 커밋한다(그것이 이 검사의 값이다). 경로를 주지 않으면 전부 실행 파일 옆의
# `selftest_profile.wal` 하나를 쓰고, 그 파일은 지워지지 않으므로 **누적한다.**
#
# 누적한 지갑은 다음 판의 시작 골드를 바꾼다. 실측(2026-08-23,
# `--selftest 25000 --buy-tier 5 --match-unix QUIET` 를 냉각 프로필에서 두 번):
#
#   1회차  [meta] scrap=0 tier=0 start_gold=420+0
#          [selftest] result=CLEAR ticks=1377 towers=10 wave=6 packet=181
#   2회차  [meta] buy start_gold_tier=1 cost=200 balance=8
#          [selftest] result=CLEAR ticks=1377 towers=11 wave=6 packet=192
#
# 코드는 한 줄도 바뀌지 않았고, 명령도 같다. 달라진 것은 **몇 번째 실행이냐**뿐이다.
# 그 상태에서는 아티클이 인용하는 수치가 독자에게 재현되지 않고, 값을 단언하는
# 검사를 세우는 순간 그것은 플래키가 된다.
#
# 아래 두 줄이 그 의존을 끊는다. 검사마다 **자기 파일**을 잡고(-j 병렬에서 서로
# 밟지 않는다), 매 실행 그 파일을 비우고 출발한다(같은 명령 = 같은 결과).
# 누적 자체는 제품의 기능이므로 지우지 않는다 — 그것을 **의도로** 검증하는
# 자리가 아래 `mega_defense_selftest_meta` 짝과 `test_profile_isolation` 이다.
set(MEGA_PROFILE_DIR ${CMAKE_CURRENT_BINARY_DIR}/profiles)
file(MAKE_DIRECTORY ${MEGA_PROFILE_DIR})

# 셀프테스트 한 줄을 세우는 유일한 자리. 프로필 격리를 함수 안에 두는 이유는
# 잊을 수 없게 하려는 것이다 — 검사를 하나 더 붙이는 사람이 --profile 을
# 빼먹으면 그 검사만 조용히 공용 프로필로 돌아간다.
# snippet-begin: selftest-profile
function(mega_add_selftest name ticks matchUnix timeout)
    add_test(NAME ${name}
             COMMAND mega_defense --selftest ${ticks} --match-unix ${matchUnix}
                     --profile ${MEGA_PROFILE_DIR}/${name}.wal --fresh-profile ${ARGN})
    set_tests_properties(${name} PROPERTIES TIMEOUT ${timeout})
endfunction()
# snippet-end: selftest-profile

# --selftest 는 이제 조기 반환하지 않는다. 창만 없을 뿐 제품의 진짜 프레임 루프를
# N틱 구동한다 — 에셋 로드 · 명령 경로 · 고정 스텝 시뮬 · 소프트웨어 렌더 ·
# 진행도 커밋이 전부 실제로 실행되고, 그 결과를 프로세스 종료 코드로 단언한다.
mega_add_selftest(mega_defense_selftest 18000 ${MEGA_SELFTEST_UNIX_QUIET} 120)

# 이벤트가 걸린 판. 위 전부가 QUIET 라 이 한 줄이 없으면 활성 이벤트가 콘텐츠
# 수치에 곱해지는 경로는 제품 실행 파일 안에서 한 번도 돌지 않는다.
# (`test_live_ops_wiring` 이 같은 배선을 라이브러리 경계 안에서 잰다 — 이쪽은
# 프로세스 경계 밖에서 같은 질문을 한 번 더 묻는다.)
mega_add_selftest(mega_defense_selftest_event 25000 ${MEGA_SELFTEST_UNIX_EVENT} 300)

# 같은 실행 파일, 다른 팔. G2 가 들여온 경로 — 절차 지형 · 기울인 카메라 ·
# 조준 색인 · BVH 정적 질의 · 격자 광역 탐색 · 잔해 물리 — 는 기본 팔에서 한 줄도
# 돌지 않는다. 이 검사가 없으면 그 전부가 "컴파일은 되는 코드"로 남는다.
# 스크립트는 배치·강화에 더해 발파와 철거까지 실제로 눌러, 지형이 매치 중에
# 바뀌고 잔해가 통행 격자를 막는 경로를 제품 안에서 통과시킨다.
mega_add_selftest(mega_defense_selftest_procgen 25000 ${MEGA_SELFTEST_UNIX_QUIET} 300
                  --ab map=procgen --ab camera=tilted3d --ab target_index=grid
                  --ab static_query=bvh --ab broadphase=grid)

# G3 가 들여온 팔들. archetype 저장 · 절차 보스 리그 · 기울인 카메라를 지나는
# 소리가 제품 안에서 한 판을 끝까지 도는지 본다. 기본 팔에서는 셋 다 한 줄도
# 돌지 않으므로, 이 검사가 없으면 전부 "컴파일은 되는 코드"로 남는다.
mega_add_selftest(mega_defense_selftest_rig 25000 ${MEGA_SELFTEST_UNIX_QUIET} 300
                  --ab entity_store=ecs --ab boss_render=rig --ab camera=tilted3d)

# G4 가 들여온 팔들. 통행 격자 · 플로우 필드 · 군집 조향 · 은신 인지 · 정예 행동
# 트리는 기본 팔(pathing=poly · steering=0 · stealth=0 · elite_brain=plain)에서
# 한 줄도 돌지 않는다. 절차 맵을 함께 켜는 것은 선택이 아니다 — 저작된 무분기
# 회랑에는 탐색할 분기도, 시야를 막을 내부 벽도 없어 경로와 인지가 표현할 것을
# 갖지 못한다.
mega_add_selftest(mega_defense_selftest_ai 25000 ${MEGA_SELFTEST_UNIX_QUIET} 300
                  --ab map=procgen --ab pathing=flowfield --ab steering=1
                  --ab stealth=1 --ab elite_brain=bt)

# 같은 축의 **다른 팔**. 위 검사가 도는 것은 필드 팔과 트리 팔뿐이라, A* 경로와
# 상태 기계 정예는 제품 안에서 한 줄도 돌지 않는다. 두 팔을 다 등록해야 "두
# 표현이 같은 게임"이라는 판정이 제품 경계 안에서도 성립한다.
mega_add_selftest(mega_defense_selftest_ai_astar 25000 ${MEGA_SELFTEST_UNIX_QUIET} 300
                  --ab map=procgen --ab pathing=astar --ab elite_brain=fsm)

# G5·G6 가 들여온 팔들. 셋 다 기본 팔에서는 한 줄도 돌지 않는다.
#
#   poly        assets/flags.txt 가 pathing 을 flowfield 로 **강제**하므로, 이제
#               기본 실행은 필드 팔이다. 명령줄이 그 배정을 이긴다는 것과 폴리라인
#               팔이 여전히 제품 안에서 도는 것을 이 한 줄이 함께 지킨다.
#   avatar_gate 건설이 아바타 사거리 안으로 묶이는 팔. 셀프테스트 스크립트가
#               목표 칸까지 걸어간 뒤에만 클릭하므로, 이 팔이 켜지면 걷는 코드가
#               제품 안에서 실제로 돈다.
#   meta        매치 밖 소비(sink). --buy-tier 가 지갑에서 스크랩을 빼 시작 골드
#               티어를 올리고, 그 값이 다음 판의 balance_.startGold 에 더해진다.
mega_add_selftest(mega_defense_selftest_poly 25000 ${MEGA_SELFTEST_UNIX_QUIET} 300
                  --ab pathing=poly)

# G8 이 들여온 팔들. 셋 다 기본 팔에서는 한 줄도 돌지 않는다.
#
#   map_maze=kruskal    골격 생성기의 두 번째 팔. 셀 사이 간선을 가중치 순으로
#                       훑으며 서로소 집합으로 사이클을 막는다. 기본 팔(재귀
#                       백트래커)과 형질이 다른 미로가 나오므로, 이 한 줄이 없으면
#                       그 지형 위에서 도는 경로·인지·잔해가 제품 안에서 한 번도
#                       검사되지 않는다.
#   build_check=artic   배치 판정을 칸 질의에서 격자 전수 판정으로 바꾼 팔.
#                       판정 결과가 같아야 하므로 이 검사의 result 는 기본 팔과
#                       같아야 한다 — 그것이 곧 "규칙 변경이 아니다"의 제품 증거다.
#   snapshot_pick=relevance
#                       스냅샷의 적 64자리를 relevancy 로 채우는 팔. 호스트 모드가
#                       아니면 스냅샷을 만들지 않으므로 아래 협동 기록 팔에 함께 건다.
#
# `--unlock 3` 은 해금 선행 그래프를 제품 안에서 실제로 소비시킨다. 상점 순서대로
# 사므로 선행이 언제나 먼저 결제되고, 잔액이 모자라면 거기서 멈춘다.
mega_add_selftest(mega_defense_selftest_maze 25000 ${MEGA_SELFTEST_UNIX_QUIET} 300
                  --ab map=procgen --ab map_maze=kruskal --ab build_check=artic
                  --ab pathing=flowfield)

# ── 해금 선행 그래프를 제품 안에서 실제로 소비한다 ──────────────────
#
# `--unlock N` 은 상점 순서를 따라 그 종류까지 산다. 냉각 프로필의 잔액은 0이고
# 첫 해금이 300스크랩이므로, 격리만 하면 이 검사는 **한 번도 사지 못하고** 조용히
# 지나간다 — `--buy-tier` 가 겪었던 것과 정확히 같은 함정이다.
#
# 그래서 데우기를 한 단계로 만든다. warm 이 프로필을 비우고 이벤트 창(스크랩 x2)
# 에서 한 판을 돌려 528스크랩을 남기고, unlock 이 그 위에서 산다 — 대포(300)를
# 사고 잔액 228 로 개틀링(450) 앞에서 멈춘다. 성공과 잔액 부족 정지가 **한 실행에
# 둘 다** 도는 자리다. fixture 가 순서를 강제하므로 -j 에서도 뒤집히지 않는다.
mega_add_selftest(mega_defense_selftest_unlock_warm 25000 ${MEGA_SELFTEST_UNIX_EVENT} 300)
set_tests_properties(mega_defense_selftest_unlock_warm PROPERTIES
                     FIXTURES_SETUP unlock_profile)

add_test(NAME mega_defense_selftest_unlock
         COMMAND mega_defense --selftest 25000 --unlock 3
                 --match-unix ${MEGA_SELFTEST_UNIX_QUIET}
                 --profile ${MEGA_PROFILE_DIR}/mega_defense_selftest_unlock_warm.wal
                 --ab map=procgen --ab map_maze=kruskal --ab build_check=artic)
set_tests_properties(mega_defense_selftest_unlock PROPERTIES
                     TIMEOUT 300 FIXTURES_REQUIRED unlock_profile)

mega_add_selftest(mega_defense_selftest_avatar_gate 25000 ${MEGA_SELFTEST_UNIX_QUIET} 300
                  --ab avatar_gate=1)

# ── meta 팔만 프로필을 **데운 채** 돈다 ─────────────────────────────
#
# 여기서 격리를 위 여덟과 똑같이 적용하면 이 검사는 아무것도 검사하지 않게 된다.
# 냉각 프로필의 잔액은 0이고, 티어 하나가 200스크랩이므로 `--buy-tier 5` 는
# **한 번도 사지 못하고** 조용히 지나간다. 매치 밖 소비 경로는 한 줄도 돌지 않고,
# 그래도 셀프테스트는 CLEAR 로 끝나 초록불이 켜진다.
#
# 지금까지 이 검사가 실제로 무언가를 산 것은 앞선 실행들이 공용 프로필을 데워
# 뒀기 때문이었다 — 즉 이 검사의 **커버리지가 실행 순서와 횟수에 딸려 있었다.**
# 우연에 기댄 커버리지는 커버리지가 아니다.
#
# 그래서 데우기를 우연이 아니라 **한 단계로** 만든다. warm 이 프로필을 비우고
# 한 판을 돌려 정확히 한 판치 스크랩(208)을 남기고, meta 가 그 위에서 산다
# (티어 0→1, 비용 200, 잔액 8). fixture 가 순서를 강제하므로 -j 에서도 뒤집히지
# 않고, warm 이 매번 비우고 출발하므로 몇 번째 ctest 이든 같은 잔액에서 시작한다.
# `--buy-tier 5` 는 그대로다 — 그 인자는 목표 티어의 **상한**이지 요구가 아니다.
mega_add_selftest(mega_defense_selftest_meta_warm 25000 ${MEGA_SELFTEST_UNIX_QUIET} 300)
set_tests_properties(mega_defense_selftest_meta_warm PROPERTIES
                     FIXTURES_SETUP meta_profile)

add_test(NAME mega_defense_selftest_meta
         COMMAND mega_defense --selftest 25000 --buy-tier 5
                 --match-unix ${MEGA_SELFTEST_UNIX_QUIET}
                 --profile ${MEGA_PROFILE_DIR}/mega_defense_selftest_meta_warm.wal)
set_tests_properties(mega_defense_selftest_meta PROPERTIES
                     TIMEOUT 300 FIXTURES_REQUIRED meta_profile)

# ══ G7: 골든 해시 체인 리플레이 ══════════════════════════════════════
#
# 지금까지의 결정성 검사는 전부 **한 프로세스 안에서 두 번 돌린** 결과를 비교했다
# (A/B 두 팔의 상태 해시). 그 방식은 프로세스 경계를 넘는 비결정성 — 시각, 환경
# 변수, 저장된 프로필, 주소에 좌우되는 순회 — 을 두 팔에 똑같이 넣어 서로 상쇄시킨다.
#
# 아래 짝은 그 경계를 넘는다. `--record` 를 돌린 프로세스가 죽은 뒤, **다른
# 프로세스**가 그 파일의 입력만 가지고 같은 체인을 다시 만든다. 상쇄될 상대가 없다.
#
# 짝을 fixture 로 묶는 이유는 순서 때문이다. 기록이 먼저 끝나야 재생이 읽을 파일이
# 있고, CTest 는 `-j` 로 병렬 실행할 수 있다. FIXTURES_SETUP/REQUIRED 가 그 순서를
# 강제하고, 기록이 실패하면 재생은 실행되지 않고 NOT RUN 으로 남는다.
#
# 세 팔의 성격이 다르다.
#   solo    1인 서브시뮬. 매 틱 전진하므로 lockstep 재생이 체크섬 전량을 대조한다.
#   host    2인인데 원격이 없다. 비예측 팔이라 서브시뮬이 **한 번도 전진하지 않고**,
#           lockstep 도 같은 이유로 멈춘다 — 문지기가 제품 안에서 도는 자리다.
#   avatar  2인 + 예측. 되감기 기계가 돌고, 없는 원격은 바닥값으로 메워진다.
set(MEGA_REPLAY_DIR ${CMAKE_CURRENT_BINARY_DIR}/replays)
file(MAKE_DIRECTORY ${MEGA_REPLAY_DIR})

# port 0 = 솔로(소켓 없음). 재생 쪽에는 --ab 를 넘기지 않는다 — 팔은 파일이
# 소유하고, 재생 실행 파일은 명령줄 스위치를 만나면 무시하지 않고 거절한다.
#
# 이 여섯에는 --profile 도 --fresh-profile 도 붙이지 않는다. 붙일 필요가 없기
# 때문이다 — `--record PATH` 와 `--verify-replay PATH` 는 제품 안에서 프로필을
# `PATH + ".wal"` 로 못박고(ReplayProfilePath) 열기 전에 비운다(TruncateFile).
# 즉 팔마다 다른 파일(격리)이고 실행마다 새 파일(재현)이다. 위 셀프테스트들이
# 겪던 누적을 리플레이가 겪지 않은 이유가 이것이고, 이번에 연 --fresh-profile 은
# 그 규칙을 셀프테스트 쪽에도 **명시적으로** 준 것이다.
# snippet-begin: replay-pair
function(mega_add_replay_pair arm ticks port)
    set(path ${MEGA_REPLAY_DIR}/${arm}.mgrp)
    set(shared "")
    if(NOT port EQUAL 0)
        set(shared --host ${port})
    endif()
    add_test(NAME mega_defense_record_${arm}
             COMMAND mega_defense --selftest ${ticks} --record ${path}
                     --match-unix ${MEGA_SELFTEST_UNIX_QUIET} ${shared} ${ARGN})
    set_tests_properties(mega_defense_record_${arm} PROPERTIES
                         TIMEOUT 300 FIXTURES_SETUP replay_${arm})
    add_test(NAME mega_defense_replay_${arm}
             COMMAND mega_defense --verify-replay ${path} ${shared})
    set_tests_properties(mega_defense_replay_${arm} PROPERTIES
                         TIMEOUT 300 FIXTURES_REQUIRED replay_${arm})
endfunction()
# snippet-end: replay-pair

mega_add_replay_pair(solo 4000 0)
# 호스트 팔은 UDP 포트를 잡는다. 기록과 재생이 같은 포트를 쓰지만 fixture 가
# 순서를 강제하므로 겹치지 않고, 두 팔이 서로 다른 포트를 쓰므로 병렬 실행에서도
# 겹치지 않는다.
mega_add_replay_pair(host 4000 47821)
mega_add_replay_pair(avatar 4000 47822 --ab avatar_predict=1 --ab avatar_gate=1)
# 네 번째 팔. 호스트 모드라 스냅샷을 실제로 만들고, 그 스냅샷의 64자리를
# relevancy 로 채운다. 절차 지형 · Kruskal 골격 · 전수 배치 판정도 함께 돈다 —
# 그 넷을 지나는 한 판이 기록되고, **다른 프로세스가 그 조작만으로 같은 체인을
# 다시 만든다**. 선택 정책이 시뮬을 건드리면 여기서 갈린다.
mega_add_replay_pair(relevance 4000 47823 --ab snapshot_pick=relevance
                     --ab map=procgen --ab map_maze=kruskal --ab build_check=artic)

# 체인·포맷·lockstep 재생의 단위 검사. 제품 프레임 루프를 두 번 돌리므로 실행
# 파일 옆의 assets/ 가 필요하다(그 디렉터리를 만드는 것은 mega_defense 의 POST_BUILD).
add_dependencies(test_replay_golden mega_defense)
set_tests_properties(test_replay_golden PROPERTIES TIMEOUT 180)

# GPU 표시 경로도 ctest 대상이다. 감사 #7·#24 는 README·docs 가 "Vulkan 스왑체인
# 5프레임 present 를 자동 검증한다"고 적으면서 ctest 에도 CI 에도 그 항목이 없는
# 상태를 지적했다. 여기서 등록해 그 문장을 사실로 만든다.
#
# `if(Vulkan_FOUND)` 안에 넣지 않는다. 그러면 테스트 목록 자체가 머신마다 달라져
# "ctest N개"라는 서술이 어느 머신에서는 틀리고, 무엇보다 GPU 가 없다는 사실이
# 결과에 아무 흔적도 남기지 않는다. 대신 항상 등록하고, 실행할 수 없는 환경에서는
# 실행 파일이 kDefenseSkipExitCode(77) 로 끝나 ctest 가 이 검사를 SKIP 으로 센다.
# 실패는 오직 "장치도 창도 있는데 present 가 깨졌을 때"뿐이다.
# snippet-begin: gpu-skip
add_test(NAME mega_defense_gpu_selftest COMMAND mega_defense --gpu-selftest)
set_tests_properties(mega_defense_gpu_selftest PROPERTIES
                     TIMEOUT 120
                     SKIP_RETURN_CODE 77)
# snippet-end: gpu-skip

# 도달성 게이트는 "헤더가 제품에서 도달 가능한가"만 강제한다. 도달 가능한 코드가
# 실제로 **실행되는가**는 묻지 않으므로, 배선 결함은 무증상으로 통과할 수 있다.
# 그 두 번째 게이트가 아래 둘이다.
#
#   test_defense_client_loop   제품 DefenseApp 을 Client 로 구성해 진짜 소켓 위에서
#                              Step() 을 돈다. PumpClient 를 부르는 첫 테스트다.
#   test_defense_coop_process  호스트도 클라이언트도 mega_defense 실행 파일이다.
#                              main.cpp → RunDefense → 클라이언트 루프까지 이어지는
#                              배선 전체를 프로세스 경계 밖에서 확인한다.
#   test_live_ops_wiring       같은 이유로 라이브 운영 축에 선 세 번째다. 배정이
#                              스위치 표에 얹히는 자리 · 이벤트 수정자가 웨이브
#                              수치에 곱해지는 자리 · 정산이 스크랩 배율을 곱하는
#                              자리를 제품 DefenseApp 안에서 관측한다. 그 셋은
#                              `live/` 단위 테스트가 닿지 못하는 배선이었다.
#
# 후자는 mega_defense 실행 파일과 그 옆의 assets 를 실제로 실행하므로 빌드 의존이
# 필요하다. 전자도 같은 assets 를 읽는다(없어도 폴백으로 돌지만, 두 테스트가 같은
# 콘텐츠를 보게 해 두는 편이 진단이 쉽다).
add_dependencies(test_defense_coop_process mega_defense)
add_dependencies(test_defense_client_loop mega_defense)
# 이 검사는 **배포되는** assets/flags.txt · events.txt · balance.script ·
# waves.txt 를 읽는다. 폴백으로 돌면 "전원 기본 팔 · 이벤트 없음"이 되어 배선이
# 죽은 상태와 같은 값을 내므로, 파일이 실제로 채택됐는지를 먼저 단언한다.
add_dependencies(test_live_ops_wiring mega_defense)
# 매치 두 판을 헤드리스로 끝까지 돌린다(정산은 매치가 끝나야 일어난다).
set_tests_properties(test_live_ops_wiring PROPERTIES TIMEOUT 180)
# 프로필 격리의 게이트. 위 `mega_add_selftest` 가 붙이는 두 인자가 실제로
# 재현성을 만드는지, 그리고 그 인자를 빼면 실제로 결과가 갈리는지를 **같은
# 파일에서 양쪽 다** 확인한다. 음성 대조가 없으면 이 검사는 "무엇을 해도 통과"가
# 될 수 있다 — 두 실행이 우연히 같은 것과 격리가 작동한 것을 구분하지 못한다.
# 제품 실행 파일을 두 번 띄우고 헤드리스 매치도 두 판 돌리므로 시간을 넉넉히 준다.
add_dependencies(test_profile_isolation mega_defense)
set_tests_properties(test_profile_isolation PROPERTIES TIMEOUT 300)
# 아래 둘도 실행 파일 옆의 assets/ 를 읽는다(에셋 리로드와 프레임 계측은 진짜
# 스프라이트가 있어야 성립한다). 그 디렉터리를 만드는 것은 mega_defense 의
# POST_BUILD 다.
add_dependencies(test_async_reload mega_defense)
add_dependencies(test_frame_trace mega_defense)
# 보스의 저작 포즈도 실행 파일 옆의 assets/ 에서 온다. 이 테스트는 **배포되는
# 그 파일**을 읽어 내장 클립과 다르다는 것까지 확인하므로, 인라인 문자열만으로는
# "로더가 제품에서도 쓰이는가"에 답할 수 없다.
add_dependencies(test_pose_content mega_defense)
# 핸들 소유 검사도 실행 파일 옆의 assets/ 로 한 프레임을 실제로 그린다.
add_dependencies(test_asset_handles mega_defense)
# 정예 행동 트리도 실행 파일 옆의 assets/ 에서 온다. 이 테스트는 **배포되는
# 그 파일**을 읽어 조용히 거부되고 있지 않은지까지 확인하므로, 인라인 문자열만
# 으로는 "저장소의 elite.bt 가 실제로 쓰이는가"에 답할 수 없다.
add_dependencies(test_elite_content mega_defense)
# 규모를 올린 A/B 는 실제로 오래 돈다. 매달리지는 않게 상한을 둔다.
set_tests_properties(test_entity_store_ab PROPERTIES TIMEOUT 180)
set_tests_properties(test_id_map_ab PROPERTIES TIMEOUT 180)
set_tests_properties(test_jobs_determinism PROPERTIES TIMEOUT 300)
set_tests_properties(test_frame_alloc_budget PROPERTIES TIMEOUT 180)
set_tests_properties(test_deferred_free PROPERTIES TIMEOUT 120)
# 잠금 A/B 와 시작·정지 반복은 "깨우기가 죽으면 실패가 아니라 매달린다" 부류다.
# 그 상태를 실패로 바꿔 주는 것은 타임아웃뿐이다.
set_tests_properties(test_scheduler_lock_ab PROPERTIES TIMEOUT 120)
set_tests_properties(test_async_reload PROPERTIES TIMEOUT 120)

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

# snippet-begin: reach-gate
add_custom_target(mega_reachability ALL
    COMMAND ${MEGA_REACHABILITY_COMMAND}
    COMMENT "도달성 게이트: include/ 의 모든 헤더가 제품 도달 또는 LAB_ONLY 등재인가"
    VERBATIM)
add_dependencies(mega_reachability mega_defense mega_defense_server)

add_test(NAME reachability_gate COMMAND ${MEGA_REACHABILITY_COMMAND})
# snippet-end: reach-gate
