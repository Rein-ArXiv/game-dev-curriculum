// RingBuffer — 고정 용량 원형 버퍼.
//
// 머리(head)와 꼬리(tail) 인덱스를 capacity 로 감싸 재사용한다. 용량이 2의
// 거듭제곱이면 모듈로를 비트 마스크로 대체해 더 싸다. 게임에서 입력 히스토리,
// 이벤트 큐, 고정 길이 슬라이딩 윈도우(프레임 타이밍 평균)에 쓴다.
//
// 이 버전은 단일 스레드용이다. jobs 모듈의 워커 큐는 이 버퍼를 쓰지 않고
// 뮤텍스로 보호한 std::deque 다(jobs/scheduler.h).

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace mega {

template <class T>
class RingBuffer {
public:
    RingBuffer() = default;
    explicit RingBuffer(size_t capacity) { Init(capacity); }

    void Init(size_t capacity) {
        cap_  = NextPow2(capacity);
        mask_ = cap_ - 1;
        buf_.assign(cap_, T{});
        head_ = tail_ = 0;
    }

    bool   Empty() const { return head_ == tail_; }
    bool   Full()  const { return Size() == cap_; }
    size_t Size()  const { return tail_ - head_; }
    size_t Capacity() const { return cap_; }

    // 가득 차면 가장 오래된 항목을 덮어쓴다(슬라이딩 윈도우 의미론).
    void Push(const T& v) {
        if (Full()) ++head_;          // 오래된 것 버림
        buf_[tail_ & mask_] = v;
        ++tail_;
    }
    void Push(T&& v) {
        if (Full()) ++head_;
        buf_[tail_ & mask_] = std::move(v);
        ++tail_;
    }

    // 가장 오래된 항목 제거. 비었으면 false.
    bool Pop(T& out) {
        if (Empty()) return false;
        out = std::move(buf_[head_ & mask_]);
        ++head_;
        return true;
    }

    // 인덱스 접근(0 = 가장 오래된). 슬라이딩 윈도우 평균 등에.
    T&       operator[](size_t i)       { return buf_[(head_ + i) & mask_]; }
    const T& operator[](size_t i) const { return buf_[(head_ + i) & mask_]; }

    T&       Newest()       { return buf_[(tail_ - 1) & mask_]; }
    const T& Newest() const { return buf_[(tail_ - 1) & mask_]; }

    void Clear() { head_ = tail_ = 0; }

private:
    static size_t NextPow2(size_t v) {
        size_t p = 1;
        while (p < v) p <<= 1;
        return p < 1 ? 1 : p;
    }

    std::vector<T> buf_;
    size_t cap_  = 0;
    size_t mask_ = 0;
    size_t head_ = 0;   // 모듈로 전 단조 증가(차이가 Size)
    size_t tail_ = 0;
};

}  // namespace mega
