#pragma once

#include <vector>

#include <QPointF>

#include <geo/infer.h>
#include <geo/vec3.h>

#include "tool.h"

namespace plnr::tools {

// Freehand: press-drag draws a curve. onPointerMove appends the ground-projected point once its SCREEN-space
// distance from the last one exceeds kThinningPx (pixels keep thinning density constant across zoom);
// onPointerUp commits an open polyline with >= 2 points. Escape cancels. Ground-plane only.
class FreehandTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerUp(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

private:
    // Resolves e via geo::infer(), ground-plane only: on-ground hit snaps z to 0, off-ground falls back to the ray's z=0 crossing.
    geo::Inference resolve(ToolContext& ctx, const PointerEvent& e) const;

    // Resets to idle: no stroke, hint and preview reset.
    void reset(ToolContext& ctx);

    static constexpr const char* kActivationHint = "Click and drag to draw a freehand curve.";
    static constexpr const char* kDrawingHint = "Drag to draw the curve. Release to finish.";
    // Screen-space thinning distance in widget pixels.
    static constexpr double kThinningPx = 4.0;

    bool drawing_ = false;
    std::vector<geo::Vec3> points_;
    // Screen position of the last recorded point, for onPointerMove's pixel-distance test.
    QPointF lastScreen_;
};

}  // namespace plnr::tools
