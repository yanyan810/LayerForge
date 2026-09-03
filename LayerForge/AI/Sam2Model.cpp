#include "Sam2Model.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <queue>
#include <stdexcept>

namespace {
constexpr int64_t InputSize = 1024;
constexpr int64_t MaskSize = 256;
constexpr float MinimumPredictedIou = 0.50f;
constexpr float MinimumRefinementGain = 0.08f;
constexpr float MinimumRefinementAreaRatio = 0.75f;
constexpr float MaximumRefinementAreaRatio = 1.35f;
constexpr float MinimumBoxMaskIou = 0.70f;
constexpr std::array<float, 3> Mean{ 0.485f, 0.456f, 0.406f };
constexpr std::array<float, 3> StandardDeviation{ 0.229f, 0.224f, 0.225f };

float SampleChannel(const ImageData& image, float x, float y, uint32_t channel) {
    x = std::clamp(x, 0.0f, static_cast<float>(image.width - 1)); y = std::clamp(y, 0.0f, static_cast<float>(image.height - 1));
    const uint32_t x0 = static_cast<uint32_t>(x), y0 = static_cast<uint32_t>(y);
    const uint32_t x1 = std::min(x0 + 1, image.width - 1), y1 = std::min(y0 + 1, image.height - 1);
    const float tx = x - x0, ty = y - y0;
    const auto at = [&](uint32_t px, uint32_t py) { return image.rgbaPixels[(static_cast<size_t>(py) * image.width + px) * 4 + channel] / 255.0f; };
    return std::lerp(std::lerp(at(x0, y0), at(x1, y0), tx), std::lerp(at(x0, y1), at(x1, y1), tx), ty);
}

float SampleLogit(const float* values, float x, float y) {
    x = std::clamp(x, 0.0f, static_cast<float>(MaskSize - 1)); y = std::clamp(y, 0.0f, static_cast<float>(MaskSize - 1));
    const int x0 = static_cast<int>(x), y0 = static_cast<int>(y), x1 = std::min(x0 + 1, static_cast<int>(MaskSize - 1)), y1 = std::min(y0 + 1, static_cast<int>(MaskSize - 1));
    const float tx = x - x0, ty = y - y0;
    return std::lerp(std::lerp(values[y0 * MaskSize + x0], values[y0 * MaskSize + x1], tx),
        std::lerp(values[y1 * MaskSize + x0], values[y1 * MaskSize + x1], tx), ty);
}

std::vector<uint8_t> BinaryMask(const std::vector<float>& logits) {
    std::vector<uint8_t> result(logits.size());
    std::transform(logits.begin(), logits.end(), result.begin(), [](float value) { return value >= 0.0f ? 1u : 0u; });
    return result;
}

uint32_t CountComponents(const std::vector<uint8_t>& mask) {
    std::vector<uint8_t> visited(mask.size());
    std::queue<size_t> pending;
    uint32_t components = 0;
    for (size_t start = 0; start < mask.size(); ++start) {
        if (!mask[start] || visited[start]) continue;
        ++components; visited[start] = 1; pending.push(start);
        while (!pending.empty()) {
            const size_t index = pending.front(); pending.pop();
            const int x = static_cast<int>(index % MaskSize), y = static_cast<int>(index / MaskSize);
            const auto visit = [&](int nx, int ny) {
                if (nx < 0 || ny < 0 || nx >= MaskSize || ny >= MaskSize) return;
                const size_t next = static_cast<size_t>(ny * MaskSize + nx);
                if (mask[next] && !visited[next]) { visited[next] = 1; pending.push(next); }
            };
            visit(x - 1, y); visit(x + 1, y); visit(x, y - 1); visit(x, y + 1);
        }
    }
    return components;
}

bool DeepestInteriorPoint(const std::vector<uint8_t>& mask, const ImageData& image, Sam2PromptPoint& point) {
    std::vector<int> distance(mask.size(), -1);
    std::queue<size_t> pending;
    for (size_t index = 0; index < mask.size(); ++index) {
        if (!mask[index]) { distance[index] = 0; pending.push(index); }
    }
    if (pending.empty()) return false;
    size_t best = 0; int bestDistance = 0;
    while (!pending.empty()) {
        const size_t index = pending.front(); pending.pop();
        const int x = static_cast<int>(index % MaskSize), y = static_cast<int>(index / MaskSize);
        const auto visit = [&](int nx, int ny) {
            if (nx < 0 || ny < 0 || nx >= MaskSize || ny >= MaskSize) return;
            const size_t next = static_cast<size_t>(ny * MaskSize + nx);
            if (distance[next] >= 0) return;
            distance[next] = distance[index] + 1;
            if (distance[next] > bestDistance) { bestDistance = distance[next]; best = next; }
            pending.push(next);
        };
        visit(x - 1, y); visit(x + 1, y); visit(x, y - 1); visit(x, y + 1);
    }
    if (bestDistance <= 0) return false;
    point.x = (static_cast<float>(best % MaskSize) + 0.5f) * image.width / MaskSize;
    point.y = (static_cast<float>(best / MaskSize) + 0.5f) * image.height / MaskSize;
    point.label = 1;
    return true;
}
}

Sam2Model::Sam2Model() = default;
Sam2Model::~Sam2Model() = default;
bool Sam2Model::IsLoaded() const noexcept { return encoder_ && decoder_; }
void Sam2Model::ClearImageCache() { imageEmbed_.clear(); highResolution0_.clear(); highResolution1_.clear(); cachedWidth_ = cachedHeight_ = 0; }
void Sam2Model::Reset() { ClearImageCache(); imageInput_.clear(); decoder_.reset(); encoder_.reset(); environment_.reset(); encoderProvider_ = decoderProvider_ = InferenceProvider::Cpu; providerWarning_.clear(); }

bool Sam2Model::Load(const std::filesystem::path& encoderPath, const std::filesystem::path& decoderPath,
    InferenceDevice encoderDevice, InferenceDevice decoderDevice, std::string& error) {
    Reset(); error.clear();
    if (!std::filesystem::is_regular_file(encoderPath) || !std::filesystem::is_regular_file(decoderPath)) {
        error = "SAM2 ONNX model files are missing. See Models/README.md."; return false;
    }
    try {
        environment_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "LayerForgeSAM2");
        std::string encoderWarning, decoderWarning;
        if (!CreateOnnxSession(*environment_, encoderPath, encoderDevice, true, encoder_, encoderProvider_, encoderWarning, error)) throw std::runtime_error(error);
        if (!CreateOnnxSession(*environment_, decoderPath, decoderDevice, true, decoder_, decoderProvider_, decoderWarning, error)) throw std::runtime_error(error);
        providerWarning_ = encoderWarning;
        if (!decoderWarning.empty()) { if (!providerWarning_.empty()) providerWarning_ += ' '; providerWarning_ += decoderWarning; }
        if (encoder_->GetInputCount() != 1 || encoder_->GetOutputCount() != 3 || decoder_->GetInputCount() != 5 || decoder_->GetOutputCount() != 2)
            throw std::runtime_error("Unexpected SAM2 encoder/decoder contract.");
        return true;
    } catch (const Ort::Exception& exception) { error = std::string("SAM2 ONNX load failed: ") + exception.what(); }
      catch (const std::exception& exception) { error = std::string("SAM2 model validation failed: ") + exception.what(); }
    Reset(); return false;
}

bool Sam2Model::Encode(const ImageData& image, double& milliseconds, std::string& error) {
    milliseconds = 0.0;
    if (!imageEmbed_.empty() && cachedWidth_ == image.width && cachedHeight_ == image.height) return true;
    ClearImageCache();
    imageInput_.resize(static_cast<size_t>(3 * InputSize * InputSize));
    for (int64_t y = 0; y < InputSize; ++y) for (int64_t x = 0; x < InputSize; ++x) {
        const float sourceX = (x + 0.5f) * image.width / InputSize - 0.5f;
        const float sourceY = (y + 0.5f) * image.height / InputSize - 0.5f;
        for (uint32_t channel = 0; channel < 3; ++channel)
            imageInput_[static_cast<size_t>(channel * InputSize * InputSize + y * InputSize + x)] = (SampleChannel(image, sourceX, sourceY, channel) - Mean[channel]) / StandardDeviation[channel];
    }
    constexpr std::array<int64_t, 4> shape{ 1, 3, InputSize, InputSize };
    auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value tensor = Ort::Value::CreateTensor<float>(memory, imageInput_.data(), imageInput_.size(), shape.data(), shape.size());
    const char* inputNames[]{ "image" }; const char* outputNames[]{ "image_embed", "high_res_0", "high_res_1" };
    const auto start = std::chrono::steady_clock::now();
    auto outputs = encoder_->Run(Ort::RunOptions{ nullptr }, inputNames, &tensor, 1, outputNames, 3);
    milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    auto copy = [](const Ort::Value& value) {
        const auto count = value.GetTensorTypeAndShapeInfo().GetElementCount(); const float* data = value.GetTensorData<float>();
        return std::vector<float>(data, data + count);
    };
    imageEmbed_ = copy(outputs[0]); highResolution0_ = copy(outputs[1]); highResolution1_ = copy(outputs[2]);
    cachedWidth_ = image.width; cachedHeight_ = image.height; return true;
}

bool Sam2Model::Decode(const std::vector<float>& points, const std::vector<int32_t>& labels,
    std::vector<float>& logits, float& predictedIou, double& milliseconds, std::string& error) {
    if (points.size() != labels.size() * 2 || labels.empty()) { error = "SAM2 received invalid prompt points."; return false; }
    constexpr std::array<int64_t, 4> embedShape{ 1, 256, 64, 64 }, high0Shape{ 1, 32, 256, 256 }, high1Shape{ 1, 64, 128, 128 };
    const std::array<int64_t, 3> pointShape{ 1, static_cast<int64_t>(labels.size()), 2 };
    const std::array<int64_t, 2> labelShape{ 1, static_cast<int64_t>(labels.size()) };
    auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::array<Ort::Value, 5> inputs{
        Ort::Value::CreateTensor<float>(memory, imageEmbed_.data(), imageEmbed_.size(), embedShape.data(), embedShape.size()),
        Ort::Value::CreateTensor<float>(memory, highResolution0_.data(), highResolution0_.size(), high0Shape.data(), high0Shape.size()),
        Ort::Value::CreateTensor<float>(memory, highResolution1_.data(), highResolution1_.size(), high1Shape.data(), high1Shape.size()),
        Ort::Value::CreateTensor<float>(memory, const_cast<float*>(points.data()), points.size(), pointShape.data(), pointShape.size()),
        Ort::Value::CreateTensor<int32_t>(memory, const_cast<int32_t*>(labels.data()), labels.size(), labelShape.data(), labelShape.size())
    };
    const char* inputNames[]{ "image_embed", "high_res_0", "high_res_1", "prompt_points_1024", "prompt_labels" };
    const char* outputNames[]{ "mask_logits", "predicted_iou" };
    const auto start = std::chrono::steady_clock::now();
    auto outputs = decoder_->Run(Ort::RunOptions{ nullptr }, inputNames, inputs.data(), inputs.size(), outputNames, 2);
    milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    predictedIou = outputs[1].GetTensorData<float>()[0];
    const auto count = outputs[0].GetTensorTypeAndShapeInfo().GetElementCount();
    const float* values = outputs[0].GetTensorData<float>(); logits.assign(values, values + count);
    return std::isfinite(predictedIou) && logits.size() == static_cast<size_t>(MaskSize * MaskSize);
}

bool Sam2Model::LogitsToMask(const std::vector<float>& logits, const ImageData& image, MaskData& mask, std::string& error) const {
    if (logits.size() != static_cast<size_t>(MaskSize * MaskSize) || !image.IsValid()) {
        error = "SAM2 returned invalid mask logits."; return false;
    }
    mask.width = image.width; mask.height = image.height;
    mask.grayscale.resize(static_cast<size_t>(mask.width) * mask.height);
    for (uint32_t y = 0; y < mask.height; ++y) for (uint32_t x = 0; x < mask.width; ++x) {
        const float sourceX = (x + 0.5f) * MaskSize / mask.width - 0.5f;
        const float sourceY = (y + 0.5f) * MaskSize / mask.height - 0.5f;
        const float probability = 1.0f / (1.0f + std::exp(-SampleLogit(logits.data(), sourceX, sourceY)));
        mask.grayscale[static_cast<size_t>(y) * mask.width + x] = static_cast<uint8_t>(std::clamp(probability, 0.0f, 1.0f) * 255.0f + 0.5f);
    }
    return true;
}

bool Sam2Model::RefineWithPrompts(const ImageData& image, const DetectionBox& box,
    const std::vector<Sam2PromptPoint>& prompts, MaskData& mask, float& predictedIou,
    double& decoderMilliseconds, std::string& error) {
    mask = {}; predictedIou = 0.0f; decoderMilliseconds = 0.0; error.clear();
    if (!IsLoaded() || imageEmbed_.empty() || cachedWidth_ != image.width || cachedHeight_ != image.height) {
        error = "SAM2 cached image features are not available for Smart Correction."; return false;
    }
    if (!image.IsValid() || !box.IsValid() || prompts.empty() || prompts.size() > 5) {
        error = "SAM2 received invalid Smart Correction prompts."; return false;
    }
    try {
        const float scaleX = static_cast<float>(InputSize) / image.width;
        const float scaleY = static_cast<float>(InputSize) / image.height;
        std::vector<float> points{ box.x1 * scaleX, box.y1 * scaleY, box.x2 * scaleX, box.y2 * scaleY };
        std::vector<int32_t> labels{ 2, 3 };
        for (const Sam2PromptPoint& prompt : prompts) {
            if (prompt.label != 0 && prompt.label != 1) { error = "Smart Correction point label must be 0 or 1."; return false; }
            points.push_back(prompt.x * scaleX); points.push_back(prompt.y * scaleY); labels.push_back(prompt.label);
        }
        std::vector<float> logits;
        if (!Decode(points, labels, logits, predictedIou, decoderMilliseconds, error)) {
            if (error.empty()) error = "SAM2 Smart Correction decoder returned invalid output.";
            return false;
        }
        return LogitsToMask(logits, image, mask, error);
    } catch (const Ort::Exception& exception) { error = std::string("SAM2 Smart Correction failed: ") + exception.what(); }
      catch (const std::exception& exception) { error = std::string("SAM2 Smart Correction failed: ") + exception.what(); }
    mask = {}; return false;
}

bool Sam2Model::Run(const ImageData& image, const DetectionBox& box, MaskData& mask, float& predictedIou,
    Sam2Timings& timings, Sam2RefinementInfo& refinement, std::string& error,
    const std::function<void()>& encoderComplete) {
    mask = {}; predictedIou = 0.0f; timings = {}; refinement = {}; error.clear();
    if (!IsLoaded()) { error = "SAM2 is not loaded."; return false; }
    if (!image.IsValid() || !box.IsValid()) { error = "SAM2 received an invalid image or box."; return false; }
    try {
        if (!Encode(image, timings.encoderMilliseconds, error)) return false;
        if (encoderComplete) encoderComplete();

        const float scaleX = static_cast<float>(InputSize) / image.width;
        const float scaleY = static_cast<float>(InputSize) / image.height;
        std::vector<float> boxPoints{ box.x1 * scaleX, box.y1 * scaleY, box.x2 * scaleX, box.y2 * scaleY };
        const std::vector<int32_t> boxLabels{ 2, 3 };
        std::vector<float> boxLogits;
        if (!Decode(boxPoints, boxLabels, boxLogits, refinement.boxPredictedIou,
            timings.boxDecoderMilliseconds, error)) {
            if (error.empty()) error = "SAM2 box decoder returned invalid output.";
            return false;
        }
        if (refinement.boxPredictedIou < MinimumPredictedIou) {
            error = "SAM2 rejected the hair mask because predicted IoU was too low."; return false;
        }

        const auto pointStart = std::chrono::steady_clock::now();
        const std::vector<uint8_t> boxBinary = BinaryMask(boxLogits);
        Sam2PromptPoint positive;
        const bool hasPositive = DeepestInteriorPoint(boxBinary, image, positive);
        if (hasPositive) refinement.points.push_back(positive);
        refinement.boxComponents = CountComponents(boxBinary);
        timings.pointGenerationMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - pointStart).count();

        std::vector<float> selectedLogits = boxLogits;
        predictedIou = refinement.boxPredictedIou;
        if (hasPositive) {
            std::vector<float> refinedPoints = boxPoints;
            refinedPoints.push_back(positive.x * scaleX); refinedPoints.push_back(positive.y * scaleY);
            const std::vector<int32_t> refinedLabels{ 2, 3, positive.label };
            std::vector<float> refinedLogits;
            std::string refinementError;
            if (Decode(refinedPoints, refinedLabels, refinedLogits, refinement.refinedPredictedIou,
                timings.refinedDecoderMilliseconds, refinementError)) {
                const std::vector<uint8_t> refinedBinary = BinaryMask(refinedLogits);
                size_t boxArea = 0, refinedArea = 0, intersection = 0, unionArea = 0;
                for (size_t index = 0; index < boxBinary.size(); ++index) {
                    boxArea += boxBinary[index]; refinedArea += refinedBinary[index];
                    intersection += boxBinary[index] && refinedBinary[index];
                    unionArea += boxBinary[index] || refinedBinary[index];
                }
                refinement.areaRatio = static_cast<float>(refinedArea) / std::max<size_t>(1, boxArea);
                refinement.boxMaskIou = static_cast<float>(intersection) / std::max<size_t>(1, unionArea);
                refinement.refinedComponents = CountComponents(refinedBinary);
                refinement.applied = refinement.refinedPredictedIou >= refinement.boxPredictedIou + MinimumRefinementGain &&
                    refinement.areaRatio >= MinimumRefinementAreaRatio && refinement.areaRatio <= MaximumRefinementAreaRatio &&
                    refinement.boxMaskIou >= MinimumBoxMaskIou &&
                    refinement.refinedComponents <= refinement.boxComponents + 2;
                if (refinement.applied) {
                    selectedLogits = std::move(refinedLogits); predictedIou = refinement.refinedPredictedIou;
                }
            }
        }
        timings.decoderMilliseconds = timings.boxDecoderMilliseconds + timings.refinedDecoderMilliseconds;

        return LogitsToMask(selectedLogits, image, mask, error);
    } catch (const Ort::Exception& exception) { error = std::string("SAM2 inference failed: ") + exception.what(); }
      catch (const std::exception& exception) { error = std::string("SAM2 postprocess failed: ") + exception.what(); }
    mask = {}; return false;
}
