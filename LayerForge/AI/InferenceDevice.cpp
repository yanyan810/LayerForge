#include "InferenceDevice.h"

#include <onnxruntime_cxx_api.h>
#include <dml_provider_factory.h>

namespace {
Ort::SessionOptions SessionOptions(bool directML, bool disableCpuArena) {
    Ort::SessionOptions options;
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
    options.DisableMemPattern();
    if (disableCpuArena) options.DisableCpuMemArena();
    if (directML) Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(options, 0));
    return options;
}
}

const char* InferenceDeviceName(InferenceDevice device) noexcept {
    switch (device) {
    case InferenceDevice::Auto: return "Auto";
    case InferenceDevice::Cpu: return "CPU";
    case InferenceDevice::DirectML: return "DirectML";
    }
    return "Unknown";
}

const char* InferenceProviderName(InferenceProvider provider) noexcept {
    return provider == InferenceProvider::DirectML ? "DirectML" : "CPU";
}

bool CreateOnnxSession(Ort::Env& environment, const std::filesystem::path& modelPath, InferenceDevice requestedDevice,
    bool disableCpuArena, std::unique_ptr<Ort::Session>& session, InferenceProvider& provider,
    std::string& fallbackWarning, std::string& error) {
    session.reset(); provider = InferenceProvider::Cpu; fallbackWarning.clear(); error.clear();
    const bool tryDirectML = requestedDevice != InferenceDevice::Cpu;
    if (tryDirectML) {
        try {
            auto options = SessionOptions(true, disableCpuArena);
            session = std::make_unique<Ort::Session>(environment, modelPath.c_str(), options);
            provider = InferenceProvider::DirectML;
            return true;
        } catch (const Ort::Exception& exception) {
            fallbackWarning = std::string("DirectML initialization failed: ") + exception.what() + " Falling back to CPU.";
        } catch (const std::exception& exception) {
            fallbackWarning = std::string("DirectML initialization failed: ") + exception.what() + " Falling back to CPU.";
        }
    }
    try {
        // Keep the CPU arena enabled for the fallback path. Reusing its allocations is
        // materially faster for the large DINO graph; DirectML sessions still disable
        // the CPU arena above to avoid retaining duplicate transient buffers.
        auto options = SessionOptions(false, false);
        session = std::make_unique<Ort::Session>(environment, modelPath.c_str(), options);
        provider = InferenceProvider::Cpu;
        return true;
    } catch (const Ort::Exception& exception) {
        error = std::string("ONNX Runtime model load failed: ") + exception.what();
    } catch (const std::exception& exception) {
        error = std::string("ONNX Runtime model load failed: ") + exception.what();
    }
    session.reset(); return false;
}
