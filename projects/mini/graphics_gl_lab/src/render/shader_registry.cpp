#include "render/shader_registry.h"

namespace gllab {

const char* ShadingModelName(ShadingModel s) {
    switch (s) {
        case ShadingModel::Lambert:    return "Lambert";
        case ShadingModel::BlinnPhong: return "Blinn-Phong";
        case ShadingModel::Cel:         return "Cel";
    }
    return "?";
}

const char* ShaderStyleName(ShaderStyle s) {
    switch (s) {
        case ShaderStyle::Uber:              return "Uber (runtime branch)";
        case ShaderStyle::StaticPermutation: return "Static permutation";
    }
    return "?";
}

void ShaderRegistry::Init() {
    uber_ = std::make_unique<ShaderProgram>();
    uber_->LoadFromFiles("shaders/common.vert", "shaders/uber.frag");

    permLambert_ = std::make_unique<ShaderProgram>();
    permLambert_->LoadFromFiles("shaders/common.vert", "shaders/perm_lambert.frag");

    permBlinn_ = std::make_unique<ShaderProgram>();
    permBlinn_->LoadFromFiles("shaders/common.vert", "shaders/perm_blinn_phong.frag");

    permCel_ = std::make_unique<ShaderProgram>();
    permCel_->LoadFromFiles("shaders/common.vert", "shaders/perm_cel.frag");

    litPerCall_ = std::make_unique<ShaderProgram>();
    litPerCall_->LoadFromFiles("shaders/common.vert", "shaders/lit_per_call.frag");

    litUbo_ = std::make_unique<ShaderProgram>();
    litUbo_->LoadFromFiles("shaders/common.vert", "shaders/lit_ubo.frag");

    litSsbo_ = std::make_unique<ShaderProgram>();
    litSsbo_->LoadFromFiles("shaders/common.vert", "shaders/lit_ssbo.frag");

    textured_ = std::make_unique<ShaderProgram>();
    textured_->LoadFromFiles("shaders/common.vert", "shaders/textured.frag");
}

ShaderProgram& ShaderRegistry::Textured() {
    return *textured_;
}

ShaderProgram& ShaderRegistry::PickLit(BindingMode mode) {
    switch (mode) {
        case BindingMode::PerCallUniform: return *litPerCall_;
        case BindingMode::UBO:            return *litUbo_;
        case BindingMode::SSBO:           return *litSsbo_;
    }
    return *litPerCall_;
}

ShaderProgram& ShaderRegistry::Pick(ShaderStyle style, ShadingModel model) {
    if (style == ShaderStyle::Uber) {
        return *uber_;
    }
    switch (model) {
        case ShadingModel::Lambert:    return *permLambert_;
        case ShadingModel::BlinnPhong: return *permBlinn_;
        case ShadingModel::Cel:        return *permCel_;
    }
    return *permLambert_;
}

bool ShaderRegistry::ProgramChangedSinceLast(ShaderProgram& prog) {
    GLuint p = prog.Program();
    bool changed = (p != lastBound_);
    lastBound_ = p;
    return changed;
}

}  // namespace gllab
