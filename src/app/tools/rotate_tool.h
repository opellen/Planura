#pragma once

#include <optional>
#include <vector>

#include <geo/entity.h>
#include <geo/pick.h>
#include <geo/scene.h>
#include <geo/scene_ops.h>
#include <geo/vec3.h>

#include "agent/events.h"
#include "tool.h"

namespace plnr::tools {

// Rotate: PlaneDetect (hover picks a plane) -> PivotSet (click 1) -> RaySet (click 2, angle from that ray) ->
// click 3 commits and rearms at PlaneDetect. Acts on ctx.selection() only. Ctrl toggles copy mode, persisting
// across rotations. Circle tints per axis alignment.
class RotateTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;
    void onVcbCommit(ToolContext& ctx, const VcbValue& value) override;

private:
    enum class Stage { PlaneDetect, PivotSet, RaySet };

    // Hovered/locked plane candidate: a point on it plus its unit normal.
    struct PlaneHit {
        geo::Vec3 point;
        geo::Vec3 normal;
        bool valid{};
    };

    // VCB retro-edit window for the last rotation; nullopt once closed (Escape or a fresh pivot click). wasCopy routes onVcbCommit to Scalar delta-replay vs. Array retype (copy only).
    struct LastCommit {
        std::vector<events::EntityRef> refs;
        geo::Vec3 pivot;
        geo::Vec3 normal;
        double angleRad{};
        bool wasCopy{};
    };

    // Toggles copyMode_ on a rising edge of ctrlNow.
    void syncCtrl(bool ctrlNow);

    // Rotation plane under the cursor: a Face hit uses its normal; Vertex/Edge/no-hit fall back to ground. !valid only when the ray is parallel to ground with nothing picked.
    PlaneHit resolvePlaneHit(ToolContext& ctx, const PointerEvent& e) const;

    // Intersects e.ray with the fixed protractor plane (pivot_/planeNormal_); nullopt when parallel.
    std::optional<geo::Vec3> resolveOnFixedPlane(const PointerEvent& e) const;

    // Signed angle (radians) from rayDir_ to (cursor - pivot_) about planeNormal_; 0 if the cursor is on pivot_. Valid only in RaySet.
    double liveAngle(const geo::Vec3& cursor) const;

    // Protractor radius: pivot_->ray-point distance once the first ray is set, else a tolerance-scaled default (constant on-screen size).
    double protractorRadius(const PointerEvent& e) const;

    // Axis color for the circle when planeNormal_ aligns with X/Y/Z, default otherwise.
    PreviewColor classifyPlaneColor() const;

    // Preview batches at angleRad: protractor circle always, ray pair once rayDir_ is set, and the rotated selection; pieces whose prerequisite isn't ready are omitted.
    std::vector<ToolContext::PreviewBatch> buildPreview(ToolContext& ctx, double angleRad, double radius) const;

    // Appends ctx.selection()'s closure edges rotated by angleRad about (pivot_, planeNormal_) to verts.
    void appendSelectionRotated(ToolContext& ctx, std::vector<float>& verts, double angleRad) const;

    // Commits a rotation by angleRad about (pivot_, planeNormal_) on ctx.selection()'s closure. Empty selection = hint only. Always resets to PlaneDetect, even on a no-op.
    void commit(ToolContext& ctx, double angleRad);

    // Resets to PlaneDetect with hint/VCB refreshed. Keeps lastCommit_/copyMode_/ctrlHeld_ (persist across a commit).
    void reset(ToolContext& ctx);

    // Sets the VCB label ("Angle") and, only in RaySet with a known cursor, the live angle.
    void updateVcb(ToolContext& ctx) const;

    static constexpr int kCircleSegments = 48;

    Stage stage_ = Stage::PlaneDetect;
    geo::Vec3 pivot_;
    geo::Vec3 planeNormal_{0.0, 0.0, 1.0};
    geo::Vec3 rayDir_;  // pivot_ -> first ray point; meaningful only in stage RaySet
    // Last on-plane cursor point, any stage, so updateVcb/onKeyDown can redraw without a PointerEvent.
    std::optional<geo::Vec3> lastCursor_;

    // Shift plane-lock (PlaneDetect only): freezes normal + anchor when Shift is pressed; while held the point slides along that plane, never re-anchoring to the raw cursor.
    std::optional<PlaneHit> shiftLock_;

    bool ctrlHeld_ = false;  // last-seen PointerEvent::ctrl, for syncCtrl's edge detection
    bool copyMode_ = false;  // toggled by Ctrl; persists across commits until toggled again or onDeactivate

    std::optional<LastCommit> lastCommit_;
};

}  // namespace plnr::tools
