#include "StyleTrainingConfig.h"

#include <fstream>
#include <iomanip>

namespace {
std::string Utf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return { reinterpret_cast<const char*>(value.data()), value.size() };
}

std::string EscapeJson(const std::string& value) {
    std::string result;
    for (const unsigned char c : value) {
        switch (c) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result += static_cast<char>(c); break;
        }
    }
    return result;
}
}

bool SaveTrainingConfig(const StyleTrainingConfig& config, const std::filesystem::path& path, std::string& error) {
    error.clear();
    std::error_code ec;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) { error = "Could not create the runtime folder: " + ec.message(); return false; }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) { error = "Could not write training_config.json."; return false; }
    stream << "{\n"
        << "  \"version\": 2,\n"
        << "  \"dataset\": \"" << EscapeJson(Utf8(config.datasetPath)) << "\",\n"
        << "  \"output_name\": \"" << EscapeJson(config.outputName) << "\",\n"
        << "  \"output_dir\": \"" << EscapeJson(Utf8(config.outputDirectory)) << "\",\n"
        << "  \"base_model\": \"" << EscapeJson(config.baseModel) << "\",\n"
        << "  \"trigger_word\": \"" << EscapeJson(config.triggerWord) << "\",\n"
        << "  \"epochs\": " << config.epochs << ",\n"
        << "  \"resolution\": " << config.resolution << ",\n"
        << "  \"train_batch_size\": " << config.trainBatchSize << ",\n"
        << "  \"gradient_accumulation_steps\": " << config.gradientAccumulationSteps << ",\n"
        << "  \"learning_rate\": " << std::fixed << std::setprecision(6) << config.learningRate << ",\n"
        << "  \"rank\": " << config.rank << ",\n"
        << "  \"mixed_precision\": \"" << EscapeJson(config.mixedPrecision) << "\",\n"
        << "  \"gradient_checkpointing\": " << (config.gradientCheckpointing ? "true" : "false") << ",\n"
        << "  \"seed\": " << config.seed << "\n"
        << "}\n";
    if (!stream) { error = "Could not finish writing training_config.json."; return false; }
    return true;
}
