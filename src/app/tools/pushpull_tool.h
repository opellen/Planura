#pragma once

#include <optional>
#include <vector>

#include <geo/model.h>
#include <geo/pick.h>
#include <geo/vec3.h>

#include "tool.h"

namespace plnr::tools {

// Push/Pull: click-move-click or press-drag-release (release commits once past the drag threshold). Preview is
// the pointer's signed distance along the face normal (positive = pull out, negative = push in); commits via
// ctx.requestExtrudeFace. Escape cancels the arm.
class PushPullTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerUp(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

    // VCB commit (drag only; no retro-edit window). Needs an armed face: Scalar extrudes by |value| with the drag's sign (0 = positive). Else "Invalid entry.".
    void onVcbCommit(ToolContext& ctx, const VcbValue& value) override;

private:
    // Armed face: id, grab point (extrusion-axis origin), unit normal at grab time (the axis), vertex loop snapshot.
    struct Arm {
        geo::Id faceId{};
        geo::Vec3 anchor;
        geo::Vec3 axis;
        std::vector<geo::Vec3> loopBase;
        // Screen position of the arming press; onPointerUp measures travel against it (drag-release vs first click).
        QPointF pressScreen;
    };

    // Resets to idle: no armed face, hint/preview/hover-face cleared.
    void reset(ToolContext& ctx);

    // While idle, highlights the face under the cursor with the reference modeler's blue dotted pattern.
    void updateHoverFace(ToolContext& ctx, const PointerEvent& e) const;

    std::optional<Arm> arm_;
    double lastDistance_ = 0.0;
};

}  // namespace plnr::tools
