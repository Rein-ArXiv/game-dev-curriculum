#pragma once

// Scene — 오브젝트 격자 씬 (개수는 Build / Rebuild 인자, 앱 기본값 1만)
//         + 드로우콜 모드 토글.
//
// Step 1: 개별 glDrawElements N 회만 동작.
// Step 3 추가: Instancing (`glDrawElementsInstanced` 한 번) + Multi-Draw Indirect
//              (`glMultiDrawElementsIndirect`) 두 모드. 같은 시각 결과 → 다른
//              CPU/GPU 비용 분포.

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <vector>
#include <cstdint>

namespace gllab {

class ShaderProgram;
struct GlCounters;

enum class DrawMode {
    Individual,        // glDrawElements × N
    Instanced,         // glDrawElementsInstanced × 1
    MultiDrawIndirect, // glMultiDrawElementsIndirect × 1
};

const char* DrawModeName(DrawMode m);

inline int ExpectedDrawSubmissions(DrawMode mode, int objectCount) {
    if (objectCount <= 0) return 0;
    return mode == DrawMode::Individual ? objectCount : 1;
}

struct ObjectTransform {
    glm::vec3 position;
    glm::vec3 axis;
    float     angle;
    float     angularSpeed;
    glm::vec3 color;
};

// glMultiDrawElementsIndirect 명령 구조체 (spec 정의 그대로).
struct DrawElementsIndirectCommand {
    uint32_t count;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t  baseVertex;
    uint32_t baseInstance;
};

// MDI 커맨드 배열을 만드는 순수 함수 — GL 호출 없음. UploadIndirectBuffer 가
// 이 결과를 그대로 GPU 로 올린다. 테스트는 GL 컨텍스트 없이 이 함수를 부른다.
std::vector<DrawElementsIndirectCommand> BuildIndirectCommands(int objectCount,
                                                               GLsizei indexCount);

// 카메라/투영 행렬을 만드는 순수 함수 — GL 호출 없음. Scene::Draw 가 이 결과를
// uView / uProj / uCameraPos 로 올린다.
struct CameraMatrices {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec3 eye;
};

CameraMatrices ComputeCamera(int objectCount, float aspectRatio);

// Scene 이 소유한 GL 핸들 스냅숏 — 자원 수명 테스트용 읽기 전용 진입점.
struct SceneHandles {
    GLuint vao;
    GLuint vboVerts;
    GLuint vboIndices;
    GLuint vboInstanceModel;
    GLuint vboInstanceColor;
    GLuint indirectBuffer;
};

class Scene {
public:
    Scene();
    ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    void Build(int objectCount);
    void Rebuild(int objectCount);     // 기존 핸들 해제 + Build 재호출.
    void Update(float dt);
    void Draw(ShaderProgram& shader, float aspectRatio, GlCounters& counters);

    int  ObjectCount() const { return (int)objects_.size(); }
    int  LastDrawCalls() const { return lastDrawCalls_; }
    GLsizei IndexCount() const { return indexCount_; }

    SceneHandles Handles() const {
        return {vao_, vboVerts_, vboIndices_,
                vboInstanceModel_, vboInstanceColor_, indirectBuffer_};
    }

    void SetDrawMode(DrawMode m);
    DrawMode GetDrawMode() const { return mode_; }

private:
    void UploadCubeMesh();
    void UploadInstanceBuffers();   // per-instance model matrix + color.
    void UploadIndirectBuffer();    // MDI command buffer.

    void UpdateInstanceData();      // 매 프레임 model matrix + color 패킹.

    GLuint vao_         = 0;
    GLuint vboVerts_    = 0;
    GLuint vboIndices_  = 0;
    GLsizei indexCount_ = 0;

    // Instancing / MDI 용.
    GLuint vboInstanceModel_ = 0;  // mat4 × N
    GLuint vboInstanceColor_ = 0;  // vec3 × N (vec4 정렬)
    GLuint indirectBuffer_   = 0;  // DrawElementsIndirectCommand × N

    std::vector<glm::mat4>           instanceModels_;
    std::vector<glm::vec4>           instanceColors_;

    DrawMode mode_ = DrawMode::Individual;

    std::vector<ObjectTransform> objects_;
    int lastDrawCalls_ = 0;
};

}  // namespace gllab
