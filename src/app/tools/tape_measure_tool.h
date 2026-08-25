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

// industry-standard Tape Measure: Ctrl cycles CreateGuideLines (default) ->
// CreateGuidePoints -> Measure.

// CreateGuideLines: click 1 must land on an edge (edge-tier-only pick); a
// miss re-shows the hint. Once armed, previews a dashed line through the
// inference point, parallel to the edge. Click 2 (or a typed VCB Scalar
// perpendicular offset) commits requestAddGuideLine.

// CreateGuidePoints: no arm/drag stage -- every click independently adds a
// guide point. No VCB grammar is defined.

// Measure: click 1 sets the start point; every move previews a rubber-band
// line + live VCB distance; click 2 freezes it (exact value, no "~").
// A further click while frozen re-arms a new measurement; a typed Scalar
// while frozen rescales the model (see applyRescale).

// Hover-measure: in each mode's idle sub-state, hovering an edge overrides
// the VCB with that edge's own exact length.

// Rescale: factor = typed / measuredDistance_; on confirm, uniformly
// scales every root edge (not a selection) about the world origin. Guides
// live in GuideStore and are untouched by the transform.

// No per-tool cursor-shape API exists -- the mode is reflected in the hint only.
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

    // Resolves e against the model + guides, with anchor set only where a
    // mode/stage has one. chargedAnchors/referenceEdge stay unset -- no
    // hover-charging or Down-arrow reference-edge step in this tool.
    geo::Inference resolve(ToolContext& ctx, const PointerEvent& e, std::optional<geo::Vec3> anchor) const;

    // Toggles mode_ on a rising edge of ctrlNow, cycling through 3 states
    // (see cycleMode()).
    void syncCtrl(ToolContext& ctx, bool ctrlNow);
    void cycleMode(ToolContext& ctx);

    // Cancels whatever interaction is in progress, clears preview/cue, and
    // refreshes hint/VCB -- without touching mode_. Shared by cycleMode
    // (switching modes cancels the prior interaction) and Escape.
    void resetInteraction(ToolContext& ctx);

    std::string currentHint() const;
    std::string ctrlSuffix() const;

    // If ctx.pick() (plain tolerances) lands on an edge, overrides the VCB
    // to that edge's exact length (no "~") and returns true. Callers fall
    // back to their own VCB label when false.
    bool tryHoverMeasure(ToolContext& ctx, const PointerEvent& e) const;

    // CreateGuideLines
    void moveGuideLines(ToolContext& ctx, const PointerEvent& e);
    void downGuideLines(ToolContext& ctx, const PointerEvent& e);
    // A stable perpendicular-to-dir fallback for a typed exact-offset VCB
    // entry before any pointer move established one (glPerpDir_).
    geo::Vec3 defaultPerpDir(const geo::Vec3& dir) const;

    // CreateGuidePoints
    void moveGuidePoints(ToolContext& ctx, const PointerEvent& e);
    void downGuidePoints(ToolContext& ctx, const PointerEvent& e);

    // Measure
    void moveMeasure(ToolContext& ctx, const PointerEvent& e);
    void downMeasure(ToolContext& ctx, const PointerEvent& e);
    // Handles a typed Scalar while Frozen (see the class comment's Rescale note).
    void applyRescale(ToolContext& ctx, double typedValue);

    Mode mode_ = Mode::CreateGuideLines;
    bool ctrlHeld_ = false;  // last-seen PointerEvent::ctrl, for syncCtrl's edge detection

    // CreateGuideLines state.
    bool glArmed_ = false;
    geo::Vec3 glClickPoint_;  // the arming click's own hit point, on the source edge
    geo::Vec3 glEdgeDir_;     // the source edge's unit direction
    // Perpendicular-to-glEdgeDir_ direction most recently derived from the
    // cursor's side, for a typed exact-offset VCB entry's sign convention.
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
