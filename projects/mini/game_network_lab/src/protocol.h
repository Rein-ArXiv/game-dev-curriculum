#pragma once

#include "network_types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace gnl {

constexpr std::uint32_t kProtocolMagic = 0x474E4C31u;
constexpr std::uint16_t kProtocolVersion = 1;
constexpr std::size_t kFrameHeaderBytes = 12;
constexpr std::size_t kDatagramHeaderBytes = 24;
constexpr std::size_t kMaxControlPayload = 64 * 1024;
constexpr std::size_t kMaxDatagramPayload = 1200;

enum class ControlType : std::uint16_t {
    Hello = 1,
    Welcome = 2,
    Ping = 3,
    Pong = 4,
    Error = 5,
};

enum class DatagramType : std::uint16_t {
    Connect = 1,
    ConnectAccepted = 2,
    Input = 3,
    Snapshot = 4,
};

struct ControlMessage {
    ControlType type{};
    std::vector<std::byte> payload;
};

struct DatagramMessage {
    DatagramType type{};
    ConnectionId connection = 0;
    std::uint32_t sequence = 0;
    std::vector<std::byte> payload;
};

class FrameDecoder {
public:
    bool Push(std::span<const std::byte> bytes);
    std::optional<ControlMessage> Pop();
    bool Failed() const { return failed_; }
    std::size_t BufferedBytes() const { return buffer_.size() - readOffset_; }

private:
    void Compact();

    std::vector<std::byte> buffer_;
    std::size_t readOffset_ = 0;
    bool failed_ = false;
};

std::vector<std::byte> EncodeControl(ControlType type,
                                     std::span<const std::byte> payload);
std::vector<std::byte> EncodeDatagram(DatagramType type,
                                      ConnectionId connection,
                                      std::uint32_t sequence,
                                      std::span<const std::byte> payload);
std::optional<DatagramMessage> DecodeDatagram(std::span<const std::byte> bytes);

void AppendU16(std::vector<std::byte>& output, std::uint16_t value);
void AppendU32(std::vector<std::byte>& output, std::uint32_t value);
void AppendU64(std::vector<std::byte>& output, std::uint64_t value);
std::optional<std::uint16_t> ReadU16(std::span<const std::byte> input,
                                     std::size_t& offset);
std::optional<std::uint32_t> ReadU32(std::span<const std::byte> input,
                                     std::size_t& offset);
std::optional<std::uint64_t> ReadU64(std::span<const std::byte> input,
                                     std::size_t& offset);

bool IsNewerSequence(std::uint32_t lhs, std::uint32_t rhs);

} // namespace gnl

