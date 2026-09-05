#include "StylePreset.h"
#include <algorithm>
#include <chrono>
#include <fstream>

namespace {
std::string Utf8(const std::filesystem::path& p){const auto s=p.generic_u8string();return{reinterpret_cast<const char*>(s.data()),s.size()};}
std::filesystem::path FromUtf8(const std::string& value){std::u8string converted(reinterpret_cast<const char8_t*>(value.data()),value.size());return std::filesystem::path(converted);}
std::string Escape(const std::string& s){std::string r;for(unsigned char c:s){switch(c){case'\\':r+="\\\\";break;case'"':r+="\\\"";break;case'\n':r+="\\n";break;case'\r':r+="\\r";break;case'\t':r+="\\t";break;default:r+=static_cast<char>(c);}}return r;}
bool Read(const std::filesystem::path& p,std::string& text){std::ifstream s(p,std::ios::binary);if(!s)return false;text.assign(std::istreambuf_iterator<char>(s),{});return true;}
bool JsonString(const std::string& j,const char* key,std::string& value){size_t at=j.find(std::string("\"")+key+"\"");if(at==std::string::npos)return false;at=j.find(':',at);at=j.find('"',at);if(at==std::string::npos)return false;++at;value.clear();bool escaped=false;for(;at<j.size();++at){char c=j[at];if(escaped){switch(c){case'n':value+='\n';break;case'r':value+='\r';break;case't':value+='\t';break;default:value+=c;}escaped=false;}else if(c=='\\')escaped=true;else if(c=='"')return true;else value+=c;}return false;}
bool JsonNumber(const std::string& j,const char* key,double& value){size_t at=j.find(std::string("\"")+key+"\"");if(at==std::string::npos)return false;at=j.find(':',at);if(at==std::string::npos)return false;try{size_t used=0;value=std::stod(j.substr(at+1),&used);return used>0;}catch(...){return false;}}
}

bool StylePreset::Save(const std::filesystem::path& dir,std::string& error)const{
    error.clear();std::error_code ec;std::filesystem::create_directories(dir,ec);if(ec){error="Could not create Style folder: "+ec.message();return false;}
    std::ofstream s(dir/"style.json",std::ios::binary|std::ios::trunc);if(!s){error="Could not write style.json.";return false;}
    s<<"{\n  \"version\": 2,\n  \"name\": \""<<Escape(name)<<"\",\n  \"lora_path\": \""<<Escape(Utf8(loraPath.filename()))
     <<"\",\n  \"base_model\": \""<<Escape(baseModel)<<"\",\n  \"trigger_word\": \""<<Escape(triggerWord)
     <<"\",\n  \"default_strength\": "<<defaultStrength<<",\n  \"resolution\": "<<resolution
     <<",\n  \"default_prompt\": \""<<Escape(defaultPrompt)<<"\",\n  \"default_negative_prompt\": \""<<Escape(defaultNegativePrompt)
     <<"\",\n  \"default_steps\": "<<defaultSteps<<",\n  \"default_guidance_scale\": "<<defaultGuidanceScale<<"\n}\n";
    if(!s){error="Could not finish style.json.";return false;}return true;
}

bool StylePreset::Load(const std::filesystem::path& jsonPath,StylePreset& preset,std::string& error){
    std::string json,lora;if(!Read(jsonPath,json)){error="Could not read "+Utf8(jsonPath);return false;}double strength=1.0,resolution=512,steps=25,guidance=7.0;
    if(!JsonString(json,"name",preset.name)||!JsonString(json,"lora_path",lora)||!JsonString(json,"base_model",preset.baseModel)||!JsonString(json,"trigger_word",preset.triggerWord)){error="Invalid style.json: "+Utf8(jsonPath);return false;}
    JsonNumber(json,"default_strength",strength);JsonNumber(json,"resolution",resolution);JsonNumber(json,"default_steps",steps);JsonNumber(json,"default_guidance_scale",guidance);JsonString(json,"default_prompt",preset.defaultPrompt);JsonString(json,"default_negative_prompt",preset.defaultNegativePrompt);preset.defaultStrength=static_cast<float>(strength);preset.resolution=static_cast<int>(resolution);preset.defaultSteps=static_cast<int>(steps);preset.defaultGuidanceScale=static_cast<float>(guidance);std::error_code timeError;preset.lastModified=std::filesystem::last_write_time(jsonPath,timeError);
    const auto stored=FromUtf8(lora);preset.loraPath=stored.is_absolute()?stored:(jsonPath.parent_path()/stored).lexically_normal();return true;
}

bool IsValidStyleName(const std::string& name,std::string& error){
    if(name.empty()){error="Style Name is required.";return false;}
    if(name=="."||name==".."||name.back()==' '||name.back()=='.'){error="Style Name cannot end with a space or period.";return false;}
    if(name.find_first_of("\\/:*?\"<>|")!=std::string::npos){error="Style Name contains an invalid filename character.";return false;}return true;
}

bool RegisterStylePreset(const StylePreset& preset,const std::filesystem::path& sourceLora,const std::filesystem::path& sourceInfo,const std::filesystem::path& root,std::string& error,bool overwrite){
    if(!IsValidStyleName(preset.name,error))return false;std::error_code ec;if(!std::filesystem::is_regular_file(sourceLora,ec)){error="Training LoRA was not found.";return false;}
    const auto destination=root/FromUtf8(preset.name);const bool existed=std::filesystem::exists(destination,ec);if(existed&&!overwrite){error="Style already exists.";return false;}
    std::filesystem::create_directories(root,ec);if(ec){error="Could not create Styles folder: "+ec.message();return false;}
    const auto temporary=root/(".style-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".incomplete");
    std::filesystem::create_directory(temporary,ec);if(ec){error="Could not create temporary Style folder: "+ec.message();return false;}
    const auto cleanup=[&]{std::error_code ignored;std::filesystem::remove_all(temporary,ignored);};
    const auto targetLora=temporary/FromUtf8(preset.name+".safetensors");
    std::filesystem::copy_file(sourceLora,targetLora,std::filesystem::copy_options::none,ec);if(ec){error="Could not copy LoRA: "+ec.message();cleanup();return false;}
    ec.clear();if(std::filesystem::is_regular_file(sourceInfo,ec)){std::filesystem::copy_file(sourceInfo,temporary/"training_info.json",std::filesystem::copy_options::none,ec);if(ec){error="Could not copy training_info.json: "+ec.message();cleanup();return false;}}
    StylePreset stored=preset;stored.loraPath=targetLora.filename();if(!stored.Save(temporary,error)){cleanup();return false;}
    if(existed){const auto backup=root/(".style-backup-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::rename(destination,backup,ec);if(ec){error="Could not prepare Style update: "+ec.message();cleanup();return false;}std::filesystem::rename(temporary,destination,ec);if(ec){std::error_code restore;std::filesystem::rename(backup,destination,restore);error="Could not update Style: "+ec.message();cleanup();return false;}std::filesystem::remove_all(backup,ec);return true;}
    std::filesystem::rename(temporary,destination,ec);if(ec){error="Could not register Style: "+ec.message();cleanup();return false;}return true;
}

std::vector<StylePreset> ScanStylePresets(const std::filesystem::path& root,std::string& warning){
    warning.clear();std::vector<StylePreset> result;std::error_code ec;if(!std::filesystem::exists(root,ec))return result;
    for(const auto& entry:std::filesystem::directory_iterator(root,std::filesystem::directory_options::skip_permission_denied,ec)){if(!entry.is_directory())continue;const auto json=entry.path()/"style.json";if(!std::filesystem::is_regular_file(json))continue;StylePreset preset;std::string error;if(StylePreset::Load(json,preset,error))result.push_back(std::move(preset));else if(warning.empty())warning=std::move(error);}
    std::ranges::sort(result,{},[](const StylePreset& p){return p.name;});return result;
}
