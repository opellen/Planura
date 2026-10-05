#pragma once

#include "tool.h"

namespace plnr::tools {

// Paint Bucket: one committing click paints or samples by modifier. Picking is container-aware (pickScene, face-only
// tolerances): a hit in a group/component Instance resolves to the top-level instance, loose geometry to the Face.
// Modifiers (Alt wins): Alt samples the target's front material; none paints the target; Shift paints every Face
// with a matching front material; Ctrl the connected-component face set (geo::connectedComponent, OUTWARD, not
// closureOf); Shift+Ctrl approximates "same object" as that set. An Instance target never expands. Shift/Ctrl
// are root-context only: inside an entered group they degrade to "target only"; plain click/Alt are unaffected.
class PaintBucketTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
};

}  // namespace plnr::tools
