#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

struct StyleGenerationConfig {
    std::string baseModel;
    std::filesystem::path loraPath;
    std::string prompt;
    std::string negativePrompt;
    std::string triggerWord;
    float loraStrength = 0.8f;
    int width = 512, height = 512, steps = 25;
    float guidanceScale = 7.5f;
    int64_t seed = -1;
    bool enableSafetyChecker = false;
    std::filesystem::path outputDirectory;
};
bool SaveGenerationConfig(const StyleGenerationConfig& config, const std::filesystem::path& path, std::string& error);
