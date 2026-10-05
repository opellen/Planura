#pragma once

#include <optional>
#include <string>

#include <geo/entity.h>
#include <geo/model.h>
#include <geo/pick.h>

#include "agent/events.h"
#include "tool.h"

namespace plnr::tools {

// Text: stateless per click. A face click commits leader text at once (computed area, no prompt: a modal would block a
// second press). Vertex/edge click prompts (default length/coords), then adds leader text; cancel is a no-op.
// Empty space prompts, then adds screen text at the exact widget pixel (fixed). Not implemented: leader/arrow
// style options, inline editing after placement.
class TextTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;

private:
    // Per-kind default text; empty for PickKind::None (callers branch on hit.kind first).
    static std::string defaultTextFor(const geo::Model& model, const geo::PickResult& hit);
};

}  // namespace plnr::tools
