#pragma once

#include "GraphicsDevice.h"
#include "ImageData.h"
#include "AI/MaskData.h"

#include <Windows.h>
#include <functional>
#include <string>

class EditorUI {
public:
    bool Initialize(HWND window, GraphicsDevice& graphics, std::string& error);
    void Shutdown();
    void BeginFrame();
    void Draw(const ImageData* image, const GraphicsDevice::Texture* texture, const MaskData* mask, const GraphicsDevice::Texture* maskTexture,
        const std::string& error, bool analyzing, double inferenceMilliseconds, const std::function<void()>& openImage, const std::function<void()>& analyzeImage);
    void Render(ID3D12GraphicsCommandList* commandList);

private:
    bool initialized_ = false;
};
