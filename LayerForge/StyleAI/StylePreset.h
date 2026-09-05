#pragma once
#include <filesystem>
#include <string>
#include <vector>

struct StylePreset {
    std::string name;
    std::filesystem::path loraPath;
    std::string baseModel;
    std::string triggerWord;
    float defaultStrength = 1.0f;
    int resolution = 512;
    bool Save(const std::filesystem::path& styleDirectory, std::string& error) const;
    static bool Load(const std::filesystem::path& styleJson, StylePreset& preset, std::string& error);
};

bool IsValidStyleName(const std::string& name, std::string& error);
bool RegisterStylePreset(const StylePreset& preset, const std::filesystem::path& sourceLora,
    const std::filesystem::path& sourceTrainingInfo, const std::filesystem::path& stylesRoot, std::string& error);
std::vector<StylePreset> ScanStylePresets(const std::filesystem::path& stylesRoot, std::string& warning);
