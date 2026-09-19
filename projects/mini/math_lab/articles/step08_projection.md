## 들어가며

`math_lab` 의 마지막 단계 — *Camera Projection + Numerical Stability Scene*. math 시리즈의 *part18 (카메라 투영)* + *part20 (수치 안정성)* 의 핵심 함정 두 가지가 *마우스로 매개변수를 끌면 즉시 갱신되는 그래프* 로 살아난다.

이 단계의 결과물.

- **Mode A — Depth Precision**: View Z 가 *near 부터 far* 까지 sweep 되면서 *Standard Z [0, 1]* 와 *Reverse Z [1, 0]* 의 NDC z 분포를 한 그래프에 겹쳐 그린다. near / far 슬라이더 + log scale 토글.
- **Mode B — Kahan Summation**: N 개의 작은 수를 *naive float 합 vs Kahan 보상 합* — 시간에 따른 오차 누적을 그래프로. 또 *큰 수에 작은 수가 묻히는 (loss of significance / absorption)* 케이스 결과 표시.

핵심 통찰들 — *Standard Z 가 near 근처에 매핑이 가팔라 (= NDC 변화율 큼) 정밀도가 몰리고 far 근처는 평평*, *Reverse-Z 가 그 매핑 분포를 뒤집어 부동소수점 depth 의 ULP 분포와 결합 시 정밀도 이득*, *naive 합산의 오차가 N 에 비례해 자라는 것*, *Kahan 이 누적 오차를 거의 일정하게 묶어 두는 것* — 모두 *마우스 슬라이더에 따라 즉시 갱신*.

*주의*: Scene 에 그린 것은 *NDC z 곡선* 만 이라 *그 자체로 정밀도 전체를 보여주지는 않는다*. 실제 정밀도 이득은 *NDC 곡선의 기울기 × float depth 의 ULP 분포 (값이 0 근처에 밀집)* 의 결합으로 결정. math part18 의 *NVIDIA Depth Precision Visualized* 가 그 결합 효과를 측정한 표준 참조.

이 Scene 으로 *math_lab 8 Scene 모두 갖춰진다*. Welcome → Rotation → Interpolation → Noise → Collision → Spatial / BVH → SDF / RayMarch → Projection / Stability.

---

## 목차
1. [Depth Precision 의 두 공식](#1-depth-precision-의-두-공식)
2. [그래프 영역과 축](#2-그래프-영역과-축)
3. [Standard vs Reverse 곡선 그리기](#3-standard-vs-reverse-곡선-그리기)
4. [Log scale 옵션](#4-log-scale-옵션)
5. [Kahan vs Naive 합산](#5-kahan-vs-naive-합산)
6. [Absorption 케이스 표시](#6-absorption-케이스-표시)
7. [패널 매개변수](#7-패널-매개변수)
8. [빌드와 실행 결과 + math_lab 의 마무리](#8-빌드와-실행-결과--math_lab-의-마무리)

---

## 1. Depth Precision 의 두 공식

math part18 의 *DX/Vulkan 표준* 깊이 매핑.

두 공식은 Scene 이 아니라 `src/algo/depth.h` 에 둔다. 그래야 GUI 없이 값을 직접 호출해 검사할 수 있다 (§8 참고).

<!-- snippet: src/algo/depth.h@ndc-z -->
```cpp
inline float StdNdcZ(float viewZ, float n, float f) {
    float A = -f / (f - n);
    float B = -f * n / (f - n);
    float w = -viewZ;
    if (std::fabs(w) < 1e-12f) return 0.0f;
    return (A * viewZ + B) / w;
}

inline float ReverseNdcZ(float viewZ, float n, float f) {
    float A = n / (f - n);
    float B = f * n / (f - n);
    float w = -viewZ;
    if (std::fabs(w) < 1e-12f) return 1.0f;
    return (A * viewZ + B) / w;
}
```

`viewZ` 는 *음수* (View 공간 카메라가 -Z 향함). z = -n → 표준은 0, reverse 는 1. z = -f → 표준은 1, reverse 는 0.

두 식은 대수적으로 `ReverseNdcZ = 1 - StdNdcZ` 다: `-1 - A_std = n/(f-n) = A_rev`, `-B_std = B_rev`. 부호를 하나만 잘못 써도 이 항등식이 깨진다.

`w = -viewZ` 의 perspective divide. *행렬 곱 → w 나눗셈* 의 결과를 직접 한 식에 묶음.

## 2. 그래프 영역과 축

X 축 = *View Z (-near 부터 -far)*, Y 축 = *NDC z (0 ~ 1)*. 캔버스 안에 *margin 60 px* 떼고 그래프.

<!-- snippet: src/scenes/projection_scene.cpp@depth-graph-axes -->
```cpp
int   margin = 60;
int   gx     = cx + margin;
int   gy     = cy + 60;
int   gw     = cw - margin * 2;
int   gh     = ch - 100;
if (gw < 100 || gh < 80) return;

// 축
DrawLine(gx, gy + gh, gx + gw, gy + gh, Color{180, 180, 180, 255});
DrawLine(gx, gy,      gx,      gy + gh, Color{180, 180, 180, 255});
```

캔버스가 그래프를 담기에 너무 작으면 (`gw < 100 || gh < 80`) 아예 그리지 않고 돌아간다 — 음수 폭으로 좌표가 뒤집히는 것을 막는다.

라벨 + 0/1 표시.

<!-- snippet: src/scenes/projection_scene.cpp@depth-graph-labels -->
```cpp
DrawText("View Z (near -> far)", gx + gw / 2 - 60,
          gy + gh + 6, 12, Color{200, 200, 200, 255});
DrawText("NDC z", gx - 50, gy + gh / 2, 12, Color{200, 200, 200, 255});
DrawText("0", gx - 16, gy + gh - 6, 12, Color{200, 200, 200, 255});
DrawText("1", gx - 16, gy - 6, 12, Color{200, 200, 200, 255});
```

## 3. Standard vs Reverse 곡선 그리기

256 샘플의 polyline. lambda 로 *공식 함수 자체* 를 매개변수화.

<!-- snippet: src/scenes/projection_scene.cpp@depth-plot-path -->
```cpp
const int N = 256;
auto plotPath = [&](auto formula, Color c) {
    for (int i = 0; i < N; ++i) {
        float t0 = (float)i / (float)(N - 1);
        float t1 = (float)(i + 1) / (float)(N - 1);
        if (i == N - 1) break;
        float vz0, vz1;
        // ... linear / log 분기 — §4 ...
        float y0 = formula(vz0, near_, far_);
        float y1 = formula(vz1, near_, far_);
        int   px0 = gx + (int)(t0 * gw);
        int   px1 = gx + (int)(t1 * gw);
        int   py0 = gy + gh - (int)(y0 * gh);
        int   py1 = gy + gh - (int)(y1 * gh);
        DrawLine(px0, py0, px1, py1, c);
    }
};

if (showStandard_) {
    plotPath(StdNdcZ, Color{120, 200, 255, 255});
}
if (showReverse_) {
    plotPath(ReverseNdcZ, Color{255, 200, 80, 255});
}
```

직관: *Standard 곡선이 가파르게 0 에서 시작 → near 에 NDC 변화가 집중, far 에 평평*. *Reverse 곡선이 1 에서 시작 → 부드럽게 0 으로 떨어짐*. 이 그래프는 *매핑 형태* 를 보여주며, 실제 depth 정밀도는 여기에 float depth 의 ULP 분포가 결합되어 결정된다.

## 4. Log scale 옵션

near = 0.1, far = 1000 처럼 *큰 비율* 일 때 linear X 축은 *near 근처가 픽셀 1~2 개* 라 안 보인다. log scale 토글로 *near 근처를 확대*.

<!-- snippet: src/scenes/projection_scene.cpp@depth-log-scale -->
```cpp
if (logScale_) {
    // log: near 에서 far 로 log 분포
    float ln_n = std::log(near_);
    float ln_f = std::log(far_);
    vz0 = -std::exp(ln_n + (ln_f - ln_n) * t0);
    vz1 = -std::exp(ln_n + (ln_f - ln_n) * t1);
} else {
    vz0 = -(near_ + (far_ - near_) * t0);
    vz1 = -(near_ + (far_ - near_) * t1);
}
```

log 모드에서 *Standard Z 가 near 근처를 거의 다 차지하는* 모습이 명확. Reverse 는 *덜 극단적*. 사용자는 여기서 *매핑 차이* 를 보고, 실제 정밀도 이득은 float depth 의 ULP 분포와 결합해 해석한다.

## 5. Kahan vs Naive 합산

보상 규칙은 `src/algo/kahan.h` 의 `KahanAccumulator` 한 곳에만 둔다 — 그래프도 순수 함수도 같은 누적기를 쓴다.

<!-- snippet: src/algo/kahan.h#KahanAccumulator -->
```cpp
class KahanAccumulator {
public:
    KahanAccumulator() = default;
    explicit KahanAccumulator(float initial) : sum_(initial) {}
    void Add(float x) {
        float y = x - c_;          // 지난번에 잘린 몫을 되돌린다
        float t = sum_ + y;
        c_      = (t - sum_) - y;  // 이번에 잘린 몫
        sum_    = t;
    }
    float Sum() const { return sum_; }
    float Compensation() const { return c_; }
private:
    float sum_ = 0.0f;
    float c_   = 0.0f;
};
```

Scene 은 그 누적기로 오차 시계열만 만든다.

<!-- snippet: src/scenes/projection_scene.cpp@kahan-error-series -->
```cpp
float naive = 0.0f;
// 누적 규칙은 algo/kahan.h 한 곳에만 둔다.
KahanAccumulator kahan;
// ... 샘플 간격 interval + 두 점 배열 준비 ...
for (int i = 0; i < N; ++i) {
    float x = kahanSmall_;
    naive += x;
    kahan.Add(x);
    if (i % interval == 0 || i == N - 1) {
        double partial = (double)kahanSmall_ * (i + 1);
        float  errN    = (float)((double)naive       - partial);
        float  errK    = (float)((double)kahan.Sum() - partial);
        // ... naivePoints / kahanPoints 에 점 추가 ...
    }
}
```

double 로 *진실 값* (`partial`) 누적 후 float 합과 차이 = 오차.

그래프 X 축 = *반복 i*, Y 축 = *오차*. naive 가 *시간에 따라 축적* 되는 반면 Kahan 은 *0 근처에 머무름*. 시각이 *math part20 의 Kahan 효과* 를 즉시.

## 6. Absorption 케이스 표시

큰 수 + 작은 수 N 회. *비슷한 크기의 두 수를 빼서 유효 자리수가 사라지는 catastrophic cancellation* 과는 다른 함정 — 큰 수의 ULP 가 작은 수보다 커서 *작은 수가 누적에 기여 못 하는* 흡수 (absorption) 케이스 (math part20 의 *Kahan summation* 동기 그대로).

같은 헤더의 순수 함수 두 개를 그대로 부른다 — Scene 안에 합산 루프를 다시 쓰지 않는다.

<!-- snippet: src/algo/kahan.h@pure-sums -->
```cpp
inline float NaiveSum(float big, float small, int n) {
    float sum = big;
    for (int i = 0; i < n; ++i) sum += small;
    return sum;
}

inline float KahanSum(float big, float small, int n) {
    KahanAccumulator acc(big);
    for (int i = 0; i < n; ++i) acc.Add(small);
    return acc.Sum();
}
```

<!-- snippet: src/scenes/projection_scene.cpp@absorption-case -->
```cpp
float  naive2 = NaiveSum(kahanBig_, kahanSmall_, N);
float  kSum2  = KahanSum(kahanBig_, kahanSmall_, N);
double truth2 = (double)kahanBig_ + (double)kahanSmall_ * N;
```

세 값을 캔버스 아래쪽에 한 줄로 찍는다.

<!-- snippet: src/scenes/projection_scene.cpp@absorption-text -->
```cpp
std::snprintf(buf, sizeof(buf),
               "Big + small case (big=%.1f): naive=%.6f, kahan=%.6f, truth=%.6f",
               kahanBig_, naive2, kSum2, truth2);
DrawText(buf, cx + 10, cy + ch - 30, 12, Color{220, 220, 220, 255});
```

big = 100,000, small = 0.0001, N = 100,000 일 때:

- truth ≈ 100,010.0
- naive ≈ 100,000 (작은 수가 *전부 ULP 미만으로 묻힘*)
- kahan ≈ 100,010 (정확)

이 한 줄로 *absorption 함정 + Kahan 의 효과* 가 즉시 보인다 — math part20 의 핵심 메시지.

## 7. 패널 매개변수

Mode A 의 패널 — near / far 슬라이더 + 토글.

<!-- snippet: src/scenes/projection_scene.cpp#ProjectionScene::DrawPanel -->
```cpp
GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
           nullptr, nullptr, &near_, 0.001f, 10.0f);
// ... far 라벨 ...
GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
           nullptr, nullptr, &far_, 100.0f, 10000.0f);
y += 42;

GuiCheckBox(Rectangle{(float)(px + 8), (float)y, 16, 16},
             " standard z [0,1]", &showStandard_);
y += 22;
GuiCheckBox(Rectangle{(float)(px + 8), (float)y, 16, 16},
             " reverse z [1,0]", &showReverse_);
y += 22;
GuiCheckBox(Rectangle{(float)(px + 8), (float)y, 16, 16},
             " log X axis", &logScale_);
```

Mode B 의 패널 — N / small / big 슬라이더.

<!-- snippet: src/scenes/projection_scene.cpp#ProjectionScene::DrawPanel -->
```cpp
GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
           nullptr, nullptr, &n, 100.0f, 1000000.0f);
kahanN_ = (int)n;
// ... small dx 라벨 ...
GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
           nullptr, nullptr, &kahanSmall_, 0.00001f, 0.01f);
// ... big initial 라벨 ...
GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
           nullptr, nullptr, &kahanBig_, 0.0f, 100000.0f);
```

N 이 1M 까지 — 한 프레임 안에 double 참값 루프, 시계열 루프의 naive + kahan, 그리고 `NaiveSum` / `KahanSum` 까지 *O(N) 합산 다섯 개* 를 끝내야 60fps 안전. 1M 덧셈 다섯 번 = 약 5ms (MSVC /O2 실측 4.8ms). 안전 범위.

## 8. 빌드와 실행 결과 + math_lab 의 마무리

```bash
cmake --build build --config Release
./build/Release/math_lab.exe
```

탭이 8 개 모두 등록.

*Welcome / Rotation / Interpolation / Noise / Collision / Spatial / BVH / SDF / RayMarch / Projection / Stability*.

마지막 탭 (Projection / Stability) 에서:

- *Depth 모드 + log X*: 두 곡선의 매핑 모양 차이가 명확. Standard 는 *near 근처가 가파르고 (NDC 변화율 크고) far 가 평평* — float depth 의 0 근처 정밀도 이점이 *Near 에 두 번 몰림*. Reverse 는 *far 가 0 에 매핑* — float ULP 분포와 결합되어 *전 구간 더 균형 잡힌 정밀도*.
- *near 슬라이더를 0.001 같이 작게*: 표준 곡선이 *극단적으로 가팔라짐* — near 가 정밀도를 결정.
- *far 슬라이더 변화*: 거의 영향 없음 — math part18 의 *near 가 더 큰 영향* 통찰을 직접 본다.
- *Kahan 모드*: 빨간 (naive) 그래프가 시간에 따라 *축적* 되는 반면 초록 (Kahan) 은 *0 근처에 머무름*.
- *Big + small 결과 (하단)*: naive 가 *작은 수가 묻혀 truth 와 큰 차이*, Kahan 은 *truth 와 일치*.

빌드: MSVC `/W4`, 8 Scene 모두 등록 후도 *경고 0 (third-party 외)* + `math_lab.exe` 정상.

이 Scene 의 계산은 전부 `src/algo/depth.h` 와 `src/algo/kahan.h` 로 나와 있어, 화면을 띄우지 않고도 위 수치가 그대로 검사된다. 이 step 이 `tests/core_tests.cpp` 에 추가한 것은 네 개다 — `depth/std_vs_reverse`, `kahan/compensated_sum`, 그리고 이 Scene 이 쓰지는 않지만 `src/algo/mat.h` 로 들어온 계산부를 검사하는 `vector/cross_project_reflect` 와 `matrix/inverse_orthogonal_trs`.

깊이 쪽은 끝점 네 개 (`StdNdcZ(-n)=0`, `StdNdcZ(-f)=1`, reverse 는 각각 1 과 0) 로 시작하지만, 끝점 네 개는 함수별 기대값을 고정하므로 두 함수를 통째로 뒤바꾸면 첫 CHECK 부터 걸리지만, 끝점만 맞춘 다른 매핑 — 예컨대 거리 선형 `(-z-n)/(f-n)` 과 그 여집합 — 은 통과한다. 그래서 전 구간을 41 지점으로 훑어 *단조성* 과 *`reverse == 1 - std`* 를 함께 걸고, 절반 거리의 값을 두 CHECK 로 더 본다 — 선형 매핑을 실제로 걸러내는 것은 이 마지막 둘이다.

<!-- snippet: tests/core_tests.cpp@step08-depth-reverse -->
```cpp
float prev = -1.0f;
for (int i = 0; i <= 40; ++i) {
    // ...
    CHECK(sv >= prev - 1e-6f);
    CHECK(Near(rv, 1.0f - sv, 1e-5f));
    prev = sv;
}

// 표준 depth 는 근거리에 극단적으로 몰린다 — 절반 거리에서 이미 0.999 이상.
CHECK(mathlab::StdNdcZ(-(n + (f - n) * 0.5f), n, f) > 0.99f);
// 반대로 reverse-Z 는 같은 지점에서 0.01 미만이다.
CHECK(mathlab::ReverseNdcZ(-(n + (f - n) * 0.5f), n, f) < 0.01f);
```

마지막 두 CHECK 가 화면에서 보던 것 — *표준 곡선이 near 쪽에 쏠린다* — 을 수치로 바꾼 것이다. 절반 거리에서 이미 0.99 를 넘는다는 것은 남은 절반 구간이 `[0.99, 1]` 안으로 짜부라졌다는 뜻이다.

Kahan 쪽은 합계 비교 (big=100,000 / small=0.0001 / N=100,000 에서 `|kahan - truth| < 1e-3`, `|naive - truth| > 1`) 위에 누적기 자체의 계약을 하나 더 건다. 보상항이 *잘린 몫을 실제로 담고 있는지* 를 직접 본다.

<!-- snippet: tests/core_tests.cpp@step08-kahan-absorption -->
```cpp
mathlab::KahanAccumulator acc(1.0f);
acc.Add(1e-8f);
CHECK(Near(acc.Sum(), 1.0f, 1e-9f));      // float 1.0 에 흡수되고
CHECK(std::fabs(acc.Compensation()) > 1e-9f);  // 보상항에 남는다
for (int i = 0; i < 1000; ++i) acc.Add(1e-8f);
CHECK(acc.Sum() > 1.0f);   // 모이면 되살아난다
```

세 줄이 이 알고리즘의 전부다 — 한 번은 묻히고, 보상항에 남고, 모이면 되살아난다. 보상항 대입을 `c_ = 0.0f` 로 지우면 두 번째와 세 번째가 함께 깨진다.

`mat.h` 쪽은 항등식으로 잡는다. `M·M⁻¹ = I` 를 양쪽 모두, 직교행렬의 `Rᵀ = R⁻¹` 과 `det(R) = 1` 을.

<!-- snippet: tests/core_tests.cpp@step08-matrix-identities -->
```cpp
const Matrix inv = mathlab::MatInverse(trs);
CHECK(NearMat(mathlab::MatMul(trs, inv), mathlab::MatIdentity(), 1e-5f));
CHECK(NearMat(mathlab::MatMul(inv, trs), mathlab::MatIdentity(), 1e-5f));
// ...
CHECK(Near(mathlab::MatDeterminant(R), 1.0f, 1e-4f));
```

여기에 `QuatToMatrix(q)·v == QuatRotateVec(q, v)` 를 무작위 벡터 100 개로 대조한다 — 행렬 경로와 쿼터니언 경로가 *독립 구현* 이므로 서로의 오라클이 된다. TRS 와 SRT 가 다른 변환이라는 비가환성, 열 우선 규약 (평행이동이 네 번째 열), 특이행렬에서 `MatInvert` 가 거짓을 돌려주는 것까지 함께 본다.

이 항등식들은 *고른 입력 하나* 에서 확인한 것이다. `trs` 는 축 `(0.3, -0.5, 0.8)` 에 각 1.1, 스케일 `(2, 0.5, 1.5)` 하나뿐이다. 그런데 역행렬의 정확도는 입력의 *조건수* 에 따라 달라지므로, 표본 하나로는 "어느 범위까지 이 오차 안에 드는가" 를 말할 수 없다. `1e-5f` 라는 숫자가 어디서 왔는지도 코드에 적혀 있지 않다. property 층이 그 둘을 명시한다.

먼저 생성기가 조건수를 상계한다. 스케일 파라미터는 `[-4, 4]` 에서 뽑되 `|s| >= 0.1` 로 *보정* 되므로 선형부의 조건수 κ = max|s| / min|s| 는 40 을 넘지 않는다. 표본을 만들어 보고 나쁘면 버리는 rejection 이 아니라 domain 안으로 옮기는 구성이라 축소 뒤에도 조건이 유지된다. 그래도 `κ > 1e3` 이면 판정하지 않는다는 문을 코드에 남겨 뒀다 — 생성기를 넓히면 그 문이 먼저 걸린다.

허용 오차는 통과시키려고 고른 값이 아니라 오차 증폭에서 유도한다. `M p + t` 를 계산할 때 잃는 자릿수가 `|t| + max|s|·|p|` 규모이고, `M⁻¹` 을 적용하면 `1/min|s|` 배 커진다.

<!-- snippet: tests/core_tests.cpp@step08-property-inverse-tolerance -->
```cpp
const double tol = 64.0 * kUnitRoundoff * kappa *
                   ((double)mathlab::V3Length(pt) +
                    (double)mathlab::V3Length(t) / maxS + 1.0);

const Vector3 back = mathlab::MatTransformPoint(inv, mathlab::MatTransformPoint(M, pt));
if (!obs.Bound("|M^-1 (M p) - p|",
               (double)mathlab::V3Length(mathlab::V3Sub(back, pt)), tol)) ok = false;
```

같은 property 의 행렬식 조항은 상대 오차가 아니라 *절대* 상계를 쓴다. TRS 의 행렬식은 스케일 곱이지만 3×3 전개는 크기 `max|s|³` 인 항들이 상쇄되며 그 값을 만들기 때문에, 스케일이 극단적으로 갈리면 상대 오차가 커지는 것이 정상이다. 상계를 `64u · max|s|³` 로 잡는 것이 그 사정을 정직하게 적는 방법이다.

두 번째 property 는 *합성 대 개별 적용* 이다. `(A·B) p` 와 `A (B p)` 가 같아야 한다. 여기에 tolerance 가 아니라 `==` 로 잡히는 조항이 둘 붙는다.

<!-- snippet: tests/core_tests.cpp@step08-property-exact-transpose -->
```cpp
if (!obs.Bound("(A B)^T != B^T A^T (exact)",
               MatBitEqual(mathlab::MatTranspose(AB),
                           mathlab::MatMul(mathlab::MatTranspose(B),
                                           mathlab::MatTranspose(A)))
                   ? 0.0 : 1.0,
               0.0)) ok = false;
if (!obs.Bound("A * I != A (exact)",
               MatBitEqual(mathlab::MatMul(A, mathlab::MatIdentity()), A) ? 0.0 : 1.0,
               0.0)) ok = false;

obs.interesting = MatMaxDiff(AB, mathlab::MatMul(B, A)) > 1e-3;
```

`(A·B)ᵀ` 와 `Bᵀ·Aᵀ` 는 *같은 곱을 같은 순서로* 더한다. `MatMul` 의 누적 순서가 양쪽에서 동일하고 IEEE 곱셈의 교환법칙은 정확하므로, 결과는 근사가 아니라 비트가 같다. 계약이 허용하는 가장 강한 관계를 고른다는 원칙이 적용된 자리다. 마지막 줄의 비가환 조항이 대조군이다 — 곱이 가환으로 바뀌면 위 관계들이 자명해진다.

세 번째 `matrix/quat-agreement` 는 고정 사례가 벡터 100 개로 하던 행렬 경로 ⇔ 쿼터니언 경로 대조를 무작위 축·각·비단위 스케일로 확장하고, `RᵀR = I` 와 `det(R) = 1` 을 함께 건다.

축소기가 하는 일은 여기서 가장 잘 보인다. `MatInvert` 의 Gauss-Jordan 소거 부호를 `-=` 에서 `+=` 로 뒤집는 변이를 넣으면 보고가 이렇게 나온다.

```text
$ ./build/Release/math_core_tests.exe --only=matrix/inverse-roundtrip
  property 'matrix/inverse-roundtrip' found a counterexample
    contract : M^-1 (M p) == p within 64u * kappa * scale; kappa is capped by the generator
    sample   : 0 of 3000   (master seed 20260823, scale 1)
    broke    : |M^-1 (M p) - p| = 1.39106e-05   (bound 7.62939e-06)
    generated: t.x=-7.729573 t.y=6.940718 t.z=0.1635618 axis.x=-0.1897763 axis.y=0.3894504 axis.z=0.9032391 angle=-3.64658 s.x=-0.9783964 s.y=1.746374 s.z=4 p.x=-2.265283 p.y=-8 p.z=0.0003816442
    shrunk   : t.x=0 t.y=0 t.z=0 axis.x=0 axis.y=0 axis.z=1 angle=-6.9553e-06 s.x=1 s.y=1 s.z=1 p.x=1 p.y=0 p.z=0   (31 shrink step(s))
    repro    : math_core_tests step08 --only=matrix/inverse-roundtrip --seed=20260823 --sample=0
```

31 번의 축소를 거쳐 평행이동 0, 스케일 `(1, 1, 1)`, 점 `(1, 0, 0)`, 축 `(0, 0, 1)` 만 남았다. 회전각만 `-6.9553e-06` 으로 남는데, 이 잔여물이 변이의 성질을 그대로 말해 준다 — 각이 정확히 0 이면 `M` 이 항등이라 소거 루프가 `f == 0` 에서 건너뛰어 통과하고, 조금이라도 회전이 있으면 즉시 깨진다. 사람이 손으로 줄였다면 각을 0 으로 만들어 놓고 "재현이 안 된다" 고 했을 자리다. 축소 후보가 `[lo, hi]` 밖으로 나가지 않고 실패가 유지되는 것만 채택하기 때문에 이런 잔여물이 살아남는다.

<!-- snippet: tests/property.h@property-shrink-candidates -->
```cpp
for (int c = 0; c < 5 && budget > 0; ++c) {
    const float candidate = candidates[c];
    // 후보가 domain 안에 있으므로 축소 뒤에도 사전조건이 유지된다.
    if (!(candidate >= spec.lo && candidate <= spec.hi)) continue;
    if (candidate == current) continue;
    // "더 단순하다" 의 정의 — 목표값에 더 가깝다.
    if (std::fabs(candidate - spec.simplest) >
        std::fabs(current - spec.simplest)) {
        continue;
    }
    values[(size_t)k] = candidate;
    --budget;
    Observation trial;
    if (StillFails(property, values, trial)) {
        obs   = trial;      // 실패가 유지됐다 — 축소를 채택한다.
        moved = true;
        ++steps;
        break;
    }
    values[(size_t)k] = current;
}
```

```text
$ ctest --test-dir build -C Release -R math_step08_projection --output-on-failure
    Start 8: math_step08_projection
1/1 Test #8: math_step08_projection ...........   Passed    0.03 sec

100% tests passed out of 1
```

```text
$ ./build/Release/math_core_tests.exe step08
[step08] depth/std_vs_reverse ............ 88 checks, 0 failed
[step08] kahan/compensated_sum ........... 8 checks, 0 failed
[step08] vector/cross_project_reflect .... 1208 checks, 0 failed
[step08] matrix/inverse_orthogonal_trs ... 322 checks, 0 failed
[step08] matrix/inverse-roundtrip ........ 3 checks, 0 failed  (3000 samples, 1386 non-degenerate, 0 skipped, worst 0.250x on '|det(M) - sx sy sz|')
[step08] matrix/compose-vs-apply ......... 3 checks, 0 failed  (2000 samples, 2000 non-degenerate, 0 skipped, worst 0.021x on '|(A B) v - A (B v)| (direction)')
[step08] matrix/quat-agreement ........... 3 checks, 0 failed  (3000 samples, 2480 non-degenerate, 0 skipped, worst 0.312x on 'max|R^T R - I|')
step08: 7 case(s), 1635 check(s), 0 failure(s)
```

property 층 전체는 별도 타깃으로도 돈다. 어느 step 이 깨졌는지는 step 타깃 이름이 알려 주고, 시드 무작위 검증만 따로 보고 싶을 때는 `math_property_layer` 하나면 된다.

```text
$ ctest --test-dir build -C Release -R math_property_layer --output-on-failure
    Start 10: math_property_layer
1/1 Test #10: math_property_layer ..............   Passed    0.07 sec

100% tests passed out of 1
```

검사가 실제로 결함을 잡는지는 변이 주입으로 확인했다. `StdNdcZ` 의 `A` 부호를 뒤집으면 `depth/std_vs_reverse` 가, `KahanAccumulator::Add` 의 보상항 대입을 `c_ = 0.0f` 로 지우면 `kahan/compensated_sum` 이 즉시 실패한다. property 쪽은 셋 모두 각자의 변이를 갖는다 — Gauss-Jordan 소거 부호 반전은 `matrix/inverse-roundtrip` 을, `MatMul` 이 `B` 를 전치해 곱하게 바꾸면 `matrix/compose-vs-apply` 를, `QuatToMatrix` 의 `m1 = xy + wz` 를 `xy - wz` 로 바꾸면 `matrix/quat-agreement` 를 죽인다. 변이를 넣고 재빌드해 죽는 것을 본 뒤 원본 바이트로 되돌리고 SHA256 을 대조하는 것까지가 한 번의 확인이다.

---

## 결론

Step 8 + math_lab 전체에서 만든 것.

| Step | Scene | math 시리즈 매핑 |
|---|---|---|
| 1 | Welcome (skeleton) | — (Scene 골격 + DragHandle + chrome) |
| 2 | Rotation | part04~05 (회전 / 쿼터니언 / 짐벌락 / Slerp) |
| 3 | Interpolation | part07~08 (보간 / 스플라인 / arc-length) |
| 4 | Noise | part09~10 (PRNG / Value / Perlin / Simplex / FBM) |
| 5 | Collision | part11~13 (OBB / SAT / GJK 단계별) |
| 6 | Spatial / BVH | part15~16 (분포 / BVH 빌드 / 광선 가지치기) |
| 7 | SDF / RayMarch | part14 (해석적 SDF / smin / Sphere Tracing) |
| 8 | Projection / Stability | part18 + part20 (Reverse-Z / Kahan) |

각 Scene 이 *math 시리즈의 도식과 수식을 마우스 조작에 반응하는 시각으로* 만들었다. 시리즈의 *주장* 이 lab 에서는 *직접 확인 가능한 증거*.

이 lab 의 8 Step 빌드가 *math 시리즈의 컴패니언 도구* 로 완결. 시리즈를 읽다가 의문이 생기는 지점 (*짐벌락이 어떻게 보이는가, smin 의 k 값이 어떻게 효과 하는가, BVH 가 광선을 얼마나 가지치기하는가, Reverse-Z 가 정말 정밀도를 개선하는가*) 에서 lab 의 해당 Scene 을 열고 슬라이더를 끌면 즉시 답이 보인다.

이 step 의 한계. 깊이 정밀도는 *NDC 매핑 곡선* 만 그릴 뿐 실제 float 비트 분포에서 몇 개의 서로 다른 깊이 값이 나오는지는 세지 않는다 — 검증도 매핑 값까지이고 "그래서 z-fighting 이 줄어드는가" 는 재지 않는다. 벡터 (part02)·행렬 (part03)·좌표계 (part06) 는 `src/algo/mat.h` 로 계산부만 들어와 검사되지만, 이 축을 마우스로 만지는 Scene 은 없다 — 이 lab 에서 유일하게 *화면 없는* 코드다. property 층이 덮는 범위도 생성기가 그은 상자 안뿐이다. 조건수는 40 까지, 평행이동은 ±8 까지만 보므로 그 밖에서 `MatInvert` 가 어떻게 무너지는지는 미측정이고, 깊이 매핑과 Kahan 누적기에는 아직 property 가 없다 — 둘 다 *명령 sequence* 에서 성질이 드러나는 대상이라 단일 호출용 하네스로는 담기지 않는다.

## 더 읽을거리

- Nathan Reed, ["Depth Precision Visualized"](https://www.reedbeta.com/blog/depth-precision-visualized/) — Reverse-Z 정밀도 분석의 표준 레퍼런스.
- William Kahan, "Further remarks on reducing truncation errors" (CACM 1965) — Kahan 합산 원전.
- David Goldberg, ["What Every Computer Scientist Should Know About Floating-Point Arithmetic"](https://docs.oracle.com/cd/E19957-01/806-3568/ncg_goldberg.html) — 부동소수점 오차의 고전.
