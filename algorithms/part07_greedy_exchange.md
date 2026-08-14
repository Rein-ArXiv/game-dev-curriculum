## 들어가며

Greedy 알고리즘은 현재 가장 좋아 보이는 선택을 하고 되돌리지 않는다. 구현은 짧지만 정확성은 가장 쉽게 오해된다. 예제 몇 개에서 잘 작동한다는 사실은 greedy choice가 최적해로 확장된다는 증거가 아니다. 동전 액면, 작업 보상, 자원 배치 조건이 조금만 바뀌어도 같은 규칙이 최적성을 잃는다.

Greedy가 맞는 문제에는 공통 구조가 있다. 어떤 최적해의 첫 선택을 greedy 선택으로 바꿔도 나머지 가능성을 잃지 않거나, 독립 집합이 교환 성질을 만족한다. 정확한 최적화가 어려운 문제에서는 greedy가 최적은 아니어도 근사비나 competitive ratio를 제공할 수 있다. 이 경우에도 “대체로 좋다”가 아니라 품질 경계를 계약한다.

이 글은 greedy 설계의 선택·가능성·목적 함수를 분리한다. 활동 선택과 deadline scheduling의 교환 논증, matroid의 구조, knapsack·동전·set cover의 성공과 반례를 분석하고, 실시간 게임의 우선순위 budget과 online 결정에서 품질을 검증하는 방법을 정리한다.

---

## 목차
1. [Greedy 문제의 세 요소](#1-greedy-문제의-세-요소)
2. [활동 선택과 교환 논증](#2-활동-선택과-교환-논증)
3. [Scheduling과 지연 최소화](#3-scheduling과-지연-최소화)
4. [Fractional Knapsack과 0/1 반례](#4-fractional-knapsack과-01-반례)
5. [Matroid와 교환 구조](#5-matroid와-교환-구조)
6. [Greedy가 실패하는 전형](#6-greedy가-실패하는-전형)
7. [근사 알고리즘과 Set Cover](#7-근사-알고리즘과-set-cover)
8. [Online Greedy와 Competitive Analysis](#8-online-greedy와-competitive-analysis)
9. [게임의 Budget 할당](#9-게임의-budget-할당)
10. [검증과 반례 생성](#10-검증과-반례-생성)

---

## 1. Greedy 문제의 세 요소

최적화 문제는 다음 세 요소를 가져야 한다.

```text
후보: 선택할 수 있는 원소는 무엇인가?
가능성: 어떤 부분집합 또는 순서가 제약을 만족하는가?
목적: 가능한 해의 무엇을 최소화하거나 최대화하는가?
```

Greedy 규칙은 현재 부분해에서 후보 하나를 고르는 local criterion이다. 정확하려면 두 조건이 필요하다.

- Greedy choice property: 어떤 최적해가 greedy 선택을 포함하도록 바꿀 수 있다.
- Optimal substructure: 그 선택 뒤 남은 부분 문제가 최적으로 풀리면 전체도 최적이다.

두 조건은 이름으로 확인할 수 없다. 교환 논증, cut property, stays-ahead argument 같은 증명이 필요하다.

### Greedy 알고리즘의 상태

일반 형태는 다음과 같다.

```text
후보를 local key 순으로 고려한다.
현재 부분해에 추가해도 feasible하면 채택한다.
되돌리지 않는다.
```

정렬 기준과 feasibility 검사 비용을 포함하면 실행 시간은 흔히 $O(n\log n)$이지만, feasibility가 비싸면 그보다 커진다. 알고리즘 복잡도는 “greedy”라는 분류가 아니라 실제 연산으로 계산한다.

## 2. 활동 선택과 교환 논증

각 활동 $i$가 반열린 시간 구간 $[s_i,f_i)$를 차지한다. 서로 겹치지 않는 활동 수를 최대화한다.

종료 시간이 가장 빠른 활동을 먼저 선택하고, 이후 시작 시간이 마지막 종료 이상인 활동 중 다시 가장 빨리 끝나는 것을 고른다.

```cpp
std::vector<Activity> SelectActivities(std::vector<Activity> a) {
    std::sort(a.begin(), a.end(), [](const Activity& x, const Activity& y) {
        if (x.finish != y.finish) return x.finish < y.finish;
        return x.id < y.id;
    });

    std::vector<Activity> chosen;
    int available = std::numeric_limits<int>::min();
    for (const Activity& x : a) {
        if (x.start < available) continue;
        chosen.push_back(x);
        available = x.finish;
    }
    return chosen;
}
```

### 교환 증명

종료 시간이 가장 빠른 활동을 $g$, 임의의 최적해 첫 활동을 $o$라 하자. 정의상 $f_g\le f_o$다. 최적해에서 $o$를 $g$로 바꾸면 뒤 활동들은 원래 $o$ 종료 뒤 시작했으므로 더 이른 $g$ 종료 뒤에도 가능하다.

따라서 $g$를 포함하는 최적해가 존재한다. $g$와 겹치는 활동을 제거한 나머지 문제에도 같은 논리를 반복할 수 있어 greedy 해가 최적이다.

### 다른 기준의 반례

- 가장 일찍 시작하는 활동: 아주 긴 활동 하나가 나머지를 막을 수 있다.
- 길이가 가장 짧은 활동: 중앙의 짧은 활동이 양쪽의 호환 가능한 두 활동을 동시에 막을 수 있다.
- 겹치는 활동 수가 가장 적은 활동: local overlap 수가 미래 조합 수를 정확히 나타내지 않는다.

반례는 규칙의 약점을 드러내는 최소 입력이어야 한다. 큰 무작위 입력보다 3~5개 구간의 구조적 반례가 더 설명력이 높다.

## 3. Scheduling과 지연 최소화

단일 machine에서 작업 $i$의 처리 시간 $p_i$, deadline $d_i$가 있고 완료 시간 $C_i$라 하자. 최대 지연

$$
L_{max}=\max_i(C_i-d_i)
$$

을 최소화하려면 deadline이 빠른 순서(EDD)로 처리한다.

### Inversion 교환

어떤 schedule에서 인접한 두 작업 $i,j$가 $d_i>d_j$인데 $i$가 먼저라면 inversion이다. 두 작업을 바꿔도 다른 작업 완료 시간은 변하지 않는다. 교환 뒤 더 이른 deadline 작업 $j$는 더 일찍 완료되고, $i$는 원래 $j$ 완료 시각에 끝난다.

두 작업이 만드는 최대 지연은 증가하지 않는다. 모든 inversion을 제거하면 deadline 순서가 되고, 최적 schedule을 그 순서로 변환할 수 있다.

### 목적 함수가 바뀌면 규칙도 바뀐다

- 평균 완료 시간 최소화: 처리 시간이 짧은 작업 우선(SPT)이 맞는 조건이 있다.
- 가중 완료 시간 최소화: $p_i/w_i$ 순서가 맞는 단일 machine 조건이 있다.
- deadline을 지킨 작업 수 최대화: 다른 선택·제거 과정이 필요하다.
- precedence와 병렬 machine: 단순 정렬 규칙이 일반적으로 최적이 아니다.

“작업 scheduling”이라는 이름만 같고 목적 함수와 제약이 다르면 전혀 다른 문제다.

## 4. Fractional Knapsack과 0/1 반례

항목 $i$의 가치 $v_i$, 무게 $w_i$가 있고 용량 $W$가 있다.

### Fractional knapsack

항목 일부를 나눠 담을 수 있으면 가치 밀도 $v_i/w_i$가 높은 순서가 최적이다. 최적해에 낮은 밀도 항목 일부가 있고 더 높은 밀도 항목에 남은 양이 있다면 같은 무게를 교환해 가치를 늘릴 수 있다. 따라서 밀도 역전이 없는 해가 최적이다.

### 0/1 knapsack

항목을 통째로만 선택하면 교환이 불가능해진다.

```text
용량 50
(무게 10, 가치 60) 밀도 6
(무게 20, 가치 100) 밀도 5
(무게 30, 가치 120) 밀도 4
```

밀도 greedy는 첫 두 항목 가치 160을 얻지만, 뒤 두 항목을 고르면 가치 220이다. Fractional 문제의 증명에서 사용한 “같은 무게만큼 교환”이 0/1 제약 때문에 불가능하다.

문제 조건의 작은 변화가 greedy proof를 어느 단계에서 깨뜨리는지 찾아야 한다.

## 5. Matroid와 교환 구조

Matroid는 greedy가 모든 비음수 weight에 대해 최대 weight 독립 집합을 찾을 수 있는 구조를 추상화한다. Ground set $E$와 독립 집합 family $\mathcal I$가 다음을 만족한다.

1. $\emptyset\in\mathcal I$.
2. $A\in\mathcal I$이고 $B\subseteq A$이면 $B\in\mathcal I$.
3. $A,B\in\mathcal I$, $|A|<|B|$이면 어떤 $b\in B\setminus A$가 있어 $A\cup\{b\}\in\mathcal I$.

세 번째가 augmentation 또는 exchange property다. 더 큰 feasible 집합에서 원소 하나를 가져와 작은 feasible 집합을 확장할 수 있다.

### Graphic matroid

무방향 그래프의 간선 집합에서 cycle이 없는 forest들을 독립 집합으로 보면 matroid다. 비용이 작은 간선부터 cycle을 만들지 않게 고르는 과정이 최소 신장 구조와 연결된다.

### Partition matroid

원소를 여러 category로 나누고 각 category에서 최대 $k_i$개를 고르는 제약도 matroid다. Slot별 한도나 장비 category 선택처럼 보이지만, category 사이 추가 조합 제약이 생기면 matroid가 아닐 수 있다.

### 왜 구조가 중요한가

Greedy가 예제에서 성공하는지보다 feasible family가 exchange property를 갖는지 묻는 편이 강하다. 두 matroid 제약을 동시에 만족해야 하는 문제는 단순 greedy로 풀리지 않을 수 있다. 제약 하나를 추가하는 순간 증명을 다시 해야 한다.

## 6. Greedy가 실패하는 전형

### 동전 교환

표준적인 일부 화폐 체계에서는 가장 큰 동전부터 고르는 규칙이 최소 동전 수를 준다. 그러나 동전 `{1,3,4}`로 금액 6을 만들면 greedy는 `4+1+1` 세 개, 최적은 `3+3` 두 개다.

Canonical coin system인지 증명되지 않은 임의 게임 화폐에 greedy를 적용하면 안 된다.

### Local choice가 미래를 차단

가장 가까운 목표를 항상 선택하는 agent는 전체 이동 경로가 길어질 수 있다. 지금 짧은 edge가 이후 비싼 되돌아가기를 만들기 때문이다. 목적이 단일 다음 이동이 아니라 전체 방문 순서라면 state는 방문 집합까지 포함한다.

### 비독립 비용

선택 비용이 다른 선택에 따라 바뀌면 고정 key 정렬이 구조를 놓친다.

- 같은 material draw를 연속 배치하면 전환 비용 감소
- 같은 지역 asset을 묶으면 I/O locality 증가
- 한 tower upgrade가 다른 tower 효과를 증폭

Marginal gain을 매번 재계산하는 greedy가 필요할 수 있고, 그조차 최적성은 submodularity 같은 조건에 달려 있다.

### Tie-break의 영향

Greedy key가 같은 후보가 많으면 tie-break가 최종 품질과 결정성을 바꿀 수 있다. 최적성 증명이 모든 동률 선택에 대해 유지되는지, 특정 tie-break만 필요한지 구분한다.

## 7. 근사 알고리즘과 Set Cover

정확한 최적해가 계산상 너무 비싸면 polynomial-time 근사 알고리즘이 품질 경계를 제공할 수 있다.

최소화 문제에서 알고리즘 해 비용 $ALG(I)$가 모든 입력 $I$에 대해

$$
ALG(I)\le\rho\,OPT(I)
$$

이면 $\rho$-approximation이다.

### Set cover greedy

Universe $U$의 모든 원소를 집합 family $S_1,\dots,S_m$로 덮는다. 매 단계 아직 덮지 않은 원소를 가장 많이 덮는 집합을 선택한다. 집합별 비용이 있으면 새로 덮는 원소당 비용이 가장 작은 집합을 고른다.

이 greedy는 최적을 항상 찾지는 않지만 harmonic factor $H_{|U|}\le \ln |U|+1$의 근사 경계를 가진다. 직관적으로 아직 $r$개가 남았을 때 최적해의 어떤 집합은 평균적으로 충분한 수를 덮어야 하며, greedy는 그보다 나쁘지 않은 효율을 고른다. 남은 수가 줄어드는 각 단계의 비용을 합하면 harmonic sum이 나온다.

Spawn 지점을 감시하는 최소 센서 배치나 테스트 case가 기능 집합을 덮는 문제로 모델링될 수 있다. 그러나 실제 시야가 연속 공간이고 품질이 binary cover가 아니면 모델 오차가 더 클 수 있다.

### 근사비와 실측 품질

최악 근사비가 보수적이어도 보장이 없는 heuristic보다 운영 계약이 분명하다. 동시에 실제 입력에서는 다음을 측정한다.

```text
작은 instance의 OPT 대비 gap
평균·P95·최악 gap
실행 시간별 품질 곡선
seed와 tie-break에 따른 분산
```

## 8. Online Greedy와 Competitive Analysis

Online 알고리즘은 미래 입력을 모른 채 현재 결정을 내려야 한다. Offline 최적해는 전체 입력을 본다는 이점이 있으므로 단순 최적성 비교가 불공정하다.

Online 알고리즘 $A$가 모든 입력 sequence $\sigma$에 대해

$$
A(\sigma)\le c\cdot OPT(\sigma)+\alpha
$$

를 만족하면 최소화 문제에서 $c$-competitive라 한다. $\alpha$는 입력과 무관한 상수다.

### Rent-or-buy 구조

Asset을 매번 stream할지 메모리에 오래 유지할지처럼, 반복 비용과 한 번의 큰 비용 사이 결정이 있다. 미래 재사용 횟수를 모르기 때문에 threshold 정책을 둘 수 있다. Competitive analysis는 어떤 sequence에서도 hindsight 최적 대비 비용이 얼마나 커질 수 있는지 본다.

### Adversarial sequence

평균 workload trace만 사용하면 online 정책이 특정 과거 분포에 overfit될 수 있다. 정책이 결정을 내린 직후 입력이 끊기거나 폭증하는 sequence를 만들어 최악을 본다.

Randomized online 알고리즘은 고정된 adversary에 대한 기대 비용을 낮출 수 있지만 seed·적대 모델·재현성 조건을 명시해야 한다.

## 9. 게임의 Budget 할당

### 우선순위 업데이트

한 프레임에 모든 AI·animation·streaming 작업을 처리할 수 없으면 benefit/cost 점수로 선택할 수 있다. 그러나 다음 조건을 확인해야 한다.

```text
작업을 부분 수행할 수 있는가?
benefit이 다른 작업 선택과 독립적인가?
deadline을 넘긴 작업의 비용은 무엇인가?
계속 낮은 점수인 작업이 starvation되는가?
```

부분 수행 가능한 작업은 fractional 모델에 가깝지만, atomic job은 0/1 제약이다. 단순 benefit/cost greedy가 atomic budget 최적을 보장하지 않는다.

### Relevancy와 bandwidth

네트워크 update를 우선순위로 packet budget에 채우는 정책도 greedy knapsack 형태다. Entity update 크기가 다르고 일부만 보낼 수 없다면 최적 보장은 없다. 오래 전송되지 않은 entity의 priority를 aging해 starvation을 막는 것은 목적 함수를 시간에 따라 바꾸는 정책이다.

### LOD와 품질 단계

각 객체가 여러 LOD level을 갖고 한 단계 상승의 비용과 품질 이득을 알 수 있다면 marginal upgrade를 후보로 만들 수 있다. 그러나 높은 단계가 낮은 단계 선택을 전제로 하는 precedence를 지켜야 한다. 후보 heap은 현재 가능한 다음 upgrade만 포함해야 한다.

### 안전한 사용 조건

Greedy 결과가 정확성이나 보안을 결정하는 위치에는 proof 없는 heuristic을 두지 않는다. 시각 품질·근사 경로처럼 품질 저하가 허용되는 위치에서도 최소 품질, starvation 상한, budget 초과 동작을 계약한다.

## 10. 검증과 반례 생성

### 작은 instance 완전 탐색

$n$이 작으면 모든 부분집합·순열을 열거해 최적해를 구하고 greedy와 비교한다. Production 입력에서 완전 탐색은 불가능해도 테스트 oracle로는 강하다.

```text
모든 feasible 해를 생성한다.
목적 값을 계산해 OPT를 구한다.
greedy 해의 feasibility와 값을 검사한다.
greedy가 최적이어야 하는 문제는 값이 OPT와 같은지 확인한다.
근사 문제는 약속한 ratio 안인지 확인한다.
```

### 교환 논증 점검표

1. 임의의 최적해를 잡는다.
2. Greedy 선택이 없으면 어떤 원소와 교환할지 지정한다.
3. 교환 뒤 feasibility가 유지됨을 보인다.
4. 목적 값이 나빠지지 않음을 보인다.
5. 남은 문제가 같은 형태인지 보인다.

한 단계라도 보이지 않으면 proof가 완성되지 않았다.

### 반례 생성 전략

- Greedy가 가장 좋아하는 후보가 여러 미래 선택을 동시에 막게 한다.
- Local key 차이는 작고 이후 손실은 크게 만든다.
- 분할 가능한 문제를 atomic 선택으로 바꾼다.
- 동률 후보의 후속 호환성을 다르게 만든다.
- 비독립 시너지와 전환 비용을 넣는다.
- Online 결정 직후 입력 분포를 바꾼다.

Property-based generator는 작은 정수 비용과 짧은 구간부터 축소해야 사람이 이해할 수 있는 최소 반례를 얻는다.

### 성능과 품질을 분리한다

실행 시간이 빠른 것과 해가 좋은 것은 별도 축이다.

| 지표 | 의미 |
|---|---|
| 실행 시간·메모리 | 계산 비용 |
| objective value | 해 품질 |
| optimality gap | 최적 대비 손실 |
| violation count | 제약 위반 여부 |
| starvation age | online 공정성 |

Feasibility를 위반한 해는 objective가 좋아도 유효한 답이 아니다.

---

## 결론

Greedy 알고리즘은 local 선택을 되돌리지 않는 구현 패턴이 아니라, 그 선택을 포함하는 최적해가 존재한다는 구조적 주장이다. 활동 선택과 scheduling은 교환 논증으로, matroid는 augmentation property로 그 주장을 뒷받침한다. Fractional 조건을 0/1로 바꾸거나 상호작용 제약을 추가하면 같은 규칙의 증명이 깨질 수 있다.

최적 계산이 비싸거나 미래 입력을 모르면 greedy는 근사·online 알고리즘으로 사용된다. 이때는 최적성 대신 approximation ratio, competitive ratio, starvation 상한을 계약해야 한다. 게임의 프레임·대역폭 budget에서는 작은 완전 탐색 oracle과 적대 sequence로 품질과 실행 비용을 분리해 검증해야 한다.

## 더 읽을거리

- Cormen, Leiserson, Rivest, Stein, *Introduction to Algorithms*, 4th ed. (MIT Press, 2022), Ch. 15, 35, 27 — greedy 증명, 근사 알고리즘, online 알고리즘.
- Steven S. Skiena, *The Algorithm Design Manual*, 3rd ed. (Springer, 2020), Ch. 1, 8, 12 — 활동 선택, MST와 어려운 조합 최적화 문제에서 greedy를 시도하고 반례·근사로 경계를 정하는 실무 절차.
- Jack Edmonds, “Matroids and the Greedy Algorithm”, *Mathematical Programming* 1, 1971 — greedy 최적성의 추상 구조.
- Václav Chvátal, “A Greedy Heuristic for the Set-Covering Problem”, *Mathematics of Operations Research* 4(3), 1979 — 가중 set cover greedy의 근사 분석.
- Allan Borodin, Ran El-Yaniv, *Online Computation and Competitive Analysis* (Cambridge University Press, 1998) — online 알고리즘의 competitive analysis.
