#include "HairBoxFilter.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr uint8_t CharacterMaskCutoff = 128;
constexpr float MaximumCropAreaRatio = 0.40f;
constexpr float GiantCharacterAreaRatio = 0.60f;
constexpr float GiantCropAreaRatio = 0.20f;
constexpr float AbsoluteCharacterAreaRatio = 1.15f;
constexpr float MaximumOutsideRatio = 0.45f;
constexpr float NmsThreshold = 0.65f;

float IntersectionArea(const DetectionBox& a, const DetectionBox& b) {
    return std::max(0.0f, std::min(a.x2, b.x2) - std::max(a.x1, b.x1)) *
        std::max(0.0f, std::min(a.y2, b.y2) - std::max(a.y1, b.y1));
}
}

std::optional<DetectionBox> HairBoxFilter::CharacterBounds(const MaskData& mask) {
    if (!mask.IsValid()) return std::nullopt;
    uint32_t minimumX = mask.width, minimumY = mask.height, maximumX = 0, maximumY = 0;
    bool found = false;
    for (uint32_t y = 0; y < mask.height; ++y) {
        for (uint32_t x = 0; x < mask.width; ++x) {
            if (mask.grayscale[static_cast<size_t>(y) * mask.width + x] < CharacterMaskCutoff) continue;
            minimumX = std::min(minimumX, x); minimumY = std::min(minimumY, y);
            maximumX = std::max(maximumX, x); maximumY = std::max(maximumY, y); found = true;
        }
    }
    if (!found) return std::nullopt;
    DetectionBox result;
    result.x1 = static_cast<float>(minimumX); result.y1 = static_cast<float>(minimumY);
    result.x2 = static_cast<float>(maximumX + 1); result.y2 = static_cast<float>(maximumY + 1);
    return result;
}

DetectionBox HairBoxFilter::ExpandAndClamp(const DetectionBox& box, float margin, uint32_t width, uint32_t height) {
    const float expandX = box.Width() * margin, expandY = box.Height() * margin;
    DetectionBox result = box;
    result.x1 = std::clamp(box.x1 - expandX, 0.0f, static_cast<float>(width));
    result.y1 = std::clamp(box.y1 - expandY, 0.0f, static_cast<float>(height));
    result.x2 = std::clamp(box.x2 + expandX, 0.0f, static_cast<float>(width));
    result.y2 = std::clamp(box.y2 + expandY, 0.0f, static_cast<float>(height));
    return result;
}

float HairBoxFilter::IntersectionOverUnion(const DetectionBox& a, const DetectionBox& b) {
    const float intersection = IntersectionArea(a, b);
    const float combined = a.Area() + b.Area() - intersection;
    return combined > 0.0f ? intersection / combined : 0.0f;
}

std::optional<DetectionBox> HairBoxFilter::Select(const std::vector<DetectionBox>& candidates, const MaskData& mask,
    const DetectionBox& character, const DetectionBox& crop, std::vector<DetectionBox>* accepted) const {
    if (accepted) accepted->clear();
    if (!mask.IsValid() || !character.IsValid() || !crop.IsValid()) return std::nullopt;
    const float characterArea = character.Area(), cropArea = crop.Area();
    if (characterArea <= 0.0f || cropArea <= 0.0f) return std::nullopt;

    std::vector<DetectionBox> ranked;
    for (DetectionBox box : candidates) {
        box.x1 = std::clamp(box.x1, 0.0f, static_cast<float>(mask.width));
        box.y1 = std::clamp(box.y1, 0.0f, static_cast<float>(mask.height));
        box.x2 = std::clamp(box.x2, 0.0f, static_cast<float>(mask.width));
        box.y2 = std::clamp(box.y2, 0.0f, static_cast<float>(mask.height));
        if (!box.IsValid() || box.confidence < 0.30f) continue;

        box.cropAreaRatio = box.Area() / cropArea;
        box.characterAreaRatio = box.Area() / characterArea;
        if (box.cropAreaRatio >= MaximumCropAreaRatio || box.characterAreaRatio > AbsoluteCharacterAreaRatio ||
            (box.characterAreaRatio >= GiantCharacterAreaRatio && box.cropAreaRatio >= GiantCropAreaRatio)) continue;

        // Phase 4D's character prior is geometric: loose hair/effects may be absent from U2NETP's
        // thresholded pixels, so using pixel occupancy here would incorrectly punish the narrow box.
        box.characterInsideRatio = IntersectionArea(box, character) / box.Area();
        box.outsideRatio = 1.0f - box.characterInsideRatio;
        if (box.outsideRatio > MaximumOutsideRatio) continue;

        const float centerY = (box.y1 + box.y2) * 0.5f;
        const float relativeCenterY = (centerY - character.y1) / std::max(1.0f, character.Height());
        box.headPrior = std::clamp(1.0f - relativeCenterY / 0.65f, 0.0f, 1.0f);
        box.rankScore = box.confidence + 0.25f * box.characterInsideRatio + 0.12f * box.headPrior -
            0.40f * box.characterAreaRatio - 0.25f * box.outsideRatio;
        ranked.push_back(box);
    }
    std::sort(ranked.begin(), ranked.end(), [](const DetectionBox& a, const DetectionBox& b) { return a.rankScore > b.rankScore; });

    std::vector<DetectionBox> deduplicated;
    for (const DetectionBox& candidate : ranked) {
        const bool duplicate = std::any_of(deduplicated.begin(), deduplicated.end(), [&](const DetectionBox& kept) {
            return IntersectionOverUnion(candidate, kept) >= NmsThreshold;
        });
        if (!duplicate) deduplicated.push_back(candidate);
    }
    if (accepted) *accepted = deduplicated;
    if (deduplicated.empty()) return std::nullopt;
    return deduplicated.front();
}
