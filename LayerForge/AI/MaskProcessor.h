#pragma once

#include "MaskData.h"
#include "../ImageData.h"

#include <string>

struct MaskAdjustmentSettings {
    static constexpr float DefaultThreshold = 0.5f;
    static constexpr float DefaultEdgeSoftness = 0.05f;

    float threshold = DefaultThreshold;
    float edgeSoftness = DefaultEdgeSoftness;

    void Reset() noexcept {
        threshold = DefaultThreshold;
        edgeSoftness = DefaultEdgeSoftness;
    }
};

class MaskProcessor {
public:
    bool Process(const MaskData& rawMask, const ImageData& original, const MaskAdjustmentSettings& settings,
        MaskData& adjustedMask, ImageData& foreground, ImageData& background, std::string& error) const;
};
