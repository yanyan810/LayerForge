#pragma once

#include "DetectionBox.h"
#include "MaskData.h"

#include <optional>
#include <vector>

class HairBoxFilter {
public:
    static std::optional<DetectionBox> CharacterBounds(const MaskData& characterMask);
    static DetectionBox ExpandAndClamp(const DetectionBox& box, float margin, uint32_t width, uint32_t height);
    std::optional<DetectionBox> Select(const std::vector<DetectionBox>& candidates, const MaskData& characterMask,
        const DetectionBox& characterBounds, const DetectionBox& cropBounds, std::vector<DetectionBox>* accepted = nullptr) const;

private:
    static float IntersectionOverUnion(const DetectionBox& a, const DetectionBox& b);
};
