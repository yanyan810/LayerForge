#include "AI/GroundingDinoModel.h"
#include "AI/HairBoxFilter.h"
#include "AI/MaskProcessor.h"
#include "AI/Sam2Model.h"
#include "AI/SegmentationModel.h"
#include "ImageLoader.h"

#include <Windows.h>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
InferenceDevice ParseDevice(std::wstring_view value) {
    if (value == L"cpu") return InferenceDevice::Cpu;
    if (value == L"dml") return InferenceDevice::DirectML;
    if (value == L"auto") return InferenceDevice::Auto;
    throw std::runtime_error("device must be cpu, dml, or auto");
}

bool TestImage(const std::filesystem::path& path, SegmentationModel& characterModel, GroundingDinoModel& dino,
    Sam2Model& sam, Sam2Model* comparisonSam, const HairBoxFilter& filter) {
    ImageLoader loader; ImageData image; std::string error;
    if (!loader.Load(path, image, error)) { std::cerr << path.string() << ": load: " << error << '\n'; return false; }
    MaskData characterRaw, characterAdjusted; ImageData character, background; double characterMs = 0.0;
    MaskProcessor processor; MaskAdjustmentSettings settings;
    if (!characterModel.Run(image, characterRaw, characterMs, error) ||
        !processor.Process(characterRaw, image, settings, characterAdjusted, character, background, error)) {
        std::cerr << path.string() << ": character: " << error << '\n'; return false;
    }
    const auto hairStart = std::chrono::steady_clock::now();
    const auto characterBox = filter.CharacterBounds(characterAdjusted);
    if (!characterBox) { std::cerr << path.string() << ": no character box\n"; return false; }
    const DetectionBox crop = HairBoxFilter::ExpandAndClamp(*characterBox, 0.20f, image.width, image.height);
    std::vector<DetectionBox> candidates; double dinoMs = 0.0;
    if (!dino.Run(image, crop, candidates, dinoMs, error)) { std::cerr << path.string() << ": dino: " << error << '\n'; return false; }
    std::vector<DetectionBox> accepted;
    auto selected = filter.Select(candidates, characterAdjusted, *characterBox, crop, &accepted);
    const bool touchesHorizontalCropEdge = selected && (selected->x1 <= crop.x1 + 2.0f || selected->x2 >= crop.x2 - 2.0f);
    bool fallback = !selected || selected->confidence < 0.40f || selected->rankScore < 0.20f || touchesHorizontalCropEdge;
    if (fallback) {
        DetectionBox full; full.x2 = static_cast<float>(image.width); full.y2 = static_cast<float>(image.height);
        std::vector<DetectionBox> extra; double extraMs = 0.0;
        if (!dino.Run(image, full, extra, extraMs, error)) { std::cerr << path.string() << ": fallback: " << error << '\n'; return false; }
        dinoMs += extraMs; candidates.insert(candidates.end(), extra.begin(), extra.end());
        selected = filter.Select(candidates, characterAdjusted, *characterBox, crop, &accepted);
    }
    if (!selected) { std::cerr << path.filename().string() << ": no filtered hair box\n"; return false; }
    sam.ClearImageCache(); MaskData hairRaw, hairAdjusted; ImageData hair, unused; float iou = 0.0f; Sam2Timings samTimes; Sam2RefinementInfo refinement;
    if (!sam.Run(image, *selected, hairRaw, iou, samTimes, refinement, error) ||
        !processor.Process(hairRaw, image, settings, hairAdjusted, hair, unused, error)) {
        std::cerr << path.filename().string() << ": sam: " << error << '\n'; return false;
    }
    const double hairTotalMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - hairStart).count();
    size_t visible = 0; for (uint8_t value : hairAdjusted.grayscale) if (value >= 128) ++visible;
    std::cout << path.filename().string() << " character_ms=" << characterMs << " dino_ms=" << dinoMs
        << " fallback=" << fallback << " candidates=" << candidates.size()
        << " box=" << selected->x1 << ',' << selected->y1 << ',' << selected->x2 << ',' << selected->y2
        << " confidence=" << selected->confidence << " rank=" << selected->rankScore
        << " character_area=" << selected->characterAreaRatio << " outside=" << selected->outsideRatio
        << " sam_encoder_ms=" << samTimes.encoderMilliseconds << " sam_decoder_ms=" << samTimes.decoderMilliseconds
        << " refinement=" << refinement.applied << " box_iou=" << refinement.boxPredictedIou
        << " hair_total_ms=" << hairTotalMs << " predicted_iou=" << iou << " visible_pixels=" << visible << '\n';
    for (const auto& candidate : accepted) std::cout << "  accepted box=" << candidate.x1 << ',' << candidate.y1 << ','
        << candidate.x2 << ',' << candidate.y2 << " confidence=" << candidate.confidence << " rank=" << candidate.rankScore
        << " character_area=" << candidate.characterAreaRatio << " outside=" << candidate.outsideRatio << '\n';
    if (comparisonSam) {
        comparisonSam->ClearImageCache(); MaskData comparisonRaw, comparisonAdjusted; ImageData comparisonHair, comparisonBackground;
        float comparisonIou = 0.0f; Sam2Timings comparisonTimes; Sam2RefinementInfo comparisonRefinement;
        if (!comparisonSam->Run(image, *selected, comparisonRaw, comparisonIou, comparisonTimes, comparisonRefinement, error) ||
            !processor.Process(comparisonRaw, image, settings, comparisonAdjusted, comparisonHair, comparisonBackground, error)) {
            std::cerr << path.filename().string() << ": comparison sam: " << error << '\n'; return false;
        }
        size_t binaryAgreement = 0, comparisonVisible = 0; uint64_t absoluteRawDifference = 0;
        for (size_t pixel = 0; pixel < hairRaw.grayscale.size(); ++pixel) {
            binaryAgreement += (hairRaw.grayscale[pixel] >= 128) == (comparisonRaw.grayscale[pixel] >= 128);
            comparisonVisible += comparisonAdjusted.grayscale[pixel] >= 128;
            absoluteRawDifference += static_cast<uint64_t>(std::abs(static_cast<int>(hairRaw.grayscale[pixel]) - static_cast<int>(comparisonRaw.grayscale[pixel])));
        }
        std::cout << "  sam_compare predicted_iou=" << comparisonIou << " iou_delta=" << std::abs(iou - comparisonIou)
            << " binary_agreement=" << (100.0 * binaryAgreement / hairRaw.grayscale.size())
            << " mean_raw_delta=" << (static_cast<double>(absoluteRawDifference) / hairRaw.grayscale.size())
            << " visible_pixels=" << comparisonVisible << " visible_delta=" << static_cast<int64_t>(visible) - static_cast<int64_t>(comparisonVisible) << '\n';
    }
    return true;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) { std::cerr << "usage: Phase4EPipelineTest image...\n"; return 2; }
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (argc == 2 && std::wstring_view(argv[1]) == L"--missing-models") {
        std::string missingError; GroundingDinoModel missingDino; Sam2Model missingSam;
        const bool dinoRejected = !missingDino.Load(L"Models/GroundingDINO/not-present.onnx", InferenceDevice::Cpu, missingError) && !missingError.empty();
        missingError.clear();
        const bool samRejected = !missingSam.Load(L"Models/SAM2/not-present-encoder.onnx", L"Models/SAM2/not-present-decoder.onnx", InferenceDevice::Cpu, InferenceDevice::Cpu, missingError) && !missingError.empty();
        std::cout << "missing_dino_rejected=" << dinoRejected << " missing_sam_rejected=" << samRejected << '\n';
        if (SUCCEEDED(com)) CoUninitialize(); return dinoRejected && samRejected ? 0 : 1;
    }
    InferenceDevice characterDevice = InferenceDevice::Cpu, dinoDevice = InferenceDevice::Cpu;
    InferenceDevice encoderDevice = InferenceDevice::Cpu, decoderDevice = InferenceDevice::Cpu;
    bool compareSam = false;
    int firstImage = 1;
    try {
        while (firstImage < argc && std::wstring_view(argv[firstImage]).starts_with(L"--")) {
            const std::wstring_view argument(argv[firstImage]);
            if (argument == L"--compare-sam") compareSam = true;
            else if (argument.starts_with(L"--character=")) characterDevice = ParseDevice(argument.substr(12));
            else if (argument.starts_with(L"--dino=")) dinoDevice = ParseDevice(argument.substr(7));
            else if (argument.starts_with(L"--sam-encoder=")) encoderDevice = ParseDevice(argument.substr(14));
            else if (argument.starts_with(L"--sam-decoder=")) decoderDevice = ParseDevice(argument.substr(14));
            else throw std::runtime_error("unknown option");
            ++firstImage;
        }
    } catch (const std::exception& exception) { std::cerr << exception.what() << '\n'; if (SUCCEEDED(com)) CoUninitialize(); return 2; }
    if (firstImage >= argc) { std::cerr << "no image supplied\n"; if (SUCCEEDED(com)) CoUninitialize(); return 2; }

    std::string error; SegmentationModel character; GroundingDinoModel dino; Sam2Model sam; Sam2Model comparisonSam; HairBoxFilter filter;
    if (!character.Load(L"Models/segmentation/u2netp.onnx", characterDevice, error) ||
        !dino.Load(L"Models/GroundingDINO/grounding-dino-base-hair-800x1333.onnx", dinoDevice, error) ||
        !sam.Load(L"Models/SAM2/sam2.1-hiera-small-encoder.onnx", L"Models/SAM2/sam2.1-hiera-small-prompt-decoder.onnx", encoderDevice, decoderDevice, error)) {
        std::cerr << error << '\n'; if (SUCCEEDED(com)) CoUninitialize(); return 1;
    }
    if (compareSam && !comparisonSam.Load(L"Models/SAM2/sam2.1-hiera-small-encoder.onnx", L"Models/SAM2/sam2.1-hiera-small-prompt-decoder.onnx",
        InferenceDevice::Cpu, InferenceDevice::Cpu, error)) {
        std::cerr << error << '\n'; if (SUCCEEDED(com)) CoUninitialize(); return 1;
    }
    std::cout << "providers character=" << InferenceProviderName(character.Provider())
        << " dino=" << InferenceProviderName(dino.Provider())
        << " sam_encoder=" << InferenceProviderName(sam.EncoderProvider())
        << " sam_decoder=" << InferenceProviderName(sam.DecoderProvider()) << '\n';
    if (!character.ProviderWarning().empty()) std::cout << character.ProviderWarning() << '\n';
    if (!dino.ProviderWarning().empty()) std::cout << dino.ProviderWarning() << '\n';
    if (!sam.ProviderWarning().empty()) std::cout << sam.ProviderWarning() << '\n';
    bool success = true;
    for (int index = firstImage; index < argc; ++index) success = TestImage(argv[index], character, dino, sam, compareSam ? &comparisonSam : nullptr, filter) && success;
    if (SUCCEEDED(com)) CoUninitialize(); return success ? 0 : 1;
}
