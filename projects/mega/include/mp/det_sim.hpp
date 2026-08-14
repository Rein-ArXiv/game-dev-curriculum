// det_sim — 결정적 게임 시뮬. 정수 연산만 써서 비트 동일성을 보장한다.
//
// lockstep/rollback 이 desync 하지 않으려면, 같은 입력 열이 어떤 머신에서도 비트
// 동일한 상태를 내야 한다. 부동소수점은 연산 순서·CPU 에 따라 마지막 비트가 달라
// 이를 깨뜨린다. 그래서 위치·속도·RNG 를 전부 정수로 둔다. 매 프레임 checksum 을
// 뽑아 피어 간 비교하면 desync 를 즉시 잡는다(netcode 결정성 챕터).
//
// 직렬화 가능(상태 저장/복원)이라 rollback 의 되감기 스냅샷으로도 쓰인다.

#pragma once

#include "mp/input.hpp"

#include <cstdint>
#include <vector>

namespace mega {
namespace mp {

struct DetState {
    static constexpr int32_t kWorld = 100000;   // 정수 월드(고정소수점 느낌: 1000 = 1유닛)
    static constexpr int32_t kSpeed = 500;      // 프레임당 이동

    uint32_t              frame = 0;
    std::vector<int32_t>  px, py;               // 플레이어 위치(정수)
    std::vector<int32_t>  score;
    uint32_t              rng = 0x1234567u;      // 결정적 RNG 상태

    void Init(int numPlayers) {
        frame = 0;
        px.assign(numPlayers, kWorld / 2);
        py.assign(numPlayers, kWorld / 2);
        score.assign(numPlayers, 0);
        rng = 0x1234567u;
    }

    // 한 프레임 전진. inputs[i] = 플레이어 i 의 입력. 전부 정수 → 결정적.
    void Step(const std::vector<Input>& inputs) {
        uint32_t r = rng;
        auto next = [&] { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return r; };
        int n = (int)px.size();
        for (int i = 0; i < n; ++i) {
            const Input& in = (i < (int)inputs.size()) ? inputs[i] : Input{};
            px[i] += (int32_t)in.moveX * kSpeed;
            py[i] += (int32_t)in.moveY * kSpeed;
            if (px[i] < 0) px[i] = 0; if (px[i] > kWorld) px[i] = kWorld;
            if (py[i] < 0) py[i] = 0; if (py[i] > kWorld) py[i] = kWorld;
            // fire 시 결정적 RNG 로 점수 가산(상태에 RNG 결정성 포함).
            if (in.Fire()) score[i] += (int32_t)(next() % 10);
        }
        rng = r;
        ++frame;
    }

    // 상태 체크섬(FNV-1a 변형). 피어 간 비교로 desync 탐지.
    uint64_t Checksum() const {
        uint64_t h = 1469598103934665603ull;
        auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
        mix(frame);
        mix(rng);
        for (size_t i = 0; i < px.size(); ++i) { mix((uint32_t)px[i]); mix((uint32_t)py[i]); mix((uint32_t)score[i]); }
        return h;
    }
};

}  // namespace mp
}  // namespace mega
