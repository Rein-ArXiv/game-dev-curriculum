#pragma once

// QueryRing — timestamp query pool 의 링 인덱싱 상태 기계.
//
// TimestampQuery 에서 VkDevice 에 의존하지 않는 부분만 떼어낸 것이다. GPU 가
// 아직 결과를 내놓지 않은 슬롯이 kRingSize 개 쌓이면 새 기록을 건너뛰고, 수확은
// 가장 오래된 슬롯부터 한다. 이 규칙이 깨지면 이전 프레임 타임스탬프를 덮어쓰거나
// 아직 쓰이지 않은 슬롯을 읽게 된다.

namespace vklab {

class QueryRing {
public:
    static constexpr int kRingSize = 3;

    // 기록 슬롯을 연다. ring 이 가득 차면 -1 (이 프레임은 측정하지 않는다).
    int Begin();

    // 열린 슬롯을 닫는다. 닫은 슬롯 인덱스, 열려 있지 않았으면 -1.
    int End();

    // 수확 대상 — 가장 오래된 미수확 슬롯. 없으면 -1.
    int ReadIndex() const;

    // 수확 성공. 미수확 개수를 하나 줄인다.
    void Retire();

    bool Recording() const { return recording_; }
    int  InFlight()  const { return inFlight_; }
    int  WriteIndex() const { return writeIdx_; }

private:
    int  writeIdx_  = 0;
    int  inFlight_  = 0;
    bool recording_ = false;
};

}  // namespace vklab
