// session — host-authoritative 협동 세션의 양 끝.
//
// 협동(PvE)이라 결정적 lockstep 의 입력 지연 없이, 호스트가 적·물리의 권위를 갖고
// 시뮬한 뒤 스냅샷을 뿌린다. 클라이언트는 스냅샷을 버퍼에 쌓아 보간 지연만큼 과거를
// 그려 부드럽게 표시한다. 클라이언트 입력(타워 건설 등)은 신뢰 채널로 호스트에
// 올려 호스트가 권위적으로 반영한다(이 헤더는 상태·스냅샷 흐름에 집중; 입력은
// reliable_channel 로 별도 전달).
//
// InterpolatedView 가 클라이언트 쪽의 전부다: 버퍼(snapshot.hpp) + 분수 렌더 시계.
// 시계는 두 방식으로 굴릴 수 있다.
//   Advance(ticks)       실시간 경과분만큼 전진 — 프레임 루프가 있는 제품 경로.
//   LatchedRenderTick()  최신 스냅샷에 고정 — 시계가 없는 도구·해석 경로.
// 제품이 프레임 시간으로 시계를 굴려야 한다는 이유로 자기 버퍼를 다시 만드는 일이
// 없도록, 그 전진·클램프 규칙까지 여기에 둔다.

#pragma once

#include "net/snapshot.hpp"
#include "net/transport.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace mega {
namespace net {

// 호스트: 권위 상태 + 틱. 스냅샷을 만든다.
class HostSession {
public:
    uint32_t Tick() const { return tick_; }
    void     Advance() { ++tick_; }

    void Add(uint32_t id, math::Vec2 pos, float hp) { ents_.push_back({id, pos, hp}); }
    std::vector<EntitySnap>&       Entities()       { return ents_; }
    const std::vector<EntitySnap>& Entities() const { return ents_; }

    Snapshot Make() const {
        Snapshot s; s.tick = tick_; s.entities = ents_; return s;
    }

private:
    uint32_t                tick_ = 0;
    std::vector<EntitySnap> ents_;
};

// 보간 렌더 시계 설정.
struct InterpolationClock {
    double delayTicks = 3.0;  // 새 타임라인에서 시계를 최신보다 이만큼 뒤에 건다
    double leadTicks  = 3.0;  // 시계는 최신에 이보다 가까이 다가가지 않는다
    size_t keep       = 16;   // 버퍼가 보관하는 스냅샷 수
};

// 클라이언트 보간 뷰: 스냅샷 버퍼 + 분수 렌더 시계. 스냅샷 타입에 무관하다.
template <class Snap>
class InterpolatedView {
public:
    explicit InterpolatedView(InterpolationClock clock = {})
        : buf_(clock.keep), clock_(clock) {}

    // 받아들였으면 true. 새 타임라인이 시작되면(첫 스냅샷·카운터 순환) 시계를
    // 최신보다 delayTicks 뒤에 다시 건다.
    bool Add(Snap s) {
        if (!buf_.Add(std::move(s))) return false;
        if (buf_.Epoch() != epoch_) {
            epoch_ = buf_.Epoch();
            renderTick_ = static_cast<double>(buf_.NewestTick()) - clock_.delayTicks;
        }
        return true;
    }

    // 실시간 경과분(틱 단위)만큼 시계를 전진시키고 보간 가능한 창으로 클램프한다.
    // 위쪽(최신에 너무 붙음)을 먼저 막고, 아래쪽(버퍼에서 밀려남)을 나중에 막는다 —
    // 버퍼에 스냅샷이 하나뿐이면 아래쪽이 이겨 그 하나를 그대로 보게 된다.
    void Advance(double ticks) {
        if (buf_.Empty()) return;
        renderTick_ += ticks;
        const double ceiling = static_cast<double>(buf_.NewestTick()) - clock_.leadTicks;
        if (renderTick_ > ceiling) renderTick_ = ceiling;
        const double floor = static_cast<double>(buf_.OldestTick());
        if (renderTick_ < floor) renderTick_ = floor;
    }

    bool     Ready() const { return !buf_.Empty(); }
    uint32_t NewestTick() const { return buf_.NewestTick(); }
    const Snap* Newest() const { return buf_.Newest(); }
    size_t   Size() const { return buf_.Size(); }

    double RenderTick() const { return renderTick_; }
    // 시계를 굴리지 않는 호출자용: 최신 스냅샷에 고정된 렌더 시각.
    double LatchedRenderTick() const {
        return static_cast<double>(buf_.NewestTick()) - clock_.delayTicks;
    }

    template <class Blend>
    Snap Render(Blend&& blend) const { return buf_.Sample(renderTick_, blend); }
    template <class Blend>
    Snap SampleAt(double renderTick, Blend&& blend) const {
        return buf_.Sample(renderTick, blend);
    }

private:
    SnapshotBufferT<Snap> buf_;
    InterpolationClock    clock_;
    double                renderTick_ = 0;
    uint32_t              epoch_      = 0;
};

// 클라이언트: 스냅샷 수신 + 보간 렌더(엔진 기본 스냅샷용, 시계 없는 고정 방식).
class ClientView {
public:
    explicit ClientView(double interpDelayTicks = 3.0, size_t keep = 16)
        : view_(InterpolationClock{interpDelayTicks, interpDelayTicks, keep}) {}

    // transport 에서 들어온 스냅샷 바이트를 버퍼에 넣는다.
    void OnSnapshotBytes(const Bytes& bytes) {
        Snapshot s;
        if (ReadSnapshot(bytes, s)) view_.Add(std::move(s));
    }

    bool     Ready() const { return view_.Ready(); }
    uint32_t NewestTick() const { return view_.NewestTick(); }

    // 보간 지연만큼 과거의 렌더 시각에서 엔티티 상태를 보간.
    std::vector<EntitySnap> Render() const {
        return view_.SampleAt(RenderTick(), LerpSnapshot).entities;
    }
    double RenderTick() const { return view_.LatchedRenderTick(); }

private:
    InterpolatedView<Snapshot> view_;
};

}  // namespace net
}  // namespace mega
