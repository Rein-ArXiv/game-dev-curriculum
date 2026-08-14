// input — 액션 기반 입력 매핑. 하드코딩된 키 대신 추상 액션으로.
//
// 물리 키/버튼을 "점프", "사격" 같은 추상 액션에 바인딩한다. 게임 로직은 키가
// 아니라 액션을 묻는다 — 리매핑·컨트롤러·컨텍스트 전환이 데이터로 끝난다. 아날로그
// 축은 데드존으로 미세 드리프트를 0으로 만든다(engine 입력 챕터).

#pragma once

#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace mega {
namespace engine {

class InputMap {
public:
    void Bind(int key, const std::string& action) { bindings_[key] = action; }
    void Unbind(int key) { bindings_.erase(key); }

    // 프레임 시작에서 edge 상태만 비운다. held 상태는 유지한다.
    void BeginFrame() {
        pressed_.clear();
        released_.clear();
    }

    // 물리 키 상태 갱신. 바인딩된 액션의 활성 여부가 따라 바뀐다.
    void OnKey(int key, bool down) {
        auto it = bindings_.find(key);
        if (it == bindings_.end()) return;
        const bool wasDown = active_.count(it->second) != 0;
        if (down) {
            active_.insert(it->second);
            if (!wasDown) pressed_.insert(it->second);
        } else {
            active_.erase(it->second);
            if (wasDown) released_.insert(it->second);
        }
    }

    bool IsActive(const std::string& action) const { return active_.count(action) != 0; }
    bool WasPressed(const std::string& action) const { return pressed_.count(action) != 0; }
    bool WasReleased(const std::string& action) const { return released_.count(action) != 0; }

    // 아날로그 데드존: |v| < dz 면 0, 아니면 [dz,1]→[0,1] 재매핑(부호 유지).
    static float ApplyDeadzone(float v, float dz) {
        float a = std::fabs(v);
        if (a < dz) return 0.0f;
        float sign = v < 0 ? -1.0f : 1.0f;
        return sign * (a - dz) / (1.0f - dz);
    }

    void Clear() {
        active_.clear();
        pressed_.clear();
        released_.clear();
    }

private:
    std::unordered_map<int, std::string> bindings_;
    std::unordered_set<std::string>      active_;
    std::unordered_set<std::string>      pressed_;
    std::unordered_set<std::string>      released_;
};

}  // namespace engine
}  // namespace mega
