// 골든 해시 체인과 리플레이 검증기.
//
// 이 저장소가 결정성에 대해 가진 검사는 여태 A/B 두 팔의 상태 해시 대조뿐이었고,
// 그것은 **한 프로세스 안에서 나란히 두 번 돌린** 결과를 비교한다. 여기서 세우는
// 것은 그보다 강한 세 가지다.
//
//   1. 체인이 **언제** 갈렸는지 말한다. 마지막 해시 하나로는 "갈렸다"까지고,
//      갈렸다가 도로 붙은 경우는 그마저도 못 말한다.
//   2. 정수 서브시뮬의 **머신 독립 골든**. 여기 박아 둔 수는 부동소수가 한 비트도
//      섞이지 않은 값이라, 다른 컴파일러·다른 최적화에서도 같아야 한다.
//      `mp::DetState::Step` 의 연산 순서를 바꾸면 이 수가 죽는다.
//   3. 제품의 프레임 루프를 실제로 기록하고 **다른 앱 인스턴스로 재생**해 체인이
//      비트 동일한지. 배선이 끊기면 여기서 걸린다.
//
// 여기서 하지 않는 것도 적어 둔다. 권위 시뮬(float)의 골든 값은 박지 않는다.
// 같은 바이너리·같은 빌드 구성 안에서만 재현을 보장하기 때문이다(`/fp:precise`
// 는 그 안에서의 재현이지 크로스 컴파일러 동치가 아니다). 프로세스 경계를 넘는
// 대조는 `mega_defense --record` / `--verify-replay` 가 ctest 에서 한다.

#include "algo/huffman.hpp"
#include "game/defense_app.hpp"
#include "game/replay.hpp"
#include "mp/det_sim.hpp"
#include "mp/golden.hpp"
#include "mp/lockstep.hpp"
#include "platform/byteorder.hpp"
#include "test_util.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace mega;
using namespace mega::game;

namespace {

// 고정 입력 열. 표를 손으로 적지 않는다 — 프레임 번호 하나가 입력을 정한다.
// 8방향 이동 · 사격 · 건설이 전부 나오도록 세 개의 서로소 주기를 쓴다.
mp::Input ScriptedInput(uint32_t frame, int player) {
    mp::Input input;
    const uint32_t k = frame * 7u + static_cast<uint32_t>(player) * 3u;
    input.moveX = static_cast<int8_t>(static_cast<int>(k % 3u) - 1);
    input.moveY = static_cast<int8_t>(static_cast<int>((k / 3u) % 3u) - 1);
    input.SetFire((k % 5u) == 0u);
    input.SetBuild((k % 11u) == 0u);
    return input;
}

constexpr uint32_t kGoldenFrames = 240;
constexpr int      kGoldenPlayers = 2;

// ── 1. 체인의 성질 ─────────────────────────────────────────────────
void ChainIsPrefixSensitive() {
    // 같은 열을 두 번 접으면 같은 값. 다른 자리에서 한 번만 다르게 접으면 그
    // 뒤로 영원히 다르다 — 그것이 "체인"이라는 말의 내용이다.
    mp::GoldenChain a, b;
    a.Reset(1);
    b.Reset(1);
    for (uint32_t tick = 1; tick <= 100; ++tick) {
        a.Append(tick * 11u, tick, tick);
        b.Append(tick == 37 ? tick * 11u + 1u : tick * 11u, tick, tick);
    }
    a.Seal();
    b.Seal();
    CHECK(a.Ticks() == 100 && b.Ticks() == 100);

    const mp::ChainDiff diff = mp::CompareChains(a, b);
    CHECK(diff.diverged);
    CHECK(diff.exact);
    CHECK_EQ(diff.lastAgreeTick, 36u);
    CHECK_EQ(diff.suspectTick, 37u);

    // 37틱에서 갈린 뒤 38틱부터는 **같은 것을 접어도** 다시 붙지 않는다.
    CHECK(a.Marks()[80].chain != b.Marks()[80].chain);
    CHECK(a.Chain() != b.Chain());
}

void ChainNarrowsToSegment() {
    // stride 를 키우면 마크가 줄고 진단은 틱이 아니라 구간이 된다. 이 맞바꿈을
    // 짐작으로 감추지 않는다 — exact 가 false 면 정확한 틱을 말하지 않는다.
    mp::GoldenChain a, b;
    a.Reset(64);
    b.Reset(64);
    for (uint32_t tick = 1; tick <= 300; ++tick) {
        a.Append(tick * 11u, tick, tick);
        b.Append(tick == 137 ? tick * 11u + 1u : tick * 11u, tick, tick);
    }
    a.Seal();
    b.Seal();
    const mp::ChainDiff diff = mp::CompareChains(a, b);
    CHECK(diff.diverged);
    CHECK(!diff.exact);
    // 137 은 (128, 192] 구간 안에 있다.
    CHECK_EQ(diff.lastAgreeTick, 128u);
    CHECK_EQ(diff.suspectTick, 192u);

    // 같은 자료로 stride 1 을 다시 돌리면 정확한 틱이 나온다. 이것이 "구간을
    // 좁혀 다시 돈다"는 진단 절차의 두 번째 걸음이다.
    mp::GoldenChain fine1, fine2;
    fine1.Reset(1);
    fine2.Reset(1);
    for (uint32_t tick = 1; tick <= 300; ++tick) {
        fine1.Append(tick * 11u, tick, tick);
        fine2.Append(tick == 137 ? tick * 11u + 1u : tick * 11u, tick, tick);
    }
    fine1.Seal();
    fine2.Seal();
    const mp::ChainDiff fine = mp::CompareChains(fine1, fine2);
    CHECK(fine.exact);
    CHECK_EQ(fine.suspectTick, 137u);
}

void ChainCatchesShortRun() {
    // 조기 종료는 상태가 아니라 **진행**의 차이다. 마지막 해시 비교로는 잡히지
    // 않는 부류라, 길이 자체를 갈림으로 센다.
    mp::GoldenChain full, cut;
    full.Reset(1);
    cut.Reset(1);
    for (uint32_t tick = 1; tick <= 50; ++tick) full.Append(tick, tick, tick);
    for (uint32_t tick = 1; tick <= 30; ++tick) cut.Append(tick, tick, tick);
    full.Seal();
    cut.Seal();
    const mp::ChainDiff diff = mp::CompareChains(full, cut);
    CHECK(diff.diverged);
    CHECK_EQ(diff.lastAgreeTick, 30u);
    CHECK_EQ(diff.suspectTick, 31u);
    CHECK_EQ(diff.goldenTicks, 50u);
    CHECK_EQ(diff.freshTicks, 30u);
}

void ChainSeesStallsThatChecksumHides() {
    // 비예측 팔의 스톨은 상태를 바꾸지 않는다. 체크섬만 접으면 "10틱 동안 멈춘
    // 실행"과 "한 번도 안 멈춘 실행"이 같은 값을 낸다. 아바타 프레임 번호를
    // 링크에 넣는 이유가 정확히 이것이다.
    mp::GoldenChain moving, stalled;
    moving.Reset(1);
    stalled.Reset(1);
    for (uint32_t tick = 1; tick <= 20; ++tick) {
        moving.Append(7u, 0xABCDu, tick);          // 매 틱 서브시뮬이 전진
        stalled.Append(7u, 0xABCDu, tick < 10 ? tick : 9u);   // 9프레임에서 멈춤
    }
    moving.Seal();
    stalled.Seal();
    CHECK(mp::CompareChains(moving, stalled).diverged);
    CHECK_EQ(mp::CompareChains(moving, stalled).suspectTick, 10u);
}

// ── 2. 정수 서브시뮬의 머신 독립 골든 ──────────────────────────────
//
// 아래 수는 `mp::DetState` 가 정수만 쓰기 때문에 박아 둘 수 있다. 부동소수가
// 한 비트도 들어가지 않으므로 컴파일러·최적화·머신이 달라도 같아야 하고,
// 달라지면 그것은 이 저장소의 결정성 주장이 거짓이라는 뜻이다.
constexpr uint64_t kGoldenChainValue = 0x4c9447828fd222f4ull;
constexpr uint64_t kGoldenFrame60 = 0x4b0c5329d6a17a62ull;
constexpr uint64_t kGoldenFrame120 = 0x8737cda3929a25d4ull;
constexpr uint64_t kGoldenFrame240 = 0x7dcf42cf5671fe9cull;

// Lockstep 으로 240프레임을 민 뒤의 체인. 필요하면 이 함수 하나만 돌려 값을 뽑는다.
mp::GoldenChain DriveLockstepGolden(std::vector<uint64_t>* checksums) {
    mp::Lockstep lockstep;
    lockstep.Init(kGoldenPlayers, 0);
    mp::GoldenChain chain;
    chain.Reset(1);
    for (uint32_t frame = 0; frame < kGoldenFrames; ++frame) {
        for (int player = 0; player < kGoldenPlayers; ++player)
            lockstep.SubmitInput(frame, player, ScriptedInput(frame, player));
        CHECK(lockstep.CanAdvance());
        lockstep.Advance();
        chain.Append(0, lockstep.Checksum(), lockstep.Frame());
        if (checksums) checksums->push_back(lockstep.Checksum());
    }
    chain.Seal();
    return chain;
}

void IntegerGoldenHolds() {
    std::vector<uint64_t> checksums;
    const mp::GoldenChain chain = DriveLockstepGolden(&checksums);
    CHECK_EQ(chain.Ticks(), kGoldenFrames);
    CHECK_EQ(checksums.size(), static_cast<size_t>(kGoldenFrames));
    CHECK_EQ(chain.Chain(), kGoldenChainValue);
    CHECK_EQ(checksums[59], kGoldenFrame60);
    CHECK_EQ(checksums[119], kGoldenFrame120);
    CHECK_EQ(checksums[239], kGoldenFrame240);
    std::printf("  정수 골든 chain=0x%016llx f60=0x%016llx f120=0x%016llx f240=0x%016llx\n",
                static_cast<unsigned long long>(chain.Chain()),
                static_cast<unsigned long long>(checksums[59]),
                static_cast<unsigned long long>(checksums[119]),
                static_cast<unsigned long long>(checksums[239]));
}

void LockstepAndBareStateAgree() {
    // Lockstep 은 DetState 를 미는 규율일 뿐이다. 같은 확정 입력을 직접 밀면
    // 같은 상태여야 한다 — 아니라면 규율이 상태를 바꾸고 있다는 뜻이다.
    mp::DetState bare;
    bare.Init(kGoldenPlayers);
    mp::Lockstep lockstep;
    lockstep.Init(kGoldenPlayers, 0);
    for (uint32_t frame = 0; frame < kGoldenFrames; ++frame) {
        std::vector<mp::Input> inputs;
        for (int player = 0; player < kGoldenPlayers; ++player) {
            inputs.push_back(ScriptedInput(frame, player));
            lockstep.SubmitInput(frame, player, inputs.back());
        }
        bare.Step(inputs);
        lockstep.Advance();
        CHECK_EQ(bare.Checksum(), lockstep.Checksum());
    }
    CHECK_EQ(bare.frame, lockstep.Frame());
}

// ── 3. lockstep 의 문지기 ──────────────────────────────────────────
void LockstepWaitsForEveryPeer() {
    mp::Lockstep lockstep;
    lockstep.Init(2, 0);
    CHECK(!lockstep.CanAdvance());          // 아무 입력도 없다

    lockstep.SubmitInput(0, 0, ScriptedInput(0, 0));
    CHECK(!lockstep.CanAdvance());          // 한 명만 왔다
    const uint64_t before = lockstep.Checksum();
    lockstep.Advance();                     // 무시돼야 한다
    CHECK_EQ(lockstep.Frame(), 0u);
    CHECK_EQ(lockstep.Checksum(), before);

    lockstep.SubmitInput(0, 1, ScriptedInput(0, 1));
    CHECK(lockstep.CanAdvance());
    lockstep.Advance();
    CHECK_EQ(lockstep.Frame(), 1u);
    CHECK(lockstep.Checksum() != before);

    // 다음 프레임의 입력이 하나라도 없으면 다시 멈춘다. 문지기는 한 번 열리고
    // 나서 열린 채로 남지 않는다.
    lockstep.SubmitInput(1, 1, ScriptedInput(1, 1));
    CHECK(!lockstep.CanAdvance());
    lockstep.SubmitInput(1, 0, ScriptedInput(1, 0));
    CHECK(lockstep.CanAdvance());
}

// ── 4. 리플레이 파일 포맷 ──────────────────────────────────────────
Replay MakeSyntheticReplay() {
    Replay replay;
    replay.mode = 0;
    replay.players = 1;
    replay.localPlayer = 0;
    replay.prediction = 0;
    replay.stride = 1;
    replay.matchUnix = 1767398400;
    replay.contentHash = 0x0123456789ABCDEFull;
    replay.switches = "entity_store=aos,pathing=poly";
    replay.chain.Reset(1);

    mp::Lockstep lockstep;
    lockstep.Init(1, 0);
    for (uint32_t frame = 0; frame < 64; ++frame) {
        const mp::Input input = ScriptedInput(frame, 0);
        ReplayFrame recorded;
        recorded.keys = frame & 0x1FFFFu;
        recorded.mouseX = static_cast<int16_t>(frame * 3);
        recorded.mouseY = static_cast<int16_t>(-static_cast<int>(frame));
        recorded.flags = (frame % 4 == 0) ? kReplayClicked : 0;
        replay.frames.push_back(recorded);

        lockstep.SubmitInput(frame, 0, input);
        lockstep.Advance();
        ReplayAvatarTick tick;
        tick.local = input;
        tick.advanced = 1;
        tick.checksum = lockstep.Checksum();
        replay.avatar.push_back(tick);
        replay.chain.Append(frame, tick.checksum, lockstep.Frame());
    }
    replay.chain.Seal();
    return replay;
}

// 본문(압축 대상)의 무압축 크기. 파일 크기가 아니라 이 수가 예전 프레이밍의
// 파일 크기와 같다 — 압축은 이 덩어리 위에 얹힌 층이다.
size_t UncompressedBodyBytes(const Replay& replay) {
    return 4 + replay.frames.size() * kReplayFrameBytes +
           4 + replay.avatar.size() * kReplayAvatarTickBytes +
           4 + replay.chain.Marks().size() * 12 + 12;
}

// 파일 헤더(압축 블록 앞까지)의 크기. 매직·버전·모드 넷·stride·시각·해시·스위치.
size_t ReplayHeaderBytes(const Replay& replay) {
    return static_cast<size_t>(4 + 2 + 4 + 4 + 8 + 8 + 2) + replay.switches.size();
}

void ReplayRoundTrips() {
    const Replay original = MakeSyntheticReplay();
    const std::vector<uint8_t> bytes = EncodeReplay(original);
    // 본문 헤더는 [크기 4][해시 8][방식 1][표 256 또는 없음][길이 4] 다.
    const size_t header = ReplayHeaderBytes(original);
    CHECK(bytes.size() > header + 4 + 8 + 1 + 4);
    const uint8_t method = bytes[header + 4 + 8];
    CHECK(method == kReplayStored || method == kReplayHuffman);
    // 어느 방식이든 파일은 예전 프레이밍보다 크지 않아야 한다. 64프레임짜리
    // 합성 기록에서는 길이표 256바이트가 이득을 먹을 수 있어 무압축이 골라질 수
    // 있고, 그 선택 자체가 "압축했더니 커졌다"를 막는 장치다.
    CHECK(bytes.size() <= header + 4 + 8 + 1 + 4 + UncompressedBodyBytes(original));

    Replay decoded;
    std::string error;
    CHECK(DecodeReplay(bytes, decoded, &error));
    CHECK_EQ(decoded.mode, original.mode);
    CHECK_EQ(decoded.players, original.players);
    CHECK_EQ(decoded.matchUnix, original.matchUnix);
    CHECK_EQ(decoded.contentHash, original.contentHash);
    CHECK(decoded.switches == original.switches);
    CHECK_EQ(decoded.frames.size(), original.frames.size());
    CHECK_EQ(decoded.avatar.size(), original.avatar.size());
    CHECK_EQ(decoded.chain.Chain(), original.chain.Chain());
    CHECK_EQ(decoded.chain.Ticks(), original.chain.Ticks());
    for (size_t i = 0; i < original.frames.size(); ++i) {
        CHECK_EQ(decoded.frames[i].keys, original.frames[i].keys);
        CHECK_EQ(decoded.frames[i].mouseX, original.frames[i].mouseX);
        CHECK_EQ(decoded.frames[i].mouseY, original.frames[i].mouseY);
        CHECK_EQ(decoded.frames[i].flags, original.frames[i].flags);
    }
    for (size_t i = 0; i < original.avatar.size(); ++i) {
        CHECK(decoded.avatar[i].local == original.avatar[i].local);
        CHECK_EQ(decoded.avatar[i].advanced, original.avatar[i].advanced);
        CHECK_EQ(decoded.avatar[i].checksum, original.avatar[i].checksum);
    }
}

void ReplayRejectsMalformed() {
    const Replay original = MakeSyntheticReplay();
    const std::vector<uint8_t> good = EncodeReplay(original);
    Replay out;
    std::string error;

    std::vector<uint8_t> badMagic = good;
    badMagic[0] ^= 0xFF;
    CHECK(!DecodeReplay(badMagic, out, &error));

    std::vector<uint8_t> badVersion = good;
    badVersion[4] = 0xFE;
    CHECK(!DecodeReplay(badVersion, out, &error));

    // 잉여 바이트. 흘려보내면 두 번째 프레이밍이 생긴다.
    std::vector<uint8_t> trailing = good;
    trailing.push_back(0);
    CHECK(!DecodeReplay(trailing, out, &error));

    // 부족 바이트.
    std::vector<uint8_t> truncated = good;
    truncated.pop_back();
    CHECK(!DecodeReplay(truncated, out, &error));

    // 범위 밖 아바타 입력. 기록된 입력도 신뢰 경계 밖이므로 정수 클램프를
    // 우회하는 통로가 되면 안 된다.
    Replay bogus = original;
    bogus.avatar[3].local.moveX = 7;
    CHECK(!DecodeReplay(EncodeReplay(bogus), out, &error));

    Replay unknownButton = original;
    unknownButton.avatar[5].local.buttons = 0x80;
    CHECK(!DecodeReplay(EncodeReplay(unknownButton), out, &error));

    // 봉인되지 않은 체인. 마지막 마크가 총 틱 수와 어긋난 파일은 재생 대상이 아니다.
    Replay unsealed = original;
    unsealed.chain.MutableMarks().pop_back();
    CHECK(!DecodeReplay(EncodeReplay(unsealed), out, &error));
}

// ── 4b. 압축 블록 ──────────────────────────────────────────────────
//
// 압축이 파일 계층에 얹혔다는 것을 확인하는 자리다. 시뮬도 체인도 이 층을 보지
// 않으므로, 여기서 묻는 것은 셋뿐이다: 작아졌는가, 정확히 되돌아오는가, 손상을
// 거부하는가.

// 치우친 분포의 큰 기록. 실제 기록의 형질을 흉내낸다 — 액션 키 비트는 대부분 0,
// 마우스는 근접 반복, 그리고 체크섬만 고엔트로피다.
//
// keys 를 (i+1) 의 후행 0 개수로 두면 값 0 이 절반, 1 이 4분의 1, 2 가 8분의 1 …
// 인 기하 분포가 나온다. 기하 분포의 Huffman 깊이는 심볼 번호를 따라가므로
// **lookup 표의 폭(9비트)을 넘는 코드가 반드시 생긴다.** 긴 코드 fallback 이
// 실제로 도는지를 우연이 아니라 구성으로 보장하는 자리다.
Replay MakeSkewedReplay(size_t frames) {
    Replay replay;
    replay.mode = 0;
    replay.players = 1;
    replay.localPlayer = 0;
    replay.prediction = 0;
    replay.stride = 1;
    replay.matchUnix = 1767398400;
    replay.contentHash = 0xFEEDFACECAFEBEEFull;
    replay.switches = "entity_store=aos,pathing=poly";
    replay.chain.Reset(1);

    uint64_t rng = 0x9E3779B97F4A7C15ull;
    auto next = [&rng]() {
        rng ^= rng << 13;
        rng ^= rng >> 7;
        rng ^= rng << 17;
        return rng;
    };
    for (size_t i = 0; i < frames; ++i) {
        uint32_t zeros = 0;
        for (uint64_t v = i + 1; (v & 1u) == 0; v >>= 1) ++zeros;
        ReplayFrame frame;
        frame.keys = zeros;
        frame.mouseX = static_cast<int16_t>(200 + (i % 3));
        frame.mouseY = static_cast<int16_t>(140 + (i % 2));
        frame.flags = 0;
        replay.frames.push_back(frame);

        ReplayAvatarTick tick;
        tick.local.moveX = static_cast<int8_t>(static_cast<int>(i % 3u) - 1);
        tick.local.moveY = 0;
        tick.local.buttons = 0;
        tick.advanced = 1;
        tick.checksum = next();
        replay.avatar.push_back(tick);
        replay.chain.Append(next(), tick.checksum, static_cast<uint32_t>(i + 1));
    }
    replay.chain.Seal();
    return replay;
}

// 압축 블록의 세 자리를 파일에서 되짚는다.
struct BlockLayout {
    size_t   headerBytes = 0;
    uint32_t bodySize = 0;
    uint8_t  method = 0;
    size_t   tableAt = 0;      // 코드 길이표의 첫 바이트(huffman 일 때만 유효)
    uint32_t codedBytes = 0;
    size_t   payloadAt = 0;
};

BlockLayout ReadBlockLayout(const Replay& replay, const std::vector<uint8_t>& bytes) {
    BlockLayout layout;
    layout.headerBytes = ReplayHeaderBytes(replay);
    layout.bodySize = platform::ReadLE32(bytes.data() + layout.headerBytes);
    layout.method = bytes[layout.headerBytes + 12];
    layout.tableAt = layout.headerBytes + 13;
    const size_t lengthAt =
        layout.method == kReplayHuffman ? layout.tableAt + kReplayCodeTableBytes : layout.tableAt;
    layout.codedBytes = platform::ReadLE32(bytes.data() + lengthAt);
    layout.payloadAt = lengthAt + 4;
    return layout;
}

void ReplayCompressionShrinksAndRestores() {
    const Replay original = MakeSkewedReplay(60000);
    const std::vector<uint8_t> bytes = EncodeReplay(original);
    const BlockLayout layout = ReadBlockLayout(original, bytes);

    CHECK_EQ(layout.method, kReplayHuffman);
    CHECK_EQ(static_cast<size_t>(layout.bodySize), UncompressedBodyBytes(original));

    // 실측 비율. 압축 전 파일 크기는 헤더 + 본문이었다.
    const size_t before = layout.headerBytes + UncompressedBodyBytes(original);
    CHECK(bytes.size() < before);
    // 본문의 절반은 해시 출력이다(아바타 체크섬 8 + 체인 8). 그 절반은 구성상
    // 압축되지 않으므로 파일 전체의 이득에는 천장이 있다. 그 천장을 함께 적는다 —
    // 적지 않으면 80% 라는 수를 보고 압축이 고장났다고 읽게 된다.
    const size_t hashBytes = original.avatar.size() * 8 + original.chain.Marks().size() * 8;
    std::printf("  리플레이 압축 %zu -> %zu 바이트 (%.1f%%, 프레임 %zu)\n",
                before, bytes.size(),
                100.0 * static_cast<double>(bytes.size()) / static_cast<double>(before),
                original.frames.size());
    std::printf("  그중 압축 불가(해시) %zu바이트 = 본문의 %.1f%% — 이론 하한 %.1f%%\n",
                hashBytes,
                100.0 * static_cast<double>(hashBytes) / static_cast<double>(layout.bodySize),
                100.0 * static_cast<double>(hashBytes) / static_cast<double>(before));
    CHECK(bytes.size() > hashBytes);   // 해시 아래로는 내려갈 수 없다

    // 이 기록에서 가장 긴 코드가 몇 비트인가. 체크섬과 체인이 고엔트로피라
    // 히스토그램이 평평해져 lookup 폭 안에 들어온다 — 그래서 긴 코드 fallback 은
    // **이 검사로는 돌지 않는다.** 그 경로를 도는 자리는 아래 따로 있다.
    uint8_t longest = 0;
    for (size_t i = 0; i < kReplayCodeTableBytes; ++i)
        if (bytes[layout.tableAt + i] > longest) longest = bytes[layout.tableAt + i];
    std::printf("  코드 길이표 최장 %u비트 (lookup 폭 %d비트)\n",
                longest, algo::kHuffmanLookupBits);

    // 그리고 정확히 되돌아온다.
    Replay decoded;
    std::string error;
    CHECK(DecodeReplay(bytes, decoded, &error));
    CHECK_EQ(decoded.frames.size(), original.frames.size());
    CHECK_EQ(decoded.avatar.size(), original.avatar.size());
    size_t mismatch = 0;
    for (size_t i = 0; i < original.frames.size(); ++i) {
        if (decoded.frames[i].keys != original.frames[i].keys ||
            decoded.frames[i].mouseX != original.frames[i].mouseX ||
            decoded.frames[i].mouseY != original.frames[i].mouseY ||
            decoded.frames[i].flags != original.frames[i].flags) ++mismatch;
    }
    for (size_t i = 0; i < original.avatar.size(); ++i)
        if (decoded.avatar[i].checksum != original.avatar[i].checksum) ++mismatch;
    CHECK_EQ(mismatch, static_cast<size_t>(0));
    CHECK_EQ(decoded.chain.Chain(), original.chain.Chain());
}

// lookup 표에 들어가지 못하는 긴 코드를 쓰는 파일. 리플레이 파일은 신뢰 경계
// **밖**에 있으므로 이런 히스토그램의 파일이 도착하는 것은 가정이 아니라 언제든
// 일어나는 일이고, 그때 복호기가 조용히 다른 바이트를 내면 안 된다.
//
// 프레임만 남기고 체크섬·체인을 최소로 두면 본문 히스토그램이 기하 분포가 된다.
// 기하 분포의 Huffman 깊이는 심볼 번호를 따라가므로 9비트를 반드시 넘는다.
Replay MakeDeepCodeReplay(size_t frames) {
    Replay replay;
    replay.mode = 0;
    replay.players = 1;
    replay.localPlayer = 0;
    replay.prediction = 0;
    replay.stride = 1;
    replay.matchUnix = 1767398400;
    replay.contentHash = 0;
    replay.switches = "pathing=poly";
    replay.chain.Reset(1);
    for (size_t i = 0; i < frames; ++i) {
        uint32_t zeros = 0;
        for (uint64_t v = i + 1; (v & 1u) == 0; v >>= 1) ++zeros;
        ReplayFrame frame;
        frame.keys = zeros;      // 0 이 절반, 1 이 4분의 1, 2 가 8분의 1 …
        frame.mouseX = 0;
        frame.mouseY = 0;
        frame.flags = 0;
        replay.frames.push_back(frame);
    }
    replay.chain.Append(1, 1, 1);
    replay.chain.Seal();
    return replay;
}

void ReplayRestoresLongCodes() {
    const Replay original = MakeDeepCodeReplay(100000);
    const std::vector<uint8_t> bytes = EncodeReplay(original);
    const BlockLayout layout = ReadBlockLayout(original, bytes);
    CHECK_EQ(layout.method, kReplayHuffman);

    uint8_t longest = 0;
    for (size_t i = 0; i < kReplayCodeTableBytes; ++i)
        if (bytes[layout.tableAt + i] > longest) longest = bytes[layout.tableAt + i];
    CHECK(longest > algo::kHuffmanLookupBits);

    Replay decoded;
    std::string error;
    CHECK(DecodeReplay(bytes, decoded, &error));
    CHECK_EQ(decoded.frames.size(), original.frames.size());
    size_t mismatch = 0;
    for (size_t i = 0; i < original.frames.size(); ++i)
        if (decoded.frames[i].keys != original.frames[i].keys) ++mismatch;
    CHECK_EQ(mismatch, static_cast<size_t>(0));
    const size_t before = layout.headerBytes + UncompressedBodyBytes(original);
    std::printf("  긴 코드 파일 최장 %u비트, %zu -> %zu 바이트 (%.1f%%)\n",
                longest, before, bytes.size(),
                100.0 * static_cast<double>(bytes.size()) / static_cast<double>(before));
}

// 파일 전 범위의 표본 위치에서 잘라 전부 거절되는가. 압축이 들어오면서 "잘린
// 파일"의 실패 모드가 늘었다 — 표가 잘릴 수도, 페이로드가 잘릴 수도 있다.
void ReplayRejectsTruncationEverywhere() {
    const Replay original = MakeSkewedReplay(4000);
    const std::vector<uint8_t> good = EncodeReplay(original);
    Replay out;
    std::string error;
    int accepted = 0, tried = 0;
    const size_t step = good.size() / 200 + 1;
    for (size_t cut = 0; cut < good.size(); cut += step) {
        std::vector<uint8_t> truncated(good.begin(),
                                       good.begin() + static_cast<ptrdiff_t>(cut));
        ++tried;
        if (DecodeReplay(truncated, out, &error)) ++accepted;
    }
    CHECK_EQ(accepted, 0);
    std::printf("  절단 표본 %d곳 전부 거절\n", tried);
}

// 비트 하나를 뒤집으면 거절되는가. 복호 결과가 우연히 문법에 맞는 본문이 될 수
// 있으므로, 원본을 접은 수 하나가 그 자리를 닫는다.
void ReplayRejectsBitDamage() {
    const Replay original = MakeSkewedReplay(4000);
    const std::vector<uint8_t> good = EncodeReplay(original);
    Replay out;
    std::string error;
    int accepted = 0, tried = 0;
    const size_t step = good.size() / 200 + 1;
    for (size_t at = 0; at < good.size(); at += step) {
        std::vector<uint8_t> damaged = good;
        damaged[at] ^= static_cast<uint8_t>(1u << (at % 8));
        ++tried;
        if (DecodeReplay(damaged, out, &error)) ++accepted;
    }
    CHECK_EQ(accepted, 0);
    std::printf("  비트 손상 표본 %d곳 전부 거절\n", tried);
}

// Kraft 등식을 어기는 코드 길이표는 파일 전체를 거절시킨다. 이 검사가 없으면
// 임의의 256바이트가 길이표 행세를 하고, 복호기는 크래시하는 대신 **조용히 다른
// 바이트를 낸다**.
void ReplayRejectsKraftViolation() {
    const Replay original = MakeSkewedReplay(4000);
    const std::vector<uint8_t> good = EncodeReplay(original);
    const BlockLayout layout = ReadBlockLayout(original, good);
    CHECK_EQ(layout.method, kReplayHuffman);

    Replay out;
    std::string error;

    // (a) 합이 1보다 작아지는 표 — 어떤 비트열에서도 심볼을 못 찾는 구멍이 생긴다.
    std::vector<uint8_t> hole = good;
    for (size_t i = 0; i < kReplayCodeTableBytes; ++i) {
        if (hole[layout.tableAt + i] == 0) continue;
        hole[layout.tableAt + i] = 0;      // 심볼 하나를 표에서 지운다
        break;
    }
    CHECK(!DecodeReplay(hole, out, &error));
    CHECK(error.find("Kraft") != std::string::npos);

    // (b) 합이 1보다 커지는 표 — 코드가 겹친다.
    std::vector<uint8_t> overlap = good;
    for (size_t i = 0; i < kReplayCodeTableBytes; ++i) {
        if (overlap[layout.tableAt + i] < 2) continue;
        overlap[layout.tableAt + i] = 1;   // 짧게 만들어 자리를 넓힌다
        break;
    }
    CHECK(!DecodeReplay(overlap, out, &error));
    CHECK(error.find("Kraft") != std::string::npos);

    // (c) 상한을 넘는 길이. 8비트에 적히지만 15를 넘으면 유효한 표가 아니다.
    std::vector<uint8_t> tooLong = good;
    tooLong[layout.tableAt] = 200;
    CHECK(!DecodeReplay(tooLong, out, &error));

    // (d) 모르는 압축 방식.
    std::vector<uint8_t> alien = good;
    alien[layout.headerBytes + 12] = 9;
    CHECK(!DecodeReplay(alien, out, &error));

    // (e) 선언된 본문 크기가 복호 결과와 다르다.
    std::vector<uint8_t> wrongSize = good;
    platform::WriteLE32(wrongSize.data() + layout.headerBytes, layout.bodySize + 1);
    CHECK(!DecodeReplay(wrongSize, out, &error));
    std::printf("  코드 길이표·방식·크기 거부 5종 확인\n");
}

// ── 5. 두 번째 재생기가 실제로 갈림을 잡는가 ───────────────────────
void LockstepReplayCatchesFaults() {
    const Replay good = MakeSyntheticReplay();
    const LockstepReport clean = VerifyWithLockstep(good);
    CHECK(clean.ok);
    CHECK_EQ(clean.frames, 64u);
    CHECK_EQ(clean.stalls, 0u);

    // 체크섬 하나만 손대면 그 틱에서 걸린다.
    Replay tampered = good;
    tampered.avatar[41].checksum ^= 1ull;
    const LockstepReport bad = VerifyWithLockstep(tampered);
    CHECK(!bad.ok);
    CHECK(bad.fault == LockstepFault::Checksum);
    CHECK_EQ(bad.faultTick, 42u);

    // 입력 하나만 손대도 그 뒤 상태가 갈린다 — 같은 틱은 아니고 **그 틱부터**다.
    Replay altered = good;
    altered.avatar[10].local.moveX = static_cast<int8_t>(-altered.avatar[10].local.moveX);
    if (altered.avatar[10].local.moveX == 0) altered.avatar[10].local.moveX = 1;
    const LockstepReport drift = VerifyWithLockstep(altered);
    CHECK(!drift.ok);
    CHECK(drift.fault == LockstepFault::Checksum);
    CHECK_EQ(drift.faultTick, 11u);

    // 제품이 멈췄다고 적혀 있는데 lockstep 은 전진할 수 있는 경우.
    Replay stalledClaim = good;
    stalledClaim.avatar[20].advanced = 0;
    const LockstepReport mismatch = VerifyWithLockstep(stalledClaim);
    CHECK(!mismatch.ok);
    CHECK(mismatch.fault == LockstepFault::Advanced);
    CHECK_EQ(mismatch.faultTick, 21u);

    // 2인 비예측 자취: 원격 확정이 없으면 두 재생 모두 멈춘다.
    Replay twoPlayers = good;
    twoPlayers.players = 2;
    twoPlayers.prediction = 0;
    for (ReplayAvatarTick& tick : twoPlayers.avatar) {
        tick.advanced = 0;
        tick.checksum = 0;
    }
    const LockstepReport stalls = VerifyWithLockstep(twoPlayers);
    CHECK(stalls.ok);
    CHECK_EQ(stalls.frames, 0u);
    CHECK_EQ(stalls.stalls, 64u);

    // 멈췄다가 전진하는 자취는 이 검증기가 다루지 않는다. 조용히 넘기지 않고
    // 실패로 보고한다 — 다루지 못하는 것을 다룬 척하면 green 이 거짓이 된다.
    Replay stallThenGo = twoPlayers;
    stallThenGo.avatar[30].advanced = 1;
    const LockstepReport refused = VerifyWithLockstep(stallThenGo);
    CHECK(!refused.ok);
    CHECK(refused.fault == LockstepFault::StallThenAdvance);
    CHECK_EQ(refused.faultTick, 31u);
}

// ── 6. 제품 프레임 루프의 기록 → 재생 ──────────────────────────────
DefenseOptions ReplayTestOptions(const char* profile) {
    DefenseOptions options;
    options.mode = RunMode::Solo;
    options.selftest = true;
    options.profilePath = std::string(profile);
    // 시계를 못박는다. 기간 한정 이벤트가 시작 골드·적 체력·발파 비용을 바꾸므로
    // 벽시계로 두 실행을 돌리면 이벤트 창 하나만 열려도 갈린다.
    options.matchUnixOverride = 1767000000;
    options.chainStride = 1;
    return options;
}

void ProductRunReplaysBitIdentically() {
    // 앱마다 다른 프로필 파일을 쓴다. 셋 다 0바이트에서 출발하므로 시작 상태는
    // 같고, 앞선 앱이 아직 열어 둔 파일을 자르려다 실패하는 일이 없다.
    const char* recordProfile = "replay_golden_record.wal";
    const char* playProfile = "replay_golden_play.wal";
    const char* perturbProfile = "replay_golden_perturb.wal";
    CHECK(TruncateFile(recordProfile));
    CHECK(TruncateFile(playProfile));
    CHECK(TruncateFile(perturbProfile));

    DefenseOptions options = ReplayTestOptions(recordProfile);
    DefenseApp recorder;
    if (!recorder.Init(options)) {
        std::printf("  SKIP: DefenseApp::Init 실패(에셋 없음?)\n");
        CHECK(false);
        return;
    }
    recorder.EnableReplayCapture(options.chainStride);
    RunDefenseHeadless(recorder, 900);

    Replay replay;
    replay.mode = static_cast<uint8_t>(options.mode);
    replay.players = static_cast<uint8_t>(recorder.AvatarPlayers());
    replay.localPlayer = static_cast<uint8_t>(recorder.LocalAvatarIndex());
    replay.prediction = recorder.Options().switches.avatarPredict ? 1 : 0;
    replay.stride = options.chainStride;
    replay.matchUnix = recorder.MatchStartUnix();
    replay.contentHash = recorder.ContentHash();
    replay.switches = recorder.Options().switches.Assignments();
    replay.frames = recorder.CapturedFrames();
    replay.avatar = recorder.CapturedAvatar();
    replay.chain = recorder.Chain();
    replay.chain.Seal();

    CHECK(!replay.frames.empty());
    CHECK_EQ(replay.chain.Ticks(), static_cast<uint32_t>(replay.avatar.size()));
    // 헤드리스에서는 한 Step 이 정확히 한 시뮬 틱이다. 이 관계가 깨지면 아래
    // 재생이 프레임과 틱을 다른 좌표로 세게 된다.
    CHECK_EQ(replay.frames.size(), replay.avatar.size());

    // 파일을 실제로 왕복시킨다. 인메모리 구조체만 비교하면 인코더·디코더가
    // 이 검사의 경로에서 빠진다.
    const char* path = "replay_golden_test.mgrp";
    CHECK(WriteReplayFile(path, replay));
    Replay loaded;
    std::string error;
    CHECK(ReadReplayFile(path, loaded, &error));
    CHECK_EQ(loaded.chain.Chain(), replay.chain.Chain());

    // 두 번째 앱 인스턴스. 기록된 조작만 먹인다.
    DefenseOptions fresh = ReplayTestOptions(playProfile);
    fresh.matchUnixOverride = loaded.matchUnix;
    std::string switchError;
    CHECK(fresh.switches.ParseList(loaded.switches, &switchError));
    DefenseApp player;
    CHECK(player.Init(fresh));
    player.EnableReplayCapture(loaded.stride);
    for (const ReplayFrame& frame : loaded.frames)
        player.Step(DefenseApp::FromReplayFrame(frame), static_cast<double>(kDefenseFixedDt));

    mp::GoldenChain rebuilt = player.Chain();
    rebuilt.Seal();
    const mp::ChainDiff diff = mp::CompareChains(loaded.chain, rebuilt);
    if (diff.diverged)
        std::printf("  체인이 갈렸다: last_agree=%u suspect=%u exact=%d\n",
                    diff.lastAgreeTick, diff.suspectTick, diff.exact ? 1 : 0);
    CHECK(!diff.diverged);
    CHECK_EQ(rebuilt.Chain(), loaded.chain.Chain());
    CHECK_EQ(rebuilt.Ticks(), loaded.chain.Ticks());

    // 아바타 자취도 비트 동일해야 하고, lockstep 재생이 그 자취를 다시 민다.
    const std::vector<ReplayAvatarTick>& track = player.CapturedAvatar();
    CHECK_EQ(track.size(), loaded.avatar.size());
    for (size_t i = 0; i < track.size() && i < loaded.avatar.size(); ++i)
        CHECK_EQ(track[i].checksum, loaded.avatar[i].checksum);

    const LockstepReport lockstep = VerifyWithLockstep(loaded);
    CHECK(lockstep.ok);
    CHECK_EQ(lockstep.frames, player.AvatarFrame());

    std::printf("  제품 리플레이 frames=%zu chain=0x%016llx lockstep_frames=%u\n",
                loaded.frames.size(),
                static_cast<unsigned long long>(loaded.chain.Chain()), lockstep.frames);

    // 그리고 갈림을 실제로 만들어 본다. 조작 하나를 바꾼 재생은 반드시 갈려야
    // 한다 — 갈리지 않으면 이 체인은 아무것도 보고 있지 않다는 뜻이다.
    DefenseOptions perturbed = fresh;
    perturbed.profilePath = perturbProfile;
    DefenseApp other;
    CHECK(other.Init(perturbed));
    other.EnableReplayCapture(loaded.stride);
    for (size_t i = 0; i < loaded.frames.size(); ++i) {
        ReplayFrame frame = loaded.frames[i];
        if (i == 500) frame.keys ^= 1u;      // 500번째 프레임에서 키 하나를 뒤집는다
        other.Step(DefenseApp::FromReplayFrame(frame), static_cast<double>(kDefenseFixedDt));
    }
    mp::GoldenChain perturbedChain = other.Chain();
    perturbedChain.Seal();
    const mp::ChainDiff perturbedDiff = mp::CompareChains(loaded.chain, perturbedChain);
    CHECK(perturbedDiff.diverged);
    CHECK(perturbedDiff.exact);
    // 501번째 틱(1부터 세는 좌표)이 그 프레임이다.
    CHECK(perturbedDiff.suspectTick >= 501u);
    std::printf("  조작 한 번 뒤집기 → 갈린 틱 %u (마지막 일치 %u)\n",
                perturbedDiff.suspectTick, perturbedDiff.lastAgreeTick);
}

void RunTests() {
    ChainIsPrefixSensitive();
    ChainNarrowsToSegment();
    ChainCatchesShortRun();
    ChainSeesStallsThatChecksumHides();
    IntegerGoldenHolds();
    LockstepAndBareStateAgree();
    LockstepWaitsForEveryPeer();
    ReplayRoundTrips();
    ReplayRejectsMalformed();
    ReplayCompressionShrinksAndRestores();
    ReplayRestoresLongCodes();
    ReplayRejectsTruncationEverywhere();
    ReplayRejectsBitDamage();
    ReplayRejectsKraftViolation();
    LockstepReplayCatchesFaults();
    ProductRunReplaysBitIdentically();
}

}  // namespace

TEST_MAIN("replay_golden")
