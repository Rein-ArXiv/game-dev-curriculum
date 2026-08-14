// packet — 패킷 헤더(seq/ack/ackBits)와 바이너리 직렬화.
//
// 모든 패킷에 시퀀스 번호와 "내가 받은 상대 패킷" 정보(ack + 직전 32개 비트필드)를
// 실어 보낸다. 이 한 헤더만으로 양쪽이 손실을 감지하고 RTT 를 잴 수 있다 — 신뢰
// 채널의 토대다(netcode 시리즈 신뢰성 UDP).
//
// seq 는 16비트라 65536 마다 한 바퀴 돈다. 비교는 순환 비교(wrap-aware)로 한다.

#pragma once

#include "serialize/archive.h"

#include <cstdint>
#include <vector>

namespace mega {
namespace net {

struct PacketHeader {
    uint16_t seq     = 0;          // 이 패킷의 시퀀스
    uint16_t ack     = 0;          // 마지막으로 받은 상대 시퀀스
    uint32_t ackBits = 0;          // ack-1 .. ack-32 수신 여부 비트필드

    template <class Ar>
    void Serialize(Ar& ar) { ar(seq, ack, ackBits); }
};

// 시퀀스 순환 비교: a 가 b 보다 "더 최신"인가(wrap 고려).
inline bool SeqGreater(uint16_t a, uint16_t b) {
    return ((a > b) && (a - b <= 32768)) || ((a < b) && (b - a > 32768));
}
inline int SeqDiff(uint16_t a, uint16_t b) {           // a - b (부호 있는 순환 거리)
    int d = (int)a - (int)b;
    if (d > 32768) d -= 65536;
    if (d < -32768) d += 65536;
    return d;
}

// 헤더 + 페이로드를 바이트로 직렬화.
inline std::vector<uint8_t> WritePacket(const PacketHeader& h, const std::vector<uint8_t>& payload) {
    BinaryArchive ar = BinaryArchive::Writer();
    PacketHeader hh = h;
    ar(hh);
    std::vector<uint8_t> out = ar.Data();
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

// 바이트에서 헤더를 읽고 페이로드를 분리. 실패 시 false.
inline bool ReadPacket(const std::vector<uint8_t>& bytes, PacketHeader& h, std::vector<uint8_t>& payload) {
    BinaryArchive ar = BinaryArchive::Reader(bytes.data(), bytes.size());
    ar(h);
    if (ar.Error()) return false;
    size_t consumed = ar.BytesRead();
    payload.assign(bytes.begin() + consumed, bytes.end());
    return true;
}

}  // namespace net
}  // namespace mega
