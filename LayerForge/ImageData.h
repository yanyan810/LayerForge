#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct ImageData {
    std::filesystem::path path;
    std::string fileNameUtf8;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgbaPixels;

    [[nodiscard]] bool IsValid() const noexcept {
        return width > 0 && height > 0 && rgbaPixels.size() == static_cast<size_t>(width) * height * 4;
    }
};
