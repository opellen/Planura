#pragma once

#include <optional>
#include <vector>

#include <geo/infer.h>
#include <geo/shapes.h>
#include <geo/vec3.h>

#include "tool.h"

namespace plnr::tools {

// industry-standard 3-Point Arc: click for the start point, click again for a
// "pivot" point the arc passes through (rubber-band start->pivot->cursor
// meanwhile), then a third click places the end -- once non-collinear,
// geo::arcPoints3Point resolves a unique circle and the preview switches
// from the straight fallback to the real arc. Commits via requestAddPolyline
// (open). Degenerate clicks stay armed; Escape resets. Ground-plane-only
class Arc3PointTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

    // Handles a committed VCB entry. Pivot/end stages: Scalar is a silent
    // no-op (typed-distance extend is deferred past this MVP);
    // Segments/CircleSegments adjust segments_ (treated identically -- no
    // sweep conversion); Radius/Dims2 invalid. Idle with lastCommit_:
    // Segments/CircleSegments regenerate via requestReplaceLastPolyline.
    // Idle with none: Segments/CircleSegments still adjust segments_ for the NEXT arc.
    void onVcbCommit(ToolContext& ctx, const VcbValue& value) override;

private:
    // Resolves e via geo::infer(), then enforces the ground-plane-only
    // contract: an on-ground hit snaps z to 0.0; an off-ground hit falls
    // back to the click ray's z=0 crossing. Same as Circle/Polygon's resolve().
    geo::Inference resolve(ToolContext& ctx, const PointerEvent& e) const;

    // The current stage's preview line verts for cursor: nothing before
    // start_, a plain start->cursor segment before pivot_, then the real arc
    // or (while collinear/degenerate) the straight fallback path.
    std::vector<float> previewVerts(const geo::Vec3& cursor) const;

    // Resets to the idle state (no start point placed) with hint, preview,
    // and the VCB all reset to the activation stage.
    void reset(ToolContext& ctx);

    // Sets the VCB for the current stage -- label is always "Length"; value
    // is the live "~ "-prefixed distance from the most recently placed point
    // to the cursor. No value before start_ is placed.
    void updateVcb(ToolContext& ctx) const;

    static constexpr const char* kActivationHint =
        "Click to place starting point of the arc. Use Ctrl '+' or Ctrl '-' to change the number of segments.";
    static constexpr const char* kPivotHint = "Click to place a point along the arc.";
    static constexpr const char* kThirdHint =
        "Click to place ending point of the arc. Use Ctrl '+' or Ctrl '-' to change the number of segments.";
    static constexpr int kMinSegments = 1;
    static constexpr int kMaxSegments = 999;

    int segments_ = geo::kArcDefaultSegments;
    std::optional<geo::Vec3> start_;
    std::optional<geo::Vec3> pivot_;
    // Most recent onPointerMove target, kept so onKeyDown's segment-count
    // adjustment can redraw the preview at the cursor's last known ground
    // position without a fresh PointerEvent (onKeyDown gets none).
    std::optional<geo::Vec3> lastGround_;

    // The VCB retro-edit window: the three points of the most recently
    // committed arc, so a follow-up typed segment-count can regenerate it
    // via ctx.requestReplaceLastPolyline. nullopt if nothing to retro-edit.
    struct LastCommit {
        geo::Vec3 p1;
        geo::Vec3 p2;
        geo::Vec3 p3;
        int segments{};
    };
    std::optional<LastCommit> lastCommit_;
};

}  // namespace plnr::tools
