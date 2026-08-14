// BinaryArchive — 단일 Serialize() 함수로 읽기/쓰기를 겸하는 이진 직렬화.
//
// 타입마다 입력 함수와 출력 함수를 따로 두면 둘이 어긋나 버그가 난다(필드를
// 추가했는데 한쪽만 고치는 경우). 대신 타입은 자기 필드를 한 번만 기술한다 —
//
//     template <class Ar> void Serialize(Ar& ar) { ar(x, y, name, items); }
//
// 같은 함수가 Archive 가 쓰기 모드면 직렬화, 읽기 모드면 역직렬화로 동작한다.
// 이 "필드를 방문하는 단 하나의 기술"이 곧 경량 리플렉션이다. cereal/boost.
// serialization 이 쓰는 방식이며, 필드 누락 버그를 구조적으로 막는다.
//
// 포맷은 네이티브 바이트 순서(little-endian 가정) — 단일 플랫폼 세이브/리플레이
// 용이다. 크로스 플랫폼이 필요하면 Raw 에 바이트 스왑을 넣으면 된다.

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace mega {

class BinaryArchive {
public:
    static BinaryArchive Writer() { BinaryArchive a; a.writing_ = true; return a; }
    static BinaryArchive Reader(const uint8_t* data, size_t n) {
        BinaryArchive a;
        a.writing_ = false;
        a.rp_   = data;
        a.rbeg_ = data;
        a.rend_ = data + n;
        return a;
    }

    bool Writing() const { return writing_; }
    bool Error()   const { return error_; }
    const std::vector<uint8_t>& Data() const { return buf_; }
    size_t BytesRead() const { return (size_t)(rp_ - rbeg_); }

    // 가변 인자: ar(a, b, c) 로 여러 필드를 한 줄에 방문.
    template <class... Ts>
    void operator()(Ts&... xs) { (Field(xs), ...); }

private:
    // 멤버 Serialize 보유 여부.
    template <class T>
    static constexpr bool HasSerialize = requires(T& t, BinaryArchive& ar) { t.Serialize(ar); };

    template <class T> struct IsVector : std::false_type {};
    template <class U> struct IsVector<std::vector<U>> : std::true_type {};

    template <class T>
    void Field(T& x) {
        if constexpr (HasSerialize<T>) {
            x.Serialize(*this);                          // 사용자 정의 필드 방문
        } else if constexpr (std::is_same_v<T, std::string>) {
            FieldString(x);
        } else if constexpr (IsVector<T>::value) {
            FieldVector(x);
        } else {
            static_assert(std::is_trivially_copyable_v<T>,
                          "Serialize() 멤버가 없고 trivially copyable 도 아닌 타입");
            Raw(&x, sizeof(T));
        }
    }

    void FieldString(std::string& s) {
        uint32_t n = (uint32_t)s.size();
        Raw(&n, sizeof(n));
        if (!writing_) { if (!CanRead(n)) { error_ = true; return; } s.resize(n); }
        if (n) Raw(s.data(), n);
    }

    template <class U>
    void FieldVector(std::vector<U>& v) {
        uint32_t n = (uint32_t)v.size();
        Raw(&n, sizeof(n));
        if (!writing_) {
            if (error_) return;
            // 모든 원소는 최소 1바이트를 소비한다(POD 는 sizeof≥1, 컨테이너는
            // 4바이트 길이 헤더). 남은 바이트보다 큰 count 는 손상이므로 거른다 —
            // 거대 resize 로 인한 OOM/크래시를 막는다.
            if (n > (size_t)(rend_ - rp_)) { error_ = true; return; }
            v.clear();
            v.resize(n);
        }
        for (uint32_t i = 0; i < n && !error_; ++i) Field(v[i]);   // 원소도 재귀 방문
    }

    bool CanRead(size_t n) const { return (size_t)(rend_ - rp_) >= n; }

    void Raw(void* p, size_t n) {
        if (writing_) {
            const uint8_t* b = static_cast<const uint8_t*>(p);
            buf_.insert(buf_.end(), b, b + n);
        } else {
            if (!CanRead(n)) { error_ = true; std::memset(p, 0, n); return; }
            std::memcpy(p, rp_, n);
            rp_ += n;
        }
    }

    std::vector<uint8_t> buf_;                  // 쓰기 버퍼
    const uint8_t* rp_   = nullptr;             // 읽기 커서
    const uint8_t* rbeg_ = nullptr;
    const uint8_t* rend_ = nullptr;
    bool writing_ = true;
    bool error_   = false;
};

// 편의: 쓰기 → 바이트, 읽기 → 객체.
template <class T>
std::vector<uint8_t> SaveBinary(T& obj) {
    BinaryArchive ar = BinaryArchive::Writer();
    ar(obj);
    return ar.Data();
}

template <class T>
bool LoadBinary(T& obj, const uint8_t* data, size_t n) {
    BinaryArchive ar = BinaryArchive::Reader(data, n);
    ar(obj);
    return !ar.Error();
}

}  // namespace mega
