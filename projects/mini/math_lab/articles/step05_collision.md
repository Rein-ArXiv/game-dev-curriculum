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

```cpp
inline SATResult OBBOBBSat2D(const OBB2D& A, const OBB2D& B) {
    SATResult r{};
    auto cornersA = A.Corners();
    auto cornersB = B.Corners();
    auto axesA    = A.Axes();
    auto axesB    = B.Axes();

    Vector2 candidates[4] = {axesA[0], axesA[1], axesB[0], axesB[1]};
    bool  intersect   = true;
    int   bestIdx    = -1;
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
    return Sub2(OBBSupport2D(a, d), OBBSupport2D(b, Neg2(d)));
}
```

OBB 의 지원 함수가 *상수 시간* — 로컬 좌표계에서 각 축 부호로 코너 결정. Minkowski 차의 지원 함수는 *두 지원 함수의 차* (math part13 의 핵심 사실).

GJK 의 모든 알고리즘이 *이 한 함수만* 호출. 도형이 OBB 든 임의 볼록 다각형이든 *지원 함수만 정의* 하면 같은 GJK 코드.

## 5. Simplex 진화의 trace

시각화를 위해 *각 단계의 simplex 상태* 를 vector 에 기록.

```cpp
struct GJKStep {
    std::vector<Vector2> simplex;
    Vector2              direction;
    Vector2              newPoint;
    bool                 done;
    bool                 intersect;
    int                  iter;
};

inline std::vector<GJKStep> GJK2DTraced(const OBB2D& A, const OBB2D& B,
                                          int maxIter = 32) {
    std::vector<GJKStep> trace;
    Vector2 d = {1, 0};
    Vector2 a = MinkowskiSupport2D(A, B, d);

    GJKStep first{};
    first.simplex = {a};
    first.direction = Neg2(a);
    trace.push_back(first);

    d = Neg2(a);
    std::vector<Vector2> S = {a};

    for (int i = 1; i <= maxIter; ++i) {
        Vector2 newP = MinkowskiSupport2D(A, B, d);
        if (Dot2(newP, d) < 0) {
            // 분리 결정
            // ... trace.push_back ... return
        }
        S.push_back(newP);

        if (S.size() == 2) {
            // 선분: ab 에 수직 + ao 쪽으로
            Vector2 ab = Sub2(S[0], S[1]);
            Vector2 perp = {-ab.y, ab.x};
            if (Dot2(perp, Neg2(S[1])) < 0) perp = {ab.y, -ab.x};
            d = perp;
        } else if (S.size() == 3) {
            // 삼각형: 원점이 각 변의 "반대 정점과 다른 쪽" 에 있는지 검사.
            // triple-product (cross2D) 부호 두 개 비교:
            //   sign(cross(edge, AO))  vs  sign(cross(edge, oppositeVertex))
            // 두 부호가 다르면 그 변의 외부 — 반대 정점 버림.
        }

        // 매 단계 trace 기록
    }
    return trace;
}
```

설계: *알고리즘 자체와 trace 가 분리*. 시각화는 trace 의 *step 인덱스* 만 알면 됨. step 별로 *어느 simplex / 어느 방향* 이 었는지를 그려준다.

## 6. GJK 의 stepping UI

```cpp
if (mode_ == Mode::GJK && gjkAuto_) {
    gjkAccum_ += dt;
    if (gjkAccum_ > 0.4f) { gjkAccum_ = 0; ++gjkStep_; }
}
```

자동 모드에서 *0.4 초마다 한 단계*. 수동 모드는 raygui 의 *Step + 버튼*.

```cpp
if (GuiButton(btnRect, "Step +"))  gjkStep_++;
if (GuiButton(resetRect, "Reset")) gjkStep_ = 0;
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

```cpp
DragHandle centerA_;     // OBB 중심
DragHandle rotA_;        // OBB X 축 끝의 회전 핸들

OBB2D BuildOBB(...) const {
    OBB2D r{};
    r.center = centerA_.Position();
    Vector2 d = rotA_.Position() - r.center;
    r.angle  = std::atan2(d.y, d.x);   // 두 핸들 사이 각도
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

이 step 이 `tests/core_tests.cpp` 에 추가한 것은 `TestCollision()` — 겹치는 OBB 쌍과 떨어진 OBB 쌍에서 `OBBOBBSat2D().intersect` 가 각각 참·거짓인지, 같은 두 쌍에 대해 `GJK2DTraced` 의 *마지막 trace 단계* 가 `done` 이고 그 `intersect` 가 SAT 결과와 일치하는지.

```text
$ ctest --test-dir build -C Release --output-on-failure
    Start 1: math_core_tests
1/1 Test #1: math_core_tests ..................   Passed    0.04 sec
100% tests passed out of 1
```

---

## 결론

Step 5 에서 만든 것.

1. **`algo/collision2d.h`** — `OBB2D`, `OBBOBBSat2D`, `OBBSupport2D`, `MinkowskiSupport2D`, `GJK2DTraced` (단계별 trace).
2. **`scenes/collision_scene.{h,cpp}`** — 두 OBB 의 *중심 + 회전 핸들*, SAT 4 축 투영 시각, GJK simplex 단계별 stepping.
3. **재사용 패턴들** — *알고리즘과 trace 분리*, *회전을 핸들로 입력*, *수동/자동 stepping* — 이후 BVH 빌드 단계 시각화 등에 재활용.

math 시리즈 part12 (SAT) + part13 (GJK/EPA) 가 *마우스로 도형을 끌면 4 축 막대 / GJK simplex 가 즉시 갱신되는* 형태로 살아 있다. *분리축이 발견되면 즉시 분리 결정*, *GJK 의 simplex 가 원점 향해 자라는 과정*, *지원 함수의 추상화* 를 사용자가 *직접 본다*.

이 step 의 한계. 자동 검증이 *축 정렬 OBB 한 쌍 + 살짝 회전한 한 쌍* 이라는 두 배치뿐이라, 후보 축에서 B 의 면 법선을 빼거나 GJK 삼각형 케이스의 바깥 판정을 상수로 고정해도 `ctest` 는 통과한다. SAT 와 GJK 가 *같은 답을 내는지* 를 무작위 배치에서 교차 확인하는 코드도 아직 없다. EPA (관통 깊이) 는 다루지 않아 `intersect` 가 참일 때 *얼마나* 겹쳤는지는 SAT 의 MTV 축 한 개로만 근사한다.

## 더 읽을거리

- Gilbert, Johnson, Keerthi, "A Fast Procedure for Computing the Distance Between Complex Objects in Three-Dimensional Space" (1988) — GJK 원전.
- Casey Muratori, ["Implementing GJK"](https://caseymuratori.com/blog_0003) (2006) — 지원 함수·simplex 케이스의 표준 해설 영상.
- Christer Ericson, *Real-Time Collision Detection* (2005) — SAT·GJK·프리미티브 교차의 표준 교과서.
