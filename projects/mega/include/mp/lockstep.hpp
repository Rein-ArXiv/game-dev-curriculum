// lockstep — 입력 동기 lockstep. 모든 피어의 입력이 모여야 그 프레임을 전진한다.
//
// 각 피어는 자기 입력을 inputDelay 프레임 뒤에 적용되도록 미리 보낸다(지연으로
// 왕복 시간을 흡수). 어떤 프레임 F 를 전진하려면 *모든 피어의 F 입력*이 있어야
// 한다 — 하나라도 없으면 스톨한다. 모든 피어가 같은 입력으로 같은 결정적 시뮬을
// 돌리므로, 상태는 교환하지 않고도 자동으로 동일하다(checksum 으로 확인).
//
// 입력만 오가 대역폭이 작고, 클라이언트가 상태를 못 만들어 치트에 강하다. 대신
// 가장 느린 피어가 전체를 막는다(netcode lockstep 챕터).

#pragma once

#include "mp/det_sim.hpp"
#include "mp/input.hpp"

#include <map>
#include <vector>

namespace mega {
namespace mp {

class Lockstep {
public:
    void Init(int numPlayers, int inputDelay = 2) {
        numPlayers_ = numPlayers;
        inputDelay_ = inputDelay;
        state_.Init(numPlayers);
    }

    // 피어 player 의 frame 프레임 입력 등록(중복 등록은 무시).
    void SubmitInput(uint32_t frame, int player, const Input& in) {
        auto& slot = frames_[frame];
        if (slot.inputs.empty()) { slot.inputs.resize(numPlayers_); slot.have.assign(numPlayers_, false); }
        if (player >= 0 && player < numPlayers_ && !slot.have[player]) {
            slot.inputs[player] = in;
            slot.have[player] = true;
            ++slot.count;
        }
    }

    // 현재 프레임의 모든 입력이 모였는가.
    bool CanAdvance() const {
        auto it = frames_.find(state_.frame);
        return it != frames_.end() && it->second.count == numPlayers_;
    }

    // 한 프레임 전진(CanAdvance 가 true 일 때만 호출).
    void Advance() {
        auto it = frames_.find(state_.frame);
        if (it == frames_.end() || it->second.count != numPlayers_) return;
        state_.Step(it->second.inputs);
        frames_.erase(it);     // 소비한 프레임 입력 정리
    }

    int      InputDelay() const { return inputDelay_; }
    uint32_t Frame()      const { return state_.frame; }
    uint64_t Checksum()   const { return state_.Checksum(); }
    const DetState& State() const { return state_; }

private:
    struct FrameInputs {
        std::vector<Input> inputs;
        std::vector<bool>  have;
        int                count = 0;
    };

    DetState                     state_;
    int                          numPlayers_ = 0;
    int                          inputDelay_ = 2;
    std::map<uint32_t, FrameInputs> frames_;
};

}  // namespace mp
}  // namespace mega
