#define NOMINMAX
#include "AI/AIModelManager.h"
#include "Editor/SmartMaskCorrection.h"
#include "ImageLoader.h"

#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

SmartCorrectionRoi PointRoi(float x, float y, uint32_t width, uint32_t height) {
    constexpr float margin = 100.0f;
    return {
        static_cast<uint32_t>(std::max(0.0f, x - margin)),
        static_cast<uint32_t>(std::max(0.0f, y - margin)),
        static_cast<uint32_t>(std::min(static_cast<float>(width), x + margin)),
        static_cast<uint32_t>(std::min(static_cast<float>(height), y + margin))
    };
}

size_t VerifyCandidate(const MaskData& before, const MaskData& candidate, const SmartCorrectionRoi& roi,
    SmartCorrectionMode mode) {
    Require(before.IsValid() && candidate.IsValid(), "Smart candidate is invalid.");
    size_t changes = 0;
    for (uint32_t y = 0; y < before.height; ++y) for (uint32_t x = 0; x < before.width; ++x) {
        const size_t index = static_cast<size_t>(y) * before.width + x;
        const bool inside = x >= roi.x1 && x < roi.x2 && y >= roi.y1 && y < roi.y2;
        if (!inside) Require(candidate.grayscale[index] == before.grayscale[index], "Smart candidate changed pixels outside its ROI.");
        if (mode == SmartCorrectionMode::Add)
            Require(candidate.grayscale[index] >= before.grayscale[index], "Smart Add decreased a mask pixel.");
        else
            Require(candidate.grayscale[index] <= before.grayscale[index], "Smart Erase increased a mask pixel.");
        changes += candidate.grayscale[index] != before.grayscale[index];
    }
    return changes;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) { std::cerr << "usage: Phase4JSmartTest image\n"; return 2; }
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        ImageData image; ImageLoader loader; std::string error;
        Require(loader.Load(argv[1], image, error), error);
        const AIModelPaths paths{
            L"Models/segmentation/u2netp.onnx",
            L"Models/GroundingDINO/grounding-dino-base-hair-800x1333.onnx",
            L"Models/SAM2/sam2.1-hiera-small-encoder.onnx",
            L"Models/SAM2/sam2.1-hiera-small-prompt-decoder.onnx"
        };
        AIModelManager manager; MaskAdjustmentSettings settings;
        const auto progress = [](AnalysisProgress) {};
        CharacterAnalysisResult character = manager.AnalyzeCharacter(image, settings, paths, {}, progress);
        Require(character.error.empty(), "Character: " + character.error);
        HairAnalysisResult hair = manager.AnalyzeHair(image, character.adjustedMask, settings, paths,
            InferenceDevice::DirectML, 1, {}, progress);
        Require(hair.error.empty(), "Hair: " + hair.error);

        MaskProcessor processor;
        const float addX = hair.selectedBox.x1 + hair.selectedBox.Width() * 0.82f;
        const float addY = hair.selectedBox.y1 + hair.selectedBox.Height() * 0.55f;
        const SmartCorrectionRoi addRoi = PointRoi(addX, addY, image.width, image.height);
        SmartHairAnalysisResult add = manager.AnalyzeSmartHair(image, hair.selectedBox, { { addX, addY, 1 } }, 1, {}, progress);
        Require(add.error.empty(), "Smart Add: " + add.error);
        MaskData adjustedAdd, addCandidate;
        Require(processor.Adjust(add.rawMask, settings, adjustedAdd, error), error);
        Require(SmartMaskCorrection::BuildCandidate(hair.adjustedMask, adjustedAdd, addRoi,
            SmartCorrectionMode::Add, addCandidate), "Could not merge Smart Add.");
        const size_t addChanges = VerifyCandidate(hair.adjustedMask, addCandidate, addRoi, SmartCorrectionMode::Add);

        const float eraseX = hair.selectedBox.x1 + hair.selectedBox.Width() * 0.90f;
        const float eraseY = hair.selectedBox.y1 + hair.selectedBox.Height() * 0.58f;
        const SmartCorrectionRoi eraseRoi = PointRoi(eraseX, eraseY, image.width, image.height);
        SmartHairAnalysisResult erase = manager.AnalyzeSmartHair(image, hair.selectedBox, { { eraseX, eraseY, 0 } }, 1, {}, progress);
        Require(erase.error.empty(), "Smart Erase: " + erase.error);
        MaskData adjustedErase, eraseCandidate;
        Require(processor.Adjust(erase.rawMask, settings, adjustedErase, error), error);
        Require(SmartMaskCorrection::BuildCandidate(hair.adjustedMask, adjustedErase, eraseRoi,
            SmartCorrectionMode::Erase, eraseCandidate), "Could not merge Smart Erase.");
        const size_t eraseChanges = VerifyCandidate(hair.adjustedMask, eraseCandidate, eraseRoi, SmartCorrectionMode::Erase);

        const std::vector<Sam2PromptPoint> boundaryPrompts{
            { hair.selectedBox.x1 + hair.selectedBox.Width() * 0.16f, hair.selectedBox.y1, 0 },
            { hair.selectedBox.x1 + hair.selectedBox.Width() * 0.74f, hair.selectedBox.y1 + hair.selectedBox.Height() * 0.19f, 0 },
            { hair.selectedBox.x1 + hair.selectedBox.Width() * 0.92f, hair.selectedBox.y1 + hair.selectedBox.Height() * 0.72f, 0 }
        };
        SmartHairAnalysisResult boundaryErase = manager.AnalyzeSmartHair(image, hair.selectedBox, boundaryPrompts, 1, {}, progress);
        Require(boundaryErase.error.empty(), "Boundary Smart Erase: " + boundaryErase.error);
        MaskData adjustedBoundaryErase, boundaryCandidate;
        Require(processor.Adjust(boundaryErase.rawMask, settings, adjustedBoundaryErase, error), error);
        const SmartCorrectionRoi boundaryRoi{
            static_cast<uint32_t>(std::max(0.0f, boundaryPrompts.front().x - 60.0f)),
            0,
            static_cast<uint32_t>(std::min(static_cast<float>(image.width), boundaryPrompts.back().x + 60.0f)),
            static_cast<uint32_t>(std::min(static_cast<float>(image.height), boundaryPrompts.back().y + 60.0f))
        };
        Require(SmartMaskCorrection::BuildCandidate(hair.adjustedMask, adjustedBoundaryErase, boundaryRoi,
            SmartCorrectionMode::Erase, boundaryCandidate), "Could not merge boundary Smart Erase.");
        const size_t boundaryEraseChanges = VerifyCandidate(hair.adjustedMask, boundaryCandidate, boundaryRoi,
            SmartCorrectionMode::Erase);

        Require(add.decoderMilliseconds < 1000.0 && erase.decoderMilliseconds < 1000.0,
            "Cached Smart decoder exceeded the one-second guardrail.");
        manager.InvalidateImageCache();
        SmartHairAnalysisResult stale = manager.AnalyzeSmartHair(image, hair.selectedBox, { { addX, addY, 1 } }, 1, {}, progress);
        Require(!stale.error.empty(), "Smart correction accepted an invalidated image cache.");
        std::cout << "Phase4J Smart test passed. add_changes=" << addChanges
            << " erase_changes=" << eraseChanges
            << " boundary_erase_changes=" << boundaryEraseChanges
            << " add_decoder_ms=" << add.decoderMilliseconds
            << " erase_decoder_ms=" << erase.decoderMilliseconds
            << " add_iou=" << add.predictedIou << " erase_iou=" << erase.predictedIou
            << " stale_cache_rejected=1\n";
        if (SUCCEEDED(com)) CoUninitialize(); return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n'; if (SUCCEEDED(com)) CoUninitialize(); return 1;
    }
}
