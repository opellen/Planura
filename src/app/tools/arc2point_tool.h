#pragma once

#include <optional>
#include <vector>

#include <geo/infer.h>
#include <geo/shapes.h>
#include <geo/vec3.h>

#include "tool.h"

namespace plnr::tools {

// 2-Point Arc: click chord start, click chord end, then a bulge stage -- the cursor's perpendicular
// offset sets |bulge| (sagitta), its side the sign; click commits an open polyline. Degenerate
// clicks stay armed; Escape resets; Ctrl+/- adjusts segments. Ground-plane only.
class Arc2PointTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

    // VCB commit. Chord stage: Scalar advances to the bulge stage. Bulge stage: Scalar = |bulge|, Radius
    // via sagittaForRadius. Idle with lastCommit_: regenerates the last arc. Segments/CircleSegments
    // adjust segments_ throughout; anything else is "Invalid entry.".
    void onVcbCommit(ToolContext& ctx, const VcbValue& value) override;

private:
    // Resolves e via geo::infer(), keeping the ground-plane-only contract (groundFallback in the .cpp).
    geo::Inference resolve(ToolContext& ctx, const PointerEvent& e) const;

    // Signed bulge (sagitta) for chord a->b and the cursor's perpendicular offset; 0.0 for a degenerate chord.
    double bulgeFor(const geo::Vec3& a, const geo::Vec3& b, const geo::Vec3& cursor) const;

    // The single geo::arcPoints2Point call site for commit/preview/retro-edit. Empty if rejected.
    std::vector<geo::Vec3> pointsForBulge(const geo::Vec3& a, const geo::Vec3& b, double bulge, int segments) const;

    // Arc points for chord a->b bulged toward the cursor. Empty when pointsForBulge rejects it.
    std::vector<geo::Vec3> pointsFor(const geo::Vec3& a, const geo::Vec3& b, const geo::Vec3& cursor) const;

    // Radius->sagitta, minor arc only; nullopt when r < half-chord.
    std::optional<double> sagittaForRadius(double chordLen, double r) const;

    // CircleSegments->Segments for retro-edit: recovers the sweep angle from chord + |bulge|.
    double sweepForChordBulge(double chordLen, double absBulge) const;

    // Commits the arc at exactly `bulge`, records lastCommit_ and resets to idle.
    void commitBulge(ToolContext& ctx, double bulge);

    // Regenerates the just-committed arc at a new bulge/segments (other unchanged).
    void regenerate(ToolContext& ctx, double bulge, int segments);

    // Preview verts: start->cursor segment before chordEnd_, the bulge arc after.
    std::vector<float> previewVerts(const geo::Vec3& cursor) const;

    // Resets to idle (hint/preview/VCB).
    void reset(ToolContext& ctx);

    // Sets the VCB for the stage: "Length" until the chord end, "Bulge" after.
    void updateVcb(ToolContext& ctx) const;

    static constexpr const char* kActivationHint =
        "Click to place starting point of the arc. Use Ctrl '+' or Ctrl '-' to change the number of segments.";
    static constexpr const char* kChordHint = "Click to place ending point of the chord.";
    static constexpr const char* kBulgeHint =
        "Click to set bulge or enter distance. Use Ctrl '+' or Ctrl '-' to change the number of segments.";
    static constexpr int kMinSegments = 1;
    static constexpr int kMaxSegments = 999;

    int segments_ = geo::kArcDefaultSegments;
    std::optional<geo::Vec3> start_;
    std::optional<geo::Vec3> chordEnd_;
    // Last onPointerMove target, so onKeyDown's segment adjustment can redraw the preview (it gets no PointerEvent).
    std::optional<geo::Vec3> lastGround_;

    // VCB retro-edit window: params of the last committed arc; nullopt if none.
    struct LastCommit {
        geo::Vec3 a;
        geo::Vec3 b;
        double bulge{};
        int segments{};
    };
    std::optional<LastCommit> lastCommit_;
};

}  // namespace plnr::tools
