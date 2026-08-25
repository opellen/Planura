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

// industry-standard Section Plane. Single-shot placement: every hover resolves
// a candidate plane (a hovered face aligns to its own normal; vertex/edge/
// miss fall back to the ground-plane normal); a plain click commits it as a
// new, immediately-active plane (name via promptText).

// Shift freezes the hovered orientation on press (the anchor still slides
// along that plane while held); arrow keys (Up/Right/Left = blue/red/
// green, Down = clear) override the normal and persist until Down/Escape.

// Double-click hit-tests every existing plane's rectangle (nearest wins)
// and toggles its active flag instead of placing; a miss is swallowed.
// hasPressedSinceActivate_/lastPlacedId_ guard the tool-switch-carryover
// and press-1-always-commits clickCount traps.

// Preview is a wireframe rectangle + corner-grip ticks only, half-extent
// sized off the model bbox diagonal; committed planes are owned by
// ui::ViewportPresenter, not this tool. No VCB, no inference cue.
class SectionPlaneTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    // MUST clear the preview overlay per the Tool contract.
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

private:
    // Point/normal + whether resolution found anything real (false only if
    // even the ground-plane ray-intersection fails).
    struct PlaneHit {
        geo::Vec3 point;
        geo::Vec3 normal;
        bool valid{};
    };

    // Resolves the hover candidate plane -- see class comment.
    PlaneHit resolveHover(ToolContext& ctx, const PointerEvent& e) const;

    // Combines resolveHover with shiftLock_/orientationOverride_ (Shift
    // wins) into this move/click's actual candidate plane.
    PlaneHit currentCandidate(ToolContext& ctx, const PointerEvent& e);

    // Two independent actions: (1) if lastPlacedId_ is set, unconditionally
    // remove it (this gesture's press 1 just placed it); (2) toggle the
    // nearest OTHER plane e.ray hits. Returns whether either happened.
    bool tryToggleExisting(ToolContext& ctx, const PointerEvent& e);

    // Commits candidate_ as a new plane and activates it -- see class comment.
    void commit(ToolContext& ctx);

    std::vector<ToolContext::PreviewBatch> buildPreview() const;

    PlaneHit candidate_;  // this move's resolved candidate -- valid gates buildPreview/commit
    // candidate_'s preview half-extent, resolved from the model bbox each
    // onPointerMove; cached since buildPreview() takes no ToolContext.
    double candidatePreviewHalf_ = 0.0;

    std::optional<PlaneHit> shiftLock_;             // frozen orientation while Shift is held -- see class comment
    std::optional<geo::Vec3> orientationOverride_;  // arrow-key normal override -- see class comment

    bool shiftHeld_ = false;  // last-seen PointerEvent::shift, for shiftLock_'s rising-edge capture

    // Guards against an inherited clickCount on this tool's first press
    // since onActivate() -- see class comment. Reset in onActivate/onDeactivate.
    bool hasPressedSinceActivate_ = false;

    // Plane id commit() most recently placed (kInvalidId if none yet this
    // activation) -- see class comment. Reset in onActivate/onDeactivate;
    // cleared once a trusted clickCount >= 2 press consumes it.
    geo::Id lastPlacedId_ = geo::kInvalidId;
};

}  // namespace plnr::tools
