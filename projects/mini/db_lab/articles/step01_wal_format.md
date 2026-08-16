## 들어가며

`db_lab`은 작은 키-값 스토어를 만들지만 목표는 기능 수가 아니다. 디스크에 무엇을 어떤 순서로 기록해야 커밋과 복구를 설명할 수 있는지 확인한다. 첫 단계에서는 WAL 프레임 형식과 CRC를 만든다.

---

## 목차
1. [최소 디스크 형식](#1-최소-디스크-형식)
2. [프레임 직렬화](#2-프레임-직렬화)
3. [CRC의 역할](#3-crc의-역할)
4. [파싱 경계](#4-파싱-경계)
5. [검증 기준](#5-검증-기준)

---

## 1. 최소 디스크 형식

한 프레임은 다음 형식이다.

```text
[u32 payload_len][u32 crc32(payload)][u8 op][body...]
```

연산은 Put, Del, Commit 세 가지다. Put과 Del은 트랜잭션의 변경 내용을 표현하고, Commit은 앞선 변경 묶음이 원자적으로 보이기 시작하는 경계를 표시한다.

## 2. 프레임 직렬화

정수는 고정 크기로 쓰고, 문자열은 길이와 바이트를 함께 기록한다. 역직렬화는 항상 남은 바이트 수를 확인해야 한다. 로그 파일의 내용은 신뢰할 수 없는 입력으로 취급한다.

디스크 바이트 순서는 little-endian으로 고정한다. `uint32_t`를 그대로 `fwrite`하거나 `memcpy`하면 호스트 엔디언에 따라 형식이 달라지므로 바이트를 명시적으로 조립한다.

<!-- snippet: src/wal_kv.hpp -->
```cpp
inline void AppendU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8));
    b.push_back(uint8_t(v >> 16)); b.push_back(uint8_t(v >> 24));
}

inline uint32_t ReadU32Le(const uint8_t* p) {
    return uint32_t(p[0]) |
           (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}
```

연산 종류와 프레임 본문은 다음과 같이 정의한다. `src/wal_kv.hpp`에 그대로 들어간다.

<!-- snippet: src/wal_kv.hpp -->
```cpp
enum class Op : uint8_t { Put = 1, Del = 2, Commit = 3 };

struct Frame {
    Op op;
    std::string key;    // Put/Del
    std::string value;  // Put
};

// ... 위의 AppendU32 / ReadU32Le

inline void AppendStr(std::vector<uint8_t>& b, const std::string& s) {
    AppendU32(b, uint32_t(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}

inline std::vector<uint8_t> EncodePayload(const Frame& f) {
    std::vector<uint8_t> p;
    p.push_back(uint8_t(f.op));
    if (f.op == Op::Put) { AppendStr(p, f.key); AppendStr(p, f.value); }
    else if (f.op == Op::Del) { AppendStr(p, f.key); }
    return p;
}
```

## 3. CRC의 역할

CRC32는 프레임의 무결성을 확인한다. 길이 필드가 남아 있어도 payload가 중간에 잘렸거나 일부 바이트가 손상되면 CRC가 달라진다.

CRC는 공격자를 막는 암호학적 MAC이 아니다. 저장 장치 오류와 torn write를 탐지할 가능성을 높이는 오류 검출 코드다. 길이·CRC·payload가 모두 일관된 과거 값으로 기록되는 저장 장치 실패까지 막아 주지는 않는다.

구현은 IEEE 다항식 테이블 방식이다.

<!-- snippet: src/wal_kv.hpp#Crc32 -->
```cpp
inline uint32_t Crc32(const uint8_t* data, size_t n) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}
```

## 4. 파싱 경계

복구기는 다음 상황에서 안전하게 멈춰야 한다.

- 길이 헤더가 중간에 잘림
- CRC 헤더가 중간에 잘림
- payload가 선언 길이보다 짧음
- CRC 불일치
- 알 수 없는 op
- 문자열 길이가 payload 경계를 넘음

손상 이후의 바이트를 억지로 재동기화하지 않고 꼬리 전체를 버리는 것이 단순하고 안전하다.

payload 역직렬화는 읽기 전에 남은 바이트를 항상 확인한다. 문자열 길이 필드가 payload 경계를 넘으면 실패로 처리한다.

<!-- snippet: src/wal_kv.hpp#DecodePayload -->
```cpp
static bool DecodePayload(const std::vector<uint8_t>& p, Frame& out) {
    size_t off = 0;
    auto readU32 = [&](uint32_t& v) {
        if (off + 4 > p.size()) return false;
        v = ReadU32Le(p.data() + off);
        off += 4;
        return true;
    };
    auto readStr = [&](std::string& s) {
        uint32_t n; if (!readU32(n)) return false;
        if (off + n > p.size()) return false;
        s.assign(reinterpret_cast<const char*>(p.data() + off), n); off += n; return true;
    };
    if (p.empty()) return false;
    out.op = Op(p[0]); off = 1;
    switch (out.op) {
        case Op::Put:    return readStr(out.key) && readStr(out.value);
        case Op::Del:    return readStr(out.key);
        case Op::Commit: return true;
        default:         return false;
    }
}
```

## 5. 검증 기준

1. Put·Del·Commit 프레임이 왕복 직렬화된다.
2. 마지막 바이트를 자르면 파서가 실패한다.
3. payload 한 바이트를 바꾸면 CRC가 실패한다.
4. 비정상 길이가 메모리 과다 할당을 유발하지 않는다.
5. 빈 파일은 정상적인 빈 로그로 처리된다.

빌드는 CMake 파일 하나로 끝난다. 외부 의존성이 없고 lab의 모든 실행 파일이 이 파일을 공유한다. 스토어 헤더와 벤치는 `src/`에, 검사 실행 파일은 `tests/`에 둔다.

ctest 등록은 두 층이다. `db_tests`는 파일 전체를 돌리고, `db_tests_step01`은 이 step이 추가한 검사만 돌린다. 뒤 step에서 검사가 늘어도 이 step 독자가 보는 대상은 그대로다.

<!-- snippet: CMakeLists.txt -->
```cmake
cmake_minimum_required(VERSION 3.20)
project(db_lab CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(db_bench      src/db_bench.cpp)
add_executable(db_crash_test tests/db_crash_test.cpp)
add_executable(db_tests      tests/db_tests.cpp)

foreach(t db_bench db_crash_test db_tests)
  target_include_directories(${t} PRIVATE src)
  if(MSVC)
    # Korean comments are UTF-8. Without /utf-8, a CP949 trail byte can be
    # interpreted as a line-splicing backslash before preprocessing.
    target_compile_options(${t} PRIVATE /W4 /utf-8)
  else()
    target_compile_options(${t} PRIVATE -Wall -Wextra)
  endif()
endforeach()

enable_testing()
add_test(NAME db_tests      COMMAND db_tests)
add_test(NAME db_crash_test COMMAND db_crash_test)
# ... db_bench_smoke — step03
add_test(NAME db_tests_step01 COMMAND db_tests --step=01)
```

이 step이 추가한 것은 스토어가 아니라 형식이므로, 검사도 스토어 없이 인코더와 CRC만 본다. 아래가 `db_tests_step01`에 들어간 `frame_codec_bytes`의 단정이다 — 위 검증 기준 1·3에 그대로 대응한다.

<!-- snippet: tests/db_tests.cpp#TestFrameCodecBytes -->
```cpp
static void TestFrameCodecBytes() {
    std::vector<uint8_t> le;
    walkv::AppendU32(le, 0x01020304u);
    CHECK(le.size() == 4);
    CHECK(le[0] == 0x04 && le[1] == 0x03 && le[2] == 0x02 && le[3] == 0x01);
    CHECK(walkv::ReadU32Le(le.data()) == 0x01020304u);

    // ... Put payload 의 배치: op(1) + [keylen(4)+key] + [vallen(4)+value]
    const std::vector<uint8_t> put = walkv::EncodePayload({walkv::Op::Put, "ab", "xyz"});
    CHECK(put.size() == 1 + 4 + 2 + 4 + 3);
    CHECK(put[0] == uint8_t(walkv::Op::Put));
    CHECK(walkv::ReadU32Le(put.data() + 1) == 2u);
    CHECK(std::string(put.begin() + 5, put.begin() + 7) == "ab");
    CHECK(walkv::ReadU32Le(put.data() + 7) == 3u);
    CHECK(std::string(put.begin() + 11, put.end()) == "xyz");

    // ... Del 은 key 만, Commit 은 op 바이트 하나뿐

    // CRC32 는 표준 IEEE 여야 한다. 아래 검사값은 반사·init·최종 xor 조합까지 고정한다.
    const std::string check_vector = "123456789";
    CHECK(walkv::Crc32(reinterpret_cast<const uint8_t*>(check_vector.data()),
                       check_vector.size()) == 0xCBF43926u);

    std::vector<uint8_t> flipped = put;
    flipped.back() ^= 0xFF;
    CHECK(walkv::Crc32(put.data(), put.size()) !=
          walkv::Crc32(flipped.data(), flipped.size()));
}
```

나머지 셋은 파서 경계 쪽이다 — `crc_mismatch_stops_replay`(CRC 불일치 뒤를 버린다), `garbage_length_rejected`(검증 기준 4의 과다 할당), `oversized_frame_rejected`(길이 상한 가드). 셋 다 `Replay`를 통해 돌지만 단정하는 대상은 이 step이 정한 파싱 경계다.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/Release/db_tests --step=01
```

관측 결과 — 이 step이 추가한 네 검사가 이름과 함께 찍힌다. 실패하면 그 줄이 `FAIL`이 되고 어긋난 CHECK의 파일·줄 번호가 위에 함께 출력되며 종료 코드가 1이 된다.

```text
  ok   frame_codec_bytes
  ok   crc_mismatch_stops_replay
  ok   garbage_length_rejected
  ok   oversized_frame_rejected
db_tests[step01]: 4/4 passed
```

같은 대상이 ctest에도 있다 — `ctest --test-dir build -C Release -R db_tests_step01`. 다만 통과 여부만 알려 주므로, 무엇을 확인했는지 보려면 위처럼 직접 실행한다.

---

## 결론

길이, 연산 종류, payload, CRC로 구성된 WAL 프레임과 경계 검사를 갖춘 파서를 정의했다. 완전한 프레임만 역직렬화하고 잘린 꼬리나 비정상 길이를 거부하므로 손상 입력이 과다 할당이나 범위 밖 읽기로 이어지지 않는다.

CRC는 우발적 손상 탐지 수단이지 인증이나 내구성 보장이 아니다. 이 형식만으로는 어떤 프레임이 하나의 트랜잭션에 속하는지, 저장 장치에 언제 영속화되었는지 판단할 수 없다.
