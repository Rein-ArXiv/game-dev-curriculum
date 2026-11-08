#pragma once

#include <cstddef>
#include <cstdint>

namespace ppl {

// 모든 커스텀 할당자가 구현하는 추상 인터페이스.
//
// 측정 목적이라 *통계 카운터* 가 인터페이스의 일부다. 매 프레임 카운터를 리셋해
// "이번 프레임에 몇 번 할당했고 몇 바이트 썼는가" 를 HUD가 표시한다.
//
// 메모리 정렬은 모든 alloc 호출이 명시적으로 받는다. 호출 측이 max_align_t 같은
// 최소 정렬을 가정하지 않는다.
//
// 공통 계약 (네 backend 모두 같은 규칙을 따른다):
//   - Allocate(0, ...)은 nullptr을 돌려주고 어떤 카운터도 바꾸지 않는다(no-op).
//   - alignment는 0이 아니고 2의 거듭제곱이어야 한다. 아니면 nullptr.
//   - 요청을 만족할 수 없거나(크기 초과·고갈·실제 확보 실패) nullptr을 돌려주면
//     FailureCount()가 1 증가한다. 논리 live/peak byte는 바뀌지 않는다.
//   - Deallocate는 같은 allocator가 돌려준, 아직 반환하지 않은, 정확한 시작 주소와
//     원래 요청 size를 받는다. Reset 이후의 모든 포인터는 무효다.
//   - Reset은 개별 Deallocate 없이도 논리 live를 0으로 만든다. 즉 LiveBytes()==0만
//     보고 "모든 개별 해제가 일어났다"라고 결론 내릴 수 없다.
//   - non-trivial 객체의 생성·소멸(construct_at/destroy_at)은 allocator가 아니라
//     저장소를 소유한 호출자의 책임이다. allocator는 raw storage만 다룬다.
class IAllocator {
public:
    virtual ~IAllocator() = default;

    virtual void* Allocate(std::size_t size, std::size_t alignment) = 0;
    virtual void  Deallocate(void* ptr, std::size_t size) = 0;

    // Arena/Pool 같이 일괄 회수가 가능한 할당자에서만 의미가 있다.
    // Malloc 같이 호출별 free가 필요한 할당자는 no-op으로 둔다.
    virtual void Reset() {}

    // 통계 ──────────────────────────────────────────────
    // requested 기준 *논리* byte다. allocator가 실제로 확보한 저장소나 RSS와 다르다.
    std::uint64_t AllocCount() const { return allocCount_; }
    std::uint64_t FreeCount()  const { return freeCount_; }
    std::uint64_t LiveBytes()  const { return liveBytes_; }
    std::uint64_t PeakBytes()  const { return peakBytes_; }
    // 실패(요청 거부·고갈·확보 실패)로 nullptr을 돌려준 횟수. Allocate(0)은 세지 않는다.
    std::uint64_t FailureCount() const { return failureCount_; }

    // 공간 회계 ─────────────────────────────────────────
    // requested(논리) byte와 실제 저장소 사용을 구분한다. 둘 다 backend가 알 수
    // 없으면 0을 돌려준다(unknown). 이 값들은 RSS가 아니다.
    //   - ReservedBacking(): allocator가 확보해 유지하는 저장소 용량.
    //   - SlotBytesLive(): class/stride 기준 현재 점유 byte(내부 낭비 포함).
    virtual std::size_t ReservedBacking() const { return 0; }
    virtual std::size_t SlotBytesLive()  const { return 0; }

    // 매 프레임 호출. 누적값은 유지하되 *이번 프레임 누적* 도 별도로 추적한다.
    void TickFrame() {
        frameAlloc_ = 0;
        frameBytes_ = 0;
    }
    std::uint64_t FrameAllocCount() const { return frameAlloc_; }
    std::uint64_t FrameAllocBytes() const { return frameBytes_; }

    // 정렬이 0이 아니고 2의 거듭제곱인지 검사. AlignUp이 의존하는 전제다.
    static bool IsValidAlignment(std::size_t alignment) {
        return alignment > 0
            && (alignment & (alignment - 1)) == 0;
    }

protected:
    void RecordAlloc(std::size_t bytes) {
        ++allocCount_;
        ++frameAlloc_;
        frameBytes_ += bytes;
        liveBytes_  += bytes;
        if (liveBytes_ > peakBytes_) peakBytes_ = liveBytes_;
    }
    void RecordFree(std::size_t bytes) {
        ++freeCount_;
        if (liveBytes_ >= bytes) liveBytes_ -= bytes;
    }
    // 요청 거부·고갈·확보 실패로 nullptr을 돌려줄 때 호출한다. Allocate(0)에는 쓰지 않는다.
    void RecordFailure() {
        ++failureCount_;
    }
    // Reset() 호출 시 LiveBytes 통계도 0으로. Arena/Pool/Slab이 호출한다.
    void ResetLiveBytes() {
        liveBytes_ = 0;
    }

private:
    std::uint64_t allocCount_   = 0;
    std::uint64_t freeCount_    = 0;
    std::uint64_t liveBytes_    = 0;
    std::uint64_t peakBytes_    = 0;
    std::uint64_t frameAlloc_   = 0;
    std::uint64_t frameBytes_   = 0;
    std::uint64_t failureCount_ = 0;
};

// 정렬 헬퍼: addr을 alignment의 배수로 올림.
// alignment는 0이 아니고 2의 거듭제곱이어야 한다. 호출자가 보장하지 않으면
// 결과가 정의되지 않는다. IAllocator::IsValidAlignment로 검사 가능.
inline std::uintptr_t AlignUp(std::uintptr_t addr, std::size_t alignment) {
    return (addr + (alignment - 1)) & ~(static_cast<std::uintptr_t>(alignment) - 1);
}

}  // namespace ppl
