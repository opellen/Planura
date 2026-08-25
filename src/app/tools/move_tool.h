#pragma once

#include <optional>
#include <vector>

#include <geo/entity.h>
#include <geo/infer.h>
#include <geo/vec3.h>

#include "agent/events.h"
#include "tool.h"

namespace plnr::tools {

// industry-standard move: click to grab a vertex/edge/face, move to preview,
// click again to commit (or press-drag-release past a screen threshold).
// Escape cancels the in-progress grab.

// Target resolution: endpoint snap to OTHER geometry wins; otherwise the
// ray hits the screen-parallel plane through the grab point, never ground.

// Shift locks the dominant axis; an automatic ~12-degree screen-space snap
// also fires without Shift.

// Ctrl toggles copy mode on a rising edge: a copy sends the whole
// selection if the grabbed entity is a member of it, else just the
// grabbed entity; a plain move moves it in place.

// Post-commit VCB retro-edit: a copy's array count replays GeometryApi's
// array window; a plain move's Scalar re-enters at an exact distance.
// Closed only by the next commit, Escape, or deactivation.
class MoveTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    // Press-drag-release also commits: a release past the drag threshold
    // commits like the second click; sub-threshold keeps waiting.
    void onPointerUp(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;
    void onVcbCommit(ToolContext& ctx, const VcbValue& value) override;

private:
    // A picked-and-held entity: kind/id, grab point (drag anchor), and a
    // snapshot of vertex ids + original positions (so the preview/commit
    // delta stays relative to the pre-grab state).
    struct Grab {
        geo::EntityKind kind{};
        geo::Id id{};
        geo::Vec3 grabPoint;
        std::vector<geo::Id> vertexIds;
        std::vector<geo::Vec3> basePositions;
        // Unit normal of the free-drag plane (camera-facing at grab time, fixed for the drag).
        geo::Vec3 dragPlaneNormal;
        // Screen position of the grabbing press; onPointerUp measures travel against this.
        QPointF pressScreen;
    };

    // VCB retro-edit window for the last committed move; nullopt once
    // closed (Escape or deactivation). wasCopy routes onVcbCommit to a
    // Scalar delta-replay vs. an Array retype (valid only after a copy).
    struct LastCommit {
        geo::EntityKind kind{};
        geo::Id id{};
        geo::Vec3 delta;
        bool wasCopy{};
    };

    // Toggles copyMode_ on a rising edge of ctrlNow.
    void syncCtrl(bool ctrlNow);

    // Refs a copy commit sends to requestTransformEntities: the grabbed
    // entity, or the whole selection if it's a member of it.
    std::vector<events::EntityRef> copyRefs(ToolContext& ctx) const;

    // resolveTarget's return: pos is nullopt when nothing is grabbed or the
    // ray misses the drag plane. axis is the signed world-axis it snapped
    // to (auto snap or Shift lock), nullopt for endpoint/free drags.
    struct TargetResolution {
        std::optional<geo::Vec3> pos;
        std::optional<geo::Vec3> axis;
    };

    // Resolves e to the drag target (+ snapped axis, if any) per the class comment.
    TargetResolution resolveTarget(ToolContext& ctx, const PointerEvent& e) const;

    // Commits the in-progress grab at e's resolved target and reset()s --
    // shared tail of the second click and drag-release. Requires grab_.
    void commitDrag(ToolContext& ctx, const PointerEvent& e);

    // Dotted hover pattern on the face a click would grab while idle (uses
    // the same e.tols pick as the arming click).
    void updateHoverFace(ToolContext& ctx, const PointerEvent& e) const;

    // Resets to idle (no grab) with hint/preview cleared. Does not touch
    // lastCommit_/copyMode_/ctrlHeld_ -- would erase a commit just recorded.
    void reset(ToolContext& ctx);

    std::optional<Grab> grab_;

    bool ctrlHeld_ = false;  // last-seen PointerEvent::ctrl, for syncCtrl's edge detection
    bool copyMode_ = false;  // toggled by Ctrl; persists across commits until toggled again, Escape, or deactivation

    std::optional<LastCommit> lastCommit_;
};

}  // namespace plnr::tools
