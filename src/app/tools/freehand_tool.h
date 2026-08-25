#pragma once

#include <vector>

#include <QPointF>

#include <geo/infer.h>
#include <geo/vec3.h>

#include "tool.h"

namespace plnr::tools {

// industry-standard Freehand: press-and-drag draws a curve entity --
// onPointerDown starts a stroke, onPointerMove appends the ground-projected
// point once its SCREEN-space distance from the last recorded point exceeds
// kThinningPx (pixel-space keeps thinning density constant across zoom),
// previewed live; onPointerUp commits (requestAddPolyline, open) once >= 2
// points. Escape cancels. Ground-plane-only
class FreehandTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerUp(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

private:
    // Resolves e via geo::infer(), then enforces the ground-plane-only
    // contract: an on-ground hit snaps z to 0.0; an off-ground hit falls
    // back to the click ray's z=0 crossing. Same as Circle/Arc2Point's resolve().
    geo::Inference resolve(ToolContext& ctx, const PointerEvent& e) const;

    // Resets to the idle state (no stroke in progress) with hint and preview
    // both cleared/reset to the activation prompt.
    void reset(ToolContext& ctx);

    static constexpr const char* kActivationHint = "Click and drag to draw a freehand curve.";
    static constexpr const char* kDrawingHint = "Drag to draw the curve. Release to finish.";
    // Screen-space thinning distance (widget pixels) -- see the class
    // comment for why this is pixel- rather than world-space.
    static constexpr double kThinningPx = 4.0;

    bool drawing_ = false;
    std::vector<geo::Vec3> points_;
    // Screen position of the last recorded point, so onPointerMove can
    // measure the next candidate's pixel distance from it.
    QPointF lastScreen_;
};

}  // namespace plnr::tools
