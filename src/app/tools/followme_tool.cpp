#include "followme_tool.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <Qt>

#include <geo/model.h>

#include "tool.h"

namespace plnr::tools {

namespace {

// Pointer travel (screen pixels) beyond which a release is a drag-commit rather
// than the first click of click-move-click -- same value as PushPullTool's.
constexpr double kDragThresholdPx = 5.0;

void pushSegment(std::vector<float>& verts, const geo::Vec3& a, const geo::Vec3& b) {
    verts.push_back(static_cast<float>(a.x));
    verts.push_back(static_cast<float>(a.y));
    verts.push_back(static_cast<float>(a.z));
    verts.push_back(static_cast<float>(b.x));
    verts.push_back(static_cast<float>(b.y));
    verts.push_back(static_cast<float>(b.z));
}

// An edge's endpoint vertex ids, or nullopt if the edge/half-edges are missing.
std::optional<std::pair<geo::Id, geo::Id>> edgeEnds(const geo::Model& model, geo::Id edgeId) {
    const geo::Edge* edge = model.edge(edgeId);
    if (!edge) return std::nullopt;
    const geo::HalfEdge* h0 = model.halfEdge(edge->halfEdges[0]);
    const geo::HalfEdge* h1 = model.halfEdge(edge->halfEdges[1]);
    if (!h0 || !h1) return std::nullopt;
    return std::make_pair(h0->origin, h1->origin);
}

}  // namespace

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
    drag_.reset();
    hoveredFaceId_ = geo::kInvalidId;
    ctx.setPreview({}, std::nullopt);
}

void FollowMeTool::touchEdge(const geo::Model& model, geo::Id edgeId) {
    Drag& d = *drag_;
    const auto it = std::find(d.edges.begin(), d.edges.end(), edgeId);
    if (it != d.edges.end()) {
        // Retreat along the path: keep the touched edge, drop everything after it.
        const std::size_t keep = static_cast<std::size_t>(it - d.edges.begin()) + 1;
        d.edges.resize(keep);
        d.verts.resize(keep + 1);
        return;
    }
    const auto ends = edgeEnds(model, edgeId);
    if (!ends) return;

    if (d.edges.empty()) {
        // The first touched edge starts the chain (docs: extrusion starts there even when it does not
        // touch the profile). One edge has no direction yet, so start at the end nearer the profile.
        const geo::Vertex* a = model.vertex(ends->first);
        const geo::Vertex* b = model.vertex(ends->second);
        if (!a || !b) return;
        const bool aNearer = geo::length(a->pos - d.profileCentroid) <= geo::length(b->pos - d.profileCentroid);
        d.verts = aNearer ? std::vector<geo::Id>{ends->first, ends->second}
                          : std::vector<geo::Id>{ends->second, ends->first};
        d.edges = {edgeId};
        return;
    }

    auto touchesTip = [&] { return ends->first == d.verts.back() || ends->second == d.verts.back(); };
    if (!touchesTip() && d.edges.size() == 1 && (ends->first == d.verts.front() || ends->second == d.verts.front())) {
        std::reverse(d.verts.begin(), d.verts.end());  // a lone edge has no committed direction yet
    }
    if (!touchesTip()) return;  // not adjacent to the chain tip -- ignored

    const geo::Id next = ends->first == d.verts.back() ? ends->second : ends->first;
    if (std::find(d.verts.begin(), d.verts.end(), next) != d.verts.end()) return;  // would revisit a chain vertex
    d.edges.push_back(edgeId);
    d.verts.push_back(next);
}

void FollowMeTool::updateDrag(ToolContext& ctx, const PointerEvent& e) {
    const geo::Model* model = ctx.model();
    if (!model) return;
    geo::PickOptions opts{0.0, e.tols.edgeTol};
    const std::vector<geo::Id>& profileVerts = drag_->profileVerts;
    // Edges lying on the profile itself are never path candidates (the pointer leaves the face over them).
    opts.filter = [model, &profileVerts](geo::EntityKind kind, geo::Id id) {
        if (kind != geo::EntityKind::Edge) return true;
        const auto ends = edgeEnds(*model, id);
        if (!ends) return true;
        const auto on = [&](geo::Id v) {
            return std::find(profileVerts.begin(), profileVerts.end(), v) != profileVerts.end();
        };
        return !(on(ends->first) && on(ends->second));
    };
    const geo::PickResult hit = ctx.pick(e, opts);
    if (hit.kind == geo::PickKind::Edge) touchEdge(*model, hit.id);
    refreshDrag(ctx);
}

void FollowMeTool::refreshDrag(ToolContext& ctx) {
    const geo::Model* model = ctx.model();
    if (!model || !drag_) return;
    std::vector<ToolContext::PreviewBatch> batches;

    // Touched chain, red -- same kAxisRedColor idiom as the preselected path.
    std::vector<float> chainVerts;
    for (std::size_t i = 0; i + 1 < drag_->verts.size(); ++i) {
        const geo::Vertex* a = model->vertex(drag_->verts[i]);
        const geo::Vertex* b = model->vertex(drag_->verts[i + 1]);
        if (a && b) pushSegment(chainVerts, a->pos, b->pos);
    }
    if (!chainVerts.empty()) {
        batches.push_back(ToolContext::PreviewBatch{std::move(chainVerts), kAxisRedColor.r, kAxisRedColor.g,
                                                      kAxisRedColor.b, kAxisRedColor.a});
    }

    // Profile outline, default color.
    const std::vector<geo::Vec3> face = facePoints(*model, drag_->profileFaceId);
    std::vector<float> faceVerts;
    faceVerts.reserve(face.size() * 6);
    for (std::size_t i = 0; i < face.size(); ++i) pushSegment(faceVerts, face[i], face[(i + 1) % face.size()]);
    if (!faceVerts.empty()) {
        batches.push_back(ToolContext::PreviewBatch{std::move(faceVerts), kDefaultPreviewColor.r,
                                                      kDefaultPreviewColor.g, kDefaultPreviewColor.b,
                                                      kDefaultPreviewColor.a});
    }
    ctx.setPreviewBatches(std::move(batches), std::nullopt);
    ctx.setHint(kDragHint);
}

bool FollowMeTool::commitDrag(ToolContext& ctx) {
    const geo::Model* model = ctx.model();
    if (!drag_ || !model || drag_->edges.empty()) return false;
    std::vector<geo::Vec3> points;
    points.reserve(drag_->verts.size());
    for (geo::Id vId : drag_->verts) {
        const geo::Vertex* v = model->vertex(vId);
        if (!v) return false;
        points.push_back(v->pos);
    }
    const geo::Id profile = drag_->profileFaceId;
    drag_.reset();
    ctx.setPreview({}, std::nullopt);
    ctx.requestFollowMe(profile, points, /*closed=*/false);
    ctx.setHint(kSweptHint);
    return true;
}

void FollowMeTool::resetDrag(ToolContext& ctx) {
    drag_.reset();
    refresh(ctx, nullptr);
}

void FollowMeTool::onPointerMove(ToolContext& ctx, const PointerEvent& e) {
    if (drag_) {
        updateDrag(ctx, e);
        return;
    }
    refresh(ctx, &e);
}

void FollowMeTool::onPointerDown(ToolContext& ctx, const PointerEvent& e) {
    if (drag_) {
        // Second click of click-move-click: touch what is under the click, then commit.
        updateDrag(ctx, e);
        commitDrag(ctx);  // empty chain: stays armed so the pointer can still reach the path
        return;
    }

    const geo::PickResult hit = ctx.pick(e, geo::PickOptions{0.0, 0.0});
    if (hit.kind != geo::PickKind::Face) return;

    if (currentPath_) {
        ctx.requestFollowMe(hit.id, currentPath_->points, currentPath_->closed);
        ctx.setHint(kSweptHint);
        // Deliberately does NOT clear currentPath_/the selection -- the tool
        // stays active with the same path armed, so the next pointer move
        // re-derives and re-previews it exactly as before.
        return;
    }

    // No qualifying preselection: arm manual drag mode on this face.
    const geo::Model* model = ctx.model();
    if (!model) return;
    const std::vector<geo::Vec3> face = facePoints(*model, hit.id);
    if (face.empty()) return;
    geo::Vec3 sum{0.0, 0.0, 0.0};
    for (const geo::Vec3& p : face) sum = sum + p;
    Drag d;
    d.profileFaceId = hit.id;
    d.profileVerts = model->faceVertexLoop(hit.id);
    d.profileCentroid = sum * (1.0 / static_cast<double>(face.size()));
    d.pressScreen = e.screen;
    drag_ = std::move(d);
    refreshDrag(ctx);
}

void FollowMeTool::onPointerUp(ToolContext& ctx, const PointerEvent& e) {
    // Press-drag-release commits only past the travel threshold -- a sub-threshold
    // release is the first half of click-move-click, not a commit.
    if (!drag_) return;
    const QPointF travel = e.screen - drag_->pressScreen;
    if (std::hypot(travel.x(), travel.y()) < kDragThresholdPx) return;

    updateDrag(ctx, e);  // the release position may itself touch the final path edge
    if (!commitDrag(ctx)) resetDrag(ctx);  // a dragged-then-released gesture always ends here
}

void FollowMeTool::onKeyDown(ToolContext& ctx, int key, bool /*ctrl*/) {
    if (key != Qt::Key_Escape) return;
    if (drag_) {
        resetDrag(ctx);  // "start over" per the docs
        return;
    }
    // Otherwise non-destructive -- see the class comment's Escape note.
    refresh(ctx, nullptr);
}

}  // namespace plnr::tools
