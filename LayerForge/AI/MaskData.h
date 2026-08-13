#pragma once

#include <cstdint>
#include <vector>

struct MaskData {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> grayscale;

    [[nodiscard]] bool IsValid() const noexcept {
        return width > 0 && height > 0 && grayscale.size() == static_cast<size_t>(width) * height;
    }
};
