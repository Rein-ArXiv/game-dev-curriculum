# 잔여 시리즈 검수 — 색인 (2026-08)

`algorithms`, `parts`, `system`, `network`는 별도 보고서에서 완료.
나머지 이론 문서 **173편 / 약 98,000줄**을 시리즈별로 검수하고, **보고서는 각 폴더 안에** 둔다.

- 기준: **확정 가능한 사실오류·산술오류·컴파일 불가 코드·서지오류·내부모순**만. 문체·구성 취향은 대상 아님.
- 방법: 파일별 전문 정독 → 모든 수치·유도 재계산 → 서지 저자/연도/게재지/장절 대조 →
  코드 컴파일 가능성·논리 검증 → 문서 내부·문서 간 교차 대조.
- **검수(2026-08) 시점에는 지적만 했고 본문을 수정하지 않았다. 그 선언은 2026-08-13부로 끝났다** —
  확정 지적은 본문에 반영됐고, 각 보고서 끝의 `## 반영 상태 (2026-08-13)` 절이 무엇이 반영되고
  무엇이 남았는지를 기록한다. **보고서 본문의 인용문·행번호는 수정 전 원문 기준**이므로
  현재 파일에서 그대로 검색되지 않는 것이 정상이다.
- 보고서를 "미반영 지적 목록"으로 읽으면 안 된다. 남은 일은 아래 [남은 일](#남은-일) 절이 전부다.

## 시리즈별 보고서

`확정`은 검수 시점 기록이고, `반영 상태`는 각 보고서의 반영 상태 절에서 그대로 옮긴 값이다.

| 시리즈 | 편수 | 줄수 | 보고서 | 확정 | 반영 상태 (2026-08-13) |
|---|---:|---:|---|---:|---|
| math | 20 | 16,759 | [math/REVIEW_MATH_2026-08.md](math/REVIEW_MATH_2026-08.md) | **43**<sup>†</sup> | [반영 4 · 기반영 47 · 이월 **0**](math/REVIEW_MATH_2026-08.md#반영-상태-2026-08-13) |
| physics | 20 | 13,083 | [physics/REVIEW_PHYSICS_2026-08.md](physics/REVIEW_PHYSICS_2026-08.md) | **24** | [반영 7 · 기반영 17 · 이월 **1**](physics/REVIEW_PHYSICS_2026-08.md#반영-상태-2026-08-13) |
| graphics | 41 | 21,339 | [graphics/REVIEW_GRAPHICS_2026-08.md](graphics/REVIEW_GRAPHICS_2026-08.md) | **54**<sup>†</sup> | [반영 2 · 기반영 62 · 기각 5 · 이월 **0**](graphics/REVIEW_GRAPHICS_2026-08.md#반영-상태-2026-08-13) |
| ai | 26 | 17,323 | [ai/REVIEW_AI_2026-08.md](ai/REVIEW_AI_2026-08.md) | **106** | [반영 54 · 기반영 50 · 기각 28 · 이월 **2**](ai/REVIEW_AI_2026-08.md#반영-상태-2026-08-13) |
| languages | 8 | 5,018 | [languages/REVIEW_LANGUAGES_2026-08.md](languages/REVIEW_LANGUAGES_2026-08.md) | **32** | [반영 32 · 기반영 0 · 기각 9 · 잔여 **2**](languages/REVIEW_LANGUAGES_2026-08.md#반영-상태-2026-08-13) |
| engine | 10 | 5,417 | [engine/REVIEW_ENGINE_2026-08.md](engine/REVIEW_ENGINE_2026-08.md) | **22** | [반영 22 · 기반영 0 · 기각 11 · 잔여 **3**](engine/REVIEW_ENGINE_2026-08.md#반영-상태-2026-08-13) |
| live_service | 10 | 5,701 | [live_service/REVIEW_LIVE_SERVICE_2026-08.md](live_service/REVIEW_LIVE_SERVICE_2026-08.md) | **24** | [반영 24 · 기반영 0 · 기각 14 · 이월 **1**](live_service/REVIEW_LIVE_SERVICE_2026-08.md#반영-상태-2026-08-13) |
| platform | 7 | 4,428 | [platform/REVIEW_PLATFORM_2026-08.md](platform/REVIEW_PLATFORM_2026-08.md) | **22** | [반영 22 · 기반영 0 · 기각 7 · 이월 **1**](platform/REVIEW_PLATFORM_2026-08.md#반영-상태-2026-08-13) |
| db | 5 | 2,448 | [db/REVIEW_DB_2026-08.md](db/REVIEW_DB_2026-08.md) | **18** | [반영 18 · 기반영 0 · 기각 7 · 이월 **0**](db/REVIEW_DB_2026-08.md#반영-상태-2026-08-13) |
| tools | 5 | 3,018 | [tools/REVIEW_TOOLS_2026-08.md](tools/REVIEW_TOOLS_2026-08.md) | **22** | [반영 22 · 기반영 0 · 기각 8 · 잔여 **3**](tools/REVIEW_TOOLS_2026-08.md#반영-상태-2026-08-13) |
| base | 21 | 3,535 | [base/REVIEW_BASE_2026-08.md](base/REVIEW_BASE_2026-08.md) | **5** | [반영 5 · 기반영 0 · 이월 **1**](base/REVIEW_BASE_2026-08.md#반영-상태-2026-08-13) |

- **기반영** = 보고서 작성 뒤·이번 배치 전에 이미 본문이 고쳐져 있던 것. math·physics·graphics·ai
  네 시리즈에만 있고(합계 176건), 나머지 일곱 시리즈의 보고서는 낡아 있지 않았다.
- **기각** = 검증에서 오탐으로 판정된 것. **다시 미반영으로 세지 말고, 본문을 고치지도 마라.**
- **이월 / 잔여** = 아직 남은 일. 확정 건수에서 빠지는 값이 아니다 —
  대부분은 반영된 항목의 파생·부수 항목이다. 전부 아래 [남은 일](#남은-일)에 옮겨 적었다.
- <sup>†</sup> math·graphics는 각 보고서의 반영 상태 절이 **소절 단위로 다시 세어** math 51 ·
  graphics 64로 집계한다(math 43은 2026-08-09 기록, graphics 54는 파일별 선언 합계).
  반영 열의 숫자는 재집계 기준이므로 `확정` 열과 더해지지 않는다. ai 106 = 54 + 50 + 2 로 맞는다.

## projects/ (별도 취급 — 코드와 과정 문서)

| 대상 | 보고서 | 발견 | 반영 상태 (2026-08-13) |
|---|---|---:|---|
| mini lab 12개 | [projects/mini/REVIEW_MINI_2026-08.md](projects/mini/REVIEW_MINI_2026-08.md) | **166** | [반영 157 · 기반영 0 · 이월 **9**](projects/mini/REVIEW_MINI_2026-08.md#반영-상태-2026-08-13) + 변이 재주입 검증 완료(아래) |
| mega 통합 감사 | [projects/mega/REVIEW_MEGA_2026-08.md](projects/mega/REVIEW_MEGA_2026-08.md) | **32** | [P0로 닫힘 11 · 부분 4 · **미해소 17**](projects/mega/REVIEW_MEGA_2026-08.md#반영-상태-2026-08-13) |
| mega 재설계안 | [projects/mega/REDESIGN_MEGA_2026-08.md](projects/mega/REDESIGN_MEGA_2026-08.md) | 제안 | [**P0 완료 · P1~P9 미착수**](projects/mega/REDESIGN_MEGA_2026-08.md#반영-상태-2026-08-13) |

**173편 전부 검수 완료.** 미커버 없음.

## 교차 대조

| 대상 | 보고서 | 발견 | 반영 상태 (2026-08-13) |
|---|---|---:|---|
| math ↔ physics | [REVIEW_CROSS_MATH_PHYSICS_2026-08.md](REVIEW_CROSS_MATH_PHYSICS_2026-08.md) | **11** | [반영 11 · 기반영 0 · 이월 0(잔여 하위 **1**)](REVIEW_CROSS_MATH_PHYSICS_2026-08.md#반영-상태-2026-08-13) |
| graphics 내부 | [REVIEW_CROSS_GRAPHICS_2026-08.md](REVIEW_CROSS_GRAPHICS_2026-08.md) | **19** | [반영 19 · 기반영 0 · 기각 4 · 이월 **1**](REVIEW_CROSS_GRAPHICS_2026-08.md#반영-상태-2026-08-13) |
| base ↔ 심화 시리즈 | base 보고서에 포함 | 0 | — |

## 그 밖에 2026-08-13에 일어난 일

- **`ai/part20_ai_debugging.md` 재검수.** 보고서가 이 파일을 "확정 0건"으로 올려 두었으나
  같은 보고서에 6건이 「검증자 무응답」으로 남아 있었고 별도 절에는 확정 1건이 있었다(자기모순).
  적대적 재검수로 **후보 10건 → 확정 5건**(컴파일 불가 1 · 논리 오류 1 · 산술 오류 1 · 내부모순 2),
  기각 2건. 확정 5건은 전부 반영됐다(`NodeDebug`에 `breakpoint` 멤버 추가, Utility 트레이스 hp 값,
  링 버퍼 메모리 수치, `Flush`의 제출/만료 순서, 노드 breakpoint 서술).
  ai 보고서의 「확정 오류 0건」·「검증에서 기각 (ai 미커버 3편)」 두 절 제목은 사실과 다르며,
  보고서 안에 그 정정이 표시돼 있다.
- **`projects/mini` 검증력 보강.** 감사가 지적한 "테스트가 변이를 못 잡는다"를 닫았다.
  11개 lab에서 변이를 실제 주입해 검출을 확인 — `math_lab` 33/33, `parts_perf_lab` 28/28,
  `live_service_lab` 26/26 등. `system_kernel_lab` 5건은 Linux 전용이라 미착수.
  lab 전체 클린 재빌드 통과(테스트 35개).
- **`projects/mini/physics_lab` 제품 결함 수정.** `SATCollide`가 침투 깊이로 교집합 길이를
  쓰고 있었다(MTD 아님). 브루트포스 참값 대조로 79 hit 중 12건 오류·최대 오차 0.384를 확인한 뒤 정정.
  이론 문서 `math/part12_collision_sat.md`에서 이미 고쳤던 것과 **같은 결함**이다.
- **`projects/mega` P0 구조 게이트 완료.** 아래 [남은 일 · mega](#projectsmega--p1-이후-전부-미착수) 참조.

## 누적 집계 (2026-08)

### 검수 결과

| | 편수 | 확정 |
|---|---:|---:|
| 편별 검수 | **173 / 173** | **372** |
| 교차 대조 (math↔physics) | 5주제 | **11** |
| 교차 대조 (graphics 내부) | 5주제 | **19** |
| **합계** | | **402** |
| 미커버 | 0 | — |

시리즈별: ai 106 · graphics 54 · math 43 · languages 32 · physics 24 · live_service 24 ·
engine 22 · platform 22 · tools 22 · db 18 · base 5

### 반영 결과 (2026-08-13)

| 대상 | 반영됨 | 기반영 | 기각 | 이월·잔여 |
|---|---:|---:|---:|---:|
| 편별 검수 11개 시리즈 | **212** | **176** | **89** | **14** |
| 교차 대조 (math↔physics) | 11 | 0 | 0 | 1 |
| 교차 대조 (graphics 내부) | 19 | 0 | 4 | 1 |
| **이론 문서 합계** | **242** | **176** | **93** | **16** |
| `projects/mini` | 157 | 0 | 0 | 9 |
| `projects/mega` 감사 32건 | 11 닫힘 | — | — | 17 미해소 + 4 부분 |

읽는 법 — **반영됨 + 기반영 = 본문이 현재 정정안대로인 건수**, **기각은 문서가 옳았던 건수로
고치면 안 되는 것**, **이월·잔여만이 실제로 남은 일**이다. 이월은 확정 건수에서 빼는 값이 아니다
(대부분 반영된 항목의 파생·부수 항목이라 확정 목록 밖에서 따로 센다).

판정 근거는 전 보고서 공통으로 `VeLog_backup_2026-08-11/` 대비
`diff -ru --strip-trailing-cr` 이고, diff에 안 잡히는 건(=기반영)은 현재 본문을 직접 열어 확인했다.

---

## 남은 일

**다음에 이 저장소를 여는 사람은 이 절만 읽으면 된다.** 위의 보고서들은 이미 반영된 지적을
현재형으로 서술하고 있으므로 할 일 목록으로 쓰면 안 된다.

### 이론 문서 — 16건

전부 "반영된 항목의 파생·부수 항목"이거나 "설계 판단이 필요해 사실오류 정정 범위를 넘는 것"이다.
구조 개편이 필요한 것은 engine 1건뿐이고 나머지는 한 줄~한 문단 규모다.

| # | 대상 | 남은 일 | 규모 |
|---|---|---|---:|
| 1 | `physics/part13_joints.md` §7.3 (L560·L568·L582) | `frequency` 기본값이 0이라 L568 게이트 때문에 soft 모드가 켜지지 않는데 L582는 "1 Hz + 감쇠 0.5" 거동을 설명한다. 코드 기본값을 `1.0f`로 올리거나(설계 의도 변경) 본문을 조건부 서술로 고친다 | 설계 판단 |
| 2 | `ai/part20_ai_debugging.md` L445 | 원 지적문이 보고서에 없어 대조 불가. 인접한 breakpoint 계열 변경으로 해소됐을 가능성이 높다. 닫을지 재검수할지 판단 | 판단 |
| 3 | `ai/part20_ai_debugging.md` L378 | 원 지적문 없음 + 해당 대목(타임트래블 시퀀스 다이어그램 L375~383)은 **현재도 무변경**임을 확인했다. 확인할 지점: `At(12830)` 다음 `At(12825)`를 "한 틱 뒤로"라 부르는 것과 hpRatio 0.36→0.34 서술의 정합 | 재검수 |
| 4 | `languages/part05_jit_compilation.md` L594~604 | 컴파일 단계 주석은 고쳐졌으나 Recording 로그의 `i=1: is_prime(1) → false → skip` 줄과 "루프를 한 번 실행하며" 서술이 남음(로그는 i=1,2,3 세 반복) | 한 문단 |
| 5 | `languages/part07_reflection_metadata.md` L191~197 | 소비 측은 `prop->size` → `prop->type`으로 바뀌었으나 등록 측 `REGISTER_PROPERTY` 매크로는 여전히 `sizeof(Class::Member)`만 넘기고, `PropertyType` 열거형·`PropertyInfo::type` 정의가 문서에 없다 — 아무도 채우지 않는 필드를 읽는다 | 한 블록 |
| 6 | `engine/part05_resource.md` §2 | 중복 로드 카운터: 무연산 `+= 0` 줄 삭제(최소안)만 반영되고 실제 계수 지점 추가가 미반영. `duplicateLoadsThisSession`은 605행 선언 한 곳뿐이고 어디서도 증가하지 않아 591행 표의 감시 지표가 영구 0 | **구조 개편** |
| 7 | `engine/part03_vfx.md` §4 | TBDR 은면 제거 부연 추가 (오류 아님, 보강 제안) | 선택 |
| 8 | `engine/part02_animation.md` §1 | 0 나누기 방지 (검증 각주가 "부수 지적"으로 평가) | 선택 |
| 9 | `live_service/part08_matchmaking.md` L138 | "1절의 품질 $Q$"가 정의 없는 기호를 가리킨다. part08-3 반영으로 L32 좌변이 $Q_{balance}$가 되면서 스칼라 $Q$ 정의가 사라졌다. (a) L138을 "품질 벡터 $q$"로 문장 교정 또는 (b) 1절에 가중합 $Q=w\cdot q$ 신설(기능 증설) | 문장 1줄 |
| 10 | `platform/part07_windows_text.md` L487 | `LOCALE_SCOUNTRY` 표 값이 '대한민국'인데 검증 각주의 실측값은 '한국'이었다. 같은 표의 `LOCALE_SLANGUAGE` 행만 정정돼 검증/미검증 값이 섞임. `GetLocaleInfoEx(L"ko-KR", LOCALE_SCOUNTRY)` 재실측 후 판단 | 실측 1회 |
| 11 | `tools/part05_asset.md` L432 | `std::string rule; // "texture.power_of_two"` 예시 주석이 옛 규칙 id. 441행은 `texture.block_multiple`로 바뀜 | 1줄 |
| 12 | `tools/part06_build.md` L79 | "컴파일러가 실제로 읽은 파일 목록" — 90행은 "사용자 헤더"로 정정됐으나 79행은 그대로 | 1줄 |
| 13 | `tools/part04_debug.md` L390 | "관측(읽기)은 무해하지만" — part04 #5 검증 노트가 명시적으로 지목했으나 181행만 정정됨 | 1줄 |
| 14 | `base/part03_data_structures.md` L38·L55 | part01 L75 지적의 파생. 이 보고서가 part03을 "확정 오류 0건 16편"에 올려놓아 판정 충돌이 있고, L38은 표 한 칸이라 행 분리/조건 열 보강 중 표 구조 선택이 따른다 | 표 구조 판단 |
| 15 | `math/part12_collision_sat.md` §4.3 | 교차 대조 1번의 하위 항목. §3.3 깊이는 `min(push+, push−)`로 교정됐으나, §4.3은 여전히 `OverlapAmount`의 *크기*만 비교하고 MTV 부호를 §4.4의 무게중심 보정에 맡긴다. **비대칭 볼록 다각형**에서 두 판정이 갈릴 수 있다. §4.3 코드 한 블록 + §4.4 한 문장 | 코드 1블록 |
| 16 | `graphics/stage1_gl_fund_04_texture.md` 10절 (L772~776) | 8절 L599와 4절 L365가 "sRGB 디퓨즈에 `glGenerateMipmap`은 부적절"이라 못 박았는데 10절 통합 예제만 그 조합을 단서 없이 쓴다. (a) 한 문장 단서를 달거나 (b) `sRGB == true` 경로에서 밉 생성을 분리 | 예제 1곳 |

### `projects/mini` — 9건

**5건은 `system_kernel_lab`이고 전부 Linux 전용**이라 이 머신(Windows)에서는 작성해도 실행 검증이
불가능하다. WSL 또는 Linux CI가 생기기 전까지 착수할 수 없다.

| # | lab · 항목 | 남은 일 | 이월 사유 |
|---|---|---|---|
| 1 | `system_kernel_lab` #1 [높음] | 부분 읽기·EINTR·취소 자동 검증. 파이프/FIFO 소스 테스트 경로(부분 읽기 강제), `pthread_kill(tid, SIGUSR1)`로 EINTR 주입, aio/uring 취소 CQE 수확 불변식. 최소선은 `fileBytes`보다 작은 파일로 short-read 경로를 밟는 것 | Linux 전용 |
| 2 | `system_kernel_lab` #2 [높음] | 실패 주입 seam 부재. `FaultingBackend` 데코레이터 또는 `SKL_FAULT=` 환경변수 seam + `/proc/self/fd` 개수 비교로 fd 누수 검사 | Linux 전용 |
| 3 | `system_kernel_lab` #3 [높음] | CTest 미실행 · backend 폴백 시 조용한 통과. 폴백 시 실패 처리, 실행된 backend 요약 출력, `TestWorkloadExecutionModels`를 전 backend × {pthread, coroutine}로 확장, Linux 실행 1회 | Linux 환경 |
| 4 | `system_kernel_lab` #8 [중간] | 시그널·io_uring 등록 자원·취소·IPC 대응. backend에 `registered files/buffers` 토글 추가 | 기능 증설 |
| 5 | `system_kernel_lab` #10 [중간] | 재현 가능한 측정 경로 부재. `--backend= --workers= --cycles= --warmup= --out=run.csv` headless 러너 타깃 신설 | 새 실행 파일 |
| 6 | `parts_perf_lab` #12 [중간] | (a) `articles/step07_container.md:270`의 생략 블록을 채운다 (b) 여섯 문서가 공유하는 드리프트 허용 문장을 걷어낸다 (c) 스니펫-소스 대조 장치 | (c)가 저장소 도구 |
| 7 | `physics_lab` #10 [중간] | `measure_scene.cpp`의 `StepSim`이 접촉 수집·Sequential Impulse·위치 보정을 자체 구현하고 유효질량을 리터럴로 박아 둔다 — 결정성 측정이 재는 SI가 Step 6의 그 SI가 아니다. 세 함수를 공유 함수로 올린다 | **구조 개편** |
| 8 | `graphics_dx_lab` #10 [중간] | 7편 어디에도 `D3D12Context` 코드·`CMakeLists.txt`·진입점·셰이더 전문이 없고 step별 변경 파일 목록도 없다 | 문서 대량 증설 + 도구 |
| 9 | `live_service_lab` #13 [낮음] | `<!-- snippet: 파일경로:시작-끝 -->` 앵커와 `check_project_docs.py`의 원문 대조 단계 | 저장소 도구 |

**공통 잔여 2건(저장소 전체).**

1. `PROJECTS_PLAN.md` step 완료 기준 7번이 요구하는 **스니펫-소스 대조 장치가 저장소에 없다.**
   `scripts/`에는 `check_project_docs.py`·`check_links.py`·`check_style.py`·`check_projects.ps1`·
   `check_live_mega.ps1`만 있다. 위 6·8·9번과 `languages_lab` #8 · `math_lab` #11 · `physics_lab` #7 ·
   `ai_lab` #11 · `game_network_lab` #6·#10 · `db_lab` #9의 재발 방지가 전부 이 장치에 걸려 있다.
2. `check_project_docs.py`의 `FORWARD_PROMISE_RE`에 `이후\s*단계` · `Step\s*\d+` · `스텝`이
   추가되지 않아 **가드가 이번에 정리한 표현들을 여전히 못 잡는다.** 문서를 다시 손대면 같은 위반이 통과된다.

**mini 반영 작업이 확인하지 못한 것** — 판정은 파일 내용 대조였고 빌드·`ctest` 재실행 결과가 아니다.
다만 그 뒤 별도로 11개 lab의 변이 재주입 검증과 클린 재빌드가 수행됐다(위 「그 밖에」 참조).
`system_kernel_lab`은 Linux 전용이라 반영됨으로 분류한 8건조차 컴파일 확인이 되지 않은 상태다.

### `projects/mega` — P1 이후 전부 미착수

**P0(구조 게이트)만 완료됐다.** 재설계안의 P1~P9는 착수되지 않았고, 그것이 감사 32건 중
17건이 미해소로 남은 이유다 — 그 17건은 대부분 "게임이 그 축을 부를 이유가 없다"는 규모 문제라
P1~P8에서 게임이 커져야 닫힌다.

**P0가 실제로 닫은 것 (파일에서 확인).**

- `include/`가 실제 정적 라이브러리 타깃 `mega_core`가 됐고 제품·테스트가 **같은 타깃**을 링크한다.
  헤더 하나당 번역 단위를 생성하므로 아무도 include 하지 않던 헤더도 이제 컴파일된다.
- **도달성 게이트**(`scripts/reachability.py`, ctest `reachability_gate`) — 헤더 99개 중
  제품 도달 41 / `docs/LAB_ONLY.md` 등재 58. 도달 불가인데 근거도 없는 제3의 상태는 **빌드 실패**.
  감사가 지목한 "근거 없는 미도달 64개"가 0개가 됐다.
- **계층 규칙 게이트**(`scripts/check_layers.py`, ctest `layer_rules`) — `games/`는 OS API를
  직접 부를 수 없고 진입점은 150줄 제한. 실제로 `games/defense/main.cpp` 29줄 · `server_main.cpp` 18줄이고,
  프레임 루프는 `include/game/defense_app.hpp`의 `DefenseApp::Step()`이다.
- 제품이 `net/reliable_channel.hpp` · `net/session.hpp`(→ `net/snapshot.hpp`)를 **실제로 호출**한다.
  `main.cpp` 익명 네임스페이스의 `ReplicaBuffer` 재구현은 사라졌다.
- 프로세스 테스트가 **제품 실행 파일**을 띄운다 — `tests/test_defense_coop_process.cpp`가
  `mega_defense.exe --host` / `--join`을 스폰한다(테스트 안의 스텁 재구현 제거).
- 테스트 **51 → 55**(`tests/test_*.cpp` 52개 + `mega_defense_selftest` + `layer_rules` + `reachability_gate`).
  감사가 "51개를 전부 통과했다"고 기록한 변이 3건(호스트 중복 억제 · 클라 스냅샷 순서 · 명령 재전송)이
  이제 전부 검출된다.
- 문서 모순 해소 — `docs/AXIS_OWNERSHIP.md` · `docs/LAB_ONLY.md` · 빌드가 생성하는 `docs/PRODUCT_LAYERS.md` 신설.

**남은 일은 두 갈래다.**

1. **재설계안 P1~P9** — ≈70주 전업 상당(파트타임 2.5~3년). 절단선은 **P5**(PROJECTS_PLAN이 정의한
   완성선). P2부터는 어느 단계에서 멈춰도 완결된 게임이 남는다. 상세는
   [REDESIGN_MEGA_2026-08.md 단계별 계획](projects/mega/REDESIGN_MEGA_2026-08.md#단계별-계획).
2. **감사 미해소 17건 + 부분 4건** — 이 중 **P1~P9를 기다리지 않고 지금 닫을 수 있는 것**이 몇 개 있다.
   특히 다음 넷은 게임 규모와 무관한 순수 결함이다.

| 감사 # | 지금 닫을 수 있는 것 | 확인한 현재 상태 |
|---|---|---|
| 8 [높음] | `tools::Script`에 **전역 스텝 예산**을 둔다. 중첩 while이 실행 예산 없이 도는 탓에 교체 가능한 `balance.script` 하나로 두 실행 파일이 시작 시 멈춘다 | `include/tools/script.hpp:189`의 방어는 여전히 While 노드당 지역 `guard`뿐 |
| 21 [중간] | 완전한 PUT/DEL 레코드를 WAL에 append한 뒤 **COMMIT 마커 없이** 재생시키는 케이스를 `tests/test_db_kv.cpp`에 추가 | 현재 테스트는 토막난 레코드만 주입해 파서 경계 검사에서 이탈한다 |
| 22 [중간] | `kMaxTowers` 세 사본을 하나로 통합하고, 테스트가 상수가 아니라 **리터럴 기대값(16/64/20)** 과 비교하게 한다 | `game.hpp:318`·`defense_protocol.hpp:230` 두 상수와 `:301` 리터럴이 그대로. `test_defense_soak.cpp:25`는 여전히 자기 자신과 비교 |
| 23 [중간] | 범위 밖 `argument`를 담은 Command와 잉여 바이트가 붙은 Snapshot의 거부를 `test_defense_protocol.cpp`에 추가 | `argument` 범위 테스트 0건 |

그 밖의 미해소 항목: 2(graphics 축 → P1·P3) · 6(parts·algorithms·system 축 → P1~P8) ·
7·24(Vulkan present가 ctest·CI에 없는데 README는 "자동 검증한다"로 적음) · 14·26(**git 이력 없음** —
`git log`가 지금도 "does not have any commits yet") · 15(languages 축 → P6) · 16(tools·platform·engine 우회) ·
25(sanitizer 0건, 고정 seed replay·checksum 없음) · 27(README 트리에 `serialize/` 등 누락) ·
29(`GenerateWaves` 테스트 전용) · 30(`snapshot_bytes` 수집만 하고 출력 안 함) ·
31(`truncated` 플래그를 아무도 읽지 않음).
부분 해소 4건: 3(축 소유권 지도는 생겼으나 mini↔mega 코드 중복은 그대로) ·
4(ai 헤더 9개에 근거는 붙었으나 게임은 여전히 안 부름) ·
5(신뢰 채널·세션은 제품 진입, `net/rtt.hpp`는 P5로 등재 — 와이어에 ping/pong 표본이 없어 입력 자체가 없다) ·
17(테스트 개수 불일치는 해소, Vulkan 자동 검증 주장은 남음).

건별 판정 근거는
[REVIEW_MEGA_2026-08.md 반영 상태](projects/mega/REVIEW_MEGA_2026-08.md#반영-상태-2026-08-13)에 있다.
