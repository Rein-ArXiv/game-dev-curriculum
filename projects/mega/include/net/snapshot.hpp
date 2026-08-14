// snapshot — 상태 스냅샷 + 보간 버퍼. 듬성한 스냅샷을 부드러운 표시로.
//
// 호스트는 대역폭을 아끼려 스냅샷을 듬성하게(예: 20Hz) 보낸다. 클라이언트가 받은
// 즉시 그리면 20Hz 로 끊긴다. 그래서 최근 스냅샷을 버퍼에 쌓고, *약간 과거*
// (보간 지연)의 렌더 시각에서 두 스냅샷 사이를 보간해 60fps 로 부드럽게 그린다.
// 손실이 나도 다음 스냅샷이 오면 따라잡으므로, 스냅샷 자체는 비신뢰로 보낸다
// (최신값만 의미 있음 — netcode 시리즈 보간 챕터).
//
// 스냅샷 직렬화는 공용 바이너리 archive를 쓴다(EntitySnap은 trivially copyable).
//
// 버퍼(SnapshotBufferT)는 스냅샷 타입에 무관하다. 요구하는 것은 `uint32_t tick`
// 멤버 하나뿐이고, 타입별 보간은 Sample() 에 넘기는 blend 함수가 맡는다. 그래서
// 게임 고유 스냅샷(game/defense_protocol.hpp 의 wire::Snapshot)도 같은 버퍼를
// 쓴다 — 제품이 "내 스냅샷 타입은 안 맞으니까"를 이유로 사본을 만들 여지를 없앤다.

#pragma once

#include "math/vec.hpp"
#include "net/sequence.hpp"
#include "serialize/archive.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace mega {
namespace net {

struct EntitySnap {
    uint32_t   id  = 0;
    math::Vec2 pos{0, 0};
    float      hp  = 0;
};

struct Snapshot {
    uint32_t                tick = 0;
    std::vector<EntitySnap> entities;

    template <class Ar>
    void Serialize(Ar& ar) { ar(tick, entities); }
};

inline std::vector<uint8_t> WriteSnapshot(Snapshot& s) {
    BinaryArchive ar = BinaryArchive::Writer();
    ar(s);
    return ar.Data();
}
inline bool ReadSnapshot(const std::vector<uint8_t>& bytes, Snapshot& s) {
    BinaryArchive ar = BinaryArchive::Reader(bytes.data(), bytes.size());
    ar(s);
    return !ar.Error();
}

// 최근 스냅샷을 보관하고, 임의의(분수) 렌더 틱에서 보간한다.
// Snap 에 요구하는 것은 `uint32_t tick` 뿐이다.
template <class Snap>
class SnapshotBufferT {
public:
    explicit SnapshotBufferT(size_t keep = 16) : keep_(keep ? keep : 1) {}

    // 받아들였으면 true. 이미 지나간 틱은 false(호출자가 '살아 있는 링크'를
    // 판정할 때 이 구분이 필요하다).
    bool Add(Snap s) {
        if (!snaps_.empty()) {
            if (s.tick == snaps_.back().tick) {
                snaps_.back() = std::move(s);
                return true;
            }
            if (!SequenceMoreRecent(s.tick, snaps_.back().tick)) return false;
            // float render ticks cannot interpolate across uint32 wrap. Start a
            // fresh interpolation epoch when the monotonic counter wraps.
            if (s.tick < snaps_.back().tick) snaps_.clear();
        }
        if (snaps_.empty()) ++epoch_;
        snaps_.push_back(std::move(s));
        while (snaps_.size() > keep_) snaps_.pop_front();
        return true;
    }

    bool Empty() const { return snaps_.empty(); }
    size_t Size() const { return snaps_.size(); }
    uint32_t NewestTick() const { return snaps_.empty() ? 0 : snaps_.back().tick; }
    uint32_t OldestTick() const { return snaps_.empty() ? 0 : snaps_.front().tick; }
    const Snap* Newest() const { return snaps_.empty() ? nullptr : &snaps_.back(); }
    const Snap* Oldest() const { return snaps_.empty() ? nullptr : &snaps_.front(); }

    // 보간 타임라인의 세대. 버퍼가 비어 있다가 첫 스냅샷을 받거나, 카운터가 한
    // 바퀴 돌아 버퍼를 비웠을 때 증가한다 — 분수 렌더 시계를 다시 앵커링해야 하는
    // 시점이 정확히 이 순간이라, 시계를 굴리는 쪽(session.hpp)이 이걸 본다.
    uint32_t Epoch() const { return epoch_; }

    // renderTick(분수) 시점의 스냅샷. blend(a, b, t) 가 타입별 보간을 맡는다.
    template <class Blend>
    Snap Sample(double renderTick, Blend&& blend) const {
        if (snaps_.empty()) return Snap{};
        if (snaps_.size() == 1) return snaps_.back();
        if (renderTick <= static_cast<double>(snaps_.front().tick)) return snaps_.front();
        if (renderTick >= static_cast<double>(snaps_.back().tick)) return snaps_.back();

        // renderTick 를 감싸는 연속 두 스냅샷 a,b 탐색.
        size_t bi = 1;
        for (size_t i = 1; i < snaps_.size(); ++i)
            if (static_cast<double>(snaps_[i].tick) >= renderTick) {
                bi = i;
                break;
            }
        const Snap& a = snaps_[bi - 1];
        const Snap& b = snaps_[bi];
        double span = static_cast<double>(b.tick - a.tick);
        float t = span > 0.0
            ? static_cast<float>((renderTick - static_cast<double>(a.tick)) / span)
            : 0.0f;
        return blend(a, b, std::clamp(t, 0.0f, 1.0f));
    }

private:
    std::deque<Snap> snaps_;
    size_t           keep_;
    uint32_t         epoch_ = 0;
};

inline const EntitySnap* FindEntity(const Snapshot& s, uint32_t id) {
    for (const auto& e : s.entities) if (e.id == id) return &e;
    return nullptr;
}

// 두 스냅샷 사이 엔티티 보간(id 로 대응). b 에만 있는 엔티티는 보간할 과거가
// 없으므로 b 값을 그대로 쓴다.
inline Snapshot LerpSnapshot(const Snapshot& a, const Snapshot& b, float t) {
    Snapshot out;
    out.tick = b.tick;
    out.entities.reserve(b.entities.size());
    for (const EntitySnap& eb : b.entities) {
        const EntitySnap* ea = FindEntity(a, eb.id);
        if (!ea) {
            out.entities.push_back(eb);                // 새로 등장: 보간 불가
            continue;
        }
        EntitySnap e;
        e.id  = eb.id;
        e.pos = ea->pos + (eb.pos - ea->pos) * t;      // Vec2 보간
        e.hp  = ea->hp + (eb.hp - ea->hp) * t;
        out.entities.push_back(e);
    }
    return out;
}

// 엔진 기본 스냅샷용 버퍼.
class SnapshotBuffer : public SnapshotBufferT<Snapshot> {
public:
    explicit SnapshotBuffer(size_t keep = 16) : SnapshotBufferT<Snapshot>(keep) {}

    // renderTick(분수) 시점의 엔티티 상태를 보간해 반환.
    std::vector<EntitySnap> Sample(double renderTick) const {
        return SnapshotBufferT<Snapshot>::Sample(renderTick, LerpSnapshot).entities;
    }
};

}  // namespace net
}  // namespace mega
