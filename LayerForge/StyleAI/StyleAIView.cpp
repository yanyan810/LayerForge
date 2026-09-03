#include "StyleAIView.h"

#include "../ThirdParty/imgui/imgui.h"

#include <shobjidl.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <vector>
#include <cmath>

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

std::filesystem::path ExecutablePath() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(length); return std::filesystem::path(buffer);
}

std::filesystem::path ResolveExisting(const std::filesystem::path& value) {
    if (value.is_absolute()) return value.lexically_normal();
    std::filesystem::path starts[] = { std::filesystem::current_path(), ExecutablePath().parent_path() };
    for (auto start : starts) {
        for (int level = 0; level < 7 && !start.empty(); ++level) {
            const auto candidate = std::filesystem::absolute(start / value).lexically_normal();
            std::error_code ec; if (std::filesystem::is_regular_file(candidate, ec)) return candidate;
            const auto parent = start.parent_path(); if (parent == start) break; start = parent;
        }
    }
    return std::filesystem::absolute(value).lexically_normal();
}

const char* StatusName(StyleBackendStatus status) {
    switch (status) {
    case StyleBackendStatus::Running: return "Running";
    case StyleBackendStatus::Completed: return "Completed";
    case StyleBackendStatus::Failed: return "Failed";
    case StyleBackendStatus::Stopped: return "Stopped";
    default: return "Idle";
    }
}
}

void StyleAIView::CreateDataset() {
    if (dataset_.Create(PathFromUtf8(path_.data()), name_.data(), message_)) {
        CopyBuffer(outputName_, dataset_.GetName());
        selected_ = -1; preview_ = {}; previewTexture_ = {}; message_ = "Dataset created."; messageIsError_ = false;
    } else messageIsError_ = true;
}

void StyleAIView::LoadDataset(HWND owner) {
    const auto folder = PickFolder(owner); if (folder.empty()) return;
    if (dataset_.Load(folder, message_)) {
        CopyBuffer(name_, dataset_.GetName()); CopyBuffer(path_, Utf8(dataset_.GetPath()));
        CopyBuffer(outputName_, dataset_.GetName());
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

void StyleAIView::StartBackend() {
    trainingMessage_.clear();
    if (!dataset_.IsOpen()) { trainingMessage_ = "Cannot start: No dataset is selected."; return; }
    if (dataset_.GetItems().empty()) { trainingMessage_ = "Cannot start: Dataset contains no images."; return; }
    if (pythonPath_[0] == '\0') { trainingMessage_ = "Cannot start: Python path is empty."; return; }
    if (outputName_[0] == '\0') { trainingMessage_ = "Cannot start: Output name is empty."; return; }
    const auto script = ResolveExisting(PathFromUtf8(backendScript_.data()));
    std::error_code ec;
    if (!std::filesystem::is_regular_file(script, ec)) { trainingMessage_ = "Cannot start: Backend script not found."; return; }
    StyleTrainingConfig config;
    config.datasetPath = dataset_.GetPath(); config.outputName = outputName_.data();
    config.epochs = epochs_; config.resolution = resolution_; config.learningRate = learningRate_;
    const auto configPath = script.parent_path().parent_path() / "runtime" / "training_config.json";
    if (!SaveTrainingConfig(config, configPath, trainingMessage_)) return;
    if (!backend_.Start(PathFromUtf8(pythonPath_.data()), script, configPath, trainingMessage_)) return;
    trainingMessage_ = "Backend started.";
}

void StyleAIView::DrawTrainingTab() {
    backend_.Update();
    ImGui::TextUnformatted("Dataset"); ImGui::SameLine(150.0f);
    ImGui::TextWrapped("%s", dataset_.IsOpen() ? Utf8(dataset_.GetPath()).c_str() : "Not selected");
    ImGui::Text("Images"); ImGui::SameLine(150.0f); ImGui::Text("%zu", dataset_.GetItems().size());
    ImGui::SetNextItemWidth(-1.0f); ImGui::InputText("Output Name", outputName_.data(), outputName_.size());
    ImGui::SetNextItemWidth(-1.0f); ImGui::InputInt("Epochs", &epochs_); epochs_ = std::max(1, epochs_);
    ImGui::SetNextItemWidth(-1.0f); ImGui::InputInt("Resolution", &resolution_); resolution_ = std::max(64, resolution_);
    ImGui::SetNextItemWidth(-1.0f); ImGui::InputFloat("Learning Rate", &learningRate_, 0.0f, 0.0f, "%.6f");
    if (!std::isfinite(learningRate_) || learningRate_ <= 0.0f) learningRate_ = 0.0001f;
    ImGui::SetNextItemWidth(-1.0f); ImGui::InputText("Python", pythonPath_.data(), pythonPath_.size());
    ImGui::SetNextItemWidth(-1.0f); ImGui::InputText("Backend Script", backendScript_.data(), backendScript_.size());
    ImGui::BeginDisabled(backend_.IsRunning());
    if (ImGui::Button("Start Backend", ImVec2(140, 0))) StartBackend();
    ImGui::EndDisabled(); ImGui::SameLine(); ImGui::BeginDisabled(!backend_.IsRunning());
    if (ImGui::Button("Stop", ImVec2(90, 0))) { backend_.Stop(); trainingMessage_ = "Backend stopped."; }
    ImGui::EndDisabled();
    if (!trainingMessage_.empty()) ImGui::TextWrapped("%s", trainingMessage_.c_str());
    ImGui::Separator();
    ImGui::Text("Status: %s", StatusName(backend_.GetStatus()));
    if (!backend_.IsRunning() && (backend_.GetStatus() == StyleBackendStatus::Completed || backend_.GetStatus() == StyleBackendStatus::Failed)) {
        ImGui::SameLine(); ImGui::TextDisabled("Exit code: %d", backend_.GetExitCode());
    }
    const float progress = backend_.GetProgress();
    ImGui::ProgressBar(progress, ImVec2(-1, 0), (std::to_string(static_cast<int>(progress * 100.0f)) + "%").c_str());
    ImGui::TextUnformatted("Console");
    ImGui::BeginChild("BackendConsole", ImVec2(0, 230), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    const auto logs = backend_.GetLogs();
    for (const auto& log : logs) {
        if (log.isError) ImGui::TextColored(ImVec4(1.0f, .4f, .4f, 1.0f), "[ERR] %s", log.text.c_str());
        else ImGui::TextUnformatted(("[OUT] " + log.text).c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
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
        if (ImGui::BeginTabItem("Training")) { DrawTrainingTab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Generate")) { ImGui::TextDisabled("Coming Soon"); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void StyleAIView::Shutdown() { backend_.Stop(); previewTexture_ = {}; preview_ = {}; }
