#include "StyleDataset.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cwctype>
#include <chrono>

namespace {
std::string Utf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return { reinterpret_cast<const char*>(value.data()), value.size() };
}

std::filesystem::path PathFromUtf8(const std::string& value) {
    std::u8string converted(reinterpret_cast<const char8_t*>(value.data()), value.size());
    return std::filesystem::path(converted);
}

std::string EscapeJson(const std::string& value) {
    std::string result;
    for (const unsigned char c : value) {
        switch (c) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result += static_cast<char>(c); break;
        }
    }
    return result;
}

bool ReadText(const std::filesystem::path& path, std::string& text) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    text.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    return stream.good() || stream.eof();
}

bool WriteText(const std::filesystem::path& path, const std::string& text, std::string& error) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) { error = "Could not write " + Utf8(path) + "."; return false; }
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream) { error = "Could not finish writing " + Utf8(path) + "."; return false; }
    return true;
}

bool ParseString(const std::string& json, size_t& position, std::string& value) {
    while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position]))) ++position;
    if (position >= json.size() || json[position++] != '"') return false;
    value.clear();
    while (position < json.size()) {
        const char c = json[position++];
        if (c == '"') return true;
        if (c != '\\') { value += c; continue; }
        if (position >= json.size()) return false;
        const char escaped = json[position++];
        switch (escaped) { case 'n': value += '\n'; break; case 'r': value += '\r'; break; case 't': value += '\t'; break; default: value += escaped; break; }
    }
    return false;
}

bool FindString(const std::string& json, const std::string& key, size_t start, std::string& value, size_t* end = nullptr) {
    size_t position = json.find('"' + key + '"', start);
    if (position == std::string::npos) return false;
    position = json.find(':', position + key.size() + 2);
    if (position == std::string::npos) return false;
    ++position;
    if (!ParseString(json, position, value)) return false;
    if (end) *end = position;
    return true;
}

bool Supported(const std::filesystem::path& path) {
    std::wstring extension = path.extension().wstring();
    std::ranges::transform(extension, extension.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return extension == L".png" || extension == L".jpg" || extension == L".jpeg" || extension == L".webp";
}
}

bool StyleDataset::Create(const std::filesystem::path& path, const std::string& name, std::string& error) {
    error.clear();
    if (path.empty() || name.empty()) { error = "Dataset name and path are required."; return false; }
    std::error_code ec;
    std::filesystem::create_directories(path / "images", ec);
    if (!ec) std::filesystem::create_directories(path / "captions", ec);
    if (ec) { error = "Could not create the dataset folders: " + ec.message(); return false; }
    datasetPath_ = std::filesystem::absolute(path).lexically_normal();
    datasetName_ = name;
    items_.clear(); nextImageId_ = 1;
    return Save(error);
}

bool StyleDataset::Load(const std::filesystem::path& path, std::string& error) {
    error.clear();
    const auto root = std::filesystem::absolute(path).lexically_normal();
    std::string json;
    if (!ReadText(root / "dataset.json", json)) { error = "Could not read dataset.json."; return false; }
    std::string name;
    if (!FindString(json, "name", 0, name)) { error = "dataset.json does not contain a valid name."; return false; }
    std::vector<StyleDatasetItem> loaded;
    size_t position = json.find("\"items\"");
    size_t highestId = 0;
    while (position != std::string::npos) {
        std::string image, captionPath;
        size_t afterImage = 0, afterCaption = 0;
        if (!FindString(json, "image", position, image, &afterImage)) break;
        if (!FindString(json, "caption", afterImage, captionPath, &afterCaption)) { error = "An item has no caption path."; return false; }
        const size_t nextImage = json.find("\"image\"", afterCaption);
        const size_t enabledAt = json.find("\"enabled\"", afterCaption);
        bool enabled = true;
        if (enabledAt != std::string::npos && (nextImage == std::string::npos || enabledAt < nextImage)) {
            const size_t colon = json.find(':', enabledAt);
            const size_t valueAt = colon == std::string::npos ? std::string::npos : json.find_first_not_of(" \t\r\n", colon + 1);
            enabled = valueAt == std::string::npos || json.compare(valueAt, 5, "false") != 0;
        }
        StyleDatasetItem item;
        item.localImagePath = root / PathFromUtf8(image);
        item.captionPath = root / PathFromUtf8(captionPath);
        std::string source;
        if (FindString(json, "source_file", afterCaption, source) && (nextImage == std::string::npos || json.find("\"source_file\"", afterCaption) < nextImage))
            item.sourcePath = PathFromUtf8(source);
        item.enabled = enabled;
        ReadText(item.captionPath, item.caption);
        loaded.push_back(std::move(item));
        try { highestId = std::max(highestId, static_cast<size_t>(std::stoull(std::filesystem::path(image).stem().string()))); } catch (...) {}
        position = nextImage;
    }
    datasetPath_ = root; datasetName_ = std::move(name); items_ = std::move(loaded); nextImageId_ = highestId + 1;
    return true;
}

bool StyleDataset::Save(std::string& error) const {
    if (!IsOpen()) { error = "No dataset is open."; return false; }
    std::ostringstream json;
    json << "{\n  \"version\": 1,\n  \"name\": \"" << EscapeJson(datasetName_) << "\",\n  \"items\": [";
    for (size_t i = 0; i < items_.size(); ++i) {
        const auto& item = items_[i];
        json << (i ? "," : "") << "\n    {\n      \"image\": \"" << EscapeJson(Utf8(std::filesystem::relative(item.localImagePath, datasetPath_)))
             << "\",\n      \"caption\": \"" << EscapeJson(Utf8(std::filesystem::relative(item.captionPath, datasetPath_)))
             << "\",\n      \"enabled\": " << (item.enabled ? "true" : "false");
        if (!item.sourcePath.empty()) json << ",\n      \"source_file\": \"" << EscapeJson(Utf8(item.sourcePath)) << "\"";
        json << "\n    }";
    }
    json << "\n  ]\n}\n";
    return WriteText(datasetPath_ / "dataset.json", json.str(), error);
}

bool StyleDataset::AddImage(const std::filesystem::path& imagePath, std::string& error) {
    if (!IsOpen()) { error = "Create or load a dataset first."; return false; }
    if (!Supported(imagePath)) { error = "Only PNG, JPG, and JPEG images are supported."; return false; }
    std::ostringstream stem; stem << std::setw(6) << std::setfill('0') << nextImageId_++;
    const auto image = datasetPath_ / "images" / (stem.str() + imagePath.extension().string());
    const auto caption = datasetPath_ / "captions" / (stem.str() + ".txt");
    std::error_code ec;
    std::filesystem::copy_file(imagePath, image, std::filesystem::copy_options::none, ec);
    if (ec) { error = "Could not copy " + Utf8(imagePath.filename()) + ": " + ec.message(); return false; }
    if (!WriteText(caption, {}, error)) { std::filesystem::remove(image, ec); return false; }
    items_.push_back({ imagePath, image, caption, {}, true });
    if (!Save(error)) { items_.pop_back(); std::filesystem::remove(image, ec); std::filesystem::remove(caption, ec); return false; }
    return true;
}

bool StyleDataset::SaveCaption(size_t index, const std::string& caption, std::string& error) {
    if (index >= items_.size()) { error = "No image is selected."; return false; }
    if (!WriteText(items_[index].captionPath, caption, error)) return false;
    items_[index].caption = caption;
    return Save(error);
}

bool StyleDataset::RemoveImage(size_t index, std::string& error) {
    if (index >= items_.size()) { error = "No image is selected."; return false; }
    std::error_code ec;
    std::filesystem::remove(items_[index].localImagePath, ec);
    if (ec) { error = "Could not remove the dataset image: " + ec.message(); return false; }
    std::filesystem::remove(items_[index].captionPath, ec);
    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
    return Save(error);
}

bool StyleDataset::Clear(std::string& error) {
    while (!items_.empty()) if (!RemoveImage(items_.size() - 1, error)) return false;
    return true;
}

bool StyleDataset::ContainsSource(const std::filesystem::path& source) const {
    std::error_code ec; const auto normalized=std::filesystem::weakly_canonical(source,ec);
    for(const auto& item:items_){if(item.sourcePath.empty())continue; std::error_code other; if(std::filesystem::weakly_canonical(item.sourcePath,other)==normalized)return true;}
    return false;
}

bool StyleDataset::ReloadCaptions(std::string& error) {
    for(auto& item:items_) if(!ReadText(item.captionPath,item.caption)){error="Could not reload caption: "+Utf8(item.captionPath);return false;}
    error.clear(); return true;
}

bool StyleDataset::SaveAs(const std::filesystem::path& path, const std::string& name, std::string& error) {
    if(!IsOpen()){error="No dataset is open.";return false;}
    if(path.empty()||name.empty()){error="Dataset name and path are required.";return false;}
    const auto destination=std::filesystem::absolute(path).lexically_normal();std::error_code ec;
    if(std::filesystem::exists(destination,ec)){error="Dataset already exists.";return false;}
    const auto temporary=destination.parent_path()/(destination.filename().wstring()+L".incomplete-"+std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temporary/"images",ec);if(!ec)std::filesystem::create_directories(temporary/"captions",ec);
    if(ec){error="Could not create the new dataset: "+ec.message();return false;}
    const auto cleanup=[&]{std::error_code ignored;std::filesystem::remove_all(temporary,ignored);};
    StyleDataset clone;clone.datasetPath_=temporary;clone.datasetName_=name;clone.nextImageId_=nextImageId_;
    for(const auto& item:items_){
        StyleDatasetItem copied=item;copied.localImagePath=temporary/"images"/item.localImagePath.filename();copied.captionPath=temporary/"captions"/item.captionPath.filename();
        std::filesystem::copy_file(item.localImagePath,copied.localImagePath,std::filesystem::copy_options::none,ec);if(ec){error="Could not copy dataset image: "+ec.message();cleanup();return false;}
        if(!WriteText(copied.captionPath,item.caption,error)){cleanup();return false;}clone.items_.push_back(std::move(copied));
    }
    if(!clone.Save(error)){cleanup();return false;}
    std::filesystem::create_directories(destination.parent_path(),ec);if(ec){error="Could not create the dataset parent folder: "+ec.message();cleanup();return false;}
    std::filesystem::rename(temporary,destination,ec);if(ec){error="Could not save the new dataset: "+ec.message();cleanup();return false;}
    return Load(destination,error);
}
