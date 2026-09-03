#pragma once

#include "EditorUI.h"
#include "GraphicsDevice.h"
#include "ImageData.h"
#include "ImageLoader.h"
#include "AI/MaskData.h"
#include "AI/MaskProcessor.h"
#include "AI/AIModelManager.h"
#include "Editor/MaskEditor.h"
#include "Editor/SmartMaskCorrection.h"
#include "StyleAI/StyleAIView.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

class Application {
public:
    int Run(HINSTANCE instance, int showCommand);

private:
    struct WorkerResult;
    bool Initialize(HINSTANCE instance, int showCommand);
    void Shutdown();
    void OpenImage();
    void StartCharacterAnalysis();
    void StartHairAnalysis();
    void StartSmartHairCorrection(const SmartStrokeRequest& request);
    void PollAnalysisResult();
    void UpdateProgressState();
    void ApplyCharacterResult(CharacterAnalysisResult&& result);
    void ApplyHairResult(HairAnalysisResult&& result);
    void ApplySmartHairResult(WorkerResult&& result);
    void ApplySmartCandidate();
    void CancelSmartCandidate();
    void ResetManualHairEdit();
    void CancelActiveAnalysis();
    [[nodiscard]] AIModelPaths ModelPaths() const;
    void RebuildDerivedLayers();
    void RebuildHairLayers();
    void RebuildManualHairLayers();
    void ResetAnalysis();
    void ResetHairResult();
    void ChangeInferenceDevice();
    void AppendProviderWarning(const std::string& warning);
    void OnResize(uint32_t width, uint32_t height);
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    HWND window_ = nullptr;
    GraphicsDevice graphics_;
    EditorUI editorUI_;
    StyleAIView styleAIView_;
    ImageLoader imageLoader_;
    ImageData image_;
    GraphicsDevice::Texture texture_;
    AIModelManager modelManager_;
    MaskProcessor maskProcessor_;
    MaskAdjustmentSettings maskSettings_;
    MaskData rawMask_;
    MaskData adjustedMask_;
    ImageData foreground_;
    ImageData background_;
    GraphicsDevice::Texture rawMaskTexture_;
    GraphicsDevice::Texture adjustedMaskTexture_;
    GraphicsDevice::Texture foregroundTexture_;
    GraphicsDevice::Texture backgroundTexture_;
    MaskAdjustmentSettings hairMaskSettings_;
    MaskData hairRawMask_;
    MaskData hairAdjustedMask_;
    MaskData hairFinalMask_;
    ImageData hairImage_;
    ImageData hairEditPreview_;
    GraphicsDevice::Texture hairRawMaskTexture_;
    GraphicsDevice::Texture hairAdjustedMaskTexture_;
    GraphicsDevice::Texture hairFinalMaskTexture_;
    GraphicsDevice::Texture hairEditPreviewTexture_;
    GraphicsDevice::Texture hairTexture_;
    MaskEditor hairMaskEditor_;
    SmartMaskCorrection smartMaskCorrection_;
    MaskData smartCandidateMask_;
    ImageData smartDifferenceImage_;
    GraphicsDevice::Texture smartCandidateTexture_;
    GraphicsDevice::Texture smartDifferenceTexture_;
    std::vector<Sam2PromptPoint> smartPromptPoints_;
    SmartCorrectionRoi smartCorrectionRoi_;
    SmartCorrectionMode smartCorrectionMode_ = SmartCorrectionMode::Add;
    double smartPromptMilliseconds_ = 0.0;
    double smartDecoderMilliseconds_ = 0.0;
    double smartMaskMilliseconds_ = 0.0;
    double smartTextureMilliseconds_ = 0.0;
    double smartTotalMilliseconds_ = 0.0;
    float smartPredictedIou_ = 0.0f;
    DetectionBox hairBox_;
    HairAnalysisStage hairAnalysisStage_ = HairAnalysisStage::NotReady;
    double groundingDinoMilliseconds_ = 0.0;
    Sam2Timings sam2Timings_;
    double hairTotalMilliseconds_ = 0.0;
    double hairMaskUpdateMilliseconds_ = 0.0;
    float samPredictedIou_ = 0.0f;
    Sam2RefinementInfo samRefinement_;
    InferenceDevice inferenceDevice_ = InferenceDevice::Auto;
    std::string providerWarning_;
    std::string error_;
    double inferenceMilliseconds_ = 0.0;
    double maskUpdateMilliseconds_ = 0.0;
    bool analyzing_ = false;
    bool maskUpdateRequested_ = false;
    bool hairMaskUpdateRequested_ = false;
    bool hairManualUpdateRequested_ = false;
    bool initialized_ = false;

    enum class AnalysisJobType { None, Character, Hair, SmartHair };
    struct WorkerResult {
        AnalysisJobType type = AnalysisJobType::None;
        uint64_t jobId = 0;
        uint64_t imageGeneration = 0;
        CharacterAnalysisResult character;
        HairAnalysisResult hair;
        SmartHairAnalysisResult smartHair;
        SmartStrokeRequest smartRequest;
        MaskData smartCandidate;
        double smartMaskMilliseconds = 0.0;
        double smartTotalMilliseconds = 0.0;
    };

    std::jthread analysisWorker_;
    std::mutex resultMutex_;
    std::optional<WorkerResult> pendingResult_;
    std::atomic<AnalysisProgress> workerProgress_ = AnalysisProgress::Idle;
    AnalysisJobType activeJobType_ = AnalysisJobType::None;
    uint64_t imageGeneration_ = 0;
    uint64_t nextJobId_ = 0;
    uint64_t activeJobId_ = 0;
};
