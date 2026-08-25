#pragma once

#include <optional>
#include <vector>

#include <geo/infer.h>
#include <geo/shapes.h>
#include <geo/vec3.h>

#include "tool.h"

namespace plnr::tools {

// industry-standard polygon: click to set the center, move to preview a
// regular polygon (6 sides default) rubber-banding radius (|cursor-center|)
// and rotation (vertex 0 along center->cursor) -- click again to commit
// (requestAddPolyline, closed). Degenerate click stays armed; Escape
// cancels. Ctrl+/- adjusts side count. VCB: "Sides" pre-center, "Radius"
// while rubber-banding
class PolygonTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

    // Handles a committed VCB entry -- identical apply table to
    // CircleTool::onVcbCommit (see its own comment), just with Polygon's
    // own 3-100 segment clamp.
    void onVcbCommit(ToolContext& ctx, const VcbValue& value) override;

private:
    // Resolves e via geo::infer(), then enforces the ground-plane-only
    // contract: an on-ground hit snaps z to 0.0; an off-ground hit falls
    // back to the click ray's z=0 crossing. Same as RectangleTool's resolve().
    geo::Inference resolve(ToolContext& ctx, const PointerEvent& e) const;

    // The single geo::regularPolygonPoints call site every commit/preview/
    // retro-regenerate path funnels through -- dir need not be normalized.
    // Empty when regularPolygonPoints' own guards reject it (e.g. radius <= 0).
    std::vector<geo::Vec3> pointsForRadius(const geo::Vec3& center, double radius, const geo::Vec3& dir,
                                            int segments) const;

    // The regular-polygon point list for a polygon at center whose cursor
    // defines both radius and rotation (vertex 0 along cursor - center).
    // Empty when pointsForRadius rejects it (e.g. cursor == center).
    std::vector<geo::Vec3> pointsFor(const geo::Vec3& center, const geo::Vec3& cursor) const;

    // Resets to idle (hint/preview/VCB reset) -- deliberately does NOT
    // touch lastCommit_, since this also runs right after a successful
    // commit where the retro-edit window must stay open.
    void reset(ToolContext& ctx);

    // Sets the VCB for the current stage -- "Sides" (segment count) before
    // center_ is placed, "Radius" ("~ " + live radius) once it is. Shared by
    // reset/onPointerMove/onKeyDown so all three stay consistent.
    void updateVcb(ToolContext& ctx) const;

    static constexpr const char* kActivationHint =
        "Select center point. Use Ctrl '+' or Ctrl '-' to change the number of segments.";
    static constexpr int kMinSegments = 3;
    static constexpr int kMaxSegments = 100;

    int segments_ = 6;
    std::optional<geo::Vec3> center_;
    // Most recent onPointerMove target, kept so onKeyDown's segment-count
    // adjustment can redraw the preview at the cursor's last known ground
    // position without a fresh PointerEvent (onKeyDown gets none).
    std::optional<geo::Vec3> lastGround_;

    // The VCB retro-edit window -- see CircleTool::LastCommit's identical
    // comment.
    struct LastCommit {
        geo::Vec3 center;
        geo::Vec3 dir;  // unnormalized cursor - center at commit time (vertex-0 placement)
        double radius{};
        int segments{};
    };
    std::optional<LastCommit> lastCommit_;
};

}  // namespace plnr::tools
