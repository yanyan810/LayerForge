#include "Editor/MaskEditor.h"
#include "Editor/SmartMaskCorrection.h"

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

        SmartMaskCorrection smart;
        SmartStrokeRequest shortRequest;
        Require(smart.BeginStroke(8.0f, 8.0f, 20.0f, SmartCorrectionMode::Add, 100, 80), "Smart Add did not begin.");
        Require(smart.EndStroke(shortRequest), "Smart Add did not create a request.");
        Require(shortRequest.prompts.size() == 1, "A short Smart stroke must create one prompt.");
        Require(shortRequest.roi.IsValid() && shortRequest.roi.x1 == 0 && shortRequest.roi.y1 == 0,
            "Smart ROI was not clipped to the image.");

        SmartStrokeRequest longRequest;
        Require(smart.BeginStroke(10.0f, 40.0f, 12.0f, SmartCorrectionMode::Erase, 160, 80), "Smart Erase did not begin.");
        Require(smart.ContinueStroke(80.0f, 40.0f) && smart.ContinueStroke(150.0f, 40.0f), "Smart stroke sampling input failed.");
        Require(smart.EndStroke(longRequest), "Long Smart stroke did not create a request.");
        Require(longRequest.prompts.size() == 3, "A long Smart stroke must create three sparse prompts.");
        Require(longRequest.mode == SmartCorrectionMode::Erase && longRequest.roi.x2 == 160,
            "Smart request did not preserve mode or clipped ROI.");

        MaskData current; current.width = 8; current.height = 6; current.grayscale.assign(48, 100);
        MaskData samMask = current; std::fill(samMask.grayscale.begin(), samMask.grayscale.end(), 220);
        const SmartCorrectionRoi roi{ 2, 1, 6, 5 };
        MaskData candidate;
        Require(SmartMaskCorrection::BuildCandidate(current, samMask, roi, SmartCorrectionMode::Add, candidate),
            "Smart Add candidate failed.");
        Require(Pixel(candidate, 3, 2) == 220 && Pixel(candidate, 0, 0) == 100,
            "Smart Add changed the wrong region or failed to preserve ROI exterior.");
        for (size_t index = 0; index < candidate.grayscale.size(); ++index)
            Require(candidate.grayscale[index] >= current.grayscale[index], "Smart Add violated the increase-only rule.");
        std::fill(samMask.grayscale.begin(), samMask.grayscale.end(), 30);
        Require(SmartMaskCorrection::BuildCandidate(current, samMask, roi, SmartCorrectionMode::Erase, candidate),
            "Smart Erase candidate failed.");
        Require(Pixel(candidate, 3, 2) == 30 && Pixel(candidate, 0, 0) == 100,
            "Smart Erase changed the wrong region or failed to preserve ROI exterior.");
        for (size_t index = 0; index < candidate.grayscale.size(); ++index)
            Require(candidate.grayscale[index] <= current.grayscale[index], "Smart Erase violated the decrease-only rule.");

        MaskEditor smartHistory; smartHistory.Initialize(current.width, current.height);
        MaskData smartFinal = current; smartFinal.grayscale[10] = 230; smartFinal.grayscale[20] = 15;
        Require(smartHistory.CommitFinal(current, smartFinal), "Smart Apply could not be committed.");
        Require(smartHistory.Apply(current, candidate), "Committed Smart mask could not be applied.");
        Require(Pixel(candidate, 2, 1) == 231 && Pixel(candidate, 4, 2) == 14,
            "Smart Apply was not represented by the manual layer.");
        Require(smartHistory.Undo() && smartHistory.Apply(current, candidate) && candidate.grayscale == current.grayscale,
            "Smart Apply was not stored as one Undo operation.");
        Require(smartHistory.Redo() && smartHistory.Apply(current, candidate), "Smart Apply Redo failed.");
        MaskData thresholdAuto = current; std::fill(thresholdAuto.grayscale.begin(), thresholdAuto.grayscale.end(), 50);
        Require(smartHistory.Apply(thresholdAuto, candidate) && Pixel(candidate, 2, 1) == 231 && Pixel(candidate, 4, 2) == 14,
            "Smart correction did not survive threshold-style Auto mask changes.");

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
        std::cout << "MaskEditor + SmartCorrection tests passed. highres_stroke_ms=" << strokeMs << " apply_ms=" << applyMs << '\n'; return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n'; return 1;
    }
}
