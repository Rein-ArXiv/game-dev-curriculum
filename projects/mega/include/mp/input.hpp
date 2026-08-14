// input — 한 플레이어의 한 프레임 입력. 결정적 시뮬의 유일한 외부 입력.
//
// lockstep/rollback 은 *상태가 아니라 입력을 교환*한다. 같은 입력 열이 같은 상태를
// 만든다는 결정성 보장이 전제다. 입력은 작게(여기선 3바이트) 두어 대역폭을 아끼고,
// trivially copyable 이라 바이너리 직렬화로 그대로 전송된다.

#pragma once

#include <cstdint>

namespace mega {
namespace mp {

struct Input {
    int8_t moveX = 0;     // -1, 0, +1
    int8_t moveY = 0;
    uint8_t buttons = 0;  // 비트 플래그(fire 등)

    bool Fire() const { return buttons & 0x1; }
    void SetFire(bool v) { buttons = (uint8_t)((buttons & ~0x1) | (v ? 0x1 : 0)); }

    bool operator==(const Input& o) const { return moveX == o.moveX && moveY == o.moveY && buttons == o.buttons; }
    bool operator!=(const Input& o) const { return !(*this == o); }

    template <class Ar>
    void Serialize(Ar& ar) { ar(moveX, moveY, buttons); }
};

}  // namespace mp
}  // namespace mega
