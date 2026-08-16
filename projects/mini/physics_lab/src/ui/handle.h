// 마우스로 끌 수 있는 핸들 (점). physics_lab 의 거의 모든 Scene 이
// 이 위젯으로 입력을 받는다 — 강체 시작 위치, 외력 인가점, 제약 anchor 등.
//
// Capture 정책: *프로세스 전체에 한 번에 하나의 핸들만 잡힌다*. 같은 좌표에
// 여러 핸들이 겹쳐도 *클릭 시점에 마우스에서 가장 가까운 한 개만* 활성화.
// 구현은 모든 핸들이 공유하는 static 한 상태 (g_activeId) 로
// "지금 잡혀 있는 핸들의 id" 를 추적하고, 새 클릭이 들어오면
// EndFrame() 이 그 프레임의 *가장 가까운 후보* 로 한 개만 선택.
//
// 사용 흐름:
//   DragHandle::BeginFrame();        // 매 프레임 시작 시 한 번
//   handleA.Update();                // 후보로 등록 + 잡혀 있으면 따라옴
//   handleB.Update();
//   DragHandle::EndFrame();          // 후보들 중 가장 가까운 하나만 잡기 시작
//
// 위 순서를 지키면 핸들 두 개가 겹쳐도 *하나만* 잡힌다.

#pragma once

#include "raylib.h"

#include <cstdint>

namespace physlab {

class DragHandle {
public:
    DragHandle();
    explicit DragHandle(Vector2 initial);

    void SetPosition(Vector2 p) { pos_ = p; }
    Vector2 Position() const   { return pos_; }
    bool    IsDragging() const;

    // 매 프레임 호출. 잡혀 있으면 마우스 따라옴 + 놓이면 release.
    // 잡혀 있지 않으면 *가장 가까운 후보* 결정에 참여 (BeginFrame/EndFrame 안에서).
    void Update();

    // 핸들을 화면에 그린다. 라벨은 nullptr 이면 생략.
    void Draw(Color fill, const char* label = nullptr) const;

    // 반경 변경 (히트테스트와 시각 모두에 영향).
    void SetRadius(float r) { radius_ = r; }

    // 매 프레임 시작 / 끝. main 또는 Scene 이 호출.
    static void BeginFrame();
    static void EndFrame();

private:
    Vector2  pos_      = {0, 0};
    float    radius_   = 7.0f;
    uint32_t id_       = 0;     // 핸들 별 고유 id
};

}  // namespace physlab
