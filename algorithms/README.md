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

## 완료 기준

- 문제를 입력·출력·가능성·목적 함수로 명세할 수 있다.
- 시간·공간 비용을 연산 모델과 입력 parameter로 유도할 수 있다.
- Loop 불변식, 귀납, 교환 논증, cut·augmenting 구조로 정확성을 설명할 수 있다.
- Greedy·DP·backtracking·graph·flow 중 어떤 구조가 맞는지 반례로 구분할 수 있다.
- 작은 기준 구현, property, differential test, fuzzing으로 구현을 검증할 수 있다.
- 평균 처리량과 최악·P99 지연, 메모리 peak, 품질 gap을 분리해 측정할 수 있다.
