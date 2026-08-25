#pragma once

#include <optional>
#include <vector>

#include <geo/model.h>
#include <geo/pick.h>
#include <geo/vec3.h>

#include "tool.h"

namespace plnr::tools {

// industry-standard push/pull: accepts both click-move-click (click a face to
// arm, move to preview, click again to commit) and press-drag-release
// (release commits once past the drag threshold).

// Preview is the pointer's signed distance along the face's normal
// (positive = outward pull, negative = inward push); commits via
// ctx.requestExtrudeFace. Escape cancels the in-progress arm.
class PushPullTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerUp(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

    // Handles a committed VCB entry (drag only -- a single extrude has no
    // retro-edit window). Requires an armed face; Scalar extrudes by |value|
    // with the current drag's sign (0 treated as positive). Anything else
    // is "Invalid entry.".
    void onVcbCommit(ToolContext& ctx, const VcbValue& value) override;

private:
    // An armed face: id, the grab point (extrusion-axis origin), the face's
    // unit normal at grab time (the axis), and its vertex loop snapshot.
    struct Arm {
        geo::Id faceId{};
        geo::Vec3 anchor;
        geo::Vec3 axis;
        std::vector<geo::Vec3> loopBase;
        // Screen position of the arming press; onPointerUp measures travel
        // against this to tell a drag-release (commit) from a first click.
        QPointF pressScreen;
    };

    // Resets to the idle state (no armed face) with hint, preview, and
    // hover-face highlight all cleared.
    void reset(ToolContext& ctx);

    // While idle, highlights the face under the cursor with the reference modeler's
    // blue dotted pattern.
    void updateHoverFace(ToolContext& ctx, const PointerEvent& e) const;

    std::optional<Arm> arm_;
    double lastDistance_ = 0.0;
};

}  // namespace plnr::tools
