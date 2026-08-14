cmake_minimum_required(VERSION 3.20)
project(graphics_dx_lab CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "graphics_dx_lab is Windows-only (DirectX 12).")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(FetchContent)

# Dear ImGui (docking) — DX12 + Win32 백엔드 포함.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.5-docking
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC d3d12 dxgi)

# ImPlot.
FetchContent_Declare(
    implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG v0.16
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(implot)

add_library(implot STATIC
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
    ${implot_SOURCE_DIR}/implot_demo.cpp
)
target_include_directories(implot PUBLIC ${implot_SOURCE_DIR})
target_link_libraries(implot PUBLIC imgui)

# DirectXMath header-only.
FetchContent_Declare(
    directxmath
    GIT_REPOSITORY https://github.com/microsoft/DirectXMath.git
    GIT_TAG dec2023
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(directxmath)

add_executable(${PROJECT_NAME} WIN32
    src/main.cpp
    src/app/app.cpp
    src/device/d3d12_context.cpp
    src/device/upload_buffer.cpp
    src/pso/pso_cache.cpp
    src/rootsig/root_signature.cpp
    src/descriptor/heap_manager.cpp
    src/barrier/barrier_helpers.cpp
    src/scene/scene.cpp
    src/scene/scene_record.cpp
    src/measure/frame_timer.cpp
    src/measure/timestamp_query.cpp
    src/measure/d3d12_debug.cpp
    src/ui/panel.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    ${directxmath_SOURCE_DIR}/Inc)

target_link_libraries(${PROJECT_NAME} PRIVATE
    imgui
    implot
    d3d12
    dxgi
    d3dcompiler
    dxguid)

# Shaders 디렉토리 복사.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders)

enable_testing()
# 계약 테스트는 *제품 TU 를 그대로 링크* 한다. 여기 나열된 파일들은 디바이스
# 생성·스왑체인·ImGui 에 의존하지 않으므로 GPU 없이 헤드리스로 검증된다.
add_executable(graphics_dx_contract_tests
    tests/graphics_dx_contract_tests.cpp
    src/barrier/barrier_helpers.cpp
    src/descriptor/heap_manager.cpp
    src/scene/scene_record.cpp
    src/measure/timestamp_query.cpp)
target_include_directories(graphics_dx_contract_tests PRIVATE
    src
    ${directxmath_SOURCE_DIR}/Inc)
target_compile_definitions(graphics_dx_contract_tests PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    DXLAB_WINDOWS=1)
target_link_libraries(graphics_dx_contract_tests PRIVATE d3d12 dxguid)
add_test(NAME graphics_dx_contract_tests COMMAND graphics_dx_contract_tests)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    UNICODE _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    DXLAB_WINDOWS=1)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/Oi>)
    target_compile_options(graphics_dx_contract_tests PRIVATE
        /W4 /permissive- /utf-8)
endif()
