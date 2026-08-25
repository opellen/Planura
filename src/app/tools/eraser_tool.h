#pragma once

#include <unordered_set>

#include <geo/model.h>

#include "tool.h"

namespace plnr::tools {

// industry-standard eraser: click an edge to remove it (dissolving any face
// that references it), or press-and-drag across several edges to remove
// each one the pointer crosses while the button stays held. No preview
// overlay is used -- the model change itself is the feedback.
class EraserTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerUp(ToolContext& ctx, const PointerEvent& e) override;

private:
    // Picks an edge under e (vertex/face picking disabled via zero
    // vertexTol) and, if it hasn't already been erased this gesture, sends
    // requestRemoveEdge and records it in erasedThisGesture_.
    void eraseEdgeUnder(ToolContext& ctx, const PointerEvent& e);

    bool pressing_ = false;
    std::unordered_set<geo::Id> erasedThisGesture_;
};

}  // namespace plnr::tools
