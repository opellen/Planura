#pragma once

#include <optional>
#include <vector>

#include <geo/infer.h>
#include <geo/shapes.h>
#include <geo/vec3.h>

#include "tool.h"

namespace plnr::tools {

// Center Arc: click center, click start point (fixes radius and 0-angle direction), then a sweep stage
// shows the signed angle from start dir to cursor dir about +Z. Commits an open polyline (PieTool is
// the closed variant). Degenerate clicks stay armed; Escape resets.
class ArcCenterTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

    // VCB commit. Radius stage: Scalar/Radius set startPoint_ and advance to sweep. Sweep stage: Scalar is
    // DEGREES, signed by the cursor's sweep. Segments/CircleSegments adjust segments_; idle with
    // lastCommit_ regenerates the last arc. Everything else is "Invalid entry.".
    void onVcbCommit(ToolContext& ctx, const VcbValue& value) override;

private:
    // Resolves e via geo::infer(), ground-plane only.
    geo::Inference resolve(ToolContext& ctx, const PointerEvent& e) const;

    // The single geo::arcPointsCenter call site (commit/preview/retro-edit). Empty if rejected.
    std::vector<geo::Vec3> pointsForSweep(const geo::Vec3& center, const geo::Vec3& startPoint, double sweep,
                                           int segments) const;

    // Arc points for a center/start/cursor triple: signed sweep from (start-center) to (cursor-center) about +Z.
    std::vector<geo::Vec3> pointsFor(const geo::Vec3& center, const geo::Vec3& startPoint,
                                      const geo::Vec3& cursor) const;

    // Preview verts: nothing before center_, a radius segment before startPoint_, then the sweep arc.
    std::vector<float> previewVerts(const geo::Vec3& cursor) const;

    // Commits the arc at exactly `sweep`, records lastCommit_ and resets to idle.
    void commitSweep(ToolContext& ctx, double sweep);

    // Regenerates the just-committed arc at a new sweep/segments (other unchanged).
    void regenerate(ToolContext& ctx, double sweep, int segments);

    // Resets to idle; does NOT touch lastCommit_ (it also runs right after a commit).
    void reset(ToolContext& ctx);

    // Sets the VCB for the current stage (Radius/Angle mapping in the class comment).
    void updateVcb(ToolContext& ctx) const;

    static constexpr const char* kActivationHint =
        "Click to place the center of the arc. Use Ctrl '+' or Ctrl '-' to change the number of segments.";
    static constexpr const char* kStartHint = "Click to place the arc's start point.";
    static constexpr const char* kSweepHint =
        "Click to set the arc's sweep, or enter an angle. Use Ctrl '+' or Ctrl '-' to change the number of segments.";
    static constexpr int kMinSegments = 1;
    static constexpr int kMaxSegments = 999;

    int segments_ = geo::kArcDefaultSegments;
    std::optional<geo::Vec3> center_;
    std::optional<geo::Vec3> startPoint_;
    // Last onPointerMove target, so onKeyDown's segment adjustment can redraw the preview (it gets no PointerEvent).
    std::optional<geo::Vec3> lastGround_;

    // VCB retro-edit window: params of the last committed arc; nullopt if none.
    struct LastCommit {
        geo::Vec3 center;
        geo::Vec3 startPoint;
        double sweep{};
        int segments{};
    };
    std::optional<LastCommit> lastCommit_;
};

}  // namespace plnr::tools
