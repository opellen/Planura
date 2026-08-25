#pragma once

#include <string>

#include <geo/infer.h>
#include <geo/vec3.h>

#include "tool.h"

namespace plnr::tools {

// industry-standard Axes tool: relocates/reorients the model's drawing-axes
// frame that the viewport grid/triad, the inference engine, and the Line
// tool's arrow-key locks resolve against instead of hardcoded world X/Y/Z.

// Idle: a click resolves the inference point and moves to OriginSet; a
// double-click instead relocates the origin only, keeping orientation.

// OriginSet: previews a dotted ray from origin_ toward the cursor, colored
// by activeAxis_ (Alt cycles Red/Blue/Green). The label is cosmetic only --
// the click that ends this stage always becomes the frame's PRIMARY
// direction (xDir), regardless of which role was labeled.

// FirstAxisSet: projects the cursor perpendicular to dir1_ (projectPerp)
// and previews it uncolored. A click commits requestSetAxes(origin_,
// dir1_, perpDir); AxesStore re-orthonormalizes, then returns to Idle.

// Escape resets to Idle from any stage. The right-click "Reset" menu entry
// the reference modeler has isn't wired in yet; requestResetAxes()/
// ResetAxesRequested already exist for when it lands.
class AxesTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

    // Which role OriginSet's preview/hint labels the first-axis click with
    // -- cosmetic only, see the class comment. Alt cycles Red->Blue->Green.
    // Public so .cpp-local helpers can name it without becoming member functions.
    enum class ActiveAxis { Red, Blue, Green };

private:
    enum class Stage { Idle, OriginSet, FirstAxisSet };

    // Resolves e against the model + guides (endpoint/midpoint ladder), no
    // anchor -- FirstAxisSet's perpendicular constraint is computed locally.
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
