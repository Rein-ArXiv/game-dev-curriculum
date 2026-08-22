// byteorder — 엔디안 변환. 직렬화를 고정 엔디안(little)으로 정규화한다.
//
// 세이브·패킷은 항상 little-endian 으로 적고 읽을 때 호스트 엔디안으로 되돌린다.
// 바이트 단위 쉬프트로 구현한 WriteLE/ReadLE 는 *런타임 엔디안과 무관하게* 같은
// 결과를 내므로, big-endian 타깃에서도 동일한 직렬 데이터를 만든다(platform 엔디안
// 챕터). HostToLE 류는 std::endian 으로 네이티브일 때 항등, 아니면 스왑이다.

#pragma once

#include "platform/detect.hpp"

#include <cstdint>
#include <vector>

namespace mega {
namespace platform {

inline uint16_t Swap16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
inline uint32_t Swap32(uint32_t v) {
    return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000u);
}
inline uint64_t Swap64(uint64_t v) {
    return ((uint64_t)Swap32((uint32_t)(v & 0xFFFFFFFFu)) << 32) | Swap32((uint32_t)(v >> 32));
}

// 바이트 단위 little-endian 쓰기/읽기(플랫폼 엔디안 무관 — 항상 같은 직렬 결과).
inline void WriteLE16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
inline void WriteLE32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
inline void WriteLE64(uint8_t* p, uint64_t v) { WriteLE32(p, (uint32_t)(v & 0xFFFFFFFFu)); WriteLE32(p + 4, (uint32_t)(v >> 32)); }

inline uint16_t ReadLE16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t ReadLE32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline uint64_t ReadLE64(const uint8_t* p) { return (uint64_t)ReadLE32(p) | ((uint64_t)ReadLE32(p + 4) << 32); }

// ── 바이트 열에 이어 붙이기 ────────────────────────────────────────
// 직렬화기는 미리 크기를 아는 버퍼가 아니라 자라는 바이트 열에 쓴다. 포인터
// 산술을 호출부에 시키면 그 자리마다 다시 시프트를 적게 되므로 — 그것이 지금
// `game/defense_protocol.hpp` 의 Writer 가 하고 있는 일이다 — 이어 붙이는
// 형태를 여기서 준다. 나가는 바이트는 WriteLE* 와 정확히 같다.
// snippet-begin: append-le
inline void AppendLE8(std::vector<uint8_t>& out, uint8_t v) { out.push_back(v); }
inline void AppendLE16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}
inline void AppendLE32(std::vector<uint8_t>& out, uint32_t v) {
    AppendLE16(out, static_cast<uint16_t>(v));
    AppendLE16(out, static_cast<uint16_t>(v >> 16));
}
inline void AppendLE64(std::vector<uint8_t>& out, uint64_t v) {
    AppendLE32(out, static_cast<uint32_t>(v));
    AppendLE32(out, static_cast<uint32_t>(v >> 32));
}
// snippet-end: append-le

// 경계 검사가 있는 읽기. 범위를 벗어나면 false 를 돌려주고 값을 건드리지 않는다.
inline bool ReadLE16At(const std::vector<uint8_t>& bytes, size_t offset, uint16_t& value) {
    if (offset + 2 > bytes.size()) return false;
    value = ReadLE16(bytes.data() + offset);
    return true;
}
inline bool ReadLE32At(const std::vector<uint8_t>& bytes, size_t offset, uint32_t& value) {
    if (offset + 4 > bytes.size()) return false;
    value = ReadLE32(bytes.data() + offset);
    return true;
}
inline bool ReadLE64At(const std::vector<uint8_t>& bytes, size_t offset, uint64_t& value) {
    if (offset + 8 > bytes.size()) return false;
    value = ReadLE64(bytes.data() + offset);
    return true;
}

// 호스트 ↔ little-endian 정수 변환(네이티브 엔디안 기준).
inline uint32_t HostToLE32(uint32_t v) { return CurrentEndian() == Endian::Little ? v : Swap32(v); }
inline uint32_t LEToHost32(uint32_t v) { return CurrentEndian() == Endian::Little ? v : Swap32(v); }
inline uint16_t HostToLE16(uint16_t v) { return CurrentEndian() == Endian::Little ? v : Swap16(v); }
inline uint16_t LEToHost16(uint16_t v) { return CurrentEndian() == Endian::Little ? v : Swap16(v); }

}  // namespace platform
}  // namespace mega
