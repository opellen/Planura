#pragma once

#include <optional>

#include "tool.h"

namespace plnr::tools {

// industry-standard click selection: click replaces the selection; Ctrl adds,
// Shift toggles, Ctrl+Shift subtracts. Clicking empty space clears (plain
// click) or no-ops (modifier held). clickCount selects how far the target
// expands: 1 = target, 2 = attached entities, 3 = everything connected.

// Drag-select: a press moving past a few pixels becomes a region drag.
// Left-to-right = window (fully contained only), right-to-left = crossing
// (touching too), matching the reference modeler; SelectMode is fixed at press time.

// Editing context: double-click on an Instance enters its editing context
// instead of expanding the selection; Escape/a miss-click (Replace, no
// hit) exit one level instead of (or alongside) deselecting.
class SelectTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    // MUST clear the preview overlay per the Tool contract; also clears the
    // screen-rect overlay and any in-progress press/drag state.
    void onDeactivate(ToolContext& ctx) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerUp(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

private:
    // Captured in onPointerDown, consumed by onPointerMove (drag threshold)
    // and onPointerUp (click or region path). nullopt when not held.
    struct PressState {
        QPointF pos;                                              // press-time screen position
        events::SelectMode mode{events::SelectMode::Replace};      // modifier-derived, fixed for the whole gesture
        // Press-time pick, for the click path only. Container-aware: a hit
        // inside a group/component Instance resolves to that Instance's id.
        geo::ScenePickResult hit;
        int clickCount{1};                                        // press-time PointerEvent::clickCount
        bool dragging{false};                                     // true once past the move threshold
    };
    std::optional<PressState> press_;
};

}  // namespace plnr::tools
