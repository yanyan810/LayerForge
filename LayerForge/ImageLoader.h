#pragma once

#include "ImageData.h"

#include <string>

class ImageLoader {
public:
    bool Load(const std::filesystem::path& path, ImageData& output, std::string& error) const;
};
