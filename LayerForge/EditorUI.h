#pragma once

#include "GraphicsDevice.h"
#include "ImageData.h"

#include <Windows.h>
#include <functional>
#include <string>

class EditorUI {
public:
    bool Initialize(HWND window, GraphicsDevice& graphics, std::string& error);
    void Shutdown();
    void BeginFrame();
    void Draw(const ImageData* image, const GraphicsDevice::Texture* texture, const std::string& error, const std::function<void()>& openImage);
    void Render(ID3D12GraphicsCommandList* commandList);

private:
    bool initialized_ = false;
};
