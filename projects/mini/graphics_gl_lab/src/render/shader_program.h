#pragma once

// ShaderProgram — 정점 / 프래그먼트 셰이더 컴파일 + 링크 + 유니폼 설정.
//
// 다단계 셰이더 (geometry / tess / compute) 는 Step 3+ 에서 필요할 때 확장.
// Uniform location 캐싱은 호출 빈도가 높은 측정 코드에서 *문자열 → location 검색*
// 비용을 회피하기 위함.

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <string>
#include <unordered_map>

namespace gllab {

class ShaderProgram {
public:
    ShaderProgram();
    ~ShaderProgram();

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    void LoadFromFiles(const std::string& vertPath, const std::string& fragPath);
    void LoadFromSources(const std::string& vertSrc, const std::string& fragSrc);

    void Use() const;
    GLuint Program() const { return program_; }

    // 반환값 — *실제로 glUniform 을 호출했는지*. 현재 프로그램에 없는 uniform 은
    // location 이 -1 이라 호출 자체가 생략되므로 false. 카운터는 이 결과로만
    // 증가시켜야 '유니폼 업데이트 수 = 실제 GL 호출 수' 가 유지된다.
    bool SetMat4(const char* name, const glm::mat4& m);
    bool SetVec3(const char* name, const glm::vec3& v);
    bool SetFloat(const char* name, float v);
    bool SetInt(const char* name, int v);

private:
    GLuint Compile(GLenum type, const std::string& src);
    GLint  Uniform(const char* name);

    GLuint program_ = 0;
    std::unordered_map<std::string, GLint> uniformCache_;
};

}  // namespace gllab
