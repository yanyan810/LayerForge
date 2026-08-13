#pragma once

#include "EditorUI.h"
#include "GraphicsDevice.h"
#include "ImageData.h"
#include "ImageLoader.h"
#include "AI/MaskData.h"
#include "AI/SegmentationModel.h"

#include <Windows.h>
#include <string>

class Application {
public:
    int Run(HINSTANCE instance, int showCommand);

private:
    bool Initialize(HINSTANCE instance, int showCommand);
    void Shutdown();
    void OpenImage();
    void AnalyzeImage();
    void OnResize(uint32_t width, uint32_t height);
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    HWND window_ = nullptr;
    GraphicsDevice graphics_;
    EditorUI editorUI_;
    ImageLoader imageLoader_;
    ImageData image_;
    GraphicsDevice::Texture texture_;
    SegmentationModel segmentationModel_;
    MaskData mask_;
    GraphicsDevice::Texture maskTexture_;
    std::string error_;
    double inferenceMilliseconds_ = 0.0;
    bool analyzing_ = false;
    bool initialized_ = false;
};
