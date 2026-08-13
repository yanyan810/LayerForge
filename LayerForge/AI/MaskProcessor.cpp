#include "MaskProcessor.h"

#include <algorithm>
#include <cmath>

namespace {
float Smoothstep(float lower, float upper, float value) {
    if (upper <= lower) return value >= upper ? 1.0f : 0.0f;
    const float t = std::clamp((value - lower) / (upper - lower), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
}

bool MaskProcessor::Process(const MaskData& rawMask, const ImageData& original, const MaskAdjustmentSettings& settings,
    MaskData& adjustedMask, ImageData& foreground, ImageData& background, std::string& error) const {
    error.clear();
    if (!rawMask.IsValid()) { error = "Raw Mask is not available."; return false; }
    if (!original.IsValid()) { error = "Original image data is invalid."; return false; }
    if (rawMask.width != original.width || rawMask.height != original.height) {
        error = "Raw Mask dimensions do not match the original image.";
        return false;
    }

    const float threshold = std::clamp(settings.threshold, 0.0f, 1.0f);
    const float softness = std::clamp(settings.edgeSoftness, 0.0f, 0.5f);
    const float lower = std::max(0.0f, threshold - softness);
    const float upper = std::min(1.0f, threshold + softness);
    const size_t pixelCount = rawMask.grayscale.size();
    uint8_t alphaLookup[256]{};
    for (size_t value = 0; value < 256; ++value) {
        alphaLookup[value] = static_cast<uint8_t>(Smoothstep(lower, upper, static_cast<float>(value) / 255.0f) * 255.0f + 0.5f);
    }

    adjustedMask.width = rawMask.width;
    adjustedMask.height = rawMask.height;
    adjustedMask.grayscale.resize(pixelCount);
    foreground.width = background.width = original.width;
    foreground.height = background.height = original.height;
    foreground.rgbaPixels.resize(pixelCount * 4);
    background.rgbaPixels.resize(pixelCount * 4);

    for (size_t index = 0; index < pixelCount; ++index) {
        const uint8_t alpha = alphaLookup[rawMask.grayscale[index]];
        adjustedMask.grayscale[index] = alpha;

        const size_t rgba = index * 4;
        foreground.rgbaPixels[rgba] = background.rgbaPixels[rgba] = original.rgbaPixels[rgba];
        foreground.rgbaPixels[rgba + 1] = background.rgbaPixels[rgba + 1] = original.rgbaPixels[rgba + 1];
        foreground.rgbaPixels[rgba + 2] = background.rgbaPixels[rgba + 2] = original.rgbaPixels[rgba + 2];
        foreground.rgbaPixels[rgba + 3] = alpha;
        background.rgbaPixels[rgba + 3] = static_cast<uint8_t>(255 - alpha);
    }
    return true;
}
