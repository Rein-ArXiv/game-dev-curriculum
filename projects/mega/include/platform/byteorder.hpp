// byteorder — 엔디안 변환. 직렬화를 고정 엔디안(little)으로 정규화한다.
//
// 세이브·패킷은 항상 little-endian 으로 적고 읽을 때 호스트 엔디안으로 되돌린다.
// 바이트 단위 쉬프트로 구현한 WriteLE/ReadLE 는 *런타임 엔디안과 무관하게* 같은
// 결과를 내므로, big-endian 타깃에서도 동일한 직렬 데이터를 만든다(platform 엔디안
// 챕터). HostToLE 류는 std::endian 으로 네이티브일 때 항등, 아니면 스왑이다.

#pragma once

#include "platform/detect.hpp"

#include <cstdint>

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

// 호스트 ↔ little-endian 정수 변환(네이티브 엔디안 기준).
inline uint32_t HostToLE32(uint32_t v) { return CurrentEndian() == Endian::Little ? v : Swap32(v); }
inline uint32_t LEToHost32(uint32_t v) { return CurrentEndian() == Endian::Little ? v : Swap32(v); }
inline uint16_t HostToLE16(uint16_t v) { return CurrentEndian() == Endian::Little ? v : Swap16(v); }
inline uint16_t LEToHost16(uint16_t v) { return CurrentEndian() == Endian::Little ? v : Swap16(v); }

}  // namespace platform
}  // namespace mega
