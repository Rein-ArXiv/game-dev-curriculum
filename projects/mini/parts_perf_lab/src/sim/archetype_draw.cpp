// ArchetypeSimulation의 렌더링 전용 번역 단위.
//
// 계산 코어(archetype.cpp)와 화면 출력을 분리한다. 테스트 타깃은 이 TU를
// 컴파일하지 않으므로 raylib 없이 빌드된다.
#include "sim/archetype.h"

#include "raylib.h"

namespace ppl {

void ArchetypeSimulation::Draw() const {
    for (const auto& chunkPtr : chunks_) {
        const Chunk& c = *chunkPtr;
        for (std::size_t i = 0; i < c.size; ++i) {
            DrawPixel(static_cast<int>(c.posX[i]),
                      static_cast<int>(c.posY[i]),
                      WHITE);
        }
    }
}

}  // namespace ppl
