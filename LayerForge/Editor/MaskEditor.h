#pragma once

#include "../AI/MaskData.h"

#include <cstdint>
#include <vector>

enum class MaskBrushMode {
    Add,
    Erase,
};

struct MaskBrushSettings {
    MaskBrushMode mode = MaskBrushMode::Add;
    float size = 40.0f;
    float strength = 1.0f;
    float hardness = 1.0f;
};

struct MaskStrokePoint {
    float x = 0.0f;
    float y = 0.0f;
};

class MaskEditor {
public:
    static constexpr size_t HistoryLimit = 20;

    void Initialize(uint32_t width, uint32_t height);
    void Clear();
    void ResetManualEdit();

    bool BeginStroke(float imageX, float imageY);
    bool ContinueStroke(float imageX, float imageY);
    bool EndStroke();
    bool Undo();
    bool Redo();
    void ClearStrokePreview() noexcept { strokePreview_.clear(); }

    bool Apply(const MaskData& autoMask, MaskData& finalMask) const;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] bool IsStrokeActive() const noexcept { return strokeActive_; }
    [[nodiscard]] bool HasManualEdit() const noexcept;
    [[nodiscard]] bool CanUndo() const noexcept { return !undo_.empty(); }
    [[nodiscard]] bool CanRedo() const noexcept { return !redo_.empty(); }
    [[nodiscard]] uint32_t Width() const noexcept { return width_; }
    [[nodiscard]] uint32_t Height() const noexcept { return height_; }
    [[nodiscard]] MaskBrushSettings& Settings() noexcept { return settings_; }
    [[nodiscard]] const MaskBrushSettings& Settings() const noexcept { return settings_; }
    [[nodiscard]] const std::vector<MaskStrokePoint>& StrokePreview() const noexcept { return strokePreview_; }
    [[nodiscard]] MaskBrushMode StrokePreviewMode() const noexcept { return strokePreviewMode_; }
    [[nodiscard]] float StrokePreviewSize() const noexcept { return strokePreviewSize_; }
    [[nodiscard]] float StrokePreviewStrength() const noexcept { return strokePreviewStrength_; }

    static bool ScreenToImage(float screenX, float screenY, float previewX, float previewY,
        float previewWidth, float previewHeight, uint32_t imageWidth, uint32_t imageHeight,
        float& imageX, float& imageY) noexcept;

private:
    bool Stamp(float imageX, float imageY);
    void PushUndo(std::vector<int8_t>&& snapshot);

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    std::vector<int8_t> manual_;
    std::vector<std::vector<int8_t>> undo_;
    std::vector<std::vector<int8_t>> redo_;
    std::vector<int8_t> strokeBefore_;
    MaskBrushSettings settings_;
    float previousX_ = 0.0f;
    float previousY_ = 0.0f;
    bool strokeActive_ = false;
    bool strokeChanged_ = false;
    std::vector<MaskStrokePoint> strokePreview_;
    MaskBrushMode strokePreviewMode_ = MaskBrushMode::Add;
    float strokePreviewSize_ = 1.0f;
    float strokePreviewStrength_ = 1.0f;
};
