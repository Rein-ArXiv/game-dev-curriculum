#include "ui/line_graph.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace physlab {

void GraphSeries::Reset(int capacity) {
    data_.assign(capacity, 0.0f);
    writeIdx_ = 0;
    count_    = 0;
}

void GraphSeries::Push(float v) {
    if (data_.empty()) return;
    data_[(size_t)writeIdx_] = v;
    writeIdx_                = (writeIdx_ + 1) % (int)data_.size();
    if (count_ < (int)data_.size()) ++count_;
}

void GraphSeries::Clear() {
    writeIdx_ = 0;
    count_    = 0;
}

float GraphSeries::operator[](int i) const {
    if (count_ == 0) return 0.0f;
    int cap   = (int)data_.size();
    int start = (writeIdx_ - count_ + cap) % cap;  // 가장 오래된 원소 인덱스
    return data_[(size_t)((start + i) % cap)];
}

float GraphSeries::Latest() const {
    if (count_ == 0) return 0.0f;
    int cap = (int)data_.size();
    int idx = (writeIdx_ - 1 + cap) % cap;
    return data_[(size_t)idx];
}

float GraphSeries::Min() const {
    if (count_ == 0) return 0.0f;
    float m = (*this)[0];
    for (int i = 1; i < count_; ++i) m = std::min(m, (*this)[i]);
    return m;
}

float GraphSeries::Max() const {
    if (count_ == 0) return 0.0f;
    float m = (*this)[0];
    for (int i = 1; i < count_; ++i) m = std::max(m, (*this)[i]);
    return m;
}

namespace {

constexpr Color kAxisColor   = {60, 64, 72, 255};
constexpr Color kBgColor     = {16, 18, 24, 255};
constexpr Color kBorderColor = {48, 52, 60, 255};
constexpr Color kLabelColor  = {180, 184, 192, 255};
constexpr Color kTitleColor  = {220, 224, 232, 255};

}  // namespace

void DrawLineGraph(Rectangle rect,
                    const GraphInput* inputs, int count,
                    const char* title,
                    float yLo, float yHi,
                    bool autoY) {
    DrawRectangleRec(rect, kBgColor);
    DrawRectangleLinesEx(rect, 1.0f, kBorderColor);

    // 제목.
    if (title) {
        DrawText(title, (int)rect.x + 6, (int)rect.y + 4, 12, kTitleColor);
    }

    // 본문 영역 (제목 / 범례 공간 제외).
    const float padTop    = title ? 20.0f : 6.0f;
    const float padBottom = 18.0f;        // 0 선 라벨용
    const float padLeft   = 38.0f;        // y 축 라벨
    const float padRight  = 6.0f;

    Rectangle plot = {
        rect.x + padLeft,
        rect.y + padTop,
        rect.width - padLeft - padRight,
        rect.height - padTop - padBottom,
    };
    if (plot.width <= 0 || plot.height <= 0) return;

    // y 범위 결정.
    if (autoY) {
        float mn = std::numeric_limits<float>::infinity();
        float mx = -std::numeric_limits<float>::infinity();
        for (int s = 0; s < count; ++s) {
            const auto& ser = *inputs[s].series;
            if (ser.Count() == 0) continue;
            mn = std::min(mn, ser.Min());
            mx = std::max(mx, ser.Max());
        }
        if (!std::isfinite(mn) || !std::isfinite(mx)) {
            mn = -1.0f; mx = 1.0f;
        }
        if (mx - mn < 1e-6f) { mn -= 0.5f; mx += 0.5f; }
        float margin = (mx - mn) * 0.1f;
        yLo = mn - margin;
        yHi = mx + margin;
    }
    if (yHi - yLo < 1e-6f) yHi = yLo + 1.0f;

    // y=0 가로 가이드.
    if (yLo <= 0.0f && yHi >= 0.0f) {
        float t  = (0.0f - yLo) / (yHi - yLo);
        float yz = plot.y + (1.0f - t) * plot.height;
        DrawLine((int)plot.x, (int)yz,
                  (int)(plot.x + plot.width), (int)yz,
                  kAxisColor);
    }

    // 좌측 y 축 라벨 (상 / 하).
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2g", yHi);
    DrawText(buf, (int)rect.x + 4, (int)plot.y - 2, 10, kLabelColor);
    std::snprintf(buf, sizeof(buf), "%.2g", yLo);
    DrawText(buf, (int)rect.x + 4,
              (int)(plot.y + plot.height - 10), 10, kLabelColor);

    // 시리즈 그리기.
    int   capacity = 0;
    for (int s = 0; s < count; ++s) {
        capacity = std::max(capacity, inputs[s].series->Capacity());
    }
    if (capacity <= 1) return;

    for (int s = 0; s < count; ++s) {
        const auto& ser = *inputs[s].series;
        int          n  = ser.Count();
        if (n < 2) continue;

        float lastX = plot.x;
        float lastY = 0.0f;
        for (int i = 0; i < n; ++i) {
            float v  = ser[i];
            float tx = (float)i / (float)(capacity - 1);
            float ty = (v - yLo) / (yHi - yLo);
            ty       = std::clamp(ty, 0.0f, 1.0f);
            float px = plot.x + tx * plot.width;
            float py = plot.y + (1.0f - ty) * plot.height;
            if (i > 0) {
                DrawLineEx(Vector2{lastX, lastY}, Vector2{px, py},
                            1.5f, inputs[s].color);
            }
            lastX = px;
            lastY = py;
        }
    }

    // 범례 (rect 우상단부터 가로로).
    float lx = rect.x + rect.width - padRight;
    float ly = rect.y + 4.0f;
    for (int s = count - 1; s >= 0; --s) {
        if (!inputs[s].label) continue;
        int  tw = MeasureText(inputs[s].label, 10);
        float swatch = 8.0f;
        lx -= (tw + swatch + 8.0f);
        DrawRectangle((int)lx, (int)ly + 1, (int)swatch, (int)swatch,
                       inputs[s].color);
        DrawText(inputs[s].label, (int)(lx + swatch + 4), (int)ly, 10,
                  kLabelColor);
    }
}

}  // namespace physlab
