#pragma once

#include "StyleDataset.h"
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

    StyleDataset dataset_;
    ImageData preview_;
    GraphicsDevice::Texture previewTexture_;
    std::array<char, 256> name_{ "MyStyle" };
    std::array<char, 1024> path_{ "datasets/MyStyle" };
    std::array<char, 4096> caption_{};
    int selected_ = -1;
    std::string message_;
    bool messageIsError_ = false;
};
