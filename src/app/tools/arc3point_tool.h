#pragma once

#include <optional>
#include <vector>

#include <geo/infer.h>
#include <geo/shapes.h>
#include <geo/vec3.h>

#include "tool.h"

namespace plnr::tools {

// 3-Point Arc: click start, click a pivot the arc passes through, click the end. Once non-collinear the
// preview switches from the straight fallback to the real arc (geo::arcPoints3Point). Commits an
// open polyline. Degenerate clicks stay armed; Escape resets. Ground-plane only.
class Arc3PointTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

    // VCB commit. Pivot/end stages: Scalar is a silent no-op (typed distance not implemented);
    // Segments/CircleSegments adjust segments_ (treated alike); Radius/Dims2 invalid. Idle with
    // lastCommit_: Segments/CircleSegments regenerate it; idle without: they set segments_ for the next arc.
    void onVcbCommit(ToolContext& ctx, const VcbValue& value) override;

private:
    // Resolves e via geo::infer(), ground-plane only: on-ground hit snaps z to 0, off-ground falls back to the ray's z=0 crossing.
    geo::Inference resolve(ToolContext& ctx, const PointerEvent& e) const;

    // Preview verts: nothing before start_, start->cursor before pivot_, then the real arc or (collinear) the straight fallback.
    std::vector<float> previewVerts(const geo::Vec3& cursor) const;

    // Resets to idle: hint, preview and VCB back to the activation stage.
    void reset(ToolContext& ctx);

    // VCB label is always "Length"; value is the live "~ " distance from the last placed point to the cursor (none before start_).
    void updateVcb(ToolContext& ctx) const;

    static constexpr const char* kActivationHint =
        "Click to place starting point of the arc. Use Ctrl '+' or Ctrl '-' to change the number of segments.";
    static constexpr const char* kPivotHint = "Click to place a point along the arc.";
    static constexpr const char* kThirdHint =
        "Click to place ending point of the arc. Use Ctrl '+' or Ctrl '-' to change the number of segments.";
    static constexpr int kMinSegments = 1;
    static constexpr int kMaxSegments = 999;

    int segments_ = geo::kArcDefaultSegments;
    std::optional<geo::Vec3> start_;
    std::optional<geo::Vec3> pivot_;
    // Last onPointerMove target, so onKeyDown's segment adjustment can redraw the preview (it gets no PointerEvent).
    std::optional<geo::Vec3> lastGround_;

    // VCB retro-edit window: the last arc's three points, regenerated via ctx.requestReplaceLastPolyline; nullopt if none.
    struct LastCommit {
        geo::Vec3 p1;
        geo::Vec3 p2;
        geo::Vec3 p3;
        int segments{};
    };
    std::optional<LastCommit> lastCommit_;
};

}  // namespace plnr::tools
