// debris — 파괴가 남긴 파편이 실제 강체가 되고, 멈춘 파편이 지형이 된다.
//
// 이 파일이 만드는 경계 하나가 이 편의 전부다: **물리 결과가 AI 입력이 된다.**
// 타워가 팔리거나 발파가 바위를 뚫으면 파편이 떨어져 굴러가고, 속도가 임계 아래로
// 일정 틱 유지되면 그 자리에 눌러앉아 칸을 막는다. 막힌 칸은 통행 격자에서
// 빠지고, 다음 경로 계산이 다른 길을 낸다.
//
// 왜 위에서 아래로 떨어지는가 — 이 아레나는 **경사면**이다. 기울인 카메라가
// 보여 주는 지면은 평면이 아니라 코어 쪽으로 기운 비탈이고, 잔해는 그 비탈을
// 따라 +y(화면 아래) 로 미끄러져 바위와 서로에게 걸려 쌓인다. 중력이 없는
// 순수 top-down 이면 파편은 생긴 자리에 그대로 멈춰 물리가 필요 없어진다.
//
// 실행 조건: 절차 맵에서만 돈다. Classic 맵은 경로가 파일이 정한 고정 회랑이라
// 막을 칸이라는 개념이 없다 — 스위치가 생기기 전의 동작이 기본값이어야 한다는
// 규약이 여기서도 그대로다.

#pragma once

#include "game/defense_map.hpp"
#include "game/switches.hpp"
#include "physics/world.hpp"

#include <cstdint>
#include <vector>

namespace mega {
namespace game {

class DebrisField {
public:
    // 동시 파편 상한. 안착한 파편은 정적으로 굳어 이 수에서 빠진다.
    static constexpr size_t kMaxLive = 96;
    // 안착 판정: 속도가 이보다 작은 상태가 이 틱 수만큼 이어지면 지형이 된다.
    //
    // 임계가 왜 8 px/s 인가. 중력 340 px/s^2 · 1/60초 스텝이면 접촉을 유지한 바디도
    // 매 스텝 5.7 px/s 를 새로 받고 솔버가 그것을 되돌린다. 그 잔여 속도가 임계보다
    // 크면 "멈춘 파편"이 영원히 멈추지 않은 것으로 판정돼 잔해가 지형이 되지 못한다.
    // 8 px/s 는 한 스텝에 0.13 픽셀 — 화면에서 정지와 구분되지 않는 속도다.
    static constexpr float  kSettleSpeed = 8.0f;
    static constexpr int    kSettleTicks = 20;

    void Configure(const Switches& switches) {
        broadphase_ = switches.broadphase;
        world_.integrator = switches.integrator == IntegratorKind::Explicit
            ? physics::Integrator::Explicit : physics::Integrator::Symplectic;
        world_.velocityIterations = switches.solverIters;
    }

    // 맵의 바위를 정적 바디로 세운다. 맵이 바뀌면 통째로 다시 짓는다 — 칸 하나가
    // 열렸는데 그 자리에 정적 상자가 남아 있으면 파편이 유령 벽에 걸린다.
    // snippet-begin: debris-reset
    void Reset(const DefenseMap& map) {
        world_.Clear();
        live_.clear();
        settleTicks_.clear();
        cellSize_ = static_cast<float>(map.CellSize());
        world_.gravity = {0.0f, kGravity};
        const float half = cellSize_ * 0.5f;
        for (int y = 0; y < map.Rows(); ++y)
            for (int x = 0; x < map.Columns(); ++x) {
                if (!map.Rock(x, y)) continue;
                physics::RigidBody2D rock;
                rock.shape = physics::Shape::Box({half, half});
                rock.SetStatic();
                rock.position = map.CellCenter(x, y);
                rock.friction = 0.7f;
                rock.restitution = 0.0f;
                world_.AddBody(rock);
            }
        staticCount_ = world_.Count();
        if (broadphase_ == BroadphaseKind::Grid) {
            const math::Aabb2 bounds{{0.0f, static_cast<float>(map.OriginY())},
                                     {static_cast<float>(map.PixelWidth()),
                                      static_cast<float>(map.PixelHeight())}};
            world_.SetBroadphase(bounds, cellSize_ * 2.0f);
        }
    }
    // snippet-end: debris-reset

    // 파편 한 뭉치. rng 는 호출자(시뮬)의 결정적 난수라 재생이 재현된다.
    int SpawnBurst(const math::Vec2& at, int count, uint32_t& rng) {
        auto next = [&rng] {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            return (rng >> 8) * (1.0f / 16777216.0f);
        };
        int spawned = 0;
        for (int i = 0; i < count && live_.size() < kMaxLive; ++i) {
            physics::RigidBody2D chunk;
            chunk.shape = physics::Shape::Circle(cellSize_ * 0.22f);
            chunk.SetMass(1.0f);
            chunk.position = {at.x + (next() - 0.5f) * cellSize_,
                              at.y + (next() - 0.5f) * cellSize_};
            chunk.velocity = {(next() - 0.5f) * 120.0f, (next() - 0.5f) * 120.0f};
            chunk.restitution = 0.15f;
            chunk.friction = 0.6f;
            const int index = world_.AddBody(chunk);
            live_.push_back(index);
            settleTicks_.push_back(0);
            ++spawned;
        }
        return spawned;
    }

    // 한 틱. 안착한 파편의 칸을 settledOut 에 담아 돌려준다 — 통행 격자를 실제로
    // 바꾸는 것은 규칙(GameWorld)의 몫이다. 여기서 직접 막으면 "코어를 봉쇄하는
    // 안착"을 되돌릴 주체가 없어진다.
    // snippet-begin: debris-step
    int Step(float dt, const DefenseMap& map, std::vector<DefenseMap::Cell>& settledOut) {
        settledOut.clear();
        if (live_.empty()) return 0;          // 살아 있는 파편이 없으면 물리도 없다
        world_.Step(dt);

        size_t write = 0;
        for (size_t i = 0; i < live_.size(); ++i) {
            const int index = live_[i];
            const physics::RigidBody2D& body = world_.Body(index);
            const float speed = math::Length(body.velocity);
            int& ticks = settleTicks_[i];
            ticks = speed < kSettleSpeed ? ticks + 1 : 0;
            if (ticks < kSettleTicks) {
                live_[write] = index;
                settleTicks_[write] = ticks;
                ++write;
                continue;
            }
            world_.Freeze(index);             // 굳은 파편은 이제 정적 기하다
            const DefenseMap::Cell cell = map.WorldToCell(body.position);
            if (map.InBounds(cell.x, cell.y)) settledOut.push_back(cell);
        }
        live_.resize(write);
        settleTicks_.resize(write);
        return static_cast<int>(settledOut.size());
    }
    // snippet-end: debris-step

    size_t Live() const { return live_.size(); }
    size_t Settled() const { return world_.Count() - staticCount_ - live_.size(); }
    size_t BodyCount() const { return world_.Count(); }
    size_t PairCount() const { return world_.PairCount(); }
    float  MaxPenetration() const { return world_.MaxPenetration(); }
    const physics::PhysicsWorld& World() const { return world_; }
    physics::PhysicsWorld& MutableWorld() { return world_; }

    // 살아 있는 파편의 역학 + 위치 에너지 합. 기준면 아래로 갈수록 위치 에너지가
    // 준다(중력이 +y 이므로). Symplectic 팔에서 이 값은 초기값을 넘지 않는다.
    float TotalEnergy(float referenceY) const {
        float total = 0.0f;
        for (int index : live_) {
            const physics::RigidBody2D& body = world_.Body(index);
            if (body.invMass <= 0.0f) continue;
            const float mass = 1.0f / body.invMass;
            total += 0.5f * mass * math::LengthSq(body.velocity);
            total += mass * kGravity * (referenceY - body.position.y);
        }
        return total;
    }

private:
    static constexpr float kGravity = 340.0f;   // px/s². 칸 10px 기준의 비탈 가속

    physics::PhysicsWorld world_;
    std::vector<int>      live_;
    std::vector<int>      settleTicks_;
    size_t                staticCount_ = 0;
    float                 cellSize_ = 10.0f;
    BroadphaseKind        broadphase_ = BroadphaseKind::Naive;
};

}  // namespace game
}  // namespace mega
