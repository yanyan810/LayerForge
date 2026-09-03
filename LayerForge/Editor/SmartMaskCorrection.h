#pragma once

#include "../AI/MaskData.h"

#include <cstdint>
#include <vector>

enum class SmartCorrectionMode { Add, Erase };

struct SmartPoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct SmartCorrectionRoi {
    uint32_t x1 = 0;
    uint32_t y1 = 0;
    uint32_t x2 = 0;
    uint32_t y2 = 0;
    [[nodiscard]] bool IsValid() const noexcept { return x2 > x1 && y2 > y1; }
};

struct SmartStrokeRequest {
    SmartCorrectionMode mode = SmartCorrectionMode::Add;
    std::vector<SmartPoint> prompts;
    std::vector<SmartPoint> stroke;
    SmartCorrectionRoi roi;
    float brushSize = 1.0f;
    double promptMilliseconds = 0.0;
};

class SmartMaskCorrection {
public:
    bool BeginStroke(float x, float y, float brushSize, SmartCorrectionMode mode,
        uint32_t imageWidth, uint32_t imageHeight);
    bool ContinueStroke(float x, float y);
    bool EndStroke(SmartStrokeRequest& request);
    void CancelStroke();

    [[nodiscard]] bool IsStrokeActive() const noexcept { return active_; }
    [[nodiscard]] const std::vector<SmartPoint>& StrokePreview() const noexcept { return stroke_; }
    [[nodiscard]] SmartCorrectionMode Mode() const noexcept { return mode_; }
    [[nodiscard]] float BrushSize() const noexcept { return brushSize_; }

    static bool BuildCandidate(const MaskData& current, const MaskData& samMask,
        const SmartCorrectionRoi& roi, SmartCorrectionMode mode, MaskData& candidate);

private:
    std::vector<SmartPoint> SamplePrompts() const;
    SmartCorrectionRoi BuildRoi() const;

    std::vector<SmartPoint> stroke_;
    float brushSize_ = 1.0f;
    SmartCorrectionMode mode_ = SmartCorrectionMode::Add;
    uint32_t imageWidth_ = 0;
    uint32_t imageHeight_ = 0;
    bool active_ = false;
};
