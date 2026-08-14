#pragma once

// HeapManager — Step 5 의 Descriptor Heap 모드 토글용.
//
// 두 모드:
//   - RebuildEachFrame : frame slot 전용 구간에 null SRV를 매 프레임 다시 쓴다.
//   - PersistentTable  : 동일한 null SRV 표를 한 번 만들고 그대로 재사용한다.
//
// 현재 큐브 셰이더는 texture descriptor를 읽지 않는다. 따라서 이 클래스는
// bindless 성능을 가장하지 않고 CPU descriptor 생성 비용과 수명 정책만
// 분리해 측정한다.

#include <algorithm>
#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace dxlab {

template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

enum class HeapPolicy {
    RebuildEachFrame,
    PersistentTable,
};

const char* HeapPolicyName(HeapPolicy p);

struct DescriptorWritePlan {
    UINT first = 0;
    UINT count = 0;
    UINT writes = 0;
    UINT persistentCount = 0;
};

// Heap 구획 규약 — capacity 를 (framesInFlight + 1) 등분한다.
// 앞쪽 framesInFlight 개 슬라이스는 frame-in-flight 별 transient 구간이고,
// 마지막 슬라이스부터 heap 끝까지가 persistent 표 전용 예약 구간이다.
// 이 분할이 있어야 Persistent 표가 in-flight transient 구간을 덮어쓰지 않는다.
inline UINT NullSrvSliceSize(UINT framesInFlight, UINT capacity) {
    if (framesInFlight == 0 || capacity == 0) return 0;
    return capacity / (framesInFlight + 1);
}

inline UINT NullSrvPersistentFirst(UINT framesInFlight, UINT capacity) {
    return framesInFlight * NullSrvSliceSize(framesInFlight, capacity);
}

inline DescriptorWritePlan PlanNullSrvWrites(
        HeapPolicy policy, UINT frameIndex, UINT framesInFlight,
        UINT requestedCount, UINT capacity, UINT previousPersistentCount,
        HeapPolicy previousPolicy) {
    if (framesInFlight == 0 || capacity == 0) return {};
    const UINT sliceSize = NullSrvSliceSize(framesInFlight, capacity);
    if (policy == HeapPolicy::RebuildEachFrame) {
        const UINT count = (std::min)(requestedCount, sliceSize);
        return {(frameIndex % framesInFlight) * sliceSize, count, count, 0};
    }
    const UINT persistentFirst = NullSrvPersistentFirst(framesInFlight, capacity);
    const UINT persistentRoom  = capacity - persistentFirst;
    const UINT count = (std::min)(requestedCount, persistentRoom);
    const bool changed = previousPolicy != policy ||
                         previousPersistentCount != count;
    return {persistentFirst, count, changed ? count : 0, count};
}

class HeapManager {
public:
    virtual ~HeapManager() = default;

    void Init(ID3D12Device* device, UINT capacity);

    ID3D12DescriptorHeap* SrvHeap() { return srvHeap_.Get(); }
    // 유효한 null Texture2D SRV를 작성하고 실제 write 수를 반환한다.
    // Rebuild 모드는 frame slot별 구간을 사용하므로 in-flight descriptor를
    // 덮어쓰지 않는다. Persistent 모드는 count/policy가 바뀔 때만 다시 쓴다.
    UINT PrepareNullSrvWorkload(ID3D12Device* device,
                                HeapPolicy policy,
                                UINT frameIndex,
                                UINT framesInFlight,
                                UINT requestedCount);

protected:
    // 실제 descriptor 작성. 파생 클래스가 대체할 수 있게 virtual 로 둔다 —
    // 헤드리스 검증은 이 지점을 가로채 PrepareNullSrvWorkload 가 계획대로
    // 호출하는지 확인한다.
    virtual void WriteNullSrvs(ID3D12Device* device, UINT first, UINT count);

    // 헤드리스 초기화 — 실제 heap 을 만들지 않고 용량/증가분만 설정한다.
    void InitHeadless(UINT capacity, UINT increment) {
        capacity_  = capacity;
        increment_ = increment;
    }

private:
    ComPtr<ID3D12DescriptorHeap> srvHeap_;
    UINT increment_ = 0;
    UINT capacity_  = 0;
    UINT persistentCount_ = 0;
    HeapPolicy lastPolicy_ = HeapPolicy::RebuildEachFrame;
};

}  // namespace dxlab
