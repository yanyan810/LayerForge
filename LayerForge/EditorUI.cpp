#include "EditorUI.h"

#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/imgui/backends/imgui_impl_dx12.h"
#include "ThirdParty/imgui/backends/imgui_impl_win32.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace {
void DrawFittedTexture(const GraphicsDevice::Texture* texture, uint32_t width, uint32_t height, bool checkerboard,
    const DetectionBox* overlay = nullptr, const std::vector<Sam2PromptPoint>* prompts = nullptr,
    MaskEditor* maskEditor = nullptr, bool editEnabled = false, const std::function<void()>* manualChanged = nullptr,
    SmartMaskCorrection* smartCorrection = nullptr, SmartCorrectionMode smartMode = SmartCorrectionMode::Add,
    float smartSize = 1.0f, bool smartEnabled = false,
    const std::function<void(const SmartStrokeRequest&)>* smartStrokeCompleted = nullptr) {
    const ImVec2 available = ImGui::GetContentRegionAvail();
    if (!texture || !texture->IsValid() || width == 0 || height == 0) {
        const char* message = "Not available. Run Analyze Image first.";
        const ImVec2 text = ImGui::CalcTextSize(message);
        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + std::max(0.0f, (available.x - text.x) * 0.5f), ImGui::GetCursorPosY() + std::max(0.0f, (available.y - text.y) * 0.5f)));
        ImGui::TextDisabled("%s", message);
        return;
    }

    const float scale = std::min(available.x / width, available.y / height);
    const ImVec2 size(std::max(1.0f, width * scale), std::max(1.0f, height * scale));
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + std::max(0.0f, (available.x - size.x) * 0.5f), ImGui::GetCursorPosY() + std::max(0.0f, (available.y - size.y) * 0.5f)));
    const ImVec2 minimum = ImGui::GetCursorScreenPos();
    const ImVec2 maximum(minimum.x + size.x, minimum.y + size.y);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (checkerboard) {
        constexpr float cell = 16.0f;
        constexpr ImU32 light = IM_COL32(176, 176, 176, 255), dark = IM_COL32(112, 112, 112, 255);
        for (float y = minimum.y; y < maximum.y; y += cell) {
            for (float x = minimum.x; x < maximum.x; x += cell) {
                const int column = static_cast<int>((x - minimum.x) / cell);
                const int row = static_cast<int>((y - minimum.y) / cell);
                drawList->AddRectFilled(ImVec2(x, y), ImVec2(std::min(x + cell, maximum.x), std::min(y + cell, maximum.y)), ((column + row) & 1) ? dark : light);
            }
        }
    }
    drawList->AddImage(static_cast<ImTextureID>(texture->gpuHandle.ptr), minimum, maximum);
    if (overlay && overlay->IsValid()) {
        const float scaleX = size.x / width, scaleY = size.y / height;
        const ImVec2 boxMinimum(minimum.x + overlay->x1 * scaleX, minimum.y + overlay->y1 * scaleY);
        const ImVec2 boxMaximum(minimum.x + overlay->x2 * scaleX, minimum.y + overlay->y2 * scaleY);
        constexpr ImU32 color = IM_COL32(40, 220, 120, 255);
        drawList->AddRect(boxMinimum, boxMaximum, color, 0.0f, 0, 3.0f);
        char label[64]{}; snprintf(label, sizeof(label), "Hair %.3f", overlay->confidence);
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        drawList->AddRectFilled(boxMinimum, ImVec2(boxMinimum.x + textSize.x + 8.0f, boxMinimum.y + textSize.y + 6.0f), IM_COL32(0, 0, 0, 190));
        drawList->AddText(ImVec2(boxMinimum.x + 4.0f, boxMinimum.y + 3.0f), color, label);
    }
    if (prompts) {
        const float scaleX = size.x / width, scaleY = size.y / height;
        for (const Sam2PromptPoint& point : *prompts) {
            const ImVec2 center(minimum.x + point.x * scaleX, minimum.y + point.y * scaleY);
            const ImU32 color = point.label == 1 ? IM_COL32(70, 255, 100, 255) : IM_COL32(255, 70, 70, 255);
            drawList->AddCircle(center, 7.0f, color, 16, 3.0f);
            drawList->AddLine(ImVec2(center.x - 5.0f, center.y), ImVec2(center.x + 5.0f, center.y), color, 2.0f);
            if (point.label == 1) drawList->AddLine(ImVec2(center.x, center.y - 5.0f), ImVec2(center.x, center.y + 5.0f), color, 2.0f);
        }
    }
    ImGui::SetCursorScreenPos(minimum);
    ImGui::InvisibleButton("##TextureSurface", size, ImGuiButtonFlags_MouseButtonLeft);
    if (maskEditor && editEnabled) {
        const auto& stroke = maskEditor->StrokePreview();
        if (!stroke.empty()) {
            const float scaleX = size.x / width, scaleY = size.y / height;
            const int alpha = std::clamp(static_cast<int>(70 + maskEditor->StrokePreviewStrength() * 100.0f), 70, 170);
            const ImU32 strokeColor = maskEditor->StrokePreviewMode() == MaskBrushMode::Add ?
                IM_COL32(45, 255, 95, alpha) : IM_COL32(255, 55, 55, alpha);
            const float radius = std::max(1.0f, maskEditor->StrokePreviewSize() * 0.5f * scaleX);
            for (size_t index = 0; index < stroke.size(); ++index) {
                const ImVec2 point(minimum.x + stroke[index].x * scaleX, minimum.y + stroke[index].y * scaleY);
                drawList->AddCircleFilled(point, radius, strokeColor, 24);
                if (index > 0) {
                    const ImVec2 previous(minimum.x + stroke[index - 1].x * scaleX, minimum.y + stroke[index - 1].y * scaleY);
                    drawList->AddLine(previous, point, strokeColor, radius * 2.0f);
                }
            }
        }
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        float imageX = 0.0f, imageY = 0.0f;
        const bool mapped = hovered && MaskEditor::ScreenToImage(mouse.x, mouse.y, minimum.x, minimum.y,
            size.x, size.y, width, height, imageX, imageY);
        if (mapped) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
            const float radius = maskEditor->Settings().size * 0.5f * (size.x / width);
            const ImU32 cursorColor = maskEditor->Settings().mode == MaskBrushMode::Add ?
                IM_COL32(75, 255, 110, 255) : IM_COL32(255, 80, 80, 255);
            drawList->AddCircle(mouse, std::max(1.0f, radius), cursorColor, 48, 2.0f);
            drawList->AddText(ImVec2(mouse.x + radius + 6.0f, mouse.y + 5.0f), cursorColor,
                maskEditor->Settings().mode == MaskBrushMode::Add ? "ADD" : "ERASE");
        }
        if (mapped && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (maskEditor->BeginStroke(imageX, imageY) && manualChanged) (*manualChanged)();
        } else if (mapped && ImGui::IsMouseDown(ImGuiMouseButton_Left) && maskEditor->IsStrokeActive()) {
            if (maskEditor->ContinueStroke(imageX, imageY) && manualChanged) (*manualChanged)();
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && maskEditor->IsStrokeActive()) {
            if (maskEditor->EndStroke() && manualChanged) (*manualChanged)();
        }
    }
    if (smartCorrection && smartEnabled) {
        const float scaleX = size.x / width, scaleY = size.y / height;
        const auto& stroke = smartCorrection->StrokePreview();
        const ImU32 color = smartCorrection->Mode() == SmartCorrectionMode::Add ?
            IM_COL32(55, 255, 105, 220) : IM_COL32(255, 65, 65, 220);
        if (!stroke.empty()) {
            const float radius = std::max(1.0f, smartCorrection->BrushSize() * 0.5f * scaleX);
            for (size_t index = 0; index < stroke.size(); ++index) {
                const ImVec2 point(minimum.x + stroke[index].x * scaleX, minimum.y + stroke[index].y * scaleY);
                drawList->AddCircle(point, radius, color, 32, 2.0f);
                if (index > 0) {
                    const ImVec2 previous(minimum.x + stroke[index - 1].x * scaleX,
                        minimum.y + stroke[index - 1].y * scaleY);
                    drawList->AddLine(previous, point, color, 3.0f);
                }
            }
        }
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        float imageX = 0.0f, imageY = 0.0f;
        const bool mapped = hovered && MaskEditor::ScreenToImage(mouse.x, mouse.y, minimum.x, minimum.y,
            size.x, size.y, width, height, imageX, imageY);
        if (mapped) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
            const ImU32 cursorColor = smartMode == SmartCorrectionMode::Add ?
                IM_COL32(75, 255, 115, 255) : IM_COL32(255, 80, 80, 255);
            const float radius = std::max(1.0f, smartSize * 0.5f * scaleX);
            drawList->AddCircle(mouse, radius, cursorColor, 48, 2.0f);
            drawList->AddText(ImVec2(mouse.x + radius + 6.0f, mouse.y + 5.0f), cursorColor,
                smartMode == SmartCorrectionMode::Add ? "SMART ADD" : "SMART ERASE");
        }
        if (mapped && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            smartCorrection->BeginStroke(imageX, imageY, smartSize, smartMode, width, height);
        } else if (mapped && ImGui::IsMouseDown(ImGuiMouseButton_Left) && smartCorrection->IsStrokeActive()) {
            smartCorrection->ContinueStroke(imageX, imageY);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && smartCorrection->IsStrokeActive()) {
            SmartStrokeRequest request;
            if (smartCorrection->EndStroke(request) && smartStrokeCompleted) (*smartStrokeCompleted)(request);
        }
    }
}

const char* HairStatusText(HairAnalysisStage stage) {
    switch (stage) {
    case HairAnalysisStage::NotReady: return "Waiting for Character analysis";
    case HairAnalysisStage::Ready: return "Ready";
    case HairAnalysisStage::LoadingModels: return "Loading AI models...";
    case HairAnalysisStage::Detecting: return "Detecting hair...";
    case HairAnalysisStage::Encoding: return "Encoding image...";
    case HairAnalysisStage::Refining: return "Refining hair mask...";
    case HairAnalysisStage::Preparing: return "Preparing result...";
    case HairAnalysisStage::Complete: return "Complete";
    case HairAnalysisStage::Failed: return "Failed";
    }
    return "Unknown";
}
}

bool EditorUI::Initialize(HWND window, GraphicsDevice& graphics, std::string& error) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.FrameRounding = 4.0f;

    const char* fonts[] = { "C:/Windows/Fonts/meiryo.ttc", "C:/Windows/Fonts/YuGothM.ttc" };
    for (const char* font : fonts) {
        if (std::filesystem::exists(font)) { io.Fonts->AddFontFromFileTTF(font, 17.0f, nullptr, io.Fonts->GetGlyphRangesJapanese()); break; }
    }

    if (!ImGui_ImplWin32_Init(window)) { error = "ImGui Win32 initialization failed."; return false; }
    ImGui_ImplDX12_InitInfo info{};
    info.Device = graphics.Device();
    info.CommandQueue = graphics.CommandQueue();
    info.NumFramesInFlight = GraphicsDevice::FrameCount;
    info.RTVFormat = graphics.BackBufferFormat();
    info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    info.SrvDescriptorHeap = graphics.SrvHeap();
    info.UserData = &graphics;
    info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* init, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
        auto& device = *static_cast<GraphicsDevice*>(init->UserData);
        const uint32_t index = device.AllocateSrv();
        *cpu = device.CpuSrv(index);
        *gpu = device.GpuSrv(index);
    };
    info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {};
    if (!ImGui_ImplDX12_Init(&info)) { ImGui_ImplWin32_Shutdown(); error = "ImGui DirectX 12 initialization failed."; return false; }
    initialized_ = true;
    return true;
}

void EditorUI::Shutdown() {
    if (!initialized_) return;
    ImGui_ImplDX12_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext(); initialized_ = false;
}

void EditorUI::BeginFrame() { ImGui_ImplDX12_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame(); }

void EditorUI::Draw(const ImageData* image, const GraphicsDevice::Texture* texture,
    const MaskData* rawMask, const GraphicsDevice::Texture* rawMaskTexture,
    const MaskData* adjustedMask, const GraphicsDevice::Texture* adjustedMaskTexture,
    const ImageData* foreground, const GraphicsDevice::Texture* foregroundTexture,
    const ImageData* background, const GraphicsDevice::Texture* backgroundTexture,
    const DetectionBox* hairBox, const Sam2RefinementInfo* samRefinement,
    const MaskData* hairRawMask, const GraphicsDevice::Texture* hairRawMaskTexture,
    const MaskData* hairAdjustedMask, const GraphicsDevice::Texture* hairAdjustedMaskTexture,
    const MaskData* hairFinalMask, const GraphicsDevice::Texture* hairFinalMaskTexture,
    const ImageData* hairEditPreview, const GraphicsDevice::Texture* hairEditPreviewTexture,
    const ImageData* hairImage, const GraphicsDevice::Texture* hairTexture,
    MaskAdjustmentSettings& maskSettings, MaskAdjustmentSettings& hairMaskSettings, MaskEditor& hairMaskEditor,
    SmartMaskCorrection& smartMaskCorrection, const std::vector<Sam2PromptPoint>* smartPrompts,
    const MaskData* smartCandidateMask, const GraphicsDevice::Texture* smartCandidateTexture,
    const ImageData* smartDifferenceImage, const GraphicsDevice::Texture* smartDifferenceTexture,
    bool smartCandidateAvailable, double smartPromptMilliseconds, double smartDecoderMilliseconds,
    double smartMaskMilliseconds, double smartTextureMilliseconds, double smartTotalMilliseconds, float smartPredictedIou,
    InferenceDevice& inferenceDevice,
    const std::string& error, const std::string& providerWarning, bool analyzing, HairAnalysisStage hairStage,
    double inferenceMilliseconds, double maskUpdateMilliseconds, double groundingDinoMilliseconds,
    const Sam2Timings& samTimings, double hairTotalMilliseconds, double hairMaskUpdateMilliseconds, float samPredictedIou,
    ModelLoadState u2netState, ModelLoadState dinoState, ModelLoadState samEncoderState, ModelLoadState samDecoderState,
    InferenceProvider u2netProvider,
    InferenceProvider dinoProvider, InferenceProvider samEncoderProvider, InferenceProvider samDecoderProvider,
    const std::function<void()>& openImage, const std::function<void()>& analyzeImage,
    const std::function<void()>& analyzeHair, const std::function<void()>& maskSettingsChanged,
    const std::function<void()>& hairMaskSettingsChanged, const std::function<void()>& hairManualChanged,
    const std::function<void(const SmartStrokeRequest&)>& smartStrokeCompleted,
    const std::function<void()>& applySmartCandidate, const std::function<void()>& cancelSmartCandidate,
    const std::function<void()>& resetManualHairEdit,
    const std::function<void()>& inferenceDeviceChanged) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("LayerForge", nullptr, flags);
    if (hairMaskEditor.IsStrokeActive() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (hairMaskEditor.EndStroke()) hairManualChanged();
    }
    if (smartMaskCorrection.IsStrokeActive() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        SmartStrokeRequest request;
        if (smartMaskCorrection.EndStroke(request)) smartStrokeCompleted(request);
    }
    if (smartCandidateAvailable && !smartCandidateWasAvailable_) selectSmartDifferenceTab_ = true;
    if (!smartCandidateAvailable) selectSmartDifferenceTab_ = false;
    smartCandidateWasAvailable_ = smartCandidateAvailable;

    ImGui::TextUnformatted("LayerForge");
    ImGui::SameLine(ImGui::GetWindowWidth() - 145.0f);
    if (ImGui::Button("Open Image", ImVec2(125.0f, 0.0f))) openImage();
    ImGui::Separator();

    if (!error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.28f, 0.07f, 0.07f, 1.0f));
        ImGui::BeginChild("Error", ImVec2(0.0f, 44.0f), ImGuiChildFlags_Borders);
        ImGui::TextWrapped("Error: %s", error.c_str());
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    if (!providerWarning.empty()) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.25f, 0.20f, 0.04f, 1.0f));
        ImGui::BeginChild("Provider Warning", ImVec2(0.0f, 44.0f), ImGuiChildFlags_Borders);
        ImGui::TextWrapped("Notice: %s", providerWarning.c_str());
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    if (image && image->IsValid()) {
        ImGui::Text("File: %s", image->fileNameUtf8.c_str());
        ImGui::SameLine(0.0f, 28.0f);
        ImGui::Text("Size: %u x %u", image->width, image->height);
    } else {
        ImGui::TextDisabled("Open a PNG or JPEG image to begin.");
    }

    const bool deviceBusy = analyzing;
    ImGui::SameLine(ImGui::GetWindowWidth() - 300.0f);
    ImGui::TextUnformatted("Inference:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(175.0f);
    ImGui::BeginDisabled(deviceBusy);
    if (ImGui::BeginCombo("##InferenceDevice", InferenceDeviceName(inferenceDevice))) {
        constexpr InferenceDevice devices[] = { InferenceDevice::Auto, InferenceDevice::Cpu, InferenceDevice::DirectML };
        for (InferenceDevice device : devices) {
            const bool selected = inferenceDevice == device;
            if (ImGui::Selectable(InferenceDeviceName(device), selected)) {
                inferenceDevice = device;
                inferenceDeviceChanged();
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (analyzing) {
        constexpr const char* dots[] = { "", ".", "..", "..." };
        const int dotIndex = static_cast<int>(ImGui::GetTime() * 3.0) & 3;
        ImGui::TextColored(ImVec4(0.35f, 0.72f, 1.0f, 1.0f), "AI worker active%s", dots[dotIndex]);
    }
    ImGui::Spacing();

    const float controlsHeight = 400.0f;
    ImVec2 previewArea = ImGui::GetContentRegionAvail();
    previewArea.y = std::max(80.0f, previewArea.y - controlsHeight);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.025f, 0.03f, 0.04f, 1.0f));
    ImGui::BeginChild("Image Preview", previewArea, ImGuiChildFlags_Borders);
    if (image && texture && texture->IsValid()) {
        if (ImGui::BeginTabBar("Preview Tabs")) {
            if (ImGui::BeginTabItem("Original")) { DrawFittedTexture(texture, image->width, image->height, false); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Character Raw")) { DrawFittedTexture(rawMaskTexture, rawMask ? rawMask->width : 0, rawMask ? rawMask->height : 0, false); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Character Adjusted")) { DrawFittedTexture(adjustedMaskTexture, adjustedMask ? adjustedMask->width : 0, adjustedMask ? adjustedMask->height : 0, false); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Character")) { DrawFittedTexture(foregroundTexture, foreground ? foreground->width : 0, foreground ? foreground->height : 0, true); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Background")) { DrawFittedTexture(backgroundTexture, background ? background->width : 0, background ? background->height : 0, true); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Hair Box")) {
                DrawFittedTexture(texture, image->width, image->height, false, hairBox,
                    showHairPrompts_ && samRefinement ? &samRefinement->points : nullptr);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Hair Raw")) { DrawFittedTexture(hairRawMaskTexture, hairRawMask ? hairRawMask->width : 0, hairRawMask ? hairRawMask->height : 0, false); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Hair Auto")) { DrawFittedTexture(hairAdjustedMaskTexture, hairAdjustedMask ? hairAdjustedMask->width : 0, hairAdjustedMask ? hairAdjustedMask->height : 0, false); ImGui::EndTabItem(); }
            const ImGuiTabItemFlags maskEditFlags = selectMaskEditTab_ ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            if (ImGui::BeginTabItem("Mask Edit", nullptr, maskEditFlags)) {
                selectMaskEditTab_ = false;
                const bool smartTool = hairEditTool_ < 2;
                DrawFittedTexture(hairEditPreviewTexture, hairEditPreview ? hairEditPreview->width : 0,
                    hairEditPreview ? hairEditPreview->height : 0, false, nullptr, smartPrompts, &hairMaskEditor,
                    hairEditMode_ && !smartTool && hairStage == HairAnalysisStage::Complete && !analyzing && !smartCandidateAvailable,
                    &hairManualChanged, &smartMaskCorrection,
                    hairEditTool_ == 0 ? SmartCorrectionMode::Add : SmartCorrectionMode::Erase,
                    hairMaskEditor.Settings().size,
                    hairEditMode_ && smartTool && hairStage == HairAnalysisStage::Complete && !analyzing && !smartCandidateAvailable,
                    &smartStrokeCompleted);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(smartCandidateAvailable ? "Hair Final (Before)" : "Hair Final")) { DrawFittedTexture(hairFinalMaskTexture, hairFinalMask ? hairFinalMask->width : 0, hairFinalMask ? hairFinalMask->height : 0, false); ImGui::EndTabItem(); }
            if (smartCandidateAvailable && ImGui::BeginTabItem("Smart Candidate")) {
                DrawFittedTexture(smartCandidateTexture, smartCandidateMask ? smartCandidateMask->width : 0,
                    smartCandidateMask ? smartCandidateMask->height : 0, false);
                ImGui::EndTabItem();
            }
            const ImGuiTabItemFlags differenceFlags = selectSmartDifferenceTab_ ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            if (smartCandidateAvailable && ImGui::BeginTabItem("Smart Difference", nullptr, differenceFlags)) {
                selectSmartDifferenceTab_ = false;
                DrawFittedTexture(smartDifferenceTexture, smartDifferenceImage ? smartDifferenceImage->width : 0,
                    smartDifferenceImage ? smartDifferenceImage->height : 0, false);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Hair RGBA")) { DrawFittedTexture(hairTexture, hairImage ? hairImage->width : 0, hairImage ? hairImage->height : 0, true); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
    } else {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const char* label = "Image Preview";
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        ImGui::SetCursorPos(ImVec2((available.x - textSize.x) * 0.5f, (available.y - textSize.y) * 0.5f));
        ImGui::TextDisabled("%s", label);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    const bool hasRawMask = rawMask && rawMask->IsValid();
    ImGui::BeginChild("Character Controls", ImVec2(ImGui::GetContentRegionAvail().x * 0.49f, 285.0f), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted("Character"); ImGui::SameLine();
    ImGui::TextDisabled("U2NETP: %s (%s)", ModelLoadStateName(u2netState), InferenceProviderName(u2netProvider));
    ImGui::BeginDisabled(!hasRawMask || analyzing);
    ImGui::SetNextItemWidth(-1.0f);
    const bool thresholdChanged = ImGui::SliderFloat("Threshold##Character", &maskSettings.threshold, 0.0f, 1.0f, "%.2f");
    ImGui::SetNextItemWidth(-1.0f);
    const bool softnessChanged = ImGui::SliderFloat("Softness##Character", &maskSettings.edgeSoftness, 0.0f, 0.5f, "%.2f");
    bool reset = false;
    if (ImGui::Button("Reset##Character", ImVec2(90.0f, 0.0f))) { maskSettings.Reset(); reset = true; }
    ImGui::EndDisabled();
    if (thresholdChanged || softnessChanged || reset) maskSettingsChanged();

    ImGui::SameLine(0.0f, 12.0f);
    ImGui::BeginDisabled(!image || !image->IsValid() || analyzing);
    if (ImGui::Button(analyzing ? "Analyzing..." : "Analyze Character", ImVec2(155.0f, 0.0f))) analyzeImage();
    ImGui::EndDisabled();
    if (inferenceMilliseconds > 0.0) ImGui::TextDisabled("Inference %.1f ms | Mask %.1f ms", inferenceMilliseconds, maskUpdateMilliseconds);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("Hair Controls", ImVec2(0.0f, 285.0f), ImGuiChildFlags_Borders);
    ImGui::Text("Hair: %s", HairStatusText(hairStage));
    if (samRefinement && !samRefinement->points.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(samRefinement->applied ? ImVec4(0.35f, 0.85f, 0.45f, 1.0f) : ImVec4(0.65f, 0.65f, 0.65f, 1.0f),
            samRefinement->applied ? "Auto refinement applied" : "Box-only retained");
        ImGui::SameLine(); ImGui::Checkbox("Show Hair Prompts", &showHairPrompts_);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("DINO %s (%s) | SAM2 Enc %s (%s) / Dec %s (%s)", ModelLoadStateName(dinoState),
        InferenceProviderName(dinoProvider), ModelLoadStateName(samEncoderState), InferenceProviderName(samEncoderProvider),
        ModelLoadStateName(samDecoderState), InferenceProviderName(samDecoderProvider));
    const bool hasHairMask = hairRawMask && hairRawMask->IsValid();
    const bool hairBusy = analyzing;
    ImGui::BeginDisabled(!hasHairMask || hairBusy || smartCandidateAvailable);
    ImGui::SetNextItemWidth(-1.0f);
    const bool hairThresholdChanged = ImGui::SliderFloat("Threshold##Hair", &hairMaskSettings.threshold, 0.0f, 1.0f, "%.2f");
    ImGui::SetNextItemWidth(-1.0f);
    const bool hairSoftnessChanged = ImGui::SliderFloat("Softness##Hair", &hairMaskSettings.edgeSoftness, 0.0f, 0.5f, "%.2f");
    bool hairReset = false;
    if (ImGui::Button("Reset##Hair", ImVec2(90.0f, 0.0f))) { hairMaskSettings.Reset(); hairReset = true; }
    ImGui::EndDisabled();
    if (hairThresholdChanged || hairSoftnessChanged || hairReset) hairMaskSettingsChanged();
    ImGui::SameLine(0.0f, 12.0f);
    ImGui::BeginDisabled(!image || !image->IsValid() || !hasRawMask || analyzing || hairBusy || smartCandidateAvailable);
    if (ImGui::Button(hairBusy ? "Analyzing..." : "Analyze Hair", ImVec2(145.0f, 0.0f))) analyzeHair();
    ImGui::EndDisabled();
    if (groundingDinoMilliseconds > 0.0 || samTimings.encoderMilliseconds > 0.0 || samTimings.decoderMilliseconds > 0.0) {
        ImGui::TextDisabled("DINO %.0f | Encoder %.0f | Box %.1f | Point %.1f | Refined %.1f | Total %.0f ms", groundingDinoMilliseconds,
            samTimings.encoderMilliseconds, samTimings.boxDecoderMilliseconds, samTimings.pointGenerationMilliseconds,
            samTimings.refinedDecoderMilliseconds, hairTotalMilliseconds);
    }
    if (samPredictedIou > 0.0f) { ImGui::SameLine(); ImGui::TextDisabled("IoU %.3f | Mask %.1f ms", samPredictedIou, hairMaskUpdateMilliseconds); }
    if (!hairMaskEditor.IsInitialized()) hairEditMode_ = false;
    const bool canEdit = hairStage == HairAnalysisStage::Complete && hairMaskEditor.IsInitialized() && !analyzing;
    ImGui::BeginDisabled(!canEdit);
    if (ImGui::Checkbox("Edit Hair Mask", &hairEditMode_) && hairEditMode_) selectMaskEditTab_ = true;
    ImGui::EndDisabled();
    if (hairEditMode_) {
        MaskBrushSettings& brush = hairMaskEditor.Settings();
        ImGui::SameLine();
        ImGui::TextDisabled("Correction:"); ImGui::SameLine();
        if (ImGui::RadioButton("Smart Add", hairEditTool_ == 0)) hairEditTool_ = 0;
        ImGui::SameLine();
        if (ImGui::RadioButton("Smart Erase", hairEditTool_ == 1)) hairEditTool_ = 1;
        ImGui::SameLine();
        if (ImGui::RadioButton("Brush Add", hairEditTool_ == 2)) { hairEditTool_ = 2; brush.mode = MaskBrushMode::Add; }
        ImGui::SameLine();
        if (ImGui::RadioButton("Brush Erase", hairEditTool_ == 3)) { hairEditTool_ = 3; brush.mode = MaskBrushMode::Erase; }
        if (hairEditTool_ == 2) brush.mode = MaskBrushMode::Add;
        if (hairEditTool_ == 3) brush.mode = MaskBrushMode::Erase;
        ImGui::BeginDisabled(smartCandidateAvailable);
        ImGui::SameLine();
        ImGui::BeginDisabled(!hairMaskEditor.CanUndo());
        if (ImGui::Button("Undo")) { if (hairMaskEditor.Undo()) hairManualChanged(); }
        ImGui::EndDisabled(); ImGui::SameLine();
        ImGui::BeginDisabled(!hairMaskEditor.CanRedo());
        if (ImGui::Button("Redo")) { if (hairMaskEditor.Redo()) hairManualChanged(); }
        ImGui::EndDisabled(); ImGui::SameLine();
        ImGui::BeginDisabled(!hairMaskEditor.HasManualEdit());
        if (ImGui::Button("Reset Manual Edit")) resetManualHairEdit();
        ImGui::EndDisabled();
        ImGui::SetNextItemWidth(-1.0f); ImGui::SliderFloat("Brush Size", &brush.size, 1.0f, 200.0f, "%.0f px");
        const bool brushTool = hairEditTool_ >= 2;
        ImGui::BeginDisabled(!brushTool);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.49f);
        ImGui::SliderFloat("Strength", &brush.strength, 0.0f, 1.0f, "%.2f");
        ImGui::SameLine(); ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("Hardness", &brush.hardness, 0.0f, 1.0f, "%.2f");
        ImGui::EndDisabled();
        const ImGuiIO& io = ImGui::GetIO();
        if (canEdit && io.KeyCtrl && !io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_Z, false) && hairMaskEditor.Undo()) hairManualChanged();
            if (ImGui::IsKeyPressed(ImGuiKey_Y, false) && hairMaskEditor.Redo()) hairManualChanged();
        }
        ImGui::EndDisabled();
        if (analyzing && smartPrompts && !smartPrompts->empty()) {
            ImGui::TextColored(ImVec4(0.35f, 0.72f, 1.0f, 1.0f), "Refining mask... cached SAM2 Decoder only");
        }
    }
    if (smartCandidateAvailable) {
        ImGui::TextColored(ImVec4(0.40f, 0.90f, 0.50f, 1.0f),
            "Candidate ready - green adds, red removes. Review Before / Candidate / Difference.");
        if (ImGui::Button("Apply Smart Correction", ImVec2(190.0f, 0.0f))) applySmartCandidate();
        ImGui::SameLine();
        if (ImGui::Button("Cancel Candidate", ImVec2(155.0f, 0.0f))) cancelSmartCandidate();
        ImGui::TextDisabled("Prompt %.2f | Decoder %.1f | Mask %.1f | Texture %.1f | Total %.1f ms | IoU %.3f",
            smartPromptMilliseconds, smartDecoderMilliseconds, smartMaskMilliseconds, smartTextureMilliseconds,
            smartTotalMilliseconds, smartPredictedIou);
    }
    ImGui::EndChild();
    ImGui::End();
}

void EditorUI::Render(ID3D12GraphicsCommandList* commandList) { ImGui::Render(); ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList); }
