#pragma once

#include <filesystem>
#include <string>

struct StyleTrainingConfig {
    std::filesystem::path datasetPath;
    std::string outputName;
    std::filesystem::path outputDirectory;
    std::string baseModel;
    std::string triggerWord = "lfstyle";
    int epochs = 10;
    int resolution = 512;
    int trainBatchSize = 1;
    int gradientAccumulationSteps = 1;
    float learningRate = 0.0001f;
    int rank = 16;
    std::string mixedPrecision = "fp16";
    bool gradientCheckpointing = true;
    int seed = 42;
};

bool SaveTrainingConfig(const StyleTrainingConfig& config, const std::filesystem::path& path, std::string& error);
