#pragma once

#include <optional>
#include <string>
#include <vector>

#include <geo/entity.h>
#include <geo/pick.h>
#include <geo/scene.h>
#include <geo/scene_ops.h>
#include <geo/vec3.h>

#include "agent/events.h"
#include "tool.h"

namespace plnr::tools {

// industry-standard Protractor. Stage machine: PlaneDetect (hover resolves a
// plane, click 1 fixes the vertex) -> VertexSet (click 2 fixes the baseline
// ray) -> BaselineSet (live signed angle, snapped to 15-degree ticks near
// the pivot, else free, always rounded to 0.1 degrees).

// Click 3 (or a VCB Scalar/Slope commit) calls commit(): guide mode
// (default on, Ctrl toggles) adds a guide line and re-arms; measure-only
// mode freezes the reading in place until Escape or a fresh click.

// Shift (shiftLock_): rising-edge plane capture, point slides on the
// locked plane while held. Alt (altFreeze_): stronger freeze -- point AND
// normal pinned, no sliding; takes precedence over Shift.

// Arrow-key plane lock: Up/Right/Left locks planeNormal_ to a world axis
// (Blue/Red/Green) through an anchor, same key again unlocks. Only
// consulted in PlaneDetect; Escape clears it.

// Down arrow: unconditional "Constraint not appropriate" flash, any stage.
// VCB is always "Angle": Scalar sets it directly; Slope (a:b) sets
// atan(a/b) degrees, rejecting b == 0.

// buildPreview never rotates the selection -- Protractor only acts on the
// vertex/baseline/angle it collects itself.
class ProtractorTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;
    void onVcbCommit(ToolContext& ctx, const VcbValue& value) override;

private:
    enum class Stage { PlaneDetect, VertexSet, BaselineSet };

    // A hovered/locked/frozen plane candidate: a point on it plus its unit
    // normal.
    struct PlaneHit {
        geo::Vec3 point;
        geo::Vec3 normal;
        bool valid{};
    };

    // Final resolved angle: degrees (tick-snapped + rounded), matching
    // radians, and whether a tick snap applied (informational only).
    struct AngleReading {
        double deg{};
        double rad{};
        bool snapped{};
    };

    // Stage PlaneDetect's raw hover resolution: a hovered face's own normal,
    // a hovered vertex/edge's ground-plane fallback, or the ray's own
    // ground-plane intersection when nothing is hit.
    PlaneHit resolvePlaneHit(ToolContext& ctx, const PointerEvent& e) const;

    // Combines resolvePlaneHit with arrow lock / Alt freeze / Shift lock.
    // Precedence (highest first): arrow lock, Alt freeze, Shift lock, hover.
    PlaneHit currentPlaneCandidate(ToolContext& ctx, const PointerEvent& e);

    // Intersects e.ray with the fixed protractor plane (pivot_/planeNormal_).
    // Used once VertexSet/BaselineSet stop the plane from moving.
    std::optional<geo::Vec3> resolveOnFixedPlane(const PointerEvent& e) const;

    // Tick-snap + 0.1-degree-rounded angle from rayDir_ to cursor about
    // planeNormal_. radius is passed in since some callers have no PointerEvent.
    AngleReading computeAngle(const geo::Vec3& cursor, double radius) const;

    // Protractor radius: pivot_->baseline distance once set, else a
    // tolerance-scaled default.
    double protractorRadius(const PointerEvent& e) const;

    // |rayDir_| -- BaselineSet-only radius for call sites with no PointerEvent.
    double committedRadius() const;

    // Point on the sweep circle at angleRad from the baseline: rayDir_
    // rotated by angleRad about (pivot_, planeNormal_).
    geo::Vec3 sweepTip(double angleRad) const;

    // Tints the whole protractor batch when planeNormal_ lands exactly on a
    // world axis (arrow lock, or an axis-aligned hover/Shift-lock).
    PreviewColor classifyPlaneColor() const;

    // Appends 24 tick marks (15 degrees apart, every 6th longer) around the
    // circle, using the SAME plane basis geo::regularPolygonPoints uses so
    // tick 0 lines up with the circle's own vertex 0.
    void appendTicks(std::vector<float>& verts, double radius, const geo::Vec3& startDir) const;

    // Full preview for the current stage: circle + ticks (always), the
    // baseline + live ray (BaselineSet only), and (guide mode only) a
    // dashed preview of the guide line about to be created.
    std::vector<ToolContext::PreviewBatch> buildPreview(double angleRad, double radius) const;

    // Commits at angleRad, rounded to 0.1 degrees regardless of source (a
    // VCB value bypasses computeAngle's own rounding). Guide mode adds a
    // guide line and re-arms; measure-only freezes the reading in place.
    void commit(ToolContext& ctx, double angleRad);

    // Resets to stage PlaneDetect with hint/VCB refreshed. Does not touch
    // guideMode_/ctrlHeld_/lockedArrowKey_/arrowLockAxis_/arrowLockAnchor_,
    // which persist across a commit until re-toggled or deactivation.
    void reset(ToolContext& ctx);

    // Sets the VCB label ("Angle") and, in BaselineSet with a known cursor
    // and not frozen, a live "~"-prefixed value (commit()'s frozen write uses formatExact).
    void updateVcb(ToolContext& ctx) const;

    // Toggles guideMode_ on a rising edge of ctrlNow.
    void syncCtrl(bool ctrlNow);

    // Toggles the arrow-key plane lock for Up/Right/Left (same key again unlocks).
    void handleArrowLock(ToolContext& ctx, int key);

    // Down arrow: the unconditional "Constraint not appropriate at this
    // time." flash.
    void showConstraintWarning(ToolContext& ctx) const;

    // The verified hint string, returned unconditionally for every stage
    // (no per-stage lead variant).
    std::string currentHint() const;

    static constexpr int kCircleSegments = 48;

    Stage stage_ = Stage::PlaneDetect;
    geo::Vec3 pivot_;
    geo::Vec3 planeNormal_{0.0, 0.0, 1.0};
    geo::Vec3 rayDir_;  // pivot_ -> baseline point; meaningful only in stage BaselineSet
    // Most recent on-plane cursor point, any stage -- lets updateVcb/preview
    // redraw without a fresh PointerEvent.
    std::optional<geo::Vec3> lastCursor_;

    std::optional<PlaneHit> shiftLock_;  // see the class comment (Shift/Alt)
    std::optional<PlaneHit> altFreeze_;  // see the class comment (Shift/Alt)

    // lockedArrowKey_ == 0 means unlocked; otherwise the Qt::Key_Up/Right/
    // Left value currently locked (same key again toggles off).
    int lockedArrowKey_ = 0;
    geo::Vec3 arrowLockAxis_;
    geo::Vec3 arrowLockAnchor_;

    bool ctrlHeld_ = false;   // last-seen PointerEvent::ctrl, for syncCtrl's edge detection
    bool guideMode_ = true;   // toggled by Ctrl; persists across commits until toggled again or onDeactivate

    // BaselineSet-only freeze (measure-only commit): no live tracking while
    // true.
    bool frozen_ = false;
};

}  // namespace plnr::tools
