#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct StyleDatasetItem {
    std::filesystem::path sourcePath;
    std::filesystem::path localImagePath;
    std::filesystem::path captionPath;
    std::string caption;
    bool enabled = true;
};

class StyleDataset {
public:
    bool Create(const std::filesystem::path& path, const std::string& name, std::string& error);
    bool Load(const std::filesystem::path& path, std::string& error);
    bool Save(std::string& error) const;
    bool AddImage(const std::filesystem::path& imagePath, std::string& error);
    bool RemoveImage(size_t index, std::string& error);
    bool SaveCaption(size_t index, const std::string& caption, std::string& error);
    bool Clear(std::string& error);
    [[nodiscard]] bool ContainsSource(const std::filesystem::path& source) const;
    bool ReloadCaptions(std::string& error);

    [[nodiscard]] const std::vector<StyleDatasetItem>& GetItems() const { return items_; }
    [[nodiscard]] const std::filesystem::path& GetPath() const { return datasetPath_; }
    [[nodiscard]] const std::string& GetName() const { return datasetName_; }
    [[nodiscard]] bool IsOpen() const { return !datasetPath_.empty(); }

private:
    std::filesystem::path datasetPath_;
    std::string datasetName_;
    std::vector<StyleDatasetItem> items_;
    size_t nextImageId_ = 1;
};
