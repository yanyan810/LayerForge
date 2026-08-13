#pragma once

#include "MaskData.h"
#include "../ImageData.h"

#include <filesystem>
#include <memory>
#include <string>

namespace Ort { struct Env; struct Session; }

class SegmentationModel {
public:
    SegmentationModel();
    ~SegmentationModel();
    SegmentationModel(const SegmentationModel&) = delete;
    SegmentationModel& operator=(const SegmentationModel&) = delete;

    bool Load(const std::filesystem::path& modelPath, std::string& error);
    bool Run(const ImageData& image, MaskData& mask, double& inferenceMilliseconds, std::string& error);
    void Reset();
    [[nodiscard]] bool IsLoaded() const noexcept;

private:
    std::unique_ptr<Ort::Env> environment_;
    std::unique_ptr<Ort::Session> session_;
    std::string inputName_;
    std::string outputName_;
};
