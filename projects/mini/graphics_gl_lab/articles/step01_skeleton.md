## 들어가며

이 글은 *graphics 시리즈 OpenGL 단계의 주장을 직접 측정·시각화* 하는 도구 — `graphics_gl_lab` — 을 처음부터 만드는 작업의 첫 단계다. 시리즈가 *주장* (Instancing 이 개별 드로우콜 비용을 크게 줄인다, Static permutation 과 uber shader 는 프로그램 수와 분기 비용을 교환한다, BC7 은 DXT5 와 같은 8 bpp 에서 더 높은 품질을 노린다) 이라면 이 lab 은 *그 주장을 토글로 재현해 그래프로 보여주는 도구*. 드로우콜 방식을 *개별 → Instancing → Multi-Draw Indirect* 로 바꾸면서 *ms/frame · GPU 시간 · 드로우콜 카운트* 가 어떻게 갈라지는지 옆에 그려진다.

성격은 `physics_lab` 과 유사하다. *같은 워크로드를 여러 구현으로 동시에 돌려놓고 수치 / 그래프로 비교* 한다. 차이는 *시간 모델* — physics_lab 이 *fixed dt accumulator* 로 결정론을 추구한다면, graphics_gl_lab 은 *가변 dt + Vsync OFF* 로 *실제 ms 측정* 에 집중한다.

이 단계의 결과물은 다음 다섯 가지를 갖춘 *최소 골격*.

1. GLFW 윈도우 + OpenGL 4.5 Core 컨텍스트.
2. *1만 오브젝트 고정 씬* — 격자 분포 + 각자 다른 회전 속도.
3. *기본 셰이더* (Lambertian) — Position + Normal 정점 속성.
4. *프레임 타이머* — ms / fps + EMA + 4초 히스토리.
5. *ImGui + ImPlot 사이드 패널* — 수치 + 시계열 그래프 + wireframe / vsync 토글.

이 단계는 *토글이 올라갈 골격* 만 깔고, 그 위에 *1만 오브젝트 × 개별 드로우콜* 한 가지 모드를 동작 가능한 형태로 만든다.

---

## 목차
1. [도구의 성격과 측정 모델](#1-도구의-성격과-측정-모델)
2. [기술 스택: GLFW + glad + ImGui + ImPlot](#2-기술-스택-glfw--glad--imgui--implot)
3. [프로젝트 구조와 CMakeLists.txt](#3-프로젝트-구조와-cmakeliststxt)
4. [GLFW 컨텍스트 + glad 로더](#4-glfw-컨텍스트--glad-로더)
5. [Scene — 1만 오브젝트 격자](#5-scene--1만-오브젝트-격자)
6. [ShaderProgram + 기본 셰이더](#6-shaderprogram--기본-셰이더)
7. [FrameTimer + ImPlot 그래프](#7-frametimer--implot-그래프)
8. [빌드와 실행 결과](#8-빌드와-실행-결과)

---

## 1. 도구의 성격과 측정 모델

`physics_lab` 이 *같은 시뮬을 여러 구현으로 비교* 하는 측정 도구라면, `graphics_gl_lab` 은 *같은 장면을 여러 GL 렌더 모드로 비교* 하는 측정 도구다. 비교 차원이 *시뮬 알고리즘 → 렌더링 API 호출 패턴* 으로 바뀐 것뿐.

| 차원 | physics_lab | graphics_gl_lab |
|---|---|---|
| 주 목적 | 알고리즘 비교 (수렴 / 안정 / 비용) | API 호출 비용 비교 (CPU + GPU 시간) |
| 시간 모델 | fixed dt accumulator | 가변 dt + Vsync OFF |
| 결정론 | 중요 (재현성) | 무관 (성능만) |
| 입력 | 드래그 + 슬라이더 | 토글 (드로우콜 / 바인딩 / 셰이더 등) |
| 그래프 | 에너지 / ms | ms (CPU/GPU) / 드로우콜 카운트 |

*Vsync OFF* 가 핵심 — Vsync ON 이면 *모든 모드가 16.67ms 에 도달* 해 *차이가 안 보임*. Vsync OFF 로 *진짜 GPU 비용* 측정.

```cpp
glfwSwapInterval(vsyncOn_ ? 1 : 0);   // 기본값은 false, 측정 시 OFF
```

## 2. 기술 스택: GLFW + glad + ImGui + ImPlot

| 라이브러리 | 역할 | 가져오는 방식 |
|---|---|---|
| GLFW 3.4 | 윈도우 + 입력 | FetchContent |
| glad2 v2.0.6 | OpenGL 4.5 Core 로더 | FetchContent (cmake 모듈) |
| Dear ImGui 1.91 | UI | FetchContent |
| ImPlot 0.16 | 그래프 | FetchContent |
| glm 1.0.1 | 벡터 / 행렬 | FetchContent |

모든 의존성을 *FetchContent* 로 가져온다. 시스템에 *별도 설치 없이* `cmake -S . -B build` 만으로 동작.

### glad2 의 cmake 통합

전통적 glad 는 *web generator 출력물* 이라 `third_party/glad/{include,src}` 에 *수동 복사* 가 필요했다. glad2 는 *cmake 모듈* 을 제공해 *명시한 API 버전의 로더를 빌드 시점에 자동 생성* 한다.

```cmake
FetchContent_Declare(glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v2.0.6
    SOURCE_SUBDIR cmake)
FetchContent_MakeAvailable(glad)
glad_add_library(glad STATIC API gl:core=4.5)
```

`gl:core=4.5` — OpenGL 4.5 Core profile. 4.5 자체에 *DSA, Multi-Draw Indirect, `GL_ARB_buffer_storage`* 가 포함된다. *`GL_ARB_bindless_texture`* 는 *core 가 아니라 확장* — 사용 전에 `glGetStringi(GL_EXTENSIONS, …)` 검사가 필요하다. 이 단계는 core 4.5 만 쓰므로 확장 요청이 없다.

## 3. 프로젝트 구조와 CMakeLists.txt

```text
graphics_gl_lab/
  CMakeLists.txt
  src/
    main.cpp                       — App 객체 생성 + Run
    app/
      app.h / app.cpp              — 메인 애플리케이션
    scene/
      scene.h / scene.cpp          — 1만 오브젝트 fixed scene
    render/
      shader_program.h / .cpp      — 셰이더 컴파일 + 유니폼 설정
    measure/
      frame_timer.h / .cpp         — CPU 프레임 ms + EMA + 히스토리
    ui/
      panel.h / .cpp               — ImGui + ImPlot 패널
  shaders/
    basic.vert                     — Position + Normal
    basic.frag                     — Lambertian
  articles/
    step01_skeleton.md             — 이 글
```

`shaders/` 디렉토리가 *빌드 출력 옆에 자동 복사* 된다 (런타임 로드용).

```cmake
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders)
```

## 4. GLFW 컨텍스트 + glad 로더

`App::InitWindow` 가 *4.5 Core* 컨텍스트를 명시.

```cpp
glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#ifndef NDEBUG
glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
#endif
```

`GLFW_OPENGL_DEBUG_CONTEXT` — 디버그 빌드에서 `GL_DEBUG_OUTPUT` 을 켤 수 있는 컨텍스트를 요청한다. 이 단계는 컨텍스트 힌트만 주고 콜백은 등록하지 않는다.

`App::InitGL` 이 glad 로더 + 기본 상태.

```cpp
if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) {
    throw std::runtime_error("gladLoadGL failed");
}
glViewport(0, 0, windowW_, windowH_);
glEnable(GL_DEPTH_TEST);
glDepthFunc(GL_LESS);
```

## 5. Scene — 1만 오브젝트 격자

`Scene::Build(10000)` 가 *cube 메시 한 개* 를 GPU 에 업로드 + *1만 ObjectTransform* 을 격자로 생성.

```cpp
int side = (int)std::ceil(std::cbrt((double)objectCount));   // 22
float spacing = 2.5f;
float origin  = -0.5f * spacing * (float)(side - 1);
```

격자 변 길이 = $\lceil \sqrt[3]{10000} \rceil$ = 22. 22³ = 10,648 칸 중 10,000 채움. 각 셀에 cube 한 개 + 무작위 *회전축 / 각속도 / 색상*.

매 프레임 `Scene::Update(dt)` 가 *각 오브젝트 회전각 누적* + `Scene::Draw` 가 *개별 `glDrawElements` × N* 호출.

```cpp
for (const auto& o : objects_) {
    glm::mat4 model = glm::translate(...) * glm::rotate(...);
    shader.SetMat4("uModel", model);
    shader.SetVec3("uColor", o.color);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
    ++lastDrawCalls_;
}
```

*이 단계의 핵심* — 1만 개별 드로우콜이 *얼마나 느린지* 직접 측정한다.

## 6. ShaderProgram + 기본 셰이더

ShaderProgram 클래스가 *컴파일 + 링크 + 에러 로그* + *Uniform location 캐시* 를 제공.

```cpp
basicShader_ = std::make_unique<ShaderProgram>();
basicShader_->LoadFromFiles("shaders/basic.vert", "shaders/basic.frag");
```

Uniform 캐시가 핵심 — 매 드로우마다 `glGetUniformLocation("uModel")` 같은 *문자열 비교 + 해시* 호출 비용을 피한다. 측정 코드에서 *이 캐시 누락이 흔한 함정*.

기본 셰이더 (Lambertian).

```glsl
// fragment
vec3 N = normalize(vNormalWS);
vec3 L = normalize(uLightDir);
float diff = max(dot(N, L), 0.0);
vec3 ambient = 0.15 * uColor;
vec3 diffuse = diff * uColor;
FragColor = vec4(ambient + diffuse, 1.0);
```

이 단계의 셰이더는 이 한 벌뿐이다 — 셰이딩 모델 선택도, 프로그램 전환도 없다.

## 7. FrameTimer + ImPlot 그래프

`FrameTimer` — `std::chrono::steady_clock` 기반 CPU ms 측정 + EMA + 240 프레임 히스토리.

```cpp
void FrameTimer::EndFrame() {
    auto now = Clock::now();
    std::chrono::duration<float, std::milli> ms = now - frameBegin_;
    lastFrameMs_ = ms.count();
    emaFrameMs_ = emaFrameMs_ * 0.9f + lastFrameMs_ * 0.1f;
    history_[(size_t)historyHead_] = lastFrameMs_;
    historyHead_ = (historyHead_ + 1) % kHistorySize;
}
```

ImPlot 그래프가 240 프레임 (약 4초 @ 60fps) 의 *ms 변화* 를 *시계열 plot*.

```cpp
ImPlot::PlotLine("ms", linearized.data(), FrameTimer::kHistorySize);
```

순환 버퍼라 *그래프 인덱스 0 이 가장 오래된 값* 이 되도록 linearize. 그래야 시간 방향이 *왼쪽 → 오른쪽*.

이 단계의 그래프에는 *CPU 프레임 ms 한 줄* 만 있다. GPU 측 시간은 아직 측정하지 않는다.

## 8. 빌드와 실행 결과

```bash
# Linux / macOS
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/graphics_gl_lab
```

```powershell
# Windows (Visual Studio generator)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build\Release\graphics_gl_lab.exe
```

첫 빌드는 *FetchContent 다운로드 + 빌드* 로 *수 분*. 이후 캐시. *Python 3 + venv 모듈* 만 시스템에 필요하다 — CMakeLists 가 build/ 안에 *전용 venv* 를 만들어 Jinja2 를 자동 설치한다 (glad2 코드 생성 의존성). 실행 시.

- 윈도우 1280×720, 어두운 배경, 회전하는 1만 개의 작은 큐브.
- 우측 ImGui 패널 — frame ms / fps / objects 수 / draw calls / wireframe & vsync 토글.
- ImPlot 시계열 — frame ms 변동.

저사양 GPU (Intel UHD 등) 에서 *1만 드로우콜 × 60fps = 60만 콜 / 초* 가 *드로우콜 오버헤드만으로 ms 가 16+* 가 나오기 쉽다. 이 골격의 관측 가능한 결과가 바로 그 *개별 드로우콜 비용* 이다.

---

## 결론

여기까지의 코드는 *graphics 시리즈 OpenGL 단계 측정의 토대* 가 된 *최소 골격*. GLFW + glad2 + ImGui + ImPlot + glm 의 *현대 OpenGL 학습 / 측정 표준 스택* 위에 *1만 오브젝트 격자 씬* + *CPU 프레임 타이머* + *시계열 그래프* 가 한 흐름으로 묶였다.

`physics_lab` 의 *Scene 인터페이스 + 탭 전환* 패턴 대신 *단일 Scene + 토글* 패턴을 택했다. 이유 — graphics 의 *비교 차원이 알고리즘이 아닌 API 호출 방식* 이라, *같은 씬을 N 가지 모드로* 그리는 게 *자연스러움*. Scene 전환 비용도 회피.

이 단계의 종료 상태는 *CPU 프레임 ms 하나만 관측 가능한 상태* 다. GPU 측 시간, GL 호출 카운터, 드라이버 디버그 메시지는 아직 없고, 렌더 모드도 *개별 드로우콜* 한 가지뿐이다.

## 더 읽을거리

- [GLFW Documentation](https://www.glfw.org/documentation.html), [Dear ImGui](https://github.com/ocornut/imgui), [ImPlot](https://github.com/epezent/implot) — 측정 스택의 공식 저장소.
- [LearnOpenGL: Getting Started](https://learnopengl.com/Getting-started/OpenGL) — 같은 스택의 표준 입문.
