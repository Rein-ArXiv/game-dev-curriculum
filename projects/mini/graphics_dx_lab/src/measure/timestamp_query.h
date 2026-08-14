#pragma once

// TimestampQuery — DX12 의 GPU 시간 측정.
//
// `ID3D12QueryHeap` 의 TIMESTAMP 쿼리 한 쌍 (begin/end) 을 매 프레임 발급.
// `GetTimestampFrequency` 로 사이클 → ms 변환.
//
// Ring buffer + fence 패턴으로 CPU stall 없이 완료된 결과만 회수.

#include <d3d12.h>
#include <wrl/client.h>

#include <array>

namespace dxlab {

template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

// Ring slot 판정 — slot fence 값이 0 이면 아직 제출되지 않았으므로 준비되지
// 않은 것이고, 그 외에는 GPU 완료값이 slot fence 값 이상일 때만 준비된 것이다.
// Begin(덮어쓰기 전 확인)과 Poll(회수 전 확인)이 같은 판정을 쓴다.
inline bool IsSlotReady(UINT64 completedValue, UINT64 slotFenceValue) {
    return slotFenceValue != 0 && completedValue >= slotFenceValue;
}

class TimestampQuery {
public:
    static constexpr int kRingSize    = 3;
    static constexpr int kHistorySize = 240;

    void Init(ID3D12Device* device, ID3D12CommandQueue* queue);
    void Shutdown();

    // 헤드리스 초기화 — 디바이스 없이 이미 만들어진(또는 대역) 객체를 물린다.
    // Init 과 같은 초기 상태를 만들되 자원 생성만 하지 않는다.
    void InitHeadless(ID3D12QueryHeap* heap, ID3D12Resource* readback,
                      ID3D12CommandQueue* queue, ID3D12Fence* fence,
                      UINT64 frequency);

    void Begin(ID3D12GraphicsCommandList* cmd);
    void End(ID3D12GraphicsCommandList* cmd);

    // 명령 목록을 queue 에 제출한 뒤 호출한다. 이 signal 값으로 해당
    // readback slot 의 GPU 완료 여부를 판정한다.
    void SignalSubmitted();

    // 가장 오래된 완료 slot 의 결과를 회수한다. 미완료면 기다리지 않는다.
    void Poll();

    double LastMs() const { return lastMs_; }
    double EmaMs()  const { return emaMs_; }
    const std::array<float, kHistorySize>& History() const { return history_; }
    int HistoryHead() const { return historyHead_; }

    // 링 상태 읽기 — 검증이 slot/fence 상태 기계를 관측하기 위한 접근자.
    UINT64 SlotFence(int slot) const { return slotFences_[(size_t)slot]; }
    int WriteIndex()  const { return writeIdx_; }
    int ReadIndex()   const { return readIdx_; }
    int ActiveSlot()  const { return activeSlot_; }

private:
    ComPtr<ID3D12QueryHeap>     heap_;
    ComPtr<ID3D12Resource>      readback_;
    ComPtr<ID3D12CommandQueue>  queue_;
    ComPtr<ID3D12Fence>         fence_;
    UINT64                      frequency_ = 0;
    UINT64                      nextFenceValue_ = 0;
    std::array<UINT64, kRingSize> slotFences_{};
    int  writeIdx_ = 0;
    int  readIdx_ = 0;
    int  activeSlot_ = -1;
    int  submittedSlot_ = -1;

    double lastMs_ = 0.0;
    double emaMs_  = 0.0;
    static constexpr double kEmaAlpha = 0.1;
    std::array<float, kHistorySize> history_{};
    int historyHead_ = 0;
};

}  // namespace dxlab
