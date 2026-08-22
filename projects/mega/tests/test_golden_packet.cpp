// 어느 타깃에서 인코딩해도 같은 바이트가 나오는가.
//
// 지금 이 프로토콜의 직렬화는 `game/defense_protocol.hpp` 의 Writer 안에
// 손으로 쓴 시프트다.
//
//     bytes_.push_back((uint8_t)v);
//     bytes_.push_back((uint8_t)(v >> 8));
//
// 리틀엔디안 머신 둘 사이에서는 이것이 옳다. 그 사실이 검사되지 않을 뿐이다 —
// 지금 이 코드가 옳은 이유가 "시프트로 적었기 때문"인지 "머신이 리틀엔디안이기
// 때문"인지 구분할 방법이 없다. 골든 바이트 벡터가 그 구분을 만든다.
//
// 이 파일이 잠그는 것.
//   1. 고정 값 표에 대한 AppendLE/WriteLE 출력이 **저장된 바이트와 정확히 일치**.
//   2. `wire::Writer` 의 출력이 같은 값에 대해 AppendLE 와 **바이트 동일** —
//      배선이 시프트를 WriteLE 로 갈아 끼워도 나가는 바이트가 변하지 않는다는
//      계약. 이 단언이 없으면 그 교체가 무증상 회귀가 될 수 있다.
//   3. CurrentEndian() 이 이 머신의 실제 바이트 배치와 맞는가(union 대조).
#include "game/defense_protocol.hpp"
#include "platform/byteorder.hpp"
#include "platform/detect.hpp"
#include "test_util.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace mega::platform;
namespace wire = mega::game::wire;

// 고정 값 표. 경계(0 · 최대) · 바이트 경계를 넘는 값 · 부호 비트가 켜진 값.
static const uint16_t kValues16[] = {0x0000, 0x0001, 0x00FF, 0x0100, 0x1234, 0xBEEF, 0xFFFF};
static const uint32_t kValues32[] = {0x00000000u, 0x00000001u, 0x000000FFu, 0x0000FF00u,
                                     0x12345678u, 0xDEADBEEFu, 0xFFFFFFFFu};
static const uint64_t kValues64[] = {0x0000000000000000ull, 0x0102030405060708ull,
                                     0xFEDCBA9876543210ull, 0xFFFFFFFFFFFFFFFFull};

// ── 1. 골든 바이트 ─────────────────────────────────────────────────
static void TestGoldenBytes() {
    // 위 세 표를 16 → 32 → 64 순으로 이어 붙인 결과. 이 배열은 손으로 적은
    // 것이고, 빅엔디안 타깃에서 인코딩해도 같아야 한다.
    static const uint8_t kGolden[] = {
        // uint16
        0x00, 0x00,  0x01, 0x00,  0xFF, 0x00,  0x00, 0x01,
        0x34, 0x12,  0xEF, 0xBE,  0xFF, 0xFF,
        // uint32
        0x00, 0x00, 0x00, 0x00,  0x01, 0x00, 0x00, 0x00,
        0xFF, 0x00, 0x00, 0x00,  0x00, 0xFF, 0x00, 0x00,
        0x78, 0x56, 0x34, 0x12,  0xEF, 0xBE, 0xAD, 0xDE,
        0xFF, 0xFF, 0xFF, 0xFF,
        // uint64
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
        0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    };

    std::vector<uint8_t> encoded;
    for (uint16_t v : kValues16) AppendLE16(encoded, v);
    for (uint32_t v : kValues32) AppendLE32(encoded, v);
    for (uint64_t v : kValues64) AppendLE64(encoded, v);

    CHECK_EQ(encoded.size(), sizeof(kGolden));
    size_t mismatch = 0;
    for (size_t i = 0; i < encoded.size() && i < sizeof(kGolden); ++i)
        if (encoded[i] != kGolden[i]) ++mismatch;
    CHECK_EQ(mismatch, (size_t)0);
    std::printf("  골든 벡터 %zu바이트 일치\n", encoded.size());

    // 되읽기 왕복 — 같은 오프셋에서 같은 값이 나온다.
    size_t offset = 0;
    for (uint16_t v : kValues16) {
        uint16_t got = 0;
        CHECK(ReadLE16At(encoded, offset, got));
        CHECK_EQ(got, v);
        offset += 2;
    }
    for (uint32_t v : kValues32) {
        uint32_t got = 0;
        CHECK(ReadLE32At(encoded, offset, got));
        CHECK_EQ(got, v);
        offset += 4;
    }
    for (uint64_t v : kValues64) {
        uint64_t got = 0;
        CHECK(ReadLE64At(encoded, offset, got));
        CHECK_EQ(got, v);
        offset += 8;
    }
    CHECK_EQ(offset, encoded.size());

    // 경계를 넘는 읽기는 거부되고 값을 건드리지 않는다.
    uint32_t untouched = 0xABCDEF01u;
    CHECK(!ReadLE32At(encoded, encoded.size() - 3, untouched));
    CHECK_EQ(untouched, 0xABCDEF01u);
    uint64_t untouched64 = 7;
    CHECK(!ReadLE64At(encoded, encoded.size(), untouched64));
    CHECK_EQ(untouched64, (uint64_t)7);
}

// ── 2. Writer 와 바이트 동일 ───────────────────────────────────────
// 배선이 Writer 안의 시프트를 WriteLE 로 갈아 끼울 때 지켜야 하는 계약이
// 정확히 이것이다: **나가는 바이트가 하나도 변하지 않는다**.
static void TestWireWriterMatchesLittleEndianHelpers() {
    wire::Writer writer(wire::PacketType::Hello);
    for (uint16_t v : kValues16) writer.U16(v);
    for (uint32_t v : kValues32) writer.U32(v);
    writer.U8(0x5A);
    writer.Raw("mega");
    const std::vector<uint8_t> packet = writer.Finish();

    // 헤더 16바이트(magic · version · type · payload 길이 · 합류 프레이밍
    // seq/ack/ackBits) 뒤가 페이로드다.
    CHECK(packet.size() > 16);
    std::vector<uint8_t> expected;
    for (uint16_t v : kValues16) AppendLE16(expected, v);
    for (uint32_t v : kValues32) AppendLE32(expected, v);
    AppendLE8(expected, 0x5A);
    for (char c : std::string("mega")) expected.push_back(static_cast<uint8_t>(c));

    CHECK_EQ(packet.size() - 16, expected.size());
    size_t mismatch = 0;
    for (size_t i = 0; i + 16 < packet.size() && i < expected.size(); ++i)
        if (packet[i + 16] != expected[i]) ++mismatch;
    CHECK_EQ(mismatch, (size_t)0);

    // 헤더의 magic 과 payload 길이도 리틀엔디안이다. 버전 바이트는 프로토콜이
    // 바뀔 때마다 오르므로 값이 아니라 자리만 확인한다.
    uint32_t magic = 0;
    CHECK(ReadLE32At(packet, 0, magic));
    CHECK_EQ(magic, wire::kMagic);
    CHECK_EQ(packet[5], static_cast<uint8_t>(wire::PacketType::Hello));
    uint16_t payload = 0;
    CHECK(ReadLE16At(packet, 6, payload));
    CHECK_EQ((size_t)payload, packet.size() - 16);

    // 읽는 쪽도 같은 값을 돌려준다.
    wire::Reader reader(packet);
    CHECK(reader.Header(wire::PacketType::Hello));
    for (uint16_t v : kValues16) CHECK_EQ(reader.U16(), v);
    for (uint32_t v : kValues32) CHECK_EQ(reader.U32(), v);
    CHECK_EQ(reader.U8(), (uint8_t)0x5A);
    CHECK_EQ(reader.Raw(4), std::string("mega"));
    CHECK(reader.Done());
    std::printf("  wire::Writer 페이로드 %zu바이트가 AppendLE 와 동일\n", expected.size());
}

// ── 3. 엔디안 판정과 실제 바이트 배치 ──────────────────────────────
static void TestEndianMatchesMachine() {
    union { uint32_t word; uint8_t byte[4]; } probe;
    probe.word = 0x01020304u;
    const bool littleOnThisMachine = probe.byte[0] == 0x04;
    CHECK_EQ(CurrentEndian() == Endian::Little, littleOnThisMachine);
    std::printf("  CurrentEndian=%s  probe[0]=0x%02X  %s\n",
                EndianName(CurrentEndian()), probe.byte[0], CurrentPlatformLine().c_str());

    // 스왑은 항등이 아니고, 두 번 스왑하면 원본이다.
    CHECK(Swap16(0x1234) != 0x1234);
    CHECK_EQ(Swap16(0x1234), (uint16_t)0x3412);
    CHECK_EQ(Swap32(0x12345678u), 0x78563412u);
    CHECK_EQ(Swap64(0x0102030405060708ull), 0x0807060504030201ull);
    for (uint16_t v : kValues16) CHECK_EQ(Swap16(Swap16(v)), v);
    for (uint32_t v : kValues32) CHECK_EQ(Swap32(Swap32(v)), v);
    for (uint64_t v : kValues64) CHECK_EQ(Swap64(Swap64(v)), v);

    // WriteLE 는 머신 엔디안과 무관하다 — memcpy 와 달리 자리마다 시프트한다.
    uint8_t buffer[4];
    WriteLE32(buffer, 0x01020304u);
    CHECK_EQ(buffer[0], (uint8_t)0x04);
    CHECK_EQ(buffer[3], (uint8_t)0x01);
    // 같은 값을 memcpy 로 적으면 빅엔디안에서 결과가 달라진다. 그 차이가 이
    // 헤더가 존재하는 이유다.
    uint8_t raw[4];
    const uint32_t value = 0x01020304u;
    std::memcpy(raw, &value, sizeof(raw));
    CHECK_EQ(raw[0] == buffer[0], littleOnThisMachine);

    // OS·Arch 와이어 코드 왕복.
    CHECK_EQ(OSFromCode(OSCode(CurrentOS())), CurrentOS());
    CHECK_EQ(ArchFromCode(ArchCode(CurrentArch())), CurrentArch());
    CHECK(OSFromCode(200) == OS::Unknown);          // 모르는 코드는 Unknown 으로 흡수
    CHECK(ArchFromCode(200) == Arch::Unknown);
}

static void RunTests() {
    TestGoldenBytes();
    TestWireWriterMatchesLittleEndianHelpers();
    TestEndianMatchesMachine();
}

TEST_MAIN("golden_packet")
