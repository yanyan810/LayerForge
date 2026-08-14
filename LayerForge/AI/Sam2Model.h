#pragma once

#include "DetectionBox.h"
#include "InferenceDevice.h"
#include "MaskData.h"
#include "../ImageData.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Ort { struct Env; struct Session; }

struct Sam2Timings {
    double encoderMilliseconds = 0.0;
    double boxDecoderMilliseconds = 0.0;
    double pointGenerationMilliseconds = 0.0;
    double refinedDecoderMilliseconds = 0.0;
    double decoderMilliseconds = 0.0;
};

struct Sam2PromptPoint {
    float x = 0.0f;
    float y = 0.0f;
    int32_t label = 1;
};

struct Sam2RefinementInfo {
    std::vector<Sam2PromptPoint> points;
    bool applied = false;
    float boxPredictedIou = 0.0f;
    float refinedPredictedIou = 0.0f;
    float boxMaskIou = 1.0f;
    float areaRatio = 1.0f;
    uint32_t boxComponents = 0;
    uint32_t refinedComponents = 0;
};

class Sam2Model {
public:
    Sam2Model();
    ~Sam2Model();
    Sam2Model(const Sam2Model&) = delete;
    Sam2Model& operator=(const Sam2Model&) = delete;

    bool Load(const std::filesystem::path& encoderPath, const std::filesystem::path& decoderPath,
        InferenceDevice encoderDevice, InferenceDevice decoderDevice, std::string& error);
    bool Run(const ImageData& image, const DetectionBox& box, MaskData& mask, float& predictedIou,
        Sam2Timings& timings, Sam2RefinementInfo& refinement,
        std::string& error, const std::function<void()>& encoderComplete = {});
    void ClearImageCache();
    void Reset();
    [[nodiscard]] bool IsLoaded() const noexcept;
    [[nodiscard]] InferenceProvider EncoderProvider() const noexcept { return encoderProvider_; }
    [[nodiscard]] InferenceProvider DecoderProvider() const noexcept { return decoderProvider_; }
    [[nodiscard]] const std::string& ProviderWarning() const noexcept { return providerWarning_; }

private:
    bool Encode(const ImageData& image, double& milliseconds, std::string& error);
    bool Decode(const std::vector<float>& points, const std::vector<int32_t>& labels,
        std::vector<float>& logits, float& predictedIou, double& milliseconds, std::string& error);
    std::unique_ptr<Ort::Env> environment_;
    std::unique_ptr<Ort::Session> encoder_;
    std::unique_ptr<Ort::Session> decoder_;
    std::vector<float> imageEmbed_;
    std::vector<float> highResolution0_;
    std::vector<float> highResolution1_;
    std::vector<float> imageInput_;
    uint32_t cachedWidth_ = 0;
    uint32_t cachedHeight_ = 0;
    InferenceProvider encoderProvider_ = InferenceProvider::Cpu;
    InferenceProvider decoderProvider_ = InferenceProvider::Cpu;
    std::string providerWarning_;
};
