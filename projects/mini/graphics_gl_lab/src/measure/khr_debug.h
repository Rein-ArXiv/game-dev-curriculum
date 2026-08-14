#pragma once

// KHRDebug — `GL_KHR_debug` 콜백을 받아 메시지를 *원형 버퍼* 에 누적.
//
// 디버그 빌드의 `GL_DEBUG_OUTPUT` 활성 시 드라이버가 *에러 / 경고 / 성능 힌트
// / undefined behavior* 메시지를 보낸다. 이 클래스가 그것을 받아 ImGui 로그
// 창에 표시.

#include <glad/gl.h>

#include <array>
#include <string>

namespace gllab {

struct DebugMessage {
    enum class Severity { Notification, Low, Medium, High };
    GLenum   source;
    GLenum   type;
    GLuint   id;
    Severity severity;
    std::string text;
};

class KHRDebug {
public:
    static constexpr int kRingSize = 256;

    void Install();
    void Clear();

    // 앱이 직접 남기는 메시지 (드라이버 콜백과 같은 로그 창에 표시).
    void PushAppMessage(const std::string& text,
                        DebugMessage::Severity severity = DebugMessage::Severity::Medium);

    int Count() const { return count_; }
    int RingHead() const { return head_; }
    const DebugMessage& Get(int i) const { return ring_[(size_t)i]; }

private:
    static void GLAPIENTRY Callback(GLenum source, GLenum type, GLuint id,
                                     GLenum severity, GLsizei length,
                                     const GLchar* message, const void* userParam);
    void Push(const DebugMessage& m);

    std::array<DebugMessage, kRingSize> ring_{};
    int head_  = 0;
    int count_ = 0;
};

}  // namespace gllab
