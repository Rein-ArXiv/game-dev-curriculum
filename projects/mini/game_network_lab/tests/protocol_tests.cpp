#include "protocol.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <vector>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

// Round trip tests pass whenever the encoder and the decoder share the same
// mistake. These byte arrays were written from the wire format description, not
// produced by the encoder, so a change to magic, version, field order or
// endianness has to break them.
void TestGoldenControlFrame() {
    using namespace gnl;

    const std::array<std::byte, 5> payload{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}};
    const std::array<std::byte, 17> expected{
        std::byte{0x47}, std::byte{0x4e}, std::byte{0x4c}, std::byte{0x31}, // magic
        std::byte{0x00}, std::byte{0x01},                                   // version 1
        std::byte{0x00}, std::byte{0x01},                                   // Hello
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x05}, // length 5
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5},
    };

    auto frame = EncodeControl(ControlType::Hello, payload);
    Require(frame.size() == expected.size(), "control frame golden size");
    for (std::size_t i = 0; i < expected.size(); ++i) {
        Require(frame[i] == expected[i], "control frame golden bytes");
    }
}

void TestGoldenDatagram() {
    using namespace gnl;

    const std::array<std::byte, 5> payload{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}};
    const std::array<std::byte, 29> expected{
        std::byte{0x47}, std::byte{0x4e}, std::byte{0x4c}, std::byte{0x31}, // magic
        std::byte{0x00}, std::byte{0x01},                                   // version 1
        std::byte{0x00}, std::byte{0x03},                                   // Input
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x2a}, // connection 42
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x63}, // sequence 99
        std::byte{0x00}, std::byte{0x05},                                   // payload bytes
        std::byte{0x00}, std::byte{0x00},                                   // reserved
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5},
    };

    auto datagram = EncodeDatagram(DatagramType::Input, 42, 99, payload);
    Require(datagram.size() == expected.size(), "datagram golden size");
    for (std::size_t i = 0; i < expected.size(); ++i) {
        Require(datagram[i] == expected[i], "datagram golden bytes");
    }
    Require(datagram.size() == kDatagramHeaderBytes + payload.size(),
            "datagram header is 24 bytes");
}

void TestFraming() {
    using namespace gnl;

    const std::array<std::byte, 5> payload{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}};
    auto frame = EncodeControl(ControlType::Hello, payload);
    Require(frame.size() == kFrameHeaderBytes + payload.size(), "frame size");

    FrameDecoder decoder;
    Require(decoder.Push(std::span(frame).first(3)), "push first fragment");
    Require(!decoder.Pop().has_value(), "partial header must wait");
    Require(decoder.Push(std::span(frame).subspan(3, 7)), "push second fragment");
    Require(!decoder.Pop().has_value(), "partial payload must wait");
    Require(decoder.Push(std::span(frame).subspan(10)), "push final fragment");
    auto decodedFrame = decoder.Pop();
    Require(decodedFrame.has_value(), "complete frame");
    Require(decodedFrame->type == ControlType::Hello, "frame type");
    Require(decodedFrame->payload == std::vector<std::byte>(payload.begin(), payload.end()),
            "frame payload");

    auto second = EncodeControl(ControlType::Ping, {});
    std::vector<std::byte> merged = frame;
    merged.insert(merged.end(), second.begin(), second.end());
    FrameDecoder mergedDecoder;
    Require(mergedDecoder.Push(merged), "push merged frames");
    Require(mergedDecoder.Pop()->type == ControlType::Hello, "first merged frame");
    Require(mergedDecoder.Pop()->type == ControlType::Ping, "second merged frame");
}

void TestDatagramRoundTrip() {
    using namespace gnl;

    const std::array<std::byte, 5> payload{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}};
    auto datagram = EncodeDatagram(DatagramType::Input, 42, 99, payload);
    auto decoded = DecodeDatagram(datagram);
    Require(decoded.has_value(), "decode datagram");
    Require(decoded->type == DatagramType::Input, "datagram type");
    Require(decoded->connection == 42, "datagram connection");
    Require(decoded->sequence == 99, "datagram sequence");
    Require(decoded->payload == std::vector<std::byte>(payload.begin(), payload.end()),
            "datagram payload");
}

// A datagram is a single OS-delivered unit, so every header field the sender did
// not write is an attacker-controlled field until the decoder rejects it.
void TestDatagramRejection() {
    using namespace gnl;

    const std::array<std::byte, 5> payload{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}};
    const auto original = EncodeDatagram(DatagramType::Input, 42, 99, payload);

    auto truncated = original;
    truncated.pop_back();
    Require(!DecodeDatagram(truncated).has_value(), "truncated datagram rejected");

    // Declared length says 6 while the datagram still carries 5 bytes.
    auto forgedLength = original;
    forgedLength[21] = std::byte{0x06};
    Require(forgedLength.size() == original.size(), "length forgery keeps the size");
    Require(!DecodeDatagram(forgedLength).has_value(),
            "declared payload length must match the received size");

    // Shrinking the declared length is the other direction of the same lie.
    auto shrunkLength = original;
    shrunkLength[21] = std::byte{0x04};
    Require(!DecodeDatagram(shrunkLength).has_value(),
            "an under-declared payload length is rejected too");

    // Reserved bytes are the forward-compatibility slot: they must be zero now.
    for (std::size_t index : {std::size_t{22}, std::size_t{23}}) {
        auto tampered = original;
        tampered[index] = std::byte{0x01};
        Require(!DecodeDatagram(tampered).has_value(),
                "non-zero reserved field rejected");
    }

    auto badMagic = original;
    badMagic[0] ^= std::byte{0xff};
    Require(!DecodeDatagram(badMagic).has_value(), "wrong datagram magic rejected");

    auto badVersion = original;
    badVersion[5] = std::byte{0x02};
    Require(!DecodeDatagram(badVersion).has_value(), "wrong datagram version rejected");

    auto tooShort = original;
    tooShort.resize(kDatagramHeaderBytes - 1);
    Require(!DecodeDatagram(tooShort).has_value(), "header-truncated datagram rejected");

    // Round trip still works for the unmodified bytes: the checks above reject
    // corruption, not everything.
    Require(DecodeDatagram(original).has_value(), "the untouched datagram still decodes");
}

// A stream decoder cannot resynchronise, so an illegal header must move it to a
// permanent failed state instead of skipping bytes.
void TestFrameHeaderRejection() {
    using namespace gnl;

    const std::array<std::byte, 5> payload{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}};
    const auto original = EncodeControl(ControlType::Hello, payload);

    {
        auto badMagic = original;
        badMagic[0] ^= std::byte{0xff};
        FrameDecoder decoder;
        Require(decoder.Push(badMagic), "push frame with wrong magic");
        Require(!decoder.Pop().has_value(), "wrong magic yields no message");
        Require(decoder.Failed(), "wrong magic fails the decoder");
        Require(!decoder.Push(original), "a failed decoder refuses more bytes");
        Require(!decoder.Pop().has_value(), "a failed decoder stays failed");
    }
    {
        auto badVersion = original;
        badVersion[5] = std::byte{0x02};
        FrameDecoder decoder;
        Require(decoder.Push(badVersion), "push frame with wrong version");
        Require(!decoder.Pop().has_value(), "wrong version yields no message");
        Require(decoder.Failed(), "wrong version fails the decoder");
    }
    {
        // 65537 declared payload bytes: one past kMaxControlPayload. The decoder
        // must reject on the header alone rather than wait for the bytes.
        auto hugeLength = original;
        hugeLength[8] = std::byte{0x00};
        hugeLength[9] = std::byte{0x01};
        hugeLength[10] = std::byte{0x00};
        hugeLength[11] = std::byte{0x01};
        FrameDecoder decoder;
        Require(decoder.Push(hugeLength), "push frame declaring 65537 payload bytes");
        Require(!decoder.Pop().has_value(), "an over-long declared length yields no message");
        Require(decoder.Failed(), "an over-long declared length fails the decoder");
        Require(decoder.BufferedBytes() == hugeLength.size(),
                "the decoder never allocated for the declared length");
    }
}

// step01: a peer that declares an enormous frame must not make the server hold
// unbounded memory while it waits for the rest.
void TestFrameBufferCap() {
    using namespace gnl;

    constexpr std::size_t kCap = kMaxControlPayload + kFrameHeaderBytes;

    {
        FrameDecoder decoder;
        Require(decoder.Push(std::vector<std::byte>(kCap)), "the cap itself fits");
        Require(decoder.BufferedBytes() == kCap, "all of it is buffered");
        Require(!decoder.Push(std::vector<std::byte>(1)), "one byte past the cap is refused");
        Require(decoder.Failed(), "exceeding the cap fails the decoder");
        Require(!decoder.Push(std::vector<std::byte>(1)), "a failed decoder refuses more");
        Require(!decoder.Pop().has_value(), "a failed decoder pops nothing");
    }
    {
        FrameDecoder decoder;
        Require(!decoder.Push(std::vector<std::byte>(kCap + 1)),
                "a single oversized push is refused");
        Require(decoder.Failed(), "a single oversized push fails the decoder");
    }
}

void TestSequenceWrap() {
    using namespace gnl;

    Require(IsNewerSequence(0, 0xffffffffu), "wrap sequence is newer");
    Require(!IsNewerSequence(0xffffffffu, 0), "old wrapped sequence rejected");
    Require(!IsNewerSequence(17, 17), "equal sequence is not newer");
    Require(IsNewerSequence(18, 17), "the next sequence is newer");
    Require(!IsNewerSequence(17, 18), "the previous sequence is not newer");
}

} // namespace

int main() {
    TestGoldenControlFrame();
    TestGoldenDatagram();
    TestFraming();
    TestDatagramRoundTrip();
    TestDatagramRejection();
    TestFrameHeaderRejection();
    TestFrameBufferCap();
    TestSequenceWrap();

    std::cout << "game_network_tests: all checks passed\n";
    return 0;
}
