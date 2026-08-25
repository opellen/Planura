#pragma once

#include <unordered_map>

#include "tool.h"

namespace plnr::tools {

// the reference modeler's six Solid Tools (Union, Outer Shell, Subtract, Trim, Intersect,
// Split) driven by ONE class parameterized by op, each registered under
// its own ToolId; only the hint text and post-2nd-click behavior differ.

// Hover: a solid root Instance shows "Solid Group"/"Solid Component"; any
// other hit shows "Not a solid". A total miss leaves the current stage
// hint untouched.

// Click protocol: Subtract/Trim take 2 clicks (cutter, target), apply, and
// reset to stage 1, staying active. Union/OuterShell/Intersect apply on
// click 2 and keep folding further clicks onto the result. Split applies
// on click 2 then deactivates to Select. A miss is ignored (no state
// advance); Escape resets to stage 1.

// A rejected op reports its own reason via StatusHintChanged; this tool
// does not re-assert its own hint right after, so that reason stays visible.

// solidCache_ avoids re-walking isDefinitionSolid (O(model)) every hover
// frame; cleared on activate and every click.
class SolidTool : public Tool {
public:
    explicit SolidTool(events::SolidOp op);

    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

private:
    // Result of classifying a scene pick against the solid cache -- a
    // miss/non-Instance/non-solid hit all collapse to solid:false.
    struct Classification {
        bool solid{};
        bool isGroup{};
    };
    Classification classify(ToolContext& ctx, const geo::ScenePickResult& hit);
    bool cachedIsDefinitionSolid(ToolContext& ctx, geo::Id definitionId);

    events::SolidOp op_;

    // kInvalidId when no click has armed a first operand yet. Otherwise the
    // cutter (Subtract/Trim), running fold result, or first pick (Split).
    geo::Id firstOperand_ = geo::kInvalidId;

    // The current click stage's own instructional hint -- re-asserted by
    // onPointerMove whenever the cursor isn't over anything.
    std::string basePrompt_;

    std::unordered_map<geo::Id, bool> solidCache_;  // definitionId -> solid
};

}  // namespace plnr::tools
