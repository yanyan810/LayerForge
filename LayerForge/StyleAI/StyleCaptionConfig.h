#pragma once
#include <filesystem>
#include <string>
#include <vector>
struct StyleCaptionConfig{std::filesystem::path datasetPath;std::string mode="all",model="SmilingWolf/wd-eva02-large-tagger-v3";float generalThreshold=.35f,characterThreshold=.85f;bool includeRatingTags=false,includeCharacterTags=false,replaceUnderscores=true,overwriteExisting=false;std::vector<std::string> items;};
bool SaveCaptionConfig(const StyleCaptionConfig&,const std::filesystem::path&,std::string&);
