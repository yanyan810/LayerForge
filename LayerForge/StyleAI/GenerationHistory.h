#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
struct GenerationHistoryEntry{std::filesystem::path imagePath,jsonPath;std::string styleName,prompt,negativePrompt,baseModel,loraPath,generatedTime;int64_t seed=-1;float strength=.8f,guidance=7.5f;int steps=25,width=512,height=512;};
std::vector<GenerationHistoryEntry> ScanGenerationHistory(const std::filesystem::path&root,std::string&warning);
bool DeleteGenerationHistoryEntry(const GenerationHistoryEntry&entry,std::string&error);
