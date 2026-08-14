#include "SegmentationModel.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <limits>
#include <numeric>

namespace {
constexpr int64_t ModelWidth = 320;
constexpr int64_t ModelHeight = 320;
constexpr std::array<float, 3> Mean{ 0.485f, 0.456f, 0.406f };
constexpr std::array<float, 3> StdDev{ 0.229f, 0.224f, 0.225f };

float SampleChannelBilinear(const ImageData& image, float x, float y, uint32_t channel) {
    x = std::clamp(x, 0.0f, static_cast<float>(image.width - 1));
    y = std::clamp(y, 0.0f, static_cast<float>(image.height - 1));
    const uint32_t x0 = static_cast<uint32_t>(x), y0 = static_cast<uint32_t>(y);
    const uint32_t x1 = std::min(x0 + 1, image.width - 1), y1 = std::min(y0 + 1, image.height - 1);
    const float tx = x - x0, ty = y - y0;
    const auto at = [&](uint32_t px, uint32_t py) { return image.rgbaPixels[(static_cast<size_t>(py) * image.width + px) * 4 + channel] / 255.0f; };
    return std::lerp(std::lerp(at(x0, y0), at(x1, y0), tx), std::lerp(at(x0, y1), at(x1, y1), tx), ty);
}

std::vector<float> Preprocess(const ImageData& image) {
    std::vector<float> tensor(static_cast<size_t>(3 * ModelWidth * ModelHeight));
    for (int64_t y = 0; y < ModelHeight; ++y) {
        const float sourceY = (static_cast<float>(y) + 0.5f) * image.height / ModelHeight - 0.5f;
        for (int64_t x = 0; x < ModelWidth; ++x) {
            const float sourceX = (static_cast<float>(x) + 0.5f) * image.width / ModelWidth - 0.5f;
            for (uint32_t channel = 0; channel < 3; ++channel) {
                tensor[static_cast<size_t>(channel * ModelWidth * ModelHeight + y * ModelWidth + x)] = (SampleChannelBilinear(image, sourceX, sourceY, channel) - Mean[channel]) / StdDev[channel];
            }
        }
    }
    return tensor;
}

uint8_t SampleMaskBilinear(const std::vector<float>& mask, uint32_t width, uint32_t height, float x, float y) {
    x = std::clamp(x, 0.0f, static_cast<float>(width - 1)); y = std::clamp(y, 0.0f, static_cast<float>(height - 1));
    const uint32_t x0 = static_cast<uint32_t>(x), y0 = static_cast<uint32_t>(y), x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
    const float tx = x - x0, ty = y - y0;
    const float value = std::lerp(std::lerp(mask[static_cast<size_t>(y0) * width + x0], mask[static_cast<size_t>(y0) * width + x1], tx), std::lerp(mask[static_cast<size_t>(y1) * width + x0], mask[static_cast<size_t>(y1) * width + x1], tx), ty);
    return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}
}

SegmentationModel::SegmentationModel() = default;
SegmentationModel::~SegmentationModel() = default;
bool SegmentationModel::IsLoaded() const noexcept { return session_ != nullptr; }
void SegmentationModel::Reset() { session_.reset(); environment_.reset(); inputName_.clear(); outputName_.clear(); provider_ = InferenceProvider::Cpu; providerWarning_.clear(); }

bool SegmentationModel::Load(const std::filesystem::path& modelPath, InferenceDevice device, std::string& error) {
    Reset(); error.clear();
    if (!std::filesystem::exists(modelPath)) { error = "AI model not found: " + modelPath.string() + ". See Models/README.md."; return false; }
    try {
        environment_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "LayerForge");
        if (!CreateOnnxSession(*environment_, modelPath, device, false, session_, provider_, providerWarning_, error)) throw std::runtime_error(error);
        if (session_->GetInputCount() != 1 || session_->GetOutputCount() < 1) throw std::runtime_error("U2NETP must have one input and at least one output.");
        Ort::AllocatorWithDefaultOptions allocator;
        auto inputName = session_->GetInputNameAllocated(0, allocator);
        auto outputName = session_->GetOutputNameAllocated(0, allocator);
        inputName_ = inputName.get(); outputName_ = outputName.get();

        const auto inputInfo = session_->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
        const auto shape = inputInfo.GetShape();
        if (inputInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || shape.size() != 4 || (shape[0] > 0 && shape[0] != 1) || (shape[1] > 0 && shape[1] != 3) || (shape[2] > 0 && shape[2] != ModelHeight) || (shape[3] > 0 && shape[3] != ModelWidth)) {
            throw std::runtime_error("Unexpected input tensor; expected float32 [1,3,320,320].");
        }
        return true;
    } catch (const Ort::Exception& exception) { error = std::string("ONNX Runtime model load failed: ") + exception.what(); }
      catch (const std::exception& exception) { error = std::string("AI model validation failed: ") + exception.what(); }
    Reset(); return false;
}

bool SegmentationModel::Run(const ImageData& image, MaskData& mask, double& inferenceMilliseconds, std::string& error) {
    mask = {}; inferenceMilliseconds = 0.0; error.clear();
    if (!session_) { error = "AI model is not loaded."; return false; }
    if (!image.IsValid()) { error = "The input image is invalid."; return false; }
    try {
        std::vector<float> input = Preprocess(image);
        constexpr std::array<int64_t, 4> inputShape{ 1, 3, ModelHeight, ModelWidth };
        auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(), inputShape.data(), inputShape.size());
        const char* inputNames[] = { inputName_.c_str() }; const char* outputNames[] = { outputName_.c_str() };
        const auto start = std::chrono::steady_clock::now();
        auto outputs = session_->Run(Ort::RunOptions{ nullptr }, inputNames, &tensor, 1, outputNames, 1);
        inferenceMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (outputs.size() != 1 || !outputs[0].IsTensor()) throw std::runtime_error("The model did not return a tensor.");
        const auto info = outputs[0].GetTensorTypeAndShapeInfo(); const auto shape = info.GetShape();
        if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || shape.size() != 4 || shape[0] != 1 || shape[1] != 1 || shape[2] <= 0 || shape[3] <= 0) throw std::runtime_error("Unexpected output tensor; expected float32 [1,1,H,W].");
        const size_t count = info.GetElementCount(); const float* values = outputs[0].GetTensorData<float>();
        const auto [minimum, maximum] = std::minmax_element(values, values + count);
        const float range = *maximum - *minimum;
        if (!std::isfinite(*minimum) || !std::isfinite(*maximum) || range <= std::numeric_limits<float>::epsilon()) throw std::runtime_error("The model returned a constant or non-finite mask.");
        std::vector<float> normalized(count);
        std::transform(values, values + count, normalized.begin(), [&](float value) { return std::clamp((value - *minimum) / range, 0.0f, 1.0f); });
        const uint32_t outputHeight = static_cast<uint32_t>(shape[2]), outputWidth = static_cast<uint32_t>(shape[3]);
        mask.width = image.width; mask.height = image.height; mask.grayscale.resize(static_cast<size_t>(mask.width) * mask.height);
        for (uint32_t y = 0; y < mask.height; ++y) for (uint32_t x = 0; x < mask.width; ++x) {
            const float sourceX = (x + 0.5f) * outputWidth / mask.width - 0.5f, sourceY = (y + 0.5f) * outputHeight / mask.height - 0.5f;
            mask.grayscale[static_cast<size_t>(y) * mask.width + x] = SampleMaskBilinear(normalized, outputWidth, outputHeight, sourceX, sourceY);
        }
        return true;
    } catch (const Ort::Exception& exception) { error = std::string("ONNX Runtime inference failed: ") + exception.what(); }
      catch (const std::exception& exception) { error = std::string("Mask postprocess failed: ") + exception.what(); }
    mask = {}; return false;
}
