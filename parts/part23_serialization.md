## 들어가며

프로그램의 메모리 구조는 포인터로 연결된 객체, 정렬된 배열, 트리, 그래프로 구성된다. 이 구조는 모두 휘발성이다. 프로세스가 종료되면 사라지고, 네트워크를 통해 전송할 수도 없다. 직렬화(Serialization)는 이 메모리 상태를 바이트 스트림으로 변환해 디스크나 네트워크 같은 외부 매체로 옮길 수 있는 형태로 만드는 작업이다.

게임 엔진과 고성능 시스템은 JSON이나 XML 같은 사람 친화적 포맷 대신 자체 바이너리 포맷을 만들어 사용하는 경우가 많다. Protocol Buffers와 FlatBuffers는 같은 직렬화 분야에 속하면서도 설계 철학이 다르고, Cap'n Proto가 내세우는 zero-copy 직렬화는 또 다른 접근이다. 이 차이는 데이터 밀도, 파싱 비용, 버전 호환성, 메모리 접근 패턴이라는 네 축에서 결정된다.

이 글에서는 텍스트와 바이너리 직렬화의 본질적 차이, 엔디안과 정렬 문제, 스키마 진화, zero-copy 설계, 그리고 실전 시스템이 어떤 트레이드오프 위에서 만들어졌는지를 다룬다.


---

## 목차
1. [직렬화란 무엇인가: 메모리에서 바이트로](#1-직렬화란-무엇인가-메모리에서-바이트로)
2. [텍스트 vs 바이너리: 트레이드오프](#2-텍스트-vs-바이너리-트레이드오프)
3. [엔디안과 정렬: 이식성의 적](#3-엔디안과-정렬-이식성의-적)
4. [스키마 기반: Protocol Buffers, FlatBuffers](#4-스키마-기반-protocol-buffers-flatbuffers)
5. [Zero-Copy 직렬화: Cap'n Proto의 혁신](#5-zero-copy-직렬화-capn-proto의-혁신)
6. [버전 호환성: 필드 추가와 제거](#6-버전-호환성-필드-추가와-제거)
7. [게임의 선택: 왜 자체 포맷인가](#7-게임의-선택-왜-자체-포맷인가)
8. [실전: 네트워크 패킷과 세이브 파일](#8-실전-네트워크-패킷과-세이브-파일)


---

## 1. 직렬화란 무엇인가: 메모리에서 바이트로

### 문제의 본질

메모리에 다음과 같은 구조체가 있다고 하자:

```cpp
struct Player {
    int32_t id;
    float x, y, z;
    std::string name;
};

Player p = {42, 10.5f, 20.3f, 5.0f, "Alice"};
```

메모리 레이아웃 (64비트 시스템):

```text
[p]
+--------+--------+--------+--------+
| id     | x      | y      | z      |  16 bytes
| 0x002A | 0x4128 | 0x41A2 | 0x40A0 |
+--------+--------+--------+--------+
| name.data*      | name.length     |  std::string 객체: 24~32 bytes
| 0x7FFF12345678  | 5               |
+--------+--------+--------+--------+
              ↓ 힙에 할당
        [A][l][i][c][e][\0]
```

이 데이터를 파일에 저장하거나 네트워크로 전송하려면 어떻게 해야 할까?

### 순진한 접근: memcpy

```cpp
// 나쁜 예
FILE* f = fopen("player.dat", "wb");
fwrite(&p, sizeof(Player), 1, f);
fclose(f);
```

이 코드는 세 가지 치명적 문제를 가진다:

**1. 포인터 저장**: `name.data`는 현재 프로세스의 주소 `0x7FFF12345678`을 가리킨다. 파일에서 읽어올 때 이 주소는 의미 없다.

**2. 힙 데이터 누락**: 이름이 SSO(Small String Optimization) 한계를 넘으면 문자열 본문은 힙에 있으므로, 객체 바이트만 복사하는 이 코드는 그 데이터를 저장하지 않는다. 한계는 구현마다 달라 libstdc++와 MSVC STL은 15자, libc++는 22자까지 객체 안에 담는다 — `"Alice"`처럼 짧은 이름은 힙 할당 자체가 없어 위 그림의 힙 블록도 생기지 않는다.

**3. 패딩 포함**: 컴파일러가 삽입한 패딩도 그대로 저장된다.

### 제대로 된 직렬화

```cpp
void serialize_player(const Player& p, std::vector<uint8_t>& out) {
    if (p.name.size() > UINT32_MAX) throw std::length_error("name too long");
    // ID (4바이트)
    uint32_t id = p.id;
    out.insert(out.end(), (uint8_t*)&id, (uint8_t*)&id + 4);

    // 좌표 (12바이트)
    out.insert(out.end(), (uint8_t*)&p.x, (uint8_t*)&p.x + 4);
    out.insert(out.end(), (uint8_t*)&p.y, (uint8_t*)&p.y + 4);
    out.insert(out.end(), (uint8_t*)&p.z, (uint8_t*)&p.z + 4);

    // 문자열 (길이 + 데이터)
    uint32_t len = p.name.length();
    out.insert(out.end(), (uint8_t*)&len, (uint8_t*)&len + 4);
    out.insert(out.end(), p.name.begin(), p.name.end());
}
```

역직렬화:

```cpp
Player deserialize_player(const uint8_t* data, size_t& offset) {
    Player p;

    // ID
    memcpy(&p.id, data + offset, 4);
    offset += 4;

    // 좌표
    memcpy(&p.x, data + offset, 4); offset += 4;
    memcpy(&p.y, data + offset, 4); offset += 4;
    memcpy(&p.z, data + offset, 4); offset += 4;

    // 문자열
    uint32_t len;
    memcpy(&len, data + offset, 4); offset += 4;
    p.name.assign((char*)(data + offset), len);
    offset += len;

    return p;
}
```

이제 같은 표현을 사용하는 호스트 사이에서는 필드를 복원할 수 있다. 하지만 코드가 길고 실수하기 쉽다. 가변 길이 데이터를 다룰 때마다 등장하는 length-prefix 패턴이 여기서도 그대로 나타난다.

이 예는 IEEE 754 binary32 float와 같은 byte order를 쓰는 호스트를 가정하고, 호출자가 `data`가 필요한 길이 이상이며 각 필드가 유효함을 미리 검증했다는 전제에 기댄다. 프로덕션 decoder는 버퍼 길이(`std::span` 등)를 함께 받아 각 `memcpy` 전에 `offset <= size`와 `n <= size - offset`을 검사하고, 잘린 입력·큰 `len`·offset 합 overflow를 오류로 반환하며 소비한 offset을 되돌리는 정책을 정한다. float32와 byte order도 wire format에 명시한다.

## 2. 텍스트 vs 바이너리: 트레이드오프

### JSON: 인간 친화적 접근

```json
{
  "id": 42,
  "x": 10.5,
  "y": 20.3,
  "z": 5.0,
  "name": "Alice"
}
```

**장점:**
- 사람이 읽을 수 있음 (디버깅 용이)
- 스키마 없이도 자기 설명적
- 언어/플랫폼 독립적
- 웹 표준 (JavaScript 네이티브)

**단점:**
- 크기: 위 2-space/LF 예제는 71바이트, 예시 바이너리는 25바이트. 텍스트 formatting에 따라 달라짐
- 파싱 비용: 문자열을 숫자로 변환 필요
- 메모리 할당: DOM 파서는 키 이름마다 문자열 객체를 만들지만, SAX·on-demand 파서는 그렇지 않다
- 타입 모호성: 정수 vs 실수 구분 없음

### 바이너리: 성능 최적화

```text
바이트 오프셋:
0x00: 2A 00 00 00           # id = 42
0x04: 00 00 28 41           # x = 10.5
0x08: 66 66 A2 41           # y = 20.3 (IEEE 754 binary32, little-endian)
0x0C: 00 00 A0 40           # z = 5.0
0x10: 05 00 00 00           # name length = 5
0x14: 41 6C 69 63 65        # "Alice"
```

총 25바이트다. 위의 2-space indent와 LF를 사용한 JSON은 71바이트이므로 이 예에서는 약 35%지만, JSON 크기는 공백·개행·숫자 표기와 escape 방식에 따라 달라진다. 비율을 포맷의 고정 속성으로 취급하지 않는다.

**장점:**
- 컴팩트: 메타데이터 최소화
- 단순한 파싱 경로: 필드별 bounds check, byte copy와 endian 변환으로 구성 가능
- 타입 명확: 4바이트면 `int32_t`
- Zero-copy 가능 (후술)

**단점:**
- 사람이 읽을 수 없음
- 스키마 필수 (바이트 해석 방법 필요)
- 버전 호환성 어려움
- 엔디안 문제

### 세이브 데이터 비교를 설계하는 법

Unreal 스타일 액터 데이터를 예로 들면 payload에는 고정 크기 벡터뿐 아니라 이름 테이블 참조와 가변 길이 객체 참조가 섞인다.

```cpp
// 1000개 액터 직렬화
struct Actor {
    FVector3f Location;    // float32 3개 = 12바이트 (UE5 기본 FVector는 double 기반)
    FRotator3f Rotation;   // float32 3개 = 12바이트
    FName Name;            // 크기·표현은 엔진 버전과 빌드에 의존
    TArray<UObject*> Refs; // 가변
};
```

JSON은 필드 이름과 숫자 텍스트 때문에 압축 전 크기와 변환 비용이 커질 수 있고, 스키마 기반 바이너리는 field tag·varint·고정 폭 값으로 더 조밀할 수 있다. 그러나 “바이너리가 압축률도 항상 좋다”는 규칙은 아니다. JSON의 반복 필드 이름은 범용 압축기가 잘 줄이고, 이미 압축된 blob이 지배하면 차이가 작다. 실제 대표 세이브 corpus에서 원본/압축 크기, encode/decode p50·p95, peak allocation과 버전 migration 비용을 측정한다.

## 3. 엔디안과 정렬: 이식성의 적

### 엔디안 문제

```cpp
uint32_t value = 0x12345678;

// Little Endian (x86, x64, ARM Windows):
// 메모리: [78 56 34 12]
//          낮은 주소에 낮은 바이트

// Big Endian (네트워크, PowerPC):
// 메모리: [12 34 56 78]
//          낮은 주소에 높은 바이트
```

Little Endian 머신에서 직렬화한 파일을 Big Endian에서 읽으면:

```cpp
// 저장: 0x12345678 → [78 56 34 12]
// 읽기: [78 56 34 12] → 0x78563412 (완전히 다른 값!)
```

### 엔디안 변환

```cpp
// 네트워크 바이트 오더 (Big Endian)로 변환
uint32_t htonl(uint32_t host) {
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        return __builtin_bswap32(host);
    #else
        return host;
    #endif
}

uint32_t ntohl(uint32_t net) {
    return htonl(net);  // 대칭 연산
}
```

이 스케치는 `__BYTE_ORDER__`를 정의하는 GCC/Clang 전용이다. MSVC에는 이 매크로와 `__builtin_bswap32`가 없으므로 `htonl`/`ntohl`은 플랫폼 헤더(Windows `winsock2.h`, POSIX `arpa/inet.h`)가 제공하는 선언을 쓰고, 직접 구현하려면 C++20 `std::endian`으로 분기한 뒤 미지원 타깃을 `#error`로 거부하며 시스템 함수와 같은 이름을 정의하지 않는다.

GCC/Clang의 `__builtin_bswap32`는 단일 CPU 명령어로 컴파일된다:

```asm
; x86-64
bswap eax

; ARM
rev r0, r0
```

하지만 모든 필드에 변환을 적용하는 것은 번거롭다:

```cpp
void serialize(const Player& p, uint8_t* out) {
    uint32_t id_bits = htonl(static_cast<uint32_t>(p.id));
    std::memcpy(out, &id_bits, sizeof(id_bits));
    out += sizeof(id_bits);

    // float object representation을 같은 크기의 정수로 안전하게 복사한다.
    uint32_t x_bits = std::bit_cast<uint32_t>(p.x);
    x_bits = htonl(x_bits);
    std::memcpy(out, &x_bits, sizeof(x_bits));
    out += sizeof(x_bits);
    // ... 모든 필드에 반복
}
```

### 바이트 단위 직렬화

엔디안 독립적 방법:

```cpp
void write_uint32(uint32_t value, uint8_t* out) {
    out[0] = (value >> 24) & 0xFF;
    out[1] = (value >> 16) & 0xFF;
    out[2] = (value >> 8) & 0xFF;
    out[3] = value & 0xFF;
}

uint32_t read_uint32(const uint8_t* in) {
    return ((uint32_t)in[0] << 24) |
           ((uint32_t)in[1] << 16) |
           ((uint32_t)in[2] << 8) |
           ((uint32_t)in[3]);
}
```

느릴까? 놀랍게도 현대 컴파일러는 이를 최적화한다:

```asm
; GCC -O2로 컴파일한 read_uint32 (x86-64)
mov eax, DWORD PTR [rdi]
bswap eax
ret
```

`memcpy` + 엔디안 변환과 동일한 성능을 낸다.

### 정렬 문제

```cpp
struct Data {
    uint8_t flag;   // 1바이트
    uint32_t id;    // 4바이트
};  // 실제 크기: 8바이트 (패딩 3바이트)
```

직렬화 시 패딩을 제거하면:

```text
[flag][id.......] (5바이트)
```

역직렬화할 때 문제가 발생한다:

```cpp
Data d;
memcpy(&d.flag, data, 1);
memcpy(&d.id, data + 1, 4);  // data+1이 4바이트 정렬되지 않음!
```

`memcpy`는 바이트 단위 계약이므로 `data + 1`이 4바이트 정렬이 아니어도 안전하고, 컴파일러가 대상 ISA에 맞는 load/store로 최적화할 수 있다. 반대로 `reinterpret_cast<uint32_t*>(data + 1)`를 역참조하면 C++ 정렬 계약을 위반한다. x86-64와 현대 AArch64 Normal memory는 많은 비정렬 하드웨어 접근을 처리하지만, 캐시 라인·페이지 경계나 Device memory, 특정 원자 명령에서는 비용 또는 fault 조건이 달라진다.

**해결책 1: Packed 구조체**

```cpp
struct __attribute__((packed)) Data {
    uint8_t flag;
    uint32_t id;
};  // 정확히 5바이트
```

하지만 packed 멤버의 자연 정렬이 보장되지 않으므로 컴파일러가 비정렬 접근 시퀀스를 생성하거나, 주소를 일반 정렬 포인터로 넘기는 코드가 UB를 만들 수 있다.

**해결책 2: 필드별 복사**

```cpp
uint8_t flag = data[0];
uint32_t id;
memcpy(&id, data + 1, 4);  // 임시 변수로 복사 후 접근
```

Protocol Buffers와 FlatBuffers는 이 문제를 어떻게 해결할까? 계속 읽어보자.

## 4. 스키마 기반: Protocol Buffers, FlatBuffers

### Protocol Buffers: Google의 접근

**스키마 정의 (.proto):**

```protobuf
syntax = "proto3";

message Player {
  int32 id = 1;
  float x = 2;
  float y = 3;
  float z = 4;
  string name = 5;
}
```

**바이너리 포맷 (TLV 인코딩):**

```text
태그 1 (id):     08 2A              # varint: 42
태그 2 (x):      15 00 00 28 41     # fixed32: 10.5
태그 3 (y):      1D 66 66 A2 41     # fixed32: 20.3
태그 4 (z):      25 00 00 A0 40     # fixed32: 5.0
태그 5 (name):   2A 05 41 6C 69 63 65  # length-prefixed: "Alice"
```

총 24바이트 (2 + 5×3 + 7)

**TLV (Type-Length-Value) 구조:**

```text
08 = (필드 번호 1 << 3) | 와이어 타입 0 (varint)
2A = varint 인코딩된 42
```

여기서 태그는 필드 번호와 와이어 타입을 함께 담고, 실제 길이 필드는 와이어 타입 2(length-delimited)에만 붙는다. 와이어 타입 1(64비트)·5(32비트)는 태그로 값의 크기를 알 수 있지만, 타입 0(varint)은 각 바이트의 continuation bit를 읽어 끝을 찾는다. 음수 `int32`/`int64`는 부호 확장 때문에 항상 10바이트가 되므로 음수가 가능한 값은 `sint32`/`sint64`의 ZigZag 인코딩을 고려한다.

### Varint 인코딩

작은 정수를 효율적으로 저장:

```cpp
void encode_varint(uint64_t value, std::vector<uint8_t>& out) {
    while (value >= 0x80) {
        out.push_back((value & 0x7F) | 0x80);  // 하위 7비트 + continuation
        value >>= 7;
    }
    out.push_back(value & 0x7F);  // 마지막 바이트
}

bool decode_varint(const uint8_t* data, size_t size, size_t& offset,
                   uint64_t& out) {
    uint64_t result = 0;
    size_t cur = offset;  // 실패하면 offset을 소비하지 않는다

    for (int i = 0; i < 10; ++i) {            // uint64는 최대 10바이트
        if (cur >= size) return false;        // 잘린 입력
        uint8_t byte = data[cur++];

        // 10번째 바이트에는 7비트가 아니라 1비트만 남는다
        if (i == 9 && byte > 1) return false;

        result |= (uint64_t)(byte & 0x7F) << (7 * i);

        if ((byte & 0x80) == 0) {
            offset = cur;  // 성공했을 때만 전진
            out = result;
            return true;
        }
    }

    return false;  // 10바이트를 넘겨도 종료 비트가 없음
}
```

**인코딩 예시:**

```text
값 42 (0x2A):
0010 1010 → 0010 1010 (7비트 이하, 그대로)
→ [2A]

값 300 (0x12C):
0001 0010 1100
→ 010 1100 (하위 7비트) | 0x80
→ 0000 010 (상위 2비트)
→ [AC 02]

값 1 (작은 수):   [01]        1바이트
값 127:           [7F]        1바이트
값 128:           [80 01]     2바이트
값 16383:         [FF 7F]     2바이트
값 16384:         [80 80 01]  3바이트
```

작은 정수가 많은 게임 데이터 (레벨, 아이템 ID)에서 공간 절약이 크다.

### FlatBuffers: Zero-Copy 설계

**스키마 (.fbs):**

```fbs
table Player {
  id:int;
  x:float;
  y:float;
  z:float;
  name:string;
}

root_type Player;
```

**핵심 차이: 오프셋 기반**

```text
table 시작 기준의 한 가지 배치 예 (전체 buffer 시작은 아님):
0x00: [VTable 오프셋] (4바이트)
0x04: [id]             (4바이트)
0x08: [x]              (4바이트)
0x0C: [y]              (4바이트)
0x10: [z]              (4바이트)
0x14: [name 오프셋]    (4바이트)
...
0x20: [name 길이]
0x24: [name 데이터]
```

**VTable (Virtual Table):**

전체 buffer의 첫 4바이트는 root table까지의 오프셋이다. table 첫 필드는 vtable까지의 signed 상대 오프셋이며, 각 필드의 물리 순서는 고정되지 않는다. 아래 vtable이 실제 필드 위치를 알려준다.

```text
0x00: VTable 크기
0x02: 객체 크기
0x04: id 오프셋
0x06: x 오프셋
0x08: y 오프셋
...
```

### Zero-Copy의 의미

**Protocol Buffers (전통적 파싱):**

```cpp
// 디스크 → 메모리
std::string data = read_file("player.pb");

// 파싱 (메모리 할당 + 복사)
Player player;
player.ParseFromString(data);  // 내부적으로 힙 할당!

// 사용
std::cout << player.name();  // 별도로 할당된 std::string
```

파싱 과정:
1. 바이트 읽기
2. varint 디코딩
3. `std::string` 객체 생성 (힙 할당)
4. 데이터 복사

**FlatBuffers (Zero-Copy):**

```cpp
// 디스크 → 메모리
std::vector<uint8_t> data = read_file("player.bin");

// "파싱" (사실상 캐스팅)
const Player* player = GetPlayer(data.data());

// 사용
std::cout << player->name()->c_str();  // data 내부 포인터!
```

접근 과정은 객체 그래프를 별도 heap 객체로 전부 재구성하지 않고 buffer 내부 offset을 따라가는 방식이다. 그래도 buffer 검증, root offset 해석, bounds/alignment 확인과 각 필드 접근의 간접 offset 계산은 남는다. Verifier를 통과했더라도 `name`처럼 required가 아닌 필드는 생략되어 null일 수 있으므로, `name()->c_str()`을 역참조하기 전에 필드 존재와 default 적용 여부를 따로 확인해야 한다.

**메모리 구조 비교:**

```text
Protocol Buffers:
[파일 버퍼] → [파싱] → [Player 객체] → [name 문자열]
                        힙 할당        힙 할당

FlatBuffers:
[파일 버퍼] → [Player* 포인터만]
              ↓
        버퍼 내부를 직접 참조
```

### 성능 비교

Protocol Buffers, FlatBuffers와 수제 바이너리는 서로 다른 문제를 최적화한다.

| 축 | Protocol Buffers | FlatBuffers | 수제 포맷 |
|---|---|---|---|
| 읽기 모델 | decode해 객체/필드 구성 | buffer 내부를 offset으로 접근 | 구현 계약에 따름 |
| 쓰기 비용 | 일반 builder·encoder | 역방향 builder와 layout 구성 | 구현 품질에 따름 |
| 진화 규칙 | field number와 unknown field | schema evolution 규칙 | 직접 정의·검증 |
| 메모리 | decode 객체와 allocator 영향 | 원본 buffer 수명 유지 | copy/zero-copy 선택 |

FlatBuffers는 “역직렬화 함수가 객체를 복사하지 않는다”는 장점이 있지만 모든 workload에서 가장 빠르다는 뜻은 아니다. 같은 필드를 여러 번 무작위 접근하면 offset chasing과 cache miss가 누적될 수 있고, 사용 전에 verifier를 돌리면 그 선형 검사 비용도 포함해야 한다. 파일 크기와 쓰기 속도도 schema·문자열·중복·builder에 따라 달라진다.

## 5. Zero-Copy 직렬화: Cap'n Proto의 혁신

### 근본적 아이디어

Cap'n Proto의 목표는 wire buffer를 별도 객체 그래프로 복사·재구성하지 않고 generated reader가 직접 해석하게 만드는 것이다. 여기서 “파싱이 없다”는 표현은 검증·bounds check·offset 해석까지 0이라는 뜻이 아니라 *eager materialization 단계를 피한다*는 뜻이다.

**Cap'n Proto 원칙:**
1. 메모리 레이아웃 = 바이너리 포맷
2. builder가 wire layout에 값을 구성하고 절대 주소 대신 인코딩된 상대 포인터 사용
3. 엔디안 고정 (Little Endian)

**스키마:**

```capnp
struct Player {
  id @0 :Int32;
  x @1 :Float32;
  y @2 :Float32;
  z @3 :Float32;
  name @4 :Text;
}
```

**바이너리 포맷:**

```text
0x00: [root struct 포인터]
0x08: [데이터 섹션]
      +00: id (4바이트)
      +04: x (4바이트)
      +08: y (4바이트)
      +0C: z (4바이트)
0x18: [name 포인터]
```

이 오프셋은 첫 segment 내부를 기준으로 한 예다. 파일·스트림의 framed 메시지에는 segment 개수와 크기를 기록한 헤더도 앞에 온다.

### 포인터 인코딩

Cap'n Proto의 struct pointer는 64비트 pointer word 안에 종류, 상대 오프셋, data section 크기와 pointer section 크기를 비트 필드로 인코딩한다. 상대 오프셋은 바이트가 아니라 **64비트 word 단위**이며 pointer word의 끝을 기준으로 한다.

```text
bits  0..1  : pointer kind (struct = 0)
bits  2..31 : signed offset in words from the end of this pointer
bits 32..47 : data section size in words
bits 48..63 : pointer section size in words
```

예시:

```text
0x10: [struct pointer: offset=0 words]
0x18: [실제 data section 시작]
```

포인터 word가 `0x10`에서 `0x17`까지 차지하므로 `offset=0`은 바로 다음 word인 `0x18`을 가리킨다. `offset=1`이면 한 word를 더 건너뛴 `0x20`을 가리킨다. list, far, capability pointer는 kind에 따라 별도 형식을 사용한다.

**왜 상대 오프셋?**
- 파일 전체를 메모리의 어디든 로드 가능 (위치 독립적)
- mmap으로 로드 시 즉시 사용 가능

### mmap을 활용한 진짜 Zero-Copy

Cap'n Proto의 zero-copy는 C++ 구조체 포인터로 캐스팅한다는 의미가 아니다. 직렬화된 wire 포맷(태그된 포인터, 64비트 정렬, little-endian)을 그대로 두고, 컴파일러가 생성한 reader 클래스가 그 wire buffer를 필드 접근 시 직접 디코드한다. 사용자 정의 C++ 구조체 레이아웃과 wire 포맷은 일치하지 않으므로 raw cast는 동작하지 않는다.

```cpp
#include <sys/mman.h>
#include <fcntl.h>
#include <capnp/serialize.h>
#include "world.capnp.h"

// 파일을 메모리에 매핑
int fd = open("world.bin", O_RDONLY);
struct stat sb;
fstat(fd, &sb);

void* addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

// 매핑된 영역을 Cap'n Proto의 word 배열로 본다
kj::ArrayPtr<const capnp::word> words(
    reinterpret_cast<const capnp::word*>(addr),
    sb.st_size / sizeof(capnp::word));

// FlatArrayMessageReader는 복사 없이 wire buffer를 그대로 본다
capnp::FlatArrayMessageReader reader(words);
World::Reader world = reader.getRoot<World>();

// 필드 접근 시 reader가 wire buffer를 디코드한다
// (구조체 포인터 캐스팅이 아니다)
auto players = world.getPlayers();
for (auto player : players) {
    std::cout << player.getName().cStr() << "\n";
}

munmap(addr, sb.st_size);
close(fd);
```

이 스케치는 `open`/`fstat`/`mmap`이 성공했고 `sb.st_size`가 `capnp::word` 크기의 배수이며 메시지가 unpacked framed 형식이라는 전제를 생략한다. 실제 코드는 각 호출의 반환값과 파일 크기·정렬을 검사하고, 실패 시 fd와 매핑을 RAII로 정리하며 reader에 traversal·nesting 한도를 둔다.

**전통적 읽기:**

```text
read() → 커널 버퍼 → 사용자 버퍼 → 파싱(필드별 복사) → 객체 트리
```

**mmap + Cap'n Proto reader:**

```text
mmap() → 커널 페이지 캐시 → reader가 wire buffer 직접 디코드 (필드 접근 시)
```

복사 없이 페이지 캐시의 바이트를 reader API가 그대로 해석한다. zero-copy는 "구조체 포인터로 캐스트"가 아니라 "wire buffer에서 직접 읽는 reader"의 의미다.

### 제약사항

**1. 엔디안 고정**

Cap'n Proto의 **wire format**은 Little Endian으로 고정된다. 호스트가 반드시 Little Endian이어야 한다는 뜻은 아니다. Big Endian 호스트의 구현은 필드 접근 시 바이트 순서를 변환하며, 그 비용이 추가될 수 있다. 임의의 호스트 C++ 객체를 그대로 덤프하는 방식과 구분해야 한다.

**2. 정렬 요구사항**

```cpp
struct Data {
    int32_t id;    // 4바이트 정렬
    int64_t time;  // 8바이트 정렬
};
```

wire format이 정한 필드 정렬과 word 경계 때문에 padding이 포함될 수 있다. 위 C++ 구조체의 컴파일러 의존 padding을 그대로 파일에 쓰는 것은 아니다.

**3. 읽기와 수정의 계약**

위 예제는 `PROT_READ` 매핑과 Reader API이므로 읽기 전용이다. mmap 자체가 항상 읽기 전용인 것은 아니지만, 메시지 수정에는 builder와 저장 공간의 소유권·용량·동기화 규약이 필요하다. 단순히 매핑을 writable로 바꾸는 것만으로 크기가 변하는 메시지의 안전한 수정이 해결되지는 않는다.

## 6. 버전 호환성: 필드 추가와 제거

### 문제 정의

```cpp
// Version 1
struct PlayerV1 {
    int32_t id;
    float x, y, z;
    std::string name;
};

// Version 2 (새 필드 추가)
struct PlayerV2 {
    int32_t id;
    float x, y, z;
    std::string name;
    int32_t level;     // NEW!
    int32_t experience; // NEW!
};
```

구버전 파일을 신버전 코드로 읽으면?
신버전 파일을 구버전 코드로 읽으면?

### Forward Compatibility (전방 호환)

구버전 코드가 신버전 데이터를 읽을 수 있는가?

**수제 바이너리 (명시적 진화 규약이 없으면 불가능):**

```cpp
// V1 코드
memcpy(&p.id, data, 4); offset += 4;
memcpy(&p.x, data + offset, 4); offset += 4;
// ...
// V2 데이터에는 뒤에 8바이트 더 있음
// → V1 코드는 이를 무시하고 읽음
// → 하지만 파일 끝 체크 실패 가능
```

수제 포맷도 필드 길이·태그·버전과 unknown 필드 skip 규약을 정의하면 모르는 필드를 건너뛸 수 있다. 전방 호환을 막는 것은 형식 자체가 아니라 진화 규약의 유무다. 그래서 field number 재사용 금지, 이진 형식과 JSON 이름의 호환, required/default 변경의 영향을 함께 정한다.

**Protocol Buffers (가능):**

```text
V2 데이터:
태그 1 (id):   ...
태그 2 (x):    ...
태그 5 (name): ...
태그 6 (level): ...  ← V1 코드가 모르는 필드

V1 파서:
- 태그 1-5: 읽음
- 태그 6: 필드 번호 인식 못함 → 스킵!
```

TLV 구조 덕분에 알 수 없는 필드를 안전하게 건너뛸 수 있다:

```cpp
while (offset < data.size()) {
    uint64_t tag = 0;
    if (!decode_varint(data, data.size(), offset, tag)) break;

    int field_number = tag >> 3;
    int wire_type = tag & 0x7;

    if (field_number == 6) {
        // 모르는 필드 → 스킵
        skip_field(wire_type, data, offset);
        continue;
    }

    // 알려진 필드 처리
}
```

### Backward Compatibility (후방 호환)

신버전 코드가 구버전 데이터를 읽을 수 있는가?

**Protocol Buffers:**

```protobuf
// proto3는 커스텀 기본값을 지원하지 않는다 ([default = ...]는 proto2 전용).
// proto3의 기본값은 타입의 0값(숫자 0, 빈 문자열, false)으로 고정된다.
message Player {
  int32 id = 1;
  float x = 2;
  float y = 3;
  float z = 4;
  string name = 5;
  int32 level = 6;        // 누락 시 0
  int32 experience = 7;   // 누락 시 0
}
```

V1 데이터를 읽으면 태그 6, 7이 없다. 파서는 자동으로 타입 기본값(0)을 사용한다. "레벨 기본 1" 같은 의미를 원하면 애플리케이션 코드에서 처리하거나 proto2를 써야 한다.

**FlatBuffers:**

```fbs
table Player {
  id:int;
  x:float;
  y:float;
  z:float;
  name:string;
  level:int = 1;       // 기본값
  experience:int = 0;
}
```

VTable 덕분에 가능:

```text
VTable V1:
  필드 0: id
  필드 1: x
  필드 2: y
  필드 3: z
  필드 4: name

VTable V2:
  필드 0: id
  ...
  필드 5: level
  필드 6: experience

V2 코드가 V1 데이터 읽기:
- level 필드 접근 → VTable에 없음 → 기본값 1 반환
```

### Schema Evolution 규칙

**안전한 변경:**
1. optional/default 필드 추가 (FlatBuffers의 암묵적 ID는 기존 필드 뒤에 추가하며, 명시적 ID를 쓰면 그 규칙을 유지)
2. 필드 이름 변경 (번호를 유지하는 바이너리 wire 기준. ProtoJSON·텍스트·생성 API의 이름 호환성은 별도)
3. 주석 변경

**위험한 변경:**
1. 필드 번호 변경 (재할당 금지!)
2. 필드 타입 변경 (`int32` → `string`)
3. 필수 필드 추가 (구버전 데이터가 이를 가질 수 없음)

**Protocol Buffers 권장:**

```protobuf
message Player {
  reserved 10, 11;  // 삭제된 필드 번호 예약
  reserved "old_field_name";

  int32 id = 1;
  // 절대 필드 1을 재사용하지 마라!
}
```

## 7. 게임의 선택: 왜 자체 포맷인가?

### Unreal Engine: Binary Serialization

Unreal은 **FArchive** 추상화를 사용한다:

```cpp
class FArchive {
public:
    virtual FArchive& operator<<(int32& Value) = 0;
    virtual FArchive& operator<<(FString& Value) = 0;
    // ...
};

// 직렬화
void UMyObject::Serialize(FArchive& Ar) {
    Ar << Id;
    Ar << Location;
    Ar << Name;

    if (Ar.IsLoading()) {
        // 역직렬화 후 처리
    }
}
```

위 `FArchive` 인터페이스는 개념도다. 실제 엔진의 `FArchive`는 템플릿 `operator<<`와 파생 아카이브로 구성되고, 버전 태그·압축·에셋 참조는 아카이브 종류와 호출 경로가 나눠 맡으므로 이름만으로 자동 보장되지 않는다. `UObject::Serialize`를 재정의할 때는 대상 버전에 맞게 `Super::Serialize(Ar)` 호출 여부를 확인한다.

**바이너리 포맷:**
- 엔디안 변환 자동 (플랫폼별)
- 버전 태그 포함 (`Ver` 필드)
- 압축 지원 (LZ4, Oodle)
- 에셋 참조 (`FObjectPtr`)

**왜 자체 포맷?**
1. **에셋 로딩 최적화**: 텍스처, 메시는 수백 MB → 파싱 오버헤드 감당 불가
2. **스트리밍**: 레벨 일부만 로드 가능해야 함
3. **에디터 통합**: Reflection 시스템과 긴밀히 연결
4. **플랫폼 특화**: PlayStation은 SSD 최적화, Xbox는 압축 해제 하드웨어 사용

### Unity: YAML + Binary Hybrid

**Scene 파일 (.unity):**

```yaml
--- !u!1 &1234567890
GameObject:
  m_ObjectHideFlags: 0
  m_PrefabParentObject: {fileID: 0}
  m_PrefabInternal: {fileID: 0}
  serializedVersion: 5
  m_Component:
  - component: {fileID: 1234567891}
  m_Name: Player
```

**에셋 번들 (.bundle):**
- 바이너리 archive이며 LZMA·chunk 기반 LZ4·비압축 중 선택
- LZ4 청크는 필요한 범위만 압축 해제할 수 있고, LZMA는 전체 content stream 압축 해제가 필요

**왜 YAML?**
- **버전 관리 친화적**: Git diff가 의미 있음
- **수동 편집 가능**: 텍스트 에디터로 수정 가능
- **멀티 플레이어 머지**: 씬 충돌 해결 용이

**왜 바이너리 번들?**
- **런타임 성능**: 빠른 로딩
- **배포 형태**: 압축률과 필요한 자산의 부분 로딩 비용에 맞춰 압축 방식 선택
- **코드와 자산 분리**: 일반 AssetBundle은 C# assembly를 포함하지 않는다. 바이너리 형식 자체가 암호화나 소스 코드 보호를 제공하는 것은 아니다

### Source Engine: KeyValues + BSP

**KeyValues (.vmt,.vmf):**

```text
"VertexLitGeneric"
{
    "$basetexture" "models/player/player_skin"
    "$bumpmap" "models/player/player_normal"
    "$phong" "1"
    "$phongexponent" "30"
}
```

**BSP 파일 (바이너리):**
- 헤더 + Lump 구조
- 각 Lump는 독립적 데이터 블록 (버텍스, 엔티티, 가시성 등)

```cpp
// 레이아웃 발췌: 실제 VBSP는 이 뒤에 mapRevision 등 추가 필드가 온다.
struct BSPLump {
    int32_t fileOffset;
    int32_t fileLength;
    int32_t version;
    char fourCC[4];
};

struct BSPHeader {
    int32_t magic;  // 'VBSP'
    int32_t version;
    BSPLump lumps[64];
};
```

**설계 철학:**
- **모듈식**: Lump 단위로 로드/언로드
- **확장성**: 새 Lump 타입 추가 가능
- **맵 컴파일 파이프라인**: BSP, VIS, RAD 각각 Lump 업데이트

### 공통 패턴

게임 엔진들의 선택에서 공통점:

1. **하이브리드**: 에디터는 텍스트, 런타임은 바이너리
2. **청크 기반**: 큰 파일을 작은 블록으로 분할
3. **압축**: zlib, LZ4, Oodle 등
4. **버전 관리**: 파일 헤더에 버전 번호
5. **플랫폼별 빌드**: 최종 배포 시 플랫폼 최적화

## 8. 실전: 네트워크 패킷과 세이브 파일

### 네트워크 패킷: Latency가 생명

**요구사항:**
- 최소 크기 (대역폭 절약)
- 빠른 인코딩/디코딩 (60fps 유지)
- 신뢰성 없는 UDP 가정

**전형적인 게임 패킷:**

```cpp
#pragma pack(push, 1)  // 패딩 제거
struct MovementPacket {
    uint8_t type;        // 패킷 타입 (1바이트)
    uint16_t seq;        // 시퀀스 번호 (2바이트)
    uint32_t timestamp;  // 타임스탬프 (4바이트)

    // 16비트 고정소수점 (x100 스케일)
    int16_t x, y, z;     // 위치 (6바이트)
    int16_t yaw, pitch;  // 회전 (4바이트)

    uint8_t flags;       // 플래그 (점프, 앉기 등)
};  // 총 18바이트
#pragma pack(pop)
```

**왜 고정소수점?**

```cpp
// float (4바이트): 정밀도 낭비
float x = 123.456f;

// int16 고정소수점 (2바이트): ±327.67 범위, 0.01 정밀도
// 유한성과 범위를 확인한 뒤 반올림해 저장한다 (정책에 따라 reject 또는 clamp)
bool encodable = std::isfinite(x) && std::abs(x) <= 327.67f;
int16_t x_fixed = encodable ? (int16_t)std::lround(x * 100.0f) : 0;

// 복원
float x_restored = x_fixed / 100.0f;  // 최대 오차 0.005
```

이 스케일은 단위가 미터이고 관심 영역이 위 범위에 들어오는 경우의 선택이다. 큰 월드는 sector·지역 원점과 상대 좌표를 쓰거나 더 넓은 정수 형식·다른 스케일을 골라야 한다. 필요한 위치·회전 오차도 게임별로 정한다.

**델타 인코딩:**

```cpp
struct StateDelta {
    uint8_t changed_mask;  // 어떤 필드가 변경되었는가?
    // changed_mask & 0x01 → x 변경
    // changed_mask & 0x02 → y 변경
    // ...

    // 변경된 필드만 전송
    int16_t x;  // changed_mask & 0x01 일 때만 존재
    int16_t y;  // changed_mask & 0x02 일 때만 존재
    // ...
};
```

플레이어가 점프만 했다면 (z만 변경):

```json
[changed_mask: 0x04][z: 2바이트]
```

총 3바이트 (원래 18바이트의 약 17%)

이 절약은 송수신자가 같은 기준 상태(baseline)를 공유할 때만 성립한다. UDP 손실·재정렬 뒤에는 잘못된 baseline에 delta를 적용하지 않도록 baseline ID·sequence와 acknowledgment를 두고, 어긋나면 full snapshot으로 다시 맞춘다. 패킷 헤더와 MTU 비용도 함께 계산한다.

### 세이브 파일: 안정성이 생명

**요구사항:**
- 데이터 무결성 (체크섬)
- 버전 호환성
- 압축 (수 MB 절약)
- 부분 로딩 (대형 RPG는 즉시 로딩 불가)

**파일 구조:**

```cpp
struct SaveFileHeader {
    char magic[4];       // 'SAVE'
    uint32_t version;    // 스키마 버전
    uint32_t checksum;   // checksum 필드를 0으로 두고 계산한 CRC-32/ISO-HDLC
    uint32_t num_chunks;
    uint32_t uncompressed_size;
};

struct ChunkHeader {
    uint32_t type;       // PLAYER, WORLD, QUEST, INVENTORY 등
    uint32_t offset;     // 파일 내 절대 오프셋
    uint32_t compressed_size;
    uint32_t uncompressed_size;
};
```

이 구조체들은 논리적 필드를 설명할 뿐이다. C++ 구조체를 그대로 `fwrite`하면 padding, native endian, 정수 폭과 ABI가 파일 형식에 섞인다. 디스크 표현은 필드별 바이트 순서와 크기를 고정해 별도로 인코딩해야 한다.

**저장 과정:**

```cpp
constexpr uint64_t kHeaderBytes = 20;
constexpr uint64_t kChunkHeaderBytes = 16;

bool encode_save(const GameState& state, std::vector<uint8_t>& file) {
    std::vector<EncodedChunk> chunks;
    chunks.push_back(encode_player(state.player));
    chunks.push_back(encode_world(state.world));
    chunks.push_back(encode_quests(state.quests));

    for (auto& chunk : chunks) {
        chunk.uncompressed_size = checked_u32(chunk.bytes.size());
        chunk.bytes = lz4_compress(chunk.bytes);
        chunk.compressed_size = checked_u32(chunk.bytes.size());
    }

    // 곱셈과 덧셈은 넓은 형식에서 검사한 뒤 32비트 파일 필드로 축소한다.
    uint64_t payload_offset =
        checked_add(kHeaderBytes,
                    checked_mul(chunks.size(), kChunkHeaderBytes));
    if (payload_offset > UINT32_MAX) return false;

    file.clear();
    append_bytes(file, {'S', 'A', 'V', 'E'});
    append_u32_le(file, SAVE_VERSION);
    const size_t checksum_offset = file.size();
    append_u32_le(file, 0);  // 계산 중에는 0
    append_u32_le(file, checked_u32(chunks.size()));
    append_u32_le(file, sum_uncompressed_sizes(chunks));

    uint64_t next_offset = payload_offset;
    for (const auto& chunk : chunks) {
        if (next_offset > UINT32_MAX) return false;
        append_u32_le(file, chunk.type);
        append_u32_le(file, static_cast<uint32_t>(next_offset));
        append_u32_le(file, chunk.compressed_size);
        append_u32_le(file, chunk.uncompressed_size);
        next_offset = checked_add(next_offset, chunk.compressed_size);
    }

    for (const auto& chunk : chunks) {
        append_bytes(file, chunk.bytes);
    }
    if (file.size() != next_offset) return false;

    // magic, version, table, payload까지 보호한다.
    store_u32_le(file, checksum_offset, crc32_ieee(file));
    return true;
}
```

`checked_add`, `checked_mul`, `checked_u32`는 overflow를 오류로 반환하는 함수다. `append_u32_le`와 `store_u32_le`는 호스트 endian과 무관하게 little-endian 4바이트를 기록한다. 실제 저장은 같은 디렉토리의 임시 파일에 모든 바이트를 쓰고 오류를 검사한 뒤, 필요한 내구성 수준에 맞게 flush하고 원본과 원자적으로 교체한다. 단순한 `fclose`는 전원 손실 뒤의 영속성을 보장하지 않는다. POSIX의 `fsync`와 디렉토리 `fsync`, Windows의 flush와 replace API처럼 플랫폼별 crash-consistency 계약을 확인해야 한다.

**부분 로딩과 적대적 입력 검증:**

```cpp
bool decode_player_only(std::span<const uint8_t> file, Player& player) {
    constexpr uint32_t kMaxChunks = 4096;
    constexpr uint32_t kMaxPlayerBytes = 16 * 1024 * 1024;

    if (file.size() < kHeaderBytes) return false;
    if (!starts_with(file, "SAVE")) return false;

    const uint32_t stored_crc = read_u32_le(file, 8);
    if (crc32_with_zeroed_field(file, 8, 4) != stored_crc) return false;

    const uint32_t version = read_u32_le(file, 4);
    const uint32_t count = read_u32_le(file, 12);
    if (!is_supported_version(version) || count > kMaxChunks) return false;

    const uint64_t table_end =
        checked_add(kHeaderBytes, checked_mul(count, kChunkHeaderBytes));
    if (table_end > file.size()) return false;

    // 먼저 표 전체를 검증한다: offset + size overflow, 순서 위반·중첩,
    // 중복 Player 청크, 파일 밖 참조와 압축 폭탄을 거부한다.
    uint64_t prev_end = table_end;
    uint32_t player_index = UINT32_MAX;
    for (uint32_t i = 0; i < count; ++i) {
        const ChunkHeader ch = decode_chunk_header_le(
            file.subspan(kHeaderBytes + i * kChunkHeaderBytes,
                         kChunkHeaderBytes));

        if (ch.offset < prev_end) return false;  // 겹치거나 순서가 어긋남
        const uint64_t end = checked_add(ch.offset, ch.compressed_size);
        if (end > file.size()) return false;
        prev_end = end;

        if (ch.type == CHUNK_PLAYER) {
            if (player_index != UINT32_MAX) return false;  // Player 청크 중복
            if (ch.uncompressed_size > kMaxPlayerBytes) return false;
            player_index = i;
        }
    }

    // 검증된 표에서 플레이어 청크만 골라 decode한다.
    if (player_index == UINT32_MAX) return false;
    const ChunkHeader ch = decode_chunk_header_le(
        file.subspan(kHeaderBytes + player_index * kChunkHeaderBytes,
                     kChunkHeaderBytes));
    auto compressed = file.subspan(ch.offset, ch.compressed_size);
    auto data = lz4_decompress_exact(compressed, ch.uncompressed_size);
    if (!data) return false;
    return decode_player_strict(*data, version, player);
}
```

`decode_player_strict`도 문자열 길이, 배열 개수, enum 범위, 부동소수점 유한성, 필수 필드와 trailing bytes를 검증해야 한다. 체크섬이 맞는다는 사실은 입력이 의미적으로 유효하거나 안전하다는 뜻이 아니다. 다만 위 코드는 전체 파일 CRC를 먼저 계산하므로 모든 바이트를 읽는다. 이것은 월드 전체를 역직렬화하지 않는 **부분 역직렬화**이지 일부만 읽는 **부분 I/O**가 아니다. I/O까지 줄이려면 header/table checksum과 청크별 checksum을 두고 필요한 범위만 읽는 설계로 나눈다.

### 체크섬: 데이터 무결성

**CRC32 (Cyclic Redundancy Check):**

```cpp
uint32_t crc32_table[256];

void crc32_init() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
        crc32_table[i] = crc;
    }
}

uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return ~crc;
}
```

**왜 CRC32?**
- 테이블 방식과 CPU 명령어 가속을 사용할 수 있어 우발적 손상 검사에 적합하다.
- 위 코드의 reflected polynomial `0xEDB88320`은 CRC-32/ISO-HDLC의 다항식 표현이다. 생성 다항식의 성질상 보호 범위 안의 모든 단일 비트 오류와 길이 32비트 이하의 burst error를 검출한다.
- 더 긴 임의 오류에 대해서는 완전한 보장이 없으며, 균등한 무작위 오류가 우연히 같은 CRC를 만들 확률을 대략 `2^-32`로 모델링할 수 있다.
- CRC 이름만으로는 매개변수가 결정되지 않는다. polynomial, 초기값, 입력·출력 반전, 최종 XOR, 보호하는 바이트 범위를 파일 형식에 함께 고정해야 한다.

CRC는 공격자가 없는 저장 매체 오류나 전송 오류를 찾는 코드다. 공격자는 payload를 바꾸고 CRC를 다시 계산할 수 있으므로 변조 방지나 치트 방지 수단이 아니다.

**암호학적 해시와 메시지 인증**

```cpp
digest = SHA256(canonical_bytes);                    // 내용 식별
tag = HMAC_SHA256(server_secret, canonical_bytes);  // 출처와 무결성 인증
ok = constant_time_equal(tag, received_tag);
```

SHA-256 digest만 저장하면 공격자가 데이터와 digest를 함께 다시 계산할 수 있다. 비밀 키를 가진 HMAC 또는 개인 키로 만든 전자서명이 있어야 공격자가 임의의 새 파일에 유효한 인증값을 붙이기 어렵다. 비밀을 클라이언트 바이너리에 넣으면 추출될 수 있으므로, 서버가 신뢰 경계라면 서버가 HMAC이나 서명을 생성·검증해야 한다. 기밀성까지 필요하면 검증된 AEAD 구성을 사용한다. CRC, SHA-256, HMAC의 상대 성능은 CPU 명령어, 라이브러리, 메시지 크기에 따라 달라지므로 고정 배수로 선택하지 않는다.


---

## 결론

직렬화는 단순해 보이지만 결정해야 할 축이 많다. JSON과 바이너리의 선택, 엔디안과 정렬, zero-copy의 가능 여부, 스키마 진화 정책까지 모두 성능, 유지보수성, 이식성 사이의 트레이드오프다.

핵심 결정 다섯 가지를 다시 정리하면 다음과 같다. 텍스트 포맷은 diff·수동 편집·진단에, 스키마 기반 바이너리는 크기·decode 계약·호환성 통제에 유리할 수 있지만 용도별 측정이 필요하다. 엔디안과 정수 폭은 wire format에 명시한다. zero-copy는 buffer 수명, 상대 오프셋, 정렬, 검증과 접근 패턴이 함께 맞아야 이득이다. 스키마 진화는 기존 세이브·패킷·에셋을 읽어야 하는 지원 기간만큼 테스트한다. 네트워크 패킷, 세이브 파일, 에셋은 지연, 부분 읽기, 디버깅, 보존 기간이 달라 하나의 포맷이 모두에 최적이라고 가정하지 않는다.

length-prefix, 정렬, 압축 결합과 buffer ownership은 네트워크·저장·메모리 경계가 공유하는 계약이다. 직렬화 포맷을 정하는 일은 byte stream의 의미와 지원 기간을 정하는 일이며, 생산자와 모든 소비자가 그 계약을 함께 지켜야 한다.


## 더 읽을거리

### 공식 문서

- [Google Protocol Buffers](https://protobuf.dev/)
 - Encoding Guide: https://protobuf.dev/programming-guides/encoding/
 - Protocol Buffers가 varint와 TLV를 사용하는 이유

- [FlatBuffers Documentation](https://flatbuffers.dev/)
 - Internals: https://flatbuffers.dev/flatbuffers_internals.html
 - VTable 구조와 Zero-copy 설계 철학

- [Cap'n Proto](https://capnproto.org/)
 - Encoding: https://capnproto.org/encoding.html
 - Kenton Varda의 블로그: Protocol Buffers의 한계와 Cap'n Proto의 해결책

- [MessagePack Specification](https://msgpack.org/)
 - Format Spec: https://github.com/msgpack/msgpack/blob/master/spec.md
 - 바이너리 JSON으로 불리는 이유

### Unreal Engine

- [Unreal Engine Serialization](https://docs.unrealengine.com/5.3/en-US/API/Runtime/CoreUObject/Serialization/)
 - FArchive 아키텍처
 - Versioning과 Property Serialization

- [Unreal Engine Save Game](https://docs.unrealengine.com/5.3/en-US/saving-and-loading-your-game-in-unreal-engine/)
 - USaveGame 시스템
 - 바이너리 포맷 + 압축

### 기술 논문 및 서적

- **"Designing Data-Intensive Applications"** by Martin Kleppmann
 - Chapter 4: Encoding and Evolution
 - Avro, Thrift, Protocol Buffers 비교 분석

- **"Game Engine Architecture"** by Jason Gregory (3rd Edition)
 - 게임 엔진의 resource·runtime object representation과 데이터 파이프라인 배경

- Rob Pike, ["The byte order fallacy"](https://commandcenter.blogspot.com/2012/04/byte-order-fallacy.html)
 - 엔디안 문제에 대한 Plan 9·Go 공동 개발자의 견해

### 소스 코드 분석

- [protobuf C++ implementation](https://github.com/protocolbuffers/protobuf/tree/main/src/google/protobuf)
 - wire_format_lite.cc: varint 인코딩 구현

- [FlatBuffers C++ implementation](https://github.com/google/flatbuffers/blob/master/include/flatbuffers/flatbuffers.h)
 - VTable 생성과 오프셋 계산

- [Cap'n Proto C++ implementation](https://github.com/capnproto/capnproto/tree/master/c%2B%2B/src/capnp)
 - 상대 포인터 인코딩

- [LZ4 Compression](https://github.com/lz4/lz4)
 - 게임 세이브 파일에서 가장 많이 쓰이는 압축 알고리즘

### 표준 및 RFC

- [RFC 8949 - Concise Binary Object Representation (CBOR)](https://www.rfc-editor.org/rfc/rfc8949) (RFC 7049의 개정판)
 - JSON의 바이너리 대안

- [IEEE 754 Floating Point](https://ieeexplore.ieee.org/document/8766229)
 - float 직렬화와 엔디안 문제

### 도구

- [Protobuf Compiler (protoc)](https://grpc.io/docs/protoc-installation/)
 -.proto → C++ 코드 생성

- [FlatBuffers Compiler (flatc)](https://github.com/google/flatbuffers/releases)
 -.fbs → C++ 코드 생성

- [Wireshark](https://www.wireshark.org/)
 - 네트워크 패킷 분석 (Protocol Buffers 플러그인 지원)

- [ImHex](https://github.com/WerWolv/ImHex)
 - 바이너리 파일 hex 에디터 (패턴 매칭 기능)
