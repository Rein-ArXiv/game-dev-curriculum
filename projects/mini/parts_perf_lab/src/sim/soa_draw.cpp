// SoASimulation의 렌더링 전용 번역 단위.
//
// Update / CopyState 같은 계산 코어는 soa.cpp에 있고 raylib에 의존하지 않는다.
// 화면 출력만 이 파일로 분리해 두면 headless 테스트 타깃이 이 TU를 빼는 것만으로
// raylib(=GLFW·OpenGL)을 전혀 링크하지 않고 계산 코어를 검증할 수 있다.
#include "sim/soa.h"

#include "raylib.h"

namespace ppl {

void SoASimulation::Draw() const {
    for (std::size_t i = 0; i < count_; ++i) {
        DrawPixel(static_cast<int>(posX_[i]),
                  static_cast<int>(posY_[i]),
                  WHITE);
    }
}

}  // namespace ppl
