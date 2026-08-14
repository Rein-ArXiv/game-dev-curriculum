## 들어가며

컴퓨터는 숫자밖에 모른다. 그렇다면 매일 사용하는 텍스트는 어떻게 메모리에 저장될까? 단순해 보이는 이 질문에 대한 답은 놀라울 정도로 복잡하며, 그 역사는 엔지니어링 결정이 어떻게 수십 년간 영향을 미치는지 보여주는 완벽한 케이스 스터디다.

게임 엔진을 개발하거나 시스템 프로그래밍을 하다 보면 불가피하게 문자 인코딩과 마주하게 된다. UTF-8과 UTF-16 중 무엇을 써야 할까? 문자열의 "길이"는 정확히 무엇을 의미할까? 한 바이트가 정확히 무엇을 표현하는가?

이 글에서는 ASCII부터 현대 Unicode까지의 여정을 따라가며, 각 설계 결정의 "왜"와 "어떻게"를 깊이 있게 탐구한다. 수학적 정확성과 실전 코드를 통해 문자 인코딩의 본질을 이해해 보자. Windows API의 A/W 이중성, MBCS 경계, Locale/NLS 같은 Windows 특화 문제도 인코딩 경계의 사례로 함께 다룬다.


---

## 목차
1. [ASCII: 7비트의 유산](#1-ascii-7비트의-유산)
2. [한글의 여정: 조합형과 완성형](#2-한글의-여정-조합형과-완성형)
3. [Unicode의 탄생과 이상](#3-unicode의-탄생과-이상)
4. [UTF-8: 냅킨에서 탄생한 천재적 설계](#4-utf-8-냅킨에서-탄생한-천재적-설계)
5. [UTF-16과 16비트 고정 길이의 환상](#5-utf-16과-16비트-고정-길이의-환상)
6. [멀티바이트 문자 경계의 함정](#6-멀티바이트-문자-경계의-함정)
7. [BOM (Byte Order Mark)](#7-bom-byte-order-mark)
8. [실전 패턴과 베스트 프랙티스](#8-실전-패턴과-베스트-프랙티스)
9. [성능 최적화](#9-성능-최적화)


---

## 1. ASCII: 7비트의 유산

### 태초의 설계

1963년, 미국 표준 협회(ASA)는 ASCII(American Standard Code for Information Interchange)를 발표했다. 설계는 놀라울 정도로 간결했다.

7비트 = 128개 문자. 바이너리 표현 범위는 `0x00`부터 `0x7F`까지다. 이 128개를 다음과 같이 나눴다.

- **0-31**: 제어 문자 (Control characters)
 - `0x00` (NUL): 문자열 종료
 - `0x0A` (LF): Line Feed
 - `0x0D` (CR): Carriage Return
 - `0x1B` (ESC): Escape
- **32-126**: 출력 가능 문자
 - 공백, 숫자 0-9
 - 영문 대문자 A-Z, 소문자 a-z
 - 구두점, 기호
- **127**: DEL (Delete)

비트 패턴을 살펴보면 설계자의 천재성이 드러난다.

```text
'A' = 0x41 = 0100 0001
'a' = 0x61 = 0110 0001
        차이 → ^^ (bit 5)
```

대소문자 변환은 단순히 5번 비트를 토글하는 것으로 가능하다.

```c
// 알파벳 범위(A-Z, a-z)에서만 유효한 트릭이다
char to_upper(char c) {
    return c & ~0x20;  // bit 5를 0으로
}

char to_lower(char c) {
    return c | 0x20;   // bit 5를 1로
}
```

숫자 변환도 우아하다.

```text
'0' = 0x30 = 0011 0000
'9' = 0x39 = 0011 1001
        → 하위 4비트가 실제 값
```

```c
int digit_to_int(char c) {
    return c & 0x0F;  // 하위 4비트 추출
}
```

### 8비트 시대와 패리티의 딜레마

실제 바이트는 8비트다. ASCII는 7비트만 사용했으니 최상위 비트(bit 7)는 어떻게 쓸까?

초기에는 패리티 비트(parity bit)로 사용했다.

```text
데이터: 100 0001 ('A', 7비트, 1이 2개)
짝수 패리티(even parity) 계산:
- 1의 개수가 짝수면 → 패리티 비트 = 0
- 1의 개수가 홀수면 → 패리티 비트 = 1

전송: [0]100 0001
       ↑ 패리티 비트 (1의 총 개수를 짝수로 유지)
```

이 방식으로 1비트 오류를 검출할 수 있었다. 하지만 통신이 안정화되면서 패리티 비트는 불필요해졌고, 128개의 빈 코드(`0x80`-`0xFF`)가 남았다.

### 코드 페이지의 혼란

각국은 상위 128개를 자국 언어를 위해 재정의했다. 이것이 코드 페이지(code page)의 시작이다.

ISO 8859 시리즈:
- **ISO 8859-1 (Latin-1)**: 서유럽 언어 (`0xE9` = é)
- **ISO 8859-2 (Latin-2)**: 동유럽 언어 (`0xE9` = é, 체코어)
- **ISO 8859-5**: 키릴 문자 (`0xE9` = щ)

Microsoft Windows 코드 페이지:
- **CP-1252** (Windows-1252): 서유럽
- **CP-1251**: 키릴
- **CP-1250**: 동유럽
- **CP-949**: 한글 (확장 완성형)

문제는 명확하다. 동일한 바이트 `0xE9`가 인코딩에 따라 다음과 같이 다르게 해석된다.

| 코드 페이지 | `0xE9`의 의미 |
|------------|----|
| CP-1252 | é (라틴 소문자 e + acute) |
| CP-1251 | щ (키릴 소문자 shcha) |
| CP-874 | ้ (태국어 tone mark) |

문서에 인코딩 메타데이터가 없으면 올바른 해석이 불가능하다. 이것이 이메일에서 깨진 글자를 보게 되는 근본 원인이다.

## 2. 한글의 여정: 조합형과 완성형

### 조합형 vs 완성형 논쟁

1980년대 한국은 한글 전산화를 위한 방법론을 놓고 격렬한 논쟁이 벌어졌다.

조합형(Johab) 방식은 한글이 자모 조합으로 이루어진다는 점을 활용한다.
- 초성 19개
- 중성 21개
- 종성 28개 (없음 포함)

수학적으로 가능한 조합은 다음과 같다.

$$
19 \times 21 \times 28 = 11{,}172
$$

정확히 현대 한글 음절의 개수다. 조합형은 2바이트 내에 초성, 중성, 종성을 비트로 인코딩한다.

```text
[5비트: 초성][5비트: 중성][5비트: 종성][나머지]
```

논리적으로 완벽하지만, 구현이 복잡했다. 화면 출력 시 비트를 분해하고 자모를 조합해 글꼴을 찾아야 했다.

완성형(KS X 1001, EUC-KR) 방식은 자주 쓰이는 2,350개 한글 음절만 테이블에 미리 넣는다. 간단한 룩업으로 글꼴에 접근할 수 있다.

하지만 문제가 있었다. 11,172개 중 2,350개만 표현 가능하니 8,822개의 한글이 누락되었다. "쀍", "쀓", "뷁" 같은 글자는 표현할 수 없다. 사람 이름, 지명 등에서 문제가 발생했다.

### CP949: 확장 완성형의 등장

Microsoft는 1990년대 Windows용으로 CP949(확장 완성형)를 개발했다. EUC-KR에 8,822개를 추가해 11,172개 모두를 지원한다.

멀티바이트 구조는 다음과 같다.

```text
1바이트 문자: 0x00-0x7F (ASCII 호환)
2바이트 문자:
  Lead byte:  0x81-0xFE
  Trail byte: 0x41-0x5A, 0x61-0x7A, 0x81-0xFE
```

예시: "가" = `0xB0 0xA1`

```c
const char* str = "ABC가나다";

// 바이트 배열:
// [41] [42] [43] [B0 A1] [B3 AA] [B4 D9]
//  A    B    C     가      나      다
```

이제 현대 한글 음절 11,172자를 모두 표현할 수 있었지만, 새로운 문제가 생겼다.

### 멀티바이트 경계의 복잡성

문자열을 중간에서 자르면 어떻게 될까?

```c
char str[] = "한글";  // [C7 D1] [B1 DB]

// 3바이트에서 자르면?
str[3] = '\0';
// → [C7 D1] [B1] [00]
//    "한"   ??? ← 불완전한 바이트
```

문제점은 다음과 같다.
1. 인덱싱 불가: `str[i]`가 i번째 문자를 의미하지 않음
2. 검색 복잡: "가"를 찾으려면 멀티바이트 경계를 따라가며 검증 필요
3. 역방향 순회 어려움: 뒤에서부터 읽을 때 문자 시작점 찾기 복잡

이러한 한계는 전 세계적으로 공통된 문제였고, 보편적 해결책이 필요했다.

## 3. Unicode의 탄생과 이상

### UCS-2: 16비트면 충분하다는 가정

1991년, Unicode Consortium은 야심찬 목표를 세웠다. 모든 인류의 문자를 하나의 표준으로 통합하자.

초기 설계 철학은 UCS-2(Universal Character Set - 2 bytes)였다.
- 모든 문자를 정확히 16비트(2바이트)로 표현
- 범위: `0x0000` ~ `0xFFFF` (65,536개)
- 고정 길이 → 간단한 인덱싱: `string[i]` = i번째 문자

이를 BMP(Basic Multilingual Plane)라고 부른다.

```text
U+0000 - U+007F:  ASCII
U+0080 - U+00FF:  라틴 확장
U+0100 - U+017F:  라틴 확장-A
U+0370 - U+03FF:  그리스어
U+0400 - U+04FF:  키릴 문자
U+0600 - U+06FF:  아랍어
U+0E00 - U+0E7F:  태국어
U+3040 - U+309F:  히라가나
U+30A0 - U+30FF:  가타카나
U+4E00 - U+9FFF:  CJK 한자 (20,992 코드 포인트, 최초 할당 20,902자)
U+AC00 - U+D7A3:  한글 음절 (11,172개)
```

65,536개면 충분할 것 같았다.

### 16비트의 한계

하지만 현실은 달랐다.

- **역사적 한자**: 70,000개 이상 (고전 중국어, 일본어, 한국어)
- **이모지**: 3,600개 이상 (계속 증가 중)
- **고대 문자**: 이집트 상형문자, 설형문자, 룬 문자
- **수학 기호**: $\mathbb{R}$, $\mathfrak{H}$, $\mathcal{L}$ 등
- **음악 기호**: 악보 표기
- **변형 문자**: 볼드, 이탤릭 수학 기호

65,536개로는 턱없이 부족했다.

### 서로게이트 페어: 16비트 한계 돌파

1996년, Unicode는 21비트로 확장하기로 결정한다.

```text
새로운 범위: U+000000 ~ U+10FFFF
총 코드 포인트: 1,114,112개
17개 Plane 구성:
  - Plane 0 (BMP):  U+0000   ~ U+FFFF
  - Plane 1 (SMP):  U+10000  ~ U+1FFFF   (추가 문자)
  - Plane 2 (SIP):  U+20000  ~ U+2FFFF   (한자 확장)
  - Plane 16:       U+100000 ~ U+10FFFF
```

하지만 이미 UCS-2를 사용하는 시스템이 있었다. 하위 호환성을 유지하며 확장하는 방법은?

서로게이트 페어(Surrogate Pair) 메커니즘이 답이었다. BMP 내에서 **U+D800 ~ U+DFFF** (2,048개)를 영구 예약한다. 이 범위는 실제 문자에 절대 할당되지 않는다.

- **U+D800 ~ U+DBFF** (1,024개): High Surrogate
- **U+DC00 ~ U+DFFF** (1,024개): Low Surrogate

두 개의 16비트 코드를 조합하면:

$$
1{,}024 \times 1{,}024 = 1{,}048{,}576
$$

BMP 외부의 모든 코드 포인트를 표현할 수 있다.

### 인코딩과 디코딩

코드 포인트 $C$ (단, $\text{0x10000} \le C \le \text{0x10FFFF}$)를 인코딩한다.

```text
1. C' = C - 0x10000        (20비트 값, 0x00000 ~ 0xFFFFF)
2. H = 0xD800 + (C' >> 10) (상위 10비트 → High Surrogate)
3. L = 0xDC00 + (C' & 0x3FF) (하위 10비트 → Low Surrogate)
```

예시로 "😀" (U+1F600)을 인코딩해보자.

```text
1. 0x1F600 - 0x10000 = 0x0F600
2. 0x0F600 = 0000 1111 0110 0000 0000 (20비트)
3. 상위 10비트: 00 0011 1101 = 0x03D
   하위 10비트: 10 0000 0000 = 0x200
4. High = 0xD800 + 0x03D = 0xD83D
   Low  = 0xDC00 + 0x200 = 0xDE00
5. UTF-16 코드 유닛: D83D DE00
   UTF-16LE 바이트: 3D D8 00 DE
```

디코딩 공식은 다음과 같다.

```c
if (H >= 0xD800 && H <= 0xDBFF &&
    L >= 0xDC00 && L <= 0xDFFF) {
    uint32_t codepoint = 0x10000 + ((H - 0xD800) << 10) + (L - 0xDC00);
}
```

수학적으로는 다음과 같이 표현한다.

$$
C = \text{0x10000} + (H - \text{0xD800}) \times 2^{10} + (L - \text{0xDC00})
$$

### 코드 포인트 vs 코드 유닛

여기서 중요한 개념 구분이 필요하다.

- **코드 포인트(Code Point)**: 추상적 문자 번호 (U+로 표기)
- **코드 유닛(Code Unit)**: 인코딩의 기본 저장 단위

| 인코딩 | 코드 유닛 크기 |
|--------|----------------|
| UTF-8 | 8비트 (1바이트) |
| UTF-16 | 16비트 (2바이트) |
| UTF-32 | 32비트 (4바이트) |

예시로 "A가😀"를 보자.

```text
코드 포인트: 3개
  - U+0041 (A)
  - U+AC00 (가)
  - U+1F600 (😀)

UTF-16 코드 유닛: 4개
  - 0x0041
  - 0xAC00
  - 0xD83D (high surrogate)
  - 0xDE00 (low surrogate)

바이트 (UTF-16 LE): 8개
  - 41 00 00 AC 3D D8 00 DE
```

이것이 많은 프로그래밍 실수의 원인이다.

```javascript
// JavaScript는 UTF-16 기반
const s = "😀";
s.length;        // 2 (코드 유닛)
[...s].length;   // 1 (코드 포인트)

// 위험한 인덱싱
s[0];  // '\uD83D' (high surrogate만, 의미 없음)
s[1];  // '\uDE00' (low surrogate만, 의미 없음)

// 올바른 방법
for (const char of s) {
    console.log(char);  // "😀"
}
```

## 4. UTF-8: 냅킨에서 탄생한 천재적 설계

### 1992년 9월 2일, 뉴저지의 식당

역사적 순간을 기록해 보자.

배경:
- Ken Thompson (Unix 창시자)
- Rob Pike (Go 언어 공동 개발자)
- Plan 9 운영체제에 Unicode 지원 추가 논의
- 당시 존재하던 방법: UCS-2 (16비트 고정)

Pike가 공개한 [UTF-8 History](https://doc.cat-v.org/bell_labs/utf-8_history)에 따르면, 저녁 식사 도중 Thompson이 식당의 플레이스매트에 비트 패킹 방식을 스케치했고, 며칠 만에 구현을 끝내 Plan 9 전체를 새 인코딩으로 전환했다. 이렇게 급조된 인코딩이 역사상 가장 성공적인 문자 인코딩 표준이 된다.

### 설계 목표

Thompson이 냅킨에 쓴 요구사항은 네 가지다.

1. **ASCII 완벽 하위 호환**: 기존 파일 그대로 동작
2. **자기 동기화(Self-synchronizing)**: 임의의 바이트에서 시작해도 복구 가능
3. **Byte-order independent**: Endian 문제 없음
4. **기존 C 문자열 함수 호환**: `strchr`, `strstr` 등

이 네 가지를 모두 만족시키는 것은 불가능해 보였다. 하지만 Thompson은 해냈다.

### 바이트 패턴의 마법

UTF-8의 핵심은 바이트 첫 비트 패턴으로 역할을 명확히 구분하는 것이다.

```text
0xxxxxxx                            : 1바이트 (ASCII)
110xxxxx 10xxxxxx                   : 2바이트
1110xxxx 10xxxxxx 10xxxxxx          : 3바이트
11110xxx 10xxxxxx 10xxxxxx 10xxxxxx : 4바이트
```

규칙:
- **Lead byte** (시작 바이트): 연속된 1의 개수 = 총 바이트 수
- **Continuation byte** (연속 바이트): 반드시 `10`으로 시작

이 간단한 규칙이 모든 것을 가능하게 한다.

### 자기 동기화의 위력

임의의 바이트 스트림 중간에 떨어졌다고 가정하자.

```text
... [87] [E4] [B8] [AD] [E5] [9B] [BD] [41] ...
```

어디가 문자의 시작일까?

```text
0x87 = 1000 0111
       ^^
       10으로 시작 → Continuation byte (건너뛰기)

0xE4 = 1110 0100
       ^^^^
       1110으로 시작 → 3바이트 문자의 시작
```

다음 2개 바이트 확인:

```text
0xB8 = 1011 1000 (10xxxxxx ✓)
0xAD = 1010 1101 (10xxxxxx ✓)
```

완전한 문자 형성: `[E4 B8 AD]` = "中" (U+4E2D).

최대 3바이트만 뒤로 가면 문자 경계 찾기가 보장된다.

```c
// 현재 위치에서 문자 시작점 찾기
const uint8_t* find_char_start(const uint8_t* pos) {
    for (int i = 0; i < 4; i++) {
        if ((pos[0] & 0xC0) != 0x80) {
            // 10xxxxxx가 아니면 문자 시작
            return pos;
        }
        pos--;
    }
    return NULL;  // 에러 (불가능)
}
```

왜 최대 3바이트일까? 4바이트 문자가 최대이므로, 최악의 경우 3개의 continuation byte를 건너뛰고 4번째에서 lead byte를 발견하기 때문이다.

### ASCII 완벽 호환

UTF-8의 가장 천재적인 부분이다.

```text
U+0000 ~ U+007F: 0xxxxxxx (그대로 ASCII)
```

즉:
- `'A'` = U+0041 = `0x41` (UTF-8에서도 `0x41`)
- `'0'` = U+0030 = `0x30` (UTF-8에서도 `0x30`)

효과:
1. 기존 ASCII 파일을 그대로 UTF-8로 취급 가능
2. NULL(`0x00`)은 절대 중간에 나타나지 않음 → C 문자열 호환
3. `strchr(str, 'A')`는 멀티바이트 내부와 절대 혼동 안 됨

왜냐하면 다음과 같이 범위가 완전히 분리되어 있기 때문이다.

```text
ASCII 영역:     0x00 - 0x7F (최상위 비트 = 0)
Lead byte:      0xC0 - 0xF7 (최상위 비트 = 1)
Continuation:   0x80 - 0xBF (최상위 비트 = 1)
```

### 인코딩 알고리즘

코드 포인트 범위별 인코딩은 다음과 같다.

| 범위 | UTF-8 패턴 | 비트 수 |
|------|-----------|---------|
| U+0000 - U+007F | `0xxxxxxx` | 7 |
| U+0080 - U+07FF | `110xxxxx 10xxxxxx` | 11 |
| U+0800 - U+FFFF | `1110xxxx 10xxxxxx 10xxxxxx` | 16 |
| U+10000 - U+10FFFF | `11110xxx 10xxxxxx 10xxxxxx 10xxxxxx` | 21 |

예시 1: "A" (U+0041)

```text
0x0041 = 0000 0000 0100 0001 (7비트 이하)
→ 1바이트 패턴: 0xxxxxxx
→ 0x41
```

예시 2: "가" (U+AC00)

```text
0xAC00 = 1010 1100 0000 0000 (16비트)
→ 3바이트 패턴: 1110xxxx 10xxxxxx 10xxxxxx

16비트를 4+6+6으로 분할:
  1010 | 110000 | 000000

패턴에 삽입:
  1110(1010) 10(110000) 10(000000)

결과:
  0xEA       0xB0       0x80
  1110 1010  1011 0000  1000 0000
```

예시 3: "😀" (U+1F600)

```text
0x1F600 = 0001 1111 0110 0000 0000 (21비트)
→ 4바이트 패턴: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx

21비트를 3+6+6+6으로 분할:
  000 | 011111 | 011000 | 000000

패턴에 삽입:
  11110(000) 10(011111) 10(011000) 10(000000)

결과:
  0xF0       0x9F       0x98       0x80
```

### 디코딩 구현

엄격한 UTF-8 디코더는 단순히 바이트 패턴만 보지 않는다. 다음 세 가지를 거부해야 한다.

- Overlong 인코딩: 더 짧은 표현이 있는 코드 포인트를 더 긴 형식으로 인코딩한 것 (예: U+0000을 `C0 80`으로)
- 서로게이트 영역(U+D800~U+DFFF): UTF-16 전용이며 UTF-8에는 등장할 수 없다
- U+10FFFF 초과: 유니코드 범위를 벗어난 값

이 세 검증을 시작 바이트의 엄격한 범위 체크와 두 번째 바이트의 추가 범위 체크로 처리한다.

```c
uint32_t decode_utf8(const uint8_t* bytes, size_t len, size_t* out_len) {
    if (len == 0) return INVALID;
    uint8_t b1 = bytes[0];

    // 1바이트 (0x00-0x7F)
    if (b1 <= 0x7F) {
        *out_len = 1;
        return b1;
    }

    // 2바이트 시작 바이트는 0xC2-0xDF만 허용 (0xC0-0xC1은 overlong)
    if (b1 >= 0xC2 && b1 <= 0xDF) {
        if (len < 2) return INVALID;
        uint8_t b2 = bytes[1];
        if ((b2 & 0xC0) != 0x80) return INVALID;
        *out_len = 2;
        return ((b1 & 0x1F) << 6) | (b2 & 0x3F);  // 0x80~0x7FF 보장
    }

    // 3바이트 시작 바이트는 0xE0-0xEF
    if (b1 >= 0xE0 && b1 <= 0xEF) {
        if (len < 3) return INVALID;
        uint8_t b2 = bytes[1], b3 = bytes[2];
        if ((b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) return INVALID;

        // 0xE0: 두 번째 바이트가 0xA0-0xBF여야 한다 (overlong 방지)
        if (b1 == 0xE0 && b2 < 0xA0) return INVALID;
        // 0xED: 두 번째 바이트가 0x80-0x9F여야 한다 (서로게이트 차단)
        if (b1 == 0xED && b2 > 0x9F) return INVALID;

        *out_len = 3;
        return ((b1 & 0x0F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);
    }

    // 4바이트 시작 바이트는 0xF0-0xF4만 허용 (0xF5-0xF7은 U+10FFFF 초과)
    if (b1 >= 0xF0 && b1 <= 0xF4) {
        if (len < 4) return INVALID;
        uint8_t b2 = bytes[1], b3 = bytes[2], b4 = bytes[3];
        if ((b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80 || (b4 & 0xC0) != 0x80)
            return INVALID;

        // 0xF0: 두 번째 바이트가 0x90-0xBF여야 한다 (overlong 방지)
        if (b1 == 0xF0 && b2 < 0x90) return INVALID;
        // 0xF4: 두 번째 바이트가 0x80-0x8F여야 한다 (U+10FFFF 초과 차단)
        if (b1 == 0xF4 && b2 > 0x8F) return INVALID;

        *out_len = 4;
        return ((b1 & 0x07) << 18) | ((b2 & 0x3F) << 12) |
               ((b3 & 0x3F) << 6) | (b4 & 0x3F);
    }

    return INVALID;  // 0x80-0xBF (단독 continuation), 0xC0-0xC1, 0xF5-0xFF
}
```

### 비트 마스킹의 수학

왜 `0xE0`, `0xF0`, `0x1F` 같은 마스크를 쓸까?

2바이트 경우를 분석한다.

```text
패턴: 110xxxxx
마스크: 111 00000 = 0xE0

비교: (byte & 0xE0) == 0xC0
      ^^^^^^^^         ^^^^
      상위 3비트만      110으로 시작해야 함

데이터 추출: byte & 0x1F
            마스크: 000 11111 = 0x1F
                    하위 5비트만 추출
```

3바이트:

```text
패턴: 1110xxxx
마스크: 1111 0000 = 0xF0

비교: (byte & 0xF0) == 0xE0

데이터: byte & 0x0F (하위 4비트)
```

Continuation byte:

```text
패턴: 10xxxxxx
마스크: 11 000000 = 0xC0

비교: (byte & 0xC0) == 0x80

데이터: byte & 0x3F (하위 6비트)
         마스크: 00 111111 = 0x3F
```

## 5. UTF-16과 16비트 고정 길이의 환상

### 결정적 타이밍

타임라인을 정리하면 다음과 같다.

```text
1988: Windows NT 개발 시작
1991: Unicode 1.0 발표 (UCS-2, 16비트 고정 길이)
1992: UTF-8 발명 (Ken Thompson)
1993: Windows NT 3.1 출시
1996: UTF-16 서로게이트 페어 도입
```

Microsoft는 NT 개발 초기에 Unicode를 도입하기로 결정했다. 당시 선택지는 UCS-2뿐이었다. UTF-8은 아직 존재하지 않았다.

### 16비트 고정 길이의 환상

당시 UCS-2는 매력적이었다.

```c
wchar_t str[100];  // 정확히 100글자 저장 가능
wchar_t c = str[i]; // i번째 문자 O(1) 접근
size_t bytes = count * sizeof(wchar_t); // 간단한 메모리 계산
```

고정 길이는 많은 것을 단순화한다.
- 버퍼 크기 계산 단순
- 인덱싱이 상수 시간
- 역방향 순회 간단

하지만 1996년 서로게이트 페어가 도입되면서 모든 것이 무너졌다.

### UTF-16: "양쪽의 최악"

[utf8everywhere.org](https://utf8everywhere.org/)에서는 UTF-16을 "worst of both worlds"라고 비판한다.

#### 고정 길이도 아니고

```cpp
wstring str = L"😀🎉🌟";

// 틀린 방법 (3글자로 착각)
for (size_t i = 0; i < str.length(); i++) {
    wchar_t c = str[i];  // 서로게이트 절반
}

// UTF-16 code unit이 uint16_t라고 가정한 검증 경로
for (size_t i = 0; i < units.size(); ) {
    uint16_t first = units[i++];
    uint32_t cp = 0;

    if (0xD800 <= first && first <= 0xDBFF) {
        if (i == units.size())
            return DecodeError::TruncatedSurrogate;

        uint16_t second = units[i++];
        if (second < 0xDC00 || second > 0xDFFF)
            return DecodeError::InvalidLowSurrogate;

        cp = 0x10000u
           + ((static_cast<uint32_t>(first) - 0xD800u) << 10)
           +  (static_cast<uint32_t>(second) - 0xDC00u);
    } else if (0xDC00 <= first && first <= 0xDFFF) {
        return DecodeError::UnpairedLowSurrogate;
    } else {
        cp = first;
    }

    consume_scalar(cp);
}
```

`wchar_t`는 Windows에서 보통 16비트지만 Unix 계열에서는 보통 32비트이므로 UTF-16 code unit 타입으로 이식 가능하게 사용할 수 없다.

#### 공간 효율적이지도 않고

| 텍스트 | UTF-8 | UTF-16 |
|--------|------:|-------:|
| 영어 "Hello" | 5바이트 | 10바이트 |
| 한글 "안녕" | 6바이트 (각 3바이트) | 4바이트 (각 2바이트) |
| 이모지 "😀" | 4바이트 | 4바이트 (서로게이트 페어) |

ASCII만 놓으면 UTF-16은 UTF-8의 두 배 바이트를 사용한다. 그러나 U+0080 이상 문자의 UTF-8 길이는 2~4바이트이므로 자연어 전체를 “서양권 2배”로 일반화할 수 없다. 실제 크기는 code point 분포, normalization과 BOM 포함 여부로 계산한다.

#### Endian 문제

```text
Little Endian (Windows 기본):
0x41 0x00 = U+0041 ('A')

Big Endian (네트워크):
0x00 0x41 = U+0041 ('A')
```

네트워크 protocol이 UTF-16의 byte order를 고정하거나 charset metadata로 전달하면 BOM은 필요하지 않다. 자체 기술 형식에서는 byte order를 schema에 명시하는 편이 안전하며, BOM에 의존할지는 해당 protocol 규약이 결정한다.

#### ASCII 호환성 파괴

```text
UTF-8에서 "test.txt" 검색:
74 65 73 74 2E 74 78 74
→ strstr() 그대로 사용 가능

UTF-16에서 "test.txt":
74 00 65 00 73 00 74 00 2E 00 74 00 78 00 74 00
→ NULL(0x00) 포함, C 문자열 함수 불가
```

### Windows API의 A/W 이중성

Windows API가 `CreateFileA`/`CreateFileW` 두 버전으로 존재하는 이유, MBCS 경계 문제, Locale/NLS, TCHAR 마이그레이션은 모두 "바이트열과 문자열의 경계를 어디에 둘 것인가"라는 같은 문제의 플랫폼별 표현이다.

```c
// ANSI 버전 (코드 페이지 의존)
BOOL CreateFileA(LPCSTR lpFileName, ...);

// Wide 버전 (UTF-16)
BOOL CreateFileW(LPCWSTR lpFileName, ...);
```

A 함수는 단순히 래퍼다. 실제 구현은 W 함수에만 있고, A 함수는 내부적으로 `MultiByteToWideChar`를 호출한 후 W 함수로 위임한다.

권장 패턴은 다음과 같다.

```cpp
// 내부적으로 UTF-8 사용
std::string filename_utf8 = "한글파일.txt";

// Windows API 호출 시에만 변환
std::wstring filename_utf16 = utf8_to_utf16(filename_utf8);
HANDLE h = CreateFileW(filename_utf16.c_str(), ...);

// 변환 함수
std::wstring utf8_to_utf16(const std::string& utf8) {
    int len = MultiByteToWideChar(CP_UTF8, 0,
                                  utf8.c_str(), -1,
                                  nullptr, 0);
    std::wstring result(len, 0);
    MultiByteToWideChar(CP_UTF8, 0,
                       utf8.c_str(), -1,
                       &result[0], len);
    return result;
}
```

## 6. 멀티바이트 문자 경계의 함정

### 바이트 길이 vs 문자 길이

근본적인 불일치가 있다.

```c
const char* str = "A가😀";

strlen(str);              // 8 (바이트)
count_codepoints(str);    // 3 (코드 포인트)

// 바이트 분해:
// 'A':  41              (1바이트)
// '가': EA B0 80        (3바이트)
// '😀': F0 9F 98 80     (4바이트)
```

인덱싱 함정:

```c
char c = str[3];  // 0xB0
// 이것은 '가'의 두 번째 바이트일 뿐, 의미 없음
```

올바른 순회:

```c
const char* p = str;
const char* end = str + strlen(str);
while (*p) {
    size_t len;
    uint32_t cp = decode_utf8((const uint8_t*)p, (size_t)(end - p), &len);
    if (cp == INVALID) break;
    // cp 사용
    p += len;
}
```

### 안전한 문자열 자르기

문제 시나리오: 데이터베이스 VARCHAR(100) 제한.

```c
char text[] = "안녕하세요 여러분 반갑습니다...";  // 300바이트

// 나쁜 방법
char truncated[101];
strncpy(truncated, text, 100);
truncated[100] = '\0';

// 문제: 마지막 문자가 잘릴 수 있음
// ...97 98 99 [EA B0 80]
//             ^^
// '가'의 첫 바이트만 포함 → 잘못된 UTF-8
```

올바른 구현은 다음과 같다.

```c
size_t utf8_safe_truncate(const char* src, char* dst, size_t max_bytes) {
    size_t written = 0;
    size_t consumed = 0;
    const size_t src_len = strlen(src);

    while (consumed < src_len && written < max_bytes) {
        size_t char_len = 0;
        uint32_t code_point = decode_utf8(
            (const uint8_t*)src + consumed,
            src_len - consumed,
            &char_len);
        if (code_point == INVALID) {
            break;  // 첫 잘못된 시퀀스 앞에서 중단
        }

        // 전체 문자가 들어가는지 확인
        if (written + char_len > max_bytes)
            break;  // 현재 문자를 포함하지 않고 종료

        // 전체 복사
        memcpy(dst + written, src + consumed, char_len);
        written += char_len;
        consumed += char_len;
    }

    dst[written] = '\0';
    return written;
}
```

여기서 `max_bytes`는 null terminator를 제외한 payload 한도이며 `dst` 용량은 최소 `max_bytes + 1`이어야 한다. 이 구현은 앞 절의 엄격한 `decode_utf8`을 사용하므로 continuation byte, overlong 인코딩, surrogate와 범위 초과 코드 포인트까지 검사한다. 출력은 100바이트 제한에서도 문자 경계를 지키며, 잘못된 입력을 만나면 그 직전에서 멈춘다. 오류와 정상 절단을 구분해야 하는 API라면 반환값에 별도 오류 상태를 추가한다.

### UTF-8 Validation과 보안

Overlong 인코딩 공격이 있다. 정상적으로 `'/'` (U+002F)는 1바이트로 인코딩된다(`0x2F`). 하지만 악의적으로 2바이트나 3바이트로 인코딩할 수 있다.

```text
2바이트: C0 AF  (규격 위반)
3바이트: E0 80 AF
4바이트: F0 80 80 AF
```

공격 시나리오:

```c
// 보안 검사
if (strchr(path, '/') != NULL) {
    reject("Path traversal attack");
}

// 하지만 0xC0 0xAF는 바이트 수준에서 0x2F가 아님
// → 검사 통과
// → 디코더가 U+002F로 해석
// → 경로 탐색 공격 성공
```

해결책은 Overlong 명시적 거부다.

```c
bool is_valid_utf8(const uint8_t* data, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint8_t b1 = data[i];

        // ASCII
        if (b1 <= 0x7F) {
            ++i;
            continue;
        }

        // 2바이트
        if (b1 >= 0xC2 && b1 <= 0xDF) {
            if (i + 1 >= len) return false;
            uint8_t b2 = data[i + 1];
            if ((b2 & 0xC0) != 0x80) return false;

            uint32_t cp = ((b1 & 0x1F) << 6) | (b2 & 0x3F);

            // Overlong 체크
            if (cp < 0x80) return false;  // 1바이트로 가능

            i += 2;
            continue;
        }

        // 3바이트
        if (b1 >= 0xE0 && b1 <= 0xEF) {
            if (i + 2 >= len) return false;
            uint8_t b2 = data[i + 1];
            uint8_t b3 = data[i + 2];
            if ((b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80)
                return false;

            uint32_t cp = ((b1 & 0x0F) << 12) |
                         ((b2 & 0x3F) << 6) |
                         (b3 & 0x3F);

            // Overlong 체크
            if (cp < 0x800) return false;  // 2바이트로 가능

            // 서로게이트 영역 거부
            if (cp >= 0xD800 && cp <= 0xDFFF) return false;

            i += 3;
            continue;
        }

        // 4바이트
        if (b1 >= 0xF0 && b1 <= 0xF4) {
            if (i + 3 >= len) return false;
            uint8_t b2 = data[i + 1];
            uint8_t b3 = data[i + 2];
            uint8_t b4 = data[i + 3];
            if ((b2 & 0xC0) != 0x80 ||
                (b3 & 0xC0) != 0x80 ||
                (b4 & 0xC0) != 0x80)
                return false;

            uint32_t cp = ((b1 & 0x07) << 18) |
                         ((b2 & 0x3F) << 12) |
                         ((b3 & 0x3F) << 6) |
                         (b4 & 0x3F);

            // Overlong 체크
            if (cp < 0x10000) return false;  // 3바이트로 가능

            // 최대 코드 포인트 체크
            if (cp > 0x10FFFF) return false;

            i += 4;
            continue;
        }

        return false;  // 잘못된 시작 바이트
    }
    return true;
}
```

## 7. BOM (Byte Order Mark)

### BOM의 목적

Byte Order Mark는 파일 시작에 삽입되는 특수 바이트 시퀀스다.

원래 목적은 UTF-16/32의 엔디안 구분이다.

| 인코딩 | BOM 바이트 | 의미 |
|--------|-----------|------|
| UTF-8 | EF BB BF | U+FEFF의 UTF-8 인코딩 |
| UTF-16 LE | FF FE | Little Endian |
| UTF-16 BE | FE FF | Big Endian |
| UTF-32 LE | FF FE 00 00 | Little Endian |
| UTF-32 BE | 00 00 FE FF | Big Endian |

### U+FEFF의 특수성

ZERO WIDTH NO-BREAK SPACE:
- 코드 포인트: U+FEFF
- 원래 의미: 보이지 않는 줄바꿈 금지 문자
- BOM 용도: 파일 시작에만 사용

U+FFFE:
- 영구 noncharacter로 예약
- 유효한 Unicode scalar value이므로 UTF-8/16/32로 인코딩할 수 있음
- open interchange에서 문자로 교환하기 위한 값은 아니지만, 디코더가 서로게이트처럼 무조건 거부할 값은 아님
- 역바이트 순서에서 BOM과 구별되는 값이라 엔디안 오류 감지에도 쓰임

엔디안 감지 메커니즘은 다음과 같다.

```text
파일 첫 2바이트가 [FF FE]:

Little Endian으로 읽으면:
  0xFEFF (올바른 BOM ✓)

Big Endian으로 읽으면:
  0xFFFE (non-character, 잘못된 해석 ✗)

→ LE임을 확신
```

반대도 마찬가지다.

```text
파일 첫 2바이트가 [FE FF]:

Big Endian으로 읽으면:
  0xFEFF (올바른 BOM ✓)

Little Endian으로 읽으면:
  0xFFFE (non-character ✗)

→ BE임을 확신
```

### UTF-8 BOM 논쟁

UTF-8은 바이트 순서가 없으므로 BOM이 기술적으로 불필요하다.

찬성 입장:
- 텍스트 에디터가 인코딩 자동 감지
- UTF-8 vs 레거시 ANSI 구분
- Windows 메모장 등 일부 도구에서 요구

반대 입장 (Unix/Linux 진영):

1. **Shebang 충돌**

```bash
#!/usr/bin/env python3
print("Hello")
```

BOM 포함 시:

```text
[EF BB BF]#!/usr/bin/env python3
```

쉘은 shebang을 인식하지 못한다.

2. **HTTP 문제**

```html
<!DOCTYPE html>
<html>
```

BOM 포함 시 일부 브라우저에서 "?" 표시.

3. **파일 비교**

```bash
diff file1.txt file2.txt
# 내용은 같은데 BOM 유무로 다르다고 판정
```

권장사항: UTF-8 파일은 BOM 없이 저장(Unix 표준). 예외는 Windows 메모장 전용 파일이나 명시적 요구 도구다.

BOM 처리 코드:

```c
bool has_utf8_bom(const uint8_t* data) {
    return data[0] == 0xEF &&
           data[1] == 0xBB &&
           data[2] == 0xBF;
}

const uint8_t* skip_bom(const uint8_t* data, size_t* len) {
    if (*len >= 3 && has_utf8_bom(data)) {
        *len -= 3;
        return data + 3;
    }
    return data;
}
```

### 인코딩 자동 감지

```c
typedef enum {
    ENC_UTF8,
    ENC_UTF16_LE,
    ENC_UTF16_BE,
    ENC_UTF32_LE,
    ENC_UTF32_BE,
    ENC_UNKNOWN
} Encoding;

Encoding detect_encoding(const uint8_t* data, size_t len) {
    // UTF-32 BOM (4바이트) 먼저
    if (len >= 4) {
        if (memcmp(data, "\xFF\xFE\x00\x00", 4) == 0)
            return ENC_UTF32_LE;
        if (memcmp(data, "\x00\x00\xFE\xFF", 4) == 0)
            return ENC_UTF32_BE;
    }

    // UTF-8 BOM (3바이트)
    if (len >= 3) {
        if (memcmp(data, "\xEF\xBB\xBF", 3) == 0)
            return ENC_UTF8;
    }

    // UTF-16 BOM (2바이트)
    if (len >= 2) {
        if (memcmp(data, "\xFF\xFE", 2) == 0)
            return ENC_UTF16_LE;
        if (memcmp(data, "\xFE\xFF", 2) == 0)
            return ENC_UTF16_BE;
    }

    return ENC_UNKNOWN;
}
```

## 8. 실전 패턴과 베스트 프랙티스

### 크로스 플랫폼 아키텍처

권장 접근은 내부 UTF-8 통일이다.

- 모든 문자열 저장
- 네트워크 전송
- 파일 저장
- 로그 출력

경계에서만 변환한다.
- Windows API: UTF-8 ↔ UTF-16
- 외부 라이브러리 요구사항

변환 레이어 예시:

```cpp
class String {
private:
    std::string utf8_data;  // 내부는 항상 UTF-8

public:
    String(const char* utf8) : utf8_data(utf8) {}

    // UTF-8 접근
    const char* c_str() const { return utf8_data.c_str(); }

#ifdef _WIN32
    // Windows API용 변환
    std::wstring to_wstring() const {
        return utf8_to_utf16(utf8_data);
    }

    // Windows API에서 생성
    static String from_wstring(const std::wstring& ws) {
        return String(utf16_to_utf8(ws).c_str());
    }
#endif
};

// 사용
String filename("한글파일.txt");

#ifdef _WIN32
HANDLE h = CreateFileW(filename.to_wstring().c_str(), ...);
#else
int fd = open(filename.c_str(), ...);
#endif
```

### 문자열 길이의 세 가지 의미

```cpp
const char* str = "A가😀";

// 1. 바이트 길이 (저장/전송)
size_t bytes = strlen(str);  // 8

// 2. 코드 포인트 개수
size_t count_codepoints(const char* s) {
    size_t count = 0;
    while (*s) {
        if ((*s & 0xC0) != 0x80) count++;
        s++;
    }
    return count;
}
// → 3

// 3. 그래핌 클러스터 (사용자가 보는 "글자")
// "👨‍👩‍👧‍👦" (가족) = 1 그래핌, 7 코드 포인트, 25바이트
// ICU 라이브러리 필요
```

### 데이터베이스 VARCHAR 처리

```sql
-- MySQL (utf8mb4)
CREATE TABLE users (
    name VARCHAR(100)  -- 100 문자 (최대 400바이트)
);

-- PostgreSQL
name VARCHAR(100)  -- 100 문자

-- SQL Server
name VARCHAR(100)    -- 100 바이트 (ANSI)
name NVARCHAR(100)   -- 100 UTF-16 코드 유닛
```

안전한 삽입:

```cpp
std::string truncate_to_chars(const std::string& utf8, size_t max_chars) {
    size_t count = 0;
    size_t pos = 0;

    while (pos < utf8.length() && count < max_chars) {
        uint8_t b = utf8[pos];

        if ((b & 0x80) == 0)         pos += 1;
        else if ((b & 0xE0) == 0xC0) pos += 2;
        else if ((b & 0xF0) == 0xE0) pos += 3;
        else if ((b & 0xF8) == 0xF0) pos += 4;
        else break;

        count++;
    }

    return utf8.substr(0, pos);
}

// 사용
std::string name = "매우 긴 이름...";
std::string safe_name = truncate_to_chars(name, 100);
// SQL에 안전하게 삽입 가능
```

### 게임 엔진에서의 고려사항

폰트 렌더링:

```cpp
// 코드 포인트 단위로 순회
void render_text(const char* utf8, float x, float y) {
    const char* p = utf8;
    const char* end = utf8 + strlen(utf8);
    while (*p) {
        size_t len;
        uint32_t codepoint = decode_utf8((const uint8_t*)p,
                                         static_cast<size_t>(end - p), &len);
        if (codepoint == INVALID) break;

        Glyph* glyph = font->get_glyph(codepoint);
        render_glyph(glyph, x, y);

        x += glyph->advance_x;
        p += len;
    }
}
```

텍스트 입력:

```cpp
class TextInput {
private:
    std::string utf8_buffer;
    size_t cursor_pos;  // 바이트 위치

public:
    void insert_char(uint32_t codepoint) {
        char utf8[5];
        size_t len = encode_utf8(codepoint, (uint8_t*)utf8);
        utf8[len] = '\0';

        utf8_buffer.insert(cursor_pos, utf8, len);
        cursor_pos += len;
    }

    void move_cursor_left() {
        if (cursor_pos == 0) return;

        // UTF-8 문자 시작점 찾기
        do {
            cursor_pos--;
        } while (cursor_pos > 0 &&
                (utf8_buffer[cursor_pos] & 0xC0) == 0x80);
    }
};
```

## 9. 성능 최적화

### SIMD를 활용한 ASCII 검증

현대 프로세서는 16바이트를 동시에 처리할 수 있다.

```c
#include <emmintrin.h>  // SSE2

bool is_ascii(const char* str, size_t len) {
    size_t i = 0;

    // 16바이트씩 처리
    for (; i + 16 <= len; i += 16) {
        __m128i chunk = _mm_loadu_si128((__m128i*)(str + i));

        // 최상위 비트 추출
        int mask = _mm_movemask_epi8(chunk);

        if (mask != 0) return false;  // 하나라도 0x80 이상
    }

    // 나머지 처리
    for (; i < len; i++) {
        if ((uint8_t)str[i] >= 0x80) return false;
    }

    return true;
}
```

### 코드 포인트 개수 계산 최적화

```c
size_t count_codepoints_fast(const char* str, size_t len) {
    size_t count = 0;

    for (size_t i = 0; i < len; i++) {
        // Continuation byte가 아닌 것만 카운트
        // 0x80 = 1000 0000
        // 0xC0 = 1100 0000
        // Continuation = 10xxxxxx
        count += ((str[i] & 0xC0) != 0x80);
    }

    return count;
}
```

이 한 줄의 비트 연산이 전체 문자 개수를 센다. 왜냐하면:
- ASCII (0xxxxxxx): `& 0xC0` = 0x00 ≠ 0x80 → count++
- Lead (110xxxxx, 1110xxxx, 11110xxx): ≠ 0x80 → count++
- Continuation (10xxxxxx): `& 0xC0` = 0x80 → count 유지

### 문자열 비교

```c
int utf8_strcmp(const char* s1, const char* s2) {
    // UTF-8은 바이트 순서가 코드 포인트 순서와 동일
    return strcmp(s1, s2);
}

// 증명:
// U+0041 'A' = 0x41
// U+0042 'B' = 0x42
// U+AC00 '가' = 0xEA 0xB0 0x80
// U+AC01 '각' = 0xEA 0xB0 0x81
//
// 바이트 비교 = 코드 포인트 비교
```

이것이 UTF-8의 또 다른 천재성이다. 정렬 순서가 바이트 순서와 일치한다.

### 빠른 참조

UTF-8 바이트 패턴:

```text
0xxxxxxx                            : 1바이트
110xxxxx 10xxxxxx                   : 2바이트
1110xxxx 10xxxxxx 10xxxxxx          : 3바이트
11110xxx 10xxxxxx 10xxxxxx 10xxxxxx : 4바이트
```

시작 바이트 인식 (엄격한 범위):

```text
0x00-0x7F: 1바이트
0xC2-0xDF: 2바이트 시작 (0xC0, 0xC1은 overlong → 무효)
0xE0-0xEF: 3바이트 시작
              0xE0의 두 번째 바이트는 0xA0-0xBF여야 함 (overlong 방지)
              0xED의 두 번째 바이트는 0x80-0x9F여야 함 (서로게이트 차단)
0xF0-0xF4: 4바이트 시작 (0xF5-0xF7은 U+10FFFF 초과 → 무효)
              0xF0의 두 번째 바이트는 0x90-0xBF여야 함 (overlong 방지)
              0xF4의 두 번째 바이트는 0x80-0x8F여야 함 (상한 초과 방지)
0x80-0xBF: Continuation (단독 출현 시 에러)
```

BOM:

```text
UTF-8:     EF BB BF
UTF-16 LE: FF FE
UTF-16 BE: FE FF
```

UTF 인코딩에서 거부할 값과 noncharacter:

```text
U+D800 - U+DFFF: Unicode scalar value가 아니므로 UTF-8/UTF-32 scalar 입력에서 거부
U+FFFE, U+FFFF: 유효한 scalar value인 noncharacter; 문자 교환용으로 지정되지 않음
```


---

## 결론

문자 인코딩의 역사는 기술적 결정이 얼마나 오래 영향을 미치는지 보여준다.

- **1963년 ASCII**: 7비트 결정이 60년 넘게 유지
- **1991년 UCS-2**: "16비트면 충분" 가정이 전 세계 시스템에 기술 부채
- **1992년 UTF-8**: 냅킨 스케치가 인터넷 표준으로

현재의 상황은 다음과 같다.
- **웹**: UTF-8이 98% 이상 점유
- **Windows**: UTF-16 레거시에 묶임
- **데이터베이스**: UTF-8 기본화
- **프로그래밍 언어**: 외부 UTF-8 지원은 널리 퍼졌지만 내부 표현은 다르다. Rust `str`과 Go 문자열은 바이트 기반이고 보통 UTF-8을 담지만, Python 3 `str`은 PEP 393의 문자열별 유연한 1·2·4바이트 코드 유닛 표현을 사용한다.

게임 엔진이나 시스템 소프트웨어를 개발한다면, 문자 인코딩은 피할 수 없는 주제다. 표면적으로는 간단해 보이지만, 그 아래에는 수십 년의 역사와 엔지니어링 트레이드오프가 층층이 쌓여 있다.

Ken Thompson의 냅킨 스케치가 증명했듯이, 좋은 설계는 시간의 시험을 견딘다. UTF-8의 자기 동기화, ASCII 호환성, 엔디안 독립성은 우아한 수학적 설계가 실용적 가치를 창출하는 완벽한 예시다.

Windows 환경의 MBCS 경계 문제, 콘솔 인코딩, Locale/NLS, TCHAR 마이그레이션, 안전한 문자열 함수는 서로 별개의 예외가 아니다. 외부 바이트열을 내부 Unicode 표현으로 변환하는 경계를 일관되게 두지 않았을 때 나타나는 같은 종류의 실패다.

## 더 읽을거리

- [Unicode Standard](https://www.unicode.org/versions/latest/)
- [UTF-8, UTF-16, UTF-32 FAQ](https://www.unicode.org/faq/utf_bom.html)
- [RFC 3629 - UTF-8](https://www.rfc-editor.org/rfc/rfc3629)
- [Rob Pike, "UTF-8 History"](https://doc.cat-v.org/bell_labs/utf-8_history) - 1992년 9월 2일 설계 과정
- [Joel Spolsky, "The Absolute Minimum Every Software Developer Must Know About Unicode"](https://www.joelonsoftware.com/2003/10/08/the-absolute-minimum-every-software-developer-absolutely-positively-must-know-about-unicode-and-character-sets-no-excuses/)
- [UTF-8 Everywhere Manifesto](https://utf8everywhere.org/) - UTF-16 비판과 UTF-8 권장 이유
- [Microsoft: Windows Unicode](https://learn.microsoft.com/en-us/windows/win32/intl/unicode)
- [Microsoft: Code Pages](https://learn.microsoft.com/en-us/windows/win32/intl/code-pages)
- [ICU - International Components for Unicode](https://icu.unicode.org/)
- [utf8proc](https://juliastrings.github.io/utf8proc/)
- [simdutf](https://github.com/simdutf/simdutf) - SIMD 최적화
- [Unicode Security (TR36)](https://www.unicode.org/reports/tr36/)
- [W3C UTF-8 Test](https://www.w3.org/2001/06/utf-8-test/)
- [UTF-8 Stress Test](https://www.cl.cam.ac.uk/~mgk25/ucs/examples/UTF-8-test.txt)
