## 들어가며

이 시리즈는 `mega_defense` — 협동 타워 디펜스 한 편 — 이 **어떻게 만들어졌는가**를 단계별로 기록한다. 각 편은 그 단계의 *시작 상태*와 *도달점*, 그 사이에서 고른 것과 **버린 것**, 그리고 그 선택이 지금 소스의 어느 줄로 남아 있는지를 앵커 달린 스니펫으로 보여 준다.

이 첫 편의 시작 상태는 아무것도 없음이다. 창도, 픽셀도, 그림도 없다. 도달점은 **480×480 창에 CPU 가 손으로 채운 프레임이 초당 60번 올라가는 상태**다. 그 사이에 세워야 할 것이 다섯이다.

1. `render::Framebuffer` — RGBA8 픽셀 배열 하나. 모든 그리기의 유일한 목적지.
2. `render::raster2d` — 사각형·원·선·삼각형. 게임을 그리는 데 실제로 필요한 최소 집합.
3. `render::Sprite` — 외부 PPM 파일 또는 내장 ASCII 마스크에서 만든 작은 비트맵.
4. `render::text` — 5×7 비트맵 글리프. HUD 한 줄을 찍기 위한 것.
5. `platform::Window` — 창을 만들고 위 픽셀 배열을 화면에 붙이는 유일한 계층.

이 편이 답하지 않는 것도 미리 적어 둔다. **무엇을** 그릴 것인가(게임 상태)는 여기 없다. 스프라이트가 그려지는 좌표는 아직 아무 데서도 오지 않고, `DefenseSkin` 은 파일을 읽어 스프라이트를 채울 뿐 누구도 그것을 배치하지 않는다. GPU 표시 경로가 존재한다는 사실도 여기서는 다루지 않는다 — 이 편의 관심은 *소프트웨어 래스터라이저를 세우는 순서*와 각 단계에서 **기각한 대안**이다.

---

## 목차
1. [왜 첫 계층이 CPU 프레임버퍼인가](#1-왜-첫-계층이-cpu-프레임버퍼인가)
2. [픽셀 하나: 패킹과 블렌딩](#2-픽셀-하나-패킹과-블렌딩)
3. [도형 넷과 베벨 하나](#3-도형-넷과-베벨-하나)
4. [스프라이트: PPM P3 로더와 마젠타 키](#4-스프라이트-ppm-p3-로더와-마젠타-키)
5. [폴백이 파일 단위가 아니라 스프라이트 단위인 이유](#5-폴백이-파일-단위가-아니라-스프라이트-단위인-이유)
6. [5×7 비트맵 폰트와 한글을 포기한 자리](#6-57-비트맵-폰트와-한글을-포기한-자리)
7. [창: 픽셀 배열을 화면에 붙이는 유일한 계층](#7-창-픽셀-배열을-화면에-붙이는-유일한-계층)
8. [480×480 은 어디서 왔는가](#8-480480-은-어디서-왔는가)
9. [실행과 관측](#9-실행과-관측)

---

## 1. 왜 첫 계층이 CPU 프레임버퍼인가

세 가지 시작점이 후보였다.

| 후보 | 첫 화면까지 | 검증 방법 | 기각 사유 |
|---|---|---|---|
| GPU(Vulkan)부터 | 인스턴스·장치·스왑체인·파이프라인·셰이더 | 화면을 눈으로 | 장치 없는 환경에서 **아무것도 못 돌린다** |
| 외부 2D 라이브러리 | 즉시 | 화면을 눈으로 | 그리기의 내부가 우리 것이 아니라 학습 가치가 사라진다 |
| CPU 프레임버퍼 | `std::vector<uint32_t>` 하나 | **픽셀을 단언** | (선택) |

결정적인 것은 세 번째 열이다. CPU 버퍼는 `fb.Get(5, 5)` 가 정확히 무슨 색인지 테스트가 물을 수 있다. GPU 백엔드에서 같은 질문을 하려면 장치·스왑체인·리드백이 전부 필요하고, 그것들이 없는 기계에서는 질문 자체가 불가능하다.

두 번째로 결정적인 것은 **선택 의존**이다. 이 저장소의 Vulkan 은 `find_package(Vulkan QUIET)` 로 찾고, 못 찾으면 표시 백엔드 헤더가 통째로 비활성이 된다. 그 환경에서도 제품은 빌드되고 돌아야 한다. 그러려면 *항상 있는* 표시 경로가 하나 필요하고, 그것이 소프트웨어 래스터라이저다.

세 번째는 이 편 다음에 올 모든 것에 영향을 준다. 프레임버퍼가 순수 메모리라면 **창 없이 게임 한 판을 통째로 그릴 수 있다**. 그 성질이 뒤에서 헤드리스 셀프테스트의 전제가 된다 — 그림이 그려졌는지를 "화면을 봤다"가 아니라 "배경색이 아닌 픽셀이 있다"로 판정하게 되는 것이 여기서 결정된다.

## 2. 픽셀 하나: 패킹과 블렌딩

픽셀은 `uint32_t` 하나로 `0xAABBGGRR` 순서다. 이 순서를 고른 이유는 뒤에서 창에 붙일 때 나온다(7절).

`Set` 은 덮어쓰고 `Blend` 는 알파 합성한다. 둘을 나눈 것은 성능 때문이다 — 맵 타일처럼 불투명한 대면적은 합성이 필요 없고, 스프라이트·HUD 오버레이만 합성이 필요하다.

<!-- snippet: include/render/framebuffer.hpp#Blend -->
```cpp
// 알파 블렌딩 set(src over dst).
void Blend(int x, int y, Color c) {
    if (x < 0 || x >= w_ || y < 0 || y >= h_) return;
    if (c.a == 255) { px_[(size_t)y * w_ + x] = Pack(c); return; }
    if (c.a == 0) return;
    Color d = Unpack(px_[(size_t)y * w_ + x]);
    float a = c.a / 255.0f, ia = 1.0f - a;
    Color o{(uint8_t)(c.r * a + d.r * ia), (uint8_t)(c.g * a + d.g * ia),
            (uint8_t)(c.b * a + d.b * ia), 255};
    px_[(size_t)y * w_ + x] = Pack(o);
}
```

세 줄이 설계다.

**경계 검사가 여기 있다.** `Blend` 는 화면 밖 좌표를 조용히 버린다. 이 한 줄 덕분에 위에 얹히는 모든 것 — 도형, 스프라이트, 글리프 — 이 클리핑 코드를 갖지 않는다. 대안은 각 그리기 함수가 자기 사각형을 화면과 교차시키는 것이었고, 그러면 클리핑 로직이 네 곳으로 복제된다. 픽셀 단위 검사는 느리지만 480×480 에서는 문제가 되지 않고, 무엇보다 **틀릴 수 없다**.

**두 특수 케이스를 먼저 친다.** `a == 255` 는 곱셈 없이 대입, `a == 0` 은 아무것도 하지 않는다. 스프라이트의 투명 픽셀이 압도적 다수라 이 두 분기가 실제 작업량을 결정한다.

**결과 알파는 255 로 고정한다.** 프레임버퍼는 화면이지 텍스처가 아니다. 누적 알파를 유지하면 "반투명한 화면"이라는 의미 없는 상태가 표현 가능해지고, 창에 붙일 때 어차피 버려진다.

`Get` 이 존재하는 이유는 오직 검증이다. 게임 코드는 프레임버퍼를 읽지 않는다 — 읽는 것은 테스트와, 마지막에 픽셀 배열을 통째로 가져가는 표시 계층뿐이다.

## 3. 도형 넷과 베벨 하나

`raster2d` 는 헤더 하나에 자유 함수 여섯이다. 클래스가 아닌 이유는 상태가 없기 때문이고, 첫 인자가 항상 `Framebuffer&` 인 이유는 **그리기 대상이 전역이 아니어야** 헤드리스에서 여러 버퍼를 동시에 굴릴 수 있기 때문이다.

채운 삼각형만 조금 특별하다. 사각형·원은 게임이 직접 쓰지만 삼각형은 지금 어떤 게임 코드도 부르지 않는다. 그런데도 둔 것은 삼각형이 **일반 폴리곤 채우기의 기본 단위**이고, 에지 함수 방식이 그대로 GPU 래스터라이저의 규칙이기 때문이다.

<!-- snippet: include/render/raster2d.hpp#FillTriangle -->
```cpp
inline void FillTriangle(Framebuffer& fb, int x0, int y0, int x1, int y1, int x2, int y2, Color c) {
    int minx = std::min({x0, x1, x2}), maxx = std::max({x0, x1, x2});
    int miny = std::min({y0, y1, y2}), maxy = std::max({y0, y1, y2});
    auto edge = [](int ax, int ay, int bx, int by, int px, int py) {
        return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
    };
    int area = edge(x0, y0, x1, y1, x2, y2);
    if (area == 0) return;
    for (int py = miny; py <= maxy; ++py)
        for (int px = minx; px <= maxx; ++px) {
            int w0 = edge(x1, y1, x2, y2, px, py);
            int w1 = edge(x2, y2, x0, y0, px, py);
            int w2 = edge(x0, y0, x1, y1, px, py);
            // 면적 부호에 맞춰 모두 같은 부호면 내부.
            bool inside = area > 0 ? (w0 >= 0 && w1 >= 0 && w2 >= 0)
                                   : (w0 <= 0 && w1 <= 0 && w2 <= 0);
            if (inside) fb.Blend(px, py, c);
        }
}
```

기각한 대안은 **스캔라인 채우기**였다. 스캔라인이 바운딩 박스 순회보다 빠르지만, 정점 정렬 · 에지 기울기 · 상단/하단 분할이 필요해 코드가 세 배가 된다. 더 중요한 것은 정확성이다 — 위 방식은 정수 산술만 쓰므로 **부동소수 오차로 인접 삼각형 사이에 틈이 생기는 문제가 원천적으로 없다**.

`area == 0` 을 먼저 거르는 것은 퇴화 삼각형(세 점이 한 직선)에서 0 으로 나누는 대신 그냥 아무것도 그리지 않기 위해서다. 부호 분기는 정점 순서(시계/반시계)에 무관하게 같은 결과를 내려는 것이다. 호출자에게 "정점을 반시계로 넘겨라"를 요구하면 그 규칙을 어긴 호출이 *조용히 아무것도 안 그리는* 형태로 실패하는데, 이 부류의 버그가 가장 찾기 어렵다.

블록이 화면에서 떠 보이게 하는 장치도 여기 있다.

<!-- snippet: include/render/raster2d.hpp#FillRectBevel -->
```cpp
inline void FillRectBevel(Framebuffer& fb, int x, int y, int w, int h, Color base, int d = 60) {
    if (w <= 0 || h <= 0) return;
    auto cl = [](int v) -> uint8_t { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); };
    Color lite{cl(base.r + d), cl(base.g + d), cl(base.b + d), base.a};
    Color dark{cl(base.r - d), cl(base.g - d), cl(base.b - d), base.a};
    FillRect(fb, x, y, w, h, base);
    for (int i = x; i < x + w; ++i) { fb.Blend(i, y, lite); fb.Blend(i, y + h - 1, dark); }
    for (int j = y; j < y + h; ++j) { fb.Blend(x, j, lite); fb.Blend(x + w - 1, j, dark); }
}
```

`cl` 람다가 `int` 로 계산한 뒤 클램프하는 것이 요점이다. `uint8_t` 산술로 `base.r + 60` 을 하면 240 짜리 색에서 **감싸 돌아 44 가 된다** — 밝게 만들려던 모서리가 갑자기 어두워진다. 정수로 올려 계산하고 마지막에 자르는 이 형태는 이 파일에서 색을 만지는 모든 곳의 규칙이다.

## 4. 스프라이트: PPM P3 로더와 마젠타 키

도형만으로는 타워와 적을 구분할 수 없다. 필요한 것은 작은 비트맵이고, 그것을 **파일로 교체 가능하게** 만들려면 포맷이 필요하다.

PNG 를 기각한 이유는 단 하나 — zlib 이 필요하다. 이 저장소는 서드파티 의존이 0 이고, 그 규칙을 스프라이트 하나 때문에 깨는 것은 비용이 크다. 남은 후보는 ASCII PPM(P3)과 바이너리 PPM(P6)이었다.

| | P3(ASCII) | P6(바이너리) |
|---|---|---|
| 파서 | 공백 구분 토큰 | 헤더 파싱 + `fread` |
| 파일 크기 | 크다(픽셀당 최대 12바이트) | 작다(픽셀당 3바이트) |
| 손으로 편집 | **가능** | 불가 |
| 결과 확인 | 텍스트 diff | 이미지 뷰어 필요 |

**읽기는 P3, 쓰기는 P6** 으로 갈랐다. 에셋은 손으로 만들고 git diff 로 읽을 수 있어야 하니 P3 이고(`assets/*.ppm` 는 16×16 이하라 크기가 문제되지 않는다), 프레임버퍼 덤프는 480×480 이라 텍스트로 쓰면 수 MB 가 되므로 P6 이다.

<!-- snippet: include/render/sprite.hpp#DecodePpmP3 -->
```cpp
inline bool DecodePpmP3(const std::string& text, Sprite& out) {
    std::istringstream input(text);
    std::string token;
    if (!ReadPpmToken(input, token) || token != "P3") return false;
    if (!ReadPpmToken(input, token)) return false;
    int width = 0, height = 0, maxValue = 0;
    try {
        width = std::stoi(token);
        // ... height · maxValue 도 같은 방식으로 읽는다
    } catch (...) {
        return false;
    }
    if (width <= 0 || height <= 0 || width > 256 || height > 256 || maxValue <= 0 || maxValue > 65535)
        return false;

    Sprite decoded;
    decoded.w = width;
    decoded.h = height;
    decoded.px.reserve(static_cast<size_t>(width * height));
    for (int i = 0; i < width * height; ++i) {
        int rgb[3]{};
        for (int channel = 0; channel < 3; ++channel) {
            if (!ReadPpmToken(input, token)) return false;
            // ... 토큰을 정수로 바꾸고 범위를 본다
            if (rgb[channel] < 0 || rgb[channel] > maxValue) return false;
            rgb[channel] = (rgb[channel] * 255 + maxValue / 2) / maxValue;
        }
        Color color{static_cast<uint8_t>(rgb[0]), static_cast<uint8_t>(rgb[1]),
                    static_cast<uint8_t>(rgb[2]), 255};
        if (color.r == 255 && color.g == 0 && color.b == 255) color.a = 0;
        decoded.px.push_back(Pack(color));
    }
    if (ReadPpmToken(input, token)) return false;
    out = std::move(decoded);
    return true;
}
```

이 함수는 **신뢰 경계 밖 입력을 읽는 첫 코드**다. 에셋 디렉터리는 사용자가 교체하라고 광고된 곳이므로, 그 안의 바이트는 우리가 고른 것이 아니다. 그래서 다섯 가지 방어가 들어 있다.

1. **크기 상한 256×256.** 헤더의 폭·높이는 파일이 주장하는 숫자일 뿐이다. 상한이 없으면 `65535 65535` 한 줄이 16GB 예약을 시도한다. 상한이 있으므로 그 뒤로는 전체를 메모리에 올려 파싱해도 안전하고, 그래서 파서를 스트리밍으로 짤 필요가 없어졌다.
2. **`maxValue` 범위 검사와 반올림 스케일.** `(v * 255 + maxValue / 2) / maxValue` 는 `maxValue` 가 255 가 아닌 파일(흔히 15, 1)도 올바르게 8비트로 옮긴다. `+ maxValue / 2` 가 없으면 항상 내림이 되어 밝은 색이 한 단계씩 어두워진다.
3. **채널 값이 `maxValue` 를 넘으면 거부.** 넘는 값을 클램프하지 않고 파일 전체를 거부하는 쪽을 골랐다. 그런 파일은 손상됐거나 다른 포맷이고, 조용히 고쳐 읽으면 사용자는 자기 파일이 정상인 줄 안다.
4. **끝에 남은 토큰이 있으면 거부.** `if (ReadPpmToken(input, token)) return false;` 다. 선언한 픽셀 수를 다 읽은 뒤에도 데이터가 남아 있다면 폭·높이가 실제 내용과 다르다는 뜻이다.
5. **디스크 접근이 `io::ReadTextFile` 을 지난다.** `<fstream>` 을 여기서 직접 열지 않는다. 파일을 여는 코드가 렌더·게임·툴로 흩어지면 그 지점을 교체할 수도, 계층 규칙으로 강제할 수도 없어진다.

투명도는 **순수 마젠타(255,0,255)를 키**로 쓴다. P3 에는 알파 채널이 없고, 알파를 담는 확장 포맷을 정의하면 그 순간 표준 뷰어로 열 수 없는 파일이 된다. 마젠타는 자연 이미지에 거의 나타나지 않아 관례적으로 쓰이는 키 색이다.

파일이 없을 때 쓸 그림은 코드 안에서 만든다. `MakeSprite` 는 ASCII 아트 행과 팔레트를 받아 스프라이트를 만들고, `Tint` 는 마스크 하나의 불투명 픽셀만 새 색으로 칠한 사본을 낸다.

<!-- snippet: include/render/sprite.hpp#Tint -->
```cpp
inline Sprite Tint(const Sprite& mask, Color c) {
    Sprite s = mask;
    for (auto& p : s.px) {
        Color o = Unpack(p);
        if (o.a) p = Pack({c.r, c.g, c.b, o.a});
    }
    return s;
}
```

`if (o.a)` 가 알파를 보존하므로 투명 픽셀은 투명한 채로 남는다. 이 한 줄 덕분에 7×7 실루엣 마스크 **하나**로 타워 4종과 적 5종의 외형을 전부 만든다 — 색만 다르다.

## 5. 폴백이 파일 단위가 아니라 스프라이트 단위인 이유

에셋 로드에는 정책 결정이 하나 있다. 파일 10개 중 3개가 깨졌을 때 어떻게 할 것인가.

- **전부 폴백**: 하나라도 실패하면 10개 모두 내장 그림. 상태가 둘뿐이라 단순하다.
- **개별 폴백**: 성공한 7개는 파일, 실패한 3개만 내장 그림.

여기서는 개별 폴백을 골랐다. 이유는 **작업 흐름**이다. 에셋을 하나씩 그려 넣는 도중에는 항상 "일부만 있는" 상태를 지나간다. 전부 폴백이면 그 구간 내내 새로 그린 스프라이트가 화면에 나타나지 않고, 그리는 사람은 자기 파일이 잘못된 줄 안다.

<!-- snippet: include/game/defense_view.hpp#Load -->
```cpp
void Load(const std::string& root) {
    // ...
    BuildFallbacks();
    externalAssets = 0;
    for (int slot = 0; slot < kFileCount; ++slot) {
        render::Sprite loaded;
        if (render::LoadPpmP3(root + "/" + FileName(slot), loaded))
            Apply(slot, std::move(loaded));
    }
    // ...
}
```

슬롯 번호 → 파일 이름(`FileName`)과 슬롯 번호 → 배열 자리(`Apply`)가 각각 한 곳에만 있다. 목록을 두 벌 들면 한쪽만 파일이 늘어나 조용히 다른 스킨이 되기 때문이다.

순서가 중요하다. **먼저 `BuildFallbacks()` 로 전부 채우고**, 그 위에 성공한 파일만 덮어쓴다. 반대 순서(파일을 먼저 시도하고 실패한 자리만 채우기)로 짜면 "채우지 않은 자리"라는 세 번째 상태가 생기고, 빈 스프라이트를 블릿하는 경로가 열린다.

`externalAssets` 카운터는 장식이 아니다. 이 숫자가 뒤에서 셀프테스트의 통과 조건 중 하나가 된다 — 10개 전부 외부 파일에서 왔는지를 프로세스 종료 코드로 단언할 수 있게 하는 것이 이 정수 하나다. 관측할 수 없는 폴백은 **조용한 폴백**이고, 조용한 폴백은 배포 사고의 표준적인 형태다.

이 폴백 정책과 콘텐츠 파일(레벨·웨이브·밸런스)의 폴백 정책은 **의도적으로 다르다**. 스프라이트는 개별 폴백이고 콘텐츠는 파일 전체 폴백인데, 그 차이의 근거는 3편에서 다룬다.

## 6. 5×7 비트맵 폰트와 한글을 포기한 자리

HUD 한 줄 — `GOLD 420  CORE 260  WAVE 1  CANNON  SOLO` — 을 찍으려면 글꼴이 필요하다.

TrueType 렌더러를 만드는 것은 이 게임의 범위 밖이고, 시스템 폰트를 쓰면 소프트웨어 프레임버퍼가 갑자기 OS API 를 요구하게 된다. 남은 것은 비트맵 폰트이고, 비트맵 폰트의 비용은 **글자 수에 비례**한다. ASCII 대문자·숫자·기호 몇 개면 46자, 한글은 완성형만 2350자다.

그래서 화면 안 텍스트는 영문 대문자·숫자·기호만 지원하고, 한글이 필요한 자리(창 제목)는 시스템이 그리게 남겼다. 이 타협이 게임의 시각적 톤을 결정했다 — HUD 가 전부 대문자 영문인 것은 미학이 아니라 이 결정의 결과다.

<!-- snippet: include/render/text.hpp#DrawChar -->
```cpp
// (x,y) 에 글자 한 자(scale 배). 대문자로 변환해 그린다.
inline void DrawChar(Framebuffer& fb, int x, int y, char c, Color col, int scale = 2) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    const uint8_t* g = Glyph5x7(c);
    for (int row = 0; row < 7; ++row)
        for (int cx = 0; cx < 5; ++cx)
            if (g[row] & (1 << (4 - cx)))
                FillRect(fb, x + cx * scale, y + row * scale, scale, scale, col);
}
```

글리프는 `uint8_t g[7]`, 각 행의 **하위 5비트**이고 `bit4` 가 왼쪽이다. 소스에서 `0b01110` 처럼 이진 리터럴로 적으므로 코드 자체가 글자 모양으로 보인다 — 폰트 편집기 없이 손으로 고칠 수 있게 하려는 선택이다.

`Glyph5x7` 의 `default` 가 빈 칸을 돌려주는 것이 중요하다. 정의되지 않은 문자가 오면 아무것도 그리지 않는다. 대안은 두부(tofu) 상자를 그리는 것이었는데, HUD 문자열이 `snprintf` 로 조립되는 이상 예상 못한 문자가 섞이는 경로가 있고 그때 화면이 상자로 덮이는 것보다 조용히 비는 편이 낫다고 판단했다.

확대는 정수 배율만 지원한다. `scale` 배 확대가 `FillRect(scale, scale)` 한 번이므로 보간이 없고, 그래서 **어떤 배율에서도 픽셀 경계가 정확히 맞는다**. 문자 간격 `6 * scale` 은 5픽셀 글리프 + 1픽셀 여백이라는 뜻이고, `TextWidth` 가 같은 상수를 쓰므로 가운데 정렬 계산이 렌더 결과와 어긋나지 않는다.

## 7. 창: 픽셀 배열을 화면에 붙이는 유일한 계층

`platform::Window` 는 이 저장소에서 **창 API 를 부를 권한을 가진 유일한 곳**이다. 그 권한 경계가 왜 필요한지는 7편에서 다루고, 여기서는 창이 실제로 무엇을 하는지만 본다.

<!-- snippet: include/platform/window.hpp#Present -->
```cpp
// 프레임버퍼(0xAABBGGRR) → 창에 blit. R/B 스왑해 GDI(BGRX)로 변환.
void Present(const uint32_t* px, int w, int h) {
    if (w != w_ || h != h_) return;
    for (int i = 0; i < w * h; ++i) {
        uint32_t s = px[i];
        uint32_t r = s & 0xFF, g = (s >> 8) & 0xFF, b = (s >> 16) & 0xFF;
        bgra_[i] = (r << 16) | (g << 8) | b;
    }
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w_;
    bi.bmiHeader.biHeight = -h_;     // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC dc = GetDC(hwnd_);
    StretchDIBits(dc, 0, 0, w_ * scale_, h_ * scale_, 0, 0, w_, h_,
                  bgra_.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(hwnd_, dc);
}
```

세 가지가 여기 있다.

**채널 스왑.** 프레임버퍼는 `0xAABBGGRR`(하위 바이트가 R), GDI 의 `BI_RGB` 32비트는 `0x00RRGGBB`(하위 바이트가 B)다. 매 프레임 480×480 = 230400 픽셀을 다시 쓰는 비용을 감수하고 프레임버퍼 쪽 순서를 바꾸지 않았다. 프레임버퍼 순서는 `Pack`/`Unpack` 이 정의하고 테스트가 그 순서를 단언하며, 무엇보다 GPU 표시 경로가 요구하는 순서와도 맞아야 하기 때문이다. **표시 장치의 바이트 순서를 그리기 계층에 새기지 않는다** — 그 규칙의 값이 이 루프다.

**`biHeight = -h_`.** 음수 높이가 top-down DIB 를 뜻한다. 이것이 없으면 GDI 는 bottom-up 으로 해석해 **화면이 위아래로 뒤집힌다**. 대안은 프레임버퍼를 bottom-up 으로 저장하는 것인데, 그러면 `Set(x, y)` 의 y 가 화면 위에서부터인지 아래에서부터인지 헷갈리는 상태가 코드 전역으로 퍼진다.

**`StretchDIBits` 의 배율.** 소스는 `w_ × h_`, 목적지는 `w_ * scale_ × h_ * scale_` 이다. 정수 배 확대를 GDI 에 맡기면 논리 해상도를 480×480 으로 고정한 채 큰 화면에서도 쓸 수 있다. 마우스 좌표를 되돌리는 쪽(`MouseX()` 가 `mouseX_ / scale_`)이 같은 상수를 쓰므로 클릭 위치와 그림 위치가 어긋나지 않는다.

입력은 창 프로시저가 정적 테이블에 적고, 프레임 시작에 이전 상태를 복사해 엣지를 만든다.

<!-- snippet: include/platform/window.hpp#PumpAndContinue -->
```cpp
// 메시지 펌프. 창이 닫히거나 ESC 면 false.
bool PumpAndContinue() {
    for (int i = 0; i < 256; ++i) prevKeys_[i] = keys_[i];
    prevMouseL_ = mouseL_;
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) quit_ = true;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (keys_[VK_ESCAPE]) quit_ = true;
    return !quit_;
}
```

`PeekMessageW` + `PM_REMOVE` 로 **큐를 비우되 블로킹하지 않는다.** `GetMessage` 를 쓰면 메시지가 없을 때 잠들어 프레임 루프가 멈춘다. 게임 루프는 입력이 없어도 계속 돌아야 하므로 논블로킹이 유일한 선택이다.

키 테이블이 `static` 인 것은 `WndProc` 이 정적 함수라 인스턴스를 모르기 때문이다. 창 하나를 전제로 한 타협이고, 그 전제가 클래스 주석에 적혀 있다.

비-Windows 에서는 같은 인터페이스의 헤드리스 스텁이 컴파일된다. `Create()` 가 항상 `false` 를 돌려주므로 **호출자는 "창을 못 만들었다" 한 갈래만 처리하면 된다**. `#ifdef` 를 호출부로 새어 나가게 두지 않는 것이 이 스텁의 유일한 목적이다.

## 8. 480×480 은 어디서 왔는가

숫자는 임의가 아니라 산수다.

```
맵      16열 × 15행 × 30픽셀 = 480 × 450
HUD     상단 30픽셀
합계    480 × 480
```

셀 크기 30픽셀이 먼저 정해졌다. 그보다 작으면 7×7 스프라이트를 2배 확대(14픽셀)했을 때 타워가 셀을 채우지 못해 격자가 비어 보이고, 그보다 크면 16열이 화면에 들어가지 않는다. 열 수 16 은 경로가 세 번 꺾이면서도 양옆에 건설 가능한 여백이 남는 최소치였다.

HUD 30픽셀은 `DrawString(..., scale=1)` 이 7픽셀 높이이므로 여유가 충분하고, 동시에 **격자 셀 하나와 같은 높이**다. 그래서 `map.OriginY()` 가 30 이고, 화면 좌표 → 셀 좌표 변환에 별도의 오프셋 테이블이 필요 없다.

정사각형이 된 것은 결과이지 목표가 아니었다. 다만 결과적으로 편리한 성질이 하나 생겼다 — 뷰 크기가 `kViewWidth`, `kViewHeight` 두 상수로 고정이고 창·GPU 표시·프레임버퍼가 전부 그 둘을 읽으므로, 창 크기가 프레임버퍼 크기와 어긋날 수 있는 경로가 없다. `Present` 의 첫 줄 `if (w != w_ || h != h_) return;` 은 그 불변식이 깨졌을 때 화면이 깨지는 대신 검게 남게 하는 최후의 방어다.

## 9. 실행과 관측

래스터 계층에는 두 개의 검사가 붙어 있다. 둘 다 창도 장치도 요구하지 않는다.

```powershell
cmake --build build --config Release -j
ctest --test-dir build -C Release -R "render"
```

```text
    Start 46: test_render_raster
1/2 Test #46: test_render_raster ...............   Passed    0.04 sec
    Start 47: test_render_sprite
2/2 Test #47: test_render_sprite ...............   Passed    0.04 sec

100% tests passed out of 2

Total Test time (real) =   0.09 sec
```

`ctest` 는 통과·실패만 알려 준다. 실행 파일을 직접 돌리면 몇 개의 단언이 걸렸는지가 보인다.

```powershell
.\build\bin\Release\test_render_raster.exe
.\build\bin\Release\test_render_sprite.exe
```

```text
[test] render_raster
[test] render_raster: 30 checks, 0 failures
[test] render_sprite
[test] render_sprite: 35 checks, 0 failures
```

30 과 35 라는 수가 무엇을 재는지가 중요하다. 이 단언들은 전부 **픽셀 좌표 하나의 색**을 묻는다. 예를 들어 `FillRect(fb, 5, 5, 6, 4, g)` 뒤에 `(10, 8)` 은 초록이고 `(11, 8)` 은 배경이어야 한다 — 폭·높이의 off-by-one 이 여기서 잡힌다. 베벨은 `(4,4)` 가 밑색보다 밝고 `(11,9)` 가 어두우며 내부 `(7,6)` 은 밑색 그대로임을 본다. 스프라이트 쪽은 좌상 모서리 밖(`-1,-1`)과 우하 모서리 밖(`5,5`)으로 블릿해 **경계에서 죽지 않는지**를 확인하고, 마젠타 픽셀의 알파가 0 이 되는지, `999 1` 크기를 주장하는 PPM 이 거부되는지를 본다.

그리기 계층이 게임 안에서 실제로 일했는지는 제품 자신이 답한다.

```powershell
.\build\bin\Release\mega_defense.exe --selftest
```

```text
[assets] loaded 10/10 external sprites from C:\...\build\bin\Release/assets
[content] map=external waves=external (5) balance=script
[selftest] result=CLEAR ticks=1377 towers=11 wave=6 packet=175 protocol=OK render=OK assets=10/10 content=external
[metrics] matches=1 remote_commands=0 snapshots=0 commands_sent=0 command_ack=0 frame_p95=0.0ms snapshot_bytes_p95=0 snapshot_bytes_max=0 snapshots_truncated=0
```

이 줄에서 이 편에 해당하는 것은 셋이다.

- `assets=10/10` — 5절의 `externalAssets` 카운터다. 스프라이트 파일 10개가 전부 P3 로더를 통과했다.
- `render=OK` — 1377틱을 돈 뒤 프레임버퍼에 **배경 클리어 색이 아닌 픽셀이 하나라도 있다**는 뜻이다. 배경색은 `{14, 18, 23, 255}` 하나뿐이므로, 이 검사는 "그리기 호출이 전부 no-op 이 되었는가"를 정확히 잡는다.
- 창이 열리지 않았다 — 위 명령은 창 백엔드를 전혀 쓰지 않는다. 2절에서 프레임버퍼를 순수 메모리로 둔 결정이 여기서 값을 낸다.

GPU 표시 경로도 같은 프레임버퍼를 받는다.

```powershell
.\build\bin\Release\mega_defense.exe --gpu-selftest
```

```text
[gpu-selftest] Vulkan present 5 frames: OK
```

이 검사는 장치도 창도 없는 환경에서는 실패가 아니라 건너뜀으로 끝난다. 그 판단의 근거와 종료 코드 77 의 의미는 7편에서 다룬다.

---

## 결론

이 단계에서 세운 것.

1. **`Framebuffer`** — `0xAABBGGRR` 픽셀 배열. 경계 검사를 `Blend` 한 곳에 모아 위 계층 전부에서 클리핑 코드를 없앴다.
2. **도형 여섯** — 사각형·베벨 사각형·원·선·테두리·삼각형. 삼각형은 정수 에지 함수라 인접 도형 사이에 틈이 없고 정점 순서에 무관하다.
3. **스프라이트** — 읽기는 P3(손으로 편집 가능), 쓰기는 P6(크기). 크기 상한 256×256, `maxValue` 반올림 스케일, 잉여 토큰 거부의 세 방어. 마젠타가 투명 키.
4. **개별 폴백** — 실패한 파일 자리만 내장 그림으로 대체하고, 몇 개가 외부에서 왔는지를 정수로 남겼다. 관측 가능한 폴백만이 폴백이다.
5. **5×7 폰트** — 46자만 지원. 한글은 화면 밖(창 제목)으로 밀어냈고, 그 결과 HUD 가 전부 대문자 영문이다.
6. **`platform::Window`** — 채널 스왑 + top-down DIB + 정수 배 확대. 논블로킹 메시지 펌프. 비-Windows 는 `Create()` 가 `false` 인 스텁이라 호출부에 `#ifdef` 가 없다.

현재 상태의 한계도 분명하다. 이 계층은 **무엇을 그릴지 모른다**. `DefenseSkin` 은 스프라이트 9장과 코어 1장을 들고 있지만 그것들이 놓일 좌표는 아직 어디에도 없고, 격자도 경로도 적도 없다. `DrawGame` 이 참조하는 `wire::Snapshot` 은 이 편에서는 빈 구조체나 다름없다. 그리는 능력은 여기 다 있고 그릴 내용이 없다 — 그 빈칸은 이 계층이 혼자 채울 수 있는 것이 아니다.
