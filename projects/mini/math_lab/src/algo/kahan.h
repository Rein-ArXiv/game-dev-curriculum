// Kahan 보상 합산 — math part20 (수치 안정성) 의 absorption 회복.
//
// float 로 큰 수에 작은 수를 반복해 더하면 작은 값이 큰 값의 ULP 아래로
// 묻혀 통째로 사라진다. Kahan 은 매 항에서 잘려나간 하위 비트를 보상항 c 에
// 담아 다음 항에 되돌린다.

#pragma once

namespace mathlab {

// 한 항씩 누적하는 보상 합산기. 시각화(오차 시계열)와 순수 함수가 같은
// 누적 규칙을 쓰도록 이 클래스 하나만 둔다.
class KahanAccumulator {
public:
    KahanAccumulator() = default;
    explicit KahanAccumulator(float initial) : sum_(initial) {}

    void Add(float x) {
        float y = x - c_;        // 지난번에 잘린 몫을 되돌린다
        float t = sum_ + y;
        c_      = (t - sum_) - y;  // 이번에 잘린 몫
        sum_    = t;
    }

    float Sum() const { return sum_; }
    float Compensation() const { return c_; }

private:
    float sum_ = 0.0f;
    float c_   = 0.0f;
};

// big 에서 시작해 small 을 n 회 더한 단순 float 합산.
inline float NaiveSum(float big, float small, int n) {
    float sum = big;
    for (int i = 0; i < n; ++i) sum += small;
    return sum;
}

// 같은 입력의 Kahan 보상 합산.
inline float KahanSum(float big, float small, int n) {
    KahanAccumulator acc(big);
    for (int i = 0; i < n; ++i) acc.Add(small);
    return acc.Sum();
}

}  // namespace mathlab
