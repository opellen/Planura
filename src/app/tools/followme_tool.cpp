#include "followme_tool.h"

#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <Qt>

#include <geo/model.h>

#include "tool.h"

namespace plnr::tools {

std::vector<geo::Vec3> FollowMeTool::facePoints(const geo::Model& model, geo::Id faceId) {
    const std::vector<geo::Id> loopIds = model.faceVertexLoop(faceId);
    std::vector<geo::Vec3> points;
    points.reserve(loopIds.size());
    for (geo::Id vId : loopIds) {
        const geo::Vertex* v = model.vertex(vId);
        if (!v) return {};
        points.push_back(v->pos);
    }
    return points;
}

std::optional<FollowMeTool::PathData> FollowMeTool::tryBuildSelectedPath(ToolContext& ctx) const {
    const std::vector<events::EntityRef>& sel = ctx.selection();
    if (sel.size() < 2) return std::nullopt;
    for (const events::EntityRef& ref : sel) {
        if (ref.kind != geo::EntityKind::Edge) return std::nullopt;
    }

    const geo::Model* model = ctx.model();
    if (!model) return std::nullopt;

    // vertex id -> [(other endpoint id, edge id)], built ONLY from the
    // selected edges (same adjacency-subgraph construction as OffsetTool's
    // tryBuildSelectedChain).
    std::unordered_map<geo::Id, std::vector<std::pair<geo::Id, geo::Id>>> adj;
    std::unordered_set<geo::Id> edgeIds;
    for (const events::EntityRef& ref : sel) {
        const geo::Edge* edge = model->edge(ref.id);
        if (!edge) return std::nullopt;
        const geo::HalfEdge* h0 = model->halfEdge(edge->halfEdges[0]);
        const geo::HalfEdge* h1 = model->halfEdge(edge->halfEdges[1]);
        if (!h0 || !h1) return std::nullopt;
        const geo::Id vA = h0->origin;
        const geo::Id vB = h1->origin;
        if (vA == vB) return std::nullopt;  // degenerate edge -- shouldn't happen, defensive
        adj[vA].push_back({vB, ref.id});
        adj[vB].push_back({vA, ref.id});
        edgeIds.insert(ref.id);
    }

    // Every vertex touched by the selected edges must have degree 1 (a
    // path endpoint) or 2 (interior); degree >= 3 is an ambiguous branch --
    // reject the whole selection rather than guess an ordering.
    geo::Id start = geo::kInvalidId;
    int degree1Count = 0;
    for (const auto& entry : adj) {
        const std::size_t degree = entry.second.size();
        if (degree >= 3) return std::nullopt;
        if (degree == 1) {
            ++degree1Count;
            start = entry.first;
        }
    }

    bool closed = false;
    if (degree1Count == 0) {
        closed = true;                // every vertex degree 2 -- a single cycle, IF connected (checked below)
        start = adj.begin()->first;   // any vertex on the cycle works as a start
    } else if (degree1Count == 2) {
        closed = false;               // exactly two endpoints -- a single simple path, IF connected (checked below)
    } else {
        return std::nullopt;          // more than 2 degree-1 vertices -- multiple disjoint pieces
    }

    // Walk the subgraph from start, always taking the one unused/non-
    // backtracking edge at the current vertex -- same walk as OffsetTool's
    // own tryBuildSelectedChain.
    std::vector<geo::Id> orderedVertices{start};
    std::unordered_set<geo::Id> usedEdges;
    geo::Id current = start;
    geo::Id prevEdge = geo::kInvalidId;
    while (usedEdges.size() < edgeIds.size()) {
        geo::Id nextVertex = geo::kInvalidId;
        geo::Id nextEdge = geo::kInvalidId;
        for (const auto& [v, eId] : adj.at(current)) {
            if (eId == prevEdge || usedEdges.count(eId) != 0) continue;
            nextVertex = v;
            nextEdge = eId;
            break;
        }
        if (nextEdge == geo::kInvalidId) return std::nullopt;
        usedEdges.insert(nextEdge);
        prevEdge = nextEdge;
        current = nextVertex;
        if (closed && current == start) break;  // cycle closed -- don't repeat start at the end
        orderedVertices.push_back(current);
    }
    if (usedEdges.size() != edgeIds.size()) return std::nullopt;  // didn't cover every selected edge

    std::vector<geo::Vec3> points;
    points.reserve(orderedVertices.size());
    for (geo::Id vId : orderedVertices) {
        const geo::Vertex* v = model->vertex(vId);
        if (!v) return std::nullopt;
        points.push_back(v->pos);
    }
    if (points.size() < (closed ? 3u : 2u)) return std::nullopt;

    // Deliberately NO coplanarity pass here -- a Follow Me path is
    // legitimately 3D (see the class comment).
    return PathData{std::move(points), closed};
}

void FollowMeTool::refresh(ToolContext& ctx, const PointerEvent* e) {
    currentPath_ = tryBuildSelectedPath(ctx);
    hoveredFaceId_ = geo::kInvalidId;

    if (!currentPath_) {
        ctx.setPreview({}, std::nullopt);
        ctx.setHint(kNoPathHint);
        return;
    }

    std::vector<ToolContext::PreviewBatch> batches;

    // Path polyline, in red (kAxisRedColor) -- see the class comment for the
    // unverified-vs-real-the reference modeler caveat.
    std::vector<float> pathVerts;
    const std::vector<geo::Vec3>& pts = currentPath_->points;
    const std::size_t segCount = currentPath_->closed ? pts.size() : pts.size() - 1;
    pathVerts.reserve(segCount * 6);
    for (std::size_t i = 0; i < segCount; ++i) {
        const geo::Vec3& a = pts[i];
        const geo::Vec3& b = pts[(i + 1) % pts.size()];
        pathVerts.push_back(static_cast<float>(a.x));
        pathVerts.push_back(static_cast<float>(a.y));
        pathVerts.push_back(static_cast<float>(a.z));
        pathVerts.push_back(static_cast<float>(b.x));
        pathVerts.push_back(static_cast<float>(b.y));
        pathVerts.push_back(static_cast<float>(b.z));
    }
    batches.push_back(ToolContext::PreviewBatch{std::move(pathVerts), kAxisRedColor.r, kAxisRedColor.g,
                                                  kAxisRedColor.b, kAxisRedColor.a});

    // Hovered face outline, default color -- only when e is present (skipped
    // on onActivate's no-PointerEvent call) and it actually hits a face.
    if (e) {
        const geo::PickResult hit = ctx.pick(*e, geo::PickOptions{0.0, 0.0});
        const geo::Model* model = ctx.model();
        if (hit.kind == geo::PickKind::Face && model) {
            const std::vector<geo::Vec3> face = facePoints(*model, hit.id);
            if (!face.empty()) {
                hoveredFaceId_ = hit.id;
                std::vector<float> faceVerts;
                faceVerts.reserve(face.size() * 6);
                for (std::size_t i = 0; i < face.size(); ++i) {
                    const geo::Vec3& a = face[i];
                    const geo::Vec3& b = face[(i + 1) % face.size()];
                    faceVerts.push_back(static_cast<float>(a.x));
                    faceVerts.push_back(static_cast<float>(a.y));
                    faceVerts.push_back(static_cast<float>(a.z));
                    faceVerts.push_back(static_cast<float>(b.x));
                    faceVerts.push_back(static_cast<float>(b.y));
                    faceVerts.push_back(static_cast<float>(b.z));
                }
                batches.push_back(ToolContext::PreviewBatch{std::move(faceVerts), kDefaultPreviewColor.r,
                                                              kDefaultPreviewColor.g, kDefaultPreviewColor.b,
                                                              kDefaultPreviewColor.a});
            }
        }
    }

    ctx.setPreviewBatches(std::move(batches), std::nullopt);
    ctx.setHint(kReadyHint);
}

void FollowMeTool::onActivate(ToolContext& ctx) {
    refresh(ctx, nullptr);
}

void FollowMeTool::onDeactivate(ToolContext& ctx) {
    currentPath_.reset();
    hoveredFaceId_ = geo::kInvalidId;
    ctx.setPreview({}, std::nullopt);
}

void FollowMeTool::onPointerMove(ToolContext& ctx, const PointerEvent& e) {
    refresh(ctx, &e);
}

void FollowMeTool::onPointerDown(ToolContext& ctx, const PointerEvent& e) {
    if (!currentPath_) return;  // no qualifying path armed -- nothing a click could commit

    const geo::PickResult hit = ctx.pick(e, geo::PickOptions{0.0, 0.0});
    if (hit.kind != geo::PickKind::Face) return;

    ctx.requestFollowMe(hit.id, currentPath_->points, currentPath_->closed);
    ctx.setHint(kSweptHint);
    // Deliberately does NOT clear currentPath_/the selection -- the tool
    // stays active with the same path armed, so the next pointer move
    // re-derives and re-previews it exactly as before.
}

void FollowMeTool::onKeyDown(ToolContext& ctx, int key, bool /*ctrl*/) {
    if (key != Qt::Key_Escape) return;
    // Non-destructive -- see the class comment's Escape note.
    refresh(ctx, nullptr);
}

}  // namespace plnr::tools
