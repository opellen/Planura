#pragma once

#include <unordered_map>

#include "tool.h"

namespace plnr::tools {

// Solid Tools (Union, Outer Shell, Subtract, Trim, Intersect, Split): ONE class parameterized by op, one ToolId each;
// only the hint text and post-2nd-click behavior differ. Hover shows "Solid Group"/"Solid Component" for a solid root
// Instance, else "Not a solid"; a total miss leaves the stage hint. Subtract/Trim: 2 clicks (cutter, target), apply, reset.
// Union/OuterShell/Intersect apply on click 2 and fold further clicks onto the result; Split applies then deactivates
// to Select. A miss is ignored; Escape resets. A rejected op's reason (StatusHintChanged) is not overwritten.
// solidCache_ avoids re-walking isDefinitionSolid (O(model)) per hover; cleared on activate and every click.
class SolidTool : public Tool {
public:
    explicit SolidTool(events::SolidOp op);

    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

private:
    // Classification of a scene pick against the solid cache; miss/non-Instance/non-solid all give solid:false.
    struct Classification {
        bool solid{};
        bool isGroup{};
    };
    Classification classify(ToolContext& ctx, const geo::ScenePickResult& hit);
    bool cachedIsDefinitionSolid(ToolContext& ctx, geo::Id definitionId);

    events::SolidOp op_;

    // kInvalidId until a click arms the first operand; then the cutter (Subtract/Trim), running fold result, or first pick (Split).
    geo::Id firstOperand_ = geo::kInvalidId;

    // The stage's instructional hint, re-asserted by onPointerMove when the cursor isn't over anything.
    std::string basePrompt_;

    std::unordered_map<geo::Id, bool> solidCache_;  // definitionId -> solid
};

}  // namespace plnr::tools
