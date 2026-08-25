#pragma once

#include <optional>
#include <vector>

#include <geo/infer.h>
#include <geo/vec3.h>

#include "tool.h"

namespace plnr::tools {

// industry-standard Rotated Rectangle: 3 clicks -- A (first corner), B (second,
// fixing baseline AB at any ground direction), then a width stage setting
// corners A, B, B+w, A+w from the perpendicular offset of (cursor - B),
// committed via ctx.requestAddPolyline(closed=true). AB need not run along
// red/green, so the result can sit at any rotation. Degenerate clicks are
// ignored (tool stays armed); Escape resets. Ground-plane-only
class RotatedRectangleTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

private:
    // Resolves e via geo::infer(), then enforces the ground-plane-only
    // contract: an on-ground hit snaps z to 0.0; an off-ground hit falls
    // back to the click ray's z=0 crossing. Same as Circle/Arc2Point's resolve().
    geo::Inference resolve(ToolContext& ctx, const PointerEvent& e) const;

    // The 4-corner point list (a, b, b+w, a+w) for baseline a->b with cursor
    // setting the perpendicular width w (see the class comment for its
    // formula). Empty when |a-b| or the perpendicular offset is below merge
    // tolerance.
    std::vector<geo::Vec3> pointsFor(const geo::Vec3& a, const geo::Vec3& b, const geo::Vec3& cursor) const;

    // Resets to the idle state (no corner placed) with hint and preview both
    // cleared/reset to the activation prompt.
    void reset(ToolContext& ctx);

    static constexpr const char* kActivationHint = "Click to place the first corner of the rectangle.";
    static constexpr const char* kBaselineHint = "Click to set the baseline edge.";
    static constexpr const char* kWidthHint = "Move to set the width, click to finish.";

    std::optional<geo::Vec3> a_;
    std::optional<geo::Vec3> b_;
};

}  // namespace plnr::tools
