#pragma once

#include <optional>
#include <vector>

#include <geo/infer.h>
#include <geo/shapes.h>
#include <geo/vec3.h>

#include "tool.h"

namespace plnr::tools {

// Pie: same center-first interaction as ArcCenterTool (center, start point, sweep) except the center is
// appended after the arc points and it commits closed, so face detection turns the two radii + arc into
// the wedge face. Click guards/VCB mapping shared with ArcCenterTool.
class PieTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

    // Same apply table as ArcCenterTool::onVcbCommit, but committing/regenerating closed (wedge, center included).
    void onVcbCommit(ToolContext& ctx, const VcbValue& value) override;

private:
    // Resolves e via geo::infer(), ground-plane only.
    geo::Inference resolve(ToolContext& ctx, const PointerEvent& e) const;

    // The single geo::arcPointsCenter call site (plus the center-append closing the wedge). Empty if rejected.
    std::vector<geo::Vec3> pointsForSweep(const geo::Vec3& center, const geo::Vec3& startPoint, double sweep,
                                           int segments) const;

    // Wedge points for a center/start/cursor triple: the sweep arc with center appended, ready to commit closed.
    std::vector<geo::Vec3> pointsFor(const geo::Vec3& center, const geo::Vec3& startPoint,
                                      const geo::Vec3& cursor) const;

    // CLOSED preview verts (what a click would commit): nothing before center_, a radius segment, then the wedge.
    std::vector<float> previewVerts(const geo::Vec3& cursor) const;

    // Commits the wedge at exactly `sweep`, records lastCommit_ and resets to idle.
    void commitSweep(ToolContext& ctx, double sweep);

    // Regenerates the just-committed wedge at a new sweep/segments (other unchanged).
    void regenerate(ToolContext& ctx, double sweep, int segments);

    // Resets to idle; does NOT touch lastCommit_ (it also runs right after a commit).
    void reset(ToolContext& ctx);

    // Sets the VCB for the stage; Radius/Angle mapping as in ArcCenterTool.
    void updateVcb(ToolContext& ctx) const;

    static constexpr const char* kActivationHint =
        "Click to place the center of the pie. Use Ctrl '+' or Ctrl '-' to change the number of segments.";
    static constexpr const char* kStartHint = "Click to place the pie's start point.";
    static constexpr const char* kSweepHint =
        "Click to set the pie's sweep, or enter an angle. Use Ctrl '+' or Ctrl '-' to change the number of segments.";
    static constexpr int kMinSegments = 1;
    static constexpr int kMaxSegments = 999;

    int segments_ = geo::kArcDefaultSegments;
    std::optional<geo::Vec3> center_;
    std::optional<geo::Vec3> startPoint_;
    // Last onPointerMove target, so onKeyDown's segment adjustment can redraw the preview (it gets no PointerEvent).
    std::optional<geo::Vec3> lastGround_;

    // VCB retro-edit window; as ArcCenterTool::LastCommit.
    struct LastCommit {
        geo::Vec3 center;
        geo::Vec3 startPoint;
        double sweep{};
        int segments{};
    };
    std::optional<LastCommit> lastCommit_;
};

}  // namespace plnr::tools
