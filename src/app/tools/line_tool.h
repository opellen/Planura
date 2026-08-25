#pragma once

#include <optional>
#include <utility>
#include <vector>

#include <QElapsedTimer>

#include <geo/infer.h>
#include <geo/vec3.h>

#include "tool.h"

namespace plnr::tools {

// industry-standard pencil: click to set the start point, move to preview a
// rubber-band edge with live inference, click again to commit and continue
// the chain from the new endpoint; clicking back onto the chain's own start
// closes it. Escape cancels the in-progress anchor without leaving the tool.
// Full inference-engine consumer (hover-charged anchors, Down-arrow reference
// edge, arrow-key axis lock, Alt linear-inference filter)
class LineTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

    // Handles a committed VCB entry (single edge, not a polyline -- no
    // retro-edit window). Requires anchor_ + lastGround_: Scalar commits at
    // that length along anchor->cursor, continuing the chain. Else "Invalid entry.".
    void onVcbCommit(ToolContext& ctx, const VcbValue& value) override;

private:
    // Linear-inference filter mode (Alt key, mid-draw): verified 3-state
    // cycle. See filterInference() for exactly which kinds each state suppresses.
    enum class LinearInferenceMode { AllOn, AllOff, ParallelPerpendicularOnly };

    // Resolves e via every InferenceContext field this tool owns, then
    // applies filterInference(). Shift (anchored, no axisLock_) locks onto the closest world axis.
    geo::Inference resolve(ToolContext& ctx, const PointerEvent& e) const;

    // Applies the Alt-cycle's filter to an already-resolved Inference:
    // AllOn passes through; AllOff/ParallelPerpendicularOnly suppress to InferenceKind::None.
    geo::Inference filterInference(geo::Inference inf) const;

    // Hover-charge bookkeeping ("From Point"): tracks whether the CURRENT
    // point-inference stays resolved across >= kHoverChargeMs of moves.
    void updateHoverCharge(const geo::Inference& inf);
    void addChargedAnchor(const geo::Vec3& pos);

    // Up/Right/Left arrow handling (Up=Blue, Right=Red, Left=Green; same
    // arrow unlocks, a different one switches). Reads ctx.axesFrame().
    void handleArrowLock(ToolContext& ctx, int key);
    // Down arrow: edge-under-cursor -> referenceEdge_ on; none -> warning; Down again clears.
    void handleDownArrow(ToolContext& ctx);
    void showConstraintWarning(ToolContext& ctx) const;

    // Alt key: cycles linearMode_ All On -> All Off -> Parallel/Perpendicular Only -> All On.
    void cycleLinearInferenceMode(ToolContext& ctx);

    // Builds the verbatim status hint for the current stage + linearMode_.
    std::string currentHint() const;

    std::optional<geo::Vec3> anchor_;

    // Most recent onPointerMove target, so onVcbCommit's exact-length commit
    // knows which direction to extend along.
    std::optional<geo::Vec3> lastGround_;

    // Full InferenceContext data this tool owns (see infer.h's contract).
    std::vector<geo::Vec3> chargedAnchors_;                          // FIFO, cap kMaxChargedAnchors
    std::optional<std::pair<geo::Vec3, geo::Vec3>> referenceEdge_;   // Down-arrow

    // Hover-charge timing state -- see updateHoverCharge(). MVP: only
    // advances on an actual onPointerMove; kHoverChargeMs is UNVERIFIED.
    std::optional<geo::Vec3> hoverPoint_;
    geo::InferenceKind hoverKind_ = geo::InferenceKind::None;
    QElapsedTimer hoverTimer_;
    bool hoverCharged_ = false;

    // Explicit arrow-key axis lock (independent of Shift's transient lock)
    // -- persists until toggled off or switched to a different arrow.
    std::optional<geo::AxisLock> axisLock_;
    int lockedArrowKey_ = 0;  // 0 = none; else the Qt::Key_Up/Right/Left currently locked

    // Last pointer ray/tolerances, so onKeyDown's Down-arrow pick can act without its own PointerEvent.
    std::optional<geo::Ray> lastRay_;
    geo::PickOptions lastTols_{};

    LinearInferenceMode linearMode_ = LinearInferenceMode::AllOn;
};

}  // namespace plnr::tools
