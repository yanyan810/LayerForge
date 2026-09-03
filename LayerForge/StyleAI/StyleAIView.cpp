#include "StyleAIView.h"

#include "../ThirdParty/imgui/imgui.h"

#include <shobjidl.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
std::vector<std::filesystem::path> PickImages(HWND owner) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return {};
    dialog->SetOptions(FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_ALLOWMULTISELECT | FOS_NOCHANGEDIR);
    const COMDLG_FILTERSPEC filters[] = { { L"Image Files", L"*.png;*.jpg;*.jpeg" }, { L"All Files", L"*.*" } };
    dialog->SetFileTypes(2, filters);
    if (FAILED(dialog->Show(owner))) return {};
    ComPtr<IShellItemArray> results;
    if (FAILED(dialog->GetResults(&results))) return {};
    DWORD count = 0; results->GetCount(&count);
    std::vector<std::filesystem::path> paths;
    for (DWORD i = 0; i < count; ++i) {
        ComPtr<IShellItem> item; PWSTR value = nullptr;
        if (SUCCEEDED(results->GetItemAt(i, &item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &value))) {
            paths.emplace_back(value); CoTaskMemFree(value);
        }
    }
    return paths;
}

std::filesystem::path PickFolder(HWND owner) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return {};
    dialog->SetOptions(FOS_FORCEFILESYSTEM | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
    if (FAILED(dialog->Show(owner))) return {};
    ComPtr<IShellItem> item; PWSTR value = nullptr;
    if (FAILED(dialog->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &value))) return {};
    std::filesystem::path result(value); CoTaskMemFree(value); return result;
}

void CopyBuffer(auto& destination, const std::string& value) {
    const size_t count = std::min(destination.size() - 1, value.size());
    std::memcpy(destination.data(), value.data(), count); destination[count] = '\0';
}

std::string Utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return { reinterpret_cast<const char*>(value.data()), value.size() };
}

std::filesystem::path PathFromUtf8(const char* value) {
    const auto length = std::strlen(value);
    std::u8string converted(reinterpret_cast<const char8_t*>(value), length);
    return std::filesystem::path(converted);
}
}

void StyleAIView::CreateDataset() {
    if (dataset_.Create(PathFromUtf8(path_.data()), name_.data(), message_)) {
        selected_ = -1; preview_ = {}; previewTexture_ = {}; message_ = "Dataset created."; messageIsError_ = false;
    } else messageIsError_ = true;
}

void StyleAIView::LoadDataset(HWND owner) {
    const auto folder = PickFolder(owner); if (folder.empty()) return;
    if (dataset_.Load(folder, message_)) {
        CopyBuffer(name_, dataset_.GetName()); CopyBuffer(path_, Utf8(dataset_.GetPath()));
        selected_ = -1; preview_ = {}; previewTexture_ = {}; message_ = "Dataset loaded."; messageIsError_ = false;
    } else messageIsError_ = true;
}

void StyleAIView::AddImages(HWND owner, GraphicsDevice& graphics, const ImageLoader& loader) {
    const auto paths = PickImages(owner); if (paths.empty()) return;
    size_t added = 0;
    for (const auto& path : paths) { if (!dataset_.AddImage(path, message_)) { messageIsError_ = true; return; } ++added; }
    message_ = std::to_string(added) + " image(s) added."; messageIsError_ = false;
    if (selected_ < 0 && !dataset_.GetItems().empty()) SelectImage(0, graphics, loader);
}

void StyleAIView::SelectImage(size_t index, GraphicsDevice& graphics, const ImageLoader& loader) {
    if (index >= dataset_.GetItems().size()) return;
    ImageData image; std::string error;
    if (!loader.Load(dataset_.GetItems()[index].localImagePath, image, error) || !graphics.CreateTexture(image, previewTexture_, error)) {
        message_ = std::move(error); messageIsError_ = true; return;
    }
    selected_ = static_cast<int>(index); preview_ = std::move(image);
    CopyBuffer(caption_, dataset_.GetItems()[index].caption); message_.clear();
}

void StyleAIView::RemoveSelected() {
    if (selected_ < 0) return;
    if (dataset_.RemoveImage(static_cast<size_t>(selected_), message_)) {
        selected_ = -1; preview_ = {}; previewTexture_ = {}; caption_.fill(0); message_ = "Image removed."; messageIsError_ = false;
    } else messageIsError_ = true;
}

void StyleAIView::Draw(HWND owner, GraphicsDevice& graphics, const ImageLoader& loader) {
    ImGui::SetNextWindowSize(ImVec2(720, 650), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Style AI")) { ImGui::End(); return; }
    if (ImGui::BeginTabBar("StyleAITabs")) {
        if (ImGui::BeginTabItem("Dataset")) {
            ImGui::InputText("Dataset Name", name_.data(), name_.size());
            ImGui::InputText("Dataset Path", path_.data(), path_.size());
            if (ImGui::Button("Create Dataset")) CreateDataset(); ImGui::SameLine();
            if (ImGui::Button("Load Dataset...")) LoadDataset(owner);
            ImGui::BeginDisabled(!dataset_.IsOpen());
            if (ImGui::Button("Add Images...")) AddImages(owner, graphics, loader); ImGui::SameLine();
            ImGui::BeginDisabled(selected_ < 0); if (ImGui::Button("Remove Selected")) RemoveSelected(); ImGui::EndDisabled(); ImGui::SameLine();
            if (ImGui::Button("Clear")) { if (dataset_.Clear(message_)) { selected_ = -1; preview_ = {}; previewTexture_ = {}; caption_.fill(0); message_ = "Dataset cleared."; messageIsError_ = false; } else messageIsError_ = true; }
            ImGui::EndDisabled();
            if (!message_.empty()) ImGui::TextColored(messageIsError_ ? ImVec4(1, .35f, .35f, 1) : ImVec4(.35f, .85f, .45f, 1), "%s", message_.c_str());
            ImGui::Separator();
            const float listWidth = 220.0f;
            ImGui::BeginChild("StyleImageList", ImVec2(listWidth, 0), ImGuiChildFlags_Borders);
            ImGui::TextUnformatted("Image List");
            for (size_t i = 0; i < dataset_.GetItems().size(); ++i) {
                const auto label = dataset_.GetItems()[i].localImagePath.filename().string();
                if (ImGui::Selectable(label.c_str(), selected_ == static_cast<int>(i))) SelectImage(i, graphics, loader);
            }
            ImGui::EndChild(); ImGui::SameLine();
            ImGui::BeginChild("StyleSelection", ImVec2(0, 0), ImGuiChildFlags_Borders);
            ImGui::TextUnformatted("Selected Image");
            if (preview_.IsValid() && previewTexture_.IsValid()) {
                const ImVec2 available(ImGui::GetContentRegionAvail().x, 310.0f);
                const float scale = std::min(available.x / preview_.width, available.y / preview_.height);
                const ImVec2 size(preview_.width * scale, preview_.height * scale);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (available.x - size.x) * .5f));
                ImGui::Image(static_cast<ImTextureID>(previewTexture_.gpuHandle.ptr), size);
                ImGui::InputTextMultiline("Caption", caption_.data(), caption_.size(), ImVec2(-1, 95));
                if (ImGui::Button("Save Caption")) {
                    if (dataset_.SaveCaption(static_cast<size_t>(selected_), caption_.data(), message_)) { message_ = "Caption saved."; messageIsError_ = false; }
                    else messageIsError_ = true;
                }
            } else ImGui::TextDisabled("Select an image to preview and edit its caption.");
            ImGui::EndChild(); ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Training")) { ImGui::TextDisabled("Coming Soon"); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Generate")) { ImGui::TextDisabled("Coming Soon"); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void StyleAIView::Shutdown() { previewTexture_ = {}; preview_ = {}; }
