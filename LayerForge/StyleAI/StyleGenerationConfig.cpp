#include "StyleGenerationConfig.h"
#include <fstream>
#include <iomanip>
namespace {
std::string Utf8(const std::filesystem::path& p) { const auto s=p.generic_u8string(); return {reinterpret_cast<const char*>(s.data()),s.size()}; }
std::string Escape(const std::string& s) { std::string r; for(char c:s){ if(c=='\\')r+="\\\\"; else if(c=='\"')r+="\\\""; else if(c=='\n')r+="\\n"; else if(c=='\r')r+="\\r"; else if(c=='\t')r+="\\t"; else r+=c;} return r; }
}
bool SaveGenerationConfig(const StyleGenerationConfig& c,const std::filesystem::path& p,std::string& error){
    error.clear(); std::error_code ec; std::filesystem::create_directories(p.parent_path(),ec); if(ec){error=ec.message();return false;}
    std::ofstream s(p,std::ios::binary|std::ios::trunc); if(!s){error="Could not write generation_config.json.";return false;}
    s<<"{\n  \"version\": 1,\n  \"base_model\": \""<<Escape(c.baseModel)<<"\",\n  \"lora_path\": \""<<Escape(Utf8(c.loraPath))
     <<"\",\n  \"trigger_word\": \""<<Escape(c.triggerWord)<<"\",\n  \"prompt\": \""<<Escape(c.prompt)<<"\",\n  \"negative_prompt\": \""<<Escape(c.negativePrompt)
     <<"\",\n  \"lora_strength\": "<<c.loraStrength<<",\n  \"width\": "<<c.width<<",\n  \"height\": "<<c.height<<",\n  \"steps\": "<<c.steps
     <<",\n  \"guidance_scale\": "<<c.guidanceScale<<",\n  \"seed\": "<<c.seed
     <<",\n  \"enable_safety_checker\": "<<(c.enableSafetyChecker?"true":"false")
     <<",\n  \"image_count\": "<<c.imageCount<<",\n  \"style_name\": \""<<Escape(c.styleName)<<"\",\n  \"command\": \""<<Escape(c.command)<<"\",\n  \"adapters\": [";
    for(size_t i=0;i<c.adapters.size();++i){const auto&a=c.adapters[i];s<<(i?",":"")<<"{\"type\": \""<<Escape(a.type)<<"\", \"path\": \""<<Escape(Utf8(a.path))<<"\", \"strength\": "<<a.strength<<"}";}
    s<<"],\n  \"comparisons\": [";
    for(size_t i=0;i<c.comparisons.size();++i){const auto&a=c.comparisons[i];s<<(i?",":"")<<"{\"name\": \""<<Escape(a.name)<<"\", \"path\": \""<<Escape(Utf8(a.path))<<"\", \"strength\": "<<a.strength<<"}";}
    s<<"]"
     <<",\n  \"output_dir\": \""<<Escape(Utf8(c.outputDirectory))<<"\"\n}\n";
    if(!s){error="Could not finish generation_config.json.";return false;} return true;
}
