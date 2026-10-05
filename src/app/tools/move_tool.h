#pragma once

#include <optional>
#include <vector>

#include <geo/entity.h>
#include <geo/infer.h>
#include <geo/vec3.h>

#include "agent/events.h"
#include "tool.h"

namespace plnr::tools {

// Move: click grabs a vertex/edge/face, click again (or press-drag past a threshold) commits; Escape cancels.
// Target: endpoint snap to OTHER geometry, else the screen-parallel plane through the grab point (never ground).
// Shift locks the dominant axis; ~12-degree screen-space auto snap also fires. Ctrl toggles copy on a rising
// edge (selection if the grabbed entity is a member, else just it). Post-commit VCB retro-edit (copy array
// count / exact distance) closes on the next commit, Escape or deactivation.
class MoveTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    // A release past the drag threshold commits like the second click; below it keeps waiting.
    void onPointerUp(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;
    void onVcbCommit(ToolContext& ctx, const VcbValue& value) override;

private:
    // A picked-and-held entity: kind/id, grab point (drag anchor), and vertex ids + original positions so the delta stays relative to the pre-grab state.
    struct Grab {
        geo::EntityKind kind{};
        geo::Id id{};
        geo::Vec3 grabPoint;
        std::vector<geo::Id> vertexIds;
        std::vector<geo::Vec3> basePositions;
        // Unit normal of the free-drag plane (camera-facing at grab time, fixed for the drag).
        geo::Vec3 dragPlaneNormal;
        // Screen position of the grabbing press; onPointerUp measures travel against it.
        QPointF pressScreen;
    };

    // VCB retro-edit window for the last committed move; nullopt once closed. wasCopy routes onVcbCommit to Scalar delta-replay vs. Array retype (copy only).
    struct LastCommit {
        geo::EntityKind kind{};
        geo::Id id{};
        geo::Vec3 delta;
        bool wasCopy{};
    };

    // Toggles copyMode_ on a rising edge of ctrlNow.
    void syncCtrl(bool ctrlNow);

    // Refs a copy commit sends to requestTransformEntities: the grabbed entity, or the whole selection if it's a member.
    std::vector<events::EntityRef> copyRefs(ToolContext& ctx) const;

    // resolveTarget's return: pos nullopt when nothing is grabbed or the ray misses the drag plane; axis = signed world axis snapped to (auto/Shift), nullopt for endpoint/free drags.
    struct TargetResolution {
        std::optional<geo::Vec3> pos;
        std::optional<geo::Vec3> axis;
    };

    // Resolves e to the drag target (+ snapped axis, if any) per the class comment.
    TargetResolution resolveTarget(ToolContext& ctx, const PointerEvent& e) const;

    // Commits the grab at e's resolved target and reset()s; shared tail of second click and drag-release. Requires grab_.
    void commitDrag(ToolContext& ctx, const PointerEvent& e);

    // Dotted hover pattern on the face a click would grab while idle (same e.tols pick as the arming click).
    void updateHoverFace(ToolContext& ctx, const PointerEvent& e) const;

    // Resets to idle with hint/preview cleared. Leaves lastCommit_/copyMode_/ctrlHeld_ alone (would erase a just-recorded commit).
    void reset(ToolContext& ctx);

    std::optional<Grab> grab_;

    bool ctrlHeld_ = false;  // last-seen PointerEvent::ctrl, for syncCtrl's edge detection
    bool copyMode_ = false;  // toggled by Ctrl; persists across commits until toggled again, Escape, or deactivation

    std::optional<LastCommit> lastCommit_;
};

}  // namespace plnr::tools
