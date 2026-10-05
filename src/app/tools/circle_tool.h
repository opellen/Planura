#pragma once

#include <optional>
#include <vector>

#include <geo/infer.h>
#include <geo/shapes.h>
#include <geo/vec3.h>

#include "tool.h"

namespace plnr::tools {

// Circle: click the center, then a 24-segment regular polygon rubber-bands radius (|cursor-center|) and
// rotation (vertex 0 along center->cursor); click commits a closed polyline. Degenerate click stays
// armed; Escape cancels; Ctrl+/- adjusts segments. VCB: "Sides" pre-center, "Radius" after.
class CircleTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

    // VCB commit. Idle, no lastCommit_: Scalar/Segments/CircleSegments set segments_ for the next circle.
    // Rubber-band: Scalar/Radius commits NOW at that radius; Segments/CircleSegments adjust segments_.
    // Idle with lastCommit_: all four regenerate the last circle. Dims2 is "Invalid entry.".
    void onVcbCommit(ToolContext& ctx, const VcbValue& value) override;

private:
    // Resolves e via geo::infer(), ground-plane only: on-ground hit snaps z to 0, off-ground falls back to the ray's z=0 crossing.
    geo::Inference resolve(ToolContext& ctx, const PointerEvent& e) const;

    // The single geo::regularPolygonPoints call site (commit/preview/retro-edit); dir need not be normalized. Empty if rejected (e.g. radius <= 0).
    std::vector<geo::Vec3> pointsForRadius(const geo::Vec3& center, double radius, const geo::Vec3& dir,
                                            int segments) const;

    // Regular-polygon points for a circle at center; cursor sets radius and rotation. Empty when pointsForRadius rejects it (cursor == center).
    std::vector<geo::Vec3> pointsFor(const geo::Vec3& center, const geo::Vec3& cursor) const;

    // Resets to idle; does NOT touch lastCommit_ (it also runs right after a commit, keeping the retro-edit window open).
    void reset(ToolContext& ctx);

    // Sets the VCB: "Sides" before center_ is placed, "Radius" ("~ " + live radius) after.
    void updateVcb(ToolContext& ctx) const;

    static constexpr const char* kActivationHint =
        "Select center point. Use Ctrl '+' or Ctrl '-' to change the number of segments.";
    static constexpr int kMinSegments = 3;
    static constexpr int kMaxSegments = 999;

    int segments_ = 24;
    std::optional<geo::Vec3> center_;
    // Last onPointerMove target, so onKeyDown's segment adjustment can redraw the preview (it gets no PointerEvent).
    std::optional<geo::Vec3> lastGround_;

    // VCB retro-edit window: params of the last committed circle; nullopt if none.
    struct LastCommit {
        geo::Vec3 center;
        geo::Vec3 dir;  // unnormalized cursor - center at commit time (vertex-0 placement)
        double radius{};
        int segments{};
    };
    std::optional<LastCommit> lastCommit_;
};

}  // namespace plnr::tools
