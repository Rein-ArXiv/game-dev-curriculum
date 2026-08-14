cmake_minimum_required(VERSION 3.20)
project(graphics_gl_lab CXX C)

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

# GLFW — 윈도우 + 입력. 시스템 설치 회피.
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glfw)

# glad2 — OpenGL 4.5 Core 로더. cmake 모듈로 *빌드 시점 생성*.
# (glad2 의 generator 가 Python + Jinja2 를 사용. Jinja2 미설치 시 빌드 실패.
#  편의를 위해 자동 venv 를 만들어 jinja2 를 설치한다 — 사용자 시스템 Python 환경
#  오염 없음.)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(GLAD_VENV_DIR ${CMAKE_BINARY_DIR}/glad_venv)
if(NOT EXISTS "${GLAD_VENV_DIR}")
    message(STATUS "Creating glad venv: ${GLAD_VENV_DIR}")
    execute_process(
        COMMAND ${Python3_EXECUTABLE} -m venv ${GLAD_VENV_DIR}
        RESULT_VARIABLE _venv_rc)
    if(NOT _venv_rc EQUAL 0)
        message(FATAL_ERROR "Failed to create glad venv (rc=${_venv_rc}). "
                            "Install Python's venv module or `pip install jinja2` "
                            "system-wide and re-run cmake.")
    endif()
endif()
if(WIN32)
    set(GLAD_VENV_PY ${GLAD_VENV_DIR}/Scripts/python.exe)
else()
    set(GLAD_VENV_PY ${GLAD_VENV_DIR}/bin/python)
endif()
execute_process(
    COMMAND ${GLAD_VENV_PY} -m pip install --disable-pip-version-check -q jinja2
    RESULT_VARIABLE _pip_rc)
if(NOT _pip_rc EQUAL 0)
    message(FATAL_ERROR "Failed to install jinja2 into glad venv (rc=${_pip_rc}). "
                        "Manually `pip install jinja2` and re-run cmake.")
endif()
# glad2 가 시스템 PATH 의 python 을 호출하지 않도록 venv 의 python 을 강제.
# glad 의 CMake 모듈은 Python3 이 아니라 Python 패키지를 다시 찾는다.
set(Python3_EXECUTABLE ${GLAD_VENV_PY} CACHE FILEPATH "" FORCE)
set(Python_EXECUTABLE  ${GLAD_VENV_PY} CACHE FILEPATH "" FORCE)

FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v2.0.6
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR cmake
)
FetchContent_MakeAvailable(glad)
glad_add_library(glad STATIC REPRODUCIBLE
    API gl:core=4.5
    EXTENSIONS GL_EXT_texture_compression_s3tc GL_ARB_texture_compression_bptc)

# Dear ImGui — UI. docking 브랜치는 ImGui::DockSpace 가능.
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
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw glad)
target_compile_definitions(imgui PUBLIC
    GLFW_INCLUDE_NONE
    IMGUI_IMPL_OPENGL_LOADER_GLAD2)

# ImPlot — 그래프 (ms/frame, GPU 시간, 드로우콜 수).
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

# glm — 행렬/벡터.
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glm)

# 메인 실행파일.
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/app/app.cpp
    src/scene/scene.cpp
    src/render/shader_program.cpp
    src/render/shader_registry.cpp
    src/render/lighting_bindings.cpp
    src/render/texture_modes.cpp
    src/render/msaa_target.cpp
    src/measure/frame_timer.cpp
    src/measure/gpu_timer.cpp
    src/measure/khr_debug.cpp
    src/ui/panel.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    src)

target_link_libraries(${PROJECT_NAME} PRIVATE
    glfw
    glad
    imgui
    implot
    glm::glm)

# 셰이더 파일을 빌드 출력 옆에 복사 (런타임 로드용).
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders)

enable_testing()

# 테스트가 실제로 검증하는 대상은 *프로덕션 번역 단위* 다. 계약 테스트는 GL
# 컨텍스트 없이 돌아야 하므로 GL 을 호출하지 않는 진입점만 부르되, 링크는
# 앱과 같은 .cpp 를 한다 — 그래야 제품 코드를 망가뜨렸을 때 테스트가 죽는다.
set(GLLAB_CORE_SOURCES
    src/scene/scene.cpp
    src/render/shader_program.cpp
    src/render/lighting_bindings.cpp
    src/render/texture_modes.cpp
    src/render/msaa_target.cpp
    src/measure/khr_debug.cpp)

add_executable(graphics_gl_contract_tests
    tests/graphics_gl_contract_tests.cpp
    ${GLLAB_CORE_SOURCES})
target_include_directories(graphics_gl_contract_tests PRIVATE src)
target_link_libraries(graphics_gl_contract_tests PRIVATE glad glm::glm)
add_test(NAME graphics_gl_contract_tests COMMAND graphics_gl_contract_tests)

# 렌더 캡처 회귀 + 자원 수명 — 실제 GL 4.5 컨텍스트가 필요하다. 히든 윈도우로
# 컨텍스트를 만들고, 만들 수 없는 환경(헤드리스 CI)에서는 77 로 skip 한다.
add_executable(graphics_gl_render_tests
    tests/graphics_gl_render_tests.cpp
    src/render/shader_registry.cpp
    ${GLLAB_CORE_SOURCES})
target_include_directories(graphics_gl_render_tests PRIVATE src)
target_link_libraries(graphics_gl_render_tests PRIVATE glfw glad glm::glm)
target_compile_definitions(graphics_gl_render_tests PRIVATE GLFW_INCLUDE_NONE)
add_custom_command(TARGET graphics_gl_render_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders
            $<TARGET_FILE_DIR:graphics_gl_render_tests>/shaders)
add_test(NAME graphics_gl_render_tests
         COMMAND graphics_gl_render_tests
         WORKING_DIRECTORY $<TARGET_FILE_DIR:graphics_gl_render_tests>)
set_tests_properties(graphics_gl_render_tests PROPERTIES
    SKIP_RETURN_CODE 77
    TIMEOUT 300)

# 플랫폼 분기.
if(UNIX AND NOT APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE GLLAB_LINUX=1 GLFW_INCLUDE_NONE)
    target_link_libraries(${PROJECT_NAME} PRIVATE dl pthread)
elseif(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE GLLAB_WINDOWS=1 GLFW_INCLUDE_NONE)
elseif(APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE GLLAB_MACOS=1 GLFW_INCLUDE_NONE)
endif()

# 컴파일러 경고.
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(graphics_gl_contract_tests PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(graphics_gl_render_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall -Wextra -Wpedantic)
    target_compile_options(graphics_gl_contract_tests PRIVATE
        -Wall -Wextra -Wpedantic)
    target_compile_options(graphics_gl_render_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

# Release 최적화.
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/Oi>)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-fno-omit-frame-pointer>)
endif()
