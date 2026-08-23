# 알고리즘 시리즈 지도

`algorithms/`는 문제 유형을 암기하는 코딩 테스트 모음이 아니다. 문제의 입력·출력·비용 모델을 정의하고, 불변식과 교환 논증으로 정확성을 증명하며, 표준 알고리즘 구조를 실제 게임 워크로드에 적용하는 공통 CS 축이다.

CLRS의 분석·증명 체계와 *The Algorithm Design Manual*의 문제 모델링·반례·catalog 관점을 결합한다. 각 장은 알고리즘 이름보다 적용 전제, 실패 조건, 검증 가능한 계약을 소유한다.

## 읽는 순서

| 순서 | 문서 | 핵심 질문 |
|---:|---|---|
| 1 | `part01_analysis_correctness.md` | 알고리즘이 맞고 충분히 빠르다는 주장을 어떻게 증명하는가 |
| 2 | `part02_sequence_order_selection.md` | 탐색·정렬·선택에서 순서와 경계 불변식을 어떻게 세우는가 |
| 3 | `part03_online_range_stream.md` | 반복 구간·스트림 질의를 어떤 누적 상태와 상각 비용으로 바꾸는가 |
| 4 | `part04_dictionary_string.md` | 사전과 문자열 검색에서 충돌·접두 상태·비신뢰 입력을 어떻게 다루는가 |
| 5 | `part05_graph_structure.md` | 연결성·의존성·component 구조를 어떤 순회 정보로 판정하는가 |
| 6 | `part06_shortest_path_search.md` | 비용과 휴리스틱의 어떤 전제가 최단 경로를 보장하는가 |
| 7 | `part07_greedy_exchange.md` | 되돌리지 않는 선택이 언제 최적이며 언제 반례를 갖는가 |
| 8 | `part08_dynamic_programming.md` | 미래에 충분한 상태를 어떻게 정의하고 상태 폭발을 어떻게 관리하는가 |
| 9 | `part09_combinatorial_search_assignment.md` | 지수 탐색과 flow·matching의 다항 구조를 어떻게 구분하는가 |
| 10 | `part10_lossless_compression.md` | 정보량·부호화·사전·파싱이 압축률과 복호 안전성을 어떻게 결정하는가 |
| 11 | `part11_property_fuzz_verification.md` | 예제 밖의 입력에서 알고리즘 계약을 어떻게 반증하고 재현하는가 |

## 개념 소유권

- 자료구조의 연산 불변식과 문제 해결 패턴은 `algorithms/`가 소유한다.
- 객체 수명, 메모리 배치, 캐시, 할당 전략은 `parts/`가 소유한다.
- 벡터·행렬·기하·수치 오차는 `math/`가 소유한다.
- 경로 탐색을 NPC 이동 공간과 행동 계층에 적용하는 문제는 `ai/`가 소유한다.
- 충돌·제약·적분에 알고리즘을 적용하는 문제는 `physics/`가 소유한다.

적용 문서는 보편 알고리즘의 전체 증명을 반복하지 않는다. 알고리즘 문서도 특정 물리·AI 구현을 대신 설명하지 않는다.

## 학습 방식

본문을 읽는 것만으로 알고리즘 설계 능력이 생기지는 않는다. 각 장은 다음 네 작업을 한 묶음으로 수행한다.

1. 문제를 입력·출력·제약·목적 함수로 다시 쓴다.
2. 핵심 불변식이나 교환 논증을 코드 없이 설명한다.
3. 단순 기준 구현과 최적화 구현을 각각 작성한다.
4. 무작위 입력뿐 아니라 증명의 전제를 깨는 최소 반례를 만든다.

장별 최소 구현 과제는 다음과 같다. 특정 문제 사이트의 풀이 순서를 뜻하지 않으며, 같은 원리를 다른 입력으로 설명할 수 있으면 된다.

| 장 | 필수 구현 | 필수 검증 |
|---:|---|---|
| 01 | 점화식 분석기 또는 상각 비용 계측 | 작은 입력 완전 탐색과 복잡도 증가율 비교 |
| 02 | `lower_bound`, stable merge sort, quickselect, LSD radix sort | 표준 구현과 differential test, 잘못된 비교자 반례, 패스 하나를 불안정하게 만든 radix 반례 |
| 03 | sliding maximum, Fenwick tree, segment tree | 모든 구간 직접 계산 oracle과 비교 |
| 04 | open-addressing map, KMP, Aho-Corasick | 충돌 hash와 naive 문자열 검색 oracle |
| 05 | BFS/DFS, SCC, DSU, MST | component·cycle·최소 비용 brute-force 검증 |
| 06 | Dijkstra, Bellman-Ford, A* | Floyd-Warshall 및 inconsistent heuristic 반례 |
| 07 | interval scheduling과 set-cover greedy | 완전 탐색 OPT와 gap 비교 |
| 08 | knapsack, LCS, interval 또는 bitmask DP | top-down·bottom-up·완전 탐색 비교 |
| 09 | backtracking CSP, max flow, bipartite matching | pruning 비활성 기준과 min-cut certificate |
| 10 | canonical Huffman 또는 작은 LZ decoder | round-trip·truncation·resource-limit 속성 |
| 11 | property generator와 state-machine model | shrinking 뒤 최소 재현 입력 보존 |

코딩 테스트 준비가 목적이라면 02~09의 구현을 제한 시간 안에 빈 파일에서 다시 작성한다. 커리큘럼의 완료 조건은 암기한 template이 아니라, 조건이 바뀌었을 때 기존 증명의 어느 단계가 깨지는지 설명하는 것이다.

## part11의 실행 자산 — 어느 lab에서 도는가

`algorithms/`에는 대응 lab이 없다. 다만 **part11(속성·퍼징 검증)만은 예외로 실행 자산을 갖는다** — 그 기법은 특정 자료구조가 아니라 *검증 방식*이라서, 다른 축의 lab이 자기 알고리즘을 검증하는 자리에 그대로 얹히기 때문이다. 아래가 그 소재다. 세 lab 모두 외부 라이브러리 없이 시드 루프 + 생성기 + 축소기 + 반례 출력으로 되어 있다.

| part11 절 | 실행 자산 | 무엇이 도는가 |
|---|---|---|
| §2 예제와 property<br>§3 입력 생성과 shrinking | `projects/mini/math_lab/tests/property.h` + `tests/core_tests.cpp` | property 13개 · 표본 42,300개. `math_core_tests --properties`. 반례는 축소 뒤 `--seed`·`--sample` 재현 명령으로 출력 |
| §3 shrinking (독립 하네스) | `projects/mini/physics_lab/tests/property_kit.h` + `tests/physics_property_tests.cpp` | property 5개(볼록쌍 1500표본 · 강체 장면 · bit 결정성). 탐욕+재시작 축소, 재현 명령 `physics_property_tests <case> <seed>` |
| §5 Metamorphic·differential | 같은 두 lab | math: SAT ⇔ GJK 판정 일치, 행렬 ⇔ 쿼터니언 샌드위치, 강체 변환·균등 스케일 불변. physics: SAT 깊이 ⇔ EPA 깊이 ⇔ 참조 MTD |
| §7 수치·확률 검증 | 같은 두 lab | 허용 오차를 통과시키려고 키우지 않고 float 단위 반올림(2⁻²⁴) 횟수에서 유도한다. 각 property의 `contract` 문자열이 그 근거를 적는다 |
| §9 비신뢰 입력과 자원 상한 | `projects/mini/languages_lab/src/fuzz.hpp` + `src/tests.cpp` | fuzz 배터리 4개 · 표본 80,000개. 문법 유도 생성 · 토큰/opcode 변형. 실행 예산·스택 상한·검증기 거부 어휘·진단 위치를 친다 |
| §10 지속 가능한 검증 절차 | `projects/mega` — `tests/test_profile_isolation.cpp` | 검증이 실행 순서에 의존하지 않게 만드는 자리. 누적이 제품 기능임을 확인하고, `--profile PATH --fresh-profile`이 실행 횟수와 무관하게 같은 결과를 내는지, **그 인자를 빼면 실제로 갈리는지**(음성 대조)까지 본다 |

**여기에 없는 것.** §4 상태 기계·model-based testing과 §6 coverage-guided fuzzing은 이 저장소에 실행 자산이 없다. 세 lab의 하네스는 coverage 되먹임도 sanitizer corpus도 쓰지 않고 고정 시드에서 결정론적으로 입력을 만들 뿐이다 — `math_lab/tests/property.h`가 자기 주석에 그렇게 못박아 두었다. §8 mutation testing은 자동화 도구가 아니라 **손으로 도는 규율**로 있다. 각 step 문서의 `### 무력화 변이` 절이 그 기록이다 — 어떤 한 줄을 어떻게 망가뜨렸고 어느 검사가 어떤 메시지로 죽었는지가 적혀 있다(예: `math_lab/articles/step05_collision.md`, `languages_lab/articles/step03_bytecode_verifier.md`). 변이는 확인 뒤 복원하고 SHA-256으로 대조한다.

## 완료 기준

- 문제를 입력·출력·가능성·목적 함수로 명세할 수 있다.
- 시간·공간 비용을 연산 모델과 입력 parameter로 유도할 수 있다.
- Loop 불변식, 귀납, 교환 논증, cut·augmenting 구조로 정확성을 설명할 수 있다.
- Greedy·DP·backtracking·graph·flow 중 어떤 구조가 맞는지 반례로 구분할 수 있다.
- 작은 기준 구현, property, differential test, fuzzing으로 구현을 검증할 수 있다.
- 평균 처리량과 최악·P99 지연, 메모리 peak, 품질 gap을 분리해 측정할 수 있다.
