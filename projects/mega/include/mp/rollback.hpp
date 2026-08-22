// rollback — GGPO 식 예측·되감기·재시뮬.
//
// lockstep 은 모든 입력이 모일 때까지 멈춘다. rollback 은 멈추지 않는다 — 원격
// 입력이 오기 전엔 "직전 입력 반복"으로 예측해 즉시 진행하고(로컬 반응성 확보),
// 확정 입력이 예측과 다르면 그 프레임의 저장 상태로 *되감아* 정정된 입력으로 다시
// 굴린다(resimulate). 결정성 덕에 재시뮬 결과는 "처음부터 그 입력을 안 경우"와
// 비트 동일하다 — 이게 rollback 정확성의 핵심이다(netcode rollback 챕터).
//
// 되감기 깊이는 maxRollback 으로 제한한다. 그보다 오래된 정정은 되감을 상태가
// 남아 있지 않으므로 **고칠 수 없다** — 그 사실을 조용히 삼키지 않고 세어서
// 보고한다(LateConfirmations). 그것이 재동기화가 필요하다는 유일한 신호다.
//
// ── 두 팔 ──────────────────────────────────────────────────────────
// SetPrediction(false) 면 예측하지 않는다. 모든 플레이어의 입력이 확정될 때까지
// Tick 이 전진을 거부하므로 되감기가 아예 일어나지 않고, 대신 로컬 조작이 왕복
// 시간만큼 늦게 반영된다. 어느 쪽이 나은지는 링크가 정한다. 두 팔의 상태는
// 같아야 하고(같은 확정 입력 열 → 같은 체크섬), 다른 것은 **언제** 그 상태에
// 도달하는가뿐이다 — 그 차이를 Stalls 가 센다.
//
// ── 보관 창 ────────────────────────────────────────────────────────
// 되감기 창 밖의 프레임은 스냅샷만이 아니라 입력 표도 버린다. 버리기 전에 그
// 프레임의 확정 입력을 플레이어별 "바닥값"으로 접어 두므로 예측 규칙은 그대로다 —
// 창 밖에서 마지막으로 확정된 입력이 무엇인지는 접힌 값이 기억한다. 접지 않고
// 전부 들고 있으면 표가 매치 길이에 비례해 자라고, 예측 한 번이 그 표를 통째로
// 훑는다(프레임당 O(전체 프레임 수)).

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
        maxRollback_ = maxRollback < 1 ? 1 : maxRollback;
        cur_.Init(numPlayers);
        frame_ = 0;
        lastDepth_ = 0;
        maxDepth_ = 0;
        stalls_ = 0;
        lateConfirmations_ = 0;
        rollbacks_ = 0;
        resimulating_ = false;
        slots_.clear(); used_.clear(); snap_.clear();
        floor_.assign(numPlayers, Input{});
    }

    void SetPrediction(bool on) { predict_ = on; }
    bool Prediction() const { return predict_; }

    // (frame, player) 입력 설정. confirmed=true 면 확정(로컬 즉시 / 원격 도착).
    // 이미 시뮬한 과거 프레임의 확정이 사용한 예측과 다르면 되감기·재시뮬한다.
    // snippet-begin: rollback-set-input
    void SetInput(uint32_t frame, int player, const Input& in, bool confirmed) {
        if (player < 0 || player >= n_) return;
        if (confirmed && frame_ > (uint32_t)maxRollback_ &&
            frame < frame_ - (uint32_t)maxRollback_) {
            // 되감을 상태가 이미 버려졌다. 여기서 조용히 반환하면 이 클라이언트는
            // 호스트와 다른 상태를 들고 아무 일 없다는 듯 계속 돈다.
            ++lateConfirmations_;
            return;
        }
        Slot& s = slots_[frame];
        if ((int)s.in.size() < n_) { s.in.resize(n_); s.conf.assign(n_, false); }
        s.in[player] = in;
        if (confirmed) s.conf[player] = true;

        if (confirmed && frame < frame_) {
            auto u = used_.find(frame);
            if (u != used_.end() && u->second[player] != in) {
                Resimulate(frame);                 // 예측 빗나감 → 되감기
            }
        }
    }
    // snippet-end: rollback-set-input

    // 한 프레임 전진. 예측 팔에서는 없는 원격 입력을 직전 확정으로 메우고 항상
    // 전진한다. 비예측 팔에서는 전원의 입력이 확정되기 전까지 전진하지 않는다.
    bool Tick() {
        if (!predict_ && !resimulating_ && !FullyConfirmed(frame_)) {
            ++stalls_;
            return false;
        }
        snap_[frame_] = cur_;                       // 프레임 시작 상태 저장
        std::vector<Input> ins = BuildInputs(frame_);
        used_[frame_] = ins;
        cur_.Step(ins);
        ++frame_;
        Prune();
        return true;
    }

    uint32_t Frame()     const { return frame_; }
    uint64_t Checksum()  const { return cur_.Checksum(); }
    const DetState& State() const { return cur_; }
    int      LastRollbackDepth() const { return lastDepth_; }
    int      MaxRollbackDepth() const { return maxDepth_; }
    int      MaxRollback() const { return maxRollback_; }
    uint64_t Rollbacks() const { return rollbacks_; }
    uint64_t Stalls() const { return stalls_; }
    // 되감을 수 없었던 정정의 수. 0 이 아니면 이 피어의 상태는 호스트와 갈렸을
    // 수 있고, 되감기로는 회복되지 않는다 — 재동기화가 필요하다는 신호다.
    uint64_t LateConfirmations() const { return lateConfirmations_; }
    bool     DesyncSuspected() const { return lateConfirmations_ > 0; }
    // 보관 중인 프레임 수(스냅샷·입력 표). 창 밖을 버리는지 확인하는 관측 지점.
    size_t   RetainedFrames() const { return snap_.size(); }
    size_t   RetainedInputSlots() const { return slots_.size(); }

    // 이 프레임의 입력이 전원 확정인가.
    bool FullyConfirmed(uint32_t frame) const {
        auto it = slots_.find(frame);
        if (it == slots_.end()) return false;
        if ((int)it->second.conf.size() < n_) return false;
        for (int p = 0; p < n_; ++p) if (!it->second.conf[p]) return false;
        return true;
    }

private:
    struct Slot { std::vector<Input> in; std::vector<bool> conf; };

    // frame 시점 player 입력 예측: 확정값 있으면 그것, 없으면 직전 확정 반복.
    // 창 밖으로 밀려난 과거는 floor_ 가 대신 기억한다.
    Input PredictedInput(uint32_t frame, int p) const {
        auto it = slots_.find(frame);
        if (it != slots_.end() && it->second.conf[p]) return it->second.in[p];
        Input last = floor_[p];
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
        if (sn == snap_.end()) { ++lateConfirmations_; return; }
        uint32_t head = frame_;
        cur_ = sn->second;
        frame_ = targetFrame;
        lastDepth_ = (int)(head - targetFrame);
        if (lastDepth_ > maxDepth_) maxDepth_ = lastDepth_;
        ++rollbacks_;
        // 재시뮬은 이미 지난 프레임을 다시 미는 것이라 비예측 팔에서도 멈추면
        // 안 된다 — 멈추면 head 로 돌아가지 못하고 상태가 과거에 남는다.
        resimulating_ = true;
        while (frame_ < head) Tick();               // 정정된 입력으로 다시 굴림
        resimulating_ = false;
    }

    // 창 밖 프레임을 버린다. 입력 표는 버리기 전에 확정값을 바닥으로 접는다.
    void Prune() {
        if (frame_ <= (uint32_t)maxRollback_) return;
        const uint32_t keepFrom = frame_ - (uint32_t)maxRollback_;
        for (auto it = snap_.begin(); it != snap_.end();)
            it = (it->first < keepFrom) ? snap_.erase(it) : std::next(it);
        for (auto it = used_.begin(); it != used_.end();)
            it = (it->first < keepFrom) ? used_.erase(it) : std::next(it);
        // 오름차순 순회라 접는 순서가 프레임 순서와 같다.
        for (auto it = slots_.begin(); it != slots_.end();) {
            if (it->first >= keepFrom) break;
            for (int p = 0; p < n_ && p < (int)it->second.conf.size(); ++p)
                if (it->second.conf[p]) floor_[p] = it->second.in[p];
            it = slots_.erase(it);
        }
    }

    int                                n_ = 0;
    int                                maxRollback_ = 8;
    int                                lastDepth_ = 0;
    int                                maxDepth_ = 0;
    uint32_t                           frame_ = 0;
    uint64_t                           rollbacks_ = 0;
    uint64_t                           stalls_ = 0;
    uint64_t                           lateConfirmations_ = 0;
    bool                               predict_ = true;
    bool                               resimulating_ = false;
    DetState                           cur_;
    std::vector<Input>                 floor_;
    std::map<uint32_t, Slot>           slots_;
    std::map<uint32_t, std::vector<Input>> used_;
    std::map<uint32_t, DetState>       snap_;
};

}  // namespace mp
}  // namespace mega
