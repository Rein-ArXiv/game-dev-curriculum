// packet — 패킷 헤더(seq/ack/ackBits)와 그 위에 서는 델타 기준선.
//
// 모든 패킷에 시퀀스 번호와 "내가 받은 상대 패킷" 정보(ack + 직전 32개 비트필드)를
// 실어 보낸다. 이 한 헤더만으로 양쪽이 손실을 감지하고 RTT 를 잴 수 있다 — 신뢰
// 채널의 토대다(netcode 시리즈 신뢰성 UDP).
//
// seq 는 16비트라 65536 마다 한 바퀴 돈다. 비교는 순환 비교(wrap-aware)로 한다.
//
// ── 이 헤더가 존재하는 이유 ────────────────────────────────────────
// 누적 ACK 하나로는 답할 수 없는 질문이 있다: **지금 이 클라이언트가 확실히 갖고
// 있는 가장 최신 스냅샷은 무엇인가.** 누적 ACK 는 "여기까지 연속으로 받았다"만
// 말하므로, 중간 하나가 빠지면 그 뒤로 아무리 많이 도착해도 경계가 멈춘다.
// 비트필드는 구멍이 뚫린 채로도 개별 도착을 말한다. 그 답이 있어야 호스트가
// **차분의 기준선**을 고를 수 있고, 그래야 적 상한을 64 위로 올리고도 스냅샷이
// 1200바이트 예산 안에 머문다.
//
// 여기 있는 것은 그 답을 내는 최소 재료 넷이다.
//   HeaderAcknowledges  — 헤더 하나가 특정 seq 를 확인해 주는가(구멍 포함).
//   AckWindow           — 받은 것을 ack/ackBits 로 접는다.
//   SequenceBuffer      — seq 로 색인되는 고정 용량 저장소. 넘치면 밀려난다.
//   EncodeDelta/ApplyDelta — 기준선 대비 차분과 그 복원.
//
// 마지막 둘이 바이트열만 보고 게임 타입을 모르는 것은 의도다. 스냅샷의 필드
// 구성은 game/ 의 프로토콜이 소유하고, "무엇이 안 바뀌었는가"는 그 구성을 몰라도
// 셀 수 있다. 여기서 게임 스냅샷 구조체를 알면 net/ 이 game/ 을 향해 거꾸로
// 의존하게 되고, 프로토콜이 필드를 하나 늘릴 때마다 이 파일이 따라 바뀐다.

#pragma once

#include "net/transport.hpp"
#include "serialize/archive.h"

#include <cstddef>
#include <cstdint>
#include <utility>
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

// 비트필드가 덮는 과거 패킷 수. ackBits 가 32비트이므로 이 값도 32다 —
// 인코더와 디코더가 같은 하나를 읽어야 하는 자리라 상수로 둔다.
inline constexpr int kAckBitCount = 32;

// 시퀀스 순환 비교: a 가 b 보다 "더 최신"인가(wrap 고려).
// snippet-begin: seq-wrap
inline bool SeqGreater(uint16_t a, uint16_t b) {
    return ((a > b) && (a - b <= 32768)) || ((a < b) && (b - a > 32768));
}
inline int SeqDiff(uint16_t a, uint16_t b) {           // a - b (부호 있는 순환 거리)
    int d = (int)a - (int)b;
    if (d > 32768) d -= 65536;
    if (d < -32768) d += 65536;
    return d;
}
// snippet-end: seq-wrap

// 헤더 하나가 seq 를 확인해 주는가. ack 그 자체이거나, ack 로부터 1..32 뒤의
// 비트가 켜져 있으면 확인이다. 그 밖(더 미래, 또는 32보다 오래된 과거)은
// **모른다**이지 아니다가 아니다 — 호출자는 이 함수가 false 인 것을 손실의
// 증거로 쓰면 안 되고, 오직 "아직 확인되지 않았다"로만 써야 한다.
// snippet-begin: header-acknowledges
inline bool HeaderAcknowledges(const PacketHeader& header, uint16_t seq) {
    if (seq == header.ack) return true;
    const int back = SeqDiff(header.ack, seq);
    if (back < 1 || back > kAckBitCount) return false;
    return (header.ackBits & (1u << (back - 1))) != 0;
}
// snippet-end: header-acknowledges

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

// ── 송신 시퀀스 발급기 ─────────────────────────────────────────────
// 하는 일은 증가 하나뿐이지만 자리를 따로 두는 이유가 있다. 여러 곳에서 각자
// 세면 같은 링크 위에 시퀀스 공간이 둘이 되고, 그 순간 ack 비트필드는 무엇을
// 확인한 것인지 말할 수 없게 된다.
class PacketSequencer {
public:
    uint16_t Next() { return next_++; }        // 순환은 uint16_t 가 알아서 한다
    uint16_t Peek() const { return next_; }
    void     Reset(uint16_t seq) { next_ = seq; }

private:
    uint16_t next_ = 0;
};

// ── 수신 창 ────────────────────────────────────────────────────────
// 받은 상대 seq 를 ack(최신) + ackBits(그 직전 32개)로 접는다. 재정렬된 과거
// 도착도 자기 비트를 켠다 — 그러지 않으면 지연이 큰 링크에서 멀쩡히 도착한
// 패킷이 손실로 보고된다.
class AckWindow {
public:
    void OnReceived(uint16_t seq) {
        ++received_;
        if (!started_) {
            started_ = true;
            ack_ = seq;
            bits_ = 0;
            return;
        }
        if (SeqGreater(seq, ack_)) {
            const int shift = SeqDiff(seq, ack_);
            // 32칸 이상 건너뛰면 옛 비트는 전부 창 밖이다. 시프트를 그대로 하면
            // 정의되지 않은 동작이므로 여기서 잘라 낸다.
            bits_ = (shift >= kAckBitCount)
                        ? 0u
                        : static_cast<uint32_t>((bits_ << shift) | (1u << (shift - 1)));
            ack_ = seq;
            return;
        }
        if (seq == ack_) return;                        // 중복
        const int back = SeqDiff(ack_, seq);
        if (back >= 1 && back <= kAckBitCount) bits_ |= (1u << (back - 1));
    }

    PacketHeader Header(uint16_t outgoingSeq) const {
        PacketHeader header;
        header.seq = outgoingSeq;
        header.ack = ack_;
        header.ackBits = bits_;
        return header;
    }

    bool     Seen(uint16_t seq) const { return started_ && HeaderAcknowledges(Header(0), seq); }
    uint16_t Ack() const { return ack_; }
    uint32_t AckBits() const { return bits_; }
    bool     Started() const { return started_; }
    uint64_t Received() const { return received_; }

private:
    uint16_t ack_ = 0;
    uint32_t bits_ = 0;
    uint64_t received_ = 0;
    bool     started_ = false;
};

// ── 시퀀스 색인 저장소 ─────────────────────────────────────────────
// seq 하위 비트를 슬롯 번호로 쓰는 고정 용량 표다. 삽입은 항상 성공하고, 같은
// 슬롯에 있던 다른 seq 는 **밀려난다**. 이 밀려남이 델타의 안전장치다 — 기준선이
// 여기서 사라졌다는 사실이 곧 "그 차분은 복원 불가"이고, 그때 풀 스냅샷으로
// 되돌아가면 된다. 무한히 보관하는 자료구조를 쓰면 이 신호가 사라진다.
//
// 순회는 슬롯 번호 오름차순이다. 삽입 순서에 의존하는 순회를 두면 시뮬 경로에서
// 결과가 실행마다 갈릴 수 있어, 애초에 그런 순회를 제공하지 않는다.
template <class T>
class SequenceBuffer {
public:
    explicit SequenceBuffer(size_t capacity = 64) {
        size_t rounded = 1;
        while (rounded < (capacity ? capacity : 1)) rounded <<= 1;
        slots_.resize(rounded);
    }

    T& Insert(uint16_t seq, T value) {
        Slot& slot = slots_[Index(seq)];
        if (!slot.used) ++live_;
        else if (slot.seq != seq) ++evicted_;
        slot.used = true;
        slot.seq = seq;
        slot.value = std::move(value);
        return slot.value;
    }

    T* Find(uint16_t seq) {
        Slot& slot = slots_[Index(seq)];
        return (slot.used && slot.seq == seq) ? &slot.value : nullptr;
    }
    const T* Find(uint16_t seq) const {
        const Slot& slot = slots_[Index(seq)];
        return (slot.used && slot.seq == seq) ? &slot.value : nullptr;
    }

    bool Remove(uint16_t seq) {
        Slot& slot = slots_[Index(seq)];
        if (!slot.used || slot.seq != seq) return false;
        slot.used = false;
        slot.value = T{};
        --live_;
        return true;
    }

    void Clear() {
        for (Slot& slot : slots_) { slot.used = false; slot.value = T{}; }
        live_ = 0;
    }

    // fn(seq, value) 를 슬롯 번호 오름차순으로.
    template <class Fn>
    void ForEach(Fn&& fn) const {
        for (const Slot& slot : slots_)
            if (slot.used) fn(slot.seq, slot.value);
    }

    size_t   Capacity() const { return slots_.size(); }
    size_t   Size() const { return live_; }
    uint64_t Evicted() const { return evicted_; }

private:
    struct Slot { T value{}; uint16_t seq = 0; bool used = false; };
    size_t Index(uint16_t seq) const { return static_cast<size_t>(seq) & (slots_.size() - 1); }

    std::vector<Slot> slots_;
    size_t            live_ = 0;
    uint64_t          evicted_ = 0;
};

// ── 델타 코덱 ──────────────────────────────────────────────────────
// 기준선 바이트열과 현재 바이트열을 비교해 "같은 구간 길이 + 다른 구간 길이 +
// 다른 구간의 원본 바이트"를 반복해 적는다. 복원은 같은 규칙을 거꾸로 밟으므로
// 결과가 원본과 **비트 동일**하다 — 손실 압축이 아니다.
//
// 왜 바이트 단위인가. 스냅샷 한 장에서 실제로 바뀌는 것은 위치·체력처럼 소수의
// 필드이고, id·최대 체력·종류·타워 블록 전체는 프레임 사이에 그대로다. 그
// "그대로"를 세는 데에는 필드가 무엇인지 알 필요가 없다. 대신 대가가 있다:
// 엔티티의 **직렬화 순서가 프레임마다 흔들리면** 같은 값이 다른 자리에 놓여
// 차분이 부풀어 오른다. 이 코덱은 그 순서 안정성을 인코더에 요구한다.
inline constexpr size_t kMaxDeltaPayload = 8192;

inline void PutVarint(Bytes& out, uint32_t value) {
    while (value >= 0x80u) {
        out.push_back(static_cast<uint8_t>(value) | 0x80u);
        value >>= 7;
    }
    out.push_back(static_cast<uint8_t>(value));
}

inline bool GetVarint(const uint8_t*& cursor, const uint8_t* end, uint32_t& value) {
    uint32_t result = 0;
    for (int shift = 0; shift <= 28; shift += 7) {
        if (cursor == end) return false;
        const uint8_t byte = *cursor++;
        // 다섯 번째 바이트는 남은 4비트만 쓸 수 있고 이어지기도 할 수 없다.
        if (shift == 28 && (byte & 0xF0u) != 0) return false;
        result |= static_cast<uint32_t>(byte & 0x7Fu) << shift;
        if ((byte & 0x80u) == 0) { value = result; return true; }
    }
    return false;
}

// 기준선보다 긴 자리는 0 으로 본다. 스냅샷이 자라도(엔티티가 늘어도) 같은
// 코덱으로 다룰 수 있게 하는 규칙이고, 인코더와 디코더가 이 함수 하나를 공유한다.
inline uint8_t DeltaBaseByte(const Bytes& base, size_t index) {
    return index < base.size() ? base[index] : uint8_t{0};
}

// snippet-begin: delta-codec
inline Bytes EncodeDelta(const Bytes& base, const Bytes& current) {
    Bytes out;
    PutVarint(out, static_cast<uint32_t>(current.size()));
    size_t cursor = 0;
    while (cursor < current.size()) {
        size_t same = cursor;
        while (same < current.size() && current[same] == DeltaBaseByte(base, same)) ++same;
        size_t diff = same;
        while (diff < current.size() && current[diff] != DeltaBaseByte(base, diff)) ++diff;
        PutVarint(out, static_cast<uint32_t>(same - cursor));
        PutVarint(out, static_cast<uint32_t>(diff - same));
        out.insert(out.end(),
                   current.begin() + static_cast<std::ptrdiff_t>(same),
                   current.begin() + static_cast<std::ptrdiff_t>(diff));
        cursor = diff;
    }
    return out;
}

inline bool ApplyDelta(const Bytes& base, const Bytes& delta, Bytes& out) {
    const uint8_t* cursor = delta.data();
    const uint8_t* end = cursor + delta.size();
    uint32_t total = 0;
    if (!GetVarint(cursor, end, total)) return false;
    if (total > kMaxDeltaPayload) return false;

    Bytes decoded;
    decoded.reserve(total);
    while (decoded.size() < total) {
        uint32_t same = 0, copy = 0;
        if (!GetVarint(cursor, end, same)) return false;
        if (!GetVarint(cursor, end, copy)) return false;
        if (same == 0 && copy == 0) return false;             // 전진하지 않는 세그먼트
        if (same > total - decoded.size()) return false;
        for (uint32_t i = 0; i < same; ++i)
            decoded.push_back(DeltaBaseByte(base, decoded.size()));
        if (copy > total - decoded.size()) return false;
        if (static_cast<size_t>(end - cursor) < copy) return false;
        decoded.insert(decoded.end(), cursor, cursor + copy);
        cursor += copy;
    }
    if (cursor != end) return false;                          // 잉여 바이트 거부
    out = std::move(decoded);
    return true;
}
// snippet-end: delta-codec

// ── 델타 기준선 저장소 ─────────────────────────────────────────────
// 호스트가 보낸 스냅샷을 seq 로 보관하고, 상대 헤더가 그 seq 를 확인하면 기준선을
// 거기까지 앞당긴다. 두 가지를 절대 하지 않는다.
//
//   1. 기준선을 **뒤로** 되돌리지 않는다. 재정렬된 옛 ack 가 늦게 도착해도
//      그것 때문에 이미 앞선 기준선을 버리면, 클라이언트가 이미 지워 버린
//      과거를 가리키게 된다.
//   2. 보관소에서 밀려난 기준선을 **가리키지 않는다**. Baseline() 이 그때
//      nullptr 을 돌려주는 것이 곧 풀 스냅샷 폴백 신호다.
class BaselineStore {
public:
    struct Entry {
        uint32_t tick = 0;
        Bytes    bytes;
    };

    explicit BaselineStore(size_t capacity = 32) : sent_(capacity) {}

    void Sent(uint16_t seq, uint32_t tick, Bytes bytes) {
        Entry entry;
        entry.tick = tick;
        entry.bytes = std::move(bytes);
        sent_.Insert(seq, std::move(entry));
    }

    // 확인된 seq 중 가장 최신으로 기준선을 옮긴다. 옮겼으면 true.
    bool Acknowledge(const PacketHeader& remote) {
        bool advanced = false;
        sent_.ForEach([&](uint16_t seq, const Entry&) {
            if (!HeaderAcknowledges(remote, seq)) return;
            if (confirmed_ && !SeqGreater(seq, confirmedSeq_)) return;
            confirmedSeq_ = seq;
            confirmed_ = true;
            advanced = true;
        });
        return advanced;
    }

    // 살아 있는 기준선. 없으면 nullptr — 호출자는 풀 스냅샷을 보내야 한다.
    const Entry* Baseline(uint16_t* seqOut = nullptr) const {
        if (!confirmed_) return nullptr;
        const Entry* entry = sent_.Find(confirmedSeq_);
        if (!entry) return nullptr;
        if (seqOut) *seqOut = confirmedSeq_;
        return entry;
    }

    bool     Confirmed() const { return confirmed_; }
    uint16_t ConfirmedSeq() const { return confirmedSeq_; }
    size_t   Live() const { return sent_.Size(); }
    size_t   Capacity() const { return sent_.Capacity(); }
    uint64_t Evicted() const { return sent_.Evicted(); }

    void Reset() {
        sent_.Clear();
        confirmed_ = false;
        confirmedSeq_ = 0;
    }

private:
    SequenceBuffer<Entry> sent_;
    uint16_t              confirmedSeq_ = 0;
    bool                  confirmed_ = false;
};

}  // namespace net
}  // namespace mega
