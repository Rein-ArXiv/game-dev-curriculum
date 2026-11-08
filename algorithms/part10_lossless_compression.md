## 들어가며

무손실 압축은 데이터를 더 짧은 비트열로 바꾸되 원본을 정확히 복원하는 가역 변환이다. "반복이 많으면 잘 압축된다"는 설명만으로는 어떤 입력이 얼마나 압축 가능한지, Huffman과 arithmetic coding이 왜 다른지, LZ 계열의 속도를 무엇이 결정하는지, 손상된 입력을 어떻게 안전하게 거부할지 설명할 수 없다.

압축기는 보통 두 종류의 중복을 제거한다.

- **통계적 중복**: 자주 나오는 심볼에 짧은 표현을 준다.
- **문맥·문자열 중복**: 이미 나타난 문자열이나 예측 가능한 문맥을 참조한다.

현대의 범용 압축 포맷은 dictionary/match 단계와 entropy coding 단계를 결합한다. 이 글은 정보량의 하한부터 prefix code, Huffman, arithmetic/range coding, ANS, LZ77, match finding, parsing, 블록·사전 설계, 복호화 안전성, 성능 측정을 하나의 알고리즘 체계로 정리한다.


---

## 목차

1. [정보량과 엔트로피](#1-정보량과-엔트로피)
2. [Prefix Code와 Huffman Coding](#2-prefix-code와-huffman-coding)
3. [Arithmetic, Range, ANS Coding](#3-arithmetic-range-ans-coding)
4. [Dictionary Coding과 LZ77](#4-dictionary-coding과-lz77)
5. [Match Finding과 Parsing](#5-match-finding과-parsing)
6. [블록, 사전, 랜덤 접근](#6-블록-사전-랜덤-접근)
7. [전체 비용 모델](#7-전체-비용-모델)
8. [안전한 복호화](#8-안전한-복호화)
9. [재현 가능한 벤치마크](#9-재현-가능한-벤치마크)


---

## 1. 정보량과 엔트로피

### 자기 정보량

확률이 낮은 사건은 관측했을 때 더 많은 정보를 준다. 사건 $x$의 자기 정보량(self-information)을

$$
I(x) = -\log_2 p(x)
$$

로 정의한다. 확률이 $1/2$인 사건은 1 bit, $1/256$인 사건은 8 bit의 정보량을 가진다.

로그를 쓰는 이유는 독립 사건의 정보량이 더해져야 하기 때문이다.

$$
I(x,y)
= -\log_2(p(x)p(y))
= I(x)+I(y)
$$

### Shannon entropy

확률 변수 $X$의 평균 정보량은

$$
H(X)
=
-\sum_{x \in \mathcal{X}} p(x)\log_2p(x)
$$

이다. $H(X)$의 단위는 bit/symbol이다. 모든 심볼이 같은 확률이면 엔트로피가 최대이고, 한 심볼만 항상 나오면 0이다.

예를 들어 $p(A)=0.75$, $p(B)=0.25$이면

$$
H(X)
=
-0.75\log_2 0.75
-0.25\log_2 0.25
\approx 0.811
$$

bit/symbol이다.

### 0차 엔트로피의 한계

바이트 빈도만 세어 구한 엔트로피는 심볼 사이의 순서를 무시한다. 아래 두 문자열은 문자 빈도가 같으므로 0차 엔트로피도 같다.

```text
ABABABABABABABAB
AAAABBBBAAAABBBB
```

하지만 반복 구조와 문맥은 다르다. 조건부 엔트로피

$$
H(X_n \mid X_{n-1}, \ldots, X_{n-k})
$$

는 길이 $k$ 문맥을 알고 있을 때 다음 심볼의 불확실성을 측정한다. dictionary coder와 context model은 이런 순서 구조를 이용한다.

### 엔트로피는 파일 크기 예언이 아니다

표본에서 계산한 empirical entropy는 선택한 심볼과 문맥 모델에 대한 기대 길이 하한이며, 개별 파일이나 모든 포맷에 대한 무조건 하한이 아니다. 실제 포맷에는 다음 비용이 추가된다.

- 빈도표나 코드 길이
- 블록 헤더와 원본 크기
- 정렬 padding
- checksum
- 짧은 입력의 모델 초기화
- dictionary와 frame metadata

또한 데이터가 독립 동일 분포(i.i.d.)가 아니면 단순한 $H(X)$만으로 최적 길이를 말할 수 없다. "엔트로피가 1.2 byte이므로 파일도 1.2 byte까지 압축된다"는 식의 계산은 정수 비트열, 모델 전달 비용, 순서 구조를 무시한다.

## 2. Prefix Code와 Huffman Coding

### 유일 복호화와 prefix-free

가변 길이 코드에서 어떤 codeword도 다른 codeword의 prefix가 아니면 왼쪽부터 모호함 없이 복호화할 수 있다.

```text
A = 0
B = 10
C = 110
D = 111
```

비트열 `010111`은 `A B D`로 유일하게 분해된다.

이진 prefix code의 길이를 $\ell_1,\ldots,\ell_n$이라 하면 Kraft inequality

$$
\sum_{i=1}^{n}2^{-\ell_i} \le 1
$$

을 만족해야 한다. 완전한 이진 코드 트리는 등호를 만족한다. 길이 $\ell$의 codeword는 깊이 $\ell$인 노드 하나를 차지하고, 그 아래의 전체 subtree를 다른 codeword가 사용할 수 없다는 사실에서 이 부피 합이 나온다.

### 기대 길이

심볼 $i$의 확률을 $p_i$, 코드 길이를 $\ell_i$라 하면 평균 길이는

$$
L = \sum_i p_i\ell_i
$$

이다. 이진 Huffman code는 주어진 심볼 확률에 대한 prefix code 중 $L$을 최소화하고

$$
H(X) \le L < H(X)+1
$$

을 만족한다. 단, $H(X)\le L<H(X)+1$은 명시한 확률 모델에 대한 기대 길이 명제이며 포맷 헤더와 블록 비용은 포함하지 않는다. 형식이 심볼당 최소 1 bit나 고정 길이를 강제하면, 예를 들어 심볼 하나짜리 alphabet에서 $H=0$이어도 $L=1$이 되어 개별 파일 길이는 이 하한에 묶이지 않는다.

### Huffman의 greedy 선택

최적 prefix tree에서 확률이 가장 작은 두 심볼을 가장 깊은 sibling leaf로 둘 수 있다. 두 심볼 $a,b$를 하나의 합성 심볼로 묶고 확률 $p_a+p_b$인 더 작은 문제를 최적으로 풀면 원래 문제도 최적이 된다.

두 심볼의 leaf 깊이는 합성 심볼보다 1 깊으므로 전체 기대 길이는

$$
L(T)
=
L(T') + p_a + p_b
$$

이다. $p_a+p_b$는 어떤 subtree 안에 두 심볼을 배치하더라도 더해지는 고정 비용이므로, 합성 문제의 최적해를 재귀적으로 확장할 수 있다.

### 구축 알고리즘

최소 힙을 사용하면 $n$개 심볼의 트리를 $O(n\log n)$에 구축한다.

```cpp
struct Node {
    std::uint64_t frequency;
    int symbol;       // 내부 노드는 -1
    Node* left;
    Node* right;
};

Node* BuildHuffman(std::span<const std::uint64_t> frequencies) {
    MinHeap<Node*> heap;

    for (std::size_t symbol = 0; symbol < frequencies.size(); ++symbol) {
        if (frequencies[symbol] != 0) {
            heap.push(NewLeaf(
                frequencies[symbol], static_cast<int>(symbol)));
        }
    }

    if (heap.empty()) {
        return nullptr;
    }

    while (heap.size() > 1) {
        Node* a = heap.pop();
        Node* b = heap.pop();
        heap.push(NewInternal(
            CheckedAdd(a->frequency, b->frequency), a, b));
    }
    return heap.pop();
}
```

빈 alphabet와 심볼 하나뿐인 alphabet은 별도 계약이 필요하다. 심볼 하나에 길이 0을 주면 bitstream을 소비하지 않고도 원본 개수를 알아야 한다. 많은 포맷은 최소 길이 1을 주거나 원본 심볼 수를 frame header에 저장한다.

### Canonical Huffman

트리 포인터를 그대로 저장할 필요는 없다. 심볼별 코드 길이만 있으면 아래 규칙으로 codeword를 재구성할 수 있다.

1. 코드 길이 오름차순, 같은 길이는 심볼 순으로 정렬한다.
2. 첫 코드는 0이다.
3. 같은 길이의 다음 코드는 이전 코드에 1을 더한다.
4. 길이가 늘면 차이만큼 왼쪽으로 shift한다.

```cpp
// 미사용 심볼의 길이 0을 제외하고, 지원 폭 상한과 Kraft 부등식을
// 먼저 검증한다. 실패하면 code 생성을 시작하지 않는다.
if (!ValidateCodeLengths(sortedByLengthThenSymbol)) {
    return std::nullopt;
}

std::uint64_t code = 0;   // 64비트 중간값으로 최대 32비트 길이를 계산한다
int previousLength = 0;

for (Entry& entry : sortedByLengthThenSymbol) {
    code <<= entry.length - previousLength;
    entry.code = static_cast<std::uint32_t>(code);
    ++code;
    previousLength = entry.length;
}
```

이 루프는 미사용 심볼의 길이 0을 제외하고, 각 길이가 지원 폭 이하이며 정렬 순서가 단조인지, Kraft 잔여 공간이 음수가 아닌지 검증한 뒤에만 실행한다(§8의 불변식). 과밀한 길이 $\{1,1,1\}$은 표현 가능한 prefix code가 아니므로 거부하고, 지원 폭을 넘는 길이는 더 넓은 중간 타입으로 처리하거나 실패로 반환한다. DEFLATE의 최대 길이 15는 이 표현 폭이 아니라 포맷 계층이 정하는 제약이다.

복호기는 상위 $k$비트 lookup table로 짧은 코드를 한 번에 처리하고, 긴 코드만 보조 테이블이나 트리를 사용한다. $k$를 키우면 table memory는 $O(2^k)$로 늘고 긴 코드의 fallback은 줄어든다.

### Huffman의 한계

확률 $0.9$인 심볼도 최소 1 bit가 필요하므로 symbol당 0.469 bit인 엔트로피에 도달할 수 없다. 여러 심볼을 block symbol로 묶으면 차이가 줄지만 alphabet과 model 비용이 폭증한다. Fractional bit에 가까운 평균 길이를 얻기 위해 arithmetic/range/ANS 계열을 사용한다.

## 3. Arithmetic, Range, ANS Coding

### Arithmetic coding

Arithmetic coding은 전체 메시지를 $[0,1)$의 부분 구간 하나로 표현한다. alphabet의 누적 확률 구간을 $[C_s,C_s+p_s)$라 하자. 현재 구간이 $[L,H)$이고 심볼 $s$를 추가하면

$$
\begin{aligned}
R &= H-L \\
L' &= L + R C_s \\
H' &= L + R(C_s+p_s)
\end{aligned}
$$

로 좁힌다.

예를 들어 $A:[0,0.75)$, $B:[0.75,1)$ 모델에서 `AB`는

```text
초기: [0, 1)
A:    [0, 0.75)
B:    [0.5625, 0.75)
```

구간 안의 이진 소수 하나가 메시지를 나타낸다.

실제 구현은 부동소수점이 아니라 고정 폭 정수와 renormalization을 사용한다. 상위 비트가 확정되면 bitstream으로 내보내고 구간을 shift한다. underflow 구간의 지연 bit 처리와 정수 overflow가 정확성의 핵심이다.

### Range coding

Range coder는 arithmetic coding의 구간을 정수 `low`와 `range`로 표현하는 구현 계열이다.

```text
unit  = range / totalFrequency
low  += unit * cumulativeFrequency[symbol]
range = unit * frequency[symbol]
```

`range`가 임계값보다 작아지면 상위 바이트를 출력하고 정규화한다. 나눗셈 반올림 때문에 마지막 빈도가 남은 range를 흡수하도록 구현하거나 포맷이 정한 규칙을 정확히 따라야 encoder와 decoder가 같은 상태를 유지한다.

### 모델 비용

Entropy coder는 확률 모델이 있어야 한다. 정적 모델은 블록의 빈도표를 header에 저장하고, adaptive 모델은 앞에서 본 심볼로 빈도를 갱신한다.

정적 모델:

- decoder가 병렬 블록을 독립 처리하기 쉽다.
- 짧은 블록에서는 빈도표 비용이 크다.
- 두 번 순회하거나 histogram을 먼저 만들어야 한다.

Adaptive 모델:

- model header를 줄일 수 있다.
- encoder와 decoder가 정확히 같은 갱신 순서를 따라야 한다.
- random access와 병렬화가 어려워질 수 있다.

빈도 합을 제한된 정수 범위로 normalize할 때 0이 아닌 심볼의 빈도가 0으로 떨어지지 않게 하고, 합이 정확히 목표값이 되도록 나머지를 배분해야 한다.

### ANS

Asymmetric Numeral Systems는 하나의 정수 상태 $x$에 정보를 누적한다. 심볼 확률이 높을수록 상태 증가가 작고, 상태가 범위를 벗어나면 하위 bit를 출력하여 renormalize한다.

ANS의 중요한 성질은 arithmetic coding에 가까운 압축률과 table 기반의 빠른 상태 전이를 결합할 수 있다는 점이다. 다만 어느 변형이든 stack과 유사하게 encoding 역순으로 symbol이 복원되므로 입력을 역방향으로 encode하거나 stream interleaving을 설계해야 한다.

Huffman, range, ANS 중 하나가 항상 우월한 것은 아니다.

| 기준 | Huffman | Range/Arithmetic | ANS |
|---|---|---|---|
| 평균 code length | 정수 bit | fractional bit 근사 | fractional bit 근사 |
| 구현 복잡도 | 낮음 | 높음 | 높음 |
| table memory | 선택적 | model table | 변형에 따라 큼 |
| 병렬화 | 블록 단위 쉬움 | 상태 의존성 | 여러 상태 interleave 가능 |
| 짧은 블록 | 단순하지만 header 영향 | model 비용 영향 | table/model 비용 영향 |

## 4. Dictionary Coding과 LZ77

### Back-reference

LZ77은 이미 복호화된 byte sequence를 dictionary로 사용한다. 현재 출력 위치에서 거리 $d$만큼 뒤의 문자열을 길이 $\ell$만큼 복사하는 match를 낸다.

```text
literal run: "ABCD"
match: distance=4, length=12
output: "ABCDABCDABCDABCD"
```

중요한 점은 match source와 destination이 겹칠 수 있다는 것이다. 위 예시는 4바이트 pattern을 반복 생성한다. 일반 `memcpy`처럼 겹침이 없다고 가정하면 복호화가 틀린다.

안전한 의미론은 한 바이트씩 이미 생성된 출력을 다시 읽는 것이다. `memmove`로도 일반 대체할 수 없다. `distance=1`, `length=4`이면 방금 쓴 바이트를 다음 반복에서 다시 읽어 pattern을 전파해야 하는데, `memmove`는 시작 시점의 원본을 한 번만 임시 보존하므로 이 반복 의미를 재현하지 못한다. 이 겹침 규칙은 DEFLATE(RFC 1951)의 match 정의와 연결된다.

```cpp
bool CopyMatch(
    std::span<std::byte> output,
    std::size_t& write,
    std::size_t distance,
    std::size_t length)
{
    if (write > output.size() || distance == 0 || distance > write) {
        return false;
    }
    if (length > output.size() - write) {
        return false;
    }

    for (std::size_t i = 0; i < length; ++i) {
        output[write] = output[write - distance];
        ++write;
    }
    return true;
}
```

이 helper는 실패 시 `write`와 출력 버퍼를 변경하지 않는다. 최적화된 decoder는 distance와 length에 따라 vector copy를 사용하지만 이 byte-wise 의미와 같은 결과를 내야 한다.

### Window

최대 distance가 $W$이면 decoder는 최근 $W$바이트만 보존하면 된다. Distance field의 비트 수는 포맷 크기와 dictionary 범위를 연결한다.

- 작은 $W$: cache locality와 작은 distance code
- 큰 $W$: 멀리 떨어진 반복 발견 가능
- 너무 큰 $W$: match finder memory와 탐색 비용 증가

Window가 커도 입력에 장거리 반복이 없으면 압축률은 좋아지지 않는다.

### Literal, length, distance 표현

LZ stream은 보통 literal run과 match를 번갈아 표현한다.

```text
[literal length][literal bytes][match distance][match length]
```

Length와 distance 자체도 분포가 치우쳐 있으므로 entropy coding하거나 prefix class로 나눈다. 작은 값은 짧은 code, 큰 값은 base code와 extra bits로 표현할 수 있다.

### LZ78와 LZW

LZ78은 이미 본 phrase를 명시적 dictionary entry로 만들고 `(dictionary index, next symbol)`을 출력한다. LZW는 입력 alphabet을 초기 dictionary로 두고 phrase index만 출력한다.

LZ77은 sliding window의 byte offset을 사용하므로 decoder dictionary가 자연스럽게 출력 자체다. LZ78/LZW는 dictionary table 성장, 최대 code width, reset 정책을 포맷에 정의해야 한다. 손상 입력이 잘못된 index를 가리키지 않는지 검증도 필요하다.

## 5. Match Finding과 Parsing

### 압축 속도는 검색이 결정한다

Decoder는 주어진 match를 복사하면 되지만 encoder는 현재 위치와 같은 문자열을 window에서 찾아야 한다. Window 전체를 모든 위치에서 비교하고 후보마다 최대 $L_{\max}$바이트를 다시 비교하면 최악에

$$
O(n W L_{\max})
$$

시간이 든다. 후보 비교가 상수 시간이거나 LCP를 재사용하거나 매칭 길이만큼 건너뛰는 전제가 있을 때만 더 강한 상한이 성립한다.

### Hash chain

현재 위치의 처음 4~8바이트를 hash해 같은 prefix를 가진 과거 위치들을 chain으로 연결한다.

```cpp
std::uint32_t h = Hash4(input + position) & hashMask;
int candidate = head[h];
prev[position & windowMask] = candidate;
head[h] = position;
```

이 갱신은 다음 전제를 요구한다. `Hash4`는 `position + 4 <= input.size()`일 때만 호출하고, 꼬리의 남은 바이트는 literal 경로로 넘긴다. `hashMask`와 `windowMask`는 각각 hash table과 window 크기가 2의 거듭제곱일 때만 유효하다. 재사용한 ring slot의 `prev`는 `candidate >= position - window`이고 `candidate < position`인 항목만 가리키며, window 밖으로 밀려난 candidate는 잘라낸다. `int` 위치는 입력 크기 상한 안에서만 쓰고, 필요하면 epoch/rebase로 구분한다. 후보 순회는 step budget으로 제한한다.

후보를 최대 $c$개만 검사하면 대략 $O(n c L_{\max})$로 제한할 수 있다. LCP를 재사용하거나 매칭 길이만큼 건너뛰면 상각 비용이 줄어들 수 있다. 압축 level은 흔히 이 후보 수, search depth, lazy matching을 조절한다.

충돌이 많은 입력에서는 후보들이 같은 짧은 prefix만 공유해 긴 비교가 반복될 수 있다. 최소 match 길이, 빠른 mismatch 검사, longest match 상한으로 비용을 제한한다.

### Binary tree와 suffix 구조

후보 문자열을 사전식 binary tree로 관리하면 긴 match 탐색을 개선할 수 있지만 포인터·index 갱신이 늘어난다. Suffix array/tree는 강력한 반복 검색을 제공하지만 구축 메모리와 시간이 커서 실시간 빠른 compressor보다 offline parsing에 더 적합한 경우가 많다.

### Greedy parsing

현재 위치에서 가장 긴 match를 즉시 선택하는 greedy parse는 빠르지만 항상 최종 bitstream을 최소화하지 않는다.

```text
현재 6바이트 match를 고르면 뒤에 literal 10바이트
현재 4바이트 match를 고르면 뒤에 12바이트 match
```

Match 길이만이 아니라 literal, length code, distance code 비용을 합쳐야 한다.

### Lazy matching

현재 match를 바로 출력하지 않고 한 위치 뒤에서 더 좋은 match가 나오는지 확인한다. 미래 match의 절감이 현재 match를 포기하는 비용보다 크면 literal 하나를 내고 미래 match를 선택한다.

### Optimal parsing

입력 위치를 정점, literal 또는 match를 간선으로 보면 최소 bit cost parse는 DAG의 shortest path다.

$$
\text{cost}[j]
=
\min_{(i \rightarrow j)}
\left(
\text{cost}[i]
+ \text{edgeCost}(i,j)
\right)
$$

문제는 entropy model이 parse 결과에 따라 바뀔 수 있다는 점이다. 실제 compressor는 초기 빈도로 cost를 추정하고 parse와 model 갱신을 반복하거나, 제한된 lookahead로 근사한다. 압축 level이 높을수록 match search와 parsing에 더 많은 CPU를 쓰는 이유다.

## 6. 블록, 사전, 랜덤 접근

### 블록은 병렬성과 압축률을 교환한다

독립 블록은 각자 decoder state를 초기화하므로 병렬 복호화와 random access가 쉽다. 대신 블록 경계 밖의 반복을 참조하지 못하고 model header를 반복 저장한다.

큰 블록:

- 장거리 반복과 안정적인 histogram
- header overhead 감소
- random access·병렬 작업 단위가 큼
- 손상 영향 범위가 큼

작은 블록:

- 낮은 접근 지연과 높은 병렬성
- dictionary·model 재시작 비용
- 더 많은 header와 checksum

블록 크기는 압축률 하나가 아니라 I/O 요청 단위, 작업 스케줄러 granularity, 메모리 상한, 손상 복구 단위와 맞춘다.

### 외부 dictionary

짧고 구조가 비슷한 레코드가 많이 있을 때 공통 dictionary를 미리 훈련하면 각 블록의 초기 cold start를 줄일 수 있다. Dictionary ID와 내용은 포맷 계약의 일부다.

- encoder와 decoder가 동일한 dictionary byte를 가져야 한다.
- ID 충돌만 믿지 말고 버전·hash를 검증한다.
- dictionary 교체 중 구버전 데이터를 읽을 수 있어야 한다.
- 신뢰할 수 없는 입력이 임의 dictionary 크기를 요구하지 못하게 한다.

### Frame과 block

Frame header에는 보통 다음 일부가 들어간다.

- magic과 format version
- 압축 전·후 크기
- block 크기 또는 개수
- dictionary ID
- checksum 종류와 값
- feature flags

원본 크기가 없으면 decoder가 출력 메모리를 무제한 늘리게 될 수 있다. 크기를 저장하더라도 신뢰할 수 없는 값이므로 애플리케이션 상한과 checked arithmetic으로 검증한다.

### Checksum의 경계

Checksum은 우발적 손상을 탐지하지만 공격자가 내용을 바꾸고 checksum을 다시 계산하는 것을 막지 못한다. 적대적 경계에서는 MAC 또는 digital signature가 필요하다. 압축 포맷의 checksum과 콘텐츠 신뢰성 검증은 목적이 다르다.

## 7. 전체 비용 모델

### 압축하면 더 빨라지는 조건

원본 크기를 $S$, 압축 비율을 $r=C/S$, 저장 장치 유효 대역폭을 $B_i$, 복호화 대역폭을 $B_d$라 하자. 파이프라인이 직렬이라면

$$
T_{\text{compressed}}
\approx
\frac{rS}{B_i}
+ \frac{S}{B_d}
+ T_{\text{fixed}}
$$

이고 비압축은

$$
T_{\text{raw}}
\approx
\frac{S}{B_i}
$$

이다. 압축이 유리하려면

$$
\frac{r}{B_i} + \frac{1}{B_d} + \frac{T_{\text{fixed}}}{S}
<
\frac{1}{B_i}
$$

이고, 오른쪽 분모가 양수일 때(즉 $(1-r)/B_i > T_{\text{fixed}}/S$) 다음으로 정리된다.

$$
B_d > \frac{1}{\frac{1-r}{B_i} - \frac{T_{\text{fixed}}}{S}}
$$

$r \ge 1$이면 압축해도 원본보다 작아지지 않으므로 순수 직렬 모델에서는 이득이 없다. $T_{\text{fixed}}$를 무시한

$$
B_d > \frac{B_i}{1-r}
$$

는 큰 입력이나 고정비가 작을 때의 근사이며, 양의 고정비에서는 필요조건일 뿐 충분조건이 아니다. 작은 블록에서는 실제로 더 느릴 수 있다. 예를 들어 $r=0.5$이고 $T_{\text{fixed}}\approx 0$이면 decoder가 저장 장치 대역폭의 2배보다 빨라야 직렬 모델에서 이득이다.

I/O와 복호화를 겹치면 총시간은 합보다

$$
T
\approx
\max\left(
\frac{rS}{B_i},
\frac{S}{B_d}
\right)
+ T_{\text{pipeline}}
$$

에 가까워진다. 실제로는 메모리 복사, job scheduling, cache pollution, GPU upload가 추가된다.

### 압축률 표기

"2배 압축"은 모호하다. 아래 두 값을 구분한다.

$$
\text{size ratio} = \frac{C}{S}
$$

$$
\text{space saving} = 1-\frac{C}{S}
$$

원본 100 MiB가 40 MiB가 되면 size ratio는 0.4, saving은 60%다. "compression ratio 2.5:1"로 표기할 수도 있지만 표에 정의를 명시한다.

### Memory bandwidth

Decoder가 5 GB/s라고 해도 출력 5 GB/s만 쓰는 것이 아니다. 압축 입력 읽기, dictionary read, 출력 write, 후속 소비의 cache miss가 메모리 대역폭을 공유한다. 여러 decoder job을 병렬화하면 코어보다 DRAM 대역폭에서 먼저 포화될 수 있다.

### 압축은 전처리의 영향을 받는다

정수 delta, byte shuffle, prediction residual처럼 데이터 의미를 아는 reversible transform은 전체 결합 엔트로피를 줄이지 않는다. 대신 단순한 부호 모델이 포착하기 쉬운 상관·빈도 구조를 드러내어 실제 부호 길이를 줄일 수 있다.

```text
원본 위치: 1000, 1003, 1007, 1010
delta:       1000,    3,    4,    3
```

단, transform metadata와 overflow 규칙을 정의해야 한다. 부동소수점 bit를 양자화하면 정보가 사라질 수 있으므로 더 이상 무손실 압축이 아니다. 무손실 bit transform과 손실 허용 모델을 구분한다.

## 8. 안전한 복호화

### Decoder는 parser다

압축 입력은 길이, offset, table size를 반복해서 제공하므로 공격 표면이다. 빠른 정상 경로와 별개로 모든 값이 입력·출력 범위 안인지 검증해야 한다.

```cpp
bool CheckedAdvance(
    std::size_t position,
    std::size_t amount,
    std::size_t limit,
    std::size_t& result)
{
    if (position > limit || amount > limit - position) {
        return false;
    }
    result = position + amount;
    return true;
}
```

`position + amount > limit`처럼 먼저 더하면 정수 overflow 뒤 검사가 통과할 수 있다. 뺄셈 형태라도 `position <= limit`를 먼저 확인하지 않으면 unsigned underflow로 큰 remaining이 생겨 손상된 위치를 허용한다. 실패 시 `result`는 변경되지 않는다.

### 검증해야 할 불변식

- literal length가 남은 입력과 출력 안에 있다.
- match distance가 0이 아니고 이미 생성된 출력보다 크지 않다.
- match length가 출력 상한을 넘지 않는다.
- Huffman code lengths가 Kraft inequality를 위반하지 않는다.
- entropy table의 frequency 합과 symbol 범위가 유효하다.
- block count와 크기 합이 frame 크기 안에 있다.
- dictionary ID와 dictionary 크기가 허용 범위다.
- 원본 크기가 애플리케이션 정책 상한 이하다.
- decoder가 정확히 선언된 출력 크기를 생성한다.

### Compression bomb

작은 입력이 매우 큰 출력을 만들 수 있다. 1 MiB compressed frame이 수 GiB 출력을 선언할 수 있으므로 비율만 보지 말고 절대 출력 상한, 작업별 메모리 quota, CPU deadline을 둔다.

중첩 컨테이너는 각 단계의 ratio가 곱해진다. 압축 archive 안의 압축 레코드를 재귀적으로 풀 수 있다면 depth와 누적 출력량도 제한한다.

### 부분 출력

오류가 발생했을 때 이미 쓴 출력은 신뢰할 수 없다. decoder API는 성공한 byte 수만 돌려주고 호출자가 앞부분을 사용하는 식이면 손상 데이터가 소비될 수 있다. 성공 시에만 전체 block을 공개하거나, streaming decoder라면 검증 완료 단위와 소비 가능 단위를 명확히 한다.

### 결정성과 호환성

동일한 입력이 항상 동일한 compressed byte를 만들 필요는 포맷에 따라 다르다. 빌드 캐시와 patch diff에는 결정적 encoder가 유리하지만, decoder 호환성에 필요한 것은 어떤 conforming stream도 같은 원본으로 복원하는 것이다.

포맷 버전, 선택 기능, dictionary, checksum 범위를 명시하고 golden vector를 유지한다. Decoder를 새로 최적화해도 기존 vector를 모두 같은 byte로 복원해야 한다.

### Fuzzing

압축 decoder는 coverage-guided fuzzing의 효과가 크다.

- seed corpus에 정상 frame과 각 경계 크기를 넣는다.
- bit flip, length mutation, block reorder, truncation을 생성한다.
- AddressSanitizer, UndefinedBehaviorSanitizer, integer sanitizer를 사용한다.
- 출력·메모리·시간 상한을 harness에서 강제한다.
- reference decoder와 differential test한다.

## 9. 재현 가능한 벤치마크

### Corpus

압축률은 데이터에 종속된다. 한 파일 결과를 일반화하지 않는다.

```text
source code/text
structured binary records
integer arrays
floating-point arrays
already compressed media
small messages
large asset blocks
highly repetitive synthetic data
incompressible random-like data
```

실제 workload의 파일별 가중치와 요청 빈도를 반영한다. 합친 평균만 제시하면 작은 파일의 고정 비용과 큰 파일의 처리량이 섞인다.

### 측정 항목

- compressed size와 size ratio
- compression MB/s
- decompression MB/s
- p50/p95/p99 block latency
- peak working memory
- allocation count
- first-block latency
- random block access latency
- 병렬 확장성과 memory bandwidth
- malformed input rejection cost

### 방법

1. CPU governor, core affinity, compiler와 library version을 기록한다.
2. cold storage I/O와 warm memory benchmark를 분리한다.
3. 출력 byte를 checksum하여 작업이 제거되지 않게 한다.
4. 충분한 반복과 confidence interval을 사용한다.
5. compressor level, thread count, dictionary를 명시한다.
6. 입력과 출력 buffer 할당 시간을 포함한 결과와 제외한 결과를 구분한다.
7. 이미 압축된 데이터에서 expansion도 기록한다.

### 선택 절차

압축 알고리즘은 이름이 아니라 제약으로 선택한다.

```text
정확성:
  무손실인가, 일부 필드에 손실 허용이 있는가?

지연:
  첫 block deadline과 전체 처리량은 얼마인가?

용량:
  다운로드·저장 공간과 실행 중 메모리 중 무엇이 병목인가?

접근:
  순차 stream인가, 작은 random block인가?

환경:
  encoder는 offline인가, runtime인가?

안전:
  입력이 신뢰 경계 밖에서 오는가?
```

빠른 decoder가 필요한 경로와 작은 다운로드가 중요한 경로는 같은 설정을 쓸 이유가 없다. 포맷 하나 안에서도 block 종류별 codec과 level을 다르게 선택할 수 있지만, decoder 수와 테스트 조합이 늘어나는 운영 비용까지 계산한다.


---

## 결론

무손실 압축은 **모델링, 표현, 탐색**의 결합이다. 엔트로피는 선택한 확률 모델에서의 평균 정보량을 주고, prefix code와 arithmetic/range/ANS는 통계적 중복을 bitstream으로 바꾼다. LZ77은 이미 출력된 문자열을 dictionary로 사용하며, encoder의 실제 비용은 match finder와 parse가 결정한다.

압축률을 높이는 선택은 독립 블록, 랜덤 접근, 메모리, encoder 시간과 충돌한다. 따라서 frame과 block, dictionary, checksum, 원본 크기는 단순 metadata가 아니라 병렬성·복구·안전 계약이다. Decoder는 신뢰할 수 없는 parser로 취급해 모든 길이와 거리를 checked arithmetic으로 검증하고, 출력·시간·메모리 상한을 강제해야 한다.

최종 선택은 "LZ 계열이 빠르다" 같은 이름표가 아니라 실제 corpus의 size ratio, decode p99, peak memory, I/O와의 overlap, 오류 입력 비용으로 내린다. 압축된 byte 수가 줄어도 전체 데이터 준비 시간이 줄지 않는다면 시스템 최적화에는 실패한 것이다.

## 더 읽을거리

- Claude E. Shannon, ["A Mathematical Theory of Communication"](https://people.math.harvard.edu/~ctm/home/text/others/shannon/entropy/entropy.pdf)
- David A. Huffman, ["A Method for the Construction of Minimum-Redundancy Codes"](https://ieeexplore.ieee.org/document/4051119)
- Jacob Ziv, Abraham Lempel, ["A Universal Algorithm for Sequential Data Compression"](https://ieeexplore.ieee.org/document/1055714)
- Jarek Duda, ["Asymmetric Numeral Systems"](https://arxiv.org/abs/0902.0271)
- RFC 1951, [DEFLATE Compressed Data Format](https://www.rfc-editor.org/rfc/rfc1951)
- RFC 8878, [Zstandard Compression and the `application/zstd` Media Type](https://www.rfc-editor.org/rfc/rfc8878)
- Yann Collet, [LZ4 Block Format](https://github.com/lz4/lz4/blob/dev/doc/lz4_Block_format.md)
- Khalid Sayood, *Introduction to Data Compression*
- David MacKay, [*Information Theory, Inference, and Learning Algorithms*](https://www.inference.org.uk/itprnn/book.pdf)
