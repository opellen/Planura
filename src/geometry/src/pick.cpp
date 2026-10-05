#include <geo/pick.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace plnr::geo {

namespace {

// 2D point in a face's dominant-axis projection, used for the point-in-
// polygon test.
struct Vec2 {
    double x{};
    double y{};
};

// Twice the signed area of triangle (o, a, b); positive when o, a, b are
// wound counter-clockwise.
double cross2(const Vec2& o, const Vec2& a, const Vec2& b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

// Standard crossing-number point-in-polygon test (Franklin's PNPOLY).
bool pointInPolygon(const std::vector<Vec2>& poly, const Vec2& p) {
    bool inside = false;
    const std::size_t n = poly.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const Vec2& pi = poly[i];
        const Vec2& pj = poly[j];
        const bool crosses = (pi.y > p.y) != (pj.y > p.y);
        if (crosses) {
            const double xIntersect = (pj.x - pi.x) * (p.y - pi.y) / (pj.y - pi.y) + pi.x;
            if (p.x < xIntersect) {
                inside = !inside;
            }
        }
    }
    return inside;
}

// Walks a face's half-edge cycle (via next) starting from its stored
// halfEdge, returning the *origin vertex* ids in cycle order. Returns an
// empty vector if the cycle is malformed or does not close.
std::vector<Id> faceVertexLoop(const Model& model, const Face& face) {
    std::vector<Id> ids;
    const Id start = face.halfEdge;
    if (start == kInvalidId) {
        return ids;
    }

    Id cur = start;
    const std::size_t maxSteps = model.halfEdges().size() + 1;  // defensive cap
    for (std::size_t step = 0; step <= maxSteps; ++step) {
        const HalfEdge* he = model.halfEdge(cur);
        if (he == nullptr) {
            return {};
        }
        ids.push_back(he->origin);
        cur = he->next;
        if (cur == start) {
            return ids;
        }
    }
    return {};  // never closed within the step cap -- malformed
}

}  // namespace

PickResult pick(const Model& model, const Ray& ray, const PickOptions& opts) {
    // Every entity kind's candidate loop below runs this first -- a filtered-
    // out entity is skipped entirely, same as if it weren't in the model, so
    // a hit behind it (a lower-priority tier, or nothing) can win instead.
    const auto passesFilter = [&opts](EntityKind kind, Id id) { return !opts.filter || opts.filter(kind, id); };

    // --- Vertex candidates (highest priority) ---
    bool foundVertex = false;
    Id bestVertexId = kInvalidId;
    double bestVertexT = 0.0;
    Vec3 bestVertexPoint;

    for (const auto& [id, v] : model.vertices()) {
        if (!passesFilter(EntityKind::Vertex, id)) continue;
        const double t = dot(v.pos - ray.origin, ray.dir);
        if (t < 0.0) {
            continue;
        }
        const Vec3 closest = ray.origin + ray.dir * t;
        if (distance(closest, v.pos) > opts.vertexTol) {
            continue;
        }
        if (!foundVertex || t < bestVertexT) {
            foundVertex = true;
            bestVertexId = id;
            bestVertexT = t;
            bestVertexPoint = v.pos;
        }
    }

    if (foundVertex) {
        return {PickKind::Vertex, bestVertexId, bestVertexPoint, bestVertexT};
    }

    // --- Edge candidates ---
    bool foundEdge = false;
    Id bestEdgeId = kInvalidId;
    double bestEdgeT = 0.0;
    Vec3 bestEdgePoint;

    for (const auto& [id, e] : model.edges()) {
        if (!passesFilter(EntityKind::Edge, id)) continue;
        const HalfEdge* he0 = model.halfEdge(e.halfEdges[0]);
        const HalfEdge* he1 = model.halfEdge(e.halfEdges[1]);
        if (he0 == nullptr || he1 == nullptr) {
            continue;
        }
        const Vertex* vA = model.vertex(he0->origin);
        const Vertex* vB = model.vertex(he1->origin);
        if (vA == nullptr || vB == nullptr) {
            continue;
        }

        const Vec3 a = vA->pos;
        const Vec3 segDir = vB->pos - a;
        const double segLenSq = dot(segDir, segDir);
        const Vec3 r = ray.origin - a;
        const double c = dot(ray.dir, r);
        const double f = dot(segDir, r);
        const double b = dot(ray.dir, segDir);
        const double denom = segLenSq - b * b;  // a-coefficient is 1 (ray.dir is unit)

        double s = 0.0;
        if (std::fabs(denom) >= kEps) {
            s = std::clamp((f - b * c) / denom, 0.0, 1.0);
        }
        const double t = s * b - c;
        if (t < 0.0) {
            continue;
        }

        const Vec3 pointOnSeg = a + segDir * s;
        const Vec3 closestOnRay = ray.origin + ray.dir * t;
        if (distance(pointOnSeg, closestOnRay) > opts.edgeTol) {
            continue;
        }

        if (!foundEdge || t < bestEdgeT) {
            foundEdge = true;
            bestEdgeId = id;
            bestEdgeT = t;
            bestEdgePoint = pointOnSeg;
        }
    }

    if (foundEdge) {
        return {PickKind::Edge, bestEdgeId, bestEdgePoint, bestEdgeT};
    }

    // --- Face candidates ---
    bool foundFace = false;
    Id bestFaceId = kInvalidId;
    double bestFaceT = 0.0;
    Vec3 bestFacePoint;

    for (const auto& [id, face] : model.faces()) {
        if (!passesFilter(EntityKind::Face, id)) continue;
        const std::vector<Id> loopIds = faceVertexLoop(model, face);
        if (loopIds.size() < 3) {
            continue;
        }

        std::vector<Vec3> positions;
        positions.reserve(loopIds.size());
        bool ok = true;
        for (Id vid : loopIds) {
            const Vertex* v = model.vertex(vid);
            if (v == nullptr) {
                ok = false;
                break;
            }
            positions.push_back(v->pos);
        }
        if (!ok) {
            continue;
        }

        const Vec3& normal = face.normal;
        const double denom = dot(normal, ray.dir);
        if (std::fabs(denom) < kEps) {
            continue;  // ray parallel to the face's plane
        }

        const double t = dot(positions[0] - ray.origin, normal) / denom;
        if (t <= kEps) {
            continue;
        }

        const Vec3 hit = ray.origin + ray.dir * t;

        // Project the loop and the hit point to the plane's dominant-axis 2D.
        const double ax = std::fabs(normal.x);
        const double ay = std::fabs(normal.y);
        const double az = std::fabs(normal.z);

        std::vector<Vec2> poly2d;
        poly2d.reserve(positions.size());
        Vec2 testPt;
        if (az >= ax && az >= ay) {
            for (const auto& p : positions) {
                poly2d.push_back({p.x, p.y});
            }
            testPt = {hit.x, hit.y};
        } else if (ax >= ay) {
            for (const auto& p : positions) {
                poly2d.push_back({p.y, p.z});
            }
            testPt = {hit.y, hit.z};
        } else {
            for (const auto& p : positions) {
                poly2d.push_back({p.z, p.x});
            }
            testPt = {hit.z, hit.x};
        }

        if (!pointInPolygon(poly2d, testPt)) {
            continue;
        }

        if (!foundFace || t < bestFaceT) {
            foundFace = true;
            bestFaceId = id;
            bestFaceT = t;
            bestFacePoint = hit;
        }
    }

    if (foundFace) {
        return {PickKind::Face, bestFaceId, bestFacePoint, bestFaceT};
    }

    return {};
}

}  // namespace plnr::geo
