#include "scene/scene.h"

#include "measure/gl_counters.h"
#include "render/shader_program.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <random>

namespace gllab {

const char* DrawModeName(DrawMode m) {
    switch (m) {
        case DrawMode::Individual:        return "Individual (N draws)";
        case DrawMode::Instanced:         return "Instanced (1 draw)";
        case DrawMode::MultiDrawIndirect: return "Multi-Draw Indirect (1 draw)";
    }
    return "?";
}

namespace {

// Position(vec3) + Normal(vec3) — 24 B / 정점.
constexpr float kCubeVertices[] = {
    // -X face
    -0.5f,-0.5f,-0.5f,  -1, 0, 0,
    -0.5f, 0.5f,-0.5f,  -1, 0, 0,
    -0.5f, 0.5f, 0.5f,  -1, 0, 0,
    -0.5f,-0.5f, 0.5f,  -1, 0, 0,
    // +X
     0.5f,-0.5f,-0.5f,   1, 0, 0,
     0.5f, 0.5f,-0.5f,   1, 0, 0,
     0.5f, 0.5f, 0.5f,   1, 0, 0,
     0.5f,-0.5f, 0.5f,   1, 0, 0,
    // -Y
    -0.5f,-0.5f,-0.5f,   0,-1, 0,
     0.5f,-0.5f,-0.5f,   0,-1, 0,
     0.5f,-0.5f, 0.5f,   0,-1, 0,
    -0.5f,-0.5f, 0.5f,   0,-1, 0,
    // +Y
    -0.5f, 0.5f,-0.5f,   0, 1, 0,
     0.5f, 0.5f,-0.5f,   0, 1, 0,
     0.5f, 0.5f, 0.5f,   0, 1, 0,
    -0.5f, 0.5f, 0.5f,   0, 1, 0,
    // -Z
    -0.5f,-0.5f,-0.5f,   0, 0,-1,
     0.5f,-0.5f,-0.5f,   0, 0,-1,
     0.5f, 0.5f,-0.5f,   0, 0,-1,
    -0.5f, 0.5f,-0.5f,   0, 0,-1,
    // +Z
    -0.5f,-0.5f, 0.5f,   0, 0, 1,
     0.5f,-0.5f, 0.5f,   0, 0, 1,
     0.5f, 0.5f, 0.5f,   0, 0, 1,
    -0.5f, 0.5f, 0.5f,   0, 0, 1,
};

constexpr uint32_t kCubeIndices[] = {
     0, 2, 1,    0, 3, 2,     // -X
     4, 5, 6,    4, 6, 7,     // +X
     8, 9,10,    8,10,11,     // -Y
    12,14,13,   12,15,14,     // +Y
    16,18,17,   16,19,18,     // -Z
    20,21,22,   20,22,23,     // +Z
};

}  // namespace

std::vector<DrawElementsIndirectCommand> BuildIndirectCommands(int objectCount,
                                                               GLsizei indexCount) {
    // MDI 의 경우 *각 오브젝트별로 1 command* — instanceCount=1, baseInstance=i.
    // (실용적으로는 instanced + MDI 조합이 더 흔하지만 학습 단계엔 이쪽이 명확.)
    const int count = objectCount > 0 ? objectCount : 0;
    std::vector<DrawElementsIndirectCommand> cmds((size_t)count);
    for (size_t i = 0; i < cmds.size(); ++i) {
        cmds[i].count         = (uint32_t)indexCount;
        cmds[i].instanceCount = 1;
        cmds[i].firstIndex    = 0;
        cmds[i].baseVertex    = 0;
        cmds[i].baseInstance  = (uint32_t)i;
    }
    return cmds;
}

CameraMatrices ComputeCamera(int objectCount, float aspectRatio) {
    int side = (int)std::ceil(std::cbrt((double)objectCount));
    float gridExtent = 2.5f * (float)side;
    float camDist    = gridExtent * 1.2f + 5.0f;

    CameraMatrices cam;
    cam.eye  = glm::vec3(camDist * 0.7f, camDist * 0.5f, camDist * 0.7f);
    cam.view = glm::lookAt(
        cam.eye,
        glm::vec3(0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    cam.proj = glm::perspective(glm::radians(45.0f), aspectRatio,
                                 0.1f, camDist * 4.0f);
    return cam;
}

Scene::Scene() {}

Scene::~Scene() {
    if (vao_)              glDeleteVertexArrays(1, &vao_);
    if (vboVerts_)         glDeleteBuffers(1, &vboVerts_);
    if (vboIndices_)       glDeleteBuffers(1, &vboIndices_);
    if (vboInstanceModel_) glDeleteBuffers(1, &vboInstanceModel_);
    if (vboInstanceColor_) glDeleteBuffers(1, &vboInstanceColor_);
    if (indirectBuffer_)   glDeleteBuffers(1, &indirectBuffer_);
}

void Scene::Rebuild(int objectCount) {
    if (vao_)              { glDeleteVertexArrays(1, &vao_);    vao_ = 0; }
    if (vboVerts_)         { glDeleteBuffers(1, &vboVerts_);    vboVerts_ = 0; }
    if (vboIndices_)       { glDeleteBuffers(1, &vboIndices_);  vboIndices_ = 0; }
    if (vboInstanceModel_) { glDeleteBuffers(1, &vboInstanceModel_); vboInstanceModel_ = 0; }
    if (vboInstanceColor_) { glDeleteBuffers(1, &vboInstanceColor_); vboInstanceColor_ = 0; }
    if (indirectBuffer_)   { glDeleteBuffers(1, &indirectBuffer_);   indirectBuffer_ = 0; }
    Build(objectCount);
}

void Scene::UploadCubeMesh() {
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    glGenBuffers(1, &vboVerts_);
    glBindBuffer(GL_ARRAY_BUFFER, vboVerts_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kCubeVertices), kCubeVertices, GL_STATIC_DRAW);

    // location 0: pos
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    // location 1: normal
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void*)(3 * sizeof(float)));

    glGenBuffers(1, &vboIndices_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vboIndices_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kCubeIndices), kCubeIndices,
                 GL_STATIC_DRAW);

    indexCount_ = sizeof(kCubeIndices) / sizeof(kCubeIndices[0]);

    glBindVertexArray(0);
}

void Scene::UploadInstanceBuffers() {
    glBindVertexArray(vao_);

    // location 2~5: per-instance model matrix (4 vec4).
    glGenBuffers(1, &vboInstanceModel_);
    glBindBuffer(GL_ARRAY_BUFFER, vboInstanceModel_);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(sizeof(glm::mat4) * objects_.size()),
                 nullptr, GL_DYNAMIC_DRAW);

    const size_t vec4Size = sizeof(glm::vec4);
    for (int i = 0; i < 4; ++i) {
        GLuint loc = (GLuint)(2 + i);
        glEnableVertexAttribArray(loc);
        glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE,
                              (GLsizei)(4 * vec4Size),
                              (void*)(uintptr_t)(i * vec4Size));
        glVertexAttribDivisor(loc, 1);     // per-instance.
    }

    // location 6: per-instance color (vec4).
    glGenBuffers(1, &vboInstanceColor_);
    glBindBuffer(GL_ARRAY_BUFFER, vboInstanceColor_);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(sizeof(glm::vec4) * objects_.size()),
                 nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, (GLsizei)vec4Size, (void*)0);
    glVertexAttribDivisor(6, 1);

    glBindVertexArray(0);
}

void Scene::UploadIndirectBuffer() {
    const std::vector<DrawElementsIndirectCommand> cmds =
        BuildIndirectCommands((int)objects_.size(), indexCount_);
    glGenBuffers(1, &indirectBuffer_);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirectBuffer_);
    glBufferData(GL_DRAW_INDIRECT_BUFFER,
                 (GLsizeiptr)(sizeof(DrawElementsIndirectCommand) * cmds.size()),
                 cmds.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
}

void Scene::Build(int objectCount) {
    UploadCubeMesh();

    objects_.clear();
    objects_.reserve((size_t)objectCount);

    int side = (int)std::ceil(std::cbrt((double)objectCount));
    float spacing = 2.5f;
    float origin  = -0.5f * spacing * (float)(side - 1);

    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> uColor(0.3f, 1.0f);
    std::uniform_real_distribution<float> uAxis(-1.0f, 1.0f);
    std::uniform_real_distribution<float> uSpeed(0.3f, 1.5f);

    int placed = 0;
    for (int z = 0; z < side && placed < objectCount; ++z) {
        for (int y = 0; y < side && placed < objectCount; ++y) {
            for (int x = 0; x < side && placed < objectCount; ++x) {
                ObjectTransform o;
                o.position = glm::vec3(origin + x * spacing,
                                        origin + y * spacing,
                                        origin + z * spacing);
                glm::vec3 axis(uAxis(rng), uAxis(rng), uAxis(rng));
                float len = glm::length(axis);
                if (len < 1e-4f) { axis = glm::vec3(0, 1, 0); len = 1; }
                o.axis = axis / len;
                o.angle = 0.0f;
                o.angularSpeed = uSpeed(rng);
                o.color = glm::vec3(uColor(rng), uColor(rng), uColor(rng));
                objects_.push_back(o);
                ++placed;
            }
        }
    }

    instanceModels_.resize(objects_.size());
    instanceColors_.resize(objects_.size());

    UploadInstanceBuffers();
    UploadIndirectBuffer();
}

void Scene::Update(float dt) {
    for (auto& o : objects_) {
        o.angle += o.angularSpeed * dt;
    }
}

void Scene::SetDrawMode(DrawMode m) {
    mode_ = m;
}

void Scene::UpdateInstanceData() {
    for (size_t i = 0; i < objects_.size(); ++i) {
        const auto& o = objects_[i];
        instanceModels_[i] = glm::translate(glm::mat4(1.0f), o.position)
                           * glm::rotate(glm::mat4(1.0f), o.angle, o.axis);
        instanceColors_[i] = glm::vec4(o.color, 1.0f);
    }
    glBindBuffer(GL_ARRAY_BUFFER, vboInstanceModel_);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(sizeof(glm::mat4) * instanceModels_.size()),
                    instanceModels_.data());
    glBindBuffer(GL_ARRAY_BUFFER, vboInstanceColor_);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(sizeof(glm::vec4) * instanceColors_.size()),
                    instanceColors_.data());
}

void Scene::Draw(ShaderProgram& shader, float aspectRatio, GlCounters& counters) {
    const CameraMatrices cam = ComputeCamera((int)objects_.size(), aspectRatio);

    // 현재 프로그램에 없는 uniform (예: lit_* 에는 uLightDir/uCameraPos 가 없다) 은
    // glUniform 호출 자체가 생기지 않으므로 계수하지 않는다.
    if (shader.SetMat4("uView", cam.view)) counters.uniformUpdates++;
    if (shader.SetMat4("uProj", cam.proj)) counters.uniformUpdates++;
    if (shader.SetVec3("uLightDir", glm::normalize(glm::vec3(0.4f, 0.8f, 0.2f))))
        counters.uniformUpdates++;
    // uCameraPos 는 Blinn-Phong / uber 에만 존재.
    if (shader.SetVec3("uCameraPos", cam.eye)) counters.uniformUpdates++;

    glBindVertexArray(vao_);
    counters.stateChanges++;

    lastDrawCalls_ = 0;

    switch (mode_) {
    case DrawMode::Individual: {
        // Uber 가 instance attr 도 정의하므로 location 2~6 를 disable 하지 않아도
        // 정점 셰이더가 무시할 수 있도록 *cookie* 가 필요 — 학습 단계엔 같은 셰이더로
        // 두 경로 모두 처리한다 (셰이더 안에서 uModel/uColor uniform 과 instance attr
        // 둘 다 받음 + uniform 이 우선).
        if (shader.SetInt("uUseInstancing", 0)) counters.uniformUpdates++;
        for (const auto& o : objects_) {
            glm::mat4 model = glm::translate(glm::mat4(1.0f), o.position)
                            * glm::rotate(glm::mat4(1.0f), o.angle, o.axis);
            if (shader.SetMat4("uModel", model))   counters.uniformUpdates++;
            if (shader.SetVec3("uColor", o.color)) counters.uniformUpdates++;
            glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
            ++lastDrawCalls_;
            counters.drawCalls++;
        }
    } break;

    case DrawMode::Instanced: {
        if (objects_.empty()) break;    // 빈 씬은 제출 0 (ExpectedDrawSubmissions 와 일치).
        UpdateInstanceData();
        if (shader.SetInt("uUseInstancing", 1)) counters.uniformUpdates++;
        glDrawElementsInstanced(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT,
                                 nullptr, (GLsizei)objects_.size());
        lastDrawCalls_ = 1;
        counters.instancedDraws++;
    } break;

    case DrawMode::MultiDrawIndirect: {
        if (objects_.empty()) break;    // 빈 씬은 제출 0 — 0 바이트 indirect 버퍼로 드로우하지 않는다.
        UpdateInstanceData();
        if (shader.SetInt("uUseInstancing", 1)) counters.uniformUpdates++;
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirectBuffer_);
        counters.stateChanges++;
        glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT,
                                     nullptr,
                                     (GLsizei)objects_.size(),
                                     sizeof(DrawElementsIndirectCommand));
        lastDrawCalls_ = 1;
        counters.indirectDraws++;
    } break;
    }

    glBindVertexArray(0);
}

}  // namespace gllab
