#pragma once

#include <optional>
#include <string>

#include <geo/entity.h>
#include <geo/model.h>
#include <geo/pick.h>

#include "agent/events.h"
#include "tool.h"

namespace plnr::tools {

// industry-standard Text tool. Stateless per click: face click commits leader
// text immediately (computed area, no prompt -- a modal would block a
// would-be 2nd press). Vertex/edge click prompts (default: length/coords)
// then adds leader text; cancelled prompt is a no-op.

// Empty-space click prompts, then adds screen text at the exact widget
// pixel (fixed, never re-anchored). Deferred: leader-style choice,
// arrow-style options, inline text editing after placement.
class TextTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;

private:
    // Per-kind default text (see class comment) -- empty string for
    // PickKind::None (never reached; callers branch on hit.kind first).
    static std::string defaultTextFor(const geo::Model& model, const geo::PickResult& hit);
};

}  // namespace plnr::tools
