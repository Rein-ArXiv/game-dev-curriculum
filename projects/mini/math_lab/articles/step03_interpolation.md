## 들어가며

`math_lab` 의 세 번째 단계 — *Interpolation Scene* . math 시리즈의 *part07 (보간)* + *part08 (스플라인)* 가 *마우스로 control point 를 끌면 곡선이 즉시 갱신되는* 형태로 살아난다.

이 단계의 결과물.

- **곡선 종류 토글**: Cubic Bezier / Catmull-Rom / Cubic Hermite. 같은 4 control point 위에서 *세 곡선의 모양 차이* 를 즉시 비교.
- **드래그 가능한 4 control point**: P0~P3. 마우스로 끌면 곡선 모양 즉시 갱신.
- **t 슬라이더 + 자동 재생**: 곡선 위에서 *움직이는 점*. t ∈ [0, 1].
- **arc-length parameterization 토글**: *매개변수 t 등속 (불균일 속도)* vs *호 길이 등속 (실제 등속)*. 같은 t 값에 다른 위치를 보여 *매개변수와 거리의 차이* 를 확인.
- **곡선 해상도 슬라이더**: 8~256 샘플. 직접 라인 스트립.

핵심 통찰들 — *Bezier 의 control polygon 은 곡선 모양의 윤곽*, *Catmull-Rom 은 모든 control point 를 통과 (= 보간 곡선)*, *Hermite 는 두 점 + 두 접선*, *t 등속이 항상 거리 등속 아님* — 이 모두가 *마우스 상호작용으로 즉시 보임*.

---

## 목차
1. [곡선 evaluator 헤더](#1-곡선-evaluator-헤더)
2. [InterpolationScene 의 자료구조](#2-interpolationscene-의-자료구조)
3. [4 control point 드래그 핸들](#3-4-control-point-드래그-핸들)
4. [세 곡선 평가 함수](#4-세-곡선-평가-함수)
5. [라인 스트립으로 곡선 그리기](#5-라인-스트립으로-곡선-그리기)
6. [Arc-Length Parameterization](#6-arc-length-parameterization)
7. [패널과 시각 디테일](#7-패널과-시각-디테일)
8. [빌드와 실행 결과](#8-빌드와-실행-결과)

---

## 1. 곡선 evaluator 헤더

`algo/curves.h` 가 *모든 곡선 식* 의 한 곳. math part07/08 의 식을 그대로 함수화.

<!-- snippet: src/algo/curves.h -->
```cpp
inline Vector2 V2Add(Vector2 a, Vector2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vector2 V2Sub(Vector2 a, Vector2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vector2 V2Mul(Vector2 a, float s)   { return {a.x * s,   a.y * s  }; }

inline Vector2 Lerp2(Vector2 a, Vector2 b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

// 3차 Bezier — 4 control point. P0, P1, P2, P3.
inline Vector2 CubicBezier(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3, float t) {
    float u = 1.0f - t;
    float b0 = u * u * u;
    float b1 = 3.0f * u * u * t;
    float b2 = 3.0f * u * t * t;
    float b3 = t * t * t;
    return {
        p0.x * b0 + p1.x * b1 + p2.x * b2 + p3.x * b3,
        p0.y * b0 + p1.y * b1 + p2.y * b2 + p3.y * b3,
    };
}

// Cubic Hermite — 두 점 + 두 접선.
inline Vector2 CubicHermite(Vector2 p0, Vector2 m0, Vector2 p1, Vector2 m1, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    float h00 = 2 * t3 - 3 * t2 + 1;
    float h10 = t3 - 2 * t2 + t;
    float h01 = -2 * t3 + 3 * t2;
    float h11 = t3 - t2;
    return {
        p0.x * h00 + m0.x * h10 + p1.x * h01 + m1.x * h11,
        p0.y * h00 + m0.y * h10 + p1.y * h01 + m1.y * h11,
    };
}

// Catmull-Rom (uniform). 4 점 P0..P3 사이의 P1→P2 구간을 t∈[0,1] 로.
// 접선: m1 = 0.5 * (P2 - P0), m2 = 0.5 * (P3 - P1).
inline Vector2 CatmullRom(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3, float t) {
    Vector2 m1 = V2Mul(V2Sub(p2, p0), 0.5f);
    Vector2 m2 = V2Mul(V2Sub(p3, p1), 0.5f);
    return CubicHermite(p1, m1, p2, m2, t);
}
```

함수 형태로 분리하면 *Scene 외부에서도 쉽게 재활용*. 이후 Spline editor 같은 다른 Scene 이 이걸 호출.

설계 결정.

- **`Vector2` 를 그대로 + 최소 벡터 연산 세 개** — raylib 자료구조. GLM 도 raymath 도 안 쓴다. `V2Add / V2Sub / V2Mul` 만 있으면 이 헤더가 자족한다.
- **Catmull-Rom 을 Hermite 로 환원**: Catmull-Rom 의 표준 정의가 *접선 = 0.5 × (P_{i+1} - P_{i-1})* 인 Hermite 곡선. 같은 함수를 두 번 작성하지 않는다.
- **`inline` 헤더 only**: cpp 파일 분리 안 함. 함수 전부 작아서 header-only 가 더 깔끔.

## 2. InterpolationScene 의 자료구조

<!-- snippet: src/scenes/interpolation_scene.h#InterpolationScene -->
```cpp
class InterpolationScene : public IScene {
public:
    // ... IScene 인터페이스 ...

private:
    enum class Curve { Bezier, CatmullRom, Hermite };

    Curve      curve_       = Curve::Bezier;
    DragHandle p0_, p1_, p2_, p3_;
    bool       initialized_ = false;
    float      tParam_      = 0.0f;
    bool       autoPlay_    = true;
    bool       useArcLen_   = false;
    int        sampleCount_ = 64;
    // ... Vector2 SampleCurve(float t) const; — §4 ...
};
```

상태가 *입력 + 토글 + 진행* 셋으로 명확. 마우스 입력 → 4 핸들 → 곡선 → 그리기.

## 3. 4 control point 드래그 핸들

Step 1 의 `DragHandle` 을 *그대로* 사용. 4 개를 멤버로.

<!-- snippet: src/scenes/interpolation_scene.cpp#InterpolationScene::Update -->
```cpp
void InterpolationScene::Update(float dt) {
    p0_.Update();
    p1_.Update();
    p2_.Update();
    p3_.Update();
    if (autoPlay_) {
        tParam_ += dt * 0.25f;
        if (tParam_ > 1.0f) tParam_ -= 1.0f;
    }
}
```

Step 1 에서 깐 *capture 정책* 덕분에 *4 핸들 중 하나만* 잡힌다. 핸들이 겹치는 케이스에도 안전.

처음 활성화될 때 캔버스 크기를 모르니 *지연 초기화*. 첫 `DrawWorld` 에서 적당한 위치에 배치.

<!-- snippet: src/scenes/interpolation_scene.cpp@handle-init -->
```cpp
if (!initialized_) {
    p0_.SetPosition({(float)(cx + cw * 0.20f), (float)(cy + ch * 0.70f)});
    p1_.SetPosition({(float)(cx + cw * 0.35f), (float)(cy + ch * 0.25f)});
    p2_.SetPosition({(float)(cx + cw * 0.65f), (float)(cy + ch * 0.25f)});
    p3_.SetPosition({(float)(cx + cw * 0.80f), (float)(cy + ch * 0.70f)});
    initialized_ = true;
}
```

## 4. 세 곡선 평가 함수

토글에 따라 *같은 4 점에 다른 식* 을 적용.

<!-- snippet: src/scenes/interpolation_scene.cpp#InterpolationScene::SampleCurve -->
```cpp
Vector2 InterpolationScene::SampleCurve(float t) const {
    Vector2 a = p0_.Position();
    Vector2 b = p1_.Position();
    Vector2 c = p2_.Position();
    Vector2 d = p3_.Position();
    switch (curve_) {
        case Curve::Bezier:     return CubicBezier(a, b, c, d, t);
        case Curve::CatmullRom: return CatmullRom(a, b, c, d, t);
        case Curve::Hermite:
        default: {
            Vector2 m0 = V2Sub(b, a);
            Vector2 m1 = V2Sub(d, c);
            return CubicHermite(a, m0, c, m1, t);
        }
    }
}
```

Hermite 의 *접선* 매핑이 흥미롭다. P1 - P0 을 *시작 접선*, P3 - P2 을 *끝 접선* 으로 사용 — 즉 *P0/P2 가 곡선의 끝점, P1/P3 가 접선의 끝점*. Bezier 와 *기하 의미가 다른* 4 점 입력.

이 매핑 덕분에 같은 4 핸들로 *세 곡선이 다른 모양* 을 보인다.

| 곡선 | P0~P3 의 의미 | 시각 효과 |
|---|---|---|
| Bezier | P0/P3 = 끝점, P1/P2 = control point (곡선 안 지남) | control polygon 가까이 곡선 |
| Catmull-Rom | P0/P3 = 외부 관점, P1/P2 = 곡선이 지나는 점 | P1/P2 통과 |
| Hermite | P0/P2 = 끝점, P1/P3 = 접선 끝 | P0→P2 의 곡선 + 두 접선 |

## 5. 라인 스트립으로 곡선 그리기

곡선을 *N 개 샘플의 polyline* 으로 근사.

<!-- snippet: src/scenes/interpolation_scene.cpp@curve-linestrip -->
```cpp
const int N  = sampleCount_;
Vector2 prev = SampleCurve(0.0f);
for (int i = 1; i <= N; ++i) {
    float u   = (float)i / (float)N;
    Vector2 p = SampleCurve(u);
    DrawLineEx(prev, p, 2.0f, Color{180, 220, 255, 255});
    prev = p;
}
```

`sampleCount_` 슬라이더 (8~256) 로 *해상도 직접 조정*. 8 정도면 *각진* 곡선이 보이고, 256 면 *매끄럽다*. *어디서부터 사용자 눈에 매끄러워 보이는가* 를 직접 측정.

추가 시각: 곡선 종류에 따라 *control polygon 또는 접선* 을 회색으로 그려 *4 점이 어떻게 곡선을 만드는가* 를 보여준다.

<!-- snippet: src/scenes/interpolation_scene.cpp@curve-guides -->
```cpp
if (curve_ == Curve::Hermite) {
    Vector2 a  = p0_.Position();
    Vector2 c  = p2_.Position();
    Vector2 m0 = V2Add(a, V2Sub(p1_.Position(), a));   // = p1_
    Vector2 m1 = V2Add(c, V2Sub(p3_.Position(), c));
    DrawLineEx(a, m0, 1.0f, Color{120, 220, 120, 200});   // 시작 접선
    DrawLineEx(c, m1, 1.0f, Color{120, 220, 120, 200});   // 끝 접선
} else if (curve_ == Curve::Bezier) {
    // Control polygon
    DrawLineEx(p0_.Position(), p1_.Position(), 1.0f, Color{120, 120, 120, 180});
    DrawLineEx(p1_.Position(), p2_.Position(), 1.0f, Color{120, 120, 120, 180});
    DrawLineEx(p2_.Position(), p3_.Position(), 1.0f, Color{120, 120, 120, 180});
}
```

## 6. Arc-Length Parameterization

math part07 의 핵심 통찰: *t 등속이 곡선 위 등속이 아님*. control point 사이가 멀면 *t 가 같은 변화 폭이라도 진짜 거리 변화는 다르다*. 카메라 트랙에서 *카메라가 일정 속도로 움직이려면* *호 길이 등속* 으로 매개화해야 한다.

`ArcLengthLUT` 가 그걸 한다. 곡선을 N 샘플로 분할 후 *누적 거리 LUT*.

<!-- snippet: src/algo/curves.h#ArcLengthLUT -->
```cpp
class ArcLengthLUT {
public:
    // ... Reset() ...
    template <class F>
    void Build(int N, F&& sampler) {
        // ... cumLen_[i] = 0 ~ i 누적 거리 ...
    }

    float TotalLength() const { return totalLen_; }

    // s ∈ [0, 1] 거리 비율 → 곡선 위 (대략) 점.
    template <class F>
    Vector2 Sample(float s, F&& sampler) const {
        if (cumLen_.empty()) return {0, 0};
        if (samples_.size() == 1 || totalLen_ <= 1e-8f) {
            return samples_.front();
        }
        if (s <= 0) return samples_.front();
        if (s >= 1) return samples_.back();
        float target = s * totalLen_;
        // binary search 로 cumLen_ 에서 target 직전 인덱스
        int lo = 0, hi = (int)cumLen_.size() - 1;
        while (lo + 1 < hi) {
            int mid = (lo + hi) / 2;
            if (cumLen_[mid] <= target) lo = mid;
            else                          hi = mid;
        }
        float seg     = cumLen_[hi] - cumLen_[lo];
        float local   = (seg > 1e-8f) ? (target - cumLen_[lo]) / seg : 0.0f;
        float u       = ((float)lo + local) / (float)(samples_.size() - 1);
        return sampler(u);   // 정확한 점은 LUT 가 아니라 sampler 다시 호출
    }
    // ... samples_ / cumLen_ / totalLen_ 멤버 ...
};
```

설계 결정.

- **template + sampler 인자**: LUT 가 *어떤 곡선이든* 받을 수 있다. 함수 객체로 곡선을 참조.
- **이진 탐색**: cumLen_ 가 *단조 증가* (음의 길이 없음) 라 가능. $O(\log N)$.
- **결과 점은 sampler 재호출**: LUT 의 sample 자체가 아니라 *u 매개변수* 를 sampler 에 다시 줘 *정확한 점*. LUT 는 *u → s 의 매핑* 만 결정.
- **퇴화 입력 가드가 `Sample` 맨 앞**: `Build(0, ...)` 이나 길이 0 인 곡선에서 `samples_.front()` 가 빈 벡터를 건드리지 않도록 두 줄이 앞에 선다.

토글이 켜져 있으면 같은 t 값에 *다른 곡선 위 위치*. 사용자가 t 슬라이더를 천천히 움직이면 *움직이는 점이 더 균일하게 진행* 하는 것을 본다 — 매개변수 t 등속에서는 *곡선 안쪽 (control point 가 멀리 떨어진 곳)* 에서 빨리 움직였다가 *바깥쪽에서 느려지는* 효과가 뚜렷.

## 7. 패널과 시각 디테일

패널의 위젯들 — `GuiToggleGroup` (3 곡선), `GuiSlider` (t, 샘플 수), `GuiCheckBox` (autoPlay, useArcLen).

<!-- snippet: src/scenes/interpolation_scene.cpp#InterpolationScene::DrawPanel -->
```cpp
int curveIdx = (int)curve_;
GuiToggleGroup(Rectangle{(float)(px + 8), (float)y, 80, 22},
                "Bezier;Catmull;Hermite", &curveIdx);
curve_ = (Curve)curveIdx;
// ... t 슬라이더 ...
GuiCheckBox(Rectangle{(float)(px + 8), (float)y, 16, 16},
             " auto play", &autoPlay_);
y += 22;
GuiCheckBox(Rectangle{(float)(px + 8), (float)y, 16, 16},
             " arc-length param", &useArcLen_);
```

위젯 사각형은 패널 좌상단 `(px, py)` 에서 `y` 를 내려가며 직접 계산한다 — raygui 는 레이아웃을 갖고 있지 않다.

라이브 정보 표시 — *곡선의 총 호 길이*. arc-length 모드에서만 표시.

<!-- snippet: src/scenes/interpolation_scene.cpp@arc-length-label -->
```cpp
if (useArcLen_) {
    ArcLengthLUT lut;
    lut.Build(sampleCount_, [this](float u) { return SampleCurve(u); });
    std::snprintf(buf, sizeof(buf), "arc length: %.1f px", lut.TotalLength());
    GuiLabel(Rectangle{(float)(px + 8), (float)y, 220, 18}, buf);
    y += 18;
}
```

매 프레임 LUT 재빌드 — 비용 적음 (64 샘플 기준 한 프레임당 64 sqrt). *control point 를 끌면 호 길이가 즉시 갱신*.

색상 컨벤션 — 4 핸들이 *서로 다른 색* (빨강·주황·보라·파랑). 사용자가 어느 것이 어떤 매개변수인지 *색만으로 인식* 가능.

## 8. 빌드와 실행 결과

```bash
cmake --build build --config Release
./build/Release/math_lab.exe
```

탭이 *Welcome / Rotation / Interpolation* 셋. Interpolation 탭에서:

- *Bezier 토글*: 4 핸들이 *control polygon* 으로 이어진 회색 선 + 그 안쪽으로 휘는 곡선.
- *Catmull-Rom 토글*: 곡선이 *P1·P2 를 정확히 통과*. P0 와 P3 가 *외부 관점* 으로 동작 — 끌면 P1·P2 근처 곡선의 *접선 방향* 이 바뀜.
- *Hermite 토글*: P0→P2 의 곡선 + 두 초록 접선 (P0→P1, P2→P3).
- *arc-length 토글*: 노란 점이 *균일한 속도* 로 곡선 위를 진행. 토글 끄면 t 등속 — 한쪽에서 빨라지고 한쪽에서 느려짐.

빌드: MSVC `/W4`, 새 Scene 추가 후도 *경고 0 (third-party 외)* + `math_lab.exe` 정상.

이 step 이 `tests/core_tests.cpp` 에 추가한 것은 `curves/basis_and_arc_length` 다. 세 곡선은 *끝점* 이 아니라 *중간 기저* 로 고정한다 — t=0·t=1 만 보면 중간 기저가 0 이 되어 계수를 아무렇게나 바꿔도 통과하기 때문이다. `CubicBezier(0.5)` 는 해석해 `(P0 + 3P1 + 3P2 + P3)/8`, `CubicHermite(0.5)` 는 `h00=0.5, h10=0.125, h01=0.5, h11=-0.125`, `CatmullRom(0.5)` 는 접선 계수 0.5 를 넣은 Hermite 와 같은 값이어야 한다.

arc-length 쪽은 화면의 노란 점이 보여주던 것 — *등속 재매개화* — 을 그대로 잰다. `x = 100t²` 이라는 가속 곡선을 s 로 20 등분해 구간 이동 거리의 max/min 을 본다.

<!-- snippet: tests/core_tests.cpp@step03-arc-length-uniform -->
```cpp
CHECK(maxSeg / minSeg < 1.1f);
// 대조군: 같은 곡선을 t 로 균일 샘플하면 전혀 균일하지 않다.
// ...
CHECK(rawMax / rawMin > 5.0f);
```

대조군이 붙어 있는 이유는 step02 의 Slerp 판별과 같다. 상한만 있으면 `Sample` 이 *입력을 그대로 돌려주는* 구현이어도 곡선이 충분히 완만하면 통과할 수 있다. 같은 곡선을 t 로 샘플했을 때 비가 5 를 넘는다는 하한이 있어야 "LUT 가 실제로 뭔가를 바꿨다" 가 된다.

```text
$ ctest --test-dir build -C Release -R math_step03_interpolation --output-on-failure
    Start 3: math_step03_interpolation
1/1 Test #3: math_step03_interpolation ........   Passed    0.03 sec

100% tests passed out of 1
```

```text
$ ./build/Release/math_core_tests.exe step03
[step03] curves/basis_and_arc_length ..... 19 checks, 0 failed
step03: 1 case(s), 19 check(s), 0 failure(s)
```

---

## 결론

Step 3 에서 만든 것.

1. **`algo/curves.h`** — Lerp / CubicBezier / CubicHermite / CatmullRom + ArcLengthLUT (template sampler).
2. **`scenes/interpolation_scene.{h,cpp}`** — 곡선 토글, 4 드래그 핸들, t 슬라이더 + 자동 재생, arc-length 토글, 곡선 해상도 슬라이더.
3. **재사용 가능한 패턴들** — *toggle 으로 알고리즘 선택*, *드래그 핸들 N 개*, *autoPlay 진행* — 이후 Scene 들이 모두 활용.

math 시리즈 part07 (보간) + part08 (스플라인 일부) 가 *마우스에 반응하는 곡선* 으로 살아 있다. *Catmull-Rom 이 control point 를 통과하는 것*, *Hermite 의 접선이 곡선 모양을 어떻게 결정하는지*, *arc-length 가 t 와 다른 매개화임* 을 사용자가 *직접 본다*.

이 step 의 한계. 자동 검증이 고정하는 것은 *t=0, 0.5, 1 세 점의 값* 이라, 그 사이를 잇는 곡선의 모양 — 특히 화면에서 눈에 띄는 C1 연속성이나 control polygon 안쪽으로 휘는 성질 (convex hull) — 은 여전히 눈으로 보는 증거뿐이다. `ArcLengthLUT` 도 *단조 증가하는 1 차원 곡선* 두 개로만 검증한다. 되돌아오는 곡선이나 첨점에서 이분 탐색이 어느 구간을 고르는지는 시각 확인에 남아 있고, LUT 해상도를 바꿨을 때의 오차 곡선도 재지 않는다.

## 더 읽을거리

- Freya Holmér, ["The Beauty of Bézier Curves"](https://www.youtube.com/watch?v=aVwxzDHniEw) — 베지에·스플라인 시각화의 결정판.
- [The Continuity of Splines (Freya Holmér)](https://www.youtube.com/watch?v=jvPPXbo87ds) — C0/C1/C2 연속성과 Catmull-Rom·B-spline 의 관계.
- Edwin Catmull, Raphael Rom, "A Class of Local Interpolating Splines" (1974) — Catmull-Rom 원전.
