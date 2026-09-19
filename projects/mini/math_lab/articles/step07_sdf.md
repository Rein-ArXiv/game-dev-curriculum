## 들어가며

`math_lab` 의 일곱 번째 단계 — *SDF + Ray Marching Scene*. math 시리즈의 *part14 (Distance Fields / SDF)* 가 *마우스로 두 도형의 위치 / 크기를 끌면 거리 등고선이 즉시 갱신되고 광선이 sphere tracing 으로 표면까지 단계별로 도달하는* 시각화.

이 단계의 결과물.

- **두 도형의 SDF**: A = Circle, B = Box. 핸들 두 개로 위치, 슬라이더로 크기.
- **합성 모드 토글**: Union (`min`), Smooth Min (`smin` polynomial), Subtract (`max(a, -b)`). math part14 의 표준 boolean / smooth 합성.
- **smooth 강도 슬라이더 k**: 0 (날카로움) → 200 (부드러운 융합). Smin 모드에서만.
- **거리 등고선 + 안/밖 색상**: 화면 전체를 256×256 텍스처에 *부호 거리에 따른 grayscale + 안쪽 약간 청록빛 음영*. *등고선 띠* 30 px 마다.
- **Sphere Tracing 광선**: ro / rd 두 핸들 + 매 단계의 *안전 구* 시각. *광선이 표면까지 몇 단계로 도달하는지* 직접 보임.

핵심 통찰들 — *SDF 가 형상을 거리 함수로 표현*, *boolean 합성이 min/max 한 줄*, *smin 이 도형을 부드럽게 녹임*, *Sphere Tracing 의 매 단계 안전 구가 SDF 의 직관* — 모두 *마우스에 따라 즉시 갱신*.

---

## 목차
1. [2D SDF 함수들](#1-2d-sdf-함수들)
2. [Smooth Min 의 polynomial 형태](#2-smooth-min-의-polynomial-형태)
3. [텍스처에 SDF 그리기](#3-텍스처에-sdf-그리기)
4. [등고선 띠와 색상 컨벤션](#4-등고선-띠와-색상-컨벤션)
5. [Sphere Tracing 광선 단계](#5-sphere-tracing-광선-단계)
6. [매 단계 안전 구 시각화](#6-매-단계-안전-구-시각화)
7. [Dirty flag 와 캔버스 리사이즈](#7-dirty-flag-와-캔버스-리사이즈)
8. [빌드와 실행 결과](#8-빌드와-실행-결과)

---

## 1. 2D SDF 함수들

`algo/sdf2d.h` 의 도형들. math part14 의 식을 2D 로. 모두 `mathlab` 안의 `sdf`
네임스페이스에 들어간다.

<!-- snippet: src/algo/sdf2d.h@sdf-shapes -->
```cpp
inline float Length2(Vector2 a) { return std::sqrt(a.x * a.x + a.y * a.y); }

// 원 — center 가 원점, radius r.
inline float Circle(Vector2 p, Vector2 center, float r) {
    Vector2 d = {p.x - center.x, p.y - center.y};
    return Length2(d) - r;
}

// AABB — center + half size.
inline float Box(Vector2 p, Vector2 center, Vector2 half) {
    Vector2 q = {std::fabs(p.x - center.x) - half.x,
                  std::fabs(p.y - center.y) - half.y};
    float outside = Length2({std::max(q.x, 0.0f), std::max(q.y, 0.0f)});
    float inside  = std::min(std::max(q.x, q.y), 0.0f);
    return outside + inside;
}

// 캡슐 — 두 끝점 + 반지름.
inline float Capsule(Vector2 p, Vector2 a, Vector2 b, float r) {
    Vector2 ab = {b.x - a.x, b.y - a.y};
    Vector2 ap = {p.x - a.x, p.y - a.y};
    float L2  = ab.x * ab.x + ab.y * ab.y;
    float t   = (L2 < 1e-8f) ? 0.0f
                                : std::clamp((ap.x * ab.x + ap.y * ab.y) / L2,
                                              0.0f, 1.0f);
    Vector2 c = {a.x + ab.x * t, a.y + ab.y * t};
    return Length2({p.x - c.x, p.y - c.y}) - r;
}

// Boolean: union / intersect / subtract.
inline float OpUnion(float a, float b)     { return std::min(a, b); }
inline float OpIntersect(float a, float b) { return std::max(a, b); }
inline float OpSubtract(float a, float b)  { return std::max(a, -b); }
```

설계 결정.

- **`Length2` 를 한 곳에**: 세 도형이 모두 같은 길이 계산을 쓴다. 도형마다 `std::sqrt` 를 다시 쓰지 않는다.
- **Box 의 inside/outside 분리**: 단순한 `max` 나 `length(max(q, 0))` 만 쓰면 *내부에서 거리가 부정확*. *outside (양수 거리) + inside (음수 거리)* 를 분리해 *모든 영역에서 정확*. math part14 의 표준 형태.
- **Capsule 의 퇴화 가드**: a == b 면 *원* — `L2 < 1e-8` 로 폴백 (math part14 의 part17 review fix 와 동일 패턴).

## 2. Smooth Min 의 polynomial 형태

<!-- snippet: src/algo/sdf2d.h#SminPoly -->
```cpp
inline float SminPoly(float a, float b, float k) {
    if (k <= 1e-6f) return OpUnion(a, b);
    float h = std::max(k - std::fabs(a - b), 0.0f) / k;
    return std::min(a, b) - h * h * k * 0.25f;
}
```

math part14 review 에서 정정된 *h = max(k - |a-b|, 0) / k* 형태. 코드의 *세 곱 + 한 std::min* — 매우 빠름.

`k` 의 역할: *두 거리의 차이가 k 이내일 때만 융합*. 차이가 k 보다 크면 그냥 `min` (boolean 같음).

## 3. 텍스처에 SDF 그리기

매 픽셀의 SDF 값을 계산해 256×256 텍스처에 그린 후 캔버스 전체 크기로 stretch.

<!-- snippet: src/scenes/sdf_scene.cpp#SdfScene::RebuildTextureIfNeeded -->
```cpp
void SdfScene::RebuildTextureIfNeeded(int cx, int cy, int cw, int ch) {
    bool resize = (cx != lastCx_ || cy != lastCy_ ||
                    cw != lastCw_ || ch != lastCh_);
    if (!dirty_ && !resize) return;
    // ... 캔버스 좌표 기억 ...
    for (int j = 0; j < texH_; ++j) {
        float py = (float)cy + (((float)j + 0.5f) / (float)texH_) * (float)ch;
        for (int i = 0; i < texW_; ++i) {
            float px = (float)cx + (((float)i + 0.5f) / (float)texW_) * (float)cw;
            float d = SampleSDF({px, py});

            float t = std::clamp(d / 200.0f, -1.0f, 1.0f);
            float gray = 0.5f + 0.5f * t;     // 표면 (d=0) = 0.5
            // ... 등고선 띠 — §4 ...
            unsigned char gv = (unsigned char)(std::clamp(gray, 0.0f, 1.0f) * 255.0f);
            unsigned char rv = (d < 0) ? (unsigned char)(gv * 0.65f) : gv;
            unsigned char bv = (d < 0) ? gv : (unsigned char)(gv * 0.85f);
            pixels_[(size_t)(j * texW_ + i)] = Color{rv, gv, bv, 255};
        }
    }
    UpdateTexture(tex_, pixels_.data());
    // ... 마지막 입력 값 기억 + dirty_ = false ...
}
```

매핑 핵심: *부호 거리를 [-200, 200] → [0, 1]* — 표면이 *gray 0.5*, 안쪽 검고 바깥쪽 밝게. 사용자가 *어느 쪽이 형상 안* 인지 즉시 인식.

`SampleSDF` 가 *현재 op 토글에 따라 boolean / smin 적용*.

<!-- snippet: src/scenes/sdf_scene.cpp#SdfScene::SampleSDF -->
```cpp
float SdfScene::SampleSDF(Vector2 p) const {
    float a = sdf::Circle(p, centerA_.Position(), sizeA_);
    float b = sdf::Box(p, centerB_.Position(), {boxBHalfX_, boxBHalfY_});
    switch (op_) {
        case Op::Union:    return sdf::OpUnion(a, b);
        case Op::Smin:     return sdf::SminPoly(a, b, smoothK_);
        case Op::Subtract: return sdf::OpSubtract(a, b);
    }
    return a;
}
```

## 4. 등고선 띠와 색상 컨벤션

거리 함수의 *등고선* 을 띠로 표시.

<!-- snippet: src/scenes/sdf_scene.cpp@contour-band -->
```cpp
if (showContours_) {
    float band = std::fmod(std::fabs(d) / 30.0f, 1.0f);
    if (band > 0.92f || band < 0.08f) gray *= 0.65f;
}
```

`band` 가 0 또는 1 근처면 *그 점이 등고선 (multiple of 30 px) 위*. 어둡게 음영. *동심원 / 동심 사각* 패턴이 자연스럽게 — *SDF 의 진짜 구조* 가 보이는 시각.

색상.

| 영역 | 색 |
|---|---|
| 안쪽 (d < 0) | 약간 청록빛 어두운 회색 (R 만 0.65 배) |
| 표면 (d ≈ 0) | 중간 회색 |
| 바깥 (d > 0) | 약간 누런 밝은 회색 (B 만 0.85 배) |
| 등고선 띠 | 어둡게 |

이 컨벤션이 *SDF 의 안/밖 + 거리 분포* 둘 다 한 시각에 보여준다.

## 5. Sphere Tracing 광선 단계

광선 ro → rd 를 *매 단계 SDF 만큼 안전하게 전진*.

<!-- snippet: src/scenes/sdf_scene.cpp@sphere-trace -->
```cpp
Vector2 ro = rayStart_.Position();
Vector2 rd = {rayEnd_.Position().x - ro.x, rayEnd_.Position().y - ro.y};
float L = std::sqrt(rd.x * rd.x + rd.y * rd.y);
if (L > 1e-6f) {                       // ro 와 rd 가 겹치면 방향이 없다
    Vector2 dn = {rd.x / L, rd.y / L};
    Vector2 cur = ro;
    const int   maxIter = 64;
    const float epsilon = 0.5f;
    const float maxT    = 2000.0f;
    float t = 0.0f;
    int   iter = 0;
    bool  hit  = false;
    for (; iter < maxIter; ++iter) {
        float d = SampleSDF(cur);
        DrawCircleLines((int)cur.x, (int)cur.y,
                          std::max(d, 0.0f),
                          Color{200, 230, 255, 70});
        if (d < epsilon) { hit = true; break; }
        if (t > maxT)    break;       // 표면 못 만남 — miss
        t   += d;
        cur  = {ro.x + dn.x * t, ro.y + dn.y * t};
    }
    // ... 광선 선분 · hit 점 · 상태 텍스트 그리기 ...
}
```

각 반복: *현재 위치의 SDF 평가 + 그 만큼 전진*. epsilon 이하면 표면 도달 (`hit`). maxIter / maxT 가 안전망.

매 step 의 *t* 가 누적 진행 거리. 화면에 *step 수 + t + hit 여부* 표시.

<!-- snippet: src/scenes/sdf_scene.cpp@ray-status -->
```cpp
char buf[64];
std::snprintf(buf, sizeof(buf),
               "ray steps: %d   t = %.0f   %s",
               iter, t, hit ? "HIT" : "miss");
DrawText(buf, cx + 10, cy + 30, 14, Color{220, 220, 220, 255});
```

## 6. 매 단계 안전 구 시각화

Sphere Tracing 의 핵심 직관 — *반지름 d 의 빈 구 안에서 안전한 진행*. 매 단계 그 *안전 구* 를 외곽선으로.

<!-- snippet: src/scenes/sdf_scene.cpp@safe-circle -->
```cpp
DrawCircleLines((int)cur.x, (int)cur.y,
                  std::max(d, 0.0f),
                  Color{200, 230, 255, 70});
```

광선 따라가며 *연속된 작은 구들* 이 보인다 — *표면 가까이 갈수록 구가 작아짐*. 사용자가 *왜 sphere tracing 인가* 를 직접 본다 (*매 step 안전 거리만큼 진행* 의 직관).

표면 도달 시 *빨간 hit 점*. 도달 못 하면 끝점 어딘가에서 멈춤.

## 7. Dirty flag 와 캔버스 리사이즈

256×256 = 65,536 픽셀에 SDF 평가 + boolean / smin = 한 프레임에 약 13만 함수 호출. 매 프레임 무조건 재계산이면 비용. *입력 변경 검출 시에만* 재계산.

<!-- snippet: src/scenes/sdf_scene.cpp#SdfScene::Update -->
```cpp
void SdfScene::Update(float) {
    centerA_.Update();
    centerB_.Update();
    rayStart_.Update();
    rayEnd_.Update();

    Vector2 a = centerA_.Position();
    Vector2 b = centerB_.Position();
    if (a.x != lastA_.x || a.y != lastA_.y || b.x != lastB_.x || b.y != lastB_.y ||
        op_ != lastOp_ || smoothK_ != lastK_ ||
        sizeA_ != lastSizeA_ ||
        boxBHalfX_ != lastBHX_ || boxBHalfY_ != lastBHY_ ||
        showContours_ != lastShowContours_) {
        dirty_ = true;
    }
}
```

광선 핸들은 *재계산 트리거 안 함* — Sphere Tracing 은 매 프레임 직접 평가 (광선 하나당 64 호출, 무시 가능).

캔버스 크기가 변하면 (탭 전환 직후 등) 자동으로 *resize 트리거*.

## 8. 빌드와 실행 결과

```bash
cmake --build build --config Release
./build/Release/math_lab.exe
```

탭에 *SDF / RayMarch* 추가. SDF 탭에서:

- *Smin 토글 + k 슬라이더*: 두 도형이 *부드럽게 녹아드는* 모습. k = 0 이면 boolean min, k = 200 이면 *blob* 처럼.
- *Union 토글*: 단순 boolean — 모서리가 날카로움.
- *Subtract 토글*: B 도형이 A 에서 *깎인* 모양.
- *contour 띠*: 형상 주변의 *동심원 / 동심 박스* 무늬가 SDF 의 *진짜 거리* 를 보여줌.
- *Sphere Tracing 광선*: ro → rd 를 끌면 *안전 구가 연속해서 그려짐*. 광선이 표면 가까이 갈수록 *구가 작아지고 step 수가 늘어남*. 표면에 닿으면 빨간 hit.

빌드: MSVC `/W4`, 새 Scene 추가 후도 *경고 0 (third-party 외)* + `math_lab.exe` 정상.

이 step 이 `tests/core_tests.cpp` 에 추가한 것은 `sdf/primitives_and_smin` 이다. 도형 쪽은 *퇴화 입력이 알고리즘을 우회한다* 는 함정을 피하는 데 초점이 있다 — `Box` 는 바깥뿐 아니라 내부 (`inside` 항이 없으면 0 이 된다) 와 모서리 바깥의 유클리드 거리를, `Capsule` 은 두 끝점이 같은 원이 아니라 선분 투영이 `[0,1]` 안팎으로 나가는 네 위치를 본다.

주제인 smooth min 은 `k` 를 실제로 켜서 잰다. `k=0` 은 `k <= 1e-6` 조기 반환 분기라 blend 식이 한 번도 실행되지 않기 때문이다. 다항식 형태의 값을 손으로 계산해 못 박는다 — `|a-b| < k` 구간에서는 `min` 보다 *작고*, `|a-b| > k` 구간에서는 `min` 과 *같아야* 한다.

<!-- snippet: tests/core_tests.cpp@step07-smin-blend-band -->
```cpp
CHECK(mathlab::sdf::SminPoly(1.0f, 1.2f, 1.0f) < 1.0f);
CHECK(Near(mathlab::sdf::SminPoly(1.0f, 1.2f, 1.0f), 1.0f - 0.16f, 1e-5f));
CHECK(Near(mathlab::sdf::SminPoly(1.0f, 5.0f, 1.0f), 1.0f, 1e-6f));
CHECK(Near(mathlab::sdf::SminPoly(2.0f, 2.0f, 1.0f), 2.0f - 0.25f, 1e-5f));
// k → 0 이면 min 으로 수렴한다.
CHECK(Near(mathlab::sdf::SminPoly(1.0f, 1.2f, 1e-3f), 1.0f, 1e-3f));
```

`a == b` 인 네 번째 줄이 최대 융합 지점 (`min - k/4`) 이다. 여기에 무작위 500 표본으로 대칭성 (`smin(a,b) == smin(b,a)`) 과 `min - k/4 ≤ smin ≤ min` 이라는 상하한을 걸어, 계수 `0.25` 를 바꾸면 어느 쪽으로 바꾸든 걸리게 해 뒀다.

```text
$ ctest --test-dir build -C Release -R math_step07_sdf --output-on-failure
    Start 7: math_step07_sdf
1/1 Test #7: math_step07_sdf ..................   Passed    0.04 sec

100% tests passed out of 1
```

```text
$ ./build/Release/math_core_tests.exe step07
[step07] sdf/primitives_and_smin ......... 1523 checks, 0 failed
step07: 1 case(s), 1523 check(s), 0 failure(s)
```

1,523 개 중 1,500 개가 무작위 표본 500 개 × (대칭성·상한·하한) 이다.

---

## 결론

Step 7 에서 만든 것.

1. **`algo/sdf2d.h`** — Circle / Box / Capsule SDF + OpUnion / OpIntersect / OpSubtract / SminPoly. math part14 의 표준 형태 그대로.
2. **`scenes/sdf_scene.{h,cpp}`** — 두 도형 + boolean / smin 토글, 256×256 SDF 텍스처, 등고선 띠, Sphere Tracing 광선 + 안전 구 시각.
3. **재사용 패턴들** — *부호 거리 → 그레이스케일 매핑*, *등고선 띠 generation*, *Sphere Tracing trace* — SDF 응용의 표준.

math 시리즈 part14 (Distance Fields) 가 *마우스로 두 도형을 끌면 거리 등고선과 sphere tracing 단계가 즉시 갱신되는* 형태로 살아 있다. *smin 의 부드러운 융합*, *boolean min 의 날카로움*, *Sphere Tracing 의 매 단계 안전 구가 표면 가까이서 작아지는 모습* 을 사용자가 *직접 본다*.

이 step 의 한계. Scene 이 실제로 합성하는 도형은 *Circle 과 Box 둘* 이다 — `Capsule` 은 헤더에 있지만 `SampleSDF` 가 쓰지 않고, `OpIntersect` 도 토글에 노출되지 않는다. SDF 텍스처 해상도는 `texW_ = 256` 고정이라 *해상도별 평가 비용* 을 비교할 수 없다. 자동 검증도 *거리 함수의 값* 까지이고, 이 Scene 이 실제로 화면에 그리는 것 — 등고선 띠의 간격, Sphere Tracing 이 표면 근처에서 몇 단계를 쓰는지, 안전 구가 겹치지 않고 이어지는지 — 은 검사에 없다. 거리장이 *Lipschitz 조건* (기울기 ≤ 1) 을 지키는지도 재지 않아, 그것이 깨져 sphere tracing 이 표면을 뚫는 경우는 눈으로만 잡힌다.

## 더 읽을거리

- Inigo Quilez, [2D distance functions](https://iquilezles.org/articles/distfunctions2d/) 및 [smooth minimum](https://iquilezles.org/articles/smin/) — 이 Scene 수식들의 원전.
- John C. Hart, "Sphere Tracing: A Geometric Method for the Antialiased Ray Tracing of Implicit Surfaces" (1996) — sphere tracing 원전.
- [Shadertoy](https://www.shadertoy.com/) — SDF 레이마칭 실험의 표준 놀이터.
