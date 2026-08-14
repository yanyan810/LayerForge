#include "MaskEditor.h"

#include <algorithm>
#include <cmath>

void MaskEditor::Initialize(uint32_t width, uint32_t height) {
    width_ = width; height_ = height;
    manual_.assign(static_cast<size_t>(width) * height, 0);
    undo_.clear(); redo_.clear(); strokeBefore_.clear();
    strokePreview_.clear();
    strokeActive_ = strokeChanged_ = false;
}

void MaskEditor::Clear() {
    width_ = height_ = 0; manual_.clear(); undo_.clear(); redo_.clear(); strokeBefore_.clear(); strokePreview_.clear();
    strokeActive_ = strokeChanged_ = false;
}

void MaskEditor::ResetManualEdit() {
    std::fill(manual_.begin(), manual_.end(), 0);
    undo_.clear(); redo_.clear(); strokeBefore_.clear();
    strokePreview_.clear();
    strokeActive_ = strokeChanged_ = false;
}

bool MaskEditor::IsInitialized() const noexcept {
    return width_ > 0 && height_ > 0 && manual_.size() == static_cast<size_t>(width_) * height_;
}

bool MaskEditor::HasManualEdit() const noexcept {
    return std::any_of(manual_.begin(), manual_.end(), [](int8_t value) { return value != 0; });
}

bool MaskEditor::BeginStroke(float imageX, float imageY) {
    if (!IsInitialized() || strokeActive_) return false;
    strokeBefore_ = manual_; strokeActive_ = true; strokeChanged_ = false;
    strokePreview_.clear(); strokePreviewMode_ = settings_.mode;
    strokePreviewSize_ = settings_.size; strokePreviewStrength_ = settings_.strength;
    previousX_ = imageX; previousY_ = imageY;
    strokeChanged_ = Stamp(imageX, imageY);
    if (strokeChanged_) strokePreview_.push_back({ imageX, imageY });
    return strokeChanged_;
}

bool MaskEditor::ContinueStroke(float imageX, float imageY) {
    if (!strokeActive_) return false;
    const float deltaX = imageX - previousX_, deltaY = imageY - previousY_;
    const float distance = std::sqrt(deltaX * deltaX + deltaY * deltaY);
    const float spacing = std::max(1.0f, settings_.size * 0.20f);
    const int steps = std::max(1, static_cast<int>(std::ceil(distance / spacing)));
    bool changed = false;
    for (int step = 1; step <= steps; ++step) {
        const float amount = static_cast<float>(step) / steps;
        const float x = previousX_ + deltaX * amount, y = previousY_ + deltaY * amount;
        if (Stamp(x, y)) { strokePreview_.push_back({ x, y }); changed = true; }
    }
    previousX_ = imageX; previousY_ = imageY; strokeChanged_ = strokeChanged_ || changed;
    return changed;
}

bool MaskEditor::EndStroke() {
    if (!strokeActive_) return false;
    strokeActive_ = false;
    if (strokeChanged_) {
        PushUndo(std::move(strokeBefore_)); redo_.clear();
    } else {
        strokeBefore_.clear();
    }
    const bool changed = strokeChanged_; strokeChanged_ = false;
    return changed;
}

void MaskEditor::PushUndo(std::vector<int8_t>&& snapshot) {
    if (undo_.size() == HistoryLimit) undo_.erase(undo_.begin());
    undo_.push_back(std::move(snapshot));
}

bool MaskEditor::Undo() {
    if (strokeActive_) EndStroke();
    if (undo_.empty()) return false;
    strokePreview_.clear();
    if (redo_.size() == HistoryLimit) redo_.erase(redo_.begin());
    redo_.push_back(std::move(manual_));
    manual_ = std::move(undo_.back()); undo_.pop_back();
    return true;
}

bool MaskEditor::Redo() {
    if (strokeActive_) EndStroke();
    if (redo_.empty()) return false;
    strokePreview_.clear();
    PushUndo(std::move(manual_));
    manual_ = std::move(redo_.back()); redo_.pop_back();
    return true;
}

bool MaskEditor::Stamp(float imageX, float imageY) {
    if (!IsInitialized() || imageX < 0.0f || imageY < 0.0f || imageX >= width_ || imageY >= height_) return false;
    // A one-pixel brush must still hit the nearest pixel when the mapped cursor
    // lands exactly on an integer image coordinate (pixel centers are at +0.5).
    const float radius = std::max(0.75f, settings_.size * 0.5f);
    const float strength = std::clamp(settings_.strength, 0.0f, 1.0f);
    const float hardness = std::clamp(settings_.hardness, 0.0f, 1.0f);
    if (strength <= 0.0f) return false;
    const int minimumX = std::max(0, static_cast<int>(std::floor(imageX - radius)));
    const int maximumX = std::min(static_cast<int>(width_) - 1, static_cast<int>(std::ceil(imageX + radius)));
    const int minimumY = std::max(0, static_cast<int>(std::floor(imageY - radius)));
    const int maximumY = std::min(static_cast<int>(height_) - 1, static_cast<int>(std::ceil(imageY + radius)));
    const int sign = settings_.mode == MaskBrushMode::Add ? 1 : -1;
    bool changed = false;
    for (int y = minimumY; y <= maximumY; ++y) for (int x = minimumX; x <= maximumX; ++x) {
        const float dx = (x + 0.5f) - imageX, dy = (y + 0.5f) - imageY;
        const float normalized = std::sqrt(dx * dx + dy * dy) / radius;
        if (normalized > 1.0f) continue;
        float coverage = 1.0f;
        if (normalized > hardness) coverage = hardness >= 1.0f ? 0.0f : (1.0f - normalized) / (1.0f - hardness);
        const int magnitude = std::clamp(static_cast<int>(std::lround(strength * coverage * 127.0f)), 0, 127);
        if (magnitude == 0) continue;
        const int8_t value = static_cast<int8_t>(sign * magnitude);
        int8_t& current = manual_[static_cast<size_t>(y) * width_ + x];
        const bool sameMode = (current > 0) == (value > 0) && current != 0;
        const int8_t next = sameMode && std::abs(static_cast<int>(current)) >= magnitude ? current : value;
        if (current != next) { current = next; changed = true; }
    }
    return changed;
}

bool MaskEditor::Apply(const MaskData& autoMask, MaskData& finalMask) const {
    if (!IsInitialized() || !autoMask.IsValid() || autoMask.width != width_ || autoMask.height != height_) return false;
    finalMask.width = width_; finalMask.height = height_; finalMask.grayscale.resize(manual_.size());
    for (size_t index = 0; index < manual_.size(); ++index) {
        const int operation = manual_[index];
        if (operation > 0) {
            const int addAlpha = static_cast<int>(std::lround(operation * 255.0 / 127.0));
            finalMask.grayscale[index] = static_cast<uint8_t>(std::max<int>(autoMask.grayscale[index], addAlpha));
        } else if (operation < 0) {
            const int remaining = 255 - static_cast<int>(std::lround(-operation * 255.0 / 127.0));
            finalMask.grayscale[index] = static_cast<uint8_t>(std::min<int>(autoMask.grayscale[index], remaining));
        } else {
            finalMask.grayscale[index] = autoMask.grayscale[index];
        }
    }
    return true;
}

bool MaskEditor::ScreenToImage(float screenX, float screenY, float previewX, float previewY,
    float previewWidth, float previewHeight, uint32_t imageWidth, uint32_t imageHeight,
    float& imageX, float& imageY) noexcept {
    if (previewWidth <= 0.0f || previewHeight <= 0.0f || imageWidth == 0 || imageHeight == 0 ||
        screenX < previewX || screenY < previewY || screenX >= previewX + previewWidth || screenY >= previewY + previewHeight) return false;
    imageX = (screenX - previewX) * imageWidth / previewWidth;
    imageY = (screenY - previewY) * imageHeight / previewHeight;
    return imageX >= 0.0f && imageY >= 0.0f && imageX < imageWidth && imageY < imageHeight;
}
