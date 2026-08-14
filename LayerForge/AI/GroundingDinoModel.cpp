#include "GroundingDinoModel.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace {
constexpr int64_t InputHeight = 800;
constexpr int64_t InputWidth = 1333;
constexpr float BoxThreshold = 0.30f;
constexpr std::array<float, 3> Mean{ 0.485f, 0.456f, 0.406f };
constexpr std::array<float, 3> StandardDeviation{ 0.229f, 0.224f, 0.225f };

float SampleChannel(const ImageData& image, float x, float y, uint32_t channel) {
    x = std::clamp(x, 0.0f, static_cast<float>(image.width - 1));
    y = std::clamp(y, 0.0f, static_cast<float>(image.height - 1));
    const uint32_t x0 = static_cast<uint32_t>(x), y0 = static_cast<uint32_t>(y);
    const uint32_t x1 = std::min(x0 + 1, image.width - 1), y1 = std::min(y0 + 1, image.height - 1);
    const float tx = x - x0, ty = y - y0;
    const auto at = [&](uint32_t px, uint32_t py) {
        return image.rgbaPixels[(static_cast<size_t>(py) * image.width + px) * 4 + channel] / 255.0f;
    };
    return std::lerp(std::lerp(at(x0, y0), at(x1, y0), tx), std::lerp(at(x0, y1), at(x1, y1), tx), ty);
}
}

GroundingDinoModel::GroundingDinoModel() = default;
GroundingDinoModel::~GroundingDinoModel() = default;
bool GroundingDinoModel::IsLoaded() const noexcept { return session_ != nullptr; }
void GroundingDinoModel::Reset() { session_.reset(); environment_.reset(); inputNames_.clear(); outputNames_.clear(); inputPixels_.clear(); inputMask_.clear(); provider_ = InferenceProvider::Cpu; providerWarning_.clear(); }

bool GroundingDinoModel::Load(const std::filesystem::path& modelPath, InferenceDevice device, std::string& error) {
    Reset(); error.clear();
    if (!std::filesystem::is_regular_file(modelPath)) {
        error = "Grounding DINO model not found: " + modelPath.string() + ". See Models/README.md."; return false;
    }
    try {
        environment_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "LayerForgeGroundingDINO");
        // DINO's fixed 800x1333 graph has large transient buffers. Retaining them in the
        // CPU arena after Run would make loading SAM2 unnecessarily expensive.
        if (!CreateOnnxSession(*environment_, modelPath, device, true, session_, provider_, providerWarning_, error)) throw std::runtime_error(error);
        if (session_->GetInputCount() != 2 || session_->GetOutputCount() != 2) throw std::runtime_error("Expected two inputs and two outputs.");
        Ort::AllocatorWithDefaultOptions allocator;
        for (size_t i = 0; i < 2; ++i) { auto name = session_->GetInputNameAllocated(i, allocator); inputNames_.emplace_back(name.get()); }
        for (size_t i = 0; i < 2; ++i) { auto name = session_->GetOutputNameAllocated(i, allocator); outputNames_.emplace_back(name.get()); }
        const auto imageShape = session_->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        const auto maskShape = session_->GetInputTypeInfo(1).GetTensorTypeAndShapeInfo().GetShape();
        if (imageShape != std::vector<int64_t>({ 1, 3, InputHeight, InputWidth }) || maskShape != std::vector<int64_t>({ 1, InputHeight, InputWidth }))
            throw std::runtime_error("Unexpected tensor contract; re-export with Scripts/export_phase4e_models.py.");
        return true;
    } catch (const Ort::Exception& exception) { error = std::string("Grounding DINO ONNX load failed: ") + exception.what(); }
      catch (const std::exception& exception) { error = std::string("Grounding DINO model validation failed: ") + exception.what(); }
    Reset(); return false;
}

bool GroundingDinoModel::Run(const ImageData& image, const DetectionBox& region, std::vector<DetectionBox>& boxes,
    double& inferenceMilliseconds, std::string& error) {
    boxes.clear(); inferenceMilliseconds = 0.0; error.clear();
    if (!session_) { error = "Grounding DINO is not loaded."; return false; }
    if (!image.IsValid() || !region.IsValid()) { error = "Grounding DINO received an invalid image region."; return false; }
    try {
        const float shortEdgeScale = 800.0f / std::min(region.Width(), region.Height());
        const float longEdgeScale = 1333.0f / std::max(region.Width(), region.Height());
        const float scale = std::min({ shortEdgeScale, longEdgeScale, static_cast<float>(InputWidth) / region.Width(), static_cast<float>(InputHeight) / region.Height() });
        const int validWidth = std::clamp(static_cast<int>(std::round(region.Width() * scale)), 1, static_cast<int>(InputWidth));
        const int validHeight = std::clamp(static_cast<int>(std::round(region.Height() * scale)), 1, static_cast<int>(InputHeight));
        inputPixels_.resize(static_cast<size_t>(3 * InputWidth * InputHeight));
        inputMask_.resize(static_cast<size_t>(InputWidth * InputHeight));
        std::fill(inputPixels_.begin(), inputPixels_.end(), 0.0f);
        std::fill(inputMask_.begin(), inputMask_.end(), 0);
        for (int y = 0; y < validHeight; ++y) {
            const float sourceY = region.y1 + (y + 0.5f) / scale - 0.5f;
            for (int x = 0; x < validWidth; ++x) {
                const float sourceX = region.x1 + (x + 0.5f) / scale - 0.5f;
                inputMask_[static_cast<size_t>(y) * InputWidth + x] = 1;
                for (uint32_t channel = 0; channel < 3; ++channel) {
                    inputPixels_[static_cast<size_t>(channel * InputWidth * InputHeight + y * InputWidth + x)] =
                        (SampleChannel(image, sourceX, sourceY, channel) - Mean[channel]) / StandardDeviation[channel];
                }
            }
        }

        constexpr std::array<int64_t, 4> pixelShape{ 1, 3, InputHeight, InputWidth };
        constexpr std::array<int64_t, 3> maskShape{ 1, InputHeight, InputWidth };
        auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::array<Ort::Value, 2> inputs{
            Ort::Value::CreateTensor<float>(memory, inputPixels_.data(), inputPixels_.size(), pixelShape.data(), pixelShape.size()),
            Ort::Value::CreateTensor(memory, inputMask_.data(), inputMask_.size(), maskShape.data(), maskShape.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL)
        };
        const char* inputNames[]{ inputNames_[0].c_str(), inputNames_[1].c_str() };
        const char* outputNames[]{ outputNames_[0].c_str(), outputNames_[1].c_str() };
        const auto start = std::chrono::steady_clock::now();
        auto outputs = session_->Run(Ort::RunOptions{ nullptr }, inputNames, inputs.data(), inputs.size(), outputNames, 2);
        inferenceMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        const auto logitsShape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        const auto boxesShape = outputs[1].GetTensorTypeAndShapeInfo().GetShape();
        if (logitsShape.size() != 3 || boxesShape.size() != 3 || logitsShape[0] != 1 || boxesShape[0] != 1 ||
            logitsShape[1] != boxesShape[1] || boxesShape[2] != 4) throw std::runtime_error("Unexpected detection output tensors.");
        const float* logits = outputs[0].GetTensorData<float>();
        const float* rawBoxes = outputs[1].GetTensorData<float>();
        const size_t queries = static_cast<size_t>(boxesShape[1]), tokens = static_cast<size_t>(logitsShape[2]);
        for (size_t query = 0; query < queries; ++query) {
            const float maximumLogit = *std::max_element(logits + query * tokens, logits + (query + 1) * tokens);
            const float confidence = 1.0f / (1.0f + std::exp(-maximumLogit));
            if (!std::isfinite(confidence) || confidence < BoxThreshold) continue;
            const float centerX = rawBoxes[query * 4] * InputWidth;
            const float centerY = rawBoxes[query * 4 + 1] * InputHeight;
            const float width = rawBoxes[query * 4 + 2] * InputWidth;
            const float height = rawBoxes[query * 4 + 3] * InputHeight;
            const float localX1 = std::clamp(centerX - width * 0.5f, 0.0f, static_cast<float>(validWidth));
            const float localY1 = std::clamp(centerY - height * 0.5f, 0.0f, static_cast<float>(validHeight));
            const float localX2 = std::clamp(centerX + width * 0.5f, 0.0f, static_cast<float>(validWidth));
            const float localY2 = std::clamp(centerY + height * 0.5f, 0.0f, static_cast<float>(validHeight));
            DetectionBox box;
            box.x1 = region.x1 + localX1 / scale; box.y1 = region.y1 + localY1 / scale;
            box.x2 = region.x1 + localX2 / scale; box.y2 = region.y1 + localY2 / scale;
            box.confidence = confidence;
            if (box.IsValid()) boxes.push_back(box);
        }
        return true;
    } catch (const Ort::Exception& exception) { error = std::string("Grounding DINO inference failed: ") + exception.what(); }
      catch (const std::exception& exception) { error = std::string("Grounding DINO postprocess failed: ") + exception.what(); }
    boxes.clear(); return false;
}
