#pragma once

#include <optional>
#include <vector>

#include <QPointF>

#include <geo/entity.h>
#include <geo/pick.h>
#include <geo/vec3.h>

#include "agent/events.h"
#include "tool.h"

namespace plnr::tools {

// Follow Me: sweeps a clicked profile face along a path -- the preselected edge chain, or a manual drag
// that touches path edges (chain red; release after a drag, or a second click, commits). Path = connected
// simple chain/cycle of 2+ edges; 3D is fine. Re-derived on every pointer event; stays active after a
// sweep. Drag chain grows by adjacency, re-touching an edge truncates to it, the profile's own edges are
// ignored; Escape clears the drag only. Hints/colors UNVERIFIED.
class FollowMeTool : public Tool {
public:
    void onActivate(ToolContext& ctx) override;
    void onDeactivate(ToolContext& ctx) override;
    void onPointerMove(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerDown(ToolContext& ctx, const PointerEvent& e) override;
    void onPointerUp(ToolContext& ctx, const PointerEvent& e) override;
    void onKeyDown(ToolContext& ctx, int key, bool ctrl) override;

private:
    // Ordered path this activation would sweep along; points = ordered vertices (last NOT repeated when closed).
    struct PathData {
        std::vector<geo::Vec3> points;
        bool closed{};
    };

    // Validates ctx.selection() as a qualifying path; nullopt on failure.
    std::optional<PathData> tryBuildSelectedPath(ToolContext& ctx) const;

    // A face's vertex loop as world points; empty if faceId is unknown or the loop is inconsistent.
    static std::vector<geo::Vec3> facePoints(const geo::Model& model, geo::Id faceId);

    // Rebuilds currentPath_/hoveredFaceId_ from ctx.selection() and e's pick, then the preview + hint. Shared by onActivate (e null) and onPointerMove.
    void refresh(ToolContext& ctx, const PointerEvent* e);

    // Manual-drag state: armed by a face press with no qualifying preselection; verts.size() == edges.size() + 1.
    struct Drag {
        geo::Id profileFaceId = geo::kInvalidId;
        std::vector<geo::Id> profileVerts;  // the profile's vertex loop -- its own edges are not touchable
        geo::Vec3 profileCentroid;
        QPointF pressScreen;
        std::vector<geo::Id> verts;
        std::vector<geo::Id> edges;
    };

    // Grows/truncates drag_->verts/edges for a touched edge (see the class comment).
    void touchEdge(const geo::Model& model, geo::Id edgeId);

    // Picks the edge under e (profile boundary excluded), updates the chain, then the preview.
    void updateDrag(ToolContext& ctx, const PointerEvent& e);

    // Red chain polyline + profile outline, kDragHint.
    void refreshDrag(ToolContext& ctx);

    // Sweeps the profile along the touched chain (open path) and returns to Idle.
    // False (state untouched) on an empty chain.
    bool commitDrag(ToolContext& ctx);

    // Drops the drag state and re-derives the idle preview/hint.
    void resetDrag(ToolContext& ctx);

    // UNVERIFIED wording.
    static constexpr const char* kDragHint = "Drag along the path; click to finish. Esc to start over.";
    static constexpr const char* kNoPathHint = "Select the path edges first, then click the profile face.";
    static constexpr const char* kReadyHint = "Click the profile face to sweep it along the selected path.";
    static constexpr const char* kSweptHint = "Follow Me applied. Select a new path to sweep another face.";

    std::optional<PathData> currentPath_;
    std::optional<Drag> drag_;
    geo::Id hoveredFaceId_ = geo::kInvalidId;
};

}  // namespace plnr::tools
