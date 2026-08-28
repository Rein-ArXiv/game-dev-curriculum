## 들어가며

무손실 압축을 짓는다. canonical Huffman, 산술 부호기, LZ77(match finder와 세 가지 파싱), 그리고 프레임 형식과 복호기.

이 단계가 소유하는 계약 셋.

1. **round-trip.** 어떤 입력에서도 `decode(encode(x)) == x`.
2. **엔트로피는 선택한 모델의 하한이지 파일 크기의 예언이 아니다.** header · 빈도표 · padding · checksum은 그 하한 밖에 있고, 측정 표에 따로 세운다.
3. **복호기는 신뢰할 수 없는 입력을 읽는 parser다.** 잘린 것, 손상된 것, 자원을 무한히 요구하는 것을 전부 거부해야 한다.

---

## 목차
1. [엔트로피와 그 밖의 비용](#1-엔트로피와-그-밖의-비용)
2. [Kraft 부등식과 길이 상한](#2-kraft-부등식과-길이-상한)
3. [canonical 배정의 한 줄](#3-canonical-배정의-한-줄)
4. [겹침 복사는 memcpy가 아니다](#4-겹침-복사는-memcpy가-아니다)
5. [적대적 입력 거부](#5-적대적-입력-거부)
6. [산술 부호기와 underflow](#6-산술-부호기와-underflow)
7. [측정과 관측](#7-측정과-관측)

---

## 1. 엔트로피와 그 밖의 비용

0차 엔트로피는 심볼을 독립으로 보는 모델의 하한이다.

<!-- snippet: src/algo/compress.hpp@compress-entropy -->
```cpp
// 0차 엔트로피 H = -sum p log2 p, 심볼당 비트. 심볼을 독립으로 보는 모델의
// 하한이다 — 반복 구조가 있는 데이터는 이 값보다 훨씬 작게 줄 수 있고,
// 그때 이기는 것은 더 나은 코드가 아니라 더 나은 *모델* 이다.
inline double Entropy0(const Bytes& data) {
    if (data.empty()) return 0.0;
    uint64_t freq[256] = {0};
    for (uint8_t b : data) ++freq[b];
    double h = 0.0;
    const double n = double(data.size());
    for (int i = 0; i < 256; ++i) {
        if (!freq[i]) continue;
        const double p = double(freq[i]) / n;
        h -= p * std::log2(p);
    }
    return h;
}
```

Huffman의 평균 코드 길이 L은 `H <= L < H + 1`을 만족한다. 검사는 이것을 코퍼스 다섯 종류에서 실제로 확인한다 — 이론이 실측에서 성립하는지 보는 것이지, 성립한다고 가정하지 않는다.

```cpp
double bits = 0.0;
for (int s = 0; s < 256; ++s) bits += double(freq[s]) * double(lengths.length[s]);
const double avg = bits / double(raw.size());
// H <= L < H + 1. 심볼 단위 부호의 이론 한계가 실측에서 성립한다.
CHECK(avg >= h - 1e-9);
CHECK(avg < h + 1.0 + 1e-9);
```

그리고 그 하한 밖의 비용을 프레임이 명시적으로 드러낸다.

<!-- snippet: src/algo/compress.hpp@compress-frame -->
```cpp
// 프레임 형식. magic · version · 원본 크기 · checksum 을 앞에 둔다.
// 이것들이 압축된 비트 밖에 있는 *가격* 이고, 작은 입력에서 압축이 지는 이유다.
//   magic     이 바이트열이 우리 형식인가
//   version   복호기가 이해하는 형식인가
//   raw_size  얼마나 나와야 하는가(자원 상한이자 종료 조건)
//   checksum  나온 것이 맞는가
struct FrameHeader {
    static const uint32_t kMagic = 0x314C4141u;   // "AAL1"
    static const uint16_t kVersion = 1;
};

inline void PutU32(Bytes& b, uint32_t v) {
    b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8));
    b.push_back(uint8_t(v >> 16)); b.push_back(uint8_t(v >> 24));
}
inline uint32_t GetU32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

inline uint32_t Adler32(const uint8_t* data, size_t n) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < n; ++i) { a = (a + data[i]) % 65521u; b = (b + a) % 65521u; }
    return (b << 16) | a;
}
```

Huffman 프레임의 길이표는 심볼당 1바이트로 그대로 싣는다. 더 줄일 수 있지만 표 비용을 눈에 보이게 두는 것이 목적이고, 측정 표에 `hdr_B` 칸이 그 270바이트를 따로 센다.

## 2. Kraft 부등식과 길이 상한

접두 부호가 존재하려면 `sum 2^-len <= 1`이어야 한다(Kraft 부등식). 손상된 길이표를 그대로 믿고 코드를 배정하면 겹치는 코드가 생겨 복호가 조용히 틀린다.

길이 상한이 필요한 이유는 복호 lookup 표 크기가 `2^maxlen`이기 때문이다. 그런데 깊이를 그냥 잘라 내면 Kraft 합이 1을 넘는다 — 접두 부호가 아닌 것을 접두 부호라고 부르는 셈이다.

<!-- snippet: src/algo/compress.hpp#LimitCodeLengths -->
```cpp
inline void LimitCodeLengths(HuffmanLengths& h, uint8_t limit) {
    if (h.max_length <= limit || h.symbols == 0) return;
    std::vector<size_t> count(size_t(h.max_length) + 1, 0);
    for (int s = 0; s < 256; ++s) if (h.length[s]) ++count[h.length[s]];

    for (size_t l = h.max_length; l > limit; --l) {
        while (count[l] > 0) {
            size_t j = l - 2;
            while (j > 0 && count[j] == 0) --j;
            if (j == 0) return;          // 상한이 심볼 수에 비해 너무 작다
            count[l]     -= 2;
            count[l - 1] += 1;
            count[j + 1] += 2;
            count[j]     -= 1;
        }
    }
    // 재배정. 원래 짧았던 심볼(=빈도가 큰 심볼)이 계속 짧도록 (길이, 심볼) 오름차순.
    std::vector<int> order;
    for (int s = 0; s < 256; ++s) if (h.length[s]) order.push_back(s);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        if (h.length[a] != h.length[b]) return h.length[a] < h.length[b];
        return a < b;
    });
    size_t at = 0;
    h.max_length = 0;
    for (size_t l = 1; l <= limit; ++l) {
        for (size_t k = 0; k < count[l]; ++k) {
            h.length[order[at++]] = uint8_t(l);
            h.max_length = uint8_t(l);
        }
    }
}
```

한 번의 조정이 `-2·2^-l + 2^-(l-1) + 2·2^-(j+1) - 2^-j = 0`이라 Kraft 합이 정확히 보존된다. 최적성은 조금 잃고(평균 길이가 미세하게 는다) 상한을 얻는다.

검사는 무작위 알파벳 40개에 대해 Kraft가 성립하고 `max_length <= 15`인지, 그리고 어떤 코드도 다른 코드의 접두가 아닌지를 모든 쌍에서 확인한다. lookup 표가 모든 유효 접두를 빈틈없이 덮는지도 본다 — Kraft 합이 정확히 1이므로 빈칸이 있으면 안 된다.

## 3. canonical 배정의 한 줄

canonical 규칙은 길이 표만으로 코드를 유일하게 재구성한다. 그것이 트리 모양을 저장하지 않아도 되는 이유다.

<!-- snippet: src/algo/compress.hpp@compress-canonical mode=partial -->
```cpp
// canonical 코드 배정. 같은 길이끼리 심볼 번호 오름차순으로 연속된 코드를 준다.
// 길이가 증가할 때 code 를 왼쪽으로 미는(shift) 한 줄이 접두 성질의 전부다 —
// 그 줄을 빼면 서로 다른 심볼이 같은 비트열을 갖는다.
inline CanonicalCode AssignCanonical(const HuffmanLengths& h) {
    CanonicalCode cc;
    std::memcpy(cc.length, h.length, sizeof(cc.length));
    cc.max_length = h.max_length;
    uint32_t code = 0;
    for (uint8_t len = 1; len <= h.max_length; ++len) {
        code <<= 1;                          // ← 길이가 하나 늘면 자리도 하나 는다
        for (int s = 0; s < 256; ++s) {
            if (h.length[s] != len) continue;
            cc.code[s] = code++;
        }
    }
    return cc;
}
```

`code <<= 1;` 한 줄이 접두 성질의 전부다. 이 줄이 없으면 길이 2짜리 코드가 길이 1짜리 코드의 값을 그대로 이어받아, 서로 다른 심볼이 겹치는 비트열을 갖는다.

lookup 표는 표 폭만큼 미리 보고 실제 소비 비트를 표에서 얻는 형태다. 손상된 표에서는 배정된 코드가 표 폭을 넘을 수 있으므로, 표를 만드는 쪽도 범위 밖을 쓰지 않아야 한다.

```cpp
// 손상된 길이표에서 배정된 코드는 표 폭을 넘을 수 있다. 호출부가 Kraft 로
// 먼저 거르지만, 표를 만드는 쪽도 범위 밖을 쓰지 않아야 한다 — 복호기는
// 신뢰할 수 없는 입력을 읽는 코드이고, 방어는 한 겹으로 두지 않는다.
if (size_t(base) + size_t(span) > t.symbol.size()) continue;
```

## 4. 겹침 복사는 memcpy가 아니다

LZ77 복사에서 source와 destination은 겹칠 수 있다. `distance < length`면 아직 쓰지 않은 바이트를 읽어야 하고, 그것이 정상이다.

<!-- snippet: src/algo/compress.hpp@compress-lz-frame mode=partial -->
```cpp
// 복호기. 신뢰할 수 없는 parser 로 다룬다.
//   - 잘린 토큰 거부
//   - distance 가 지금까지 낸 바이트 수보다 크면 거부 (범위 밖 읽기)
//   - 출력 상한(raw_size)을 넘으면 거부
//   - **겹침 복사**: distance < length 면 source 와 destination 이 겹친다.
//     memcpy 의미론이 아니라 바이트 단위로 앞에서부터 복사해야 한다 —
//     그것이 "aaaa..." 를 (dist=1, len=100) 하나로 표현할 수 있게 하는 규칙이다.
inline bool LzDecode(const Bytes& frame, Bytes* out) {
    // ...
        if (length == 0 || distance == 0) return false;
        if (distance > out->size()) return false;
        // 상한 검사를 *더하기 전에* 한다. out->size() + length 를 먼저 계산해
        // 비교하면 큰 값에서 감기며 통과한다.
        if (length > raw_size - uint32_t(out->size())) return false;
        const size_t from = out->size() - distance;
        for (uint32_t k = 0; k < length; ++k) out->push_back((*out)[from + k]);
    }
    // ...
}
```

검사는 이 의미론을 **손으로 만든 프레임**으로 강제한다. 인코더가 그런 토큰을 낼 때까지 기다리지 않는다.

```cpp
// 겹침 복사. distance=1, length=200 은 memcpy 로 짜면 틀린다 — 원본이
// 복사 도중 자라야 "aaaa..." 가 나온다.
frame.push_back(0); frame.push_back('q');            // 리터럴 하나
frame.push_back(1);
frame.push_back(200); frame.push_back(0);            // length = 200
frame.push_back(1); frame.push_back(0);              // distance = 1
Bytes back;
CHECK(LzDecode(frame, &back));
CHECK(back == want);
```

상한 검사의 순서도 계약이다. `out->size() + length > raw_size`로 쓰면 length가 큰 값일 때 덧셈이 감기며 통과한다. 그래서 `length > raw_size - out->size()`로 뺄셈 쪽에 둔다.

파싱 전략은 세 가지다. 같은 match finder 위에서 선택 규칙만 다르다.

<!-- snippet: src/algo/compress.hpp@compress-lz-parse mode=partial -->
```cpp
// 세 파싱 전략. 같은 match finder 위에서 *선택 규칙만* 다르다.
//   Greedy   지금 가장 긴 것을 잡는다.
//   Lazy     다음 위치가 더 길면 지금은 리터럴로 흘린다.
//   Optimal  각 위치를 노드로 보고 비용 최단 경로를 푼다 — DAG 위 DP 다.
inline std::vector<LzToken> LzParse(const Bytes& data, ParseMode mode,
                                    uint32_t window = 4096, uint32_t depth = 32) {
    std::vector<LzToken> out;
    if (data.empty()) return out;
    MatchFinder finder(data, window, depth);
    // ...
}
```

## 5. 적대적 입력 거부

복호기 검사는 일곱 갈래다.

| 공격 | 무엇을 확인하나 |
|---|---|
| 절단 | 모든 절단 위치에서 거부하거나 정확히 복원. "부분적으로 그럴듯한 출력"은 없다 |
| magic 손상 | 형식 판별 |
| version 손상 | 버전 판별 |
| 길이표 손상 | Kraft 또는 checksum이 잡는다 |
| checksum 손상 | 형식은 맞고 내용만 틀린 경우 |
| 범위 밖 distance | 지금까지 낸 바이트보다 멀리 가리킴 |
| 자원 상한 초과 | 선언한 원본 크기를 넘는 토큰 |
| 알 수 없는 토큰 종류 | 미래 형식으로 오해하지 않음 |

절단 검사가 흥미롭다. 4,000바이트 텍스트를 압축한 프레임을 7바이트 간격으로 잘라 전부 돌린다.

```cpp
// (1) 잘림. 모든 절단 위치에서 거부하거나 정확히 복원해야 한다 —
//     "부분적으로 그럴듯한 출력" 은 없어야 한다.
for (size_t cut = 0; cut < huff.size(); cut += 7) {
    const Bytes chopped(huff.begin(), huff.begin() + std::ptrdiff_t(cut));
    if (HuffmanDecompress(chopped, &out)) CHECK(out == raw);
    else CHECK(out.empty() || out.size() < raw.size());
}
```

"거부하거나 정확히 복원"이라는 이중 조건인 이유는 마지막 padding 바이트가 잘려도 심볼 수가 다 채워지면 정상 복원이 가능하기 때문이다. 금지하는 것은 성공을 보고하면서 다른 내용을 내는 경우다.

## 6. 산술 부호기와 underflow

산술 부호기는 심볼 하나를 정수 개 비트에 맞출 필요가 없다. 그래서 확률이 매우 치우친 알파벳에서 Huffman의 "심볼당 최소 1비트" 하한을 넘어선다.

<!-- snippet: src/algo/compress.hpp@compress-arith-coder mode=partial -->
```cpp
// 산술 부호기(range coder 계열). 심볼 하나를 정수 개 비트에 맞출 필요가 없어,
// 확률이 매우 치우친 알파벳에서 Huffman 의 "심볼당 최소 1비트" 하한을 넘어선다.
//
// 32비트 [low, high] 구간을 좁혀 가며, 상위 비트가 확정되면 흘려보낸다.
// 어려운 곳은 세 번째 분기다 — low 와 high 가 half 를 사이에 두고 가까워지면
// 상위 비트가 확정되지 않은 채 구간만 좁아진다(underflow). 그때는 가운데를
// 잘라내고 '보류 비트' 를 세어 두었다가, 다음에 확정되는 비트의 반대값으로 낸다.
// 이 처리를 빼면 대부분의 입력에서 잘 돌다가 특정 확률 배치에서만 깨진다.
//
// 정적 0차 모델만 쓴다(빈도표를 프레임에 싣는다). total <= 2^30 이어야
// 재정규화 뒤 range/total >= 1 이 보장된다 — 빈도를 4095 로 눌러 담는 이유다.
class ArithEncoder {
public:
    void Encode(uint32_t cum_low, uint32_t cum_high, uint32_t total) {
        const uint64_t range = uint64_t(high_) - uint64_t(low_) + 1;
        high_ = uint32_t(low_ + (range * cum_high) / total - 1);
        low_  = uint32_t(low_ + (range * cum_low) / total);
        for (;;) {
            if (high_ < kHalf) {
                PutBit(0);
            } else if (low_ >= kHalf) {
                PutBit(1);
                low_  -= kHalf;
                high_ -= kHalf;
            } else if (low_ >= kQuarter && high_ < kThreeQuarter) {
                ++pending_;                    // ← underflow. 확정을 미룬다.
                low_  -= kQuarter;
                high_ -= kQuarter;
            } else {
                break;
            }
            low_  = low_ << 1;
            high_ = (high_ << 1) | 1u;
        }
    }
    // ...
};
```

이 lab의 첫 판본은 carry 처리 없는 range coder였다. 왕복 검사가 여덟 코퍼스 중 넷에서 실패했고, 그 실패가 "특정 확률 배치에서만 깨진다"는 성질을 그대로 보여 줬다 — 짧은 입력이나 균등 분포에서는 통과했다. 보류 비트를 세는 형태로 다시 짓고 나서 전부 통과했다.

검사는 왕복만 보지 않고 Huffman을 실제로 이기는지도 본다.

```cpp
// 극단적으로 치우친 알파벳에서 range coder 는 심볼당 1비트 하한을 넘어선다.
Bytes skewed(40000, 'a');
for (size_t i = 0; i < skewed.size(); i += 500) skewed[i] = 'b';
const size_t huff_bits = HuffmanCompress(skewed).size() * 8;
const size_t range_bits = RangeCompress(skewed).size() * 8;
CHECK(range_bits < huff_bits);
CHECK(double(huff_bits) / double(skewed.size()) >= 1.0);   // Huffman 은 못 넘는다
```

## 7. 측정과 관측

### 코퍼스 여덟 종류

`algo_bench --compress`, 40만 바이트(tiny-msg만 17바이트).

| corpus | H(bits) | codec | ratio | saving% | enc MB/s | dec MB/s | hdr B |
|---|---|---|---|---|---|---|---|
| text | 4.293 | huffman | 0.5437 | 45.63 | 81.4 | 61.4 | 270 |
| text | 4.293 | arith | 0.5380 | 46.20 | 16.5 | 14.1 | 526 |
| text | 4.293 | lz-greedy | 0.5115 | 48.85 | 35.7 | 193.9 | 14 |
| text | 4.293 | lz-optimal | 0.4890 | 51.10 | 5.0 | 191.8 | 14 |
| struct-bin | 3.654 | huffman | 0.4589 | 54.11 | 100.0 | 59.0 | 270 |
| struct-bin | 3.654 | lz-greedy | 0.8376 | 16.24 | 41.5 | 213.3 | 14 |
| int-array | 4.935 | huffman | 0.6194 | 38.06 | 90.3 | 70.5 | 270 |
| int-array | 4.935 | lz-greedy | 1.0659 | -6.59 | 45.6 | 167.6 | 14 |
| repetitive | 2.000 | huffman | 0.2507 | 74.93 | 156.2 | 147.8 | 270 |
| repetitive | 2.000 | lz-greedy | 0.0194 | 98.06 | 64.0 | 217.9 | 14 |
| random | 8.000 | huffman | 1.0007 | -0.07 | 76.5 | 74.0 | 270 |
| random | 8.000 | lz-greedy | 1.9990 | -99.90 | 38.1 | 218.9 | 14 |
| tiny-msg | 3.499 | huffman | 16.3529 | -1535.29 | 1.4 | 5.2 | 270 |

읽을 것이 다섯이다.

**압축률은 데이터의 성질이지 알고리즘의 성질이 아니다.** 같은 Huffman이 repetitive에서 75%를 줄이고 random에서 0.07% *늘린다*. 엔트로피 칸이 그 차이를 그대로 예고한다 — H가 2.000이면 8비트 심볼을 2비트로 줄일 수 있고, H가 8.000이면 줄일 것이 없다.

**난수 입력에서 모든 무손실 압축기가 커진다.** Huffman은 header 270바이트만큼, LZ는 두 배 가까이. LZ가 두 배가 되는 것은 이 형식의 리터럴이 `[0][byte]` 2바이트이기 때문이고, 실제 압축기가 리터럴에 엔트로피 부호를 얹는 이유가 정확히 이것이다. 셈 논증(비둘기집)의 실측이 이 두 행이다.

**모델이 다르면 이기는 데이터가 다르다.** struct-bin에서 Huffman이 54% 줄이는 동안 LZ는 16%만 줄인다 — 16바이트 레코드의 반복이 LZ 창(4KiB) 안에서 잘 안 잡히고, 대신 바이트 빈도가 치우쳐 Huffman이 이긴다. repetitive에서는 정반대로 LZ가 98%를 줄인다. int-array에서 LZ는 오히려 6.6% 늘리고 Huffman은 38% 줄인다.

**optimal 파싱이 텍스트에서 2.3%p를 더 얻고 인코딩이 7배 느려진다.** 0.5115 → 0.4890, 35.7MB/s → 5.0MB/s. 각 위치에서 가능한 모든 일치 길이를 시도하는 DP라 비용이 그만큼 든다. 복호 속도는 그대로다 — 파싱은 인코더만의 일이기 때문이다.

**작은 입력에서는 header가 전부다.** 17바이트 메시지에 Huffman은 278바이트를, 산술 부호기는 534바이트를 쓴다. 압축률 16배·31배 *증가*다. 엔트로피 3.499는 "17바이트를 7.4바이트로 줄일 수 있다"고 말하지만, 그 하한은 빈도표를 공짜로 가정한 값이다. LZ는 header가 14바이트뿐이라 2.8배 증가에 그친다.

산술 부호기가 Huffman을 이기는 폭은 이 코퍼스들에서 미미하다(text 0.5437 → 0.5380). 심볼당 평균 코드 길이의 소수점 이하만큼이 이득인데, 속도는 5배 느리고 빈도표가 두 배(526 대 270)다. 극단적으로 치우친 분포에서만 이 교환이 맞는다.

### 블록 크기

| block | blocks | total bytes | ratio | p99 block ms |
|---|---|---|---|---|
| 4,096 | 489 | 1,215,323 | 0.6077 | 0.134 |
| 65,536 | 31 | 1,094,208 | 0.5471 | 1.168 |
| 1,048,576 | 2 | 1,087,082 | 0.5435 | 11.744 |

블록이 작으면 표 비용이, 크면 지연이 는다. 4KiB 블록은 489개의 길이표(각 270바이트 = 129KiB)를 실어 압축률이 6%p 나쁘고, 1MiB 블록은 p99 블록 지연이 11.7ms다. 실시간 스트림이라면 4KiB의 0.13ms를, 아카이브라면 1MiB를 고른다 — 같은 코덱에서 블록 크기만으로 갈리는 결정이다.

64KiB에서 1MiB로 갈 때 압축률 이득이 0.36%p뿐이라는 점이 실용적인 관찰이다. 지연은 10배 늘어난다.

### 무력화 변이

**변이 1 — canonical 배정의 `code <<= 1;` 제거.**

```text
  ok   compress_entropy_bound
  FAIL compress_huffman_roundtrip
  FAIL compress_canonical_prefix
  ok   compress_lz_roundtrip
  FAIL compress_decoder_hostile
  ok   compress_range_coder
```

셋이 함께 죽는다. 접두 성질 검사가 가장 좁게 잡고(코드 쌍의 접두 관계를 직접 본다), 왕복 검사는 결과로 잡으며, 적대 입력 검사는 절단된 프레임이 "성공을 보고하면서 다른 내용을 내는" 상태가 되어 잡는다. LZ 쪽은 canonical 배정을 쓰지 않으므로 영향이 없다 — 어느 검사가 어느 코드에 걸려 있는지가 출력에 그대로 드러난다.

**변이 2 — LZ 겹침 복사를 고정 크기 사본으로.** 복사 전에 `min(length, avail)` 바이트를 스냅숏으로 떠 두고 그것만 쓰는 형태, 즉 memcpy 의미론이다. `compress_lz_roundtrip`이 죽는다 — distance=1, length=200 프레임이 'q' 하나 + 0 199개를 낸다.

**변이 3 — `KraftOk`를 항상 true로.** `compress_canonical_prefix`가 죽는다. 그 검사가 `CHECK(!KraftOk(bad))`로 Kraft 판정 자체를 직접 단언하기 때문이다.

### 살아남은 변이 하나

**`HuffmanDecompress`에서 `if (!KraftOk(lengths)) return false;` 호출을 지우면 검사가 죽지 않는다.** 이것을 감추지 않고 적어 둔다.

이유는 이 프레임 형식에 Adler-32가 있기 때문이다. 접두 부호가 아닌 길이표로 복호하면 나온 바이트가 원본과 다르고, 그러면 checksum이 잡는다. 손상된 길이표 검사(`compress_decoder_hostile`의 (3))도 Kraft 대신 checksum 경로로 거부에 도달한다.

그러니 이 코드베이스에서 Kraft 검사가 하는 일은 "잡는 것"이 아니라 **더 일찍, 더 싸게 거부하는 것**이다. `raw_size`가 큰 손상 프레임에서 checksum은 그 크기만큼 복호한 뒤에야 판정하고, Kraft는 표를 읽자마자 끊는다. 검사가 잡지 못하는 성질을 검사가 잡는 척하지 않는 것이 이 lab의 규칙이므로, 이 변이는 생존으로 기록한다.

세 변이와 이 생존 사례 모두, 주입과 복원 뒤 소스 SHA-256이 주입 전과 동일함을 확인했다.

### 실행

```powershell
cmake --build build --config Release
.\build\Release\algo_tests.exe --step=08
```

```text
  ok   compress_entropy_bound
  ok   compress_huffman_roundtrip
  ok   compress_canonical_prefix
  ok   compress_lz_roundtrip
  ok   compress_decoder_hostile
  ok   compress_range_coder
algo_tests[step08]: 6/6 passed (492020 check(s))
```

```powershell
.\build\Release\algo_tests.exe --properties --step=08
```

```text
  ok   compress/roundtrip           표본   340 · 비퇴화  59% · 건너뜀 0
algo_tests[properties]: 1/1 passed (총 340 표본, seed 20260824)
```

이 property는 무작위 코퍼스·크기·파싱 모드 조합마다 세 코덱이 왕복하고, checksum을 손상시킨 프레임을 거부하는지 확인한다.

lab 전체는 ctest 11개로 등록돼 있다.

```powershell
ctest --test-dir build -C Release --output-on-failure
```

```text
100% tests passed out of 11

Total Test time (real) =   2.53 sec
```

---

## 결론

압축률이 데이터의 성질이라는 것이 여덟 코퍼스 표의 결론이다. 같은 Huffman이 반복 데이터에서 75%를 줄이고 난수에서 커지며, 그 갈림은 엔트로피 칸이 미리 말한다. LZ와 Huffman이 이기는 데이터가 서로 반대라는 것도 같은 표에서 읽힌다 — 이기는 것은 더 나은 코드가 아니라 데이터에 맞는 모델이다.

엔트로피가 파일 크기의 예언이 아니라는 것은 17바이트 메시지에서 가장 선명했다. H=3.499는 7.4바이트를 약속하지만 실제 출력은 278바이트다. 차이의 전부가 빈도표와 header이고, 그 비용을 표에 따로 세운 것이 이 단계의 설계였다.

복호기를 신뢰할 수 없는 parser로 다루는 규칙은 겹침 복사와 상한 검사 순서에서 구체적인 코드가 됐다. 그리고 Kraft 검사가 checksum과 겹쳐 무력화 변이에서 살아남는다는 사실도 그대로 기록했다 — 그 검사가 거부의 *비용*을 줄이지 거부 자체를 만들지는 않는다.

여기서 다루지 않은 것은 적응형 모델과 문맥 모델링이다. 이 lab의 두 엔트로피 부호기는 전부 정적 0차 모델이고, 빈도표를 프레임에 싣는 대가를 그대로 문다. 문맥 혼합이나 ANS 계열, 그리고 사전을 공유하는 스트림 형식은 구현 범위 밖이다.
