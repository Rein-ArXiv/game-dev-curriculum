cmake_minimum_required(VERSION 3.20)
project(graphics_vk_lab CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Vulkan COMPONENTS glslc)
if(NOT Vulkan_FOUND OR NOT Vulkan_GLSLC_EXECUTABLE)
    message(FATAL_ERROR
        "Vulkan SDK not found or glslc is missing. "
        "Install the LunarG Vulkan SDK, ensure VULKAN_SDK is set, "
        "and verify `glslc --version` works in this shell.")
endif()

include(FetchContent)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(glfw)

# Dear ImGui + ImPlot.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.5-docking
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)

FetchContent_Declare(implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG v0.16
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(implot)
add_library(implot STATIC
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
    ${implot_SOURCE_DIR}/implot_demo.cpp)
target_include_directories(implot PUBLIC ${implot_SOURCE_DIR})
target_link_libraries(implot PUBLIC imgui)

# VMA (Vulkan Memory Allocator) — 메모리 alloc 헬퍼.
FetchContent_Declare(vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG v3.1.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(vma)

FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(glm)

# Shader 컴파일 — glslc 로 .spv 생성.
set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
file(GLOB SHADER_SOURCES
    ${SHADER_DIR}/*.vert
    ${SHADER_DIR}/*.frag
    ${SHADER_DIR}/*.comp)

set(SHADER_BINARIES)
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPV ${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER_NAME}.spv)
    add_custom_command(OUTPUT ${SPV}
        COMMAND ${CMAKE_COMMAND} -E make_directory
                ${CMAKE_CURRENT_BINARY_DIR}/shaders
        COMMAND ${Vulkan_GLSLC_EXECUTABLE} -O ${SHADER} -o ${SPV}
        DEPENDS ${SHADER}
        COMMENT "glslc ${SHADER_NAME}")
    list(APPEND SHADER_BINARIES ${SPV})
endforeach()
add_custom_target(vk_shaders DEPENDS ${SHADER_BINARIES})

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/app/app.cpp
    src/device/vk_context.cpp
    src/device/vk_swapchain.cpp
    src/device/swapchain_select.cpp
    src/device/vk_alloc.cpp
    src/pipeline/pipeline_cache.cpp
    src/barrier/barrier_helpers.cpp
    src/sync/timeline_sync.cpp
    src/sync/frame_completion.cpp
    src/scene/scene.cpp
    src/scene/scene_core.cpp
    src/measure/frame_timer.cpp
    src/measure/query_ring.cpp
    src/measure/timestamp_query.cpp
    src/measure/validation_log.cpp
    src/ui/panel.cpp
)
add_dependencies(${PROJECT_NAME} vk_shaders)

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    ${vma_SOURCE_DIR}/include)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    glfw
    imgui
    implot
    glm::glm)

# Vulkan 클립 공간 — depth 0..1, 라디안. 모든 TU 에서 동일해야 ODR 안전.
set(VKLAB_SHADER_RUNTIME_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
target_compile_definitions(${PROJECT_NAME} PRIVATE
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_RADIANS
    "VKLAB_SHADER_DIR=\"${VKLAB_SHADER_RUNTIME_DIR}\"")

# Shaders 디렉토리 복사.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_BINARY_DIR}/shaders
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders)

enable_testing()
# 테스트 타깃은 디바이스가 필요 없는 제품 TU 를 전부 링크한다. 여기 없는 TU 는
# 자동 검증 밖이라는 뜻이므로, 순수 로직을 새로 분리하면 이 목록에 추가한다.
add_executable(graphics_vk_contract_tests
    tests/graphics_vk_contract_tests.cpp
    src/barrier/barrier_helpers.cpp
    src/device/swapchain_select.cpp
    src/measure/query_ring.cpp
    src/scene/scene_core.cpp
    src/sync/frame_completion.cpp
    src/sync/timeline_sync.cpp)
target_include_directories(graphics_vk_contract_tests PRIVATE
    src
    ${vma_SOURCE_DIR}/include)
target_link_libraries(graphics_vk_contract_tests PRIVATE
    Vulkan::Vulkan
    glm::glm)
# 앱 타깃과 같은 glm 설정 — scene_core.cpp 가 두 타깃에 모두 들어가므로 ODR 안전.
target_compile_definitions(graphics_vk_contract_tests PRIVATE
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_RADIANS)
add_test(NAME graphics_vk_contract_tests COMMAND graphics_vk_contract_tests)

if(UNIX AND NOT APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE VKLAB_LINUX=1)
    target_link_libraries(${PROJECT_NAME} PRIVATE dl pthread)
elseif(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE VKLAB_WINDOWS=1)
elseif(APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE VKLAB_MACOS=1)
endif()

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(graphics_vk_contract_tests PRIVATE
        /W4 /permissive- /utf-8)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(graphics_vk_contract_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()
