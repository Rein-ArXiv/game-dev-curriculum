## 들어가며

`math_lab` 의 네 번째 단계 — *Noise Scene*. math 시리즈의 *part09 (Random / PRNG)* + *part10 (절차적 노이즈)* 가 *zoom·offset·seed 슬라이더로 즉시 갱신되는 256×256 heatmap* 으로 살아난다.

이 단계의 결과물.

- **3 종 노이즈 토글**: Value / Perlin (gradient) / Simplex. 같은 (x, y) 에서 *세 알고리즘이 만드는 패턴 차이* 를 즉시 비교.
- **단일 옥타브 vs FBM 토글**: FBM 켜면 *옥타브 슬라이더 (1~8) + lacunarity + gain*. *fractal 디테일* 의 효과가 매개변수에 따라 어떻게 변하는지 직접.
- **스케일 (zoom) / offset / seed 슬라이더**: 노이즈 *공간을 스크롤·확대*. 같은 패턴의 *다른 영역* 을 본다.
- **256×256 heatmap 텍스처**: 매 프레임 *변경된 매개변수가 있을 때만* 재계산 (dirty flag). 60fps 안전.

핵심 통찰들 — *Value 노이즈가 격자 정점 값 보간이라 사각형 격자감이 보이는 것*, *Perlin 의 gradient 가 격자감을 줄이지만 axis-aligned bias 가 남아 있는 것*, *Simplex 가 정삼각형 격자라 등방성 (isotropy) 이 더 좋은 것*, *FBM 옥타브가 실제 게임 텍스처 / 지형 노이즈의 표준* — 모두 *마우스 토글로 즉시 보임*.

---

## 목차
1. [노이즈 헤더의 구조](#1-노이즈-헤더의-구조)
2. [Hash 와 보간 함수](#2-hash-와-보간-함수)
3. [Value · Perlin · Simplex 구현](#3-value--perlin--simplex-구현)
4. [FBM 의 옥타브 누적](#4-fbm-의-옥타브-누적)
5. [Heatmap 텍스처 빌드](#5-heatmap-텍스처-빌드)
6. [Dirty flag 로 재계산 절감](#6-dirty-flag-로-재계산-절감)
7. [패널 슬라이더 / 토글](#7-패널-슬라이더--토글)
8. [빌드와 실행 결과](#8-빌드와-실행-결과)

---

## 1. 노이즈 헤더의 구조

`algo/noise.h` 의 namespace.

```cpp
namespace mathlab::nz {

uint32_t Hash2(int x, int y, uint32_t seed);
float    HashTo01(int x, int y, uint32_t seed);

float Smoothstep(float t);
float Quintic(float t);

float Value2D  (float x, float y, uint32_t seed);
float Perlin2D (float x, float y, uint32_t seed);
float Simplex2D(float x, float y, uint32_t seed);

template <class NoiseFn>
float FBM2D(float x, float y, uint32_t seed,
             int octaves, float lacunarity, float gain,
             NoiseFn&& fn);

}  // namespace mathlab::nz
```

설계 결정.

- **헤더 only**: 모두 inline. 한 cpp 에서 호출하니 분리 비용이 가치 없다.
- **`uint32_t` seed**: 정수형. Scene 에서 슬라이더로 직접 입력.
- **`NoiseFn` template**: FBM 이 *어떤 노이즈든* 옥타브로 누적 가능.

## 2. Hash 와 보간 함수

모든 격자 기반 노이즈가 *정점에 결정론적 값을 부여* 하는데, 그 *결정론적 값* 을 만드는 게 *2D hash*.

<!-- snippet: src/algo/noise.h@hash-2d -->
```cpp
inline uint32_t Hash2(int x, int y, uint32_t seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
    return HashU32(h);
}

// [0, 1) float 으로.
inline float HashTo01(int x, int y, uint32_t seed) {
    return (Hash2(x, y, seed) >> 8) * (1.0f / 16777216.0f);   // 24 bit
}
```

Wang hash 의 변형을 활용. 분포가 충분히 균일 — *비트 0 근처가 약간 약한* 알려진 함정이 있지만 게임 시각화엔 무관. 24-bit 변환은 part09 의 *uniform float 표준 형식*.

보간은 두 가지.

<!-- snippet: src/algo/noise.h@fade-curves -->
```cpp
inline float Smoothstep(float t) { return t * t * (3.0f - 2.0f * t); }
inline float Quintic(float t)    { return t * t * t * (t * (t * 6 - 15) + 10); }
```

- *Smoothstep* — 1차·2차 미분 연속 (C¹). Value 노이즈에 충분.
- *Quintic* — 1차·2차 미분 모두 연속 (C²). Perlin 의 *기울기 부드러움* 을 위해 필수. math part10 의 표준.

## 3. Value · Perlin · Simplex 구현

**Value Noise** — 격자 정점에 [0, 1) 랜덤 값 + 셀 안 보간.

<!-- snippet: src/algo/noise.h#Value2D -->
```cpp
inline float Value2D(float x, float y, uint32_t seed) {
    int   ix = (int)std::floor(x);
    int   iy = (int)std::floor(y);
    float fx = x - (float)ix;
    float fy = y - (float)iy;

    float v00 = HashTo01(ix,     iy,     seed);
    float v10 = HashTo01(ix + 1, iy,     seed);
    float v01 = HashTo01(ix,     iy + 1, seed);
    float v11 = HashTo01(ix + 1, iy + 1, seed);

    float u = Smoothstep(fx);
    float v = Smoothstep(fy);
    float a = v00 + (v10 - v00) * u;
    float b = v01 + (v11 - v01) * u;
    return a + (b - a) * v;   // ∈ [0, 1]
}
```

시각: *격자 정점에서 값이 결정* 되고 *셀 안에서 부드럽게 보간*. 사각 격자 패턴이 약간 보인다.

**Perlin Noise** — 격자 정점에 *그라디언트 벡터*. 4 방향 (+x, -x, +y, -y) 으로 단순화 (math part10 의 2D 버전).

<!-- snippet: src/algo/noise.h@perlin-2d -->
```cpp
inline void Grad2(int ix, int iy, uint32_t seed, float& gx, float& gy) {
    uint32_t h = Hash2(ix, iy, seed);
    switch (h & 3) {
        case 0:  gx = 1;  gy = 0;  break;
        case 1:  gx = -1; gy = 0;  break;
        case 2:  gx = 0;  gy = 1;  break;
        default: gx = 0;  gy = -1; break;
    }
}

inline float Perlin2D(float x, float y, uint32_t seed) {
    int   ix = (int)std::floor(x);
    int   iy = (int)std::floor(y);
    float fx = x - (float)ix;
    float fy = y - (float)iy;

    float gx, gy;
    Grad2(ix,     iy,     seed, gx, gy);  float n00 = gx * fx       + gy * fy;
    Grad2(ix + 1, iy,     seed, gx, gy);  float n10 = gx * (fx - 1) + gy * fy;
    Grad2(ix,     iy + 1, seed, gx, gy);  float n01 = gx * fx       + gy * (fy - 1);
    Grad2(ix + 1, iy + 1, seed, gx, gy);  float n11 = gx * (fx - 1) + gy * (fy - 1);

    float u = Quintic(fx);
    float v = Quintic(fy);
    float a = n00 + (n10 - n00) * u;
    float b = n01 + (n11 - n01) * u;
    return a + (b - a) * v;
}
```

시각: *격자 정점이 0* (각 정점에서 거리 0 이라 dot = 0) — 격자감이 *Value 보다 약함*. 하지만 axis-aligned 그라디언트라 *45° 사선 패턴* 이 약간 보인다.

**Simplex-like Noise** — 정삼각형 격자. 등방성이 좋다. (정확한 Ken Perlin 의 Simplex 가 아니라 *교육용 단순화 버전* — 정삼각형 격자 + radial fall-off 의 핵심 아이디어만 살림.)

<!-- snippet: src/algo/noise.h#Simplex2D -->
```cpp
inline float Simplex2D(float x, float y, uint32_t seed) {
    constexpr float F2 = 0.36602540378f;   // (sqrt(3) - 1) / 2
    constexpr float G2 = 0.21132486540f;   // (3 - sqrt(3)) / 6

    float s  = (x + y) * F2;
    int   i  = (int)std::floor(x + s);
    int   j  = (int)std::floor(y + s);
    float t0 = (i + j) * G2;
    float X0 = i - t0;
    float Y0 = j - t0;
    float x0 = x - X0;
    float y0 = y - Y0;

    int i1, j1;
    if (x0 > y0) { i1 = 1; j1 = 0; } else { i1 = 0; j1 = 1; }

    float x1 = x0 - i1 + G2;
    float y1 = y0 - j1 + G2;
    float x2 = x0 - 1.0f + 2.0f * G2;
    float y2 = y0 - 1.0f + 2.0f * G2;

    auto contrib = [&](float xi, float yi, int gi, int gj) {
        float t = 0.5f - xi * xi - yi * yi;
        if (t < 0) return 0.0f;
        t *= t;
        float gx, gy;
        Grad2(gi, gj, seed, gx, gy);
        return t * t * (gx * xi + gy * yi);
    };

    float n0 = contrib(x0, y0, i,      j     );
    float n1 = contrib(x1, y1, i + i1, j + j1);
    float n2 = contrib(x2, y2, i + 1,  j + 1 );
    return 70.0f * (n0 + n1 + n2);
}
```

`F2`, `G2` 가 *정사각 → 정삼각 격자 변환* 의 상수. *세 정점 contrib + radial fall-off (`0.5 - r²`)* 로 부드러운 패턴. 시각: *방향성 없는 등방 패턴* — Perlin 의 사선이 사라진다.

## 4. FBM 의 옥타브 누적

여러 옥타브를 *주파수 lacunarity 배 + 진폭 gain 배* 로 누적.

<!-- snippet: src/algo/noise.h@fbm-2d -->
```cpp
template <class NoiseFn>
inline float FBM2D(float x, float y, uint32_t seed,
                    int octaves, float lacunarity, float gain,
                    NoiseFn&& fn) {
    float sum  = 0.0f;
    float amp  = 1.0f;
    float freq = 1.0f;
    float norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        sum  += fn(x * freq, y * freq, seed + (uint32_t)o) * amp;
        norm += amp;
        amp  *= gain;
        freq *= lacunarity;
    }
    return (norm > 0.0f) ? sum / norm : 0.0f;
}
```

`norm` 으로 정규화 → 결과 범위 안정. seed 가 *옥타브마다 +1* — 같은 패턴이 여러 스케일에 정확히 겹치는 *bands* 효과 회피.

시각: 1 옥타브는 *한 스케일의 부드러운 패턴*. 8 옥타브는 *큰 윤곽 + 미세 디테일* 의 자연스러운 fractal — 게임의 지형·구름·돌 텍스처의 표준.

## 5. Heatmap 텍스처 빌드

매 프레임 256×256 픽셀에 노이즈 평가 + raylib `Texture2D` 에 업로드.

<!-- snippet: src/scenes/noise_scene.cpp#NoiseScene::OnEnter -->
```cpp
void NoiseScene::OnEnter() {
    pixels_.assign((size_t)(texW_ * texH_), Color{0, 0, 0, 255});
    Image img = GenImageColor(texW_, texH_, BLACK);
    tex_      = LoadTextureFromImage(img);
    UnloadImage(img);
    texLoaded_ = true;
    dirty_     = true;
}
```

재계산은 `dirty_` 가 켜져 있을 때만 돈다.

<!-- snippet: src/scenes/noise_scene.cpp#NoiseScene::RebuildIfNeeded -->
```cpp
void NoiseScene::RebuildIfNeeded() {
    if (!dirty_) return;
    // ... 캔버스 크기가 변했으면 pixels_ 재할당 ...
    auto sample = [&](float x, float y, uint32_t s) -> float {
        switch (kind_) {
            case Kind::Value:   return nz::Value2D(x, y, s) * 2.0f - 1.0f;
            case Kind::Perlin:  return nz::Perlin2D(x, y, s);
            case Kind::Simplex: return nz::Simplex2D(x, y, s);
        }
        return 0.0f;
    };

    for (int j = 0; j < texH_; ++j) {
        for (int i = 0; i < texW_; ++i) {
            float x = (i + offsetX_) * scale_;
            float y = (j + offsetY_) * scale_;
            float v;
            if (fbm_) {
                v = nz::FBM2D(x, y, (uint32_t)seed_,
                               octaves_, lacunarity_, gain_, sample);
            } else {
                v = sample(x, y, (uint32_t)seed_);
            }
            // [-1, 1] → [0, 255]
            float t = std::clamp(v * 0.5f + 0.5f, 0.0f, 1.0f);
            unsigned char gv = (unsigned char)(t * 255.0f);
            pixels_[(size_t)(j * texW_ + i)] = Color{gv, gv, gv, 255};
        }
    }
    UpdateTexture(tex_, pixels_.data());
    // ... last* 에 현재 매개변수 기록 + dirty_ = false ...
}
```

`Value2D` 는 *[0, 1]* 범위 — `* 2.0f - 1.0f` 로 *[-1, 1]* 로 매핑해 다른 둘과 같은 형식. 클램프 + 8-bit 양자화로 그레이스케일.

다만 *세 노이즈의 실제 출력 범위가 같지 않다*. seed=7 로 20 만 표본 + 격자 스캔을 돌려 재면 `Perlin2D` 는 약 *[-0.535, 0.535]*, `Simplex2D` 는 약 *[-0.684, 0.684]* 다 — 둘 다 교과서의 *[-1, 1]* 보다 좁다. 원인은 `Grad2` 가 축 정렬 단위 벡터 4 종만 쓰기 때문 (표준 구현은 길이 √2 급 그래디언트 12 종을 쓰고, `Simplex2D` 의 `70.0` 은 그 표준 구현에서 온 정규화 상수다). 그래서 위의 `v * 0.5 + 0.5` 매핑에서 *Value 는 0~255 를 전부 쓰고 Perlin 은 약 59~196, Simplex 는 약 40~215 만 쓴다* — 세 토글을 나란히 볼 때 *대비 차이* 가 먼저 눈에 들어온다는 점을 감안하고 봐야 한다.

## 6. Dirty flag 로 재계산 절감

256×256 = 65,536 픽셀 × FBM (예: 4 옥타브) = 한 프레임에 약 26만 호출. 매 프레임 무조건 재계산하면 *시각화의 핫 패스가 노이즈 계산*. 사용자가 *매개변수를 안 바꿀 때* 는 재계산 회피.

<!-- snippet: src/scenes/noise_scene.cpp -->
```cpp
void NoiseScene::Update(float) {
    // 입력 파라미터 변경 검출 — 패널이 갱신한 값과 last 값 비교.
    if (kind_ != lastKind_ || seed_ != lastSeed_ ||
        scale_ != lastScale_ ||
        offsetX_ != lastOffX_ || offsetY_ != lastOffY_ ||
        fbm_ != lastFbm_ || octaves_ != lastOct_ ||
        lacunarity_ != lastLac_ || gain_ != lastGain_) {
        dirty_ = true;
    }
}
// ...
void NoiseScene::DrawWorld(int cx, int cy, int cw, int ch) {
    RebuildIfNeeded();   // dirty 일 때만 실제 재계산
    // ... 텍스처 표시 ...
}
```

매개변수가 *안 변하면* 계산 0. 슬라이더를 끄는 동안만 비싼 계산.

## 7. 패널 슬라이더 / 토글

raygui 위젯의 *밀집* 한 패널.

<!-- snippet: src/scenes/noise_scene.cpp#NoiseScene::DrawPanel -->
```cpp
int kindIdx = (int)kind_;
GuiToggleGroup(Rectangle{(float)(px + 8), (float)y, 70, 22},
                "Value;Perlin;Simplex", &kindIdx);
kind_ = (Kind)kindIdx;
// ... scale (zoom) 라벨 ...
GuiSlider(Rectangle{(float)(px + 8), (float)(y + 18), 220, 16},
           nullptr, nullptr, &scale_, 0.005f, 0.30f);
// ... offset X / offset Y / seed 슬라이더 ...
GuiCheckBox(Rectangle{(float)(px + 8), (float)y, 16, 16},
             " FBM (multi-octave)", &fbm_);
y += 24;

if (fbm_) {
    float oct = (float)octaves_;
    // ... octaves / lacunarity / gain 슬라이더 ...
}
```

FBM 매개변수가 *FBM 켤 때만* 표시 — 패널 공간 효율.

## 8. 빌드와 실행 결과

```bash
cmake --build build --config Release
./build/Release/math_lab.exe
```

탭이 *Welcome / Rotation / Interpolation / Noise* 넷. Noise 탭에서:

- *Value 토글*: 사각 격자감이 약하게 보이는 *부드러운 grayscale 패턴*.
- *Perlin 토글*: 격자 정점이 0 인 *45° 사선 bias* 가 살짝.
- *Simplex 토글*: 등방성 강한 *원형 / 자연스러운 패턴*.
- *FBM 켜고 옥타브 8*: 큰 *대륙 같은 윤곽* + 미세 디테일.
- *zoom 슬라이더*: 같은 seed 의 같은 패턴 *확대 / 축소*.
- *offset 슬라이더*: 패턴 위에서 카메라 *팬*.
- *seed 슬라이더*: 완전히 다른 패턴.

빌드: MSVC `/W4`, 새 Scene 추가 후도 *경고 0 (third-party 외)* + `math_lab.exe` 정상.

이 step 이 `tests/core_tests.cpp` 에 추가한 것은 `noise/value_perlin_simplex_fbm` 이다. 결정성 (`Value2D` 가 같은 입력에 같은 값) 과 유한성은 출발점일 뿐이고, 실제로 잡고 싶은 것은 *세 노이즈가 서로 다른 방식으로 섞는다* 는 주장이다.

가장 날카로운 것은 Perlin 의 fade 다. 격자선 위 (`fy=0`) 에서는 y 보간이 사라져 x 방향 1 차원 보간만 남으므로, 가중치를 손으로 써서 값을 통째로 고정할 수 있다. `Value2D` 는 `Smoothstep`, `Perlin2D` 는 `Quintic` — 둘을 맞바꾸면 그 자리에서 깨진다.

<!-- snippet: tests/core_tests.cpp@step04-perlin-quintic-fade -->
```cpp
const float fx = 0.25f;
const float u = mathlab::nz::Quintic(fx);
int discriminating = 0;
for (int ix = 0; ix < 8; ++ix) {
    for (int iy = 0; iy < 8; ++iy) {
        // ...
        CHECK(Near(mathlab::nz::Perlin2D((float)ix + fx, (float)iy,
                                         seed),
                   n00 + (n10 - n00) * u, 1e-6f));
    }
}
// 가중치가 결과에 실제로 드러나는 셀이 있어야 위 CHECK 가 의미를 갖는다.
CHECK(discriminating > 4);
```

`discriminating` 이 대조군이다. 64 개 격자 셀 중 상당수에서 두 끝 기여가 *실제로 달라야* 가중치 `u` 가 결과에 드러난다. 두 끝이 우연히 비슷한 셀만 모여 있으면 `u` 를 아무 값으로 바꿔도 통과하기 때문이다.

나머지는 *상수화 방어* 다. 세 노이즈의 표본 분산 하한 (해시나 그라디언트를 상수로 바꾸면 0 이 된다), 격자 정점에서 Perlin 이 정확히 0, 그리고 FBM 이 *진폭 가중 평균* 이라는 것 — 상수 노이즈를 넣으면 옥타브·gain 이 무엇이든 그 상수를 돌려줘야 한다 (정규화를 빼면 1.875 가 나온다).

```text
$ ctest --test-dir build -C Release -R math_step04_noise --output-on-failure
    Start 4: math_step04_noise
1/1 Test #4: math_step04_noise ................   Passed    0.03 sec

100% tests passed out of 1
```

```text
$ ./build/Release/math_core_tests.exe step04
[step04] noise/value_perlin_simplex_fbm .. 91 checks, 0 failed
step04: 1 case(s), 91 check(s), 0 failure(s)
```

---

## 결론

Step 4 에서 만든 것.

1. **`algo/noise.h`** — Hash2 / HashTo01 + Smoothstep / Quintic + Value2D / Perlin2D / Simplex2D + FBM2D (template).
2. **`scenes/noise_scene.{h,cpp}`** — 3 노이즈 토글 + FBM + zoom/offset/seed 슬라이더 + Texture2D 기반 256×256 heatmap.
3. **재사용 패턴들** — *Texture 빌드 + UpdateTexture*, *dirty flag* 로 비싼 계산 절감 — 이후 SDF Scene 등에서 재활용.

math 시리즈 part09 (PRNG) + part10 (절차적 노이즈) 가 *마우스에 반응하는 256×256 heatmap* 으로 살아 있다. 사용자가 직접 *Value 의 격자감*, *Perlin 의 사선 bias*, *Simplex 의 등방성*, *FBM 의 fractal 디테일* 을 본다.

이 step 의 한계. `Grad2` 가 축 정렬 단위 벡터 4 종만 쓰기 때문에 `Perlin2D` 와 `Simplex2D` 의 실제 출력 범위가 각각 약 *±0.535* / *±0.684* 로 좁고, `Simplex2D` 의 `70.0` 은 그 코드에 맞지 않는 상수다. 그래서 세 노이즈를 나란히 비교할 때 *대비 차이* 가 알고리즘 차이보다 먼저 보인다. 자동 검증에도 빈 곳이 있다 — `Simplex2D` 는 유한성과 분산 하한까지만 잡혀 있어, skew/unskew 상수나 기여 반경을 바꿔도 값이 유한하고 변하기만 하면 통과한다. Perlin 처럼 *가중치를 손으로 쓴 기대값* 으로 못 박힌 것은 격자선 위 한 줄뿐이고, 화면의 등방성 차이 자체는 여전히 눈으로 보는 증거다.

## 더 읽을거리

- Ken Perlin, ["Improving Noise"](https://mrl.cs.nyu.edu/~perlin/paper445.pdf) (SIGGRAPH 2002) — quintic fade·개선판 Perlin 의 원전.
- Stefan Gustavson, ["Simplex noise demystified"](https://github.com/stegu/perlin-noise/blob/master/simplexnoise.pdf) — Simplex 의 가장 명료한 해설. (구 weber.itn.liu.se 호스팅은 소멸 — 저자 깃허브 사본)
- Inigo Quilez, [fBM 아티클](https://iquilezles.org/articles/fbm/) — 옥타브 합성의 실전 변형들.
