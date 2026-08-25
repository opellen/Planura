#pragma once

#include <optional>
#include <vector>

#include <geo/infer.h>
#include <geo/shapes.h>
#include <geo/vec3.h>

#include "tool.h"

namespace plnr::tools {

// industry-standard 2-Point Arc: click for the chord start, click again for
// the chord end (rubber-band line meanwhile), then a bulge stage rubber-
// bands the arc itself -- cursor's perpendicular offset from the chord
// sets |bulge| (sagitta), which side sets the sign -- click to commit
// (requestAddPolyline, open). Degenerate clicks stay armed; Escape resets;
// Ctrl+/- adjusts segments. Ground-plane-only
class Arc2PointTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

    // Handles a committed VCB entry. Chord stage: Scalar advances to the
    // bulge stage at that length; Segments/CircleSegments adjust segments_.
    // Bulge stage: Scalar commits at |value| bulge; Radius converts via
    // sagittaForRadius; Segments/CircleSegments adjust segments_. Idle with
    // lastCommit_: Scalar/Radius/Segments/CircleSegments regenerate --
    // doc: tools.md. Dims2 (and out-of-stage kinds) are "Invalid entry.".
    void onVcbCommit(ToolContext& ctx, const VcbValue& value) override;

private:
    // Resolves e via geo::infer(), enforcing the ground-plane-only contract
    // (see groundFallback in the .cpp). Same as Circle/Polygon's resolve().
    geo::Inference resolve(ToolContext& ctx, const PointerEvent& e) const;

    // The signed bulge (sagitta) for chord a->b and cursor's perpendicular
    // offset -- see class comment for sign. 0.0 for a degenerate chord.
    double bulgeFor(const geo::Vec3& a, const geo::Vec3& b, const geo::Vec3& cursor) const;

    // The single geo::arcPoints2Point call site every commit/preview/retro-
    // regenerate path funnels through. Empty if arcPoints2Point rejects it.
    std::vector<geo::Vec3> pointsForBulge(const geo::Vec3& a, const geo::Vec3& b, double bulge, int segments) const;

    // The arc point list for chord a->b, bulged toward cursor's perpendicular
    // offset. Empty when pointsForBulge rejects it (cursor on the chord).
    std::vector<geo::Vec3> pointsFor(const geo::Vec3& a, const geo::Vec3& b, const geo::Vec3& cursor) const;

    // Radius->sagitta conversion, minor-arc only
    // when r < half-chord.
    std::optional<double> sagittaForRadius(double chordLen, double r) const;

    // CircleSegments->Segments conversion for the retro-edit path: recovers
    // the arc's sweep angle from chord + |bulge|
    double sweepForChordBulge(double chordLen, double absBulge) const;

    // Commits the arc at exactly `bulge`, recording lastCommit_ and resetting
    // to idle -- shared by onPointerDown and onVcbCommit's bulge-stage cases.
    void commitBulge(ToolContext& ctx, double bulge);

    // Regenerates the just-committed arc at a new bulge/segments (other left
    // unchanged) -- shared by onVcbCommit's idle+lastCommit cases.
    void regenerate(ToolContext& ctx, double bulge, int segments);

    // The current stage's preview verts: a plain start->cursor segment
    // before chordEnd_, the bulge arc once it's placed.
    std::vector<float> previewVerts(const geo::Vec3& cursor) const;

    // Resets to the idle state (hint/preview/VCB reset).
    void reset(ToolContext& ctx);

    // Sets the VCB for the current stage -- "Length" through the chord-end
    // stage, "Bulge" once the chord is fixed.
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
    // Most recent onPointerMove target, kept so onKeyDown's segment-count
    // adjustment can redraw the preview at the cursor's last known ground
    // position without a fresh PointerEvent (onKeyDown gets none).
    std::optional<geo::Vec3> lastGround_;

    // The VCB retro-edit window: params of the most recently committed arc.
    // nullopt if nothing to retro-edit.
    struct LastCommit {
        geo::Vec3 a;
        geo::Vec3 b;
        double bulge{};
        int segments{};
    };
    std::optional<LastCommit> lastCommit_;
};

}  // namespace plnr::tools
