// headless 테스트 타깃 전용 렌더링 심(seam).
//
// Draw()는 ISimulation의 가상 함수라 vtable에 주소가 들어간다. 그래서 계산 코어만
// 링크해도 정의가 하나는 있어야 링크가 끝난다. 앱 타깃은 raylib을 쓰는
// src/sim/*_draw.cpp 를 링크하고, 테스트 타깃은 이 파일을 링크한다.
// 덕분에 테스트 바이너리에는 raylib / GLFW / OpenGL 심볼이 하나도 들어가지 않는다.
//
// 테스트는 Draw()를 호출하지 않는다. 검증 대상은 Update와 CopyState다.
#include "sim/aos.h"
#include "sim/archetype.h"
#include "sim/soa.h"

namespace ppl {

void SoASimulation::Draw() const {}
void AoSSimulation::Draw() const {}
void ArchetypeSimulation::Draw() const {}

}  // namespace ppl
