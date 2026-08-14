#define NOMINMAX
#include "AI/AIModelManager.h"
#include "ImageLoader.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace {
InferenceDevice ParseDevice(std::wstring_view argument) {
    if (argument == L"--device=cpu") return InferenceDevice::Cpu;
    if (argument == L"--device=dml") return InferenceDevice::DirectML;
    if (argument == L"--device=auto") return InferenceDevice::Auto;
    throw std::runtime_error("Expected --device=cpu, --device=dml, or --device=auto.");
}

template <typename Result, typename Function>
double RunResponsiveJob(Function&& function, Result& result, uint64_t& heartbeats, double& maximumGapMs,
    int cancelAfterMilliseconds = -1) {
    std::mutex resultMutex; std::optional<Result> pending; std::atomic_bool done = false;
    const auto start = std::chrono::steady_clock::now(); auto previous = start; bool cancellationSent = false;
    std::jthread worker([&](std::stop_token token) {
        Result completed = function(token);
        { std::scoped_lock lock(resultMutex); pending = std::move(completed); }
        done.store(true, std::memory_order_release);
    });
    while (!done.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        const auto now = std::chrono::steady_clock::now();
        maximumGapMs = std::max(maximumGapMs, std::chrono::duration<double, std::milli>(now - previous).count());
        previous = now; ++heartbeats;
        if (!cancellationSent && cancelAfterMilliseconds >= 0 &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() >= cancelAfterMilliseconds) {
            worker.request_stop(); cancellationSent = true;
        }
    }
    worker.join();
    { std::scoped_lock lock(resultMutex); result = std::move(*pending); }
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { std::cerr << "usage: Phase4GAsyncTest --device=auto|cpu|dml image [regression-image...]\n"; return 2; }
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        const InferenceDevice device = ParseDevice(argv[1]);
        ImageData image; ImageLoader loader; std::string error;
        if (!loader.Load(argv[2], image, error)) throw std::runtime_error(error);
        const AIModelPaths paths{
            L"Models/segmentation/u2netp.onnx",
            L"Models/GroundingDINO/grounding-dino-base-hair-800x1333.onnx",
            L"Models/SAM2/sam2.1-hiera-small-encoder.onnx",
            L"Models/SAM2/sam2.1-hiera-small-prompt-decoder.onnx"
        };
        AIModelManager manager; MaskAdjustmentSettings characterSettings, hairSettings;
        std::atomic_uint32_t progressMask = 0;
        const auto progress = [&](AnalysisProgress value) { progressMask.fetch_or(1u << static_cast<unsigned>(value), std::memory_order_relaxed); };

        CharacterAnalysisResult character; uint64_t characterTicks = 0; double characterGap = 0.0;
        const double characterWall = RunResponsiveJob([&](std::stop_token token) {
            return manager.AnalyzeCharacter(image, characterSettings, paths, token, progress);
        }, character, characterTicks, characterGap);
        if (!character.error.empty() || !character.adjustedMask.IsValid()) throw std::runtime_error("Character: " + character.error);

        HairAnalysisResult firstHair; uint64_t firstTicks = 0; double firstGap = 0.0;
        const double firstWall = RunResponsiveJob([&](std::stop_token token) {
            return manager.AnalyzeHair(image, character.adjustedMask, hairSettings, paths, device, 1, token, progress);
        }, firstHair, firstTicks, firstGap);
        if (!firstHair.error.empty() || !firstHair.adjustedMask.IsValid()) throw std::runtime_error("Hair first: " + firstHair.error);

        HairAnalysisResult warmHair; uint64_t warmTicks = 0; double warmGap = 0.0;
        const double warmWall = RunResponsiveJob([&](std::stop_token token) {
            return manager.AnalyzeHair(image, character.adjustedMask, hairSettings, paths, device, 1, token, progress);
        }, warmHair, warmTicks, warmGap);
        if (!warmHair.error.empty() || !warmHair.adjustedMask.IsValid()) throw std::runtime_error("Hair warm: " + warmHair.error);

        HairAnalysisResult cancelled; uint64_t cancelTicks = 0; double cancelGap = 0.0;
        RunResponsiveJob([&](std::stop_token token) {
            return manager.AnalyzeHair(image, character.adjustedMask, hairSettings, paths, device, 2, token, progress);
        }, cancelled, cancelTicks, cancelGap, 20);
        if (cancelled.error != "Analysis cancelled.") throw std::runtime_error("Cancellation did not return the expected safe result.");

        const AIModelStatus status = manager.Status();
        const auto area = [](const MaskData& mask) { size_t count = 0; for (uint8_t value : mask.grayscale) count += value >= 128; return count; };
        const size_t firstArea = area(firstHair.adjustedMask), warmArea = area(warmHair.adjustedMask);
        if (firstArea != warmArea || std::abs(firstHair.predictedIou - warmHair.predictedIou) > 1e-6f)
            throw std::runtime_error("Warm analysis changed the Hair result.");
        if (warmHair.samTimings.encoderMilliseconds != 0.0) throw std::runtime_error("SAM feature cache was not reused.");

        AIModelManager missingManager; AIModelPaths missing = paths; missing.character = L"Models/not-present.onnx";
        CharacterAnalysisResult missingResult; uint64_t missingTicks = 0; double missingGap = 0.0;
        RunResponsiveJob([&](std::stop_token token) {
            return missingManager.AnalyzeCharacter(image, characterSettings, missing, token, progress);
        }, missingResult, missingTicks, missingGap);
        if (missingResult.error.empty()) throw std::runtime_error("Missing model was not rejected.");

        for (int index = 3; index < argc; ++index) {
            ImageData regressionImage;
            if (!loader.Load(argv[index], regressionImage, error)) throw std::runtime_error(error);
            CharacterAnalysisResult regressionCharacter; uint64_t regressionCharacterTicks = 0; double regressionCharacterGap = 0.0;
            RunResponsiveJob([&](std::stop_token token) {
                return manager.AnalyzeCharacter(regressionImage, characterSettings, paths, token, progress);
            }, regressionCharacter, regressionCharacterTicks, regressionCharacterGap);
            if (!regressionCharacter.error.empty()) throw std::runtime_error("Regression character: " + regressionCharacter.error);
            HairAnalysisResult regressionHair; uint64_t regressionHairTicks = 0; double regressionHairGap = 0.0;
            const double regressionWall = RunResponsiveJob([&](std::stop_token token) {
                return manager.AnalyzeHair(regressionImage, regressionCharacter.adjustedMask, hairSettings, paths, device,
                    static_cast<uint64_t>(index), token, progress);
            }, regressionHair, regressionHairTicks, regressionHairGap);
            if (!regressionHair.error.empty()) throw std::runtime_error("Regression hair: " + regressionHair.error);
            std::cout << "regression=" << std::filesystem::path(argv[index]).filename().string()
                << " wall_ms=" << regressionWall << " max_gap_ms=" << regressionHairGap
                << " box=" << regressionHair.selectedBox.x1 << ',' << regressionHair.selectedBox.y1 << ','
                << regressionHair.selectedBox.x2 << ',' << regressionHair.selectedBox.y2
                << " iou=" << regressionHair.predictedIou << " area=" << area(regressionHair.adjustedMask) << '\n';
        }

        std::cout << "device=" << InferenceDeviceName(device)
            << " providers=CPU," << InferenceProviderName(status.groundingDinoProvider) << ','
            << InferenceProviderName(status.samEncoderProvider) << ',' << InferenceProviderName(status.samDecoderProvider) << '\n'
            << "character_wall_ms=" << characterWall << " heartbeats=" << characterTicks << " max_gap_ms=" << characterGap << '\n'
            << "hair_first_wall_ms=" << firstWall << " heartbeats=" << firstTicks << " max_gap_ms=" << firstGap << '\n'
            << "hair_warm_wall_ms=" << warmWall << " model_total_ms=" << warmHair.totalMilliseconds
            << " dino_ms=" << warmHair.groundingDinoMilliseconds << " encoder_ms=" << warmHair.samTimings.encoderMilliseconds
            << " decoder_ms=" << warmHair.samTimings.decoderMilliseconds << " heartbeats=" << warmTicks << " max_gap_ms=" << warmGap << '\n'
            << "hair_box=" << warmHair.selectedBox.x1 << ',' << warmHair.selectedBox.y1 << ',' << warmHair.selectedBox.x2 << ',' << warmHair.selectedBox.y2
            << " iou=" << warmHair.predictedIou << " area=" << warmArea << '\n'
            << "cancelled=1 cancel_heartbeats=" << cancelTicks << " missing_model_rejected=1 progress_mask=" << progressMask.load() << '\n';
        if (SUCCEEDED(com)) CoUninitialize(); return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n'; if (SUCCEEDED(com)) CoUninitialize(); return 1;
    }
}
