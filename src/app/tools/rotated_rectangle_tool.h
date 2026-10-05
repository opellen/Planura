#pragma once

#include <optional>
#include <vector>

#include <geo/infer.h>
#include <geo/vec3.h>

#include "tool.h"

namespace plnr::tools {

// Rotated Rectangle: 3 clicks -- A, B (baseline AB at any ground direction), then a width stage giving corners
// A, B, B+w, A+w from the perpendicular offset of (cursor - B); commits closed via ctx.requestAddPolyline.
// Degenerate clicks are ignored (stay armed); Escape resets. Ground-plane only.
class RotatedRectangleTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

private:
    // Resolves e via geo::infer(), ground-plane only: on-ground hit snaps z to 0, off-ground falls back to the ray's z=0 crossing.
    geo::Inference resolve(ToolContext& ctx, const PointerEvent& e) const;

    // 4-corner points (a, b, b+w, a+w) for baseline a->b, the cursor's perpendicular offset giving w. Empty when |a-b| or the offset is below merge tolerance.
    std::vector<geo::Vec3> pointsFor(const geo::Vec3& a, const geo::Vec3& b, const geo::Vec3& cursor) const;

    // Resets to idle: no corner placed, hint and preview reset.
    void reset(ToolContext& ctx);

    static constexpr const char* kActivationHint = "Click to place the first corner of the rectangle.";
    static constexpr const char* kBaselineHint = "Click to set the baseline edge.";
    static constexpr const char* kWidthHint = "Move to set the width, click to finish.";

    std::optional<geo::Vec3> a_;
    std::optional<geo::Vec3> b_;
};

}  // namespace plnr::tools
