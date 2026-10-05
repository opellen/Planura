#pragma once

#include <unordered_set>

#include <geo/model.h>

#include "tool.h"

namespace plnr::tools {

// Eraser: click an edge to remove it (dissolving faces that reference it), or drag across edges to remove each one crossed. No preview overlay.
class EraserTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerUp(ToolContext& ctx, const PointerEvent& e) override;

private:
    // Picks an edge under e (vertex/face picking off via zero vertexTol); if not yet erased this gesture, requestRemoveEdge + record it.
    void eraseEdgeUnder(ToolContext& ctx, const PointerEvent& e);

    bool pressing_ = false;
    std::unordered_set<geo::Id> erasedThisGesture_;
};

}  // namespace plnr::tools
