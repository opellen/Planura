#include <geo/infer.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace plnr::geo {

namespace {

// --- Shared point/line-vs-ray primitives -----------------------------------
// Duplicates pick.cpp's candidate-distance math rather than depending on it.

// Nearest vertex within tol along the ray (t >= 0), or nullptr if none qualifies.
const Vertex* nearestVertexOnRay(const Model& model, const Ray& ray, double tol, double& outT) {
    const Vertex* best = nullptr;
    double bestT = 0.0;
    for (const auto& [id, v] : model.vertices()) {
        const double t = dot(v.pos - ray.origin, ray.dir);
        if (t < 0.0) {
            continue;
        }
        const Vec3 closest = ray.origin + ray.dir * t;
        if (distance(closest, v.pos) > tol) {
            continue;
        }
        if (best == nullptr || t < bestT) {
            best = &v;
            bestT = t;
        }
    }
    outT = bestT;
    return best;
}

// Nearest point in candidates within tol along the ray (t >= 0), or nullopt.
template <typename T, typename PosFn>
std::optional<std::pair<Vec3, const T*>> nearestPointOnRay(const std::vector<T>& candidates, PosFn posOf,
                                                             const Ray& ray, double tol) {
    const T* best = nullptr;
    double bestT = 0.0;
    Vec3 bestPos;
    for (const auto& c : candidates) {
        const Vec3 pos = posOf(c);
        const double t = dot(pos - ray.origin, ray.dir);
        if (t < 0.0) {
            continue;
        }
        const Vec3 closest = ray.origin + ray.dir * t;
        if (distance(closest, pos) > tol) {
            continue;
        }
        if (best == nullptr || t < bestT) {
            best = &c;
            bestT = t;
            bestPos = pos;
        }
    }
    if (best == nullptr) {
        return std::nullopt;
    }
    return std::make_pair(bestPos, best);
}

// A resolved model edge's two endpoint positions, for steps that walk edges as plain segments.
struct EdgeSegment {
    Id id{};
    Vec3 a;
    Vec3 b;
};

std::vector<EdgeSegment> collectEdgeSegments(const Model& model) {
    std::vector<EdgeSegment> segs;
    segs.reserve(model.edges().size());
    for (const auto& [id, e] : model.edges()) {
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
        segs.push_back({id, vA->pos, vB->pos});
    }
    return segs;
}

// Nearest point on segment [a, b] to the ray (t >= 0 only), clamped to the segment.
struct SegmentHit {
    Vec3 pointOnSeg;
    double distToRay{};
    double t{};
};

SegmentHit closestPointOnSegmentToRay(const Vec3& a, const Vec3& b, const Ray& ray) {
    const Vec3 segDir = b - a;
    const double segLenSq = dot(segDir, segDir);
    const Vec3 r = ray.origin - a;
    const double c = dot(ray.dir, r);
    const double f = dot(segDir, r);
    const double bCoef = dot(ray.dir, segDir);
    const double denom = segLenSq - bCoef * bCoef;

    double s = 0.0;
    if (std::fabs(denom) >= kEps) {
        s = std::clamp((f - bCoef * c) / denom, 0.0, 1.0);
    }
    const double t = s * bCoef - c;
    const Vec3 pointOnSeg = a + segDir * s;
    const Vec3 closestOnRay = ray.origin + ray.dir * t;
    return {pointOnSeg, distance(pointOnSeg, closestOnRay), t};
}

// Nearest point on the infinite line (origin, dir; dir must be unit) to the ray.
struct LineHit {
    Vec3 pointOnLine;
    double distToRay{};
    double t{};
};

LineHit closestPointOnLineToRay(const Vec3& origin, const Vec3& dir, const Ray& ray) {
    const Vec3 r = ray.origin - origin;
    const double b = dot(ray.dir, dir);
    const double c = dot(ray.dir, r);
    const double f = dot(dir, r);
    const double denom = 1.0 - b * b;  // dir is unit length

    Vec3 pointOnLine;
    double s = 0.0;
    if (std::fabs(denom) < kEps) {
        pointOnLine = origin;  // near-parallel lines: fall back to the line origin
    } else {
        s = (f - b * c) / denom;
        pointOnLine = origin + dir * s;
    }
    const double t = s * b - c;
    const Vec3 pointOnRay = ray.origin + ray.dir * t;
    return {pointOnLine, distance(pointOnLine, pointOnRay), t};
}

// --- Step 3: Intersection ---------------------------------------------------

// Coplanar segment x segment crossing point; nullopt if parallel, non-coplanar within tolerance, or
// crossing outside [0, 1] on either segment.
std::optional<Vec3> segmentSegmentIntersection(const EdgeSegment& s1, const EdgeSegment& s2, double planeTol) {
    const Vec3 d1 = s1.b - s1.a;
    const Vec3 d2 = s2.b - s2.a;
    const Vec3 n = cross(d1, d2);
    const double nLenSq = lengthSq(n);
    if (nLenSq < kEps) {
        return std::nullopt;  // parallel (or degenerate) -- no unique crossing
    }

    const Vec3 r = s2.a - s1.a;

    // Coplanarity: distance from s2.a to the plane through s1.a spanned by d1/d2 is |dot(r,n)|/|n|.
    if (std::fabs(dot(r, n)) > planeTol * std::sqrt(nLenSq)) {
        return std::nullopt;  // not coplanar within tolerance
    }

    const double s = dot(cross(r, d2), n) / nLenSq;
    const double u = dot(cross(r, d1), n) / nLenSq;
    constexpr double kParamEps = 1e-9;
    if (s < -kParamEps || s > 1.0 + kParamEps || u < -kParamEps || u > 1.0 + kParamEps) {
        return std::nullopt;  // crossing falls outside one of the segments
    }

    return s1.a + d1 * s;
}

// --- Step 7: OnFace ----------------------------------------------------------

struct Vec2 {
    double x{};
    double y{};
};

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

std::optional<std::pair<Vec3, Id>> nearestFaceHitOnRay(const Model& model, const Ray& ray) {
    bool found = false;
    double bestT = 0.0;
    Vec3 bestPoint;
    Id bestId = kInvalidId;

    for (const auto& [id, face] : model.faces()) {
        const std::vector<Id> loopIds = model.faceVertexLoop(id);
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

        const double ax = std::fabs(normal.x);
        const double ay = std::fabs(normal.y);
        const double az = std::fabs(normal.z);

        std::vector<Vec2> poly2d;
        poly2d.reserve(positions.size());
        Vec2 testPt;
        if (az >= ax && az >= ay) {
            for (const auto& p : positions) poly2d.push_back({p.x, p.y});
            testPt = {hit.x, hit.y};
        } else if (ax >= ay) {
            for (const auto& p : positions) poly2d.push_back({p.y, p.z});
            testPt = {hit.y, hit.z};
        } else {
            for (const auto& p : positions) poly2d.push_back({p.z, p.x});
            testPt = {hit.z, hit.x};
        }

        if (!pointInPolygon(poly2d, testPt)) {
            continue;
        }

        if (!found || t < bestT) {
            found = true;
            bestT = t;
            bestPoint = hit;
            bestId = id;
        }
    }

    if (!found) {
        return std::nullopt;
    }
    return std::make_pair(bestPoint, bestId);
}

}  // namespace

Inference infer(const Model& model, const Ray& ray, const InferenceContext& ctx) {
    // Priority 1: Endpoint.
    double vertexT = 0.0;
    if (const Vertex* v = nearestVertexOnRay(model, ray, ctx.tols.vertexTol, vertexT)) {
        (void)vertexT;
        return {InferenceKind::Endpoint, v->pos, v->id};
    }

    const std::vector<EdgeSegment> edges = collectEdgeSegments(model);

    // Priority 2: Midpoint.
    {
        const auto hit = nearestPointOnRay(
            edges, [](const EdgeSegment& e) { return (e.a + e.b) * 0.5; }, ray, ctx.tols.vertexTol);
        if (hit) {
            return {InferenceKind::Midpoint, hit->first, hit->second->id};
        }
    }

    // Priority 3: Intersection. Only edges already within tols.vertexTol of the ray can host a
    // crossing that close (the crossing lies ON the edge); O(k^2) over that filtered band.
    {
        std::vector<const EdgeSegment*> band;
        band.reserve(edges.size());
        for (const auto& e : edges) {
            const SegmentHit h = closestPointOnSegmentToRay(e.a, e.b, ray);
            if (h.t >= 0.0 && h.distToRay <= ctx.tols.vertexTol) {
                band.push_back(&e);
            }
        }

        bool found = false;
        double bestT = 0.0;
        Vec3 bestPos;
        for (std::size_t i = 0; i < band.size(); ++i) {
            for (std::size_t j = i + 1; j < band.size(); ++j) {
                const auto crossing = segmentSegmentIntersection(*band[i], *band[j], ctx.tols.vertexTol);
                if (!crossing) {
                    continue;
                }
                const double t = dot(*crossing - ray.origin, ray.dir);
                if (t < 0.0) {
                    continue;
                }
                const Vec3 closest = ray.origin + ray.dir * t;
                if (distance(closest, *crossing) > ctx.tols.vertexTol) {
                    continue;
                }
                if (!found || t < bestT) {
                    found = true;
                    bestT = t;
                    bestPos = *crossing;
                }
            }
        }
        if (found) {
            // Two edges are involved -- no single unambiguous refId.
            return {InferenceKind::Intersection, bestPos, kInvalidId};
        }
    }

    // Priority 4: GuidePoint.
    {
        const auto hit = nearestPointOnRay(
            ctx.guidePoints, [](const GuidePointData& g) { return g.pos; }, ray, ctx.tols.vertexTol);
        if (hit) {
            return {InferenceKind::GuidePoint, hit->first, hit->second->id};
        }
    }

    // Priority 5: OnEdge.
    {
        bool found = false;
        double bestT = 0.0;
        SegmentHit bestHit;
        const EdgeSegment* bestEdge = nullptr;
        for (const auto& e : edges) {
            const SegmentHit h = closestPointOnSegmentToRay(e.a, e.b, ray);
            if (h.t < 0.0 || h.distToRay > ctx.tols.edgeTol) {
                continue;
            }
            if (!found || h.t < bestT) {
                found = true;
                bestT = h.t;
                bestHit = h;
                bestEdge = &e;
            }
        }
        if (found) {
            Inference result{InferenceKind::OnEdge, bestHit.pointOnSeg, bestEdge->id};
            result.dir = normalized(bestEdge->b - bestEdge->a);
            return result;
        }
    }

    // Priority 6: GuideLine.
    {
        bool found = false;
        double bestT = 0.0;
        LineHit bestHit;
        Id bestId = kInvalidId;
        Vec3 bestDir;
        for (const auto& g : ctx.guideLines) {
            const LineHit h = closestPointOnLineToRay(g.point, g.dir, ray);
            if (h.t < 0.0 || h.distToRay > ctx.tols.edgeTol) {
                continue;
            }
            if (!found || h.t < bestT) {
                found = true;
                bestT = h.t;
                bestHit = h;
                bestId = g.id;
                bestDir = g.dir;
            }
        }
        if (found) {
            return {InferenceKind::GuideLine, bestHit.pointOnLine, bestId, bestDir};
        }
    }

    // Priority 7: OnFace.
    {
        const auto hit = nearestFaceHitOnRay(model, ray);
        if (hit) {
            return {InferenceKind::OnFace, hit->first, hit->second};
        }
    }

    // Priority 8: OnAxis, only when a lock is active. Unconditional -- no proximity tolerance.
    if (ctx.axisLock) {
        const AxisLock& lock = *ctx.axisLock;
        const LineHit hit = closestPointOnLineToRay(lock.origin, lock.dir, ray);
        return {InferenceKind::OnAxis, hit.pointOnLine, kInvalidId, lock.dir};
    }

    // Priority 9: FromPoint, against ctx.axisDirs.
    if (!ctx.chargedAnchors.empty()) {
        bool found = false;
        double bestT = 0.0;
        LineHit bestHit;
        Vec3 bestAxis;
        Vec3 bestAnchor;
        for (const Vec3& anchor : ctx.chargedAnchors) {
            // A charged anchor that IS ctx.anchor belongs to the automatic OnAxis rung, not here --
            // letting FromPoint claim its axis lines mislabels every start-point axis ride.
            if (ctx.anchor && almostEqual(anchor, *ctx.anchor)) {
                continue;
            }
            for (const Vec3& axis : ctx.axisDirs) {
                const LineHit h = closestPointOnLineToRay(anchor, axis, ray);
                if (h.t < 0.0 || h.distToRay > ctx.tols.edgeTol) {
                    continue;
                }
                if (!found || h.t < bestT) {
                    found = true;
                    bestT = h.t;
                    bestHit = h;
                    bestAxis = axis;
                    bestAnchor = anchor;
                }
            }
        }
        if (found) {
            Inference result{InferenceKind::FromPoint, bestHit.pointOnLine, kInvalidId, bestAxis};
            result.source = bestAnchor;
            return result;
        }
    }

    // Priority 10: Parallel / Perpendicular (only when both are set).
    if (ctx.referenceEdge && ctx.anchor) {
        const Vec3& anchor = *ctx.anchor;
        const Vec3 refDir = normalized(ctx.referenceEdge->second - ctx.referenceEdge->first);
        if (lengthSq(refDir) >= kEps) {
            // Parallel.
            {
                const LineHit h = closestPointOnLineToRay(anchor, refDir, ray);
                if (h.t >= 0.0 && h.distToRay <= ctx.tols.edgeTol) {
                    return {InferenceKind::Parallel, h.pointOnLine, kInvalidId, refDir};
                }
            }
            // Perpendicular: horizontal, derived from the ground plane, not the edge's face plane.
            const Vec3 perpDir = normalized(cross(refDir, Vec3{0.0, 0.0, 1.0}));
            if (lengthSq(perpDir) >= kEps) {
                const LineHit h = closestPointOnLineToRay(anchor, perpDir, ray);
                if (h.t >= 0.0 && h.distToRay <= ctx.tols.edgeTol) {
                    return {InferenceKind::Perpendicular, h.pointOnLine, kInvalidId, perpDir};
                }
            }
        }
    }

    // Priority 11: OnAxis through ctx.anchor, either t sign. |t| <= edgeTol is rejected: a cursor
    // still on the anchor is near all three axis lines and has no drawing direction yet.
    if (ctx.anchor) {
        const Vec3& anchor = *ctx.anchor;

        // Screen-space direction gate: line proximity alone snaps short ground segments to the vertical
        // axis when zoomed out. dRaw (perpendicular to ray.dir) vs each axis's projection is the
        // screen-angle test; kMinAxisProjection drops view-aligned axes, |dRaw| < kMergeTol = no aim yet.
        constexpr double kOnAxisMaxAngleDeg = 12.0;
        const double kMinAxisAlignment = std::cos(kOnAxisMaxAngleDeg * 3.14159265358979323846 / 180.0);
        const double kMinAxisProjection = std::sin(kOnAxisMaxAngleDeg * 3.14159265358979323846 / 180.0);
        const Vec3 rayClosest = ray.origin + ray.dir * dot(anchor - ray.origin, ray.dir);
        const Vec3 dRaw = rayClosest - anchor;
        const double dLen = std::sqrt(lengthSq(dRaw));
        const bool haveScreenDir = dLen >= kMergeTol;

        bool found = false;
        LineHit bestHit;
        Vec3 bestAxis;
        for (const Vec3& axis : ctx.axisDirs) {
            if (haveScreenDir) {
                const Vec3 proj = axis - ray.dir * dot(axis, ray.dir);
                const double projLen = std::sqrt(lengthSq(proj));
                if (projLen < kMinAxisProjection) {
                    continue;
                }
                if (std::fabs(dot(dRaw, proj)) / (dLen * projLen) < kMinAxisAlignment) {
                    continue;
                }
            }
            const LineHit h = closestPointOnLineToRay(anchor, axis, ray);
            if (std::fabs(h.t) <= ctx.tols.edgeTol || h.distToRay > ctx.tols.edgeTol) {
                continue;
            }
            if (!found || h.distToRay < bestHit.distToRay) {
                found = true;
                bestHit = h;
                bestAxis = axis;
            }
        }
        if (found) {
            const Vec3 dir = bestAxis * (bestHit.t < 0.0 ? -1.0 : 1.0);
            return {InferenceKind::OnAxis, bestHit.pointOnLine, kInvalidId, dir};
        }
    }

    // Priority 12: GroundPlane (z = 0).
    const double denom = ray.dir.z;
    if (std::fabs(denom) >= kEps) {
        const double t = -ray.origin.z / denom;
        if (t > kEps) {
            return {InferenceKind::GroundPlane, ray.origin + ray.dir * t, kInvalidId};
        }
    }

    return {};  // None
}

}  // namespace plnr::geo
