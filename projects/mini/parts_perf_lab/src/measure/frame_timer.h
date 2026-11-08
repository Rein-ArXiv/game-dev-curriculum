#pragma once

#include <array>
#include <chrono>
#include <cstddef>

namespace ppl {

// 최근 N 프레임의 frame/sim/draw-submit/alloc/container 시간을 슬라이딩 윈도우로
// 보관한다. HUD가 이 데이터를 그래프로 표시한다.
//
// 구간 정의(서로 다른 지표를 같은 이름으로 부르지 않는다):
//   - frame wall      : BeginFrame()부터 EndFrame()까지. HUD와 EndDrawing(flush/swap)을
//                       포함하지만 GPU 실행 시간이나 전체 프로그램 CPU 시간은 아니다.
//   - sim             : 시뮬레이션 Update 호출 구간.
//   - draw-submit     : BeginDrawing/Clear/점 그리기 호출 구간. HUD와 EndDrawing 제외.
//                       일부 CPU 렌더 명령 발행 비용이며 GPU 완료 시간이 아니다.
//   - alloc/container : 각 벤치마크가 자체 측정해 CommitFrame으로 넘기는 외부 값.
class FrameTimer {
public:
    static constexpr std::size_t kCapacity = 600;

    // 프레임 전체 wall 구간. BeginFrame은 루프 선두, EndFrame은 EndDrawing 뒤에 둔다.
    void BeginFrame();
    void EndFrame();

    void BeginUpdate();
    void EndUpdate();

    void BeginDraw();
    void EndDraw();

    // 호출 직전에 EndFrame, BeginUpdate/EndUpdate, BeginDraw/EndDraw가 모두
    // 호출되었다고 가정한다. 한 프레임의 측정값과 별도 채널 ms를 링 버퍼에 push한다.
    void CommitFrame(float allocMs = 0.0f, float containerMs = 0.0f);

    // 최근 N 프레임 ms 배열. ring buffer라 head 위치도 함께 반환한다.
    const std::array<float, kCapacity>& FrameWallMs() const { return frameWallMs_; }
    const std::array<float, kCapacity>& UpdateMs()    const { return updateMs_; }
    const std::array<float, kCapacity>& DrawMs()      const { return drawMs_; }
    const std::array<float, kCapacity>& AllocMs()     const { return allocMs_; }
    const std::array<float, kCapacity>& ContainerMs() const { return containerMs_; }
    std::size_t Head()   const { return head_; }
    std::size_t Filled() const { return filled_; }

    float LastFrameWallMs()      const { return lastFrameWallMs_; }
    float LastUpdateMs()         const { return lastUpdateMs_; }
    float LastDrawSubmitMs()     const { return lastDrawMs_; }
    // sim + draw-submit. 전체 CPU 프레임이 아니므로 옛 이름(LastTotalMs)을 쓰지 않는다.
    float LastSimDrawSubmitMs()  const { return lastUpdateMs_ + lastDrawMs_; }
    float LastAllocMs()          const { return lastAllocMs_; }
    float LastContainerMs()      const { return lastContainerMs_; }

private:
    using Clock = std::chrono::steady_clock;
    using Time  = Clock::time_point;

    Time frameStart_{};
    Time updateStart_{};
    Time drawStart_{};

    std::array<float, kCapacity> frameWallMs_{};
    std::array<float, kCapacity> updateMs_{};
    std::array<float, kCapacity> drawMs_{};
    std::array<float, kCapacity> allocMs_{};
    std::array<float, kCapacity> containerMs_{};
    std::size_t head_ = 0;
    std::size_t filled_ = 0;

    float lastFrameWallMs_ = 0.0f;
    float lastUpdateMs_    = 0.0f;
    float lastDrawMs_      = 0.0f;
    float lastAllocMs_     = 0.0f;
    float lastContainerMs_ = 0.0f;
};

}  // namespace ppl
