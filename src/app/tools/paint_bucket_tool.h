#pragma once

#include "tool.h"

namespace plnr::tools {

// Picking is container-aware (ToolContext::pickScene, face-only
// tolerances): a hit inside a group/component Instance resolves to that
// top-level instance; loose geometry resolves to the Face. No vertex/edge picking.

// Modifier scopes (Alt wins outright): Alt samples the target's own front
// material (no mutation); none paints just the target; Shift paints every
// Face whose front material matches; Ctrl paints the target's connected-
// component face set (geo::connectedComponent, OUTWARD -- not closureOf);
// Shift+Ctrl approximates "same object" as the same connected-component set.
// An Instance target never expands (no face-closure concept for a container).

// Shift/Ctrl/Shift+Ctrl are root-context only -- inside an entered group/
// component they degenerate to "target only" rather than risk enumerating
// the wrong Model's face ids. A plain click / Alt-sample are unaffected.
class PaintBucketTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
};

}  // namespace plnr::tools
