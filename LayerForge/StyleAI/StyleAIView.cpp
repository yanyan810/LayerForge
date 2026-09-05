#include "StyleAIView.h"

#include "../ThirdParty/imgui/imgui.h"

#include <shobjidl.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <vector>
#include <cmath>
#include <fstream>
#include <cwctype>
#include <shellapi.h>
#include <unordered_map>
#include <unordered_set>
#include <bit>
#include <chrono>
#include <ctime>

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

std::filesystem::path PickLoraFile(HWND owner) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return {};
    dialog->SetOptions(FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_NOCHANGEDIR);
    const COMDLG_FILTERSPEC filters[] = { { L"LoRA Weights", L"*.safetensors" }, { L"All Files", L"*.*" } };
    dialog->SetFileTypes(2, filters); if (FAILED(dialog->Show(owner))) return {};
    ComPtr<IShellItem> item; PWSTR value = nullptr;
    if (FAILED(dialog->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &value))) return {};
    std::filesystem::path result(value); CoTaskMemFree(value); return result;
}

std::vector<std::filesystem::path> PickFolders(HWND owner) {
    ComPtr<IFileOpenDialog> dialog; if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return{};
    dialog->SetOptions(FOS_FORCEFILESYSTEM|FOS_PICKFOLDERS|FOS_PATHMUSTEXIST|FOS_ALLOWMULTISELECT|FOS_NOCHANGEDIR); if(FAILED(dialog->Show(owner)))return{};
    ComPtr<IShellItemArray> results; if(FAILED(dialog->GetResults(&results)))return{}; DWORD count=0;results->GetCount(&count);std::vector<std::filesystem::path> folders;
    for(DWORD i=0;i<count;++i){ComPtr<IShellItem> item;PWSTR value=nullptr;if(SUCCEEDED(results->GetItemAt(i,&item))&&SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&value))){folders.emplace_back(value);CoTaskMemFree(value);}}
    return folders;
}

bool JsonString(const std::filesystem::path& path, const char* key, std::string& value) {
    std::ifstream stream(path, std::ios::binary); if (!stream) return false;
    const std::string json((std::istreambuf_iterator<char>(stream)), {});
    size_t at = json.find(std::string("\"") + key + "\""); if (at == std::string::npos) return false;
    at = json.find(':', at); at = json.find('"', at); if (at == std::string::npos) return false; ++at;
    value.clear(); bool escaped = false;
    for (; at < json.size(); ++at) { const char c=json[at]; if(escaped){value+=c;escaped=false;} else if(c=='\\')escaped=true; else if(c=='"')return true; else value+=c; }
    return false;
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
std::string ModifiedText(std::filesystem::file_time_type value){if(value==std::filesystem::file_time_type{})return"Unknown";const auto system=std::chrono::time_point_cast<std::chrono::system_clock::duration>(value-std::filesystem::file_time_type::clock::now()+std::chrono::system_clock::now());const std::time_t time=std::chrono::system_clock::to_time_t(system);std::tm local{};localtime_s(&local,&time);char text[32];std::strftime(text,sizeof(text),"%Y-%m-%d %H:%M",&local);return text;}
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

void StyleAIView::ImportFolders(HWND owner) {
    if(!dataset_.IsOpen()){message_="Create or load a dataset first.";messageIsError_=true;return;} const auto folders=PickFolders(owner);if(folders.empty())return;
    std::vector<std::filesystem::path> images;std::error_code ec;
    for(const auto& folder:folders){if(includeSubfolders_){for(const auto& entry:std::filesystem::recursive_directory_iterator(folder,std::filesystem::directory_options::skip_permission_denied,ec))if(entry.is_regular_file()){auto x=entry.path().extension().wstring();std::ranges::transform(x,x.begin(),::towlower);if(x==L".png"||x==L".jpg"||x==L".jpeg"||x==L".webp")images.push_back(entry.path());}}else{for(const auto& entry:std::filesystem::directory_iterator(folder,ec))if(entry.is_regular_file()){auto x=entry.path().extension().wstring();std::ranges::transform(x,x.begin(),::towlower);if(x==L".png"||x==L".jpg"||x==L".jpeg"||x==L".webp")images.push_back(entry.path());}}}
    std::ranges::sort(images);size_t imported=0,skipped=0;for(const auto& image:images){if(skipImported_&&dataset_.ContainsSource(image)){++skipped;continue;}if(!dataset_.AddImage(image,message_)){messageIsError_=true;return;}++imported;}
    message_="Found "+std::to_string(images.size())+" images. Imported "+std::to_string(imported)+", skipped "+std::to_string(skipped)+".";messageIsError_=false;
}

void StyleAIView::StartCaption(bool selectedOnly) {
    captionStatus_.clear();if(!dataset_.IsOpen()){captionStatus_="No dataset selected.";return;}if(selectedOnly&&selected_<0){captionStatus_="No image selected.";return;}
    const auto script=ResolveExisting(PathFromUtf8(backendScript_.data()));StyleCaptionConfig config;config.datasetPath=dataset_.GetPath();config.mode=selectedOnly?"selected":"all";config.model=captionModel_.data();config.generalThreshold=generalThreshold_;config.characterThreshold=characterThreshold_;config.includeCharacterTags=includeCharacterTags_;config.includeRatingTags=includeRatingTags_;config.replaceUnderscores=replaceUnderscores_;config.overwriteExisting=!skipNonEmptyCaptions_;if(selectedOnly)config.items.push_back(dataset_.GetItems()[selected_].localImagePath.stem().string());
    const auto path=script.parent_path().parent_path()/"runtime"/"caption_config.json";if(!SaveCaptionConfig(config,path,captionStatus_))return;auto python=PathFromUtf8(pythonPath_.data());if(python.has_parent_path())python=ResolveExisting(python);if(!captionBackend_.Start(python,script,L"caption",path,captionStatus_))return;captionReloaded_=false;captionStatus_="Auto Caption started.";
}

void StyleAIView::UpdateCaptionResult(){if(captionBackend_.IsRunning()||captionReloaded_)return;if(captionBackend_.GetStatus()==StyleBackendStatus::Completed){if(dataset_.ReloadCaptions(captionStatus_)){if(selected_>=0)CopyBuffer(caption_,dataset_.GetItems()[selected_].caption);captionStatus_="Auto Caption completed.";}}captionReloaded_=true;}

void StyleAIView::StartBackend() {
    trainingMessage_.clear();
    if (!dataset_.IsOpen()) { trainingMessage_ = "Cannot start: No dataset is selected."; return; }
    if (dataset_.GetItems().empty()) { trainingMessage_ = "Cannot start: Dataset contains no images."; return; }
    if (pythonPath_[0] == '\0') { trainingMessage_ = "Cannot start: Python path is empty."; return; }
    if (outputName_[0] == '\0') { trainingMessage_ = "Cannot start: Output name is empty."; return; }
    if (baseModel_[0] == '\0') { trainingMessage_ = "Cannot start: Base model is empty."; return; }
    const auto script = ResolveExisting(PathFromUtf8(backendScript_.data()));
    std::error_code ec;
    if (!std::filesystem::is_regular_file(script, ec)) { trainingMessage_ = "Cannot start: Backend script not found."; return; }
    StyleTrainingConfig config;
    config.datasetPath = dataset_.GetPath(); config.outputName = outputName_.data();
    config.outputDirectory = script.parent_path().parent_path() / "models" / "lora";
    config.baseModel = baseModel_.data(); config.triggerWord = triggerWord_.data();
    config.epochs = epochs_; config.resolution = resolution_; config.trainBatchSize = trainBatchSize_;
    config.gradientAccumulationSteps = gradientAccumulationSteps_; config.learningRate = learningRate_;
    config.rank = rank_; config.mixedPrecision = mixedPrecisionIndex_ == 0 ? "fp16" : "no";
    config.gradientCheckpointing = gradientCheckpointing_; config.seed = seed_;
    const auto configPath = script.parent_path().parent_path() / "runtime" / "training_config.json";
    if (!SaveTrainingConfig(config, configPath, trainingMessage_)) return;
    auto python = PathFromUtf8(pythonPath_.data());
    if (python.has_parent_path()) python = ResolveExisting(python);
    if (!backend_.Start(python, script, configPath, trainingMessage_)) return;
    trainingMessage_ = "Backend started.";
}

void StyleAIView::SaveDatasetAs() {
    if(dataset_.SaveAs(PathFromUtf8(path_.data()),name_.data(),message_)){
        CopyBuffer(outputName_,dataset_.GetName());selected_=-1;preview_={};previewTexture_={};caption_.fill(0);
        message_="Dataset saved as: "+dataset_.GetName();messageIsError_=false;
    }else messageIsError_=true;
}

void StyleAIView::RefreshStyles() {
    const auto script=ResolveExisting(PathFromUtf8(backendScript_.data()));
    const auto root=script.parent_path().parent_path()/"Styles";
    const std::string selectedName=selectedStyle_>=0&&selectedStyle_<static_cast<int>(styles_.size())?styles_[selectedStyle_].name:std::string{};
    std::string warning;styles_=ScanStylePresets(root,warning);stylesLoaded_=true;selectedStyle_=-1;
    for(size_t i=0;i<styles_.size();++i)if(styles_[i].name==selectedName){selectedStyle_=static_cast<int>(i);break;}
    if(!warning.empty())generationMessage_=std::move(warning);
}

void StyleAIView::ApplyStyle(int index) {
    if(index<0||index>=static_cast<int>(styles_.size()))return;
    const auto& style=styles_[index];CopyBuffer(loraPath_,Utf8(style.loraPath));
    CopyBuffer(generationBaseModel_,style.baseModel);CopyBuffer(generationTrigger_,style.triggerWord);
    CopyBuffer(stylePrompt_,style.defaultPrompt);CopyBuffer(negativePrompt_,style.defaultNegativePrompt);loraStrength_=style.defaultStrength;generationSteps_=style.defaultSteps;guidanceScale_=style.defaultGuidanceScale;generationMessage_="Style selected: "+style.name;
}

void StyleAIView::SaveAsStyle(bool overwrite) {
    trainingMessage_.clear();const auto script=ResolveExisting(PathFromUtf8(backendScript_.data()));
    const auto projectRoot=script.parent_path().parent_path();const auto outputFolder=projectRoot/"models"/"lora"/PathFromUtf8(outputName_.data());
    const std::string loraName=std::string(outputName_.data())+".safetensors";const auto sourceLora=outputFolder/PathFromUtf8(loraName.c_str());
    StylePreset preset;preset.name=styleName_.data();preset.baseModel=baseModel_.data();preset.triggerWord=triggerWord_.data();preset.defaultStrength=loraStrength_;preset.resolution=resolution_;preset.defaultPrompt=stylePrompt_.data();preset.defaultNegativePrompt=negativePrompt_.data();preset.defaultSteps=generationSteps_;preset.defaultGuidanceScale=guidanceScale_;
    if(!RegisterStylePreset(preset,sourceLora,outputFolder/"training_info.json",projectRoot/"Styles",trainingMessage_,overwrite)){if(trainingMessage_=="Style already exists.")confirmStyleOverwrite_=true;return;}
    trainingMessage_="Style saved: "+preset.name;RefreshStyles();
    for(size_t i=0;i<styles_.size();++i)if(styles_[i].name==preset.name){selectedStyle_=static_cast<int>(i);ApplyStyle(selectedStyle_);break;}
}

void StyleAIView::ApplyTrainingPreset(int index){if(index<0||index>=static_cast<int>(trainingPresets_.size()))return;const auto&p=trainingPresets_[index];resolution_=p.resolution;epochs_=p.epochs;trainBatchSize_=p.batchSize;gradientAccumulationSteps_=p.gradientAccumulation;learningRate_=p.learningRate;rank_=p.rank;mixedPrecisionIndex_=p.mixedPrecision=="fp16"?0:1;gradientCheckpointing_=p.gradientCheckpointing;seed_=p.seed;trainingMessage_="Training preset selected: "+p.name;}

void StyleAIView::RefreshHistory(){const auto script=ResolveExisting(PathFromUtf8(backendScript_.data()));std::string warning;history_=ScanGenerationHistory(script.parent_path().parent_path()/"Outputs"/"Generated",warning);historyThumbnails_.clear();historyThumbnailTextures_.clear();historyLoaded_=true;if(selectedHistory_>=static_cast<int>(history_.size()))selectedHistory_=-1;if(!warning.empty())generationMessage_=warning;}

void StyleAIView::AnalyzeDatasetQuality(const ImageLoader& loader){emptyCaptionCount_=duplicateCaptionCount_=exactDuplicateCount_=0;similarPairs_.clear();selectedSimilarPair_=-1;std::unordered_map<std::string,size_t> captions;std::unordered_map<uint64_t,size_t> exact;std::vector<uint64_t> perceptual(dataset_.GetItems().size());for(size_t index=0;index<dataset_.GetItems().size();++index){const auto&item=dataset_.GetItems()[index];if(item.caption.empty())++emptyCaptionCount_;else if(++captions[item.caption]>1)++duplicateCaptionCount_;std::ifstream stream(item.localImagePath,std::ios::binary);uint64_t hash=1469598103934665603ull;char buffer[8192];while(stream.read(buffer,sizeof(buffer))||stream.gcount()){for(std::streamsize i=0;i<stream.gcount();++i){hash^=(unsigned char)buffer[i];hash*=1099511628211ull;}}if(exact.contains(hash))++exactDuplicateCount_;else exact[hash]=index;ImageData image;std::string error;if(!loader.Load(item.localImagePath,image,error)||!image.IsValid())continue;uint64_t ph=0;for(int y=0;y<8;++y)for(int x=0;x<8;++x){auto gray=[&](int sx){const size_t p=((size_t)(y*image.height/8)*image.width+(size_t)(sx*image.width/9))*4;return image.rgbaPixels[p]*3+image.rgbaPixels[p+1]*6+image.rgbaPixels[p+2];};if(gray(x)>gray(x+1))ph|=1ull<<(y*8+x);}perceptual[index]=ph;}for(size_t a=0;a<perceptual.size();++a)for(size_t b=a+1;b<perceptual.size();++b)if(perceptual[a]&&perceptual[b]&&std::popcount(perceptual[a]^perceptual[b])<=8)similarPairs_.push_back({a,b});message_="Dataset quality check completed.";messageIsError_=false;}

void StyleAIView::DrawTrainingTab(HWND owner) {
    backend_.Update();
    constexpr float labelWidth = 165.0f;
    const auto beginField = [](const char* label) {
        ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(label); ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-1.0f);
    };
    ImGui::TextUnformatted("Dataset"); ImGui::SameLine(labelWidth);
    ImGui::TextWrapped("%s", dataset_.IsOpen() ? Utf8(dataset_.GetPath()).c_str() : "Not selected");
    ImGui::Text("Images"); ImGui::SameLine(labelWidth); ImGui::Text("%zu", dataset_.GetItems().size());
    if(trainingPresets_.empty()){const auto root=ResolveExisting(PathFromUtf8(backendScript_.data())).parent_path().parent_path()/"TrainingPresets";std::string warning;EnsureDefaultTrainingPresets(root,warning);trainingPresets_=ScanTrainingPresets(root,warning);}
    beginField("Training Preset");const char* selectedPreset=selectedTrainingPreset_>=0?trainingPresets_[selectedTrainingPreset_].name.c_str():"(Custom)";
    if(ImGui::BeginCombo("##TrainingPreset",selectedPreset)){for(size_t i=0;i<trainingPresets_.size();++i)if(ImGui::Selectable(trainingPresets_[i].name.c_str(),selectedTrainingPreset_==(int)i)){selectedTrainingPreset_=(int)i;ApplyTrainingPreset((int)i);}ImGui::EndCombo();}
    beginField("Preset Name");ImGui::InputText("##TrainingPresetName",trainingPresetName_.data(),trainingPresetName_.size());
    if(ImGui::Button("Save Training Preset")){TrainingPreset p;p.name=trainingPresetName_.data();p.resolution=resolution_;p.epochs=epochs_;p.batchSize=trainBatchSize_;p.gradientAccumulation=gradientAccumulationSteps_;p.learningRate=learningRate_;p.rank=rank_;p.mixedPrecision=mixedPrecisionIndex_==0?"fp16":"no";p.gradientCheckpointing=gradientCheckpointing_;p.seed=seed_;const auto root=ResolveExisting(PathFromUtf8(backendScript_.data())).parent_path().parent_path()/"TrainingPresets";if(p.Save(root,trainingMessage_)){trainingPresets_=ScanTrainingPresets(root,trainingMessage_);trainingMessage_="Training preset saved.";}}
    ImGui::Separator();
    beginField("Base Model"); ImGui::SetNextItemWidth(-92.0f); ImGui::InputText("##BaseModel", baseModel_.data(), baseModel_.size()); ImGui::SameLine();
    if (ImGui::Button("Browse...")) { const auto folder = PickFolder(owner); if (!folder.empty()) CopyBuffer(baseModel_, Utf8(folder)); }
    beginField("Trigger Word"); ImGui::InputText("##TriggerWord", triggerWord_.data(), triggerWord_.size());
    beginField("Output Name"); ImGui::InputText("##OutputName", outputName_.data(), outputName_.size());
    beginField("Epochs"); ImGui::InputInt("##Epochs", &epochs_); epochs_ = std::max(1, epochs_);
    beginField("Resolution"); ImGui::InputInt("##Resolution", &resolution_); resolution_ = std::max(64, resolution_);
    beginField("Batch Size"); ImGui::InputInt("##BatchSize", &trainBatchSize_); trainBatchSize_ = std::max(1, trainBatchSize_);
    beginField("Gradient Accumulation"); ImGui::InputInt("##GradientAccumulation", &gradientAccumulationSteps_); gradientAccumulationSteps_ = std::max(1, gradientAccumulationSteps_);
    beginField("Learning Rate"); ImGui::InputFloat("##LearningRate", &learningRate_, 0.0f, 0.0f, "%.6f");
    if (!std::isfinite(learningRate_) || learningRate_ <= 0.0f) learningRate_ = 0.0001f;
    beginField("LoRA Rank"); ImGui::InputInt("##LoraRank", &rank_); rank_ = std::clamp(rank_, 1, 256);
    const char* precisions[] = { "fp16", "no" }; beginField("Mixed Precision"); ImGui::Combo("##MixedPrecision", &mixedPrecisionIndex_, precisions, 2);
    ImGui::Checkbox("Gradient Checkpointing", &gradientCheckpointing_);
    beginField("Seed"); ImGui::InputInt("##Seed", &seed_);
    ImGui::Separator();
    beginField("Python"); ImGui::InputText("##PythonPath", pythonPath_.data(), pythonPath_.size());
    beginField("Backend Script"); ImGui::InputText("##BackendScript", backendScript_.data(), backendScript_.size());
    ImGui::BeginDisabled(backend_.IsRunning());
    if (ImGui::Button("Start Training", ImVec2(140, 0))) StartBackend();
    ImGui::EndDisabled(); ImGui::SameLine(); ImGui::BeginDisabled(!backend_.IsRunning());
    if (ImGui::Button("Stop", ImVec2(90, 0))) { backend_.Stop(); trainingMessage_ = "Backend stopped."; }
    ImGui::EndDisabled();
    if (!trainingMessage_.empty()) ImGui::TextWrapped("%s", trainingMessage_.c_str());
    ImGui::Separator();
    ImGui::Text("Status: %s", StatusName(backend_.GetStatus()));
    if (!backend_.IsRunning() && (backend_.GetStatus() == StyleBackendStatus::Completed || backend_.GetStatus() == StyleBackendStatus::Failed)) {
        ImGui::SameLine(); ImGui::TextDisabled("Exit code: %d", backend_.GetExitCode());
    }
    if(backend_.GetStatus()==StyleBackendStatus::Completed){const auto root=ResolveExisting(PathFromUtf8(backendScript_.data())).parent_path().parent_path();const auto output=root/"models"/"lora"/PathFromUtf8(outputName_.data())/(std::string(outputName_.data())+".safetensors");ImGui::TextWrapped("LoRA Output: %s",Utf8(output).c_str());if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",Utf8(output).c_str());}
    const float progress = backend_.GetProgress();
    ImGui::ProgressBar(progress, ImVec2(-1, 0), (std::to_string(static_cast<int>(progress * 100.0f)) + "%").c_str());
    ImGui::Separator();
    beginField("Style Name"); ImGui::InputText("##StyleName", styleName_.data(), styleName_.size());
    beginField("Default Style Prompt");ImGui::InputText("##DefaultStylePrompt",stylePrompt_.data(),stylePrompt_.size());
    beginField("Default Negative");ImGui::InputText("##DefaultNegativePrompt",negativePrompt_.data(),negativePrompt_.size());
    beginField("Default Strength");ImGui::SliderFloat("##DefaultStrength",&loraStrength_,0,2,"%.2f");
    beginField("Default Steps");ImGui::SliderInt("##DefaultSteps",&generationSteps_,1,100);
    beginField("Default Guidance");ImGui::SliderFloat("##DefaultGuidance",&guidanceScale_,1,20,"%.1f");
    ImGui::BeginDisabled(backend_.IsRunning());
    if (ImGui::Button("Save as Style", ImVec2(140, 0))) SaveAsStyle();
    ImGui::EndDisabled();
    if(confirmStyleOverwrite_)ImGui::OpenPopup("Overwrite Style?");confirmStyleOverwrite_=false;
    if(ImGui::BeginPopupModal("Overwrite Style?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::Text("Style \"%s\" already exists.",styleName_.data());if(ImGui::Button("Overwrite")){SaveAsStyle(true);ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();}
    ImGui::TextUnformatted("Console");
    ImGui::BeginChild("BackendConsole", ImVec2(0, 230), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    const auto logs = backend_.GetLogs();
    for (const auto& log : logs) {
        if (log.isError && log.text.rfind("[ERROR]", 0) == 0) ImGui::TextColored(ImVec4(1.0f, .4f, .4f, 1.0f), "[ERR] %s", log.text.c_str());
        else if (log.isError) ImGui::TextColored(ImVec4(1.0f, .78f, .30f, 1.0f), "[WARN] %s", log.text.c_str());
        else ImGui::TextUnformatted(("[OUT] " + log.text).c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

void StyleAIView::SelectLora(HWND owner) {
    const auto selected = PickLoraFile(owner); if (selected.empty()) return;
    selectedStyle_ = -1;
    CopyBuffer(loraPath_, Utf8(selected));
    std::string value;
    if (JsonString(selected.parent_path() / "training_info.json", "base_model", value)) CopyBuffer(generationBaseModel_, value);
    if (JsonString(selected.parent_path() / "training_info.json", "trigger_word", value)) CopyBuffer(generationTrigger_, value);
}

void StyleAIView::StartGeneration(bool comparison) {
    generationMessage_.clear(); generatedPath_.clear(); generatedImage_ = {}; generatedTexture_ = {};comparisonImages_.clear();comparisonTextures_.clear();comparisonActive_=comparison;generationLogStart_=usePersistentBackend_?persistentGenerationBackend_.GetLogs().size():0;
    if (generationBaseModel_[0] == '\0') { generationMessage_ = "Cannot generate: Base model is empty."; return; }
    if (prompt_[0] == '\0'&&stylePrompt_[0]=='\0') { generationMessage_ = "Cannot generate: Prompt is empty."; return; }
    const auto lora = ResolveExisting(PathFromUtf8(loraPath_.data())); std::error_code ec;
    if (!std::filesystem::is_regular_file(lora, ec)) { generationMessage_ = "Cannot generate: LoRA file not found."; return; }
    const auto script = ResolveExisting(PathFromUtf8(backendScript_.data()));
    if (!std::filesystem::is_regular_file(script, ec)) { generationMessage_ = "Cannot generate: Backend script not found."; return; }
    if (generationWidth_ < 64 || generationHeight_ < 64 || generationWidth_ % 8 || generationHeight_ % 8) { generationMessage_ = "Cannot generate: Width and height must be multiples of 8."; return; }
    std::string finalPrompt=stylePrompt_.data();if(!finalPrompt.empty()&&prompt_[0])finalPrompt+=", ";finalPrompt+=prompt_.data();StyleGenerationConfig config; config.baseModel=generationBaseModel_.data(); config.loraPath=lora; config.prompt=finalPrompt;
    config.negativePrompt=negativePrompt_.data(); config.triggerWord=generationTrigger_.data(); config.loraStrength=loraStrength_;
    config.width=generationWidth_; config.height=generationHeight_; config.steps=generationSteps_; config.guidanceScale=guidanceScale_; config.seed=generationSeed_;
    config.enableSafetyChecker=enableSafetyChecker_;
    config.imageCount=generationImageCount_;config.styleName=selectedStyle_>=0&&selectedStyle_<static_cast<int>(styles_.size())?styles_[selectedStyle_].name:lora.stem().string();config.adapters.push_back({"style",lora,loraStrength_});
    if(comparison){config.command="compare";for(size_t i=0;i<styles_.size()&&i<compareSelected_.size();++i)if(compareSelected_[i]){if(styles_[i].baseModel!=config.baseModel){generationMessage_="Comparison Styles must use the same Base Model.";return;}config.comparisons.push_back({styles_[i].name,styles_[i].loraPath,styles_[i].defaultStrength});}if(config.comparisons.empty()){generationMessage_="Select at least one Style for comparison.";return;}}
    config.outputDirectory=script.parent_path().parent_path()/"Outputs"/"Generated";
    const auto configPath=script.parent_path().parent_path()/"runtime"/"generation_config.json";
    if(!SaveGenerationConfig(config,configPath,generationMessage_)) return;
    auto python=PathFromUtf8(pythonPath_.data()); if(python.has_parent_path()) python=ResolveExisting(python);
    if(usePersistentBackend_){const auto requests=script.parent_path().parent_path()/"runtime"/"generation_requests";std::filesystem::create_directories(requests,ec);if(!persistentGenerationBackend_.IsRunning()&&!persistentGenerationBackend_.Start(python,script,L"serve",requests,generationMessage_))return;const auto name="request-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());const auto temporary=requests/("."+name+".tmp");const auto request=requests/(name+".json");std::filesystem::copy_file(configPath,temporary,std::filesystem::copy_options::none,ec);if(!ec)std::filesystem::rename(temporary,request,ec);if(ec){generationMessage_="Could not queue generation: "+ec.message();return;}}
    else if(!generationBackend_.Start(python,script,comparison?L"compare":L"generate",configPath,generationMessage_)) return;
    generationMessage_="Generation started.";historyReloaded_=false;
}

void StyleAIView::UpdateGeneratedPreview(GraphicsDevice& graphics, const ImageLoader& loader) {
    if (!usePersistentBackend_&&generationBackend_.IsRunning()) return;
    const auto logs=usePersistentBackend_?persistentGenerationBackend_.GetLogs():generationBackend_.GetLogs();if(comparisonActive_){for(size_t i=generationLogStart_;i<logs.size();++i)if(!logs[i].isError&&logs[i].text.rfind("[Result] ",0)==0){const auto result=PathFromUtf8(logs[i].text.c_str()+9);bool known=false;for(const auto&image:comparisonImages_)if(image.path==result)known=true;if(!known){ImageData image;GraphicsDevice::Texture texture;std::string error;if(loader.Load(result,image,error)&&graphics.CreateTexture(image,texture,error)){comparisonImages_.push_back(std::move(image));comparisonTextures_.push_back(std::move(texture));generatedPath_=result;}}}}for (auto iterator=logs.rbegin();iterator!=logs.rend();++iterator){const auto& log=*iterator;if (!log.isError && log.text.rfind("[Result] ",0)==0) {
        const auto result=PathFromUtf8(log.text.c_str()+9); ImageData image; std::string error;
        if(result==generatedPath_)break;
        if(loader.Load(result,image,error) && graphics.CreateTexture(image,generatedTexture_,error)){generatedImage_=std::move(image);generatedPath_=result;generationMessage_="Generation completed.";}
        else generationMessage_=error;if(!historyReloaded_){RefreshHistory();historyReloaded_=true;}break;}}
}

void StyleAIView::DrawGenerateTab(HWND owner, GraphicsDevice& graphics, const ImageLoader& loader) {
    generationBackend_.Update();persistentGenerationBackend_.Update(); UpdateGeneratedPreview(graphics,loader);
    constexpr float label=145.0f;
    auto field=[](const char* text){ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(text);ImGui::SameLine(label);ImGui::SetNextItemWidth(-1);};
    field("Style");
    const char* preview=selectedStyle_>=0&&selectedStyle_<static_cast<int>(styles_.size())?styles_[selectedStyle_].name.c_str():"(Manual LoRA)";
    if(ImGui::BeginCombo("##StylePreset",preview)){
        if(ImGui::Selectable("(Manual LoRA)",selectedStyle_==-1))selectedStyle_=-1;
        for(size_t i=0;i<styles_.size();++i){const bool selected=selectedStyle_==static_cast<int>(i);if(ImGui::Selectable(styles_[i].name.c_str(),selected)){selectedStyle_=static_cast<int>(i);ApplyStyle(selectedStyle_);}if(selected)ImGui::SetItemDefaultFocus();}
        ImGui::EndCombo();
    }
    if(ImGui::Button("Refresh Styles"))RefreshStyles();
    if(selectedStyle_>=0&&selectedStyle_<(int)styles_.size())ImGui::TextDisabled("Last Modified: %s",ModifiedText(styles_[selectedStyle_].lastModified).c_str());
    if(compareSelected_.size()!=styles_.size())compareSelected_.resize(styles_.size());
    if(ImGui::CollapsingHeader("Compare Styles")){for(size_t i=0;i<styles_.size();++i){bool checked=compareSelected_[i]!=0;if(ImGui::Checkbox((styles_[i].name+"##compare"+std::to_string(i)).c_str(),&checked))compareSelected_[i]=checked?1:0;}if(ImGui::Button("Generate Comparison"))StartGeneration(true);}
    field("Base Model"); ImGui::InputText("##GenBaseModel",generationBaseModel_.data(),generationBaseModel_.size());
    field("LoRA"); ImGui::SetNextItemWidth(-92); ImGui::InputText("##LoraPath",loraPath_.data(),loraPath_.size()); ImGui::SameLine(); if(ImGui::Button("Browse...##Lora"))SelectLora(owner);
    ImGui::TextDisabled("Active LoRA: %s",loraPath_.data());if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",loraPath_.data());
    field("Trigger Word"); ImGui::InputText("##GenTrigger",generationTrigger_.data(),generationTrigger_.size());
    ImGui::TextUnformatted("Style Prompt"); ImGui::InputTextMultiline("##StylePrompt",stylePrompt_.data(),stylePrompt_.size(),ImVec2(-1,45));
    ImGui::TextUnformatted("User Prompt"); ImGui::InputTextMultiline("##Prompt",prompt_.data(),prompt_.size(),ImVec2(-1,58));
    ImGui::TextUnformatted("Negative Prompt"); ImGui::InputTextMultiline("##NegativePrompt",negativePrompt_.data(),negativePrompt_.size(),ImVec2(-1,45));
    ImGui::SliderFloat("LoRA Strength",&loraStrength_,0,2,"%.2f");
    field("Width"); ImGui::InputInt("##GenWidth",&generationWidth_); field("Height"); ImGui::InputInt("##GenHeight",&generationHeight_);
    ImGui::SliderInt("Steps",&generationSteps_,1,100); ImGui::SliderFloat("Guidance Scale",&guidanceScale_,1,20,"%.1f");
    field("Seed (-1 = random)"); ImGui::InputScalar("##GenSeed",ImGuiDataType_S64,&generationSeed_);
    field("Images");ImGui::InputInt("##GenerationImages",&generationImageCount_);generationImageCount_=std::clamp(generationImageCount_,1,8);
    ImGui::Checkbox("Enable Safety Checker",&enableSafetyChecker_);
    ImGui::Checkbox("Persistent Generation Backend",&usePersistentBackend_);
    ImGui::BeginDisabled(generationBackend_.IsRunning()); if(ImGui::Button("Generate Image",ImVec2(140,0)))StartGeneration(); ImGui::EndDisabled(); ImGui::SameLine();
    const bool canStop=generationBackend_.IsRunning()||persistentGenerationBackend_.IsRunning();ImGui::BeginDisabled(!canStop); if(ImGui::Button("Stop##Generate",ImVec2(90,0))){generationBackend_.Stop();persistentGenerationBackend_.Stop();generationMessage_="Generation stopped.";} ImGui::EndDisabled();
    if(!generationMessage_.empty())ImGui::TextWrapped("%s",generationMessage_.c_str());
    const auto& activeGeneration=usePersistentBackend_?persistentGenerationBackend_:generationBackend_;ImGui::Text("Status: %s",StatusName(activeGeneration.GetStatus())); ImGui::ProgressBar(activeGeneration.GetProgress(),ImVec2(-1,0));
    if(generatedImage_.IsValid()&&generatedTexture_.IsValid()){
        const ImVec2 available(ImGui::GetContentRegionAvail().x,260); const float scale=std::min(available.x/generatedImage_.width,available.y/generatedImage_.height);
        ImGui::Image(static_cast<ImTextureID>(generatedTexture_.gpuHandle.ptr),ImVec2(generatedImage_.width*scale,generatedImage_.height*scale));
        ImGui::TextWrapped("Output: %s",Utf8(generatedPath_).c_str());
    }
    if(!comparisonImages_.empty()){ImGui::SeparatorText("Comparison Results");const float width=180;for(size_t i=0;i<comparisonImages_.size();++i){if(i&&i%3)ImGui::SameLine();const auto&image=comparisonImages_[i];const float scale=std::min(width/image.width,180.0f/image.height);ImGui::BeginGroup();ImGui::TextUnformatted(image.path.parent_path().filename().string().c_str());ImGui::Image((ImTextureID)comparisonTextures_[i].gpuHandle.ptr,ImVec2(image.width*scale,image.height*scale));ImGui::EndGroup();}}
    ImGui::BeginChild("GenerationConsole",ImVec2(0,160),ImGuiChildFlags_Borders);
    for(const auto& log:activeGeneration.GetLogs()) if(log.isError && log.text.rfind("[ERROR]",0)==0) ImGui::TextColored(ImVec4(1,.35f,.35f,1),"%s",log.text.c_str()); else ImGui::TextWrapped("%s",log.text.c_str());
    ImGui::EndChild();
}

void StyleAIView::DrawHistoryTab(HWND owner,GraphicsDevice& graphics,const ImageLoader& loader){
    if(!historyLoaded_)RefreshHistory();if(ImGui::Button("Refresh History"))RefreshHistory();if(historyThumbnails_.size()!=history_.size()){historyThumbnails_.clear();historyThumbnailTextures_.clear();for(const auto&e:history_){ImageData image;GraphicsDevice::Texture texture;std::string error;if(loader.Load(e.imagePath,image,error)&&graphics.CreateTexture(image,texture,error)){historyThumbnails_.push_back(std::move(image));historyThumbnailTextures_.push_back(std::move(texture));}else{historyThumbnails_.push_back({});historyThumbnailTextures_.push_back({});}}}ImGui::Separator();
    ImGui::BeginChild("HistoryList",ImVec2(285,0),ImGuiChildFlags_Borders);for(size_t i=0;i<history_.size();++i){const auto&e=history_[i];if(historyThumbnails_[i].IsValid()){const auto&thumb=historyThumbnails_[i];const float scale=std::min(64.0f/thumb.width,64.0f/thumb.height);ImGui::Image((ImTextureID)historyThumbnailTextures_[i].gpuHandle.ptr,ImVec2(thumb.width*scale,thumb.height*scale));ImGui::SameLine();}const std::string label=e.styleName+"\nSeed "+std::to_string(e.seed)+"##"+std::to_string(i);if(ImGui::Selectable(label.c_str(),selectedHistory_==(int)i,0,ImVec2(0,64))){ImageData image;std::string error;if(loader.Load(e.imagePath,image,error)&&graphics.CreateTexture(image,historyTexture_,error)){selectedHistory_=(int)i;historyImage_=std::move(image);generationMessage_.clear();}else generationMessage_=error;}}ImGui::EndChild();ImGui::SameLine();ImGui::BeginChild("HistoryDetails",ImVec2(0,0),ImGuiChildFlags_Borders);
    if(selectedHistory_>=0&&selectedHistory_<(int)history_.size()){const auto&e=history_[selectedHistory_];if(historyImage_.IsValid()&&historyTexture_.IsValid()){const ImVec2 available(ImGui::GetContentRegionAvail().x,300);const float scale=std::min(available.x/historyImage_.width,available.y/historyImage_.height);ImGui::Image((ImTextureID)historyTexture_.gpuHandle.ptr,ImVec2(historyImage_.width*scale,historyImage_.height*scale));}ImGui::Text("Style: %s",e.styleName.c_str());ImGui::TextWrapped("Prompt: %s",e.prompt.c_str());ImGui::Text("Seed: %lld  Strength: %.2f",(long long)e.seed,e.strength);ImGui::Text("Steps: %d  Guidance: %.1f",e.steps,e.guidance);ImGui::TextWrapped("Generated: %s",e.generatedTime.c_str());
        if(ImGui::Button("Reuse Settings")){stylePrompt_.fill(0);CopyBuffer(prompt_,e.prompt);CopyBuffer(negativePrompt_,e.negativePrompt);CopyBuffer(generationBaseModel_,e.baseModel);CopyBuffer(loraPath_,e.loraPath);generationSeed_=e.seed;loraStrength_=e.strength;generationSteps_=e.steps;guidanceScale_=e.guidance;generationMessage_="History settings restored.";}ImGui::SameLine();if(ImGui::Button("Open Folder"))ShellExecuteW(owner,L"open",e.imagePath.parent_path().c_str(),nullptr,nullptr,SW_SHOWNORMAL);ImGui::SameLine();if(ImGui::Button("Delete Entry"))confirmHistoryDelete_=true;
        if(confirmHistoryDelete_)ImGui::OpenPopup("Delete History Entry?");confirmHistoryDelete_=false;if(ImGui::BeginPopupModal("Delete History Entry?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::TextUnformatted("Delete this PNG and its JSON metadata?");if(ImGui::Button("Delete")){std::string error;if(DeleteGenerationHistoryEntry(e,error)){selectedHistory_=-1;historyImage_={};historyTexture_={};RefreshHistory();}else generationMessage_=error;ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();}}
    ImGui::EndChild();
}

void StyleAIView::Draw(HWND owner, GraphicsDevice& graphics, const ImageLoader& loader) {
    UpdateCaptionResult();
    if(!stylesLoaded_)RefreshStyles();
    ImGui::SetNextWindowSize(ImVec2(720, 650), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Style AI")) { ImGui::End(); return; }
    if (ImGui::BeginTabBar("StyleAITabs")) {
        if (ImGui::BeginTabItem("Dataset")) {
            ImGui::InputText("Dataset Name", name_.data(), name_.size());
            ImGui::InputText("Dataset Path", path_.data(), path_.size());
            if (ImGui::Button("Create Dataset")) CreateDataset(); ImGui::SameLine();
            if (ImGui::Button("Load Dataset...")) LoadDataset(owner); ImGui::SameLine();
            ImGui::BeginDisabled(!dataset_.IsOpen());if (ImGui::Button("Save Dataset As")) SaveDatasetAs();ImGui::EndDisabled();
            ImGui::BeginDisabled(!dataset_.IsOpen());
            if (ImGui::Button("Add Images...")) AddImages(owner, graphics, loader); ImGui::SameLine();
            if (ImGui::Button("Add Folder...")) ImportFolders(owner); ImGui::SameLine();
            if (ImGui::Button("Add Folders...")) ImportFolders(owner);
            ImGui::Checkbox("Include Subfolders",&includeSubfolders_);ImGui::SameLine();ImGui::Checkbox("Skip already imported files",&skipImported_);
            ImGui::BeginDisabled(selected_ < 0); if (ImGui::Button("Remove Selected")) RemoveSelected(); ImGui::EndDisabled(); ImGui::SameLine();
            if (ImGui::Button("Clear")) { if (dataset_.Clear(message_)) { selected_ = -1; preview_ = {}; previewTexture_ = {}; caption_.fill(0); message_ = "Dataset cleared."; messageIsError_ = false; } else messageIsError_ = true; }
            ImGui::EndDisabled();
            if (!message_.empty()) ImGui::TextColored(messageIsError_ ? ImVec4(1, .35f, .35f, 1) : ImVec4(.35f, .85f, .45f, 1), "%s", message_.c_str());
            ImGui::SetNextItemWidth(-1);ImGui::InputText("Tagger Model",captionModel_.data(),captionModel_.size());
            ImGui::SliderFloat("General Threshold",&generalThreshold_,.1f,.9f,"%.2f");ImGui::SliderFloat("Character Threshold",&characterThreshold_,.1f,.9f,"%.2f");
            ImGui::Checkbox("Character Tags",&includeCharacterTags_);ImGui::SameLine();ImGui::Checkbox("Rating Tags",&includeRatingTags_);ImGui::SameLine();ImGui::Checkbox("Replace Underscores",&replaceUnderscores_);
            ImGui::Checkbox("Skip non-empty Captions",&skipNonEmptyCaptions_);
            ImGui::BeginDisabled(captionBackend_.IsRunning()||selected_<0);if(ImGui::Button("Auto Caption Selected"))StartCaption(true);ImGui::EndDisabled();ImGui::SameLine();
            ImGui::BeginDisabled(captionBackend_.IsRunning()||dataset_.GetItems().empty());if(ImGui::Button("Auto Caption All"))StartCaption(false);ImGui::EndDisabled();ImGui::SameLine();
            ImGui::BeginDisabled(!captionBackend_.IsRunning());if(ImGui::Button("Stop##Caption")){captionBackend_.Stop();captionStatus_="Auto Caption stopped.";}ImGui::EndDisabled();
            ImGui::Text("Caption Status: %s",StatusName(captionBackend_.GetStatus()));ImGui::ProgressBar(captionBackend_.GetProgress(),ImVec2(-1,0));if(!captionStatus_.empty())ImGui::TextWrapped("%s",captionStatus_.c_str());
            if(ImGui::Button("Check Dataset Quality"))AnalyzeDatasetQuality(loader);ImGui::SameLine();ImGui::Text("Empty captions: %zu | Duplicate captions: %zu | Exact duplicates: %zu",emptyCaptionCount_,duplicateCaptionCount_,exactDuplicateCount_);
            ImGui::Text("Similar image candidates: %zu",similarPairs_.size());if(!similarPairs_.empty()){ImGui::SetNextItemWidth(300);const std::string pairPreview=selectedSimilarPair_>=0?(std::to_string(similarPairs_[selectedSimilarPair_].first+1)+" / "+std::to_string(similarPairs_[selectedSimilarPair_].second+1)):"Select pair";if(ImGui::BeginCombo("##SimilarPairs",pairPreview.c_str())){for(size_t i=0;i<similarPairs_.size();++i){const auto pair=similarPairs_[i];const std::string label=dataset_.GetItems()[pair.first].localImagePath.filename().string()+" / "+dataset_.GetItems()[pair.second].localImagePath.filename().string();if(ImGui::Selectable(label.c_str(),selectedSimilarPair_==(int)i))selectedSimilarPair_=(int)i;}ImGui::EndCombo();}ImGui::SameLine();ImGui::BeginDisabled(selectedSimilarPair_<0);if(ImGui::Button("Disable Selected")){const auto index=similarPairs_[selectedSimilarPair_].second;if(dataset_.SetEnabled(index,false,message_)){message_="Similar image disabled (file kept).";messageIsError_=false;}else messageIsError_=true;}ImGui::EndDisabled();}
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
        if (ImGui::BeginTabItem("Training")) { DrawTrainingTab(owner); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Generate")) { DrawGenerateTab(owner, graphics, loader); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("History")) { DrawHistoryTab(owner, graphics, loader); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void StyleAIView::Shutdown() { backend_.Stop(); generationBackend_.Stop(); persistentGenerationBackend_.Stop(); captionBackend_.Stop(); previewTexture_ = {}; generatedTexture_ = {}; historyTexture_={}; preview_ = {}; generatedImage_ = {};historyImage_={}; }
