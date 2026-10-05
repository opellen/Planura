#pragma once

#include <optional>
#include <utility>
#include <vector>

#include <QElapsedTimer>

#include <geo/infer.h>
#include <geo/vec3.h>

#include "tool.h"

namespace plnr::tools {

// Line: click sets the start, move previews a rubber-band edge with live inference, click commits and
// continues the chain; clicking the chain's own start closes it. Escape cancels the anchor, staying in the
// tool. Full inference consumer: hover-charged anchors, Down-arrow reference edge, arrow axis lock, Alt filter.
class LineTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

    // VCB commit (single edge, no retro-edit window). Needs anchor_ + lastGround_: Scalar commits at that length along anchor->cursor, continuing the chain. Else "Invalid entry.".
    void onVcbCommit(ToolContext& ctx, const VcbValue& value) override;

private:
    // Alt linear-inference filter (mid-draw), 3-state cycle; filterInference() says what each state suppresses.
    enum class LinearInferenceMode { AllOn, AllOff, ParallelPerpendicularOnly };

    // Resolves e via every InferenceContext field this tool owns, then filterInference(). Shift (anchored, no axisLock_) locks the closest world axis.
    geo::Inference resolve(ToolContext& ctx, const PointerEvent& e) const;

    // Applies the Alt filter: AllOn passes through; AllOff/ParallelPerpendicularOnly suppress to InferenceKind::None.
    geo::Inference filterInference(geo::Inference inf) const;

    // Hover-charge ("From Point"): tracks whether the current point inference stays resolved for >= kHoverChargeMs.
    void updateHoverCharge(const geo::Inference& inf);
    void addChargedAnchor(const geo::Vec3& pos);

    // Up/Right/Left arrows (Up=Blue, Right=Red, Left=Green); same arrow unlocks, a different one switches. Reads ctx.axesFrame().
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

    // Hover-charge timing; only advances on an actual onPointerMove. kHoverChargeMs is UNVERIFIED.
    std::optional<geo::Vec3> hoverPoint_;
    geo::InferenceKind hoverKind_ = geo::InferenceKind::None;
    QElapsedTimer hoverTimer_;
    bool hoverCharged_ = false;

    // Arrow-key axis lock, independent of Shift's transient lock; persists until toggled off or switched.
    std::optional<geo::AxisLock> axisLock_;
    int lockedArrowKey_ = 0;  // 0 = none; else the Qt::Key_Up/Right/Left currently locked

    // Last pointer ray/tolerances, so onKeyDown's Down-arrow pick can act without its own PointerEvent.
    std::optional<geo::Ray> lastRay_;
    geo::PickOptions lastTols_{};

    LinearInferenceMode linearMode_ = LinearInferenceMode::AllOn;
};

}  // namespace plnr::tools
