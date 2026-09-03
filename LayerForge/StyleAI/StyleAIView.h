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
    void DrawTrainingTab();
    void StartBackend();

    StyleDataset dataset_;
    ImageData preview_;
    GraphicsDevice::Texture previewTexture_;
    std::array<char, 256> name_{ "MyStyle" };
    std::array<char, 1024> path_{ "datasets/MyStyle" };
    std::array<char, 4096> caption_{};
    std::array<char, 256> outputName_{ "MyStyle" };
    std::array<char, 1024> pythonPath_{ "python" };
    std::array<char, 1024> backendScript_{ "backend/style_backend.py" };
    int epochs_ = 10;
    int resolution_ = 1024;
    float learningRate_ = 0.0001f;
    StyleAIBackend backend_;
    int selected_ = -1;
    std::string message_;
    bool messageIsError_ = false;
    std::string trainingMessage_;
};
