#pragma once

#include "StyleDataset.h"
#include "StyleAIBackend.h"
#include "StyleTrainingConfig.h"
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
    int selected_ = -1;
    std::string message_;
    bool messageIsError_ = false;
    std::string trainingMessage_;
};
