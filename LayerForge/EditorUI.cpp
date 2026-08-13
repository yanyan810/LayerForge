#include "EditorUI.h"

#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/imgui/backends/imgui_impl_dx12.h"
#include "ThirdParty/imgui/backends/imgui_impl_win32.h"

#include <algorithm>
#include <filesystem>

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

void EditorUI::Draw(const ImageData* image, const GraphicsDevice::Texture* texture, const MaskData* mask, const GraphicsDevice::Texture* maskTexture,
    const std::string& error, bool analyzing, double inferenceMilliseconds, const std::function<void()>& openImage, const std::function<void()>& analyzeImage) {
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

    const float controlsHeight = 58.0f;
    ImVec2 previewArea = ImGui::GetContentRegionAvail();
    previewArea.y = std::max(80.0f, previewArea.y - controlsHeight);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.025f, 0.03f, 0.04f, 1.0f));
    ImGui::BeginChild("Image Preview", previewArea, ImGuiChildFlags_Borders);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    if (image && texture && texture->IsValid()) {
        const bool hasMask = mask && mask->IsValid() && maskTexture && maskTexture->IsValid();
        const float panelWidth = hasMask ? std::max(1.0f, (available.x - 20.0f) * 0.5f) : available.x;
        const auto drawPreview = [&](const char* title, const GraphicsDevice::Texture& previewTexture) {
            ImGui::BeginGroup();
            const float titleWidth = ImGui::CalcTextSize(title).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (panelWidth - titleWidth) * 0.5f));
            ImGui::TextUnformatted(title);
            const float imageHeight = std::max(1.0f, available.y - ImGui::GetTextLineHeightWithSpacing());
            const float scale = std::min(panelWidth / image->width, imageHeight / image->height);
            const ImVec2 size(std::max(1.0f, image->width * scale), std::max(1.0f, image->height * scale));
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (panelWidth - size.x) * 0.5f));
            ImGui::Image(static_cast<ImTextureID>(previewTexture.gpuHandle.ptr), size);
            ImGui::EndGroup();
        };
        drawPreview("Original", *texture);
        if (hasMask) { ImGui::SameLine(0.0f, 20.0f); drawPreview("AI Mask", *maskTexture); }
    } else {
        const char* label = "Image Preview";
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        ImGui::SetCursorPos(ImVec2((available.x - textSize.x) * 0.5f, (available.y - textSize.y) * 0.5f));
        ImGui::TextDisabled("%s", label);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::BeginDisabled(!image || !image->IsValid() || analyzing);
    if (ImGui::Button(analyzing ? "Analyzing..." : "Analyze Image", ImVec2(145.0f, 0.0f))) analyzeImage();
    ImGui::EndDisabled();
    if (inferenceMilliseconds > 0.0) { ImGui::SameLine(); ImGui::TextDisabled("CPU inference: %.1f ms", inferenceMilliseconds); }
    ImGui::End();
}

void EditorUI::Render(ID3D12GraphicsCommandList* commandList) { ImGui::Render(); ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList); }
