#pragma once

#include <string>

#include <geo/infer.h>
#include <geo/vec3.h>

#include "tool.h"

namespace plnr::tools {

// Axes tool: relocates/reorients the drawing-axes frame the grid/triad, inference and Line arrow-key
// locks resolve against. Idle: click sets the origin (double-click relocates origin only). OriginSet:
// dotted ray colored by activeAxis_ (Alt cycles); the label is cosmetic, the next click is always the
// PRIMARY dir (xDir). FirstAxisSet: cursor projected perpendicular to dir1_; click commits
// requestSetAxes. Escape resets. Right-click "Reset" is not wired (requestResetAxes() exists).
class AxesTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

    // Role OriginSet labels the first-axis click with (cosmetic); Alt cycles Red->Blue->Green.
    // Public so .cpp-local helpers can name it.
    enum class ActiveAxis { Red, Blue, Green };

private:
    enum class Stage { Idle, OriginSet, FirstAxisSet };

    // Resolves e against model + guides (endpoint/midpoint ladder), no anchor; the perpendicular constraint is local.
    geo::Inference resolve(ToolContext& ctx, const PointerEvent& e) const;

    void cycleActiveAxis();
    void resetToIdle(ToolContext& ctx);
    std::string currentHint() const;

    Stage stage_ = Stage::Idle;
    geo::Vec3 origin_;
    geo::Vec3 dir1_;
    ActiveAxis activeAxis_ = ActiveAxis::Red;
};

}  // namespace plnr::tools
