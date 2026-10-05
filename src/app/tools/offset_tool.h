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

// Offset: click a face (or arm a preselected coplanar edge chain), drag to preview an offset polyline at the signed
// distance given by the cursor's side of the source loop, click to commit. VCB retype ("Distance", live "~ <value>") rides
// the requestReplaceLastPolyline window. A clickCount==2 press with an open lastOffset_ re-offsets the face under the
// cursor by the same distance. Alt toggles keepOverlaps (default false; altHeld_, as the VCB path has no PointerEvent).
// Escape cancels drag + window. Hints UNVERIFIED.
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

    // Source loop/chain being offset: a face's loop (closed) or a preselected coplanar edge chain (open or closed); fixed for one drag.
    struct Arm {
        std::vector<geo::Vec3> points;
        geo::Vec3 planeNormal;
        bool closed{};
    };

    // VCB retro-edit window + double-click-repeat source: the last offset's loop/plane/closed-ness/distance. nullopt once closed.
    struct LastOffset {
        std::vector<geo::Vec3> sourcePoints;
        geo::Vec3 planeNormal;
        bool closed{};
        double distance{};
    };

    // Validates ctx.selection() as an offsettable chain: 2+ Edge refs forming one connected simple path/cycle, planar within
    // geo::kPlaneTol. A vertex touched by 3+ selected edges is a branch and rejects the selection. nullopt on failure.
    std::optional<Arm> tryBuildSelectedChain(ToolContext& ctx) const;

    // Signed in-plane distance of the cursor from the loop's nearest segment; positive = cursor's side (geo::offsetLoop's shiftDir).
    double signedDistanceForCursor(const Arm& arm, const geo::Vec3& cursor) const;

    // Preview line-list for points: a closed loop when closed, else an open polyline.
    std::vector<float> previewVerts(const std::vector<geo::Vec3>& points, bool closed) const;

    // Resolves e.ray against arm_'s plane (through points[0], normal planeNormal); nullopt when parallel (caller keeps lastCursor_).
    std::optional<geo::Vec3> resolveOnArmedPlane(const PointerEvent& e) const;

    // Idle hover: previews the selected chain if it qualifies, else a hovered face's loop, else clears. Updates hint/VCB.
    void updateIdlePreview(ToolContext& ctx, const PointerEvent& e);

    // Commits arm_ at distance, records lastOffset_, returns to Idle, true. False (stays armed, no change) if arm_ is unset or geo::offsetLoop rejects it.
    bool commit(ToolContext& ctx, double distance);

    // Returns to Idle: clears arm_/lastCursor_/preview, refreshes hint/VCB. Leaves lastOffset_ (only onActivate/onDeactivate/Escape/a fresh arm clear it).
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
