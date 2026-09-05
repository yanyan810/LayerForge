#pragma once

#include "StyleDataset.h"
#include "StyleAIBackend.h"
#include "StyleTrainingConfig.h"
#include "StyleGenerationConfig.h"
#include "StyleCaptionConfig.h"
#include "StylePreset.h"
#include "TrainingPreset.h"
#include "GenerationHistory.h"
#include "../GraphicsDevice.h"
#include "../ImageData.h"
#include "../ImageLoader.h"

#include <Windows.h>
#include <array>

class StyleAIView {
public:
    void Draw(HWND owner, GraphicsDevice& graphics, const ImageLoader& loader);
    void Shutdown();

private:
    void CreateDataset();
    void LoadDataset(HWND owner);
    void AddImages(HWND owner, GraphicsDevice& graphics, const ImageLoader& loader);
    void SelectImage(size_t index, GraphicsDevice& graphics, const ImageLoader& loader);
    void RemoveSelected();
    void DrawTrainingTab(HWND owner);
    void StartBackend();
    void DrawGenerateTab(HWND owner, GraphicsDevice& graphics, const ImageLoader& loader);
    void StartGeneration(bool comparison = false);
    void SelectLora(HWND owner);
    void UpdateGeneratedPreview(GraphicsDevice& graphics, const ImageLoader& loader);
    void ImportFolders(HWND owner);
    void StartCaption(bool selectedOnly);
    void UpdateCaptionResult();
    void SaveDatasetAs();
    void SaveAsStyle(bool overwrite = false);
    void RefreshStyles();
    void ApplyStyle(int index);
    void RefreshHistory();
    void DrawHistoryTab(HWND owner, GraphicsDevice& graphics, const ImageLoader& loader);
    void ApplyTrainingPreset(int index);
    void AnalyzeDatasetQuality(const ImageLoader& loader);

    StyleDataset dataset_;
    ImageData preview_;
    GraphicsDevice::Texture previewTexture_;
    std::array<char, 256> name_{ "MyStyle" };
    std::array<char, 1024> path_{ "datasets/MyStyle" };
    std::array<char, 4096> caption_{};
    std::array<char, 256> outputName_{ "MyStyle" };
    std::array<char, 1024> baseModel_{ "stable-diffusion-v1-5/stable-diffusion-v1-5" };
    std::array<char, 256> triggerWord_{ "lfstyle" };
    std::array<char, 1024> pythonPath_{ "runtime/python/Scripts/python.exe" };
    std::array<char, 1024> backendScript_{ "backend/style_backend.py" };
    int epochs_ = 10;
    int resolution_ = 512;
    int trainBatchSize_ = 1;
    int gradientAccumulationSteps_ = 1;
    float learningRate_ = 0.0001f;
    int rank_ = 16;
    int mixedPrecisionIndex_ = 0;
    bool gradientCheckpointing_ = true;
    int seed_ = 42;
    StyleAIBackend backend_;
    StyleAIBackend generationBackend_;
    StyleAIBackend captionBackend_;
    StyleAIBackend persistentGenerationBackend_;
    bool includeSubfolders_=false,skipImported_=true;
    std::array<char,1024> captionModel_{"SmilingWolf/wd-eva02-large-tagger-v3"};
    float generalThreshold_=.35f,characterThreshold_=.85f;
    bool includeCharacterTags_=false,includeRatingTags_=false,replaceUnderscores_=true,skipNonEmptyCaptions_=true;
    std::string captionStatus_;
    bool captionReloaded_=true;
    ImageData generatedImage_;
    GraphicsDevice::Texture generatedTexture_;
    std::array<char, 1024> generationBaseModel_{ "stable-diffusion-v1-5/stable-diffusion-v1-5" };
    std::array<char, 1024> loraPath_{ "Models/lora/MyStyle/MyStyle.safetensors" };
    std::array<char, 256> generationTrigger_{ "lfstyle" };
    std::array<char, 4096> prompt_{};
    std::array<char, 4096> stylePrompt_{};
    std::array<char, 4096> negativePrompt_{ "low quality, blurry, bad anatomy" };
    float loraStrength_ = 0.8f;
    int generationWidth_ = 512, generationHeight_ = 512, generationSteps_ = 25;
    int generationImageCount_ = 1;
    float guidanceScale_ = 7.5f;
    int64_t generationSeed_ = -1;
    std::string generationMessage_;
    std::filesystem::path generatedPath_;
    int selected_ = -1;
    std::string message_;
    bool messageIsError_ = false;
    std::string trainingMessage_;
    std::array<char, 256> styleName_{ "MyStyle" };
    std::vector<StylePreset> styles_;
    int selectedStyle_ = -1;
    bool stylesLoaded_ = false;
    bool enableSafetyChecker_ = false;
    bool usePersistentBackend_ = true;
    bool confirmStyleOverwrite_ = false;
    std::vector<TrainingPreset> trainingPresets_;
    int selectedTrainingPreset_=-1;
    std::array<char,128> trainingPresetName_{"Custom"};
    std::vector<GenerationHistoryEntry> history_;
    int selectedHistory_=-1;
    ImageData historyImage_;GraphicsDevice::Texture historyTexture_;
    std::vector<ImageData> historyThumbnails_;std::vector<GraphicsDevice::Texture> historyThumbnailTextures_;
    bool historyLoaded_=false,confirmHistoryDelete_=false,historyReloaded_=true;
    size_t emptyCaptionCount_=0,duplicateCaptionCount_=0,exactDuplicateCount_=0;
    std::vector<std::pair<size_t,size_t>> similarPairs_;
    int selectedSimilarPair_=-1;
    std::vector<uint8_t> compareSelected_;
    std::vector<ImageData> comparisonImages_;
    std::vector<GraphicsDevice::Texture> comparisonTextures_;
    size_t generationLogStart_=0;
    bool comparisonActive_=false;
};
