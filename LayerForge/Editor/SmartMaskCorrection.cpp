#include "SmartMaskCorrection.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace {
float Distance(const SmartPoint& a, const SmartPoint& b) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    return std::sqrt(dx * dx + dy * dy);
}
}

bool SmartMaskCorrection::BeginStroke(float x, float y, float brushSize, SmartCorrectionMode mode,
    uint32_t imageWidth, uint32_t imageHeight) {
    if (active_ || imageWidth == 0 || imageHeight == 0 || x < 0.0f || y < 0.0f || x >= imageWidth || y >= imageHeight) return false;
    stroke_.clear(); stroke_.push_back({ x, y }); brushSize_ = std::max(1.0f, brushSize);
    mode_ = mode; imageWidth_ = imageWidth; imageHeight_ = imageHeight; active_ = true;
    return true;
}

bool SmartMaskCorrection::ContinueStroke(float x, float y) {
    if (!active_ || x < 0.0f || y < 0.0f || x >= imageWidth_ || y >= imageHeight_) return false;
    const SmartPoint point{ x, y };
    if (Distance(stroke_.back(), point) < 1.0f) return false;
    stroke_.push_back(point); return true;
}

bool SmartMaskCorrection::EndStroke(SmartStrokeRequest& request) {
    if (!active_ || stroke_.empty()) return false;
    const auto start = std::chrono::steady_clock::now();
    active_ = false; request = {};
    request.mode = mode_; request.stroke = stroke_; request.brushSize = brushSize_;
    request.prompts = SamplePrompts(); request.roi = BuildRoi();
    request.promptMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    return !request.prompts.empty() && request.roi.IsValid();
}

void SmartMaskCorrection::CancelStroke() {
    active_ = false; stroke_.clear(); imageWidth_ = imageHeight_ = 0;
}

std::vector<SmartPoint> SmartMaskCorrection::SamplePrompts() const {
    if (stroke_.empty()) return {};
    std::vector<float> cumulative(stroke_.size());
    for (size_t index = 1; index < stroke_.size(); ++index)
        cumulative[index] = cumulative[index - 1] + Distance(stroke_[index - 1], stroke_[index]);
    const float length = cumulative.back();
    int count = length < brushSize_ * 1.5f ? 1 : (length < brushSize_ * 4.0f ? 2 : 3);
    const float minimumSpacing = std::max(16.0f, brushSize_ * 0.75f);
    constexpr float fractions[3][3]{ { 0.50f, 0.0f, 0.0f }, { 0.25f, 0.75f, 0.0f }, { 0.15f, 0.50f, 0.85f } };
    std::vector<SmartPoint> result;
    for (int pointIndex = 0; pointIndex < count; ++pointIndex) {
        const float target = length * fractions[count - 1][pointIndex];
        auto upper = std::lower_bound(cumulative.begin(), cumulative.end(), target);
        const size_t upperIndex = static_cast<size_t>(std::distance(cumulative.begin(), upper));
        SmartPoint point = stroke_[upperIndex];
        if (upperIndex > 0 && cumulative[upperIndex] > cumulative[upperIndex - 1]) {
            const float amount = (target - cumulative[upperIndex - 1]) /
                (cumulative[upperIndex] - cumulative[upperIndex - 1]);
            point.x = stroke_[upperIndex - 1].x + (stroke_[upperIndex].x - stroke_[upperIndex - 1].x) * amount;
            point.y = stroke_[upperIndex - 1].y + (stroke_[upperIndex].y - stroke_[upperIndex - 1].y) * amount;
        }
        if (result.empty() || Distance(result.back(), point) >= minimumSpacing) result.push_back(point);
    }
    if (result.empty()) result.push_back(stroke_[stroke_.size() / 2]);
    return result;
}

SmartCorrectionRoi SmartMaskCorrection::BuildRoi() const {
    if (stroke_.empty()) return {};
    float minimumX = stroke_[0].x, maximumX = stroke_[0].x;
    float minimumY = stroke_[0].y, maximumY = stroke_[0].y;
    for (const SmartPoint& point : stroke_) {
        minimumX = std::min(minimumX, point.x); maximumX = std::max(maximumX, point.x);
        minimumY = std::min(minimumY, point.y); maximumY = std::max(maximumY, point.y);
    }
    const float margin = std::max(48.0f, brushSize_ * 1.5f);
    SmartCorrectionRoi roi;
    roi.x1 = static_cast<uint32_t>(std::max(0.0f, std::floor(minimumX - margin)));
    roi.y1 = static_cast<uint32_t>(std::max(0.0f, std::floor(minimumY - margin)));
    roi.x2 = static_cast<uint32_t>(std::min(static_cast<float>(imageWidth_), std::ceil(maximumX + margin + 1.0f)));
    roi.y2 = static_cast<uint32_t>(std::min(static_cast<float>(imageHeight_), std::ceil(maximumY + margin + 1.0f)));
    return roi;
}

bool SmartMaskCorrection::BuildCandidate(const MaskData& current, const MaskData& samMask,
    const SmartCorrectionRoi& roi, SmartCorrectionMode mode, MaskData& candidate) {
    if (!current.IsValid() || !samMask.IsValid() || current.width != samMask.width || current.height != samMask.height ||
        !roi.IsValid() || roi.x2 > current.width || roi.y2 > current.height) return false;
    candidate = current;
    for (uint32_t y = roi.y1; y < roi.y2; ++y) for (uint32_t x = roi.x1; x < roi.x2; ++x) {
        const size_t index = static_cast<size_t>(y) * current.width + x;
        candidate.grayscale[index] = mode == SmartCorrectionMode::Add ?
            std::max(current.grayscale[index], samMask.grayscale[index]) :
            std::min(current.grayscale[index], samMask.grayscale[index]);
    }
    return true;
}
