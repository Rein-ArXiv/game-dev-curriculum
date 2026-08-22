// replay — 결정성 리플레이의 파일 포맷과 그 파일의 두 번째 재생기.
//
// 이 저장소가 결정성에 대해 할 수 있던 말은 여태 "같은 시드면 같은 상태가 나온다"
// 였고, 그 문장을 검사하는 것은 A/B 두 팔의 상태 해시 대조뿐이었다. 그 대조는
// **한 프로세스 안에서 나란히 두 번 돌린** 결과를 비교한다. 그러면 프로세스
// 경계를 넘는 비결정성 — 시각, 환경 변수, 저장된 프로필, 순회 순서가 주소에
// 좌우되는 자료구조 — 은 두 팔에 똑같이 들어가 서로를 상쇄한다.
//
// 리플레이는 그 경계를 넘는다. 한 실행이 입력 열과 체인을 파일로 적고, **다른
// 프로세스**가 그 입력만 가지고 같은 체인을 다시 만든다. 상쇄될 상대가 없다.
//
// ── 파일이 고정하는 것 ────────────────────────────────────────────
// 결정성은 입력을 고정한다고 얻어지지 않는다. 이 제품의 한 프레임에는 조작 말고도
// 세 개의 외부 입력이 있고, 리플레이 파일은 셋 다 적는다.
//
//   A/B 팔 전체    `assets/flags.txt` 배정까지 확정된 뒤의 27개 스위치 값.
//                  명시된 것만 적으면 재생 시점의 롤아웃이 나머지를 정한다.
//   매치 시작 시각  기간 한정 이벤트가 시작 골드·적 체력·발파 비용을 바꾼다.
//                  벽시계로 재생하면 이벤트 창을 넘는 순간 갈린다.
//   프로필 저장소   지갑의 시작 골드 티어가 `balance_.startGold` 에 더해진다.
//                  기록과 재생이 같은 빈 저장소에서 출발해야 한다.
//
// 셋 중 하나라도 빠지면 검증기는 결정성 결함이 아닌 것을 결정성 결함으로 보고한다.
// 거짓 실패를 내는 게이트는 며칠 안에 꺼진다.
//
// ── 두 번째 재생기 ────────────────────────────────────────────────
// 파일에는 아바타 서브시뮬의 자취(입력·전진 여부·체크섬)도 들어 있다. 제품은 그
// 서브시뮬을 `mp::Rollback` 으로 굴리는데, 같은 입력 열을 `mp::Lockstep` 으로도
// 굴려 프레임마다 체크섬을 맞춰 본다. 두 재생기는 같은 `mp::DetState` 를 서로
// 다른 규율로 민다 — 하나는 예측하고 되감으며, 하나는 전원의 입력이 모일 때까지
// 멈춘다. **확정 입력이 같으면 결과도 같아야 한다**는 것이 rollback 정확성의
// 정의이므로, 이 대조는 그 정의를 실행 가능한 단언으로 바꾼다.

#pragma once

#include "io/file.h"
#include "io/read_file.hpp"
#include "mp/golden.hpp"
#include "mp/input.hpp"
#include "mp/lockstep.hpp"
#include "platform/byteorder.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace mega {
namespace game {

// 'M' 'G' 'R' 'P' 를 리틀엔디안 u32 로 읽은 값. 다른 포맷과 섞이면 디코더가
// 첫 4바이트에서 즉시 거절한다.
inline constexpr uint32_t kReplayMagic = 0x5052474Du;
// 프레이밍이 바뀔 때마다 한 단계씩 오른다. 낡은 파일은 재생하지 않고 거절한다 —
// 낡은 파일을 새 디코더로 억지로 읽으면 갈림이 아닌 것을 갈림으로 보고한다.
inline constexpr uint16_t kReplayVersion = 1;
// 상한. 신뢰 경계 밖 입력이므로 크기와 개수에 예산을 건다.
inline constexpr uint32_t kReplayMaxTicks = 2000000u;
inline constexpr size_t   kReplayMaxBytes = 64u * 1024u * 1024u;
inline constexpr size_t   kReplayMaxSwitchBytes = 4096;

// 한 프레임의 조작. `FrameInput` 의 와이어 형태다 — 17개 액션 키의 비트, 화면
// 좌표 마우스, 이번 프레임의 누름 엣지.
//
// 왜 `FrameInput` 을 그대로 쓰지 않는가. 그 구조체는 `defense_app.hpp` 가
// 소유하고 이 헤더는 그 헤더에 인클루드된다 — 반대로 의존하면 순환이다.
// 변환은 소유자 쪽에 둔다.
struct ReplayFrame {
    uint32_t keys = 0;      // 비트 i = kDefenseActionKeys[i] 가 눌렸다
    int16_t  mouseX = 0;
    int16_t  mouseY = 0;
    uint8_t  flags = 0;
};
inline constexpr uint8_t kReplayClicked = 0x1;
inline constexpr size_t  kReplayFrameBytes = 8;    // 키 3 + 좌표 4 + 플래그 1

// 한 시뮬 틱의 아바타 자취. `advanced` 가 0 이면 그 틱에 서브시뮬은 멈췄고
// (원격 확정이 없는 비예측 팔) `checksum` 은 직전 값 그대로다.
struct ReplayAvatarTick {
    mp::Input local;
    uint8_t   advanced = 0;
    uint64_t  checksum = 0;
};
inline constexpr size_t kReplayAvatarTickBytes = 12;   // 입력 3 + 전진 1 + 체크섬 8

struct Replay {
    uint16_t    version = kReplayVersion;
    uint8_t     mode = 0;           // RunMode 의 값(0=Solo 1=Host 2=Client)
    uint8_t     players = 1;        // 아바타 서브시뮬 인원
    uint8_t     localPlayer = 0;
    uint8_t     prediction = 0;     // avatar_predict 팔이 켜졌는가
    uint32_t    stride = 1;
    int64_t     matchUnix = 0;
    uint64_t    contentHash = 0;
    std::string switches;           // Switches::Assignments() 전체
    std::vector<ReplayFrame>      frames;
    std::vector<ReplayAvatarTick> avatar;
    mp::GoldenChain               chain;
};

// snippet-begin: replay-encode
inline std::vector<uint8_t> EncodeReplay(const Replay& replay) {
    std::vector<uint8_t> out;
    platform::AppendLE32(out, kReplayMagic);
    // 언제나 **지금** 버전을 찍는다. `replay.version` 은 디코더가 읽은 값을
    // 담는 자리이고, 그것을 그대로 다시 쓰면 낡은 파일을 읽어 새 프레이밍으로
    // 다시 쓴 순간 버전이 내용과 어긋난다.
    platform::AppendLE16(out, kReplayVersion);
    platform::AppendLE8(out, replay.mode);
    platform::AppendLE8(out, replay.players);
    platform::AppendLE8(out, replay.localPlayer);
    platform::AppendLE8(out, replay.prediction);
    platform::AppendLE32(out, replay.stride);
    platform::AppendLE64(out, static_cast<uint64_t>(replay.matchUnix));
    platform::AppendLE64(out, replay.contentHash);

    platform::AppendLE16(out, static_cast<uint16_t>(replay.switches.size()));
    for (char c : replay.switches) platform::AppendLE8(out, static_cast<uint8_t>(c));

    platform::AppendLE32(out, static_cast<uint32_t>(replay.frames.size()));
    for (const ReplayFrame& frame : replay.frames) {
        platform::AppendLE8(out, static_cast<uint8_t>(frame.keys));
        platform::AppendLE8(out, static_cast<uint8_t>(frame.keys >> 8));
        platform::AppendLE8(out, static_cast<uint8_t>(frame.keys >> 16));
        platform::AppendLE16(out, static_cast<uint16_t>(frame.mouseX));
        platform::AppendLE16(out, static_cast<uint16_t>(frame.mouseY));
        platform::AppendLE8(out, frame.flags);
    }

    platform::AppendLE32(out, static_cast<uint32_t>(replay.avatar.size()));
    for (const ReplayAvatarTick& tick : replay.avatar) {
        platform::AppendLE8(out, static_cast<uint8_t>(tick.local.moveX));
        platform::AppendLE8(out, static_cast<uint8_t>(tick.local.moveY));
        platform::AppendLE8(out, tick.local.buttons);
        platform::AppendLE8(out, tick.advanced);
        platform::AppendLE64(out, tick.checksum);
    }

    const std::vector<mp::GoldenMark>& marks = replay.chain.Marks();
    platform::AppendLE32(out, static_cast<uint32_t>(marks.size()));
    for (const mp::GoldenMark& mark : marks) {
        platform::AppendLE32(out, mark.tick);
        platform::AppendLE64(out, mark.chain);
    }
    platform::AppendLE32(out, replay.chain.Ticks());
    platform::AppendLE64(out, replay.chain.Chain());
    return out;
}
// snippet-end: replay-encode

// 커서 하나로 정확히 소비한다. 남는 바이트도 모자란 바이트도 거절이다 —
// 이 저장소의 다른 디코더(wire::Reader::Done)와 같은 규율이고, 이유도 같다:
// 잉여를 흘려보내면 두 번째 프레이밍이 생긴다.
// snippet-begin: replay-decode
inline bool DecodeReplay(const std::vector<uint8_t>& bytes, Replay& out,
                         std::string* error = nullptr) {
    size_t cursor = 0;
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    auto need = [&](size_t n) { return bytes.size() - cursor >= n; };
    auto u8 = [&]() { return bytes[cursor++]; };
    auto u16 = [&]() {
        const uint16_t v = platform::ReadLE16(bytes.data() + cursor);
        cursor += 2;
        return v;
    };
    auto u32 = [&]() {
        const uint32_t v = platform::ReadLE32(bytes.data() + cursor);
        cursor += 4;
        return v;
    };
    auto u64 = [&]() {
        const uint64_t v = platform::ReadLE64(bytes.data() + cursor);
        cursor += 8;
        return v;
    };

    if (!need(4) || u32() != kReplayMagic) return fail("리플레이 매직이 아니다");
    if (!need(2)) return fail("헤더가 잘렸다");
    Replay decoded;
    decoded.version = u16();
    if (decoded.version != kReplayVersion) return fail("리플레이 버전이 다르다");
    if (!need(4 + 4 + 8 + 8 + 2)) return fail("헤더가 잘렸다");
    decoded.mode = u8();
    decoded.players = u8();
    decoded.localPlayer = u8();
    decoded.prediction = u8();
    decoded.stride = u32();
    decoded.matchUnix = static_cast<int64_t>(u64());
    decoded.contentHash = u64();
    if (decoded.players < 1 || decoded.players > 8) return fail("인원 수가 범위 밖이다");
    if (decoded.localPlayer >= decoded.players) return fail("로컬 번호가 인원 밖이다");
    if (decoded.stride == 0) return fail("stride 0 은 마크를 남기지 않는다");

    const uint16_t switchBytes = u16();
    if (switchBytes > kReplayMaxSwitchBytes) return fail("스위치 문자열이 상한을 넘는다");
    if (!need(switchBytes)) return fail("스위치 문자열이 잘렸다");
    decoded.switches.assign(reinterpret_cast<const char*>(bytes.data() + cursor), switchBytes);
    cursor += switchBytes;

    if (!need(4)) return fail("프레임 수가 잘렸다");
    const uint32_t frameCount = u32();
    if (frameCount > kReplayMaxTicks) return fail("프레임 수가 상한을 넘는다");
    if (!need(static_cast<size_t>(frameCount) * kReplayFrameBytes))
        return fail("프레임 열이 잘렸다");
    decoded.frames.resize(frameCount);
    for (uint32_t i = 0; i < frameCount; ++i) {
        ReplayFrame& frame = decoded.frames[i];
        const uint32_t low = u8();
        const uint32_t mid = u8();
        const uint32_t high = u8();
        frame.keys = low | (mid << 8) | (high << 16);
        frame.mouseX = static_cast<int16_t>(u16());
        frame.mouseY = static_cast<int16_t>(u16());
        frame.flags = u8();
    }

    if (!need(4)) return fail("아바타 자취 수가 잘렸다");
    const uint32_t avatarCount = u32();
    if (avatarCount > kReplayMaxTicks) return fail("아바타 자취가 상한을 넘는다");
    if (!need(static_cast<size_t>(avatarCount) * kReplayAvatarTickBytes))
        return fail("아바타 자취가 잘렸다");
    decoded.avatar.resize(avatarCount);
    for (uint32_t i = 0; i < avatarCount; ++i) {
        ReplayAvatarTick& tick = decoded.avatar[i];
        tick.local.moveX = static_cast<int8_t>(u8());
        tick.local.moveY = static_cast<int8_t>(u8());
        tick.local.buttons = u8();
        tick.advanced = u8();
        tick.checksum = u64();
        // 기록된 입력도 신뢰 경계 밖이다. 범위 밖 축이나 모르는 버튼 비트를
        // 그대로 서브시뮬에 넣으면 정수 클램프를 우회하는 통로가 된다.
        if (!mp::InputInRange(tick.local)) return fail("아바타 입력이 범위 밖이다");
        if (tick.advanced > 1) return fail("전진 플래그가 0|1 이 아니다");
    }

    if (!need(4)) return fail("마크 수가 잘렸다");
    const uint32_t markCount = u32();
    if (markCount > kReplayMaxTicks) return fail("마크가 상한을 넘는다");
    if (!need(static_cast<size_t>(markCount) * 12)) return fail("마크가 잘렸다");
    std::vector<mp::GoldenMark> marks(markCount);
    for (uint32_t i = 0; i < markCount; ++i) {
        marks[i].tick = u32();
        marks[i].chain = u64();
    }
    if (!need(12)) return fail("체인 꼬리가 잘렸다");
    const uint32_t ticks = u32();
    const uint64_t chain = u64();
    if (cursor != bytes.size()) return fail("파일 끝에 남는 바이트가 있다");
    // 마크의 마지막은 Seal 이 남긴 것이라 반드시 총 틱 수와 같다. 어긋나면
    // 그 파일은 봉인되지 않았거나 손상된 것이고, 둘 다 재생 대상이 아니다.
    if (markCount == 0 || marks.back().tick != ticks) return fail("체인이 봉인되지 않았다");
    if (marks.back().chain != chain) return fail("마지막 마크가 체인 값과 다르다");
    decoded.chain.Adopt(decoded.stride, ticks, chain, marks);
    out = decoded;
    return true;
}
// snippet-end: replay-decode

// 쓴 바이트 수를 돌려준다. 호출자가 그 수를 로그에 남기려고 인코더를 한 번 더
// 부르면 25000틱 기록에서 megabyte 급 버퍼를 두 번 짓게 된다.
inline bool WriteReplayFile(const std::string& path, const Replay& replay,
                            size_t* bytesWritten = nullptr) {
    const std::vector<uint8_t> bytes = EncodeReplay(replay);
    if (bytesWritten) *bytesWritten = bytes.size();
    io::File file;
    if (!file.Open(path.c_str(), io::FileMode::Write)) return false;
    return file.WriteAt(0, bytes.data(), bytes.size()).ok;
}

inline bool ReadReplayFile(const std::string& path, Replay& out, std::string* error = nullptr) {
    std::vector<uint8_t> bytes;
    if (!io::ReadFileBytes(path, bytes, kReplayMaxBytes)) {
        if (error) *error = "리플레이 파일을 읽을 수 없다";
        return false;
    }
    return DecodeReplay(bytes, out, error);
}

// 파일을 0바이트로 만든다. 기록과 재생이 **같은 빈 프로필 저장소**에서 출발하게
// 하는 장치다. 지우지 않고 열면 앞선 기록이 커밋한 지갑·최고 점수를 재생이
// 물려받고, 시작 골드가 달라져 첫 틱부터 갈린다.
inline bool TruncateFile(const std::string& path) {
    io::File file;
    if (!file.Open(path.c_str(), io::FileMode::Write)) return false;
    return true;
}

// ── Lockstep 두 번째 재생 ─────────────────────────────────────────
enum class LockstepFault : uint8_t {
    None,
    Stalled,        // 제품은 전진했는데 lockstep 은 입력이 모이지 않았다
    Advanced,       // 제품은 멈췄는데 lockstep 은 전진할 수 있다
    Checksum,       // 같은 프레임에서 두 재생의 상태가 다르다
    StallThenAdvance,  // 이 검증기가 다루지 않는 자취(아래 주석)
};

struct LockstepReport {
    bool          ok = false;
    LockstepFault fault = LockstepFault::None;
    uint32_t      frames = 0;    // lockstep 이 전진한 프레임 수
    uint32_t      stalls = 0;    // 두 재생이 함께 멈춘 틱 수
    uint32_t      faultTick = 0; // 처음 어긋난 틱(1부터). ok 면 0
    uint64_t      expected = 0;
    uint64_t      actual = 0;
};

inline const char* LockstepFaultName(LockstepFault fault) {
    switch (fault) {
        case LockstepFault::Stalled: return "lockstep_stalled";
        case LockstepFault::Advanced: return "lockstep_advanced";
        case LockstepFault::Checksum: return "checksum";
        case LockstepFault::StallThenAdvance: return "stall_then_advance";
        default: return "none";
    }
}

// 기록된 확정 입력만으로 아바타 서브시뮬을 다시 민다.
//
// 다루지 않는 자취가 하나 있고, 조용히 넘기는 대신 실패로 보고한다. 어떤 프레임이
// **멈췄다가 나중에 전진**하면 `Rollback` 은 그 사이 마지막 로컬 입력을 쓰고
// `Lockstep::SubmitInput` 은 첫 등록을 지킨다(중복 무시). 두 규율이 다른 입력을
// 고르므로 대조가 성립하지 않는다. 헤드리스 기록에서는 이 자취가 나오지 않지만 —
// 솔로는 언제나 전진하고, 원격 없는 비예측 팔은 언제나 멈춘다 — 나오지 않는다는
// 사실에 기대는 대신 나오면 터지게 둔다.
// snippet-begin: lockstep-replay
inline LockstepReport VerifyWithLockstep(const Replay& replay) {
    LockstepReport report;
    mp::Lockstep lockstep;
    lockstep.Init(replay.players, 0);
    uint32_t frame = 0;
    bool sawStall = false;
    for (size_t i = 0; i < replay.avatar.size(); ++i) {
        const ReplayAvatarTick& tick = replay.avatar[i];
        const uint32_t tickNumber = static_cast<uint32_t>(i) + 1;
        lockstep.SubmitInput(frame, replay.localPlayer, tick.local);
        if (replay.prediction) {
            // 예측 팔에서 확정이 오지 않은 자리를 `Rollback` 은 '바닥값'으로
            // 메운다. 헤드리스에는 원격 피어가 없어 그 바닥값이 처음부터 끝까지
            // 기본 입력이다 — 같은 값을 여기 넣어야 두 재생이 같은 것을 민다.
            for (int p = 0; p < replay.players; ++p)
                if (p != replay.localPlayer) lockstep.SubmitInput(frame, p, mp::Input());
        }
        const bool canAdvance = lockstep.CanAdvance();
        if (tick.advanced == 0) {
            // 제품이 멈췄다. lockstep 도 멈춰야 한다 — 전원의 입력이 모이기
            // 전에 전진하면 그것이 lockstep 이 아니다.
            if (canAdvance) {
                report.fault = LockstepFault::Advanced;
                report.faultTick = tickNumber;
                return report;
            }
            sawStall = true;
            ++report.stalls;
            continue;
        }
        if (sawStall) {
            report.fault = LockstepFault::StallThenAdvance;
            report.faultTick = tickNumber;
            return report;
        }
        if (!canAdvance) {
            report.fault = LockstepFault::Stalled;
            report.faultTick = tickNumber;
            return report;
        }
        lockstep.Advance();
        ++frame;
        if (lockstep.Checksum() != tick.checksum) {
            report.fault = LockstepFault::Checksum;
            report.faultTick = tickNumber;
            report.expected = tick.checksum;
            report.actual = lockstep.Checksum();
            return report;
        }
    }
    report.ok = true;
    report.frames = frame;
    return report;
}
// snippet-end: lockstep-replay

}  // namespace game
}  // namespace mega
