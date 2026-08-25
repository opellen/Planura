#include "offset_tool.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <Qt>

#include <geo/model.h>

namespace plnr::tools {

namespace {

// "~ " + value formatted to 2 decimals -- the VCB's approximate-readout convention.
std::string formatApprox(double value) {
    std::ostringstream oss;
    oss << "~ " << std::fixed << std::setprecision(2) << value;
    return oss.str();
}

// Ray ∩ plane(point, normal); nullopt when parallel or behind the ray origin.
std::optional<geo::Vec3> rayPlaneIntersect(const geo::Ray& ray, const geo::Vec3& point, const geo::Vec3& normal) {
    const double denom = geo::dot(ray.dir, normal);
    if (std::fabs(denom) < geo::kEps) return std::nullopt;
    const double t = geo::dot(point - ray.origin, normal) / denom;
    if (t <= geo::kEps) return std::nullopt;
    return ray.origin + ray.dir * t;
}

// Reads a face's vertex loop as world-space points, or empty if faceId is
// unknown or the loop is inconsistent (defensive; shouldn't happen).
std::vector<geo::Vec3> facePoints(const geo::Model& model, geo::Id faceId) {
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

// A face's own unit normal, falling back to world +Z if degenerate
// (shouldn't happen; geo::offsetLoop also guards a zero normal).
geo::Vec3 faceNormalOrDefault(const geo::Face& face) {
    if (geo::length(face.normal) > geo::kEps) return geo::normalized(face.normal);
    return geo::Vec3{0.0, 0.0, 1.0};
}

}  // namespace

void OffsetTool::onActivate(ToolContext& ctx) {
    lastOffset_.reset();
    reset(ctx);
}

void OffsetTool::onDeactivate(ToolContext& ctx) {
    arm_.reset();
    lastCursor_.reset();
    lastOffset_.reset();
    ctx.setPreview({}, std::nullopt);
}

std::optional<OffsetTool::Arm> OffsetTool::tryBuildSelectedChain(ToolContext& ctx) const {
    const std::vector<events::EntityRef>& sel = ctx.selection();
    if (sel.size() < 2) return std::nullopt;
    for (const events::EntityRef& ref : sel) {
        if (ref.kind != geo::EntityKind::Edge) return std::nullopt;
    }

    const geo::Model* model = ctx.model();
    if (!model) return std::nullopt;

    // vertex id -> [(other endpoint id, edge id)], built only from the
    // selected edges -- the subgraph this orders into a polyline.
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

    // Every vertex must have degree 1 (endpoint) or 2 (interior); degree >=
    // 3 is an ambiguous branch -- reject rather than guess an ordering.
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

    // Walk the subgraph from start, always taking the one unused, non-
    // backtracking edge. A dead end before all edges are used isn't a single chain.
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

    // Coplanarity: derive a normal from the first non-collinear triple,
    // then require every point within geo::kPlaneTol of that plane.
    geo::Vec3 normal{};
    bool foundNormal = false;
    for (std::size_t i = 2; i < points.size(); ++i) {
        const geo::Vec3 candidate = geo::cross(points[1] - points[0], points[i] - points[0]);
        if (geo::length(candidate) > geo::kEps) {
            normal = geo::normalized(candidate);
            foundNormal = true;
            break;
        }
    }
    if (!foundNormal) return std::nullopt;  // every point collinear -- no plane to offset within
    for (const geo::Vec3& p : points) {
        if (std::fabs(geo::dot(p - points[0], normal)) > geo::kPlaneTol) return std::nullopt;
    }

    return Arm{std::move(points), normal, closed};
}

double OffsetTool::signedDistanceForCursor(const Arm& arm, const geo::Vec3& cursor) const {
    double best = 0.0;
    double bestDistSq = std::numeric_limits<double>::max();
    const std::size_t n = arm.points.size();
    if (n < 2) return best;
    const std::size_t segCount = arm.closed ? n : n - 1;

    for (std::size_t i = 0; i < segCount; ++i) {
        const geo::Vec3& a = arm.points[i];
        const geo::Vec3& b = arm.points[(i + 1) % n];
        const geo::Vec3 segVec = b - a;
        const double segLenSq = geo::lengthSq(segVec);

        double t = 0.0;
        if (segLenSq > geo::kEps) {
            t = std::clamp(geo::dot(cursor - a, segVec) / segLenSq, 0.0, 1.0);
        }
        const geo::Vec3 nearest = a + segVec * t;
        const double distSq = geo::lengthSq(cursor - nearest);
        if (distSq >= bestDistSq) continue;

        bestDistSq = distSq;
        const geo::Vec3 segDir = geo::normalized(segVec);
        const geo::Vec3 inPlaneNormal = geo::normalized(geo::cross(arm.planeNormal, segDir));
        best = geo::dot(cursor - nearest, inPlaneNormal);
    }
    return best;
}

std::vector<float> OffsetTool::previewVerts(const std::vector<geo::Vec3>& points, bool closed) const {
    std::vector<float> verts;
    if (points.size() < 2) return verts;

    const std::size_t segCount = closed ? points.size() : points.size() - 1;
    verts.reserve(segCount * 6);
    for (std::size_t i = 0; i < segCount; ++i) {
        const geo::Vec3& a = points[i];
        const geo::Vec3& b = points[(i + 1) % points.size()];
        verts.push_back(static_cast<float>(a.x));
        verts.push_back(static_cast<float>(a.y));
        verts.push_back(static_cast<float>(a.z));
        verts.push_back(static_cast<float>(b.x));
        verts.push_back(static_cast<float>(b.y));
        verts.push_back(static_cast<float>(b.z));
    }
    return verts;
}

std::optional<geo::Vec3> OffsetTool::resolveOnArmedPlane(const PointerEvent& e) const {
    if (!arm_ || arm_->points.empty()) return std::nullopt;
    return rayPlaneIntersect(e.ray, arm_->points[0], arm_->planeNormal);
}

void OffsetTool::updateIdlePreview(ToolContext& ctx, const PointerEvent& e) {
    const std::optional<Arm> chain = tryBuildSelectedChain(ctx);
    if (chain) {
        ctx.setPreview(previewVerts(chain->points, chain->closed), std::nullopt);
        ctx.setHint(kActivationHint);
        updateVcb(ctx, std::nullopt);
        return;
    }

    const geo::PickResult hit = ctx.pick(e, geo::PickOptions{0.0, 0.0});
    const geo::Model* model = ctx.model();
    if (hit.kind == geo::PickKind::Face && model) {
        const std::vector<geo::Vec3> points = facePoints(*model, hit.id);
        if (!points.empty()) {
            ctx.setPreview(previewVerts(points, /*closed=*/true), std::nullopt);
            ctx.setHint(kActivationHint);
            updateVcb(ctx, std::nullopt);
            return;
        }
    }

    ctx.setPreview({}, std::nullopt);
    ctx.setHint(kActivationHint);
    updateVcb(ctx, std::nullopt);
}

bool OffsetTool::commit(ToolContext& ctx, double distance) {
    if (!arm_) return false;
    const geo::OffsetResult result =
        geo::offsetLoop(arm_->points, arm_->planeNormal, distance, arm_->closed, /*keepOverlaps=*/altHeld_);
    if (!result.ok) return false;

    ctx.requestAddPolyline(result.points, arm_->closed);
    lastOffset_ = LastOffset{arm_->points, arm_->planeNormal, arm_->closed, distance};
    reset(ctx);
    return true;
}

void OffsetTool::reset(ToolContext& ctx) {
    stage_ = Stage::Idle;
    arm_.reset();
    lastCursor_.reset();
    ctx.setPreview({}, std::nullopt);
    ctx.setHint(kActivationHint);
    updateVcb(ctx, std::nullopt);
}

void OffsetTool::updateVcb(ToolContext& ctx, std::optional<double> liveDistance) const {
    ctx.setVcbLabel("Distance");
    if (liveDistance) ctx.setVcbValue(formatApprox(*liveDistance));
}

void OffsetTool::onPointerMove(ToolContext& ctx, const PointerEvent& e) {
    altHeld_ = e.alt;

    if (stage_ == Stage::Idle) {
        updateIdlePreview(ctx, e);
        return;
    }

    // Dragging: resolveOnArmedPlane's nullopt (ray parallel) keeps
    // lastCursor_ at its previous value (no flicker on degenerate projection).
    const std::optional<geo::Vec3> onPlane = resolveOnArmedPlane(e);
    if (onPlane) lastCursor_ = onPlane;
    if (!lastCursor_) {
        ctx.setHint(kDraggingHint);
        updateVcb(ctx, std::nullopt);
        return;
    }

    const double distance = signedDistanceForCursor(*arm_, *lastCursor_);
    lastDistance_ = distance;

    const geo::OffsetResult result =
        geo::offsetLoop(arm_->points, arm_->planeNormal, distance, arm_->closed, /*keepOverlaps=*/altHeld_);
    ctx.setPreview(result.ok ? previewVerts(result.points, arm_->closed) : std::vector<float>{}, *lastCursor_);
    ctx.setHint(kDraggingHint);
    updateVcb(ctx, distance);
}

void OffsetTool::onPointerDown(ToolContext& ctx, const PointerEvent& e) {
    altHeld_ = e.alt;

    // Double-click repeat: checked first, ahead of normal stage dispatch,
    // so it preempts whatever press 1 already armed via the Idle branch.
    if (e.clickCount == 2 && lastOffset_) {
        const geo::PickResult facePick = ctx.pick(e, geo::PickOptions{0.0, 0.0});
        const geo::Model* model = ctx.model();
        const geo::Face* face = (facePick.kind == geo::PickKind::Face && model) ? model->face(facePick.id) : nullptr;
        if (face) {
            const std::vector<geo::Vec3> points = facePoints(*model, facePick.id);
            if (!points.empty()) {
                const geo::Vec3 normal = faceNormalOrDefault(*face);
                const geo::OffsetResult result =
                    geo::offsetLoop(points, normal, lastOffset_->distance, /*closed=*/true, /*keepOverlaps=*/altHeld_);
                if (result.ok) {
                    ctx.requestAddPolyline(result.points, /*closed=*/true);
                    lastOffset_ = LastOffset{points, normal, true, lastOffset_->distance};
                    reset(ctx);
                    return;
                }
            }
        }
        // Not a (usable) face hit -- fall through to normal single-click handling.
    }

    switch (stage_) {
        case Stage::Idle: {
            const std::optional<Arm> chain = tryBuildSelectedChain(ctx);
            if (chain) {
                const geo::PickResult edgePick = ctx.pick(e, e.tols);
                bool hitSelectedEdge = false;
                if (edgePick.kind == geo::PickKind::Edge) {
                    for (const events::EntityRef& ref : ctx.selection()) {
                        if (ref.kind == geo::EntityKind::Edge && ref.id == edgePick.id) {
                            hitSelectedEdge = true;
                            break;
                        }
                    }
                }
                if (hitSelectedEdge) {
                    arm_ = chain;
                    lastCursor_.reset();
                    lastDistance_ = 0.0;
                    lastOffset_.reset();  // a fresh arm invalidates the retro-edit window
                    stage_ = Stage::Dragging;
                    ctx.setHint(kDraggingHint);
                    updateVcb(ctx, std::nullopt);
                    return;
                }
                // Selected chain exists but this click hit something else --
                // fall through to face picking.
            }

            const geo::PickResult facePick = ctx.pick(e, geo::PickOptions{0.0, 0.0});
            if (facePick.kind != geo::PickKind::Face) return;
            const geo::Model* model = ctx.model();
            if (!model) return;
            const geo::Face* face = model->face(facePick.id);
            if (!face) return;
            const std::vector<geo::Vec3> points = facePoints(*model, facePick.id);
            if (points.empty()) return;

            arm_ = Arm{points, faceNormalOrDefault(*face), /*closed=*/true};
            lastCursor_.reset();
            lastDistance_ = 0.0;
            lastOffset_.reset();
            stage_ = Stage::Dragging;
            ctx.setHint(kDraggingHint);
            updateVcb(ctx, std::nullopt);
            return;
        }
        case Stage::Dragging: {
            const std::optional<geo::Vec3> onPlane = resolveOnArmedPlane(e);
            if (onPlane) lastCursor_ = onPlane;
            if (!lastCursor_) return;
            const double distance = signedDistanceForCursor(*arm_, *lastCursor_);
            // A degenerate/collapsed distance's failed commit leaves the tool armed and silent.
            commit(ctx, distance);
            return;
        }
    }
}

void OffsetTool::onKeyDown(ToolContext& ctx, int key, bool /*ctrl*/) {
    if (key != Qt::Key_Escape) return;
    lastOffset_.reset();
    reset(ctx);
}

void OffsetTool::onVcbCommit(ToolContext& ctx, const VcbValue& value) {
    if (value.kind != VcbValue::Kind::Scalar) {
        ctx.setHint("Invalid entry.");
        return;
    }
    const double magnitude = std::fabs(value.a);
    if (magnitude < geo::kMergeTol) {
        ctx.setHint("Invalid entry.");
        return;
    }

    if (stage_ == Stage::Dragging) {
        if (!arm_) {
            ctx.setHint("Invalid entry.");
            return;
        }
        // Magnitude of the typed value, sign of the current drag direction
        // (0/no-move treated as positive); a negative typed value flips the side.
        const double baseSign = lastDistance_ < 0.0 ? -1.0 : 1.0;
        const double sign = value.a < 0.0 ? -baseSign : baseSign;
        if (!commit(ctx, sign * magnitude)) ctx.setHint("Invalid entry.");
        return;
    }

    // Idle: retro-edit the just-committed offset via the still-open window,
    // same magnitude/sign rule as Dragging but relative to the last committed direction.
    if (!lastOffset_) {
        ctx.setHint("Invalid entry.");
        return;
    }
    const double baseSign = lastOffset_->distance < 0.0 ? -1.0 : 1.0;
    const double sign = value.a < 0.0 ? -baseSign : baseSign;
    const double distance = sign * magnitude;
    const geo::OffsetResult result = geo::offsetLoop(lastOffset_->sourcePoints, lastOffset_->planeNormal, distance,
                                                       lastOffset_->closed, /*keepOverlaps=*/altHeld_);
    if (!result.ok) {
        ctx.setHint("Invalid entry.");
        return;
    }
    ctx.requestReplaceLastPolyline(result.points, lastOffset_->closed);
    lastOffset_->distance = distance;
}

}  // namespace plnr::tools
