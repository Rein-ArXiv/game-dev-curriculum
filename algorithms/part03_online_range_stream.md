## 들어가며

많은 문제는 정렬이나 그래프 탐색보다 단순한 한 번의 순회로 풀린다. 그러나 단순한 순회와 무계획한 중첩 반복은 다르다. 누적합은 반복 구간 합을 전처리로 옮기고, 두 포인터는 되돌아가지 않는 경계를 이용하며, 단조 자료구조는 후보가 영원히 쓸모없어지는 순간 제거한다.

게임에서는 최근 5초의 입력률, 화면 구간의 최대 부하, 충돌 이벤트의 시간 겹침, 상위 우선순위 작업처럼 데이터가 계속 도착하고 질의가 반복된다. 이때 전체 데이터를 매번 다시 계산하면 평균 프레임은 버텨도 입력 폭주에서 지연이 튄다. 반대로 복잡한 트리를 무조건 도입하면 작은 데이터의 상수 비용과 동기화 부담이 더 커진다.

이 글은 선형 스캔, 누적합, 두 포인터, 슬라이딩 윈도, sweep line, 단조 stack·queue, heap, Fenwick tree, segment tree를 하나의 관점으로 묶는다. 각 기법이 이전 상태에서 무엇을 보존하고 어떤 갱신을 상각하는지 증명하며, batch와 online 워크로드의 선택 기준을 세운다.

---

## 목차
1. [중복 계산을 상태로 바꾸기](#1-중복-계산을-상태로-바꾸기)
2. [누적합과 차분 배열](#2-누적합과-차분-배열)
3. [두 포인터와 슬라이딩 윈도](#3-두-포인터와-슬라이딩-윈도)
4. [Sweep Line과 이벤트 정렬](#4-sweep-line과-이벤트-정렬)
5. [단조 Stack과 Queue](#5-단조-stack과-queue)
6. [Heap과 Online Top-k](#6-heap과-online-top-k)
7. [좌표 압축과 Fenwick Tree](#7-좌표-압축과-fenwick-tree)
8. [Segment Tree와 결합 가능한 요약](#8-segment-tree와-결합-가능한-요약)
9. [상각 분석과 지연 상한](#9-상각-분석과-지연-상한)
10. [게임 시스템의 적용과 검증](#10-게임-시스템의-적용과-검증)

---

## 1. 중복 계산을 상태로 바꾸기

구간 $[l,r)$의 합을 질의할 때마다 직접 순회하면 한 질의는 $O(r-l)$이다. 질의가 $q$개면 최악 $O(nq)$가 된다. 입력이 변하지 않는다면 이전 계산을 요약한 상태를 두어 질의를 줄일 수 있다.

알고리즘 설계 질문은 다음과 같다.

```text
질의가 반복해서 다시 읽는 정보는 무엇인가?
새 원소 하나가 들어올 때 갱신할 수 있는 요약은 무엇인가?
오래된 원소가 빠질 때 요약에서 제거할 수 있는가?
요약을 두 부분에서 결합할 수 있는가?
```

합은 역연산인 뺄셈이 있어 prefix 두 개의 차로 구간을 복원할 수 있다. 최댓값 연산에는 앞 구간의 기여를 제거하는 역연산이 없으므로, 두 prefix 최댓값만으로 임의 구간 최댓값을 복원할 수 없다. 연산의 대수적 성질이 자료구조를 결정한다.

### Batch와 online

- **Batch**: 입력 전체를 알고 전처리한 뒤 질의한다.
- **Online**: 미래 입력을 모르는 상태에서 도착 순서대로 답한다.
- **Offline query**: 질의 전체를 알고 순서를 재배치해 처리할 수 있다.

같은 문제도 계약에 따라 해법이 달라진다. 리플레이 분석은 이벤트를 모두 모은 batch 처리가 가능하지만, 실시간 서버의 과부하 감지는 현재까지의 데이터만 사용해야 한다.

## 2. 누적합과 차분 배열

### 1차원 누적합

수열 $A$의 prefix sum을 다음처럼 정의한다.

$$
P[0]=0,\qquad P[i+1]=P[i]+A[i]
$$

그러면 반열린 구간 합은

$$
\sum_{i=l}^{r-1}A[i]=P[r]-P[l]
$$

이다. 전처리는 $O(n)$, 질의는 $O(1)$이다.

```cpp
std::vector<std::int64_t> PrefixSum(std::span<const int> a) {
    std::vector<std::int64_t> p(a.size() + 1, 0);
    for (std::size_t i = 0; i < a.size(); ++i) {
        p[i + 1] = p[i] + a[i];
    }
    return p;
}
```

누적 타입은 입력 타입보다 넓어야 할 수 있다. 원소가 32비트여도 누적기가 32비트면 합이 32비트 범위를 넘는 질의만 조용히 틀리고 나머지 질의는 맞아 보여 발견이 늦다(부호 있는 정수라면 넘치는 순간부터 정의되지 않은 동작이다). 최대 원소 크기와 길이로 상한을 계산해야 한다.

### 2차원 누적합

격자 $A[y][x]$의 직사각형 합은 포함-배제 원리를 사용한다.

$$
S(y_0,x_0,y_1,x_1)=P[y_1][x_1]-P[y_0][x_1]-P[y_1][x_0]+P[y_0][x_0]
$$

마지막 항을 다시 더하는 이유는 왼쪽 위 영역을 두 번 뺐기 때문이다. 영향력 맵의 직사각형 합이나 타일 구역 통계에 사용할 수 있다. 데이터가 자주 변하면 전체 prefix 재구축 비용 $O(WH)$가 질의 이득을 지울 수 있다.

### 차분 배열

누적합이 점 값에서 구간 질의를 빠르게 한다면, 차분 배열은 구간 갱신을 두 경계 표시로 바꾼다. 구간 `[l,r)`에 `v`를 더할 때

```text
diff[l] += v
diff[r] -= v
```

로 기록하고 마지막에 prefix를 취한다. 모든 갱신을 먼저 알고 최종 값만 필요할 때 갱신당 $O(1)$이다.

실시간으로 중간 결과를 계속 물으면 단순 차분 배열은 맞지 않는다. 갱신과 질의가 섞이는 계약에는 동적 구간 자료구조가 필요하다.

## 3. 두 포인터와 슬라이딩 윈도

두 포인터 기법의 핵심은 각 포인터가 한 방향으로만 움직여 전체 이동 횟수가 $O(n)$이라는 점이다.

### 정렬 수열의 두 합

오름차순 배열에서 합이 목표 `target`인 쌍을 찾는다.

```cpp
std::optional<std::pair<std::size_t, std::size_t>> TwoSumSorted(
    std::span<const int> a, int target) {
    if (a.size() < 2) return std::nullopt;

    std::size_t left = 0;
    std::size_t right = a.size() - 1;
    while (left < right) {
        const std::int64_t sum = std::int64_t(a[left]) + a[right];
        if (sum == target) return {{left, right}};
        if (sum < target) ++left;
        else              --right;
    }
    return std::nullopt;
}
```

합이 작으면 현재 `left`와 짝지은 어떤 더 작은 `right`도 목표에 도달하지 못한다. 따라서 `left`를 버려도 된다. 이 제거 논증은 정렬과 합의 단조성에 의존한다.

### 가변 길이 윈도

모든 원소가 음이 아니고 한도가 음이 아닐 때, 합이 한도를 넘지 않는 가장 긴 연속 구간을 찾을 수 있다.

```text
right를 한 칸 확장하고 합에 추가한다.
합이 한도를 넘는 동안 left를 전진하며 제거한다.
현재 유효 구간 길이로 답을 갱신한다.
```

각 원소는 한 번 들어오고 한 번 나가므로 $O(n)$이다. 한도가 음수이면 빈 구간의 합 0조차 조건을 만족하지 못해 `left`를 계속 전진시키는 의사코드가 유효 범위를 벗어나므로, 한도가 음이 아니라는 전제를 두거나 유효 구간 없음 상태를 먼저 반환하는 규약이 필요하다. 음수가 허용되면 `left`를 옮겼을 때 합이 단조롭게 줄지 않아 이 논리가 깨진다. 그 경우 prefix sum과 ordered structure 같은 다른 모델이 필요하다.

### 고정 시간 윈도

최근 5초처럼 시간 기반 윈도는 원소 수가 고정되지 않는다. timestamp 순 queue에 이벤트를 넣고 `now-window`보다 오래된 이벤트를 앞에서 제거한다.

clock이 역행하거나 timestamp가 재정렬되면 앞에서만 제거한다는 불변식이 깨진다. monotonic clock과 입력 정렬 정책이 알고리즘 계약에 포함된다.

## 4. Sweep Line과 이벤트 정렬

구간 겹침 문제는 모든 쌍을 비교하면 $O(n^2)$이다. 각 구간의 시작과 끝을 이벤트로 바꾸고 좌표 순서대로 훑으면 활성 집합의 변화만 처리할 수 있다.

```text
[start, end) -> (start, +1), (end, -1)
```

이벤트를 정렬해 누적하면 동시에 활성인 최대 구간 수를 $O(n\log n)$에 구한다. 경계가 같은 이벤트의 tie-break가 중요하다. 반열린 구간 `[start,end)`에서는 끝 이벤트를 시작 이벤트보다 먼저 처리해야 맞닿기만 한 두 구간을 겹침으로 세지 않는다. 다만 `[x,x)`는 공집합이므로 이벤트를 만들지 않는다. 빈 구간을 end-first 규칙으로 처리하면 없는 ID를 지우거나 마지막에 유령 활성 원소를 남길 수 있다. 활성 개수의 최댓값만 세는 경로는 이 경우 값이 우연히 맞을 수 있으므로, 실제 겹치는 쌍을 출력하는 경로와 구분해 검사해야 한다. `start>end`는 계약 위반으로 거부한다.

```cpp
struct Event {
    int coordinate;
    int delta; // end=-1, start=+1
};

std::sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
    if (a.coordinate != b.coordinate) return a.coordinate < b.coordinate;
    return a.delta < b.delta;
});
```

### 활성 집합

단순 최대 겹침은 counter만 있으면 된다. 실제로 겹치는 쌍을 찾아야 하면 현재 sweep 위치와 교차할 수 있는 객체를 ordered set에 유지한다. 2D 충돌 후보 생성에서는 x축 시작·끝 이벤트로 활성 객체를 관리하고, y구간이 겹치는 후보만 좁힌다.

Sweep line은 한 축의 순서를 이용하지만, 객체가 매 프레임 크게 이동하면 이벤트 정렬 유지 비용이 커질 수 있다. 거의 정렬된 이벤트 배열은 insertion 계열 갱신이 유리할 수 있고, 이동이 무작위면 재정렬 비용을 측정해야 한다.

## 5. 단조 Stack과 Queue

단조 자료구조는 후보를 값 순서로 유지하고, 새 원소가 들어왔을 때 앞으로 절대 답이 될 수 없는 후보를 제거한다.

### 다음으로 큰 원소

수열에서 각 원소 오른쪽의 첫 번째 더 큰 값의 인덱스를 찾는다. `answer[i]`는 `i` 오른쪽에서 처음으로 `a[i]`보다 큰 값의 인덱스이고, 없으면 `-1`이다. stack에는 아직 답을 못 찾은 인덱스를 값 내림차순으로 둔다.

```cpp
std::vector<int> NextGreater(std::span<const int> a) {
    std::vector<int> answer(a.size(), -1);
    std::vector<std::size_t> stack;

    for (std::size_t i = 0; i < a.size(); ++i) {
        while (!stack.empty() && a[stack.back()] < a[i]) {
            answer[stack.back()] = static_cast<int>(i);
            stack.pop_back();
        }
        stack.push_back(i);
    }
    return answer;
}
```

각 인덱스는 한 번 push되고 최대 한 번 pop되므로 총 $O(n)$이다. 내부 `while`이 한 반복에서 길어 보여도 전체 작업량은 선형이다. 반환값은 값이 아니라 인덱스이고 `-1`은 없음 표시다. 인덱스를 `int`로 담으므로 $n\le$ `INT_MAX`인 입력에만 적용된다.

### 슬라이딩 윈도 최댓값

길이 $k$ 윈도의 최댓값은 deque에 값이 감소하는 순서로 인덱스를 유지한다.

1. 윈도 밖 인덱스를 앞에서 제거한다.
2. 새 값보다 작거나 같은 후보를 뒤에서 제거한다.
3. 새 인덱스를 뒤에 넣는다.
4. deque 앞이 현재 최댓값이다.

뒤에서 제거된 값은 새 값보다 크지 않고 더 오래되었으므로 어떤 미래 윈도에서도 새 값보다 유리할 수 없다. 이 지배 관계가 정확성의 핵심이다.

동률에서 오래된 원소를 남길지 새 원소를 남길지는 인덱스 반환 의미에 따라 정한다. 값만 필요하면 오래된 동률을 제거해 deque를 짧게 유지할 수 있다.

## 6. Heap과 Online Top-k

Heap은 완전한 정렬을 유지하지 않고 루트의 최솟값 또는 최댓값만 보장한다. 삽입과 루트 제거는 $O(\log n)$, 조회는 $O(1)$이다.

### 우선순위 queue의 불변식

min-heap 배열에서 인덱스 `i`의 부모는 `(i-1)/2`다. 모든 자식은 부모보다 작지 않아야 한다.

$$
A[parent(i)]\le A[i]
$$

삽입은 끝에 추가한 뒤 위로 올리고, 제거는 마지막 원소를 루트로 옮긴 뒤 아래로 내린다. 전체 배열은 정렬돼 있지 않다. heap 컨테이너를 순회한 순서를 우선순위 순서로 해석하면 안 된다.

### 만료 이벤트와 stale 항목

게임 타이머나 최단 경로 구현은 기존 heap 항목의 우선순위를 직접 수정하기보다 새 항목을 넣고 꺼낼 때 stale 여부를 검사할 수 있다.

```cpp
while (!heap.empty()) {
    auto [time, id, generation] = heap.top();
    if (generation != states[id].generation) {
        heap.pop();
        continue;
    }
    if (time > now) break;
    heap.pop();
    Expire(id);
}
```

이 방식은 decrease-key 위치 추적을 피하지만 stale 항목이 쌓여 메모리와 pop 비용이 증가한다. 유효 항목 대비 stale 비율을 측정하고 재구축 조건을 둬야 한다.

### 스트림 Top-k

가장 큰 $k$개를 유지하려면 크기 $k$의 min-heap을 둔다. 새 값이 루트보다 클 때만 교체한다. 메모리는 $O(k)$, 전체 시간은 $O(n\log k)$다.

동점 처리와 결과 순서가 필요하면 `(score,id)`의 전체 순서를 정의한다. heap에 남았다는 사실은 상위 집합만 보장하며 최종 출력 정렬에는 별도 $O(k\log k)$가 필요하다.

## 7. 좌표 압축과 Fenwick Tree

키가 `10`, `1000000`, `900000000`처럼 넓은 범위에 sparse하게 놓여도 상대 순서만 필요할 수 있다. 정렬한 고유 키에 dense rank를 부여하는 것이 좌표 압축이다.

```text
원본:  [1,000,000, 10, 900,000,000, 10]
고유 정렬: [10, 1,000,000, 900,000,000]
rank: [1, 0, 2, 0]
```

압축은 순서와 동등성은 보존하지만 실제 거리 차이는 보존하지 않는다. 좌표 차이가 물리적 길이를 뜻하면 원본 값도 유지해야 한다.

### Fenwick tree

Fenwick tree는 점 갱신과 prefix 합을 각각 $O(\log n)$에 처리한다. 인덱스의 최하위 set bit가 각 노드가 담당하는 구간 길이를 나타낸다.

```cpp
class Fenwick {
public:
    explicit Fenwick(std::size_t n) : tree_(n + 1, 0) {}

    void Add(std::size_t index, std::int64_t delta) {
        for (std::size_t i = index + 1; i < tree_.size(); i += i & -i)
            tree_[i] += delta;
    }

    std::int64_t Prefix(std::size_t end) const {
        std::int64_t sum = 0;
        for (std::size_t i = end; i > 0; i -= i & -i) sum += tree_[i];
        return sum;
    }
private:
    std::vector<std::int64_t> tree_;
};
```

`Prefix(end)`는 `[0,end)`를 합한다. 구간 합은 `Prefix(r)-Prefix(l)`이다. 1-based 내부 인덱스와 0-based 외부 인덱스를 섞는 실수가 흔하므로 공개 API의 구간 표기를 고정해야 한다. 공개 계약은 `0 <= index < n`과 `0 <= end <= n`이며, `Add(index)`는 외부 0-based `index`가 가리키는 원소를 갱신하고 `Prefix(end)`는 처음 `end`개를 합한다. `end > n`은 범위 밖 읽기이고 `Add(SIZE_MAX)`는 `index+1`이 0으로 wrap해 lowbit가 0이 되어 무한 반복하므로, 외부 입력 경계에서 이 범위를 검증해야 한다. 모든 누적합이 `int64` 표현 범위에 들어간다는 전제도 함께 선언한다. 안전한 외부용 래퍼는 실패 반환 또는 예외를 사용한다.

Fenwick tree는 합처럼 역연산으로 구간을 복원할 수 있는 연산에 특히 단순하다. 일반적인 구간 최솟값과 lazy 구간 갱신에는 segment tree가 더 자연스럽다.

## 8. Segment Tree와 결합 가능한 요약

Segment tree는 배열 구간을 이진 트리로 분해하고 각 노드에 구간 요약을 저장한다. 점 갱신과 구간 질의는 루트에서 $O(\log n)$개 경계를 지나고, 배열 기반 저장은 보통 $O(n)$ 공간을 쓴다.

### 결합 연산의 조건

두 인접 구간 요약을 순서를 보존한 채 괄호 묶기만 바꿔 결합하려면 연산 `combine`이 결합법칙을 만족해야 한다. 결합법칙은 교환법칙이 아니므로 `combine(a,b)`와 `combine(b,a)`는 다를 수 있고, 왼쪽·오른쪽 구간의 순서를 뒤집으면 안 된다.

$$
combine(combine(a,b),c)=combine(a,combine(b,c))
$$

합, 최솟값, 최댓값, GCD는 가능하다. 평균은 평균값 하나만 저장하면 구간 길이가 달라 결합할 수 없지만 `(sum,count)`를 저장하면 가능하다. `combine`에는 빈 구간을 나타내는 항등원 $e$가 있어 $combine(e,x)=combine(x,e)=x$를 만족해야 한다. 질의 구간이 노드 구간과 정확히 겹치지 않을 때 빈 구간을 반환하게 되기 때문이다.

최대 연속 부분합도 다음 네 값을 가진 요약으로 결합할 수 있다.

```text
total: 전체 합
prefix: 최대 prefix 합
suffix: 최대 suffix 합
best: 최대 연속 부분합
```

부모의 `best`는 왼쪽 `best`, 오른쪽 `best`, 왼쪽 `suffix + 오른쪽 prefix` 중 최댓값이다. 자료구조의 힘은 트리 모양보다 문제에 맞는 결합 가능한 요약을 찾는 데 있다.

### Lazy propagation

구간 전체에 같은 갱신을 적용할 때 모든 leaf를 방문하면 $O(n)$이다. 노드 구간 전체에 적용 가능한 갱신을 lazy tag로 남기고 자식이 필요할 때 전파하면 구간 갱신과 질의를 $O(\log n)$에 처리할 수 있다.

tag의 합성 순서가 중요하다. 대입 뒤 덧셈과 덧셈 뒤 대입은 같지 않다. 여러 갱신 종류를 지원하면 `apply(tag,node)`와 `compose(new,old)`의 대수적 계약을 먼저 정의해야 한다. `apply`는 tag를 노드 요약에 작용시키는 $mapping(tag,summary)$이고, `compose(new,old)`는 새 갱신을 기존 tag 뒤에 겹치는 합성이다. 이때 $mapping(compose(g,f),s)=mapping(g,mapping(f,s))$라는 작용 호환성과, 작용이 $O(1)$에 요약을 갱신한다는 조건이 필요하다. 구간 덧셈과 합 질의는 노드 구간 길이가 있어야 `sum += length * delta`로 $O(1)$에 작용하므로 요약에 길이를 함께 저장한다.

### 언제 쓰지 않는가

원소가 수십 개이고 매 프레임 전체를 순회한다면 선형 스캔이 더 빠르고 단순할 수 있다. 동적 트리의 분기와 넓은 메모리 footprint는 캐시 비용을 만든다. 질의·갱신 비율과 $n$의 실제 분포를 측정해야 한다.

## 9. 상각 분석과 지연 상한

단조 stack, queue, stale heap은 한 연산이 비싸 보여도 전체 sequence 비용이 작다는 상각 분석을 사용한다.

### aggregate 분석

단조 stack에서 각 원소는 한 번 push되고 한 번 pop된다. $n$개 입력 전체 stack 연산은 최대 $2n$이므로 원소당 상각 $O(1)$이다.

### 상각 비용과 프레임 지연

상각 $O(1)$은 개별 연산의 최악 $O(1)$을 뜻하지 않는다. 오래 쌓인 stale heap 항목을 한 프레임에 모두 제거하면 총비용은 양호해도 해당 프레임이 deadline을 넘을 수 있다.

실시간 시스템에서는 다음을 별도로 본다.

```text
전체 작업량의 상각 상한
한 tick에서 수행할 수 있는 최대 작업량
초과 작업을 나눌 수 있는가
지연된 정리가 메모리 상한을 넘는가
```

작업 budget을 두고 정리를 여러 프레임에 나누면 deadline은 지킬 수 있지만 오래된 상태가 남는 의미를 정의해야 한다. 정확성에 필요한 제거와 성능을 위한 정리를 구분한다.

## 10. 게임 시스템의 적용과 검증

### 적용 지도

| 문제 | 가능한 구조 | 핵심 전제 |
|---|---|---|
| 고정 맵 직사각형 비용 합 | 2D prefix sum | 갱신이 드묾 |
| 최근 시간 구간의 요청률 | timestamp queue | 입력 시간이 단조 |
| 구간 동시 접속 최대치 | sweep line | endpoint 의미 고정 |
| 최근 $k$ 프레임 최대 시간 | monotonic deque | 고정 길이 윈도 |
| 다음 만료 타이머 | min-heap | stale 정책 존재 |
| 점 갱신과 누적 통계 | Fenwick tree | 결합·역연산 가능 |
| 구간 갱신과 복합 질의 | segment tree | 결합·tag 합성 정의 |

### 기준 구현과 differential test

작은 입력에서는 느린 $O(n)$ 또는 $O(n^2)$ 구현이 강한 oracle이다.

- Fenwick·segment tree의 모든 구간 질의를 직접 합산과 비교한다.
- 슬라이딩 최댓값을 각 윈도 `max_element`와 비교한다.
- sweep line의 최대 동시 활성 수는 모든 endpoint와 인접 열린 구간의 표본 점에서 그 점을 포함하는 구간을 직접 세어 비교한다.
- 실제로 겹치는 쌍을 출력하는 기능은 모든 구간 쌍 multiset과 대조한다.
- top-k 결과를 전체 정렬의 앞 $k$개와 비교한다.

### 생성해야 할 경계 입력

```text
빈 입력과 원소 하나
모든 timestamp 또는 좌표가 같음
구간 길이가 0인 이벤트(빈 [x,x)는 이벤트 생성에서 제외되는지)
윈도보다 오래된 원소가 한꺼번에 제거됨
모든 값이 증가 또는 감소
합이 정수 범위에 근접함
같은 우선순위가 대량으로 발생함
압축 좌표에 중복이 많음
```

### 측정

알고리즘 이름만 비교하지 않고 다음을 기록한다.

```text
입력 크기와 질의/갱신 비율
batch인지 online인지
평균과 P99 연산 시간
최대 queue/heap/tree 크기
재구축과 stale 정리 시간
할당 횟수와 메모리 peak
```

같은 총 작업량이라도 한 프레임에 몰리는 정도가 다르면 플레이 경험이 달라진다. 처리량과 tail latency를 함께 봐야 한다.

---

## 결론

누적합, 두 포인터, sweep line, 단조 자료구조는 중첩 반복을 없애는 서로 다른 요령이 아니다. 미래에도 필요한 정보만 상태로 남기고, 다시 답이 될 수 없는 후보를 한 번만 제거한다는 공통 원리를 가진다. 그 정확성은 합의 역연산, 경계의 단조성, 후보 사이의 지배 관계 같은 전제에 의존한다.

Fenwick tree와 segment tree는 갱신과 질의가 섞일 때 구간 요약을 계층적으로 유지한다. 복잡한 구조가 항상 빠른 것은 아니며, 입력 크기·질의 비율·상각 spike·메모리 지역성을 함께 측정해야 한다. 실시간 게임에서는 총 연산량뿐 아니라 한 tick의 최대 작업량까지 알고리즘 계약에 포함해야 한다.

## 더 읽을거리

- Cormen, Leiserson, Rivest, Stein, *Introduction to Algorithms*, 4th ed. (MIT Press, 2022), Ch. 6, 16, 17 — heap, 상각 분석, 동적 요약 구조의 분석 기반.
- Steven S. Skiena, *The Algorithm Design Manual*, 3rd ed. (Springer, 2020), Ch. 3~5 — 자료구조 선택, 정렬, divide-and-conquer를 실제 문제 형태에 연결하는 기준.
- Peter M. Fenwick, “A New Data Structure for Cumulative Frequency Tables”, *Software: Practice and Experience* 24(3), 1994 — Fenwick tree 원전.
- Jon Bentley, *Programming Pearls*, 2nd ed. (Addison-Wesley, 2000) — 누적·스캔·불변식으로 반복 계산을 제거하는 문제 해결 사례.
