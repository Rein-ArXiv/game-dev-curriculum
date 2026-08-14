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

```cpp
int margin = 60;
int gx = cx + margin, gy = cy + 60;
int gw = cw - margin * 2, gh = ch - 100;

// 축
DrawLine(gx, gy + gh, gx + gw, gy + gh, gray);   // X
DrawLine(gx, gy,      gx,      gy + gh, gray);   // Y
```

라벨 + 0/1 표시.

```cpp
DrawText("View Z (near → far)", gx + gw / 2 - 60, gy + gh + 6, 12, white);
DrawText("NDC z", gx - 50, gy + gh / 2, 12, white);
DrawText("0", gx - 16, gy + gh - 6, 12, white);
DrawText("1", gx - 16, gy - 6, 12, white);
```

## 3. Standard vs Reverse 곡선 그리기

256 샘플의 polyline. lambda 로 *공식 함수 자체* 를 매개변수화.

```cpp
const int N = 256;
auto plotPath = [&](auto formula, Color c) {
    for (int i = 0; i < N - 1; ++i) {
        float t0 = (float)i / (N - 1);
        float t1 = (float)(i + 1) / (N - 1);
        float vz0 = -(near_ + (far_ - near_) * t0);   // linear
        float vz1 = -(near_ + (far_ - near_) * t1);
        float y0 = formula(vz0, near_, far_);
        float y1 = formula(vz1, near_, far_);
        int   px0 = gx + (int)(t0 * gw);
        int   px1 = gx + (int)(t1 * gw);
        int   py0 = gy + gh - (int)(y0 * gh);
        int   py1 = gy + gh - (int)(y1 * gh);
        DrawLine(px0, py0, px1, py1, c);
    }
};

if (showStandard_) plotPath(StdNdcZ,     blue);
if (showReverse_)  plotPath(ReverseNdcZ, orange);
```

직관: *Standard 곡선이 가파르게 0 에서 시작 → near 에 NDC 변화가 집중, far 에 평평*. *Reverse 곡선이 1 에서 시작 → 부드럽게 0 으로 떨어짐*. 이 그래프는 *매핑 형태* 를 보여주며, 실제 depth 정밀도는 여기에 float depth 의 ULP 분포가 결합되어 결정된다.

## 4. Log scale 옵션

near = 0.1, far = 1000 처럼 *큰 비율* 일 때 linear X 축은 *near 근처가 픽셀 1~2 개* 라 안 보인다. log scale 토글로 *near 근처를 확대*.

```cpp
if (logScale_) {
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

```cpp
class KahanAccumulator {
public:
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

```cpp
float naive = 0.0f;
KahanAccumulator kahan;
for (int i = 0; i < N; ++i) {
    float x = kahanSmall_;
    naive += x;
    kahan.Add(x);
    if (i % interval == 0) {
        double partial = (double)kahanSmall_ * (i + 1);
        float  errN    = (float)((double)naive       - partial);
        float  errK    = (float)((double)kahan.Sum() - partial);
        // 점 추가
    }
}
```

double 로 *진실 값* (`partial`) 누적 후 float 합과 차이 = 오차.

그래프 X 축 = *반복 i*, Y 축 = *오차*. naive 가 *시간에 따라 축적* 되는 반면 Kahan 은 *0 근처에 머무름*. 시각이 *math part20 의 Kahan 효과* 를 즉시.

## 6. Absorption 케이스 표시

큰 수 + 작은 수 N 회. *비슷한 크기의 두 수를 빼서 유효 자리수가 사라지는 catastrophic cancellation* 과는 다른 함정 — 큰 수의 ULP 가 작은 수보다 커서 *작은 수가 누적에 기여 못 하는* 흡수 (absorption) 케이스 (math part20 의 *Kahan summation* 동기 그대로).

같은 헤더의 순수 함수 두 개를 그대로 부른다 — Scene 안에 합산 루프를 다시 쓰지 않는다.

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

```cpp
float  naive2 = NaiveSum(kahanBig_, kahanSmall_, N);
float  kSum2  = KahanSum(kahanBig_, kahanSmall_, N);
double truth2 = (double)kahanBig_ + (double)kahanSmall_ * N;

char buf[200];
std::snprintf(buf, sizeof(buf),
               "Big + small case (big=%.1f): naive=%.6f, kahan=%.6f, truth=%.6f",
               kahanBig_, naive2, kSum2, truth2);
DrawText(buf, cx + 10, cy + ch - 30, 12, ...);
```

big = 100,000, small = 0.0001, N = 100,000 일 때:

- truth ≈ 100,010.0
- naive ≈ 100,000 (작은 수가 *전부 ULP 미만으로 묻힘*)
- kahan ≈ 100,010 (정확)

이 한 줄로 *absorption 함정 + Kahan 의 효과* 가 즉시 보인다 — math part20 의 핵심 메시지.

## 7. 패널 매개변수

Mode A 의 패널 — near / far 슬라이더 + 토글.

```cpp
GuiSlider(rect, ..., &near_, 0.001f, 10.0f);
GuiSlider(rect, ..., &far_,  100.0f, 10000.0f);
GuiCheckBox(rect, " standard z [0,1]", &showStandard_);
GuiCheckBox(rect, " reverse z [1,0]",  &showReverse_);
GuiCheckBox(rect, " log X axis",        &logScale_);
```

Mode B 의 패널 — N / small / big 슬라이더.

```cpp
GuiSlider(rect, ..., &n,           100.0f, 1000000.0f);  // N
GuiSlider(rect, ..., &kahanSmall_, 0.00001f, 0.01f);
GuiSlider(rect, ..., &kahanBig_,   0.0f, 100000.0f);
```

N 이 1M 까지 — 한 프레임 안에 *naive + kahan 두 합산* 을 끝내야 60fps 안전. 1M float 덧셈 두 번 = 약 2~3ms. 안전 범위.

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

```text
$ ctest --test-dir build -C Release --output-on-failure
    Start 1: math_core_tests
1/1 Test #1: math_core_tests ..................   Passed    0.04 sec
100% tests passed out of 1
```

이 Scene 의 계산은 전부 `src/algo/depth.h` 와 `src/algo/kahan.h` 로 나와 있고, `tests/core_tests.cpp` 의 `TestDepth()` / `TestKahan()` 이 위 수치를 그대로 못 박는다 — `StdNdcZ(-n, n, f) == 0`, `StdNdcZ(-f, n, f) == 1`, reverse 는 각각 1 과 0, 전 구간 `reverse == 1 - std`, 그리고 big = 100,000 / small = 0.0001 / N = 100,000 에서 `|kahan - truth| < 1e-3` 과 `|naive - truth| > 1`. 화면을 띄우지 않고도 이 Scene 의 주장이 검사된다.

검사가 실제로 결함을 잡는지는 변이 주입으로 확인했다. `StdNdcZ` 의 `A` 부호를 뒤집으면 `TestDepth` 가, `KahanAccumulator::Add` 의 보상항 대입을 `c_ = 0.0f` 로 지우면 `TestKahan` 이 즉시 실패한다.

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

이 step 의 한계. 깊이 정밀도는 *NDC 매핑 곡선* 만 그릴 뿐 실제 float 비트 분포에서 몇 개의 서로 다른 깊이 값이 나오는지는 세지 않는다. 벡터 (part02)·행렬 (part03)·좌표계 (part06) 는 `src/algo/mat.h` 로 계산부만 들어와 `tests/core_tests.cpp` 의 `TestVectorAlgebra()` / `TestMatrix()` 가 항등식 (`M·M⁻¹ = I`, 직교행렬의 `Rᵀ = R⁻¹` 과 `det = 1`, `QuatToMatrix(q)·v == QuatRotateVec(q, v)`, TRS 와 SRT 의 비가환성, 전치 왕복) 을 검사할 뿐, 이 축을 마우스로 만지는 Scene 은 없다.

## 더 읽을거리

- Nathan Reed, ["Depth Precision Visualized"](https://www.reedbeta.com/blog/depth-precision-visualized/) — Reverse-Z 정밀도 분석의 표준 레퍼런스.
- William Kahan, "Further remarks on reducing truncation errors" (CACM 1965) — Kahan 합산 원전.
- David Goldberg, ["What Every Computer Scientist Should Know About Floating-Point Arithmetic"](https://docs.oracle.com/cd/E19957-01/806-3568/ncg_goldberg.html) — 부동소수점 오차의 고전.
