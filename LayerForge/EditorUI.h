#pragma once

#include "GraphicsDevice.h"
#include "ImageData.h"
#include "AI/MaskData.h"
#include "AI/MaskProcessor.h"

#include <Windows.h>
#include <functional>
#include <string>

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
        MaskAdjustmentSettings& maskSettings, const std::string& error, bool analyzing, double inferenceMilliseconds, double maskUpdateMilliseconds,
        const std::function<void()>& openImage, const std::function<void()>& analyzeImage, const std::function<void()>& maskSettingsChanged);
    void Render(ID3D12GraphicsCommandList* commandList);

private:
    bool initialized_ = false;
};
