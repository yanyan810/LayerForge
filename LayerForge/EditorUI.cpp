#include "EditorUI.h"

#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/imgui/backends/imgui_impl_dx12.h"
#include "ThirdParty/imgui/backends/imgui_impl_win32.h"

#include <algorithm>
#include <filesystem>

namespace {
void DrawFittedTexture(const GraphicsDevice::Texture* texture, uint32_t width, uint32_t height, bool checkerboard) {
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
    ImGui::Dummy(size);
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
    MaskAdjustmentSettings& maskSettings, const std::string& error, bool analyzing, double inferenceMilliseconds, double maskUpdateMilliseconds,
    const std::function<void()>& openImage, const std::function<void()>& analyzeImage, const std::function<void()>& maskSettingsChanged) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("LayerForge", nullptr, flags);

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

    if (image && image->IsValid()) {
        ImGui::Text("File: %s", image->fileNameUtf8.c_str());
        ImGui::SameLine(0.0f, 28.0f);
        ImGui::Text("Size: %u x %u", image->width, image->height);
    } else {
        ImGui::TextDisabled("Open a PNG or JPEG image to begin.");
    }
    ImGui::Spacing();

    const float controlsHeight = 145.0f;
    ImVec2 previewArea = ImGui::GetContentRegionAvail();
    previewArea.y = std::max(80.0f, previewArea.y - controlsHeight);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.025f, 0.03f, 0.04f, 1.0f));
    ImGui::BeginChild("Image Preview", previewArea, ImGuiChildFlags_Borders);
    if (image && texture && texture->IsValid()) {
        if (ImGui::BeginTabBar("Preview Tabs")) {
            if (ImGui::BeginTabItem("Original")) { DrawFittedTexture(texture, image->width, image->height, false); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Raw Mask")) { DrawFittedTexture(rawMaskTexture, rawMask ? rawMask->width : 0, rawMask ? rawMask->height : 0, false); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Adjusted Mask")) { DrawFittedTexture(adjustedMaskTexture, adjustedMask ? adjustedMask->width : 0, adjustedMask ? adjustedMask->height : 0, false); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Foreground")) { DrawFittedTexture(foregroundTexture, foreground ? foreground->width : 0, foreground ? foreground->height : 0, true); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Background")) { DrawFittedTexture(backgroundTexture, background ? background->width : 0, background ? background->height : 0, true); ImGui::EndTabItem(); }
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
    ImGui::BeginDisabled(!hasRawMask || analyzing);
    ImGui::TextUnformatted("Mask Settings");
    ImGui::SetNextItemWidth(std::min(420.0f, ImGui::GetContentRegionAvail().x * 0.45f));
    const bool thresholdChanged = ImGui::SliderFloat("Threshold", &maskSettings.threshold, 0.0f, 1.0f, "%.2f");
    ImGui::SetNextItemWidth(std::min(420.0f, ImGui::GetContentRegionAvail().x * 0.45f));
    const bool softnessChanged = ImGui::SliderFloat("Edge Softness", &maskSettings.edgeSoftness, 0.0f, 0.5f, "%.2f");
    bool reset = false;
    if (ImGui::Button("Reset", ImVec2(90.0f, 0.0f))) { maskSettings.Reset(); reset = true; }
    ImGui::EndDisabled();
    if (thresholdChanged || softnessChanged || reset) maskSettingsChanged();

    ImGui::SameLine(0.0f, 20.0f);
    ImGui::BeginDisabled(!image || !image->IsValid() || analyzing);
    if (ImGui::Button(analyzing ? "Analyzing..." : "Analyze Image", ImVec2(145.0f, 0.0f))) analyzeImage();
    ImGui::EndDisabled();
    if (inferenceMilliseconds > 0.0) { ImGui::SameLine(); ImGui::TextDisabled("CPU inference: %.1f ms", inferenceMilliseconds); }
    if (maskUpdateMilliseconds > 0.0) { ImGui::SameLine(); ImGui::TextDisabled("Mask update: %.1f ms", maskUpdateMilliseconds); }
    ImGui::End();
}

void EditorUI::Render(ID3D12GraphicsCommandList* commandList) { ImGui::Render(); ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList); }
