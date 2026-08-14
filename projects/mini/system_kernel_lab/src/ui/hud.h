#pragma once

#include <cstddef>

namespace skl {

class FrameTimer;
class FileIoWorkload;
struct Toggles;

class Hud {
public:
    void Init(int windowW, int windowH);

    // ImGui::NewFrame() ~ ImGui::Render() 사이에서 호출.
    // 3-zone: 좌측 stats / 중앙 토글 패널 / 우측 두 그래프.
    // toggles는 in-out — 사용자 선택이 즉시 갱신된다.
    void Draw(Toggles& toggles,
              const FrameTimer& timer,
              const FileIoWorkload& workload);

private:
    int windowW_ = 0;
    int windowH_ = 0;
};

}  // namespace skl
