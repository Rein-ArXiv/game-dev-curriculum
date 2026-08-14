// Step 8 — 종합 측정 + *baseline determinism sanity check*. 작은 N-body
// 시뮬 (원형 강체 + Sequential Impulse + Semi-implicit Euler) 을 *살아있는
// 시뮬* 로 돌리며 에너지 / 평균 침투 그래프 + 결정성 sanity check.
//
// "Sanity check" 의 한계.
//   - 같은 프로세스, 같은 빌드, 같은 컴파일러, 같은 CPU 에서 *동일 코드 경로* 를
//     두 번 돌렸을 때 결과가 같은지만 확인. *진짜 lockstep 결정성* (cross-
//     compiler / cross-CPU) 은 별도 검증 필요.
//   - "Shuffle contacts" 토글로 *순서 변경이 결정성을 어떻게 깨는지* 직접 관찰
//     가능 — SI 의 Gauss-Seidel 풀이가 *순서 의존* 임을 실증.
//
// 세 분류는 이 실행 환경 안의 repeatability만 나타낸다:
//   bit-exact repeatable : max delta == 0
//   near-repeatable      : max delta < epsilon
//   diverged             : max delta 가 매크로 단위

#pragma once

#include "scenes/iscene.h"
#include "ui/line_graph.h"

#include "raylib.h"

#include <vector>

namespace physlab {

struct MeasureBody {
    Vector2 pos;
    Vector2 vel;
    float   radius;
};

class MeasureScene : public IScene {
public:
    MeasureScene();

    void OnEnter() override;
    void HandleInput() override;
    void Step(float dt) override;
    void DrawWorld(int canvasX, int canvasY, int canvasW, int canvasH) override;
    void DrawPanel(int panelX, int panelY, int panelW, int panelH) override;
    std::string_view Name() const override { return "Measure"; }

    // --- 헤드리스 검증 진입점 (계약 확장 — 시뮬 동작은 그대로) ---
    // InitBodies / StepSim 은 캔버스 좌표(lastCanvas*)에 의존하므로 SetCanvas 로
    // 먼저 주입한다. GUI 의 KEY_T 경로와 같은 RunDeterminismTest 를 그대로 쓴다.
    void InitBodies(std::vector<MeasureBody>& out, int n, unsigned seed) const;
    void StepSim(std::vector<MeasureBody>& sim, float dt) const;
    void RunDeterminismTest();
    void ComputeMetrics(const std::vector<MeasureBody>& sim,
                          float& outAvgPen, float& outKE) const;

    void SetCanvas(int x, int y, int w, int h) {
        lastCanvasX_ = x; lastCanvasY_ = y;
        lastCanvasW_ = w; lastCanvasH_ = h;
    }
    void SetBodyCount(int n)            { N_ = n; }
    void SetSeed(unsigned seed)         { seed_ = seed; }
    void SetIterations(int iterations)  { iterations_ = iterations; }
    void SetTestFrames(int frames)      { testFrames_ = frames; }
    void SetShuffleContacts(bool on)    { shuffleContacts_ = on; }
    bool  TestRan() const               { return testRan_; }
    float LastTestMaxDelta() const      { return lastTestMaxDelta_; }
    float LastTestAvgDelta() const      { return lastTestAvgDelta_; }
    bool  LastTestDeterministic() const { return lastTestDeterministic_; }
    bool  LastTestShuffle() const       { return lastTestShuffle_; }

private:

    // live 시뮬.
    std::vector<MeasureBody> live_;
    int                      N_           = 24;
    unsigned                 seed_        = 1u;
    int                      iterations_  = 8;
    // 중력 / 반발계수는 결정성 비교를 위해 UI 없이 고정한다 —
    // measure_scene.cpp 의 kSimGravity / kSimRestitution 참조.

    // 결정성 테스트.
    int   testFrames_       = 600;
    bool  testRan_          = false;
    bool  shuffleContacts_  = false;   // true 면 매 step 마다 다른 순서 — 결정성 깨짐
    float lastTestMaxDelta_ = 0;
    float lastTestAvgDelta_ = 0;
    bool  lastTestDeterministic_ = false;
    bool  lastTestShuffle_  = false;   // 마지막 테스트 시점의 shuffle 상태

    // live 측정 그래프.
    GraphSeries energyHist_;
    GraphSeries penHist_;

    bool initialized_ = false;
    int  lastCanvasX_ = 0, lastCanvasY_ = 0;
    int  lastCanvasW_ = 0, lastCanvasH_ = 0;

    float lastAvgPen_ = 0;
    float lastKE_     = 0;
};

}  // namespace physlab
