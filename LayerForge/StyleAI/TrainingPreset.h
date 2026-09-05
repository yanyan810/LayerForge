#pragma once
#include <filesystem>
#include <string>
#include <vector>

struct TrainingPreset {
    std::string name; int resolution=512,epochs=3,batchSize=1,gradientAccumulation=1,rank=8,seed=42;
    float learningRate=.0001f;std::string mixedPrecision="fp16";bool gradientCheckpointing=true;
    bool Save(const std::filesystem::path& root,std::string& error)const;
    static bool Load(const std::filesystem::path& json,TrainingPreset& value,std::string& error);
};
std::vector<TrainingPreset> ScanTrainingPresets(const std::filesystem::path& root,std::string& warning);
bool EnsureDefaultTrainingPresets(const std::filesystem::path& root,std::string& error);
