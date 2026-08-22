// input — 한 플레이어의 한 프레임 입력. 결정적 서브시뮬의 유일한 외부 입력.
//
// lockstep/rollback 은 *상태가 아니라 입력을 교환*한다. 같은 입력 열이 같은 상태를
// 만든다는 결정성 보장이 전제다. 입력은 작게(여기선 3바이트) 두어 대역폭을 아끼고,
// 고정 폭 정수라 바이트 배치가 플랫폼에 좌우되지 않는다.
//
// ── 이 게임에서 무엇을 뜻하는가 ────────────────────────────────────
// 지금까지 플레이어가 보낸 것은 이산 명령 넷(Place/Upgrade/Sell/Target)뿐이었다.
// 명령은 "언제 눌렀는가"가 상관없는 요청이라 신뢰·순서 채널에 실으면 그만이고,
// 프레임이라는 개념이 필요 없었다.
//
// 맵 위를 도는 엔지니어 아바타가 생기면서 그것이 바뀐다. 이동은 매 프레임 갱신되는
// 연속 입력이고, "3프레임 늦게 반영해도 같은 결과"가 성립하지 않는다. 그래서
// 명령과 별개로 **프레임 입력 스트림**이 와이어에 생긴다. 둘은 성질이 반대다.
//
//   명령      드물다 · 잃으면 안 된다 · 순서가 의미다      → 신뢰·순서 채널
//   프레임 입력 촘촘하다 · 최신이 중요하다 · 늦으면 무의미  → 비신뢰 + 중복 전송
//
// 프레임 입력을 신뢰 채널에 실으면 손실 한 번이 그 뒤의 모든 입력을 막는다
// (head-of-line blocking) — 아바타가 그 자리에 굳는다. 대신 최근 몇 프레임분을
// 매번 통째로 다시 실어 보낸다. 중복이 재전송보다 싸고, 무엇보다 지연이 없다.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mega {
namespace mp {

// 버튼 비트. 이름 없는 상수를 쓰지 않는 이유는 와이어 호환 때문이다 — 비트 하나가
// 무엇을 뜻하는지 인코더와 디코더가 같은 이름으로 읽어야 한다.
inline constexpr uint8_t kButtonFire  = 0x1;   // 아바타의 사격
inline constexpr uint8_t kButtonBuild = 0x2;   // 발밑 칸에 건설 요청
// 정의된 비트의 합집합. 여기 없는 비트가 켜진 입력은 디코더가 거부한다.
// 읽지 않는 비트를 통과시키면 같은 동작을 뜻하는 입력이 여러 가지가 되고,
// 다음 버전이 그 비트에 의미를 주는 순간 옛 클라이언트의 쓰레기가 조작이 된다.
inline constexpr uint8_t kButtonMask  = kButtonFire | kButtonBuild;

// 이동 축의 절대값 상한. 아날로그가 아니라 8방향이라 -1/0/+1 뿐이고, 그 사실이
// 와이어 계약이다(범위 밖 값은 정수 시뮬의 클램프를 우회하는 통로가 된다).
inline constexpr int8_t kMoveAxisMax = 1;

struct Input {
    int8_t moveX = 0;     // -1, 0, +1
    int8_t moveY = 0;
    uint8_t buttons = 0;  // 비트 플래그(kButtonFire 등)

    bool Fire() const { return (buttons & kButtonFire) != 0; }
    void SetFire(bool v) { buttons = (uint8_t)((buttons & ~kButtonFire) | (v ? kButtonFire : 0)); }
    bool Build() const { return (buttons & kButtonBuild) != 0; }
    void SetBuild(bool v) { buttons = (uint8_t)((buttons & ~kButtonBuild) | (v ? kButtonBuild : 0)); }

    bool operator==(const Input& o) const { return moveX == o.moveX && moveY == o.moveY && buttons == o.buttons; }
    bool operator!=(const Input& o) const { return !(*this == o); }

    template <class Ar>
    void Serialize(Ar& ar) { ar(moveX, moveY, buttons); }
};

// 와이어 계약. 필드 순서가 이 상수 안에 들어 있으므로 순서를 바꾸면 바이트가
// 달라지고, 왕복 테스트가 그것을 잡는다.
inline constexpr size_t kInputBytes = 3;

inline bool InputInRange(const Input& input) {
    if (input.moveX < -kMoveAxisMax || input.moveX > kMoveAxisMax) return false;
    if (input.moveY < -kMoveAxisMax || input.moveY > kMoveAxisMax) return false;
    return (input.buttons & ~kButtonMask) == 0;
}

// snippet-begin: input-codec
inline void WriteInput(const Input& input, std::vector<uint8_t>& out) {
    out.push_back(static_cast<uint8_t>(input.moveX));   // 2의 보수 그대로
    out.push_back(static_cast<uint8_t>(input.moveY));
    out.push_back(input.buttons);
}

// 커서를 3바이트 전진시키고 범위를 검사한다. 거부면 커서를 원위치로 되돌리지
// 않는다 — 스트림 전체가 무효이므로 부분 복구할 것이 없다.
inline bool ReadInput(const uint8_t*& cursor, const uint8_t* end, Input& out) {
    if (static_cast<size_t>(end - cursor) < kInputBytes) return false;
    Input decoded;
    decoded.moveX = static_cast<int8_t>(*cursor++);
    decoded.moveY = static_cast<int8_t>(*cursor++);
    decoded.buttons = *cursor++;
    if (!InputInRange(decoded)) return false;
    out = decoded;
    return true;
}
// snippet-end: input-codec

// 한 패킷에 싣는 최근 프레임 수. 8프레임 = 60Hz 에서 133ms 이므로, 그보다 짧은
// 연속 손실은 다음 패킷 하나가 통째로 메운다. 상한이 있어야 하는 이유는 예산이다 —
// 이 수를 올리면 매 패킷이 그만큼 커지고, 그 비용은 손실이 없을 때도 낸다.
inline constexpr size_t kMaxInputFrames = 8;
// [baseFrame:u32 LE][count:u8][입력 count개]
inline constexpr size_t kInputStreamHeaderBytes = 5;

inline std::vector<uint8_t> WriteInputStream(uint32_t baseFrame, const Input* inputs,
                                             size_t count) {
    std::vector<uint8_t> out;
    if (count > kMaxInputFrames) count = kMaxInputFrames;
    out.reserve(kInputStreamHeaderBytes + count * kInputBytes);
    out.push_back(static_cast<uint8_t>(baseFrame));
    out.push_back(static_cast<uint8_t>(baseFrame >> 8));
    out.push_back(static_cast<uint8_t>(baseFrame >> 16));
    out.push_back(static_cast<uint8_t>(baseFrame >> 24));
    out.push_back(static_cast<uint8_t>(count));
    for (size_t i = 0; i < count; ++i) WriteInput(inputs[i], out);
    return out;
}

// baseFrame 은 out[0] 의 프레임 번호이고 이후는 1씩 증가한다.
inline bool ReadInputStream(const std::vector<uint8_t>& bytes, uint32_t& baseFrame,
                            std::vector<Input>& out) {
    if (bytes.size() < kInputStreamHeaderBytes) return false;
    const uint32_t frame = static_cast<uint32_t>(bytes[0]) |
                           (static_cast<uint32_t>(bytes[1]) << 8) |
                           (static_cast<uint32_t>(bytes[2]) << 16) |
                           (static_cast<uint32_t>(bytes[3]) << 24);
    const uint8_t count = bytes[4];
    if (static_cast<size_t>(count) > kMaxInputFrames) return false;
    if (bytes.size() != kInputStreamHeaderBytes + static_cast<size_t>(count) * kInputBytes)
        return false;   // 잉여·부족 바이트 거부. 다른 디코더와 같은 규율이다.

    const uint8_t* cursor = bytes.data() + kInputStreamHeaderBytes;
    const uint8_t* end = bytes.data() + bytes.size();
    std::vector<Input> decoded;
    decoded.reserve(count);
    for (uint8_t i = 0; i < count; ++i) {
        Input input;
        if (!ReadInput(cursor, end, input)) return false;
        decoded.push_back(input);
    }
    baseFrame = frame;
    out = std::move(decoded);
    return true;
}

}  // namespace mp
}  // namespace mega
