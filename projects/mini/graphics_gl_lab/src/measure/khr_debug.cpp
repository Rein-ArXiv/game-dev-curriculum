#include "measure/khr_debug.h"

namespace gllab {

namespace {

DebugMessage::Severity ToSeverity(GLenum s) {
    switch (s) {
        case GL_DEBUG_SEVERITY_HIGH:         return DebugMessage::Severity::High;
        case GL_DEBUG_SEVERITY_MEDIUM:       return DebugMessage::Severity::Medium;
        case GL_DEBUG_SEVERITY_LOW:          return DebugMessage::Severity::Low;
        case GL_DEBUG_SEVERITY_NOTIFICATION:
        default:                              return DebugMessage::Severity::Notification;
    }
}

}  // namespace

void KHRDebug::Install() {
    if (GLAD_GL_VERSION_4_3 && glDebugMessageCallback && glDebugMessageControl) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(&KHRDebug::Callback, this);
        // 모든 메시지 활성. NOTIFICATION 은 너무 시끄러우면 필터링 가능.
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE,
                              0, nullptr, GL_TRUE);
    }
}

void KHRDebug::Clear() {
    head_  = 0;
    count_ = 0;
}

void KHRDebug::PushAppMessage(const std::string& text,
                              DebugMessage::Severity severity) {
    DebugMessage m;
    m.source   = GL_DEBUG_SOURCE_APPLICATION;
    m.type     = GL_DEBUG_TYPE_OTHER;
    m.id       = 0;
    m.severity = severity;
    m.text     = text;
    Push(m);
}

void KHRDebug::Push(const DebugMessage& m) {
    ring_[(size_t)head_] = m;
    head_ = (head_ + 1) % kRingSize;
    if (count_ < kRingSize) ++count_;
}

void GLAPIENTRY KHRDebug::Callback(GLenum source, GLenum type, GLuint id,
                                    GLenum severity, GLsizei length,
                                    const GLchar* message, const void* userParam) {
    (void)length;
    auto* self = const_cast<KHRDebug*>(reinterpret_cast<const KHRDebug*>(userParam));
    DebugMessage m;
    m.source   = source;
    m.type     = type;
    m.id       = id;
    m.severity = ToSeverity(severity);
    m.text     = message ? message : "";
    self->Push(m);
}

}  // namespace gllab
