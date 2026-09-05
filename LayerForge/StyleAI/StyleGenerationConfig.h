#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct StyleGenerationConfig {
    struct Adapter { std::string type="style"; std::filesystem::path path; float strength=0.8f; };
    struct Comparison { std::string name; std::filesystem::path path; float strength=0.8f; };
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
    int imageCount = 1;
    std::string styleName;
    std::vector<Adapter> adapters;
    std::vector<Comparison> comparisons;
    std::string command = "generate";
    std::filesystem::path outputDirectory;
};
bool SaveGenerationConfig(const StyleGenerationConfig& config, const std::filesystem::path& path, std::string& error);
