#pragma once

#include <optional>
#include <string>
#include <vector>

#include <geo/entity.h>
#include <geo/pick.h>
#include <geo/vec3.h>

#include "agent/events.h"
#include "tool.h"

namespace plnr::tools {

// Section Plane: single-shot placement. Hover resolves a candidate plane (a face uses its normal; vertex/edge/miss use the
// ground normal); a plain click commits it as a new active plane (name via promptText). Shift freezes the hovered
// orientation on press; arrows (Up/Right/Left = blue/red/green, Down = clear) override the normal until Down/Escape.
// Double-click toggles the nearest existing plane (a miss is swallowed); hasPressedSinceActivate_/lastPlacedId_ guard the
// clickCount traps. Preview: wireframe rectangle + corner ticks; no VCB, no inference cue.
class SectionPlaneTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    // MUST clear the preview overlay (Tool contract).
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

private:
    // Point/normal + whether resolution found anything real (false only if even the ground-plane intersection fails).
    struct PlaneHit {
        geo::Vec3 point;
        geo::Vec3 normal;
        bool valid{};
    };

    // Resolves the hover candidate plane (class comment).
    PlaneHit resolveHover(ToolContext& ctx, const PointerEvent& e) const;

    // resolveHover combined with shiftLock_/orientationOverride_ (Shift wins) into this move/click's candidate.
    PlaneHit currentCandidate(ToolContext& ctx, const PointerEvent& e);

    // Two independent actions: (1) if lastPlacedId_ is set, remove it (press 1 of this gesture placed it); (2) toggle the nearest OTHER plane e.ray hits. Returns whether either happened.
    bool tryToggleExisting(ToolContext& ctx, const PointerEvent& e);

    // Commits candidate_ as a new active plane.
    void commit(ToolContext& ctx);

    std::vector<ToolContext::PreviewBatch> buildPreview() const;

    PlaneHit candidate_;  // this move's resolved candidate -- valid gates buildPreview/commit
    // candidate_'s preview half-extent from the model bbox, resolved each onPointerMove (buildPreview() has no ToolContext).
    double candidatePreviewHalf_ = 0.0;

    std::optional<PlaneHit> shiftLock_;             // frozen orientation while Shift is held -- see class comment
    std::optional<geo::Vec3> orientationOverride_;  // arrow-key normal override -- see class comment

    bool shiftHeld_ = false;  // last-seen PointerEvent::shift, for shiftLock_'s rising-edge capture

    // Guards an inherited clickCount on this tool's first press since onActivate(). Reset in onActivate/onDeactivate.
    bool hasPressedSinceActivate_ = false;

    // Plane id commit() last placed (kInvalidId if none this activation). Reset in onActivate/onDeactivate; cleared once a trusted clickCount >= 2 press consumes it.
    geo::Id lastPlacedId_ = geo::kInvalidId;
};

}  // namespace plnr::tools
