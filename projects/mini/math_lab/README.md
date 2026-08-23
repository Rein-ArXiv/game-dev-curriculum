# math_lab — math 시리즈의 원리 측정·시각화

## 컨셉

이 lab은 **수학적 개념을 인터랙티브 캔버스 위에서 직접 조작하며 동작을 관찰**하는 도구다. 마우스로 점을 끌어 옮기면 회전·보간·노이즈·충돌·공간 분할 알고리즘이 그 변화에 반응해 즉시 다시 계산되고 그 과정이 단계별로 시각화된다. 동일한 입력을 여러 표현(Euler vs Quaternion, Lerp vs Slerp, SAT vs GJK)으로 돌려 차이를 동시에 본다.

## 짝 시리즈

- **math** — 회전 표현, 보간, 노이즈, 충돌 검사, 공간 분할, SDF를 다루는 시리즈의 정의와 도식을 이 lab이 살아있는 시각화로 만든다.
- **algorithms** — property·fuzz 검증 편이 가르치는 기법(property / 불변식 / oracle / 생성기 / shrink)을 이 lab의 `tests/property.h` 와 13종 property 가 실행 가능한 자산으로 만든다. lab 을 새로 늘리는 대신 기존 검증 절에 흡수한 형태다.

## 토글 항목

이 lab은 측정보다 *인터랙티브 시각화*가 주이므로, 토글은 보여줄 알고리즘 선택에 가깝다.

| 항목 | 옵션 | 비교 대상 |
|---|---|---|
| 회전 표현 | Euler XYZ / Slerp vs Nlerp | math/part04~05 |
| 보간 | Lerp / Slerp / Bezier / Catmull-Rom | math/part07~08 |
| 노이즈 | Value / Perlin / Simplex / FBM | math/part09~10 |
| 충돌 검사 | SAT / GJK (단계별 stepping) | math/part11~13 |
| SDF | Circle / Box / Union / Smooth Min | math/part14 |
| 공간 분할 | BVH median split (빌드 시각화 + Ray-AABB 트래버설) | math/part15~16 |
| 투영·수치 안정성 | Standard Z vs Reverse-Z 깊이 곡선 / Kahan 합산 | math/part18 + part20 |
| 단계 stepping | GJK iteration·BVH 빌드 단계 추적 | 알고리즘 내부 추적 |

## 출력

- 메인 캔버스 — 마우스로 끌 수 있는 핸들, 알고리즘이 그려내는 곡선/도형/필드.
- 사이드 패널 — 현재 단계의 수치(쿼터니언 성분, GJK simplex 정점, BVH 노드 깊이).
- Euler 짐벌락 데모 — 세 축 회전 슬라이더와 짐벌 시각화. 락이 걸리는 순간 빨갛게 표시(가운데 축 `yaw ≈ ±90°`).
- 노이즈 패널 — Value/Perlin/Simplex 토글, scale·offset·seed 슬라이더, FBM(octaves/lacunarity/gain).
- SDF 등고선 토글 — `showContours_` 로 거리장 등고선 표시 on/off.
- BVH 빌드 단계 슬라이더 — 빌드 과정을 0~N 프레임으로 되감기 가능.

미구현(문서 예고만 있고 코드에 없는 것): 축-각 회전 모드, 회전축 드래그 핸들, 노이즈 등고선/3D 높이 표면 토글.

## 측정 항목

성능 측정이 주 목적은 아니지만 보조적으로 다음을 표시한다.

- 알고리즘 단계 수(GJK iteration, BVH 분할 횟수).
- ms/frame — 푸터의 `FrameTimer` 값. **프레임 전체 시간**이며 알고리즘 단일 호출 비용이 아니다.

미구현: 토글 알고리즘별 호출 비용 계측, 그리드 해상도별 SDF 평가 비용(`texW_` 는 256 고정, 해상도 슬라이더 없음).

## 추적 단위

`PROJECTS_PLAN.md` 의 mini lab 완료 기준 7항목을 이 lab 기준으로 적으면 다음과 같다.

| 항목 | 이 lab 에서 |
|---|---|
| 이론 주장 | 쿼터니언 항등식(단위 norm, 샌드위치 회전, Hamilton 곱의 결합성, q 와 −q 의 이중 덮개, 짐벌락에서 바깥 두 축의 축약), Slerp 의 등각속도, 행렬 항등식(M·M⁻¹ = I, 직교행렬의 Rᵀ = R⁻¹ 과 det = 1, (AB)ᵀ = BᵀAᵀ, QuatToMatrix ⇔ 샌드위치 회전, 합성의 비가환성), SAT ⇔ GJK 판정 일치와 분리면의 존재, 강체 변환·균등 스케일에서의 판정 불변, BVH 트래버설이 brute force 와 같은 hit, smooth min 의 blend 구간, Perlin/Simplex/Value 의 출력 범위 상계, FBM 이 진폭 가중 평균이라는 것, Reverse-Z = 1 − Standard Z, Kahan 합산의 absorption 회복 |
| 조작 변수 | Scene 토글(알고리즘 선택), 슬라이더(각도·t·scale·seed·octaves·k·near/far·N), 드래그 핸들 위치. 검증 쪽에서는 property 층의 master seed(`--seed=`)와 표본 배율(`--scale=`) |
| 통제 변수 | 같은 입력 도형/제어점/좌표, 같은 seed, 같은 캔버스 해상도 — 토글만 바꾸고 나머지는 고정 |
| 관측값 | 화면의 기하(호/곡선/분리축/노드 박스/등고선) + 패널의 수치(쿼터니언 성분, norm(q), GJK iteration, BVH 노드 수, naive vs kahan 합) + property 실행 요약(표본 수, 비퇴화 표본 수, 사전조건 탈락 수, 최악 관측/상계 비) |
| 기대 결과 | norm(q) = 1, Slerp 가 호 위·Nlerp 가 현 근처, 분리축이 있으면 비충돌, 광선이 놓친 노드는 방문하지 않음, k→0 에서 smin→min, 생성된 모든 표본이 계약 상계 안 |
| 실패 주입 | Slerp: 큰 호(2.6 rad)에서 인접 샘플 각도의 max/min 비 — Nlerp 로 바꾸면 1.0 대에서 1.5 대로 튄다. SAT: B 의 면 법선에서만 분리되는 배치 — 후보축을 A 것만 쓰면 충돌로 오판한다. BVH: 노드별 median 분할 불변식과 brute force 대조 — 분할·정렬·가지치기 중 하나만 없애도 깨진다. 노이즈: 표본 분산 하한 — hash 나 그라디언트를 상수로 바꾸면 0 이 된다. Kahan: big=1e5, small=1e-4, N=1e5 에서 naive 와의 차이. property 층에는 13개 property 각각에 대응 변이를 하나씩 붙여 두었다(정규화 제거, Hamilton 곱 부호 반전, 짧은 호 뒤집기 제거, slerp 가중치 선형화, Perlin 2배, FBM 정규화 제거, 격자 오프셋 −1 누락, SAT 후보축에서 B 법선 탈락, 분리 문턱 0→0.05, A 법선을 월드축으로 고정, Gauss-Jordan 부호 반전, MatMul 전치, QuatToMatrix 부호 반전). 모두 실제로 주입해 대상 property 가 죽는 것을 확인하고 원본 바이트로 복원 후 SHA256 을 대조했다 |
| 자동 테스트 | `tests/core_tests.cpp` — GUI 없이 도는 두 층. (a) 고정 사례 13개 함수: layout / quaternion(+slerp 등각속도/짐벌락) / curves / collision / sdf / bvh / noise / depth / kahan / vector / matrix. 무작위 교차 검증 3종(OBB 20,000쌍 SAT⇔GJK, 광선 3,000개 BVH⇔brute force, smin 500 표본)을 고정 seed 로 돌린다. (b) `tests/property.h` 하네스 위의 시드 무작위 property 13종(쿼터니언 4 · 노이즈 3 · 충돌 3 · 행렬 3), 기본 42,300 표본. 파일 끝의 `kCases` 표가 두 층을 모두 그것을 들여온 step 에 묶어, `ctest` 타깃이 step별로 나뉜다(`math_stepNN_*`). property 층만 도는 `math_property_layer` 타깃이 따로 있다 |

## 기술 스택

- C++20.
- raylib 5.0 (2D / 3D 캔버스, 입력 처리). FetchContent 자동.
- raygui 4.0 (단일 헤더 — 사이드 패널의 라벨 / 슬라이더 / 토글).
- CMake 3.20+.
- (선택) GLM — 회전 / 행렬 검증이 필요한 Scene 에서 비교 기준으로 추가 가능. 코어에는 raylib 의 `Vector2/3` + 자체 quaternion 만으로 충분.

## 디렉토리 구조

```text
math_lab/
  CMakeLists.txt
  src/
    main.cpp
    scenes/                     씬(장면)들 — iscene.h 인터페이스
      welcome_scene.{h,cpp}
      rotation_scene.{h,cpp}
      interpolation_scene.{h,cpp}
      noise_scene.{h,cpp}
      collision_scene.{h,cpp}
      spatial_scene.{h,cpp}     공간 분할(BVH/그리드)
      projection_scene.{h,cpp}
      sdf_scene.{h,cpp}
    algo/                       헤더온리 알고리즘 (GUI 비의존, 테스트가 직접 include)
      quaternion.h  curves.h  noise.h
      collision2d.h  bvh2d.h  sdf2d.h
      mat.h                     벡터·4x4 행렬 대수 (raymath 미사용)
      depth.h  kahan.h          투영 깊이 매핑 / 보상 합산
    measure/  frame_timer.{h,cpp}
    ui/
      handle.{h,cpp}            마우스로 끄는 점
      scene_chrome.{h,cpp}  raygui_impl.cpp
  tests/
    core_tests.cpp              GUI 없는 코어 검증. 고정 사례 + property 사례를 한 표에
                                묶는다. {step, 이름, 함수 | property} + 첫 인자 step 필터
    property.h                  시드 무작위 property 층 하네스 (외부 라이브러리 없음).
                                생성기(분포 설계) + 시드 루프 + 축소기 + 반례 보고
                                ctest 타깃은 math_stepNN_*(step별 8개) + math_core_tests(전체)
                                + math_property_layer(property 만) = 10개
  # raylib / raygui 는 CMake FetchContent 가 build/_deps 아래 자동으로 가져옴.
```

## 빌드와 실행

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/math_lab
```

자동 검증은 GUI 없이 별도 타깃으로 돈다. 실행 단위는 step 입도다 — step별 8개 + 전체 1개 + property 층 1개.

```bash
ctest --test-dir build -C Release --output-on-failure
#  1/10 Test  #1: math_step01_skeleton ............. Passed
# ...
#  9/10 Test  #9: math_core_tests .................. Passed
# 10/10 Test #10: math_property_layer .............. Passed
# 100% tests passed out of 10
```

한 step 만 다시 돌리려면 이름으로 거른다. 실행 파일을 직접 부르면 어떤 검증이 몇 개 돌았는지가 보인다.

```bash
ctest --test-dir build -C Release -R math_step05_collision --output-on-failure
./build/Release/math_core_tests.exe step05
# [step05] collision/sat_vs_gjk ............ 14 checks, 0 failed
# [step05] collision/sat-gjk-agree ......... 4 checks, 0 failed  (5000 samples, 734 non-degenerate, 1 skipped, worst 0.000x)
# [step05] collision/axis-witness .......... 4 checks, 0 failed  (2500 samples, 2131 non-degenerate, 0 skipped, worst 0.004x on '|separating axis| - 1')
# [step05] collision/rigid-invariance ...... 4 checks, 0 failed  (3000 samples, 456 non-degenerate, 0 skipped, worst 0.000x)
# step05: 4 case(s), 26 check(s), 0 failure(s)
```

시드 무작위 property 층은 `--properties` 로 따로 돌릴 수 있다. 기본 표본 수는 매 커밋에 도는 크기(전체 42,300 표본, 0.07초)이고, 오래 도는 캠페인은 같은 실행 파일에 배율을 준다. 야간 확장은 `ctest` 타깃으로 걸지 않았다 — 매 커밋에 도는 것과 오래 도는 것을 섞지 않는다.

```bash
./build/Release/math_core_tests.exe --properties
./build/Release/math_core_tests.exe --properties --scale=25          # 약 106만 표본
./build/Release/math_core_tests.exe --properties --scale=25 --seed=7  # 다른 master seed
```

반례가 나오면 축소된 파라미터와 재현 명령 한 줄이 `stderr` 로 나온다. 그 줄을 그대로 복사하면 그 표본 하나만 다시 돈다.

```text
    shrunk   : t.x=0 t.y=0 t.z=0 axis.x=0 axis.y=0 axis.z=1 angle=-6.9553e-06 s.x=1 s.y=1 s.z=1 p.x=1 p.y=0 p.z=0   (31 shrink step(s))
    repro    : math_core_tests step08 --only=matrix/inverse-roundtrip --seed=20260823 --sample=0
```

상단 탭으로 씬을 전환한다. 각 씬은 독립이며 따로 빌드하지 않아도 즉시 전환 가능하다.

## 시리즈 글과의 관계

math 시리즈의 모든 핵심 도식이 이 lab에서 살아있는 형태로 재현된다. 글에서 "Slerp는 단위 구 위의 호를 따라 움직인다"고 적은 문장이 이 lab에서는 점이 실제로 호를 따라 이동하는 모습으로 보인다. GJK가 simplex를 키워가는 과정, BVH가 median split으로 분할하는 과정, SDF가 Smooth Min으로 두 도형을 매끄럽게 합치는 과정이 모두 단계별로 실행 가능하다.

## 거대 프로젝트와의 관계

거대 프로젝트의 현재 디펜스 제품 경로는 고정 2D 타일맵과 선형 스냅샷 보간을 사용하므로 Slerp 카메라, BVH 광역 충돌, FBM 지형을 사용하지 않는다. 이 lab은 그런 기능이 필요한 장르에서 표현과 알고리즘을 선택할 판단 근거를 만든다. 거대 프로젝트 안에서는 수학·공간·노이즈 모듈의 독립 테스트가 같은 불변식을 비시각적으로 검증한다.
