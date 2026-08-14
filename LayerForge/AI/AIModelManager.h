#pragma once

#include "GroundingDinoModel.h"
#include "HairBoxFilter.h"
#include "MaskProcessor.h"
#include "Sam2Model.h"
#include "SegmentationModel.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <stop_token>
#include <string>

enum class ModelLoadState {
    NotLoaded,
    Loading,
    Ready,
    Failed,
};

enum class AnalysisProgress {
    Idle,
    LoadingModels,
    AnalyzingCharacter,
    DetectingHair,
    EncodingImage,
    RefiningMask,
    PreparingResult,
    Complete,
    Failed,
    Cancelled,
};

struct AIModelPaths {
    std::filesystem::path character;
    std::filesystem::path groundingDino;
    std::filesystem::path samEncoder;
    std::filesystem::path samDecoder;
};

struct AIModelStatus {
    ModelLoadState character = ModelLoadState::NotLoaded;
    ModelLoadState groundingDino = ModelLoadState::NotLoaded;
    ModelLoadState samEncoder = ModelLoadState::NotLoaded;
    ModelLoadState samDecoder = ModelLoadState::NotLoaded;
    InferenceProvider characterProvider = InferenceProvider::Cpu;
    InferenceProvider groundingDinoProvider = InferenceProvider::Cpu;
    InferenceProvider samEncoderProvider = InferenceProvider::Cpu;
    InferenceProvider samDecoderProvider = InferenceProvider::Cpu;
};

struct CharacterAnalysisResult {
    MaskData rawMask;
    MaskData adjustedMask;
    ImageData foreground;
    ImageData background;
    double inferenceMilliseconds = 0.0;
    double maskMilliseconds = 0.0;
    std::string providerWarning;
    std::string error;
};

struct HairAnalysisResult {
    MaskData rawMask;
    MaskData adjustedMask;
    ImageData hairImage;
    DetectionBox selectedBox;
    double groundingDinoMilliseconds = 0.0;
    Sam2Timings samTimings;
    double totalMilliseconds = 0.0;
    double maskMilliseconds = 0.0;
    float predictedIou = 0.0f;
    Sam2RefinementInfo refinement;
    std::string providerWarning;
    std::string error;
};

class AIModelManager {
public:
    using ProgressCallback = std::function<void(AnalysisProgress)>;

    CharacterAnalysisResult AnalyzeCharacter(const ImageData& image, const MaskAdjustmentSettings& settings,
        const AIModelPaths& paths, std::stop_token stopToken, const ProgressCallback& progress);
    HairAnalysisResult AnalyzeHair(const ImageData& image, const MaskData& characterMask,
        const MaskAdjustmentSettings& settings, const AIModelPaths& paths, InferenceDevice device,
        uint64_t imageGeneration, std::stop_token stopToken, const ProgressCallback& progress);

    void Reset();
    void InvalidateImageCache();
    [[nodiscard]] AIModelStatus Status() const;

private:
    void SetCharacterState(ModelLoadState state);
    void SetHairStates(ModelLoadState dino, ModelLoadState encoder, ModelLoadState decoder);
    void RefreshProviders();

    mutable std::mutex modelMutex_;
    mutable std::mutex statusMutex_;
    SegmentationModel character_;
    GroundingDinoModel groundingDino_;
    Sam2Model sam2_;
    HairBoxFilter hairBoxFilter_;
    MaskProcessor maskProcessor_;
    AIModelStatus status_;
    uint64_t samCacheGeneration_ = 0;
};

[[nodiscard]] const char* ModelLoadStateName(ModelLoadState state) noexcept;
