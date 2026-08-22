// det_sim — 아바타 서브시뮬. 정수 연산만 써서 비트 동일성을 보장한다.
//
// 예측·되감기가 desync 하지 않으려면, 같은 입력 열이 어떤 머신에서도 비트 동일한
// 상태를 내야 한다. 부동소수점은 연산 순서·컴파일러 최적화에 따라 마지막 비트가
// 달라 이를 깨뜨린다. 그래서 위치·기여도·RNG 를 전부 정수로 둔다. 매 프레임
// checksum 을 뽑아 피어 간 비교하면 desync 를 즉시 잡는다(netcode 결정성 챕터).
//
// 직렬화 가능(상태 저장/복원)이라 rollback 의 되감기 스냅샷으로도 쓰인다.
//
// ── 왜 아바타만 정수인가 ───────────────────────────────────────────
// 이 게임의 권위 시뮬 — 적·타워·발사체·잔해 강체 — 은 float 이고 앞으로도 float 다.
// 강체 적분과 접촉 솔버를 정수로 옮기는 것은 이 게임이 요구한 적 없는 비용이고,
// 그 부분은 애초에 **클라이언트가 예측하지 않는다.** 클라이언트는 호스트의 스냅샷을
// 보간해 그릴 뿐이라 재현할 필요가 없다.
//
// 예측하는 부분만이 비트 동일성을 요구한다. 클라이언트가 자기 아바타를 즉시
// 움직이고 나중에 호스트 확정과 대조하려면, "같은 입력에서 같은 결과"가 두 머신에서
// 성립해야 하기 때문이다. 그 부분이 정확히 이 파일이고, 그래서 이 파일만 정수다.
// 정수화의 비용을 필요한 곳에만 낸다.

#pragma once

#include "mp/input.hpp"

#include <cstdint>
#include <vector>

namespace mega {
namespace mp {

struct DetState {
    static constexpr int32_t kWorld = 100000;   // 정수 월드(고정소수점 느낌: 1000 = 1유닛)
    static constexpr int32_t kSpeed = 500;      // 프레임당 이동
    // 격자 한 변. 제품 맵이 16x16 이므로 같은 수를 쓴다 — 아바타가 "어느 칸에
    // 서 있는가"가 건설 승인 규칙의 입력이라, 두 격자가 다르면 규칙이 성립하지
    // 않는다. 정수 월드를 칸으로 나눈 몫이 칸 크기다.
    static constexpr int32_t kCells = 16;
    static constexpr int32_t kCellUnits = kWorld / kCells;
    static constexpr int32_t kNoBuild = -1;     // 이 프레임에 건설 요청이 없다

    uint32_t              frame = 0;
    std::vector<int32_t>  px, py;               // 아바타 위치(정수)
    std::vector<int32_t>  score;                // 협동 기여도
    // 밟은 칸의 이력을 접은 값. 위치만 비교하면 갈라졌다가 양쪽 다 같은 벽에
    // 눌려 다시 붙은 divergence 를 놓친다 — 그 순간 체크섬이 도로 일치해
    // desync 가 없었던 것처럼 보인다. 이력은 한 번 갈리면 되돌아오지 않는다.
    std::vector<uint32_t> trail;
    // 이 프레임에 요청한 건설 칸. 호스트가 이것을 보고 승인·거부한다.
    std::vector<int32_t>  buildCell;
    uint32_t              rng = 0x1234567u;      // 결정적 RNG 상태

    void Init(int numPlayers) {
        frame = 0;
        px.assign(numPlayers, kWorld / 2);
        py.assign(numPlayers, kWorld / 2);
        score.assign(numPlayers, 0);
        trail.assign(numPlayers, 2166136261u);
        buildCell.assign(numPlayers, kNoBuild);
        rng = 0x1234567u;
    }

    // 좌표 → 칸. 경계(px == kWorld)는 마지막 칸으로 접는다. 이 접기가 없으면
    // 벽에 붙은 아바타의 칸 번호가 격자 밖으로 하나 튀어나간다.
    static int32_t CellOf(int32_t coordinate) {
        const int32_t cell = coordinate / kCellUnits;
        if (cell < 0) return 0;
        return cell >= kCells ? kCells - 1 : cell;
    }
    static int32_t CellIndex(int32_t x, int32_t y) {
        return CellOf(y) * kCells + CellOf(x);
    }
    // 두 칸이 체비셰프 거리 reach 안인가. 호스트의 건설 승인과 클라이언트의
    // 커서 표시가 **같은 함수**를 부른다 — 둘이 갈라지면 눌리는데 안 서는 칸이 생긴다.
    static bool CellsWithinReach(int32_t a, int32_t b, int32_t reach) {
        if (a < 0 || b < 0) return false;
        const int32_t dx = (a % kCells) - (b % kCells);
        const int32_t dy = (a / kCells) - (b / kCells);
        const int32_t ax = dx < 0 ? -dx : dx;
        const int32_t ay = dy < 0 ? -dy : dy;
        return (ax > ay ? ax : ay) <= reach;
    }

    int32_t Cell(int player) const { return CellIndex(px[player], py[player]); }

    // 한 프레임 전진. inputs[i] = 플레이어 i 의 입력. 전부 정수 → 결정적.
    // snippet-begin: det-step
    void Step(const std::vector<Input>& inputs) {
        uint32_t r = rng;
        auto next = [&] { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return r; };
        int n = (int)px.size();
        for (int i = 0; i < n; ++i) {
            const Input& in = (i < (int)inputs.size()) ? inputs[i] : Input{};
            px[i] += (int32_t)in.moveX * kSpeed;
            py[i] += (int32_t)in.moveY * kSpeed;
            // 클램프가 먼저다. 아래 세 줄이 전부 "이 아바타가 실제로 서 있는
            // 자리"를 묻기 때문이다. 다만 이 순서는 **지금 관측되지 않는다** —
            // CellOf 가 자기 몫의 클램프를 따로 하고 있어서, 두 줄을 아래로
            // 옮겨도 어떤 테스트도 죽지 않는다(변이 실험으로 확인했다). 두 겹인
            // 것을 결함으로 보지 않는 이유는 CellOf 의 클램프가 이 함수만을 위한
            // 것이 아니기 때문이다: 호스트가 클라이언트에게 받은 칸 번호를
            // 검증할 때도 같은 함수를 부른다.
            if (px[i] < 0) px[i] = 0; if (px[i] > kWorld) px[i] = kWorld;
            if (py[i] < 0) py[i] = 0; if (py[i] > kWorld) py[i] = kWorld;
            const int32_t cell = CellIndex(px[i], py[i]);
            trail[i] = (trail[i] ^ (uint32_t)cell) * 16777619u;
            buildCell[i] = in.Build() ? cell : kNoBuild;
            // fire 시 결정적 RNG 로 점수 가산(상태에 RNG 결정성 포함).
            if (in.Fire()) score[i] += (int32_t)(next() % 10);
        }
        rng = r;
        ++frame;
    }
    // snippet-end: det-step

    // 상태 체크섬(FNV-1a 변형). 피어 간 비교로 desync 탐지.
    uint64_t Checksum() const {
        uint64_t h = 1469598103934665603ull;
        auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
        mix(frame);
        mix(rng);
        for (size_t i = 0; i < px.size(); ++i) {
            mix((uint32_t)px[i]); mix((uint32_t)py[i]); mix((uint32_t)score[i]);
            mix(trail[i]); mix((uint32_t)buildCell[i]);
        }
        return h;
    }
};

}  // namespace mp
}  // namespace mega
