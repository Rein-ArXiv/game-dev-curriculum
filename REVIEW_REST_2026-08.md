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
- **2026-08-14에 2차 반영이 있었다.** 아래 표의 `반영 상태 (2026-08-13)` 열은 그날의 기록이고,
  그 옆 `→ 2026-08-14` 열이 현재 상태다. 2차에서 무엇이 닫혔는지는
  [2026-08-13 이후 닫힌 것](#2026-08-13-이후-닫힌-것)에 있다.
- 보고서를 "미반영 지적 목록"으로 읽으면 안 된다. 남은 일은 아래 [남은 일](#남은-일) 절이 전부다.

## 시리즈별 보고서

`확정`은 검수 시점 기록이고, `반영 상태`는 각 보고서의 반영 상태 절에서 그대로 옮긴 값이다.

| 시리즈 | 편수 | 줄수 | 보고서 | 확정 | 반영 상태 (2026-08-13) | → 2026-08-14 |
|---|---:|---:|---|---:|---|---|
| math | 20 | 16,759 | [math/REVIEW_MATH_2026-08.md](math/REVIEW_MATH_2026-08.md) | **43**<sup>†</sup> | [반영 4 · 기반영 47 · 이월 **0**](math/REVIEW_MATH_2026-08.md#반영-상태-2026-08-13) | — |
| physics | 20 | 13,083 | [physics/REVIEW_PHYSICS_2026-08.md](physics/REVIEW_PHYSICS_2026-08.md) | **24** | [반영 7 · 기반영 17 · 이월 **1**](physics/REVIEW_PHYSICS_2026-08.md#반영-상태-2026-08-13) | 이월 1 **닫힘** |
| graphics | 41 | 21,339 | [graphics/REVIEW_GRAPHICS_2026-08.md](graphics/REVIEW_GRAPHICS_2026-08.md) | **54**<sup>†</sup> | [반영 2 · 기반영 62 · 기각 5 · 이월 **0**](graphics/REVIEW_GRAPHICS_2026-08.md#반영-상태-2026-08-13) | 교차 이월 1건을 여기서 닫음 |
| ai | 26 | 17,323 | [ai/REVIEW_AI_2026-08.md](ai/REVIEW_AI_2026-08.md) | **106** | [반영 54 · 기반영 50 · 기각 28 · 이월 **2**](ai/REVIEW_AI_2026-08.md#반영-상태-2026-08-13) | 이월 2 **닫힘**(1건은 이미 해소 확인) |
| languages | 8 | 5,018 | [languages/REVIEW_LANGUAGES_2026-08.md](languages/REVIEW_LANGUAGES_2026-08.md) | **32** | [반영 32 · 기반영 0 · 기각 9 · 잔여 **2**](languages/REVIEW_LANGUAGES_2026-08.md#반영-상태-2026-08-13) | 잔여 2 **닫힘** |
| engine | 10 | 5,417 | [engine/REVIEW_ENGINE_2026-08.md](engine/REVIEW_ENGINE_2026-08.md) | **22** | [반영 22 · 기반영 0 · 기각 11 · 잔여 **3**](engine/REVIEW_ENGINE_2026-08.md#반영-상태-2026-08-13) | 2 닫힘 · **1 보류**(part03_vfx §4, 오류 아님) |
| live_service | 10 | 5,701 | [live_service/REVIEW_LIVE_SERVICE_2026-08.md](live_service/REVIEW_LIVE_SERVICE_2026-08.md) | **24** | [반영 24 · 기반영 0 · 기각 14 · 이월 **1**](live_service/REVIEW_LIVE_SERVICE_2026-08.md#반영-상태-2026-08-13) | 이월 1 **닫힘** |
| platform | 7 | 4,428 | [platform/REVIEW_PLATFORM_2026-08.md](platform/REVIEW_PLATFORM_2026-08.md) | **22** | [반영 22 · 기반영 0 · 기각 7 · 이월 **1**](platform/REVIEW_PLATFORM_2026-08.md#반영-상태-2026-08-13) | 이월 1 **닫힘**(실측) |
| db | 5 | 2,448 | [db/REVIEW_DB_2026-08.md](db/REVIEW_DB_2026-08.md) | **18** | [반영 18 · 기반영 0 · 기각 7 · 이월 **0**](db/REVIEW_DB_2026-08.md#반영-상태-2026-08-13) | — |
| tools | 5 | 3,018 | [tools/REVIEW_TOOLS_2026-08.md](tools/REVIEW_TOOLS_2026-08.md) | **22** | [반영 22 · 기반영 0 · 기각 8 · 잔여 **3**](tools/REVIEW_TOOLS_2026-08.md#반영-상태-2026-08-13) | 잔여 3 **닫힘** |
| base | 21 | 3,535 | [base/REVIEW_BASE_2026-08.md](base/REVIEW_BASE_2026-08.md) | **5** | [반영 5 · 기반영 0 · 이월 **1**](base/REVIEW_BASE_2026-08.md#반영-상태-2026-08-13) | 이월 1 **닫힘** |

- **기반영** = 보고서 작성 뒤·이번 배치 전에 이미 본문이 고쳐져 있던 것. math·physics·graphics·ai
  네 시리즈에만 있고(합계 176건), 나머지 일곱 시리즈의 보고서는 낡아 있지 않았다.
- **기각** = 검증에서 오탐으로 판정된 것. **다시 미반영으로 세지 말고, 본문을 고치지도 마라.**
- **이월 / 잔여** = 2026-08-13 시점에 남아 있던 일. 확정 건수에서 빠지는 값이 아니다 —
  대부분은 반영된 항목의 파생·부수 항목이다. **이론 16건 중 15건은 2026-08-14에 닫혔다**;
  지금도 남은 것만 아래 [남은 일](#남은-일)에 있고, 닫힌 것은
  [2026-08-13 이후 닫힌 것](#2026-08-13-이후-닫힌-것)에 있다.
- <sup>†</sup> math·graphics는 각 보고서의 반영 상태 절이 **소절 단위로 다시 세어** math 51 ·
  graphics 64로 집계한다(math 43은 2026-08-09 기록, graphics 54는 파일별 선언 합계).
  반영 열의 숫자는 재집계 기준이므로 `확정` 열과 더해지지 않는다. ai 106 = 54 + 50 + 2 로 맞는다.
- **`→ 2026-08-14` 열은 이 색인에만 있다.** 각 시리즈 보고서의 「집계」 표는 제목부터
  `(2026-08-13)` 시점 기록이라 그날의 판정을 보존하려고 값을 덮어쓰지 않았고, 후속 처리는
  각 보고서 이월 절 **안의 블록**으로만 적혀 있다. 그래서 보고서의 집계 표만 보고 지나가면
  아직 이월이 남은 것으로 읽힌다 — 판정은 이 열이 기준이다.

## projects/ (별도 취급 — 코드와 과정 문서)

| 대상 | 보고서 | 발견 | 반영 상태 (2026-08-13) | → 2026-08-14 |
|---|---|---:|---|---|
| mini lab 12개 | [projects/mini/REVIEW_MINI_2026-08.md](projects/mini/REVIEW_MINI_2026-08.md) | **166** | [반영 157 · 기반영 0 · 이월 **9**](projects/mini/REVIEW_MINI_2026-08.md#반영-상태-2026-08-13) + 변이 재주입 검증 완료(아래) | 3 닫힘 · 1 부분 · **잔여 6** (5는 `system_kernel_lab`) |
| mega 통합 감사 | [projects/mega/REVIEW_MEGA_2026-08.md](projects/mega/REVIEW_MEGA_2026-08.md) | **32** | [P0로 닫힘 11 · 부분 4 · **미해소 17**](projects/mega/REVIEW_MEGA_2026-08.md#반영-상태-2026-08-13) | 12 추가 닫힘 → **미해소 5 · 부분 4** |
| mega 재설계안 | [projects/mega/REDESIGN_MEGA_2026-08.md](projects/mega/REDESIGN_MEGA_2026-08.md) | 제안 | [**P0 완료 · P1~P9 미착수**](projects/mega/REDESIGN_MEGA_2026-08.md#반영-상태-2026-08-13) | 정정 — 완료된 것은 **P0 구조 게이트**이고 P0 계측 인프라(장치 B L1~L4)는 미착수 |

**173편 전부 검수 완료.** 미커버 없음.

## 교차 대조

| 대상 | 보고서 | 발견 | 반영 상태 (2026-08-13) | → 2026-08-14 |
|---|---|---:|---|---|
| math ↔ physics | [REVIEW_CROSS_MATH_PHYSICS_2026-08.md](REVIEW_CROSS_MATH_PHYSICS_2026-08.md) | **11** | [반영 11 · 기반영 0 · 이월 0(잔여 하위 **1**)](REVIEW_CROSS_MATH_PHYSICS_2026-08.md#반영-상태-2026-08-13) | 잔여 하위 1 **닫힘** |
| graphics 내부 | [REVIEW_CROSS_GRAPHICS_2026-08.md](REVIEW_CROSS_GRAPHICS_2026-08.md) | **19** | [반영 19 · 기반영 0 · 기각 4 · 이월 **1**](REVIEW_CROSS_GRAPHICS_2026-08.md#반영-상태-2026-08-13) | 본문은 **닫힘**, 그러나 **이 보고서가 미갱신**([남은 일 1번](#지금-바로-할-수-있는-것--8건)) |
| base ↔ 심화 시리즈 | base 보고서에 포함 | 0 | — | — |

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
- **`projects/mega` P0 구조 게이트 완료.** 무엇이 닫혔는지는
  [2026-08-13에 닫힌 것 — mega P0 구조 게이트](#2026-08-13에-닫힌-것--mega-p0-구조-게이트),
  남은 것은 [남은 일 · mega](#projectsmega--규모에-묶인-것) 참조.

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

### 2차 반영 결과 (2026-08-14)

위 표의 마지막 열(`이월·잔여`)만 다시 센 것이다. 확정·기반영·기각 숫자는 바뀌지 않았다.

| 대상 | 08-13 이월·잔여 | 08-14 닫힘 | 남음 |
|---|---:|---:|---|
| 편별 검수 11개 시리즈 | 14 | **13** | **1** — `engine/part03_vfx.md` §4 보류(오류 아님) |
| 교차 대조 (math↔physics) | 1 | **1** | 0 |
| 교차 대조 (graphics 내부) | 1 | **1** | 0 — 다만 소관 보고서가 미갱신 |
| **이론 문서 합계** | **16** | **15** | **1** |
| `projects/mini` | 9 | **3** (+1 부분) | **6** — `system_kernel_lab` 5 + `graphics_dx_lab` #10 테스트 대역 |
| `projects/mega` 감사 | 17 미해소 + 4 부분 | **12** | **5 미해소 + 4 부분** |

닫힌 건별 내역은 [2026-08-13 이후 닫힌 것](#2026-08-13-이후-닫힌-것)에 있다.
2차 반영의 판정 근거도 같은 백업 대비 diff이고, 여기에 **실행 검증**이 더해졌다 —
mega 클린 빌드 2회 + `ctest 56/56`, mini 11개 lab 클린 재빌드,
`python scripts/check_project_docs.py` → 문서 80개 / 앵커 118개 / 0 error 0 warning.

---

## 저장소 도구 — 문서를 손대기 전에

**`scripts/check_snippets.py` (2026-08-14 신설)** — 문서의 코드 펜스를 **실제 소스 파일과 대조**한다.
`PROJECTS_PLAN.md`의 Step 문서 완료 기준 7번(*최종 코드와 문서 코드가 드리프트하지 않는다*)을
사람 대신 기계가 집행하는 장치다. `scripts/check_project_docs.py`가 이것을 import 해서 함께 돌리므로
**진입점은 하나다.**

```powershell
python scripts/check_project_docs.py   # 완료 기준 2번(후속 step 예고 금지) + 7번(스니펫 대조)
python scripts/check_snippets.py --stats        # 앵커 종류별 현황
python scripts/check_snippets.py --fix-ranges   # 밀린 줄번호 앵커 자동 갱신
```

**문서를 손댔으면 이것을 돌려라.** 검사는 **opt-in**이다 — 코드 펜스 바로 앞 줄에
`<!-- snippet: 경로#심볼 -->` 형태의 HTML 주석을 달았을 때만 대조한다. 앵커가 없는 펜스는
검사되지 않으며 그게 의도다: step 문서의 스니펫 상당수는 *그 step 시점의 스냅숏*이라
디스크의 코드와 다른 것이 정상이기 때문이다. 앵커를 다는 것은 **"이 펜스는 지금 소스와 같다"는
저자의 주장**이고, 다는 순간부터 게이트가 그 주장을 지킨다. 문법·경로 규칙·앵커 종류는
[`scripts/SNIPPETS.md`](scripts/SNIPPETS.md)에 있다.

현재 상태(2026-08-14 실측): **앵커 118개** — 심볼 78 · region 28 · 파일 12 · 줄범위 0.
`check_project_docs.py` 기준 문서 80개 / 0 error 0 warning.
아직 앵커가 0개인 lab이 8개 남아 있다([남은 일](#스니펫-앵커가-아직-0개인-lab-8개)).

같은 날 `check_project_docs.py`의 `FORWARD_PROMISE_RE`도 보강됐다 —
`이후…단계`, `다음/이후/후속/추후 스텝|step`, 자기 step 번호 대조 규칙이 추가됐다.
첫 실행 14건 중 **오탐 8건**을 먼저 제거했고(기존 `~` 제거 코드가 `Step 1~7`을 `Step 17`로
만들던 버그, 세는 말 `step 4번 모이면`), 진짜 6건은 전부 수정했다.

---

## 남은 일

**다음에 이 저장소를 여는 사람은 이 절만 읽으면 된다.** 위의 보고서들은 이미 반영된 지적을
현재형으로 서술하고 있으므로 할 일 목록으로 쓰면 안 된다.
2026-08-13 판 이 절에 있던 **이론 16건 표는 15건이 닫혔고 1건만 보류**로 남았다 —
닫힌 것을 다시 열지 않도록 [2026-08-13 이후 닫힌 것](#2026-08-13-이후-닫힌-것)을 먼저 보라.

남은 것은 아래 넷이다.

| 갈래 | 건수 | 성격 |
|---|---:|---|
| [지금 바로 할 수 있는 것](#지금-바로-할-수-있는-것--8건) | 8 | 1줄~한 문단. 환경도 판단도 필요 없다 |
| [스니펫 앵커가 0개인 lab](#스니펫-앵커가-아직-0개인-lab-8개) | 8 lab | 장치는 있고 켜지지 않았다 |
| [`projects/mini`](#projectsmini--6건) | 6 | 5는 Linux 환경 필요, 1은 문서 증설 |
| [`projects/mega`](#projectsmega--규모에-묶인-것) | 감사 미해소 4 + 부분 3 + P0 잔여 + P1~P9 | 대부분 게임 규모가 커져야 닫힌다 |

### 지금 바로 할 수 있는 것 — 8건

| # | 대상 | 남은 일 | 규모 |
|---|---|---|---|
| 1 | [`REVIEW_CROSS_GRAPHICS_2026-08.md`](REVIEW_CROSS_GRAPHICS_2026-08.md) 「이월 — 1건」 절 | 그 1건(`graphics/stage1_gl_fund_04_texture.md` 10절 sRGB + `glGenerateMipmap`)은 2026-08-14에 한 문장 단서로 닫혔는데, 그 사실이 `graphics/REVIEW_GRAPHICS_2026-08.md`에만 기록돼 있다. **같은 항목이 두 보고서에서 서로 다른 상태를 말한다** | 한 문단 — 이월 절에 후속 처리 블록 1개 + 집계표 이월 1 → 0 |
| 2 | [`PROJECTS_PLAN.md`](PROJECTS_PLAN.md):37 | `physics_lab` 완료 기준이 여전히 "적분기 오차 차수, 운동량·에너지 범위, 접촉·**마찰**·제약 불변식". `physics_lab` #5가 지적한 대로 마찰(`physics/part09`)은 lab 소스에 없고, lab README는 2026-08-13에 이미 매핑에서 빼고 비범위를 명시했다. 계획서만 어긋나 있다. 완료 기준 2·7번은 새 장치를 정확히 가리키도록 갱신됐으므로 이 한 줄이 마지막 불일치다 | 1줄 — 다만 **마찰을 lab에 구현할지 계획에서 뺄지 판단이 선행** |
| 3 | [`scripts/check_style.py`](scripts/check_style.py) | `REVIEW_*.md` 검수 보고서를 기사 문서 규격으로 검사해 **ERROR 393건**(2026-08-14 실측, 검사 대상 250개 문서). "들어가며/목차/더 읽을거리 절 필수" · "H3 제목에 번호 금지" · "다른 시리즈 디렉터리 이름 직접 참조 금지" 같은 규칙이 보고서 성격과 맞지 않는다. 이번 작업 전부터 있던 상태이지만 **게이트가 상시 빨간 탓에 진짜 스타일 위반이 묻힌다** | 도구 1개 — `REVIEW_*`/`REDESIGN_*` 제외 패턴 |
| 4 | [`math/REVIEW_MATH_2026-08.md`](math/REVIEW_MATH_2026-08.md) 6행 | 파일 전체에서 **이 한 줄만** CRLF가 아니라 bare LF다(백업 2026-08-11 시점에도 동일 — 최근 작업이 만든 것이 아니다). 줄 단위 자동 편집 도구가 이 파일에서만 인덱스가 2줄씩 밀리고, 실제로 2026-08-14 삽입이 엉뚱한 절로 들어가 수동으로 옮겨야 했다 | 1줄 — 줄끝 문자만 CRLF로 |
| 5 | `projects/mini/graphics_dx_lab/articles/diagrams/step01_skeleton_diagram_01.png` | 고아 파일 — `articles/`와 `README.md` 어디에서도 **참조 0건**. step01이 114 → 1,044줄로 다시 쓰이면서 그 그림을 부르던 자리가 사라졌는데 파일만 남았다. mini의 다른 PNG(gl 3 · math 3 · parts_perf 7)는 전부 참조가 살아 있어 이 하나만 예외다 | 1파일 — 참조 복구 또는 삭제 |
| 6 | git 커밋 | 커밋은 `870198c` 하나뿐이고 **2026-08-14 작업분은 전부 워킹 트리 미커밋**이다(`git status` 93 경로). 감사 #14·#26을 닫은 근거는 "저장소가 git 저장소가 됐다"이지 "`PROJECTS_PLAN.md:12`의 개발 순서 설명이 성립한다"가 아니다 — **단계별 소스 스냅숏을 폐기한 대가로 변경 이력에 위임한 설명은 커밋이 쌓여야 생긴다.** '이력 있음'만 보고 완결로 읽지 마라 | 운영 — 커밋 1회로 이번 회차분 반영 |
| 7 | `projects/mega` 감사 **#25** | sanitizer 0건(`CMakeLists.txt`·`.github/`·`docs/` 전체에 `sanitiz\|fsanitize\|asan\|ubsan` **0 hit**, 2026-08-14 실측) + `GameWorld` 고정 seed replay·checksum 없음. Checksum을 쓰는 테스트는 `test_mp_lockstep`·`test_mp_rollback` 둘뿐이고 둘 다 제품 미사용 모듈 `mp/det_sim.hpp`를 겨냥한다. **P1~P9를 기다리지 않는 유일한 미해소 감사 항목**이고, 절반(결정성 replay)이 재설계안 장치 B의 L1(골든 해시 체인)과 같은 방향이다 — 즉 P0의 마지막 조각과 감사의 마지막 '지금 가능'이 같은 일이다. **다음에 손댈 곳이 있다면 여기** | CI 잡 1개 + 테스트 1개 (아래 P0 골든 해시 인프라와 연동) |
| 8 | `projects/mega` 감사 **#17** 잔여 | `include/render/framebuffer.hpp:3`의 "이 머신엔 Vulkan SDK 가 없어 GPU 백엔드를 빌드·검증할 수 없다" 주석이 그대로인데, 같은 저장소 README는 Vulkan 백엔드를 지원 기능으로 적는다. **소스 주석과 README가 반대 사실을 말하는 상태**는 #17이 지적한 바로 그 종류의 불일치다. 감사 #2도 같은 줄을 지목한다. 2026-08-14 작업 입력에는 없던 판정이라 다음 회차가 놓치기 쉽다 | 주석 한 문단 — 아래 #2/P1의 소프트 래스터 승격 서술과 함께 고치는 편이 자연스럽다 |

### 스니펫 앵커가 아직 0개인 lab 8개

`ai_lab` · `db_lab` · `game_network_lab` · `graphics_gl_lab` · `graphics_vk_lab` ·
`languages_lab` · `math_lab` · `system_kernel_lab`.

앵커가 있는 lab은 넷뿐이다 — `graphics_dx_lab` 85 · `live_service_lab` 16 ·
`parts_perf_lab` 13 · `physics_lab` 4 (합계 118, 2026-08-14 실측).

**장치가 저장소에 생긴 것과 그 lab이 장치를 켠 것은 다르다.** `languages_lab` #8 ·
`math_lab` #11 · `ai_lab` #11 · `game_network_lab` #6·#10 · `db_lab` #9는 전부
"문서가 존재하지 않는 식별자·시그니처를 쓴다"는 같은 부류의 지적이었고 스니펫 자체는
2026-08-13에 고쳐졌지만, **재발은 여전히 사람이 봐야 잡힌다.**
`live_service_lab`에서 앵커를 켜자 보고서가 세었던 3건이 아니라 **12건**이 나온 것이
이 공백의 크기를 보여준다(그리고 그 12건은 전부 문서 쪽 오류였다).

**규모** — lab당 앵커 수십 개. `graphics_dx_lab`이 든 85개가 비용 기준선이다.

### `projects/mini` — 6건

**5건은 `system_kernel_lab`이고 전부 Linux 전용**이라 이 머신(Windows)에서는 작성해도
실행 검증이 불가능하다. WSL 또는 Linux CI가 생기기 전까지 착수할 수 없다.
2026-08-13 이후 상태 변화가 **0**이다 — `tests/system_core_tests.cpp`와 `src/main.cpp`는
백업본과 바이트 단위로 동일하고 `projects/mini/VERIFY.md:46`은 여전히 "현재 환경에서 미검증"이다.
이 lab의 문서 성격 지적 8건은 이미 반영됐고(2026-08-13, 그리고 08-14에 step02·03·04의
드리프트 허용 문장·후속 step 예고 제거) **남은 5건은 전부 검증력 항목**이다.

`VERIFY.md:29`가 이 lab을 `scripts/check_projects.ps1`에서 제외하므로,
2026-08-14의 "클린 재빌드 통과"도 **12개 중 11개 lab에서만** 돌았다 — 빠진 하나가 이것이다.

| # | lab · 항목 | 남은 일 | 규모 |
|---|---|---|---|
| 1 | `system_kernel_lab` #1 [높음] | 부분 읽기·EINTR·취소 자동 검증. 파이프/FIFO 소스 테스트 경로(부분 읽기 강제), `pthread_kill(tid, SIGUSR1)`로 EINTR 주입, aio/uring 취소 CQE 수확 불변식. 최소선은 `fileBytes`보다 작은 파일로 short-read 경로를 밟는 것 | **환경 필요** (Linux) |
| 2 | `system_kernel_lab` #2 [높음] | 실패 주입 seam 부재. `FaultingBackend` 데코레이터 또는 `SKL_FAULT=` 환경변수 seam + `/proc/self/fd` 개수 비교로 fd 누수 검사 | **환경 필요** (Linux) |
| 3 | `system_kernel_lab` #3 [높음] | CTest 미실행 · backend 폴백 시 조용한 통과. 폴백 시 실패 처리, 실행된 backend 요약 출력, `TestWorkloadExecutionModels`를 전 backend × {pthread, coroutine}로 확장, Linux 실행 1회 | **환경 필요** (Linux) |
| 4 | `system_kernel_lab` #8 [중간] | 시그널·io_uring 등록 자원·취소·IPC 대응. backend에 `registered files/buffers` 토글 추가 | **환경 필요** + 기능 증설 |
| 5 | `system_kernel_lab` #10 [중간] | 재현 가능한 측정 경로 부재. `--backend= --workers= --cycles= --warmup= --out=run.csv` headless 러너 타깃 신설 | **환경 필요** + 새 실행 파일 1개 |
| 6 | `graphics_dx_lab` #10 **부분 잔여** | 제품 코드는 2026-08-14에 닫혔다(7편 전부에 "이 step이 추가·변경하는 파일" 표 신설, 누락 재현 코드 게재, 앵커 85개). 그러나 **테스트 대역 1,301줄**(`tests/d3d12_stubs.h` 442 + `tests/graphics_dx_contract_tests.cpp` 859)이 문서에 실리지 않았다. 실린 테스트 앵커는 `articles/step04_rootsig.md:174`의 `TestRootConstantBudget` 하나뿐이고 나머지는 산문과 CMake 타깃 정의로만 등장한다. **문서만 따라가면 `graphics_dx_contract_tests` 타깃을 빌드할 수 없다** — 완료 기준 4번이 테스트 대역에 대해 미충족이다. 이 lab의 #1 지적이 "테스트가 제품 `.cpp`를 하나도 링크하지 않는다"(변이 5건 전부 생존)였던 만큼 가벼운 잔여가 아니다 | **문서 증설** — 두 파일에 region 마커를 넣고 발췌 게재, 또는 "테스트 전문은 소스를 보라"는 명시적 선언 한 문장 |

### `projects/mega` — 규모에 묶인 것

감사 32건 중 **미해소 5 · 부분 4**가 남았다. 이 중 지금 닫을 수 있는 둘(#25·#17)은
[지금 바로 할 수 있는 것](#지금-바로-할-수-있는-것--8건) 7·8번에 올렸다. 나머지는 아래다.

**미해소 4건 — 전부 "게임이 그 축을 부를 이유가 없다"는 규모 문제.**

| 감사 # | 확인한 현재 상태 | 규모 |
|---|---|---|
| **2** [치명] | 그래픽스 전량이 여전히 CPU 프레임버퍼 blit. `include/render/`는 4파일(`framebuffer`·`raster2d`·`sprite`·`text`) 전부 소프트웨어, `platform/vk_present.hpp`는 `mipLevels=1` · `VK_SAMPLE_COUNT_1_BIT` · 깊이 어태치먼트 없음의 present-only. **GL·DX 대응물 0줄.** 32건 중 유일하게 남은 [치명]이다 — 문서량 2위 축(40편·21,286줄)에 대한 제품 대응이 16줄 blit 셰이더 한 쌍 | **장기** — P1(소프트웨어 래스터 백엔드, 8주) + P3(Vulkan 백엔드, 8주). 게임이 3D가 되기 전에는 깊이·밉·인스턴싱·컬링을 부를 이유 자체가 없다 |
| **6** [높음] | SIMD 연산 intrinsic 0건(`include`·`games`·`tests` 전수 grep에서 `_mm_` 히트는 `include/sync/futex.h`의 `_mm_pause()` 스핀 힌트 둘뿐이고 SIMD 연산이 아니다), algorithms 키워드(huffman/fenwick/aho/union-find/kruskal/bellman) **0건**, system 대응 858줄 전부 제품 미도달. 문서량 1·2위 축 합계 56,326줄(전체 이론의 33%)에 대한 대응이 이 상태다 | **장기** — P1~P8. 게임 상태가 `std::vector<Enemy>` + `std::remove_if`인 한 SoA·아레나·잡 스케줄러를 부를 **측정 근거가 생기지 않는다**. `algorithms`는 mini에 대응 lab조차 없어 **소유자 결정이 선행** |
| **15** | languages 축 → P6 | **장기**. 2026-08-13 미해소 목록에서 그대로 이월 — 2026-08-14 회차에 재확인하지 않았다 |
| **16** | tools·platform·engine 우회 | **장기**. 위와 같음 |

**부분 3건.**

| 감사 # | 무엇이 남았나 | 규모 |
|---|---|---|
| **3** | 축 소유권 지도(`docs/AXIS_OWNERSHIP.md`·`docs/LAB_ONLY.md`)는 생겼으나 mini↔mega 같은 주제의 **두 구현이 물리적으로 그대로** | 재설계안 `## 이관` 전반 |
| **4** | ai 헤더 9개가 `docs/LAB_ONLY.md`에 등재된 채 제품 미도달이고, `game.hpp:157`의 `candidateCount != 1` 단일 경로 제약도 그대로 | P2·P4 |
| **5** | `net/rtt.hpp`를 include 하는 곳은 `tests/test_net_reliable.cpp`뿐이다(그 외 히트는 빌드가 생성하는 `build/mega_core_units/unit_net_rtt_hpp.cpp`). **실질 손해가 열려 있다** — 재전송 주기가 100ms 고정이라 RTT가 그보다 큰 실제 경로에서 매 주기 중복 전송이 난다. `LAB_ONLY.md:85`의 이월 사유(와이어에 ping/pong 왕복 표본이 없어 넣을 입력이 없다)는 여전히 유효 | P5 |

**P0 잔여 — A/B 스위치 레지스트리 · 벤치 하네스 · 골든 해시 체인(재설계안 장치 B의 L1~L4) 미착수.**

2026-08-13에 완료된 것은 **P0의 구조 게이트 부분**이다(아래 [닫힌 것](#2026-08-13에-닫힌-것--mega-p0-구조-게이트) 참조).
계측 인프라는 손대지 않았고, 그래서 재설계안의 「규모 수치 (잠정 — P0에서 실측 후 확정)」이
**아직 잠정**이다. 설계가 스스로 건 규율 — *저자 머신에서 SoA/AoS가 2배 갈라지는 엔티티 수를
먼저 측정하고 그 수가 설계 목표가 된다* — 이 아직 집행되지 않았고,
「저자가 결정해야 할 것」 2번(2.5~3년 감당 / 절단선 앞당기기)의 판단 근거도 여기서 나온다.
**규모: P0의 남은 지분. P1 착수 전에 하는 것이 설계 의도이고, 위 #25와 절반이 같은 일이다.**

**재설계안 P1~P9 — ≈70주 전업 상당(파트타임 2.5~3년).**

절단선은 **P5**(PROJECTS_PLAN이 정의한 완성선). P2부터는 어느 단계에서 멈춰도 완결된 게임이 남는다.
상세는 [REDESIGN_MEGA_2026-08.md 단계별 계획](projects/mega/REDESIGN_MEGA_2026-08.md#단계별-계획).
**규모: 장기.**

건별 판정 근거는
[REVIEW_MEGA_2026-08.md 반영 상태](projects/mega/REVIEW_MEGA_2026-08.md#반영-상태-2026-08-13)에 있다.

---

## 닫힌 것 — 다시 열지 마라

### 2026-08-13 이후 닫힌 것

2026-08-14 2차 반영. **아래 항목을 할 일로 다시 열지 마라.**

**이론 16건 → 15 닫힘 · 1 보류.**

- `physics/part13_joints.md` §7.3 — 코드 기본값을 올리는 대신 **본문을 조건부 서술로** 고쳤다.
  §7.4가 "엔진마다 hard mode 전환 조건이 다르다"고 명시하므로 `frequency = 0` = rigid가 이 문서의 관례다.
- `ai/part20_ai_debugging.md` — 시퀀스 다이어그램 `At(12825)` → `At(12829)`(틱=프레임이고 간격이 1이므로
  "한 틱 뒤로"는 12829). L445 건은 **이미 해소**돼 있음을 확인하고 닫았다.
- `languages/part05_jit_compilation.md` — Recording 로그를 한 이터레이션으로 정정.
- `languages/part07_reflection_metadata.md` — `PropertyType` 열거형 + `PropertyInfo::type` + 매크로
  세 곳을 맞췄다. **MSVC로 실제 컴파일·실행해 확인.**
- `engine/part05_resource.md` §8 — `duplicateLoadsThisSession` 계수 지점 추가(지표가 영구 0이던 것).
- `engine/part02_animation.md` §4 — `activeBlendTime == 0` 가드(분모가 실제로 0이 될 수 있음을 확인).
- `live_service/part08_matchmaking.md` L138 — `$Q$` → `$Q_{\mathrm{balance}}$`.
- `platform/part07_windows_text.md` L487 — 이 머신에서 `GetLocaleInfoEx`를 **직접 호출**해 `한국`을 확인했다.
  값뿐 아니라 **의미 열도 정정**했다("전체 국가 이름" → "지역화된 국가 이름").
  `대한민국`은 `LOCALE_SNATIVECOUNTRYNAME`(0x08)이다.
- `tools/part05_asset.md` L432 · `tools/part06_build.md` L79 · `tools/part04_debug.md` L390 — 셋 다 정정.
- `base/part03_data_structures.md` — L55 산문에 "이중" 추가. L38은 **"주요 조건" 열에 단서**로 넣었다
  (중간 삭제 열은 우측 정렬 수치 열이라 산문을 넣으면 열 성격이 깨진다).
- `math/part12_collision_sat.md` §4.3 — MTV 부호를 밀어내기 방향에서 직접 뽑도록 교정.
  **삼각형 반례를 직접 계산**해 무게중심 보정이 부호를 뒤집는 것을 확인했고 §4.2·§4.4도 연쇄 수정했다.
  §5.1 `OBBOBBSAT`·§7.2는 **오류가 아님**으로 판정했다 — OBB는 중심 대칭이라 두 규칙이 항등이다.
- `graphics/stage1_gl_fund_04_texture.md` 10절 — sRGB + `glGenerateMipmap` 조합에 단서 한 문장.
- **보류 1건** — `engine/part03_vfx.md` §4. 사실오류(TBR/TBDR 용어)는 2026-08-13에 이미 닫혔고,
  남은 것은 "은면 제거는 불투명 기하에만 유효하고 반투명 파티클에는 적용되지 않는다"는 **선택적 보강**뿐이다.
  파일은 2026-08-14에 변경되지 않았다. **오류가 아니므로 사실오류 정정 대상으로 다시 열지 마라.**
  넣을지 말지의 판단만 남아 있다(규모: 문장 1개).

**저장소 공통 2건 → 완료.** `scripts/check_snippets.py` 신설과 `FORWARD_PROMISE_RE` 보강.
내용은 [저장소 도구](#저장소-도구--문서를-손대기-전에) 절에 있다.

**mini 이월 9 → 3 닫힘 · 1 부분.**

- `parts_perf_lab` #12 — 생략 블록을 실제 소스로 채우고 **드리프트 허용 문장을 6개 문서에서 제거**했다.
  `articles/step07_container.md`에 앵커 11개.
- `physics_lab` #10 — **차이가 7군데였다**(지적은 1건). 유효질량이 리터럴로 박혀 있었고 임펄스에 `* invMass`가
  빠져 있었다. `src/core/contact_solver.h`로 통합. 통합 전 실측 차이는 240프레임 후 위치 0.557px /
  속도 4.77px/s였고 **통합 후 비트 일치**. 결정성 수치를 갱신했다(shuffled maxDelta 0.700067 → 0.399962 등).
  재발 방지 테스트 `TestMeasureRunsTheResponseSolver` 추가.
- `live_service_lab` #13 — 앵커 16개. **드리프트 12건 발견, 전부 문서 쪽 오류**였다
  (`new GamePayload(...)` bare new = CS0201 컴파일 불가, 존재하지 않는 식별자 다수).
- `graphics_dx_lab` #10 — **부분**. 제품 코드 쪽은 닫혔다(7편 전부에 변경 파일 표 신설, 누락 재현 코드 게재,
  앵커 85개). 테스트 대역은 [남은 일 mini 6번](#projectsmini--6건).
- 추가: `system_kernel_lab` 문서 3편에 남아 있던 드리프트 허용 문장 제거.

**mega 감사 미해소 17 → 12 닫힘.**

- **#8** `tools::Script` 전역 스텝 예산. 지역 guard는 중첩되면 **곱해져서**(2겹 = 10<sup>12</sup>)
  방어가 되지 않았다. **같은 결함의 다른 두 경로도 함께 닫았다** — 파서 전진 보장(`}` 한 글자로 20초+ 미반환,
  노드 무한 할당)과 중첩 깊이 상한(스택 오버플로 크래시). 옛 코드에서 **30.77초 / 20.22초 미반환**을
  실행으로 증명했다. ctest에 `TIMEOUT 30`으로 등록(`test_tools_script`). 진단은
  `<스크립트>:<줄>: 실행 스텝 예산 … 초과` 형태이고, 두 실행 파일이 조용히 폴백하지 않도록 stderr 진단을 추가했다.
- **#21** WAL COMMIT 마커 경로 테스트(46 → 73 checks). 변이로 검출 확인.
- **#22** `kMaxTowers` 사본 통합 + 테스트가 리터럴과 비교. 변이 프로브 4건 전부 검출.
- **#23** 프로토콜 거부 경로. **수정 전 15 failures** — 범위 밖 `argument` 7건이 전부 디코드에 성공했고,
  거부된 패킷이 out-param을 오염시킨 것이 4건, 그리고 **소비자 층 4건**: 위조 명령이 신뢰 채널 시퀀스를
  태워 정직한 클라이언트의 진짜 명령이 사라진다.
- **#7·#24** Vulkan present가 ctest에 등록됐다 — `mega_defense_gpu_selftest`(`mega_defense --gpu-selftest`,
  `SKIP_RETURN_CODE 77`이라 장치·창이 없는 환경에서는 SKIP으로 세고, 실패는 "장치도 창도 있는데 present가
  깨졌을 때"뿐이다).
- **#27** README 트리 누락 — `scripts/reachability.py --readme-tree`가 `CMakeLists.txt`에서 대조한다.
- **#29** `GenerateWaves` 테스트 전용 — `include/game/game.hpp:286`에
  "제품은 이 함수를 부르지 않는다. 그것이 설계다"로 근거를 명시해 닫았다.
- **#30** `snapshot_bytes` 수집만 하고 출력 안 함 — `include/game/defense_app.hpp`가 p95·max를 출력한다.
- **#31** `truncated` 플래그 — 제품이 와이어 flags의 `truncated`를 읽어 `snapshots_truncated` 카운터에
  반영하고, 테스트를 추가했다.
- **#14·#26** git 이력 — 저장소가 git 저장소가 됐다. **다만 커밋 축적은 여전히
  [남은 일 6번](#지금-바로-할-수-있는-것--8건)이다.**

**2026-08-14 최종 검증.**

- mega — 클린 빌드 2회, **ctest 56/56**(`tests/test_*.cpp` 52 + `mega_defense_selftest` +
  `mega_defense_gpu_selftest` + `layer_rules` + `reachability_gate`).
  P0 종료 조건 유지(변이 3건 전부 검출), 두 게이트 green.
- mini — 11개 lab 클린 재빌드 통과(12개 중 `system_kernel_lab`은 `VERIFY.md:29`에 따라 제외).
- 문서 — `python scripts/check_project_docs.py` → 문서 80개 / 앵커 118개 / **0 error 0 warning**.

### 2026-08-13에 닫힌 것 — mega P0 구조 게이트

**파일에서 확인한 것.** 이것도 할 일 목록이 아니다.

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
- 테스트 **51 → 55**. 감사가 "51개를 전부 통과했다"고 기록한 변이 3건(호스트 중복 억제 ·
  클라 스냅샷 순서 · 명령 재전송)이 이제 전부 검출된다.
  (2026-08-14에 `mega_defense_gpu_selftest`가 더해져 **56**이 됐다.)
- 문서 모순 해소 — `docs/AXIS_OWNERSHIP.md` · `docs/LAB_ONLY.md` ·
  빌드가 생성하는 `docs/PRODUCT_LAYERS.md` 신설.
