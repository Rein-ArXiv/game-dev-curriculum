## 들어가며

`math_lab` 의 다섯 번째 단계 — *Collision Scene*. math 시리즈의 *part11 (충돌 프리미티브)* + *part12 (SAT)* + *part13 (GJK/EPA)* 가 *마우스로 두 OBB 의 위치 / 회전 / 크기를 조작하면 SAT 4 분리축의 투영이 즉시 갱신되고 GJK 의 simplex 가 단계별로 자라는* 형태로 살아난다.

이 단계의 결과물.

- **2D OBB-OBB 입력**: 두 OBB 의 *중심 + 회전 핸들*. 회전 핸들을 끌면 OBB 가 *그 방향으로 회전* (중심에서 회전 핸들 방향이 OBB 의 X 축).
- **Mode A — SAT**: 4 분리축 후보 (각 OBB 의 두 면 법선) + 각 축의 *투영 막대* + 각 축의 *overlap 수치 + SEPARATING 표시* + *MTV (최소 overlap 축)*.
- **Mode B — GJK**: simplex 진화의 *단계별 stepping*. 자동 (0.4 초마다 한 단계) 또는 수동 (Step + 버튼). Minkowski 차의 *원점 + simplex 점들 + 다음 search direction* 시각.

핵심 통찰들 — *SAT 가 도형의 면 법선을 후보로 다 평가한 뒤 최소 overlap 을 MTV 로 결정하는 것*, *분리축 한 개라도 발견되면 즉시 분리 결정*, *GJK 가 simplex 를 1점 → 2점 → 3점 으로 진화시키며 원점에 다가가는 것*, *Minkowski 차의 원점 포함 = 두 도형 교차* — 모두 *마우스 조작에 따라 실시간 갱신*.

---

## 목차
1. [2D OBB 자료구조](#1-2d-obb-자료구조)
2. [SAT — 분리축 후보와 투영](#2-sat--분리축-후보와-투영)
3. [SAT 결과의 시각화](#3-sat-결과의-시각화)
4. [GJK — 지원 함수와 Minkowski 차](#4-gjk--지원-함수와-minkowski-차)
5. [Simplex 진화의 trace](#5-simplex-진화의-trace)
6. [GJK 의 stepping UI](#6-gjk-의-stepping-ui)
7. [회전 핸들 입력 패턴](#7-회전-핸들-입력-패턴)
8. [빌드와 실행 결과](#8-빌드와-실행-결과)

---

## 1. 2D OBB 자료구조

<!-- snippet: src/algo/collision2d.h#OBB2D -->
```cpp
struct OBB2D {
    Vector2 center;
    float   halfX;
    float   halfY;
    float   angle;   // radian

    std::array<Vector2, 2> Axes() const {
        float c = std::cos(angle);
        float s = std::sin(angle);
        return {Vector2{c, s}, Vector2{-s, c}};
    }

    std::array<Vector2, 4> Corners() const {
        auto ax = Axes();
        Vector2 ex = {ax[0].x * halfX, ax[0].y * halfX};
        Vector2 ey = {ax[1].x * halfY, ax[1].y * halfY};
        return {
            Vector2{center.x - ex.x - ey.x, center.y - ex.y - ey.y},
            Vector2{center.x + ex.x - ey.x, center.y + ex.y - ey.y},
            Vector2{center.x + ex.x + ey.x, center.y + ex.y + ey.y},
            Vector2{center.x - ex.x + ey.x, center.y - ex.y + ey.y},
        };
    }
};
```

`Axes()` 가 *두 직교 축 (X, Y)*. SAT 에서 이 두 축이 *그 OBB 의 분리축 후보 두 개*. `Corners()` 가 *4 정점*.

회전 표현은 *radian 한 개*. 2D 라 단순 — 3D 의 quaternion 무관.

## 2. SAT — 분리축 후보와 투영

2D 의 SAT 는 *4 후보 축* — 각 OBB 의 두 면 법선.

<!-- snippet: src/algo/collision2d.h#OBBOBBSat2D -->
```cpp
inline SATResult OBBOBBSat2D(const OBB2D& A, const OBB2D& B) {
    SATResult r{};
    auto cornersA = A.Corners();
    auto cornersB = B.Corners();
    auto axesA    = A.Axes();
    auto axesB    = B.Axes();

    Vector2 candidates[4] = {axesA[0], axesA[1], axesB[0], axesB[1]};
    r.axes.reserve(4);

    bool intersect = true;
    int  bestIdx   = -1;
    float bestOverlap = 1e30f;
    for (int i = 0; i < 4; ++i) {
        SATAxisInfo info = SATEvaluateAxis(cornersA, cornersB, candidates[i]);
        r.axes.push_back(info);
        if (info.separating) intersect = false;
        else if (info.overlap < bestOverlap) {
            bestOverlap = info.overlap;
            bestIdx     = i;
        }
    }
    r.intersect = intersect;
    r.mtvIdx    = intersect ? bestIdx : -1;
    return r;
}
```

`SATEvaluateAxis` 가 *한 축에 두 도형 투영* 후 overlap 계산.

<!-- snippet: src/algo/collision2d.h#SATEvaluateAxis -->
```cpp
inline SATAxisInfo SATEvaluateAxis(const std::array<Vector2, 4>& a,
                                     const std::array<Vector2, 4>& b,
                                     Vector2 axis) {
    SATAxisInfo r{};
    r.axis = axis;
    r.minA = r.maxA = Dot2(a[0], axis);
    r.minB = r.maxB = Dot2(b[0], axis);
    for (int i = 1; i < 4; ++i) {
        float pa = Dot2(a[i], axis);
        float pb = Dot2(b[i], axis);
        if (pa < r.minA) r.minA = pa; if (pa > r.maxA) r.maxA = pa;
        if (pb < r.minB) r.minB = pb; if (pb > r.maxB) r.maxB = pb;
    }
    float overlap = std::min(r.maxA, r.maxB) - std::max(r.minA, r.minB);
    r.overlap    = overlap;
    r.separating = (overlap < 0);
    return r;
}
```

핵심: *두 도형의 [min, max] 구간이 분리되어 있으면 (overlap < 0) 그 축이 분리축*. *어느 축에서 분리되면* 즉시 결정 — 시각화에서는 *모든 축* 을 보여주려고 early-out 안 함.

## 3. SAT 결과의 시각화

각 축에 *작은 막대 그래프* — 두 도형의 투영 구간이 그 축 위 어디에 있는지.

```cpp
for (int i = 0; i < (int)sat.axes.size(); ++i) {
    const auto& ax = sat.axes[i];
    float L = std::sqrt(ax.axis.x * ax.axis.x + ax.axis.y * ax.axis.y);
    Vector2 dir = {ax.axis.x / L, ax.axis.y / L};
    Vector2 base = {origin.x, origin.y + (float)i * 30.0f};

    // 축 라인
    DrawLineEx(base, base + dir * axisLen, 1, gray);

    // 두 도형의 투영 구간을 base 위에 매핑
    float lo = std::min(ax.minA, ax.minB);
    float hi = std::max(ax.maxA, ax.maxB);
    float span = hi - lo;
    auto plot = [&](float v0, float v1, Color c, float yOff) {
        float t0 = (v0 - lo) / span, t1 = (v1 - lo) / span;
        Vector2 p0 = {base.x + dir.x * t0 * axisLen,
                        base.y + dir.y * t0 * axisLen + yOff};
        // ... DrawLineEx p0 → p1 ...
    };
    plot(ax.minA, ax.maxA, yellow, -3.0f);   // A
    plot(ax.minB, ax.maxB, blue,    3.0f);   // B
}
```

직관: *두 색 막대가 겹치면 그 축은 분리 못 함*, *겹치지 않으면 분리축*. 4 축 모두 겹치면 두 OBB 가 교차.

상단에 *INTERSECTING* (초록) 또는 *SEPARATED* (빨강) — 사용자가 즉시 결과 확인.

## 4. GJK — 지원 함수와 Minkowski 차

GJK 의 핵심: *지원 함수 + Minkowski 차*.

<!-- snippet: src/algo/collision2d.h@gjk-support -->
```cpp
inline Vector2 OBBSupport2D(const OBB2D& obb, Vector2 d) {
    auto axes = obb.Axes();
    float dx = Dot2(d, axes[0]);
    float dy = Dot2(d, axes[1]);
    float sx = (dx >= 0) ? obb.halfX : -obb.halfX;
    float sy = (dy >= 0) ? obb.halfY : -obb.halfY;
    return {
        obb.center.x + axes[0].x * sx + axes[1].x * sy,
        obb.center.y + axes[0].y * sx + axes[1].y * sy,
    };
}

inline Vector2 MinkowskiSupport2D(const OBB2D& a, const OBB2D& b, Vector2 d) {
    Vector2 p1 = OBBSupport2D(a, d);
    Vector2 p2 = OBBSupport2D(b, Neg2(d));
    return Sub2(p1, p2);
}
```

OBB 의 지원 함수가 *상수 시간* — 로컬 좌표계에서 각 축 부호로 코너 결정. Minkowski 차의 지원 함수는 *두 지원 함수의 차* (math part13 의 핵심 사실).

GJK 의 모든 알고리즘이 *이 한 함수만* 호출. 도형이 OBB 든 임의 볼록 다각형이든 *지원 함수만 정의* 하면 같은 GJK 코드.

## 5. Simplex 진화의 trace

시각화를 위해 *각 단계의 simplex 상태* 를 vector 에 기록.

<!-- snippet: src/algo/collision2d.h#GJKStep -->
```cpp
struct GJKStep {
    std::vector<Vector2> simplex;   // 이 단계의 simplex
    Vector2              direction; // 다음 search direction
    Vector2              newPoint;  // 이 단계에서 추가된 지원점
    bool                 done;
    bool                 intersect;
    int                  iter;
};
```

알고리즘은 그 구조체를 매 반복 하나씩 쌓는다.

<!-- snippet: src/algo/collision2d.h#GJK2DTraced -->
```cpp
inline std::vector<GJKStep> GJK2DTraced(const OBB2D& A, const OBB2D& B,
                                          int maxIter = 32) {
    std::vector<GJKStep> trace;
    Vector2 d = {1, 0};   // 초기 임의 방향
    Vector2 a = MinkowskiSupport2D(A, B, d);

    GJKStep first{};
    first.simplex   = {a};
    first.newPoint  = a;
    first.direction = Neg2(a);
    // ... first 의 나머지 필드 + trace.push_back(first) ...
    d = Neg2(a);
    if (d.x * d.x + d.y * d.y < 1e-12f) d = {1, 0};
    std::vector<Vector2> S = {a};

    for (int i = 1; i <= maxIter; ++i) {
        Vector2 newP = MinkowskiSupport2D(A, B, d);
        if (Dot2(newP, d) < 0) {
            // ... 분리됨 — done/intersect=false 를 trace 에 남기고 return ...
        }
        S.push_back(newP);

        if (S.size() == 2) {
            // ... 선분: ab 에 수직인 방향 중 ao 쪽 ...
        } else if (S.size() == 3) {
            // ... 삼각형: 변마다 sign(cross(e, AO)) 와 sign(cross(e, e_opp))
            //     를 비교해 원점이 그 변의 외부면 반대 정점을 버린다 ...
        }
        // ... 매 단계 trace 기록 ...
    }
    // ... 최대 반복 도달 — 안전 종료 ...
    return trace;
}
```

설계: *알고리즘 자체와 trace 가 분리*. 시각화는 trace 의 *step 인덱스* 만 알면 됨. step 별로 *어느 simplex / 어느 방향* 이 었는지를 그려준다.

## 6. GJK 의 stepping UI

<!-- snippet: src/scenes/collision_scene.cpp@gjk-auto-step -->
```cpp
if (mode_ == Mode::GJK && gjkAuto_) {
    gjkAccum_ += dt;
    if (gjkAccum_ > 0.4f) {
        gjkAccum_ = 0;
        ++gjkStep_;
    }
}
```

자동 모드에서 *0.4 초마다 한 단계*. 수동 모드는 raygui 의 *Step + 버튼*.

<!-- snippet: src/scenes/collision_scene.cpp@gjk-step-buttons -->
```cpp
if (GuiButton(Rectangle{(float)(px + 8), (float)y, 90, 22}, "Step +")) {
    gjkStep_++;
}
if (GuiButton(Rectangle{(float)(px + 100), (float)y, 90, 22}, "Reset")) {
    gjkStep_ = 0;
}
```

매 프레임 전체 trace 를 *재계산* — OBB 가 바뀌면 trace 도 바뀜. 단계 인덱스만 *유지*.

trace 의 *각 단계* 가 그림으로.

```cpp
const GJKStep& step = trace[gjkStep_];
Vector2 origin = canvas 가운데;
DrawCircleV(origin, 4, white);   // Minkowski 차의 원점

// simplex 점들
for (auto& p : step.simplex) {
    DrawCircleV(origin + p, 5, yellow);
}
// 선분 / 삼각형 잇기
// search direction 화살표
```

원점이 *항상 캔버스 중앙* — Minkowski 차 자체가 기하학적으로 원점 주변에 분포. simplex 점들이 *캔버스 가운데 + offset* 으로 그려진다.

## 7. 회전 핸들 입력 패턴

OBB 의 *회전을 어떻게 입력* 받을까. 슬라이더 라디언은 추상적 — *회전 핸들* 패턴을 도입.

Scene 의 멤버는 OBB 하나당 *중심 핸들 + 회전 핸들* 두 개다 (`centerA_` / `rotA_`,
`centerB_` / `rotB_`). 두 핸들 사이의 각도가 곧 OBB 의 회전.

<!-- snippet: src/scenes/collision_scene.cpp#CollisionScene::BuildOBB -->
```cpp
OBB2D CollisionScene::BuildOBB(DragHandle const& center, DragHandle const& rot,
                                  float halfX, float halfY) const {
    OBB2D r{};
    r.center = center.Position();
    Vector2 d = {rot.Position().x - r.center.x,
                  rot.Position().y - r.center.y};
    r.angle  = std::atan2(d.y, d.x);   // 두 핸들 사이 각도
    r.halfX  = halfX;
    r.halfY  = halfY;
    return r;
}
```

사용자가 *회전 핸들을 끌어 OBB 를 회전*. 직관적: *X 축이 어디를 가리키는지* 가 회전. 라인으로 *중심 → 회전 핸들* 를 연결해 시각.

이 패턴이 *2D 게임 에디터의 표준* — Unity 2D, Godot 가 모두 이런 회전 핸들.

## 8. 빌드와 실행 결과

```bash
cmake --build build --config Release
./build/Release/math_lab.exe
```

탭이 *Welcome / Rotation / Interpolation / Noise / Collision* 다섯. Collision 탭에서:

- *SAT 토글*: 두 OBB 가 분리되어 있으면 *SEPARATED* (빨강) + 한 축 이상의 막대가 분리. 겹치게 끌면 *INTERSECTING* (초록) + 모든 축에서 막대 겹침. MTV 축은 *최소 overlap*.
- *GJK 토글 + auto step*: simplex 가 *1점 → 2점 (선분) → 3점 (삼각형)* 으로 자라는 모습. 분리되어 있으면 *SEPARATED* 로 종료, 겹치면 *INTERSECT* 로 종료.
- *회전 핸들*: rotA / rotB 핸들을 끌어 OBB 회전. 회전이 변할 때마다 SAT / GJK 즉시 재계산.

빌드: MSVC `/W4`, 새 Scene 추가 후도 *경고 0 (third-party 외)* + `math_lab.exe` 정상.

이 step 이 `tests/core_tests.cpp` 에 추가한 것은 `collision/sat_vs_gjk` 다. 이 Scene 은 *같은 질문에 답하는 두 알고리즘* 을 나란히 두고 있으므로, 검증도 그 구조를 그대로 쓴다 — 서로가 서로의 오라클이다. 고정 seed LCG 로 만든 OBB 20,000 쌍에서 두 판정이 한 번도 갈리지 않아야 한다.

<!-- snippet: tests/core_tests.cpp@step05-sat-gjk-agree -->
```cpp
const bool sat = mathlab::OBBOBBSat2D(A, B).intersect;
const auto trace = mathlab::GJK2DTraced(A, B);
const bool gjk =
    !trace.empty() && trace.back().done && trace.back().intersect;
// ...
CHECK(disagree == 0);
// 표본이 한쪽으로 쏠려 위 CHECK 가 자명해지지 않았는지 확인.
CHECK(hits > kPairs / 10);
CHECK(hits < kPairs - kPairs / 10);
```

뒤의 두 CHECK 가 없으면 이 대조는 자명해질 수 있다. 표본이 전부 "떨어짐" 으로 쏠려 있으면 *두 함수가 모두 `false` 를 상수로 돌려줘도* 일치하기 때문이다. 표본의 10~90% 가 충돌이라는 하한·상한이 그 구멍을 막는다.

무작위 대조만으로는 놓치는 경우가 하나 있어 배치를 손으로 하나 더 만들었다 — *오직 B 의 면 법선에서만 분리되는* 배치다. 45° 로 기운 얇고 긴 막대를 정사각형의 대각선 투영 반경 바깥에 법선 방향으로만 밀어 두면, A 의 두 축과 B 의 긴 축에서는 투영이 겹친다. 후보 축에서 B 의 법선을 빼면 "충돌" 로 오판하는 배치이고, 실제로 `axes[0..2]` 는 분리되지 않고 `axes[3]` 만 분리된다는 것까지 못 박아 뒀다.

```text
$ ctest --test-dir build -C Release -R math_step05_collision --output-on-failure
    Start 5: math_step05_collision
1/1 Test #5: math_step05_collision ............   Passed    0.05 sec

100% tests passed out of 1
```

```text
$ ./build/Release/math_core_tests.exe step05
[step05] collision/sat_vs_gjk ............ 14 checks, 0 failed
step05: 1 case(s), 14 check(s), 0 failure(s)
```

CHECK 는 14 개지만 그 뒤에서 도형 쌍 20,000 개가 돌아간다 — 루프 안에서 세고 밖에서 한 번 단언하는 형태라 실패해도 출력이 터지지 않는다. 갈리는 쌍이 나오면 처음 3 개의 인덱스와 두 판정값이 `stderr` 로 찍힌다.

---

## 결론

Step 5 에서 만든 것.

1. **`algo/collision2d.h`** — `OBB2D`, `OBBOBBSat2D`, `OBBSupport2D`, `MinkowskiSupport2D`, `GJK2DTraced` (단계별 trace).
2. **`scenes/collision_scene.{h,cpp}`** — 두 OBB 의 *중심 + 회전 핸들*, SAT 4 축 투영 시각, GJK simplex 단계별 stepping.
3. **재사용 패턴들** — *알고리즘과 trace 분리*, *회전을 핸들로 입력*, *수동/자동 stepping* — 이후 BVH 빌드 단계 시각화 등에 재활용.

math 시리즈 part12 (SAT) + part13 (GJK/EPA) 가 *마우스로 도형을 끌면 4 축 막대 / GJK simplex 가 즉시 갱신되는* 형태로 살아 있다. *분리축이 발견되면 즉시 분리 결정*, *GJK 의 simplex 가 원점 향해 자라는 과정*, *지원 함수의 추상화* 를 사용자가 *직접 본다*.

이 step 의 한계. 교차 검증이 고정하는 것은 *충돌하는가* 라는 불리언 하나다. SAT 의 MTV 축과 overlap 값, GJK 가 몇 번 만에 끝나는지, simplex 정점이 어떤 순서로 자라는지는 화면에만 있다 — 두 알고리즘이 같은 답을 내되 *같은 이유로* 내는지는 검사하지 않는다. 접촉면이 정확히 맞닿는 퇴화 배치도 무작위 표본에서는 사실상 뽑히지 않아 부동소수 경계 동작은 미검증이고, EPA (관통 깊이) 는 다루지 않아 `intersect` 가 참일 때 *얼마나* 겹쳤는지는 SAT 의 MTV 축 한 개로만 근사한다.

## 더 읽을거리

- Gilbert, Johnson, Keerthi, "A Fast Procedure for Computing the Distance Between Complex Objects in Three-Dimensional Space" (1988) — GJK 원전.
- Casey Muratori, ["Implementing GJK"](https://caseymuratori.com/blog_0003) (2006) — 지원 함수·simplex 케이스의 표준 해설 영상.
- Christer Ericson, *Real-Time Collision Detection* (2005) — SAT·GJK·프리미티브 교차의 표준 교과서.
