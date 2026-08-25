#include <geo/select.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace plnr::geo {

std::vector<Id> faceBoundaryEdges(const Model& model, Id faceId) {
    const Face* face = model.face(faceId);
    if (face == nullptr) {
        return {};
    }

    std::vector<Id> edges;
    const Id start = face->halfEdge;
    Id cur = start;
    while (cur != kInvalidId) {
        const HalfEdge* he = model.halfEdge(cur);
        if (he == nullptr) {
            return {};  // defensive: corrupt cycle
        }
        edges.push_back(he->edge);
        cur = he->next;
        if (cur == start) {
            break;
        }
    }
    return edges;
}

ConnectedSet connectedComponent(const Model& model, EntityKind kind, Id seedId) {
    ConnectedSet result;

    bool seedValid = false;
    switch (kind) {
        case EntityKind::Vertex:
            seedValid = model.vertex(seedId) != nullptr;
            break;
        case EntityKind::Edge:
            seedValid = model.edge(seedId) != nullptr;
            break;
        case EntityKind::Face:
            seedValid = model.face(seedId) != nullptr;
            break;
        case EntityKind::Instance:
            // An Instance isn't part of this Model's half-edge adjacency
            // graph (see collectVertices's Instance case) -- there is
            // nothing here to walk outward from.
            break;
    }
    if (!seedValid) {
        return result;
    }

    std::unordered_set<Id> visitedVertices;
    std::unordered_set<Id> visitedEdges;
    std::unordered_set<Id> visitedFaces;

    // Three worklists rather than one tagged queue: each entity kind has its
    // own visited set, and draining one list can push onto either of the
    // other two -- the outer loop runs until all three are empty.
    std::vector<Id> vertexQueue;
    std::vector<Id> edgeQueue;
    std::vector<Id> faceQueue;

    switch (kind) {
        case EntityKind::Vertex:
            vertexQueue.push_back(seedId);
            break;
        case EntityKind::Edge:
            edgeQueue.push_back(seedId);
            break;
        case EntityKind::Face:
            faceQueue.push_back(seedId);
            break;
        case EntityKind::Instance:
            break;  // unreachable: seedValid is false for Instance, see above
    }

    while (!vertexQueue.empty() || !edgeQueue.empty() || !faceQueue.empty()) {
        while (!vertexQueue.empty()) {
            const Id v = vertexQueue.back();
            vertexQueue.pop_back();
            if (!visitedVertices.insert(v).second) {
                continue;
            }
            const Vertex* vertex = model.vertex(v);
            if (vertex == nullptr) {
                continue;  // defensive: shouldn't happen for an id we reached via adjacency
            }
            for (Id heId : vertex->outgoing) {
                const HalfEdge* he = model.halfEdge(heId);
                if (he == nullptr) {
                    continue;
                }
                if (visitedEdges.find(he->edge) == visitedEdges.end()) {
                    edgeQueue.push_back(he->edge);
                }
            }
        }

        while (!edgeQueue.empty()) {
            const Id e = edgeQueue.back();
            edgeQueue.pop_back();
            if (!visitedEdges.insert(e).second) {
                continue;
            }
            for (Id vid : collectVertices(model, EntityKind::Edge, e)) {
                if (visitedVertices.find(vid) == visitedVertices.end()) {
                    vertexQueue.push_back(vid);
                }
            }
            const Edge* edge = model.edge(e);
            if (edge == nullptr) {
                continue;
            }
            for (Id heId : edge->halfEdges) {
                const HalfEdge* he = model.halfEdge(heId);
                if (he == nullptr || he->face == kInvalidId) {
                    continue;  // wire half-edge -- no face on this side
                }
                if (visitedFaces.find(he->face) == visitedFaces.end()) {
                    faceQueue.push_back(he->face);
                }
            }
        }

        while (!faceQueue.empty()) {
            const Id f = faceQueue.back();
            faceQueue.pop_back();
            if (!visitedFaces.insert(f).second) {
                continue;
            }
            for (Id eid : faceBoundaryEdges(model, f)) {
                if (visitedEdges.find(eid) == visitedEdges.end()) {
                    edgeQueue.push_back(eid);
                }
            }
        }
    }

    result.vertices.assign(visitedVertices.begin(), visitedVertices.end());
    result.edges.assign(visitedEdges.begin(), visitedEdges.end());
    result.faces.assign(visitedFaces.begin(), visitedFaces.end());
    std::sort(result.vertices.begin(), result.vertices.end());
    std::sort(result.edges.begin(), result.edges.end());
    std::sort(result.faces.begin(), result.faces.end());
    return result;
}

namespace {

// True when p is inside every plane of f (dot(normal, p) + d >= 0, with a
// small epsilon so a point exactly on a boundary plane still counts as
// inside rather than being lost to rounding).
bool pointInFrustum(const Frustum& f, const Vec3& p) {
    for (const Plane& plane : f.planes) {
        if (dot(plane.normal, p) + plane.d < -kEps) {
            return false;
        }
    }
    return true;
}

// Clips segment a->b against f's 4 planes (Cyrus-Beck-style parametric
// clip): true if a non-empty portion of the segment's [0,1] parameter range
// survives every plane, i.e. the segment crosses (or touches) the frustum.
bool segmentCrossesFrustum(const Frustum& f, const Vec3& a, const Vec3& b) {
    double tMin = 0.0;
    double tMax = 1.0;
    const Vec3 dir = b - a;

    for (const Plane& plane : f.planes) {
        const double d0 = dot(plane.normal, a) + plane.d;
        const double denom = dot(plane.normal, dir);  // d(t) = d0 + t * denom

        if (std::fabs(denom) < kEps) {
            // Segment runs parallel to this plane: either it's entirely on
            // the inside (d0 >= 0, this plane clips nothing) or entirely
            // outside (reject the whole segment).
            if (d0 < -kEps) {
                return false;
            }
            continue;
        }

        const double t = -d0 / denom;
        if (denom > 0.0) {
            // d(t) increases with t -- inside is t >= t.
            tMin = std::max(tMin, t);
        } else {
            // d(t) decreases with t -- inside is t <= t.
            tMax = std::min(tMax, t);
        }
        if (tMin > tMax) {
            return false;
        }
    }
    return tMin <= tMax;
}

}  // namespace

Frustum frustumFromCornerRays(const std::array<Ray, 4>& corners) {
    // All 4 corner rays share one eye point (see the header comment) -- take
    // it from the first, arbitrarily.
    const Vec3 eye = corners[0].origin;
    const std::array<Vec3, 4> dirs = {corners[0].dir, corners[1].dir, corners[2].dir, corners[3].dir};

    Frustum f;
    for (std::size_t i = 0; i < 4; ++i) {
        const std::size_t j = (i + 1) % 4;          // the side's other corner
        const std::size_t other1 = (i + 2) % 4;     // the two corners NOT on
        const std::size_t other2 = (i + 3) % 4;      // this side, for sign disambiguation

        Vec3 normal = normalized(cross(dirs[i], dirs[j]));
        const Vec3 refDir = normalized(dirs[other1] + dirs[other2]);
        if (dot(normal, refDir) < 0.0) {
            normal = -normal;
        }

        f.planes[i] = Plane{normal, -dot(normal, eye)};
    }
    return f;
}

RegionPick pickInFrustum(const Model& model, const Frustum& f, RegionMode mode,
                          const std::function<bool(EntityKind, Id)>& filter) {
    RegionPick result;

    const auto passesFilter = [&filter](EntityKind kind, Id id) { return !filter || filter(kind, id); };

    // Vertex-inside is checked repeatedly (once per vertex candidate, again
    // for every edge/face that touches it) -- cache it rather than
    // re-testing the same point against all 4 planes each time.
    std::unordered_map<Id, bool> vertexInsideCache;
    const auto vertexInside = [&](Id id) -> bool {
        auto it = vertexInsideCache.find(id);
        if (it != vertexInsideCache.end()) {
            return it->second;
        }
        const Vertex* v = model.vertex(id);
        const bool inside = v != nullptr && pointInFrustum(f, v->pos);
        vertexInsideCache.emplace(id, inside);
        return inside;
    };

    for (const auto& [id, v] : model.vertices()) {
        (void)v;
        if (!passesFilter(EntityKind::Vertex, id)) continue;
        if (vertexInside(id)) {
            result.vertices.push_back(id);
        }
    }

    // Edge qualification is also the face rule's building block (a face
    // qualifies under Crossing iff one of its boundary edges does), so it's
    // pulled out rather than inlined into the edges loop below.
    const auto edgeQualifies = [&](Id edgeId) -> bool {
        const Edge* e = model.edge(edgeId);
        if (e == nullptr) return false;
        const HalfEdge* he0 = model.halfEdge(e->halfEdges[0]);
        const HalfEdge* he1 = model.halfEdge(e->halfEdges[1]);
        if (he0 == nullptr || he1 == nullptr) return false;

        if (mode == RegionMode::Window) {
            return vertexInside(he0->origin) && vertexInside(he1->origin);
        }

        // Crossing: either endpoint inside, or the segment itself clips
        // against the frustum (covers the "both endpoints outside, midspan
        // crosses through" case).
        if (vertexInside(he0->origin) || vertexInside(he1->origin)) {
            return true;
        }
        const Vertex* vA = model.vertex(he0->origin);
        const Vertex* vB = model.vertex(he1->origin);
        if (vA == nullptr || vB == nullptr) return false;
        return segmentCrossesFrustum(f, vA->pos, vB->pos);
    };

    for (const auto& [id, e] : model.edges()) {
        (void)e;
        if (!passesFilter(EntityKind::Edge, id)) continue;
        if (edgeQualifies(id)) {
            result.edges.push_back(id);
        }
    }

    for (const auto& [id, face] : model.faces()) {
        (void)face;
        if (!passesFilter(EntityKind::Face, id)) continue;

        const std::vector<Id> loop = model.faceVertexLoop(id);
        if (loop.size() < 3) continue;

        bool qualifies = false;
        if (mode == RegionMode::Window) {
            qualifies = std::all_of(loop.begin(), loop.end(), [&](Id vid) { return vertexInside(vid); });
        } else {
            // Crossing: any boundary edge qualifying is enough (interior-only
            // overlap is a deliberate MVP cut per pickInFrustum's header
            // comment, not selected here).
            const std::vector<Id> boundary = faceBoundaryEdges(model, id);
            qualifies = std::any_of(boundary.begin(), boundary.end(), edgeQualifies);
        }

        if (qualifies) {
            result.faces.push_back(id);
        }
    }

    std::sort(result.vertices.begin(), result.vertices.end());
    std::sort(result.edges.begin(), result.edges.end());
    std::sort(result.faces.begin(), result.faces.end());
    return result;
}

}  // namespace plnr::geo
