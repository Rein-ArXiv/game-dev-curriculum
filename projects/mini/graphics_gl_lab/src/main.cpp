// graphics_gl_lab — graphics 시리즈 OpenGL 단계의 원리 측정·시각화.
//
// 진입점은 단순. App 객체 한 개를 만들고 Run() 호출. 윈도우 / GL 컨텍스트 /
// ImGui / Scene 초기화는 App 안에서.
//
// Step 1 (이 단계): 윈도우 + 1만 오브젝트 fixed scene + 기본 셰이더 + ImGui
//                    + ImPlot + CPU frame timer.
// Step 2 예정     : GL_TIME_ELAPSED + 드로우콜 카운터 + KHR_debug.
// Step 3~7 예정   : 토글 항목 추가 (드로우콜 / 셰이더 perm / 바인딩 / 압축 / MSAA).

#include "app/app.h"

#include <cstdio>
#include <exception>

int main() {
    try {
        gllab::App app;
        return app.Run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[fatal] %s\n", e.what());
        return 1;
    }
}
