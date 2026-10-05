#include "agent/geometry_api.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <geo/csg.h>
#include <geo/follow.h>
#include <geo/intersect.h>
#include <geo/scene_ops.h>
#include <geo/solid.h>

#include "agent/events.h"

namespace plnr::agent {

GeometryApi::GeometryApi() : Agent(std::string(kGeometryApiName)) {}

geo::AddEdgeResult GeometryApi::addEdge(geo::Vec3 a, geo::Vec3 b) {
    geo::AddEdgeResult result = rootModel().addEdge(a, b);
    if (result.created || !result.newFaces.empty()) {
        if (result.created) {
            runSplittingPass({result.edge});
        }
        clearLastPolylineOp();  // a real geometry edit closes the retro-edit window
        clearArrayOp();         // and the array-copy retro-edit window too
        send(events::GeometryChanged{});
    }
    return result;
}

bool GeometryApi::removeEdge(geo::Id id) {
    const bool removed = rootModel().removeEdge(id);
    if (removed) {
        // removeEdge can dissolve faces and GC vertices: drop orphaned hidden_ entries before the event, so subscribers never see a hidden ref for a dead entity.
        pruneHiddenDeadRefs();
        clearLastPolylineOp();
        clearArrayOp();
        send(events::GeometryChanged{});
    }
    return removed;
}

bool GeometryApi::removeEntities(const std::vector<events::EntityRef>& refs) {
    bool changed = false;

    // Edges, then faces, then instances (order doesn't affect the final state). Each calls the primitive directly so a mixed batch fires ONE GeometryChanged.
    for (const events::EntityRef& ref : refs) {
        if (ref.kind == geo::EntityKind::Edge) {
            changed |= rootModel().removeEdge(ref.id);
        }
    }
    for (const events::EntityRef& ref : refs) {
        if (ref.kind == geo::EntityKind::Face) {
            changed |= rootModel().removeFace(ref.id);
        }
    }
    for (const events::EntityRef& ref : refs) {
        if (ref.kind == geo::EntityKind::Instance) {
            changed |= scene_.removeInstance(geo::kRootDefinitionId, ref.id);
        }
    }
    // Vertex refs: no-op by design.

    if (changed) {
        // Edge/face removal can dissolve faces/vertices and instance removal retires it: drop orphaned hidden_ entries (as removeEdge does).
        pruneHiddenDeadRefs();
        clearLastPolylineOp();
        clearArrayOp();
        send(events::GeometryChanged{});
    }
    return changed;
}

namespace {

// A solid-face Push/Pull target (every boundary edge borders a non-coplanar face) MOVES the face
// rather than extruding a prism -- the prism path loses/duplicates geometry there.
bool pushPullMovesFace(const geo::Model& model, geo::Id faceId) {
    const geo::Face* face = model.face(faceId);
    if (face == nullptr) {
        return false;
    }
    const auto& halfEdges = model.halfEdges();
    const geo::Id start = face->halfEdge;
    geo::Id cur = start;
    while (cur != geo::kInvalidId) {
        const auto heIt = halfEdges.find(cur);
        if (heIt == halfEdges.end()) {
            return false;  // defensive: corrupt cycle -- let the prism path refuse it
        }
        const auto twinIt = halfEdges.find(heIt->second.twin);
        if (twinIt == halfEdges.end() || twinIt->second.face == geo::kInvalidId) {
            return false;  // naked boundary edge -- plain sheet face, prism path
        }
        const geo::Face* neighbor = model.face(twinIt->second.face);
        if (neighbor == nullptr ||
            std::fabs(geo::dot(face->normal, neighbor->normal)) > 1.0 - geo::kEps) {
            return false;  // coplanar neighbor (chord-split sibling) -- prism path
        }
        cur = heIt->second.next;
        if (cur == start) {
            break;
        }
    }
    return true;
}

// Clamps a face MOVE to stop kMergeTol short of geometry its stretching neighbors would otherwise
// pass through -- an unclamped move can flip the shell inside-out.
double clampFaceMoveDistance(const geo::Model& model, geo::Id faceId, double distance) {
    const geo::Face* face = model.face(faceId);
    const std::vector<geo::Id> loop = model.faceVertexLoop(faceId);
    if (face == nullptr || loop.empty()) {
        return distance;  // defensive -- caller's translate will refuse anyway
    }
    const geo::Vec3 n = face->normal;
    const double planeT = geo::dot(n, model.vertex(loop.front())->pos);
    const std::unordered_set<geo::Id> loopSet(loop.begin(), loop.end());

    // Nearest obstructing offset on each side of the moving plane.
    double gapBelow = std::numeric_limits<double>::infinity();
    double gapAbove = std::numeric_limits<double>::infinity();
    const auto& halfEdges = model.halfEdges();
    const geo::Id start = face->halfEdge;
    geo::Id cur = start;
    while (cur != geo::kInvalidId) {
        const auto heIt = halfEdges.find(cur);
        if (heIt == halfEdges.end()) {
            break;
        }
        const auto twinIt = halfEdges.find(heIt->second.twin);
        if (twinIt != halfEdges.end() && twinIt->second.face != geo::kInvalidId) {
            for (geo::Id vId : model.faceVertexLoop(twinIt->second.face)) {
                if (loopSet.count(vId) != 0) {
                    continue;
                }
                const double t = geo::dot(n, model.vertex(vId)->pos) - planeT;
                if (t < -geo::kEps) {
                    gapBelow = std::min(gapBelow, -t);
                } else if (t > geo::kEps) {
                    gapAbove = std::min(gapAbove, t);
                }
            }
        }
        cur = heIt->second.next;
        if (cur == start) {
            break;
        }
    }

    const double gap = (distance < 0.0) ? gapBelow : gapAbove;
    if (std::isinf(gap) || std::fabs(distance) < gap - geo::kMergeTol) {
        return distance;  // no obstruction on that side, or well short of it
    }
    const double allowed = gap - geo::kMergeTol;
    if (allowed <= 0.0) {
        return 0.0;  // already touching -- refuse the move entirely
    }
    return (distance < 0.0) ? -allowed : allowed;
}

}  // namespace

geo::ExtrudeResult GeometryApi::extrudeFace(geo::Id faceId, double distance) {
    // Solid-face Push/Pull is a face MOVE, not an extrusion (see pushPullMovesFace). Vertex moves are journaled, so undo covers it.
    if (pushPullMovesFace(rootModel(), faceId)) {
        const geo::Face* face = rootModel().face(faceId);
        const double clamped = clampFaceMoveDistance(rootModel(), faceId, distance);
        const bool moved = clamped != 0.0 &&
                           rootModel().translateVertices(rootModel().faceVertexLoop(faceId),
                                                          face->normal * clamped);
        if (moved) {
            clearLastPolylineOp();
            clearArrayOp();
            send(events::GeometryChanged{});
        }
        geo::ExtrudeResult result;
        result.ok = moved;
        result.capFace = faceId;  // the moved face itself -- nothing created
        return result;
    }

    geo::ExtrudeResult result = rootModel().extrudeFace(faceId, distance);
    if (result.ok) {
        // extrudeFace dissolves faceId and usually recreates it under a new id: same stale-ref risk as removeEdge.
        pruneHiddenDeadRefs();
        clearLastPolylineOp();
        clearArrayOp();
        send(events::GeometryChanged{});
    }
    return result;
}

bool GeometryApi::moveEntity(geo::EntityKind kind, geo::Id id, geo::Vec3 delta) {
    if (kind == geo::EntityKind::Instance) {
        // Whole-instance move: root context only. The geo::kEps guard mirrors translateVertices' "was this a move" check.
        if (geo::length(delta) < geo::kEps) {
            return false;
        }
        const geo::Instance* inst = scene_.findInstance(geo::kRootDefinitionId, id);
        if (inst == nullptr) {
            return false;
        }
        // World-space translation, independent of the instance's rotation/scale: composed(current) applies the existing placement first, then shifts by delta.
        const geo::Transform newTransform = geo::Transform::translation(delta).composed(inst->transform);
        const bool moved = scene_.setInstanceTransform(geo::kRootDefinitionId, id, newTransform);
        if (moved) {
            clearLastPolylineOp();
            clearArrayOp();
            send(events::GeometryChanged{});
        }
        return moved;
    }

    const std::vector<geo::Id> vertexIds = geo::collectVertices(rootModel(), kind, id);
    if (vertexIds.empty()) {
        return false;
    }
    const bool moved = rootModel().translateVertices(vertexIds, delta);
    if (moved) {
        clearLastPolylineOp();
        clearArrayOp();
        send(events::GeometryChanged{});
    }
    return moved;
}

namespace {

// Aliveness check shared by pruneHiddenDeadRefs/setHidden. An Instance ref is alive iff it is still a direct child of the root (Instances aren't in `model`).
bool entityAlive(const geo::Model& model, const geo::Scene& scene, const events::EntityRef& ref) {
    switch (ref.kind) {
        case geo::EntityKind::Vertex:
            return model.vertex(ref.id) != nullptr;
        case geo::EntityKind::Edge:
            return model.edge(ref.id) != nullptr;
        case geo::EntityKind::Face:
            return model.face(ref.id) != nullptr;
        case geo::EntityKind::Instance:
            return scene.findInstance(geo::kRootDefinitionId, ref.id) != nullptr;
    }
    return false;  // Unknown kind -- treat as dead.
}

}  // namespace

void GeometryApi::pruneHiddenDeadRefs() {
    for (auto it = hidden_.begin(); it != hidden_.end();) {
        if (entityAlive(rootModel(), scene_, *it)) {
            ++it;
        } else {
            it = hidden_.erase(it);
        }
    }
}

bool GeometryApi::setHidden(const std::vector<events::EntityRef>& refs, bool hidden) {
    bool changed = false;
    for (const events::EntityRef& ref : refs) {
        if (!entityAlive(rootModel(), scene_, ref)) {
            continue;  // unknown to the model/scene -- ignored per contract
        }
        if (hidden) {
            changed |= hidden_.insert(ref).second;
        } else {
            changed |= (hidden_.erase(ref) > 0);
        }
    }
    if (changed) {
        send(events::GeometryChanged{});
    }
    return changed;
}

bool GeometryApi::unhideAll() {
    if (hidden_.empty()) {
        return false;
    }
    hidden_.clear();
    send(events::GeometryChanged{});
    return true;
}

bool GeometryApi::isHidden(events::EntityRef ref) const {
    return hidden_.count(ref) != 0;
}

const std::unordered_set<events::EntityRef>& GeometryApi::hidden() const {
    return hidden_;
}

bool GeometryApi::addRectangle(geo::Vec3 c1, geo::Vec3 c2) {
    const double x1 = std::min(c1.x, c2.x);
    const double x2 = std::max(c1.x, c2.x);
    const double y1 = std::min(c1.y, c2.y);
    const double y2 = std::max(c1.y, c2.y);

    if (std::abs(x2 - x1) < geo::kMergeTol || std::abs(y2 - y1) < geo::kMergeTol) {
        // Degenerate rectangle (zero width or height) -- no mutation, no event.
        return false;
    }

    // Four corners on the ground plane (z = 0), CCW winding seen from +Z.
    const geo::Vec3 p1{x1, y1, 0.0};
    const geo::Vec3 p2{x2, y1, 0.0};
    const geo::Vec3 p3{x2, y2, 0.0};
    const geo::Vec3 p4{x1, y2, 0.0};

    const geo::AddEdgeResult r1 = rootModel().addEdge(p1, p2);
    const geo::AddEdgeResult r2 = rootModel().addEdge(p2, p3);
    const geo::AddEdgeResult r3 = rootModel().addEdge(p3, p4);
    const geo::AddEdgeResult r4 = rootModel().addEdge(p4, p1);  // closes the loop

    const bool changed = r1.created || !r1.newFaces.empty() || r2.created || !r2.newFaces.empty() || r3.created ||
                          !r3.newFaces.empty() || r4.created || !r4.newFaces.empty();
    if (changed) {
        std::vector<geo::Id> newEdgeIds;
        for (const geo::AddEdgeResult* r : {&r1, &r2, &r3, &r4}) {
            if (r->created) {
                newEdgeIds.push_back(r->edge);
            }
        }
        runSplittingPass(std::move(newEdgeIds));
        clearLastPolylineOp();
        clearArrayOp();
        send(events::GeometryChanged{});
    }
    return changed;
}

bool GeometryApi::addPolyline(const std::vector<geo::Vec3>& points, bool closed) {
    if (points.size() < 2) {
        // Not even one edge's worth of points: no-op, and any open retro-edit window closes (see clearLastPolylineOp()).
        clearLastPolylineOp();
        clearArrayOp();
        return false;
    }

    std::vector<geo::Id> createdEdgeIds;
    bool changed = false;
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const geo::AddEdgeResult r = rootModel().addEdge(points[i], points[i + 1]);
        changed |= r.created || !r.newFaces.empty();
        if (r.created) {
            createdEdgeIds.push_back(r.edge);
        }
    }
    if (closed && points.size() >= 3) {
        const geo::AddEdgeResult r = rootModel().addEdge(points.back(), points.front());
        changed |= r.created || !r.newFaces.empty();
        if (r.created) {
            createdEdgeIds.push_back(r.edge);
        }
    }

    if (changed) {
        // Remember only the edges THIS call created (runSplittingPass's return value, not createdEdgeIds): a crossing split can replace an id with fragment ids.
        lastPolylineEdgeIds_ = runSplittingPass(std::move(createdEdgeIds));
        hasLastPolylineOp_ = true;
        clearArrayOp();  // closes the array-copy window
        send(events::GeometryChanged{});
    } else {
        // Every point resolved to an already-connected pair: nothing changed, no new op; any stale one is cleared (as in the < 2 points guard).
        clearLastPolylineOp();
        clearArrayOp();
    }
    return changed;
}

bool GeometryApi::replaceLastPolyline(const std::vector<geo::Vec3>& points, bool closed) {
    if (!hasLastPolylineOp_) {
        return false;
    }

    for (geo::Id edgeId : lastPolylineEdgeIds_) {
        rootModel().removeEdge(edgeId);
    }
    // The removed edges may have dissolved faces/vertices: same stale hidden_ guard as removeEdge.
    pruneHiddenDeadRefs();

    std::vector<geo::Id> createdEdgeIds;
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const geo::AddEdgeResult r = rootModel().addEdge(points[i], points[i + 1]);
        if (r.created) {
            createdEdgeIds.push_back(r.edge);
        }
    }
    if (closed && points.size() >= 3) {
        const geo::AddEdgeResult r = rootModel().addEdge(points.back(), points.front());
        if (r.created) {
            createdEdgeIds.push_back(r.edge);
        }
    }

    // Always a real change (the old edges are gone): ONE GeometryChanged, window stays open on the new edges (runSplittingPass return value, as in addPolyline).
    lastPolylineEdgeIds_ = runSplittingPass(std::move(createdEdgeIds));
    hasLastPolylineOp_ = true;
    clearArrayOp();  // closes the array-copy window
    send(events::GeometryChanged{});
    return true;
}

geo::Id GeometryApi::makeGroup(const std::vector<events::EntityRef>& refs, bool asComponent, std::string name) {
    std::vector<std::pair<geo::EntityKind, geo::Id>> seeds;
    seeds.reserve(refs.size());
    for (const events::EntityRef& ref : refs) {
        seeds.emplace_back(ref.kind, ref.id);
    }

    const geo::EntitySet closure = geo::closureOf(rootModel(), seeds);
    if (closure.edges.empty()) {
        // Nothing edge-bearing in the selection (empty, all unknown, or only stray vertices): nothing to group; no mutation, no event.
        return geo::kInvalidId;
    }

    const bool isGroup = !asComponent;
    const geo::Id defId = scene_.createDefinition(std::string(), isGroup);
    geo::Definition* def = scene_.definition(defId);  // non-null: just created

    if (name.empty()) {
        name = (asComponent ? std::string("Component ") : std::string("Group ")) + std::to_string(defId);
    }
    def->name = name;

    geo::copyInto(def->model, rootModel(), closure, geo::Transform::identity());
    geo::removeFrom(rootModel(), closure);
    // The moved entities' old ids are gone: drop orphaned hidden_ entries (as removeEdge/extrudeFace do). A tag assignment on a moved entity just never resolves again (harmless).
    pruneHiddenDeadRefs();

    const geo::Id instanceId =
        scene_.addInstance(geo::kRootDefinitionId, defId, geo::Transform::identity(), std::move(name));

    clearLastPolylineOp();
    clearArrayOp();
    send(events::GeometryChanged{});
    return instanceId;
}

bool GeometryApi::explode(geo::Id instanceId) {
    geo::Instance* inst = scene_.findInstance(geo::kRootDefinitionId, instanceId);
    if (inst == nullptr) {
        return false;  // not a direct child of the root -- unknown to this call
    }
    geo::Definition* def = scene_.definition(inst->definitionId);
    if (def == nullptr) {
        return false;  // defensive: Scene keeps this id valid
    }

    // Merge ALL of the definition's entities (explode restores everything the group holds) into root, at the instance's own transform.
    geo::EntitySet all;
    all.edges.reserve(def->model.edges().size());
    for (const auto& [id, edge] : def->model.edges()) {
        (void)edge;
        all.edges.push_back(id);
    }
    // Faces listed explicitly: copyInto's loop detection recovers at most cycle-rank (E - V + 1) faces of a closed solid, so a boxed group would lose one.
    all.faces.reserve(def->model.faces().size());
    for (const auto& [id, face] : def->model.faces()) {
        (void)face;
        all.faces.push_back(id);
    }

    geo::copyInto(rootModel(), def->model, all, inst->transform);
    scene_.removeInstance(geo::kRootDefinitionId, instanceId);
    // The definition stays registered (other instances may use it); an orphaned group definition is just unreachable garbage.

    clearLastPolylineOp();
    clearArrayOp();
    send(events::GeometryChanged{});
    return true;
}

geo::Id GeometryApi::add3dText(const std::string& name, const std::vector<std::vector<geo::Vec3>>& outlines,
                                  double extrusion, geo::Vec3 origin) {
    bool hasValidOutline = false;
    for (const std::vector<geo::Vec3>& outline : outlines) {
        if (outline.size() >= 3) {
            hasValidOutline = true;
            break;
        }
    }
    if (!hasValidOutline) {
        // Empty outlines, or all degenerate (< 3 points): no definition created, no mutation, no event.
        return geo::kInvalidId;
    }

    const geo::Id defId = scene_.createDefinition(std::string(), /*isGroup=*/false);  // component, like the reference modeler's 3D Text
    geo::Definition* def = scene_.definition(defId);  // non-null: just created

    std::string instName = name.empty() ? "3D Text " + std::to_string(defId) : name;
    def->name = instName;

    for (const std::vector<geo::Vec3>& outline : outlines) {
        if (outline.size() < 3) {
            continue;  // a degenerate/unpolygonizable subpath is silently skipped
        }

        std::vector<geo::Id> newFaceIds;
        for (std::size_t i = 0; i + 1 < outline.size(); ++i) {
            const geo::AddEdgeResult r = def->model.addEdge(outline[i], outline[i + 1]);
            newFaceIds.insert(newFaceIds.end(), r.newFaces.begin(), r.newFaces.end());
        }
        // Closes the loop back to the first point (as addPolyline(closed=true)) so the face re-emerges via auto-detection.
        const geo::AddEdgeResult closing = def->model.addEdge(outline.back(), outline.front());
        newFaceIds.insert(newFaceIds.end(), closing.newFaces.begin(), closing.newFaces.end());

        if (extrusion > geo::kMergeTol) {
            for (geo::Id faceId : newFaceIds) {
                def->model.extrudeFace(faceId, extrusion);
            }
        }
    }

    const geo::Id instanceId =
        scene_.addInstance(geo::kRootDefinitionId, defId, geo::Transform::translation(origin), std::move(instName));

    clearLastPolylineOp();
    clearArrayOp();
    send(events::GeometryChanged{});
    return instanceId;
}

bool GeometryApi::transformEntities(const std::vector<events::EntityRef>& refs, const events::TransformSpec& spec,
                                       int copies) {
    std::vector<std::pair<geo::EntityKind, geo::Id>> seeds;
    seeds.reserve(refs.size());
    for (const events::EntityRef& ref : refs) {
        seeds.emplace_back(ref.kind, ref.id);
    }
    const geo::EntitySet closure = geo::closureOf(rootModel(), seeds);
    if (closure.vertices.empty()) {
        // Nothing in the selection resolves to a vertex: no-op (as in makeGroup's closure guard).
        return false;
    }

    if (copies == 0) {
        const bool changed = rootModel().transformVertices(closure.vertices, spec.at(1.0));
        if (changed) {
            clearLastPolylineOp();
            arrayOp_.reset();  // this is not an array copy -- close any open array window too
            send(events::GeometryChanged{});
        }
        return changed;
    }

    // copies >= 1: originals stay put, N copies at spec.at(1..N). copyInto's dst==src aliasing is safe; each pass reads the SAME original closure, never a prior copy.
    std::vector<geo::Id> createdEdgeIds;
    for (int k = 1; k <= copies; ++k) {
        geo::copyInto(rootModel(), rootModel(), closure, spec.at(static_cast<double>(k)), &createdEdgeIds);
    }
    if (createdEdgeIds.empty()) {
        // Nothing created (e.g. refs resolved to isolated vertices only; copyInto walks edges/faces): no mutation, no event, no window opened.
        return false;
    }

    clearLastPolylineOp();
    arrayOp_ = ArrayOp{refs, spec, copies, createdEdgeIds};
    send(events::GeometryChanged{});
    return true;
}

bool GeometryApi::applyArrayTimes(int n) {
    return retypeArray(n, /*divide=*/false);
}

bool GeometryApi::applyArrayDivide(int n) {
    return retypeArray(n, /*divide=*/true);
}

bool GeometryApi::retypeArray(int n, bool divide) {
    if (!arrayOp_ || n < 1) {
        return false;
    }

    for (geo::Id edgeId : arrayOp_->copyEdgeIds) {
        rootModel().removeEdge(edgeId);
    }
    // The removed copies may have dissolved faces/vertices: same stale hidden_ guard as removeEdge.
    pruneHiddenDeadRefs();

    // Recomputed from the window's stored refs (the original geometry, never the removed copies), so repeated retyping never drifts.
    std::vector<std::pair<geo::EntityKind, geo::Id>> seeds;
    seeds.reserve(arrayOp_->refs.size());
    for (const events::EntityRef& ref : arrayOp_->refs) {
        seeds.emplace_back(ref.kind, ref.id);
    }
    const geo::EntitySet closure = geo::closureOf(rootModel(), seeds);

    std::vector<geo::Id> createdEdgeIds;
    for (int k = 1; k <= n; ++k) {
        // Times: 1x..nx. Divide: n equal steps (the reference modeler semantics: "/3" is 1/3, 2/3, 3/3, not one fractional copy).
        const double factor = divide ? static_cast<double>(k) / static_cast<double>(n) : static_cast<double>(k);
        geo::copyInto(rootModel(), rootModel(), closure, arrayOp_->spec.at(factor), &createdEdgeIds);
    }

    arrayOp_->copies = n;
    arrayOp_->copyEdgeIds = std::move(createdEdgeIds);
    clearLastPolylineOp();
    send(events::GeometryChanged{});
    return true;
}

bool GeometryApi::followMe(geo::Id profileFaceId, const std::vector<geo::Vec3>& pathPoints, bool closedPath) {
    const geo::Face* face = rootModel().face(profileFaceId);
    if (face == nullptr) {
        return false;  // unknown face id -- no mutation, no event
    }

    const std::vector<geo::Id> loopIds = rootModel().faceVertexLoop(profileFaceId);
    std::vector<geo::Vec3> profile;
    profile.reserve(loopIds.size());
    for (geo::Id vId : loopIds) {
        const geo::Vertex* v = rootModel().vertex(vId);
        if (v == nullptr) {
            return false;  // defensive: a live face's loop names live vertices
        }
        profile.push_back(v->pos);
    }

    const geo::FollowResult result = geo::sweep(profile, pathPoints, closedPath);
    if (!result.ok) {
        return false;  // geo::sweep's own guards (degenerate profile/path) -- no mutation, no event
    }

    const std::size_t profileSize = profile.size();
    const std::size_t sectionCount = result.sections.size();
    bool changed = false;

    // Each section's loop edge (a closed ring); addEdge's merge welds section 0 onto the profile's boundary edges.
    // detectFaces=false: auto-detection would wrongly cap each section.
    for (std::size_t k = 0; k < sectionCount; ++k) {
        const std::vector<geo::Vec3>& section = result.sections[k];
        for (std::size_t i = 0; i < profileSize; ++i) {
            const geo::AddEdgeResult r =
                rootModel().addEdge(section[i], section[(i + 1) % profileSize], /*detectFaces=*/false);
            changed |= r.created;
        }
    }

    // Rail edges between adjacent sections: the same (k, k+1) pairing as geo::sweep's sideQuads (wrap pair when closedPath).
    const std::size_t stripCount = closedPath ? sectionCount : sectionCount - 1;
    for (std::size_t k = 0; k < stripCount; ++k) {
        const std::size_t k2 = (k + 1) % sectionCount;
        for (std::size_t i = 0; i < profileSize; ++i) {
            const geo::AddEdgeResult r =
                rootModel().addEdge(result.sections[k][i], result.sections[k2][i], /*detectFaces=*/false);
            changed |= r.created;
        }
    }

    // Flat position lookup matching geo::sweep's own section*profileSize + vertexIndex indexing,
    // so each sideQuads entry resolves straight to a position (and, via findVertex, a vertex id).
    std::vector<geo::Vec3> flatPositions;
    flatPositions.reserve(sectionCount * profileSize);
    for (const std::vector<geo::Vec3>& section : result.sections) {
        flatPositions.insert(flatPositions.end(), section.begin(), section.end());
    }

    for (const std::array<int, 4>& quad : result.sideQuads) {
        std::vector<geo::Id> vertexLoop;
        vertexLoop.reserve(4);
        std::vector<geo::Vec3> quadPositions;
        quadPositions.reserve(4);
        bool allFound = true;
        for (int flatIndex : quad) {
            const geo::Vec3& pos = flatPositions[static_cast<std::size_t>(flatIndex)];
            const geo::Vertex* v = rootModel().findVertex(pos);
            if (v == nullptr) {
                allFound = false;  // defensive: the edges above already placed it
                break;
            }
            vertexLoop.push_back(v->id);
            quadPositions.push_back(pos);
        }
        if (!allFound) {
            continue;
        }
        const geo::Vec3 normal = geo::normalized(geo::cross(quadPositions[1] - quadPositions[0],
                                                              quadPositions[2] - quadPositions[0]));
        const geo::Id newFace = rootModel().addFaceOnLoop(vertexLoop, normal);
        // kInvalidId: both windings of some edge are already claimed by another face (weld/seam case); skipped silently per addFaceOnLoop's contract.
        changed |= (newFace != geo::kInvalidId);
    }

    // End cap only for an OPEN path: a closed path's sideQuads wrap already closes the tube to section 0. The start cap is profileFaceId itself, untouched.
    if (!closedPath) {
        const std::vector<geo::Vec3>& lastSection = result.sections.back();
        std::vector<geo::Id> vertexLoop;
        vertexLoop.reserve(profileSize);
        bool allFound = true;
        for (const geo::Vec3& pos : lastSection) {
            const geo::Vertex* v = rootModel().findVertex(pos);
            if (v == nullptr) {
                allFound = false;
                break;
            }
            vertexLoop.push_back(v->id);
        }
        if (allFound) {
            const geo::Vec3 capNormal = geo::normalized(geo::cross(lastSection[1] - lastSection[0],
                                                                     lastSection[2] - lastSection[0]));
            const geo::Id capFace = rootModel().addFaceOnLoop(vertexLoop, capNormal);
            changed |= (capFace != geo::kInvalidId);
        }
    }

    if (changed) {
        // followMe only CREATES geometry, so no pruneHiddenDeadRefs() (unlike removeEdge/extrudeFace/makeGroup): hidden_ can't go stale.
        clearLastPolylineOp();
        clearArrayOp();
        send(events::GeometryChanged{});
    }
    return changed;
}

bool GeometryApi::divideEdge(geo::Id edgeId, int n) {
    if (n < 2) {
        return false;
    }
    geo::Model& model = rootModel();
    const geo::Edge* edge = model.edge(edgeId);
    if (edge == nullptr) {
        return false;
    }

    const geo::Vec3 posA = model.vertex(model.halfEdge(edge->halfEdges[0])->origin)->pos;
    const geo::Vec3 posB = model.vertex(model.halfEdge(edge->halfEdges[1])->origin)->pos;

    geo::Id tail = edgeId;
    for (int k = 1; k < n; ++k) {
        const double t = static_cast<double>(k) / static_cast<double>(n);
        const geo::Vec3 p = posA + (posB - posA) * t;
        const geo::SplitEdgeResult result = model.splitEdge(tail, p);
        if (!result.ok) {
            // Defensive: not expected for a valid interior fraction on a normal-length edge.
            break;
        }
        tail = result.edgeB;
    }

    clearLastPolylineOp();
    clearArrayOp();
    send(events::GeometryChanged{});
    return true;
}

namespace {

// True if id appears in loop. Linear scan is fine: the loops here are tiny.
bool loopContains(const std::vector<geo::Id>& loop, geo::Id id) {
    return std::find(loop.begin(), loop.end(), id) != loop.end();
}

}  // namespace

std::vector<geo::Id> GeometryApi::runSplittingPass(std::vector<geo::Id> newEdgeIds) {
    if (newEdgeIds.empty()) {
        return {};
    }
    geo::Model& model = rootModel();

    // newFamily: every live edge id descended from newEdgeIds, kept in sync at each split so later phases' "existing" pool excludes prior fragments.
    std::unordered_set<geo::Id> newFamily(newEdgeIds.begin(), newEdgeIds.end());

    // --- (a-1) endpoint-lands-on-existing-edge --------------------------
    // Each new edge's endpoints are tried against splitEdge for every existing edge.
    for (geo::Id original : newEdgeIds) {
        const geo::Edge* edge = model.edge(original);
        if (edge == nullptr) {
            continue;  // defensive
        }
        for (geo::Id half : edge->halfEdges) {
            const geo::Vec3 endpointPos = model.vertex(model.halfEdge(half)->origin)->pos;
            std::vector<geo::Id> existingIds;
            for (const auto& [existingId, existingEdge] : model.edges()) {
                (void)existingEdge;
                if (newFamily.count(existingId) == 0) {
                    existingIds.push_back(existingId);
                }
            }
            for (geo::Id existingId : existingIds) {
                if (model.splitEdge(existingId, endpointPos).ok) {
                    break;  // this endpoint landed on existingId's interior -- done for this endpoint
                }
            }
        }
    }

    // --- (a-2) edge-cross splits, new vs EXISTING ------------------------
    // Splits `tail` against the nearest existing crossing, repeatedly continuing on the far side.
    const auto settleAgainstExisting = [&](geo::Id tail) {
        while (true) {
            const geo::Edge* tailEdge = model.edge(tail);
            if (tailEdge == nullptr) {
                return;  // defensive
            }
            const geo::Vec3 posA = model.vertex(model.halfEdge(tailEdge->halfEdges[0])->origin)->pos;
            const geo::Vec3 posB = model.vertex(model.halfEdge(tailEdge->halfEdges[1])->origin)->pos;
            const geo::Vec3 dirUnit = geo::normalized(posB - posA);

            bool found = false;
            geo::Id bestExisting = geo::kInvalidId;
            geo::Vec3 bestPoint{};
            double bestT = 0.0;
            for (const auto& [existingId, existingEdge] : model.edges()) {
                if (existingId == tail || newFamily.count(existingId) != 0) {
                    continue;  // existing-only
                }
                const geo::Vec3 eposA = model.vertex(model.halfEdge(existingEdge.halfEdges[0])->origin)->pos;
                const geo::Vec3 eposB = model.vertex(model.halfEdge(existingEdge.halfEdges[1])->origin)->pos;
                const auto hit = geo::segmentIntersect(posA, posB, eposA, eposB, geo::kMergeTol);
                if (!hit) {
                    continue;
                }
                const double t = geo::dot(*hit - posA, dirUnit);
                if (!found || t < bestT) {
                    found = true;
                    bestT = t;
                    bestExisting = existingId;
                    bestPoint = *hit;
                }
            }
            if (!found) {
                return;
            }

            const geo::SplitEdgeResult splitExisting = model.splitEdge(bestExisting, bestPoint);
            const geo::SplitEdgeResult splitTail = model.splitEdge(tail, bestPoint);
            if (!splitExisting.ok || !splitTail.ok) {
                return;  // defensive: a genuine crossing point should satisfy both splits' own guards
            }
            newFamily.erase(tail);
            newFamily.insert(splitTail.edgeA);
            newFamily.insert(splitTail.edgeB);
            tail = splitTail.edgeB;
        }
    };

    for (geo::Id original : newEdgeIds) {
        if (newFamily.count(original) != 0) {
            settleAgainstExisting(original);
        }
    }

    // --- (a-3) edge-cross splits, new vs NEW ------------------------------
    // Handles a self-crossing new-edge set; retests every pair until a full pass finds none.
    // O(n^2) rescans, fine at gesture sizes.
    bool progressed = true;
    while (progressed) {
        progressed = false;
        const std::vector<geo::Id> snapshot(newFamily.begin(), newFamily.end());
        for (std::size_t i = 0; i < snapshot.size() && !progressed; ++i) {
            const geo::Edge* edgeI = model.edge(snapshot[i]);
            if (edgeI == nullptr) {
                continue;  // defensive: can't happen mid-pass
            }
            const geo::Vec3 iA = model.vertex(model.halfEdge(edgeI->halfEdges[0])->origin)->pos;
            const geo::Vec3 iB = model.vertex(model.halfEdge(edgeI->halfEdges[1])->origin)->pos;
            for (std::size_t j = i + 1; j < snapshot.size() && !progressed; ++j) {
                const geo::Edge* edgeJ = model.edge(snapshot[j]);
                if (edgeJ == nullptr) {
                    continue;
                }
                const geo::Vec3 jA = model.vertex(model.halfEdge(edgeJ->halfEdges[0])->origin)->pos;
                const geo::Vec3 jB = model.vertex(model.halfEdge(edgeJ->halfEdges[1])->origin)->pos;
                const auto hit = geo::segmentIntersect(iA, iB, jA, jB, geo::kMergeTol);
                if (!hit) {
                    continue;
                }
                const geo::SplitEdgeResult splitI = model.splitEdge(snapshot[i], *hit);
                const geo::SplitEdgeResult splitJ = model.splitEdge(snapshot[j], *hit);
                if (splitI.ok) {
                    newFamily.erase(snapshot[i]);
                    newFamily.insert(splitI.edgeA);
                    newFamily.insert(splitI.edgeB);
                }
                if (splitJ.ok) {
                    newFamily.erase(snapshot[j]);
                    newFamily.insert(splitJ.edgeA);
                    newFamily.insert(splitJ.edgeB);
                }
                progressed = true;
            }
        }
    }

    const std::vector<geo::Id> finalEdgeIds(newFamily.begin(), newFamily.end());

    // --- (b) face-chord split ---------------------------------------------
    // For each surviving fragment, finds a face whose loop contains both endpoints;
    // splitFaceByChord decides. Single-edge only.
    for (geo::Id n : finalEdgeIds) {
        const geo::Edge* chordEdge = model.edge(n);
        if (chordEdge == nullptr) {
            continue;  // defensive
        }
        const geo::Id vA = model.halfEdge(chordEdge->halfEdges[0])->origin;
        const geo::Id vB = model.halfEdge(chordEdge->halfEdges[1])->origin;

        std::vector<geo::Id> candidateFaces;
        candidateFaces.reserve(model.faces().size());
        for (const auto& [faceId, face] : model.faces()) {
            (void)face;
            candidateFaces.push_back(faceId);
        }
        for (geo::Id faceId : candidateFaces) {
            const std::vector<geo::Id> loop = model.faceVertexLoop(faceId);
            if (!loopContains(loop, vA) || !loopContains(loop, vB)) {
                continue;
            }
            if (model.splitFaceByChord(faceId, n).ok) {
                break;  // this fragment is now spent as a chord -- move to the next
            }
        }
    }

    return finalEdgeIds;
}

geo::Id GeometryApi::importMesh(std::string name, const std::vector<geo::Vec3>& vertices,
                                   const std::vector<std::vector<std::size_t>>& faces) {
    if (vertices.empty()) {
        return geo::kInvalidId;
    }
    bool hasBuildableFace = false;
    for (const std::vector<std::size_t>& face : faces) {
        if (face.size() >= 3) {
            hasBuildableFace = true;
            break;
        }
    }
    if (!hasBuildableFace) {
        return geo::kInvalidId;
    }

    const geo::Id defId = scene_.createDefinition(std::string(), /*isGroup=*/false);  // component, like the reference modeler's OBJ import
    geo::Definition* def = scene_.definition(defId);  // non-null: just created
    geo::Model& model = def->model;

    // Collect every unique UNDIRECTED vertex pair across all faces: restoreEdge runs ONCE per pair, so a shared edge resolves to the same edge regardless of traversal order.
    std::vector<std::pair<std::size_t, std::size_t>> edgeList;
    std::unordered_map<std::uint64_t, std::size_t> edgeIndexByKey;
    const auto edgeKey = [](std::size_t a, std::size_t b) -> std::uint64_t {
        const std::size_t lo = std::min(a, b);
        const std::size_t hi = std::max(a, b);
        return (static_cast<std::uint64_t>(lo) << 32) | static_cast<std::uint64_t>(hi);
    };
    for (const std::vector<std::size_t>& face : faces) {
        if (face.size() < 3) {
            continue;
        }
        for (std::size_t i = 0; i < face.size(); ++i) {
            const std::size_t a = face[i];
            const std::size_t b = face[(i + 1) % face.size()];
            const std::uint64_t key = edgeKey(a, b);
            if (edgeIndexByKey.find(key) == edgeIndexByKey.end()) {
                edgeIndexByKey.emplace(key, edgeList.size());
                edgeList.emplace_back(a, b);
            }
        }
    }

    // Id layout in this fresh model: vertices 1..V, edges V+1..V+E, faces V+E+1..V+E+F (a skipped < 3-vertex face still reserves its slot).
    // restoreNextId must run BEFORE any restoreVertex/Edge/Face call.
    const geo::Id vertexIdBase = 0;
    const geo::Id edgeIdBase = static_cast<geo::Id>(vertices.size());
    const geo::Id faceIdBase = edgeIdBase + static_cast<geo::Id>(edgeList.size());
    const geo::Id reservedNextId = faceIdBase + static_cast<geo::Id>(faces.size()) + 1;
    model.restoreNextId(reservedNextId);

    for (std::size_t i = 0; i < vertices.size(); ++i) {
        model.restoreVertex(vertexIdBase + static_cast<geo::Id>(i) + 1, vertices[i]);
    }
    for (std::size_t i = 0; i < edgeList.size(); ++i) {
        const auto& [a, b] = edgeList[i];
        model.restoreEdge(edgeIdBase + static_cast<geo::Id>(i) + 1, vertexIdBase + static_cast<geo::Id>(a) + 1,
                           vertexIdBase + static_cast<geo::Id>(b) + 1);
    }
    for (std::size_t i = 0; i < faces.size(); ++i) {
        const std::vector<std::size_t>& face = faces[i];
        const geo::Id faceId = faceIdBase + static_cast<geo::Id>(i) + 1;
        if (face.size() < 3) {
            continue;  // slot reserved above, simply unused
        }
        std::vector<geo::Id> loop;
        loop.reserve(face.size());
        for (std::size_t idx : face) {
            loop.push_back(vertexIdBase + static_cast<geo::Id>(idx) + 1);
        }
        // restoreFace's contract: a loop whose winding is already claimed by another face fails silently (accepted content problem).
        model.restoreFace(faceId, loop);
    }

    if (name.empty()) {
        name = "Component " + std::to_string(defId);
    }
    def->name = name;

    const geo::Id instanceId =
        scene_.addInstance(geo::kRootDefinitionId, defId, geo::Transform::identity(), std::move(name));

    clearLastPolylineOp();
    clearArrayOp();
    send(events::GeometryChanged{});
    return instanceId;
}

bool GeometryApi::setInstanceTransform(geo::Id instanceId, const geo::Transform& transform) {
    const bool changed = scene_.setInstanceTransform(geo::kRootDefinitionId, instanceId, transform);
    if (changed) {
        clearLastPolylineOp();
        clearArrayOp();
        send(events::GeometryChanged{});
    }
    return changed;
}

// Local alias: geo::csg's functions/types are used extensively below.
namespace csg = geo::csg;

namespace {

// Emits mesh into dst via the correct replay pattern for a fresh Model: every edge via
// addEdge(detectFaces=false) FIRST, then every face via addFaceOnLoop.
std::vector<geo::Id> emitMergedMesh(geo::Model& dst, const csg::MeshSpec& mesh) {
    for (const csg::PolyFace& face : mesh.faces) {
        for (std::size_t i = 0; i < face.loop.size(); ++i) {
            const geo::Vec3& a = mesh.vertices[static_cast<std::size_t>(face.loop[i])];
            const geo::Vec3& b = mesh.vertices[static_cast<std::size_t>(face.loop[(i + 1) % face.loop.size()])];
            dst.addEdge(a, b, /*detectFaces=*/false);
        }
    }

    std::vector<geo::Id> faceIds;
    faceIds.reserve(mesh.faces.size());
    for (const csg::PolyFace& face : mesh.faces) {
        std::vector<geo::Id> loopIds;
        loopIds.reserve(face.loop.size());
        bool allFound = true;
        for (int idx : face.loop) {
            const geo::Vertex* v = dst.findVertex(mesh.vertices[static_cast<std::size_t>(idx)]);
            if (v == nullptr) {
                allFound = false;  // defensive
                break;
            }
            loopIds.push_back(v->id);
        }
        faceIds.push_back(allFound ? dst.addFaceOnLoop(loopIds, face.normal) : geo::kInvalidId);
    }
    return faceIds;
}

// One N-ary-fold operand: geo::csg::Operand plus a face-provenance map from its Model's face ids back to the source Definition's (see SolidOpFaceProvenance).
struct FoldOperand {
    csg::Operand op;
    std::unordered_map<geo::Id, geo::Id> provenance;
};

// A real leaf operand (an operand Definition's Model, not a scratch fold Model): provenance is the identity map.
FoldOperand makeLeafOperand(const geo::Model& model, const geo::Transform& toWorld) {
    FoldOperand fo;
    fo.op.model = &model;
    fo.op.toWorld = toWorld;
    for (const auto& [faceId, face] : model.faces()) {
        (void)face;
        fo.provenance.emplace(faceId, faceId);
    }
    return fo;
}

// Resolves face's provenance (operandIndex 0 -> left, 1 -> right) to the real source face id; kInvalidId only defensively.
geo::Id resolveProvenance(const csg::PolyFace& face, const FoldOperand& left, const FoldOperand& right) {
    const std::unordered_map<geo::Id, geo::Id>& src = (face.operandIndex == 0) ? left.provenance : right.provenance;
    const auto it = src.find(face.sourceFaceId);
    return it != src.end() ? it->second : geo::kInvalidId;
}

}  // namespace

GeometryApi::SolidOpResult GeometryApi::applySolidOp(events::SolidOp op,
                                                          const std::vector<geo::Id>& instanceIds) {
    const bool isNary =
        (op == events::SolidOp::Union || op == events::SolidOp::OuterShell || op == events::SolidOp::Intersect);
    if (isNary) {
        if (instanceIds.size() < 2) {
            return SolidOpResult{false, "Select at least 2 solids."};
        }
    } else if (instanceIds.size() != 2) {
        return SolidOpResult{false, "Select exactly 2 solids."};
    }

    // -- Resolve + validate every operand: a direct root child whose Definition passes geo::isSolidDefinition.
    std::vector<const geo::Definition*> defs;
    std::vector<geo::Transform> transforms;
    defs.reserve(instanceIds.size());
    transforms.reserve(instanceIds.size());
    for (geo::Id id : instanceIds) {
        const geo::Instance* inst = scene_.findInstance(geo::kRootDefinitionId, id);
        if (inst == nullptr) {
            return SolidOpResult{false, "Not a solid."};
        }
        const geo::Definition* def = scene_.definition(inst->definitionId);
        if (def == nullptr || !geo::isSolidDefinition(*def)) {
            return SolidOpResult{false, "Not a solid."};
        }
        defs.push_back(def);
        transforms.push_back(inst->transform);
    }

    std::vector<FoldOperand> leaves;
    leaves.reserve(defs.size());
    for (std::size_t i = 0; i < defs.size(); ++i) {
        leaves.push_back(makeLeafOperand(defs[i]->model, transforms[i]));
    }

    // -- Overlap gate (Subtract/Trim/Split only): Union/OuterShell proceed regardless (disjoint union is legal);
    // Intersect's fold detects emptiness itself (mustOverlap short-circuit).
    if (op == events::SolidOp::Subtract || op == events::SolidOp::Trim || op == events::SolidOp::Split) {
        const csg::Result overlap = csg::apply(leaves[0].op, leaves[1].op, csg::Op::Intersect);
        if (!overlap.ok) {
            return SolidOpResult{false, "Could not perform boolean operation."};
        }
        if (overlap.mesh.faces.empty()) {
            return SolidOpResult{false, "The solids must overlap."};
        }
    }

    struct ResultPiece {
        csg::MeshSpec mesh;
        std::string instanceName;
        const FoldOperand* left{};
        const FoldOperand* right{};
    };
    std::vector<ResultPiece> pieces;

    std::vector<std::unique_ptr<geo::Model>> scratchModels;  // keeps intermediate fold Models alive
    std::vector<FoldOperand> folds;                          // keeps intermediate/final fold operands alive
    folds.reserve(defs.size());

    if (isNary) {
        const csg::Op rawOp = (op == events::SolidOp::Intersect) ? csg::Op::Intersect : csg::Op::Union;
        const bool mustOverlap = (op == events::SolidOp::Intersect);
        const FoldOperand* left = &leaves[0];
        for (std::size_t i = 1; i < leaves.size(); ++i) {
            const FoldOperand* right = &leaves[i];
            const csg::Result raw = (op == events::SolidOp::OuterShell) ? csg::outerShell(left->op, right->op)
                                                                          : csg::apply(left->op, right->op, rawOp);
            if (!raw.ok) {
                return SolidOpResult{false, "Could not perform boolean operation."};
            }
            csg::MeshSpec merged =
                (op == events::SolidOp::OuterShell) ? std::move(raw.mesh) : csg::mergeCoplanarFaces(raw.mesh);

            if (mustOverlap && merged.faces.empty()) {
                // Further intersecting an empty set stays empty: short-circuit rather than feed an empty Model (a structural failure) into the next apply().
                return SolidOpResult{false, "The solids must overlap."};
            }

            const bool isLast = (i + 1 == leaves.size());
            if (isLast) {
                const char* name = op == events::SolidOp::Union       ? "Union"
                                    : op == events::SolidOp::OuterShell ? "Outer Shell"
                                                                        : "Intersection";
                pieces.push_back(ResultPiece{std::move(merged), name, left, right});
                break;
            }

            scratchModels.push_back(std::make_unique<geo::Model>());
            geo::Model& scratch = *scratchModels.back();
            const std::vector<geo::Id> newFaceIds = emitMergedMesh(scratch, merged);
            FoldOperand next;
            next.op.model = &scratch;
            next.op.toWorld = geo::Transform::identity();
            for (std::size_t f = 0; f < merged.faces.size(); ++f) {
                if (newFaceIds[f] == geo::kInvalidId) continue;
                next.provenance.emplace(newFaceIds[f], resolveProvenance(merged.faces[f], *left, *right));
            }
            folds.push_back(std::move(next));
            left = &folds.back();
        }
    } else if (op == events::SolidOp::Subtract || op == events::SolidOp::Trim) {
        const FoldOperand& cutter = leaves[0];  // ids.front()
        const FoldOperand& target = leaves[1];  // ids.back()
        const csg::Result raw = csg::apply(target.op, cutter.op, csg::Op::Subtract);
        if (!raw.ok) {
            return SolidOpResult{false, "Could not perform boolean operation."};
        }
        csg::MeshSpec merged = csg::mergeCoplanarFaces(raw.mesh);
        pieces.push_back(ResultPiece{std::move(merged), "Difference", &target, &cutter});
    } else {  // Split
        const FoldOperand& a = leaves[0];
        const FoldOperand& b = leaves[1];
        csg::SplitResult sr = csg::split(a.op, b.op);
        if (!sr.ok) {
            return SolidOpResult{false, "Could not perform boolean operation."};
        }
        if (!sr.aMinusB.faces.empty()) {
            pieces.push_back(ResultPiece{std::move(sr.aMinusB), "", &a, &b});
        }
        if (!sr.bMinusA.faces.empty()) {
            pieces.push_back(ResultPiece{std::move(sr.bMinusA), "", &a, &b});
        }
        if (!sr.aIntersectB.faces.empty()) {
            pieces.push_back(ResultPiece{std::move(sr.aIntersectB), "", &a, &b});
        }
    }

    // -- Materialize: one new Definition (isGroup=true) + root Instance per result piece, world-baked (identity instance transform) via the merged-mesh replay above.
    SolidOpResult result;
    result.ok = true;
    for (ResultPiece& piece : pieces) {
        const geo::Id defId = scene_.createDefinition(piece.instanceName, /*isGroup=*/true);
        geo::Definition* def = scene_.definition(defId);  // non-null: just created
        const std::vector<geo::Id> newFaceIds = emitMergedMesh(def->model, piece.mesh);
        for (std::size_t f = 0; f < piece.mesh.faces.size(); ++f) {
            if (newFaceIds[f] == geo::kInvalidId) continue;
            const geo::Id sourceFaceId = resolveProvenance(piece.mesh.faces[f], *piece.left, *piece.right);
            if (sourceFaceId != geo::kInvalidId) {
                result.provenance.push_back(SolidOpFaceProvenance{newFaceIds[f], sourceFaceId});
            }
        }
        const geo::Id instanceId =
            scene_.addInstance(geo::kRootDefinitionId, defId, geo::Transform::identity(), piece.instanceName);
        result.newInstanceIds.push_back(instanceId);
    }

    // -- Consume: Union/OuterShell/Intersect/Subtract/Split remove every
    // operand; Trim keeps the cutter (only the target is retired).
    if (op == events::SolidOp::Trim) {
        scene_.removeInstance(geo::kRootDefinitionId, instanceIds.back());
    } else {
        for (geo::Id id : instanceIds) {
            scene_.removeInstance(geo::kRootDefinitionId, id);
        }
    }
    pruneHiddenDeadRefs();

    clearLastPolylineOp();
    clearArrayOp();
    send(events::GeometryChanged{});
    return result;
}

const geo::Model& GeometryApi::model() const {
    return rootModel();
}

const geo::Scene& GeometryApi::scene() const {
    return scene_;
}

const geo::Definition* GeometryApi::contextDefinition(const std::vector<geo::Id>& path) const {
    const geo::Definition* def = scene_.definition(geo::kRootDefinitionId);
    for (geo::Id instanceId : path) {
        if (def == nullptr) {
            return nullptr;  // defensive: scene_ keeps every definition id valid
        }
        const geo::Instance* inst = scene_.findInstance(def->id, instanceId);
        if (inst == nullptr) {
            return nullptr;  // instanceId isn't a child of the previous step's definition
        }
        def = scene_.definition(inst->definitionId);
    }
    return def;
}

const geo::Definition* GeometryApi::contextDefinition() const {
    return contextDefinition({});
}

const geo::Model* GeometryApi::contextModel(const std::vector<geo::Id>& path) const {
    const geo::Definition* def = contextDefinition(path);
    return def != nullptr ? &def->model : nullptr;
}

geo::Model& GeometryApi::rootModel() {
    return scene_.root().model;
}

const geo::Model& GeometryApi::rootModel() const {
    return scene_.root().model;
}

void GeometryApi::clearForRestore() {
    scene_ = geo::Scene();
    hidden_.clear();
    lastPolylineEdgeIds_.clear();
    hasLastPolylineOp_ = false;
    arrayOp_.reset();
}

void GeometryApi::adoptScene(geo::Scene&& scene) {
    scene_ = std::move(scene);
}

void GeometryApi::restoreHidden(std::vector<events::EntityRef> refs) {
    hidden_ = std::unordered_set<events::EntityRef>(refs.begin(), refs.end());
}

geo::Scene& GeometryApi::sceneForUndoRollback() {
    return scene_;
}

void GeometryApi::clearLastPolylineOp() {
    lastPolylineEdgeIds_.clear();
    hasLastPolylineOp_ = false;
}

void GeometryApi::clearArrayOp() {
    arrayOp_.reset();
}

}  // namespace plnr::agent
