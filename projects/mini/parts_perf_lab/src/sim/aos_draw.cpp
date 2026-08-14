// AoSSimulation의 렌더링 전용 번역 단위.
//
// 계산 코어(aos.cpp)와 화면 출력을 분리한다. 테스트 타깃은 이 TU를 컴파일하지
// 않으므로 raylib 없이 빌드된다.
#include "sim/aos.h"

#include "raylib.h"

namespace ppl {

void AoSSimulation::Draw() const {
    for (const auto& e : entities_) {
        DrawPixel(static_cast<int>(e.posX),
                  static_cast<int>(e.posY),
                  WHITE);
    }
}

}  // namespace ppl
