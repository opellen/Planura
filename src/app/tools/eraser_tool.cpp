#include "eraser_tool.h"

namespace plnr::tools {

void EraserTool::onActivate(ToolContext& ctx) {
    pressing_ = false;
    erasedThisGesture_.clear();
    ctx.setHint("Click or drag over entities to erase.");
}

void EraserTool::onDeactivate(ToolContext& ctx) {
    pressing_ = false;
    erasedThisGesture_.clear();
    ctx.setPreview({}, std::nullopt);
}

void EraserTool::eraseEdgeUnder(ToolContext& ctx, const PointerEvent& e) {
    // Vertex/face picking disabled (0 tolerance) -- the eraser only ever
    // targets edges.
    const geo::PickResult hit = ctx.pick(e, geo::PickOptions{0.0, e.tols.edgeTol});
    if (hit.kind != geo::PickKind::Edge) return;
    if (erasedThisGesture_.count(hit.id) > 0) return;

    ctx.requestRemoveEdge(hit.id);
    erasedThisGesture_.insert(hit.id);
}

void EraserTool::onPointerDown(ToolContext& ctx, const PointerEvent& e) {
    pressing_ = true;
    erasedThisGesture_.clear();
    eraseEdgeUnder(ctx, e);
}

void EraserTool::onPointerMove(ToolContext& ctx, const PointerEvent& e) {
    if (!pressing_) return;
    eraseEdgeUnder(ctx, e);
}

void EraserTool::onPointerUp(ToolContext&, const PointerEvent&) {
    pressing_ = false;
}

}  // namespace plnr::tools
