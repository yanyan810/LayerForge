#include "Application.h"

#include "ThirdParty/imgui/backends/imgui_impl_win32.h"

#include <commdlg.h>
#include <chrono>
#include <filesystem>
#include <optional>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

namespace {
std::optional<std::filesystem::path> FindModelFrom(std::filesystem::path directory, const std::filesystem::path& relativePath) {
    constexpr int MaximumParentLevels = 6;
    for (int level = 0; level <= MaximumParentLevels && !directory.empty(); ++level) {
        const auto candidate = directory / relativePath;
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) return candidate;
        const auto parent = directory.parent_path();
        if (parent == directory) break;
        directory = parent;
    }
    return std::nullopt;
}

std::filesystem::path ResolveModelPath(const std::filesystem::path& executablePath, const std::filesystem::path& relativePath) {
    if (auto path = FindModelFrom(std::filesystem::current_path(), relativePath)) return *path;
    if (auto path = FindModelFrom(executablePath.parent_path(), relativePath)) return *path;
    return std::filesystem::current_path() / relativePath;
}

std::filesystem::path ExecutablePath() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(std::wstring_view(buffer, length));
}

ImageData MaskPreviewImage(const MaskData& mask) {
    ImageData image;
    if (!mask.IsValid()) return image;
    image.width = mask.width; image.height = mask.height;
    image.rgbaPixels.resize(mask.grayscale.size() * 4);
    for (size_t index = 0; index < mask.grayscale.size(); ++index) {
        const uint8_t value = mask.grayscale[index];
        image.rgbaPixels[index * 4] = value; image.rgbaPixels[index * 4 + 1] = value;
        image.rgbaPixels[index * 4 + 2] = value; image.rgbaPixels[index * 4 + 3] = 255;
    }
    return image;
}

void ClearTexturePreservingDescriptor(GraphicsDevice::Texture& texture) {
    const uint32_t descriptorIndex = texture.descriptorIndex;
    texture = {};
    texture.descriptorIndex = descriptorIndex;
}
}

bool Application::Initialize(HINSTANCE instance, int showCommand) {
    const wchar_t* className = L"LayerForgeWindow";
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = className;
    if (!RegisterClassExW(&wc)) { error_ = "Could not register the LayerForge window class."; return false; }

    RECT rect{ 0, 0, 1280, 800 };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    window_ = CreateWindowExW(0, className, L"LayerForge", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, this);
    if (!window_) { error_ = "Could not create the LayerForge window."; return false; }

    if (!graphics_.Initialize(window_, 1280, 800, error_)) return false;
    if (!editorUI_.Initialize(window_, graphics_, error_)) return false;
    initialized_ = true;
    ShowWindow(window_, showCommand);
    UpdateWindow(window_);
    return true;
}

int Application::Run(HINSTANCE instance, int showCommand) {
    HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (!Initialize(instance, showCommand)) {
        MessageBoxA(nullptr, error_.c_str(), "LayerForge initialization error", MB_OK | MB_ICONERROR);
        Shutdown();
        if (SUCCEEDED(comResult)) CoUninitialize();
        return 1;
    }

    MSG message{};
    while (message.message != WM_QUIT) {
        if (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessage(&message); continue; }
        if (IsIconic(window_)) { WaitMessage(); continue; }
        PollAnalysisResult();
        UpdateProgressState();
        const AIModelStatus modelStatus = modelManager_.Status();
        editorUI_.BeginFrame();
        editorUI_.Draw(image_.IsValid() ? &image_ : nullptr, texture_.IsValid() ? &texture_ : nullptr,
            rawMask_.IsValid() ? &rawMask_ : nullptr, rawMaskTexture_.IsValid() ? &rawMaskTexture_ : nullptr,
            adjustedMask_.IsValid() ? &adjustedMask_ : nullptr, adjustedMaskTexture_.IsValid() ? &adjustedMaskTexture_ : nullptr,
            foreground_.IsValid() ? &foreground_ : nullptr, foregroundTexture_.IsValid() ? &foregroundTexture_ : nullptr,
            background_.IsValid() ? &background_ : nullptr, backgroundTexture_.IsValid() ? &backgroundTexture_ : nullptr,
            hairBox_.IsValid() ? &hairBox_ : nullptr, &samRefinement_,
            hairRawMask_.IsValid() ? &hairRawMask_ : nullptr, hairRawMaskTexture_.IsValid() ? &hairRawMaskTexture_ : nullptr,
            hairAdjustedMask_.IsValid() ? &hairAdjustedMask_ : nullptr, hairAdjustedMaskTexture_.IsValid() ? &hairAdjustedMaskTexture_ : nullptr,
            hairFinalMask_.IsValid() ? &hairFinalMask_ : nullptr, hairFinalMaskTexture_.IsValid() ? &hairFinalMaskTexture_ : nullptr,
            hairEditPreview_.IsValid() ? &hairEditPreview_ : nullptr, hairEditPreviewTexture_.IsValid() ? &hairEditPreviewTexture_ : nullptr,
            hairImage_.IsValid() ? &hairImage_ : nullptr, hairTexture_.IsValid() ? &hairTexture_ : nullptr,
            maskSettings_, hairMaskSettings_, hairMaskEditor_, inferenceDevice_, error_, providerWarning_, analyzing_, hairAnalysisStage_, inferenceMilliseconds_, maskUpdateMilliseconds_,
            groundingDinoMilliseconds_, sam2Timings_, hairTotalMilliseconds_, hairMaskUpdateMilliseconds_, samPredictedIou_,
            modelStatus.character, modelStatus.groundingDino, modelStatus.samEncoder, modelStatus.samDecoder,
            modelStatus.characterProvider, modelStatus.groundingDinoProvider, modelStatus.samEncoderProvider, modelStatus.samDecoderProvider,
            [this] { OpenImage(); }, [this] { StartCharacterAnalysis(); }, [this] { StartHairAnalysis(); },
            [this] { maskUpdateRequested_ = true; }, [this] { hairMaskUpdateRequested_ = true; },
            [this] { hairManualUpdateRequested_ = true; }, [this] { ChangeInferenceDevice(); });
        graphics_.BeginFrame();
        editorUI_.Render(graphics_.CommandList());
        graphics_.EndFrame();
        if (maskUpdateRequested_) RebuildDerivedLayers();
        else if (hairMaskUpdateRequested_) RebuildHairLayers();
        else if (hairManualUpdateRequested_) {
            // Keep painting responsive: the cursor and CPU stroke update every frame,
            // while full-resolution textures are rebuilt once when the drag ends.
            if (!hairMaskEditor_.IsStrokeActive()) RebuildManualHairLayers();
        }
    }
    Shutdown();
    if (SUCCEEDED(comResult)) CoUninitialize();
    return static_cast<int>(message.wParam);
}

void Application::OpenImage() {
    wchar_t path[MAX_PATH]{};
    OPENFILENAMEW dialog{ sizeof(dialog) };
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = L"Image Files (*.png;*.jpg;*.jpeg)\0*.png;*.jpg;*.jpeg\0PNG Files (*.png)\0*.png\0JPEG Files (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    dialog.lpstrDefExt = L"png";
    if (!GetOpenFileNameW(&dialog)) return;

    ImageData candidate;
    std::string loadError;
    if (!imageLoader_.Load(std::filesystem::path(path), candidate, loadError)) { error_ = std::move(loadError); return; }
    if (!graphics_.CreateTexture(candidate, texture_, loadError)) { error_ = std::move(loadError); return; }
    const bool hadActiveAnalysis = analyzing_;
    CancelActiveAnalysis();
    ++imageGeneration_;
    if (!hadActiveAnalysis) modelManager_.InvalidateImageCache();
    image_ = std::move(candidate);
    ResetAnalysis();
    error_.clear();
}

ImageData MaskOverlayImage(const ImageData& original, const MaskData& mask) {
    ImageData image;
    if (!original.IsValid() || !mask.IsValid() || original.width != mask.width || original.height != mask.height) return image;
    image.width = original.width; image.height = original.height; image.rgbaPixels = original.rgbaPixels;
    constexpr float overlayStrength = 0.48f;
    for (size_t index = 0; index < mask.grayscale.size(); ++index) {
        const float alpha = mask.grayscale[index] / 255.0f * overlayStrength;
        const size_t rgba = index * 4;
        image.rgbaPixels[rgba] = static_cast<uint8_t>(image.rgbaPixels[rgba] * (1.0f - alpha) + 35.0f * alpha);
        image.rgbaPixels[rgba + 1] = static_cast<uint8_t>(image.rgbaPixels[rgba + 1] * (1.0f - alpha) + 145.0f * alpha);
        image.rgbaPixels[rgba + 2] = static_cast<uint8_t>(image.rgbaPixels[rgba + 2] * (1.0f - alpha) + 255.0f * alpha);
        image.rgbaPixels[rgba + 3] = 255;
    }
    return image;
}

void Application::ResetAnalysis() {
    rawMask_ = {}; adjustedMask_ = {}; foreground_ = {}; background_ = {};
    ClearTexturePreservingDescriptor(rawMaskTexture_); ClearTexturePreservingDescriptor(adjustedMaskTexture_);
    ClearTexturePreservingDescriptor(foregroundTexture_); ClearTexturePreservingDescriptor(backgroundTexture_);
    maskSettings_.Reset(); inferenceMilliseconds_ = 0.0; maskUpdateMilliseconds_ = 0.0;
    ResetHairResult(); hairMaskSettings_.Reset();
    maskUpdateRequested_ = false;
}

void Application::ResetHairResult() {
    hairRawMask_ = {}; hairAdjustedMask_ = {}; hairFinalMask_ = {}; hairImage_ = {}; hairEditPreview_ = {}; hairBox_ = {};
    hairMaskEditor_.Clear();
    ClearTexturePreservingDescriptor(hairRawMaskTexture_); ClearTexturePreservingDescriptor(hairAdjustedMaskTexture_);
    ClearTexturePreservingDescriptor(hairFinalMaskTexture_); ClearTexturePreservingDescriptor(hairEditPreviewTexture_);
    ClearTexturePreservingDescriptor(hairTexture_);
    groundingDinoMilliseconds_ = 0.0; sam2Timings_ = {}; hairTotalMilliseconds_ = 0.0;
    hairMaskUpdateMilliseconds_ = 0.0; samPredictedIou_ = 0.0f; samRefinement_ = {};
    hairMaskUpdateRequested_ = hairManualUpdateRequested_ = false;
    hairAnalysisStage_ = adjustedMask_.IsValid() ? HairAnalysisStage::Ready : HairAnalysisStage::NotReady;
}

AIModelPaths Application::ModelPaths() const {
    const auto executable = ExecutablePath();
    return {
        ResolveModelPath(executable, L"Models/segmentation/u2netp.onnx"),
        ResolveModelPath(executable, L"Models/GroundingDINO/grounding-dino-base-hair-800x1333.onnx"),
        ResolveModelPath(executable, L"Models/SAM2/sam2.1-hiera-small-encoder.onnx"),
        ResolveModelPath(executable, L"Models/SAM2/sam2.1-hiera-small-prompt-decoder.onnx")
    };
}

void Application::StartCharacterAnalysis() {
    if (!image_.IsValid() || analyzing_) return;
    analyzing_ = true; activeJobType_ = AnalysisJobType::Character; activeJobId_ = ++nextJobId_;
    const uint64_t jobId = activeJobId_, generation = imageGeneration_;
    const ImageData image = image_; const MaskAdjustmentSettings settings = maskSettings_; const AIModelPaths paths = ModelPaths();
    error_.clear(); workerProgress_.store(AnalysisProgress::LoadingModels, std::memory_order_release);
    analysisWorker_ = std::jthread([this, image, settings, paths, jobId, generation](std::stop_token stopToken) mutable {
        WorkerResult completed; completed.type = AnalysisJobType::Character; completed.jobId = jobId; completed.imageGeneration = generation;
        try {
            completed.character = modelManager_.AnalyzeCharacter(image, settings, paths, stopToken,
                [this](AnalysisProgress progress) { workerProgress_.store(progress, std::memory_order_release); });
        } catch (const std::exception& exception) {
            completed.character.error = std::string("Character analysis worker failed: ") + exception.what();
        } catch (...) {
            completed.character.error = "Character analysis worker failed with an unknown exception.";
        }
        std::scoped_lock lock(resultMutex_); pendingResult_ = std::move(completed);
    });
}

void Application::RebuildDerivedLayers() {
    maskUpdateRequested_ = false;
    if (!rawMask_.IsValid()) return;
    const auto start = std::chrono::steady_clock::now();

    if (!maskProcessor_.Process(rawMask_, image_, maskSettings_, adjustedMask_, foreground_, background_, error_)) return;
    const ImageData adjustedPreview = MaskPreviewImage(adjustedMask_);
    if (!graphics_.CreateTexture(adjustedPreview, adjustedMaskTexture_, error_) ||
        !graphics_.CreateTexture(foreground_, foregroundTexture_, error_) ||
        !graphics_.CreateTexture(background_, backgroundTexture_, error_)) return;
    maskUpdateMilliseconds_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    ResetHairResult();
    error_.clear();
}

void Application::StartHairAnalysis() {
    if (!image_.IsValid() || !adjustedMask_.IsValid() || analyzing_) return;
    analyzing_ = true; activeJobType_ = AnalysisJobType::Hair; activeJobId_ = ++nextJobId_;
    const uint64_t jobId = activeJobId_, generation = imageGeneration_;
    const ImageData image = image_; const MaskData characterMask = adjustedMask_;
    const MaskAdjustmentSettings settings = hairMaskSettings_; const AIModelPaths paths = ModelPaths();
    const InferenceDevice device = inferenceDevice_;
    error_.clear(); hairAnalysisStage_ = HairAnalysisStage::LoadingModels;
    workerProgress_.store(AnalysisProgress::LoadingModels, std::memory_order_release);
    analysisWorker_ = std::jthread([this, image, characterMask, settings, paths, device, jobId, generation](std::stop_token stopToken) mutable {
        WorkerResult completed; completed.type = AnalysisJobType::Hair; completed.jobId = jobId; completed.imageGeneration = generation;
        try {
            completed.hair = modelManager_.AnalyzeHair(image, characterMask, settings, paths, device, generation, stopToken,
                [this](AnalysisProgress progress) { workerProgress_.store(progress, std::memory_order_release); });
        } catch (const std::exception& exception) {
            completed.hair.error = std::string("Hair analysis worker failed: ") + exception.what();
        } catch (...) {
            completed.hair.error = "Hair analysis worker failed with an unknown exception.";
        }
        std::scoped_lock lock(resultMutex_); pendingResult_ = std::move(completed);
    });
}

void Application::PollAnalysisResult() {
    std::optional<WorkerResult> completed;
    {
        std::scoped_lock lock(resultMutex_);
        if (pendingResult_) { completed = std::move(pendingResult_); pendingResult_.reset(); }
    }
    if (!completed) return;
    if (completed->jobId != activeJobId_) return;

    analyzing_ = false; activeJobType_ = AnalysisJobType::None;
    workerProgress_.store(AnalysisProgress::Idle, std::memory_order_release);
    if (completed->imageGeneration != imageGeneration_) {
        modelManager_.InvalidateImageCache();
        hairAnalysisStage_ = adjustedMask_.IsValid() ? HairAnalysisStage::Ready : HairAnalysisStage::NotReady;
        return;
    }
    if (completed->type == AnalysisJobType::Character) ApplyCharacterResult(std::move(completed->character));
    else if (completed->type == AnalysisJobType::Hair) ApplyHairResult(std::move(completed->hair));
}

void Application::UpdateProgressState() {
    if (!analyzing_ || activeJobType_ != AnalysisJobType::Hair) return;
    switch (workerProgress_.load(std::memory_order_acquire)) {
    case AnalysisProgress::LoadingModels: hairAnalysisStage_ = HairAnalysisStage::LoadingModels; break;
    case AnalysisProgress::DetectingHair: hairAnalysisStage_ = HairAnalysisStage::Detecting; break;
    case AnalysisProgress::EncodingImage: hairAnalysisStage_ = HairAnalysisStage::Encoding; break;
    case AnalysisProgress::RefiningMask: hairAnalysisStage_ = HairAnalysisStage::Refining; break;
    case AnalysisProgress::PreparingResult: hairAnalysisStage_ = HairAnalysisStage::Preparing; break;
    default: break;
    }
}

void Application::ApplyCharacterResult(CharacterAnalysisResult&& result) {
    AppendProviderWarning(result.providerWarning);
    if (!result.error.empty()) { if (result.error != "Analysis cancelled.") error_ = std::move(result.error); return; }

    GraphicsDevice::Texture rawTexture, adjustedTexture, foregroundTexture, backgroundTexture;
    const ImageData rawPreview = MaskPreviewImage(result.rawMask);
    const ImageData adjustedPreview = MaskPreviewImage(result.adjustedMask);
    std::string textureError;
    if (!graphics_.PrepareTexture(rawPreview, rawTexture, textureError) ||
        !graphics_.PrepareTexture(adjustedPreview, adjustedTexture, textureError) ||
        !graphics_.PrepareTexture(result.foreground, foregroundTexture, textureError) ||
        !graphics_.PrepareTexture(result.background, backgroundTexture, textureError)) {
        error_ = std::move(textureError); return;
    }
    if (!graphics_.CommitTexture(std::move(rawTexture), rawMaskTexture_, textureError) ||
        !graphics_.CommitTexture(std::move(adjustedTexture), adjustedMaskTexture_, textureError) ||
        !graphics_.CommitTexture(std::move(foregroundTexture), foregroundTexture_, textureError) ||
        !graphics_.CommitTexture(std::move(backgroundTexture), backgroundTexture_, textureError)) {
        error_ = std::move(textureError); return;
    }
    rawMask_ = std::move(result.rawMask); adjustedMask_ = std::move(result.adjustedMask);
    foreground_ = std::move(result.foreground); background_ = std::move(result.background);
    inferenceMilliseconds_ = result.inferenceMilliseconds; maskUpdateMilliseconds_ = result.maskMilliseconds;
    ResetHairResult(); hairMaskSettings_.Reset(); error_.clear();
}

void Application::ApplyHairResult(HairAnalysisResult&& result) {
    AppendProviderWarning(result.providerWarning);
    if (!result.error.empty()) {
        if (result.error != "Analysis cancelled.") error_ = std::move(result.error);
        hairAnalysisStage_ = HairAnalysisStage::Failed; return;
    }

    hairMaskEditor_.Initialize(result.adjustedMask.width, result.adjustedMask.height);
    MaskData finalMask;
    if (!hairMaskEditor_.Apply(result.adjustedMask, finalMask)) {
        error_ = "Could not initialize the manual Hair mask layer."; hairAnalysisStage_ = HairAnalysisStage::Failed; return;
    }
    ImageData hairImage, unusedBackground;
    if (!maskProcessor_.Compose(finalMask, image_, hairImage, unusedBackground, error_)) {
        hairAnalysisStage_ = HairAnalysisStage::Failed; return;
    }
    const ImageData editPreview = MaskOverlayImage(image_, finalMask);
    GraphicsDevice::Texture rawTexture, adjustedTexture, finalTexture, editTexture, hairTexture;
    const ImageData rawPreview = MaskPreviewImage(result.rawMask);
    const ImageData adjustedPreview = MaskPreviewImage(result.adjustedMask);
    std::string textureError;
    if (!graphics_.PrepareTexture(rawPreview, rawTexture, textureError) ||
        !graphics_.PrepareTexture(adjustedPreview, adjustedTexture, textureError) ||
        !graphics_.PrepareTexture(MaskPreviewImage(finalMask), finalTexture, textureError) ||
        !graphics_.PrepareTexture(editPreview, editTexture, textureError) ||
        !graphics_.PrepareTexture(hairImage, hairTexture, textureError)) {
        error_ = std::move(textureError); hairAnalysisStage_ = HairAnalysisStage::Failed; return;
    }
    if (!graphics_.CommitTexture(std::move(rawTexture), hairRawMaskTexture_, textureError) ||
        !graphics_.CommitTexture(std::move(adjustedTexture), hairAdjustedMaskTexture_, textureError) ||
        !graphics_.CommitTexture(std::move(finalTexture), hairFinalMaskTexture_, textureError) ||
        !graphics_.CommitTexture(std::move(editTexture), hairEditPreviewTexture_, textureError) ||
        !graphics_.CommitTexture(std::move(hairTexture), hairTexture_, textureError)) {
        error_ = std::move(textureError); hairAnalysisStage_ = HairAnalysisStage::Failed; return;
    }
    hairRawMask_ = std::move(result.rawMask); hairAdjustedMask_ = std::move(result.adjustedMask);
    hairFinalMask_ = std::move(finalMask); hairImage_ = std::move(hairImage); hairEditPreview_ = editPreview; hairBox_ = result.selectedBox;
    groundingDinoMilliseconds_ = result.groundingDinoMilliseconds; sam2Timings_ = result.samTimings;
    hairTotalMilliseconds_ = result.totalMilliseconds; hairMaskUpdateMilliseconds_ = result.maskMilliseconds;
    samPredictedIou_ = result.predictedIou; samRefinement_ = std::move(result.refinement);
    hairAnalysisStage_ = HairAnalysisStage::Complete; error_.clear();
}

void Application::CancelActiveAnalysis() {
    if (analysisWorker_.joinable()) analysisWorker_.request_stop();
}

void Application::AppendProviderWarning(const std::string& warning) {
    if (warning.empty() || providerWarning_.find(warning) != std::string::npos) return;
    if (!providerWarning_.empty()) providerWarning_ += ' ';
    providerWarning_ += warning;
}

void Application::ChangeInferenceDevice() {
    if (analyzing_) return;
    graphics_.WaitForGpu();
    modelManager_.Reset();
    ResetAnalysis(); error_.clear(); providerWarning_.clear();
}

void Application::RebuildHairLayers() {
    hairMaskUpdateRequested_ = false;
    if (!hairRawMask_.IsValid()) return;
    const auto start = std::chrono::steady_clock::now();
    ImageData unusedAutoHair, unusedBackground;
    if (!maskProcessor_.Process(hairRawMask_, image_, hairMaskSettings_, hairAdjustedMask_, unusedAutoHair, unusedBackground, error_)) return;
    RebuildManualHairLayers();
    hairMaskUpdateMilliseconds_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    error_.clear();
}

void Application::RebuildManualHairLayers() {
    hairManualUpdateRequested_ = false;
    if (!hairAdjustedMask_.IsValid() || !hairMaskEditor_.IsInitialized()) return;
    const auto start = std::chrono::steady_clock::now();
    MaskData finalMask; ImageData hairImage, unusedBackground;
    if (!hairMaskEditor_.Apply(hairAdjustedMask_, finalMask) ||
        !maskProcessor_.Compose(finalMask, image_, hairImage, unusedBackground, error_)) {
        if (error_.empty()) error_ = "Could not rebuild the manual Hair mask.";
        return;
    }
    const ImageData finalPreview = MaskPreviewImage(finalMask);
    const ImageData editPreview = MaskOverlayImage(image_, finalMask);
    GraphicsDevice::Texture finalTexture, editTexture, hairTexture;
    std::string textureError;
    if (!graphics_.PrepareTexture(finalPreview, finalTexture, textureError) ||
        !graphics_.PrepareTexture(editPreview, editTexture, textureError) ||
        !graphics_.PrepareTexture(hairImage, hairTexture, textureError) ||
        !graphics_.CommitTexture(std::move(finalTexture), hairFinalMaskTexture_, textureError) ||
        !graphics_.CommitTexture(std::move(editTexture), hairEditPreviewTexture_, textureError) ||
        !graphics_.CommitTexture(std::move(hairTexture), hairTexture_, textureError)) {
        error_ = std::move(textureError); return;
    }
    hairFinalMask_ = std::move(finalMask); hairImage_ = std::move(hairImage); hairEditPreview_ = editPreview;
    hairMaskEditor_.ClearStrokePreview();
    hairMaskUpdateMilliseconds_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    error_.clear();
}

void Application::OnResize(uint32_t width, uint32_t height) { if (initialized_) graphics_.Resize(width, height); }

void Application::Shutdown() {
    CancelActiveAnalysis();
    if (analysisWorker_.joinable()) analysisWorker_.join();
    { std::scoped_lock lock(resultMutex_); pendingResult_.reset(); }
    analyzing_ = false; activeJobType_ = AnalysisJobType::None;
    modelManager_.Reset();
    if (initialized_) { graphics_.WaitForGpu(); editorUI_.Shutdown(); ResetAnalysis(); texture_ = {}; graphics_.Shutdown(); initialized_ = false; }
    if (window_) { DestroyWindow(window_); window_ = nullptr; }
}

LRESULT CALLBACK Application::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam)) return TRUE;
    Application* app = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = static_cast<Application*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    switch (message) {
    case WM_SIZE: if (app && wParam != SIZE_MINIMIZED) app->OnResize(LOWORD(lParam), HIWORD(lParam)); return 0;
    case WM_GETMINMAXINFO: reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = { 640, 480 }; return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: return DefWindowProcW(window, message, wParam, lParam);
    }
}
