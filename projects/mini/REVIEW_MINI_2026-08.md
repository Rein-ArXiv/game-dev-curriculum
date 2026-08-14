# projects/mini 검수 보고서 (2026-08)

- 대상: `projects/mini/` 12개 lab — 문서(`README.md` + `articles/step*.md`)와 코드(`src/`, `tests/`)
- 저자가 밝힌 mini의 성격: **"관련 문서에서 배운 걸 점차 검증해 나가면서 나아가는 것.
  과정에서 배우는 것을 제외한, 필요한 라이브러리는 적용 가능"**
- 따라서 1순위 판정 기준은 **학습 대상 자체를 라이브러리로 대체했는가**다.
  창 생성·이미지 로딩·UI 같은 비학습 대상에 라이브러리를 쓰는 것은 정상이다.
- 그 밖: `PROJECTS_PLAN.md`의 mini 완료 기준 7항목(이론 주장/조작 변수/통제 변수/관측값/
  기대 결과/**실패 주입**/**자동 테스트**)과 step 문서 완료 기준 7항목
- 방법: 문서 정독 → 외부 의존성 전수 분류 → 이론 축 주제 대조 → **복사본에 변이 주입 후 재빌드·재실행**
- **검수 시점(2026-08)에는 지적만 했고 코드·문서를 수정하지 않았다. 그 뒤 2026-08-13에
  이 보고서의 지적을 본문에 반영했다.** 따라서 아래 각 항목의 "현재 코드는 …" 서술과 인용된
  코드·문장은 **2026-08 검수 시점의 상태**이며, 지금의 파일과 다를 수 있다. 인용문이 현재
  파일에서 검색되지 않는다면 대개 반영으로 그 줄이 바뀐 것이다.
- 지적별 처리 결과와 **아직 남은 일**은 문서 끝 [반영 상태 (2026-08-13)](#반영-상태-2026-08-13)과
  [2차 반영 (2026-08-14)](#2차-반영-2026-08-14) 두 절에 있다. **후자가 최신이다.**

> 총 **166건** — 치명 6 · 높음 54 · 중간 76 · 낮음 30
>
> 2026-08-13 기준 처리: **반영됨 157 · 이월 9 · 기반영 0 · 기각 0**
>
> 2026-08-14 기준 처리: **반영됨 160 · 부분 반영 1 · 이월 5 · 기반영 0 · 기각 0**
> — 이월 9건 중 4건(`parts_perf_lab` #12 · `physics_lab` #10 · `graphics_dx_lab` #10 ·
> `live_service_lab` #13)을 처리했고, 남은 5건은 전부 `system_kernel_lab`이며 **Linux 환경을 요구한다.**

## 총평

**학습 대상 대체는 사실상 없다.** 166건 중 그 분류는 **2건**뿐이고, 나머지 10개 lab은
배움의 대상을 전부 자작했다. 할당자·SIMD·intrusive list, 렉서·파서·바이트코드 VM,
futex 직접 syscall, SAT·GJK·BVH·노이즈, 적분기·솔버, A*·JPS·flow field·boids,
원시 GL/D3D12/Vulkan 호출 — 이 축들의 핵심은 라이브러리가 대신하지 않는다.
raylib·GLFW·ImGui·glm·DirectXMath는 창·UI·수학 편의로 한정되며, 이는 저자가 허용한 범위다.

**진짜 문제는 검증력이다.** 166건 중 최대 분류가 `검증력부족` 54건이고,
12개 lab 전부에서 **변이 테스트가 생존**했다. 대표적으로:

| lab | 무력화해도 ctest 통과 |
|---|---|
| `math_lab` | 변이 31건 중 **24건(77%) 생존**. Slerp→Nlerp, Smooth Min→min, BVH median split 제거 |
| `physics_lab` | SAT 침투깊이 0, 반발계수 0, 누적 λ 클램프 제거, Explicit→Semi-implicit 교체 |
| `languages_lab` | 변이 12건 중 **8건 생존** — 토큰 위치, 분기 합류, 결합 법칙, 스택 한도 |
| `ai_lab` | JPS forced neighbor 규칙 삭제 — **`TestForcedNeighbors`라는 이름의 테스트가 못 잡음** |
| `graphics_dx_lab` | 배리어 전량 미발급, 디스크립터 write 전량 제거, 드로우 절반 삭제 |
| `graphics_gl_lab` | 드로우 N회→1회, 광원 업로드 제거, UBO 크기 0, VRAM 측정 0 고정 |

구조적 원인이 공통된다 — **테스트 타깃이 제품 번역 단위를 링크하지 않는다.**
`graphics_gl_lab`·`graphics_dx_lab`·`graphics_vk_lab`은 테스트가 계약 파일 하나만 컴파일하고
제품 `.cpp` 전체를 빼며, `ai_lab`은 951줄의 계산 코어가 raylib Scene 안에 갇혀 headless 실행이
구조적으로 불가능하다. **mega 감사에서 나온 "테스트와 제품이 다른 코드를 본다"와 같은 병이다.**

---

## parts_perf_lab — 보완 필요 (15건)

학습 대상 대체는 없다. 외부 의존성은 raylib(창·DrawPixel·DrawText)과 raygui(토글 패널) 둘뿐이고, 이 lab의 학습 대상인 4종 할당자(bump pointer/free list/크기 클래스/AlignUp), SoA·AoS·Archetype 레이아웃, SSE·AVX2 인트린식, intrusive list는 모두 직접 구현되어 있다. std::vector·std::list는 대체물이 아니라 비교 baseline이며 Boost.Intrusive·Google Benchmark·EASTL 같은 대체 라이브러리는 링크되지 않는다. 반면 검증력은 선언된 기준에 미달한다. PROJECTS_PLAN.md:31이 요구한 세 항목 중 "자료구조 결과 동등성"은 존재하지 않으며(measure/*.cpp가 테스트 타깃에 아예 포함되지 않음), 변이 테스트로 Archetype SSE 경로 전체를 no-op으로 만들거나 AoS·Archetype의 branchless 경로에서 속도 반전을 제거해도 ctest가 그대로 통과했고, 세 레이아웃 20곳의 경계 반사를 전부 제거해도 통과했다(테스트가 전부 구현 간 동등성 비교라 불변식 오라클이 없다). 문서 쪽에서는 자동 테스트와 CopyState가 README·step01~08 어디에도 등장하지 않고, README 토글 표의 이론 문서 상호참조 6건 중 4건이 실제 parts 번호와 어긋나며, step05와 step08이 같은 워크로드의 분기 예측 가능성을 정반대로 서술한다. step 문서 완료 기준 2·4·7은 체계적으로 지켜지지 않았다.

### 1. [높음] PROJECTS_PLAN이 요구한 "자료구조 결과 동등성" 자동 검증이 없고, measure/ 모듈 전체가 테스트 타깃에 포함되지 않음

<sub>분류: 검증력부족</sub>

**근거**

```text
C:/Users/REIN/Documents/VeLog/PROJECTS_PLAN.md:31 — "`parts_perf_lab`: 할당 정렬·크기 클래스·해제, scalar/SIMD 허용 오차, 자료구조 결과 동등성"
C:/Users/REIN/Documents/VeLog/projects/mini/parts_perf_lab/CMakeLists.txt:60-70 — parts_core_tests 소스 목록: tests/core_tests.cpp, sim/{soa,aos,archetype,factory}.cpp, alloc/{malloc,arena,pool,slab}_allocator.cpp. measure/container_benchmark.cpp, measure/event_benchmark.cpp 없음.
C:/Users/REIN/Documents/VeLog/projects/mini/parts_perf_lab/tests/core_tests.cpp 전체 5개 테스트: TestAlignmentHelpers, TestMallocAndArena, TestPoolAndSlab, TestLayoutEquivalence, TestBranchAndSimdEquivalence. Container/Event 관련 심볼 없음(grep으로 lab 전체 확인).
```

vector / std::list / IntrusiveList 세 컨테이너는 같은 시드(0xBADCAFE)로 같은 값 집합을 채우므로 결과 동등성(또는 부동소수 합산 순서 차이의 허용 범위)을 검증할 수 있는 구조인데, 이를 확인하는 테스트가 없다. ContainerBenchmark::LastSum()은 정의만 있고 호출자가 lab 전체에 하나도 없다. 마찬가지로 EventBenchmark가 문서에서 "정상 신호"로 규정한 계약(프레임 종료 시 LiveBytes()==0, lastAllocCount_ == target_)도 자동 검증되지 않는다. 결과적으로 6개 토글 축 중 allocator 워크로드와 container 워크로드 두 축은 어떤 자동 테스트도 통과하지 않는 코드다.

**방향**: CMakeLists의 parts_core_tests 소스에 measure/container_benchmark.cpp와 measure/event_benchmark.cpp를 추가하고, (1) 세 컨테이너의 NodeCount()와 LastSum()이 허용 오차 안에서 일치하는지, (2) RunFrame 후 alloc.LiveBytes()==0 및 LastFrameAllocCount()==Target()인지, (3) Pool 고갈 시 count가 blockCount_에서 멈추는지(실패 주입)를 검증하는 테스트를 추가한다.

### 2. [높음] 변이 테스트: Archetype SIMD 경로와 AoS·Archetype branchless 경로가 무력화돼도 ctest 통과

<sub>분류: 검증력부족</sub>

**근거**

```text
복사본(scratchpad/ppl_mut)에서 파일별 단일 지점 변이 후 parts_core_tests 전체 재컴파일·실행:
[M1 archetype UpdateSSE: vx = SseSelect(vx,_mm_sub_ps(zero,vx),mX); -> vx = vx;] -> SURVIVED (tests still pass)
[M2 AoS branchless: e.velX = outX ? -e.velX : e.velX; -> e.velX = e.velX;] -> SURVIVED
[M3 archetype branchless: c.velX[i] = outX ? -c.velX[i] : c.velX[i]; -> c.velX[i] = c.velX[i];] -> SURVIVED
[M10 ArchetypeSimulation::UpdateSSE 본문 전체를 즉시 return] -> SURVIVED
대조군 [M4 SoA UpdateSSE 동일 변이] -> KILLED (tests fail)
```

C:/Users/REIN/Documents/VeLog/projects/mini/parts_perf_lab/tests/core_tests.cpp:104-126 TestLayoutEquivalence는 세 레이아웃을 모두 Update(dt, SimdWidth::Scalar, false)로만 돌리고, :128-146 TestBranchAndSimdEquivalence는 SoA 인스턴스 세 개만 사용한다(Scalar/false, Scalar/true, SSE/true). 그 결과 ArchetypeSimulation::UpdateSSE, ArchetypeSimulation::UpdateScalar의 branchless 분기, AoSSimulation::UpdateScalar의 branchless 분기는 테스트에서 단 한 번도 실행되지 않는다. README.md:15-22가 활성으로 표시한 6축 중 layout×simd, layout×branchless 조합의 대부분이 무검증 상태다.

**방향**: TestLayoutEquivalence를 {Layout} × {Scalar, SSE} × {branchless false,true} 조합 순회로 확장해 모든 조합이 SoA/Scalar/false baseline과 일치하는지 확인한다. 엔티티 수 513은 SSE 잔여(513%4=1)와 Archetype 부분 chunk(2*256+1)를 이미 잘 자극하므로 그대로 두면 된다.

### 3. [높음] 불변식 오라클 부재 — 세 레이아웃 전체에서 경계 반사를 제거해도 테스트가 통과한다

<sub>분류: 검증력부족</sub>

**근거**

```text
soa.cpp / aos.cpp / archetype.cpp 세 파일에서 속도 반전 20곳(`= -vel*`, `? -vel*`, `_mm_sub_ps(zero,vx|vy)`)을 일괄 제거하고 세 파일 모두 재컴파일(빌드 로그에 aos.cpp / archetype.cpp / soa.cpp 확인) 후 실행:
"parts core tests passed"
[M5] reflection removed at 20 sites across 3 layouts; testExit=0
```

tests/core_tests.cpp의 정확성 검증은 전부 구현 간 동등성 비교(NearStates)다. 이론 주장에 해당하는 불변식 — 갱신 후 모든 엔티티가 [0, w-1]×[0, h-1] 안에 있다, 경계에 닿은 엔티티의 속도 부호가 뒤집힌다, |v|가 보존된다 — 을 확인하는 절대 기준이 없다. 그래서 워크로드의 물리 규칙 자체를 통째로 삭제해도 모든 구현이 여전히 서로 같은 값을 내므로 테스트가 녹색이다. articles/step01_skeleton.md:130이 워크로드를 "N개의 점이 임의 초기 속도로 출발해 화면 경계에서 반사하며 떠다닌다"로 규정했지만 그 규정을 검사하는 코드는 없다.

**방향**: CopyState 스냅숏에 대해 (a) 0 <= posX <= viewportW-1 및 posY 동일, (b) 초기 |v|와 갱신 후 |v|의 성분별 절댓값 집합 보존, (c) 경계를 넘도록 강제한 엔티티에서 velX 부호가 실제로 반전되는지(실패 주입)를 검사하는 불변식 테스트를 추가한다.

### 4. [높음] 자동 테스트와 ISimulation::CopyState가 lab의 어떤 문서에도 존재하지 않음 (완료 기준 3·7 위반)

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
grep "CopyState|core_tests|ctest|parts_core_tests|NearStates" over C:/Users/REIN/Documents/VeLog/projects/mini/parts_perf_lab/articles -> "No matches found"
C:/Users/REIN/Documents/VeLog/projects/mini/parts_perf_lab/README.md:52-76 디렉터리 구조 블록에 tests/ 없음; :78-85 빌드·실행 절에 ctest 명령 없음(README 전체 grep에 test/테스트/검증 3건뿐이며 모두 다른 맥락).
최종 코드: C:/Users/REIN/Documents/VeLog/projects/mini/parts_perf_lab/src/sim/isimulation.h:33 `virtual void CopyState(std::vector<float>& out) const = 0;`, CMakeLists.txt:59-73 `enable_testing()` / `add_executable(parts_core_tests ...)` / `add_test(NAME parts_core_tests ...)`.
```

step03_layout.md:85-99가 제시한 ISimulation 스니펫에는 CopyState가 없고, 이후 어떤 step도 이 순수 가상 함수의 추가를 다루지 않는다. 즉 문서의 코드만 따라 만들면 세 레이아웃 클래스가 추상 클래스로 남아 최종 코드와 컴파일 자체가 달라진다. 완료 기준 3("현재 step에서 추가·변경한 코드가 식별된다")과 7("최종 코드와 문서 코드가 드리프트하지 않도록 스니펫 검증을 둔다")을 동시에 위반한다. 더 넓게는, 이 lab의 유일한 자동 검증 수단이 lab 자신의 README에서 보이지 않는다(상위 projects/mini/README.md:35과 VERIFY.md:35에서만 언급).

**방향**: README에 tests/ 디렉터리와 `ctest --test-dir build -C Release --output-on-failure` 절차를 추가하고, CopyState 도입과 core_tests 추가를 담당하는 step 문서를 하나 두거나 해당 step(레이아웃/SIMD)에 편입한다.

### 5. [중간] README 토글 표의 이론 문서 상호참조 4건이 실제 parts 번호와 어긋남

<sub>분류: 문서오류</sub>

**근거**

```text
C:/Users/REIN/Documents/VeLog/projects/mini/parts_perf_lab/README.md:18-22
:18 "| 메모리 레이아웃 | SoA / AoS / Archetype | parts/05 데이터 레이아웃 |"  -> 실제 parts/part05_smart_pointer.md
:19 "| SIMD 폭 | 스칼라 / SSE / AVX2 | parts/08 SIMD |"  -> 실제 parts/part08_vector.md (SIMD는 part19_simd.md)
:20 "| 분기 정렬 | on / off (predication 토글) | parts/09 분기 예측 |"  -> 실제 parts/part09_intrusive_list.md (분기 예측은 part21_branch_prediction.md)
:22 "| 컨테이너 | vector / std::list / Intrusive | parts/06 STL 컨테이너 비용 |"  -> 실제 parts/part06_string.md
(:21 "parts/03 메모리 할당자"만 parts/part03_memory_allocator.md와 일치)
```

README가 각 토글 축이 검증한다고 선언한 이론 문서 6개 참조 중 4개가 존재하지 않는 매핑이다. 독자가 표를 따라 parts/05, parts/08, parts/09, parts/06을 열면 각각 스마트 포인터, std::vector, intrusive list, 문자열 글이 나온다. 데이터 레이아웃은 part18_dod.md·part11_ecs.md, SIMD는 part19_simd.md, 분기 예측은 part21_branch_prediction.md가 대응 문서다.

**방향**: 표의 '비교 대상' 열을 parts/18(DOD)·parts/11(ECS), parts/19(SIMD), parts/21(분기 예측), parts/03·04(할당자), parts/08·09(vector·intrusive list)로 정정한다.

### 6. [중간] AVX2 경로는 어떤 빌드 구성에서도 테스트 타깃에 컴파일되지 않음

<sub>분류: 검증력부족</sub>

**근거**

```text
C:/Users/REIN/Documents/VeLog/projects/mini/parts_perf_lab/CMakeLists.txt:111-118 — PERF_LAB_AVX2 정의와 /arch:AVX2·-mavx2 -mfma 가 모두 `${PROJECT_NAME}`(앱 타깃)에만 부여되고 parts_core_tests에는 부여되지 않음.
실증: `cmake -DPERF_LAB_ENABLE_AVX2=ON` 구성 후 생성물 검사
  parts_core_tests.vcxproj: PERF_LAB_AVX2 / AdvancedVectorExtensions 매치 0건
  parts_perf_lab.vcxproj:  <EnableEnhancedInstructionSet>AdvancedVectorExtensions2</EnableEnhancedInstructionSet>, PreprocessorDefinitions에 PERF_LAB_AVX2=1
```

README.md:19,24와 articles/step04_simd.md가 AVX2를 활성 토글로 선언하지만, SoASimulation::UpdateAVX2 / ArchetypeSimulation::UpdateAVX2는 `#if defined(PERF_LAB_AVX2)` 가드 안에 있어 테스트 바이너리에서는 아예 컴파일되지 않는다. 따라서 PROJECTS_PLAN.md:31의 "scalar/SIMD 허용 오차" 검증은 SoA의 SSE 경로 하나에만 적용되며, AVX2 경로의 FMA 사용으로 인한 결과 차이(스칼라 대비 중간 반올림 없음)를 확인할 수단이 전혀 없다.

**방향**: AVX2 활성화 블록의 target_compile_definitions/options를 parts_core_tests에도 적용하고, TestBranchAndSimdEquivalence에 AVX2 경로 비교 케이스를 `#if defined(PERF_LAB_AVX2)` 가드로 추가한다.

### 7. [중간] 분기 예측 축: 이론의 핵심 실험(정렬/랜덤 분기 패턴)이 구현되지 않았고 이름만 "분기 정렬"로 남음

> **[반영됨 · 잔여 있음 · 2026-08-13]** 방향의 대안 경로를 택했다 — README 토글 표의 축 이름이 `분기 정렬` → `branchless predication`으로 바뀌었고 step04의 "데이터 정렬 토글을 추가하면" 예고도 제거됐다. **잔여**: 정렬/랜덤 분기 패턴 토글 자체는 미구현이고 파일명 `step05_branch_sort.md`도 그대로다.

<sub>분류: 문서대응누락</sub>

**근거**

```text
이론: C:/Users/REIN/Documents/VeLog/parts/part21_branch_prediction.md:614 "### 정렬 vs 비정렬 데이터", :238 "**시나리오 1: 정렬된 데이터** - 분기 패턴이 예측 가능하다"
예고: articles/step04_simd.md:511 "분기 패턴을 *데이터 정렬* 로 통제해 분기 예측 미스율을 낮추는 토글을 추가하면..."
실제 구현: 파일명 articles/step05_branch_sort.md 이지만 조작 변수는 predication 하나뿐이고 정렬·랜덤 패턴 토글이 없음. README.md:20 축 이름은 "분기 정렬"인데 옵션은 "on / off (predication 토글)".
본인 서술: step05_branch_sort.md:39 "이 도구의 경계 반사 분기는 *대부분 false* 다. ... 적중률 99% 이상이 나오고, 분기 비용은 측정에서 거의 안 드러난다."
```

조작 변수가 분기 예측 가능성이 아니라 코드 생성 형태(if vs ternary)이고, 통제 워크로드는 미스율이 1% 미만이라 이론 주장(미스 1회당 15~20 사이클)이 관측될 수 없다. step05:211도 "현재 계기는 실행 시간만 기록하므로 *분기 예측 미스율* 자체를 설명하지 못한다"고 인정한다. 즉 이 축은 실패 주입(예측 불가능한 분기 입력)이 없어 잘못된 구현이 드러날 조건이 없다.

**방향**: 엔티티 초기 상태를 정렬(예: 경계 근접 여부로 파티션)한 세트와 무작위 세트를 토글로 전환할 수 있게 하거나, 워크로드에 데이터 의존적이며 50% 확률로 갈리는 분기를 추가한다. 그게 어렵다면 README와 파일명에서 "분기 정렬" 표현을 predication으로 정정한다.

### 8. [중간] step05와 step08이 같은 워크로드의 분기 예측 가능성을 정반대로 서술

<sub>분류: 문서오류</sub>

**근거**

```text
C:/Users/REIN/Documents/VeLog/projects/mini/parts_perf_lab/articles/step05_branch_sort.md:39 — "이 도구의 경계 반사 분기는 *대부분 false* 다. 1만 개 점 중 매 프레임 *경계에 닿는 점* 은 전체의 1% 미만. CPU는 거의 항상 false로 예측해 적중률 99% 이상이 나오고"
C:/Users/REIN/Documents/VeLog/projects/mini/parts_perf_lab/articles/step08_visualization.md:308 — "이 도구의 워크로드는 *모든 점이 같은 분기 결과* 가 아니라 *반반 갈라지는* 입력이라 branch predictor가 별로 도움이 되지 않는다."
```

두 문장은 같은 시뮬레이션(1만~10만 점, 매 프레임 경계 반사)에 대한 상반된 주장이며, 코드가 뒷받침하는 것은 step05 쪽이다. src/sim/soa.cpp:85-92의 분기는 pos가 [0, w-1] 밖으로 나갈 때만 참인데, 속도 ±120 px/s, dt≈1/수백초, 뷰포트 1280×580이므로 한 프레임에 경계를 넘는 점의 비율은 1%보다도 작다. step08의 서술을 근거로 시나리오 B를 수행하는 독자는 잘못된 기대치를 갖게 된다.

**방향**: step08:308을 step05:39과 같은 사실(경계 반사 분기는 대부분 예측 성공)로 정정하고, 그 결과 branchless 토글의 효과가 작게 나오는 이유를 자동 벡터화 가능성 차이로 설명한다.

### 9. [중간] step 문서 완료 기준 2(후속 step 예고 금지) 체계적 위반

> **[반영됨 · 잔여 있음 · 2026-08-13]** `articles/`에서 "이후 단계"·"그게 확장의 토대다"가 0건이 됐고 `src/ui/toggles.h` 주석도 갱신됐다. **잔여**: `scripts/check_project_docs.py`의 `FORWARD_PROMISE_RE`에 `이후\s*단계`가 추가되지 않아 가드는 여전히 이 표현을 못 잡는다.

<sub>분류: 문서오류</sub>

**근거**

```text
C:/Users/REIN/Documents/VeLog/PROJECTS_PLAN.md:45 "2. 후속 step을 예고하지 않는다."
grep -c "이후 단계": step01_skeleton.md:11, step02_toggles.md:5, step03_layout.md:3 (총 19곳)
grep -n "확장의 토대": step01:507, step02:569, step03:588, step04:511, step05:227, step06:491, step07:411 (7곳, 모두 다음 step 주제를 명시)
예: step01_skeleton.md:13 "이후 단계에서 raygui 토글, 프레임 그래프, 레이아웃 변종(AoS/Archetype), SIMD 경로, 할당자 비교, perf 카운터가 차례로 추가된다."
예: step06_allocator.md:491 "같은 워크로드를 `std::list` 또는 *intrusive list* 로 처리하면 ... 측정에 들어온다. 그게 확장의 토대다."
```

소스 주석에도 같은 예고가 남아 있다(src/ui/toggles.h:8 "step 2(토글 시스템)는 인프라만 만든다. 실제 동작은 후속 step에서 각 모듈을 채울 때 연결한다.", src/sim/soa.h 인접 주석은 정상). 저장소 자체 검사기 scripts/check_project_docs.py의 FORWARD_PROMISE_RE는 "(다음|후속)\s*단계"만 잡고 "이후 단계"는 패턴에 없어 `python scripts/check_project_docs.py` 실행 결과가 "Checked 80 project documents: 0 error(s)"로 통과한다. 즉 가드가 있는데 이 문서군의 표현이 가드를 우회한다.

**방향**: "이후 단계" 19곳과 결론부 "그게 확장의 토대다" 7곳을 현재 step의 한계 기술로 바꾸고, check_project_docs.py의 FORWARD_PROMISE_RE에 "이후\s*단계"를 추가한다.

### 10. [중간] step04의 CMake 스니펫이 최종 CMakeLists의 아키텍처 가드를 누락하고, 바로 아래 산문과도 모순

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
C:/Users/REIN/Documents/VeLog/projects/mini/parts_perf_lab/articles/step04_simd.md:115-122 스니펫:
  if(PERF_LAB_ENABLE_AVX2)
      target_compile_definitions(${PROJECT_NAME} PRIVATE PERF_LAB_AVX2=1)
      if(MSVC) ... /arch:AVX2 else() ... -mavx2 -mfma
  endif()
같은 문서 :125 산문: "비-x86/x64 타깃에서 이 옵션을 켜도 CMake는 AVX2 컴파일 플래그를 붙이지 않고, SIMD 경로는 scalar로 폴백한다."
최종 코드 CMakeLists.txt:105-124: PERF_LAB_TARGET_X86 판정(CMAKE_CXX_COMPILER_ARCHITECTURE_ID / CMAKE_SYSTEM_PROCESSOR) 후 x86일 때만 정의·플래그 부여, 아니면 message(WARNING).
```

스니펫대로 따라 하면 ARM 타깃에서 -mavx2 -mfma가 그대로 붙어 컴파일이 깨지므로, 문서가 그 줄 바로 아래에서 보장한다고 적은 동작이 스니펫으로는 성립하지 않는다. 완료 기준 4("문서의 코드만 따라도 해당 상태를 빌드할 수 있다")와 7(드리프트 금지)을 동시에 위반한다. step04:159가 "Apple Silicon에서 cmake -DPERF_LAB_ENABLE_AVX2=ON -> 컴파일 OK"라고 단언하는 것도 스니펫 기준으로는 거짓이다.

**방향**: step04의 CMake 스니펫을 최종 CMakeLists.txt:105-124의 PERF_LAB_TARGET_X86 가드 포함 형태로 교체한다.

### 11. [중간] 문서가 관측하라고 지시한 HUD 항목(live bytes)과 확인 절차(LastSum)가 최종 앱에 없음

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
C:/Users/REIN/Documents/VeLog/projects/mini/parts_perf_lab/README.md:34 "할당 통계 — `IAllocator` 내장 카운터(프레임당 호출 수·바이트, live/peak bytes)."
articles/step06_allocator.md:408 표에 "| **live bytes** | 현재 살아 있는 메모리 (Reset 후 0이 정상) |", :440 "화면 좌측 stats 영역 하단에 `alloc ms`, `alloc count`, `alloc bytes`, `live bytes` 가 매 프레임 갱신된다", :460 검증 체크리스트 "live bytes가 0이 아님 -> Reset 누락 또는 Deallocate 누락"
최종 코드 src/ui/hud.cpp:72-89 DrawStats 포맷 문자열: "alloc ms  : %6.3f  (%llu allocs, %llu B)" / "container : %6.3f ms  (%zu nodes)" — LiveBytes/PeakBytes 없음.
grep LiveBytes|PeakBytes over src/: iallocator.h와 arena/pool/slab의 ResetLiveBytes 호출만 매치. hud.cpp·main.cpp에서 호출 0건.
grep LastSum over src/: container_benchmark.h:43 정의 1건뿐, 호출자 없음.
```

README와 step06이 출력물로 선언한 live/peak bytes가 실제 HUD에 표시되지 않으므로, step06:460과 step07:384가 제시한 검증 체크리스트를 독자가 코드 수정 없이 수행할 수 없다. step07:137이 LastSum()의 존재 이유로 든 "필요하면 디버그 표시로 잠깐 출력해 sum이 실제로 변하는지 확인"도 표시 경로가 없어 성립하지 않으며, 앱은 /GL /LTCG로 빌드되는데(CMakeLists.txt:128-133) 이 값을 읽는 코드가 하나도 없어 루프 제거 방지 근거로도 약하다.

**방향**: HUD DrawStats에 live/peak bytes 한 줄을 추가하거나 README:34와 step06의 해당 서술을 실제 출력 항목으로 정정한다. LastSum은 HUD 표시 또는 자동 테스트 중 한 곳에서 실제로 읽도록 만든다.

### 12. [중간] step 문서 완료 기준 4·7 미충족 — 스니펫 생략과 드리프트 허용 선언, 검증 장치 부재

> **[이월 · 2026-08-13]** step02의 토글 패널 생략 블록과 step04의 SSE/AVX2 잔여 루프 생략 블록은 전문으로 채워졌다. 그러나 핵심인 **스니펫 검증 장치가 여전히 없고**, `step07_container.md:270`의 `// ... (기존 시뮬, allocator, EventBenchmark 초기화)` 생략과 여섯 문서가 공유하는 드리프트 허용 문장("디스크의 통합 코드는 누적되어 더 진행된 상태일 수 있다")도 그대로다.
>
> **[반영됨 · 2026-08-14]** 세 갈래 전부 닫혔다.
> (a) `step07_container.md`의 생략 블록을 실제 소스로 채웠다 — 지적된 문자열
> `// ... (기존 시뮬, allocator, EventBenchmark 초기화)`는 이제 lab 어디에도 없다.
> (b) 드리프트 허용 문장을 **여섯 문서 전부에서 제거**했다. 백업본에는 step02~07 여섯 파일에
> 모두 있었고, 지금 `projects/mini/` 전체에서 이 문장이 남은 곳은 이 보고서의 인용문뿐이다.
> (c) 검증 장치는 `scripts/check_snippets.py`로 신설됐다(아래 [2차 반영](#2차-반영-2026-08-14) 참조).
> 이 lab에 앵커 **13개**를 달았다 — `step04_simd.md` 2개(`src/platform.h` 파일 앵커,
> `src/sim/soa.cpp#SoASimulation::UpdateSSE` 심볼 앵커), `step07_container.md` 11개.
> 즉 위 "생략 블록을 채웠다"는 주장이 이제 검사기가 지키는 주장이다.

<sub>분류: 문서오류</sub>

**근거**

```text
C:/Users/REIN/Documents/VeLog/PROJECTS_PLAN.md:47,50 "4. 문서의 코드만 따라도 해당 상태를 빌드할 수 있다." / "7. 최종 코드와 문서 코드가 드리프트하지 않도록 스니펫 검증을 둔다."
articles/step02_toggles.md:358 "// 3행 layout, 우측 컬럼 container/simd/branch는 같은 패턴.\n// 코드 분량 줄이기 위해 생략. 디스크의 src/ui/hud.cpp 전체를 참조."
articles/step04_simd.md:397-402 "// ... (SoA UpdateSSE의 핫 루프와 동일한 패턴)" / "// ... (scalar)", :360 "// ... (scalar 루프, SSE와 동일)"
articles/step07_container.md:270 "// ... (기존 시뮬, allocator, EventBenchmark 초기화)"
step02:15 / step03:17 / step04:15 / step05:13 / step06:15 / step07:15 공통 문장 "이 글의 코드 스니펫은 *해당 단계의 스냅샷* 이다. 디스크의 통합 코드는 누적되어 더 진행된 상태일 수 있다."
scripts/ 아래 스니펫-소스 대조 도구 없음(check_project_docs.py는 헤딩·구분선·전방참조 정규식만 검사).
```

기준 4는 문서만으로 해당 step 상태를 빌드할 수 있어야 한다고 요구하는데, step02는 5개 콤보 중 2개만 보여 주고 나머지를 디스크 참조로 넘긴다(그 상태로는 step02가 선언한 "콤보박스 5개 + 체크박스 1개" 화면이 나오지 않는다). 기준 7이 요구한 스니펫 검증 장치는 저장소 어디에도 없고, 대신 여섯 문서가 드리프트 가능성을 명시적으로 허용하는 문장을 공유한다. 실제 드리프트 사례는 별도 항목(CopyState·tests 미문서화, step04 CMake 가드 누락, live bytes)으로 확인된다.

**방향**: 각 step의 스니펫을 최종 소스에서 마커 기반으로 추출하거나, 문서 코드 블록과 src/ 실제 코드의 일치를 검사하는 스크립트를 scripts/에 추가한다. 생략 블록은 최소한 컴파일 가능한 전체 형태로 채운다.

### 13. [중간] algorithms/ 12편에 대응하는 측정이 lab에 없음

<sub>분류: 문서대응누락</sub>

**근거**

```text
C:/Users/REIN/Documents/VeLog/algorithms/ = README.md + part01~part11 (분석·정렬/선택·스트리밍·문자열 사전·그래프·최단경로·그리디·DP·조합탐색·압축·property/fuzz 검증)
C:/Users/REIN/Documents/VeLog/projects/mini/parts_perf_lab/README.md:7-9 "## 짝 시리즈\n\n- **parts** — ..." (algorithms 언급 없음)
grep -i "algorithms" over projects/mini/README.md, projects/mini/parts_perf_lab/README.md, PROJECTS_PLAN.md -> 매치 0건. projects/mini/*/README.md 중 "algorithms/"를 참조하는 파일 0건.
src/ 전체에 정렬·탐색·그래프·DP·압축 관련 코드 없음(6축은 layout/SIMD/branch/allocator/container/entity count).
```

이 lab의 대응 이론 축으로 지정된 두 시리즈 중 algorithms/ 쪽은 lab이 다루지도 않고 다룬다고 주장하지도 않는다. 특히 algorithms/part11_property_fuzz_verification.md(property/fuzz 검증)의 기법은 이 lab의 검증력 부족(불변식 오라클 부재)을 직접 메울 수 있는 주제인데 반영되어 있지 않다. 또한 README.md:89의 "이 lab은 parts 시리즈의 핵심 측정을 모두 재현 가능하게 만든다"는 parts 24편 중 실제 대응이 있는 것이 약 6편(03·04 할당자, 08·09 컨테이너, 11·18 레이아웃, 19 SIMD, 21 분기 일부)이라는 점에서도 과장이다.

**방향**: README의 짝 시리즈 표기를 실제 대응 범위(parts 03·04·08·09·11·18·19·21)로 좁혀 적고, algorithms/ 축은 별도 lab을 두거나 커리큘럼 매핑 표에서 대응 lab 없음을 명시한다.

### 14. [낮음] src/ui/toggles.h의 주석이 최종 코드와 어긋남 (grayed out 토글은 존재하지 않음)

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
C:/Users/REIN/Documents/VeLog/projects/mini/parts_perf_lab/src/ui/toggles.h:8-9 — "// step 2(토글 시스템)는 인프라만 만든다. 실제 동작은 후속 step에서 각 모듈을 채울 때 연결한다.\n// 미동작 토글은 HUD에서 grayed out으로 표시한다."
grep "GuiSetState|STATE_DISABLED" over C:/Users/REIN/Documents/VeLog/projects/mini/parts_perf_lab/src -> 매치 0건
src/ui/hud.cpp:92-179 DrawTogglePanel — 6개 위젯 모두 비활성 처리 없이 그려지고, README.md:26 "여섯 축 ... 이 모두 활성 상태다"
```

최종 소스에 남은 주석이 이미 지난 중간 상태를 현재형으로 서술한다. 이 주석만 읽는 독자는 존재하지 않는 grayed-out 동작을 찾게 되고, 소스 안에 step 번호 기반 예고가 남아 완료 기준 2의 취지에도 어긋난다.

**방향**: toggles.h:8-9 주석을 현재 상태(6축 모두 활성, RequiresRestart가 재구성 대상을 구분)로 갱신한다.

### 15. [낮음] 계산 코어가 렌더링과 분리되지 않아 headless 테스트가 raylib을 링크해야 함

<sub>분류: 검증력부족</sub>

**근거**

```text
C:/Users/REIN/Documents/VeLog/PROJECTS_PLAN.md:29 "렌더링과 UI에서 계산 코어를 분리하고 최소한 다음 자동 검증을 둔다."
src/sim/soa.cpp:3 `#include "raylib.h"` (aos.cpp:3, archetype.cpp:3 동일) — Update/CopyState와 Draw가 같은 번역 단위에 있음
CMakeLists.txt:64-72 — parts_core_tests가 sim/*.cpp를 그대로 컴파일하고 `target_link_libraries(parts_core_tests PRIVATE raylib)`
빌드 로그: parts_core_tests 빌드 시 glfw3.lib, raylib.lib 선행 빌드 필요
```

Update/CopyState 자체는 raylib에 의존하지 않지만 Draw()가 같은 .cpp에 있어 테스트 바이너리가 GLFW·OpenGL을 링크한다. ctest 자체는 통과하지만(0.52초), 계산 코어의 독립 검증이라는 선언과 달리 그래픽 스택 없이는 테스트를 빌드할 수 없다.

**방향**: 각 레이아웃의 Draw()를 별도 .cpp(예: sim/*_draw.cpp)로 분리해 테스트 타깃에서 제외하고 raylib 링크를 떼거나, ISimulation에서 Draw를 분리한 렌더러 어댑터로 옮긴다.

---

## languages_lab — 보완 필요 (12건)

학습 대상 대체는 전혀 없다. CMakeLists.txt에 find_package·target_link_libraries·FetchContent가 0건이고, src/ 4개 파일의 `#include`는 전부 C++ 표준 라이브러리(charconv, cmath, queue 등)뿐이라 렉서·파서·바이트코드·검증기·VM·호스트 바인딩이 모두 자작이다. 이 축에서 가장 대체되기 쉬운 부분(파서 생성기, 스크립트 VM 임베딩)이 하나도 외주화되지 않았고 구현 품질도 높다 — 손수 만든 probe로 분기 합류 불일치 거부, 음수 점프 거부, 3행 12열 진단 위치, 좌결합(8-3-2=3), 미등록 호스트 함수 거부, VM 스택 한도, 비유한 값 차단이 전부 정상 동작함을 확인했다. 문제는 검증력이다. 사본에서 변이 12건을 주입해 전체 재빌드 후 ctest를 돌린 결과 8건이 생존했고(M1 토큰 위치, M2 분기 합류 깊이, M3 결합 법칙, M4 스택 한도, M5 비유한 값, M6 미등록 네이티브, M7 Halt 종결, M9 0 나눗셈), 검출된 것은 4건(초기화되지 않은 전역, 명령 예산, 정적 피연산자 범위, 스택 언더플로)뿐이다. 즉 PROJECTS_PLAN.md:32가 languages_lab에 명시적으로 요구한 네 항목 중 "토큰 위치", "바이트코드 제어 흐름(합류)", "호스트 바인딩"의 핵심 계약이 자동 테스트로 잠겨 있지 않다. 여기에 더해 파서 재귀에 깊이 한도가 없어 40KB짜리 중첩 괄호 스크립트가 프로세스를 STATUS_STACK_OVERFLOW(0xC00000FD)로 죽이는데, 이는 "신뢰하지 않는 스크립트의 자원 한도"라는 lab 자신의 주장에 대한 실제 반례다. 문서 쪽은 step 완료 기준 4·7(문서 코드만으로 빌드, 스니펫 드리프트 방지)이 미충족이며 대조 장치도 없다.

### 1. [높음] PROJECTS_PLAN이 요구한 "토큰 위치" 자동 검증이 사실상 없다 (위치 추적을 상수로 고정해도 전체 테스트 통과)

<sub>분류: 검증력부족</sub>

**근거**

```text
C:/Users/REIN/Documents/VeLog/PROJECTS_PLAN.md:32 `- \`languages_lab\`: 토큰 위치·우선순위, 바이트코드 제어 흐름·스택 검증, 실행 예산, 호스트 바인딩`
위치를 확인하는 단 하나의 단언은 C:/Users/REIN/Documents/VeLog/projects/mini/languages_lab/src/tests.cpp:70 `CHECK(syntax.diagnostics.front().location.line == 1);` 뿐이다. 그런데 C:/Users/REIN/Documents/VeLog/projects/mini/languages_lab/src/language.hpp:18-21 에서 `struct SourceLocation { int line = 1; int column = 1; };` 이므로 이 단언은 위치 추적이 전혀 동작하지 않아도 참이다. `column`을 검사하는 단언은 lab 전체(tests.cpp/main.cpp/bench.cpp)에 0건(grep `column` 결과 tests.cpp 히트 없음).
변이 검증(사본 C:/Users/REIN/AppData/Local/Temp/.../scratchpad/langlab): `const SourceLocation start{line_, column_};` → `{1, 1}`, End 토큰도 `{1,1}` 로 고정 후 전체 재빌드(-t:Rebuild) → `M1_lexer_position_frozen -> TESTS PASS (mutation SURVIVED)`
한편 실제 구현은 정상이다(probe: `lexer_position: diagnostics=1 first="3:12: 알 수 없는 문자: @"` — 3행 12열 정확).
```

step01은 문서 전체가 "위치를 보존한 의미 계약"을 주장하고(step01_lexical_contract.md:117-118), README 최종 구현 첫 줄도 "줄·열 정보를 보존하는 렉서와 오류 진단"이다. 구현은 옳지만 회귀를 잡을 자동 검증이 0이다. `line == 1`을 1행짜리 소스에 대해 확인하는 것은 구조상 항등식 단언이다.

**방향**: 여러 줄 소스에 대해 진단의 (line, column) 쌍을 정확한 값으로 단언하는 테스트를 추가한다. 예: `"let a = 1;\nlet b = 2;\n   let c = @;\n"` → 진단이 `3:12`인지 검사. 토큰별 location을 노출해 `==`/`!=` 같은 2문자 토큰의 시작 열도 확인하면 최대 길이 일치 규칙까지 고정된다.

### 2. [높음] 검증기의 핵심 기능인 "분기 합류점 스택 깊이 일치" 검사가 자동 테스트에 없다 (무력화해도 통과)

<sub>분류: 검증력부족</sub>

**근거**

```text
C:/Users/REIN/Documents/VeLog/projects/mini/languages_lab/src/language.hpp:576-582:
```
            if (depth[target] != nextDepth) {
                error = "분기 합류점의 스택 깊이가 일치하지 않는다";
                return false;
            }
```
step03_bytecode_verifier.md:76-92 (`## 4. 분기 합류`)가 이 검사를 검증기의 중심으로 설명하고, step03:109 는 "한 분기에서만 값을 남기기"를 실패 주입 항목으로 적는다.
그러나 tests.cpp의 유일한 검증기 테스트 `TestVerifierRejectsMalformedProgram`(tests.cpp:86-99)은 (a) `Pop`+`Halt` 언더플로, (b) 도달 불가 `Constant 99` 두 가지뿐이다. 분기 자체가 없는 프로그램이라 합류 경로가 실행되지 않는다.
변이 검증: `enqueue`의 불일치 분기를 제거하고 `return true;`만 남김 → `M2_verify_merge_depth_check_disabled -> TESTS PASS (mutation SURVIVED)`
구현 자체는 동작한다(probe로 손수 만든 불균형 분기 프로그램: `merge_mismatch: verify=false error="분기 합류점의 스택 깊이가 일치하지 않는다"`).
```

PROJECTS_PLAN.md:32가 요구하는 "바이트코드 제어 흐름" 검증이 바로 이 항목이다. 현재 테스트가 보장하는 것은 선형 언더플로와 정적 피연산자 범위뿐이며(변이 M12·M13은 정상적으로 검출됨), 제어 흐름 그래프 부분은 전혀 잠겨 있지 않다. 음수 점프 대상, `Halt`로 끝나지 않는 프로그램, "제어 흐름이 코드 밖으로 떨어진다" 경로도 같은 이유로 미검증이다(변이 M7 생존).

**방향**: tests.cpp에 손수 만든 프로그램 세 개를 추가한다. (1) `Constant / JumpIfFalse→3 / Jump→4 / Pop / Halt` 형태로 참·거짓 경로의 깊이를 다르게 만들어 합류 불일치를 유도, (2) `Jump` 대상 -1, (3) 마지막 명령이 `Halt`가 아닌 프로그램. 각각 `Verify`가 false를 반환하고 `error` 문자열이 기대한 사유와 일치하는지까지 단언한다.

### 3. [높음] 호스트 바인딩의 핵심 주장(등록되지 않은 이름 거부)이 "수동 실험"으로만 남아 자동 검증이 없다

<sub>분류: 검증력부족</sub>

**근거**

```text
README.md:13 `5. 호스트 바인딩은 임의의 C++ 호출이 아니라 등록된 이름과 값 변환만 노출해야 한다.`
step05_host_binding.md:98 `수동 실험에서는 \`twice\` 등록 줄을 제거하고 실행한다. 프로세스 크래시나 기본값 대체가 아니라 소스 위치가 붙은 오류가 출력되어야 한다.`
tests.cpp의 `TestHostBoundary`(56-64)는 등록된 `twice`의 성공 경로만 확인한다. `CallNative` 실패 경로를 다루는 단언은 lab 전체에 0건이다(`TestDiagnostics`의 `missing`은 괄호가 없어 `LoadGlobal` 경로다).
변이 검증: language.hpp:683-685의 미등록 함수 Failure를 "인자를 그대로 되돌려주는 항등 함수"로 바꿈 → `M6_unregistered_native_silently_identity -> TESTS PASS (mutation SURVIVED)`
구현은 정상(probe: `unregistered_native: ok=0 msg="등록되지 않은 호스트 함수: ghost"`).
```

PROJECTS_PLAN.md:32의 요구 항목 "호스트 바인딩"과 PROJECTS_PLAN.md:26-27의 "실패 주입 / 자동 테스트" 기준에 직접 걸린다. 샌드박스 경계에서 가장 중요한 것은 허용 목록에 없는 호출이 거부되는가인데, 그 케이스가 자동화되지 않아 권한 폐쇄성 회귀를 아무도 잡지 못한다. step05가 스스로 "수동 실험"이라고 적어 두어 누락이 은폐되지는 않았지만, 완료 기준상 자동 테스트 항목이다.

**방향**: `Compile("let x = ghost(1);")` 후 아무것도 Bind하지 않은 VM으로 실행해 `!run.ok`와 진단 메시지·위치를 단언하는 테스트를 추가한다. 같은 바이트코드를 bind한 VM과 bind하지 않은 VM으로 각각 돌려 결과가 갈리는지 확인하면 "같은 바이트코드, 다른 권한 집합"(step05:46-47)이라는 주장까지 검증된다.

### 4. [높음] 파서 재귀에 깊이 한도가 없어 40KB 스크립트가 프로세스를 스택 오버플로로 죽인다 — 자원 한도가 VM에만 있고 프런트엔드에 없다

<sub>분류: 코드오류</sub>

**근거**

```text
README.md:12 `4. 신뢰하지 않는 스크립트에는 명령 수와 스택 크기의 자원 한도가 필요하다.`
step04_vm_runtime.md:97-99 `스택에도 별도 상한을 둔다. ... 손상된 바이트코드나 기능 확장 뒤의 재귀가 프로세스 메모리를 제한 없이 소비하지 못하게 한다.`
그러나 한도는 `VirtualMachine::Run`(language.hpp:616-617 `uint64_t instructionBudget = 100000, size_t stackLimit = 256`)에만 있다. 컴파일러의 `Compiler::Prefix` → `Expression` → `ParsePrecedence` → `Prefix`(language.hpp:391, 393-404, 422-425) 재귀에는 깊이 카운터가 없다.
실측(사본 probe, `let x = ((((...1...))));`):
`parser_recursion(depth=1000): program=1 diagnostics=0  exit=0`
`parser_recursion(depth=20000): compiling 40010 bytes...  exit=-1073741571`
`parser_recursion(depth=100000): compiling 200010 bytes...  exit=-1073741571`
(-1073741571 = 0xC00000FD = STATUS_STACK_OVERFLOW, 출력 중간에 프로세스 사망)
```

lab이 "신뢰하지 않는 스크립트"의 샌드박싱을 확인하는 실험실이라고 선언한 이상, 40KB짜리 텍스트가 VM 예산이 적용되기도 전에 호스트 프로세스를 죽이는 것은 선언된 주장의 정면 반례다. `stackLimit`은 실행 시점 값 스택만 막고, 컴파일 시점 C++ 호출 스택은 무방비다. 게임 런타임에서 스크립트를 로드하는 상황이면 그대로 크래시 경로가 된다.

**방향**: `Compiler`에 재귀 깊이 카운터(예: 최대 128~256)를 두고 초과 시 진단으로 거부한다. 그리고 그 한도를 넘는 소스가 크래시 대신 `program == nullopt` + 진단을 내는지 자동 테스트로 고정한다. 소스 길이·토큰 수 상한도 같은 계약에 넣는 편이 일관적이다.

### 5. [중간] 결합 법칙 검증을 README가 명시했으나 테스트에 없다 — 파서를 우결합으로 바꿔도 통과

<sub>분류: 검증력부족</sub>

**근거**

```text
README.md:11 `2. 연산자 우선순위는 파서 구조에 명시되어야 하며, 괄호와 결합 법칙으로 검증해야 한다.`
step02_precedence_compiler.md:63-64 `오른쪽 피연산자를 한 단계 높은 우선순위로 파싱하므로 같은 우선순위의 \`-\`와 \`/\`는 왼쪽 결합이 된다. \`8 - 3 - 2\`는 \`(8 - 3) - 2\`로 컴파일된다.`
tests.cpp:40 `auto execution = Execute("let a = 2 + 3 * 4; let b = (2 + 3) * 4;");` — 우선순위 2건뿐이고 결합 법칙 단언은 lab 전체에 0건. 비교(`<`)와 상등(`==`)의 상대 우선순위, 단항 `-`의 우선순위도 미검증.
변이 검증: language.hpp:401 `ParsePrecedence(static_cast<Precedence>(static_cast<int>(precedence) + 1));` → `ParsePrecedence(precedence);` (좌결합 → 우결합) → `M3_right_associative_parser -> TESTS PASS (mutation SURVIVED)`
구현은 정상(probe: `associativity: ok=1 a=3.000 b=2.000` — `8-3-2=3`, `16/4/2=2`).
```

`+`, `*`는 결합 법칙이 성립해 우결합으로 바뀌어도 값이 같고, 기존 테스트는 그 두 연산만 쓴다. 그래서 결합 방향이 뒤집혀도 잡히지 않는다. PROJECTS_PLAN.md:32의 "우선순위" 요구는 절반만 충족된 상태다.

**방향**: `let a = 8 - 3 - 2;`(=3), `let b = 16 / 4 / 2;`(=2), `let c = 1 < 2 == 1;`(=1), `let d = -2 - 3;`(=-5) 같은 케이스를 추가한다. 비결합 연산자를 써야 결합 방향이 값에 드러난다.

### 6. [중간] "명령 수와 스택 크기" 두 예산 중 스택 상한이 전혀 검증되지 않는다 (Run의 3번째 인자를 아무도 넘기지 않음)

<sub>분류: 검증력부족</sub>

**근거**

```text
language.hpp:616-617 `RunResult Run(const Program& program, uint64_t instructionBudget = 100000, size_t stackLimit = 256)`, language.hpp:635-636 `if (stack_.size() >= stackLimit) return Failure(location, "VM 스택 한도를 초과했다", executed);`
`stackLimit`을 명시적으로 전달하는 호출은 lab 전체에 0건이다: tests.cpp:31 `result.run = result.vm.Run(*result.compiled.program, budget);`, main.cpp:27 `vm.Run(*compiled.program)`, bench.cpp:35,43 동일. grep `stackLimit` → language.hpp 2줄만 히트.
변이 검증: `if (stack_.size() >= stackLimit)` → `if (false && ...)` → `M4_vm_stack_limit_disabled -> TESTS PASS (mutation SURVIVED)`
구현은 정상(probe로 300단계 중첩 `1 + (1 + (...))` 실행: `stack_limit(depth=300): ok=0 msg="VM 스택 한도를 초과했다"`).
```

README.md:12와 step04 §4가 "시간 예산과 메모리 예산은 서로 대체되지 않는다"고 명시하는데, 실제로 검증되는 것은 시간(명령 수) 예산 하나뿐이다. 명령 예산 쪽은 tests.cpp:83 `CHECK(execution.run.instructions == 251);`로 정확히 고정되어 있어 좋은 테스트다(변이 M11이 정상 검출됨). 대칭인 스택 예산만 비어 있다.

**방향**: `Execute` 헬퍼가 `stackLimit`도 받도록 하고, 깊게 중첩된 식(예: 300단계) 또는 낮은 `stackLimit`(예: 8)으로 실행해 `!run.ok`와 진단 메시지를 단언한다. `Verify`가 최대 도달 깊이를 계산해 `stackLimit`과 비교하도록 확장하면 실행 전 거부도 검증할 수 있다.

### 7. [중간] 0 나눗셈 차단과 비유한 값 차단이 서로를 가려, 어느 쪽도 개별 검증되지 않는다

<sub>분류: 검증력부족</sub>

**근거**

```text
tests.cpp:76-78:
```
    auto division = Execute("let x = 1 / 0;");
    CHECK(!division.run.ok);
```
`!ok`만 보고 진단 메시지는 확인하지 않는다.
변이 검증 두 건이 모두 생존:
- `M9_div_by_zero_check_disabled` (language.hpp:701-706의 `if (rhs == 0.0) return Failure(...)` 제거) → `TESTS PASS (mutation SURVIVED)`. 제거해도 `1/0 = inf`가 language.hpp:633의 `isfinite` 검사에 걸려 `!ok`가 유지되기 때문이다.
- `M5_nonfinite_check_disabled` (language.hpp:633-634 무력화) → `TESTS PASS (mutation SURVIVED)`. 0 나눗셈 검사가 먼저 걸리기 때문이다.
비유한 값 자체를 유발하는 테스트는 없다. 구현은 정상(probe: `let x = 2; while (x > 0) { x = x * x; }` → `non_finite: ok=0 msg="연산 결과가 유한하지 않다" instructions=110`).
```

README.md:25는 "초기화되지 않은 변수, 0 나눗셈, 비유한 값의 런타임 오류" 세 가지를 최종 구현으로 적는데, 세 번째는 자동 검증이 없고 두 번째는 첫 번째와 구분되지 않는다. 초기화되지 않은 변수 쪽은 제대로 검증된다(변이 M10이 정상 검출됨).

**방향**: `division.run.diagnostic->message`가 "0으로 나눌 수 없다"인지까지 단언하고, 곱셈 오버플로로 inf를 만드는 별도 케이스(`let x = 2; while (x > 0) { x = x * x; }`)를 추가해 "연산 결과가 유한하지 않다" 경로를 독립적으로 고정한다.

### 8. [중간] step 문서의 코드가 실제 소스에 존재하지 않는 식별자·시그니처를 사용한다 (완료 기준 4·7 미충족, 스니펫 검증 장치 부재)

> **[반영됨 · 잔여 있음 · 2026-08-13]** step02~05의 스니펫이 `// language.hpp: … 전문/발췌` 표시와 함께 실제 식별자·시그니처(`tokens_[current_++]`, `Block()`, `Failure(...)`, 3인자 `Emit`)로 교체됐다. **잔여**: CI 원문 대조 검사는 도입되지 않았다.

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
step02_precedence_compiler.md:52-60:
```cpp
void ParsePrecedence(Precedence minimum) {
    Prefix();
    while (InfixPrecedence(Peek().kind) >= minimum) {
        Token op = Advance();
        Precedence p = InfixPrecedence(op.kind);
        ParsePrecedence(NextHigher(p));
        Emit(BinaryOp(op.kind));
    }
}
```
실제 language.hpp:393-404에는 `Compiler::Advance()`도 `NextHigher()`도 없다(파서는 `tokens_[current_++]`, `static_cast<Precedence>(static_cast<int>(precedence) + 1)`을 쓴다). `Emit`의 실제 시그니처는 3인자(language.hpp:286)다.
step02:85-93도 `CompileBlock()`, `CompileElseBlock()`, 1인자 `Emit(OpCode::Pop)`을 쓰지만 실제 이름은 `Block()`이고 else는 `if (Match(TokenKind::Else)) Block();`(language.hpp:358)이다.
step03:86 `return Error("분기 합류점의 ...")`, step04:63,77,94 `return RuntimeError(...)` — 실제로는 `Failure(...)`(language.hpp:732)이며 `Error`/`RuntimeError`는 존재하지 않는다.
드리프트 방지 장치도 없다: scripts/check_project_docs.py는 `## 들어가며`/`## 목차`/`## 결론` 개수, `---` 3개, H1 금지, 전방 참조 정규식만 검사하고 소스 대조는 하지 않는다(실행 결과 `Checked 80 project documents: 0 error(s), 0 warning(s).`).
```

PROJECTS_PLAN.md:47 "문서의 코드만 따라도 해당 상태를 빌드할 수 있다", :50 "최종 코드와 문서 코드가 드리프트하지 않도록 스니펫 검증을 둔다" 두 항목에 걸린다. 6개 step 문서 어디에도 750줄짜리 `language.hpp`의 빌드 가능한 형태가 없고, 인용된 조각들은 실제 코드와 이름·인자 개수가 다른 의사코드다. 의미는 대체로 보존되어 있으므로 사실 오류라기보다 계약 위반이지만, 소스가 바뀌어도 문서가 틀렸다는 신호가 나올 경로가 전혀 없다는 점이 문제다.

**방향**: 문서 스니펫에 `language.hpp`의 실제 식별자와 시그니처를 그대로 쓰고(발췌면 발췌라고 표시), 스니펫에 소스 라인 범위 앵커를 달아 CI에서 원문 대조하는 검사를 check_project_docs.py에 추가한다. 최소한 함수명·인자 개수만이라도 기계적으로 대조하면 드리프트가 드러난다.

### 9. [낮음] step06이 바이트코드 검증을 "컴파일 경로만의 비용"으로 적었으나 두 경로 모두 매 반복 Verify를 호출한다

<sub>분류: 문서오류</sub>

**근거**

```text
step06_measurement.md:33-34 `첫 경로는 반복마다 렉싱, 파싱, 바이트코드 검증, 실행을 모두 수행한다. 두 번째 경로는 컴파일된 \`Program\`을 재사용하고 VM 상태만 새로 만든다.`
실제로는 language.hpp:618-620에서 `Run`이 항상 검증한다:
```
        std::string verificationError;
        if (!Verify(program, verificationError))
            return Failure({}, "바이트코드 검증 실패: " + verificationError, 0);
```
bench.cpp:35와 :43 모두 `vm.Run(...)`을 호출하므로 캐시 경로도 10,000번 검증한다.
같은 문서 :80 은 반대로 맞게 적혀 있다: `캐시 경로가 줄이는 것은 VM 디스패치 비용이 아니라 렉서·파서·벡터 할당·이름 인턴 비용이다.`
실측(사본): 원본 bench `compile+run_ms=145.698 / run_cached_bytecode_ms=99.199 / speedup=1.47x`, 별도 probe로 같은 프로그램 10,000회 검증만 측정 `verify_only_ms=3.621 cached_run_ms=66.839 verify_share=5.4%`.
```

§1과 §4가 서로 어긋나며, §1만 읽으면 캐시가 검증 비용까지 상각한다고 오해하게 된다. 정량 영향은 작지만(캐시 경로 시간의 약 5%), 이 step의 목적 자체가 "비용이 어느 수명 구간에 속하는지 확인"(step06:8-9)이므로 비용 귀속 서술의 오류다.

**방향**: §1에서 검증을 첫 경로 전용 비용 목록에서 빼고, `Run`이 매 호출 검증한다는 사실과 그 몫(약 5%)을 명시한다. 검증을 상각하고 싶다면 `Run`에 이미 검증된 프로그램을 받는 경로를 두고 세 번째 측정 경로로 비교하는 편이 이 step의 주장과 맞다.

### 10. [낮음] languages/ 7편 중 GC(part03)·JIT(part05)·리플렉션(part07)에 대응하는 lab 요소가 없다

<sub>분류: 문서대응누락</sub>

**근거**

```text
이론 축: C:/Users/REIN/Documents/VeLog/languages/ = part01_frontend_semantics, part02_execution_runtime, part03_garbage_collection(1216줄), part04_compiler_optimization_abi, part05_jit_compilation(1116줄), part06_engine_scripting, part07_reflection_metadata(860줄).
lab이 다루는 것: part01 §2 어휘 분석, §3 문법·구문 분석, §7 진단 / part02 §3 바이트코드 VM, §7 호스트 경계와 샌드박스, §8 관측성과 검증 / part04 §2 CFG(검증기가 CFG 위 데이터 흐름 전파를 함) / part06 §3 바인딩 레이어.
대응 요소가 0인 것: part03 전체, part05 전체, part07 전체. 추가로 part02 §2 트리 순회 인터프리터 vs 바이트코드 비교, part02 §4 호출 프레임·클로저, part04 §4-§5 최적화·레지스터 할당, part06 §4 네이티브 경계 비용, §5 핫 리로드.
배제는 명시되어 있다 — README.md:56-59 `문자열, 객체, 함수, 클로저, GC, 디버그 심볼 파일, JIT는 이 실험의 범위가 아니다. ... 메모리 관리와 최적화 계층은 이 계약 위에서 독립적으로 확장할 수 있다.`
PROJECTS_PLAN.md:32의 요구 항목에도 GC/JIT는 없다.
```

은폐된 누락이 아니다. README가 범위를 선언했고 공식 완료 기준도 요구하지 않으므로 기준 위반은 아니다. 다만 축 문서 분량의 절반 이상(GC+JIT+리플렉션 약 3200줄)이 대응 실험실 없이 남는다는 사실은 기록해 둘 값어치가 있다. 특히 part06 §4 "네이티브 경계 비용"은 lab이 이미 `CallNative`와 벤치 하네스를 갖고 있어 추가 비용이 거의 없는데도 측정 대상이 아니다.

**방향**: README 범위 절에 "이 축의 part03/part05/part07은 별도 실험실 없이 이론 문서로만 다룬다"를 한 줄 추가해 대응 관계를 명시한다. 저비용 확장으로는 bench에 네이티브 호출 유무를 조작 변수로 둔 경로(`sum = sum + x` vs `sum = add_native(x)`)를 넣어 part06 §4의 경계 비용을 관측값으로 만드는 것이 있다.

### 11. [낮음] `let`이 bare 할당과 완전히 동일한 바이트코드를 내고 재선언·블록 스코프 누출을 허용하는데, 문법 계약 주장과 어긋난다

<sub>분류: 문서오류</sub>

**근거**

```text
README.md:9 `1. 문법은 표기법이 아니라 허용할 프로그램과 거부할 프로그램을 정하는 계약이다.`
step02_precedence_compiler.md:28-29 는 두 형태를 별도 생산 규칙으로 제시한다: `statement -> "let" IDENT "=" expression ";" | IDENT "=" expression ";"`. 차이에 대한 설명은 문서 어디에도 없다.
실제로 `VariableDeclaration`(language.hpp:321-328)과 `AssignmentStatement`(language.hpp:330-337)은 둘 다 `Emit(StoreGlobal, InternName(name.text)) + Emit(Pop)`만 방출한다.
probe 실측:
`let vs assign same code A    program=1 diags=0 code=4 names=1 consts=1 run_ok=1 z=7.000`  (`let z = 7;`)
`let vs assign same code B    program=1 diags=0 code=4 names=1 consts=1 run_ok=1 z=7.000`  (`z = 7;`)
`let-redeclare                program=1 diags=0 code=7 ... x=2.000`  (`let x = 1; let x = 2;` — 진단 0건)
`block-scope leak             program=1 diags=0 code=12 names=2 ... inner=5.000 out=5.000`  (`if (1) { let inner = 5; } let out = inner;`)
```

README가 "전역 변수만 지원"(README.md:56)이라고 적어 스코프 부재는 선언되어 있으나, `let` 키워드가 아무 의미도 갖지 않는다는 사실(선언 없이 쓰는 할당도 허용, 중복 선언도 허용)은 어디에도 적혀 있지 않다. 문법을 "거부할 프로그램의 계약"으로 세운 주장과, 실제로는 거부하는 것이 없는 키워드가 문법표에 놓여 있는 상태 사이의 괴리다. 진단 품질 면에서도 오타 난 대입(`helth = 100;`)이 조용히 새 전역을 만든다.

**방향**: 둘 중 하나를 택해 문서에 반영한다. (a) `let`을 실제 선언으로 만들어 중복 `let`과 미선언 대입을 진단으로 거부하고, 그 거부 두 건을 tests.cpp에 고정한다. (b) 현 동작을 유지한다면 step02 §3에 "`let`은 가독성 표지이며 bare 할당과 동일한 바이트코드를 낸다. 중복 선언과 미선언 대입을 거부하지 않는다"를 명시한다.

### 12. [낮음] 6개 step 문서 모두 실행 명령만 있고 기대 출력(관측 가능한 결과)이 적혀 있지 않다

<sub>분류: 문서오류</sub>

**근거**

```text
PROJECTS_PLAN.md:48 `5. step 종료 시 실행 명령과 관측 가능한 결과가 있다.`
step01:104-108 cmake/ctest 3줄, step02:115 `.\build\Release\languages_tests.exe`, step03:113 ctest, step04:115-116 tests+demo, step05:102 ctest, step06:93-95 ctest+demo+bench — 어느 문서에도 실제 출력 문자열이나 수치가 없다. step04:119는 `데모는 유한한 웨이브 체력 계산을 실행하고 실제 명령 수와 결과를 출력한다`라고만 적고 값을 적지 않는다.
실제 실행 출력(사본, Release):
`languages_tests: all checks passed`
`instructions=96 hp=270.271`
`iterations=10000 / compile+run_ms=145.698 / run_cached_bytecode_ms=99.199 / speedup=1.47x checksum=32240000`
```

명령은 모두 정확하고 그대로 실행된다(직접 확인). 다만 독자가 자기 결과가 맞는지 대조할 기준이 문서에 없다. bench의 경우 절대 시간은 환경 의존이므로 문서가 수치 고정을 피한 것은 합리적이지만, 최소한 `speedup > 1`, `instructions=96`, `all checks passed` 같은 환경 무관한 관측값은 적을 수 있다.

**방향**: 각 step §검증 절 끝에 실제 출력 블록을 넣는다. tests는 `languages_tests: all checks passed`, demo는 `instructions=96 hp=270.271`(결정적 값), bench는 출력 형식과 "캐시 경로가 더 빠르다"는 방향성만 적고 절대 수치는 환경 의존임을 명시한다.

---

## system_kernel_lab — 보완 필요 (13건)

학습 대상 대체는 발견되지 않았다. 동기화 4종(spin/mutex/futex/CAS)은 직접 구현했고 FutexSync는 라이브러리 대신 `syscall(SYS_futex, ...)`를 직접 호출하며, 코루틴도 ucontext로 직접 짰다. SDL2/OpenGL/ImGui는 HUD 전용이고 `system_core_tests` 타깃은 이 셋을 링크하지 않아 "렌더링·UI에서 계산 코어를 분리하라"는 기준을 실제로 지킨다. 다만 검증력은 선언된 기준에 크게 못 미친다. PROJECTS_PLAN.md:33이 이 lab에 명시한 세 항목 중 "I/O 결과 동등성"만 검증되고 "부분 읽기·EINTR·취소"는 테스트에 전혀 없으며, lab 전체에 실패 주입 수단(시그널·파이프·짧은 읽기·권한 실패 모사)이 하나도 없다. 게다가 VERIFY.md:46이 밝히듯 이 CTest는 한 번도 실행된 적이 없고, backend가 런타임 폴백해도 테스트는 조용히 통과하므로 aio/io_uring 경로가 단 한 번도 실행되지 않은 채 "backend 동등성 통과"가 나올 수 있다. 문서 쪽은 step02~08의 일괄 면책 문구가 기준 4·7(문서 코드만으로 빌드, 드리프트 금지)을 사실상 포기한 상태이고, 특히 step06의 cgroup 코드는 최종 코드의 `SKL_CGROUP_DIR` 승인 게이트가 빠져 같은 문서의 산문과 코드가 서로 모순된다.

### 1. [높음] PROJECTS_PLAN이 이 lab에 지정한 3개 검증 항목 중 "부분 읽기·EINTR·취소"가 테스트에 전혀 없다

> **[이월 · 2026-08-13]** `tests/system_core_tests.cpp`는 변경되지 않았다(main은 여전히 세 함수만 호출). Linux 전용 테스트 신설이 필요하고 반영 환경에 WSL·POSIX 툴체인이 없어 작성해도 실행 검증이 불가능했다.

<sub>분류: 검증력부족</sub>

**근거**

```text
C:/Users/REIN/Documents/VeLog/PROJECTS_PLAN.md:33 — "`system_kernel_lab`: I/O 결과 동등성, 부분 읽기·EINTR·취소, 동기화 불변식". tests/system_core_tests.cpp:152-155의 main은 TestIoBackends / TestSyncPrimitives / TestWorkloadExecutionModels 세 개만 호출한다. `grep -rn "sigaction|signal(|SIGALRM|SIGUSR|::kill|alarm(|::pipe|mkfifo|O_NONBLOCK|pthread_cancel" src tests` → 일치 0건.
```

세 항목의 처리 코드는 존재하지만 전부 테스트가 닿지 않는 죽은 경로다. EINTR 재시도는 read_backend.cpp:33, pread_backend.cpp:36, aio_backend.cpp:107, uring_backend.cpp:136에 있고, 짧은 읽기 처리는 read_backend.cpp:29-48(루프 + `short read` throw), pread_backend.cpp:30-51, aio_backend.cpp:77-82, uring_backend.cpp:168-173에 있으며, 취소는 aio_backend.cpp:117,143의 `aio_cancel`뿐이다. 그런데 픽스처는 항상 정확히 fileBytes 크기의 정규 파일이라(file_io_workload.cpp:145-180, tests:38-50) `read`가 짧게 반환할 일이 없고, 테스트 중 시그널이 전달되는 일도 없다. 결과적으로 (a) EINTR 분기, (b) 부분 읽기 루프의 2회차 이상, (c) short-read throw, (d) aio_cancel 경로 네 가지 모두 한 번도 실행되지 않는다. 취소는 더 나쁘다 — 사용자가 진행 중 사이클을 취소할 수단 자체가 없고, io_uring backend에는 `IORING_OP_ASYNC_CANCEL`/`io_uring_prep_cancel`이 아예 없다(uring_backend.cpp 전체 검색 0건). 참고로 이 세 주제는 이론 축이 정면으로 다루는 것이다 — system/part07_signal.md:571-584가 SA_RESTART·EINTR·"이미 일부 바이트를 전송했다면 성공한 짧은 길이를 반환"을, system/part14_io_uring.md:741이 취소 CQE drain 규약을 다룬다.

**방향**: 파이프/FIFO를 소스로 받는 테스트 전용 경로를 추가하면 세 항목이 동시에 검증 가능하다. 파이프는 요청보다 적게 반환해 부분 읽기 루프를 강제하고, 별도 스레드에서 `pthread_kill(tid, SIGUSR1)`(SA_RESTART 없이)로 EINTR을 주입할 수 있으며, aio/uring은 대용량 요청 후 취소를 걸어 "취소 요청 CQE와 원래 요청 CQE를 모두 수확"하는 불변식을 검사할 수 있다. 최소한 short-read 케이스는 fileBytes보다 작은 파일을 만들어 read/pread가 throw하고 aio/uring이 같은 오류를 내는지(현재는 서로 다르게 처리한다) 대조해야 한다.

### 2. [높음] 실패 주입 수단이 lab 전체에 없어 모든 오류 경로가 검증 불가

> **[이월 · 2026-08-13]** `src/`에 fault injection seam이 여전히 없다. #1과 같은 이유(Linux 실행 검증 불가)로 이월.

<sub>분류: 검증력부족</sub>

**근거**

```text
PROJECTS_PLAN.md:26 — 추적 단위 "실패 주입 | 어떤 입력이 잘못된 구현을 드러내는가". src/ 전체에 fault injection seam(환경변수·토글·주입 가능한 syscall wrapper)이 없고 tests/system_core_tests.cpp에도 오류를 유도하는 케이스가 하나도 없다.
```

코드에는 상당히 공들인 오류 경로가 있는데 전부 미검증이다. io_backend_factory.cpp:64-77의 io_uring 런타임 폴백, aio_backend.cpp:98-133의 `aio_suspend` 실패 시 cancel+polling 수확 경로, uring_backend.cpp:105-126의 submit 음수/부분 제출 시 링 재초기화 경로, uring_backend.cpp:144-151의 잘못된 user_data 방어, mmap_backend.cpp:27-33의 fstat 크기 가드, sched_policy.cpp:77-98의 권한 실패 폴백, sched_policy.cpp:135-180의 cgroup 쓰기 실패·복구 경로가 모두 그렇다. 특히 aio의 취소+수확 경로(aio_backend.cpp:101-132)는 "버퍼와 aiocb를 해제하기 전에 모든 요청을 aio_return까지 수확해야 한다"는 비자명한 수명 규약을 구현한 곳인데, 그 규약이 깨졌는지 확인할 방법이 없다. 오류 경로가 잘못돼도 테스트는 초록이다.

**방향**: backend를 감싸는 `FaultingBackend` 데코레이터(첫 N회 성공 후 지정 errno 반환) 또는 `SKL_FAULT=...` 환경변수 seam을 두고, 각 backend가 오류 발생 시 fd 누수·미수확 요청·링 오염 없이 예외를 던지는지 테스트하라. fd 누수는 `/proc/self/fd` 개수를 사이클 전후로 비교하면 값싸게 검사된다.

### 3. [높음] CTest가 한 번도 실행된 적 없고, backend 런타임 폴백 시 테스트가 조용히 통과한다

> **[이월 · 2026-08-13]** 테스트 소스와 `VERIFY.md`의 "현재 환경에서 미검증"이 모두 그대로다. 폴백 실패 처리·실행된 backend 요약 출력·`TestWorkloadExecutionModels` 확장은 미착수.

<sub>분류: 검증력부족</sub>

**근거**

```text
projects/mini/VERIFY.md:46 — "| `system_kernel_lab` | 현재 환경에서 미검증 | CTest 1개 정의, Linux 실행 미검증 | 8 |". tests/system_core_tests.cpp:90-93 — `if (actual != requested) { std::cout << ... << " unavailable at runtime; verified read fallback\n"; }` (CHECK 없음, 실패 처리 없음).
```

두 문제가 겹친다. 첫째, 이 lab의 유일한 자동 테스트는 저장소 스스로 밝힌 대로 실행된 적이 없다 — PROJECTS_PLAN.md:29의 "빌드 성공은 완료 조건이 아니다" 이전에 빌드 성공조차 미확인 상태다(이번 감사 환경에도 WSL·POSIX 컴파일러·Docker가 없어 확인 불가). 둘째, TestIoBackends(tests:75-94)는 `BackendCompiledIn`으로 컴파일 여부만 거르고, 런타임 폴백(io_backend_factory.cpp:68-77이 UringBackend 생성 실패를 catch해 ReadBackend를 반환)이 일어나면 `actual != requested`를 stdout 한 줄로만 알린다. ctest는 기본적으로 통과한 테스트의 stdout을 감추므로 이 신호는 사라진다. 즉 커널이 io_uring을 막았거나 seccomp에 걸린 환경에서는 UringBackend::RunCycle이 단 한 줄도 실행되지 않은 채 "5개 backend 동등성 통과"가 보고된다 — 검증하는 코드와 실제 실행되는 코드가 다른 전형적인 사례다. TestWorkloadExecutionModels(tests:120-148)도 `BackendType::Read`만 하드코딩해 워커풀·코루틴 경로에서 aio/uring/mmap을 전혀 돌리지 않는다.

**방향**: 폴백이 일어나면 테스트를 실패시키거나 최소한 별도 exit code/ctest 라벨로 구분하고, 어느 backend가 실제 실행됐는지 요약 라인을 항상 출력하라. TestWorkloadExecutionModels도 `BackendCompiledIn`인 모든 backend × {pthread, coroutine}로 돌려 워커당 ring 4개 같은 조합을 실제로 밟게 해야 한다. Linux 실행 검증은 lab의 최소 완료 조건이므로 WSL/CI 중 한 경로는 확보되어야 한다.

### 4. [높음] step 문서의 코드만으로는 최종 상태를 빌드할 수 없다 (기준 4·7)

> **[반영됨 · 잔여 있음 · 2026-08-13]** step08에 §9 "최종 코드의 계약"이 신설되어 (1) `CreateBackend(BackendType, BackendType*)`와 `CreateEffectiveBackend`, (2) 워커 예외 전파 3함수, (3) `system_core_tests` CMake 블록·실행 명령·기대 출력이 실렸고, step04의 POSIX AIO 검출 스니펫도 `check_cxx_source_compiles` 형태로 교체됐다. **잔여**: 근거 (5)의 short-read throw·mmap fstat 가드·uring `idx >= n` 방어·`fileBytes > UINT_MAX` 가드는 여전히 문서 0건이고, `tests/system_core_tests.cpp` 전문과 스니펫-소스 대조 장치도 없다.

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
articles/step02~08 모두 면책 문구를 단다 — step02:17/step03:17/step04:17 "이 글의 코드 스니펫은 *해당 단계의 스냅샷* 이다. 디스크의 통합 코드는 누적되어 더 진행된 상태일 수 있다.", step05:17/step06:17/step07:17/step08:15 "오류 처리와 보조 함수는 통합 코드에 있다." PROJECTS_PLAN.md:47,50 — "문서의 코드만 따라도 해당 상태를 빌드할 수 있다", "최종 코드와 문서 코드가 드리프트하지 않도록 스니펫 검증을 둔다".
```

면책 문구는 드리프트를 인정한 것이지 해소한 것이 아니며, 저장소에 스니펫 검증 도구는 없다. 최종 코드에 있으나 어느 step에도 등장하지 않는 항목: (1) `CreateBackend(BackendType, BackendType* actualType)`의 2번째 인자와 `FileIoWorkload::CreateEffectiveBackend()` — 모든 문서가 1인자 형태만 보인다(step02:308,330 / step03:431,455,467 / step04:464 / step05:227,375 / step06:319,397 / step07:404). 이건 HUD가 요청 backend 대신 실제 backend를 표시하게 하는 공개 계약이라 기준 6에도 걸린다. (2) `enable_testing()` / `system_core_tests` 타깃 / `add_test` / `TIMEOUT 30`(CMakeLists.txt:81-100) — `grep -n "ctest|system_core_tests|enable_testing|add_test" articles/*.md` 결과 0건. lab의 유일한 자동 검증이 8편 어디에도 없다. (3) 워커 예외 전파 기구 `workerErrorMutex_`/`workerError_`/`RecordWorkerError`/`RethrowWorkerError`/`ClearWorkerError`(file_io_workload.cpp:126-143) — 문서 0건. (4) POSIX AIO 검출: 최종은 `check_cxx_source_compiles`로 실제 링크를 검사하고 `if(SKL_POSIX_AIO_AVAILABLE)`로 가드하는데(CMakeLists.txt:26-36,117), step04:96은 `include(CheckCXXSourceCompiles)`만 넣고 호출을 보이지 않은 채 step04:105에서 `if(SKL_RT_LIB)`로 가드한다 — 문서 코드를 그대로 따르면 다른 조건으로 컴파일된다. (5) short-read throw(read_backend.cpp:44-48), mmap fstat 가드(mmap_backend.cpp:27-33), uring `idx >= n` 방어(uring_backend.cpp:144-151), `fileBytes > UINT_MAX` 가드(uring_backend.cpp:51-53) — 모두 문서 0건.

**방향**: 최소한 최종 step(step08)에 "이 시점의 전체 CMakeLists"와 `tests/system_core_tests.cpp` 전문, 그리고 `CreateBackend`의 최종 시그니처를 싣고, 문서의 코드 블록을 추출해 실제 파일과 대조하는 스니펫 검증 스크립트를 두어라(PROJECTS_PLAN.md:50이 요구하는 항목이다).

### 5. [높음] step06의 cgroup 코드가 최종 코드의 안전 게이트를 뺀 상태이며 같은 문서의 산문과 모순된다

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
articles/step06_sched.md:221-237 `CurrentCgroupV2Path()`는 검출한 경로를 그대로 반환하고, :245-262 `ApplyCgroupCpuWeight()`가 곧바로 `cpu.weight`에 쓴다. 최종 src/sched/sched_policy.cpp:59-67은 `const char* approved = std::getenv("SKL_CGROUP_DIR"); if (!approved || detected != approved) { fprintf(stderr, "skl: refusing to modify the current cgroup..."); return {}; }`로 거부한다. 같은 문서 step06:73은 "이 lab은 `SKL_CGROUP_DIR`이 `/proc/self/cgroup`에서 검출한 현재 경로와 정확히 일치할 때만 `cpu.weight`를 200으로 변경한다"고 적는다.
```

문서 안에서 산문과 코드가 정면으로 어긋난다. 산문은 opt-in 승인 계약을 설명하는데 바로 아래 코드 블록에는 그 계약이 없다. 독자가 문서의 코드를 그대로 따라 쓰면 `SKL_CGROUP_DIR` 없이 실행해도 현재 프로세스가 속한 cgroup — 보통 사용자의 공유 session scope — 의 `cpu.weight`를 말없이 200으로 바꾸는 프로그램이 나온다. README.md:109-119가 "cgroup 토글은 현재 shell이나 사용자 세션이 공유하는 cgroup을 우발적으로 변경하지 않는다"고 보장하는 바로 그 성질이 문서 코드에서는 사라진다. 부수적으로 step06:255의 `WriteCgroupFile(dir, "cpu.weight", buf)`는 3인자이고 최종은 errno 반환용 4번째 인자를 갖는다(sched_policy.cpp:101-103,153). `CaptureOriginalCgroupWeight` 실패 시 중단하는 분기(sched_policy.cpp:143-148)도 문서에 없다.

**방향**: step06의 `CurrentCgroupV2Path`/`ApplyCgroupCpuWeight` 스니펫을 최종 코드로 교체하라. 안전 게이트는 이 step이 도입한 핵심 결정이지 "보조 함수"가 아니다.

### 6. [중간] 최종 step이 적은 관측 가능한 결과가 실제 프로그램 출력과 다르고, step06은 정반대를 단언한다

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
articles/step08_perf_visualization.md:229-243의 최종 stats 블록은 `sched        : default` 한 줄이며, :262 다이어그램은 "좌 340px<br/>stats 텍스트<br/>13 줄". 실제 src/ui/hud.cpp:74-92는 `"sched req   : %s\n" "sched apply : %s\n"` 두 줄을 포함해 14줄을 찍고, apply 값은 `workload.SchedApplied() ? "yes" : "NO (see stderr / N/A)"`다. articles/step06_sched.md:444는 "sched 라인은 *현재 토글 값* 만 보여준다. 적용 성공/실패는 stats에 표시되지 않고, 실패 시 *stderr 메시지* 로만 안내"라고 적는다(:377도 같은 취지).
```

기준 5("step 종료 시 실행 명령과 관측 가능한 결과가 있다")와 기준 7 위반이다. step06은 최종 코드가 실제로 하는 일과 반대되는 서술을 하고, 최종 step인 step08의 출력 예시는 실제와 다르다. `SchedApplied()`/`schedApplied_`(file_io_workload.h:64-66,100 / file_io_workload.cpp:70-77,103-108,222-223,393-394)는 "workers=1 + nice/FIFO 조합에서는 워커 전용 정책이 적용되지 않는다"는 비자명한 사실을 사용자에게 알리는 기구인데 8편 어디에도 설명이 없다(`grep -n "SchedApplied|sched apply|schedApplied" articles/*.md` → 0건). 반면 README.md:30은 "`sched apply` — 요청한 정책이 이 조합과 권한에서 실제 적용됐는지 표시"로 맞게 적혀 있어, step 문서만 뒤처진 상태임이 확인된다.

**방향**: step06에서 `schedApplied_` 도입과 `workers<=1 + nice/FIFO`에서 false로 두는 이유를 설명하고, step08의 stats 예시와 "13 줄" 표기를 실제 14줄 출력으로 맞춰라.

### 7. [중간] io_uring에 대해 lab이 세운 핵심 주장이 liburing에 가려져 있고 lab 안에 검증 채널이 없다

<sub>분류: 학습대상대체</sub>

**근거**

```text
articles/step04_aio_uring.md:445-455가 표로 "io_uring | open 16 + io_uring_submit 1회 + CQE 수확 + close 16"을 제시하며 "io_uring의 *제출 호출이 1번* 인 점이 핵심"이라고 적지만, 같은 줄에서 "strace를 붙이면 이 차이를 별도 측정 채널로 시각화할 수 있다"고 외부 도구로 미룬다. README.md:32 "strace/ftrace/perf 타임라인 통합은 미구현". step08:455 표 "시스템 콜 카운트 | X | 외부 strace -c". src/io/uring_backend.cpp:92-105는 `io_uring_get_sqe`/`io_uring_prep_read`/`io_uring_sqe_set_data64`/`io_uring_submit`만 호출하고 SQ/CQ 링 메모리나 `io_uring_enter` 호출 횟수에 접근하지 않는다.
```

저자 기준("학습 대상이 아닌 것에 라이브러리를 쓰는 것은 정상")을 그대로 적용하면 판정이 갈리는 경계 사례다. 다른 곳에서는 저자가 이 원칙을 엄격히 지킨다 — FutexSync는 라이브러리 대신 `::syscall(SYS_futex, ...)`를 직접 호출하고(sync/futex_sync.h:70-83), 코루틴도 Boost.Context 대신 ucontext로 직접 짰다. 그런데 io_uring만은 이 lab이 측정하겠다고 선언한 대상 자체(제출 경계 = `io_uring_enter` 횟수, SQ/CQ 링 인덱스 갱신)를 liburing이 대신 처리하고, 그 결과 lab 내부에는 그 주장을 확인할 관측값이 하나도 없다. 4개 측정 채널(cycle ms / throughput / ctx-sw / page faults)은 어느 것도 syscall 경계 수를 보여주지 않는다. 즉 "제출 경계가 1회로 줄어든다"는 이 lab의 대표 가설(step08:553 "가설 1: io_uring이 제출 경계 줄임")은 lab 안에서 참·거짓 판정이 불가능하다.

**방향**: liburing 사용 자체를 바꿀 필요는 없다. 대신 (a) `/proc/self/syscall`이 아닌 `perf_event_open`이나 최소한 `getrusage` 밖의 채널을 하나 더 열거나, (b) 사이클 단위로 `io_uring_submit`/`io_uring_wait_cqe` 호출 횟수와 `cqe`가 이미 CQ에 있어 syscall 없이 반환한 횟수를 직접 카운트해 채널로 노출하면 이 lab만으로 가설 1을 판정할 수 있다. 그게 어렵다면 step04/step08에서 "이 주장은 이 lab이 검증하지 않는다"고 명시적으로 강등해야 한다.

### 8. [중간] 이론 축의 핵심 주제 중 시그널, io_uring 등록 자원·취소, IPC가 lab에 대응이 없다

> **[이월 · 2026-08-13]** `registered files/buffers` 토글도, 테스트 전용 시그널·파이프 소스도 추가되지 않았다. 기능 증설이라 이월.

<sub>분류: 문서대응누락</sub>

**근거**

```text
system/README.md:15 "07 | 시그널 | process 비동기 통지·job control·PTY", :22 "14 | 비동기 I/O | submission/completion·등록 자원·취소·보안", :23 "15 | IPC | pipe·shared memory·message·local socket". system/part14_io_uring.md:951-994가 `io_uring_register_files`/등록 버퍼를, :741이 취소 CQE drain 규약을 다룬다. src/io/uring_backend.cpp:80-88은 매 사이클 `::open`/`::close`를 반복하고 `io_uring_register_files`/`io_uring_register_buffers`를 호출하지 않는다.
```

22편 축 중 lab이 실제로 조작 변수로 만든 것은 part08(스케줄러: nice/FIFO/cgroup weight), part09(동기화: spin/mutex/futex/CAS — RCU·per-CPU는 제외), part03(가상 메모리: mmap + page fault 카운트), part02(프로세스·스레드: pthread vs coroutine), part13(페이지 캐시 — 통제하지 않고 관측만), part14(io_uring 제출/수확 일부)다. 반면 part07 시그널은 전혀 없는데, 이건 단순 누락이 아니라 상위 항목과 연결된다 — EINTR을 발생시키는 유일한 정상 경로가 시그널이므로 시그널을 다루지 않으면 EINTR 검증도 불가능하다. part15 IPC(pipe)도 없는데, 파이프는 부분 읽기를 강제하는 가장 값싼 수단이다. part14의 등록 자원은 "등록·갱신·해제 비용과 slot 수명 관리가 새로 생긴다"(part14:1041)는 트레이드오프가 있어 토글로 만들기에 이상적인 주제인데 backend에 흔적이 없다. 취소는 aio의 오류 경로에만 부수적으로 존재하고 io_uring에는 아예 없다.

**방향**: backend에 `registered files/buffers` 토글을 하나 추가하면 part14의 등록 자원 주제와 "lab의 조작 변수" 요구를 동시에 만족한다. 시그널·파이프는 새 backend가 아니라 테스트 전용 소스로 도입해도 축 대응과 검증 공백을 함께 메울 수 있다.

### 9. [중간] RunCycleMulti의 remaining_ 설정이 큐 push보다 늦어 직전 사이클 지연 워커가 카운터를 언더플로시킬 수 있다

<sub>분류: 코드오류</sub>

**근거**

```text
src/workload/file_io_workload.cpp:311-317 — 큐를 먼저 채우고(`queueSync_->Lock(); ... for (i) queue_.push(i); queueSync_->Unlock();`) 그 다음에 `remaining_.store(paths_.size(), std::memory_order_release);`. src/workload/file_io_workload.cpp:248-275 — 워커의 내부 루프는 `remaining_.fetch_sub(1, acq_rel)` 직후 곧바로 다시 `queueSync_->Lock()`으로 돌아가며, 새 사이클 진입 여부(cycleSeq)를 확인하지 않는다.
```

직전 사이클의 마지막 작업을 처리한 워커는 `remaining_`을 0으로 만든 뒤(:274) 곧장 :252의 Lock으로 돌아간다. 메인은 :329에서 0을 보고 반환하고, main.cpp:124-141의 ImGui 프레임과 SwapWindow를 거쳐 다음 RunCycleMulti의 :312에서 큐를 채운다. 그 워커가 :274와 :252 사이에서 선점되어 있다가 메인이 push를 끝내고 unlock한 직후(:315)이면서 `remaining_.store(16)`(:317) 이전에 깨어나면, 새 사이클의 항목을 pop해 처리하고 0에서 `fetch_sub(1)`을 수행한다 — `std::atomic<std::size_t>`이므로 SIZE_MAX가 되고, 그 뒤 메인의 store(16)이 이를 덮는다. 큐에는 15개만 남았으므로 최종 `remaining_`은 1에서 멈추고 메인은 :329의 `while (remaining_ > 0) std::this_thread::yield();`에서 영구히 돈다. 창은 좁지만 락 해제 직후 대기 워커가 깨어나는 handoff 지점이라 다중 코어에서 실제로 겹칠 수 있고, 하필 이 lab이 권장하는 측정 시나리오(step06:451-455, step08:324-331의 `stress-ng --cpu $(nproc)` 부하 + SCHED_FIFO 워커)가 워커 선점 확률을 크게 높인다. 현재 이 조건을 잡을 테스트는 없다 — TestWorkloadExecutionModels는 사이클을 조합당 1회만 돌린다(tests:138,144).

**방향**: `remaining_.store(paths_.size())`를 큐 push보다 먼저(같은 락 구간 안 또는 직전) 수행하거나, 워커의 내부 pop 루프가 사이클 경계를 넘지 않도록 pop 조건에 현재 cycleSeq를 함께 확인하게 하라. 회귀 테스트로 같은 조합을 수백 사이클 반복하는 케이스를 추가하면 이 계열 버그가 드러난다(현재 CTest TIMEOUT 30이 hang을 실패로 잡아 준다).

### 10. [중간] 측정값을 재현 가능하게 남길 경로가 없어 관측이 GUI 육안 확인에만 의존한다

> **[이월 · 2026-08-13]** headless 러너 실행 파일이 없고 `src/main.cpp`는 여전히 인자를 무시한다. 새 타깃 신설이라 이월.

<sub>분류: 검증력부족</sub>

**근거**

```text
PROJECTS_PLAN.md:9 — mini의 완료 상태는 "계산 정확성 테스트와 재현 가능한 측정이 있다". src/main.cpp:35 `int main(int /*argc*/, char** /*argv*/)` — 인자를 무시하며 CLI 옵션이 없다. FrameTimer의 시계열은 src/ui/hud.cpp:215-243의 `ImGui::PlotLines`로만 소비되고, CSV/JSON 출력이나 파일 기록 코드가 없다.
```

토글 5축·통제 변수·4개 측정 채널까지는 설계가 갖춰졌지만, 관측값을 남기는 수단이 없다. `--backend=uring --workers=4 --cycles=500 --out=run.csv` 같은 headless 모드가 없어 (a) 같은 조합을 두 번 돌려 비교하거나, (b) 사이클 분포의 p50/p99를 계산하거나, (c) 다른 머신 결과와 대조하는 것이 불가능하다. README.md:121-127이 "GUI의 성능 곡선은 정답 수치가 아니라 현재 머신·커널·파일시스템·캐시 상태의 관측값"이라고 정직하게 적은 것과 별개로, 관측값을 저장할 방법 자체가 없다는 문제는 남는다. 이 lab이 Linux 전용 GUI라 VERIFY.md:29대로 자동 검사 스크립트에서 제외되는 점과 겹쳐, 어떤 측정도 재현 형태로 남지 않는다.

**방향**: `FileIoWorkload` + `FrameTimer`는 이미 SDL2/ImGui와 분리돼 있으므로(CMakeLists.txt:82-98의 테스트 타깃이 증거), 같은 오브젝트로 headless 러너 실행 파일을 하나 더 만들면 비용이 작다. 조합·사이클 수·warmup을 인자로 받아 사이클별 4채널을 CSV로 뱉게 하면 재현 가능한 측정 요구가 충족된다.

### 11. [중간] README의 이론 문서 상호참조 4개 중 3개가 다른 문서를 가리킨다

<sub>분류: 문서오류</sub>

**근거**

```text
README.md:15-19 토글 표 — "IO 모델 | ... | system/04 IO 모델", "동기화 | ... | system/06 동기화 프리미티브", "스케줄링 | ... | system/08 스케줄러", "실행 모델 | ... | system/09 스레드 vs 코루틴". system/README.md:12,14,16,17 — 04는 `part04_kernel_allocator.md`(커널 할당), 06은 `part06_interrupt.md`(인터럽트), 08은 `part08_scheduler.md`(스케줄러), 09는 `part09_sync.md`(커널 동기화).
```

스케줄링(08)만 맞고 나머지 셋이 어긋난다. IO 모델은 04가 아니라 part13(블록 I/O)·part14(비동기 I/O)이고, 동기화 프리미티브는 06이 아니라 part09이며, 스레드 vs 코루틴은 09가 아니라 part02(프로세스)다. 하필 잘못 가리킨 번호가 모두 실재하는 다른 문서라 독자가 오류를 눈치채기 어렵다 — "system/06 동기화 프리미티브"를 따라가면 인터럽트 문서가 나온다. lab의 존재 이유가 "짝 시리즈의 주장을 측정으로 검증"(README.md:9)인데 그 연결 고리 자체가 틀렸다.

**방향**: IO 모델 → part13/part14, 동기화 → part09, 실행 모델 → part02로 고치고, 가능하면 번호 대신 파일명 링크(`part09_sync.md`)를 써 번호 변경에 견디게 하라.

### 12. [중간] 후속 step 예고 금지(기준 2) 위반이 반복되며, 존재하지 않는 step09를 참조한다

<sub>분류: 문서오류</sub>

**근거**

```text
PROJECTS_PLAN.md:45 — "후속 step을 예고하지 않는다". articles/step01_skeleton.md:99 — "| `strace`, `trace-cmd`, `linux-tools-generic` | step09의 측정 도구 |" (articles/에는 step01~step08만 존재). step01:98 "step04의 io_uring 백엔드용", step01:15 "완성된 구성에서 토글 패널, IO 백엔드 5종 ... 차례로 추가된다". step02:15,289,313,317,398,430,474,526,539가 step03·step04·step08을 반복 예고(예: step02:313 "step03에서 `Pread`, `Mmap` 이 추가되고, step04에서 `Aio`, `Uring` 이 추가된다").
```

저자가 이 규칙을 인지하고 있다는 근거가 문서 안에 있다 — step01:137,404,412, step03:216,297,307 등에서는 같은 내용을 "현재 범위 밖의 확장에서"라는 중립 표현으로 바꿔 적는다. 즉 정리가 step01·step02에서만 덜 끝난 상태다. 특히 step01:99의 step09는 실재하지 않는 회차이므로 예고 위반인 동시에 끊어진 참조다(최종 회차는 step08이며 README.md:32도 step08을 가리킨다). step02는 문서 하나에서 아홉 번 후속 회차를 언급해 위반이 가장 집중돼 있다.

**방향**: step01:98-99와 step01:15, step02의 아홉 곳을 다른 문서와 동일하게 "현재 범위 밖의 확장" 표현으로 통일하라. step09 참조는 존재하지 않는 회차이므로 제거가 필요하다.

### 13. [낮음] step05가 sync 토글 효과를 해석하는 근거 수치(사이클당 락 32회)가 코드와 다르다

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
articles/step05_workers_sync.md:54 — "이 lab의 작업 큐는 *push 16번 / pop 16번 = 락 32회* 가 한 사이클이다." (:464에서 "16 파일 × 1MB는 *락 32회 / 사이클* 이라"로 반복). src/workload/file_io_workload.cpp:312-315 — `queueSync_->Lock(); while (!queue_.empty()) queue_.pop(); for (i) queue_.push(i); queueSync_->Unlock();` 로 16번의 push가 단일 Lock/Unlock 쌍 안에서 일어난다.
```

실제 사이클당 락 획득 횟수는 push 1회 + 성공 pop 16회 + 워커별 마지막 실패 pop 1회씩(file_io_workload.cpp:252-260의 루프가 빈 큐를 확인하고 break)이므로 워커 4개에서 21회, 8개에서 25회다. 문서가 말하는 32회가 아니다. 수치 자체는 사소하지만, 이 값이 "sync 토글 차이가 측정 노이즈에 묻히는 이유"를 설명하는 근거로 두 번 인용되므로(step05:54, :464) 검증 서술의 근거가 코드와 어긋난 사례에 해당한다. 결론의 방향(락 비용이 cycle ms의 작은 부분)은 실제 값이 더 작으므로 유지된다.

**방향**: "push 1회 + pop 16회 + 워커당 종료 확인 1회"로 정정하고, 워커 수에 따라 달라지는 값임을 밝혀라. 혹은 락 획득 횟수를 사이클당 카운터로 노출해 문서가 코드에서 값을 읽게 만들면 재발을 막는다.

---

## math_lab — 기준 미달 (17건)

학습 대상 대체는 대체로 회피되어 있다. SAT·GJK·BVH·노이즈·곡선·SDF는 전부 직접 구현이고, 무작위 교차 검증(OBB 20,000쌍 SAT vs GJK 0건 불일치, 3,000광선 BVH vs brute force 0건 불일치)에서 실제로 정확하며, Scene들이 algo 모듈을 우회해 같은 기능을 재구현하는 mega식 중복도 없다. 다만 사원수 곱(QuaternionMultiply), 샌드위치 회전(Vector3RotateByQuaternion), 쿼터니언→행렬 변환(QuaternionToMatrix)은 raymath에 위임되어 있는데 이는 math/part05의 학습 대상이자 PROJECTS_PLAN이 요구한 '쿼터니언 항등식'의 검증 대상이므로 부분적 대체로 판정한다. 반면 검증력은 기준 미달이다. 변이 31개를 주입해 재빌드·재실행한 결과 24개(77%)가 생존했고, Slerp를 Nlerp로 통째로 바꾸거나 Smooth Min을 min으로 바꾸거나 BVH median split을 완전히 없애도 ctest가 통과한다. PROJECTS_PLAN이 math_lab에 명시한 네 항목 중 '행렬 항등식'은 코드 자체가 없고 '수치 오차'는 계산이 projection_scene.cpp의 익명 네임스페이스에 갇혀 자동 검증이 구조적으로 불가능하며, 7개 추적 단위와 tests/ctest의 존재 자체가 README·8개 step 문서 어디에도 기재되어 있지 않다. Step 문서 기준으로도 후속 step 예고가 8/8이고, 문서-코드 드리프트는 step01의 DragHandle 설계와 step02의 QuatSlerp(동일 입력에서 45° vs 68.4°)에서 수치로 확인된다.

### 1. [치명] 자동 테스트가 사실상 아무것도 보장하지 않음 — 변이 31개 중 24개(77%) 생존

<sub>분류: 검증력부족</sub>

**근거**

```text
원본을 건드리지 않고 스크래치패드 사본(src/, tests/core_tests.cpp)에 변이를 주입해 MSVC로 재빌드·재실행. 생존(테스트 여전히 통과) 목록:
  M01 QuatSlerp 본문을 통째로 `return QuatNlerp(a,b,t);`로 대체 → 통과
  M04 SminPoly를 `return std::min(a,b);`로 대체 → 통과
  M10 CubicBezier 중간 기저 b1=3u²t→7u²t, b2=3ut²→0.5ut² → 통과
  M11 CubicHermite h10=t³-2t²+t → 5t³-9t²+t → 통과
  M12 CatmullRom 접선 계수 0.5 → 2.0 → 통과
  M13 BVH2D::BuildRec 분할 조건을 `if(true)`로 바꿔 단일 leaf로 → 통과
  M14 분할 축을 항상 0으로 고정 → 통과
  M15 std::nth_element 호출 제거(정렬 없음) → 통과
  M16 Trace의 `if (tn > r.hitT) continue;` 가지치기 제거 → 통과
  M18 SAT 후보축을 A의 두 축만으로 축소(B 면 법선 삭제) → 통과
  M20 GJK 삼각형 판정 outsideAB/outsideAC를 false 고정 → 통과
  M22 RayAABB2D의 `return tFar >= 0;` → `return true;` → 통과
  M26 FBM2D의 norm 나눗셈 제거 → 통과
  M27 sdf::Capsule의 clamp 제거(무한 직선화) → 통과
  M29 nz::HashTo01을 상수 0.5 반환으로 → 통과
  M06 Perlin2D를 상수 0 반환으로 → 통과
  M07 Simplex2D를 상수 0 반환으로 → 통과
  M08 Value2D의 Smoothstep을 선형 보간으로 → 통과
  M09 Quintic을 Smoothstep으로 → 통과
  M02 QuatFromEulerXYZ 곱 순서 반전 → 통과
  M03 QuatNlerp의 최종 정규화 제거 → 통과
  M05 sdf::Box의 inside 항 삭제 → 통과
  M31 AABBUnion의 max를 min으로 → 통과
잡힌 변이는 7개뿐이다(M17 SAT separating, M19 OBB support 부호, M21 GJK 조기 분리 종료, M23 slab 평행 가드, M24 LUT 이진탐색, M25 half-angle, M28 gimbal ε).
```

tests/core_tests.cpp(147줄)는 6개 테스트 함수·약 30개 CHECK로 구성되지만 검사 대상 대부분이 알고리즘 내부 로직에 의존하지 않는다. 그 결과 이 lab이 "살아있는 증거"라고 주장하는 알고리즘들 — Slerp, Smooth Min, 세 종류의 곡선, BVH median split, Perlin/Simplex 노이즈, GJK 삼각형 케이스 — 을 전부 무력화해도 ctest는 green이다. PROJECTS_PLAN의 "빌드 성공은 완료 조건이 아니다 ... 최소한 자동 검증을 둔다"를 형식적으로만 충족한 상태다.

**방향**: 이론 주장을 직접 겨냥한 판별 테스트를 추가하라. 실측으로 판별력을 확인한 후보: (a) Slerp 등각속도 — t를 100등분해 인접 쿼터니언 간 각도의 max/min을 재면 slerp=1.0014, nlerp=1.5667(축(0,0,1), 2.6rad)이라 상한을 걸면 M01이 즉사한다. (b) SminPoly — k>0에서 smin<min인 구간 존재와 |a-b|>k에서 min과 일치. (c) 곡선 — CatmullRom(...,0)==p1, (...,1)==p2(실측 확인됨), Bezier는 t=0.5 해석해 비교. (d) BVH — 무작위 40박스×3000광선을 brute force와 대조(실측 0 mismatch). (e) SAT vs GJK 교차 검증 — 무작위 OBB 20000쌍에서 intersect 플래그 일치(실측 0 disagreement). M18/M20을 모두 잡는다.

### 2. [높음] 핵심 알고리즘이 자기 자신과 항등이 되는 퇴화 입력으로만 테스트됨

<sub>분류: 검증력부족</sub>

**근거**

```text
tests/core_tests.cpp:86 `CHECK(Near(mathlab::sdf::SminPoly(1.0f, 2.0f, 0.0f), 1.0f));` — k=0은 sdf2d.h:54 `if (k <= 1e-6f) return OpUnion(a, b);` 분기라 smooth min 식이 한 번도 실행되지 않는다. Step 7 전체의 주제가 min과의 항등식으로만 검증된다.
tests/core_tests.cpp:124-129 FBM2D를 octaves=0으로 호출하고 결과가 0인지 확인 — 루프가 0회 도는 경로.
tests/core_tests.cpp:51-54 CubicBezier를 t=0, t=1에서만 확인 — 중간 기저 b1,b2가 0이 되는 지점.
tests/core_tests.cpp:85 `sdf::Capsule({0,2},{0,0},{0,0},2)` — a==b 퇴화(원) 분기만. 선분 clamp 경로 미검증.
tests/core_tests.cpp:84 Box는 `> 0.0f`(바깥)만 확인 — inside 항 미검증.
tests/core_tests.cpp:119-122 Perlin2D/Simplex2D는 `std::isfinite`만 확인.
또한 QuatFromEulerXYZ, CubicHermite, CatmullRom, Lerp2, OpUnion/OpIntersect/OpSubtract는 테스트에서 단 한 번도 직접 호출되지 않는다.
```

mega 감사에서 지적된 "자기 자신과 비교하는 항등식 테스트"와 같은 유형이다. 각 CHECK가 통과하는 이유가 알고리즘의 정확성이 아니라 입력이 알고리즘을 우회하도록 골라졌기 때문이다. 앞 항목의 변이 생존율이 높은 직접 원인이다.

**방향**: 각 함수마다 "그 함수만이 만족하는" 성질을 하나씩 잡아라. smin은 k>0 구간, FBM은 octaves≥2에서 옥타브 수에 따른 분산 증가, Bezier는 t=0.5 해석해, Capsule은 선분 중간의 수직거리, Box는 내부 점의 음수 거리.

### 3. [높음] 짐벌락을 '임계값 함수'로만 검증 — 실제 퇴화 불변식은 코드가 만족하는데도 테스트가 없음

<sub>분류: 검증력부족</sub>

**근거**

```text
src/algo/quaternion.h:107-112 EulerNearGimbalLock은 각도 하나를 받아 π/2와의 거리만 재는 술어다. tests/core_tests.cpp:45-46 `CHECK(EulerNearGimbalLock(halfPi)); CHECK(!EulerNearGimbalLock(0.0f));`가 이를 그 임계값 자신으로 되묻는다.
실제 이론 주장(step02_rotation.md:119 "바깥 두 축인 X 회전(pitch)과 Z 회전(roll)이 같은 회전으로 축약")은 검증되지 않는다.
직접 측정해 보니 코드는 그 불변식을 정확히 만족한다: yaw=π/2 고정, p∈[-1,1], r∈[-1,1], c∈[-0.9,0.9] 격자에서 QuatFromEulerXYZ(p+c, π/2, r+c)와 QuatFromEulerXYZ(p, π/2, r)의 worst 1-|dot| = 2.38e-07. 같은 shift를 yaw=0에서 적용하면 1-|dot| = 0.0223으로 확연히 다르다.
```

검증 가능한 이론 주장이 코드에 이미 구현되어 있는데도, 테스트는 그 주장 대신 UI 경고 문구를 띄우는 임계값 상수를 검사한다. 변이 M28(ε를 0.05→1.6)이 잡힌 것도 알고리즘 정확성이 아니라 상수 하나를 잡은 것이다.

**방향**: CHECK로 `QuatFromEulerXYZ(p+c, π/2, r+c)`와 `QuatFromEulerXYZ(p, π/2, r)`의 |dot|이 1에 수렴하고 yaw=0에서는 수렴하지 않음을 확인하라. 이것이 짐벌락의 실제 정의이며 M02(곱 순서 반전)도 함께 잡는다.

### 4. [높음] Step 8의 계산 전체(깊이 정밀도·Kahan 합산)가 Scene 안에 인라인되어 자동 검증이 구조적으로 불가능

<sub>분류: 검증력부족</sub>

**근거**

```text
src/scenes/projection_scene.cpp:13-33 — StdNdcZ / ReverseNdcZ가 익명 네임스페이스 안에 정의되어 외부에서 호출 불가.
src/scenes/projection_scene.cpp:141-182 — naive/Kahan 합산 루프가 DrawWorld() 본문 안에 있고 그래프 좌표 계산과 뒤섞여 있다.
src/algo/ 아래에 대응 헤더 없음. tests/core_tests.cpp의 6개 테스트 함수(TestQuaternion/TestCurves/TestCollision/TestSdf/TestBvh/TestNoise) 어디에도 깊이·Kahan 항목이 없다.
CMakeLists.txt:59 `add_executable(math_core_tests tests/core_tests.cpp)` — 이 파일이 유일한 테스트 소스다.
```

PROJECTS_PLAN.md:29 "렌더링과 UI에서 계산 코어를 분리하고 최소한 다음 자동 검증을 둔다"와 PROJECTS_PLAN.md:34가 math_lab에 요구한 "수치 오차"를 정면으로 위배한다. step08_projection.md:173-178은 "big=100,000, small=0.0001, N=100,000 → truth≈100,010, naive≈100,000, kahan≈100,010"이라는 검증 가능한 수치 주장을 적어놓고도 그것을 테스트로 옮기지 않았다.

**방향**: src/algo/depth.h와 src/algo/kahan.h로 순수 함수를 뽑아내고 문서에 이미 적힌 수치를 그대로 CHECK로 옮겨라. 최소: StdNdcZ(-n,n,f)==0, StdNdcZ(-f,n,f)==1, ReverseNdcZ는 반대; KahanSum(big,small,N)이 double 기준값과 1e-3 이내, NaiveSum은 기준값에서 유의미하게 벗어남(즉 두 결과가 실제로 갈라짐).

### 5. [높음] PROJECTS_PLAN이 math_lab에 명시한 '행렬 항등식'에 해당하는 코드·테스트가 전혀 없음

<sub>분류: 문서대응누락</sub>

**근거**

```text
PROJECTS_PLAN.md:34 "`math_lab`: 행렬·쿼터니언 항등식, 교차 판정, 퇴화 기하, 수치 오차".
src/algo/ 6개 헤더(bvh2d, collision2d, curves, noise, quaternion, sdf2d) 어디에도 행렬 타입·행렬 곱·역행렬·직교성 검사가 없다. src/ 전체에서 행렬 관련 코드는 src/scenes/rotation_scene.cpp:54 `Matrix m = QuaternionToMatrix(q);`(raymath 호출, 렌더 전용) 하나뿐이다.
대응 이론 문서 중 무커버: math/part02_vectors.md(내적·외적·투영·반사·정규화), math/part03_matrices.md(행렬 곱·기저 변환·동차 좌표·역행렬·직교 행렬·column vs row major), math/part06_coordinate_systems.md(Local→World→View→Clip·핸드니스·LookAt), math/part19_geometry_primitives.md(평면·무게중심 좌표·convex hull), math/part20_numerical_stability.md 중 Catastrophic Cancellation·Floating Origin·dt 누적.
```

이 축의 20편 중 lab이 다루는 것은 part04,05,07,08,09,10,11,12,13,14,15,16,17(일부),18(일부) 정도이고, 벡터·행렬·좌표계라는 이 축의 기초 3편(part02/03/06)이 통째로 빠져 있다. 특히 '행렬 항등식'은 완료 기준에 이름이 박혀 있는데도 검증 대상 코드 자체가 존재하지 않는다.

**방향**: src/algo/mat.h를 추가하고 최소한의 항등식을 테스트로 고정하라: M·M⁻¹ == I, 직교행렬 R에 대해 Rᵀ == R⁻¹ 및 det(R)==1, QuatToMatrix(q)·v == QuatRotateVec(q,v), 합성 순서(TRS vs SRT)의 비가환성, column-major/row-major 전치 왕복. 전부 GUI 없이 검증 가능하고 mega의 변환 코드와도 직결된다.

### 6. [높음] 쿼터니언 대수의 핵심 연산(곱·샌드위치 회전·행렬 변환)을 raymath가 대신 수행

<sub>분류: 학습대상대체</sub>

**근거**

```text
src/algo/quaternion.h:15 `#include "raymath.h"`.
src/algo/quaternion.h:36-41 QuatFromEulerXYZ가 `return QuaternionMultiply(QuaternionMultiply(qz, qy), qx);` — Hamilton 곱 자체를 구현하지 않는다.
src/algo/quaternion.h:54-57 — 주석은 `// q 로 vec3 회전. v' = q * (v, 0) * q⁻¹ 의 sandwich.` 라고 식을 명시해 놓고 본문은 `return Vector3RotateByQuaternion(v, q);` 한 줄이다.
src/scenes/rotation_scene.cpp:54 `Matrix m = QuaternionToMatrix(q);` — 쿼터니언→회전행렬 변환도 라이브러리.
반면 QuatFromAxisAngle, QuatNorm, QuatNormalize, QuatNlerp, QuatSlerp, EulerNearGimbalLock은 직접 구현되어 있다.
```

저자 기준("과정에서 배우는 것을 제외한, 필요한 라이브러리는 적용 가능")에서 raylib의 창 생성·입력·텍스처·raygui는 명백히 정상이고 Vector2/Vector3 자료형도 정상이다. 그러나 사원수 곱과 v'=qvq⁻¹은 math/part05의 학습 대상 그 자체이고 PROJECTS_PLAN이 요구한 '쿼터니언 항등식'의 검증 대상이다. 결합성 (qa⊗qb)v == qa(qb v)를 측정하면 두 변이 완전히 일치(2.014851, 0.987258, 0.752129)하는데, 이는 lab의 코드가 아니라 raymath의 정확성을 확인한 것이다. 전면 대체(예: glm으로 회전을 푸는 경우)는 아니므로 치명이 아니라 높음으로 판정한다.

**방향**: QuaternionMultiply와 Vector3RotateByQuaternion을 mathlab 네임스페이스에 직접 구현하고(각 6줄 내외) raymath 구현과 결과가 일치하는지 비교하는 테스트를 두어라. 그러면 라이브러리는 '정답지'가 되고 학습 대상은 코드에 남는다. QuaternionToMatrix도 직접 구현해 직교성·QuatRotateVec와의 일치·왕복 변환을 테스트하면 '행렬 항등식' 요구도 동시에 채워진다.

### 7. [높음] tests/core_tests.cpp와 ctest 타깃이 8개 step 문서 어디에도 등장하지 않음

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
`grep -rniE "ctest|core_tests|enable_testing|add_test|자동 (검증|테스트)|unit test" articles/ README.md` → 결과 0건.
실제 코드에는 CMakeLists.txt:58-62에 `enable_testing()` / `add_executable(math_core_tests tests/core_tests.cpp)` / `add_test(NAME math_core_tests COMMAND math_core_tests)`가 존재한다.
step01_skeleton.md:90-133의 CMakeLists 스니펫에는 이 세 줄이 없고, step02~step08의 "빌드와 실행 결과" 절도 모두 `cmake --build ... && ./build/Release/math_lab.exe` 실행과 육안 관찰만 적는다(step02:226-239, step03:258-270, step04:287-302, step05:291-302, step06:256-271, step07:232-245, step08:205-222).
```

Step 문서 완료 기준 3("현재 step에서 추가·변경한 코드가 식별된다")과 4("문서의 코드만 따라도 해당 상태를 빌드할 수 있다")를 위반한다. 문서만 따라간 독자는 이 lab에 자동 검증이 존재한다는 사실 자체를 알 수 없고, 재현한 프로젝트에는 테스트 타깃이 없다. PROJECTS_PLAN의 추적 단위 중 '자동 테스트'가 문서 차원에서 완전히 누락된 상태다.

**방향**: 각 step 문서의 마지막 절에서 그 step이 추가한 CHECK를 명시하고 `ctest --test-dir build -C Release` 출력을 관측 결과로 넣어라. step01의 CMakeLists 스니펫에 enable_testing/add_test를 포함시켜 기준 4를 회복하라.

### 8. [높음] PROJECTS_PLAN의 7개 추적 단위가 README·step 문서 어디에도 선언되지 않음 (특히 '실패 주입' 부재)

<sub>분류: 문서대응누락</sub>

**근거**

```text
`grep -rnE "이론 주장|조작 변수|통제 변수|관측값|기대 결과|실패 주입" README.md articles/` → 0건.
대신 README.md:13 "이 lab은 측정보다 *인터랙티브 시각화*가 주이므로, 토글은 보여줄 알고리즘 선택에 가깝다", README.md:36 "성능 측정이 주 목적은 아니지만", step01_skeleton.md:35-41의 비교표 "주 목적 | 측정 (ms, cache miss) | 시각화 (단계별 그림)"으로 lab의 성격을 재정의한다.
같은 저장소의 languages_lab은 해당 어휘를 4개 문서에서 사용한다.
```

PROJECTS_PLAN.md:17-27이 "각 lab은 다음 추적 단위를 갖는다"고 규정한 7항목 중 '실패 주입'(어떤 입력이 잘못된 구현을 드러내는가)이 개념 수준에서 부재하며, 이것이 변이 생존율 77%와 직접 연결된다. 취향 문제가 아니라 선언된 기준과 실제의 괴리다.

**방향**: README에 7항목 표를 세우고, 특히 '실패 주입' 열에 Scene별로 잘못된 구현을 드러내는 입력을 명시하라(예: Slerp에는 큰 호+등각속도, SAT에는 두 축 중 하나만 분리되는 회전 배치, BVH에는 leafThreshold=1 vs 16의 노드 수, 노이즈에는 상수 hash 검출용 분산 하한).

### 9. [중간] step02의 Euler 규약 설명이 코드와 정반대 (intrinsic/extrinsic 뒤바뀜)

<sub>분류: 문서오류</sub>

**근거**

```text
step02_rotation.md:82 "**Euler XYZ intrinsic**: math part04 에서 다룬 여러 Euler convention 중 가장 흔한 형태 — *pitch (X) → yaw (Y) → roll (Z)* 의 body 축 순서. 합성은 `qz * qy * qx` 로 (intrinsic 은 *반대 순서로 곱*)."
상반되게 src/algo/quaternion.h:33-35 주석은 "Euler XYZ (extrinsic — 고정 월드 축 기준 X→Y→Z 순서로 적용, Tait-Bryan). ... 같은 회전 행렬은 intrinsic Z-Y'-X'' 시퀀스와 동치다".
수치 확인(p=0.4, y=0.9, r=-0.6):
  code == intrinsic X-Y'-Z''   : |dot| = 0.947734
  code == extrinsic X,Y,Z(fixed): |dot| = 1.000000
```

intrinsic 시퀀스는 적용 순서와 곱 순서가 같고(R = R1·R2·R3), 순서가 뒤집히는 쪽은 extrinsic이다. step02는 이 관계를 반대로 적었고 그 결과 '이 lab의 합성은 intrinsic X-Y-Z'라는 잘못된 설명이 남았다. 최종 헤더 주석은 이미 올바르게 고쳐져 있어 문서만 뒤처진 상태다.

**방향**: step02 §2의 세 번째 설계 결정 항목을 헤더 주석과 동일하게 "extrinsic X→Y→Z (= intrinsic Z-Y'-X''), 먼저 적용할 회전이 곱의 오른쪽"으로 수정하라. §4의 짐벌락 설명(가운데 축 = Y)은 이미 이 규약과 정합하므로 그대로 두면 된다.

### 10. [중간] step02 §8의 실행 관측 결과가 코드와 불일치 — 짐벌락 경고는 pitch가 아니라 yaw에서 뜬다

<sub>분류: 문서오류</sub>

**근거**

```text
step02_rotation.md:236 "- *Mode A 토글*: pitch 슬라이더를 90° 로 → 빨간 GIMBAL LOCK 경고 + yaw/roll 슬라이더 의 효과가 *한 평면으로 축약*."
실제 코드 src/scenes/rotation_scene.cpp:103 `if (mode_ == Mode::Euler && EulerNearGimbalLock(yaw_))` — 경고 조건은 yaw_다.
같은 문서 step02_rotation.md:119("가운데 축이 Y — 즉 `yaw ≈ ±90°`")와 :135(`EulerNearGimbalLock(yaw_)`)와도 자기모순이다.
```

Step 문서 완료 기준 5("step 종료 시 실행 명령과 관측 가능한 결과가 있다")를 위반한다. 문서대로 pitch 슬라이더를 90°로 올린 독자는 경고를 보지 못하고 lab이 고장났다고 판단하게 된다.

**방향**: 236행을 "yaw 슬라이더를 ±90°로 → 빨간 GIMBAL LOCK 경고 + pitch/roll 슬라이더의 효과가 한 평면으로 축약"으로 고쳐라.

### 11. [중간] step02의 QuatSlerp 스니펫에 정규화 두 줄이 빠져 같은 입력에서 45° vs 68.4°로 갈린다

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
step02_rotation.md:157-170의 QuatSlerp는 `float d = a.x*b.x + ...`로 바로 시작한다. 최종 src/algo/quaternion.h:76-78은 `a = QuatNormalize(a); b = QuatNormalize(b);`를 먼저 수행한다.
이 lab 자신의 테스트 입력(core_tests.cpp:40-43, z90을 3배 스케일한 비단위 쿼터니언)으로 두 버전을 실행한 결과:
  final QuatSlerp(id, 3*q90, .5) z=0.382683 w=0.923880  (angle=45.000 deg)
  doc   QuatSlerp(id, 3*q90, .5) z=0.562097 w=0.827072  (angle=68.402 deg)
원인: 문서 버전은 내적 d=2.1213(>0.9995)이라 nlerp 분기로 빠지고, 최종 버전은 정규화 후 d=0.7071이라 acos 경로를 탄다.
부수적으로 문서가 함정 3번(step02:177 "acos 전에 clamp")으로 설명한 clamp 두 줄은 문서 버전에서도 최종 버전에서도 도달 불가능한 죽은 코드다(d>1이면 이미 d>0.9995 분기에서 반환).
```

Step 문서 완료 기준 7("최종 코드와 문서 코드가 드리프트하지 않도록 스니펫 검증을 둔다") 위반이다. 앱 내부에서는 항상 단위 쿼터니언이 들어가므로 실행 화면에서는 차이가 안 보이지만, lab이 자기 테스트에 넣은 바로 그 입력에서 23.4° 어긋난다.

**방향**: step02 스니펫에 정규화 두 줄을 넣고, 함정 3번 설명을 "입력을 먼저 정규화하므로 clamp는 방어용 잔여 코드"로 정정하라. 저장소에 스니펫-소스 대조 검사를 추가하면 재발을 막는다.

### 12. [중간] step01의 DragHandle 스니펫이 최종 구현과 다른 설계이며 같은 문서의 결론과도 모순

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
step01_skeleton.md:178-197의 클래스 정의는 `DragHandle() = default;` + private 멤버 `bool dragging_ = false;`이고, :202-218의 Update()는 핸들마다 독립적으로 `dragging_`을 켠다. BeginFrame/EndFrame이 없다.
최종 src/ui/handle.h:26-53은 `DragHandle();` / `explicit DragHandle(Vector2)` 생성자, `uint32_t id_` 멤버, `static void BeginFrame(); static void EndFrame();`를 갖고, src/ui/handle.cpp:13-19의 전역 g_activeId/g_pendingId/g_pendingDistSq로 capture를 관리한다. `dragging_` 멤버는 존재하지 않는다.
같은 문서 step01_skeleton.md:413은 이미 "프로세스 전체에서 *한 번에 한 핸들만 잡힘* (BeginFrame / EndFrame 으로 후보 중 가장 가까운 한 개만 capture)"라고 최종 설계를 서술한다 — §5의 코드와 결론이 서로 다른 구현을 말한다.
또한 step01:223은 "그리는 순서의 나중 핸들이 먼저 잡힘 이슈가 가능하지만 ... 필요하면 가장 가까운 핸들 우선 같은 정책을 추가"라고 미해결로 남겨두는데 최종 코드는 이미 해결되어 있다.
```

Step 문서 완료 기준 4·7 위반. 문서의 코드만 따라 만든 DragHandle은 최종 코드와 동작이 다르고(겹친 핸들 동시 드래그), 이후 step들(step03:113 "Step 1 에서 깐 capture 정책 덕분에 4 핸들 중 하나만 잡힌다")은 문서에 없는 최종 동작을 전제한다.

**방향**: §5의 스니펫을 최종 handle.h/handle.cpp와 동일하게 교체하고, §5의 '나중 핸들이 먼저 잡힘' 문단을 현재 capture 정책 설명으로 대체하라.

### 13. [중간] 8개 step 문서 전부가 후속 step을 예고하며 저장소 자체 검사기는 이를 통과시킨다

> **[반영됨 · 잔여 있음 · 2026-08-13]** 8편의 "다음 Step N 은 …" 문단이 모두 "이 step 의 한계" 기술로 교체됐다. **잔여**: `scripts/check_project_docs.py`의 `FORWARD_PROMISE_RE`에 `Step\s*\d+` 패턴이 추가되지 않아 가드는 여전히 이 형태를 못 잡는다(physics #9, vk #9와 같은 잔여).

<sub>분류: 문서오류</sub>

**근거**

```text
step01_skeleton.md:15 "이후 단계에서 회전 (Step 2), 보간 (3), 노이즈 (4), 충돌 SAT/GJK (5), 공간 분할·BVH (6), SDF·레이 마칭 (7), 카메라 투영·수치 안정성 (8) 이 차례로 추가된다."
step01_skeleton.md:418 "다음 Step 부터는 ... 이후 보간 (Step 3, part07~08), 노이즈 (Step 4, part09~10), 충돌 (Step 5, part11~13), 공간 분할 + BVH (Step 6, part15~16), SDF + 레이 마칭 (Step 7, part14·part17), 카메라 투영 + 수치 안정성 (Step 8, part18·part20) 까지"
step02:251 "다음 Step 3 은 *Interpolation Scene* —" / step03:284 "다음 Step 4 는 *Noise Scene* —" / step04:316 "다음 Step 5 는 *Collision Scene* —" / step05:316 "다음 Step 6 은 *Spatial Partitioning + BVH Scene* —" / step06:285 "다음 Step 7 은 *SDF + Ray Marching Scene* —" / step07:259 "다음 Step 8 은 *Camera Projection + 수치 안정성 Scene* —" / step08:245 "다음 자료실 (physics_lab, graphics_gl_lab) 도 같은 패턴으로"
`python scripts/check_project_docs.py` → "Checked 80 project documents: 0 error(s), 0 warning(s)."
```

Step 문서 완료 기준 2("후속 step을 예고하지 않는다") 위반이 예외 없이 8/8이다. 더 중요한 것은 저장소가 이미 scripts/check_project_docs.py에 FORWARD_PROMISE_RE / ARTICLE_REFERENCE_RE 가드를 두고도 통과시킨다는 점이다 — 정규식이 '다음 단계는' / '다음 ... 다룬다·살펴본다' 형태만 잡고, 실제로 쓰인 '다음 Step N 은 ... 이어진다' 형태는 '단계'도 '다룬다'도 없어 빠져나간다.

**방향**: FORWARD_PROMISE_RE에 `(?:다음|이후|후속)\s*Step\s*\d+`와 종결어 '이어진다/추가된다/쌓아 올린다'를 추가해 가드를 실제로 작동시키고, 8개 문서의 해당 문단을 '이 step의 한계'로 바꿔라.

### 14. [중간] README가 미구현 기능과 존재하지 않는 측정 항목을 구현된 것처럼 나열

<sub>분류: 문서오류</sub>

**근거**

```text
README.md:17 "| 회전 표현 | Euler / Quaternion / 축-각 |" — src/scenes/rotation_scene.h의 모드는 `enum class Mode { Euler, SlerpVsNlerp }` 둘뿐이고 rotation_scene.cpp:120의 토글 문자열도 "Euler;Slerp/Nlerp"다. 축-각 모드 없음. (step01:418도 "Euler vs Quaternion vs 축-각"과 "마우스로 회전축을 끌면"을 예고하지만 회전축 드래그 핸들도 없다 — 입력은 슬라이더뿐)
README.md:31 "노이즈 등고선 토글 — 등고선/그라디언트/3D 높이 표면." — `grep -rniE "contour|gradient|height|등고" src/scenes/noise_scene.*` → 0건. noise_scene.cpp:116-177의 패널에는 Value/Perlin/Simplex 토글, scale, offsetX/Y, seed, FBM 체크박스와 octaves/lacunarity/gain만 있다.
README.md:39 "ms/frame (toggle 알고리즘의 단일 호출 비용)." — `grep -rniE "GetTime|clock|chrono|micro" src/scenes/` → 0건. FrameTimer는 src/main.cpp:59,103에서 푸터의 전체 프레임 시간에만 쓰인다.
README.md:40 "SDF 평가 비용 (그리드 해상도별)." — sdf_scene.h:45 `int texW_ = 256;`은 고정이고 해상도 슬라이더도 타이밍 코드도 없다.
부수: src/scenes/sdf_scene.h:2 주석 "두 도형 (Circle / Box / Capsule) 의 위치 / 크기를 핸들로" — sdf_scene.cpp:49-58 SampleSDF는 Circle과 Box만 쓴다.
```

PROJECTS_PLAN.md:141 "README에는 구현된 기능, 시뮬레이션으로만 검증한 기능, 교체 지점만 있는 기능을 구분해 적는다"를 위반한다. 특히 '측정 항목' 절의 3개 항목 중 2개가 코드에 존재하지 않는다.

**방향**: 축-각 모드·노이즈 등고선 토글·알고리즘별 호출 비용·해상도별 SDF 비용을 구현하거나 README에서 삭제하고 '미구현' 표시를 하라. sdf_scene.h:2 주석의 Capsule도 삭제.

### 15. [중간] Perlin/Simplex의 실측 출력 범위가 문서·주석의 '약 [-1,1]'과 어긋나 세 노이즈의 시각 비교가 불공정

<sub>분류: 문서오류</sub>

**근거**

```text
실측(200,000 무작위 좌표, seed=7): Perlin2D 범위 [-0.5353, 0.5345], Simplex2D 범위 [-0.6840, 0.6840]. 별도 격자 스캔에서도 |Perlin|max=0.5352, |Simplex|max=0.6840.
문서·주석 주장: src/algo/noise.h:86 `return a + (b - a) * v;     // ∈ 약 [-1, 1]`, noise.h:125 `return 70.0f * (n0 + n1 + n2);   // 약 [-1, 1]`, step04_noise.md:172-176도 동일.
소비 지점: src/scenes/noise_scene.cpp:67-68 `// [-1, 1] → [0, 255]` / `float t = std::clamp(v * 0.5f + 0.5f, 0.0f, 1.0f);`. 같은 함수 :49는 Value2D만 `* 2.0f - 1.0f`로 [-1,1] 전 구간으로 늘린다.
```

결과적으로 heatmap에서 Value는 0~255를 전부 쓰고 Perlin은 약 59~196, Simplex는 약 40~215만 쓴다. step04_noise.md:294-296이 요구하는 관측("Value 토글: 사각 격자감", "Perlin 토글: 45° 사선 bias", "Simplex 토글: 등방성")이 알고리즘 차이가 아니라 대비 차이에 묻힌다. Simplex의 70.0 스케일 상수는 표준 구현(길이 √2급 그래디언트 12종)에서 온 값인데 이 코드의 Grad2(noise.h:60-68)는 축 정렬 단위 벡터 4종을 쓰므로 상수가 맞지 않는다.

**방향**: 주석과 step04의 범위 표기를 실측값으로 정정하거나, 세 노이즈를 각자의 실제 범위로 정규화한 뒤 grayscale에 매핑하라. 범위 상한 테스트(|Perlin2D|≤1 등)만으로는 M06/M07(상수 0 반환)이 안 잡히므로 '분산이 0이 아님'을 함께 검사하라.

### 16. [낮음] README의 math 파트 매핑 오류 — Kahan 합산을 part17~18로 표기

<sub>분류: 문서오류</sub>

**근거**

```text
README.md:23 "| 투영·수치 안정성 | Standard Z vs Reverse-Z 깊이 곡선 / Kahan 합산 | math/part17~18 |"
실제: math/part17_raycasting.md, math/part18_camera_projection.md, math/part20_numerical_stability.md(§3이 Kahan Summation).
step08_projection.md:3과 src/main.cpp:11-12는 올바르게 "part18 + part20"으로 적는다.
```

README 한 곳만 다른 번호를 가리키는 문서 간 수치 불일치다.

**방향**: README.md:23의 매핑을 `math/part18 + part20`으로 수정.

### 17. [낮음] algo 헤더에 어디서도 참조되지 않는 함수들이 남아 있음

<sub>분류: 코드오류</sub>

**근거**

```text
src 전체와 tests 전체에서 정의 외 참조가 0건인 함수: src/algo/collision2d.h:46 `Norm2`, :51 `TripleZ`, src/algo/bvh2d.h:28 `AABBContains`, :33 `AABBOverlap`, src/algo/sdf2d.h:47 `OpIntersect`, src/algo/curves.h:17 `Lerp2`.
(TripleZ는 GJK 구현에서 쓰일 것처럼 보이지만 collision2d.h:202-204가 동일한 식의 로컬 람다 `cross2D`를 따로 정의해 쓴다.)
```

동작에는 영향이 없으나 시각화 코드가 쓰지 않고 테스트도 하지 않는 API가 헤더 공개 표면에 남아 '무엇이 검증된 코드인지'를 흐린다. GJK 내부에서 TripleZ 대신 동일 식의 로컬 람다를 재정의한 것은 mega 감사에서 나온 '모듈이 있는데 제품이 다시 구현' 패턴의 축소판이다.

**방향**: GJK의 `cross2D` 람다를 기존 `TripleZ`로 치환하고, 나머지 미사용 함수는 삭제하거나 테스트를 붙여 살려라.

---

## physics_lab — 보완 필요 (14건)

학습 대상 대체는 없다. CMakeLists.txt의 링크 대상은 raylib 하나뿐이고(:62, :79), 소스 전체의 외부 #include는 raylib.h/raygui.h/표준 라이브러리가 전부다 — 적분기·SAT·GJK/EPA·SAP·Spatial Hash·Penalty/Impulse/SI/PBD·Baumgarte가 모두 자체 구현이며, README.md:47 "외부 물리 엔진 사용 안 함"은 사실이다. 또한 mega 감사에서 나온 "테스트가 다른 코드를 검증" 패턴도 없다 — physics_collision_tests는 제품이 쓰는 src/scenes/narrow_phase_scene.cpp를 그대로 컴파일하고, src/core/physics_core.h는 5개 Scene이 실제로 호출한다. 문제는 검증력이다: 핵심 로직 5곳에 변이를 주입해 실제로 빌드·실행한 결과 4곳이 ctest를 통과했다(SAT 침투깊이를 0으로, 반발계수 공식을 0으로, 누적 λ≥0 클램프 제거, Explicit Euler를 Semi-implicit으로 바꿔도 전부 "tests passed"). Broad Phase 두 알고리즘, 응답 4종, 제약 솔버, 결정성은 자동 테스트가 0건이고 문서가 "정확성 불변식"이라 부른 항목조차 GUI 라벨로만 존재한다. 이론 축 대응도 마찰(part09)·회전 강체 동역학(part03/04)·조인트(part13)가 소스 전체에서 grep 0건이라, PROJECTS_PLAN.md:35이 요구한 "접촉·마찰·제약 불변식" 중 마찰은 기능 자체가 없다. step 문서는 후속 step 예고 21곳과 tests/·src/core/ 전체 미문서화로 완료 기준 2·3·4·7을 어긴다.

### 1. [높음] TestNormalImpulse가 자기 자신과 비교하는 항등식 테스트 — 반발계수 공식과 λ≥0 클램프를 무력화해도 통과

<sub>분류: 검증력부족</sub>

**근거**

```text
tests/physics_core_tests.cpp:51-70. target을 RestitutionTarget()으로 구해놓고 다시 `CHECK(std::fabs((vj - vi) - target) < 1e-6f)`(:64)로 검사한다.

변이 주입 실험(스크래치패드 복사본, MSVC /std:c++20 /O2, 원본 무수정):
- M3: src/core/physics_core.h:62-63 `return -std::clamp(restitution,0,1)*initialNormalVelocity;` → `return 0.0f;` ⇒ `physics core tests passed`, exit=0
- M9: src/core/physics_core.h:73 `const float next = std::max(0.0f, accumulated + delta);` → `const float next = accumulated + delta;` ⇒ `physics core tests passed`, exit=0
(baseline 동일 빌드에서도 passed, exit=0)
```

AccumulateNormalImpulse를 RestitutionTarget이 내놓은 목표값 자체와 비교하므로, 반발계수 식이 무엇이든(심지어 항상 0이어도) 솔버가 그 목표에 도달하기만 하면 통과한다. 독립 검증은 `RestitutionTarget(-0.1f, 1.0f, 0.5f) == 0.0f`(:69) 한 줄, 즉 slop 분기뿐이고 e=0.5일 때 분리속도가 실제로 1.0이어야 한다는 값 검증이 없다. λ≥0 클램프도 마찬가지다 — 테스트의 두 번째 호출(:66-67)은 이미 수렴한 상태라 delta가 0이어서 max(0, ·) 분기를 한 번도 지나지 않는다. Sequential Impulse에서 "당기는 방향 impulse 금지"는 step06_response.md:121이 명시한 핵심 불변식인데 이것이 자동 검증되지 않는다.

**방향**: (1) 목표값을 테스트가 직접 계산한 상수로 고정하라(e=0.5, vn=-2 ⇒ 분리속도 1.0을 리터럴로). (2) 이미 분리 중인 접촉에 대해 AccumulateNormalImpulse를 여러 번 호출해 accumulated가 음수로 내려가지 않고 applied가 되돌리는 양만큼만 나오는지 검사하는 케이스를 추가하라.

### 2. [높음] lab의 간판 주장(Explicit Euler 발산 / Semi-implicit 유계)이 자동 테스트에 전혀 없음 — Semi-implicit은 어떤 테스트도 호출하지 않음

<sub>분류: 검증력부족</sub>

**근거**

```text
tests/physics_core_tests.cpp:28-49 TestIntegratorOrder는 ExplicitEuler / VelocityVerlet / RK4의 위치 오차 수렴비만 본다. HarmonicIntegrator::SemiImplicitEuler는 tests/ 전체에서 단 한 번도 등장하지 않는다. 에너지 E=0.5v²+0.5ω²x²를 계산하는 코드는 테스트에 없다(Energy()는 src/scenes/integrators_scene.cpp:75-77에만 존재).

변이 주입:
- M7: src/core/physics_core.h ExplicitEuler 분기를 Semi-implicit 코드(`v -= w2*x*dt; x += v*dt;`)로 교체 ⇒ `physics core tests passed`, exit=0
- M8: SemiImplicitEuler 분기 본문을 통째로 삭제(no-op) ⇒ `physics core tests passed`, exit=0
```

step02_integrators.md:58은 "순서를 의식 안 하면 newV가 x 갱신에 끼어듦 ... 가장 흔한 implementation bug"라고 이 변이를 콕 집어 경고하는데, 정작 테스트가 그 버그를 잡지 못한다. Explicit도 Semi-implicit도 SHO 전역 오차가 1차라 오차 수렴비 ~2가 나와 CHECK(explicitCoarse/explicitFine > 1.7f)를 통과하기 때문이다. README.md:5과 step02가 내세우는 실제 주장은 오차 차수가 아니라 "에너지가 발산하느냐 유계냐"인데, 그 불변식(Explicit: E/E0가 단조 증가하며 임계 초과 / Semi-implicit: E/E0가 1 주변 유계)은 헤드리스로 전혀 검증되지 않는다. PROJECTS_PLAN.md:35의 "에너지 범위" 항목이 비어 있다.

**방향**: physics_core_tests에 (a) ExplicitEuler로 N스텝 후 E/E0 > 1 + c 이며 단조 증가, (b) SemiImplicitEuler/VelocityVerlet로 같은 dt·스텝에서 |E/E0 - 1| < ε 유계, (c) ExplicitEuler와 SemiImplicitEuler의 첫 스텝 x 결과가 서로 달라야 함(순서 뒤바뀜 검출) 세 가지를 추가하라.

### 3. [높음] Narrow Phase 자동 테스트가 침투 깊이를 전혀 검증하지 않아 SAT depth를 0으로 만들어도 통과 — GUI 검사보다 약함

<sub>분류: 검증력부족</sub>

**근거**

```text
tests/physics_collision_tests.cpp:41-50 — depth에 대한 검사는 `CHECK(satDepth >= 0.0f)`, `CHECK(std::isfinite(satDepth))`뿐이고, satDepth와 gjkDepth를 서로 비교하지도 않는다. gjk normal 검사는 `if (gjkDepth > 0.0f)`(:47) 가드 안에 있어 depth가 0이면 건너뛴다.

변이 주입:
- M1: src/scenes/narrow_phase_scene.cpp:118 `outDepth = minDepth;` → `outDepth = 0.0f;` ⇒ `physics collision tests passed`, exit=0

반면 GUI는 src/scenes/narrow_phase_scene.cpp:344-346에서 `depthDiff < 1.5f`로 두 알고리즘의 depth 일치를 매 프레임 검사한다.
```

테스트 케이스는 3개뿐이고(:55-57) 전부 hit/miss 불린만 실질 검증한다. 2×2 박스 두 개를 중심거리 1.5로 두면 침투 깊이는 0.5여야 하는데 그 값을 어디서도 확인하지 않는다. 그 결과 GUI의 cross-check(bothAgree_)가 자동 테스트보다 강한 역전 현상이 생겼다 — PROJECTS_PLAN.md:27의 "GUI 없이 어떤 정확성을 검증하는가"와 정반대다. depth와 normal은 step05_narrow_phase.md:3이 명시한 Narrow Phase의 출력 계약이고 step06 응답의 입력이므로, 값이 틀리면 Response 전체가 무의미해진다.

**방향**: (1) 축 정렬 박스에 대해 해석적으로 계산 가능한 depth(0.5 등)를 리터럴로 고정 검증. (2) `CHECK(std::fabs(satDepth - gjkDepth) < tol)`로 두 독립 구현의 depth 일치를 자동 테스트로 승격. (3) normal 방향이 a→b인지(Dot(normal, b.pos - a.pos) > 0) 검사. (4) 회전·정점수 조합을 무작위 seed로 돌리는 property test 추가.

### 4. [높음] Broad Phase·Response·Constraints·결정성에 자동 테스트가 0건 — 문서가 "정확성 불변식"이라 부른 항목이 GUI 라벨로만 존재

<sub>분류: 검증력부족</sub>

**근거**

```text
tests/ 전체에서 `grep -rniE "sap|spatialhash|broad|response|penalty|impulseonce|constraint|determin|chain|stretch"` ⇒ (no matches). ctest 등록은 CMakeLists.txt:67, :80 두 개뿐.

반면 문서는 검증을 주장한다:
- articles/step04_broad_phase.md:211 "**정확성 불변식**: `algo overlaps == ground truth` 여야 한다. 다르면 *false negative — 알고리즘 버그*." → 구현은 src/scenes/broad_phase_scene.cpp:389-397의 GUI 빨간 띠 라벨뿐.
- articles/step08_measure.md:142 "이 코드는 ... *기본 토글 (shuffle off)* 일 때 *항상 bit-exact* 다" → 검증은 src/scenes/measure_scene.cpp:266 `IsKeyPressed(KEY_T)` 수동 실행뿐.
- PROJECTS_PLAN.md:35 "physics_lab: 적분기 오차 차수, 운동량·에너지 범위, 접촉·마찰·제약 불변식"
```

lab이 구현한 알고리즘 12종(brute/SAP/SpatialHash, SAT/GJK+EPA, Penalty/Impulse/SI/PBD, PBD chain/SI+Baumgarte) 중 헤드리스로 검증되는 것은 SAT/GJK의 hit-miss 불린 하나뿐이다. broad_phase_scene에 이미 brute ground-truth 비교 로직(:205-245)이 있고 measure_scene에 이미 두 번 돌려 max delta를 재는 로직(:229-258)이 있는데도, 둘 다 raylib 창을 띄우고 사람이 탭을 눌러야만 실행된다. 계산 코어는 렌더링과 잘 분리돼 있어(RunSAP/RunSpatialHash/StepSim 모두 순수 함수적) 헤드리스 테스트를 붙이는 데 구조적 장애물이 없다.

**방향**: broad/response/constraints/measure의 계산부를 Scene 헤더 밖 자유 함수로 노출하거나 현 헤더 그대로 테스트 타깃에 컴파일해서: (1) 무작위 배치 N개에 대해 SAP·SpatialHash의 overlap 집합이 brute와 완전히 같은지(경계 접촉·음수 셀 좌표·cellSize < bodyHalf 포함), (2) SI/PBD의 iterations를 올릴수록 avg penetration이 단조 감소하고 상한 이하로 떨어지는지, (3) 체인 avg stretch가 iterations에 따라 감소하고 baumgarte=0에서 drift가 누적되는지, (4) 같은 seed 600스텝 두 번이 bit-exact이고 shuffle on이면 갈라지는지를 ctest로 검사하라.

### 5. [높음] 마찰(physics/part09)이 lab 전체에 존재하지 않음 — PROJECTS_PLAN이 요구한 자동 검증 항목이자 README가 대응을 주장한 이론 편

> **[반영됨 · 잔여 있음 · 2026-08-13]** 방향의 후자를 택했다 — README 토글 표의 응답 행이 `physics/08·11·12 (법선 impulse 만)`으로 좁혀졌고 "비범위 — 마찰(part09), 회전 강체 동역학(part03·04), 조인트(part13), part14~20" 문장이 README·step01·step08에 추가됐다. **잔여**: `PROJECTS_PLAN.md:37`은 여전히 "접촉·**마찰**·제약 불변식"이라 마찰이 physics_lab의 자동 검증 요구 목록에 남아 있다.

<sub>분류: 문서대응누락</sub>

**근거**

```text
`grep -rni "friction|마찰|tangent" projects/mini/physics_lab/src` ⇒ 0건. tests/도 0건. (articles/step03:14, step06:193·224에 단어만 언급)

대응 이론 문서: physics/part09_friction_restitution.md — "*normal impulse* 가 ... 처리. 충돌 응답이 완성됐는가? 거의 — *접선 (tangential)* 방향이 남았다", 목표 3번 "**Tangential impulse 계산** — friction impulse 의 도출".

README.md:16 `| 충돌 응답 | Penalty / Impulse / Sequential Impulse / PBD | physics/08·09·11·12 |`
PROJECTS_PLAN.md:35 "physics_lab: ... 접촉·마찰·제약 불변식"
```

Response Scene은 법선 impulse와 restitution만 다룬다(src/scenes/response_scene.cpp:161-239). Coulomb 마찰(|λ_t| ≤ μλ_n), 정/동마찰, friction cone 어느 것도 구현·측정되지 않는다. 그런데 README 토글표는 응답 항목의 비교 대상으로 physics/09를 명시하고, PROJECTS_PLAN은 마찰 불변식을 physics_lab의 최소 자동 검증 목록에 넣어 두었다. step06_response.md:193은 "rigid stacking 에서는 *contact 의 friction / 회전* 처리에 부족함이 있어"라고 한계를 정직하게 적었지만, 그 한계가 README와 PROJECTS_PLAN에는 반영되지 않아 선언된 기준과 실제가 어긋난다.

**방향**: Response Scene에 tangential impulse + μ 슬라이더를 추가하고 "경사면 정지 vs 미끄러짐" 불변식(|λ_t| ≤ μλ_n, μ가 임계 이상이면 정지 유지)을 ctest로 검사하거나, 구현하지 않기로 한다면 README.md:16의 physics/09 매핑을 제거하고 PROJECTS_PLAN.md:35의 physics_lab 항목에서 "마찰"을 빼라.

### 6. [높음] 회전 강체 동역학(part03/04)과 조인트(part13)가 소스에 없는데 코드 주석·README는 "강체"·"joint"로 표기

<sub>분류: 문서대응누락</sub>

**근거**

```text
`grep -rni "angular|torque|inertia|관성|각속도" projects/mini/physics_lab/src` ⇒ 0건.
실제 body 자료구조는 전부 회전 자유도 없음: src/scenes/response_scene.h CircleRb(pos/vel/prevPos/radius/mass/invMass), src/scenes/measure_scene.h:29-33 MeasureBody(pos/vel/radius), src/scenes/constraints_scene.h Particle. Polygon2D의 angle(src/scenes/narrow_phase_scene.cpp:41-45)은 마우스 핸들이 직접 쓰는 값이고 동역학이 아니다.

그런데:
- src/main.cpp:16 `//   Constraints      (Step 7, physics part10~13) — joint / PBD`
- README.md:102 `constraints_scene.cpp     joint / PBD constraint (Step 7)`
- articles/step07_constraints.md:10 "Sequential Impulse joint | ... | rigid 한 joint, 회전까지 | ... | ragdoll, machinery, vehicle"
- 실제 constraints_scene의 제약은 distance constraint 단 하나(src/scenes/constraints_scene.cpp:69-108)
```

physics/part03(강체 동역학)·part04(회전 동역학: 관성 모멘트/텐서, 각속도·각운동량·토크)·part13(joints: hinge, slider, motor, ragdoll)은 이 축의 핵심 주제인데 lab에 대응 구현·측정이 없다. lab의 모든 물체는 점질량이거나 회전을 갖지 않는 원이다. 그 자체는 범위 결정으로 정당할 수 있으나, main.cpp와 README와 step07 표가 "joint", "part10~13", "회전까지", "ragdoll / vehicle"이라고 적어 실제보다 넓은 대응을 주장한다.

**방향**: (a) revolute joint 하나와 각속도·관성모멘트를 갖는 강체를 추가해 각운동량 보존을 ctest로 검증하거나, (b) main.cpp:16을 `physics part10~12 — distance constraint (PBD / SI)`로, README.md:102와 step07의 표를 distance constraint 범위로 정정하고 회전·joint를 명시적 비범위로 적어라.

### 7. [높음] src/core/physics_core.h와 tests/ 전체가 어떤 step 문서에도 README 구조에도 없음 — ctest 실행 명령도 없음

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
`grep -rn "physics_core|core::" projects/mini/physics_lab/articles/*.md` ⇒ 0건.
`grep -rn "ctest|physics_core_tests|physics_collision_tests" articles/*.md README.md` ⇒ 0건.
README.md:53-85 "디렉토리 구조 (현재 Step 진행 시점)"에 core/도 tests/도 없다.
README.md:115-120 빌드·실행 절에도 테스트 실행 명령이 없다.
반면 CMakeLists.txt:64-80은 physics_core_tests, physics_collision_tests 두 타깃과 add_test 2개를 정의하고, src/core/physics_core.h는 5개 Scene(integrators/broad_phase/response/constraints/measure)이 실제로 include한다.

step02_integrators.md:50-56, :67-71, :85-89, :96-106은 적분기 4종을 Scene 로컬 코드(`o.x`, `o.v`)로 제시하지만, 실제 src/scenes/integrators_scene.cpp:79-85는 `core::StepHarmonic(...)` 한 줄 위임이다.
```

PROJECTS_PLAN.md 완료 기준 3(추가·변경 코드 식별), 4(문서의 코드만 따라도 해당 상태를 빌드), 5(실행 명령과 관측 가능한 결과), 7(최종 코드와 문서 코드 드리프트 없음)에 모두 걸린다. 문서만 따라가면 적분기·임펄스·AABB·거리제약이 Scene 안에 흩어진 상태가 만들어지고 core 모듈도 테스트 타깃도 생기지 않는다. 즉 이 lab에서 유일하게 자동 검증되는 코드가 문서에서 완전히 보이지 않으며, 독자가 `ctest`를 돌려야 한다는 사실 자체를 알 수 없다. 기준 7이 요구한 "스니펫 검증"도 저장소 어디에도 없다(scripts/에 스니펫-소스 대조 스크립트 없음).

**방향**: core/physics_core.h 도입과 두 테스트 타깃을 해당 step 문서에 명시적으로 넣고(어느 step에서 무엇이 core로 승격되는지), 각 step 결론에 `ctest --test-dir build -C Release` 실행 명령과 기대 출력을 적어라. README 디렉토리 구조에 core/와 tests/를 추가하고 빌드 절에 테스트 명령을 넣어라. 문서 스니펫과 소스를 대조하는 스크립트를 scripts/에 추가하면 기준 7을 실제로 강제할 수 있다.

### 8. [중간] step04 본문 스니펫이 같은 문서의 결론 및 실제 코드와 3곳에서 불일치

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
1) SAP 정렬 tie-break
  articles/step04_broad_phase.md:79-80 `std::sort(eps.begin(), eps.end(), [](const EndPoint& a, const EndPoint& b){ return a.v < b.v; });` (tie-break 없음)
  같은 문서 :262 결론 "SAP 정렬에 *tie-break (min 이 max 보다 먼저)* — 경계 접촉을 overlap 으로 검출."
  실제 src/scenes/broad_phase_scene.cpp:103-107은 `if (a.v != b.v) return a.v < b.v; return a.isMin && !b.isMin;`

2) Spatial Hash cellKey
  articles/step04_broad_phase.md:112-114 `return ((int64_t)cx << 32) ^ (uint32_t)cy;`
  같은 문서 :262 결론 "Spatial Hash 의 `cellKey` 는 unsigned shift 로 작성 — *음수 셀 좌표에서도 UB 없음*."
  실제 src/scenes/broad_phase_scene.cpp:139-141은 `return ((uint64_t)(uint32_t)cx) << 32 | (uint32_t)cy;`

3) AabbOverlap
  articles/step04_broad_phase.md:57-61은 fabs 비교 2줄뿐
  실제 src/core/physics_core.h:86-93은 음수 half를 0으로 clamp하며, 그 clamp가 tests/physics_core_tests.cpp:91-93의 검증 대상이다
```

기준 4·7 위반이다. 특히 1)과 2)는 문서가 자기 결론과 모순돼 독자가 어느 쪽이 맞는지 판단할 수 없다. 본문 코드를 그대로 따라 쓰면 SAP는 경계 접촉(x축 min == 다른 박스의 max)을 놓칠 수 있어 결론이 말하는 "missed = 0" 불변식이 깨지고, AabbOverlap은 테스트가 검증하는 clamp 동작을 갖지 않는다.

**방향**: step04 본문 스니펫 3곳을 실제 소스와 일치시키고, cellKey 스니펫은 unsigned 캐스트 형태로 교체하라. 나아가 문서 코드블록을 소스에서 발췌·검증하는 스크립트를 도입해 재발을 막아라.

### 9. [중간] 후속 step 예고 21곳 — 완료 기준 2 위반이며 저장소 검사기가 "Step"이라는 영어 단위를 인식하지 못해 통과

> **[반영됨 · 잔여 있음 · 2026-08-13]** 8편의 예고 문장이 현재 단계의 한계 기술로 교체됐고 `src/main.cpp:9`의 "Step 1~8 예정" 주석도 "등록 Scene (8 개)"로 정정됐다. **잔여**: `check_project_docs.py`의 정규식에 `step`/`Step`/`스텝`이 추가되지 않았고, `src/scenes/forces_scene.cpp:109`에 "Step 6 에서 본격적인 응답 비교" 예고 주석이 남아 있다.

<sub>분류: 문서오류</sub>

**근거**

```text
PROJECTS_PLAN.md:45 "2. 후속 step을 예고하지 않는다."

위반 예:
- articles/step01_skeleton.md:15 "이후 단계에서 적분기 비교 (Step 2), 외력·드래그·스프링 (Step 3), Broad Phase (Step 4) ... 이 차례로 추가된다."
- step01:194 "Step 2 에서 슬라이더로 1/30 ~ 1/240 토글 가능하게."
- step01:316 "Step 6 에서 *PGS / Baumgarte* 로 더 정밀한 응답을 다룬다."
- step01:400, step02:259, step03:142·182, step04:270, step05:334, step07:236 등 총 21개 라인

검사기 통과 확인: `python scripts/check_project_docs.py` ⇒ `Checked 80 project documents: 0 error(s), 0 warning(s).`
원인: scripts/check_project_docs.py:14-20 ARTICLE_REFERENCE_RE와 :28-35 FORWARD_PROMISE_RE가 "단계/글/편/파트/part/문서/주제/토픽"만 매칭한다. 이 lab의 문서가 쓰는 단위는 영어 "Step"이라 정규식이 전부 비껴간다.
```

규칙이 존재하고 자동 검사기까지 있는데 검사기의 어휘 목록에 구멍이 있어 위반이 전량 통과했다. "선언된 기준과 실제의 괴리"가 도구 층에서 발생한 사례다. 소스 주석에도 같은 예고가 있다(src/main.cpp:9 `// 등록 Scene (Step 1~8 예정)`, src/scenes/welcome_scene.cpp:136 "Step 2+ adds integrator + scene tabs.", src/scenes/welcome_scene.cpp:14 "Step 2 부터 실제 m / s 단위로 변환").

**방향**: check_project_docs.py의 두 정규식에 `step`/`Step`/`스텝`을 추가해 mini 문서 전체를 재검사하고, 예고 문장을 "현재 단계의 한계" 기술로 바꿔라. src/main.cpp:9의 "Step 1~8 예정" 주석도 현재 등록 상태 서술로 정정하라.

### 10. [중간] Step 8 종합 측정이 Step 6 응답 솔버를 쓰지 않고 접촉수집+SI+위치보정을 통째로 재구현

> **[이월 · 2026-08-13]** `measure_scene.cpp`의 `StepSim`은 여전히 접촉 수집·SI·위치 보정을 자체 구현하고, 유효질량 리터럴 `2.0f`/`1.0f`(measure_scene.cpp:148-149, :157-158)도 그대로다. `response_scene`의 `GatherContacts`/`ApplySequentialImpulse`/`ApplyPositionCorrection`은 별개로 남아 있다. 공유 모듈로 올리는 구조 개편이 필요해 이월. 다만 두 경로 각각에는 이번에 자동 테스트가 붙었다(`physics_response_tests`, `physics_determinism_tests`).
>
> **[반영됨 · 2026-08-14]** 접촉 수집 / Sequential Impulse / 위치 보정을 `src/core/contact_solver.h`
> (신설, 248줄)의 템플릿 함수 한 벌로 올렸고, `MeasureScene::StepSim`과 `ResponseScene`이 둘 다
> 그것을 호출한다. `StepSim`은 이제 시나리오(중력 상수·반지름·shuffle 정책)만 정한다.
>
> **이 지적은 1건이었지만 실제 차이는 7군데였다.** 위 근거 블록이 잡은 것은 세 갈래(유효질량 리터럴,
> 위치 보정 재구현, constexpr 고정)인데, 통합하면서 드러난 나머지 넷은 **임펄스 적용식에서
> `* invMass`가 통째로 빠져 있던 것**이다 — body-body 양쪽 2곳, 벽 1곳, 그리고 유효질량 인자
> 자체(`2.0f` / `1.0f` → `bi.invMass + bj.invMass` / `bi.invMass`). 이 워크로드는 `invMass`가 1이라
> **일곱 군데 전부가 숫자로는 아무 차이도 내지 않았다.** 값이 맞아서가 아니라 가정이 우연히
> 성립해서였고, 그래서 지적에도 실측에도 걸리지 않고 더 오래 숨어 있었다.
>
> **실측.** 통합 직전 두 Scene을 같은 조건(캔버스 400×320, body 24개, iteration 8, dt 1/120)으로
> 240 스텝 돌리면 위치가 **0.557 px**, 속도가 **4.77 px/s** 벌어져 있었다. 통합 후에는 둘 다
> 정확히 0 — **비트 일치**다. 갱신 순서가 바뀌었으므로 결정성 수치도 함께 갱신했다:
> shuffled maxDelta **0.700067 → 0.399962**, 900 스텝 뒤 평균 침투 0.6946 → 0.6965,
> 최저점 316.572 → 316.583, KE 3.26e-4 → 3.23e-4.
>
> **재발 방지.** `tests/physics_determinism_tests.cpp`에 `TestMeasureRunsTheResponseSolver`를 추가했다.
> 두 Scene을 240 스텝 나란히 돌려 `CHECK(worstPos == 0.0f)` / `CHECK(worstVel == 0.0f)`로
> 비트 일치를 단언하므로, 어느 한쪽이 다시 갈라지면 ctest가 잡는다.

<sub>분류: 코드오류</sub>

**근거**

```text
src/scenes/measure_scene.cpp:68-205 `MeasureScene::StepSim`은 body-body/벽 접촉 수집(:87-114), Sequential Impulse(:133-162), 위치 보정(:165-204)을 자체 구현한다.
src/scenes/response_scene.cpp:85-139 GatherContacts, :188-239 ApplySequentialImpulse, :241-261 ApplyPositionCorrection이 같은 일을 이미 한다.

두 구현의 실제 차이:
- measure는 유효질량을 리터럴로 박음: `core::AccumulateNormalImpulse(vn, c.targetVelocity, 2.0f, c.lambda)`(:148-149), 벽은 `1.0f`(:156-157). response는 실제 invMass 사용(:225, :233).
- measure는 위치 보정을 접촉 목록 대신 O(n²) 재검출 + 축별 인라인 클램프로 다시 짬(:166-203). response는 Contact.normal 기반(:241-261).
- measure는 gravity/restitution/slop/posCorrect를 constexpr로 고정(:30-34), response는 UI 슬라이더.
```

mega 감사에서 나온 "잘 만든 모듈이 있는데 제품이 그걸 안 쓰고 같은 기능을 다시 구현" 패턴과 동일하다. articles/step08_measure.md:34는 "가장 큰 함정은 *결정성 테스트가 Live 시뮬과 다른 코드를 돌면 답이 무의미*"라고 정확히 지적하고 StepSim 공유로 그것을 해결했지만, 그 공유는 MeasureScene 내부에 한정된다. 결과적으로 step08:183이 말하는 "lab 전체의 마무리 / 종합 측정"이 측정하는 SI는 Step 6에서 사용자가 보고 튜닝한 그 SI가 아니다. 한쪽 솔버를 고쳐도 다른 쪽은 그대로 남는다.

**방향**: 접촉 수집 + SI + 위치 보정을 src/core/ 또는 src/response/ 아래 공유 함수로 올리고 ResponseScene과 MeasureScene이 둘 다 호출하게 하라. 그러면 결정성 측정이 실제 응답 솔버를 대상으로 하게 되고, 4번 finding의 헤드리스 테스트도 한 곳만 붙이면 된다.

### 11. [중간] README의 출력·측정 항목 5개와 step01이 예고한 푸터 ms 분해가 미구현

<sub>분류: 문서오류</sub>

**근거**

```text
- README.md:28 "발산 지점 마커 — 에너지가 일정 임계 이상 증가한 프레임을 그래프에 점으로 표시." ⇒ src/ui/line_graph.cpp:66-173 DrawLineGraph에 마커 로직 없음; src/ 전체 `grep -i "diverg|marker"` 결과는 라벨 문자열 3건뿐.
- README.md:31 "안정성 지표 — penetration depth 평균, jitter (정지 상태에서의 미세 흔들림 진폭)." ⇒ jitter 진폭을 계산하는 코드 없음(문자열 "Impulse (1-shot) jitters." 라벨만, response_scene.cpp:490).
- README.md:29 "ms/frame 그래프 — 통합기·Broad Phase 비용 분리 표시." 및 README.md:36 "프레임 비용: Broad Phase / Narrow Phase / 응답 / 통합 단계별 시간을 분리 측정." ⇒ IntegratorsScene에는 시간 측정 자체가 없고, 각 Scene은 자기 알고리즘 하나만 잰다.
- README.md:19 "| 강체 수 | 100 / 1k / 10k | 스케일링 |" ⇒ 실제 상한: broad_phase_scene.cpp:333 N 슬라이더 `10.0f, 2000.0f`, response_scene.cpp:437 `4.0f, 100.0f`, measure_scene.cpp:325 `4.0f, 64.0f`. 10k 불가.
- articles/step01_skeleton.md:253 "Step 8 의 측정 단계에서는 푸터에 *시뮬 종류별 ms 분해* 가 추가된다 — Broad / Narrow / Solve / Integrate." ⇒ src/ui/scene_chrome.h:29 `void DrawFooter(Rectangle footer, float frameMs, float fps, std::string_view sceneName);` — 분해 인자 없음, Step 8에서도 변경 없음.
```

README의 "출력"·"측정 항목" 절은 이 lab이 무엇을 관측하는지를 정의하는 계약인데 그 중 다섯 항목이 코드에 없다. 특히 "단계별 비용 분리"는 README.md:5의 컨셉 문장("수치 안정성과 비용 차이를 측정")을 뒷받침하는 핵심 관측값이다. 10k는 실제 상한의 5배(broad) ~ 156배(measure) 과장이다.

**방향**: 미구현 항목은 README에서 제거하거나 "미구현" 표시를 달고, 강체 수 행을 실제 슬라이더 상한(2000 / 100 / 64)으로 정정하라. step01:253의 푸터 예고는 기준 2 위반이기도 하므로 삭제하라.

### 12. [중간] 이론 축 대응 범위를 문서가 과대 주장 (part01~20 / part01~13)

<sub>분류: 문서오류</sub>

**근거**

```text
articles/step01_skeleton.md:5 "그래서 시리즈 part01~20 의 *주장이 그래프 한 장에서 검증* 된다."
articles/step08_measure.md:183 "*physics 시리즈의 part01~13* 를 인터랙티브하게 검증하는 *측정 도구* 완성."

실제 대응 없는 편: physics/part03_rigid_body_dynamics.md, part04_rotational_dynamics.md, part09_friction_restitution.md, part13_joints.md, part14_character_controller.md, part15_projectile_trajectory.md, part16_particle_physics.md, part17_soft_body.md, part18_fluids_sph.md, part19_ragdoll.md, part20_engine_integration.md (해당 개념 grep 0건 또는 Scene 부재)
```

lab이 실제로 측정하는 것은 part01~02(운동학·적분기), part05(broad), part06~07(SAT/GJK), part08(법선 응답), part10~12(제약·SI·PBD)이다. part14~20은 Scene 자체가 없고 part03/04/09/13은 앞선 findings대로 개념이 없다. README.md:130의 "이 lab은 강체, 관절, 로프나 천이 실제 제품 요구사항이 될 때 ... 선택할 측정 근거를 만든다"는 정직한 서술과도 어긋난다.

**방향**: step01:5와 step08:183의 범위 표기를 실제 대응 편으로 좁히고(part01·02·05·06·07·08·10·11·12), 미대응 편을 명시적 비범위로 적어라.

### 13. [낮음] MeasureScene의 죽은 멤버와, 결정성 결과 라벨에 shuffle 상태가 표시되지 않는 문제

<sub>분류: 코드오류</sub>

**근거**

```text
src/scenes/measure_scene.h:57-58 `float gravity_ = 600.0f; float restitution_ = 0.1f;` — measure_scene.cpp에서 한 번도 읽히지 않는다(실제 시뮬은 :30-31 `kSimGravity`, `kSimRestitution` 사용).
src/scenes/measure_scene.h:60 `bool lastTestShuffle_ = false;` — measure_scene.cpp:256 `lastTestShuffle_ = shuffleContacts_;`로 대입만 되고 읽는 곳이 없다. DrawPanel(:383-410)은 max/avg delta와 3분류 색띠만 표시한다.
```

gravity_/restitution_은 UI 슬라이더도 없고 시뮬도 안 쓰므로 순수 사문(死文)이다. lastTestShuffle_는 의도가 분명하다 — 마지막 결정성 테스트가 shuffle on 상태에서 돌았는지를 결과와 함께 보여주려던 것인데 표시가 빠져 있다. 그 결과 "RUNS DIVERGED" 띠만 보고는 그것이 의도된 실패 주입 결과인지 진짜 결정성 위반인지 UI만으로는 구분할 수 없다. (실패 주입 메커니즘 자체는 measure_scene.cpp:130-140에 제대로 있고 step08:144가 정확히 설명한다 — 이 lab에서 확인된 유일한 실패 주입이라 표시 누락이 아깝다.)

**방향**: gravity_/restitution_ 멤버를 삭제하거나 kSim* 상수를 대체하도록 실제로 연결하라. DrawPanel의 결정성 결과 옆에 lastTestShuffle_ 값을 "(shuffle: on/off)"로 출력하라.

### 14. [낮음] step03이 인용한 Step 1 반발계수 수치가 실제와 불일치

<sub>분류: 문서오류</sub>

**근거**

```text
articles/step03_forces.md:142 "벽 / 바닥 충돌은 *Step 1 의 단순 응답* (restitution 0.5, 위치 교정 + 속도 반전)."
실제 Step 1: src/scenes/welcome_scene.h:30 `float restitution_ = 0.6f;` (articles/step01_skeleton.md:274, :386도 0.6으로 서술)
실제 Step 3: src/scenes/forces_scene.cpp:113, :119, :123 모두 `* 0.5f` 하드코딩
```

ForcesScene이 쓰는 값은 0.5가 맞지만 그것을 "Step 1 의 단순 응답"이라고 지칭한 것이 틀렸다 — Step 1은 0.6이고, ForcesScene은 그 값을 재사용한 것이 아니라 별도 리터럴을 새로 박았다. 문서 간 수치 불일치이자, Step 3 시나리오 A(step03:148)가 "e = 0.5 ... KE가 1/4로 감소"를 계산 근거로 쓰고 있어 독자가 Step 1 화면과 대조하면 결과가 다르다.

**방향**: step03:142를 "이 Scene 자체의 단순 응답 (restitution 0.5 고정)"으로 고치거나, ForcesScene이 WelcomeScene과 같은 상수를 공유하도록 값을 통일하라.

---

## ai_lab — 보완 필요 (12건)

학습 대상 대체는 없다. CMakeLists와 전체 #include를 훑어 확인한 외부 의존성은 raylib(창·2D 드로잉·입력)와 raygui(패널 UI) 둘뿐이고, A*·Dijkstra·Greedy·JPS·flow field·boids·steering·influence map은 전부 자체 구현이다 — 이 축의 배움을 라이브러리에 넘긴 곳은 한 군데도 없으며, 검증 프로브로 원본 pathfinder가 랜덤 200맵에서 JPS≡Dijkstra 비용 동등성과 경로 인접성·통행 가능성·비용 일치를 모두 만족함도 확인했다. 문제는 검증력이다. 변이 테스트에서 (1) JPS 대각 forced neighbor 규칙 삭제, (2) JPS 경로 직선 보간 무력화, (3) 대각 비용 √2→1.0, (4) expanded 카운터 제거 네 가지가 모두 `ai_pathfinder_tests` 전체 통과를 유지했고, 그중 (1)(2)(3)은 별도 프로브로 실제 결과가 깨짐을 증명했다 — 특히 `TestForcedNeighbors`라는 이름의 테스트가 forced neighbor 파괴를 잡지 못한다. 더 구조적인 문제로, PROJECTS_PLAN이 요구한 "steering·boids 경계 조건" 중 steering은 자동 테스트가 0이며, flow field integration 생성·influence stamp/spread/decay·boids 세 규칙을 포함한 951줄의 Scene 코드가 어떤 테스트 타깃에도 컴파일되지 않는다(계산 코어가 raylib Scene 안에 갇혀 headless 실행 자체가 불가). 문서 쪽은 기준 2(후속 step 예고 금지)는 저장소 검사기로 통과하지만, 6편 모두 실행 명령과 관측 결과가 없어 기준 5를 충족하지 못하고, step01이 선언한 "알고리즘 진행은 Update에서만" 불변식을 세 Scene이 실제로 위반하며(DrawPanel에서 pf_.Step 호출 등), step05의 maxForce는 구현에 아예 존재하지 않는다. `ai_model_tests`는 변이 5건을 전부 검출해 그 범위 안에서는 건강하다.

### 1. [높음] JPS 대각 forced neighbor 규칙을 통째로 삭제해도 전체 테스트 통과 (변이 테스트 확인)

<sub>분류: 검증력부족</sub>

**근거**

```text
src/pathfind/pathfinder.h:212-215 (대각 케이스의 forced neighbor 두 줄)을 삭제한 복사본을 빌드 → `ai_pathfinder_tests.exe` 출력 "pathfinder tests passed", exit=0. 같은 복사본에 붙인 프로브(랜덤 30x22 맵 200개, density 0.28, Dijkstra vs JPS 비용 비교)는 즉시 불일치를 검출: "seed 1: cost mismatch dij=27.48528 jps=28.31371", "seed 7: dij=29.14213 jps=30.79899", "seed 11: dij=29.72792 jps=32.79898", "seed 13: dij=28.31371 jps=29.14213". 원본 코드로 같은 프로브를 돌리면 "random maps: 0 cost/found mismatches".
```

tests/pathfinder_tests.cpp:64-72 `TestForcedNeighbors`는 이름 그대로 forced neighbor를 검증하려는 유일한 테스트지만, 고른 맵이 x=15 단일 수직벽에 y=4·y=15 두 구멍만 뚫은 형태라 대각 forced neighbor 규칙을 자극하지 못한다. 결과적으로 JPS의 핵심 정확성 규칙(대칭 경로를 건너뛰면서도 최적성을 유지하게 하는 유일한 장치)이 무력화돼도 테스트가 잡지 못한다. 참고로 수평·수직 forced neighbor까지 함께 제거한 변이는 테스트가 검출했다("CHECK failed: dijkstra.found == jps.found", exit=1). 즉 테스트는 "JPS가 아예 길을 못 찾는" 수준만 잡고 "JPS가 조용히 비최적이 되는" 수준은 못 잡는다.

**방향**: 대각 forced neighbor가 필수인 맵(대각 통로 옆에 벽 한 칸이 붙어 코너 옆 forced neighbor가 발생하는 배치)을 테스트 맵에 추가하고, 더 근본적으로는 `Grid::RandomWalls`로 생성한 시드 고정 맵 수십 개에 대해 Dijkstra 대비 JPS 비용 동등성을 반복 검증하는 property test를 넣어라. 프로브에서 확인했듯 랜덤 200맵이면 seed 1에서 바로 잡힌다.

### 2. [높음] Pathfinder::Path() 내용을 검증하는 테스트가 전무 — JPS 경로 보간을 무력화해도 통과

<sub>분류: 검증력부족</sub>

**근거**

```text
tests/pathfinder_tests.cpp 전체에서 `finder.Path()` 호출이 0회다(파일 내 Path( 검색 결과 없음). 검증은 `PathStats`의 pathCost/pathLen/found/done 4개 필드뿐. src/pathfind/pathfinder.h:276 `if (!path_.empty() && algo_ == Algo::JPS) {` 를 `if (false) {` 로 바꾼 변이 → `ai_pathfinder_tests.exe` "pathfinder tests passed", exit=0. 같은 변이에서 프로브는 "[JPS/open] step 1 not adjacent (2,8)->(21,8)", "[JPS/open] traced cost 1.0000 != reported pathCost 19.0000", "[JPS/forced] step 1 not adjacent (2,10)->(7,15)" 를 출력.
```

pathfinder.h:272 주석은 "JPS 는 jump point 사이가 비어 있으므로 직선 보간으로 셀을 채운다"라고 명시하고, pathfind_scene.cpp:60-61이 그 `Path()`를 그대로 화면에 칠한다. 즉 보간이 깨지면 화면에 그려지는 경로가 끊긴 점 몇 개가 되지만 자동 테스트는 pathLen>0만 보므로 전부 통과한다. 경로의 시작·끝 셀, 인접성(|dx|<=1 && |dy|<=1), 통행 가능성, 그리고 경로를 따라 재계산한 비용이 보고된 pathCost와 일치하는지가 하나도 검증되지 않는다.

**방향**: `Path()`를 직접 읽어 (1) front==Start, back==Goal, (2) 연속 셀 인접성, (3) 모든 셀 Walkable, (4) 경로를 따라 다시 더한 비용 == stats.pathCost 를 확인하는 검사를 4개 알고리즘 전부에 대해 추가하라. 이 4개 검사는 감사용 프로브에서 원본 코드에 대해 200맵 전부 통과함을 확인했으므로 그대로 붙여도 green이다.

### 3. [높음] 8방향 비용 모델(직선 1, 대각 √2)이 절대값으로 전혀 검증되지 않음 — kDiag=1.0으로 바꿔도 통과

<sub>분류: 검증력부족</sub>

**근거**

```text
src/pathfind/pathfinder.h:104 `static constexpr float kDiag = 1.41421356f;` → `1.0f` 변이 후 `ai_pathfinder_tests.exe` "pathfinder tests passed", exit=0. 프로브는 같은 변이에서 "[JPS/forced] traced cost 31.1421 != reported pathCost 27.0000", "[JPS/rnd] traced cost 27.4853 != reported pathCost 25.0000" 등 비용 모델 붕괴를 검출.
```

tests/pathfinder_tests.cpp의 모든 비용 검사는 `Near(dijkstra.pathCost, astar.pathCost)` 형태의 알고리즘 간 상대 동등성뿐이다(33-48행). kDiag는 이동 비용과 Octile 휴리스틱 양쪽에서 동시에 쓰이므로 함께 바뀌면 세 알고리즘이 여전히 같은 답을 내고 테스트는 조용히 통과한다. 절대값을 고정하는 유일한 assertion은 `TestStartEqualsGoal`의 pathCost==0(94행)인데 이건 어떤 비용 모델에서도 0이라 정보가 없다. 결과적으로 step02_pathfinding.md:20-21이 "공정한 비교 조건"의 첫 항목으로 선언한 "직선 비용 1, 대각선 비용 √2"가 자동 검증 대상이 아니다.

**방향**: 빈 격자에서 알려진 절대 비용을 하나 이상 못 박아라. 예: `Grid::Init(24,16)`의 기본 start(2,8)-goal(21,8)은 순수 수평 19칸이라 pathCost==19.0이어야 하고, 대각을 강제하는 배치(예: goal을 (21,15)로 옮김)에서는 `7*√2 + 12` 같은 해석적 기대값을 그대로 비교하면 kDiag 변이가 즉시 죽는다.

### 4. [높음] PROJECTS_PLAN이 ai_lab에 명시한 "steering 경계 조건" 자동 검증이 0 — 알고리즘 코어 대부분이 raylib Scene 안에 갇혀 headless 테스트 불가

<sub>분류: 검증력부족</sub>

**근거**

```text
PROJECTS_PLAN.md:29,36 — "빌드 성공은 완료 조건이 아니다. 렌더링과 UI에서 계산 코어를 분리하고 최소한 다음 자동 검증을 둔다." / "`ai_lab`: 경로 비용, 도달 불가 상태, flow field, steering·boids 경계 조건". CMakeLists.txt:57-67 — 테스트 타깃 2개가 컴파일하는 소스는 `tests/pathfinder_tests.cpp`, `tests/ai_model_tests.cpp`, `src/grid/grid.cpp` 뿐이며 6개 `src/scenes/*.cpp`(총 951줄)는 어느 테스트에도 들어가지 않는다. steering 관련 심볼(Seek/Arrive/Wander/Pursue/Truncate/maxForce)은 tests/ 전체에서 0건.
```

자동 검증되는 계산 코어는 `pathfind/pathfinder.h`, `grid/grid.cpp`, `model/ai_model.h`의 4개 헬퍼(LimitSpeed, CombineInfluence, SelectFlowDirection, ReflectAxis)뿐이다. 다음은 전부 Scene .cpp 안에 있고 테스트되지 않는다: (a) SteeringScene::Update의 Seek/Arrive/Wander/Pursue 전부와 maxForce/maxSpeed 절단, 경계 반사(steering_scene.cpp:30-91, 특히 84-87의 자체 clamp+0.5 감쇠 — 여긴 검증된 ReflectAxis도 쓰지 않는다), (b) FlowFieldScene::Rebuild의 goal 기점 Dijkstra integration field(flowfield_scene.cpp:38-63) — flow field 검증이 국소 정책 SelectFlowDirection에만 있고 필드 생성 자체엔 없다, (c) InfluenceScene::Step의 decay/stamp/spread 루프(influence_scene.cpp:28-60), (d) BoidsScene의 세 규칙과 공간 해시(boids_scene.cpp:35-108). 더구나 이 Scene들의 Update는 GetMousePosition/IsMouseButtonPressed/GetScreenWidth를 직접 호출하므로 창 없이는 실행 자체가 불가능하다. "boids 경계 조건"만 ReflectAxis/LimitSpeed 단위 테스트로 간접 충족되고, "steering 경계 조건"은 어떤 형태로도 충족되지 않는다.

**방향**: 각 Scene에서 순수 계산부를 pathfinder.h/ai_model.h처럼 raylib 비의존 헤더로 뽑아내라. 최소한 (1) steering 한 스텝을 계산하는 함수(desired→truncate→적분→경계)를 분리해 "속도가 항상 maxSpeed 이하"(step04:68이 직접 요구), Arrive의 slowRadius 내 감속 단조성, Pursue lookahead 경계를 테스트하고, (2) integration field 생성을 함수로 분리해 좁은 통로 우회 비용과 도달 불가 셀 무한대를 테스트하며, (3) influence Step을 분리해 step06:86이 "검증한다"고 적은 결정론(같은 배치·설정 → 같은 결과)과 벽 차단을 실제 테스트로 만들어라.

### 5. [중간] README·step02의 대표 측정치 expanded/maxOpen에 assertion이 하나도 없음

<sub>분류: 검증력부족</sub>

**근거**

```text
src/pathfind/pathfinder.h:80 `++stats_.expanded;` 를 삭제한 변이 → `ai_pathfinder_tests.exe` "pathfinder tests passed", exit=0. 프로브 출력은 "forced-neighbor map: dij cost=31.14213 exp=0 | jps cost=31.14214 exp=0"으로 계측치가 전부 0이 됐음을 보여준다. tests/pathfinder_tests.cpp에 `expanded`·`maxOpen` 문자열이 등장하지 않는다.
```

README.md:7은 이 lab의 존재 이유를 "글에서 'JPS는 jump point만 open list에 넣어 A* 대비 expand 노드 수를 줄인다'고 적은 문장이, 이 lab에서는 같은 맵에서 두 알고리즘의 expand 수가 실제로 갈라지는 수치로 확인된다"라고 선언하고, README.md:35-36이 expand 노드 수와 open list 최대 크기를 측정 항목 1·2번으로 올린다. 그런데 그 수치를 검증하는 자동 테스트가 전혀 없다. 실측(원본 코드 프로브)으로는 forced-neighbor 맵에서 Dijkstra exp=588 vs JPS exp=6으로 주장 자체는 성립하므로, 문제는 구현이 아니라 검증 부재다.

**방향**: 고정 맵에서 `jps.expanded < astar.expanded`, `astar.expanded <= dijkstra.expanded`, `maxOpen > 0` 같은 부등식 불변식을 테스트로 못 박아라. 열린 격자 등 감소 폭이 큰 맵을 고르면 안정적이다.

### 6. [중간] Greedy와 Manhattan은 테스트에 단 한 번도 등장하지 않음

<sub>분류: 검증력부족</sub>

**근거**

```text
`grep -rn "Greedy\|Manhattan" tests/` → 결과 0건(exit 1). tests/pathfinder_tests.cpp가 쓰는 알고리즘은 Dijkstra/AStar/JPS, 휴리스틱은 Octile/Euclidean/Chebyshev뿐(34-36, 55-59, 79-81행).
```

README.md:37은 "Manhattan은 대각 이동에서 과대평가하므로 비최적 경로가 가능한 비교용 설정이고, Greedy도 최적성을 보장하지 않는다"라고, step02_pathfinding.md:25·51은 "Manhattan은 ... A*의 최적성 조건을 깨뜨린다", "Greedy는 휴리스틱만 따라가므로 더 긴 경로를 낼 수 있다"라고 주장한다. 이 주장들은 lab이 검증해야 할 이론 명제인데(비최적성은 '실패 주입'에 해당하는 관측 대상이다) 자동 테스트가 없다. 4개 알고리즘 중 1개, 4개 휴리스틱 중 1개가 컴파일만 되고 실행 검증이 없는 상태이며, Greedy/Manhattan 코드 경로가 깨져도 CI는 초록이다.

**방향**: Greedy의 pathCost >= Dijkstra의 pathCost, 그리고 Manhattan 휴리스틱 A*가 특정 대각 지형에서 Dijkstra보다 큰 비용을 낸다는 것을 명시적 테스트로 고정하라(같은 맵에서 최소 하나는 실제로 크게 나오는 배치를 골라야 자명한 항등식이 되지 않는다).

### 7. [중간] step01이 선언한 "알고리즘 진행은 Update에서만" 불변식을 세 Scene이 위반

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
step01_skeleton.md:56 "`Update`와 `DrawWorld`를 분리하는 이유는 측정과 재현성이다. 알고리즘 진행은 Update에서만 일어나야 하며, 화면을 두 번 그렸다고 탐색이 두 단계 진행되면 안 된다." / step01_skeleton.md:96 "4. Draw 호출 횟수가 Scene 상태를 바꾸지 않는다." 그러나 pathfind_scene.cpp:98-101 `if (GuiButton(..., "Step")) pf_.Step(grid_); if (GuiButton(..., "Run")) pf_.RunToCompletion(grid_);` 은 `DrawPanel` 안이다. flowfield_scene.cpp:182-186 `DrawWorld`가 `grid_.Layout(...); layoutReady_ = true; if (dirty_) Rebuild(); if (...) RespawnAgents();` 를 수행하고, boids_scene.cpp:110-113 `DrawWorld`가 `if (first) Respawn();` 를 호출한다.
```

탐색 진행(Pathfinder::Step/RunToCompletion), 벡터장 재계산(Rebuild), 에이전트 재생성(RespawnAgents/Respawn)이 모두 Draw 경로에서 일어난다. 특히 FlowFieldScene::Update는 flowfield_scene.cpp:131 `if (!layoutReady_) return;` 로 첫 프레임에 즉시 반환하는데 layoutReady_는 DrawWorld에서만 세워지므로, integration field 최초 빌드와 에이전트 최초 스폰은 구조적으로 렌더 호출에 의존한다. 이는 PROJECTS_PLAN.md:29의 "렌더링과 UI에서 계산 코어를 분리하고"와도 어긋난다. 게다가 step01 §5의 검증 기준 5항목 전부에 대해 자동 검증이 존재하지 않아 이 위반이 드러날 통로가 없다.

**방향**: 패널의 Step/Run 버튼은 플래그만 세우고 다음 Update에서 소비하도록 바꾸고, FlowFieldScene의 레이아웃 의존을 걷어내 Rebuild/RespawnAgents를 Update 경로로 옮겨라. 그 뒤 "DrawWorld/DrawPanel을 N번 호출해도 시뮬레이션 상태가 동일하다"를 실제 테스트로 만들 수 있다.

### 8. [중간] step05 문서가 말하는 maxForce 제한이 BoidsScene에 존재하지 않음

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
step05_boids.md:34 "합성 뒤에는 maxForce로 제한하고, 적분 뒤 속도는 maxSpeed로 제한한다. 개별 규칙을 먼저 과도하게 clamp하면 가중치의 의미가 달라진다." 그러나 boids_scene.h:34-39의 멤버는 `count_ / perception_ / wSep_ / wAli_ / wCoh_ / maxSpeed_` 뿐이고 maxForce가 없다. boids_scene.cpp:93-96 `Vector2 v = Add(b.vel, Mul(acc, dt)); const auto limited = LimitSpeed({v.x, v.y}, maxSpeed_ * 0.3f, maxSpeed_);` — acc는 어떤 절단도 없이 적분되고 속도만 제한된다.
```

문서가 설명하는 2단계 절단(힘 절단 + 속도 절단) 중 힘 절단 단계가 구현에 없다. 반대로 구현에는 문서에 없는 하한선(최소 속도 = maxSpeed*0.3)이 LimitSpeed로 들어가 있어, 이웃이 없거나 규칙이 상쇄돼도 boid가 절대 멈추지 않는다. step04_steering.md:22-23이 설명하는 SteeringScene의 2단계 절단(Truncate 두 번, steering_scene.cpp:79-80)과도 모델이 다른데 문서는 이 차이를 언급하지 않는다.

**방향**: BoidsScene에 maxForce 절단을 추가해 문서와 맞추거나, step05 §2를 실제 모델(가중 합 → 속도 절단, 최소 속도 하한 포함)로 고쳐 쓰고 하한선을 둔 이유를 적어라.

### 9. [중간] 6개 step 문서 전부에 실행 명령과 관측 결과가 없다

<sub>분류: 문서오류</sub>

**근거**

```text
`grep -rn "cmake\|ctest\|ai_lab.exe\|--build" projects/mini/ai_lab/articles/` → 0건(grep exit=1). 6개 문서의 구현 언어 코드 펜스는 step01 2개(45-53, 75-84), step02 1개(32-35), step03 1개(41-48), step04 1개(21-24), step05 1개(41-56), step06 2개(31-46, 58-65)로 총 ~55줄 조각이다.
```

PROJECTS_PLAN.md:48 "step 종료 시 실행 명령과 관측 가능한 결과가 있다"와 :47 "문서의 코드만 따라도 해당 상태를 빌드할 수 있다" 두 기준을 6개 문서 모두 충족하지 못한다. 각 문서의 §5 "검증 실험"은 "좁은 통로를 만들고 히트맵이 우회 비용을 반영하는지 본다"(step03:71) 같은 GUI 수동 관찰 목록이며, 명령·기대 수치·합격 조건이 없다. 반면 실제로는 `ctest` 로 돌릴 수 있는 자동 테스트 2개가 존재하는데(빌드 산출물 build/Testing/Temporary/LastTest.log에 2/2 통과 기록이 있다) 어느 문서도 이를 언급하지 않는다. 참고로 기준 2(후속 step 예고 금지)는 저장소 자체 검사기 `python scripts/check_project_docs.py`에서 ai_lab 관련 오류 0건으로 충족한다.

**방향**: 각 step 말미에 `cmake --build build --config Release && ctest --test-dir build -C Release -R <해당 테스트>` 와 그 출력(예: "pathfinder tests passed"), 그리고 GUI에서 읽을 수 있는 구체 수치(예: forced-neighbor 맵에서 Dijkstra expanded=588 vs JPS expanded=6)를 적어라. 실측으로 확인한 값이다.

### 10. [중간] 이론 축의 핵심 주제인 국소 회피(part21)와 flow field의 cost field 단계가 lab에도 없고 범위 메모에도 없다

<sub>분류: 문서대응누락</sub>

**근거**

```text
ai/part21_local_avoidance.md는 §3 Velocity Obstacle, §4 RVO와 ORCA, §5 Crowd 파라미터, §6 교착 상태와 병목을 다루고 ai/README.md:36에서 "고급 이동과 군중" 필수 구성으로 분류돼 있다. ai/part10_flow_field.md는 §2 Cost Field 빌드(통행 비용 인코딩, 정적/동적 비용 합성)와 §6 유닛별 Steering 적용(양선형 보간)을 별도 절로 둔다. 반면 ai_lab의 회피는 flowfield_scene.cpp:153-164의 O(n²) separation 한 종류뿐이고, integration field는 flowfield_scene.cpp:55 `float step = (dx[k] && dy[k]) ? kDiag : 1.0f;` 로 균일 비용 고정이며 셀별 통행 비용 개념이 없다. 에이전트는 flowfield_scene.cpp:150에서 셀 방향을 그대로 읽어 보간이 없다.
```

README.md:83-85의 "구현 범위 메모"는 part07의 Theta*/HPA*/D* Lite, part08 NavMesh, part01~05 BT, part13~16만 제외 대상으로 명시한다. part17~25(국소 회피·AI 디버깅·적대 탐색·행동 계층·블랙보드·EQS 포함)에 대해서는 한 줄도 없어, 독자가 이 lab의 커버리지를 판단할 근거가 없다. 25편 중 lab이 실제로 다루는 것은 part06·07(일부)·09·10(일부)·11·12의 6편이다.

**방향**: README의 구현 범위 메모에 part17~25 그룹의 처리(특히 part21 국소 회피가 미구현이며 flowfield의 separation은 part21 §2가 말하는 한계 있는 방식임)와 part10의 cost field·양선형 보간 미구현을 명시하라. 구현을 늘릴 필요는 없고, 선언과 실제를 일치시키면 된다.

### 11. [낮음] step06 spreadOne 스니펫이 벽 셀 초기화를 빠뜨려 문서 코드와 실제 동작이 다름

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
influence_scene.cpp:47 `if (!grid_.Walkable(x, y)) { next[(size_t)grid_.Index(x, y)] = 0.0f; continue; }` 가 실제 코드에 있고, influence_scene.cpp:40 주석은 "3) spread (벽 우회, 거리 감쇠 — 반복 확산)"라고 적는다. 반면 step06_influence.md:31-46의 `spreadOne` 스니펫에는 이 분기가 없다.
```

문서 스니펫대로 구현하면 벽 셀도 stamp/decay로 얻은 값을 보존하고 이웃에 다시 뿌리므로 "벽 우회"라는 문서의 전제가 성립하지 않는다. Step 문서 완료 기준 4·7(문서 코드만으로 해당 상태 재현, 최종 코드와 드리프트 없음)에 어긋난다. 같은 문서 안에서 §3의 파생 뷰 스니펫(57-66행)은 ai_model.h:44-51과 정확히 일치하므로, 드리프트는 이 한 블록에 국한된다.

**방향**: 스니펫에 벽 셀 0 초기화 분기를 넣어라. 근본적으로는 PROJECTS_PLAN.md:50이 요구하는 "스니펫 검증" 장치가 저장소에 없다 — scripts/check_project_docs.py는 제목·코드펜스 언어·후속 예고만 검사하고 스니펫과 소스 대조는 하지 않는다.

### 12. [낮음] step01의 IScene 스니펫에 OnExit가 없어 문서 코드만으로는 빌드 불가

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
step01_skeleton.md:45-53 스니펫은 `~IScene / Name / OnEnter / Update / DrawWorld / DrawPanel` 6개만 선언한다. 실제 src/scenes/iscene.h:16 에 `virtual void OnExit() {}` 가 있고, src/main.cpp:54 `scenes[(size_t)prevIdx]->OnExit();` 와 main.cpp:93 이 이를 호출한다.
```

Step 문서 완료 기준 4("문서의 코드만 따라도 해당 상태를 빌드할 수 있다")를 문자 그대로 적용하면 이 스니펫으로는 main.cpp가 컴파일되지 않는다. step01_skeleton.md:29의 구조 다이어그램도 "OnEnter / Update / DrawWorld / DrawPanel"만 나열해 OnExit를 빠뜨린다.

**방향**: 스니펫과 다이어그램에 OnExit를 넣고, Scene 전환 시 OnExit→OnEnter 순서(main.cpp:53-57)를 한 줄로 설명하라.

---

## graphics_gl_lab — 기준 미달 (13건)

학습 대상 대체는 없다. GLFW(창)·glad2(로더)·ImGui/ImPlot(UI)·glm(행렬)은 모두 이 lab의 학습 대상이 아닌 편의이며, VAO/VBO·divisor·indirect buffer·UBO/SSBO·압축 internalFormat·multisample FBO·GL_TIME_ELAPSED·KHR_debug 같은 학습 대상은 전부 원시 GL 호출로 직접 작성돼 있다(glm으로 MVP를 쓰는 것은 대응 문서 stage1_gl_fund_02_transform.md 자체가 권하는 방식이므로 대체가 아니다). 문제는 검증력이다. 유일한 자동 테스트 타깃은 `tests/graphics_gl_contract_tests.cpp` 한 파일만 컴파일하며 lab의 프로덕션 번역 단위를 하나도 링크하지 않는다 — 복사본에서 Individual 드로우를 N회에서 1회로 줄이고, per-call 광원 업로드를 전부 없애고, UBO 업로드 크기를 0으로 만들고, VRAM 측정을 0으로 고정해도 ctest는 1/1 통과했다. PROJECTS_PLAN이 graphics lab에 요구한 "자원 수명과 동기화 상태, 변환 결과, 가능한 경우 렌더 캡처 회귀"는 세 항목 모두 존재하지 않고 실패 주입도 없다. 대응 축 stage1 10편 중 프레임버퍼(렌더 타깃·멀티패스), 그림자, 후처리(HDR·톤 매핑·감마), 모델 로딩, 변환은 lab이 전혀 다루지 않는 반면, Step 4의 uber/permutation은 stage1 어디에도 없는 주제를 "시리즈의 주장"이라 부른다. 문서가 관측값으로 제시한 카운터 수치도 최종 바이너리 출력과 어긋나며, 6/7 step 문서가 후속 step을 예고한다.

### 1. [치명] 자동 테스트가 lab의 프로덕션 코드를 한 줄도 컴파일하지 않는다 — 렌더 코어를 망가뜨려도 ctest 통과

<sub>분류: 검증력부족</sub>

**근거**

```text
CMakeLists.txt:163 `add_executable(graphics_gl_contract_tests tests/graphics_gl_contract_tests.cpp)` — 테스트 타깃의 소스는 이 파일 하나뿐이다. build/graphics_gl_contract_tests.vcxproj 확인: `ClCompile Include="...\tests\graphics_gl_contract_tests.cpp"` 단 한 줄. scene.cpp / lighting_bindings.cpp / texture_modes.cpp / msaa_target.cpp / gpu_timer.cpp는 테스트 바이너리에 포함되지 않는다. 테스트가 호출하는 세 함수는 전부 헤더의 inline 함수이고, 저장소 전체 검색 결과 프로덕션 코드에서 호출되는 곳이 0이다: `grep -rn "ExpectedMipChainBytes|ExpectedBindingCost|ExpectedDrawSubmissions"` → 정의 3곳(scene.h:29, lighting_bindings.h:35, texture_modes.h:30) + 테스트 12곳, 그 외 없음. 변이 실험(원본 미변경, scratchpad 복사본): (1) scene.cpp Individual 경로를 `glDrawElements` 1회 + `counters.drawCalls = 1`로 축소, (2) lighting_bindings.cpp per-call 루프를 `for (int i = 0; i < 0; ++i)`로 무력화하고 UBO glBufferSubData 크기를 0으로, (3) texture_modes.cpp `MeasureSize`를 `return 0`으로 고정 → 동일한 include/컴파일 조건으로 재빌드 후 `ctest -C Release` 결과 `1/1 Test #1: graphics_gl_contract_tests ....... Passed`, `100% tests passed out of 1`.
```

이 lab이 주장하는 모든 것(드로우 제출 수, 바인딩 호출 비용, mip chain 메모리)은 실제로는 GL을 호출하는 .cpp 안에 있는데, 테스트는 그 .cpp들을 전혀 보지 않고 같은 규칙을 다시 적은 3개의 inline 예측 함수만 검사한다. mega 감사에서 나온 "테스트가 검증하는 코드와 실제 실행되는 코드가 다름" 유형과 정확히 같다. 특히 `BindingCost::bufferUpdates`는 런타임 카운터(`GlCounters`)에 대응 필드조차 없어(gl_counters.h:15-31에 drawCalls/instancedDraws/indirectDraws/stateChanges/uniformUpdates/barrierCalls만 존재) 테스트 모델 안에서만 존재하는 값이다. 결과적으로 자동 테스트의 실제 검증 범위는 0이며, PROJECTS_PLAN.md:29 "빌드 성공은 완료 조건이 아니다... 최소한 다음 자동 검증을 둔다"를 충족하지 못한다.

**방향**: 계산 코어를 GL에서 분리해 테스트에 링크하라. 최소한 (a) Scene의 드로우 제출 계획(모드별 제출 수·MDI 커맨드 배열 내용)을 GL 호출 없이 만들어 반환하는 함수를 두고 테스트가 그 결과와 `ExpectedDrawSubmissions`를 비교, (b) LightingBindings::Apply를 GL 호출부와 카운팅부로 분리해 실제 counters 증가분과 `ExpectedBindingCost`를 비교, (c) TextureRegistry의 GL 측정값(BytesFor)과 `ExpectedMipChainBytes`를 실행 시 비교해 불일치를 KHR_debug 로그/패널에 띄우도록 하라. 그리고 tests 타깃에 해당 .cpp를 실제로 링크하라.

### 2. [높음] PROJECTS_PLAN이 graphics lab에 요구한 3개 자동 검증 항목이 모두 없다 — 실패 주입도 없음

<sub>분류: 검증력부족</sub>

**근거**

```text
PROJECTS_PLAN.md:37 "graphics labs: 자원 수명과 동기화 상태, 변환 결과, 가능한 경우 렌더 캡처 회귀". tests/graphics_gl_contract_tests.cpp(60줄)의 전체 내용은 TestSubmissionCounts / TestBindingCosts / TestMipStorage 세 함수뿐이며 자원 수명·동기화·변환 결과·캡처 회귀 어느 것도 없다. `Scene::Rebuild`(scene.cpp:90-98)가 6개 GL 핸들을 삭제·재생성하지만 그 수명 규칙을 검사하는 테스트가 없고, `MsaaTarget::Recreate`의 FBO 완전성 폴백(msaa_target.cpp:58-61)도 검사되지 않는다. 변환 결과도 미검증 — 뷰/투영 행렬은 scene.cpp:254-260의 glm::lookAt/glm::perspective로만 만들어지고 어떤 테스트도 그 결과를 보지 않는다. 실패 주입에 해당하는 것은 `ExpectedDrawSubmissions(Instanced, 0) == 0`(테스트 24행)과 `ExpectedMipChainBytes(None, 512, 512) == 0`(48행) 두 경계값뿐이며, 이는 inline 예측 함수의 경계일 뿐 구현을 깨뜨리는 입력이 아니다.
```

articles/step03_drawcall.md:5는 세 드로우 모드의 "시각 결과는 *비트 동일*"이라고 단언하지만 이 동일성을 확인하는 수단이 lab에 전혀 없다. glReadPixels 한 번 없고 캡처 비교도 없다. mini 완료 기준표(projects/mini/README.md:11-19)의 '실패 주입'·'자동 테스트' 칸이 이 lab에서는 사실상 비어 있다.

**방향**: 오프스크린 FBO에 세 드로우 모드로 같은 프레임을 그린 뒤 glReadPixels 결과를 해시 비교하는 headless 회귀를 추가하면 '비트 동일' 주장과 자원 수명을 동시에 잡을 수 있다. Rebuild를 수천 회 반복해 핸들 누수·이중 삭제를 KHR_debug 로그로 검출하는 스모크도 GUI 없이 가능하다.

### 3. [높음] 대응 축 stage1 10편의 절반 이상이 lab에 없고, 반대로 Step 4의 주제는 대응 문서에 존재하지 않는다

<sub>분류: 문서대응누락</sub>

**근거**

```text
lab 소스 전체 검색: `grep -rni "glFramebufferTexture|shadow|tonemap|bloom|RGBA16F|SRGB|glGenSamplers|glTexStorage|ANISOTROPY|assimp" src shaders` → 히트 0. glFramebuffer* 호출은 msaa_target.cpp:53,55의 `glFramebufferRenderbuffer` 두 개뿐이라 렌더 타깃 텍스처가 아예 없다. 반면 대응 문서는: stage1_gl_fund_05_framebuffer.md:5 "그림자 매핑, HDR 후처리, 디퍼드 셰이딩, 블룸, SSAO, 화면 전체 효과 — 은 *화면이 아닌 텍스처에 한 번 그린 뒤 그 결과를 다시 입력으로 쓰는* 모델로 동작한다", stage1_gl_proj_04_shadow.md(547줄, 그림자 매핑 전편), stage1_gl_proj_05_postprocess.md(730줄, HDR/블룸/톤매핑/ping-pong FBO), stage1_gl_proj_02_model.md(607줄, Assimp 모델 로딩), stage1_gl_fund_04_texture.md의 `glTexStorage2D` 13회 언급(lab은 glTexImage2D만 사용). 역방향: `grep -rni "permutation" graphics/stage1_*.md` → 히트 0, `grep -rni "uber" graphics/stage1_*.md` → 히트 0. 그런데 README.md:108은 "graphics OpenGL 단계에서 다룬 ... \"static permutation과 uber shader는 프로그램 수와 분기 비용을 교환한다\" ... 주장을 이 lab에서 측정한다"고 적는다.
```

이 축의 핵심 주제(컨텍스트·파이프라인·셰이더, 버퍼·VAO, 텍스처, 프레임버퍼, 변환 행렬, 라이팅·그림자·후처리) 중 lab이 실제로 측정하는 것은 버퍼·VAO, 텍스처 압축, 라이팅(Lambert/Blinn-Phong)뿐이다. 프레임버퍼는 MSAA 렌더버퍼 한 용도로만 쓰이고 문서가 강조한 '텍스처에 그려 다시 입력으로 쓰는' 모델은 없으며, 그림자·후처리·모델·변환은 검증 대상에서 완전히 빠졌다. 동시에 Step 4는 짝 문서가 하지 않은 주장을 짝 문서의 주장이라고 소개한다.

**방향**: README와 step 문서의 '짝 시리즈' 서술을 실제 대응 관계로 정정하고(permutation 주제는 stage0_fund_05_shading_materials.md:307에만 언급된다), lab이 다루지 않는 축 주제(FBO 렌더 타깃, 그림자, 후처리, 변환)를 '이 lab 범위 밖'으로 명시하거나 최소 한 개 이상의 멀티패스 토글을 추가하라.

### 4. [높음] 문서가 '관측 가능한 결과'로 제시한 카운터 수치가 최종 바이너리 출력과 다르다

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
articles/step02_measure.md:223-229는 관측 결과로 `draw calls: 10000 / uniform updates: 20003 / state changes: 2 / barrier calls: 0`을, step02_measure.md:231은 "유니폼 업데이트 20003 (모델 + 색상 × 10000 + 글로벌 3)"을 적는다. step03_drawcall.md:51도 `uniform updates:  20003`, 105행과 176행은 Instanced/MDI에서 `uniform updates:  4`, 108행은 "Uniform 업데이트 *20,000 → 4*"라고 적는다. 최종 코드의 실제 합계: app.cpp:196-197 `prog->SetInt("uShadingModel", ...)` + `counters_.uniformUpdates++`(+1), scene.cpp:262-266 uView/uProj/uLightDir/uCameraPos + `counters.uniformUpdates += 4`(+4), scene.cpp:279-280 `shader.SetInt("uUseInstancing", 0)` + `counters.uniformUpdates++`(+1), scene.cpp:286 `counters.uniformUpdates += 2`(×10000) → Individual 20006, Instanced/MDI 6. 또 step02가 출력 예시에 넣은 `barrier calls: 0` 줄은 패널에 존재하지 않는다 — ui/panel.cpp:96-100은 draw calls / instanced draws / indirect draws / state changes / uniform updates 다섯 줄만 출력한다. state changes도 정상 상태에서는 1이다(app.cpp:193-195 `ProgramChangedSinceLast`는 프로그램이 바뀐 프레임에만 +1, scene.cpp:268-269 VAO 바인딩 +1).
```

각 step 문서의 수치는 그 step 시점 상태와는 맞아떨어지지만(예: 20003 = 글로벌 3 + 20000은 uCameraPos·uUseInstancing 도입 전 상태), 최종 코드를 빌드해 실행한 사용자가 보는 값은 20006/6이다. PROJECTS_PLAN.md:50 "최종 코드와 문서 코드가 드리프트하지 않도록 스니펫 검증을 둔다"는 장치가 이 lab에 전혀 없어 이런 어긋남이 검출되지 않는다.

**방향**: 문서의 카운터 표를 최종 코드 기준으로 갱신하거나, 각 step의 기대 카운터를 자동 테스트가 계산해 문서 값과 대조하는 스니펫 검증을 추가하라. barrier calls 줄은 GL lab에 실제 배리어 개념이 없으므로 문서 예시에서 삭제하거나 패널에 실제로 노출하라.

### 5. [높음] README·step06이 '측정한다'고 선언한 BC7 품질 비교가 lab에 존재하지 않는다

<sub>분류: 검증력부족</sub>

**근거**

```text
README.md:108 "...\"BC7은 DXT5와 같은 8 bpp에서 더 높은 품질을 노린다\"는 주장을 이 lab에서 측정한다", articles/step06_texture.md:3 "그 사이의 *품질 차이* 도 같은 패널에서 비교", :5 "학습자가 ... 주장을 *직접 측정*", :213 "graphics 시리즈의 *BC7 주장* 이 *측정으로 검증*". 실제 구현: ui/panel.cpp:159-190의 텍스처 섹션은 Combo 하나로 모드를 배타 선택하고 `BytesFor` 바이트 수만 출력한다. 동시 표시·A/B·차분·PSNR/SSIM 등 품질 지표는 소스 전체에 없고, textured.frag는 한 번에 sampler 하나(`uniform sampler2D uDiffuse`)만 받는다. step06_texture.md:176 "*품질* — DXT1 = *블록 경계 ringing artifact* 가장 큼. BC7 = uncompressed 와 *blind test 어려움*"은 측정 결과 표(§5) 안에 놓여 있지만 대응하는 측정 코드가 없다. 패널 자신은 panel.cpp:188-189에서 "압축은 드라이버의 온라인 인코더를 사용한다. 크기는 비교할 수 있지만 품질 벤치마크는 아니다"라고 반대로 적는다.
```

검증한다고 선언한 세 주장 중 하나(BC7 품질)가 lab의 관측 수단으로는 원리적으로 확인 불가능하다. 게다가 압축 자체가 드라이버의 generic 인코더에 위임돼 있어 품질은 드라이버마다 달라진다(step06_texture.md:73도 이를 인정한다). 문서 본문과 UI 문구가 서로 모순된다.

**방향**: 품질 주장을 검증하려면 같은 텍스처를 두 모드로 동시 바인딩해 화면 분할·차분 시각화하거나, glGetCompressedTexImage로 되읽어 원본 대비 RMSE/PSNR을 계산하는 관측값을 추가하라. 추가하지 않을 것이라면 README.md:108과 step06_texture.md:3,5,176,213에서 '측정/검증' 표현을 제거하고 패널 문구와 일치시켜라.

### 6. [중간] 'CPU frame ms'가 SwapBuffers를 포함해 GPU 대기를 흡수 — 문서의 bottleneck 판정 전제가 성립하지 않는다

<sub>분류: 검증력부족</sub>

**근거**

```text
src/app/app.cpp:136-144 `timer_->BeginFrame(); glfwPollEvents(); ProcessFrame(); Render(); RenderUI(); glfwSwapBuffers(window_); timer_->EndFrame();` — measure/frame_timer.cpp:23-26 `EndFrame()`은 `now - frameBegin_`을 그대로 `lastFrameMs_`로 삼고, ui/panel.cpp:71-72가 이를 "CPU frame: %.3f ms"로 표시한다. 즉 관측값에 SwapBuffers 블로킹(드라이버 큐 포화 시 발생)이 포함된다. 그런데 articles/step02_measure.md:48-53의 "Bottleneck 판단" 표와 step07_scale_msaa.md:157-167의 진단 플로우차트(`CPU ms 가 GPU ms 보다 큼? → CPU bound`)는 CPU ms가 순수 제출 비용이라는 전제 위에 서 있다. step07_scale_msaa.md:151의 예시 행 `| 100k | Instanced | off | 0.6 | 10 | GPU 약간 |`은 이 타이머 구현으로는 나올 수 없다 — GPU가 프레임당 10ms를 쓰면 큐 한도(통상 1~3프레임)에 도달한 뒤 SwapBuffers가 블록해 정상 상태의 프레임 wall-clock은 약 10ms로 수렴한다.
```

이 lab의 핵심 관측값 두 개 중 하나가 조작 변수(CPU 제출량)를 분리하지 못한다. GPU 바운드 구간에서 CPU 곡선이 GPU 곡선에 달라붙어 문서가 가르치려는 '두 선이 갈라진다'는 관찰 자체가 무너진다.

**방향**: 제출 구간(Render() 시작~끝)만 재는 CPU 타이머를 별도로 두어 'CPU submit ms'와 'frame wall-clock ms'를 분리 표시하거나, 최소한 step02·step07 문서에 현재 CPU 곡선이 swap 대기를 포함한다는 사실과 그로 인한 해석 한계를 명시하라.

### 7. [중간] step 완료 기준 2 위반 — 7편 중 6편이 후속 step을 예고한다

<sub>분류: 문서오류</sub>

**근거**

```text
PROJECTS_PLAN.md:45 "2. 후속 step을 예고하지 않는다." 위반 사례: step01_skeleton.md:15 "이후 단계에서 GPU 측정 (Step 2), 드로우콜 토글 (Step 3), 셰이더 perm (Step 4), 메모리 바인딩 (Step 5), 텍스처 압축 (Step 6), 스케일링 + MSAA (Step 7) 가 차례로 추가된다", :185 "Step 4 의 *셰이더 permutation 토글* 에서 ... 추가될 토대", :210 "GPU 측 시간은 *Step 2 의 `GL_TIME_ELAPSED` 쿼리* 가 추가될 자리"; step02_measure.md:251 "Step 3 의 *드로우콜 토글 ...* 이 자연스러운 다음 단계"; step03_drawcall.md:205 "Step 4 의 *Static permutation* 이 이 분기를 ... 분리한다", :239 "다음 단계 — *셰이더 자체의 비용 분포* ... Step 4 의 토픽"; step04_shader_perm.md:186 "다음 단계 — ... Step 5 의 토픽"; step05_binding.md:202 "다음 단계 — *텍스처 압축* ... Step 6 의 토픽"; step06_texture.md:217 "다음 단계 — *오브젝트 수와 MSAA 의 스케일링* ... Step 7 의 토픽". 소스 주석도 같은 패턴이다(src/app/app.h:5-9, src/scene/scene.h:5-8).
```

기준 위반이 산발적 실수가 아니라 전 문서의 구성 패턴이다. step01은 심지어 아직 존재하지 않는 코드의 자리를 예고하는 문장을 본문 중간에 둔다.

**방향**: 각 step 결론의 '다음 단계' 문단과 본문의 'Step N에서 추가된다' 문장을 제거하고, 해당 step의 종료 상태만 서술하도록 정리하라.

### 8. [중간] step06의 코드만 따라서는 빌드되지 않는다 — glad EXTENSIONS 요구와 지원 검사 코드가 문서에 없다

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
PROJECTS_PLAN.md:47 "4. 문서의 코드만 따라도 해당 상태를 빌드할 수 있다." step01_skeleton.md:71이 문서화한 CMake는 `glad_add_library(glad STATIC API gl:core=4.5)`이고, 이후 어떤 step 문서도 CMake 변경을 언급하지 않는다(`grep -rn "EXTENSIONS|GLAD_GL_EXT|glad_add_library" articles/*.md` → step01의 두 줄이 전부). 그러나 실제 CMakeLists.txt:74-76은 `glad_add_library(glad STATIC REPRODUCIBLE API gl:core=4.5 EXTENSIONS GL_EXT_texture_compression_s3tc GL_ARB_texture_compression_bptc)`이고, 생성된 헤더 build/gladsources/glad/include/glad/gl.h:22에 `--api='gl:core=4.5' --extensions='GL_ARB_texture_compression_bptc,GL_EXT_texture_compression_s3tc'`가 기록돼 있다. step06_texture.md:57이 쓰라고 제시한 `GL_COMPRESSED_RGB_S3TC_DXT1_EXT`(gl.h:350)와 실제 코드가 쓰는 `GLAD_GL_EXT_texture_compression_s3tc`(gl.h:1637, texture_modes.cpp:56)는 S3TC 확장을 요청하지 않으면 생성되지 않으므로 문서대로만 구성하면 컴파일 에러가 난다. 또한 texture_modes.cpp:56-57,65-76,107-109의 지원 여부 검사·폴백 로직과 panel.cpp:171-179의 unsupported 처리는 step06 문서에 전혀 등장하지 않는다(기준 3 '현재 step에서 추가·변경한 코드가 식별된다'도 함께 위반).
```

텍스처 압축 step은 빌드 구성 변경이 필수인데 그 변경만 문서에서 빠져 있다. 독자는 DXT1/DXT5 상수를 찾지 못해 막히고, 미지원 GPU에서의 폴백 동작도 알 수 없다.

**방향**: step06에 CMakeLists의 EXTENSIONS 추가 diff와 `GLAD_GL_EXT_texture_compression_s3tc` / `GLAD_GL_ARB_texture_compression_bptc` 런타임 검사, IsSupported 폴백 코드를 포함하라.

### 9. [중간] README의 짝 문서 참조 경로가 저장소에 존재하지 않는다

<sub>분류: 문서오류</sub>

**근거**

```text
README.md:15-20 토글 표의 '비교 대상' 열이 `graphics/gl/02 드로우콜`, `graphics/gl/04 셰이더 분기`, `graphics/gl/05 바인딩`, `graphics/gl/06 텍스처`를 가리킨다. `ls graphics/gl` → `No such file or directory`. 실제 축 문서는 graphics/stage1_gl_fund_01_context.md ~ stage1_gl_proj_05_postprocess.md 10편이며 graphics/README.md:'Stage 1 — OpenGL' 표가 그 목록이다. 번호 체계(02/04/05/06)도 실제 문서 번호와 대응하지 않는다.
```

lab이 '어느 문서의 어느 주장을 측정하는지'를 밝히는 유일한 대응표가 존재하지 않는 경로를 가리키고 있어, 문서↔lab 대응 자체를 독자가 확인할 수 없다. 앞서 지적한 permutation 주제 불일치도 이 표가 실제 문서와 대조되지 않은 결과로 보인다.

**방향**: 토글 표의 참조를 실제 파일 경로(예: `../../graphics/stage1_gl_fund_03_pipeline.md`)로 교체하고, 대응 문서가 없는 토글(셰이더 permutation)은 '대응 문서 없음'으로 표기하라.

### 10. [낮음] step02~07에 실행 명령이 없고, 어떤 step 문서도 자동 테스트를 언급하지 않는다

<sub>분류: 문서오류</sub>

**근거**

```text
PROJECTS_PLAN.md:48 "5. step 종료 시 실행 명령과 관측 가능한 결과가 있다." step별 실행 명령 히트 수(`cmake --build|ctest|graphics_gl_lab.exe|./build/graphics_gl_lab`): step01=4, step02=0, step03=0, step04=0, step05=0, step06=0, step07=0. 또 `grep -rn "ctest|contract_tests|자동 테스트" articles/*.md README.md` → 히트 0. README.md:96-102의 '진행 상태' 체크리스트도 Step 1~7만 나열하고 tests/graphics_gl_contract_tests.cpp나 CTest를 언급하지 않는다.
```

자동 검증은 mini 완료 기준의 핵심 항목인데(projects/mini/README.md:18-19) 이 lab의 문서 어디에도 그 존재·실행 방법·검증 범위가 적혀 있지 않다. step 2~7은 기대 수치 표만 있고 그 수치를 재현할 명령이 없다.

**방향**: 각 step 말미에 빌드·실행 명령과 그 step에서 눈으로 확인할 패널 값을 넣고, README 진행 상태에 `ctest -C Release`와 그 테스트가 실제로 보장하는 범위(현재는 예측 함수 3개)를 정직하게 적어라.

### 11. [낮음] uniformUpdates 카운터가 현재 프로그램에 없는 uniform 설정 시도까지 계수해 실제 GL 호출 수보다 크다

<sub>분류: 코드오류</sub>

**근거**

```text
scene.cpp:262-266은 프로그램 종류와 무관하게 `uView/uProj/uLightDir/uCameraPos`를 설정하고 `counters.uniformUpdates += 4`를 무조건 더한다. 그러나 shader_program.cpp:106-109 `SetVec3`는 `if (loc >= 0)`일 때만 glUniform3fv를 호출한다. shaders/lit_per_call.frag·lit_ubo.frag·lit_ssbo.frag에는 uLightDir·uCameraPos가 없고(common.vert:8-12에도 없다) perm_lambert.frag·perm_cel.frag·textured.frag에는 uCameraPos가 없다. 즉 Step 5 광원 모드에서는 프레임당 2회, Step 4 Lambert/Cel·Step 6 텍스처 모드에서는 1회의 '유령 업데이트'가 계수된다. gl_counters.h:5-9는 이 카운터를 "Scene 이 자기가 호출한 횟수를 알린다"고 정의한다.
```

이 lab의 관측값 정의 자체가 '실제 GL 호출 수'와 어긋난다. 바인딩 모드 비교(Step 5)에서 per-call vs UBO의 uniform 호출 차이를 재는 것이 핵심 관측인데, 기준선에 일정한 오차가 섞인다.

**방향**: SetXxx 계열이 실제로 glUniform을 호출했는지(bool 반환) 알려주게 하고 카운터는 그 결과로만 증가시켜라. 그러면 카운터가 문서의 정의와 일치한다.

### 12. [낮음] MSAA FBO 생성 실패 시 매 프레임 재시도하고 UI 표시와 실제 상태가 어긋난다

<sub>분류: 코드오류</sub>

**근거**

```text
msaa_target.cpp:58-61 `if (glCheckFramebufferStatus(...) != GL_FRAMEBUFFER_COMPLETE) { current_ = MsaaSamples::Off; }`. 그런데 msaa_target.cpp:37 `if (width == w_ && height == h_ && samples == current_) return;`가 유일한 게이트이므로, app.cpp:165가 매 프레임 호출하는 `EnsureSize(w, h, msaa_samples_)`는 요청값(예: X4)과 폴백된 current_(Off)가 계속 달라 매 프레임 Recreate를 재시도한다. 동시에 패널(panel.cpp:203-210)은 App의 `msaa_samples_`를 그대로 표시하므로 화면은 "4x"라고 알리지만 렌더는 기본 프레임버퍼로 나간다.
```

실패 경로가 조용히 흡수돼 관측값(MSAA 설정)과 실제 렌더 상태가 불일치한다. 측정 도구에서는 이 종류의 무언 폴백이 잘못된 결론으로 이어진다.

**방향**: 폴백 시 요청값 자체를 Off로 되돌려 UI와 동기화하고, 실패 사실을 KHR_debug 로그 창이나 패널에 한 번 표시하라.

### 13. [낮음] 어떤 코드도 로드하지 않는 shaders/basic.vert·basic.frag가 빌드 출력에 함께 복사된다

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
shader_registry.cpp:22-46이 로드하는 파일은 common.vert, uber.frag, perm_lambert/perm_blinn_phong/perm_cel.frag, lit_per_call/lit_ubo/lit_ssbo.frag, textured.frag 아홉 개뿐이고 basic.vert/basic.frag는 저장소 전체에서 참조되지 않는다. README.md:64-69의 디렉터리 구조에도 basic.* 는 없다. 그러나 CMakeLists.txt:157-160의 `copy_directory shaders`가 이 두 파일까지 build/Release/shaders로 복사한다(build/Release/shaders 확인). step01_skeleton.md:93-95, :168은 basic.vert/basic.frag를 이 lab의 셰이더로 소개한다.
```

Step 1의 잔재가 최종 트리에 남아, 문서(step01)와 최종 코드(ShaderRegistry) 중 어느 쪽이 현재 상태인지 파일만 봐서는 판단할 수 없다.

**방향**: basic.* 를 삭제하고 step01 문서가 common.vert/uber.frag 계보의 초기 형태를 설명하도록 정리하거나, 반대로 step01 상태 재현용임을 README에 명시하라.

---

## graphics_dx_lab — 기준 미달 (16건)

학습 대상 대체는 없다. 외부 의존성은 ImGui/ImPlot(UI), DirectXMath(수학 — 이 축의 학습 대상 아님), d3d12/dxgi/d3dcompiler/dxguid(API 본체)뿐이고, CD3DX12 헬퍼조차 쓰지 않은 채 D3D12_RESOURCE_BARRIER·힙 프로퍼티·루트 시그니처·PSO desc·타임스탬프 링을 전부 직접 채운다. 이 축의 학습 대상(배리어, 디스크립터 힙, 루트 시그니처, 커맨드 리스트, 펜스)은 라이브러리가 대신하지 않는다. 그러나 검증력이 사실상 비어 있다. CMakeLists가 테스트 타깃에 `tests/graphics_dx_contract_tests.cpp` 한 개만 넣어 제품 .cpp 13개를 하나도 링크하지 않으므로, 테스트가 만지는 것은 헤더 인라인 함수 3개(약 30줄)뿐이고 나머지 2,340줄은 전혀 검증되지 않는다. 복사본에 변이 5건(배리어 전량 미발급, 디스크립터 write 전량 제거, 루트 상수 오프셋 파괴, 루트 시그니처 상수 24→21, 드로우 절반 삭제)을 넣고 재빌드했으나 테스트는 그대로 exit 0으로 통과했다. 게다가 검증된 인라인 함수 중 `ExpectedDrawSubmissions`는 렌더러가 호출하지 않는 함수여서 실행 경로 밖을 검증한다. 문서 쪽에서는 step03의 루트 상수 오프셋 스니펫이 실제 코드와 다르고(1/17 vs 4/20), step03·step04의 "측정 결과" 표는 현재 Instanced 경로의 명령 호출 구조상 재현이 불가능하며, RootSigPolicy::PerDraw 경로에서는 FrameCB root CBV가 루트 시그니처 없이 설정되어 그 토글 자체가 정상 렌더를 내지 못한다.

### 1. [치명] 자동 테스트가 제품 .cpp를 하나도 링크하지 않는다 — 변이 5건 전부 생존

<sub>분류: 검증력부족</sub>

**근거**

```text
C:/Users/REIN/Documents/VeLog/projects/mini/graphics_dx_lab/CMakeLists.txt:101
`add_executable(graphics_dx_contract_tests tests/graphics_dx_contract_tests.cpp)`

실행 파일 타깃(66~80행)에 들어가는 src/*.cpp 13개(app.cpp, d3d12_context.cpp, upload_buffer.cpp, pso_cache.cpp, root_signature.cpp, heap_manager.cpp, barrier_helpers.cpp, scene.cpp, frame_timer.cpp, timestamp_query.cpp, d3d12_debug.cpp, panel.cpp, main.cpp) 중 테스트 타깃에 들어가는 것은 0개다.

테스트가 호출하는 것은 헤더 인라인 함수 3개뿐:
- src/scene/scene.h:46 `ExpectedDrawSubmissions`
- src/descriptor/heap_manager.h:36 `PlanNullSrvWrites`
- src/barrier/barrier_helpers.h:27 `MakeTransitionBarrier`
합계 약 30줄. src+shaders 실측 2,340줄.

변이 실험(원본 미변경, 스크래치패드 복사본 C:/Users/REIN/AppData/Local/Temp/claude/.../scratchpad/dxmut):
1) barrier_helpers.cpp:29 `cmd->ResourceBarrier((UINT)pending_.size(), pending_.data());` 삭제
2) heap_manager.cpp:55 `if (plan.writes != 0) WriteNullSrvs(device, plan.first, plan.writes);` 삭제
3) scene.cpp:246 루트 상수 오프셋 4 → 1
4) root_signature.cpp:30 `Num32BitValues = 24` → `21`
5) scene.cpp:233 `for (size_t i = 0; i < objects_.size(); ++i)` → `objects_.size()/2`

재빌드 후 실행 결과:
```
BUILD_EXIT=0
DirectX contract tests passed
exit=0
```
(원본 빌드 산출물 build/Release/graphics_dx_contract_tests.exe도 동일하게 exit=0)
```

PROJECTS_PLAN.md:29는 "빌드 성공은 완료 조건이 아니다. 렌더링과 UI에서 계산 코어를 분리하고 최소한 다음 자동 검증을 둔다"고 못박고, graphics lab에 대해 "자원 수명과 동기화 상태, 변환 결과"를 요구한다. 실제로는 배리어를 전부 없애도, 디스크립터를 한 개도 쓰지 않아도, 오브젝트의 절반만 그려도 CTest가 초록으로 통과한다. 이 lab의 주제가 바로 배리어·디스크립터 힙·드로우 제출이라는 점에서, 검증 장치가 주제와 무관한 자리에 놓여 있다. projects/mini/VERIFY.md:41이 "CTest 1개"를 완료 근거로 세는 것도 이 실체와 어긋난다.

**방향**: 테스트 타깃에 최소한 barrier_helpers.cpp / heap_manager.cpp / scene.cpp의 순수 계산부를 링크하고, 렌더 명령 기록을 ID3D12GraphicsCommandList 인터페이스 뒤의 기록기(fake)로 받아 (a) 정책별 SetPipelineState/SetGraphicsRootSignature/ResourceBarrier 호출 수, (b) PRESENT→RT→PRESENT 상태 전이 순서, (c) 오브젝트 수와 드로우 제출 수의 일치를 assert하라. 위 변이 5건이 각각 최소 한 개의 CHECK를 깨는 것을 회귀 기준으로 삼으면 된다.

### 2. [높음] RootSigPolicy::PerDraw 경로에서 FrameCB(b1) root CBV가 루트 시그니처 없이 설정된다

<sub>분류: 코드오류</sub>

**근거**

```text
C:/Users/REIN/Documents/VeLog/projects/mini/graphics_dx_lab/src/app/app.cpp:216-225
```
    // Root signature.
    if (rsPolicy_ == RootSigPolicy::Global) {
        cmd->SetGraphicsRootSignature(rootSig_.Get());
        counters_.rootSigSets++;
    }

    // Frame CBV.
    D3D12_GPU_VIRTUAL_ADDRESS cbAddr =
        frameCB_->GetGPUVirtualAddress() + f * sizeof(FrameCB);
    cmd->SetGraphicsRootConstantBufferView(1, cbAddr);
```
src/device/d3d12_context.cpp:183 `cmdList_->Reset(allocators_[frameIndex_].Get(), nullptr);` — Reset은 루트 시그니처 바인딩을 초기화한다.
src/scene/scene.cpp:176-181 — PerDraw일 때만 Scene 내부에서 처음으로 `SetGraphicsRootSignature`를 호출한다.
Scene::Draw 어디에서도 b1(root CBV)을 다시 설정하지 않는다(grep `SetGraphicsRootConstantBufferView` → src/app/app.cpp:225 한 곳뿐).
```

rsPolicy_ == PerDraw이면 app.cpp:225 시점에 이 커맨드 리스트에는 루트 시그니처가 하나도 바인딩돼 있지 않다. 루트 인자를 루트 시그니처보다 먼저 설정하는 것은 유효하지 않으며(디버그 레이어 오류), 이후 Scene::Draw가 루트 시그니처를 "없음 → rootSig_"로 *변경*하므로 그 이전 인자는 stale이 된다. 결과적으로 Per-Draw RootSig 정책에서는 viewProj/lightDir/cameraPos를 담은 FrameCB가 프레임 내내 유효하게 바인딩되지 않는다. 즉 Step 4의 토글은 "CPU 호출 수만 다르고 화면은 같다"가 아니라 정상 렌더 자체가 나오지 않는 경로다.

문서는 반대로 적혀 있다.
- src/rootsig/root_signature.h:9 "실제 변경되는 시그니처는 *하나* 라 시각 결과 동일. CPU 호출 빈도만 비교."
- articles/step04_rootsig.md:34 "Per-Draw — 각 드로우 직전에 *같은 RS 를 다시 set*. 실제로는 *no-op 캐시 hit* 되지만 CPU 호출 비용은 발생."
같은 RS를 중복 설정하는 것 자체는 인자를 무효화하지 않지만, 여기서 문제는 "중복 설정"이 아니라 프레임 첫 바인딩이 루트 인자 설정보다 뒤에 온다는 순서다.

**방향**: 정책과 무관하게 프레임 시작 시 `SetGraphicsRootSignature`를 한 번 호출해 유효 상태를 만든 뒤(카운터는 Global일 때만 증가시키든 별도 표기하든), 그다음 b1 CBV를 설정하라. 또는 Scene::Draw가 RS를 (재)설정한 직후 b1 CBV를 함께 재바인딩하라. 어느 쪽이든 "루트 인자는 항상 바인딩된 루트 시그니처 뒤에 온다"를 기록기 기반 테스트로 고정하는 것이 이 lab의 주제에 맞는다.

### 3. [높음] 테스트가 검증하는 ExpectedDrawSubmissions를 렌더러가 사용하지 않는다

<sub>분류: 검증력부족</sub>

**근거**

```text
grep 결과(build/ 제외):
```
src/scene/scene.h:46:inline int ExpectedDrawSubmissions(DrawMode mode, int objectCount) {
tests/graphics_dx_contract_tests.cpp:22,23,24  (호출 3곳)
```
제품 경로의 실제 제출 수는 별도 로직이 만든다.
- src/scene/scene.cpp:206-207 `cmd->DrawIndexedInstanced(indexCount_, (UINT)objects_.size(), 0, 0, 0); counters.instancedDraws++;`
- src/scene/scene.cpp:250-251 `cmd->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0); counters.drawCalls++;`

projects/mini/VERIFY.md:60 "`graphics_*_lab`: draw 제출 수, 리소스 수명 정책, barrier·mip 계산"
```

`ExpectedDrawSubmissions(PerDraw, 10000) == 10000` 은 인라인 헬퍼 자기 자신을 확인할 뿐, Scene::Draw의 루프가 실제로 10,000번 제출하는지와 아무 연결이 없다. 변이 실험 5번(드로우 루프를 절반으로 축소)에서 이 테스트가 통과한 것이 그 증거다. VERIFY.md가 "draw 제출 수"를 CTest가 검사한다고 적은 항목이 실제로는 실행 경로 밖에 있다.

**방향**: Scene::Draw가 채우는 GlCounters를 fake 커맨드 리스트로 받아 `counters.drawCalls + counters.instancedDraws == ExpectedDrawSubmissions(mode, n)` 형태로 두 값을 맞물리게 하라. 그래야 헬퍼가 제품 계약이 된다.

### 4. [높음] step03의 루트 상수 오프셋 스니펫(1/17)이 실제 코드(4/20) 및 step04 표와 불일치

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
articles/step03_drawcall_pso.md:22-24
```cpp
cmd->SetGraphicsRoot32BitConstants(0, 16, &model, 1);   // mat4
cmd->SetGraphicsRoot32BitConstants(0, 4,  &color, 17);  // color
cmd->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
```
src/scene/scene.cpp:246-248
```cpp
cmd->SetGraphicsRoot32BitConstants(0, 16, &mt, 4);    // offset 4.
float c[4] = { o.color.x, o.color.y, o.color.z, 1.0f };
cmd->SetGraphicsRoot32BitConstants(0, 4, c, 20);      // offset 20.
```
src/rootsig/root_signature.cpp:30 `params[0].Constants.Num32BitValues = 24;   // useInstancing + pad3 + mat4 + color.`
articles/step04_rootsig.md:22 "useInstancing(1) + pad(3) + mat4(16) + color(4) — cbuffer 패킹상 float4x4 가 16바이트 경계에서 시작해 패딩 3 DWORD 포함"
shaders/cube.hlsl:4-6 도 동일하게 pad(3) 전제
```

step03의 오프셋 1/17은 패딩 없는 21 DWORD 레이아웃을 전제하며, 24 DWORD 루트 상수 + HLSL cbuffer 16바이트 정렬 규칙과 맞지 않는다. 문서 스니펫대로 따라 하면 model 행렬과 color가 셰이더에서 어긋나 읽힌다. 게다가 step03은 useInstancing(offset 0) 설정 자체를 스니펫에서 빠뜨려, 그대로는 Per-Draw 분기가 동작하지 않는다. 이 C++ 오프셋 ↔ HLSL cbuffer 패킹 계약은 대응 이론 문서 graphics/stage2_dx_fund_03_hlsl.md의 "3. Constant Buffer와 packoffset / 16바이트 정렬 규칙 / packing과 컨벤션이 맞물리는 함정"의 핵심 주제인데, lab에는 이를 확인하는 자동 검증이 하나도 없다.

**방향**: step03 스니펫을 offset 0/4/20 세 호출로 고치고, `offsetof` 기반 정적 검사(useInstancing=DWORD 0, model=DWORD 4, color=DWORD 20, 합계 24)를 계약 테스트에 넣어 문서·루트 시그니처·HLSL 세 곳이 함께 움직이게 하라.

### 5. [높음] step03·step04의 "측정 결과" 표가 현재 코드 구조로 재현 불가

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
articles/step03_drawcall_pso.md:75-81 표
```
| Instanced | Per-Pass  | 0.3 | 2 |
| Instanced | Per-Draw  | 25  | 5 |
```
그러나 src/scene/scene.cpp:192-207 Instanced 분기는 PSO를 정확히 한 번만 설정한다.
```cpp
if (mode == DrawMode::Instanced) {
    setRootSigForDraw();
    if (psoPolicy == PsoSwitchPolicy::PerMaterial && primaryPso) { ... }
    else { setPsoForDraw(0); }
    ...
    cmd->DrawIndexedInstanced(indexCount_, (UINT)objects_.size(), 0, 0, 0);
```
(`setPsoForDraw`는 src/scene/scene.cpp:183-190, 인자 0으로 한 번 호출)

articles/step04_rootsig.md:47-50 표
```
| Global   | 8 | 0.3 |
| Per-Draw | 9 | 0.4 |
```
그러나 Instanced 분기에서 `setRootSigForDraw()`(scene.cpp:193)는 1회, Global 경로(app.cpp:218)도 1회다.

README.md:23-24
```
| PSO policy | Per-Pass / Per-Material / Per-Draw | `SetPipelineState` 호출 수 변경 |
| RootSig policy | Global / Per-Draw | `SetGraphicsRootSignature` 호출 수 변경 |
```
```

Instanced 모드에서는 PSO set도 RootSig set도 정책과 무관하게 1회다. 따라서 step03의 "Instanced + Per-Draw = CPU 25 ms, GPU 5 ms"(Per-Pass 대비 80배)와 step04의 "Instanced에서 Global 0.3 vs Per-Draw 0.4"는 이 코드가 만들 수 있는 수치가 아니다. README의 토글 표도 Instanced 모드에서는 성립하지 않는다. 추가로 두 표는 step05가 프레임 루프에 무조건 넣은 디스크립터 워크로드(app.cpp:206-210, 기본 정책 RebuildEachFrame에서 오브젝트 수만큼 최대 32,768회 `CreateShaderResourceView`)가 CPU 프레임 타이머(app.cpp:162, 282 사이) 안에 포함된다는 사실과도 맞지 않는다.

**방향**: 두 표를 현재 코드로 다시 측정하거나, 재현 불가한 행(Instanced × PSO/RootSig 정책)을 제거하고 "이 정책은 Per-Draw 드로우 모드에서만 호출 수를 바꾼다"고 명시하라. 측정 표에는 오브젝트 수·힙 정책·vsync 등 통제 변수를 함께 적어야 재현 가능해진다.

### 6. [중간] 문서 전체에 자동 테스트·ctest 언급이 0회

<sub>분류: 문서대응누락</sub>

**근거**

```text
grep -rn "ctest|contract_tests" articles/ README.md → 일치 0건.
articles/step05_descriptor.md:210-227 "## 9. 검증"은 빌드·실행 명령과 육안 체크리스트만 제시한다.
```
- Transient 모드에서 `descriptorWrites`가 매 프레임 N이다.
- Persistent 모드의 첫 프레임은 N, 안정 상태는 0이다.
```
반면 CMakeLists.txt:100-105에는 `enable_testing()` / `add_test(NAME graphics_dx_contract_tests ...)`가 존재하고, tests/graphics_dx_contract_tests.cpp:27-48 `TestDescriptorLifetimePlan`이 바로 그 계약을 자동 검사한다.
```

PROJECTS_PLAN.md의 mini lab 추적 단위는 "자동 테스트: GUI 없이 어떤 정확성을 검증하는가"를 요구하고, step 문서 기준 5는 "step 종료 시 실행 명령과 관측 가능한 결과"를 요구한다. 실제로 존재하는 유일한 자동 검증을 문서가 한 번도 소개하지 않아, 독자는 이 lab에 자동 검증이 있다는 사실 자체를 알 수 없다. 그 반대 방향(문서가 있다고 적은 검증이 코드에 없음)의 문제는 아니지만, 선언된 완료 기준과 문서 실제의 괴리다.

**방향**: README 빌드 절차와 각 step 말미에 `ctest --test-dir build -C Release --output-on-failure`와 기대 출력을 적고, step05 §9 체크리스트 중 자동화된 항목(디스크립터 계획 수식)과 육안 항목을 구분해 표기하라.

### 7. [중간] step07의 Scene Rebuild 서술과 UI 문구가 실제 코드와 반대(Flush)

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
articles/step07_scale.md:18-25
```
이 lab 의 단순한 구현 — *직접 Rebuild 호출 + 다음 프레임 시작 시 적용*. 진짜 안전을 위해 *Flush()* 권장 (수 ms 의 frame hitch 감수).

if (pendingCount != objectCount) {
    objectCount = pendingCount;
    scene.Rebuild(objectCount, ctx, /* cmd */, /* keepAlive */);
}
```
실제 src/app/app.cpp:164-170
```cpp
    if (pendingCount_ != objectCount_) {
        objectCount_ = pendingCount_;
        // Instance upload buffer 를 교체할 수 있으므로 이전 frame-in-flight 사용이
        // 끝났음을 보장한 뒤 재빌드한다.
        ctx_.Flush();
        scene_.Rebuild(objectCount_, ctx_, ctx_.CmdList(), uploadKeepAlive_);
    }
```
src/ui/panel.cpp:150 `ImGui::TextDisabled("(다음 프레임 시작 시 Scene 재빌드 — Flush 권장)");`
```

코드는 이미 Flush를 넣어 in-flight 자원 교체 문제를 해결했는데, 문서와 화면 문구는 아직 "권장" 상태로 남아 있다. 자원 수명은 이 lab의 명시적 학습 대상이고 PROJECTS_PLAN이 graphics lab에 요구한 "자원 수명과 동기화 상태" 항목이라, 정확히 이 지점의 서술이 낡아 있는 것은 완료 기준 7(최종 코드와 문서 코드 드리프트 금지) 위반이다.

**방향**: step07 §1을 "Flush로 이전 프레임 완료를 보장한 뒤 Rebuild한다"로 고치고 스니펫에 `ctx_.Flush();`를 포함시켜라. panel.cpp:150 문구도 함께 갱신하라.

### 8. [중간] LOC 비교 수치가 문서 간에 어긋나고 실측과 방향이 반대

<sub>분류: 문서오류</sub>

**근거**

```text
articles/step01_skeleton.md:30 "같은 1 만 큐브를 그리는 코드가 *OpenGL ~ 1 K LOC* 가 *DX12 ~ 3 K LOC*."
articles/step07_scale.md:52 표 "| 코드 LOC | ~3,000 | ~5,500 |" (OpenGL / DX12)

실측(build/ 제외, src+shaders):
```
graphics_dx_lab  src shaders  = 2340
graphics_gl_lab  src shaders  = 2542
```
(tests 포함 시 2413 / 2602)
```

step01은 3배, step07은 1.83배라고 서로 다르게 적고, 절대값도 두 문서가 다르다. 실제로는 DX12 lab이 GL lab보다 오히려 200줄 정도 짧다. "같은 장면을 DX12로 옮기면 코드가 3배"라는 서술은 이 저장소의 두 lab을 근거로는 성립하지 않는다.

**방향**: 두 문서의 수치를 실측값으로 통일하거나, LOC 비교 주장을 빼고 "명시적으로 다뤄야 하는 개념 수"처럼 실제로 확인 가능한 지표로 바꿔라.

### 9. [중간] step 문서 완료 기준 2(후속 step 예고 금지) 위반이 문서와 소스 주석 전반에 있음

<sub>분류: 문서오류</sub>

**근거**

```text
articles/step01_skeleton.md:7 "Step 2 의 GPU 타이머 + 디버그 콜백이 그 위에 얹힌다."
articles/step01_skeleton.md:88 "Step 5 의 Application heap 과 *분리* 유지."
articles/step01_skeleton.md:108 "Step 2 의 *Timestamp Query ...* 가 *측정 인프라* 의 다음 자연스러운 추가. 그 위에 Step 3~7 의 토글들이 누적된다."
articles/step02_measure.md:82 "Step 3~6 의 토글이 *어느 카운터를 줄이는지* 가 직관적."
articles/step03_drawcall_pso.md:98 "다음 Step 4 의 *RootSig 변경 빈도* 도 *근본 동일 패턴*"
articles/step04_rootsig.md:25 "| 3 | Descriptor Table | t1~t4 | bindless 텍스처 (Step 5+) |"
소스 주석에도 남아 있다: src/scene/scene.h:6 "Indirect 는 Step 7 의 Bindless 와 결합 시 추가.", src/rootsig/root_signature.cpp:24 "3: Descriptor Table (t1~t4) — bindless / 텍스처 (Step 5+)."
```

PROJECTS_PLAN.md:45 "후속 step을 예고하지 않는다"에 정면으로 어긋난다. 특히 step04:25와 root_signature.cpp:24가 예고한 "Step 5+의 bindless 텍스처"는 끝내 구현되지 않았고(아래 별도 항목), step05 §8은 반대로 "이 실행 코드에는 SM 6.6 직접 인덱싱이 없다"고 못박아 예고와 결론이 충돌한다.

**방향**: 각 step의 예고 문장을 삭제하고, 후속 주제는 마지막 step의 "범위 밖" 절에만 남겨라. 소스 주석의 step 번호 참조도 함께 정리해야 코드가 문서 순서에 종속되지 않는다.

### 10. [중간] step 문서 완료 기준 4(문서의 코드만 따라도 빌드) 미달

> **[이월 · 2026-08-13]** 개별 드리프트(step03 루트 상수 오프셋, step07 Flush 등)는 고쳐졌지만 구조적 격차는 남았다 — 7편 어디에도 `D3D12Context` 코드·CMakeLists·진입점·셰이더 전문이 없고, step별 "추가/변경 파일 목록"도 없다. 마커 기반 스니펫 대조 검사도 미도입.
>
> **[부분 반영 · 2026-08-14]** 세 가지가 들어갔고, 두 가지는 여전히 남았다.
>
> 들어간 것 — (a) **7편 전부에 `## 이 step 이 추가·변경하는 파일` 표를 신설**했다(step01:9,
> step02:7, step03:9, step04:9, step05:12, step06:9, step07:7). (b) 누락돼 있던 재현 코드를
> 게재했다 — 본문이 **815줄 → 2,801줄**로 늘었고(step01 114→1044, step02 125→472,
> step03 104→390, step04 68→211, step05 239→340, step06 89→223, step07 76→121),
> `D3D12Context`·`CMakeLists.txt`·진입점·셰이더가 이제 문서 안에 있다. (c) **앵커 85개**를
> 달았다(파일 6 / region 22 / 심볼 57) — 이 lab 한 곳이 저장소 118개 중 72%다. `check_snippets.py`가
> 0 error로 통과하므로 step03 오프셋 같은 드리프트는 이제 기계가 잡는다.
>
> **여전히 남은 것 둘, 그리고 부분 처리로 판정한 근거.**
>
> 1. **테스트 대역 1,300줄이 미게재다.** `tests/d3d12_stubs.h` 442줄 + `tests/graphics_dx_contract_tests.cpp`
>    859줄 = **1,301줄**인데, 문서에 실린 앵커는 `step04_rootsig.md:174`의
>    `tests/graphics_dx_contract_tests.cpp#TestRootConstantBudget` **한 개**뿐이다. 나머지는
>    "무엇을 검사하는지" 산문 서술과 CMake 타깃 정의로만 등장한다. 즉 **완료 기준 4를
>    제품 코드에 대해서는 충족했지만 테스트 대역에 대해서는 충족하지 못했다** — 문서만 따라가면
>    `graphics_dx_contract_tests`는 빌드할 수 없다. 이 lab의 #1이 "테스트가 제품 .cpp를 하나도
>    링크하지 않는다"였던 만큼, 그 대역이 문서 밖에 있는 것은 사소한 잔여가 아니다.
> 2. **고아 다이어그램 PNG 1개.** `articles/diagrams/step01_skeleton_diagram_01.png`가 `articles/`와
>    `README.md` 어디에서도 참조되지 않는다(`grep`으로 참조 0건 확인). step01이 1044줄로 다시
>    쓰이면서 그 그림을 부르던 자리가 사라졌는데 파일은 남았다.
>
> **남은 일**: 대역과 계약 테스트에 region 마커를 넣어 최소한 `d3d12_stubs.h`의 인터페이스 골격과
> 각 `Test*` 함수를 문서에 싣거나, 문서가 "테스트 전문은 소스를 보라"고 명시적으로 선언한다.
> PNG는 참조를 되살리거나 삭제한다.

<sub>분류: 문서오류</sub>

**근거**

```text
7편의 코드 블록 실측 줄 수:
```
step01 14, step02 39, step03 15, step04 4, step05 48, step06 15, step07 4  (합계 139줄)
```
실제 소스 src+shaders 2,340줄.
step01 §2 "D3D12Context 의 책임"(articles/step01_skeleton.md:32-42)은 책임을 불릿으로만 나열하고 D3D12Context 코드를 한 줄도 제시하지 않는다. CMakeLists, 진입점, 셰이더 전문도 어느 step에도 없다.
```

문서만 따라가면 어떤 step의 상태도 빌드할 수 없다. PROJECTS_PLAN.md:47(문서의 코드만 따라도 해당 상태를 빌드할 수 있다)과 :50(스니펫 검증을 둔다)이 요구하는 수준과 격차가 크고, 실제로 스니펫 검증 장치도 없어서 step03 오프셋 같은 드리프트가 걸러지지 않았다.

**방향**: 각 step에 "이 step에서 추가/변경한 파일 목록 + 핵심 전문 스니펫"을 넣고, 스니펫을 소스에서 발췌·대조하는 검사(마커 주석 기반 추출 후 diff)를 CTest에 추가하라.

### 11. [중간] step05가 선언한 실패 신호 "frame 구간 겹침"이 실제로 성립하는데 검사하지 않는다

<sub>분류: 검증력부족</sub>

**근거**

```text
articles/step05_descriptor.md:36 "| 실패 신호 | persistent 모드에서 매 프레임 write가 발생하거나 frame 구간이 겹침 |"
src/descriptor/heap_manager.h:43-48
```cpp
    if (policy == HeapPolicy::RebuildEachFrame) {
        return {(frameIndex % framesInFlight) * sliceSize, count, count, 0};
    }
    const bool changed = previousPolicy != policy ||
                         previousPersistentCount != count;
    return {0, count, changed ? count : 0, count};
```
Persistent는 항상 first=0, Transient의 frame slot 0도 first=0이다. 두 정책이 같은 heap 구간 [0, N)을 공유한다.
tests/graphics_dx_contract_tests.cpp:27-48은 frameIndex 5 하나만 확인하고, 구간 겹침·비겹침을 검사하는 CHECK가 없다.
```

heap_manager.h:57-58 주석은 "Rebuild 모드는 frame slot별 구간을 사용하므로 in-flight descriptor를 덮어쓰지 않는다"고만 보장하는데, 실행 중 정책을 Transient→Persistent로 바꾸면 현재 frame이 0번 slot이 아니어도 [0, N)에 쓴다. 즉 in-flight 상태인 slot 0 구간을 CPU가 덮어쓴다. 셰이더가 이 표를 읽지 않으므로 화면에는 드러나지 않지만, 문서가 스스로 실패 신호로 규정한 조건이 코드에서 성립하고 자동 검사도 없다.

**방향**: PlanNullSrvWrites에 persistent 전용 예약 구간(예: framesInFlight * sliceSize 이후)을 두거나, 최소한 테스트에 "모든 frameIndex의 transient 구간이 서로 겹치지 않고 persistent 구간과도 겹치지 않는다"는 불변식 CHECK를 추가하라.

### 12. [중간] barrier 카운터가 실제 ResourceBarrier 호출이 아니라 토글 값에서 유도된다

<sub>분류: 검증력부족</sub>

**근거**

```text
src/app/app.cpp:189, 270 (동일 패턴 2회)
```cpp
    counters_.barrierCalls += (barrierPolicy_ == BarrierPolicy::Split) ? 2 : 1;
```
src/barrier/barrier_helpers.h:52 `size_t PendingCount() const { return pending_.size(); }` — grep 결과 정의만 있고 호출처가 없다.
변이 실험 1번(barrier_helpers.cpp의 `cmd->ResourceBarrier((UINT)pending_.size(), pending_.data());` 삭제)에서도 UI 카운터는 여전히 1을 표시하며, 자동 테스트도 통과했다.
```

README.md:33이 "호출 카운터 - draw, instanced draw, PSO set, RootSig set, heap set, descriptor write, root constants, barrier"를 관측값으로 제시하는데, barrier만 실제 호출을 세지 않고 토글에서 역산한다. 다른 카운터(pipelineSets, rootSigSets, descriptorWrites)는 실제 호출 지점에서 증가하므로 관측값 자격이 있으나, barrierCalls는 무엇을 바꿔도 정책이 정하는 값을 그대로 되풀이한다. 현재는 계류 배리어가 항상 1개라 숫자가 우연히 맞지만, 관측이 아니라 선언이다.

**방향**: FlushImmediate/FlushBatched/FlushSplit이 실제로 발급한 ResourceBarrier 호출 수를 반환하게 하고 그 값을 counters_.barrierCalls에 더하라. 세 Flush의 반환값(계류 K개일 때 K / 1 / 2K)을 계약 테스트로 고정하면 변이 1번이 잡힌다.

### 13. [중간] 이론 축(stage2) 핵심 주제 상당수가 lab에 검증 대응물이 없다

<sub>분류: 문서대응누락</sub>

**근거**

```text
대응 이론 문서(C:/Users/REIN/Documents/VeLog/graphics/): stage2_dx_fund_01_com.md(COM 인터페이스·QueryInterface·참조 카운팅·ComPtr·GetAddressOf vs ReleaseAndGetAddressOf), stage2_dx_fund_02_resource.md(SRV/RTV/DSV/UAV/CBV, Heap Type, Residency, 업로드 정렬), stage2_dx_fund_03_hlsl.md(packing·packoffset·행렬 곱 순서·semantic), stage2_dx_fund_04_dx12.md(Resource State 머신, Transition/Aliasing/UAV/Split/Enhanced Barrier, Allocator/List, Queue·Fence, Descriptor Heap, PSO).

lab의 자동 검증 3건은 draw 제출 수 헬퍼, 디스크립터 write 계획, 단일 transition barrier desc 필드뿐이다(tests/graphics_dx_contract_tests.cpp:21-62).
검증 대응물이 전무한 항목: COM 참조 카운팅/QueryInterface 규약, UAV·CBV·텍스처 SRV, 업로드 정렬, 상태 전이 시퀀스(PRESENT→RT→PRESENT), 묵시적 promotion/decay, Aliasing/UAV barrier, Fence·frame-in-flight 불변식, 변환 결과(행렬).
특히 articles/step02_measure.md:41-67이 한 절 전체를 할애한 "ring slot을 fence 완료로만 읽는다" 규칙(src/measure/timestamp_query.cpp:54-56, 90-91, 101-102)은 순수 상태 기계인데 테스트가 0건이다.
```

PROJECTS_PLAN.md:37은 graphics lab에 "자원 수명과 동기화 상태, 변환 결과, 가능한 경우 렌더 캡처 회귀"를 요구한다. 현재 lab은 자원 수명 중 디스크립터 갱신 계획 하나만 자동 검증하고, 동기화 상태와 변환 결과는 전혀 검증하지 않는다. 여러 큐(Compute/Copy)와 큐 간 펜스 동기화는 구현 자체가 없어 fund_04 §5에 대응물이 없다.

**방향**: GPU 없이 검증 가능한 것부터 채워라. (1) TimestampQuery의 slot/fence 상태 기계를 fence 값을 주입하는 fake로 돌려 "미완료 slot은 절대 Map하지 않는다", "덮어쓰기 전에 항상 완료를 확인한다"를 assert. (2) 프레임의 배리어 시퀀스를 기록해 PRESENT→RENDER_TARGET→PRESENT 순서와 상태 짝을 검사. (3) BuildFrameCB의 viewProj와 인스턴스 model 행렬을 알려진 입력에 대해 오차 허용치로 비교.

### 14. [중간] InfoQueue1 메시지 콜백을 해제하지 않는다 — 이 축의 학습 주제(COM 수명) 위반

<sub>분류: 코드오류</sub>

**근거**

```text
src/measure/d3d12_debug.cpp:20-28
```cpp
void D3D12Debug::Install(ID3D12Device* device) {
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&infoQueue_)))) { ... return; }
    infoQueue_->RegisterMessageCallback(&Callback,
                                         D3D12_MESSAGE_CALLBACK_FLAG_NONE,
                                         this, &cookie_);
}
```
grep -rn "UnregisterMessageCallback" src → 일치 0건. `cookie_`(src/measure/d3d12_debug.h:44)는 저장만 되고 사용되지 않는다.
소멸 순서: src/app/app.h:53-62 선언 순서상 멤버는 역순 파괴 → `debug_`가 `ctx_`(디바이스 보유)보다 먼저 파괴된다. src/app/app.cpp:137-146 Shutdown()도 콜백 해제를 하지 않는다.
```

콜백은 `this`(D3D12Debug)를 컨텍스트로 등록돼 있는데, 등록을 해제하지 않은 채 D3D12Debug가 먼저 파괴된다. 이후 디바이스/자원 파괴 과정에서 디버그 레이어가 메시지를 내면 이미 파괴된 객체의 `Push`가 호출된다. 대응 이론 문서 stage2_dx_fund_01_com.md의 주제가 정확히 COM 객체 수명과 참조 규약인데, lab이 그 규칙을 어긴 자리가 하필 그 주제를 실습하는 모듈이다.

**방향**: D3D12Debug에 `Uninstall()`(또는 소멸자)에서 `infoQueue_->UnregisterMessageCallback(cookie_)`를 호출하고, App::Shutdown에서 ctx_.Shutdown()보다 먼저 부르라. 멤버 선언 순서로 파괴 순서에 의존하지 말고 명시적으로 해제 순서를 고정하는 편이 이 축의 교보재로도 맞는다.

### 15. [낮음] 루트 시그니처 슬롯 3(디스크립터 테이블 t1~t4)이 한 번도 바인딩되지 않는 죽은 슬롯

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
src/rootsig/root_signature.cpp:43-55에서 t1~t4 범위의 D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE을 정의한다.
grep -rn "SetGraphicsRootDescriptorTable" src → 일치 0건.
grep -rn "register(t" shaders → `shaders/cube.hlsl:33:StructuredBuffer<Instance> instances : register(t0);` 하나뿐. t1~t4를 선언하는 셰이더가 없다.
articles/step04_rootsig.md:25 "| 3 | Descriptor Table | t1~t4 | bindless 텍스처 (Step 5+) |"
src/rootsig/root_signature.cpp:24 "//   3: Descriptor Table (t1~t4) — bindless / 텍스처 (Step 5+)."
```

문서와 주석이 Step 5 이후 사용을 예고했으나 step05는 §8에서 bindless를 명시적으로 범위 밖으로 선언했고 실제 바인딩 호출도 없다. 루트 시그니처 예산 1 DWORD를 소비하는 죽은 파라미터가 남아 step04의 "29 DWORD" 계산에도 포함돼 있다.

**방향**: 슬롯 3을 제거하고 step04의 예산 계산을 28 DWORD로 정정하거나, 남긴다면 "현재 바인딩하지 않는 예약 슬롯"임을 문서와 주석에 명시하라.

### 16. [낮음] D3D12Context 헤더 주석이 실제 구현과 다름(Compute/Copy Queue, BGRA8)

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
src/device/d3d12_context.h:3 "// D3D12Context — Device + DXGI Factory + Direct/Compute/Copy Queue + Swap Chain."
그러나 src/device/d3d12_context.cpp:59-71 `CreateQueues()`는 D3D12_COMMAND_LIST_TYPE_DIRECT 큐 하나와 fence만 만든다. Compute/Copy 큐는 코드 어디에도 없다.

src/device/d3d12_context.h:5 "// Step 1 시점: Direct Queue 한 개 + BGRA8 RTV + Depth 24S8 DSV."
그러나 src/device/d3d12_context.h:79 `DXGI_FORMAT rtvFormat_ = DXGI_FORMAT_R8G8B8A8_UNORM;` (BGRA가 아니라 RGBA)
```

헤더 첫 줄 주석은 그 모듈이 무엇을 소유하는지 알리는 계약인데 두 항목이 사실과 다르다. 특히 Compute/Copy 큐 언급은 이론 문서 stage2_dx_fund_04 §5(세 가지 Queue 타입)와 stage2_dx_proj_04 §1(Command Queue × 3)이 다루는 주제여서, 구현되지 않은 것을 구현된 것처럼 읽히게 한다.

**방향**: 주석을 "Direct Queue 1개, RGBA8 RTV, D24S8 DSV"로 정정하라. 다중 큐를 실제로 다룰 계획이라면 별도 항목으로 분리해 명시하라.

---

## graphics_vk_lab — 보완 필요 (16건)

학습 대상 대체는 없다. instance/device/feature chain, swapchain 협상, dynamic rendering, descriptor set/UBO/SSBO/push constant, `vkCmdPipelineBarrier2` 배리어, `vkQueueSubmit2` + binary/timeline semaphore + fence, timestamp query, validation messenger가 모두 raw Vulkan으로 직접 작성되어 있고, 외부 의존성(GLFW·ImGui·ImPlot·glm·VMA·glslc)은 창 생성·UI·수학·메모리 suballocation 같은 비학습 대상에 한정된다 — VMA는 대응 이론 문서 stage3_vk_fund_02 §7이 스스로 권하는 도구이고 README도 명시한다. 반면 검증력은 이 lab의 결정적 약점이다. CTest 바이너리는 src의 13개 .cpp 중 barrier_helpers.cpp 한 개(80/1923줄)만 컴파일하며, 변이 실험으로 `b.oldLayout`/`b.newLayout`을 GENERAL로 고정하고, `b.image`를 VK_NULL_HANDLE로 바꾸고, subresourceRange를 0으로 만들고, 배리어를 기록하는 `dep.imageMemoryBarrierCount`를 0으로 낮춰도 세 변이 모두 `Vulkan contract tests passed` exit=0으로 생존했다 — 즉 "image layout 전이"라는 lab의 핵심 주장이 자동 검증 밖에 있다. 또한 테스트가 검사하는 `ExpectedDrawSubmissions`는 제품 코드가 한 번도 호출하지 않는 함수이고, Over-sync 정책은 렌더러가 실제로 만드는 두 전이가 아니라 앱이 절대 만들지 않는 layout 쌍으로만 검사되며, 바로 그 미검사 분기가 step03 문서 스니펫과 드리프트해 있다(문서 버전으로 코드를 바꿔도 테스트 통과). PROJECTS_PLAN이 요구한 "실패 주입"은 lab 전체에 0건이고, step 문서 7편 모두 실행 명령·관측 결과가 없으며(같은 저장소의 math/parts/languages/system lab은 있다), step01은 Step 3·4·5·6·7을 명시적으로 예고해 완료 기준 2를 위반한다. 대응 축 8편 중 컴퓨트(stage3_vk_proj_05) 전체가 lab에 없고, 그 결과 배리어 policy라는 조작 변수가 관측 가능한 차이를 만들 수 있는 producer/consumer가 워크로드에 하나도 없다.

### 1. [치명] 배리어 테스트가 layout·image·subresource를 전혀 검증하지 않는다 (변이 생존 확인)

<sub>분류: 검증력부족</sub>

**근거**

```text
tests/graphics_vk_contract_tests.cpp:26-49 는 srcStageMask/dstStageMask/srcAccessMask/dstAccessMask/src·dstQueueFamilyIndex 만 CHECK 한다. src/barrier/barrier_helpers.cpp:18-25 의 `b.image = image; b.oldLayout = oldLayout; b.newLayout = newLayout;` 와 subresourceRange 세 줄에 대한 단언은 파일 전체에 없다.

변이 실험(원본 미변경, 스크래치 복사본에서 동일 테스트 소스로 빌드):
- MUT1: `b.oldLayout = VK_IMAGE_LAYOUT_GENERAL; b.newLayout = VK_IMAGE_LAYOUT_GENERAL;` 로 치환 → `Vulkan contract tests passed`, exit=0
- MUT2: `b.image = VK_NULL_HANDLE;` + `aspectMask=0; levelCount=0; layerCount=0;` → `Vulkan contract tests passed`, exit=0
```

이 lab이 표방하는 학습 주제는 "image layout, stage/access mask"인데, 그중 image layout 전이 자체(oldLayout/newLayout)와 전이 대상(image, subresourceRange)이 자동 검증 밖에 있다. 어떤 layout을 넣어도 항상 GENERAL→GENERAL을 반환하도록 코드를 망가뜨려도 CTest는 green이다. 형제 lab인 graphics_dx_lab 의 tests/graphics_dx_contract_tests.cpp:55-60 은 같은 개념에서 `barrier.Transition.pResource == fake`, `StateBefore`, `StateAfter`, `Subresource` 를 모두 단언하므로, vk lab 쪽이 같은 축에서 명백히 약하다.

**방향**: BuildImageBarrier 반환값의 image/oldLayout/newLayout/subresourceRange(aspect·levelCount·layerCount)를 두 실제 전이(UNDEFINED→COLOR_ATTACHMENT_OPTIMAL, COLOR_ATTACHMENT_OPTIMAL→PRESENT_SRC_KHR) 각각에 대해 단언하고, 잘못된 layout 쌍을 넣었을 때의 fallback 동작도 계약으로 고정한다.

### 2. [높음] 실제 배리어를 기록하는 유일한 경로 ImageLayoutTransition 이 테스트에 전혀 없다

<sub>분류: 검증력부족</sub>

**근거**

```text
src/barrier/barrier_helpers.cpp:65-78 의 `ImageLayoutTransition` 은 `VkDependencyInfo` 를 채우고 `vkCmdPipelineBarrier2` 를 호출하는 유일한 함수이며, 제품 코드에서 src/app/app.cpp:438, src/app/app.cpp:492 두 곳에서만 호출된다. tests/ 전체를 검색해도 `ImageLayoutTransition` 호출은 0건이다(grep 결과: tests 파일에는 `BuildImageBarrier` 3회만 존재).

변이 실험: barrier_helpers.cpp:75 `dep.imageMemoryBarrierCount = 1;` → `= 0;` 으로 바꿔도 `Vulkan contract tests passed`, exit=0.
```

배리어를 하나도 기록하지 않도록 만들어도 자동 테스트가 통과한다. 즉 테스트가 검증하는 것은 순수 구조체 빌더뿐이고, 렌더 루프가 실제로 실행하는 기록 경로는 검증 대상이 아니다. mega 감사에서 나온 "테스트가 검증하는 코드와 실제 실행되는 코드가 다름" 패턴과 동일하다.

**방향**: vkCmdPipelineBarrier2 를 얇은 seam(함수 포인터/인터페이스)으로 감싸 headless 에서 기록된 VkDependencyInfo 를 캡처·단언하거나, 최소한 barrier 개수·필드가 BuildImageBarrier 결과와 일치하는지 검사하는 테스트를 둔다.

### 3. [높음] step03 Over-sync 스니펫이 실제 코드와 다르고, 그 분기는 테스트도 없다

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
articles/step03_barrier.md:48 `b.dstStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;` / :52 `b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;` (무조건).
실제 src/barrier/barrier_helpers.cpp:31-39 은 조건부다:
```
b.dstStageMask = newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    ? VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT
    : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
...
b.dstAccessMask = newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR ? 0 : ...
```
변이 실험: 코드를 step03 문서 버전(무조건 ALL_COMMANDS / MEMORY_READ|WRITE)으로 바꿔도 `Vulkan contract tests passed`, exit=0.
```

문서를 그대로 따라 쓰면 present 전이에 dstAccessMask 를 걸게 되어 실제 코드와 다른 배리어가 나온다. 그런데 테스트는 tests/graphics_vk_contract_tests.cpp:51-59 에서 Over-sync 를 TRANSFER_DST_OPTIMAL→SHADER_READ_ONLY_OPTIMAL 조합으로만 검사한다 — 이 layout 쌍은 렌더러가 한 번도 만들지 않는 조합이고, 반대로 앱이 실제 실행하는 두 전이의 Over-sync 결과는 단 한 줄도 단언되지 않는다. 완료 기준 7("최종 코드와 문서 코드가 드리프트하지 않도록 스니펫 검증을 둔다")에 해당하는 스니펫 검증 도구는 저장소 어디에도 없다(scripts/ 전체에 snippet 검사 없음, scripts/check_project_docs.py 는 구조·전방예고 표현만 검사).

**방향**: Over-sync 정책도 실제 두 전이(UNDEFINED→COLOR_ATTACHMENT_OPTIMAL, COLOR_ATTACHMENT_OPTIMAL→PRESENT_SRC_KHR)에 대해 단언하고, step03 스니펫을 조건부 코드 그대로 고치거나 스니펫을 소스에서 추출하도록 만든다.

### 4. [높음] PROJECTS_PLAN이 요구한 "실패 주입"이 lab 전체에 0건

<sub>분류: 검증력부족</sub>

**근거**

```text
PROJECTS_PLAN.md:26 `| 실패 주입 | 어떤 입력이 잘못된 구현을 드러내는가 |`. lab 전체(src/, tests/, articles/, README.md)에서 `inject|주입|broken|deliberate|일부러|고의` 검색 결과 0건. README.md:16-26 의 토글 표에도 잘못된 배리어/누락된 배리어/잘못된 semaphore 값 같은 실패 모드가 없다.
```

Over-sync vs Minimal 은 둘 다 올바른 배리어이므로 실패 주입이 아니라 비교 조건이다. 이 lab은 "validation이 조용한 것"(README.md:93)을 학습 기준으로 내세우지만, validation이 실제로 무언가를 잡는 것을 보여 주는 경로가 없다. 배리어를 빼거나 stage/access를 틀리게 하는 토글이 있으면 step02가 표로 정리한 오류 유형(articles/step02_measure.md:66-72)이 Validation log에 실제로 나타나는 것을 관측할 수 있는데 그런 장치가 없다.

**방향**: "barrier 생략", "잘못된 srcAccessMask", "image 재사용 시 완료 대기 생략" 같은 의도적 오류 토글을 추가하고, Debug 빌드에서 validation 메시지가 실제로 발생하는지를 관측 결과로 문서화한다.

### 5. [높음] 테스트가 검증하는 ExpectedDrawSubmissions 는 제품이 호출하지 않는 함수

<sub>분류: 검증력부족</sub>

**근거**

```text
src/scene/scene.h:29-32 에 `inline int ExpectedDrawSubmissions(DrawMode mode, int objectCount)` 정의. 저장소 검색 결과 호출처는 tests/graphics_vk_contract_tests.cpp:21-23 세 줄과 articles/step07_mesh_scale.md:171-172 뿐이며 src/ 안에서는 0건이다. 실제 제출 수는 src/scene/scene.cpp:129-163 의 `Scene::Draw` 가 독립적으로 결정하고 `counters.drawCalls` / `counters.instancedDraws` 를 따로 증가시킨다.
```

테스트는 `PerDraw→N, Instanced→1` 이라는 공식을 확인하지만, 렌더러가 그 공식을 지키는지는 확인하지 않는다. Scene::Draw 의 Instanced 분기를 N회 draw 로 바꿔도 자동 테스트는 통과한다(scene.cpp 는 테스트 타깃에 아예 링크되지 않는다). mega의 "잘 만든 모듈이 있는데 제품이 그걸 안 쓴다 / 항등식 테스트" 패턴과 같다.

**방향**: Scene::Draw 가 채운 GlCounters 와 ExpectedDrawSubmissions(mode, N) 을 비교하는 테스트를 두거나, Scene::Draw 가 ExpectedDrawSubmissions 를 실제 계약으로 사용하도록 만든다. 그러려면 command 기록을 fake VkCommandBuffer/recorder 로 분리해야 한다.

### 6. [높음] 테스트 타깃이 src의 13개 .cpp 중 1개만 컴파일한다

<sub>분류: 검증력부족</sub>

**근거**

```text
CMakeLists.txt:143-145
```
add_executable(graphics_vk_contract_tests
    tests/graphics_vk_contract_tests.cpp
    src/barrier/barrier_helpers.cpp)
```
src의 .cpp 총 1923줄 중 테스트에 들어가는 것은 barrier_helpers.cpp 80줄뿐이다. timeline_sync.cpp / scene.cpp / vk_swapchain.cpp / vk_context.cpp / vk_alloc.cpp / timestamp_query.cpp / pipeline_cache.cpp / frame_timer.cpp / validation_log.cpp 는 링크조차 되지 않는다.
```

테스트 파일이 `#include "sync/timeline_sync.h"` 와 `#include "scene/scene.h"` 를 하지만 링크가 되는 이유는 사용하는 심볼(`TimelineSemaphore::Next/Current`, `ExpectedDrawSubmissions`)이 모두 헤더 inline 이기 때문이다. 즉 `TimelineSemaphore::Init/Wait/Shutdown`, `SyncModeName`, `Scene::WriteInstances/ModelMatrix/BuildObjects`, swapchain format·present mode 선택 로직, timestamp ring 로직 등 lab의 나머지 학습 대상은 전부 자동 검증 밖이다. PROJECTS_PLAN.md:37 이 graphics lab에 요구하는 "자원 수명과 동기화 상태, 변환 결과" 중 실제로 검증되는 것은 없다.

**방향**: 디바이스가 필요 없는 순수 로직(timestamp ring 인덱싱, present mode fallback 선택, WriteInstances capacity clamp, ModelMatrix, frame/image 완료 추적 상태 머신)을 device 호출과 분리해 테스트 타깃에 넣는다.

### 7. [중간] step07이 "자동 테스트로 확인한다"고 나열한 6항목 중 4개가 테스트에 없다

<sub>분류: 문서대응누락</sub>

**근거**

```text
articles/step07_mesh_scale.md:169-176
```
다음 계약을 자동 테스트와 화면 카운터로 확인한다.
1. `ExpectedDrawSubmissions(PerDraw, N) == N`
2. `ExpectedDrawSubmissions(Instanced, N) == 1`
3. object count가 1k·10k·100k에서 정확히 반영됨
4. Instanced 모드의 SSBO 쓰기가 capacity를 넘지 않음
5. 두 모드가 같은 camera에서 같은 배치와 색을 그림
6. validation error가 발생하지 않음
```
자동 테스트(tests/graphics_vk_contract_tests.cpp)에 존재하는 것은 1·2뿐이다. 3·4를 담당하는 `Scene::BuildObjects`(scene.cpp:66-94)와 `Scene::WriteInstances`(scene.cpp:119-127)는 CMakeLists.txt:143-145 에 의해 테스트 바이너리에 포함조차 되지 않는다.
```

3·4는 화면 카운터로도 확인되지 않는다. src/ui/panel.cpp:65 는 `Active objects` 만 표시하고 SSBO 쓰기 개수나 capacity 초과 여부는 표시하지 않는다. 문서가 "자동 검증한다"고 적은 항목이 실제 테스트에 없는 전형적 사례다.

**방향**: 3·4는 순수 함수 테스트로 즉시 추가 가능하다(`BuildObjects(100000)` 후 ObjectCount()==100000, `WriteInstances(buf, 10)` 이 10을 넘지 않음). 5·6은 자동 검증이 불가하면 "수동 관측"으로 문구를 분리한다.

### 8. [중간] step 문서 7편 전부에 실행 명령과 관측 결과가 없다 (완료 기준 5 미충족)

<sub>분류: 문서대응누락</sub>

**근거**

```text
PROJECTS_PLAN.md:48 `5. step 종료 시 실행 명령과 관측 가능한 결과가 있다.`
args/articles 전체에서 `cmake --build|ctest|.exe|powershell|bash` 검색 결과 0건 (articles/step01~step07 모두). 반면 같은 저장소의 math_lab 8편 전부, parts_perf_lab 6편, languages_lab 6편, system_kernel_lab 7편에는 실행 명령 블록이 있다(projects/mini 전체 34개 step 문서가 매칭, graphics_vk_lab 은 0개).
```

README.md:56-66 에만 빌드 명령이 있고 step 문서에는 없다. 그래서 각 step의 "종료 상태"를 독자가 객관적으로 확인할 방법이 문서 안에 없다. 완료 기준 1(시작·종료 상태를 객관적으로 적는다)도 함께 약해진다 — 7편 모두 토픽 설명으로 시작해 개념 정리로 끝나며, "이 step 이후 무엇이 실행되고 무엇이 보이는가"를 적은 문장이 없다.

**방향**: 각 step 말미에 해당 상태의 빌드·실행 명령과 관측 항목(예: 패널의 barriers 카운터가 2, Timeline value 가 프레임마다 1씩 증가)을 추가한다.

### 9. [중간] step01이 Step 3·4·5·6·7의 내용을 명시적으로 예고한다 (완료 기준 2 위반)

> **[반영됨 · 잔여 있음 · 2026-08-13]** step01이 범위·한계 서술로 재작성되어 7편 전체에서 다른 step 번호 예고가 0건이 됐다. **잔여**: 린터 정규식에 `Step\s*\d` 패턴은 추가되지 않았다(math #13, physics #9와 같은 잔여).

<sub>분류: 문서대응누락</sub>

**근거**

```text
PROJECTS_PLAN.md:45 `2. 후속 step을 예고하지 않는다.`
articles/step01_skeleton.md:30 "Step 1~3 과 Step 5 는 *실제 동작·측정* 으로 구현하고, Step 4·6·7 의 multi-queue / bindless / mesh shader 는 *언제 필요한가* 를 판단하는 설계 토픽으로 분리한다."
같은 파일 :40-42, :73 "`synchronization2` 없이는 Step 3 의 barrier 설명이 낡은 API가 된다", :75 "`descriptorIndexing` 과 `runtimeDescriptorArray` 는 Step 6 의 bindless 설계 조건으로만 다룬다", :5 "이후 단계의 barrier·timestamp·sync 검증이 올라설 수 있는".
```

저장소 자체 린터 scripts/check_project_docs.py 의 FORWARD_PROMISE_RE 는 한글 표현("다음 단계", "후속…다룬다")만 잡고 영문 "Step N" 표기를 잡지 못해 이 위반이 통과한다(`python scripts/check_project_docs.py` → 0 error). 다른 step 문서에는 전방 예고가 없어 step01만의 문제다.

**방향**: step01은 현재 step의 범위·한계로만 서술하고 다른 step 번호를 지우거나, 린터 정규식에 `Step\s*\d` 패턴을 추가한다.

### 10. [중간] step05 semaphore 스니펫이 sync1 VkSubmitInfo인데 코드는 전부 VkSubmitInfo2

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
articles/step05_semaphore.md:23-33 은 `submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &imageAvailable; submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &renderFinished;` 형태의 Vulkan 1.0 `VkSubmitInfo` 를 보여 준다.
실제 코드 src/app/app.cpp:596-621 은 `VkSemaphoreSubmitInfo waitInfo` / `VkSemaphoreSubmitInfo signalInfos[2]` / `VkSubmitInfo2 submit` 이며 제출은 app.cpp:624 `vkQueueSubmit2(...)` 다. lab 전체에 `vkQueueSubmit(` (2 없는 형태) 호출은 없다.
```

step01_skeleton.md:73 은 "`synchronization2` 없이는 Step 3 의 barrier 설명이 낡은 API가 된다"고 못 박았는데, 정작 semaphore를 다루는 step05 본문의 제출 스니펫만 낡은 API다. 문서 코드만 따라 쓰면 이 lab의 실제 제출 경로(2개 signal, timeline value 포함)를 재현할 수 없다.

**방향**: step05 §1 스니펫을 VkSubmitInfo2/VkSemaphoreSubmitInfo 형태로 바꾸고, Timeline 모드에서 signal 이 2개가 되는 실제 구조(app.cpp:601-612)를 보여 준다.

### 11. [중간] 제품이 barrier 모듈을 우회해 depth 전이를 직접 조립한다

<sub>분류: 검증력부족</sub>

**근거**

```text
src/app/app.cpp:148-169 에서 `std::vector<VkImageMemoryBarrier2> bs(n);` 를 만들어 sType/oldLayout/newLayout/stage/access/subresourceRange 를 손으로 채우고 `vkCmdPipelineBarrier2(cmd, &dep)` 를 직접 호출한다. `BuildImageBarrier` / `ImageLayoutTransition` 는 쓰지 않는다(호출처는 app.cpp:438, 492 두 곳뿐).
```

lab이 배리어 학습용으로 만든 모듈이 있는데 세 번째 layout 전이(UNDEFINED→DEPTH_ATTACHMENT_OPTIMAL)는 그 모듈을 통과하지 않는다. 결과적으로 (a) 이 전이는 Barrier policy 토글(조작 변수)의 영향을 전혀 받지 않고, (b) 유일하게 테스트되는 코드 경로 밖에 있으며, (c) counters_.barrierCalls 에도 잡히지 않는다. 근본 원인은 BuildImageBarrier 의 Minimal 분기가 두 개의 swapchain layout 쌍만 하드코딩하고 있어(barrier_helpers.cpp:42-59) depth 전이를 표현할 수 없다는 점이다.

**방향**: BuildImageBarrier 의 Minimal 테이블에 depth attachment 전이를 추가하고 app.cpp:148-169 를 모듈 호출로 교체한다. 그러면 depth 전이도 policy 토글과 테스트의 적용 범위에 들어온다.

### 12. [중간] "Minimal" 정책은 2개 layout 쌍 밖에서는 Over-sync와 완전히 동일한데 문서는 이를 밝히지 않는다

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
src/barrier/barrier_helpers.cpp:54-59 (Minimal 의 else 분기)
```
b.srcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
b.dstStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
b.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
```
articles/step03_barrier.md:8 은 Minimal 을 "실제 전이에 필요한 stage/access 만 지정", :85 "정확히 넣어야 빠르고 안전" 이라고만 서술한다. README.md:20 도 "같은 layout 전이의 broad vs minimal stage/access mask" 로만 적는다.
```

Minimal 은 사실상 2-엔트리 lookup table 이고 그 밖의 모든 layout 조합에서는 Over-sync 와 비트 단위로 같은 값을 낸다. 테스트도 이 사실을 드러내지 않는다 — tests/graphics_vk_contract_tests.cpp:51-59 는 TRANSFER_DST→SHADER_READ_ONLY 를 Over-sync 로만 검사하고, 같은 쌍에 Minimal 을 넣으면 동일한 결과가 나온다는 점은 어디에도 기록되지 않았다. "정확한 mask 를 쓰는 습관"을 목표로 내건 lab에서, 학습자가 새 전이를 추가하면 조용히 over-sync 로 떨어진다.

**방향**: else 분기를 명시적 미지원(assert 또는 진단 로그)으로 바꾸거나, 최소한 step03에 "현재 Minimal 이 정확한 mask 를 아는 전이는 두 가지뿐이고 나머지는 보수적 fallback" 이라고 적고 그 계약을 테스트로 고정한다.

### 13. [중간] 대응 축의 컴퓨트(8편 중 1편 전체)가 lab에 전혀 없고, 준비된 인프라만 남아 있다

<sub>분류: 문서대응누락</sub>

**근거**

```text
graphics/README.md:69 는 stage3 프로젝트 6편에 `[05 컴퓨트](stage3_vk_proj_05_compute.md)` 를 포함한다. graphics/stage3_vk_proj_05_compute.md 는 compute pipeline, workgroup/local size, storage buffer 레이아웃, compute→graphics 배리어, indirect draw, atomic, subgroup 을 다룬다.
lab: `find . -name "*.comp" -not -path "./build/*"` → 0건, shaders/ 에는 cube.vert / lambert.frag / blinn.frag 만 있다. `dispatch|vkCmdDispatch` 검색 결과 src/ tests/ shaders/ 에 0건.
동시에 CMakeLists.txt:85 는 `${SHADER_DIR}/*.comp` 를 glob 하고, src/device/vk_context.cpp:132-140, 210-211 은 dedicated compute/transfer queue family 를 고르고 `computeQueue_`/`transferQueue_` 를 생성한다 — 이 두 큐는 src/ 어디에서도 사용되지 않는다(접근자 선언 vk_context.h:27-31 외 사용처 0건).
```

README.md:24 는 multi-queue 를 "설계 문서" 로 정직하게 표시하지만, 컴퓨트는 README·step 문서 어디에도 항목으로 등장하지 않는다. 즉 축의 8편 중 1편은 설계 토픽으로도 다뤄지지 않는다. 또한 이 lab의 유일한 배리어 실험 대상이 swapchain color 전이 두 개뿐이라, stage/access mask 가 실제로 문제가 되는 producer/consumer(compute→graphics, transfer→graphics)가 워크로드에 하나도 없다. step03_barrier.md:91-97 스스로 "단일 color pass = 차이 작음" 이라고 적고 있으므로, 조작 변수(Barrier policy)의 기대 결과를 이 lab 안에서 관측할 방법이 구조적으로 없다.

**방향**: compute dispatch 하나(예: 인스턴스 transform 을 compute 로 계산해 SSBO에 쓰기)를 추가하면 compute→vertex 의 buffer barrier 가 생기고, 그때 비로소 over-sync vs minimal 이 관측 가능한 차이를 만든다. 추가하지 않을 경우 README에 컴퓨트를 "설계 문서" 또는 "범위 밖"으로 명시한다.

### 14. [낮음] VERIFY.md가 graphics lab의 자동 검증에 "리소스 수명 정책"을 포함시키지만 vk lab에는 없다

<sub>분류: 문서오류</sub>

**근거**

```text
projects/mini/VERIFY.md:60 `- graphics_*_lab: draw 제출 수, 리소스 수명 정책, barrier·mip 계산`.
graphics_dx_lab/tests/graphics_dx_contract_tests.cpp:27-47 에는 `TestDescriptorLifetimePlan` 이 있고 transient/persistent descriptor 재사용 계약을 검사한다. graphics_vk_lab 의 테스트 3개(TestDrawSubmissions / TestMinimalBarriers / TestOverSyncAndTimelineCounter)에는 자원 수명을 다루는 단언이 없다 — 프레임 슬롯·swapchain image 완료 추적(app.cpp:554-575)이 vk lab의 수명 정책인데 이 로직은 app.cpp 안에 있어 테스트 타깃에 포함되지도 않는다.
```

`graphics_*_lab` 로 세 lab을 묶어 서술하면서 실제로는 dx/gl 에만 해당하는 항목이 vk 에도 있는 것처럼 읽힌다. mini/README.md:35 는 "동기화 상태 테스트와 validation smoke 보강 필요" 라고 정직하게 적고 있어 두 문서가 서로 다른 인상을 준다.

**방향**: VERIFY.md:60 을 lab별로 분리해 적거나, vk lab에 frame/image 완료 추적 상태 머신을 순수 로직으로 분리한 테스트를 추가한다.

### 15. [낮음] TimelineSemaphore 테스트가 Vulkan 객체가 아니라 정수 카운터만 검사한다

<sub>분류: 검증력부족</sub>

**근거**

```text
tests/graphics_vk_contract_tests.cpp:61-65
```
vklab::TimelineSemaphore timeline;
CHECK(timeline.Current() == 0);
CHECK(timeline.Next() == 1);
CHECK(timeline.Next() == 2);
CHECK(timeline.Current() == 2);
```
검사 대상은 src/sync/timeline_sync.h:28-29 의 `uint64_t Next() { return ++value_; }` / `Current()` 두 inline 접근자다. timeline_sync.cpp(Init/Wait/Shutdown)는 CMakeLists.txt:143-145 에 의해 테스트 바이너리에 링크되지 않는다.
```

`++value_` 가 1씩 증가한다는 것만 확인하는 자명한 테스트다. step05가 주장하는 실제 계약 — 값이 단조 증가한다, submit의 signal 값과 slot/image 별 마지막 값이 일치한다, 모드 전환 시 두 완료 추적 체계가 섞이지 않는다(app.cpp:542-548) — 은 어느 것도 검증되지 않는다.

**방향**: frame slot/ image 별 완료 값 갱신·대기 판정을 App에서 분리한 순수 상태 클래스로 만들고, 모드 전환·swapchain 재생성 시 값이 올바르게 초기화되는지 테스트한다.

### 16. [낮음] stage3 이론의 메모리 관리 절이 VMA로 전량 대체되어 lab에서 실습되지 않는다

<sub>분류: 문서대응누락</sub>

**근거**

```text
graphics/stage3_vk_fund_02_vulkan_core.md:422-472 (7. Memory Management)은 `vkGetBufferMemoryRequirements`, `FindMemoryType`, `vkAllocateMemory`, `vkBindBufferMemory`, memory property/heap 선택, `maxMemoryAllocationCount` 제약을 다룬다. lab의 src/device/vk_alloc.cpp 는 전부 `vmaCreateBuffer` / `vmaCreateImage` / `vmaFlushAllocation` 으로 처리하며 위 API 호출은 0건이다.
```

판정: 이것은 실격 사유가 아니다. 같은 이론 문서 :472 가 "Vulkan Memory Allocator(VMA) 같은 라이브러리가 이 일을 표준화해 제공한다"고 직접 권하고 있고, lab의 README.md:48 도 VMA를 기술 스택으로 정직하게 명시한다. 또 staging→device-local 복사와 그 뒤의 buffer barrier(vk_alloc.cpp:60-89)는 손으로 작성되어 있어 개념 자체는 남아 있다. 다만 축의 한 절이 lab에서 전혀 실습되지 않는다는 사실은 기록해 둔다.

**방향**: 현 상태 유지로 충분하다. 굳이 보완한다면 step 문서에 "메모리 타입 선택과 suballocation 은 VMA에 위임했고 이 lab의 학습 대상이 아니다"라고 한 줄 명시한다.

---

## game_network_lab — 보완 필요 (11건)

학습 대상 대체는 전혀 없다. `src/`·`tests/`의 전체 `#include` 목록은 C++ 표준 라이브러리와 raw OS 헤더(`winsock2.h`, `mswsock.h`, `sys/epoll.h`, `sys/eventfd.h`)뿐이고 CMake 링크 대상도 `ws2_32`/`mswsock`/`pthread`뿐이다. framing, IOCP operation 소유권, epoll readiness drain, endpoint binding, 고정 tick이 모두 직접 구현돼 있고 테스트 프레임워크조차 쓰지 않았다. 반면 검증력은 선언한 기준에 크게 못 미친다. 샌드박스 복사본에서 변이 테스트 9종을 돌린 결과, IOCP의 WSAEMSGSIZE 재게시 제거(M10) 하나만 잡히고 권위 시뮬레이션 무력화·snapshot 주기 10배 변경·UDP endpoint 일치 검사 제거·token 검사 제거·중복 입력 수용·ACK 상수화·reserved 필드 검사 제거·frame buffer 상한 제거·magic/version 검사 제거가 전부 `100% tests passed`로 생존했다. `GameServerRuntime`은 `server_main.cpp`에서만 인스턴스화되며 `IServerBackend`라는 주입 가능한 seam이 있는데도 fake backend 기반 단위 테스트가 하나도 없다. 그 결과 PROJECTS_PLAN이 이 lab에 요구한 4항목 중 "UDP endpoint binding"과 "권위 tick과 snapshot"이 자동 검증되지 않는다. 추가로 step 문서 스니펫 4곳이 최종 코드와 드리프트하고(`NetworkEventType`/`bytes`, `std::array<std::byte,4096> buffer`, `CHECK` 루프, `--ticks 120`), step03이 서술한 WSASendTo 길이 검사는 구현에 존재하지 않는다.

### 1. [치명] 권위 runtime 상태 기계에 자동 테스트가 0개 — 변이 6종이 전부 생존

<sub>분류: 검증력부족</sub>

**근거**

```text
`GameServerRuntime`을 lab 전체에서 검색하면 `src/server_main.cpp:58`의 `gnl::GameServerRuntime runtime(*backend);` 한 곳에서만 생성된다. `tests/protocol_tests.cpp`와 `tests/process_tests.cpp` 어디에도 등장하지 않고, `IServerBackend`를 구현한 fake backend도 저장소에 없다(`grep -rn "IServerBackend"` 결과는 `network_types.h`, `iocp_server.cpp`, `epoll_server.cpp`, `server_runtime.*`뿐).

샌드박스 복사본(scratchpad/gnl)에서 원본은 건드리지 않고 변이 후 `cmake --build ... --config Release && ctest -C Release` 를 돌린 결과:
- M5 `src/server_runtime.cpp:177-178` `client.positionX += client.moveX;` → `+= 0;` (권위 시뮬레이션 완전 무력화) → `M5_dead_simulation: rc=0  100% tests passed out of 2`
- M6 `src/server_runtime.cpp:10` `kSnapshotEveryTicks = 3;` → `= 30;` (20Hz→2Hz) → `M6_snapshot_rate_2hz: rc=0  100% tests passed out of 2`
- M1 `src/server_runtime.cpp:143` `if (!client.udpBound || !(client.endpoint == endpoint)) {` → `if (!client.udpBound) {` (UDP endpoint 결합 검사 제거) → `M1_no_endpoint_check: rc=0  100% tests passed out of 2`
- M2 `src/server_runtime.cpp:131` `if (!TokenEquals(message->payload, client.token)) {` → `if (false) {` → `M2_no_token_check: rc=0  100% tests passed out of 2`
- M3 `src/server_runtime.cpp:149` `if (client.hasInput &&` → `if (false &&` (오래된·중복 input 수용) → `M3_no_stale_input: rc=0  100% tests passed out of 2`
- M4 `src/server_runtime.cpp:188` `AppendU32(payload, client.lastInputSequence);` → `AppendU32(payload, 0u);` (snapshot ACK 상수화) → `M4_ack_always_zero: rc=0  100% tests passed out of 2`

기준: `PROJECTS_PLAN.md:38` "`game_network_lab`: 실제 TCP framing, IOCP/epoll 수명, UDP endpoint binding, 권위 tick과 snapshot", `PROJECTS_PLAN.md:29` "빌드 성공은 완료 조건이 아니다."
`articles/step06_verification.md:7` "검증은 순수 protocol, runtime 상태 기계, 플랫폼 backend, 실제 두 프로세스의 경계를 분리한다."
```

이 lab이 요구받은 4개 자동 검증 항목 중 "UDP endpoint binding"과 "권위 tick과 snapshot" 두 개가 실질적으로 검증되지 않는다. process smoke test는 정직한 클라이언트 한 명만 띄우므로, 서버가 token을 검사하지 않아도·다른 endpoint에서 온 input을 받아들여도·중복 sequence를 재적용해도·아예 시뮬레이션을 하지 않아도 클라이언트는 여전히 snapshot을 받고 exit 0을 반환한다.

특히 M5가 통과하는 이유가 구조적이다. `src/client_main.cpp:306`은 `return snapshots > 0 ? 0 : 2;` 로 snapshot 수신 여부만 본다. 권위 위치가 항상 (0,0)이어도 snapshot은 오고, README:104 "correction이 항상 0일 필요는 없다"는 설명 때문에 correction 값 자체도 판정 기준으로 쓰이지 않는다. 즉 권위 simulation 출력은 관측만 되고 assert되는 곳이 없다.

step06이 "runtime 상태 기계"를 독립 검증 계층으로 선언했지만 그 계층의 테스트 바이너리는 존재하지 않는다. `IServerBackend`가 순수 가상 인터페이스이고 `GameServerRuntime`이 그것만 참조하므로(`server_runtime.h:55` `IServerBackend& backend_;`), fake backend 주입 비용은 매우 낮은데도 활용되지 않았다.

**방향**: `tests/runtime_tests.cpp`를 추가하고 `SendStream`/`SendDatagram`/`Disconnect` 호출을 기록하는 fake `IServerBackend`를 주입해 결정적으로 검증한다: (1) Hello→Welcome payload가 24바이트이고 id+token 구조인지, (2) 잘못된 token의 Connect가 `udpBound`를 세우지 않고 `rejectedPackets`를 증가시키는지, (3) 결합된 endpoint와 다른 endpoint에서 온 Input이 거부되는지, (4) 동일·역행 sequence Input이 `moveX/moveY`를 바꾸지 않는지, (5) 고정 시각을 `Advance()`에 주입해 N tick 뒤 positionX == N*moveX 인지와 tick 3회마다 정확히 1개의 Snapshot이 나오는지, (6) 4tick catch-up 상한이 지켜지는지. 위 M1~M6 변이가 모두 실패하는지로 테스트의 검출력을 확인한다.

### 2. [높음] protocol decoder의 거부 경로 3종이 미검증 — 문서는 검증한다고 서술

<sub>분류: 검증력부족</sub>

**근거**

```text
변이 결과(같은 샌드박스, 같은 명령):
- M7 `src/protocol.cpp:178` 의 `*reserved != 0 ||` 삭제 → `M7_no_reserved_check: rc=0  100% tests passed out of 2`
- M8 `src/protocol.cpp:94` `if (bytes.size() > kMaxBufferedControlBytes - BufferedBytes()) {` → `if (false) {` (frame 누적 상한 제거) → `M8_no_frame_buffer_cap: rc=0  100% tests passed out of 2`
- M9 `src/protocol.cpp:114` `if (magic != kProtocolMagic || version != kProtocolVersion ||` → `if (false ||` → `M9_no_magic_version_check: rc=0  100% tests passed out of 2`

문서 주장:
`articles/step06_verification.md:36` "UDP는 마지막 byte 제거, payload 길이 위조, reserved field 변경을 각각 거부해야 한다."
`articles/step01_protocol.md:68` "magic, version, 길이가 불법이다: 연결을 복구하려 하지 않고 decoder를 실패 상태로 만든다."
`articles/step01_protocol.md:58` "공격자가 길이 필드에 4GiB를 적더라도 디코더가 그만큼 할당하거나 기다리지 않는다."

실제 `tests/protocol_tests.cpp` 전체(63줄)에서 손상 입력은 `datagram.pop_back();`(54행) 하나뿐이다. reserved 변조, 선언 길이 위조, 잘못된 magic/version, 누적 상한 초과가 전부 없다.
```

step06은 UDP 거부 사례 3종(마지막 byte 제거 / payload 길이 위조 / reserved 변경)을 "각각" 거부한다고 적었지만 실제로는 1종만 있다. 게다가 `pop_back()`은 `bytes.size() != kDatagramHeaderBytes + *payloadBytes` 한 가지 조건만 건드리므로 "길이 위조"와 사실상 같은 경로다.

step06:39-40이 "정상 round trip만 테스트하면 encoder와 decoder가 같은 잘못을 공유해도 통과할 수 있다. 그래서 알려진 byte 배열과 손상 입력을 함께 사용해야 한다"고 스스로 규칙을 세웠는데, 현재 테스트에 고정 byte 배열(golden vector)은 없고 전부 `EncodeControl`/`EncodeDatagram`이 만든 것을 다시 디코드하는 encoder-decoder 왕복이다. magic 상수나 필드 순서를 encoder와 decoder에서 동시에 바꾸면 M9처럼 통과한다. 즉 step06이 경고한 바로 그 실패 양식에 현재 테스트가 해당한다.

**방향**: (1) 하드코딩된 golden byte 배열로 `EncodeControl(Hello, {1,2,3,4,5})`의 12바이트 헤더 전 바이트를 검사한다(magic 0x47 0x4E 0x4C 0x31, version, type, big-endian 길이). (2) reserved 바이트를 1로 바꾼 datagram, 선언 payload 길이만 +1 한 datagram, magic 1바이트를 틀린 frame, version을 2로 바꾼 frame이 각각 거부/`Failed()`가 되는지 확인한다. (3) `kMaxControlPayload + kFrameHeaderBytes + 1` 바이트를 Push했을 때 `Push`가 false를 반환하고 `Failed()`가 true가 되는지 확인한다.

### 3. [중간] PROJECTS_PLAN이 요구한 추적 단위 7항목 중 README에 존재하는 것은 사실상 '이론 주장'뿐

<sub>분류: 문서대응누락</sub>

**근거**

```text
`PROJECTS_PLAN.md:19-27` 표는 각 lab이 이론 주장 / 조작 변수 / 통제 변수 / 관측값 / 기대 결과 / 실패 주입 / 자동 테스트를 갖도록 요구한다.

`README.md:9-19`의 "검증하는 계약" 표는 열이 `| 계약 | 구현과 관측 |` 두 개뿐이고 내용은 전부 구현 서술이다. 예: `| 느린 receiver를 무한 queue로 감추지 않는다 | 연결별 queued send byte에 상한을 둔다. |`. 조작 변수·통제 변수·기대 결과 범위·실패 주입 항목이 README와 articles 어디에도 표로 존재하지 않는다.

실패 주입은 저장소 전체에서 `tests/process_tests.cpp:64-83` `SendOversizedDatagrams` 한 종류(4KiB UDP 64개)뿐이다. `--workers` 옵션(`src/server_main.cpp:39-40`)이 조작 변수 후보인데 어떤 테스트도 이 값을 바꿔 비교하지 않는다.
```

README 표는 8개 계약을 나열하지만 그중 자동 검증이 실제로 존재하는 것은 1행("TCP는 byte stream이다")과, 표에 없는 WSAEMSGSIZE 재게시뿐이다. 나머지 6행(buffer 재사용 금지, close와 session 해제 분리, bounded send queue, TCP 신원과 UDP endpoint 분리, simulation을 worker에서 실행 안 함, sequence wrap 적용)은 구현 서술만 있고 관측·기대 결과·실패 주입이 정의돼 있지 않다.

특히 "입력 순서는 wrap될 수 있다" 행은 `IsNewerSequence` 단위 테스트가 있어 검증된 것처럼 보이지만, 그 함수를 실제로 쓰는 `GameServerRuntime::OnDatagram`의 적용(`server_runtime.cpp:149-150`)은 M3에서 확인했듯 무력화해도 통과한다. 즉 순수 함수는 검증되고 그 함수의 사용처는 검증되지 않는 형태다.

**방향**: README에 추적 단위 7열 표를 명시하고, 각 계약마다 조작 변수(예: `--workers` 1 vs N, prepostedDatagrams 값, 인위적 send 지연)와 기대 결과 범위(예: tick N회 뒤 position, snapshot/tick 비율 1/3 ± 경계 오차)를 적는다. 검증 수단이 없는 행은 "구현만 있음"으로 명시 구분해 표가 검증 주장으로 읽히지 않게 한다.

### 4. [중간] IOCP 종료 drain에 lost wakeup 경합 — 문서가 강조한 종료 불변식 지점

<sub>분류: 코드오류</sub>

**근거**

```text
`src/iocp_server.cpp:232-235`
```cpp
if (started_) {
    std::unique_lock lock(outstandingMutex_);
    outstandingCv_.wait(lock, [this] { return outstanding_.load() == 0; });
```
`src/iocp_server.cpp:620-624`
```cpp
void AddOutstanding() { outstanding_.fetch_add(1); }

void RemoveOutstanding() {
    if (outstanding_.fetch_sub(1) == 1) outstandingCv_.notify_all();
}
```
`grep -n outstandingMutex_ src/iocp_server.cpp` 결과는 233행(wait)과 646행(선언) 두 줄뿐이다. 즉 카운터를 감소시키고 notify하는 쪽이 `outstandingMutex_`를 한 번도 잡지 않는다.
```

`outstanding_`는 atomic이지만 mutex 밖에서 변경·통지된다. Stop() 스레드가 predicate를 평가해 false를 얻은 뒤 실제 `wait()`로 진입해 대기자로 등록되기 전에, worker 스레드가 마지막 `fetch_sub`로 0을 만들고 `notify_all()`을 호출하면 그 통지는 유실된다. 이후 `outstanding_`는 다시 증가하지 않는다 — `PostAccept`(iocp_server.cpp:328)와 datagram 재게시(iocp_server.cpp:572)가 모두 `stopping_.load()` 가드 뒤에 있고 Stop()이 이미 true로 만들었기 때문이다. 따라서 Stop()은 영구 대기하고 서버 프로세스가 종료되지 않는다.

이 지점은 `articles/step02_runtime.md:119-130`의 "종료 불변식" 5단계와 `articles/step03_iocp.md:110-124`의 "안전한 종료" 전체가 자랑하는 바로 그 코드다. step03:116-121 스니펫도 동일하게 mutex 밖 감소를 전제로 적혀 있어 문서와 코드가 같은 결함을 공유한다.

확률은 낮지만 무해하지 않다. 발생 시 `tests/process_tests.cpp:168` `WaitFor(serverProcess, 7000)`이 timeout → `TerminateProcess` → `code=99` → 테스트 실패로 나타나므로 재현이 어려운 간헐적 CI 실패가 된다. step06:79 "도구가 process를 강제 종료한 결과는 성공으로 세지 않는다"는 원칙 덕에 은폐되지는 않는다.

**방향**: `RemoveOutstanding()`에서 0에 도달했을 때 `{ std::lock_guard lock(outstandingMutex_); }` 로 mutex를 한 번 획득했다 놓은 뒤 `notify_all()`을 호출하거나, `outstanding_`를 atomic이 아닌 `outstandingMutex_` 보호 변수로 바꾼다. 그리고 step03의 스니펫도 함께 고친다.

### 5. [중간] step06 문서의 automated smoke 성공 조건 수치가 실제 테스트와 다름 (120 vs 60)

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
`articles/step06_verification.md:48-55`
```
성공 조건은 단순 connect가 아니다.

- TCP Hello와 Welcome을 완성한다.
- Welcome token으로 UDP endpoint를 결합한다.
- 120개의 input tick을 전송한다.
- snapshot을 하나 이상 decode한다.
```
실제 `tests/process_tests.cpp:163-165`
```cpp
const std::string clientCommand = "\"" + client + "\" --tcp-port " +
    std::to_string(tcpPort) + " --udp-port " + std::to_string(udpPort) +
    " --ticks 60";
```
동일하게 Linux 경로 `tests/process_tests.cpp:171-172`도 `"--ticks", "60"`.
```

step06의 이 목록은 "자동 process test"의 성공 조건을 설명하는 문맥(같은 절 57행부터 자동 process test 서술이 이어진다)인데, 자동 테스트는 60 tick을 보낸다. 120은 README:129와 step06:120의 수동 관측 명령(`--ticks 120`)의 값이다. 두 절차의 수치가 문서 한 곳에서 섞여 있어, 독자가 자동 테스트의 커버리지를 실제의 두 배로 읽게 된다.

mega 감사에서 나온 "문서 간 수치 불일치"와 같은 유형이다.

**방향**: step06의 성공 조건 목록을 자동 테스트 값(60 tick)으로 고치거나, 자동 테스트와 수동 관측 절차를 두 목록으로 분리해 각각의 tick 수를 명시한다.

### 6. [중간] step02의 NetworkEvent 스니펫이 최종 코드와 타입명·필드명 모두 다름

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
`articles/step02_runtime.md:29-37`
```cpp
struct NetworkEvent {
    NetworkEventType type{};
    ConnectionId connection = 0;
    Endpoint endpoint{};
    std::vector<std::byte> bytes;
    int error = 0;
};
```
최종 코드 `src/network_types.h:30-36`
```cpp
struct NetworkEvent {
    NetworkEventKind kind{};
    ConnectionId connection = 0;
    Endpoint endpoint{};
    std::vector<std::byte> payload;
    int error = 0;
};
```
`NetworkEventType`이라는 이름은 저장소 소스 어디에도 없다(`grep -rn "NetworkEventType" src tests` 무결과).
```

열거형 타입명(`NetworkEventType` vs `NetworkEventKind`), 멤버명(`type` vs `kind`), payload 멤버명(`bytes` vs `payload`) 세 가지가 모두 다르다. `src/server_runtime.cpp:39-51`은 `event.kind`와 `event.payload`를 쓰므로 문서 스니펫을 그대로 옮기면 컴파일되지 않는다.

`PROJECTS_PLAN.md:47` "문서의 코드만 따라도 해당 상태를 빌드할 수 있다", `PROJECTS_PLAN.md:50` "최종 코드와 문서 코드가 드리프트하지 않도록 스니펫 검증을 둔다" 두 항목에 동시에 걸린다. 그리고 `scripts/check_project_docs.py`는 구조(들어가며/목차/결론, 코드펜스 언어, 후속 예고 금지)만 검사하고 스니펫 내용을 소스와 대조하지 않는다 — 즉 요구된 스니펫 검증 장치가 저장소에 존재하지 않는다.

**방향**: 스니펫을 `kind`/`payload`/`NetworkEventKind`로 수정하고, `scripts/`에 articles의 cpp 코드펜스가 `projects/mini/*/src/` 파일의 부분 문자열인지 확인하는 검사를 추가한다(부분 인용은 앵커 주석이나 라인 범위 지정으로 허용).

### 7. [중간] step03의 IoOperation 스니펫 buffer 타입이 실제와 다르고 step02의 '고정 buffer' 서술과도 어긋남

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
`articles/step03_iocp.md:39-47`
```cpp
struct IoOperation {
    OVERLAPPED overlapped{};
    OperationKind kind{};
    std::shared_ptr<Session> session;
    std::array<std::byte, 4096> buffer{};
    WSABUF wsabuf{};
};
```
최종 코드 `src/iocp_server.cpp:50-62`
```cpp
struct IoOperation {
    // IOCP returns the same pointer passed to Winsock. Keep this first.
    OVERLAPPED overlapped{};
    OperationKind kind{};
    std::shared_ptr<Session> session;
    SOCKET acceptedSocket = INVALID_SOCKET;
    std::vector<std::byte> buffer;
    WSABUF wsabuf{};
    sockaddr_storage remote{};
    int remoteLength = sizeof(sockaddr_storage);
    DWORD flags = 0;
    std::size_t offset = 0;
};
```
`articles/step02_runtime.md:99-101` "stack buffer 주소를 overlapped operation에 저장하면 함수 반환 뒤 use after free가 된다. operation 객체 안의 고정 buffer를 제출하고 completion handler에서만 읽는다."
```

실제 buffer는 `std::vector<std::byte>`이고 크기는 용도별로 다르다: `kReceiveBytes = 16 * 1024`(iocp_server.cpp:29, PostReceive), `kDatagramBytes = 2048`(iocp_server.cpp:30, PostDatagramReceive), accept는 `2 * (sizeof(sockaddr_storage) + 16)`(iocp_server.cpp:337-338). 문서의 4096은 어느 경로에도 해당하지 않는 숫자다.

또 스니펫에 `remote`/`remoteLength`/`offset`이 없는데, 이 세 필드가 각각 UDP endpoint 추출(iocp_server.cpp:561-563), 재게시(iocp_server.cpp:574-575), partial send 이어보내기(iocp_server.cpp:540-543)라는 step03 본문이 강조하는 동작의 핵심 상태다. 소유권을 설명하는 절의 대표 스니펫에서 그 상태가 빠져 있다.

"고정 buffer"라는 표현도 vector(힙 동적 크기)와 어긋난다. 수명 보장은 여전히 성립하지만(operation이 소유), 서술과 코드가 다르다.

**방향**: 스니펫을 실제 `IoOperation` 정의로 교체하고 수신 buffer 크기 상수(16KiB TCP, 2KiB UDP)를 본문에 명시한다. step02의 "고정 buffer" 문구는 "operation 객체가 소유하는 buffer"로 고친다.

### 8. [중간] step03이 서술한 WSASendTo 전송 길이 검사가 구현에 없음 — snapshot metric은 제출 시점 카운트

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
`articles/step03_iocp.md:98-99` "UDP `WSASendTo`는 데이터그램 하나가 전부 전송되거나 실패해야 한다. 성공 결과가 요청 길이와 다르면 정상 snapshot으로 취급하지 않는다."

실제 완료 처리 `src/iocp_server.cpp:464-466`
```cpp
        case OperationKind::DatagramSend:
            delete operation;
            break;
```
`bytes`도 `error`도 검사하지 않는다. 제출부 `src/iocp_server.cpp:198`
```cpp
        if (rc == 0 || WSAGetLastError() == WSA_IO_PENDING) return true;
```
호출부 `src/server_runtime.cpp:191-193`
```cpp
        auto packet = EncodeDatagram(DatagramType::Snapshot, id,
                                     snapshotSequence_, payload);
        if (backend_.SendDatagram(client.endpoint, packet)) ++metrics_.snapshots;
```
```

`SendDatagram`은 `WSA_IO_PENDING`만 확인하고 true를 반환하므로 `metrics_.snapshots`는 "제출 성공" 카운터다. 완료 시점에 전송 byte가 요청보다 작거나 completion이 실패해도 아무 곳에서도 감지·차감되지 않는다. 문서가 명시한 검사가 코드에 존재하지 않는다.

epoll backend도 같다. `src/epoll_server.cpp:377-380`의 `sendto(...)` 반환값을 쓰지 않고, `SendDatagram`(epoll_server.cpp:131-140)은 command queue에 넣은 즉시 true를 반환한다.

이 metric은 `articles/step06_verification.md:86-88` "snapshot은 60Hz tick의 1/3 부근이어야 하지만..."의 해석 근거로 쓰이므로, 무엇을 세는 값인지가 문서 서술과 달라지면 metric 기반 판정이 흔들린다.

**방향**: 둘 중 하나를 택한다. (1) 문서대로 구현: `DatagramSend` 완료에서 `error != ERROR_SUCCESS || bytes != operation->buffer.size()` 인 경우를 별도 카운터로 집계하고 `metrics_.snapshots`에서 제외한다. (2) 문서 수정: step03의 문장을 삭제하고 `metrics_.snapshots`가 제출 카운터임을 step06 metric 절에 명시한다.

### 9. [낮음] bounded send queue: 상한 초과 동작이 문서 스니펫·두 backend에서 각각 다르고 검증도 없음

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
문서 `articles/step02_runtime.md:81` "상한을 넘는 연결은 새 데이터를 무시하지 않고 종료시킨다." 및 83-88행 스니펫
```cpp
if (session.queuedBytes + frame.size() > kMaxQueuedSendBytes) {
    CloseSession(session.id, slow_receiver_error);
    return false;
}
```
IOCP `src/iocp_server.cpp:160-164`
```cpp
            if (bytes.size() > config_.maxQueuedSendBytesPerConnection -
                                   std::min(session->queuedSendBytes,
                                            config_.maxQueuedSendBytesPerConnection)) {
                return false;
            }
```
— `CloseSession` 호출 없음. 종료는 호출자(`src/server_runtime.cpp:100`, `:106`의 `if (!backend_.SendStream(id, frame)) backend_.Disconnect(id);`)가 한다.

epoll `src/epoll_server.cpp:392-396`
```cpp
            if (command.payload.size() > config_.maxQueuedSendBytesPerConnection -
                    std::min(session->queuedSendBytes,
                             config_.maxQueuedSendBytesPerConnection)) {
                CloseSession(session, ENOBUFS);
                continue;
            }
```
— backend 내부에서 종료하고 `Disconnected` 이벤트에 `error = ENOBUFS`를 실어 보낸다. 반면 epoll `SendStream`(epoll_server.cpp:120-129)은 항상 true를 반환하므로 runtime은 상한 초과를 알 수 없다.

변이 결과: IOCP의 위 상한 검사를 `if (false) { return false; }` 로 바꿔도 `M11_unbounded_send_queue: rc=0  100% tests passed out of 2`.
```

`articles/step04_epoll.md:106` "느린 TCP 연결에는 같은 byte 상한을 적용한다"는 공통 경계 주장이 있지만, 상한값(`maxQueuedSendBytesPerConnection = 256 * 1024`)만 같고 초과 시의 관측 가능한 결과는 다르다. epoll에서는 `Disconnected.error == ENOBUFS`가 관측되고 IOCP에서는 `error == 0`인 Disconnect가 된다. 두 backend가 같은 상위 불변식을 만족하는지 확인한다는 step04의 목표에 비추면, 이 차이는 지금 아무 테스트로도 드러나지 않는다.

README:14 "느린 receiver를 무한 queue로 감추지 않는다 | 연결별 queued send byte에 상한을 둔다."가 검증된 계약처럼 표에 올라 있으나 상한을 제거해도 전체 테스트가 통과한다.

**방향**: fake backend 대신 실제 backend를 쓰는 소형 테스트로, 수신을 하지 않는 TCP 피어를 붙인 뒤 서버가 상한을 넘는 양을 큐잉했을 때 정확히 한 번의 Disconnected가 나오는지 확인한다. 두 backend의 error 코드 규약(0 vs ENOBUFS)을 `network_types.h`에 명문화하고 IOCP도 같은 코드로 맞춘다.

### 10. [낮음] step06의 테스트 스니펫이 실제 protocol_tests.cpp와 다름

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
`articles/step06_verification.md:28-34`
```cpp
for (std::size_t offset = 0; offset < frame.size(); offset += 3) {
    const std::size_t count = std::min<std::size_t>(3, frame.size() - offset);
    CHECK(decoder.Push(std::span(frame).subspan(offset, count)));
}
CHECK(decoder.Pop().has_value());
```
실제 `tests/protocol_tests.cpp:26-33`
```cpp
    FrameDecoder decoder;
    Require(decoder.Push(std::span(frame).first(3)), "push first fragment");
    Require(!decoder.Pop().has_value(), "partial header must wait");
    Require(decoder.Push(std::span(frame).subspan(3, 7)), "push second fragment");
    Require(!decoder.Pop().has_value(), "partial payload must wait");
    Require(decoder.Push(std::span(frame).subspan(10)), "push final fragment");
    auto decodedFrame = decoder.Pop();
```
`CHECK`라는 매크로/함수는 lab 전체에 존재하지 않는다(`grep -rn "CHECK" tests src` 무결과, 실제 헬퍼는 `Require`).
```

실제 테스트는 3바이트 반복 루프가 아니라 3 / 7 / 나머지 세 조각으로 나누고, 중간 상태(`!Pop().has_value()`)까지 확인한다 — 문서 스니펫보다 오히려 강한 검사다. 그런데 문서는 존재하지 않는 API(`CHECK`)와 다른 분할 전략을 보여 준다. 문서만 보고 재현하면 컴파일되지 않는다.

`PROJECTS_PLAN.md:50`의 "스니펫 검증을 둔다"가 미이행이라는 점을 확인하는 사례이기도 하다. `scripts/check_project_docs.py`는 코드펜스에 언어 태그가 있는지(88행)와 구현 언어 블록의 존재 여부(117행)만 보고 내용은 대조하지 않는다.

**방향**: 스니펫을 실제 `Require` 기반 코드로 교체한다. 아울러 articles의 cpp 블록을 소스와 대조하는 검사를 `scripts/`에 추가해 이 유형(총 4건: 이 건, NetworkEvent, IoOperation, --ticks 120)이 재발하지 않게 한다.

### 11. [낮음] 이론축 핵심 주제 중 신뢰성 계층·상태 복제·reconciliation replay가 lab에서 미검증 (범위 밖으로 명시 선언됨)

<sub>분류: 문서대응누락</sub>

**근거**

```text
이론축 `network/README.md:14` "06 | 게임 전송 프로토콜 | sequence·ack·channel·retransmission·pacing", `:17` "09 | 상태 복제 | snapshot·baseline·delta·AOI·priority·bandwidth budget", `:16` "08 | 권위와 지연 은폐 | fixed tick·authority·prediction·reconciliation·interpolation·lag compensation".
`network/README.md:41` 완료 조건 "UDP 위 reliable·unreliable channel이 wrap, loss, duplication, reordering에서 불변식을 지키는가?"

lab 측 선언 `README.md:145-153` "구현하지 않은 범위도 명시한다. ... UDP reliable channel과 congestion control / delta snapshot, baseline, AOI ..."
`articles/step05_authoritative_tick.md:112-113` "이 클라이언트는 권위 위치로 되돌린 뒤 ACK 이후의 미확정 입력을 replay하지 않는다. 따라서 완전한 server reconciliation 구현이 아니라 prediction error를 측정하는 최소 관측기다."

실제 대응 상황: full snapshot·fixed tick·prediction 관측기는 있고, ack bitfield·재전송·중복 억제·delta/baseline·input history replay는 없다. loss/latency/reorder 주입 테스트도 없다(`tests/` 전체에서 실패 주입은 oversized UDP 1종).
```

이 항목은 은폐가 아니다. README와 step05가 미구현 범위를 명시적으로 열거하고 "구현하지 않은 기능을 주석이나 빈 interface로 존재하는 것처럼 표시하지 않는다"(README:155)는 원칙까지 지킨다 — mega 감사에서 문제였던 "빈 interface로 대체" 유형이 여기엔 없다.

다만 결과적으로 network 축 15편의 핵심 모델 중 part06(sequence·ack·retransmission), part09(delta·baseline·AOI·budget), part10(rollback/resimulation), part12(loss·jitter 주입)이 이 lab에서 하나도 실험되지 않는다. `PROJECTS_PLAN.md:110-115`에 따르면 이 중 reliable ordered 전달·손실/지연/재정렬 주입은 mega가 담당하므로 축 전체로는 공백이 아니지만, mini lab 단독으로는 "이 축에서 배운 것을 단계적으로 검증"의 절반 정도만 덮는다.

**방향**: README의 "구현하지 않은 범위" 목록 옆에 "이 항목은 projects/mega에서 검증됨 / 어디서도 검증되지 않음"을 구분해 적는다. 저비용 확장으로는 `IServerBackend` seam에 loss/reorder/지연 주입 decorator를 끼운 결정적 테스트가 있다 — 실제 소켓을 건드리지 않고도 step06이 강조한 "loopback에서는 손실이 거의 없다"(step06:3-4)는 문제를 정면으로 다룰 수 있다.

---

## live_service_lab — 보완 필요 (15건)

학습 대상 대체는 없다. 세 csproj 전부 `PackageReference`가 0개이고, Core는 BCL(HMACSHA256, CryptographicOperations.FixedTimeEquals, System.Text.Json)만 쓰며 token 형식·claim 범위·canonical fingerprint·매칭 정책·fleet 상태 기계·멱등 전이를 전부 직접 구현했다. ASP.NET Core는 라우팅/JSON 바인딩만 담당하고 bearer 파싱·constant-time server key 비교·예외→status 매핑은 손으로 짰으므로 학습 대상이 아닌 편의다. 문제는 검증력이다. 32개 변이(mutant)를 주입한 결과 24개가 살아남았고, 양성 대조 6개는 전부 잡혔으므로 이는 하네스 결함이 아니라 실제 테스트 공백이다. PROJECTS_PLAN L39가 이 lab에 명시한 자동 검증 4항목 중 token audience·game token 만료·payload 충돌 3항목이 테스트에 없고, README '검증하는 계약' 11행 중 자동 테스트가 실제로 잡는 것은 4행뿐이다. step05가 8단계로 기술한 process smoke는 저장소 어디에도 result 제출(7·8단계)과 잘못된 login 400 검사(2단계)가 없다. 추가로 매치 종료 뒤 ticket이 Assigned에 영구히 머물러 플레이어가 두 번째 매치를 배정받지 못하는 상태 기계 구멍을 실행으로 확인했다.

### 1. [치명] 핵심 계약 변이 24개가 테스트를 통과 — 자동 테스트가 실질적으로 무력

<sub>분류: 검증력부족</sub>

**근거**

```text
tests/LiveService.Tests/Program.cs 전체(106줄)를 대상으로 src/LiveService.Core 사본에 변이를 주입하고 `dotnet run --project tests/LiveService.Tests/LiveService.Tests.csproj -c Release`를 반복 실행했다. 원본은 "live_service_tests: all checks passed" 통과.

SURVIVED(테스트가 여전히 통과) 24건:
- M1 ControlPlane.cs:216 `candidate.ActiveMatches < candidate.Capacity` 제거
- M15 ControlPlane.cs:26 `registration.Capacity <= 0` 가드 제거
- M2 ControlPlane.cs:215 heartbeat 신선도 필터 제거 / M2b ControlPlane.cs:255-257 MarkUnhealthyServers 무력화 / X2 ServerHealthTimeout 15초→15일 / X3 Heartbeat()를 no-op으로
- M3 ControlPlane.cs:208 `allowedGap = 100 + Math.Min(400, ...)` → `= 100` (점진 확대 제거) / M3b 상한 400→4000 / M3c Math.Max(0,...) clamp 제거
- M4 ControlPlane.cs:82 playerTickets 중복 억제 제거
- M5 ControlPlane.cs:137 fingerprint 충돌 검사 제거
- M6 ControlPlane.cs:105 ticket 소유자 검사 제거
- M7 ControlPlane.cs:156 `match.Players.SetEquals` 제거
- M8 ControlPlane.cs:152 `match.Completed` 이중 완료 검사 제거
- M9 ControlPlane.cs:148 신규 result의 match owner 검사 제거
- M13 ControlPlane.cs:214 서버 region 일치 제거 / M14 ControlPlane.cs:203 ticket region 일치 제거
- M10 TokenService.cs:61 `payload.ServerId != expectedServerId`(audience) 제거
- M11 TokenService.cs:62 game token 만료 검사 제거
- M12 TokenService.cs:64-65 MatchId/TokenId 공백 가드 제거
- X1 GameTokenLifetime 45초→45일
- X4 ControlPlane.cs:168 `server.ActiveMatches--` 제거
- X5 Assign()의 TokenId를 두 플레이어 모두 고정 문자열로
- X6 ControlPlane.cs:247 `ticket.Endpoint = server.Endpoint` → 리터럴 "127.0.0.1:42001"

KILLED(정상 검출) 7건: 접근 토큰 만료, HMAC 비교 ||→&&, ValidIdentifier 무력화, PlayerId 공백 가드, drain(Ready lifecycle) 필터, duplicate receipt 플래그, duplicate 경로 owner 검사. 즉 하네스는 정상 작동한다.
```

살아남은 변이 목록이 곧 이 lab이 자동으로 보장하지 못하는 항목 목록이다. capacity 초과 금지, heartbeat timeout, progressive widening, ticket 중복 억제, idempotency payload 충돌, assignment 소유권, result player 집합, 이중 완료, 신규 result owner, game token audience·만료 — README가 '검증하는 계약'으로 선언한 항목 대부분이 여기에 포함된다. 구현 자체는 별도 probe로 확인한 결과 대체로 옳다(capacity 1 서버에 4명 큐 → 2명만 Assigned, gap 450은 t=35s에 allowedGap 450에서 배정, gap 600은 300초까지 미배정). 문제는 그 옳음을 코드 변경으로부터 지켜 줄 장치가 없다는 것이다. X6은 특히 테스트가 자기 입력 리터럴을 되비추는 형태여서(tests Program.cs:27에서 등록한 "127.0.0.1:42001"을 L48에서 그대로 기대) endpoint 전달 경로를 통째로 지워도 잡히지 않는다.

**방향**: 살아남은 변이 각각에 대응하는 음성 테스트를 추가한다. 최소한 PROJECTS_PLAN L39가 명시한 4항목(audience, 만료, 매치 할당, drain, payload 충돌)은 전부 실패 주입 형태로 두어야 한다. 예: 다른 serverId로 game token 검증 실패, +46초에서 만료, capacity 1 서버에 4명 큐 넣고 2명만 Assigned, 15초 heartbeat 공백 뒤 Waiting 유지, 같은 resultId·다른 score로 InvalidOperationException, 남의 ticketId 조회 시 null.

### 2. [높음] PROJECTS_PLAN이 이 lab에 요구한 자동 검증 항목 중 3/4가 테스트에 없음

<sub>분류: 문서대응누락</sub>

**근거**

```text
PROJECTS_PLAN.md:39 `- \`live_service_lab\`: token audience·만료, 매치 할당, server drain, 멱등 결과와 payload 충돌`. 같은 문서 L29: "빌드 성공은 완료 조건이 아니다. 렌더링과 UI에서 계산 코어를 분리하고 최소한 다음 자동 검증을 둔다."

대조 결과:
- token audience: tests Program.cs:50은 올바른 "server-1"로만 검증. 잘못된 audience로 거부되는지 검사 없음 → 변이 M10 SURVIVED
- token 만료: 접근 토큰 만료는 tests Program.cs:16에서 검증되나 game token 만료는 검사 없음 → 변이 M11, X1 SURVIVED
- 매치 할당: tests Program.cs:39-47이 2인 배정을 검사. capacity·region·widening은 미검사 → M1, M13, M14, M3 SURVIVED
- server drain: tests Program.cs:86-92에서 검증됨 (변이 C1 KILLED)
- 멱등 결과: tests Program.cs:62-64에서 검증됨 (C2, C3 KILLED)
- payload 충돌: 같은 resultId에 다른 payload를 넣는 테스트가 파일 전체에 없음 → M5 SURVIVED
```

VERIFY.md:63도 `live_service_lab`: token 위조·만료, match allocation, drain, server-bound idempotent result`라고 적지만, 여기서 '만료'는 접근 토큰만이고 game token 쪽은 비어 있다. 'server-bound'도 duplicate 경로(ControlPlane.cs:132)만 검증되고 최초 제출 경로의 owner 검사(ControlPlane.cs:148)는 M9 SURVIVED로 확인된 대로 미검증이다.

**방향**: PROJECTS_PLAN L39의 5개 키워드를 테스트 함수명에 1:1로 대응시키고, 각각 정상 1건 + 실패 주입 1건을 두어 명세와 테스트의 대응을 눈으로 확인할 수 있게 한다.

### 3. [높음] README '검증하는 계약' 11행 중 자동 테스트가 실제로 잡는 것은 4행

<sub>분류: 문서대응누락</sub>

**근거**

```text
README.md:27-39의 표. 변이 실험으로 판정한 결과:
- L29 "token 위조를 탐지한다" → 검증됨 (C4 KILLED)
- L30 "token은 audience와 만료를 가진다" → 미검증 (M10, M11 SURVIVED)
- L31 "queue 재시도는 ticket을 중복 생성하지 않는다" → 미검증 (M4 SURVIVED). tests Program.cs에서 같은 playerId로 두 번 Enqueue하는 호출이 없다
- L32 "대기 시간이 길어지면 검색 범위가 넓어진다" → 미검증 (M3, M3b SURVIVED)
- L33 "unhealthy·draining server에는 새 match를 주지 않는다" → draining만 검증, unhealthy 미검증 (M2, M2b SURVIVED). `control.Heartbeat(...)`는 테스트 전체에서 한 번도 호출되지 않는다
- L34 "capacity를 초과하지 않는다" → 미검증 (M1 SURVIVED)
- L35 "assignment는 ticket 소유자만 조회한다" → 미검증 (M6 SURVIVED)
- L36 "match result retry는 한 번만 반영된다" → 검증됨 (C2 KILLED)
- L37 "같은 idempotency key에 다른 payload는 실패한다" → 미검증 (M5 SURVIVED)
- L38 "server는 자기 match 결과만 제출한다" → 미검증 (M9 SURVIVED)
- L39 "중복 result도 owner만 receipt를 받는다" → 검증됨 (C3 KILLED)
```

표의 오른쪽 열은 '구현'이라고만 적혀 있어 엄밀히는 구현 존재만 주장한다. 그러나 projects/mini/README.md:37은 이 lab을 ".NET Release 빌드·core contract test·HTTP smoke 통과"로, VERIFY.md:44는 "통과"로 표기하고, 같은 mini/README.md:40-42는 "이 표의 '보강 필요'는 실행 코드가 없다는 뜻이 아니다. 시각화 구현은 존재하지만, 잘못된 결과를 자동으로 검출하는 독립 검증이 아직 완료 조건에 미치지 못한다는 뜻이다"라고 '보강 필요'의 기준을 스스로 정의한다. 11개 계약 중 7개가 그 정의에 정확히 해당하는데도 live_service_lab만 무조건 '통과'로 표기되어 있다.

**방향**: README 표에 '자동 테스트' 열을 추가해 계약별로 검증 여부를 표시하거나, mini/README.md와 VERIFY.md의 live_service_lab 행을 다른 lab과 같은 기준으로 '보강 필요'로 정정한다.

### 4. [높음] step05가 기술한 process smoke 8단계 중 3단계가 저장소 어디에도 없음

<sub>분류: 문서대응누락</sub>

**근거**

```text
articles/step05_http_verification.md:80 "실제 HTTP smoke는 API를 loopback 숨김 process로 실행한 뒤 요청 전체를 연결한다." 이어 L82-89에 8단계를 나열한다.

실제 존재하는 유일한 HTTP smoke는 scripts/check_live_mega.ps1이며(bin/obj 제외 저장소 전체에서 `servers/register`·`/results`·`assignments/` 패턴으로 검색한 결과 이 파일과 README·Program.cs뿐), 다음이 없다:
- step05:83 "2. 잘못된 player ID login이 400인지 확인한다" → check_live_mega.ps1은 L99-104에서 유효한 `cross-a`/`cross-b`로만 login한다
- step05:88 "7. owner server가 result를 두 번 제출한다" → 스크립트에 `/results` 호출이 0건
- step05:89 "8. 첫 receipt는 신규, 두 번째는 duplicate이며 recorded count는 1인지 확인한다" → 없음
- step05:86 "5. 두 assignment의 match ID와 endpoint가 같은지 확인한다" → L117-119는 matchId만 비교하고 endpoint는 비교하지 않는다

또한 tests/LiveService.Tests/LiveService.Tests.csproj는 `..\..\src\LiveService.Core\LiveService.Core.csproj`만 참조하고 LiveService.Api는 참조하지 않으며, tests/Program.cs 106줄에 HttpClient·Process.Start가 전혀 없다.
```

step05는 '오류 매핑' 표(L47-53)에서 401/400/404/409/200 다섯 가지를 계약으로 선언하지만, 실행되는 HTTP 경로 어디에도 비2xx 응답을 확인하는 코드가 없다. check_live_mega.ps1은 Invoke-RestMethod의 성공 경로만 통과시키므로 상태 코드 매핑은 한 줄도 검증되지 않는다. 결과적으로 Program.cs:96-111의 catch 5개 블록 전체가 미검증이다.

**방향**: step05의 8단계 목록을 실제 스크립트가 하는 일로 축소하거나, check_live_mega.ps1(또는 lab 안의 별도 smoke)에 login 400, /results 2회 제출과 duplicate 판정, /metrics/state의 recordedResults==1, 남의 ticket 조회 404/401을 추가한다.

### 5. [높음] 매치 종료 뒤에도 ticket이 Assigned로 남아 플레이어가 두 번째 매치를 영원히 받지 못함

<sub>분류: 코드오류</sub>

**근거**

```text
ControlPlane.cs:82-87의 중복 억제는 Waiting/Assigned ticket이 있으면 기존 ticket ID를 그대로 반환한다. RecordResult(ControlPlane.cs:161-169)는 receipt 저장·match.Completed·server.ActiveMatches-- 만 수행하고 ticket 상태나 playerTickets 항목을 건드리지 않는다. `playerTickets`는 파일 전체에서 L11(선언)·L82(조회)·L92(삽입)에만 등장하고 제거 코드가 없다.

원본 Core를 참조하는 probe 실행 결과:
```
C requeue after completed match: sameTicketId=True status=Assigned matchId==oldMatch=True gameToken==oldToken=True
C old game token still valid at t=20s? True  at t=50s? False
C metrics: ControlPlaneMetrics { ReadyServers = 1, DrainingServers = 0, WaitingTickets = 1, ActiveMatches = 0, RecordedResults = 1 }
```
(pa/pb 매치 → t=10s에 result 기록 → t=20s에 pa가 다시 /queue → 같은 ticketId, 이미 완료된 match의 ID와 이미 만료 임박한 같은 game token을 되받음. 함께 큐에 들어온 pc는 WaitingTickets=1로 홀로 대기.)
```

제어 평면의 목적이 '매치 할당'인데 프로세스 수명당 플레이어 1인 1매치가 상한이 된다. 반환된 assignment는 완료된 match ID와 45초 수명이 이미 흘러간 game token이라 게임 서버 handshake도 실패한다. README.md:129-137의 '구현 범위 밖' 목록(durable DB, party queue, region ping probe, server orchestrator, key rotation, refresh token, distributed leader election)에 ticket 수명/재큐잉은 들어 있지 않으므로 문서화된 제약도 아니다. 관련해 Models.cs:14의 `TicketStatus.Cancelled`는 선언만 있고 저장소 전체에서 대입되는 곳이 없으며, step01_control_plane.md:52는 "ticket은 `Waiting`, `Assigned`, `Cancelled`다"라고 3상태 기계를 설명한다.

**방향**: RecordResult에서 match.Players에 속한 ticket을 종료 상태로 전이시키고 playerTickets 역색인에서 제거하거나, 최소한 Assigned ticket에 만료를 두어 재큐잉을 허용한다. 그리고 '완료된 매치 뒤 재큐잉 시 새 ticket과 새 game token을 받는다'는 테스트를 추가한다.

### 6. [중간] 멱등성 테스트가 같은 객체를 두 번 넘기는 항등식 형태 — fingerprint 경로가 의미 있게 검증되지 않음

<sub>분류: 검증력부족</sub>

**근거**

```text
tests/LiveService.Tests/Program.cs:58-64
```csharp
var request = new MatchResultRequest(
    "result-1",
    firstView.MatchId!,
    new Dictionary<string, int> { ["p1"] = 1, ["p2"] = 0 });
ResultReceipt receipt = control.RecordResult(request, "server-1", now.AddMinutes(1));
ResultReceipt duplicate = control.RecordResult(request, "server-1", now.AddMinutes(2));
```
동일 인스턴스를 두 번 넘기므로 ResultFingerprint(ControlPlane.cs:263-269)의 정렬·canonical 문자열 생성이 어떤 값을 만들든 두 호출이 같아진다. 변이 M5(ControlPlane.cs:137의 fingerprint 비교를 `if (false)`로 치환) SURVIVED.
```

step04_idempotent_results.md:36-48은 "result ID만 dictionary에 보관하면 같은 key를 다른 payload에 재사용하는 오류를 숨긴다"며 정렬 기반 canonical fingerprint를 도입한 이유를 설명하지만, 정작 그 오류를 드러내는 테스트가 없다. 별도 probe로 실제 구현은 정상 동작함을 확인했다(같은 resultId에 score를 뒤집어 제출 → "Idempotency key was reused with a different payload."). 즉 구현은 옳고 테스트만 퇴화해 있다.

**방향**: (a) 같은 resultId·다른 score로 InvalidOperationException, (b) 같은 resultId·같은 내용이지만 Dictionary 삽입 순서만 다른 새 인스턴스로 Duplicate=true — 두 케이스를 추가하면 정렬 로직까지 실제로 검증된다.

### 7. [중간] 'negative elapsed time clamps' 테스트가 경계에서 퇴화 — clamp를 제거해도 통과

<sub>분류: 검증력부족</sub>

**근거**

```text
tests/LiveService.Tests/Program.cs:94-104는 clock-a(rating 1500, t=0 enqueue)와 clock-b(rating 1550, t=-5s enqueue)를 만든 뒤 t=-5s에서 Assigned를 기대한다. 변이 M3c(ControlPlane.cs:205의 `Math.Max(0, Math.Min(` → `(Math.Min(`, 즉 clamp 제거) SURVIVED.

산술: clamp 있음 → waitedSeconds=0, allowedGap=100. clamp 없음 → waitedSeconds=-5, allowedGap=100+min(400,-50)=50. rating 차이가 |1500-1550|=50이고 ControlPlane.cs:209의 조건은 `> allowedGap`이므로 50 > 50이 거짓 → 두 경우 모두 매칭된다.
```

step03_matching_fleet.md:55-56은 "elapsed time은 0 아래로 내려가지 않게 clamp한다. 그렇지 않으면 기본 허용 폭 100도 음수로 줄어 정상 후보를 거부한다"고 주장하지만, 테스트가 고른 rating 차 50은 clamp 유무를 구분하지 못하는 정확한 경계값이다. rating 차를 51~100 사이(예: 1500/1580)로 두면 clamp 없이는 allowedGap 50 < 80이 되어 Waiting으로 떨어지고 변이가 잡힌다.

**방향**: clock-b의 rating을 1580 등으로 바꿔 clamp 유무가 결과를 가르는 값으로 옮긴다. 같은 방식으로 widening 테스트도 gap 450을 t=34s(미배정)와 t=35s(배정)로 나눠 경계를 고정하면 M3·M3b가 잡힌다.

### 8. [중간] step01의 lock 스니펫이 실제 코드에 존재하지 않음

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
articles/step01_control_plane.md:85-93
```csharp
lock (gate)
{
    if (server.ActiveMatches >= server.Capacity) return;
    server.ActiveMatches++;
    matches[matchId] = match;
    Assign(first, matchId, server, now);
    Assign(second, matchId, server, now);
}
```
실제 ControlPlane.cs의 TryMatch(L188-231)에는 `lock (gate)`도 `if (server.ActiveMatches >= server.Capacity) return;`도 없다. capacity 검사는 L216의 LINQ 술어 `candidate.ActiveMatches < candidate.Capacity`이고, lock은 호출자인 Enqueue(L79)·GetTicket(L100)·RegisterServer(L28)가 잡는다.
```

완료 기준 3번(현재 step에서 추가·변경한 코드가 식별된다)과 7번(최종 코드와 문서 코드가 드리프트하지 않는다) 위반이다. 독자가 문서만 보면 '명시적 early-return 가드'라는, 존재하지 않는 코드 형태를 기억하게 된다. 원자성 주장 자체는 맞다(호출자가 lock을 잡으므로 check-and-increment는 같은 critical section 안에 있다).

**방향**: 스니펫을 TryMatch의 실제 후보 필터 + Assign 호출로 교체하고, lock이 공개 진입점에 있다는 사실을 본문으로 설명한다.

### 9. [중간] step04의 RecordedResult 스니펫이 실제 생성자 시그니처와 불일치(컴파일 불가)

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
articles/step04_idempotent_results.md:84-88
```csharp
results[resultId] = new RecordedResult(receipt, fingerprint);
match.Completed = true;
if (server.ActiveMatches > 0) server.ActiveMatches--;
```
실제 ControlPlane.cs:324-327
```csharp
private sealed record RecordedResult(
    ResultReceipt Receipt,
    string Fingerprint,
    string ServerId);
```
실제 호출은 ControlPlane.cs:162-163 `results[request.ResultId] = new RecordedResult(receipt, fingerprint, serverId);`
```

2인자 호출은 컴파일되지 않으므로 완료 기준 4번(문서의 코드만 따라도 해당 상태를 빌드할 수 있다)과 7번 위반이다. 더구나 같은 문서 L50-52가 "기존 receipt를 반환하기 전에도 저장된 owner server ID와 호출자의 X-Server-Id가 같아야 한다"고 owner 저장의 필요성을 역설하는데, 정작 저장 스니펫에서 그 필드가 빠져 있어 문서 내부에서도 어긋난다.

**방향**: 스니펫에 serverId 인자를 넣어 실제 코드와 일치시킨다.

### 10. [중간] 5개 step 문서 전부가 완료 기준 4번·5번을 충족하지 않음

<sub>분류: 문서오류</sub>

**근거**

```text
articles/*.md의 코드 펜스 언어를 전수 추출한 결과:
```
step01_control_plane.md ['text', 'csharp', 'csharp', 'csharp']
step02_tokens.md ['text', 'csharp', 'csharp', 'csharp', 'csharp']
step03_matching_fleet.md ['csharp', 'csharp', 'csharp', 'csharp']
step04_idempotent_results.md ['csharp', 'csharp', 'csharp']
step05_http_verification.md ['csharp', 'csharp']
```
powershell/bash/console 블록이 0개다. 즉 어느 step에도 빌드·실행 명령과 관측 가능한 출력이 없다(PROJECTS_PLAN.md:48 기준 5번). 실행 명령은 lab README.md:63-85에만 있고 step별 종료 상태와 연결되어 있지 않다.

또한 16개 csharp 블록은 전부 단편이며 합계 약 80줄인 반면 실제 소스는 ControlPlane.cs 328줄 + TokenService.cs 136줄 + Models.cs 59줄 + Program.cs 161줄이다. 어느 step 문서로도 해당 상태를 빌드할 수 없다(기준 4번).
```

각 문서의 `## 들어가며`도 '이 step의 시작 상태 → 종료 상태'를 객관적으로 적지 않고 개념 도입으로 시작한다(기준 1번). 다만 기준 2번(후속 step 예고 금지)은 잘 지켜져 `python scripts/check_project_docs.py` 실행 결과 `Checked 80 project documents: 0 error(s), 0 warning(s).`로 통과한다.

**방향**: 각 step 말미에 해당 시점의 빌드·실행 명령과 기대 출력(예: `live_service_tests: all checks passed`, `/metrics/state` 응답 JSON)을 추가하고, 시작/종료 상태를 한 문단으로 명시한다.

### 11. [중간] PROJECTS_PLAN의 추적 단위 중 관측값·기대 결과·실패 주입이 문서에도 코드에도 없음

<sub>분류: 문서대응누락</sub>

**근거**

```text
PROJECTS_PLAN.md:19-27은 각 lab이 이론 주장/조작 변수/통제 변수/관측값/기대 결과/실패 주입/자동 테스트를 갖도록 요구하고, L9는 mini의 완료 상태를 "계산 정확성 테스트와 재현 가능한 측정이 있다"로 정의한다.

lab 전체(bin/obj 제외)에서 `Stopwatch|Benchmark|elapsed|처리량|측정|p99|throughput`를 검색한 결과 코드 일치가 0건이고, 산문 3건(README.md:121, step03:54, step03:55)과 테스트 메시지 1건(tests/Program.cs:104)만 나온다. 대기 시간 분포, 매칭 지연, 처리량 등 어떤 수치도 기록되지 않는다.

lab README.md:27-39의 표는 '계약 ↔ 구현' 2열뿐이고 조작 변수·통제 변수·관측값·기대 결과·실패 주입 항목이 없다.
```

live_service_lab은 성능 lab이 아니므로 시간·메모리 측정 요구는 완화될 수 있으나, 최소한 '실패 주입'(어떤 입력이 잘못된 구현을 드러내는가)은 이 lab에서 가장 자연스럽게 정의되는 항목인데도 문서에 정의되어 있지 않고 테스트에서도 3건(잘못된 서버 식별자, 잘못된 player 식별자, null score map)에 그친다. step08 이론 문서(live_service/part08_matchmaking.md L78-135)가 대기 시간 큐잉 모델·Little의 법칙·꼬리 지연을 다루는데, lab은 progressive widening 정책의 관측값을 하나도 남기지 않는다.

**방향**: README에 PROJECTS_PLAN L19-27 형식의 추적 단위 표를 추가하고, 최소한 '실패 주입' 열에 각 계약을 깨뜨리는 입력을 명시한 뒤 그 입력들을 테스트로 옮긴다. 결정적 시계를 이미 주입하고 있으므로 큐 대기 시간 분포(N명 투입 시 배정까지 걸린 가상 초 수)는 sleep 없이 재현 가능한 관측값으로 기록할 수 있다.

### 12. [낮음] step05의 /results 핸들러 스니펫이 실제 코드와 불일치(컴파일 불가)

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
articles/step05_http_verification.md:31-36
```csharp
if (!IsServer(http, serverKey)) return Results.Unauthorized();
if (!http.Headers.TryGetValue("X-Server-Id", out var serverId))
    return Results.Unauthorized();
return Results.Ok(control.RecordResult(result, serverId, now));
```
실제 src/LiveService.Api/Program.cs:88-94
```csharp
if (!IsServer(http, serverKey)) return Results.Unauthorized();
if (!http.Headers.TryGetValue("X-Server-Id", out var serverId) ||
    string.IsNullOrWhiteSpace(serverId)) return Results.Unauthorized();
try
{
    return Results.Ok(control.RecordResult(
        result, serverId.ToString(), DateTimeOffset.UtcNow));
```
```

`serverId`는 `StringValues`이므로 `RecordResult(result, serverId, now)`는 string 매개변수에 바인딩되지 않아 컴파일되지 않고, 공백 헤더를 거르는 `IsNullOrWhiteSpace` 검사도 스니펫에서 누락됐다. 완료 기준 4·7번 위반이다.

**방향**: 실제 Program.cs 코드를 그대로 인용한다.

### 13. [낮음] 완료 기준 7번이 요구하는 스니펫 검증 장치가 없음

> **[이월 · 2026-08-13]** step01·04·05의 개별 스니펫은 실제 코드로 교체됐으나, 앵커 주석과 `check_project_docs.py`의 원문 대조 단계는 도입되지 않았다. 저장소 전체에 스니펫 검증 장치가 없는 것은 12개 lab 공통 잔여다.
>
> **[반영됨 · 2026-08-14]** 이 lab에 앵커 **16개**를 달았다(region 4 / 심볼 12). 대상은
> `ControlPlane.cs`(`@state-maps`, `#GetTicket`, `#TryMatch`, `#Enqueue`, `#ResultFingerprint`,
> `#RecordResult`), `TokenService.cs`(`@sign-body`, `#IssueGameToken`, `@verify-signature`,
> `#TryValidateGameToken`), `Api/Program.cs`(`@results-endpoint`), `Tests/Program.cs`(`#IdempotentResult`)다.
>
> **앵커를 달자 드리프트 12건이 드러났고, 12건 전부가 문서 쪽 오류였다.** 소스는 한 줄도
> 고치지 않았다 — `.cs` 파일에 들어간 변경은 `// snippet-begin:` / `// snippet-end:` 마커 주석
> 8줄이 전부다(`TokenService.cs` 4, `Api/Program.cs` 2, `ControlPlane.cs` 2). 유형은 둘이다.
>
> - **컴파일 불가.** `step02_tokens.md`의 `new GamePayload(...)`는 대입 없는 bare `new`라
>   C#에서 **CS0201**(식 문장으로 쓸 수 없는 식)이다. `var payload = new GamePayload(...)`로 정정했다.
> - **존재하지 않는 식별자.** `step03_matching_fleet.md`가 대표적이다 — `playerId` →
>   `request.PlayerId`, `ticket` → `existing`/`first`, `waited` → `waitedSeconds`,
>   `players` → `[first.PlayerId, second.PlayerId]`. 문서 안에서만 통하던 이름들이었다.
>
> 이것이 이 지적("장치가 없으므로 드리프트 3건이 검사망을 그대로 통과한다")이 예상한 그대로다.
> 장치를 켜자 보고서가 세었던 3건이 아니라 12건이 나왔다.

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
PROJECTS_PLAN.md:50 "최종 코드와 문서 코드가 드리프트하지 않도록 스니펫 검증을 둔다." 실제 검사 도구인 scripts/check_project_docs.py는 L67-118에서 제목 개수, `---` 개수, H1 금지, 코드 펜스 언어 존재, 타 문서 경로 참조, 후속 예고 정규식만 검사하고 스니펫 본문을 소스와 대조하는 코드가 없다. scripts/check_projects.ps1:27이 이 스크립트를 호출하는 것이 전부다.
```

장치가 없으므로 위에 보고한 step01·step04·step05 드리프트 3건이 검사망을 그대로 통과한다.

**방향**: 스니펫에 `<!-- snippet: 파일경로:시작-끝 -->` 형태 앵커를 붙이고 check_project_docs.py에 원문 대조 단계를 추가한다. 최소한 컴파일 가능한 블록만이라도 소스 부분 문자열인지 확인한다.

### 14. [낮음] ValidIdentifier가 Core와 Api에 동일하게 중복 구현

<sub>분류: 코드오류</sub>

**근거**

```text
src/LiveService.Core/ControlPlane.cs:271-275
```csharp
private static bool ValidIdentifier(string value, int maximumLength) =>
    !string.IsNullOrWhiteSpace(value) && value.Length <= maximumLength &&
    value.All(character =>
        character is >= 'a' and <= 'z' or >= 'A' and <= 'Z' or >= '0' and <= '9' or
        '-' or '_' or '.');
```
src/LiveService.Api/Program.cs:153-157에 같은 본문이 `static bool ValidIdentifier(...)`로 다시 존재한다. Core 쪽이 private이라 Api가 재사용할 수 없다.
```

`/login`은 Api 사본으로 playerId를 검사해 access token을 발급하고, 그 뒤 `/queue`는 Core 사본으로 같은 playerId를 다시 검사한다. 두 사본이 갈라지면 토큰은 발급되지만 큐 진입이 400으로 거부되는 상태가 만들어진다. step02_tokens.md:92-95는 이 규칙을 "Mega의 작은 C++ token parser와 JSON escape 규칙이 달라지지 않도록" 두는 소비자 간 명시적 계약이라고 설명하는데, 계약의 정의가 두 곳에 복제되어 있다.

**방향**: Core에 `public static class Identifiers { public static bool IsValid(string, int) }`를 두고 Api가 그것을 호출하게 한다. 계약이 하나뿐임을 확인하는 테스트도 함께 둔다.

### 15. [낮음] VERIFY.md와 mini/README.md가 lab 밖 스크립트의 smoke를 이 lab의 검증으로 계상

<sub>분류: 문서오류</sub>

**근거**

```text
projects/mini/VERIFY.md:44 `| \`live_service_lab\` | 통과 | core contract + HTTP smoke | 5 |`, projects/mini/README.md:37 "…core contract test·HTTP smoke 통과".

실제 scripts/check_projects.ps1의 live_service_lab 구간(L87-100)은 `dotnet build`와 `dotnet run --project ...LiveService.Tests...`만 실행하고 결과를 `Tests = "Core contract tests passed"`로 기록한다. HTTP를 실제로 호출하는 check_live_mega.ps1은 L123에서 mega 구간의 일부로 호출되며 그 결과는 L132 `Tests = "CTest + live admission smoke passed"`로 mega 행에 기록된다.
```

그 smoke도 register→login→queue→assignments 행복 경로와 UDP admission까지만 다루고 /results·상태 코드 매핑은 건드리지 않으므로, 'HTTP smoke 통과'라는 표기가 실제 커버리지보다 넓게 읽힌다.

**방향**: VERIFY.md와 mini/README.md의 해당 셀을 'core contract test(교차 언어 admission smoke는 mega 항목)'처럼 실제 실행 위치와 범위를 드러내는 표현으로 정정한다.

---

## db_lab — 보완 필요 (12건)

학습 대상 대체는 전혀 없다 — CMakeLists.txt에 find_package/target_link_libraries가 하나도 없고, 소스의 모든 #include가 표준 헤더 + <io.h>/<unistd.h>뿐이다. CRC32 테이블, 프레임 직렬화, WAL append, COMMIT 마커 복구, fsync 분기를 전부 직접 구현했으므로 이 축(WAL 원자성·내구성·절단 복구)의 배울 것을 라이브러리에 넘긴 부분은 없다. 검증력의 핵심 축(COMMIT 마커 원자성)도 진짜 이빨이 있다 — 복사본에서 Replay가 COMMIT 전에 op를 적용하도록 변이하니 크래시 스윕이 633 trials 중 319 failures를 냈고, Open()의 꼬리 절단을 제거하니 db_tests가 잡았다. 그러나 그 주변은 비어 있다: 변이 4건(Del 프레임 디코드 무력화, kMaxFrame 가드 제거, fsync ON 경로 상시 실패, CRC 불일치 시 break→continue 재동기화)이 clean 재빌드 후 ctest 2/2를 그대로 통과했고, 이 넷은 모두 step 문서가 "검증 기준"으로 명시한 항목이다. PROJECTS_PLAN.md가 db_lab에 요구한 4개 주제 중 "멱등성"과 "동시성"은 구현·테스트가 전무하다. step 문서는 완료 기준 4번(문서 코드만으로 빌드)과 5번(실행 명령·관측 결과)을 충족하지 못하며, step03의 벤치 스니펫은 실제 db_bench.cpp에 존재하지 않는 식별자로 쓰여 있다.

### 1. [높음] CRC 손상 이후 '꼬리 전체 폐기' 규칙이 무검증 — break를 continue로 바꿔도 전체 테스트 통과

<sub>분류: 검증력부족</sub>

**근거**

```text
src/wal_kv.hpp:157 `if (Crc32(payload.data(), len) != crc) break;           // 찢긴 쓰기 검출`
step01_wal_format.md:65 "손상 이후의 바이트를 억지로 재동기화하지 않고 꼬리 전체를 버리는 것이 단순하고 안전하다."
변이 M9(복사본): 위 `break`를 `continue`로 교체 → `cmake --build build --config Release --clean-first` 후
  ./build/Release/db_crash_test.exe → "crash sweep: 633 trials, 0 failures"
  ./build/Release/db_tests.exe      → "db_tests: all passed"
```

손상 프레임을 만나면 멈추지 않고 그 뒤 프레임을 계속 재생하는(재동기화) 구현으로 바꿔도 어떤 테스트도 실패하지 않는다. 원인은 유일한 손상 테스트인 TestCorruptionDetected(db_tests.cpp:91-117)가 파일 *마지막* 프레임의 CRC 필드만 뒤집기 때문이다(`std::fseek(f, -3, SEEK_END)`, 마지막 프레임은 header 8B + payload 1B의 COMMIT 프레임이므로 -3은 crc 필드). 손상 프레임 뒤에 유효한 COMMIT이 더 있는 배치가 한 번도 만들어지지 않아, '손상 이후는 전부 버린다'는 설계 규칙 자체가 검사되지 않는다. db_crash_test는 절단만 주입하고 바이트 손상을 전혀 주입하지 않으므로 633 trials도 이 규칙을 건드리지 않는다.

**방향**: db_tests에 '커밋 3개 로그 → 두 번째 트랜잭션의 PUT payload 한 바이트 flip → Replay' 케이스를 추가하고, committed_txns == 1 이며 세 번째 트랜잭션의 키가 인덱스에 없음을 검사하라. db_crash_test 스윕에 절단과 별개로 '임의 오프셋 1바이트 flip' 주입 축을 추가하면 같은 규칙을 통계적으로도 덮는다.

### 2. [높음] Op::Del 프레임의 디스크 디코드 경로가 어떤 테스트에도 걸리지 않음

<sub>분류: 검증력부족</sub>

**근거**

```text
src/wal_kv.hpp:206 `            case Op::Del:    return readStr(out.key);`
step01_wal_format.md:69 "1. Put·Del·Commit 프레임이 왕복 직렬화된다."
README.md:25 "| `db_tests` | 단위 테스트 (ctest) | put/get/del 가시성, 재시작 복구, ..."
변이 M1(복사본): 위 줄을 `case Op::Del:    return false;` 로 교체 → clean 재빌드 후
  db_crash_test.exe → "crash sweep: 633 trials, 0 failures"
  db_tests.exe      → "db_tests: all passed"
```

Del 프레임을 디스크에서 절대 해석하지 못하게 만들어도 전체 스위트가 통과한다. TestBasic(db_tests.cpp:17-30)은 put 후 del을 같은 트랜잭션에서 커밋하지만 재오픈하지 않으므로 메모리 ApplyToIndex만 검사하고, TestReopen/TestUncommittedTailDiscarded/TestCorruptionDetected와 db_crash_test·db_bench는 모두 Put만 기록한다. 즉 Del은 '왕복 직렬화'가 한 번도 확인되지 않았다 — step01 검증 기준 1번이 실제 테스트에 없다.

**방향**: TestBasic 뒤에 재오픈 단계를 붙이거나 별도 테스트를 추가해 (a) 커밋된 Del이 재시작 후에도 키를 지운 상태로 남는지, (b) Del 후 다시 Put한 값이 복구되는지를 검사하라.

### 3. [높음] fsync ON 커밋 경로에 자동 검증이 0건 — 상시 실패로 만들어도 ctest 통과

<sub>분류: 검증력부족</sub>

**근거**

```text
src/wal_kv.hpp:114 `        if (opt_.fsync_on_commit && WALKV_FSYNC(f_) != 0) return false;`
테스트·스윕의 모든 Open 호출: db_tests.cpp:20,37,44,57,74,83 / db_crash_test.cpp:28 — 전부 `{.fsync_on_commit = false}`
변이 M5(복사본): 위 줄을 `if (opt_.fsync_on_commit) return false;` 로 교체 → clean 재빌드 후
  db_crash_test.exe → "crash sweep: 633 trials, 0 failures"
  db_tests.exe      → "db_tests: all passed"
src/db_bench.cpp:35 `        db.Commit();`  (반환값 폐기)
```

README.md:9가 이 lab의 첫 번째 주장으로 내건 "내구성에는 가격이 있다 — fsync ON/OFF"의 ON 경로가 ctest에서 한 번도 실행되지 않는다. 유일하게 fsync=true를 켜는 db_bench는 add_test에 등록되어 있지 않고(CMakeLists.txt:26-27), 더구나 db_bench.cpp:35에서 Commit()의 bool 반환을 버리므로 fsync가 항상 실패해도 벤치는 그대로 '성공한 커밋'의 지연 표를 출력한다. 잘못된 구현을 드러낼 입력이 없다.

**방향**: db_tests에 fsync_on_commit = true로 put→commit→재오픈 복구를 확인하는 케이스를 추가하고, db_bench의 `db.Commit();`을 반환값 검사(실패 시 즉시 중단·비정상 종료)로 바꿔라.

### 4. [높음] 공식 완료 기준의 '멱등성'과 '동시성' 항목이 lab에 전혀 없음

> **[반영됨 · 2026-08-13]** 방향 (b)를 택했다. 수정 위치가 `projects/mini/` 밖이다 — `PROJECTS_PLAN.md:42`가 `db_lab: WAL 원자성·내구성, 절단 복구, 손상 프레임 이후 꼬리 폐기 (멱등성은 live_service_lab, 동시성은 system_kernel_lab·game_network_lab의 범위)`로 바뀌어 두 항목이 다른 lab의 책임으로 이관됐다.

<sub>분류: 문서대응누락</sub>

**근거**

```text
PROJECTS_PLAN.md:40 "- `db_lab`: WAL 원자성·내구성, 절단 복구, 멱등성, 동시성"
db/part04_economy_integrity.md:9 "읽는 기준은 지급 함수다. 같은 요청이 100번 병렬로 들어와도 원장 행이 하나만 생기는지, ..."
README.md:62 "- 단일 스레드·단일 파일 — 동시성(잠금·MVCC)과 인덱스 자료구조(B-tree/LSM)는 시리즈 글의 영역이고, 이 lab은 내구성·원자성의 물리만 떼어 측정한다."
step02_transaction_recovery.md:47 "... 이 때문에 경제 연산 재시도에는 트랜잭션 ID와 멱등성 정책이 필요하다."
lab 전체 검색(build/ 제외): '멱등'은 step02:47 한 줄뿐, 트랜잭션 ID·중복 억제·스레드·잠금 코드는 0건
```

PROJECTS_PLAN이 db_lab에 요구한 4개 주제 중 2개가 코드·테스트에 존재하지 않는다. 멱등성은 이론 문서(part04)의 중심 주제이자 WAL이 만드는 '결과 미확정' 상태의 직접적 후속인데(step02:47이 그 필요성을 스스로 적어 놓고 구현하지 않는다), 트랜잭션 ID나 중복 커밋 억제가 프레임 형식에도 API에도 없다. 동시성은 README·step02·step04가 '범위 밖'이라고 명시하지만 PROJECTS_PLAN은 갱신되지 않아, 선언된 기준과 실제가 어긋난 채 남아 있다.

**방향**: 둘 중 하나를 택하라. (a) 프레임에 txn_id를 추가해 Replay가 이미 적용된 id를 건너뛰게 하고, '같은 트랜잭션을 2회 append → 잔액 합 불변' 테스트를 넣어 멱등성을 실제 검증 항목으로 만든다. (b) 멱등성·동시성을 db_lab이 아닌 다른 lab의 책임으로 옮기고 PROJECTS_PLAN.md:40의 목록을 실제 범위(WAL 원자성·내구성, 절단 복구)로 수정한다.

### 5. [높음] step 문서만으로는 어떤 단계도 빌드할 수 없음 — 전체 4편의 C++ 스니펫이 3개(약 30줄)뿐

<sub>분류: 문서대응누락</sub>

**근거**

```text
PROJECTS_PLAN.md:47 "4. 문서의 코드만 따라도 해당 상태를 빌드할 수 있다."
코드 펜스 전수: step01(```cpp 1개: AppendU32/ReadU32Le), step02(```cpp 1개: Commit()), step03(```cpp 1개: BenchCommit 스케치), step04(```text 1개 + ```bash 1개, cpp 0개)
문서에 전혀 없는 실제 소스: wal_kv.hpp의 Crc32(31-45), Frame/EncodePayload(51-78), class WalKv 선언·Open(81-101), Replay(139-179), ApplyToIndex(184-187), DecodePayload(189-210), WriteFrame(212-220) — 그리고 CMakeLists.txt, db_tests.cpp, db_crash_test.cpp, db_bench.cpp main 전체
```

src/ 4개 파일 약 600줄 중 문서에 실린 것은 30줄 미만이다. Replay·DecodePayload·WriteFrame처럼 이 lab의 학습 대상 그 자체인 함수가 문서에 코드로 한 줄도 없고 산문 설명만 있다. 완료 기준 4번이 4개 step 전부에서 미충족이며, 결과적으로 3번(현재 step에서 추가·변경한 코드가 식별된다)도 파일·함수 단위로는 식별되지 않는다.

**방향**: 각 step에 그 단계에서 추가되는 함수 전문을 싣고(step01: Crc32·EncodePayload·DecodePayload·프레임 구조체, step02: class WalKv·Open·Replay, step03: db_bench.cpp 전문, step04: db_crash_test.cpp·db_tests.cpp 전문), step01 또는 step02에 CMakeLists.txt를 포함하라.

### 6. [중간] step04 결론이 검사하지 않는 '비음수 재고' 불변식을 검사한다고 서술

<sub>분류: 문서오류</sub>

**근거**

```text
articles/step04_crash_test.md:70 "로그의 모든 절단 지점을 순회하는 크래시 주입과 단위 테스트를 결합해, 복구 상태가 완전히 커밋된 트랜잭션의 접두사라는 불변식을 검사한다. 잔액 보존과 비음수 재고 같은 도메인 불변식도 저장 형식 검증과 분리해 확인한다."
src/db_crash_test.cpp:41 `        // 음수 잔액은 허용한다. 이 실험은 보존과 트랜잭션 원자성만 검사한다.`
src/db_crash_test.cpp:69-84 CheckInvariants — 키 존재 검사와 `sum != kAccounts * kInitBalance` 검사만 존재, 음수·비음수 검사 없음
```

문서가 '확인한다'고 단언한 도메인 불변식을 코드는 명시적으로 확인하지 않기로 선언했다. 코드 주석과 문서 결론이 정면으로 충돌한다.

**방향**: step04 결론에서 '비음수 재고' 문구를 삭제하거나, BuildLog에 잔액 부족 시 이체를 건너뛰는 규칙을 넣고 CheckInvariants에 비음수 검사를 추가해 문서와 코드를 맞춰라.

### 7. [중간] step04 결론의 '모든 절단 지점'이 실제 샘플링(약 1.7%)과 불일치 — 같은 문서 안에서 모순

<sub>분류: 문서오류</sub>

**근거**

```text
articles/step04_crash_test.md:70 "로그의 모든 절단 지점을 순회하는 크래시 주입과 ..."
articles/step04_crash_test.md:18 "실제 전원을 수백 번 끄는 대신 정상 WAL의 파일 전체 범위에서 절단 위치를 샘플링해 torn tail을 만든다."
articles/step04_crash_test.md:27 "모든 바이트를 검사하려면 보폭을 1로 바꿀 수 있지만, 로그 길이에 따라 실행 시간이 크게 늘어난다."
src/db_crash_test.cpp:93 `        for (long long cutAt = 0; cutAt <= size; cutAt += (size / 211) + 1) {`
계측(복사본 probe): "file size=12566 stride=60", seed당 210 지점 → 12,566 바이트 중 210곳(약 1.7%)
```

§1·§3과 코드는 '샘플링'이라고 정확히 적어 놓았는데 결론 한 문장만 '모든 절단 지점'이라고 과장한다. README.md:45는 반대로 정확하다("검사한 절단 위치에서"). 결론만 고치면 되는 국소적 문서 오류지만, 완료 기준을 읽는 사람이 보장 범위를 잘못 이해하게 만든다.

**방향**: step04_crash_test.md:70의 '모든 절단 지점을 순회하는'을 '파일 전체 범위에서 표본 절단 지점을 순회하는'으로 수정하라.

### 8. [중간] step04가 선언한 세 번째 불변식('복구 트랜잭션 수가 가능한 접두사와 일치')이 구현되지 않음

<sub>분류: 문서대응누락</sub>

**근거**

```text
articles/step04_crash_test.md:35-37 "- 전체 잔액 합은 초기 합과 같다.\n- 한 이체의 차감만 또는 입금만 적용되지 않는다.\n- 복구된 트랜잭션 수가 가능한 접두사와 맞는다."
src/db_crash_test.cpp:69-71 `static bool CheckInvariants(const std::unordered_map<std::string, std::string>& idx,\n                            uint64_t committed, std::string& why) {\n    if (committed == 0) return true;  // 초기화 커밋조차 못 미침 — 빈 스토어, 일관`
committed 파라미터는 함수 본문에서 위 조기 반환 외에 사용되지 않음(db_crash_test.cpp:69-84 전체)
```

세 번째 불변식은 committed 값을 절단 길이로부터 계산 가능한 상한/하한과 비교해야 성립하는데, 코드는 committed를 '0인가'만 본다. 결과적으로 '복구된 트랜잭션 수'는 어떤 값이 나와도 통과한다. 문서가 자동 검증한다고 적은 항목이 테스트에 없는 전형적 사례다. 부수적으로 cut < 480바이트인 seed당 8개 trial(전체 633 중 24개)은 이 조기 반환으로 아무것도 검사하지 않고 통과한다.

**방향**: BuildLog가 각 커밋 직후의 파일 오프셋을 배열로 남기게 하고, 절단 길이 cutAt에 대해 기대 committed = (cutAt 이하 최대 오프셋의 인덱스)를 계산해 rs.committed_txns와 동등 비교하라. 이렇게 하면 '완전한 접두사'라는 주장이 합 보존이 아니라 직접 검사로 바뀐다.

### 9. [중간] step03의 BenchCommit 스니펫이 실제 db_bench.cpp와 드리프트 — 스니펫의 식별자가 저장소에 존재하지 않음

<sub>분류: 문서코드드리프트</sub>

**근거**

```text
articles/step03_benchmark.md:20-32 스니펫은 `transactionCount`, `operationsPerTransaction`, `KeyFor(t, i)`, `Milliseconds(begin, end)`, `samples.push_back(...)`, `const bool committed = db.Commit(); ... if (!committed) throw std::runtime_error("commit failed");` 를 사용
src/ 전체 grep(KeyFor|samples|Milliseconds|runtime_error|transactionCount) → "No matches found"
실제 코드 src/db_bench.cpp:28-38 은 `n_txns`, `txn_ops`, 인라인 키 문자열, `Ms(a, b)`, `total`/`worst` 누산, `db.Commit();`(반환값 폐기)
```

PROJECTS_PLAN 완료 기준 7번(최종 코드와 문서 코드가 드리프트하지 않도록 스니펫 검증)이 지켜지지 않았다. 이름 차이에 그치지 않고 동작이 다르다 — 문서 코드는 커밋 실패 시 예외를 던지지만 실제 벤치는 실패를 무시한다(위 fsync 항목과 결합하면 잘못된 수치를 그대로 출력한다). step01·step02의 스니펫은 변수명만 다르고 의미는 일치하므로 문제는 step03에 국한된다.

**방향**: step03의 스니펫을 db_bench.cpp:28-38의 실제 코드로 교체하거나, 반대로 db_bench.cpp를 문서대로(Commit 반환값 검사 포함) 고쳐 일치시켜라. 스니펫을 소스에서 발췌·대조하는 스크립트를 두면 재발을 막는다.

### 10. [중간] step01·02·03에 실행 명령과 관측 가능한 결과가 없음

<sub>분류: 문서대응누락</sub>

**근거**

```text
PROJECTS_PLAN.md:48 "5. step 종료 시 실행 명령과 관측 가능한 결과가 있다."
step01_wal_format.md:67-79 — §5 '검증 기준' 5개 항목 나열 후 결론, 실행 명령 없음
step02_transaction_recovery.md:61-76 — §5 '검증 기준' 6개 항목 나열 후 결론, 실행 명령 없음
step03_benchmark.md:59-73 — §5 '결과 해석' 5개 항목 나열 후 결론, 실행 명령 없음
step04_crash_test.md:56-62 — 유일하게 ```bash 블록(cmake/ctest/db_bench/db_crash_test) 보유
```

step01~03은 '검증 기준'을 산문 목록으로만 제시하고, 그 기준을 어떤 명령으로 확인해 어떤 출력이 나와야 하는지를 적지 않는다. step04만 완료 기준 5번을 충족한다. 독자가 각 단계 종료 시점의 상태를 스스로 관측할 방법이 문서에 없다.

**방향**: step01·02는 해당 시점까지의 db_tests 부분 실행 명령과 기대 출력(예: "db_tests: all passed")을, step03은 `./build/Release/db_bench` 실행과 표 형태 출력 예시(자기 장비의 실측 수치와 장치 종류 명기)를 결론 직전에 넣어라.

### 11. [중간] 비정상 길이 필드 방어(kMaxFrame)가 무검증 — 가드를 제거해도 전체 테스트 통과

<sub>분류: 검증력부족</sub>

**근거**

```text
src/wal_kv.hpp:154 `            if (len == 0 || len > kMaxFrame) break;                 // 쓰레기 길이 — 잘림 취급`
step01_wal_format.md:72 "4. 비정상 길이가 메모리 과다 할당을 유발하지 않는다."
변이 M2(복사본): 위 줄을 `if (len == 0) break;` 로 교체 → clean 재빌드 후
  db_crash_test.exe → "crash sweep: 633 trials, 0 failures"
  db_tests.exe      → "db_tests: all passed"
```

어떤 테스트도 길이 필드를 거대한 값으로 손상시키지 않는다. db_crash_test는 절단만 하므로 잘린 헤더는 header_bytes != 8 또는 payload 부족으로 걸러지고 kMaxFrame 경로에 도달하지 않는다. step01 검증 기준 4번은 실제 테스트에 없다.

**방향**: db_tests에 '유효 커밋 1개 뒤에 len = 0xFFFFFFFF, crc = 0인 8바이트 헤더를 덧붙인 로그' 케이스를 추가하고, Replay가 committed_txns == 1을 반환하며 discarded_bytes > 0인지, 그리고 과다 할당 없이 즉시 반환하는지 확인하라.

### 12. [낮음] 633 trial 크래시 스윕이 CRC 경로를 한 번도 실행하지 않음에도 문서는 'CRC 조합'을 검사한다고 서술

<sub>분류: 검증력부족</sub>

**근거**

```text
articles/step04_crash_test.md:27 "각 절단본을 Replay하면 파서·CRC·Commit 마커의 조합이 다양한 경계를 견디는지 확인할 수 있다."
변이 M6(복사본): src/wal_kv.hpp:157의 CRC 검사문을 통째로 삭제 → clean 재빌드 후
  db_crash_test.exe → "crash sweep: 633 trials, 0 failures"  (스윕은 전혀 감지 못함)
  db_tests.exe      → "db_tests: 3 check(s) failed"           (단위 테스트만 감지)
```

절단만 주입하면 잘린 프레임은 길이·본문 부족 검사에서 먼저 걸러지므로 CRC 불일치가 발생하지 않는다. CRC를 검사하는 것은 단위 테스트 1건뿐이며, 크래시 스윕은 '파서 + Commit 마커' 조합만 검사한다. 문서의 서술이 스윕의 실제 커버리지를 넘어선다.

**방향**: step04 §1의 문장을 '파서와 Commit 마커의 조합'으로 정정하거나, 스윕에 바이트 flip 주입 축을 추가해 문서 서술을 사실로 만들어라(후자를 택하면 위 M9 항목도 함께 해소된다).

---

## 반영 상태 (2026-08-13)

이 절은 위 166건이 **2026-08-13 반영 작업 뒤 어떻게 되었는지**를 기록한다.

- 판정 근거는 `VeLog_backup_2026-08-11/`(검수 직후 원본)과 현재 트리 사이의
  `diff -ru --strip-trailing-cr projects/mini` 다. diff에서 대응 변경을 찾지 못한 항목은
  해당 파일의 **현재 상태를 직접 열어** 확인했다.
- 백업본은 `.md/.cpp/.h/.hpp/.cs/CMakeLists.txt`만 담고 있다. 그래서 `*.sh`·`*.csproj`·
  `build*/`·`*.wal`이 "Only in current"로 뜨는 것은 변경이 아니라 백업의 한계이므로 집계에서 제외했다.
- 판정은 **파일의 현재 상태**로 했다. 빌드·ctest를 다시 돌려 확인한 것은 아니다(§확인하지 못한 것).

### 집계

| lab | 지적 | 반영됨 | 기반영 | 기각 | 이월 |
|---|---:|---:|---:|---:|---:|
| `parts_perf_lab` | 15 | 14 | 0 | 0 | 1 |
| `languages_lab` | 12 | 12 | 0 | 0 | 0 |
| `system_kernel_lab` | 13 | 8 | 0 | 0 | 5 |
| `math_lab` | 17 | 17 | 0 | 0 | 0 |
| `physics_lab` | 14 | 13 | 0 | 0 | 1 |
| `ai_lab` | 12 | 12 | 0 | 0 | 0 |
| `graphics_gl_lab` | 13 | 13 | 0 | 0 | 0 |
| `graphics_dx_lab` | 16 | 15 | 0 | 0 | 1 |
| `graphics_vk_lab` | 16 | 16 | 0 | 0 | 0 |
| `game_network_lab` | 11 | 11 | 0 | 0 | 0 |
| `live_service_lab` | 15 | 14 | 0 | 0 | 1 |
| `db_lab` | 12 | 12 | 0 | 0 | 0 |
| **합계** | **166** | **157** | **0** | **0** | **9** |

**개별 지적에 표시가 없으면 반영됨이다.** 이월 9건과, 반영됐지만 잔여가 남은 항목에만
`> **[이월 · 2026-08-13]**` 또는 `> **[반영됨 · 잔여 있음 · 2026-08-13]**` 줄을 지적 바로 아래에 달았다.

- **기반영 0건.** "보고서 작성 뒤 누군가 이미 본문에 반영해 두었다"는 사례는 `projects/mini`에서
  한 건도 없었다. 166건 전부가 이 보고서를 쓴 시점의 상태 그대로였고, 그것을 바꾼 것이 이번 반영이다.
  본문의 인용문이 현재 파일에서 검색되지 않는다면 **이번 반영이 그 줄을 바꾼 것**이지
  보고서가 낡았던 것이 아니다.
- **기각 0건.** 이 보고서에는 "검증에서 기각된 것" 류의 절이 없다. 고치면 안 되는 것으로 분류된
  항목은 없었다.
- 반영의 성격은 대부분 **검증력 보강**이었다. 12개 lab 중 9개에서 테스트 소스가 크게 다시 쓰였고
  (`math_lab/tests/core_tests.cpp` +858줄, `live_service_lab` 테스트 +749줄,
  `graphics_dx_lab` 계약 테스트 +796줄, `graphics_vk_lab` +662줄), 계산 코어를 GUI에서 떼어내
  headless로 돌리기 위한 새 파일이 생겼다(`ai_lab/src/model/{steering,flow_field,influence}.h`,
  `math_lab/src/algo/{mat,depth,kahan}.h`, `parts_perf_lab/src/sim/*_draw.cpp`,
  `graphics_vk_lab/src/{scene/scene_core.cpp,sync/frame_completion.cpp,measure/query_ring.cpp,device/swapchain_select.cpp}`,
  `graphics_dx_lab/src/scene/scene_record.cpp`). CTest 타깃도 늘었다 —
  `physics_lab` 2 → 6, `game_network_lab` 2 → 4, `db_lab` 2 → 3, `graphics_gl_lab` 1 → 2.

### 이월 — 아직 남은 일 (9건)

> **이 목록은 2026-08-13 시점이다.** 9건 중 4건(6·7·8·9번)은 2026-08-14에 처리됐다.
> 각 항목 끝에 `→ [2026-08-14]` 줄로 결과를 적었고, 최신 상태는
> [2차 반영 (2026-08-14)](#2차-반영-2026-08-14) 절에 있다.

이 절만 읽어도 무엇이 남았는지 알 수 있도록 적는다. 앞의 `lab #번호`를 따라가면 원래 근거가 있다.

1. **`system_kernel_lab` #1 [높음] — 부분 읽기·EINTR·취소 자동 검증 부재**
   `tests/system_core_tests.cpp`는 이번 작업에서 변경되지 않았다. `main`은 여전히
   `TestIoBackends` / `TestSyncPrimitives` / `TestWorkloadExecutionModels` 셋만 호출한다.
   **남은 일**: 파이프/FIFO를 소스로 받는 테스트 전용 경로(부분 읽기 강제), 별도 스레드에서
   `pthread_kill(tid, SIGUSR1)`로 EINTR 주입, aio/uring의 취소 CQE 수확 불변식 검사.
   최소선은 `fileBytes`보다 작은 파일로 short-read 경로를 밟게 하는 것.
   **이월 사유**: Linux 전용 코드 신설이 필요한데 반영 환경에 WSL·POSIX 툴체인이 없어
   작성해도 실행 검증이 불가능했다.
   → **[2026-08-14] 여전히 이월.** `tests/system_core_tests.cpp`는 백업본과 **바이트 단위로 동일**하다.
   Linux 환경이 붙기 전에는 착수 자체가 무의미하다.

2. **`system_kernel_lab` #2 [높음] — 실패 주입 수단 부재**
   `src/` 어디에도 fault injection seam이 없다. io_uring 런타임 폴백, aio의 cancel+수확 경로,
   uring 링 재초기화, mmap fstat 가드, sched 권한 실패 폴백, cgroup 쓰기 실패·복구가 모두 미검증.
   **남은 일**: `FaultingBackend` 데코레이터(첫 N회 성공 후 지정 errno) 또는 `SKL_FAULT=` 환경변수
   seam + `/proc/self/fd` 개수 비교로 fd 누수 검사.
   **이월 사유**: #1과 같다.
   → **[2026-08-14] 여전히 이월.** `src/`에 fault injection seam은 아직 없다. #1과 같은 사유.

3. **`system_kernel_lab` #3 [높음] — CTest 미실행, backend 폴백 시 조용한 통과**
   `VERIFY.md`의 `system_kernel_lab` 행은 여전히 "현재 환경에서 미검증"이다.
   **남은 일**: (a) 런타임 폴백이 일어나면 테스트를 실패시키거나 별도 exit code/ctest 라벨로 구분,
   (b) 어느 backend가 실제로 실행됐는지 항상 요약 출력, (c) `TestWorkloadExecutionModels`를
   `BackendCompiledIn`인 모든 backend × {pthread, coroutine}로 확장, (d) WSL 또는 CI에서 Linux 실행 1회.
   **이월 사유**: (d)가 다른 환경을 요구한다.
   → **[2026-08-14] 여전히 이월.** `projects/mini/VERIFY.md:46`의 `system_kernel_lab` 행은 지금도
   "현재 환경에서 미검증 / CTest 1개 정의, Linux 실행 미검증"이고, :29는 이 lab을
   `check_projects.ps1`에서 제외한다고 명시한다. 2026-08-14의 "mini 11개 lab 클린 재빌드"에서
   **빠진 한 lab이 바로 이것**이다(12개 중 11개).

4. **`system_kernel_lab` #8 [중간] — 시그널·io_uring 등록 자원·취소·IPC 대응 없음**
   **남은 일**: backend에 `registered files/buffers` 토글을 추가하면 part14의 등록 자원 주제와
   "조작 변수" 요구를 동시에 만족한다. 시그널·파이프는 새 backend가 아니라 테스트 전용 소스로
   도입해도 #1의 검증 공백과 함께 메워진다.
   **이월 사유**: 기능 증설.
   → **[2026-08-14] 여전히 이월.** 기능 증설분은 그대로다. 다만 이 lab의 **문서 쪽**은 이번에도
   손댔다 — step02·03·04 세 문서에 남아 있던 드리프트 허용 문장("디스크의 통합 코드는 누적되어
   더 진행된 상태일 수 있다")과 후속 step 예고 표현을 제거했다. 코드·테스트는 미착수.

5. **`system_kernel_lab` #10 [중간] — 재현 가능한 측정 경로 없음**
   `src/main.cpp`는 여전히 `argc`/`argv`를 무시하고, 측정값을 파일로 남기는 코드가 없다.
   **남은 일**: `--backend= --workers= --cycles= --warmup= --out=run.csv` 형태의 headless 러너
   실행 파일을 추가한다. `FileIoWorkload`+`FrameTimer`가 이미 SDL2/ImGui와 분리돼 있어
   (`system_core_tests` 타깃이 증거) 새 타깃 하나면 된다.
   **이월 사유**: 새 실행 파일 신설.
   → **[2026-08-14] 여전히 이월.** `src/main.cpp`는 백업본과 **바이트 단위로 동일**하다
   (`argc`/`argv` 무시, 측정값 파일 출력 없음). headless 러너 미신설.

6. **`parts_perf_lab` #12 [중간] — 스니펫 생략·드리프트 허용 선언·검증 장치 부재**
   반영된 것: step02의 토글 패널 생략 블록, step04의 SSE/AVX2 잔여 루프 생략 블록을 전문으로 채웠다.
   **남은 일**: (a) `articles/step07_container.md:270`의
   `// ... (기존 시뮬, allocator, EventBenchmark 초기화)` 생략을 채운다.
   (b) 여섯 문서(step02:15, step03:17, step04:15, step05:13, step06:15, step07:15)가 공유하는
   드리프트 허용 문장 "디스크의 통합 코드는 누적되어 더 진행된 상태일 수 있다"를 걷어낸다.
   (c) 스니펫-소스 대조 장치를 둔다(아래 공통 잔여 참조).
   **이월 사유**: (c)가 저장소 도구 신설이라 lab 하나의 범위를 넘는다.
   → **[2026-08-14] 해소.** (a) 생략 블록을 실제 소스로 채웠고, (b) 드리프트 허용 문장을 여섯 문서
   전부에서 걷어냈으며, (c) `scripts/check_snippets.py`가 신설돼 이 lab 앵커 13개를 지킨다.

7. **`physics_lab` #10 [중간] — Step 8 종합 측정이 Step 6 응답 솔버를 재구현**
   `measure_scene.cpp`의 `StepSim`은 여전히 접촉 수집·Sequential Impulse·위치 보정을 자체 구현하고,
   유효질량을 리터럴 `2.0f`(body-body, :148-149) / `1.0f`(벽, :157-158)로 박아 둔다.
   `response_scene`의 `GatherContacts` / `ApplySequentialImpulse` / `ApplyPositionCorrection`은 별개다.
   즉 결정성 측정이 재는 SI는 Step 6에서 사용자가 튜닝하는 그 SI가 아니다.
   **남은 일**: 세 함수를 `src/core/` 또는 `src/response/`의 공유 함수로 올리고 두 Scene이 모두 호출하게 한다.
   **이월 사유**: 구조 개편. 다만 두 경로 각각에는 이번에 자동 테스트가 붙었다
   (`physics_response_tests`, `physics_determinism_tests`).
   → **[2026-08-14] 해소.** `src/core/contact_solver.h`(248줄, 신설)로 통합. **지적은 1건이었으나
   실제 차이는 7군데**였고(유효질량 리터럴 2 + 임펄스 `* invMass` 누락 4 + 위치 보정 갱신 순서 1),
   `invMass = 1` 워크로드라 일곱 군데 전부가 숫자로는 무해했다. 통합 직전 실측 차이는 240 스텝 뒤
   위치 0.557 px / 속도 4.77 px/s, 통합 후 비트 일치. `TestMeasureRunsTheResponseSolver`로 고정.

8. **`graphics_dx_lab` #10 [중간] — 문서의 코드만으로는 어떤 step도 빌드 불가**
   개별 드리프트(step03 루트 상수 오프셋 0/4/20, step07 Flush, LOC 비교 등)는 고쳐졌지만
   구조적 격차는 그대로다 — 7편 어디에도 `D3D12Context` 코드·`CMakeLists.txt`·진입점·셰이더 전문이 없고,
   step별 "이 step에서 추가/변경한 파일 목록"도 없다. ctest 명령이 들어간 step은 7편 중 3편이다.
   **남은 일**: 각 step에 변경 파일 목록 + 핵심 전문 스니펫을 싣고, 마커 주석 기반 추출·diff 검사를 붙인다.
   **이월 사유**: 문서 대량 증설 + 도구 신설.
   → **[2026-08-14] 부분 해소.** 7편 전부에 `## 이 step 이 추가·변경하는 파일` 표가 생겼고, 본문이
   815 → 2,801줄로 늘었으며, 앵커 85개가 붙었다. **남은 것 둘**: 테스트 대역 1,301줄
   (`d3d12_stubs.h` 442 + `graphics_dx_contract_tests.cpp` 859) 미게재 — 실린 앵커는
   `#TestRootConstantBudget` 하나뿐이다. 그리고 고아 PNG
   `articles/diagrams/step01_skeleton_diagram_01.png` 1개(참조 0건).

9. **`live_service_lab` #13 [낮음] — 스니펫 검증 장치 부재**
   step01(lock)·step04(`RecordedResult`)·step05(`/results` 핸들러)의 개별 스니펫은 실제 코드로 교체됐다.
   **남은 일**: `<!-- snippet: 파일경로:시작-끝 -->` 앵커와 `check_project_docs.py`의 원문 대조 단계.
   **이월 사유**: 저장소 도구 신설.
   → **[2026-08-14] 해소.** 앵커 16개(region 4 / 심볼 12). 켜자마자 **드리프트 12건**이 드러났고
   **전부 문서 쪽 오류**였다 — bare `new GamePayload(...)`(CS0201로 컴파일 불가), 존재하지 않는
   식별자 다수. `.cs` 소스 변경은 마커 주석 8줄이 전부다.

### 반영됐지만 잔여가 남은 것

집계에서는 "반영됨"으로 셌다. 주된 지적은 해소됐지만 방향에 적힌 것 중 일부가 남았다.

| 항목 | 남은 것 |
|---|---|
| `parts_perf_lab` #7 | README 축 이름은 `branchless predication`으로 정정됐으나, 정렬/랜덤 분기 패턴 토글은 미구현이고 파일명 `step05_branch_sort.md`도 그대로 |
| `parts_perf_lab` #9 · `math_lab` #13 · `physics_lab` #9 · `graphics_vk_lab` #9 | 문서의 후속 step 예고는 전량 제거됐으나, `scripts/check_project_docs.py`의 `FORWARD_PROMISE_RE`에 `이후\s*단계` · `Step\s*\d+` · `스텝`이 추가되지 않아 **가드는 여전히 이 표현들을 못 잡는다** |
| `physics_lab` #5 | README 매핑에서 physics/09가 빠지고 비범위가 명시됐으나, `PROJECTS_PLAN.md:37`은 여전히 "접촉·**마찰**·제약 불변식" |
| `physics_lab` #9 | `src/scenes/forces_scene.cpp:109` 주석에 "Step 6 에서 본격적인 응답 비교" 예고가 남음 |
| `system_kernel_lab` #4 | step08 §9가 신설돼 `CreateBackend` out 파라미터·워커 예외 전파·테스트 타깃이 문서화됐으나, short-read throw / mmap fstat 가드 / uring `idx >= n` 방어 / `fileBytes > UINT_MAX` 가드는 여전히 문서 0건이고 `tests/system_core_tests.cpp` 전문도 없음 |
| `languages_lab` #8 · `math_lab` #11 · `physics_lab` #7 · `ai_lab` #11 · `game_network_lab` #6·#10 · `db_lab` #9 | 스니펫 자체는 실제 소스와 일치하게 교체됐으나, **재발을 막을 대조 장치는 없음** |

**공통 잔여 둘.** (1) `PROJECTS_PLAN.md` step 완료 기준 7번이 요구하는 **스니펫-소스 대조 장치가
저장소 전체에 없다** — `scripts/`에는 `check_project_docs.py`(헤딩·구분선·전방 참조 정규식),
`check_links.py`, `check_style.py`, `check_projects.ps1`, `check_live_mega.ps1`만 있다.
(2) `check_project_docs.py`의 전방 참조 정규식이 이번에 정리된 표현들(`이후 단계`, `Step N`)을
여전히 인식하지 못한다. 문서를 다시 손대면 같은 위반이 통과된다.

> **[2026-08-14] 공통 잔여 둘 다 닫혔다.** 자세한 내용과 이 장치가 어느 항목의 재발을 막는지는
> 바로 아래 [2차 반영](#2차-반영-2026-08-14) 절의 "저장소 공통 2건" 항에 있다. 위 표에서
> "대조 장치는 없음"·"가드가 못 잡는다"로 적힌 행들은 이제 **장치가 생겼다**로 읽어야 한다.

### 확인하지 못한 것

- **실행 검증은 하지 않았다.** 이 절의 판정은 파일 내용 대조이지 빌드·`ctest` 재실행 결과가 아니다.
  새로 추가된 테스트가 실제로 통과하는지, 문서에 적힌 기대 출력이 실제 출력과 같은지는
  각 lab을 빌드해 확인해야 한다.
- **`system_kernel_lab`은 Linux 전용**이라 반영 환경에서 빌드조차 하지 못했다. 이 lab의
  이월 5건은 그래서 "미착수"가 맞지만, 반영됨으로 분류한 8건(문서 수정과
  `file_io_workload.cpp`의 `remaining_` 순서 수정)도 컴파일 확인이 되지 않은 상태다.
- **변이 테스트를 다시 돌리지 않았다.** 이 보고서의 검증력 지적은 변이 주입으로 얻은 것인데,
  반영 뒤 그 변이들이 실제로 죽는지는 재확인하지 않았다. 새 테스트의 서술(예: `math_lab`
  step08의 "변이 주입으로 확인했다", `physics_lab` step05의 "옛 식으로 되돌리면 65개 CHECK가
  깨진다")은 반영 작업자의 주장이며 이 절이 독립적으로 확인한 것은 아니다.

---

## 2차 반영 (2026-08-14)

앞 절이 남긴 **이월 9건 중 4건**을 처리하고, 그 4건이 공통으로 기다리던 **저장소 공통 2건**을
닫은 기록이다. 판정 근거는 같다 — `VeLog_backup_2026-08-11/` 대비
`diff -ru --strip-trailing-cr`(`-x "REVIEW_*" -x diagrams -x build`)와 파일의 현재 상태.
집계 수치는 실제로 도구를 돌려 얻은 것이며, 출처는 각 항에 적었다.

### 갱신된 집계

| lab | 지적 | 08-13 이월 | 08-14 결과 |
|---|---:|---:|---|
| `parts_perf_lab` | 15 | 1 | #12 **완료** |
| `physics_lab` | 14 | 1 | #10 **완료** |
| `live_service_lab` | 15 | 1 | #13 **완료** |
| `graphics_dx_lab` | 16 | 1 | #10 **부분 처리** — 잔여 2개(§2) |
| `system_kernel_lab` | 13 | 5 | **5건 전부 이월 유지** — Linux 필요(§3) |
| 나머지 7개 lab | 93 | 0 | — |
| **합계** | **166** | **9** | **완료 3 · 부분 1 · 이월 유지 5** |

전체로 보면 **반영됨 160 · 부분 반영 1 · 이월 5**다. 이월 5건은
**전부 `system_kernel_lab`이고 전부 Linux를 요구한다.** 아래 §3 참조.

### 1. 저장소 공통 2건 — 닫힘

이 둘이 이월의 진짜 병목이었다. `parts_perf_lab` #12(c), `graphics_dx_lab` #10,
`live_service_lab` #13이 전부 "저장소 도구 신설이 필요해서" 미뤄진 것이었기 때문이다.

**(1) `scripts/check_snippets.py` 신설 (796줄).** 문서의 코드 펜스를 실제 소스와 대조한다.
사용법은 `scripts/SNIPPETS.md`(139줄)에 있다. 설계의 핵심은 **opt-in**이다 — 모든 펜스를
대조하면 안 되기 때문이다. step 문서의 스니펫 상당수는 *그 step 시점의 스냅숏*이고 디스크의
코드는 더 진행돼 있다. 그래서 저자가 "이 펜스는 지금 소스와 같다"고 주장할 때만 앵커를 달고,
**앵커를 다는 순간 그 주장을 검사기가 지킨다.**

대상 지정은 네 가지이고 낡음에 대한 내성이 다르다.

| 문법 | 의미 | 내성 |
|---|---|---|
| `src/platform.h` | 파일 전체 | 파일이 사라지면 error |
| `src/sim/soa.cpp@sse-hot-loop` | 소스의 `// snippet-begin:` region 마커 | **가장 강함** — 마커가 코드와 함께 움직인다 |
| `src/sim/soa.cpp#UpdateSSE` | 심볼 정의 블록 | 줄 이동에 면역, 이름이 바뀌면 error |
| `src/sim/soa.cpp:107-146` | 줄 범위 | **가장 약함** — 항상 WARN |

생략(`// ...`)·주석 번역·들여쓰기 차이는 기본 허용하고, **식별자·시그니처·리터럴·제어흐름의
차이만 잡는다.** 이것이 이 보고서에서 반복 지적된 "문서가 존재하지 않는 식별자를 쓴다" 부류를
정확히 겨냥한 지점이다.

`check_project_docs.py`가 이것을 `import`해(:11, :181) **단일 진입점**이 됐고,
`PROJECTS_PLAN.md`의 완료 기준 2번·7번이 각각 이 두 검사를 명시적으로 가리킨다.

현재 상태 — `python scripts/check_project_docs.py`:

```text
Checked 80 project documents (118 snippet anchor(s)): 0 error(s), 0 warning(s).
```

`python scripts/check_snippets.py --stats`:

```text
앵커 종류: file 12, region 28, symbol 78
```

**줄범위 앵커는 0개다.** 가장 약한 형태를 하나도 쓰지 않았다는 뜻이고, 그래서 warning도 0이다.
lab별 분포는 `graphics_dx_lab` 85 · `live_service_lab` 16 · `parts_perf_lab` 13 ·
`physics_lab` 4 = 118.

> 참고: 작업 중 메모에는 `parts_perf_lab` 앵커가 11개로 적혀 있으나, 그것은
> `step07_container.md` 한 편의 수다. `check_snippets.py`로 lab 전체를 세면
> `step04_simd.md`의 2개를 더해 **13개**다. 이 보고서는 도구 출력을 따른다.

**이 장치가 재발을 막는 항목.** 아래 표의 지적들은 전부 "문서 스니펫이 소스와 다르다"였고,
2026-08-13에 스니펫 자체는 고쳤지만 **재발을 막을 수단이 없다**고 기록됐던 것들이다.

| 항목 | 원래 지적 | 지금 |
|---|---|---|
| `parts_perf_lab` #12 | 생략 블록 + 드리프트 허용 선언 + 장치 부재 | 앵커 13개가 지킴 |
| `graphics_dx_lab` #10 · #4 | 문서 코드로 빌드 불가, 루트 상수 오프셋 1/17 vs 4/20 | 앵커 85개가 지킴 |
| `live_service_lab` #13 · #8 · #9 · #12 | lock·`RecordedResult`·`/results` 스니펫이 컴파일 불가 | 앵커 16개가 지킴 |
| `physics_lab` #7 · #8 | step04 본문 스니펫이 결론·실제 코드와 3곳 불일치 | 앵커 4개가 지킴 |
| `languages_lab` #8 · `math_lab` #11 · `ai_lab` #11 · `game_network_lab` #6·#10 · `db_lab` #9 | 스니펫이 존재하지 않는 식별자·시그니처 사용 | **아직 앵커 0개** — 장치는 생겼으나 이 lab들은 아직 붙이지 않았다 |

마지막 행이 정직한 한계다. 장치가 있다는 것과 그 lab이 장치를 켰다는 것은 다르다.
`ai_lab`·`db_lab`·`game_network_lab`·`graphics_gl_lab`·`graphics_vk_lab`·`languages_lab`·
`math_lab`·`system_kernel_lab` 8개 lab은 앵커가 0개이므로, 그 lab들의 스니펫 드리프트는
여전히 사람이 봐야 잡힌다.

**(2) `FORWARD_PROMISE_RE` 보강.** 완료 기준 2번(후속 step 예고 금지)의 가드가 이 보고서가
정리한 표현들을 못 잡던 문제다 — `parts_perf_lab` #9 · `math_lab` #13 · `physics_lab` #9 ·
`graphics_vk_lab` #9 · `system_kernel_lab` #12가 전부 같은 구멍을 지적했다. 추가된 규칙은 셋이다.

- `(?:이후|추후)\s*(?:[^.\n]{0,25}?\s)?단계(?:는|가|를|의|에서|에|까지|부터)` — **조사를 요구해**
  회고적 표현과 가른다. `이후 BVH 빌드 단계 시각화` 같은 조사 없는 명사 수식은 예고가 아니므로 일부러 뺐다.
- `(?:다음|이후|후속|추후)\s*(?:스텝|step)\b` — 영어 단위 `Step`을 잡는다. 이것이
  `physics_lab` #9가 "저장소 검사기가 'Step'이라는 영어 단위를 인식하지 못해 통과한다"고
  지적한 바로 그 지점이다.
- `STEP_NUMBER_RE` + `OWN_STEP_RE` — **자기 step 번호와 대조**한다. `Step 4 의 토픽`(예고)과
  `Step 2 에서 만든 것`(회고)은 문법이 아니라 번호로 갈리기 때문이다. 자기 번호보다 큰 숫자를
  가리키면 아직 없는 글이므로 error다.

**보강 첫 실행에서 14건이 걸렸고, 그중 8건이 오탐이었다.** 오탐을 먼저 제거했다 —
기존의 `~` 제거 코드가 `Step 1~7`을 `Step 17`로 만들어 버리던 버그가 하나였고, 나머지는
세는 말(`step 4번 모이면`)이었다. 그래서 지금 부정 전방탐색이
`(?!\s*(?:번|회|개|차례|차|줄|배|초|분|시간|프레임|픽셀|바이트|ms|Hz))`로 들어가 있다.
남은 **진짜 6건은 전부 수정**했고, 현재 0 error다.

### 2. 처리된 이월 3건 + 부분 1건

각 항목의 자세한 결과는 본문 지적 아래 `> **[반영됨 · 2026-08-14]**` 줄에 있다. 요약만 둔다.

| 항목 | 결과 | 한 줄 |
|---|---|---|
| `parts_perf_lab` #12 | 완료 | 생략 블록을 소스로 채움 · 드리프트 허용 문장 **6개 문서에서 제거** · 앵커 13개 |
| `physics_lab` #10 | 완료 | `src/core/contact_solver.h`로 통합. **지적 1건 → 실제 차이 7군데.** 통합 전 240 스텝 뒤 위치 0.557 px / 속도 4.77 px/s → 통합 후 **비트 일치** |
| `graphics_dx_lab` #10 | **부분** | 7편에 변경 파일 표 신설 · 본문 815 → 2,801줄 · 앵커 85개. **테스트 대역 1,301줄 미게재 + 고아 PNG 1개** |
| `live_service_lab` #13 | 완료 | 앵커 16개. 켜자 **드리프트 12건**이 드러났고 **전부 문서 쪽 오류** |

**`physics_lab` #10이 남긴 교훈을 따로 적어 둔다.** 이 보고서의 지적은 "차이 3갈래"였는데
통합해 보니 7군데였다. 놓친 넷은 **임펄스 적용식에서 `* invMass`가 빠져 있던 것**이다.
그런데 이 워크로드는 `invMass = 1`이라 일곱 군데 전부가 *숫자로는* 무해했다 — 값이 맞아서가
아니라 가정이 우연히 성립해서다. 검수가 정적 대조로 세 갈래까지 짚었지만 나머지 넷은
**실제로 통합해 두 경로를 나란히 돌려 보기 전에는 드러나지 않았다.** "지적 건수 = 결함 건수"가
아니라는 사례로 남긴다.

`graphics_dx_lab`을 **부분**으로 판정한 근거는 둘 다 실측이다.

- `wc -l tests/*` → `d3d12_stubs.h` 442 + `graphics_dx_contract_tests.cpp` 859 = **1,301줄**.
  문서에 실린 테스트 앵커는 `step04_rootsig.md:174`의 `#TestRootConstantBudget` **1개**뿐이고,
  나머지는 산문 서술과 CMake 타깃 정의로만 나온다. 완료 기준 4를 제품 코드에 대해서는
  충족했지만 테스트 대역에 대해서는 충족하지 못한 상태다. 이 lab의 #1이
  "테스트가 제품 .cpp를 하나도 링크하지 않는다"였던 만큼 가벼운 잔여가 아니다.
- `articles/diagrams/step01_skeleton_diagram_01.png`가 `articles/`·`README.md` 어디에서도
  참조되지 않는다(참조 0건). step01이 114 → 1,044줄로 다시 쓰이면서 그림을 부르던 자리가
  사라졌는데 파일만 남았다.

### 3. `system_kernel_lab` 5건 — 여전히 Linux 필요

**이월 5건은 상태가 바뀌지 않았다.** 착수하지 않은 것이 아니라 **착수할 수 없다.**

| 이월 | 지적 | 현재 상태 |
|---|---|---|
| #1 [높음] | 부분 읽기·EINTR·취소 자동 검증 부재 | `tests/system_core_tests.cpp`가 백업본과 **바이트 단위로 동일** |
| #2 [높음] | 실패 주입 수단 부재 | `src/`에 fault injection seam 없음 |
| #3 [높음] | CTest 미실행, backend 폴백 시 조용한 통과 | `projects/mini/VERIFY.md:46` = "현재 환경에서 미검증" |
| #8 [중간] | 시그널·io_uring 등록 자원·취소·IPC 대응 없음 | 기능 증설분 미착수 |
| #10 [중간] | 재현 가능한 측정 경로 없음 | `src/main.cpp`가 백업본과 **바이트 단위로 동일** |

배경을 분명히 해 둔다. 이 lab의 지적 13건 중 **문서 성격의 8건은 이미 반영됐고**(2026-08-13),
남은 5건은 **전부 검증력** 항목이다. 즉 "문서는 정리됐는데 그 문서가 참이라고 확인해 줄
실행 환경이 없다"가 지금의 정확한 상태다. 2026-08-14에도 이 lab의 문서는 손댔다 — step02·03·04
세 문서에서 드리프트 허용 문장과 후속 step 예고 표현을 제거했다. 그러나 `tests/`와 `src/main.cpp`는
그대로다.

`projects/mini/VERIFY.md:29`가 이 lab을 `check_projects.ps1`에서 제외한다고 명시하고 있고,
그래서 2026-08-14의 "mini 클린 재빌드"도 **12개 중 11개 lab**에서만 돌았다. 빠진 하나가 이것이다.

**필요한 것은 하나뿐이다** — WSL 배포판 또는 별도 Linux 환경 1회. 그것이 붙기 전에는
5건 모두 "코드를 써도 실행 검증이 불가능"하므로 이월 상태를 유지한다.

### 4. 이번 절이 확인한 것과 확인하지 못한 것

**확인한 것.**

- `python scripts/check_project_docs.py` → 80개 문서 / 앵커 118개 / **0 error 0 warning** (직접 실행).
- 앵커 종류별 분포와 lab별 분포 (`check_snippets.py --stats`, 직접 실행).
- 백업본 대비 diff로 파일 신설·삭제·동일 여부 (`contact_solver.h` 신설,
  `system_kernel_lab`의 `tests/`·`src/main.cpp` 동일, `.cs` 변경이 마커 주석뿐임).
- `graphics_dx_lab`의 본문 줄 수 증가와 테스트 1,301줄, PNG 참조 0건 (직접 계수).
- `TestMeasureRunsTheResponseSolver`가 `CHECK(worstPos == 0.0f)` / `CHECK(worstVel == 0.0f)`로
  비트 일치를 단언한다는 것 (소스 확인).

**확인하지 못한 것.**

- **이 절도 빌드·ctest를 다시 돌리지 않았다.** "mini 11개 lab 클린 재빌드 통과"와
  `physics_lab` ctest 6/6은 반영 작업자의 실행 결과이며, 이 절은 그 주장이 가리키는
  **파일 상태**만 대조했다.
- 문서에 실린 실측 수치(0.557 px / 4.77 px/s, shuffled maxDelta 0.700067 → 0.399962,
  평균 침투 0.6946 → 0.6965 등)는 반영 작업자가 통합 전후로 측정한 값이다. 이 절은 그 수치가
  문서와 소스에 일관되게 기록돼 있음을 확인했을 뿐 재측정하지 않았다.
- **변이 테스트는 이번에도 다시 돌리지 않았다.** 앞 절의 같은 한계가 그대로 유효하다.
