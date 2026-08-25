#pragma once

#include <array>
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

// industry-standard Scale: grips on the selection's world-axis-aligned bbox,
// dragged to scale about an anchor (the grip's opposite point, or the bbox
// center if Ctrl-toggled). Idle/GripIdle/Dragging stages; GripIdle rebuilds
// bbox_/grips_ from the selection closure every non-dragging event.

// buildGrips yields up to 8 corner (3-axis) + 6 face (1-axis) + 12 edge
// (2-axis) grips, dropping degenerate axes and duplicates. Ctrl/Shift
// toggle aboutCenter_/uniformMode_ on a rising edge (never onKeyDown).
class ScaleTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;
    void onVcbCommit(ToolContext& ctx, const VcbValue& value) override;

private:
    enum class Stage { Idle, GripIdle, Dragging };

    struct Bbox {
        geo::Vec3 min, max;
        bool valid{};
    };

    // One grip: pos is its original (pre-scale) location on bbox_; anchor
    // is the opposite point it scales about by default. axis[i]: whether
    // world axis i is active (3 true = corner, 2 = edge, 1 = face).
    struct Grip {
        geo::Vec3 pos;
        geo::Vec3 anchor;
        std::array<bool, 3> axis{};
        int activeCount{};
    };

    // Per-world-axis scale factors -- 1.0 on any axis a grip doesn't touch.
    struct Factors {
        double fx{1.0}, fy{1.0}, fz{1.0};
    };

    // Last committed scale this activation, for a post-commit VCB retype to
    // replay against. fx/fy/fz are ABSOLUTE factors applied so far about
    // center; a retype computes a multiplicative delta against these.
    struct LastCommit {
        std::vector<events::EntityRef> refs;
        geo::Vec3 center;
        double fx{1.0}, fy{1.0}, fz{1.0};
        std::array<bool, 3> activeAxes{};
    };

    // Rebuilds bbox_/grips_ from ctx.selection()'s closure. Empty
    // selection/closure resets to Idle and returns false; on success,
    // stage_ becomes GripIdle (unless already Dragging) and returns true.
    bool refreshFromSelection(ToolContext& ctx);

    // Builds the grip set for bbox -- see the class comment for the
    // corner/face/edge/degenerate-axis rule.
    std::vector<Grip> buildGrips(const Bbox& bbox) const;

    // Nearest grip to e.ray within a vertexTol-scaled pick tolerance (world-
    // space, not screen-space). -1 if nothing is within tolerance.
    int pickGrip(const PointerEvent& e) const;

    // Toggles aboutCenter_ on a rising edge of ctrlNow; persists across
    // drags until reset on (de)activation.
    void syncCtrl(bool ctrlNow);

    // Toggles uniformMode_ on a rising edge of shiftNow, only while
    // Dragging -- a hover-only Shift tap has nothing yet to invert.
    void syncShift(bool shiftNow);

    // Current fixed point: bbox center if aboutCenter_, else the grabbed
    // grip's anchor; falls back to bbox center if nothing is grabbed.
    geo::Vec3 currentAnchor() const;
    geo::Vec3 bboxCenter() const;
    double gripHalfSize() const;

    // Live per-axis factors for dragGrip_ given e.ray: uniformMode_ ? one
    // factor along the anchor->grip diagonal : one per active world axis.
    Factors computeFactors(const PointerEvent& e) const;

    // Appends bbox_ transformed by xf (12 edges) to verts.
    void appendBboxWireframe(std::vector<float>& verts, const geo::Transform& xf) const;

    // Appends ctx.selection()'s closure edges, each endpoint mapped through
    // xf, to verts.
    void appendSelectionTransformed(ToolContext& ctx, std::vector<float>& verts, const geo::Transform& xf) const;

    // Builds the full preview (bbox wireframe, grip cubes, transformed selection) for factors f.
    std::vector<ToolContext::PreviewBatch> buildPreview(ToolContext& ctx, const Factors& f) const;

    // Sets the VCB label ("Scale") and, when live is non-null, a readout:
    // uniformMode_ ? the shared factor : the dominant active axis' factor
    // (per-axis live display is a simplification).
    void updateVcb(ToolContext& ctx, const Factors* live) const;

    // Validates f against the grabbed grip's active axes (factor 0 on an
    // active axis is rejected; negative flips), requests the transform,
    // records lastCommit_, and returns to GripIdle.
    void commit(ToolContext& ctx, const Factors& f);

    // Returns to GripIdle (or Idle if bbox_ isn't valid), clearing
    // drag/hover grips and redrawing. Shared by commit() and Escape-during-drag.
    void cancelDrag(ToolContext& ctx);

    Stage stage_ = Stage::Idle;
    Bbox bbox_;
    std::vector<Grip> grips_;
    int hoverGrip_ = -1;  // GripIdle only
    int dragGrip_ = -1;   // Dragging only

    // Shift's per-drag toggle: seeded from the grabbed grip's default
    // (corner starts uniform; edge/face starts per-axis), flippable via Shift.
    bool uniformMode_ = true;

    bool aboutCenter_ = false;  // Ctrl toggle -- see syncCtrl
    bool ctrlHeld_ = false;
    bool shiftHeld_ = false;

    std::optional<LastCommit> lastCommit_;
};

}  // namespace plnr::tools
