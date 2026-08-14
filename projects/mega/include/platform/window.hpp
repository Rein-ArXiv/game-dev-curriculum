// window — 창 · 키보드 · 마우스 · 프레임버퍼 표시를 소유하는 유일한 계층.
//
// 계층 규칙(A3): games/ 는 창 API 를 직접 부를 수 없다. 이전에는 이 코드가
// games/defense/win_app.hpp 에 있었고, 그래서 제품 진입점이 <windows.h> 를
// 통째로 들이마셨다 — main.cpp 가 UDP 든 창이든 무엇이든 그 자리에서 다시
// 구현할 수 있는 물리적 경로가 거기서 열렸다. 이제 그 권한은 platform/ 에만 있다.
//
// 구현은 소프트웨어 Framebuffer 의 픽셀을 GDI 로 창에 blit 하는 최소 프런트엔드다.
// (windows.h 의 min/max 매크로 오염을 막으려 NOMINMAX 를 켠다.)
// 비-Windows 에서는 같은 인터페이스의 헤드리스 스텁이다 — Create() 가 false 를
// 반환하므로 호출자는 "창을 못 만들었다" 한 갈래만 처리하면 된다.

#pragma once

#include <cstdint>
#include <string>

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <vector>

namespace mega {
namespace platform {

// 단일 창 전제. 키 상태는 WndProc 가 갱신하는 정적 테이블로 둔다.
class Window {
public:
    Window() = default;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool Create(const wchar_t* title, int w, int h, int scale = 1) {
        w_ = w; h_ = h; scale_ = scale < 1 ? 1 : scale;
        bgra_.assign((size_t)w * h, 0xFF000000u);

        HINSTANCE inst = GetModuleHandleW(nullptr);
        WNDCLASSW wc{};
        wc.lpfnWndProc = &Window::WndProc;
        wc.hInstance = inst;
        wc.lpszClassName = L"MegaPlayWindow";
        wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
        RegisterClassW(&wc);

        RECT rc{0, 0, w_ * scale_, h_ * scale_};
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
        hwnd_ = CreateWindowExW(0, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
                                nullptr, nullptr, inst, nullptr);
        if (!hwnd_) return false;
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
        quit_ = false;
        return true;
    }

    // 메시지 펌프. 창이 닫히거나 ESC 면 false.
    bool PumpAndContinue() {
        for (int i = 0; i < 256; ++i) prevKeys_[i] = keys_[i];
        prevMouseL_ = mouseL_;
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) quit_ = true;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (keys_[VK_ESCAPE]) quit_ = true;
        return !quit_;
    }

    // 프레임버퍼(0xAABBGGRR) → 창에 blit. R/B 스왑해 GDI(BGRX)로 변환.
    void Present(const uint32_t* px, int w, int h) {
        if (w != w_ || h != h_) return;
        for (int i = 0; i < w * h; ++i) {
            uint32_t s = px[i];
            uint32_t r = s & 0xFF, g = (s >> 8) & 0xFF, b = (s >> 16) & 0xFF;
            bgra_[i] = (r << 16) | (g << 8) | b;
        }
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w_;
        bi.bmiHeader.biHeight = -h_;     // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        HDC dc = GetDC(hwnd_);
        StretchDIBits(dc, 0, 0, w_ * scale_, h_ * scale_, 0, 0, w_, h_,
                      bgra_.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
        ReleaseDC(hwnd_, dc);
    }

    bool Key(int vk) const { return (vk >= 0 && vk < 256) ? keys_[vk] : false; }
    // 이번 프레임에 새로 눌림(엣지) — 턴제용.
    bool KeyPressed(int vk) const { return (vk >= 0 && vk < 256) && keys_[vk] && !prevKeys_[vk]; }
    // 마우스(논리 좌표 = 창/scale). 좌클릭 누름 상태·엣지.
    int  MouseX() const { return mouseX_ / scale_; }
    int  MouseY() const { return mouseY_ / scale_; }
    bool MouseDown() const { return mouseL_; }
    bool MouseClicked() const { return mouseL_ && !prevMouseL_; }
    void SetTitle(const std::wstring& t) { if (hwnd_) SetWindowTextW(hwnd_, t.c_str()); }
    // 네이티브 핸들. 표시 백엔드(Vulkan WSI)만 쓰라고 void* 로 내보낸다 —
    // 이렇게 해야 게임 코드가 HWND 라는 타입을 아예 모른다.
    void* NativeHandle() const { return static_cast<void*>(hwnd_); }

    ~Window() { if (hwnd_) DestroyWindow(hwnd_); }

private:
    static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
        switch (m) {
            case WM_KEYDOWN: if (wp < 256) keys_[wp] = true;  return 0;
            case WM_KEYUP:   if (wp < 256) keys_[wp] = false; return 0;
            case WM_MOUSEMOVE:   mouseX_ = (short)LOWORD(lp); mouseY_ = (short)HIWORD(lp); return 0;
            case WM_LBUTTONDOWN: mouseL_ = true;  return 0;
            case WM_LBUTTONUP:   mouseL_ = false; return 0;
            case WM_CLOSE:   quit_ = true; return 0;
            case WM_DESTROY: PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(h, m, wp, lp);
    }

    HWND                  hwnd_ = nullptr;
    int                   w_ = 0, h_ = 0, scale_ = 1;
    std::vector<uint32_t> bgra_;
    bool                  prevKeys_[256] = {};
    bool                  prevMouseL_ = false;
    static bool           keys_[256];
    static bool           quit_;
    static int            mouseX_, mouseY_;
    static bool           mouseL_;
};

inline bool Window::keys_[256] = {};
inline bool Window::quit_ = false;
inline int  Window::mouseX_ = 0;
inline int  Window::mouseY_ = 0;
inline bool Window::mouseL_ = false;

}  // namespace platform
}  // namespace mega

#else  // 창 백엔드가 없는 플랫폼: 같은 인터페이스의 헤드리스 스텁

namespace mega {
namespace platform {

class Window {
public:
    Window() = default;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool Create(const wchar_t*, int, int, int = 1) { return false; }
    bool PumpAndContinue() { return false; }
    void Present(const uint32_t*, int, int) {}
    bool Key(int) const { return false; }
    bool KeyPressed(int) const { return false; }
    int  MouseX() const { return -1; }
    int  MouseY() const { return -1; }
    bool MouseDown() const { return false; }
    bool MouseClicked() const { return false; }
    void SetTitle(const std::wstring&) {}
    void* NativeHandle() const { return nullptr; }
};

}  // namespace platform
}  // namespace mega

#endif  // _WIN32
