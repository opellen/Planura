#pragma once

#include <optional>
#include <string>

#include <geo/entity.h>
#include <geo/infer.h>
#include <geo/pick.h>
#include <geo/vec3.h>

#include "agent/events.h"
#include "tool.h"

namespace plnr::tools {

// Tape Measure: Ctrl cycles CreateGuideLines (default) -> CreateGuidePoints -> Measure. GuideLines: click 1 must hit an
// edge; click 2 or a typed VCB Scalar (perpendicular offset) commits a parallel guide. GuidePoints: each click adds a point;
// no VCB. Measure: click 1 starts, live VCB, click 2 freezes (exact, no "~"); a typed Scalar then rescales the model
// (applyRescale: factor = typed / measuredDistance_, uniform about the world origin, every root edge, guides untouched).
// Idle hover on an edge overrides the VCB with its exact length.
class TapeMeasureTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;
    void onVcbCommit(ToolContext& ctx, const VcbValue& value) override;

private:
    enum class Mode { CreateGuideLines, CreateGuidePoints, Measure };
    enum class MeasureStage { Idle, Armed, Frozen };

    // Resolves e against model + guides; anchor only where a mode/stage has one. chargedAnchors/referenceEdge stay unset (no hover-charge or Down-arrow here).
    geo::Inference resolve(ToolContext& ctx, const PointerEvent& e, std::optional<geo::Vec3> anchor) const;

    // Toggles mode_ on a rising edge of ctrlNow, cycling 3 states (cycleMode()).
    void syncCtrl(ToolContext& ctx, bool ctrlNow);
    void cycleMode(ToolContext& ctx);

    // Cancels the in-progress interaction, clears preview/cue, refreshes hint/VCB; keeps mode_. Shared by cycleMode and Escape.
    void resetInteraction(ToolContext& ctx);

    std::string currentHint() const;
    std::string ctrlSuffix() const;

    // If ctx.pick() (plain tolerances) hits an edge, overrides the VCB with its exact length (no "~") and returns true; else callers use their own label.
    bool tryHoverMeasure(ToolContext& ctx, const PointerEvent& e) const;

    // CreateGuideLines
    void moveGuideLines(ToolContext& ctx, const PointerEvent& e);
    void downGuideLines(ToolContext& ctx, const PointerEvent& e);
    // Stable perpendicular-to-dir fallback for a typed exact offset before any pointer move set one (glPerpDir_).
    geo::Vec3 defaultPerpDir(const geo::Vec3& dir) const;

    // CreateGuidePoints
    void moveGuidePoints(ToolContext& ctx, const PointerEvent& e);
    void downGuidePoints(ToolContext& ctx, const PointerEvent& e);

    // Measure
    void moveMeasure(ToolContext& ctx, const PointerEvent& e);
    void downMeasure(ToolContext& ctx, const PointerEvent& e);
    // Handles a typed Scalar while Frozen (class comment's Rescale).
    void applyRescale(ToolContext& ctx, double typedValue);

    Mode mode_ = Mode::CreateGuideLines;
    bool ctrlHeld_ = false;  // last-seen PointerEvent::ctrl, for syncCtrl's edge detection

    // CreateGuideLines state.
    bool glArmed_ = false;
    geo::Vec3 glClickPoint_;  // the arming click's own hit point, on the source edge
    geo::Vec3 glEdgeDir_;     // the source edge's unit direction
    // Perpendicular-to-glEdgeDir_ direction last derived from the cursor's side; fixes the sign of a typed offset.
    std::optional<geo::Vec3> glPerpDir_;
    std::optional<geo::Vec3> glLastThrough_;  // last resolved inference point -- what click 2 commits
    double glLastDistance_ = 0.0;             // last perpendicular offset magnitude, for the live VCB readout

    // Measure state.
    MeasureStage measureStage_ = MeasureStage::Idle;
    geo::Vec3 measureStart_;
    std::optional<geo::Vec3> measureLast_;  // last resolved inference point, Armed only
    double measuredDistance_ = 0.0;         // frozen at click 2 -- what applyRescale divides by
};

}  // namespace plnr::tools
