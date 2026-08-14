#pragma once

// D3D12 Debug Layer 메시지 콜백 — Step 2 의 디버그 로그 창.
//
// `ID3D12InfoQueue1::RegisterMessageCallback` 으로 매 디버그 레이어 메시지 후킹.

#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <string>

namespace dxlab {

template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

struct DebugMessage {
    enum class Severity { Notification, Low, Medium, High, Corruption };
    UINT64 id;
    Severity severity;
    std::string text;
};

class D3D12Debug {
public:
    static constexpr int kRingSize = 256;

    ~D3D12Debug();

    void Install(ID3D12Device* device);
    // 콜백 컨텍스트가 `this` 이므로 이 객체가 파괴되기 전에 반드시 등록을
    // 해제해야 한다. 멤버 선언 순서에 의존하지 않도록 명시적으로 호출한다.
    void Uninstall();
    void Clear();

    int Count() const { return count_; }
    int RingHead() const { return head_; }
    const DebugMessage& Get(int i) const { return ring_[(size_t)i]; }

private:
    static void __stdcall Callback(D3D12_MESSAGE_CATEGORY category,
                                    D3D12_MESSAGE_SEVERITY severity,
                                    D3D12_MESSAGE_ID id,
                                    LPCSTR description,
                                    void* context);
    void Push(const DebugMessage& m);

    ComPtr<ID3D12InfoQueue1> infoQueue_;
    DWORD cookie_ = 0;
    std::array<DebugMessage, kRingSize> ring_{};
    int head_  = 0;
    int count_ = 0;
};

}  // namespace dxlab
