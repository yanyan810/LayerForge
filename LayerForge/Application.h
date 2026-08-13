#pragma once

#include "EditorUI.h"
#include "GraphicsDevice.h"
#include "ImageData.h"
#include "ImageLoader.h"
#include "AI/MaskData.h"
#include "AI/MaskProcessor.h"
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
    void RebuildDerivedLayers();
    void ResetAnalysis();
    void OnResize(uint32_t width, uint32_t height);
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    HWND window_ = nullptr;
    GraphicsDevice graphics_;
    EditorUI editorUI_;
    ImageLoader imageLoader_;
    ImageData image_;
    GraphicsDevice::Texture texture_;
    SegmentationModel segmentationModel_;
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
    std::string error_;
    double inferenceMilliseconds_ = 0.0;
    double maskUpdateMilliseconds_ = 0.0;
    bool analyzing_ = false;
    bool maskUpdateRequested_ = false;
    bool initialized_ = false;
};
