// Step 4 (BroadPhase) 헤드리스 검증. SAP / Spatial Hash 가 낸 overlap *쌍
// 집합* 이 brute 의 집합과 완전히 같은지 본다 — 개수만 비교하면 false positive
// 와 false negative 가 서로를 상쇄해 둘 다 가린다.

#include "scenes/broad_phase_scene.h"

#include "step_cases.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(expr)                                                          \
    do {                                                                     \
        if (!(expr)) {                                                       \
            std::cerr << "CHECK failed: " #expr << " (" << __FILE__ << ':' \
                      << __LINE__ << ")\n";                                  \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

using Pairs = std::vector<physlab::BroadPhaseScene::OverlapPair>;

uint32_t Lcg(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return s;
}
float Rand01(uint32_t& s) {
    return (float)(Lcg(s) >> 8) / (float)(1u << 24);
}

Pairs Sorted(Pairs p) {
    std::sort(p.begin(), p.end());
    return p;
}

void ReportDiff(const std::string& label, const Pairs& truth,
                const Pairs& got) {
    Pairs missing, extra;
    std::set_difference(truth.begin(), truth.end(), got.begin(), got.end(),
                        std::back_inserter(missing));
    std::set_difference(got.begin(), got.end(), truth.begin(), truth.end(),
                        std::back_inserter(extra));
    std::cerr << "[" << label << "] missing=" << missing.size()
              << " extra=" << extra.size();
    if (!missing.empty()) {
        std::cerr << " firstMissing=(" << missing.front().first << ','
                  << missing.front().second << ')';
    }
    if (!extra.empty()) {
        std::cerr << " firstExtra=(" << extra.front().first << ','
                  << extra.front().second << ')';
    }
    std::cerr << '\n';
}

// 세 알고리즘이 같은 배치에서 같은 쌍 집합을 내는지. 반환값은 brute 가 센
// overlap 쌍 수 — 각 케이스가 자기 배치의 관측값을 한 줄로 찍는 데 쓴다.
int CheckAllAgree(const std::string& label,
                  const std::vector<physlab::AabbBody>& bodies,
                  float cellSize,
                  int expectedOverlap = -1) {
    physlab::BroadPhaseScene scene;
    scene.SetBodies(bodies);
    scene.SetCellSize(cellSize);

    int bruteCand = 0, bruteOv = 0;
    int sapCand = 0, sapOv = 0;
    int hashCand = 0, hashOv = 0;
    Pairs brutePairs, sapPairs, hashPairs;
    scene.RunBrute(bruteCand, bruteOv, &brutePairs);
    scene.RunSAP(sapCand, sapOv, &sapPairs);
    scene.RunSpatialHash(hashCand, hashOv, &hashPairs);

    const Pairs truth = Sorted(brutePairs);
    const Pairs sap   = Sorted(sapPairs);
    const Pairs hash  = Sorted(hashPairs);

    CHECK((int)truth.size() == bruteOv);
    CHECK((int)sap.size() == sapOv);
    CHECK((int)hash.size() == hashOv);

    // snippet-begin: pair-set-equality
    // 개수가 아니라 *쌍 집합* 을 통째로 대조한다. 개수 비교는 false positive 와
    // false negative 가 같은 수로 생기면 둘 다 가린다.
    if (sap != truth) ReportDiff(label + "/SAP", truth, sap);
    if (hash != truth) ReportDiff(label + "/Hash", truth, hash);
    CHECK(sap == truth);
    CHECK(hash == truth);
    // snippet-end: pair-set-equality

    // 후보 수 계약: brute 는 모든 쌍, 다른 둘은 그보다 많을 수 없고
    // 실제 overlap 보다 적을 수 없다.
    const int n = (int)bodies.size();
    CHECK(bruteCand == n * (n - 1) / 2);
    CHECK(sapCand <= bruteCand);
    CHECK(sapCand >= sapOv);
    CHECK(hashCand >= hashOv);

    if (expectedOverlap >= 0) {
        if (bruteOv != expectedOverlap) {
            std::cerr << "[" << label << "] brute overlap=" << bruteOv
                      << " expected=" << expectedOverlap << '\n';
        }
        CHECK(bruteOv == expectedOverlap);
    }
    return bruteOv;
}

physlab::AabbBody Body(float x, float y, float half) {
    physlab::AabbBody b;
    b.pos  = {x, y};
    b.vel  = {0, 0};
    b.half = {half, half};
    return b;
}

// (1) 경계 접촉 — 한 박스의 x-max 와 다른 박스의 x-min 이 정확히 같다.
// AabbOverlap 이 <= 규약이라 이 쌍도 overlap 이며, SAP 의 tie-break
// (같은 좌표면 min 먼저) 가 없으면 놓친다.
void TestExactTouchRow() {
    std::vector<physlab::AabbBody> bodies;
    const float half = 8.0f;
    // 배열 순서를 정렬 순서와 어긋나게 넣어 sort 의 동률 처리를 자극한다.
    const int n = 24;
    for (int k = 0; k < n; ++k) {
        const int idx = (k * 7) % n;   // 7 과 24 는 서로소 → 순열
        bodies.push_back(Body(idx * 2.0f * half, 100.0f, half));
    }
    // 인접한 두 박스는 정확히 맞닿는다 → n-1 쌍.
    const int row  = CheckAllAgree("touch-row", bodies, 16.0f, n - 1);
    const int row2 = CheckAllAgree("touch-row/cell37", bodies, 37.0f, n - 1);

    // 세로 방향 경계 접촉도 같이 본다.
    std::vector<physlab::AabbBody> column;
    for (int k = 0; k < 12; ++k) {
        const int idx = (k * 5) % 12;
        column.push_back(Body(-50.0f, idx * 2.0f * half, half));
    }
    const int col = CheckAllAgree("touch-column", column, 16.0f, 11);
    std::cout << "  touching pairs: row=" << row << " row(cell37)=" << row2
              << " column=" << col << '\n';
}

// (2) 음수 셀 좌표 — 캔버스 원점 왼쪽 / 위쪽. cellKey 의 unsigned 캐스팅과
// floor 처리가 음수에서도 맞아야 한다.
void TestNegativeCells() {
    uint32_t rng = 7u;
    std::vector<physlab::AabbBody> bodies;
    for (int k = 0; k < 120; ++k) {
        bodies.push_back(Body(-400.0f + Rand01(rng) * 380.0f,
                              -300.0f + Rand01(rng) * 280.0f,
                              10.0f));
    }
    const int negative = CheckAllAgree("negative-cells", bodies, 60.0f);

    // 원점을 가로지르는 배치 — 셀 좌표 부호가 섞인다.
    std::vector<physlab::AabbBody> straddle;
    for (int k = 0; k < 80; ++k) {
        straddle.push_back(Body(-60.0f + Rand01(rng) * 120.0f,
                                -60.0f + Rand01(rng) * 120.0f,
                                12.0f));
    }
    const int straddling = CheckAllAgree("straddle-origin", straddle, 25.0f);
    std::cout << "  negative-cell pairs=" << negative
              << " straddle-origin pairs=" << straddling << '\n';
}

// (3) cellSize < bodyHalf — 한 박스가 여러 셀에 등록되어 dedupe 경로를 밟는다.
void TestSmallCells() {
    uint32_t rng = 11u;
    std::vector<physlab::AabbBody> bodies;
    for (int k = 0; k < 60; ++k) {
        bodies.push_back(Body(Rand01(rng) * 300.0f,
                              Rand01(rng) * 200.0f,
                              20.0f));
    }
    const int c4 = CheckAllAgree("small-cells/4", bodies, 4.0f);
    const int c1 = CheckAllAgree("small-cells/1", bodies, 1.0f);
    // cellSize 하한(1.0) 아래로 내려도 결과는 같아야 한다.
    const int c025 = CheckAllAgree("small-cells/0.25", bodies, 0.25f);
    std::cout << "  same pairs at every cell size: " << c4 << ' ' << c1 << ' '
              << c025 << '\n';
}

// (4) 무작위 배치 50 개 × N=200.
void TestRandomBatches() {
    int totalPairs = 0;
    for (int batch = 0; batch < 50; ++batch) {
        uint32_t rng = 1000u + (uint32_t)batch;
        std::vector<physlab::AabbBody> bodies;
        const float half = 6.0f + Rand01(rng) * 10.0f;
        for (int k = 0; k < 200; ++k) {
            bodies.push_back(Body(Rand01(rng) * 640.0f,
                                  Rand01(rng) * 480.0f,
                                  half));
        }
        const float cell = 8.0f + Rand01(rng) * 80.0f;
        totalPairs += CheckAllAgree("random#" + std::to_string(batch), bodies,
                                    cell);
    }
    std::cout << "  50 batches x 200 bodies: " << totalPairs
              << " overlap pairs, all three algorithms agree\n";
}

// (5) 퇴화 입력 — 완전히 겹친 박스, 반크기 0.
void TestDegenerate() {
    std::vector<physlab::AabbBody> bodies;
    for (int k = 0; k < 6; ++k) bodies.push_back(Body(50.0f, 50.0f, 5.0f));
    bodies.push_back(Body(50.0f, 50.0f, 0.0f));   // 점
    bodies.push_back(Body(500.0f, 500.0f, 5.0f)); // 외톨이
    // 7 개가 한 점에 겹침 → 7*6/2 = 21 쌍.
    const int pairs = CheckAllAgree("degenerate", bodies, 16.0f, 21);
    std::cout << "  7 coincident bodies + 1 loner -> " << pairs << " pairs\n";
}

const physlab::testkit::StepCase kCases[] = {
    {"step04", "exact_touch_row", TestExactTouchRow},
    {"step04", "negative_cells",  TestNegativeCells},
    {"step04", "small_cells",     TestSmallCells},
    {"step04", "random_batches",  TestRandomBatches},
    {"step04", "degenerate",      TestDegenerate},
};

}  // namespace

int main(int argc, char** argv) {
    return physlab::testkit::RunStepCases(
        argc, argv, kCases, (int)(sizeof(kCases) / sizeof(kCases[0])),
        "physics broad phase", &failures);
}
