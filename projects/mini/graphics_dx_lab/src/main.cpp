// graphics_dx_lab — DirectX 12 측정 lab.
//
// WinMain 진입점 (WIN32 subsystem). App 객체 생성 + Run.

#include "app/app.h"

#include <Windows.h>
#include <exception>

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    try {
        dxlab::App app(hInstance, nCmdShow);
        return app.Run();
    } catch (const std::exception& e) {
        MessageBoxA(nullptr, e.what(), "graphics_dx_lab fatal", MB_ICONERROR | MB_OK);
        return 1;
    }
}
