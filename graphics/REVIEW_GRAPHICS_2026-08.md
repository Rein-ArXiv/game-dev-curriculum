# graphics/ 시리즈 검수 보고서 (2026-08)

- 대상: graphics/ 41편 (21,339줄)
- 기준: **확정 가능한 사실오류·산술오류·컴파일 불가 코드·서지오류·내부모순**만. 문체·구성 취향은 대상 아님.
- 방법: 파일별 전문 정독 → 모든 수치·유도 재계산 → 서지 저자/연도/게재지/장절 대조 → 코드 컴파일 가능성·논리 검증 → 문서 내부·문서 간 교차 대조.
- **반영 상태 — 검수 시점은 2026-08, 본문 반영은 2026-08-09(1차)와 2026-08-13(2차)에 이뤄졌다. 즉 이 보고서는 "지적만 하고 본문은 건드리지 않은" 문서가 아니다. 확정 지적 64소절(위치 기준 71건)은 현재 전건이 본문에 반영돼 있고 미해결 이월은 0건이다. 1차에서 확정 항목을 규격·수식과 재대조해 반영했고(`glDrawBuffers`도 OpenGL 4.6 Core 규격에 따라 `bufs[i]`에는 `GL_COLOR_ATTACHMENTi` 또는 `GL_NONE`만 허용되는 것으로 정정), 2차에서 남은 2건을 마저 반영했다. 항목별 판정과 근거는 문서 끝의 `## 반영 상태 (2026-08-13)` 절에 있다. 아래 본문은 수정 전 근거를 보존하기 위해 원래 지적 그대로 남겨 둔 것이다.**

---

## graphics/stage0_fund_03_rasterization.md — 확정 3건

> **반영 상태(2026-08-13): 2건 기반영 + 1건 반영됨** — 1·2번은 선반영. 3번은 코드 주석의 `.z` 필드가 선반영돼 있었고, L213 본문 가정("NDC 깊이 $z$")이 2026-08-13 배치에서 추가돼 마무리됐다.

### 1. L70 — 엣지 함수 증분식의 부호가 **둘 다** 반대

L50의 정의:

$$E_{AB}(P) = (B_x - A_x)(P_y - A_y) - (B_y - A_y)(P_x - A_x)$$

이를 그대로 편미분하면 $\partial E/\partial x = -(B_y - A_y)$, $\partial E/\partial y = +(B_x - A_x)$ 다.
그런데 L70은:

$$E(x+1, y) = E(x, y) \mathbf{+} (B_y - A_y), \qquad E(x, y+1) = E(x, y) \mathbf{-} (B_x - A_x)$$

**두 항 모두 부호가 반대다.** 검산: $A=(0,0),\ B=(0,4)$ 이면 $E(x,y) = -4x$ 이므로
$E(x+1,y) = E - 4$ 인데 $(B_y-A_y) = 4$ 라 문서식은 $E+4$ 를 준다.
$A=(0,0),\ B=(4,0)$ 이면 $E(x,y) = 4y$ 이므로 $E(x,y+1) = E+4$ 인데 문서식은 $E-4$ 다.

**래스터라이저 내부 루프의 핵심 최적화라 그대로 구현하면 삼각형이 뒤집혀 채워진다.**
(L47-51의 정의 자체, L55-57의 부호 해석, L87-90의 무게중심 분모는 모두 정확하다.)

### 2. L208 — far쪽 깊이 해상도 근사식에서 $f$ 하나가 빠짐

> $f \gg n$에서 far쪽 $\delta z \approx \delta z_d\, f/n$ 이라 $n$에 반비례

같은 문단 L205의 $\delta z = \delta z_d \dfrac{f-n}{fn} z^2$ 에 $z = f$ 를 넣으면
$\delta z = \delta z_d \dfrac{(f-n)f}{n} \approx \delta z_d \dfrac{f^2}{n}$ 이다.

**문서의 자체 수치가 이를 반증한다** — $\delta z_d = 2^{-24} = 5.96\times10^{-8}$, $n=0.1$, $f=1000$ 일 때
문서가 두 문장 앞에서 제시한 값은 **0.60 m** 인데,
$\delta z_d f/n = 5.96\times10^{-8}\times10^{4} = 5.96\times10^{-4}$ m = **0.6 mm** 로 **1000배** 어긋난다.
$\delta z_d f^2/n = 5.96\times10^{-8}\times10^{7} = 0.596$ m ✓ 로 계산해야 0.60 m가 나온다.

결론("$n$에 반비례", "$n$을 0.1→0.2로 늘리면 0.60m→0.30m")은 두 식 모두 $1/n$ 이라 영향받지 않고,
"far/near 정밀도 비 $(f/n)^2 = 10^8$" 도 정확하다. **식 한 줄만 틀렸다.**

### 3. L216 / L245 — 선언한 필드 목록에 없는 `.z` 사용

L213 본문("정점은 이미 화면 공간 좌표 $(x,y)$와 클립 공간 $w$, 그리고 속성을 갖는다고 가정한다")과
L216 주석("각각 `.x .y .w .attr`")이 `Vert`의 멤버를 넷으로 못박는데,
L245 `float z = l0*v0.z + l1*v1.z + l2*v2.z;` 가 **선언되지 않은 `.z`(NDC 깊이)** 를 읽는다.
필드 목록과 본문 가정에 NDC 깊이를 추가해야 한다.

---

## graphics/stage0_fund_04_color_gamma.md — 확정 1건

> **반영 상태(2026-08-13): 전건 기반영** — 2026-08-11 백업 대비 diff 에 변경이 없고, 현재 본문이 이미 정정안대로다(2026-08-09 1차 배치에서 선반영).

### L98 — 감마 버그의 원인-증상 대응이 뒤집힘

> 이중 인코딩(이미 sRGB인 값을 또 인코딩)하면 화면이 *씻긴 듯 밝아지고*,
> **디코딩을 빼먹으면**(sRGB를 선형으로 착각) *전체적으로 어두워진다*.

**뒤쪽 절이 틀렸고, 동시에 앞쪽 절과 수학적으로 모순된다.**

- 입력 **디코딩을 빼먹으면** sRGB 코드값 0.5가 선형 0.5로 쓰인다(정답은 0.2176).
  $[0,1]$ 에서 인코딩값 $E = L^{1/2.2} \ge L$ 이므로 **알베도가 실제보다 밝아지고**,
  이후 출력 인코딩은 정상 수행되므로 **화면은 밝고 씻긴다.**
  즉 디코딩 누락의 순 효과는 "$^{1/2.2}$ 가 한 번 더 걸리는 것"으로
  **앞 절의 이중 인코딩과 정확히 같은 오류 클래스**인데 정반대 증상을 주장한다.
- "**전체적으로 어두워지는**" 증상의 실제 원인은 **출력 인코딩 누락**이다 —
  선형 값을 그대로 non-sRGB 프레임버퍼에 쓰면 디스플레이가 $^{2.2}$ 를 적용해 어두워진다.

바로 위 L94-96이 파이프라인을 "입력 디코딩 / 선형 계산 / 출력 인코딩" 세 단계로 정확히 나눠 놓았으므로,
증상도 그 세 단계에 맞춰 "디코딩 누락 또는 이중 인코딩 → 밝아짐 / 출력 인코딩 누락 → 어두워짐"으로
정리하면 된다. (L100의 sRGB 코드값 평균이 칙칙해진다는 예시는 별개 논점이고 정확하다.)

---

## graphics/stage0_fund_09_geometry_mesh.md — 확정 4건

> **반영 상태(2026-08-13): 전건 기반영** — 2026-08-11 백업 대비 diff 에 변경이 없고, 현재 본문이 이미 정정안대로다(2026-08-09 1차 배치에서 선반영).

### 1. L95-97 — 감김 순서 그림이 캡션과 정반대

```text
   v0          앞면(CCW)을 보는 시점에서
  /  \         v0→v1→v2 가 반시계 방향
 v2——v1        → 이 면이 카메라를 향한 앞면
```

그림은 **v0 = 위, v1 = 오른쪽 아래, v2 = 왼쪽 아래**이므로 $v_0 \to v_1 \to v_2$ 순회는
12시 → 4시 → 8시, 즉 **시계 방향(CW)** 이다.

부호 있는 면적으로 확인: $v_0 = (0.5, 1),\ v_1 = (1,0),\ v_2 = (0,0)$ 일 때
$\tfrac12[(x_1-x_0)(y_2-y_0) - (x_2-x_0)(y_1-y_0)] = \tfrac12[(0.5)(-1) - (-0.5)(-1)] = -0.5 < 0$ → **CW**.

**감김 순서를 정의하는 절의 유일한 그림이 정반대 감김을 보여준다.**
아래 두 정점 라벨을 맞바꿔 ` v1——v2` 로 하면 실제로 CCW가 된다.

### 2. L191 / L209 / L233 — 통합 이전 문서의 절 번호가 남음 (개정 잔재 3곳)

| 위치 | 문서 서술 | 실제 |
|---|---|---|
| L191 | "적응적 테셀레이션의 핵심이다(**3절**)" | 3절은 "인덱스 버퍼와 정점 재사용", 적응적 테셀레이션은 **10절** |
| L209 | "높이 변화가 심한 곳(**4절**)" | 4절은 "감김 순서", 디스플레이스먼트는 **11절** |
| L233 | "(**3절**의 적응 분할이 …)" | 같은 오류 반복 → **10절** |

세 곳 모두 **8~13절이 별도 문서(증폭=1, 테셀레이션 파이프라인=2, 적응적 테셀레이션=3,
디스플레이스먼트=4 …)였던 시절의 번호**를 그대로 쓰고 있다.
독자가 3절로 가면 인덱스 버퍼 이야기가 나온다.

---

## graphics/stage0_fund_05_shading_materials.md — 확정 3건

> **반영 상태(2026-08-13): 전건 기반영** — 2026-08-11 백업 대비 diff 에 변경이 없고, 현재 본문이 이미 정정안대로다(2026-08-09 1차 배치에서 선반영).

### 1. L234-237 / L300-303 — 상호참조 절 번호 8개가 전부 어긋남 (같은 오류가 두 표에 반복)

§8 표와 §13 표가 똑같이 다음처럼 가리킨다:

| 표의 표기 | 실제 절 |
|---|---|
| 이방성(**2절**) | **9절** (2절은 "Lambert 확산 반사") |
| 표면하 산란(**3절**) | **10절** (3절은 "Phong과 Blinn-Phong 정반사") |
| 투과·굴절(**4절**) | **11절** (4절은 "경험적 모델의 한계") |
| 레이어(**5절**) | **12절** (5절은 "렌더링 방정식과 BRDF") |

**두 표가 같은 오류를 공유하므로 단순 오타가 아니라 병합 시 갱신되지 않은 상호참조**다.
이방성이 2절, SSS가 3절, 투과가 4절, 레이어가 5절이던 별도 문서의 번호가 그대로 남았다.

### 2. L319 / L322 — Burley 논문 중복 등재

동일 문헌(Brent Burley, "Physically-Based Shading at Disney", SIGGRAPH 2012 —
**저자·제목·게재지·연도·URL 전부 일치**)이 "더 읽을거리"에 두 번 실려 있고 설명 문구만 다르다.

---

## graphics/stage0_fund_08_framebuffer_multipass.md — 확정 1건

> **반영 상태(2026-08-13): 전건 기반영** — 2026-08-11 백업 대비 diff 에 변경이 없고, 현재 본문이 이미 정정안대로다(2026-08-09 1차 배치에서 선반영).

### L296 — GPU Pro / GPU Zen 편집자

> **Eric Lengyel** ed., 각 권의 *GPU Pro / GPU Zen* 시리즈

**GPU Pro 시리즈(1~7, A K Peters/CRC Press)와 GPU Zen 시리즈(1~3, Black Cat Publishing)의
편집자는 Wolfgang Engel**이다. Eric Lengyel은 *Game Engine Gems* 1~3의 편집자이자
*Foundations of Game Engine Development*의 저자로, GPU Pro/GPU Zen 편집에 관여하지 않았다.

같은 시리즈의 `stage0_fund_02`(L232)와 `stage0_fund_09`(L277)는
Lengyel을 각각 FGED 저자·탄젠트 공간 글 저자로 **정확히** 인용하고 있어 대비된다.

---

## graphics/stage0_adv_03_global_illumination_raytracing.md — 확정 1건

> **반영 상태(2026-08-13): 전건 기반영** — 2026-08-11 백업 대비 diff 에 변경이 없고, 현재 본문이 이미 정정안대로다(2026-08-09 1차 배치에서 선반영).

### L286 — *Ray Tracing Gems* II권의 편집진

> **Eric Haines, Tomas Akenine-Möller** eds., *Ray Tracing Gems* **I·II**

- *Ray Tracing Gems* (Apress, 2019) — Eric Haines, Tomas Akenine-Möller 공동 편집 ✓
- *Ray Tracing Gems II* (Apress, 2021) — **Adam Marrs, Peter Shirley, Ingo Wald** 편집

두 권의 편집진이 다른데 하나로 묶었다.

---

## graphics/stage0_fund_02_coordinate_transform.md — 확정 1건

> **반영 상태(2026-08-13): 전건 기반영** — 2026-08-11 백업 대비 diff 에 변경이 없고, 현재 본문이 이미 정정안대로다(2026-08-09 1차 배치에서 선반영).

### L7 — 본문에 없는 내용을 예고 (개정 잔재)

> **`Mat4` 구현**과 곱셈 순서·메모리 레이아웃의 실전 디테일까지 포함해 …

문서 전체(1~236행)에 **코드 블록은 L29-36의 mermaid 다이어그램 하나뿐**이고
C++/셰이더 코드가 한 줄도 없다. §8은 메모리 레이아웃과 곱셈 순서를 **서술로만** 다루며
`Mat4` 타입·구현·API 시그니처는 등장하지 않는다.

같은 문장 안의 다른 예고(원근 투영 행렬 절두체 유도 → 4절, 클립 공간 $w$ → 5절,
법선 변환 → 7절)는 **모두 실제로 존재**하므로 `Mat4` 항목만 잔재다.

---

## graphics/stage1_gl_fund_01_context.md — 확정 2건

> **반영 상태(2026-08-13): 전건 기반영** — 2026-08-11 백업 대비 diff 에 변경이 없고, 현재 본문이 이미 정정안대로다(2026-08-09 1차 배치에서 선반영).

### 1. L165 — 초기화 순서가 뒤집힘 (그대로 따라 하면 Core Profile이 안 만들어짐)

> 함수 이름과 핸들 타입이 다를 뿐 흐름은 동일하다. **윈도우 만들기 → 속성 힌트 → 컨텍스트 생성** → 메인 루프 → 정리.

바로 위 GLFW 예제(L100-104)와 SDL 예제(L139-147)가 **둘 다 "속성 힌트 → 윈도우 생성"** 순서이고,
§3(L298)도 **"힌트는 `glfwCreateWindow` 호출 *이전* 에 설정해야 한다"** 고 명시한다.

힌트를 윈도우 생성 뒤에 설정하면 그 윈도우에는 적용되지 않으므로,
초보자가 이 요약대로 따라 하면 **Core Profile 컨텍스트가 만들어지지 않는다.**
"속성 힌트 → 윈도우/컨텍스트 생성 → 메인 루프 → 정리"가 맞다.

### 2. L686 — `glGenBuffers`의 코어 승격 버전

> `glGenBuffers`(**2.0**), `glCreateProgram`(2.0), `glBindVertexArray`(3.0)

버퍼 오브젝트(`glGenBuffers`/`glBindBuffer`/`glBufferData`/`glDeleteBuffers`)는
**OpenGL 1.5**(2003, ARB_vertex_buffer_object)에서 코어로 들어왔다.
2.0은 **셰이더 오브젝트**(`glCreateProgram`/`glCreateShader` 등)가 코어가 된 버전이다.
`glCreateProgram`(2.0)과 `glBindVertexArray`(3.0)은 정확하다.

---

## graphics/stage1_gl_fund_02_transform.md — 확정 1건

> **반영 상태(2026-08-13): 전건 기반영** — 2026-08-11 백업 대비 diff 에 변경이 없고, 현재 본문이 이미 정정안대로다(2026-08-09 1차 배치에서 선반영).

### L49 — 버전 한정 없는 단정

> **OpenGL ES**의 uniform 행렬 업로드는 `transpose`가 `GL_FALSE`여야 한다.

이 제약은 **OpenGL ES 2.0에만** 해당한다. ES 2.0 명세는 `UniformMatrix*` 계열에서
`transpose`가 `FALSE`가 아니면 `INVALID_VALUE`를 내도록 규정했지만,
**ES 3.0에서 이 제약이 제거되어 `transpose = GL_TRUE`(행 우선 순서로 읽기)가 정식으로 허용**된다.
현재 문장대로면 ES 3.x 타깃 독자가 유효한 사용을 금지된 것으로 오해한다.

---

## graphics/stage1_gl_fund_04_texture.md — 확정 2건

> **반영 상태(2026-08-13): 전건 기반영** — 2026-08-11 백업 대비 diff 에 변경이 없고, 현재 본문이 이미 정정안대로다(2026-08-09 1차 배치에서 선반영).

### 1. L121 — `GL_TEXTURE_MIN_FILTER` 기본값 (문서 내부 모순)

> **`GL_LINEAR_MIPMAP_LINEAR`이 기본**인데 `glGenerateMipmap`을 빠뜨리면 정확히 이 상태가 된다.

OpenGL의 `GL_TEXTURE_MIN_FILTER` 초깃값은 **`GL_NEAREST_MIPMAP_LINEAR`** 다
(GL 3.3~4.6 명세의 텍스처 파라미터 초기 상태 표, 샘플러 객체도 동일).

**같은 문서 L427이 "min filter의 기본값이 `GL_NEAREST_MIPMAP_LINEAR`라는 것이다
(mag 기본값은 `GL_LINEAR`)"로 정확히 적고 있어 문서 안에서 서로 모순된다.**

### 2. L170 / L214 — `ARB_bindless_texture`는 코어가 아니다

> OpenGL **4.5+** 또는 `GL_ARB_bindless_texture` 확장은 텍스처 유닛 모델을 완전히 우회한다.

`ARB_bindless_texture`는 **코어로 승격된 적이 없다.**
GL 4.5 코어 추가분(ARB_direct_state_access, ARB_clip_control, ARB_cull_distance,
ARB_texture_barrier 등)에도, 4.6 코어 추가분(ARB_gl_spirv, ARB_texture_filter_anisotropic,
ARB_shader_draw_parameters 등)에도 들어 있지 않다.
최신 4.6 컨텍스트라도 **확장 지원 여부를 따로 질의해야 하고 벤더별로 갈린다.**
L170의 "4.5의 bindless로 우회한다"도 같은 오류다.

---

## graphics/stage1_gl_fund_05_framebuffer.md — 확정 2건

> **반영 상태(2026-08-13): 전건 기반영** — 2026-08-11 백업 대비 diff 에 변경이 없고, 현재 본문이 이미 정정안대로다(2026-08-09 1차 배치에서 선반영).

### 1. L252 — 깊이 분해능이 약 100배 과대

> $n = 0.1$, $f = 1000$인 일반적 투영에서, 24비트 깊이 버퍼는 **카메라에서 100m 떨어진 지점**의
> 깊이 분해능이 **수십 cm** 단위로 거칠어진다.

본문 자신의 식에서 창 깊이 $d(D) = \dfrac{f(D-n)}{(f-n)D}$ 이므로
$\dfrac{dd}{dD} = \dfrac{fn}{(f-n)D^2}$ 다. $f=1000,\ n=0.1$ 이면 $\dfrac{fn}{f-n} = 0.10001$.

24비트 unorm 1스텝 $= 2^{-24} = 5.96\times10^{-8}$ 이므로
$$\Delta D = \frac{2^{-24} D^2}{0.10001}$$

- $D = 100$ m → $\Delta D = 5.96\times10^{-8} \times 10^4 / 0.1 = 5.96\times10^{-3}$ m ≈ **6 mm**
- $D = 410$ m → 약 10 cm
- $D = 1000$ m(far) → 약 **60 cm** ← "수십 cm"는 여기서 나온다

**100 m 지점의 실제 분해능은 밀리미터 단위**다. 거리를 far plane 근처로 바꾸거나
수치를 6mm로 고쳐야 한다. (같은 시리즈 `stage0_fund_03` L208이 $z=1000$에서 0.60 m로
정확히 같은 값을 내고 있어 대비된다.)

### 2. L344 — 명세가 금지하는 동작을 가능하다고 서술

> 따라서 **location 0을 ATTACHMENT2로, location 1을 ATTACHMENT0으로 라우팅**하는 식의 재배치도 가능하다.

GL 3.3 Core §4.2.1(4.6 기준 §17.4.1) `DrawBuffers`:
FBO에 바인딩된 경우 **`bufs`의 $i$번째 원소는 반드시 `COLOR_ATTACHMENT`$i$ 또는 `NONE`** 이어야 하며,
순서를 바꿔 지정하면 **`GL_INVALID_OPERATION`** 이 발생한다.
즉 location ↔ 어태치먼트 슬롯 재배치는 FBO에서 불가능하다.
(기본 프레임버퍼에 대해서만 `BACK_LEFT` 등 다른 값이 허용된다.)

---

## graphics/stage1_gl_proj_01_cube.md — 확정 2건

> **반영 상태(2026-08-13): 전건 기반영** — 2026-08-11 백업 대비 diff 에 변경이 없고, 현재 본문이 이미 정정안대로다(2026-08-09 1차 배치에서 선반영).

### 1. L410 — 절약되는 곱셈 횟수

> 행렬 곱 한 번이 64개의 곱셈-덧셈이므로, 오브젝트 10,000개 × 60fps면 **프레임당 약 384만 회** 곱셈이 절약된다.

$4\times4$ 행렬 곱 1회 = 64회 곱셈이므로 오브젝트 10,000개면 **프레임당 640,000회(64만)** 다.
바로 위 코드의 `Model()`이 `translate × rot × scale` 로 mat4 곱을 2회 하는 것으로 세어도 **128만 회**다.
**384만은 어느 해석으로도 나오지 않는다.**

$64\text{만} \times 60\text{fps} = 3{,}840\text{만}$(초당)이므로, **초당 값의 자릿수를 1/10로 옮긴 것**으로 보인다.
"프레임당 × fps"라는 단위 혼용도 함께 정리해야 한다.

### 2. L646-649 — 배율과 값이 서로 어긋남

> near = 0.1 → near = **0.001**로 줄이면 … near를 **10분의 1**로 줄이면 …
> - near = 0.1: 약 0.0008 (NDC 단위)
> - near = 0.001: 약 **0.00008** (10배 미세)

$0.1 \to 0.001$ 은 **10분의 1이 아니라 100분의 1**이다.
정밀도가 $n$에 거의 비례한다는 본문 논리를 그대로 적용하면 near = 0.001에서의 값은
0.0008의 **100분의 1인 0.000008**이어야 한다.
제시된 near 값(0.001)·배율 서술(10분의 1)·불릿 값(0.00008, 정확히 10배 차이)이 삼중으로 어긋난다.
**near를 0.01로 고치면 나머지 둘이 모두 맞는다.**

---

## graphics/stage1_gl_proj_02_model.md — 확정 3건

> **반영 상태(2026-08-13): 전건 기반영** — 2026-08-11 백업 대비 diff 에 변경이 없고, 현재 본문이 이미 정정안대로다(2026-08-09 1차 배치에서 선반영).

### 1. L338 / L455 — 4K 텍스처 크기가 정확히 2배 틀림 (2곳 반복)

> 4K 디퓨즈 텍스처 한 장이 GPU에서 **32MB**(`4096 × 4096 × 4B` + 밉맵 약 1/3)를 차지하므로

**괄호 안 식을 그대로 계산하면** $4096 \times 4096 \times 4 = 67{,}108{,}864$ B = **64 MiB**다.
32MB는 정확히 2배 틀렸고 **수식과 결과가 서로 모순**된다. 밉맵 1/3을 더하면 약 85 MB.

L455에서 같은 오류가 반복되는데, 여기서는 **바로 앞 문장과도 충돌**한다:

> 4K 텍스처 한 장이 … GPU에서 약 **32MB**를 차지하는데, BC7 같은 GPU 압축 포맷으로는 약 16MB

BC7은 8 bpp이므로 $4096^2 \times 1\text{B} = 16$ MiB가 맞다.
그런데 기준값을 32MB로 잡으면 감소율이 **1/2**이 되어
앞 문장의 **"VRAM 사용량을 1/4에서 1/8까지 줄인다"** 와 정면으로 어긋난다.
**64MB → BC7 16MB(1/4) → ETC2 RGB 8MB(1/8)** 이어야 앞 문장과 맞는다.

### 2. L242 — 코드 설명과 코드가 불일치

> `aiProcess_SortByPType`로 프리미티브 종류를 분리하거나, **위처럼 인덱스 수를 확인하며 삼각형 면만 골라 담는다.**

L234-239 코드에는 **삼각형 필터가 전혀 없다**:

```cpp
for (unsigned j = 0; j < face.mNumIndices; ++j)
    out.indices.push_back(face.mIndices[j]);
```

`mNumIndices`가 1이든 2이든 **무조건 전부 push** 한다.
즉 바로 앞 문장이 경고한 "면이 1개나 2개 인덱스를 가질 수 있고, 그런 면을 삼각형 인덱스 버퍼에
섞으면 그리기가 깨진다"는 상황을 **이 코드가 그대로 유발한다.**
`if (face.mNumIndices != 3) continue;` 를 face 루프 첫 줄에 넣어야 설명과 맞는다.

---

## graphics/stage1_gl_proj_04_shadow.md — 확정 2건

> **반영 상태(2026-08-13): 전건 기반영** — 2026-08-11 백업 대비 diff 에 변경이 없고, 현재 본문이 이미 정정안대로다(2026-08-09 1차 배치에서 선반영).

### 1. L220 — FBO 불완전 오류 코드

> 이 호출이 없으면 … **`GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT`** 오류로 FBO가 불완전 상태가 된다.

OpenGL 명세(4.6 core §9.4.2)상 `GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT`는
**"프레임버퍼에 이미지가 하나도 붙어 있지 않을 때"** 만 반환된다.
여기서는 `GL_DEPTH_ATTACHMENT`에 `depthMap`이 붙어 있으므로 해당하지 않는다.

`glDrawBuffer(GL_NONE)`를 생략하면 FBO의 기본 `DRAW_BUFFER0`가 `GL_COLOR_ATTACHMENT0`인데
그 어태치먼트의 `OBJECT_TYPE`이 `NONE`이므로 → **`GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER`** 다.
(`glReadBuffer(GL_NONE)`를 빠뜨리면 READ_FRAMEBUFFER 검사에서 `..._READ_BUFFER`.)

### 2. L409-415 — 표는 월드 공간, 설명은 정규화 공간 (단위 혼동)

| 섀도 맵 | 깊이 범위 | LSB당 깊이 | 권장 bias |
|---|---|---|---|
| 1024², 24bit | 50 | $50/2^{24} \approx 3\times10^{-6}$ | 0.005 ~ 0.01 |
| 2048², 24bit | 100 | $100/2^{24} \approx 6\times10^{-6}$ | 0.005 |

표의 "LSB당 깊이" 열은 $(far - near)/2^{24}$, 즉 **월드 공간 거리**를 계산해 놓았다.
그런데 L415는 이를 **"정규화 공간에서 갖는 크기"** 라고 설명한다.

정규화 $[0,1]$ 깊이 공간에서 24비트 1 LSB는 **절두체 깊이 범위와 무관하게 항상**
$2^{-24} \approx 5.96\times10^{-8}$ 이므로 표 값과 설명은 **서로 배타적**이다.
게다가 L407이 "bias는 $[0,1]$로 정규화된 깊이 공간에서의 값"이라고 명시했으므로,
L415의 **"bias는 이 양자화 단위보다 충분히 커야"** 는 서로 다른 단위를 비교하는 문장이 된다.

---

## graphics/stage1_gl_proj_05_postprocess.md — 확정 2건

> **반영 상태(2026-08-13): 전건 기반영** — 2026-08-11 백업 대비 diff 에 변경이 없고, 현재 본문이 이미 정정안대로다(2026-08-09 1차 배치에서 선반영).

### 1. L453 — ACES에서 RRT를 "입력 변환"이라 부름

> **입력 변환**(RRT, Reference Rendering Transform)과 디스플레이 변환(ODT, Output Device Transform)의
> **두 단계**로 장면 색을 디스플레이 색으로 매핑한다.

ACES에서 **입력 변환은 IDT(Input Device Transform)** 이고,
**RRT는 scene-referred 값을 display-referred(OCES)로 옮기는 "참조 렌더링 변환"** 이다.
괄호 안 영문 전개(Reference Rendering Transform)와 앞의 한글 라벨(입력 변환)이 서로 어긋난다.
ACES 파이프라인은 **IDT → RRT → ODT의 세 단계**다.

### 2. L473 — `clamp`가 필요한 진짜 이유가 빠졌고 앞 줄과 모순

> `clamp`가 마지막에 붙는 이유는 **입력이 음수이거나 수치적으로 분모가 매우 작아질 때**
> 결과가 [0, 1]을 벗어날 수 있기 때문이다.

바로 위 L470이 이미 **"2차항 비 $a/c \approx 1.03$에 수렴"** 이라고 옳게 적었다.
$a/c = 2.51/2.43 = 1.0329 > 1$ 이므로 **이 fit은 큰 양수 입력에서 필연적으로 1을 넘는다.**

교차점을 직접 풀면 $2.51x^2 + 0.03x = 2.43x^2 + 0.59x + 0.14$
→ $x^2 - 7x - 1.75 = 0$ → $x = \dfrac{7 + \sqrt{56}}{2} = **7.2416**$.
즉 $x > 7.24$ 에서 이미 출력 > 1이고($f(8) = 1.00312$, $f(20) = 1.021$, $f(\infty) = 1.0329$),
이는 **노출 적용 후 HDR 입력에서 흔한 범위**다.

clamp는 "음수·수치 예외" 때문이 아니라 **정상적인 밝은 입력 때문에** 필요하다.
같은 문단의 "곡선이 완만히 1로 휜다"도 1.033으로 수렴하는 것이므로 표현을 맞춰야 한다.


## graphics/stage2_dx_fund_01_com.md — 확정 2건

> **반영 상태(2026-08-13): 전건 기반영** — 2026-08-11 백업 대비 diff 에 변경이 없고, 현재 본문이 이미 정정안대로다(2026-08-09 1차 배치에서 선반영).

### 1. L126 / L201 — `ID3D11Device1`에 없는 메서드 (컴파일 불가, 2곳)

```cpp
device1->ClearState();
```

`ClearState()`는 **`ID3D11DeviceContext`의 메서드**이지 `ID3D11Device`/`ID3D11Device1`의 것이 아니다.
`ID3D11Device1`이 `ID3D11Device`에 추가하는 메서드는 `GetImmediateContext1`,
`CreateDeferredContext1`, `CreateBlendState1`, `CreateRasterizerState1`,
`CreateDeviceContextState`, `OpenSharedResource1`, `OpenSharedResourceByName` **7개뿐**이다.
두 예제 모두 `C2039`(멤버 없음)로 빌드되지 않는다.

### 2. L509 — `FLIP_DISCARD`의 최소 OS 버전

> | `FLIP_DISCARD` (권장) | **Windows 8 이상** | … |

`DXGI_SWAP_EFFECT_FLIP_DISCARD`(값 4)는 **Windows 10부터**다.
Windows 8부터 지원되는 것은 바로 윗줄의 **`FLIP_SEQUENTIAL`(값 3)** 뿐이다.
표대로 Windows 8/8.1 타깃에서 `FLIP_DISCARD`를 쓰면 `CreateSwapChainForHwnd`가 실패한다.

---

## graphics/stage2_dx_fund_02_resource.md — 확정 1건

> **반영 상태(2026-08-13): 전건 기반영** — 2026-08-11 백업 대비 diff 에 변경이 없고, 현재 본문이 이미 정정안대로다(2026-08-09 1차 배치에서 선반영).

### L170 — RTV의 배열 슬라이스 제약

> RTV는 한 번에 mip 한 장, **배열 슬라이스 한 면만** 가리킨다.

**mip 제약은 맞지만 배열 슬라이스 제약은 틀렸다.**
`D3D11_TEX2D_ARRAY_RTV`는 `{ UINT MipSlice; UINT FirstArraySlice; UINT ArraySize; }` 로,
`MipSlice`는 단수지만 **`ArraySize`는 1보다 클 수 있다.**
`D3D11_TEX2D_ARRAY_DSV`도 동일하고, `D3D11_TEX3D_RTV`는 `FirstWSlice`/`WSize`로
여러 깊이 슬라이스를 묶는다. D3D12의 `D3D12_TEX2D_ARRAY_RTV`도 같다.

실제로 **지오메트리 셰이더의 `SV_RenderTargetArrayIndex`로 큐브맵 6면·캐스케이드 그림자를
한 패스에 그리는 기법이 여러 슬라이스를 한 RTV에 묶는 것**에 의존한다.

---

## graphics/stage2_dx_fund_04_dx12.md — 확정 1건

> **반영 상태(2026-08-13): 반영됨** — L223 코드는 선반영(`DrawIndexedInstanced`)돼 있었고, L198 본문의 `DrawIndexed` 표기가 2026-08-13 배치에서 정정됐다.

### L223 (및 L198 본문) — `ID3D12GraphicsCommandList`에 없는 메서드

```cpp
cmdList->DrawIndexed(indexCount, 1, 0, 0, 0);
```

D3D12의 인덱스 드로우 메서드는 **`DrawIndexedInstanced(IndexCountPerInstance, InstanceCount,
StartIndexLocation, BaseVertexLocation, StartInstanceLocation)`** 하나뿐이다.
`DrawIndexed(UINT, UINT, INT)`는 **D3D11의 `ID3D11DeviceContext`** 메서드이고 인자도 3개다.
코드가 인자 5개를 넘기고 있으므로 의도는 명백히 `DrawIndexedInstanced`이며,
현재 상태로는 `C2039` 컴파일 에러다.

---

## graphics/stage2_dx_proj_02_model_dx11.md — 확정 4건

> **반영 상태(2026-08-13): 전건 기반영** — 2026-08-11 백업 대비 diff 에 변경이 없고, 현재 본문이 이미 정정안대로다(2026-08-09 1차 배치에서 선반영).

### 1. L576 — SimpleMath의 좌표계가 정반대

> `CreateLookAt`과 `CreatePerspectiveFieldOfView`는 DX의 **왼손 좌표계를 전제**한다.
> 1절에서 `aiProcess_ConvertToLeftHanded`로 모델을 왼손 좌표계로 변환했으므로 이 함수들과 일관된다.

**정반대다.** DirectXTK **SimpleMath는 XNA Game Studio 호환을 위해 오른손 좌표계를 기본**으로 한다.
`SimpleMath.inl`에서 `Matrix::CreateLookAt`은 **`XMMatrixLookAtRH`** 를,
`Matrix::CreatePerspectiveFieldOfView`는 **`XMMatrixPerspectiveFovRH`** 를 감싼다
(`CreatePerspective`/`CreateOrthographic`도 모두 RH).

따라서 `aiProcess_ConvertToLeftHanded`로 변환한 모델과 함께 쓰면 "일관"되는 게 아니라
**정확히 어긋나며, Z 부호가 반대라 모델이 카메라 뒤로 가거나 컬링이 뒤집힌다.**
**이 문서의 결론을 정면으로 뒤집는 오류다.**

### 2. L588 — `GeometricPrimitive::Draw` 인자 순서가 뒤바뀜

```cpp
sphere->Draw(world2, view, proj, Colors::Red, false, texture.Get());
```

실제 시그니처는

```cpp
Draw(FXMMATRIX world, CXMMATRIX view, CXMMATRIX projection,
     FXMVECTOR color = Colors::White,
     ID3D11ShaderResourceView* texture = nullptr,   // ← 먼저
     bool wireframe = false,                        // ← 나중
     std::function<void()> setCustomState = nullptr);
```

**color 다음이 `texture`, 그다음이 `wireframe`** 이다. 코드는 texture 자리에 `bool false`를,
wireframe 자리에 SRV 포인터를 넘긴다.

### 3. L401 — 존재하지 않는 DXGI 포맷

> 노멀을 **`BC5_UNORM_SRGB`** 로 변환하면 법선이 일그러진다.

`DXGI_FORMAT`에 **`BC5_UNORM_SRGB`라는 값은 없다.**
sRGB 변종이 있는 블록 압축 포맷은 **`BC1_UNORM_SRGB`(72), `BC2_UNORM_SRGB`(75),
`BC3_UNORM_SRGB`(78), `BC7_UNORM_SRGB`(99)** 넷뿐이고,
BC4/BC5(79~84)와 BC6H에는 sRGB 변종이 정의되어 있지 않다
(단일/2채널 데이터라 sRGB 전달함수를 적용할 대상 자체가 없다).

경고하려는 실수 자체는 실재하지만 **존재하지 않는 상수로 설명하고 있다.**
실제로 일어나는 실수는 `BC7_UNORM_SRGB`/`BC1_UNORM_SRGB` 같은 변종으로 굽는 경우다.

### 4. L428 — `texconv.exe`의 소속 라이브러리

> **DirectXTK**가 제공하는 `texconv.exe`는 …

`texconv.exe`는 **DirectXTex**(microsoft/DirectXTex)의 명령줄 도구다.
DirectXTK는 런타임 헬퍼(DDSTextureLoader, SpriteBatch, Effects 등)이고
texconv/texassemble/texdiag는 DirectXTex 산출물이다.
**같은 문서의 "더 읽을거리"(L647)가 DDS·BC 압축 도구를 DirectXTex로 올바르게 인용**하고 있어
내부적으로도 어긋난다.

---

## graphics/stage2_dx_proj_04_dx12_transition.md — 확정 3건

> **반영 상태(2026-08-13): 전건 기반영** — 2026-08-11 백업 대비 diff 에 변경이 없고, 현재 본문이 이미 정정안대로다(2026-08-09 1차 배치에서 선반영).

### 1. L121 / L135 — DX11 스왑 체인 생성 인자

> DX11에서는 스왑 체인 생성에 **디바이스 컨텍스트**를 넘겼지만, DX12에서는 명령 큐를 넘긴다.
> ```cpp
> directQueue.Get(),     // DX11은 DeviceContext, DX12는 CommandQueue
> ```

`IDXGIFactory2::CreateSwapChainForHwnd`의 첫 인자 `pDevice`에 D3D11에서 넘기는 것은
**`ID3D11Device`(디바이스)** 이지 `ID3D11DeviceContext`가 아니다.
MS 문서는 `pDevice`를 "For Direct3D 11 and earlier, a pointer to the **Direct3D device**;
for Direct3D 12, a pointer to a **direct command queue**"로 규정한다.
`D3D11CreateDeviceAndSwapChain`도 컨텍스트를 **입력으로 받지 않고 출력으로 돌려준다.**

**DX11 → DX12 대비의 핵심 문장인데 DX11 쪽 사실이 틀렸다.**

### 2. L161 — RTV 포맷이 PSO 포맷과 불일치 (본문 설명과 코드가 어긋남)

```cpp
device->CreateRenderTargetView(backBuffers[i].Get(), nullptr, rtvHandle);
```

`pDesc`에 `nullptr`을 넘기면 런타임이 **리소스의 포맷 그대로** 기본 RTV를 만든다.
백 버퍼는 `scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM`(L126)이므로 이 RTV는 **`_UNORM`** 이다.

그런데 PSO는 `psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB`(L216)로 박혀 있고,
L225는 **"실제 RTV의 포맷이 PSO에 박힌 포맷과 다르면 디버그 레이어가 즉시 오류를 낸다"** 고
스스로 못박은 뒤 **"여기서 RTV 포맷은 `_SRGB`로 지정했다"** 고 서술한다.
**코드는 `_SRGB`를 지정하지 않으므로, 문서가 스스로 경고한 오류를 그대로 유발한다.**
sRGB 캐스트를 하려면 명시적 `D3D12_RENDER_TARGET_VIEW_DESC`가 필요하다.

### 3. L691 — 가리키는 표의 행이 한 칸 어긋남 (개정 잔재)

> 표의 **마지막 세 항목**이 DX11에 대응이 없는, 책임 이전을 가장 잘 보여주는 행이다.

표(L668-689)의 실제 마지막 세 행은 `(자동 residency)`, `Deferred Context`, `Present`인데,
이어지는 문장은 **자동 배리어·자동 residency·디퍼드 컨텍스트** 셋을 설명한다.
또한 **`Deferred Context`는 DX11에 엄연히 존재하는 API**이므로
"DX11에 대응이 없는"이라는 수식도 그 행에는 맞지 않는다.

---

## graphics/stage3_vk_fund_01_pipeline.md — 확정 1건

> **반영 상태(2026-08-13): 전건 기반영** — 2026-08-11 백업 대비 diff 에 변경이 없고, 현재 본문이 이미 정정안대로다(2026-08-09 1차 배치에서 선반영).

### L301 — 존재하지 않는 SPIR-V 명령

> `discard`(GLSL) 또는 **`OpVkill`** 계열은 프래그먼트를 버려 …

SPIR-V에 **`OpVkill`이라는 명령은 없다.** 해당 명령은 **`OpKill`** 이며
(SPIR-V 1.6에서 deprecated), 계열 명령은 **`OpTerminateInvocation`** 과
**`OpDemoteToHelperInvocation`** 이다.

---

## graphics/stage3_vk_proj_02_model_loading.md — 확정 1건

> **반영 상태(2026-08-13): 전건 기반영** — 2026-08-11 백업 대비 diff 에 변경이 없고, 현재 본문이 이미 정정안대로다(2026-08-09 1차 배치에서 선반영).

### L675 — `vkCmdResolveImage`는 깊이에 쓸 수 없다

> 후처리에서 멀티 샘플 깊이를 표본 단위로 읽어야 한다면 **`vkCmdResolveImage`** 나
> 셰이더 resolve를 별도로 쓰지만 …

`vkCmdResolveImage`는 **컬러 전용**이다. Vulkan 명세 **VUID-VkImageResolve-aspectMask-00266**은
`srcSubresource`/`dstSubresource`의 `aspectMask`가 **`VK_IMAGE_ASPECT_COLOR_BIT`만** 포함해야
한다고 규정한다. 깊이/스텐실에 쓰면 유효하지 않은 사용이다.

깊이 resolve는 **`VK_KHR_depth_stencil_resolve`(Vulkan 1.2 코어)** 의
`VkSubpassDescriptionDepthStencilResolve`로 서브패스에서 처리한다.

**바로 앞 L555에서 저자 스스로 "정수 포맷이나 depth/stencil resolve는 지원 방식과 연산이
다르므로 color 평균과 같은 계약으로 가정하지 않는다"고 hedge**해 두었는데,
L675에서 컬러 전용 API를 깊이에 쓰라고 안내해 앞뒤가 어긋난다.

---

## graphics/stage3_vk_proj_03_pbr.md — 확정 1건

> **반영 상태(2026-08-13): 전건 기반영** — 2026-08-11 백업 대비 diff 에 변경이 없고, 현재 본문이 이미 정정안대로다(2026-08-09 1차 배치에서 선반영).

### L237 — BRDF LUT의 Smith 기하항에서 뷰 마스킹이 통째로 사라짐

```glsl
float G = GeometrySchlickGGX(max(N.z,0.0), k) * GeometrySchlickGGX(NoL, k);
```

이 셰이더에서 **`N = vec3(0,0,1)`로 고정**이므로 `max(N.z, 0.0)`은 **항상 상수 1.0**이고,
`GeometrySchlickGGX(1.0, k) = 1.0/(1.0(1.0-k)+k) = 1.0` 이 되어 **뷰 쪽 마스킹 항이 사라진다.**

즉 $G$가 **L 쪽 한 항만 남아 LUT가 과대평가**되며,
특히 **NoV가 작은 그레이징 각(uv.x → 0)에서 오차가 발산**한다.

실제 뷰 코사인은 `V = vec3(sqrt(1-NoV*NoV), 0.0, NoV)` 이므로 `dot(N,V) = V.z = NoV` 이고,
**바로 위 줄에서 이미 `NoV` 변수를 갖고 있다.** `GeometrySchlickGGX(NoV, k)` 로 고치면 된다.

## graphics/stage2_dx_fund_03_hlsl.md — 확정 8건 (dxc 실측으로 확인)

> **반영 상태(2026-08-13): 전건 기반영** — 2026-08-11 백업 대비 diff 에 변경이 없고, 현재 본문이 이미 정정안대로다(2026-08-09 1차 배치에서 선반영).

> 이 파일의 핵심 오류는 **HLSL cbuffer 패킹 규칙에 GLSL `std140` 규칙을 적용한 것**이다.
> 검증 과정에서 **Windows SDK dxc(10.0.26100.0 x64)로 본문과 동일한 cbuffer를 실제 컴파일해
> 리플렉션을 확인**했다. 하나의 오해가 문서 네 곳으로 번져 있다.

### 1. L242-243 / L249 — cbuffer 오프셋 (실측 불일치)

문서:

```hlsl
float  a;      // offset 0
float3 b;      // 12바이트라 offset 4에 들어가면 16B 경계를 넘음 → offset 16으로 점프
float  c;      // offset 28, 4바이트 (b 다음 빈 4바이트에 들어맞음)
```

**dxc 실측 결과**: `a` → **Offset 0**, `b` → **Offset 4**, `c` → **Offset 16**, `d` → **Offset 32**, `Size: 48`.
DXIL도 `cbufferLoadLegacy regIndex 0/1/2`로 **`a`와 `b`가 같은 레지스터(c0)** 에서 나온다.

HLSL 패킹 규칙은 **"4-vector 경계를 straddle 할 때만 다음 레지스터로 바운스"** 다.
`offset 4 + 12 = 16`은 경계에 **닿을 뿐 넘지 않으므로** `b`는 `c0.yzw`를 정확히 채운다.
따라서 L249의 세 문장 — "offset 4~16이 16바이트 경계를 가로지른다",
"12바이트가 패딩으로 버려진다", "`c`는 offset 28" — 이 **모두 성립하지 않는다.**

**문서 자신이 두 곳에서 반대 사실을 적고 있다:**

- L237 규칙 서술: "더 작은 타입은 16바이트 블록 안에 들어맞으면 같은 블록을 채우지만"
- **L298**: `float3 c : packoffset(c1.y); // c1.yzw — 같은 레지스터의 남은 자리에 채움`

(총 크기 48바이트라는 결론값은 우연히 일치하므로 중간 서술만 고치면 된다.)

### 2. L306 / L577 — HLSL cbuffer와 std140을 같은 규칙이라고 단정

> L306: `std140`에서 `vec3`는 16바이트로 정렬되고 … **cbuffer에서 본 규칙과 사실상 같다.**
>
> L577: `vec3`가 16바이트로 정렬되는 함정은 GLSL std140과 **같은 규칙**이고 …

**배열 원소가 16바이트 경계에 놓이는 규칙은 양쪽이 같지만, `vec3`/`float3` 정렬은 다르다.**
`std140`은 `vec3`에 **16바이트 base alignment를 강제**하므로 `float a; vec3 b;` 에서 `b`가 offset 16으로 밀리지만,
HLSL cbuffer는 **straddle 금지만** 있어 같은 선언에서 `b`가 offset 4(`c0.yzw`)에 들어간다(위 실측).

**이 차이가 HLSL↔GLSL 이식에서 가장 흔한 레이아웃 불일치 원인이며,
DXC가 `-fvk-use-dx-layout` / `-fvk-use-gl-layout` 플래그를 따로 두는 이유다.**
이 절의 잘못된 동일성 주장이 §3 예제(L242-249)의 오프셋 오류를 낳았다.

### 3. L263 — 파생 오류: C++ 레이아웃과 어긋나는 지점

> `b`는 offset 4에 그대로 놓이고 … **`b` 이후의 모든 값**이 엉뚱한 위치에서 읽힌다.

HLSL도 `b`를 offset 4에 놓으므로 **`a`·`b`·`c` 세 멤버는 정확히 일치**한다.
기본 glm 기준 C++는 `a@0, b@4, c@16, d@20`(sizeof 36), HLSL은 `a@0, b@4, c@16, d@32`(48).
**실제로 어긋나는 것은 `d` 하나와 전체 크기**이므로 "`d`부터 어긋난다"가 맞다.

흥미롭게도 **L253-259의 C++ 주석(`c@16`, 보통 `d@20`)은 이미 올바르게 적혀 있어**,
틀린 쪽이 HLSL 측 설명임이 문서 내부에서도 확인된다.
(레이아웃 불일치로 `memcpy`가 위험하다는 절의 결론 자체는 유지된다.)

### 4. L536 — `[RootSignature]`가 없는 블롭을 `CreateRootSignature`에 넘김

```cpp
psShaderBlob->GetBufferPointer(),
```

바로 앞 예제(L524)에서 `[RootSignature(ROOT_SIG)]`를 붙인 진입점은 **정점 셰이더 `VSMain`** 이다.
Root Signature 정보(**RTS0 파트**)는 그 어트리뷰트가 붙은 진입점의 블롭에만 박힌다.

**실측 확인**: 문서 L517-527과 동일한 `ROOT_SIG`/`VSMain`을 dxc로 VS·PS 각각 컴파일해
컨테이너 파트를 확인한 결과 **`vs.cso`에는 RTS0가 있고 `ps.cso`에는 없다.**
문서 어디에도 PS 진입점에 같은 어트리뷰트를 붙였다는 서술이 없으므로,
현재 코드는 `CreateRootSignature`가 **`E_INVALIDARG`로 실패**한다.

### 5. L229 — `SV_DispatchThreadID` 계산식

> `SV_DispatchThreadID`는 … **그룹 크기와 디스패치 크기를 곱해** 계산된다.

"그룹 크기 × 디스패치 크기"는 **전체 스레드 개수(ID 공간의 범위)** 이지 개별 스레드의 ID가 아니다.
실제 정의는 **`SV_DispatchThreadID = SV_GroupID * numthreads + SV_GroupThreadID`** 다.
앞 절반("디스패치 전체에서 이 스레드의 전역 좌표")과 뒤 절반이 서로 맞지 않는다.

### 6. L58 — 어구 파손 (개정 잔재)

> HLSL은 **정수 배열**을 행렬에 채울 때 기본으로 column-major packing을 쓴다.

이 절이 다루는 것은 **상수 버퍼의 16바이트(float4) 레지스터**를 행렬에 채우는 순서이고,
"정수 배열"은 행렬 packing 규칙과 무관하다("상수" → "정수" 오타로 보인다).
뒤 문장("첫 16바이트가 첫 열")과 기본값 `column_major` 자체는 정확하다.

---

## graphics/stage2_dx_proj_01_cube_dx11.md — 확정 1건

> **반영 상태(2026-08-13): 전건 기반영** — 2026-08-11 백업 대비 diff 에 변경이 없고, 현재 본문이 이미 정정안대로다(2026-08-09 1차 배치에서 선반영).

### L465 — 뷰 포맷 재해석의 전제 조건

> SRV의 포맷을 텍스처와 다르게(**단, 같은 비트 폭**) 지정해 같은 비트를 sRGB와 선형 두 시각으로
> 동시에 보는 것도 가능하다.

D3D11에서 뷰 포맷을 자원 포맷과 다르게 지정하려면 **자원이 `_TYPELESS` 포맷으로 생성되어야 한다.**
"같은 비트 폭"은 **필요조건일 뿐 충분조건이 아니다.**

바로 앞 코드(L427)에서 이 텍스처는 `DXGI_FORMAT_R8G8B8A8_UNORM_SRGB`라는 **완전 타입 포맷**으로
만들었으므로, 여기에 `R8G8B8A8_UNORM` SRV를 만들면
`CreateShaderResourceView`가 **`E_INVALIDARG`로 실패**한다
(디버그 레이어: `CREATESHADERRESOURCEVIEW_INVALIDFORMAT`).

---

## graphics/stage2_dx_proj_05_compute_dxr.md — 확정 5건

> **반영 상태(2026-08-13): 전건 기반영** — 2026-08-11 백업 대비 diff 에 변경이 없고, 현재 본문이 이미 정정안대로다(2026-08-09 1차 배치에서 선반영).

### 1. L71 — 1080이 8로 안 나눠떨어진다는 서술 (문서 자신이 반증)

> **1080은 8로 나누어 떨어지지 않으므로** 마지막 그룹의 일부 스레드는 이미지 경계 밖을 가리킨다.

$1080 = 8 \times 135$ 로 **정확히 나누어떨어진다**($1920 \div 8 = 240$ 도 마찬가지).
**바로 위 코드(L64-68)가 `gy = (1080+7)/8 = 135`, 총 스레드 $240 \times 135 \times 64 = 2{,}073{,}600 = 1920 \times 1080$**
으로 **경계 밖 스레드가 0개임을 문서 스스로 증명**하고 있다.

### 2. L458 — refit/재빌드를 가르는 플래그

> 큰 변형이 누적되면 주기적으로 **`ALLOW_UPDATE` 없는** 전체 재빌드로 트리 품질을 회복시키는 것이 표준 절충이다.

refit과 전체 재빌드를 가르는 것은 **빌드 시점의 `..._BUILD_FLAG_PERFORM_UPDATE`** 이고,
`ALLOW_UPDATE`는 **"갱신 가능성"을 여는 최초 빌드 속성**이다
(문서 L451이 "갱신 가능하게 하려면 최초 빌드부터 `ALLOW_UPDATE`가 필요하다"고 정확히 설명한다).

`ALLOW_UPDATE`를 켠 채로도 `PERFORM_UPDATE` 없이 부르면 그것이 곧 전체 재빌드다.
문서 서술대로 **`ALLOW_UPDATE`를 빼고 재빌드하면 그 AS는 이후 refit이 원천 불가**해져
"주기적으로 재빌드하고 사이사이 refit"이라는 서술과 모순된다.

### 3. L526 — 선언되지 않은 식별자

```hlsl
TraceRay(scene, RAY_FLAG_NONE, 0xFF, 0, 1, 0, r, p);   // 반사 광선
```

같은 §6의 L467이 `RaytracingAccelerationStructure sceneTLAS : register(t0);` 로 선언했고
L490의 RayGen도 `sceneTLAS`를 쓰는데, **같은 셰이더 라이브러리의 ClosestHit에서만 `scene`** 을 쓴다.
`scene`은 문서 어디에도 선언되어 있지 않다.

### 4. L119-121 — float를 정수 아토믹에 넘김

```hlsl
float waveSum = WaveActiveSum(value);
if (WaveIsFirstLane())
    groupTotal.InterlockedAdd(0, waveSum);
```

첫 인자가 **바이트 오프셋 0**인 멤버 호출 형태는 `RWByteAddressBuffer`에만 존재하고,
그 `InterlockedAdd` 오버로드는 **`uint`만 받는다**.
`float waveSum`이 **암시적으로 uint로 절단**되어 소수부가 날아가고 합이 조용히 망가진다
(표준 HLSL SM 6.0에는 float 원자 덧셈이 없다). `groupTotal`도 문서 어디에도 선언되어 있지 않다.

### 5. L203 — Wave 크기와 그룹 크기 대응이 반대

> 1차원 디스패치에서 그룹 크기 64는 **한 Wave(32)** 또는 두 Wave를 채우는 무난한 기본값이다.

L113이 Wave를 NVIDIA 32 / AMD 32 또는 64 / Intel 8·16·32로 정의했으므로,
**64스레드 그룹은 32레인 하드웨어에서 두 Wave, 64레인 하드웨어에서 한 Wave**다.
괄호 `(32)`가 "한 Wave"에 붙어 있어 정확히 반대로 읽힌다.

---

## graphics/stage3_vk_proj_01_textured_cube.md — 확정 2건

> **반영 상태(2026-08-13): 전건 기반영** — 2026-08-11 백업 대비 diff 에 변경이 없고, 현재 본문이 이미 정정안대로다(2026-08-09 1차 배치에서 선반영).

### 1. L399 — 배리어 `dstStageMask`의 위험 방향이 반대

> 대표적으로 dst stage를 `FRAGMENT_SHADER` 대신 **너무 이른 `VERTEX_SHADER`로 잡거나**,
> dstAccess를 비워 두면 GPU가 복사가 끝나기 전에 텍스처를 읽어 …

Vulkan 명세(Synchronization, "Pipeline Stages"): **동기화 명령의 두 번째 동기화 범위는
`dstStageMask`에 지정된 스테이지와 그보다 논리적으로 이후의 모든 스테이지를 포함한다.**

따라서 dst를 더 이른 `VERTEX_SHADER`로 잡으면 `FRAGMENT_SHADER`까지 **함께 포함되어
오히려 과도하게 보수적인(느린) 배리어**가 되며, **동기화 누락(hazard)은 발생하지 않는다.**
dst 쪽의 위험한 실수는 "너무 이르게"가 아니라 **"너무 늦게"** 잡는 것이다.
(`dstAccess`를 비워 두는 쪽 지적은 정확하다.)

### 2. L656 — 링크 제목과 URL이 가리키는 문서가 다름

> [Vulkan Guide: **Descriptor Sets**](https://docs.vulkan.org/guide/latest/descriptor_dynamic_offset.html) — 디스크립터 모델 공식 해설.

URL을 실제로 확인한 결과 `descriptor_dynamic_offset.html`의 제목은 **"Descriptor Dynamic Offset"** 이고,
내용은 `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC`/`STORAGE_BUFFER_DYNAMIC`의
바인드 시점 오프셋, `VK_WHOLE_SIZE` 동작, 정렬 한계 등 **"동적 오프셋"이라는 좁은 주제에 국한**된다.
"디스크립터 모델 공식 해설"에 해당하는 일반 문서가 아니다.

---

## graphics/stage3_vk_proj_04_shadow.md — 확정 5건

> **반영 상태(2026-08-13): 전건 기반영** — 2026-08-11 백업 대비 diff 에 변경이 없고, 현재 본문이 이미 정정안대로다(2026-08-09 1차 배치에서 선반영).

### 1. L258 — Vulkan에 24비트만 쓰는 깊이 포맷은 없다

> 24비트 정규화 깊이(`D24`)보다 **메모리는 33% 늘지만** …

`VK_FORMAT_X8_D24_UNORM_PACK32`는 이름 그대로 **PACK32**, `VK_FORMAT_D24_UNORM_S8_UINT`도
**텍셀당 32비트(4바이트)** 다. `D32_SFLOAT`로 바꿔도 **VRAM은 동일**하며 33% 증가하지 않는다.
**같은 문서의 표(L50-54)도 D32를 4바이트/텍셀로 계산**하고 있다.

### 2. L84 — 로그 분할 예시 값

> $n$을 0.1m처럼 작게 잡으면 첫 캐스케이드가 **0.1m–0.6m** 처럼 비현실적으로 좁아져

같은 문단이 확정한 예($f = 500$m, $N = 4$)에 로그 분할식 $z_i = n(f/n)^{i/N}$ 을 적용하면
$z_1 = 0.1 \times 5000^{0.25} = 0.1 \times 8.409 = 0.841$ m 다. 0.6m가 아니다(0.6m는 $f=100$m일 때).

> 참고 — 같은 문단의 $n=1$ 기준 값 4.7 / 22.4 / 105.7은 정확하다
> ($500^{0.25}=4.729$, $500^{0.5}=22.36$, $500^{0.75}=105.74$).

### 3. L110 / L455 — 분할 경계 배열의 인덱스가 한 칸 밀림

§2의 `ComputeCascadeSplits`는 `std::array<float,5>`를 돌려주고
`splits[0] = n`(카메라 near), `splits[1..3]`이 실제 경계, `splits[4] = f` 다.
L110은 **"분할 경계 배열을 그대로 유니폼으로 GPU에 넘긴다"** 고 못박는다.

그런데 §6 셰이더는 `cascadeSplits[i]`를 **"캐스케이드 $i$의 far 경계"** 로 취급한다:

```glsl
if (viewDepth < cascadeSplits[i]) { cascade = i; break; }
```

그대로 넘긴 5원소 배열에서는 `cascadeSplits[0] = n = 1`m이므로
**`viewDepth < 1m`인 픽셀만 cascade 0**이 되어 캐스케이드 선택 전체가 한 칸 밀린다.

### 4. L141 — 주석 두 줄이 바로 아래 코드와 다름

> ```
> // 1. 이 캐스케이드 구간의 절두체 코너를 view 공간에서 구성한다.
> //    camProj의 near/far를 이 구간으로 바꾼 투영의 역행렬을 NDC 코너에 적용한다.
> ```

- 코드는 `glm::inverse(camProj * camView)`를 곱하므로 결과는 view 공간이 아니라 **월드 공간**이고,
  **문서 자신이 변수명을 `worldCorners[8]`로 붙여 놓았다.**
- 코드는 `camProj`의 near/far를 구간 값으로 **바꾸지 않고** 원본을 그대로 역변환한 뒤
  L162-169에서 광선 보간으로 잘라낸다.
  **L159 본문도 "코너를 전체 절두체로 잡았다면, 이를 캐스케이드 구간의 비율로 보간해
  sub-frustum을 만든다"로 코드와 같은 방식을 서술**해, 주석만 다른 방식을 가리킨다.

### 5. L609 / L617 — normal offset bias라 설명하고 깊이 바이어스를 구현

> 표면을 **노멀 방향으로 약간 밀어 receiver를 빛 쪽으로 옮기는** 보강이 있다
> … 노멀 바이어스는 **receiver 위치**의 미세 보정을 담당

L611-614 코드는 **위치를 전혀 건드리지 않고** `compareDepth = lightSpacePos.z - max(0.05*(1-NdotL), 0.005)`
로 **비교 깊이만 뺀다.** 이는 $N\cdot L$ 로 스케일한 **깊이 바이어스**이지 노멀 방향 위치 이동이 아니다.
두 기법은 결과도 다르다 — 위치를 $N$ 방향으로 $d$만큼 밀면 라이트 공간 깊이 변화는 $d(N\cdot L)$ 이다.
L609의 "하드웨어 바이어스가 깊이를 미는 것과 **별개로**"도 코드와 어긋난다(코드 역시 깊이를 민다).

---

## graphics/stage3_vk_proj_05_compute.md — 확정 6건 (glslangValidator 실측)

> **반영 상태(2026-08-13): 전건 기반영** — 2026-08-11 백업 대비 diff 에 변경이 없고, 현재 본문이 이미 정정안대로다(2026-08-09 1차 배치에서 선반영).

### 1. L197-199 / L212 — std140에서 `vec3` 뒤 `float`의 오프셋

> ```glsl
> vec3  pos;       // offset 0,  점유 12, 다음 멤버는 16에 정렬
> float scale;     // offset 16
> ```
>
> | `vec3` 뒤 `float` 오프셋 | **16** | 12 (남는 자리) |

**std140에서도 12다.** std140/std430 모두 **스칼라 멤버의 정렬 요구는 4바이트**이고,
앞 멤버가 소비한 마지막 바이트 다음(=12)을 자기 base alignment(4)로 올림하면 **12**가 된다.

std140이 vec4로 올림하는 대상은 **(a) 배열 원소의 base alignment/ArrayStride,
(b) 구조체의 base alignment와 크기**뿐이지 **구조체 내부 스칼라 멤버의 오프셋이 아니다**
(OpenGL 4.6 §7.6.2.2 / Vulkan Standard Uniform Buffer Layout).
glslangValidator(Vulkan SDK 1.4.350.0) 실측에서도 SPIR-V가 offset 12로 데코레이트한다.

### 2. L486-489 — 확장 하나가 빠져 컴파일 실패 (실측)

```glsl
#extension GL_EXT_shader_atomic_int64 : require
layout(std430, binding = 0) buffer Counter { uint64_t count; };
```

`GL_EXT_shader_atomic_int64`는 64비트 아토믹 **연산**만 추가할 뿐 **`uint64_t` 타입을 도입하지 않는다.**
**실측**: `glslangValidator -V --target-env vulkan1.2`로 그대로 컴파일하면
`ERROR: 4: '' : syntax error, unexpected IDENTIFIER` 로 죽는다.
`#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require` 를 함께 켜야 한다.

### 3. L553-566 — 문서가 스스로 세운 규칙을 예제가 어김 (실측)

L550이 **"서브그룹 연산은 종류별로 GLSL 확장이 나뉘어 있어, 쓰는 만큼만 활성화한다"** 고 해 놓고,
정작 예제가 활성화하지 않은 확장의 함수를 호출한다.

**실측**: 문서의 확장 3줄(arithmetic/ballot/vote)로 컴파일하면
`ERROR: 14: 'subgroupShuffle' : required extension not requested: GL_KHR_shader_subgroup_shuffle`.

### 4. L587 — 서브그룹 합이 실제로 일어나지 않음

> | 서브그룹 합 + atomic | `if (subgroupElect) atomicAdd(out, subgroupAdd(v));` | 가장 적은 전역 접근 |

두 가지가 틀렸다.

- **문법**: `if (subgroupElect)` 는 함수 지시자를 조건식에 쓴 것 →
  실측 `ERROR: 'subgroupElect' : undeclared identifier`. `subgroupElect()` 여야 한다.
- **의미(더 본질적)**: SPIR-V의 `GroupNonUniform*Reduce`(= `subgroupAdd`)는
  **그 명령을 실제로 실행하는 활성 invocation에 대해서만** 정의된다.
  `subgroupElect()` 분기 **안에서** 호출하면 활성 레인이 선출된 하나뿐이라
  **결과가 서브그룹 전체 합이 아니라 그 레인의 `v` 값 하나**가 된다. 총합이 틀린다.

**같은 문서 L469-473의 올바른 구현**은 `subgroupAdd`를 분기 **밖에서** 먼저 계산하고
그 결과만 elect 분기 안에서 `atomicAdd` 하므로, 표만 어긋난다.

### 5. L501 — `memoryBarrierBuffer`의 가시성 범위

> | `memoryBarrierBuffer` | **워크그룹**의 스토리지 버퍼 접근 | GLSL |

표의 열 제목이 "가시성 범위"인데 `memoryBarrierBuffer`의 범위는 **워크그룹이 아니라 디바이스**다.

**실측**(spirv-dis): `memoryBarrierBuffer()` → `OpMemoryBarrier %uint_1 %uint_72`
(**Scope = Device(1)**, UniformMemory\|AcquireRelease),
`groupMemoryBarrier()` → `OpMemoryBarrier %uint_2 %uint_3400` (**Scope = Workgroup(2)**).
워크그룹 범위를 갖는 것은 `groupMemoryBarrier` 쪽이다.

---

## 검증에서 기각된 것 (참고)

> **반영 상태(2026-08-13): 전건 기각 유지 — 고치면 안 되는 항목이다.** 여기 실린 오탐이 걸린 네 파일(`stage2_dx_fund_03`, `stage2_dx_proj_01`, `stage2_dx_proj_05`, `stage3_vk_proj_04`)은 2026-08-11 백업 대비 diff 가 **한 hunk도 없어**, 오탐 정정이 본문에 들어가지 않았음을 확인했다.

미커버 6편에서 1차 발견 39건 중 **13건이 적대적 검증에서 기각**됐다. 대표 사례:

- `stage2_dx_fund_03` L374 — 실측(dxc)으로 문서가 맞음이 확인됨
- `stage2_dx_proj_01` L526 — 검수자의 전제(D3D CW/CCW 라벨과 화면 방향이 반대)가 틀렸음
- `stage2_dx_proj_05` L606-608 — 문서가 L383에서 모든 지오메트리를 `GEOMETRY_FLAG_OPAQUE`로
  지정했으므로 단발 `Proceed()`가 정확함
- `stage3_vk_proj_04` L76 — 균등 분할 근사값(125/250/375 vs 정확값 125.75/250.5/375.25,
  오차 0.6%/0.2%/0.07%)이라 오류가 아님
- `stage3_vk_proj_04` L203 — 검수자가 요구한 정정 내용을 문서가 바로 앞 문장에서 이미 말하고 있음

---

## 반영 상태 (2026-08-13)

2026-08-13에 수정 전 원본(`VeLog_backup_2026-08-11/graphics/`)과 현재 본문(`VeLog/graphics/`)을
기계적으로 대조하고, 항목마다 현재 파일을 직접 열어 확인해 기록한 절이다. 판정 근거는 두 가지다.

```
diff -ru --strip-trailing-cr VeLog_backup_2026-08-11/graphics VeLog/graphics \
     -x "REVIEW_*" -x "REDESIGN_*" -x diagrams -x build
```

- **diff 결과** — 2026-08-11 이후 실제로 바뀐 줄
- **현재 본문 확인** — diff 에 없더라도 지금 파일이 정정안대로인지

### 집계

확정 지적은 소절(`###`) 단위로 **64건**이다. 각 파일 머리말이 선언한 "확정 N건"의 합계 71건은
한 소절이 2~3곳을 함께 지적한 경우를 위치 단위로 센 값이므로, 아래 집계는 소절 단위로 센다.

| 분류 | 수 | 판정 근거 |
|---|---|---|
| **반영됨** (2026-08-13 배치에서 본문이 바뀜) | **2** | diff 에 대응 hunk 존재 |
| **기반영** (보고서 작성 후 이미 반영돼 있던 것) | **62** | diff 무변경 + 현재 본문이 정정안대로임을 파일에서 확인 |
| **기각** (고치면 안 되는 오탐) | **5** (본문 선언 총 13건 중 대표 사례) | 해당 4개 파일 diff 전무 → 오탐이 본문에 반영되지 않음 |
| **이월** (아직 남은 일) | **0** | 미해결로 남은 확정 항목 없음 |

### 이월 항목 — **없다**

64건 전부가 본문에 반영돼 있다. 이 보고서에서 다음 사람이 이어받아야 할 작업은 남아 있지 않다.
구조 개편·기능 증설을 요구해 미룬 항목도 없다. 확인하지 못해 판정을 보류한 항목도 없다 —
64건 모두 현재 파일의 해당 위치를 직접 열어 대조했다.

**[2026-08-13 후속 처리]** (2026-08-14) — 이 보고서의 이월은 **여전히 0건**이다. 다만
`stage1_gl_fund_04_texture.md` 10절이 **graphics 내부 교차 대조 보고서**
([`REVIEW_CROSS_GRAPHICS_2026-08.md`](../REVIEW_CROSS_GRAPHICS_2026-08.md#이월--1건-아직-남은-일))
의 이월 1건으로 남아 있다가 이번에 닫혔다. 아래 「주의 — diff 에는 이 보고서와 무관한 변경이 함께 들어 있다」
목록에 같은 파일의 L596 이 이미 올라 있으므로, **같은 파일의 새 변경을 그 목록과 혼동하지 않도록** 여기 적어 둔다.

- **바뀐 곳**: 10절 통합 예제의 해설 문단(현 L783). `glGenerateMipmap` 호출 순서 설명 뒤에 한 문장이 붙었다 —
  이 예제가 `sRGB = true` 경로에도 `glGenerateMipmap` 을 그대로 쓰는 것은 흐름을 한 함수로 보이기 위한
  **단순화**이고, 배포용 sRGB 디퓨즈의 밉은 4절·8절에서 말한 대로 선형 공간에서 디코드→필터→인코드해
  오프라인으로 만든 뒤 레벨별로 업로드해야 한다는 단서다. (diff `@@ -780,7 +780,7 @@`)
- **택한 것은 (a) 한 문장 단서다.** (b) `sRGB == true` 경로에서 밉 생성을 분리하도록 예제 코드를 고치는 길은
  택하지 않았다 — 10절 예제의 목적이 "텍스처 적재 전 과정을 한 함수로 보이는 것" 이라 여기에 분기를 넣으면
  그 목적이 깨지고, 올바른 절차는 4절·8절이 이미 설명하고 있어 중복이 된다.
  코드 블록(L772~776)은 손대지 않았다.

### 반영됨 2건 (2026-08-13 배치)

| 파일 | 지적 | 이번에 바뀐 것 |
|---|---|---|
| `stage0_fund_03_rasterization.md` | 3. L216 / L245 — 선언한 필드 목록에 없는 `.z` 사용 | L213 본문 가정에 "NDC 깊이 $z$" 추가. 코드 주석의 `.x .y .z .w .attr`는 이미 선반영돼 있었으므로 이번 변경으로 소절이 마무리됨 |
| `stage2_dx_fund_04_dx12.md` | L223 (및 L198 본문) — `ID3D12GraphicsCommandList`에 없는 메서드 | L198 본문의 `DrawIndexed` → `DrawIndexedInstanced`. L223 코드는 이미 선반영돼 있었음 |

### 보고서가 낡아 있었다는 기록

**64건 중 62건(97%)은 2026-08-11 백업 시점에 이미 본문에서 고쳐져 있었다.**
그런데도 본문의 지적들은 미해결인 것처럼 쓰여 있었다. 머리말이 "반영 상태(2026-08-09)"라고
한 줄로만 적어 두고 어느 항목이 반영됐는지는 남기지 않았기 때문이다.
그 결과 2026-08-13 반영 작업은 "보고서가 인용한 문장이 파일에 없다"를 대량으로 만났고,
어느 것이 실제로 남은 일인지 가려내는 데 시간이 들었다. 이 절은 같은 혼란을 반복하지 않으려고 남긴다.

기반영 62건의 파일별 분포:

| 파일 | 기반영 | 비고 |
|---|---|---|
| `stage0_fund_03_rasterization.md` | 2 | 1·2번(엣지 함수 증분 부호, $\delta z\approx\delta z_d f^2/n$) |
| `stage0_fund_04_color_gamma.md` | 1 | |
| `stage0_fund_09_geometry_mesh.md` | 2 | 감김 순서 그림 `v1——v2`, 절 번호 10/11/10절 |
| `stage0_fund_05_shading_materials.md` | 2 | 상호참조 9/10/11/12절, Burley 중복 제거 |
| `stage0_fund_08_framebuffer_multipass.md` | 1 | Wolfgang Engel |
| `stage0_adv_03_global_illumination_raytracing.md` | 1 | RTG II 편집진 분리 |
| `stage0_fund_02_coordinate_transform.md` | 1 | `Mat4` 예고 삭제 |
| `stage1_gl_fund_01_context.md` | 2 | 초기화 순서, `glGenBuffers`(1.5) |
| `stage1_gl_fund_02_transform.md` | 1 | ES 2.0 한정 |
| `stage1_gl_fund_04_texture.md` | 2 | `GL_NEAREST_MIPMAP_LINEAR`, bindless 비코어 |
| `stage1_gl_fund_05_framebuffer.md` | 2 | 100m→6mm, `glDrawBuffers` 재배치 불가 |
| `stage1_gl_proj_01_cube.md` | 2 | 64만/초당 3,840만, near 0.01 |
| `stage1_gl_proj_02_model.md` | 2 | 64MiB·BC7 16MiB, `mNumIndices != 3` 필터 |
| `stage1_gl_proj_04_shadow.md` | 2 | `INCOMPLETE_DRAW_BUFFER`, 정규화/월드 열 분리 |
| `stage1_gl_proj_05_postprocess.md` | 2 | IDT→RRT→ODT, clamp 이유 |
| `stage2_dx_fund_01_com.md` | 2 | `context1->ClearState()`, FLIP_DISCARD Windows 10 |
| `stage2_dx_fund_02_resource.md` | 1 | `ArraySize`/`WSize` |
| `stage2_dx_proj_02_model_dx11.md` | 4 | SimpleMath RH, `Draw` 인자 순서, BC5 sRGB 변종 없음, DirectXTex |
| `stage2_dx_proj_04_dx12_transition.md` | 3 | `ID3D11Device`, 명시적 `rtvDesc`, 표 마지막 세 행 |
| `stage3_vk_fund_01_pipeline.md` | 1 | `OpKill`/`OpTerminateInvocation` |
| `stage3_vk_proj_02_model_loading.md` | 1 | color aspect 전용 명시 |
| `stage3_vk_proj_03_pbr.md` | 1 | `GeometrySchlickGGX(NoV,k)` |
| `stage2_dx_fund_03_hlsl.md` | 6 | cbuffer 오프셋 `b@4`/`c@16`, std140 차이, `d`부터 어긋남, `vsShaderBlob`, `SV_GroupID*numthreads+SV_GroupThreadID`, "상수 데이터" |
| `stage2_dx_proj_01_cube_dx11.md` | 1 | `_TYPELESS` 전제 |
| `stage2_dx_proj_05_compute_dxr.md` | 5 | 1080 정확히 나눠떨어짐, `PERFORM_UPDATE`, `sceneTLAS`, float 아토믹 제거(`wavePartials`), 64레인=한 Wave |
| `stage3_vk_proj_01_textured_cube.md` | 2 | dst stage 방향, 링크 제목 |
| `stage3_vk_proj_04_shadow.md` | 5 | 33% 부정, 0.84m, `gpuCascadeFar[i]=splits[i+1]`, 월드 공간 주석, slope-scaled depth bias 명시 |
| `stage3_vk_proj_05_compute.md` | 5 | std140 offset 12, `explicit_arithmetic_types_int64`, `subgroup_shuffle` 확장, `subgroupElect()` 분기 밖, 디바이스 범위 |

### 주의 — diff 에는 이 보고서와 무관한 변경이 함께 들어 있다

2026-08-13 diff 는 graphics/ 12개 파일을 건드리지만, 그중 **이 보고서의 지적에 대응하는 것은
위 2건뿐**이다. 나머지는 이 보고서 이후의 별도 검수에서 나온 변경이므로 여기 집계에 넣지 않았다.
다음 사람이 같은 diff 를 보고 혼동하지 않도록 목록을 남긴다.

- `stage0_adv_02_compute_gpgpu.md` L161 — 발산 설명을 warp/wave/subgroup 중립 서술로
- `stage0_fund_03_rasterization.md` L205 — reverse-Z가 "전 구간 균일"이 아니라는 단서 추가
- `stage1_gl_fund_04_texture.md` L596 — sRGB 자동 변환 계약이 샘플러 경로 한정임을 명시
- `stage1_gl_fund_05_framebuffer.md` L238·L253·L255 — 32F reverse-Z의 `glClipControl` 전제
- `stage1_gl_proj_01_cube.md` L291·L356·L630·L672 — MVP 표기 `P×V×M`, `transpose` 설명, 깊이 표 값, 클립 깊이 설명
- `stage1_gl_proj_05_postprocess.md` L392·L568 — 확장 Reinhard `clamp`, `GL_FRAMEBUFFER_SRGB` 전제 조건
- `stage2_dx_proj_03_pbr_dx11.md` L264·L280 — IBL 디퓨즈의 $k_D=(1-F)(1-\text{metallic})$
- `stage2_dx_proj_04_dx12_transition.md` L517 — 배리어 배칭 효과를 드라이버 재량으로 완화
- `stage3_vk_fund_01_pipeline.md` L185 — GL 투영을 그대로 쓰면 앞쪽 절반이 클립됨
- `stage3_vk_fund_02_vulkan_core.md` L363 — `vkResetFences`를 acquire 성공 뒤로 이동
- `stage3_vk_proj_03_pbr.md` L61·L133·L218·L274 — IBL $k=\alpha/2$, 프리필터 밉 체인, split-sum에 $F_0$
