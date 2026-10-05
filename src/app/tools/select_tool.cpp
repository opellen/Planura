#include "select_tool.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <Qt>

namespace plnr::tools {

namespace {

// Maps a pick hit's kind to the EntityKind used by selection/event
// payloads. Callers must check hit.kind != PickKind::None first.
geo::EntityKind toEntityKind(geo::PickKind kind) {
    switch (kind) {
        case geo::PickKind::Vertex:
            return geo::EntityKind::Vertex;
        case geo::PickKind::Edge:
            return geo::EntityKind::Edge;
        case geo::PickKind::Face:
            return geo::EntityKind::Face;
        case geo::PickKind::None:
            return geo::EntityKind::Face;
    }
    return geo::EntityKind::Face;
}

// Maps a synthesized clickCount to how far the target expands: single =
// just the target, double = attached entities, triple = everything connected.
events::SelectExpand toExpand(int clickCount) {
    switch (clickCount) {
        case 2:
            return events::SelectExpand::Attached;
        case 3:
            return events::SelectExpand::Connected;
        default:
            return events::SelectExpand::None;
    }
}

// Ctrl/Shift -> SelectMode, shared by click and drag paths (captured once
// at press time).
events::SelectMode modeFromModifiers(const PointerEvent& e) {
    if (e.ctrl && e.shift) return events::SelectMode::Subtract;
    if (e.ctrl) return events::SelectMode::Add;
    if (e.shift) return events::SelectMode::Toggle;
    return events::SelectMode::Replace;
}

// Mirrors ToolController's own click-vs-drag tolerance (kClickMoveTolPx).
constexpr qreal kDragThresholdPx = 5.0;

}  // namespace

void SelectTool::onActivate(ToolContext& ctx) {
    ctx.setHint("Select entities. Ctrl = add, Shift = toggle, Ctrl+Shift = subtract.");
}

void SelectTool::onDeactivate(ToolContext& ctx) {
    ctx.setPreview({}, std::nullopt);
    ctx.setScreenRect(std::nullopt, std::nullopt);
    press_.reset();
}

void SelectTool::onPointerDown(ToolContext& ctx, const PointerEvent& e) {
    // Doesn't send requestSelect yet -- onPointerMove decides drag vs. click,
    // and onPointerUp is where either path actually dispatches.
    PressState state;
    state.pos = e.screen;
    state.mode = modeFromModifiers(e);
    state.hit = ctx.pickScene(e, e.tols);  // container-aware
    state.clickCount = e.clickCount;
    state.dragging = false;
    press_ = state;
}

void SelectTool::onPointerMove(ToolContext& ctx, const PointerEvent& e) {
    if (!press_) return;  // hover only (no press in progress) -- nothing to drag

    if (!press_->dragging) {
        const qreal dx = e.screen.x() - press_->pos.x();
        const qreal dy = e.screen.y() - press_->pos.y();
        if (std::sqrt(dx * dx + dy * dy) < kDragThresholdPx) {
            return;  // still within click tolerance -- not a drag yet
        }
        press_->dragging = true;
    }

    ctx.setScreenRect(press_->pos, e.screen);
    ctx.setHint("Release to select (drag right for window, left for crossing).");
}

void SelectTool::onPointerUp(ToolContext& ctx, const PointerEvent& e) {
    if (!press_) return;
    const PressState state = *press_;
    press_.reset();

    if (!state.dragging) {
        // Click path: press-time pick + press-time clickCount (release
        // carries no clickCount of its own). A hit inside a group/component
        // Instance selects that whole Instance instead; it never expands.
        std::optional<events::EntityRef> target;
        events::SelectExpand expand = events::SelectExpand::None;
        if (state.hit.instanceId != geo::kInvalidId) {
            if (state.clickCount == 2) {
                // Double-click on an Instance hit enters its editing context (edit in
                // place) instead of expanding -- an Instance target never expands anyway.
                ctx.requestEnterContext(state.hit.instanceId);
                return;
            }
            target = events::EntityRef{geo::EntityKind::Instance, state.hit.instanceId};
        } else if (state.hit.kind != geo::PickKind::None) {
            target = events::EntityRef{toEntityKind(state.hit.kind), state.hit.id};
            expand = toExpand(state.clickCount);
        } else if (state.mode == events::SelectMode::Replace && !ctx.atRootContext()) {
            // the reference modeler exits one level of context on a miss-click (Replace, no hit)
            // instead of merely deselecting; ExitContextCommand still clears selection.
            ctx.requestExitContext();
            return;
        }
        ctx.requestSelect(state.mode, target, expand);
        return;
    }

    ctx.setScreenRect(std::nullopt, std::nullopt);
    ctx.setHint("Select entities. Ctrl = add, Shift = toggle, Ctrl+Shift = subtract.");

    const double w = std::fabs(e.screen.x() - state.pos.x());
    const double h = std::fabs(e.screen.y() - state.pos.y());
    if (w < 1.0 || h < 1.0) {
        // Zero-area guard: a drag that never opened a rectangle -- treat as
        // nothing happened rather than falling back to the click path.
        return;
    }

    const double minX = std::min(state.pos.x(), e.screen.x());
    const double maxX = std::max(state.pos.x(), e.screen.x());
    const double minY = std::min(state.pos.y(), e.screen.y());
    const double maxY = std::max(state.pos.y(), e.screen.y());

    // Screen-axis-aligned rect spanned by press/release, corners in TL, TR,
    // BR, BL order (Qt's top-left origin: minY is the top).
    const std::array<geo::Ray, 4> corners = {
        ctx.makeRay(QPointF(minX, minY)),
        ctx.makeRay(QPointF(maxX, minY)),
        ctx.makeRay(QPointF(maxX, maxY)),
        ctx.makeRay(QPointF(minX, maxY)),
    };

    // the reference modeler convention: dragging right-to-left (release.x < press.x) is a
    // crossing selection; left-to-right is a window selection.
    const bool crossing = e.screen.x() < state.pos.x();
    ctx.requestSelectRegion(state.mode, corners, crossing);
}

void SelectTool::onKeyDown(ToolContext& ctx, int key, bool /*ctrl*/) {
    if (key != Qt::Key_Escape) return;

    // Escape mid-drag cancels the drag (clears the rubber-band) rather
    // than leaving a stale rect with no press left to clear it.
    if (press_ && press_->dragging) {
        ctx.setScreenRect(std::nullopt, std::nullopt);
    }
    press_.reset();

    ctx.requestSelect(events::SelectMode::Replace, std::nullopt, events::SelectExpand::None);

    // the reference modeler gates context-exit on Escape on an already-empty selection
    // (a separate second Escape); with no live selection query available
    // here, this collapses both steps: Escape always deselects + exits one level.
    if (!ctx.atRootContext()) {
        ctx.requestExitContext();
    }
}

}  // namespace plnr::tools
