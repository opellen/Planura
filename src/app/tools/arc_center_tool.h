#pragma once

#include <optional>
#include <vector>

#include <geo/infer.h>
#include <geo/shapes.h>
#include <geo/vec3.h>

#include "tool.h"

namespace plnr::tools {

// industry-standard Center Arc (protractor-style): click for the center,
// click again for the start point (fixes radius + 0-angle direction,
// rubber-band radius line meanwhile), then a sweep stage rubber-bands the
// arc via the signed angle from start dir to cursor dir about +Z. Commits
// via requestAddPolyline (open); PieTool is the same but closed with center
// appended. Degenerate clicks stay armed; Escape resets
class ArcCenterTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

    // Handles a committed VCB entry. Radius stage: Scalar/Radius set
    // startPoint_ at that radius, advancing to sweep; Segments/CircleSegments
    // adjust segments_. Sweep stage: Scalar is a DEGREES angle, signed by the
    // current cursor's sweep; Segments/CircleSegments adjust segments_. Idle
    // with lastCommit_: Scalar/Radius/Segments/CircleSegments regenerate --
    // doc: tools.md. Everything else is "Invalid entry.".
    void onVcbCommit(ToolContext& ctx, const VcbValue& value) override;

private:
    // Resolves e via geo::infer(), enforcing the ground-plane-only contract.
    // Same as Circle/Polygon's resolve().
    geo::Inference resolve(ToolContext& ctx, const PointerEvent& e) const;

    // The single geo::arcPointsCenter call site every commit/preview/retro-
    // regenerate path funnels through. Empty if arcPointsCenter rejects it.
    std::vector<geo::Vec3> pointsForSweep(const geo::Vec3& center, const geo::Vec3& startPoint, double sweep,
                                           int segments) const;

    // The arc point list for a center/start/cursor triple -- the signed
    // sweep from (startPoint-center) to (cursor-center) about +Z.
    std::vector<geo::Vec3> pointsFor(const geo::Vec3& center, const geo::Vec3& startPoint,
                                      const geo::Vec3& cursor) const;

    // The current stage's preview verts: nothing before center_, a radius
    // segment before startPoint_, then the sweep arc once it is.
    std::vector<float> previewVerts(const geo::Vec3& cursor) const;

    // Commits the arc at exactly `sweep`, recording lastCommit_ and
    // resetting to idle -- shared by onPointerDown and onVcbCommit.
    void commitSweep(ToolContext& ctx, double sweep);

    // Regenerates the just-committed arc at a new sweep/segments (other left
    // unchanged) -- shared by onVcbCommit's idle+lastCommit cases.
    void regenerate(ToolContext& ctx, double sweep, int segments);

    // Resets to idle (hint/preview/VCB reset) -- deliberately does NOT touch
    // lastCommit_, since this also runs right after a successful commit.
    void reset(ToolContext& ctx);

    // Sets the VCB for the current stage -- see class comment for the
    // Radius/Angle mapping.
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
    // Most recent onPointerMove target, kept so onKeyDown's segment-count
    // adjustment can redraw the preview at the cursor's last known ground
    // position without a fresh PointerEvent (onKeyDown gets none).
    std::optional<geo::Vec3> lastGround_;

    // The VCB retro-edit window: params of the most recently committed arc.
    // nullopt if nothing to retro-edit.
    struct LastCommit {
        geo::Vec3 center;
        geo::Vec3 startPoint;
        double sweep{};
        int segments{};
    };
    std::optional<LastCommit> lastCommit_;
};

}  // namespace plnr::tools
