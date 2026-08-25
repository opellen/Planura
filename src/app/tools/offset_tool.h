#pragma once

#include <optional>
#include <vector>

#include <geo/entity.h>
#include <geo/offset.h>
#include <geo/pick.h>
#include <geo/vec3.h>

#include "agent/events.h"
#include "tool.h"

namespace plnr::tools {

// industry-standard Offset: click a face (or a preselected coplanar edge
// chain) to arm it, drag to preview an offset polyline at the signed
// distance implied by the cursor's side of the source loop, click again to
// commit. VCB retype rides the existing requestReplaceLastPolyline window.

// Double-click repeat: a clickCount==2 press with an open lastOffset_
// window re-offsets the face under the cursor by the same distance instead
// of arming a drag; checked before normal stage dispatch in onPointerDown.

// Alt toggles keepOverlaps (default false), tracked in altHeld_ since the
// VCB retro-edit path has no PointerEvent. Escape cancels drag + window.

// VCB: label "Distance", live "~ <value>". Hints are unverified against the reference modeler.
class OffsetTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;
    void onVcbCommit(ToolContext& ctx, const VcbValue& value) override;

private:
    enum class Stage { Idle, Dragging };

    // The source loop/chain being offset: a face's vertex loop (closed) or
    // a preselected coplanar edge chain (open or closed), fixed for one drag.
    struct Arm {
        std::vector<geo::Vec3> points;
        geo::Vec3 planeNormal;
        bool closed{};
    };

    // VCB retro-edit window + double-click-repeat source: the last committed
    // offset's loop/plane/closed-ness/distance. nullopt once closed.
    struct LastOffset {
        std::vector<geo::Vec3> sourcePoints;
        geo::Vec3 planeNormal;
        bool closed{};
        double distance{};
    };

    // Validates ctx.selection() as an offsettable chain: 2+ Edge refs
    // forming a single connected simple path or cycle, planar within
    // geo::kPlaneTol. A vertex touched by 3+ selected edges is an ambiguous
    // branch and rejects the whole selection. nullopt on any failure.
    std::optional<Arm> tryBuildSelectedChain(ToolContext& ctx) const;

    // Signed in-plane distance of cursor from the loop's nearest segment;
    // matches geo::offsetLoop's own shiftDir convention (positive = cursor's side).
    double signedDistanceForCursor(const Arm& arm, const geo::Vec3& cursor) const;

    // Builds the preview line-list for points: a closed loop (loopVerts
    // style) when closed, a plain open polyline otherwise.
    std::vector<float> previewVerts(const std::vector<geo::Vec3>& points, bool closed) const;

    // Resolves e.ray against arm_'s plane (through points[0], normal
    // planeNormal); nullopt when parallel (caller keeps lastCursor_'s prior value).
    std::optional<geo::Vec3> resolveOnArmedPlane(const PointerEvent& e) const;

    // Idle-stage hover: previews the selected chain if it qualifies, else a
    // hovered face's own vertex loop, else clears. Also updates hint/VCB.
    void updateIdlePreview(ToolContext& ctx, const PointerEvent& e);

    // Commits arm_ offset at distance, records lastOffset_, returns to Idle,
    // and returns true. Returns false (stays armed, no state change) if
    // arm_ is unset or geo::offsetLoop rejects it -- callers decide what to tell the user.
    bool commit(ToolContext& ctx, double distance);

    // Returns to Idle: clears arm_/lastCursor_/preview, refreshes hint/VCB.
    // Does not touch lastOffset_ -- only onActivate/onDeactivate/Escape/a fresh arm do that.
    void reset(ToolContext& ctx);

    void updateVcb(ToolContext& ctx, std::optional<double> liveDistance) const;

    static constexpr const char* kActivationHint = "Select a face or a set of connected coplanar edges to offset.";
    static constexpr const char* kDraggingHint = "Click to set the offset distance, or type a value.";

    Stage stage_ = Stage::Idle;
    bool altHeld_{};                       // Alt modifier state, updated on every pointer event
    std::optional<Arm> arm_;
    std::optional<geo::Vec3> lastCursor_;  // most recent on-plane cursor point, Dragging only
    double lastDistance_ = 0.0;            // most recent signed distance, Dragging only -- VCB sign source

    std::optional<LastOffset> lastOffset_;
};

}  // namespace plnr::tools
