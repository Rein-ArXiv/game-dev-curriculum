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

```cpp
namespace mathlab {

inline Vector2 Lerp2(Vector2 a, Vector2 b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

inline Vector2 CubicBezier(Vector2 p0, Vector2 p1,
                            Vector2 p2, Vector2 p3, float t) {
    float u = 1.0f - t;
    float b0 = u*u*u, b1 = 3*u*u*t, b2 = 3*u*t*t, b3 = t*t*t;
    return { p0.x*b0 + p1.x*b1 + p2.x*b2 + p3.x*b3,
             p0.y*b0 + p1.y*b1 + p2.y*b2 + p3.y*b3 };
}

inline Vector2 CubicHermite(Vector2 p0, Vector2 m0,
                              Vector2 p1, Vector2 m1, float t) {
    float t2 = t*t, t3 = t2*t;
    float h00 =  2*t3 - 3*t2 + 1;
    float h10 =      t3 - 2*t2 + t;
    float h01 = -2*t3 + 3*t2;
    float h11 =      t3 -   t2;
    return { p0.x*h00 + m0.x*h10 + p1.x*h01 + m1.x*h11,
             p0.y*h00 + m0.y*h10 + p1.y*h01 + m1.y*h11 };
}

inline Vector2 CatmullRom(Vector2 p0, Vector2 p1,
                           Vector2 p2, Vector2 p3, float t) {
    Vector2 m1 = V2Mul(V2Sub(p2, p0), 0.5f);
    Vector2 m2 = V2Mul(V2Sub(p3, p1), 0.5f);
    return CubicHermite(p1, m1, p2, m2, t);
}

}  // namespace mathlab
```

함수 형태로 분리하면 *Scene 외부에서도 쉽게 재활용*. 이후 Spline editor 같은 다른 Scene 이 이걸 호출.

설계 결정.

- **`Vector2` 를 그대로** — raylib 자료구조. GLM 도입 안 함 (Step 2 의 quaternion 만 raymath 에 의존, 나머지는 raw).
- **Catmull-Rom 을 Hermite 로 환원**: Catmull-Rom 의 표준 정의가 *접선 = 0.5 × (P_{i+1} - P_{i-1})* 인 Hermite 곡선. 같은 함수를 두 번 작성하지 않는다.
- **`inline` 헤더 only**: cpp 파일 분리 안 함. 함수 전부 작아서 header-only 가 더 깔끔.

## 2. InterpolationScene 의 자료구조

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
};
```

상태가 *입력 + 토글 + 진행* 셋으로 명확. 마우스 입력 → 4 핸들 → 곡선 → 그리기.

## 3. 4 control point 드래그 핸들

Step 1 의 `DragHandle` 을 *그대로* 사용. 4 개를 멤버로.

```cpp
void InterpolationScene::Update(float dt) {
    p0_.Update(); p1_.Update(); p2_.Update(); p3_.Update();
    if (autoPlay_) {
        tParam_ += dt * 0.25f;
        if (tParam_ > 1.0f) tParam_ -= 1.0f;
    }
}
```

Step 1 에서 깐 *capture 정책* 덕분에 *4 핸들 중 하나만* 잡힌다. 핸들이 겹치는 케이스에도 안전.

처음 활성화될 때 캔버스 크기를 모르니 *지연 초기화*. 첫 `DrawWorld` 에서 적당한 위치에 배치.

```cpp
if (!initialized_) {
    p0_.SetPosition({cx + cw * 0.20f, cy + ch * 0.70f});
    p1_.SetPosition({cx + cw * 0.35f, cy + ch * 0.25f});
    p2_.SetPosition({cx + cw * 0.65f, cy + ch * 0.25f});
    p3_.SetPosition({cx + cw * 0.80f, cy + ch * 0.70f});
    initialized_ = true;
}
```

## 4. 세 곡선 평가 함수

토글에 따라 *같은 4 점에 다른 식* 을 적용.

```cpp
Vector2 InterpolationScene::SampleCurve(float t) const {
    Vector2 a = p0_.Position(), b = p1_.Position();
    Vector2 c = p2_.Position(), d = p3_.Position();
    switch (curve_) {
        case Curve::Bezier:     return CubicBezier(a, b, c, d, t);
        case Curve::CatmullRom: return CatmullRom(a, b, c, d, t);
        case Curve::Hermite: {
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

```cpp
if (curve_ == Curve::Bezier) {
    DrawLineEx(p0, p1, 1.0f, gray);
    DrawLineEx(p1, p2, 1.0f, gray);
    DrawLineEx(p2, p3, 1.0f, gray);
} else if (curve_ == Curve::Hermite) {
    DrawLineEx(p0, p1, 1.0f, green);   // 시작 접선
    DrawLineEx(p2, p3, 1.0f, green);   // 끝 접선
}
```

## 6. Arc-Length Parameterization

math part07 의 핵심 통찰: *t 등속이 곡선 위 등속이 아님*. control point 사이가 멀면 *t 가 같은 변화 폭이라도 진짜 거리 변화는 다르다*. 카메라 트랙에서 *카메라가 일정 속도로 움직이려면* *호 길이 등속* 으로 매개화해야 한다.

`ArcLengthLUT` 가 그걸 한다. 곡선을 N 샘플로 분할 후 *누적 거리 LUT*.

```cpp
class ArcLengthLUT {
public:
    template <class F>
    void Build(int N, F&& sampler) {
        // ... cumLen_[i] = 0 ~ i 누적 거리 ...
    }

    template <class F>
    Vector2 Sample(float s, F&& sampler) const {
        if (s <= 0) return samples_.front();
        if (s >= 1) return samples_.back();
        float target = s * totalLen_;
        // binary search 로 cumLen_ 에서 target 직전 인덱스
        int lo = 0, hi = (int)cumLen_.size() - 1;
        while (lo + 1 < hi) {
            int mid = (lo + hi) / 2;
            if (cumLen_[mid] <= target) lo = mid; else hi = mid;
        }
        float seg   = cumLen_[hi] - cumLen_[lo];
        float local = (seg > 1e-8f) ? (target - cumLen_[lo]) / seg : 0;
        float u     = ((float)lo + local) / (float)(samples_.size() - 1);
        return sampler(u);
    }
};
```

설계 결정.

- **template + sampler 인자**: LUT 가 *어떤 곡선이든* 받을 수 있다. 함수 객체로 곡선을 참조.
- **이진 탐색**: cumLen_ 가 *단조 증가* (음의 길이 없음) 라 가능. $O(\log N)$.
- **결과 점은 sampler 재호출**: LUT 의 sample 자체가 아니라 *u 매개변수* 를 sampler 에 다시 줘 *정확한 점*. LUT 는 *u → s 의 매핑* 만 결정.

토글이 켜져 있으면 같은 t 값에 *다른 곡선 위 위치*. 사용자가 t 슬라이더를 천천히 움직이면 *움직이는 점이 더 균일하게 진행* 하는 것을 본다 — 매개변수 t 등속에서는 *곡선 안쪽 (control point 가 멀리 떨어진 곳)* 에서 빨리 움직였다가 *바깥쪽에서 느려지는* 효과가 뚜렷.

## 7. 패널과 시각 디테일

패널의 위젯들 — `GuiToggleGroup` (3 곡선), `GuiSlider` (t, 샘플 수), `GuiCheckBox` (autoPlay, useArcLen).

```cpp
int curveIdx = (int)curve_;
GuiToggleGroup(toggleRect, "Bezier;Catmull;Hermite", &curveIdx);
curve_ = (Curve)curveIdx;

GuiCheckBox(checkRect1, " auto play", &autoPlay_);
GuiCheckBox(checkRect2, " arc-length param", &useArcLen_);
```

라이브 정보 표시 — *곡선의 총 호 길이*. arc-length 모드에서만 표시.

```cpp
if (useArcLen_) {
    ArcLengthLUT lut;
    lut.Build(sampleCount_, [this](float u) { return SampleCurve(u); });
    std::snprintf(buf, sizeof(buf), "arc length: %.1f px", lut.TotalLength());
    GuiLabel(rect, buf);
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

이 step 이 `tests/core_tests.cpp` 에 추가한 것은 `TestCurves()` — `CubicBezier` 가 t=0·t=1 에서 양 끝 control point 와 일치하는지, 길이 10 인 직선을 넣은 `ArcLengthLUT` 의 `TotalLength()` 가 10 이고 `Sample(0.25)` 가 x=2.5 인지, `Build(0, ...)` 같은 퇴화 입력에서 길이 0 + `Sample` 이 죽지 않는지.

```text
$ ctest --test-dir build -C Release --output-on-failure
    Start 1: math_core_tests
1/1 Test #1: math_core_tests ..................   Passed    0.04 sec
100% tests passed out of 1
```

---

## 결론

Step 3 에서 만든 것.

1. **`algo/curves.h`** — Lerp / CubicBezier / CubicHermite / CatmullRom + ArcLengthLUT (template sampler).
2. **`scenes/interpolation_scene.{h,cpp}`** — 곡선 토글, 4 드래그 핸들, t 슬라이더 + 자동 재생, arc-length 토글, 곡선 해상도 슬라이더.
3. **재사용 가능한 패턴들** — *toggle 으로 알고리즘 선택*, *드래그 핸들 N 개*, *autoPlay 진행* — 이후 Scene 들이 모두 활용.

math 시리즈 part07 (보간) + part08 (스플라인 일부) 가 *마우스에 반응하는 곡선* 으로 살아 있다. *Catmull-Rom 이 control point 를 통과하는 것*, *Hermite 의 접선이 곡선 모양을 어떻게 결정하는지*, *arc-length 가 t 와 다른 매개화임* 을 사용자가 *직접 본다*.

이 step 의 한계. `CubicHermite` 와 `CatmullRom` 은 화면에는 그려지지만 *테스트에서 한 번도 직접 호출되지 않는다* — 곡선 계수를 바꿔도 `ctest` 는 통과한다. `CubicBezier` 도 t=0·t=1 이라는 *중간 기저가 0 이 되는* 두 점에서만 확인되고, `ArcLengthLUT` 는 곡률이 0 인 직선으로만 검증한다. 즉 이 step 이 자동으로 지키는 것은 *끝점 통과와 퇴화 입력 안전성* 까지이고, 곡선 모양 자체는 눈으로 보는 증거만 있다.

## 더 읽을거리

- Freya Holmér, ["The Beauty of Bézier Curves"](https://www.youtube.com/watch?v=aVwxzDHniEw) — 베지에·스플라인 시각화의 결정판.
- [The Continuity of Splines (Freya Holmér)](https://www.youtube.com/watch?v=jvPPXbo87ds) — C0/C1/C2 연속성과 Catmull-Rom·B-spline 의 관계.
- Edwin Catmull, Raphael Rom, "A Class of Local Interpolating Splines" (1974) — Catmull-Rom 원전.
