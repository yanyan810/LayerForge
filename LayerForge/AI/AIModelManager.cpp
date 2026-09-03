#include "AIModelManager.h"

#include <exception>
#include <utility>
#include <vector>

namespace {
void AppendWarning(std::string& destination, const std::string& warning) {
    if (warning.empty() || destination.find(warning) != std::string::npos) return;
    if (!destination.empty()) destination += ' ';
    destination += warning;
}

bool Cancelled(std::stop_token token, std::string& error) {
    if (!token.stop_requested()) return false;
    error = "Analysis cancelled.";
    return true;
}
}

const char* ModelLoadStateName(ModelLoadState state) noexcept {
    switch (state) {
    case ModelLoadState::NotLoaded: return "Not loaded";
    case ModelLoadState::Loading: return "Loading";
    case ModelLoadState::Ready: return "Ready";
    case ModelLoadState::Failed: return "Failed";
    }
    return "Unknown";
}

void AIModelManager::SetCharacterState(ModelLoadState state) {
    std::scoped_lock lock(statusMutex_); status_.character = state;
}

void AIModelManager::SetHairStates(ModelLoadState dino, ModelLoadState encoder, ModelLoadState decoder) {
    std::scoped_lock lock(statusMutex_);
    status_.groundingDino = dino; status_.samEncoder = encoder; status_.samDecoder = decoder;
}

void AIModelManager::RefreshProviders() {
    std::scoped_lock lock(statusMutex_);
    status_.characterProvider = character_.Provider();
    status_.groundingDinoProvider = groundingDino_.Provider();
    status_.samEncoderProvider = sam2_.EncoderProvider();
    status_.samDecoderProvider = sam2_.DecoderProvider();
}

AIModelStatus AIModelManager::Status() const {
    std::scoped_lock lock(statusMutex_); return status_;
}

void AIModelManager::Reset() {
    std::scoped_lock modelLock(modelMutex_);
    sam2_.Reset(); groundingDino_.Reset(); character_.Reset(); samCacheGeneration_ = 0;
    std::scoped_lock statusLock(statusMutex_); status_ = {};
}

void AIModelManager::InvalidateImageCache() {
    std::scoped_lock modelLock(modelMutex_);
    sam2_.ClearImageCache(); samCacheGeneration_ = 0;
}

CharacterAnalysisResult AIModelManager::AnalyzeCharacter(const ImageData& image, const MaskAdjustmentSettings& settings,
    const AIModelPaths& paths, std::stop_token stopToken, const ProgressCallback& progress) {
    std::scoped_lock modelLock(modelMutex_);
    CharacterAnalysisResult result;
    try {
        progress(AnalysisProgress::LoadingModels);
        if (!character_.IsLoaded()) {
            SetCharacterState(ModelLoadState::Loading);
            if (!character_.Load(paths.character, InferenceDevice::Cpu, result.error)) {
                SetCharacterState(ModelLoadState::Failed); return result;
            }
            SetCharacterState(ModelLoadState::Ready); RefreshProviders();
        }
        AppendWarning(result.providerWarning, character_.ProviderWarning());
        if (Cancelled(stopToken, result.error)) return result;

        progress(AnalysisProgress::AnalyzingCharacter);
        if (!character_.Run(image, result.rawMask, result.inferenceMilliseconds, result.error)) return result;
        if (Cancelled(stopToken, result.error)) return result;

        progress(AnalysisProgress::PreparingResult);
        ImageData unusedBackground;
        const auto start = std::chrono::steady_clock::now();
        if (!maskProcessor_.Process(result.rawMask, image, settings, result.adjustedMask,
            result.foreground, result.background, result.error)) return result;
        result.maskMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        progress(AnalysisProgress::Complete);
    } catch (const std::exception& exception) {
        result.error = std::string("Character worker failed: ") + exception.what();
    } catch (...) {
        result.error = "Character worker failed with an unknown exception.";
    }
    return result;
}

HairAnalysisResult AIModelManager::AnalyzeHair(const ImageData& image, const MaskData& characterMask,
    const MaskAdjustmentSettings& settings, const AIModelPaths& paths, InferenceDevice device,
    uint64_t imageGeneration, std::stop_token stopToken, const ProgressCallback& progress) {
    std::scoped_lock modelLock(modelMutex_);
    HairAnalysisResult result;
    const auto totalStart = std::chrono::steady_clock::now();
    try {
        progress(AnalysisProgress::LoadingModels);
        if (!groundingDino_.IsLoaded()) {
            { std::scoped_lock lock(statusMutex_); status_.groundingDino = ModelLoadState::Loading; }
            if (!groundingDino_.Load(paths.groundingDino, device, result.error)) {
                { std::scoped_lock lock(statusMutex_); status_.groundingDino = ModelLoadState::Failed; }
                return result;
            }
            { std::scoped_lock lock(statusMutex_); status_.groundingDino = ModelLoadState::Ready; }
            RefreshProviders();
        }
        AppendWarning(result.providerWarning, groundingDino_.ProviderWarning());
        if (Cancelled(stopToken, result.error)) return result;

        progress(AnalysisProgress::DetectingHair);
        const auto characterBounds = hairBoxFilter_.CharacterBounds(characterMask);
        if (!characterBounds) { result.error = "Character mask has no usable bounding box."; return result; }
        const DetectionBox crop = HairBoxFilter::ExpandAndClamp(*characterBounds, 0.20f, image.width, image.height);
        std::vector<DetectionBox> candidates;
        if (!groundingDino_.Run(image, crop, candidates, result.groundingDinoMilliseconds, result.error)) return result;
        auto best = hairBoxFilter_.Select(candidates, characterMask, *characterBounds, crop);

        // Keep the Phase 4E fallback and selection rules unchanged.
        const bool touchesHorizontalCropEdge = best && (best->x1 <= crop.x1 + 2.0f || best->x2 >= crop.x2 - 2.0f);
        const bool suspicious = !best || best->confidence < 0.40f || best->rankScore < 0.20f || touchesHorizontalCropEdge;
        if (suspicious) {
            DetectionBox full; full.x2 = static_cast<float>(image.width); full.y2 = static_cast<float>(image.height);
            std::vector<DetectionBox> fallback; double fallbackMilliseconds = 0.0;
            if (groundingDino_.Run(image, full, fallback, fallbackMilliseconds, result.error)) {
                result.groundingDinoMilliseconds += fallbackMilliseconds;
                candidates.insert(candidates.end(), fallback.begin(), fallback.end());
                best = hairBoxFilter_.Select(candidates, characterMask, *characterBounds, crop);
            }
        }
        if (!best) { result.error = "No reliable hair bounding box remained after filtering."; return result; }
        result.selectedBox = *best;
        if (Cancelled(stopToken, result.error)) return result;

        progress(AnalysisProgress::LoadingModels);
        if (!sam2_.IsLoaded()) {
            SetHairStates(ModelLoadState::Ready, ModelLoadState::Loading, ModelLoadState::Loading);
            if (!sam2_.Load(paths.samEncoder, paths.samDecoder, device, InferenceDevice::Cpu, result.error)) {
                SetHairStates(ModelLoadState::Ready, ModelLoadState::Failed, ModelLoadState::Failed); return result;
            }
            SetHairStates(ModelLoadState::Ready, ModelLoadState::Ready, ModelLoadState::Ready);
            RefreshProviders();
        }
        AppendWarning(result.providerWarning, sam2_.ProviderWarning());
        if (samCacheGeneration_ != imageGeneration) { sam2_.ClearImageCache(); samCacheGeneration_ = imageGeneration; }
        if (Cancelled(stopToken, result.error)) return result;

        progress(AnalysisProgress::EncodingImage);
        if (!sam2_.Run(image, result.selectedBox, result.rawMask, result.predictedIou, result.samTimings,
            result.refinement, result.error,
            [&progress] { progress(AnalysisProgress::RefiningMask); })) return result;
        if (Cancelled(stopToken, result.error)) return result;

        progress(AnalysisProgress::PreparingResult);
        ImageData unusedBackground;
        const auto maskStart = std::chrono::steady_clock::now();
        if (!maskProcessor_.Process(result.rawMask, image, settings, result.adjustedMask,
            result.hairImage, unusedBackground, result.error)) return result;
        result.maskMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - maskStart).count();
        result.totalMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - totalStart).count();
        progress(AnalysisProgress::Complete);
    } catch (const std::exception& exception) {
        result.error = std::string("Hair worker failed: ") + exception.what();
    } catch (...) {
        result.error = "Hair worker failed with an unknown exception.";
    }
    return result;
}

SmartHairAnalysisResult AIModelManager::AnalyzeSmartHair(const ImageData& image, const DetectionBox& hairBox,
    const std::vector<Sam2PromptPoint>& prompts, uint64_t imageGeneration,
    std::stop_token stopToken, const ProgressCallback& progress) {
    std::scoped_lock modelLock(modelMutex_);
    SmartHairAnalysisResult result;
    try {
        if (Cancelled(stopToken, result.error)) return result;
        if (!sam2_.IsLoaded() || samCacheGeneration_ != imageGeneration) {
            result.error = "Smart Correction requires the cached SAM features from the current Hair analysis."; return result;
        }
        progress(AnalysisProgress::RefiningMask);
        const auto promptStart = std::chrono::steady_clock::now();
        std::vector<Sam2PromptPoint> promptCopy = prompts;
        result.promptMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - promptStart).count();
        if (!sam2_.RefineWithPrompts(image, hairBox, promptCopy, result.rawMask, result.predictedIou,
            result.decoderMilliseconds, result.error)) return result;
        if (Cancelled(stopToken, result.error)) return result;
        progress(AnalysisProgress::PreparingResult);
    } catch (const std::exception& exception) {
        result.error = std::string("Smart Correction worker failed: ") + exception.what();
    } catch (...) {
        result.error = "Smart Correction worker failed with an unknown exception.";
    }
    return result;
}
