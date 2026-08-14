// rollback — GGPO 식 예측·되감기·재시뮬.
//
// lockstep 은 모든 입력이 모일 때까지 멈춘다. rollback 은 멈추지 않는다 — 원격
// 입력이 오기 전엔 "직전 입력 반복"으로 예측해 즉시 진행하고(로컬 반응성 확보),
// 확정 입력이 예측과 다르면 그 프레임의 저장 상태로 *되감아* 정정된 입력으로 다시
// 굴린다(resimulate). 결정성 덕에 재시뮬 결과는 "처음부터 그 입력을 안 경우"와
// 비트 동일하다 — 이게 rollback 정확성의 핵심이다(netcode rollback 챕터).
//
// 되감기 깊이는 maxRollback 으로 제한한다(그보다 오래된 정정은 desync 로 처리).

#pragma once

#include "mp/det_sim.hpp"
#include "mp/input.hpp"

#include <map>
#include <vector>

namespace mega {
namespace mp {

class Rollback {
public:
    void Init(int numPlayers, int maxRollback = 8) {
        n_ = numPlayers;
        maxRollback_ = maxRollback;
        cur_.Init(numPlayers);
        frame_ = 0;
        lastDepth_ = 0;
        slots_.clear(); used_.clear(); snap_.clear();
    }

    // (frame, player) 입력 설정. confirmed=true 면 확정(로컬 즉시 / 원격 도착).
    // 이미 시뮬한 과거 프레임의 확정이 사용한 예측과 다르면 되감기·재시뮬한다.
    void SetInput(uint32_t frame, int player, const Input& in, bool confirmed) {
        Slot& s = slots_[frame];
        if ((int)s.in.size() < n_) { s.in.resize(n_); s.conf.assign(n_, false); }
        bool was = s.conf[player];
        Input prev = s.in[player];
        s.in[player] = in;
        if (confirmed) s.conf[player] = true;

        if (confirmed && frame < frame_) {
            auto u = used_.find(frame);
            if (u != used_.end() && u->second[player] != in) {
                Resimulate(frame);                 // 예측 빗나감 → 되감기
            }
            (void)was; (void)prev;
        }
    }

    // 한 프레임 전진(없는 원격 입력은 직전 확정으로 예측).
    void Tick() {
        snap_[frame_] = cur_;                       // 프레임 시작 상태 저장
        std::vector<Input> ins = BuildInputs(frame_);
        used_[frame_] = ins;
        cur_.Step(ins);
        ++frame_;
        Prune();
    }

    uint32_t Frame()     const { return frame_; }
    uint64_t Checksum()  const { return cur_.Checksum(); }
    const DetState& State() const { return cur_; }
    int      LastRollbackDepth() const { return lastDepth_; }

private:
    struct Slot { std::vector<Input> in; std::vector<bool> conf; };

    // frame 시점 player 입력 예측: 확정값 있으면 그것, 없으면 직전 확정 반복.
    Input PredictedInput(uint32_t frame, int p) const {
        auto it = slots_.find(frame);
        if (it != slots_.end() && it->second.conf[p]) return it->second.in[p];
        Input last{};
        for (const auto& [f, s] : slots_) {
            if (f > frame) break;
            if (p < (int)s.conf.size() && s.conf[p]) last = s.in[p];
        }
        return last;
    }
    std::vector<Input> BuildInputs(uint32_t frame) const {
        std::vector<Input> v(n_);
        for (int p = 0; p < n_; ++p) v[p] = PredictedInput(frame, p);
        return v;
    }

    // targetFrame 으로 되감아 현재 프레임(head)까지 재시뮬.
    void Resimulate(uint32_t targetFrame) {
        auto sn = snap_.find(targetFrame);
        if (sn == snap_.end()) return;             // 너무 오래됨(maxRollback 초과)
        uint32_t head = frame_;
        cur_ = sn->second;
        frame_ = targetFrame;
        lastDepth_ = (int)(head - targetFrame);
        while (frame_ < head) Tick();              // 정정된 입력으로 다시 굴림
    }

    void Prune() {
        if (frame_ <= (uint32_t)maxRollback_) return;
        uint32_t keepFrom = frame_ - (uint32_t)maxRollback_;
        for (auto it = snap_.begin(); it != snap_.end();)
            it = (it->first < keepFrom) ? snap_.erase(it) : std::next(it);
    }

    int                                n_ = 0;
    int                                maxRollback_ = 8;
    int                                lastDepth_ = 0;
    uint32_t                           frame_ = 0;
    DetState                           cur_;
    std::map<uint32_t, Slot>           slots_;
    std::map<uint32_t, std::vector<Input>> used_;
    std::map<uint32_t, DetState>       snap_;
};

}  // namespace mp
}  // namespace mega
