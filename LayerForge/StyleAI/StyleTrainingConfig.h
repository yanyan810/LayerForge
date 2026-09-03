#pragma once

#include <filesystem>
#include <string>

struct StyleTrainingConfig {
    std::filesystem::path datasetPath;
    std::string outputName;
    int epochs = 10;
    int resolution = 1024;
    float learningRate = 0.0001f;
};

bool SaveTrainingConfig(const StyleTrainingConfig& config, const std::filesystem::path& path, std::string& error);
