# algorithms_lab — 알고리즘의 불변식과 A/B 측정

## 컨셉

이 lab은 **알고리즘을 직접 구현하고, 그 구현이 지켜야 할 불변식을 느린 독립 구현과 대조해 확인하고, 같은 문제에 대한 서로 다른 팔을 통제된 워크로드에서 비교**하는 도구다. GUI 없이 CLI로 돈다 — 측정 대상이 화면이 아니라 작업량과 시간이다.

외부 의존성이 없다. 학습 대상이 알고리즘 자체이므로 라이브러리를 끌어오면 검증할 것이 사라진다. 표준 라이브러리는 오라클(`std::sort`, `std::next_permutation` 등)로만 쓴다.

확인하는 주장:

1. **알고리즘의 보장은 입력의 성질에서 나온다.** 음수 간선 하나가 Dijkstra의 확정 논리를 깨고, 동전 `{1,3,4}`가 탐욕의 최적성을 깨고, 비교자가 엄격 약순서를 어기면 분할이 경계를 넘는다. 각 주장마다 그것을 깨는 입력을 손으로 만들어 검사에 넣는다.
2. **점근은 답을 주지 않는 구간이 있다.** radix의 최적 기수는 점근이 가리키는 17이 아니라 8~11이고, n이 수십일 때는 선형 스캔이 Fenwick tree를 이긴다. 그 경계를 실측으로 찍는다.
3. **일부 결함은 답을 바꾸지 않는다.** Dijkstra의 stale 항목 처리, DSU의 union by size — 이것들은 정확성 검사로 잡히지 않고 계측기로만 드러난다. 검사가 못 잡는 것을 검사가 잡는 척하지 않는다.

## 짝 시리즈

- **algorithms** — 정렬·선택, 온라인·구간 질의, 사전·문자열, 그래프 구조, 최단 경로, 탐욕, 동적 계획, 조합 탐색, 무손실 압축을 다루는 시리즈. 유도와 불변식의 *의미*는 그쪽이 소유하고, 이 lab은 그 불변식을 **실행 가능한 판정**으로 만든다.

## 구성 요소와 측정 항목

| 실행 파일 | 무엇을 하나 | 측정·검사 |
|---|---|---|
| `algo_tests` | 고정 사례 45개 (ctest) | 불변식·오라클 대조. `--step=NN`으로 소속 step만 |
| `algo_tests --properties` | 시드 무작위 property 11종 | 생성기·축소기·반례 재현 명령 |
| `algo_bench` | A/B 팔 측정 | 비교 횟수·이동 바이트·방문 노드·probe 길이·P50/P95/P99·임시 peak |

### 소스 구성

```text
algorithms_lab/
  CMakeLists.txt
  src/
    algo/
      order.hpp      이진 탐색, introsort, 안정 merge, counting, LSD radix, 순서 보존 실수 키,
                     quickselect, median-of-medians, 크기 제한 힙 top-k
      range.hpp      접두합 1D/2D, 차분, 두 포인터, 단조 deque/stack, 이진 힙, 좌표 압축,
                     Fenwick(binary lifting 포함), lazy segment tree, 최대 부분합 트리
      dict.hpp       chaining, open addressing 3정책(tombstone/backward-shift/Robin Hood),
                     seed 키드 해시, Bloom filter
      text.hpp       trie(자식 표현 2종), KMP, Z, BMH, Aho-Corasick, rolling hash
      graph.hpp      CSR, BFS, 반복 DFS, 순환 판정, Kahn 위상 정렬, Tarjan/Kosaraju SCC,
                     절단점·다리, DSU(최적화 2종 토글), Kruskal, Prim
      paths.hpp      BFS 거리, 0-1 BFS, DAG 완화, Dijkstra(힙/dense), Bellman-Ford,
                     격자 A*, 임의 그래프 A*, 일관성 검사기
      greedy.hpp     활동 선택, EDD, 분할 가능/밀도 배낭, 집합 덮개, 동전 교환, 스키 대여
      dp.hpp         0/1 배낭(2D·1D), LCS+복원, 편집 거리, LIS 2판, 행렬 연쇄,
                     트리 최대 가중 독립 집합, 비트마스크 순회
      flow.hpp       residual, Ford-Fulkerson, Edmonds-Karp, Dinic, min-cut,
                     이분 매칭 3판, 최소 비용 유량, Hungarian
      backtrack.hpp  변경 로그 CSP(전방 검사·AC-3·MRV·대칭 깨기), 분기 한정
      compress.hpp   엔트로피, canonical Huffman(+길이 상한), lookup 복호표,
                     LZ77(hash chain, greedy/lazy/optimal), 프레임 형식, 산술 부호기
    measure/
      counters.hpp   작업량 계측기 + 백분위 표본 + 단조 시계
      workload.hpp   적대 입력 생성기(정렬 모양 10종, 실수 경계값, 키 모양 4종,
                     Hall 위반 이분 그래프, 압축 코퍼스 8종)
    algo_bench.cpp   A/B 측정 러너
  tests/
    oracle.h         느린 기준 구현 (전수 열거·정의 그대로의 재계산,
                     Floyd-Warshall 전 쌍 거리)
    property.h       고정 시드 생성기·축소기·반례 보고
    algo_tests.cpp   검증 배터리 (kCases 표가 step 소속을 정한다)
```

## 짝 시리즈 대비 A/B 팔 표

| 영역 | 팔 | 통제 변수 | 관측 |
|---|---|---|---|
| 정렬 | introsort · 안정 merge · counting · radix8 · radix11 | 같은 입력 복사본, 모양 10종 | 비교·이동 바이트·P50/P95/P99·임시 peak |
| 선택 | quickselect · median-of-medians · 크기 제한 힙 · 전체 정렬 | 같은 배열, k/n 3점 | 시간·비교 |
| 우선순위 큐 | 이진 힙 · 정렬 배열 | 같은 키 열 | 시간·비교·이동 |
| 구간 질의 | 선형 스캔 · Fenwick · segment tree | 같은 연산 열, n 4점 × 질의비율 2점 | 시간 |
| 사전 | chaining · linear+tombstone · linear+backshift · Robin Hood | 같은 키, load 3점, 절반 삭제 | 성공/실패 probe 평균·P99·최대 군집 |
| 단일 패턴 | naive · KMP · Z · BMH | 같은 텍스트, 알파벳 2점 × 패턴 길이 2점 | 시간·비교 |
| 다중 패턴 | 패턴별 KMP · Aho-Corasick | 같은 텍스트, 패턴 수 3점 | 시간·상태 수 |
| SCC | Tarjan · Kosaraju | 같은 그래프 | 시간·성분 수 |
| MST | Kruskal · Prim(dense) | 같은 그래프, 밀도 3점 | 시간·비용 |
| DSU | {경로 압축} × {union by size} | 같은 합병 열 | 평균 find 깊이·시간 |
| 최단 경로 | Dijkstra(힙) · Dijkstra(dense) · Bellman-Ford · 재확장 | 같은 그래프, 밀도 2점 | 시간·확장·push·stale pop |
| A* | 휴리스틱 3종 × 가중치 3점 × 재개방 2점, tie-break 3종 | 같은 격자 | 시간·확장·재개방·큐 peak·최적 대비 비용 |
| 배낭 | 2D DP · 1D DP · 분기 한정 · 밀도 탐욕 | 같은 물건, 용량 2점 | 시간·상태 수·표 바이트·gap |
| 유량 | Ford-Fulkerson · Edmonds-Karp · Dinic | 같은 그래프 | 시간·증가 횟수·phase |
| 매칭 | DFS 증가 · Hopcroft-Karp · 유량 환원 | 같은 이분 그래프 | 시간·크기·증가·phase |
| CSP | plain · +전방 검사 · +AC-3 · +MRV · +대칭 깨기 | 같은 문제 | 시간·노드·해 수·가지치기 이유별 |
| 압축 | Huffman · 산술 · LZ-greedy · LZ-optimal | 같은 코퍼스 8종 | 압축률·space saving·MB/s·header 바이트 |
| 블록 | 4KiB · 64KiB · 1MiB | 같은 텍스트 | 총 바이트·압축률·p99 블록 지연 |

## 테스트가 실제로 무엇을 잡는가

각 검사는 '이 구현 결함이 들어오면 빨개진다'는 대응을 가진다. 아래 대응은 소스에 결함을 직접 넣어 확인한 것이고, 복원 뒤 SHA-256이 동일함을 매번 대조했다.

| 결함 | 잡는 검사 |
|---|---|
| radix 흩뿌리기를 역순으로 (안정성 파괴) | `order_sort_contract` |
| `OrderedKey`의 부호 분기 제거 | `order_ordered_key` |
| lazy tag 합성에서 대입이 앞의 덧셈을 지우지 않음 | `range_segment_tree_algebra` |
| Aho-Corasick failure link를 전부 root로 | `text_aho_corasick` |
| rolling hash의 바이트 확인 제거 | `text_rolling_hash_verify` |
| 절단점 조건 `>=`를 `>`로 | `graph_articulation_bridges` |
| Bellman-Ford의 도달 불가 가드 제거 | `paths_overflow_guard` |
| A* 재개방 금지 줄 무력화 | `paths_inconsistent_heuristic` |
| 0/1 배낭의 이전 행 참조를 현재 행으로 | `dp_knapsack_arms` · `dp_direction_changes_problem` |
| residual 짝 간선 갱신 제거 | `flow_matching_arms` |
| CSP 되돌리기에서 값 복원 누락 | `csp_apply_undo` |
| canonical 배정의 길이 증가 shift 제거 | `compress_huffman_roundtrip` · `compress_canonical_prefix` · `compress_decoder_hostile` |
| LZ 겹침 복사를 고정 크기 사본으로 | `compress_lz_roundtrip` |
| `KraftOk`를 항상 true로 | `compress_canonical_prefix` |

### 검사가 잡지 못하는 것 (기록)

정직성을 위해 그대로 남긴다.

- **DSU의 union by size 제거.** 네 조합이 전부 같은 연결 판정을 내므로 어떤 정확성 검사도 죽지 않는다. 최적화는 정의상 답을 바꾸지 않는다. 대신 `algo_bench --graph`의 평균 find 깊이가 1.00 대 9,999.50으로 갈린다.
- **Dijkstra의 lazy deletion 처리 제거.** 낡은 항목 건너뛰기와 확정 표시를 둘 다 끄면(`algo_bench --paths`의 `dijkstra(re-expand)` 팔) 비용이 음이 아닌 그래프에서 답은 그대로다. 드러나는 것은 확장 수뿐이고, `settled` 칸이 1,200 대 3,322로 그것을 찍는다.
- **`HuffmanDecompress`의 Kraft 호출 제거.** 이 프레임 형식에는 Adler-32가 있어서, 접두 부호가 아닌 표로 복호한 결과는 어차피 checksum에서 걸린다. Kraft가 하는 일은 *더 일찍, 더 싸게* 거부하는 것이지 거부 자체를 만드는 것이 아니다. (`KraftOk` 함수 자체를 무력화하면 `compress_canonical_prefix`가 잡는다.)

## 빌드와 실행

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/algo_bench
```

Windows(MSVC)에서는 `cmake --build build --config Release`, 실행 파일은 `build/Release/`, ctest는 `ctest --test-dir build -C Release --output-on-failure`. 외부 의존성이 없다 — C++20 컴파일러와 CMake만 있으면 된다.

ctest에 등록된 대상은 열하나다.

| 테스트 이름 | 명령 | step | 걸리는 시간 |
|---|---|---|---|
| `algo_tests` | `./build/Release/algo_tests` | — (전체) | 1초 안팎 |
| `algo_tests_step01` | `./build/Release/algo_tests --step=01` | 순서와 선택 | 0.1초 미만 |
| `algo_tests_step02` | `./build/Release/algo_tests --step=02` | 온라인·구간·스트림 | 0.1초 미만 |
| `algo_tests_step03` | `./build/Release/algo_tests --step=03` | 사전과 문자열 | 0.1초 미만 |
| `algo_tests_step04` | `./build/Release/algo_tests --step=04` | 그래프 구조 | 0.1초 미만 |
| `algo_tests_step05` | `./build/Release/algo_tests --step=05` | 최단 경로와 휴리스틱 | 0.1초 미만 |
| `algo_tests_step06` | `./build/Release/algo_tests --step=06` | 탐욕과 동적 계획 | 0.1초 미만 |
| `algo_tests_step07` | `./build/Release/algo_tests --step=07` | 유량·매칭·탐색 | 0.1초 미만 |
| `algo_tests_step08` | `./build/Release/algo_tests --step=08` | 무손실 압축 | 0.1초 안팎 |
| `algo_properties` | `./build/Release/algo_tests --properties` | 전 step | 0.3초 안팎 |
| `algo_bench_smoke` | `./build/Release/algo_bench --smoke` | 전 step | 0.4초 안팎 |

step별 실행은 전체 실행을 대체하지 않고 *더한다*. 어느 검사가 어느 step 소속인지는 `tests/algo_tests.cpp`의 `kCases` 표가 정한다.

```text
$ ./build/Release/algo_tests --step=04
  ok   graph_bfs_dfs_invariants
  ok   graph_cycle_and_topo
  ok   graph_scc_arms
  ok   graph_articulation_bridges
  ok   graph_dsu_arms
  ok   graph_mst_arms
algo_tests[step04]: 6/6 passed (349 check(s))
```

`algo_bench --smoke`는 표본을 줄인 축소 실행이라 측정값으로는 쓸 수 없다. 등록 이유는 벤치가 각 팔에 걸어 둔 실패 검사(답이 어긋나면 즉시 비정상 종료)를 매 ctest 실행에서 지나가게 하기 위해서다. 수치를 보려면 인자 없이 `./build/Release/algo_bench`를 직접 돌린다(이 기계에서 14초 안팎).

기대 출력:

```text
algo_tests: all passed (45 case(s), 520876 check(s))
algo_tests[properties]: 11/11 passed (총 6950 표본, seed 20260824)
100% tests passed out of 11
```

### property 층의 재현

property가 실패하면 반례를 축소해 시드·축소 입력·재현 명령을 함께 찍는다. 아래는 `Residual::Push`의 짝 간선 갱신을 지우고 실제로 얻은 출력이다.

```text
  FAIL flow/maxflow-mincut          표본 31

PROPERTY 반례 [flow/maxflow-mincut] 표본 30
  계약: 세 유량 팔이 같은 값을 내고 그 값이 잔여 도달 집합의 절단 용량과 같다
  실패: max-flow == min-cut (관측 48, 기대 44)
  생성: n=5 seed=1 density=90
  축소: n=5 seed=1 density=50 (1 걸음)
  재현: algo_tests --properties --only=flow/maxflow-mincut --seed=20260824 --sample=30

algo_tests[properties]: 1/1 실패
```

유량이 48을 보고하는데 절단 용량은 44다. 역간선으로 되돌릴 수 없게 되자 알고리즘이 실제 최대보다 큰 값을 냈고, certificate가 그것을 잡았다. 첫 줄의 `표본 31`은 실패까지 *돌린* 표본 수이고, 반례의 `표본 30`은 그 표본의 0-based 번호다.

마지막 줄의 명령이 그 표본 하나만 다시 돌린다.

```text
$ algo_tests --properties --only=flow/maxflow-mincut --seed=20260824 --sample=30
  ok   flow/maxflow-mincut          표본     1 · 비퇴화 100% · 건너뜀 0
algo_tests[properties]: 1/1 passed (총 1 표본, seed 20260824)
```

`--scale`로 표본을 배수만큼 늘릴 수 있고, `--step=NN`으로 그 step 소속 property만 고를 수 있다.

## 결과 해석 가이드

- **절대 시간은 하드웨어·컴파일러에 종속된다.** 이 lab이 보장하는 것은 특정 배수가 아니라 측정 방법, 통제 변수, 교차점을 찾는 절차다. 팔 사이의 *비율*과 교차점의 *존재*가 재현 대상이고, 그 위치는 기계마다 이동한다.
- **비교 횟수·이동 바이트·방문 노드는 기계 독립이다.** 시간이 달라도 이 수치들은 같아야 한다. 두 실행에서 이 값이 다르면 워크로드가 결정적이지 않다는 뜻이다.
- **`checksum` 줄을 지우지 마라.** 벤치 마지막의 checksum 출력이 없으면 최적화기가 측정 대상 계산을 죽은 코드로 지울 수 있고, 그러면 표의 시간이 아무것도 뜻하지 않는다.
- **`-` 칸은 "0"이 아니라 "재지 않음"이다.** counting sort의 키 범위가 넓어 돌리지 않은 행, 비교 계측을 달지 않은 Z 배열 팔이 그렇다.
- **해시 표의 `요청 load`와 `실제 load`를 함께 읽어야 한다.** 표 크기가 2의 거듭제곱이라 용량이 양자화되고, 서로 다른 요청이 같은 용량으로 접히면 두 행이 완전히 같은 수치를 낸다.
- **property의 `비퇴화` 비율이 하한 아래면 실패로 처리한다.** 자명하게 통과하는 표본만 모이면 property는 통과하면서도 아무것도 강제하지 않는다.

## 구현 범위 메모

이 lab이 **소유하지 않는 것**을 적어 둔다.

- **suffix automaton · suffix array 계열.** 텍스트를 전처리해 두고 임의 패턴을 답하는 구조인데, 여기 팔은 전부 패턴을 먼저 알고 텍스트를 지나간다. 전처리 대상이 반대다.
- **ANS(asymmetric numeral systems)와 문맥 혼합.** 두 엔트로피 부호기는 정적 0차 모델뿐이고 빈도표를 프레임에 싣는 대가를 그대로 문다.
- **병렬·외부 메모리 정렬.** 모든 팔이 "키가 메모리에 다 들어간다"를 전제한다.
- **coverage-guided fuzzing.** property 층은 시드에서 결정론적으로 입력을 만들 뿐 되먹임이 없다. 깊은 파서 상태를 여는 일은 이 하네스의 몫이 아니다.
- **push-relabel 유량과 일반 그래프 매칭.** 세 유량 팔이 공유하는 "증가 경로" 골격 밖이다.
- **동적 연결성 · 영속 자료구조.** 여기 트리와 그래프는 전부 정적 크기다.
- **전처리 기반 경로 가속(contraction hierarchies, ALT).** 질의마다 처음부터 시작하는 팔만 있다.

성능 수치는 이 기계(Windows, MSVC Release)의 것이다. 다른 컴파일러·플랫폼에서 교차점의 위치가 이동하는 것은 결함이 아니라 관찰 대상이다.
