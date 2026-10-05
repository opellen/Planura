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

// Protractor: PlaneDetect (hover plane, click 1 = vertex) -> VertexSet (click 2 = baseline) -> BaselineSet (signed
// angle, 15-degree tick snap near the pivot, rounded to 0.1). Click 3 or a VCB Scalar/Slope commit: guide mode
// (Ctrl toggles, default on) adds a guide and re-arms; measure-only freezes the reading. Shift slides on the
// locked plane; Alt pins point AND normal (wins). Arrows Up/Right/Left lock the normal to Blue/Red/Green
// (PlaneDetect only). VCB "Angle"; Slope a:b = atan(a/b) degrees, b == 0 rejected.
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

    // A hovered/locked/frozen plane candidate: a point on it plus its unit normal.
    struct PlaneHit {
        geo::Vec3 point;
        geo::Vec3 normal;
        bool valid{};
    };

    // Final resolved angle: degrees (tick-snapped, rounded), matching radians, and whether a tick snap applied (informational).
    struct AngleReading {
        double deg{};
        double rad{};
        bool snapped{};
    };

    // PlaneDetect's raw hover plane: a hovered face's normal, a vertex/edge's ground-plane fallback, or the ray's ground intersection when nothing is hit.
    PlaneHit resolvePlaneHit(ToolContext& ctx, const PointerEvent& e) const;

    // Combines resolvePlaneHit with arrow lock / Alt freeze / Shift lock; precedence: arrow, Alt, Shift, hover.
    PlaneHit currentPlaneCandidate(ToolContext& ctx, const PointerEvent& e);

    // Intersects e.ray with the fixed protractor plane (pivot_/planeNormal_) once VertexSet/BaselineSet fix it.
    std::optional<geo::Vec3> resolveOnFixedPlane(const PointerEvent& e) const;

    // Tick-snapped, 0.1-degree-rounded angle from rayDir_ to cursor about planeNormal_; radius is a parameter (some callers have no PointerEvent).
    AngleReading computeAngle(const geo::Vec3& cursor, double radius) const;

    // Protractor radius: pivot_->baseline distance once set, else a tolerance-scaled default.
    double protractorRadius(const PointerEvent& e) const;

    // |rayDir_|; BaselineSet-only radius for callers with no PointerEvent.
    double committedRadius() const;

    // Point on the sweep circle at angleRad from the baseline (rayDir_ rotated about pivot_/planeNormal_).
    geo::Vec3 sweepTip(double angleRad) const;

    // Tints the batch when planeNormal_ lands exactly on a world axis.
    PreviewColor classifyPlaneColor() const;

    // Appends 24 tick marks (15 degrees apart, every 6th longer) using the SAME plane basis as geo::regularPolygonPoints, so tick 0 meets the circle's vertex 0.
    void appendTicks(std::vector<float>& verts, double radius, const geo::Vec3& startDir) const;

    // Preview: circle + ticks always, baseline + live ray in BaselineSet, and in guide mode a dashed guide-line preview.
    std::vector<ToolContext::PreviewBatch> buildPreview(double angleRad, double radius) const;

    // Commits at angleRad rounded to 0.1 degrees regardless of source (a VCB value bypasses computeAngle's rounding). Guide mode adds a guide and re-arms; measure-only freezes the reading.
    void commit(ToolContext& ctx, double angleRad);

    // Resets to PlaneDetect with hint/VCB refreshed. Keeps guideMode_/ctrlHeld_/lockedArrowKey_/arrowLockAxis_/arrowLockAnchor_ (persist until re-toggled or deactivation).
    void reset(ToolContext& ctx);

    // Sets the VCB label ("Angle") and, in BaselineSet with a known cursor and not frozen, a live "~" value (commit()'s frozen write uses formatExact).
    void updateVcb(ToolContext& ctx) const;

    // Toggles guideMode_ on a rising edge of ctrlNow.
    void syncCtrl(bool ctrlNow);

    // Toggles the Up/Right/Left plane lock (same key again unlocks).
    void handleArrowLock(ToolContext& ctx, int key);

    // Down arrow: the unconditional "Constraint not appropriate at this time." flash.
    void showConstraintWarning(ToolContext& ctx) const;

    // The verified hint string, returned for every stage (no per-stage variant).
    std::string currentHint() const;

    static constexpr int kCircleSegments = 48;

    Stage stage_ = Stage::PlaneDetect;
    geo::Vec3 pivot_;
    geo::Vec3 planeNormal_{0.0, 0.0, 1.0};
    geo::Vec3 rayDir_;  // pivot_ -> baseline point; meaningful only in stage BaselineSet
    // Last on-plane cursor point, any stage, so updateVcb/preview can redraw without a PointerEvent.
    std::optional<geo::Vec3> lastCursor_;

    std::optional<PlaneHit> shiftLock_;  // see the class comment (Shift/Alt)
    std::optional<PlaneHit> altFreeze_;  // see the class comment (Shift/Alt)

    // 0 = unlocked; else the Qt::Key_Up/Right/Left value currently locked.
    int lockedArrowKey_ = 0;
    geo::Vec3 arrowLockAxis_;
    geo::Vec3 arrowLockAnchor_;

    bool ctrlHeld_ = false;   // last-seen PointerEvent::ctrl, for syncCtrl's edge detection
    bool guideMode_ = true;   // toggled by Ctrl; persists across commits until toggled again or onDeactivate

    // BaselineSet-only freeze (measure-only commit): no live tracking while true.
    bool frozen_ = false;
};

}  // namespace plnr::tools
