#pragma once

#include <optional>
#include <vector>

#include <geo/infer.h>
#include <geo/shapes.h>
#include <geo/vec3.h>

#include "tool.h"

namespace plnr::tools {

// industry-standard Pie: identical center-first interaction to ArcCenterTool
// (center, start point fixing radius + 0-angle, sweep stage) except the
// point list appends the center after the arc's points and commits closed
// -- the two radii + arc chain bound a planar loop, so face detection
// turns it into the wedge face. Shared click-guards/VCB mapping with
// ArcCenterTool (see its class comment)
class PieTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

    // Identical apply table to ArcCenterTool::onVcbCommit, just committing/
    // regenerating closed (wedge, center included) instead of open.
    void onVcbCommit(ToolContext& ctx, const VcbValue& value) override;

private:
    // Resolves e via geo::infer(), enforcing the ground-plane-only contract.
    // Same as Circle/Polygon's resolve().
    geo::Inference resolve(ToolContext& ctx, const PointerEvent& e) const;

    // The single geo::arcPointsCenter call site (plus the center-append that
    // closes the wedge). Empty when arcPointsCenter's guards reject it.
    std::vector<geo::Vec3> pointsForSweep(const geo::Vec3& center, const geo::Vec3& startPoint, double sweep,
                                           int segments) const;

    // The wedge point list for a center/start/cursor triple: the sweep arc's
    // points with center appended, ready to commit closed.
    std::vector<geo::Vec3> pointsFor(const geo::Vec3& center, const geo::Vec3& startPoint,
                                      const geo::Vec3& cursor) const;

    // The current stage's CLOSED preview verts (matches what a click would
    // commit) -- nothing before center_, a radius segment, then the wedge.
    std::vector<float> previewVerts(const geo::Vec3& cursor) const;

    // Commits the wedge at exactly `sweep`, recording lastCommit_ and
    // resetting to idle -- shared by onPointerDown and onVcbCommit.
    void commitSweep(ToolContext& ctx, double sweep);

    // Regenerates the just-committed wedge at a new sweep/segments (other
    // left unchanged) -- shared by onVcbCommit's idle+lastCommit cases.
    void regenerate(ToolContext& ctx, double sweep, int segments);

    // Resets to idle (hint/preview/VCB reset) -- deliberately does NOT touch
    // lastCommit_, since this also runs right after a successful commit.
    void reset(ToolContext& ctx);

    // Sets the VCB for the current stage -- see ArcCenterTool's identical Radius/Angle mapping.
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
    // Most recent onPointerMove target, kept so onKeyDown's segment-count
    // adjustment can redraw the preview at the cursor's last known ground
    // position without a fresh PointerEvent (onKeyDown gets none).
    std::optional<geo::Vec3> lastGround_;

    // The VCB retro-edit window -- see ArcCenterTool::LastCommit's
    // identical comment.
    struct LastCommit {
        geo::Vec3 center;
        geo::Vec3 startPoint;
        double sweep{};
        int segments{};
    };
    std::optional<LastCommit> lastCommit_;
};

}  // namespace plnr::tools
