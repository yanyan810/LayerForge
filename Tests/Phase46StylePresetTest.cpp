#include "StyleAI/StylePreset.h"
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc,char** argv){
    if(argc==2&&std::string(argv[1])=="--register-real"){
        const auto root=std::filesystem::current_path();const auto source=root/"Models"/"lora"/"MyStyle";std::string error;
        StylePreset first{"MyStyle",{},"stable-diffusion-v1-5/stable-diffusion-v1-5","lfstyle",1.0f,512};
        StylePreset second{"MyStyle_v2",{},"stable-diffusion-v1-5/stable-diffusion-v1-5","lfstyle",1.2f,512};
        if(!RegisterStylePreset(first,source/"MyStyle.safetensors",source/"training_info.json",root/"Styles",error)){std::cerr<<error;return 10;}
        if(!RegisterStylePreset(second,source/"MyStyle.safetensors",source/"training_info.json",root/"Styles",error)){std::cerr<<error;return 11;}
        std::cout<<"Registered MyStyle and MyStyle_v2.\n";return 0;
    }
    const auto root=std::filesystem::current_path()/"Tests"/".phase46-style-test";std::error_code ec;std::filesystem::remove_all(root,ec);std::filesystem::create_directories(root/"source");
    std::ofstream(root/"source"/"trained.safetensors",std::ios::binary)<<"test";
    std::ofstream(root/"source"/"training_info.json",std::ios::binary)<<"{}";
    StylePreset first{"MyStyle",{},"base/model-a","lfstyle",0.75f,512};std::string error;
    if(!RegisterStylePreset(first,root/"source"/"trained.safetensors",root/"source"/"training_info.json",root/"Styles",error)){std::cerr<<error;return 1;}
    StylePreset second{"MyStyle_v2",{},"base/model-b","lfstyle_v2",1.25f,768};
    if(!RegisterStylePreset(second,root/"source"/"trained.safetensors",root/"source"/"training_info.json",root/"Styles",error)){std::cerr<<error;return 2;}
    if(RegisterStylePreset(first,root/"source"/"trained.safetensors",{},root/"Styles",error)||error!="Style already exists."){std::cerr<<"Duplicate was not rejected";return 3;}
    auto styles=ScanStylePresets(root/"Styles",error);if(styles.size()!=2||styles[0].name!="MyStyle"||styles[0].baseModel!="base/model-a"||styles[0].triggerWord!="lfstyle"||styles[0].defaultStrength!=0.75f||styles[1].name!="MyStyle_v2"||styles[1].baseModel!="base/model-b"||styles[1].triggerWord!="lfstyle_v2"||styles[1].defaultStrength!=1.25f){std::cerr<<"Scanned settings do not match";return 4;}
    if(!std::filesystem::is_regular_file(root/"Styles"/"MyStyle"/"training_info.json")||!std::filesystem::is_regular_file(styles[1].loraPath)){std::cerr<<"Copied files are missing";return 5;}
    std::filesystem::remove_all(root,ec);std::cout<<"Two Style presets registered and switched successfully.\n";return 0;
}
