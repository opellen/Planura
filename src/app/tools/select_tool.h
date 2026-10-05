#pragma once

#include <optional>

#include "tool.h"

namespace plnr::tools {

// Select: click replaces the selection; Ctrl adds, Shift toggles, Ctrl+Shift subtracts; empty space clears (plain)
// or no-ops (modifier). clickCount expands: 1 = target, 2 = attached, 3 = everything connected. A press moving
// past a few pixels becomes a region drag: left-to-right = window (fully contained), right-to-left = crossing
// (touching too); SelectMode is fixed at press time. Double-click on an Instance enters its editing context;
// Escape / a miss-click (Replace, no hit) exit one level instead of (or with) deselecting.
class SelectTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    // MUST clear the preview overlay (Tool contract); also clears the screen-rect overlay and any press/drag state.
    void onDeactivate(ToolContext& ctx) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerUp(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

private:
    // Captured in onPointerDown, consumed by onPointerMove (drag threshold) and onPointerUp. nullopt when not held.
    struct PressState {
        QPointF pos;                                              // press-time screen position
        events::SelectMode mode{events::SelectMode::Replace};      // modifier-derived, fixed for the whole gesture
        // Press-time pick, click path only; container-aware: a hit inside a group/component resolves to that Instance's id.
        geo::ScenePickResult hit;
        int clickCount{1};                                        // press-time PointerEvent::clickCount
        bool dragging{false};                                     // true once past the move threshold
    };
    std::optional<PressState> press_;
};

}  // namespace plnr::tools
