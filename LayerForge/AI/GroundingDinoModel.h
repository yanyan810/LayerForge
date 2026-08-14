#pragma once

#include "DetectionBox.h"
#include "InferenceDevice.h"
#include "../ImageData.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Ort { struct Env; struct Session; }

class GroundingDinoModel {
public:
    GroundingDinoModel();
    ~GroundingDinoModel();
    GroundingDinoModel(const GroundingDinoModel&) = delete;
    GroundingDinoModel& operator=(const GroundingDinoModel&) = delete;

    bool Load(const std::filesystem::path& modelPath, InferenceDevice device, std::string& error);
    bool Run(const ImageData& image, const DetectionBox& region, std::vector<DetectionBox>& boxes,
        double& inferenceMilliseconds, std::string& error);
    void Reset();
    [[nodiscard]] bool IsLoaded() const noexcept;
    [[nodiscard]] InferenceProvider Provider() const noexcept { return provider_; }
    [[nodiscard]] const std::string& ProviderWarning() const noexcept { return providerWarning_; }

private:
    std::unique_ptr<Ort::Env> environment_;
    std::unique_ptr<Ort::Session> session_;
    std::vector<std::string> inputNames_;
    std::vector<std::string> outputNames_;
    std::vector<float> inputPixels_;
    std::vector<uint8_t> inputMask_;
    InferenceProvider provider_ = InferenceProvider::Cpu;
    std::string providerWarning_;
};
