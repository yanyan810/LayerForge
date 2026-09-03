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
    bool Adjust(const MaskData& rawMask, const MaskAdjustmentSettings& settings,
        MaskData& adjustedMask, std::string& error) const;
    bool Process(const MaskData& rawMask, const ImageData& original, const MaskAdjustmentSettings& settings,
        MaskData& adjustedMask, ImageData& foreground, ImageData& background, std::string& error) const;
    bool Compose(const MaskData& mask, const ImageData& original,
        ImageData& foreground, ImageData& background, std::string& error) const;
};
