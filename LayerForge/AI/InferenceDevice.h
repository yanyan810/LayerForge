#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace Ort { struct Env; struct Session; }

enum class InferenceDevice {
    Auto,
    Cpu,
    DirectML,
};

enum class InferenceProvider {
    Cpu,
    DirectML,
};

[[nodiscard]] const char* InferenceDeviceName(InferenceDevice device) noexcept;
[[nodiscard]] const char* InferenceProviderName(InferenceProvider provider) noexcept;

bool CreateOnnxSession(Ort::Env& environment, const std::filesystem::path& modelPath, InferenceDevice requestedDevice,
    bool disableCpuArena, std::unique_ptr<Ort::Session>& session, InferenceProvider& provider,
    std::string& fallbackWarning, std::string& error);
