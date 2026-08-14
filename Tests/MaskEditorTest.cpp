#include "Editor/MaskEditor.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

uint8_t Pixel(const MaskData& mask, uint32_t x, uint32_t y) {
    return mask.grayscale[static_cast<size_t>(y) * mask.width + x];
}
}

int main() {
    try {
        MaskData automatic; automatic.width = 32; automatic.height = 16;
        automatic.grayscale.assign(32 * 16, 96);
        MaskEditor editor; editor.Initialize(automatic.width, automatic.height);
        MaskData finalMask;
        Require(editor.Apply(automatic, finalMask), "Initial apply failed.");
        Require(finalMask.grayscale == automatic.grayscale, "Untouched Final mask differs from Auto mask.");

        float imageX = 0.0f, imageY = 0.0f;
        Require(MaskEditor::ScreenToImage(150.0f, 75.0f, 100.0f, 50.0f, 320.0f, 160.0f, 32, 16, imageX, imageY),
            "Coordinate mapping rejected an inside point.");
        Require(imageX == 5.0f && imageY == 2.5f, "Coordinate mapping returned the wrong pixel position.");
        Require(!MaskEditor::ScreenToImage(99.0f, 75.0f, 100.0f, 50.0f, 320.0f, 160.0f, 32, 16, imageX, imageY),
            "Coordinate mapping accepted an outside point.");

        auto& brush = editor.Settings(); brush.mode = MaskBrushMode::Add; brush.size = 1.0f;
        brush.strength = 1.0f; brush.hardness = 1.0f;
        Require(editor.BeginStroke(1.0f, 1.0f) && editor.EndStroke(), "One-pixel brush missed an integer coordinate.");
        Require(editor.Undo(), "One-pixel brush cleanup failed.");

        brush.mode = MaskBrushMode::Add; brush.size = 4.0f;
        brush.strength = 1.0f; brush.hardness = 1.0f;
        Require(editor.BeginStroke(3.0f, 8.0f), "Add stroke did not begin.");
        Require(editor.ContinueStroke(27.0f, 8.0f), "Interpolated Add stroke did not change pixels.");
        Require(editor.EndStroke(), "Add stroke was not committed.");
        Require(editor.StrokePreview().size() > 2, "Live Stroke preview did not retain the interpolated trail.");
        editor.ClearStrokePreview(); Require(editor.StrokePreview().empty(), "Stroke preview did not clear after texture commit.");
        Require(editor.Apply(automatic, finalMask), "Add apply failed.");
        for (uint32_t x = 3; x <= 27; ++x) Require(Pixel(finalMask, x, 8) == 255, "Stroke interpolation left a gap.");

        Require(editor.Undo(), "Undo failed."); Require(editor.Apply(automatic, finalMask), "Undo apply failed.");
        Require(finalMask.grayscale == automatic.grayscale, "Undo did not restore the full stroke.");
        Require(editor.Redo(), "Redo failed."); Require(editor.Apply(automatic, finalMask), "Redo apply failed.");
        Require(Pixel(finalMask, 15, 8) == 255, "Redo did not restore the Add stroke.");

        brush.mode = MaskBrushMode::Erase; brush.size = 6.0f;
        Require(editor.BeginStroke(15.0f, 8.0f), "Erase stroke did not begin.");
        Require(editor.EndStroke(), "Erase stroke was not committed.");
        Require(editor.Apply(automatic, finalMask), "Erase apply failed.");
        Require(Pixel(finalMask, 15, 8) == 0, "Last Erase operation did not win over Add.");
        Require(editor.Undo(), "Erase undo failed."); Require(editor.Apply(automatic, finalMask), "Erase undo apply failed.");
        Require(Pixel(finalMask, 15, 8) == 255, "Erase undo did not restore Add.");

        MaskData changedAuto = automatic; std::fill(changedAuto.grayscale.begin(), changedAuto.grayscale.end(), 24);
        Require(editor.Apply(changedAuto, finalMask), "Threshold-style Auto mask reapply failed.");
        Require(Pixel(finalMask, 15, 8) == 255 && Pixel(finalMask, 0, 0) == 24,
            "Manual layer was not reapplied over the changed Auto mask.");

        editor.ResetManualEdit(); Require(editor.Apply(changedAuto, finalMask), "Reset apply failed.");
        Require(finalMask.grayscale == changedAuto.grayscale, "Reset Manual Edit did not restore Auto mask exactly.");
        Require(!editor.CanUndo() && !editor.CanRedo() && !editor.HasManualEdit(), "Reset did not clear editor state.");

        MaskData highAuto; highAuto.width = 2000; highAuto.height = 1200;
        highAuto.grayscale.assign(static_cast<size_t>(highAuto.width) * highAuto.height, 128);
        MaskEditor highEditor; highEditor.Initialize(highAuto.width, highAuto.height);
        highEditor.Settings().size = 200.0f; highEditor.Settings().strength = 1.0f;
        const auto strokeStart = std::chrono::steady_clock::now();
        Require(highEditor.BeginStroke(100.0f, 100.0f), "High-resolution stroke did not begin.");
        Require(highEditor.ContinueStroke(1900.0f, 1100.0f), "High-resolution interpolation failed.");
        Require(highEditor.EndStroke(), "High-resolution stroke did not commit.");
        const double strokeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - strokeStart).count();
        const auto applyStart = std::chrono::steady_clock::now();
        Require(highEditor.Apply(highAuto, finalMask), "High-resolution apply failed.");
        const double applyMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - applyStart).count();
        std::cout << "MaskEditor tests passed. highres_stroke_ms=" << strokeMs << " apply_ms=" << applyMs << '\n'; return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n'; return 1;
    }
}
