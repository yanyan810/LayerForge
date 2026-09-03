#pragma once

#include "GraphicsDevice.h"
#include "ImageData.h"
#include "AI/MaskData.h"
#include "AI/MaskProcessor.h"
#include "AI/DetectionBox.h"
#include "AI/Sam2Model.h"
#include "AI/InferenceDevice.h"
#include "AI/AIModelManager.h"
#include "Editor/MaskEditor.h"
#include "Editor/SmartMaskCorrection.h"

#include <Windows.h>
#include <functional>
#include <string>

enum class HairAnalysisStage { NotReady, Ready, LoadingModels, Detecting, Encoding, Refining, Preparing, Complete, Failed };

class EditorUI {
public:
    bool Initialize(HWND window, GraphicsDevice& graphics, std::string& error);
    void Shutdown();
    void BeginFrame();
    void Draw(const ImageData* image, const GraphicsDevice::Texture* texture,
        const MaskData* rawMask, const GraphicsDevice::Texture* rawMaskTexture,
        const MaskData* adjustedMask, const GraphicsDevice::Texture* adjustedMaskTexture,
        const ImageData* foreground, const GraphicsDevice::Texture* foregroundTexture,
        const ImageData* background, const GraphicsDevice::Texture* backgroundTexture,
        const DetectionBox* hairBox, const Sam2RefinementInfo* samRefinement,
        const MaskData* hairRawMask, const GraphicsDevice::Texture* hairRawMaskTexture,
        const MaskData* hairAdjustedMask, const GraphicsDevice::Texture* hairAdjustedMaskTexture,
        const MaskData* hairFinalMask, const GraphicsDevice::Texture* hairFinalMaskTexture,
        const ImageData* hairEditPreview, const GraphicsDevice::Texture* hairEditPreviewTexture,
        const ImageData* hairImage, const GraphicsDevice::Texture* hairTexture,
        MaskAdjustmentSettings& maskSettings, MaskAdjustmentSettings& hairMaskSettings, MaskEditor& hairMaskEditor,
        SmartMaskCorrection& smartMaskCorrection, const std::vector<Sam2PromptPoint>* smartPrompts,
        const MaskData* smartCandidateMask, const GraphicsDevice::Texture* smartCandidateTexture,
        const ImageData* smartDifferenceImage, const GraphicsDevice::Texture* smartDifferenceTexture,
        bool smartCandidateAvailable, double smartPromptMilliseconds, double smartDecoderMilliseconds,
        double smartMaskMilliseconds, double smartTextureMilliseconds, double smartTotalMilliseconds, float smartPredictedIou,
        InferenceDevice& inferenceDevice,
        const std::string& error, const std::string& providerWarning, bool analyzing, HairAnalysisStage hairStage,
        double inferenceMilliseconds, double maskUpdateMilliseconds, double groundingDinoMilliseconds,
        const Sam2Timings& samTimings, double hairTotalMilliseconds, double hairMaskUpdateMilliseconds, float samPredictedIou,
        ModelLoadState u2netState, ModelLoadState dinoState, ModelLoadState samEncoderState, ModelLoadState samDecoderState,
        InferenceProvider u2netProvider,
        InferenceProvider dinoProvider, InferenceProvider samEncoderProvider, InferenceProvider samDecoderProvider,
        const std::function<void()>& openImage, const std::function<void()>& analyzeImage,
        const std::function<void()>& analyzeHair, const std::function<void()>& maskSettingsChanged,
        const std::function<void()>& hairMaskSettingsChanged, const std::function<void()>& hairManualChanged,
        const std::function<void(const SmartStrokeRequest&)>& smartStrokeCompleted,
        const std::function<void()>& applySmartCandidate, const std::function<void()>& cancelSmartCandidate,
        const std::function<void()>& resetManualHairEdit,
        const std::function<void()>& inferenceDeviceChanged);
    void Render(ID3D12GraphicsCommandList* commandList);

private:
    bool initialized_ = false;
    bool showHairPrompts_ = false;
    bool hairEditMode_ = false;
    bool selectMaskEditTab_ = false;
    bool selectSmartDifferenceTab_ = false;
    bool smartCandidateWasAvailable_ = false;
    int hairEditTool_ = 0;
};
