#pragma once

#include <algorithm>

struct DetectionBox {
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;
    float confidence = 0.0f;
    float rankScore = 0.0f;
    float characterInsideRatio = 0.0f;
    float outsideRatio = 1.0f;
    float characterAreaRatio = 0.0f;
    float cropAreaRatio = 0.0f;
    float headPrior = 0.0f;

    [[nodiscard]] float Width() const noexcept { return std::max(0.0f, x2 - x1); }
    [[nodiscard]] float Height() const noexcept { return std::max(0.0f, y2 - y1); }
    [[nodiscard]] float Area() const noexcept { return Width() * Height(); }
    [[nodiscard]] bool IsValid() const noexcept { return x2 > x1 && y2 > y1; }
};
