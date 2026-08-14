#pragma once

// ShaderRegistry — Step 4 의 셰이더 perm 토글용 프로그램 묶음.
//
// 두 카테고리:
//   - Uber : 단일 frag (uber.frag) + runtime uniform branch.
//   - Static permutation : 각 모델 (lambert/blinn-phong/cel) 마다 별도 frag.
//
// 측정 — uber 의 *runtime branch 비용* vs static 의 *프로그램 전환 비용*.
// 보통 frag stage 자체는 static 이 약간 빠르지만, *프로그램 전환이 잦으면* uber
// 가 유리. 게임 엔진의 일반 패턴은 *공유 영역은 uber + 분기가 큰 영역은 perm*.

#include "render/shader_program.h"
#include "render/lighting_bindings.h"

#include <memory>

namespace gllab {

enum class ShadingModel {
    Lambert = 0,
    BlinnPhong = 1,
    Cel = 2,
};

enum class ShaderStyle {
    Uber,                // 단일 프로그램 + uniform 분기.
    StaticPermutation,   // 모델당 별도 프로그램.
};

const char* ShadingModelName(ShadingModel s);
const char* ShaderStyleName(ShaderStyle s);

class ShaderRegistry {
public:
    void Init();

    // Step 4 셰이더 — 단일 광원 (uLightDir) 사용.
    ShaderProgram& Pick(ShaderStyle style, ShadingModel model);

    // Step 5 셰이더 — 광원 배열을 binding mode 에 맞게 사용.
    ShaderProgram& PickLit(BindingMode mode);

    // Step 6 셰이더 — 디퓨즈 텍스처 + Lambert.
    ShaderProgram& Textured();

    // 마지막 사용 프로그램의 변경 여부 추적.
    bool ProgramChangedSinceLast(ShaderProgram& prog);

private:
    std::unique_ptr<ShaderProgram> uber_;
    std::unique_ptr<ShaderProgram> permLambert_;
    std::unique_ptr<ShaderProgram> permBlinn_;
    std::unique_ptr<ShaderProgram> permCel_;

    std::unique_ptr<ShaderProgram> litPerCall_;
    std::unique_ptr<ShaderProgram> litUbo_;
    std::unique_ptr<ShaderProgram> litSsbo_;

    std::unique_ptr<ShaderProgram> textured_;

    GLuint lastBound_ = 0;
};

}  // namespace gllab
