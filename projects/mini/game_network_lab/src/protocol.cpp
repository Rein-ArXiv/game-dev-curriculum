#include "protocol.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace gnl {
namespace {

constexpr std::size_t kMaxBufferedControlBytes =
    kMaxControlPayload + kFrameHeaderBytes;

std::uint16_t LoadU16(const std::byte* data) {
    return (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(data[0])) << 8u) |
           static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(data[1]));
}

std::uint32_t LoadU32(const std::byte* data) {
    return (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[0])) << 24u) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[1])) << 16u) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[2])) << 8u) |
           static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[3]));
}

std::uint64_t LoadU64(const std::byte* data) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8u) |
                static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(data[i]));
    }
    return value;
}

} // namespace

void AppendU16(std::vector<std::byte>& output, std::uint16_t value) {
    output.push_back(static_cast<std::byte>((value >> 8u) & 0xffu));
    output.push_back(static_cast<std::byte>(value & 0xffu));
}

void AppendU32(std::vector<std::byte>& output, std::uint32_t value) {
    output.push_back(static_cast<std::byte>((value >> 24u) & 0xffu));
    output.push_back(static_cast<std::byte>((value >> 16u) & 0xffu));
    output.push_back(static_cast<std::byte>((value >> 8u) & 0xffu));
    output.push_back(static_cast<std::byte>(value & 0xffu));
}

void AppendU64(std::vector<std::byte>& output, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        output.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
    }
}

std::optional<std::uint16_t> ReadU16(std::span<const std::byte> input,
                                     std::size_t& offset) {
    if (offset > input.size() || input.size() - offset < 2) return std::nullopt;
    std::uint16_t value = LoadU16(input.data() + offset);
    offset += 2;
    return value;
}

std::optional<std::uint32_t> ReadU32(std::span<const std::byte> input,
                                     std::size_t& offset) {
    if (offset > input.size() || input.size() - offset < 4) return std::nullopt;
    std::uint32_t value = LoadU32(input.data() + offset);
    offset += 4;
    return value;
}

std::optional<std::uint64_t> ReadU64(std::span<const std::byte> input,
                                     std::size_t& offset) {
    if (offset > input.size() || input.size() - offset < 8) return std::nullopt;
    std::uint64_t value = LoadU64(input.data() + offset);
    offset += 8;
    return value;
}

std::vector<std::byte> EncodeControl(ControlType type,
                                     std::span<const std::byte> payload) {
    if (payload.size() > kMaxControlPayload) return {};

    std::vector<std::byte> output;
    output.reserve(kFrameHeaderBytes + payload.size());
    AppendU32(output, kProtocolMagic);
    AppendU16(output, kProtocolVersion);
    AppendU16(output, static_cast<std::uint16_t>(type));
    AppendU32(output, static_cast<std::uint32_t>(payload.size()));
    output.insert(output.end(), payload.begin(), payload.end());
    return output;
}

bool FrameDecoder::Push(std::span<const std::byte> bytes) {
    if (failed_) return false;
    if (bytes.size() > kMaxBufferedControlBytes - BufferedBytes()) {
        failed_ = true;
        return false;
    }
    Compact();
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
    return true;
}

std::optional<ControlMessage> FrameDecoder::Pop() {
    if (failed_) return std::nullopt;
    std::size_t available = BufferedBytes();
    if (available < kFrameHeaderBytes) return std::nullopt;

    const std::byte* header = buffer_.data() + readOffset_;
    std::uint32_t magic = LoadU32(header);
    std::uint16_t version = LoadU16(header + 4);
    std::uint16_t type = LoadU16(header + 6);
    std::uint32_t payloadBytes = LoadU32(header + 8);

    if (magic != kProtocolMagic || version != kProtocolVersion ||
        payloadBytes > kMaxControlPayload) {
        failed_ = true;
        return std::nullopt;
    }

    std::size_t frameBytes = kFrameHeaderBytes + payloadBytes;
    if (available < frameBytes) return std::nullopt;

    ControlMessage message;
    message.type = static_cast<ControlType>(type);
    const std::byte* payload = header + kFrameHeaderBytes;
    message.payload.assign(payload, payload + payloadBytes);
    readOffset_ += frameBytes;
    Compact();
    return message;
}

void FrameDecoder::Compact() {
    if (readOffset_ == 0) return;
    if (readOffset_ == buffer_.size()) {
        buffer_.clear();
        readOffset_ = 0;
        return;
    }
    if (readOffset_ < buffer_.size() / 2) return;
    buffer_.erase(buffer_.begin(), buffer_.begin() +
        static_cast<std::ptrdiff_t>(readOffset_));
    readOffset_ = 0;
}

std::vector<std::byte> EncodeDatagram(DatagramType type,
                                      ConnectionId connection,
                                      std::uint32_t sequence,
                                      std::span<const std::byte> payload) {
    if (payload.size() > kMaxDatagramPayload) return {};

    std::vector<std::byte> output;
    output.reserve(kDatagramHeaderBytes + payload.size());
    AppendU32(output, kProtocolMagic);
    AppendU16(output, kProtocolVersion);
    AppendU16(output, static_cast<std::uint16_t>(type));
    AppendU64(output, connection);
    AppendU32(output, sequence);
    AppendU16(output, static_cast<std::uint16_t>(payload.size()));
    AppendU16(output, 0);
    output.insert(output.end(), payload.begin(), payload.end());
    return output;
}

std::optional<DatagramMessage> DecodeDatagram(std::span<const std::byte> bytes) {
    if (bytes.size() < kDatagramHeaderBytes) return std::nullopt;
    std::size_t offset = 0;
    auto magic = ReadU32(bytes, offset);
    auto version = ReadU16(bytes, offset);
    auto type = ReadU16(bytes, offset);
    auto connection = ReadU64(bytes, offset);
    auto sequence = ReadU32(bytes, offset);
    auto payloadBytes = ReadU16(bytes, offset);
    auto reserved = ReadU16(bytes, offset);

    if (!magic || !version || !type || !connection || !sequence ||
        !payloadBytes || !reserved) return std::nullopt;
    if (*magic != kProtocolMagic || *version != kProtocolVersion ||
        *reserved != 0 || *payloadBytes > kMaxDatagramPayload) return std::nullopt;
    if (bytes.size() != kDatagramHeaderBytes + *payloadBytes) return std::nullopt;

    DatagramMessage message;
    message.type = static_cast<DatagramType>(*type);
    message.connection = *connection;
    message.sequence = *sequence;
    message.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                           bytes.end());
    return message;
}

bool IsNewerSequence(std::uint32_t lhs, std::uint32_t rhs) {
    return lhs != rhs && static_cast<std::int32_t>(lhs - rhs) > 0;
}

} // namespace gnl

