// system_kernel_lab — Linux 커널 메커니즘(IO·동기화·스케줄러·컨텍스트)을
// 동일 워크로드 위에서 토글로 비교하는 측정 도구.
//
// 이 단계의 활성 토글: backend (5종) / sync (4종) / sched (4종) /
// ctx (pthread/coroutine) / worker count (1/2/4/8).
// 콤보 변경 → 워크로드 Reconfigure → 다음 사이클부터.

#include "measure/frame_timer.h"
#include "measure/proc_counters.h"
#include "ui/hud.h"
#include "ui/toggles.h"
#include "workload/file_io_workload.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#include <SDL.h>
#include <SDL_opengl.h>

#include <cstdio>
#include <cstdlib>
#include <exception>

namespace {

constexpr int kWindowW = 1280;
constexpr int kWindowH = 720;

constexpr std::size_t kFileCount = 16;
constexpr std::size_t kFileBytes = 1u * 1024u * 1024u;  // 1 MB

}  // namespace

int main(int /*argc*/, char** /*argv*/) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

    constexpr Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
                                   | SDL_WINDOW_ALLOW_HIGHDPI;
    SDL_Window* window = SDL_CreateWindow("system_kernel_lab",
                                           SDL_WINDOWPOS_CENTERED,
                                           SDL_WINDOWPOS_CENTERED,
                                           kWindowW, kWindowH, windowFlags);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }
    SDL_GL_MakeCurrent(window, glContext);
    SDL_GL_SetSwapInterval(0);  // VSync 끔. 측정이 모니터 갭에 묶이지 않게.

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 150");

    skl::Toggles toggles;
    skl::Toggles prevToggles = toggles;

    skl::FileIoWorkload workload;
    workload.Init(toggles.backend, toggles.sync, toggles.sched, toggles.ctx,
                  skl::WorkerCountFromIdx(toggles.workerIdx),
                  kFileCount, kFileBytes);
    toggles.backend = workload.Backend();
    prevToggles = toggles;

    skl::FrameTimer timer;

    skl::Hud hud;
    hud.Init(kWindowW, kWindowH);

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_WINDOWEVENT
                && event.window.event == SDL_WINDOWEVENT_CLOSE
                && event.window.windowID == SDL_GetWindowID(window)) {
                running = false;
            }
        }

        // 측정: 한 *사이클* = 한 패스 워크로드. ImGui 렌더링과 분리한다.
        // 사이클 시작·끝에 ProcCounters를 읽어 ctxSwitch / pageFault delta를
        // FrameTimer에 같이 push한다.
        timer.BeginCycle();
        const skl::ProcCounters before = skl::ReadProcCounters();
        std::uint64_t bytes = 0;
        try {
            bytes = workload.RunCycle();
        } catch (const std::exception& e) {
            std::fprintf(stderr, "system_kernel_lab cycle failed: %s\n", e.what());
            running = false;
            break;
        }
        const skl::ProcCounters after = skl::ReadProcCounters();
        const skl::ProcCounters delta = skl::DiffCounters(before, after);
        const std::uint64_t ctxSwTotal = static_cast<std::uint64_t>(
            delta.voluntaryCtxSw + delta.involuntaryCtxSw);
        const std::uint64_t faultsTotal = static_cast<std::uint64_t>(
            delta.minorFaults + delta.majorFaults);
        timer.EndCycle(bytes, ctxSwTotal, faultsTotal);

        // ImGui 프레임 시작.
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        int w = 0, h = 0;
        SDL_GetWindowSize(window, &w, &h);
        hud.Init(w, h);  // 윈도우 크기 변경 시 매 프레임 갱신.
        hud.Draw(toggles, timer, workload);

        ImGui::Render();
        glViewport(0, 0, w, h);
        glClearColor(0.06f, 0.06f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);

        // 토글 변경 처리.
        // backend / sync / sched / ctx / workerCount 중 하나라도 바뀌면 재구성.
        // fixture는 그대로 유지된다.
        if (skl::RequiresRestart(prevToggles, toggles)) {
            try {
                workload.Reconfigure(toggles.backend, toggles.sync, toggles.sched,
                                      toggles.ctx,
                                      skl::WorkerCountFromIdx(toggles.workerIdx));
                toggles.backend = workload.Backend();
            } catch (const std::exception& e) {
                std::fprintf(stderr,
                    "system_kernel_lab reconfigure failed: %s\n", e.what());
                running = false;
            }
        }
        prevToggles = toggles;
    }

    workload.Shutdown();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}
