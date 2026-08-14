// 단순 라인 차트. 시간 시리즈 (원형 버퍼) 를 여러 개 같은 축에 겹쳐 그린다.
// physics_lab 의 에너지 그래프 / ms 그래프 / 후보 쌍 수 그래프 등이
// 이 위젯 위에 올라간다.
//
// GraphSeries 는 *링 버퍼* — Push 가 가장 오래된 값을 덮어쓴다. DrawLineGraph
// 는 *가장 오래된 → 가장 최신* 순으로 N 개의 값을 좌→우로 그린다.

#pragma once

#include "raylib.h"

#include <vector>

namespace physlab {

class GraphSeries {
public:
    explicit GraphSeries(int capacity = 600) { data_.assign(capacity, 0.0f); }

    void Reset(int capacity);
    void Push(float v);
    void Clear();

    int   Count()    const { return count_; }
    int   Capacity() const { return (int)data_.size(); }
    float operator[](int i) const;  // 0 = 가장 오래된, Count()-1 = 가장 최신
    float Latest() const;            // 비어 있으면 0

    float Min() const;
    float Max() const;

private:
    std::vector<float> data_;
    int                writeIdx_ = 0;
    int                count_    = 0;
};

struct GraphInput {
    const GraphSeries* series;
    Color              color;
    const char*        label;     // nullptr 이면 범례 생략
};

// rect 안에 라인 그래프를 그린다.
//   inputs: 시리즈들 — 같은 capacity 라고 가정 (다르면 가장 짧은 것에 맞춤).
//   title: 그래프 위에 작게.
//   yLo, yHi: y 축 범위. autoY true 면 무시되고 모든 시리즈의 min/max 로 설정.
void DrawLineGraph(Rectangle rect,
                    const GraphInput* inputs, int count,
                    const char* title,
                    float yLo, float yHi,
                    bool autoY);

}  // namespace physlab
