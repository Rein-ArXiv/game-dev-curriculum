# graphics_gl_lab - graphics 시리즈 OpenGL 단계의 원리 측정·시각화

## 컨셉

이 lab은 **동일한 장면을 OpenGL의 여러 렌더 모드로 그려놓고 CPU/GPU 비용 차이를 측정**한다. 1만 오브젝트로 구성된 고정 씬을 두고, 드로우콜 방식, 셰이더 분기 전략, 메모리 바인딩 방식, 텍스처 압축, MSAA를 토글한다. 토글이 바뀌는 순간 ms/frame과 GPU 시간이 어떻게 갈라지는지 확인한다.

## 짝 시리즈

- **graphics Stage 1 — OpenGL** - 버퍼/VAO·UBO/SSBO 바인딩, 텍스처 압축 포맷, 인스턴싱의 드로우 제출 비용을 다루는 글의 주장을 이 lab이 측정으로 확인한다. 대응 문서 목록은 [`graphics/README.md`](../../../graphics/README.md) 의 'Stage 1 — OpenGL' 표.
- 축 문서 중 **프레임버퍼 렌더 타깃·그림자·후처리(HDR/톤 매핑/블룸)·모델 로딩·변환 행렬** 은 이 lab의 범위 밖이다. lab의 프레임버퍼 사용은 MSAA multisample 렌더버퍼 한 가지뿐이고, 텍스처에 그려 다시 입력으로 쓰는 멀티패스는 구현하지 않는다.
- 반대로 **셰이더 uber/permutation 토글은 대응 축 문서가 없다.** stage1 OpenGL 10편 어디에도 permutation·uber 주제가 없고, 관련 언급은 [`stage0_fund_05_shading_materials.md`](../../../graphics/stage0_fund_05_shading_materials.md) 의 셰이더 변형 한 문단뿐이다.

## 토글 항목

| 항목 | 옵션 | 비교 대상 |
|---|---|---|
| 드로우콜 | 개별 / Instancing / Multi-Draw Indirect | [stage1_gl_proj_05_postprocess.md](../../../graphics/stage1_gl_proj_05_postprocess.md) 인스턴싱 절 |
| 셰이더 | branchy uber / static permutation | 대응 문서 없음 |
| 메모리 바인딩 | per-call Uniform / UBO / SSBO | [stage1_gl_fund_03_pipeline.md](../../../graphics/stage1_gl_fund_03_pipeline.md) 7~8절 (UBO/SSBO) |
| 텍스처 압축 | none / DXT1 / DXT5 / BC7 | [stage1_gl_fund_04_texture.md](../../../graphics/stage1_gl_fund_04_texture.md) 9절 (압축 포맷) |
| 오브젝트 수 | 100 / 1k / 10k / 100k | 스케일링 |
| MSAA | off / 2x / 4x / 8x | 픽셀 비용 |

## 출력

- 메인 뷰포트 - 여러 큐브가 회전하는 고정 씬.
- ms/frame 그래프 - CPU side / GPU side 분리.
- 드로우콜 카운트 - 프레임당 `glDraw*` 호출 수.
- GPU 타임 측정 - `GL_TIME_ELAPSED` 쿼리로 렌더 구간 전체 GPU 시간 측정.
- 텍스처 메모리 - 각 mip level 의 `GL_TEXTURE_COMPRESSED_IMAGE_SIZE` 합산.
- 디버그 출력 - `KHR_debug` 콜백 메시지를 ImGui 로그 창에 표시.

## 기술 스택

- OpenGL 4.5 Core (Multi-Draw Indirect, SSBO, BPTC/BC7).
- GLFW (윈도우/입력).
- glad2 (OpenGL 로더 생성).
- Dear ImGui + ImPlot.
- glm (벡터/행렬).
- CMake 3.20+.

## 디렉토리 구조

```text
graphics_gl_lab/
  CMakeLists.txt
  src/
    main.cpp
    app/
      app.h / app.cpp              메인 애플리케이션
    scene/
      scene.h / scene.cpp          오브젝트 생성 + Individual/Instanced/MDI
    render/
      shader_program.h / .cpp      셰이더 컴파일/링크 + uniform cache
      shader_registry.h / .cpp     uber/static/lit/textured shader 선택
      lighting_bindings.h / .cpp   per-call Uniform / UBO / SSBO
      texture_modes.h / .cpp       프로시저 텍스처 + 압축 internalFormat
      msaa_target.h / .cpp         multisample FBO + resolve
    measure/
      frame_timer.h / .cpp         CPU frame timer
      gpu_timer.h / .cpp           GL_TIME_ELAPSED ring buffer
      khr_debug.h / .cpp           GL debug callback ring log
      gl_counters.h                draw/state/uniform counter
    ui/
      panel.h / .cpp               ImGui + ImPlot 패널
  shaders/
    common.vert
    uber.frag
    perm_*.frag
    lit_*.frag
    textured.frag
    basic.vert / basic.frag        Step 1 상태 재현용. ShaderRegistry 는 로드하지 않는다
  articles/
    step01_skeleton.md ... step07_scale_msaa.md
```

`basic.vert` / `basic.frag` 는 Step 1 의 단일 셰이더 상태를 그대로 다시 만들어 볼 수 있도록 남겨 둔 파일이다. 최종 실행 경로가 쓰는 셰이더는 `common.vert` + `uber.frag` / `perm_*.frag` / `lit_*.frag` / `textured.frag` 아홉 개이며, `shaders/` 디렉터리 전체가 빌드 출력 옆에 복사되므로 `basic.*` 도 함께 복사된다.

## 빌드와 실행

```bash
# Linux / macOS
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/graphics_gl_lab

# Windows (Visual Studio generator)
cmake -S . -B build
cmake --build build --config Release
build\Release\graphics_gl_lab.exe
```

GPU 드라이버에서 OpenGL 4.5 Core가 필요하다. Intel iGPU도 최근 드라이버에서 가능하다. 측정 정확도를 위해 기본값은 Vsync OFF(`glfwSwapInterval(0)`)이며, UI에서 다시 켤 수 있다.

의존성(GLFW, glad2, ImGui, ImPlot, glm)은 *FetchContent* 로 자동 다운로드된다. 첫 빌드는 다운로드 + 빌드로 수 분 걸릴 수 있고 이후에는 캐시를 사용한다.

*Python 3 + venv 모듈* 이 시스템에 필요하다. glad2가 빌드 시점에 OpenGL 로더 코드를 생성할 때 Jinja2를 사용하기 때문이다. `CMakeLists.txt`가 build 디렉토리 안에 전용 venv를 만들고 Jinja2를 설치하므로 시스템 Python 환경은 오염되지 않는다.

## 진행 상태

- [x] **Step 1 골격** - 윈도우 + 1만 오브젝트 fixed scene + 기본 셰이더 + ImGui/ImPlot + CPU 프레임 타이머.
- [x] **Step 2 측정 인프라** - `GL_TIME_ELAPSED` GPU 타이머, 드로우콜/상태/유니폼 카운터, `GL_KHR_debug` 로그 창.
- [x] **Step 3 드로우콜 토글** - Individual / Instanced / Multi-Draw Indirect. 셰이더 한 벌로 세 모드 처리.
- [x] **Step 4 셰이더 perm** - Uber runtime branch vs static permutation. Lambert / Blinn-Phong / Cel 세 모델.
- [x] **Step 5 메모리 바인딩** - per-call Uniform / UBO(std140) / SSBO(std430). 광원 수 슬라이더.
- [x] **Step 6 텍스처 압축** - Uncompressed / DXT1 / DXT5 / BC7. 512² 프로시저 텍스처, Triplanar 매핑, mip chain 메모리 비교.
- [x] **Step 7 스케일링 + MSAA** - 100 / 1k / 10k / 100k 오브젝트 + MSAA Off/2x/4x/8x.

각 Step 의 자세한 설계는 [`articles/`](articles/) 의 markdown 글에 들어간다.

## 자동 테스트

```bash
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

실행 파일은 두 개고, CTest 항목은 **12 개**다 — 전체를 도는 두 개 + step 별 열 개. 두 실행 파일 모두 첫 인자를 필터로 받으므로(`graphics_gl_contract_tests step05`) step 별 항목은 새 실행 파일 없이 `add_test` 만 나뉜 것이다. step 문서의 검증 절은 자기 step 의 항목만 부른다.

```bash
ctest --test-dir build -C Release -R gl_step05 --output-on-failure   # step05 가 추가한 것만
```

| 항목 | 실행 파일 | 도는 검증 |
|---|---|---|
| `gl_step01_skeleton` | contract | `camera_matrices`, `frame_timer_history` |
| `gl_step02_measure` | contract | `counter_reset`, `debug_message_ring` |
| `gl_step03_drawcall_contract` | contract | `draw_submission_counts`, `indirect_commands` |
| `gl_step03_drawcall_render` | render | `draw_mode_capture` |
| `gl_step04_shader_perm` | render | `shader_permutations` |
| `gl_step05_binding_contract` | contract | `binding_cost_model`, `counted_binding_cost`, `per_call_uniform_names` |
| `gl_step05_binding_render` | render | `binding_counters` |
| `gl_step06_texture_contract` | contract | `mip_chain_bytes`, `block_bytes` |
| `gl_step06_texture_render` | render | `texture_vram` |
| `gl_step07_scale_msaa` | render | `scene_rebuild_handles`, `msaa_fallback` |

두 실행 파일 다 `scene.cpp` / `lighting_bindings.cpp` / `texture_modes.cpp` / `msaa_target.cpp` / `shader_program.cpp` / `khr_debug.cpp` / `frame_timer.cpp` 를 **앱과 같은 소스로 함께 컴파일**한다. 헤더의 예측 함수만 검사하던 이전 구성과 달리, 제품 코드를 망가뜨리면 테스트가 실제로 죽는다.

### `graphics_gl_contract_tests` — GL 컨텍스트 불필요

| 검사 대상 | 위치 | 내용 |
|---|---|---|
| `BuildIndirectCommands` | `src/scene/scene.cpp` | MDI 커맨드 배열의 길이·`count`·`instanceCount`·`baseInstance` |
| `ComputeCamera` | `src/scene/scene.cpp` | 원점의 NDC 중심 사상, 깊이 부호와 단조성, 격자 최외곽 큐브의 클립 볼륨 포함, aspect 가 `proj[0][0]` 만 바꾼다는 성질 |
| `FrameTimer` | `src/measure/frame_timer.cpp` | EMA 씨앗 대입과 `α = 0.1` 혼합식, 240 슬롯 히스토리 링의 머리 회전 |
| `GlCounters::Reset` | `src/measure/gl_counters.h` | 일곱 칸이 모두 0 으로 돌아가는가(프레임 경계) |
| `KHRDebug` 링 | `src/measure/khr_debug.cpp` | 256 슬롯을 넘겨도 개수가 멈추고 최근 것만 남는가, 앱 메시지의 source/severity |
| `CountBindingCost` / `BuildPerCallUniformNames` | `src/render/lighting_bindings.cpp` | 바인딩 모드별 uniform/buffer 갱신 수(`1 + 2N` vs `{1, 1}`)와 per-call uniform 이름 목록 |
| `BlockBytesFor` | `src/render/texture_modes.cpp` | 포맷별 블록 바이트와 mip chain 합산, DXT1 = 1/8 · DXT5/BC7 = 1/4 압축비 |
| `ExpectedDrawSubmissions` / `ExpectedBindingCost` / `ExpectedMipChainBytes` | 각 헤더 | 위 구현과 맞대는 예측 모델 |

### `graphics_gl_render_tests` — 실제 OpenGL 4.5 컨텍스트 필요

히든 GLFW 윈도우로 컨텍스트를 만든다. 만들 수 없는 환경에서는 77 로 종료하고 CTest 가 `Skipped` 로 표시한다.

- **렌더 캡처 회귀** — 256×256 오프스크린 FBO 에 같은 64 오브젝트 씬을 Individual / Instanced / Multi-Draw Indirect 로 그려 `glReadPixels` 결과의 FNV-1a 해시가 셋 다 같은지 검사한다. '아무것도 안 그림' 이 3-way 일치로 통과하지 않도록 클리어만 한 프레임의 해시와도 대조한다.
- **자원 수명** — `Scene::Rebuild` 를 1000 회 반복한 뒤 핸들이 유효한지, 드라이버가 발급한 이름 값이 누적되지 않는지(누수 시 1000 단위로 올라간다), GL 에러와 `KHR_debug` HIGH 메시지가 0 건인지 확인한다.
- **동기화·폴백 상태** — `MsaaTarget` 이 4x 를 정상 생성하는 경로와, `GL_MAX_RENDERBUFFER_SIZE` 초과 요청으로 FBO 를 불완전하게 만든 경로를 둘 다 밟아 폴백 후 `Current()`/`Requested()` 가 Off 이고 폴백 통지가 정확히 한 번만 소비되는지 검사한다.
- **런타임 카운터 대조** — `LightingBindings::Apply` 를 실제 셰이더에 대해 호출해 `GlCounters` 증가분이 `CountBindingCost` 예측과 일치하는지, `TextureRegistry` 가 드라이버에서 읽은 mip chain 바이트가 `ExpectedMipChainBytes` 와 일치하는지(어긋나면 로그 통지가 남는지) 확인한다.
- **셰이더 perm 구성** — `ShaderRegistry::Pick` 이 uber 에서는 세 모델에 같은 프로그램을, static permutation 에서는 서로 다른 세 프로그램을 돌려주는지, `uShadingModel` uniform 이 uber 에만 있는지(`SetInt` 반환값), `ProgramChangedSinceLast` 가 전환을 정직하게 잡는지 검사한다.

`ctest` 출력에 GPU 이름과 세 모드의 픽셀 해시, 재빌드 후 핸들 값, 포맷별 VRAM 실측치가 그대로 찍힌다. 세 드로우 모드의 CPU/GPU 시간 분포처럼 값 자체가 머신마다 달라지는 항목은 여전히 실행 후 패널로 확인한다.

## 시리즈 글과의 관계

이 lab이 측정으로 확인하는 것은 "Instancing이 개별 드로우 제출 비용을 크게 줄인다"(→ CPU ms·드로우콜 카운터), "UBO/SSBO 한 번의 버퍼 갱신이 광원 수에 비례하는 per-call uniform 호출을 대체한다"(→ uniform updates 카운터), "DXT1은 1/8, DXT5/BC7은 1/4 메모리를 쓴다"(→ mip chain 바이트) 세 가지다. 절대 수치는 GPU/드라이버마다 달라지지만, 병목이 CPU 드로우 제출인지 GPU 샘플링/픽셀 비용인지 구분하는 흐름은 그대로 재현된다.

측정하지 않는 것도 분명히 해 둔다. "BC7은 DXT5와 같은 8 bpp에서 더 높은 품질을 낸다"는 화질 주장은 이 lab의 관측 수단으로 확인할 수 없다 — 패널은 모드를 배타 선택해 바이트 수만 비교하며, 동시 표시·차분·PSNR 같은 품질 지표가 없고 압축 자체도 드라이버의 온라인 인코더에 맡겨져 결과가 드라이버마다 달라진다. "static permutation과 uber shader의 교환 관계"도 대응 축 문서가 없는 이 lab 자체의 토글이다.

## 거대 프로젝트와의 관계

거대 프로젝트의 현재 표시 백엔드는 GDI와 Vulkan이며 OpenGL 경로는 없다. 이 lab은 OpenGL을 선택한 별도 제품에서 Multi-Draw Indirect, shader permutation, 텍스처 압축을 채택할 근거를 측정한다. 거대 프로젝트와 공유하는 것은 특정 API 객체가 아니라 드로우 제출량, GPU 시간, 자원 전환 비용을 관측하는 방법이다.
