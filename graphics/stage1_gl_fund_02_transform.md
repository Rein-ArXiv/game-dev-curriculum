## 들어가며

좌표 변환의 수학 — 로컬·월드·뷰·클립·NDC·화면 공간의 사슬, 동차 좌표, 모델·뷰·투영 행렬, 클립 공간과 원근 분할, 법선의 역전치 변환, 메모리 레이아웃과 곱셈 순서 — 은 API와 무관하다. OpenGL에서 중요한 것은 이 수학을 어떤 API 계약으로 전달하느냐다. 이 글은 `gl_Position`의 출력 계약, `glUniformMatrix4fv`의 전치 인자, `glClipControl`로 여는 Reverse-Z, 뷰포트·깊이 범위 API, 그리고 GLM으로 쓰는 MVP를 다룬다.

필요한 전제는 다음과 같다. 정점 셰이더의 출력 계약은 *클립 공간* 이고, 원근 분할과 뷰포트 변환은 고정 함수가 처리한다. 원근의 본질은 클립 좌표의 $w_c$로 나누는 것이며, OpenGL의 기본 NDC 깊이는 $[-1,1]$이다. 행렬의 *저장 레이아웃* 과 *벡터 곱 순서* 는 별개 선택이다. 이 전제 위에서 OpenGL API가 무엇을 요구하는지가 이 글의 내용이다.


---

## 목차
1. [정점 셰이더의 출력 계약](#1-정점-셰이더의-출력-계약)
2. [행렬 업로드와 메모리 레이아웃](#2-행렬-업로드와-메모리-레이아웃)
3. [OpenGL의 클립·NDC와 Reverse-Z](#3-opengl의-클립ndc와-reverse-z)
4. [통합 예제: GLM으로 MVP](#4-통합-예제-glm으로-mvp)


---

## 1. 정점 셰이더의 출력 계약

OpenGL은 정점 속성의 의미를 정하지 않는다. 응용이 위치 속성을 Local Space로 해석하는 것이 흔한 관습이며, API가 강제하는 계약은 정점 셰이더의 `gl_Position`이 *Clip Space* 좌표라는 점이다. 클립→NDC(원근 분할)와 NDC→화면(뷰포트 변환)은 셰이더 이후 고정 함수가 처리한다. 일반적인 물체 렌더링에서는 셰이더가 MVP 곱까지 책임진다.

```glsl
gl_Position = uP * uV * uM * vec4(localPos, 1.0);   // 오른쪽부터 적용: M→V→P
```

곱셈은 오른쪽부터 적용되므로 `localPos`에 `M`이 먼저 곱해져 World로, 그 다음 `V`로 View, 마지막 `P`로 Clip이 된다. 라이팅 등으로 중간 공간이 필요 없다면 CPU에서 미리 합쳐 한 행렬만 올린다.

```cpp
glm::mat4 mvp = projection * view * model;   // M*v 컨벤션: P*V*M
shader.SetUniform("uMVP", mvp);
```

```glsl
gl_Position = uMVP * vec4(localPos, 1.0);
```

합쳐 보내면 GPU 곱이 줄지만, 월드 공간 라이팅처럼 View·Model을 셰이더에서 따로 쓸 일이 생기면 분리해 올린다. 단순 변환은 MVP 합성, 월드 라이팅이 필요해지는 순간부터 분리가 흔한 패턴이다.


## 2. 행렬 업로드와 메모리 레이아웃

저장 레이아웃과 벡터 곱 순서는 서로 다른 문제다. OpenGL 구현에서는 CPU가 의도한 *수학적 행렬*, 메모리에 놓인 순서, GLSL의 곱 식을 따로 맞춰야 한다. GLSL의 일반 uniform 행렬과 GLM의 기본 저장은 column-major이므로 가장 단순한 경로는 하나의 열벡터 컨벤션으로 정규화하고 `GL_FALSE`로 업로드하는 것이다.

```cpp
glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mvp));  // GLM: 이미 column-major
```

데스크톱 OpenGL에서 세 번째 인자 `transpose=GL_TRUE`는 전달한 값을 행 우선 순서로 읽는 효과를 내지만, 이것이 “DirectX 행렬 변환” 스위치는 아니다. DirectXMath의 전형적인 행벡터 행렬을 GLSL의 열벡터 식에 재사용하면 필요한 수학적 행렬은 원본의 전치이고, 원시 바이트를 column-major로 해석하는 과정이 이미 그 전치 효과를 낼 수도 있다. 투영 깊이·손잡이까지 다를 수 있으므로 라이브러리 이름만 보고 `GL_TRUE`를 선택하면 안 된다. CPU 경계에서 명시적으로 수학적 행렬을 변환하고, 기준점 변환 테스트를 통과한 column-major 배열을 `GL_FALSE`로 보내는 편이 안전하다. OpenGL ES 2.0에서는 `transpose`가 반드시 `GL_FALSE`여야 하지만, ES 3.x에서는 `GL_TRUE`도 행 우선 입력으로 정의된다.

UBO/SSBO 블록 안의 행렬 멤버 배치는 `layout` 한정자로 따로 지정한다(단일 `uniform mat4`에는 적용되지 않는다 — 그쪽은 위의 transpose 인자로 맞춘다).

```glsl
layout(row_major) uniform Block { mat4 mModel; };
```


## 3. OpenGL의 클립·NDC와 Reverse-Z

OpenGL의 기본 NDC 깊이는 $[-1,1]$로, Direct3D·Vulkan의 $[0,1]$과 다르다. 일반 깊이 테스트에서는 이 값도 `glDepthRange`를 거쳐 깊이 버퍼의 $[0,1]$로 매핑되므로 범위 차이 자체가 정밀도를 절반으로 만들지는 않는다. 32비트 부동소수점 reverse-Z의 이점을 얻으려면 OpenGL 4.5의 `glClipControl` 또는 `ARB_clip_control`로 클립 깊이 조건을 $0\le z_c\le w_c$로 바꿀 수 있다.

```cpp
glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);  // 클립 Z를 [0,1]로
glDepthFunc(GL_GREATER);                       // 비교 방향 반전
glClearDepth(0.0);                             // clear는 far(=0)
// 투영 행렬도 near→depth 1, far→depth 0으로 가도록 재구성해야 한다.
// GLM은 GLM_FORCE_DEPTH_ZERO_TO_ONE 매크로로 [0,1] 투영을 생성.
```

핵심은 단순히 API 상태만 뒤집는 것이 아니라 투영 행렬이 near를 depth 1, far를 depth 0에 매핑하도록 구성하는 것이다. float의 0 근처 절대 간격과 결합하면 먼 거리 정밀도가 크게 좋아지고, 무한 far 투영도 실용적이 된다. 정밀도가 완전히 균일해지거나 동일 평면에서 생기는 Z-fighting이 모두 사라지는 것은 아니다. D24 같은 고정소수점 깊이에서는 뒤집기 자체로 양자화 단계 수가 늘지 않는다는 점도 구분해야 한다.

뷰포트와 깊이 범위는 GL API로 지정한다. 기본 clip origin은 좌하단이지만 `glClipControl`의 첫 인자로 상단 원점도 선택할 수 있다.

```cpp
glViewport(x, y, w, h);          // (x,y)=윈도 좌표계에서 뷰포트 사각형의 오프셋
glDepthRange(0.0, 1.0);          // NDC z를 깊이 버퍼 범위로 (기본 0~1)
```

사용자 정의 클립 평면이 필요하면 `gl_ClipDistance[]`에 평면까지의 부호 있는 거리를 출력하고 대응하는 `GL_CLIP_DISTANCEi`를 활성화한다. GPU는 음의 영역을 잘라낸다. 물 반사나 단면 표시에 쓴다.

```glsl
out gl_PerVertex {
    vec4 gl_Position;
    float gl_PointSize;
    float gl_ClipDistance[1];
};

void main() {
    vec4 worldPos = uModel * vec4(localPos, 1.0);
    gl_ClipDistance[0] = dot(worldPos, uClipPlane);
    gl_Position = uMVP * vec4(localPos, 1.0);
}
```

```cpp
glEnable(GL_CLIP_DISTANCE0);
```


## 4. 통합 예제: GLM으로 MVP

GLM은 OpenGL에서 흔히 쓰는 열벡터·column-major 기본값과 `...RH`/`...LH`, 깊이 범위별 함수를 제공한다. 전역 구성 매크로가 번역 단위마다 달라지면 같은 타입 이름이 다른 의미를 갖게 되므로 프로젝트 전체에서 한 설정을 사용해야 한다.

```cpp
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

void RenderObject(const glm::vec3& pos, float rotY, GLuint shader) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), pos)
                    * glm::rotate(glm::mat4(1.0f), rotY, glm::vec3(0, 1, 0));
    glm::mat4 view  = glm::lookAt(glm::vec3(0, 3, 5), glm::vec3(0), glm::vec3(0, 1, 0));
    glm::mat4 proj  = glm::perspective(glm::radians(60.0f), 16.0f/9.0f, 0.1f, 100.0f);
    glm::mat4 mvp   = proj * view * model;

    GLint loc = glGetUniformLocation(shader, "uMVP");
    glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mvp));  // GLM은 column-major
}
```

```glsl
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main() { gl_Position = uMVP * vec4(aPos, 1.0); }
```

직접 구현한 행렬 클래스로 옮길 수도 있다. 위 코드와 같은 열벡터·column-major 계약이라면 평행이동은 마지막 열의 저장 인덱스 12·13·14에 놓인다. 검증은 범위 포함만 보지 않고 기준점을 정확히 검사한다. 카메라 위치는 view 원점으로, near 중앙은 NDC 중앙과 near 깊이로, far 중앙은 far 깊이로 가야 하며, $w_c\le0$인 카메라 뒤 점은 정상 가시 후보가 되어서는 안 된다. 비대칭 절두체와 비균등 스케일 법선도 테스트해야 전치가 두 번 상쇄된 우연을 잡을 수 있다.

GLM의 `GLM_FORCE_DEPTH_ZERO_TO_ONE` 매크로는 투영 함수가 클립 Z를 $[0,1]$로 만들게 하며, 모든 GLM 헤더보다 먼저 정의해야 한다. 3절의 `glClipControl(..., GL_ZERO_TO_ONE)` 또는 Vulkan용 투영과 함께 사용한다. 일반 OpenGL $[-1,1]$ 클립 규칙과 섞으면 near 클리핑과 깊이 매핑이 어긋난다. 이 매크로는 범위만 바꾸며 reverse-Z 자체를 켜 주지는 않으므로, near=1·far=0 투영은 별도로 구성해야 한다.


---

## 결론

좌표 변환의 수학은 API 중립이고, OpenGL 구현에서 따로 챙길 것은 *API 계약* 이다. 정점 셰이더는 `gl_Position`으로 클립 좌표를 출력하고, 업로드 전에는 수학적 행렬·저장 레이아웃·벡터 곱 컨벤션을 구분한다. `glClipControl`, 깊이 비교·clear 값, 투영 행렬은 하나의 reverse-Z 계약으로 함께 바꿔야 한다. 다른 API로 옮길 때도 MVP라는 이름만 공유할 뿐 클립 볼륨, 화면 방향, 버퍼 패킹, 셰이더 곱 규칙을 각각 확인해야 한다.

이 문서의 범위는 행렬을 OpenGL 파이프라인에 전달하는 계약까지다. VAO·VBO·셰이더 컴파일은 같은 계약을 실제 드로우콜과 연결하는 별도 구현 문제다.

## 더 읽을거리

- [LearnOpenGL: Transformations](https://learnopengl.com/Getting-started/Transformations), [Coordinate Systems](https://learnopengl.com/Getting-started/Coordinate-Systems) — GLM 기준 OpenGL 구현 자료.
- [OpenGL 4.6 Core Specification](https://registry.khronos.org/OpenGL/specs/gl/glspec46.core.pdf) — uniform 행렬 업로드, `gl_Position`, 클립·뷰포트 계약의 규범적 원문.
- [OpenGL Wiki: glClipControl](https://www.khronos.org/opengl/wiki/GLAPI/glClipControl) — Reverse-Z를 위한 클립 범위 전환.
- [GLM 매뉴얼](https://github.com/g-truc/glm) — column-major·RH/LH 함수의 산업 표준 헤더온리 라이브러리.
- [Song Ho Ahn, OpenGL Transformation](https://www.songho.ca/opengl/gl_transform.html) — GL 투영·뷰포트 변환의 구체.
