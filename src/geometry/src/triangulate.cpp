#include <geo/triangulate.h>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace plnr::geo {

namespace {

// 2D point in a face's dominant-axis projection.
struct Vec2 {
    double x{};
    double y{};
};

// Twice the signed area of triangle (o, a, b); positive when o, a, b are
// wound counter-clockwise.
double cross2(const Vec2& o, const Vec2& a, const Vec2& b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

// Point-in-triangle test assuming (a, b, c) is wound CCW; boundary points
// count as inside, keeping the ear test conservative (never accepts an
// ear that merely grazes another vertex).
bool pointInTriangle(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c) {
    return cross2(a, b, p) >= -kEps && cross2(b, c, p) >= -kEps && cross2(c, a, p) >= -kEps;
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

std::vector<Id> triangulate(const Model& model, Id faceId) {
    const Face* face = model.face(faceId);
    if (face == nullptr) {
        return {};
    }

    std::vector<Id> ids = faceVertexLoop(model, *face);
    const std::size_t n = ids.size();
    if (n < 3) {
        return {};
    }

    std::vector<Vec3> positions;
    positions.reserve(n);
    for (Id vid : ids) {
        const Vertex* v = model.vertex(vid);
        if (v == nullptr) {
            return {};
        }
        positions.push_back(v->pos);
    }

    const Vec3& normal = face->normal;
    if (length(normal) < kEps) {
        return {};  // degenerate face
    }

    // Project to the plane's dominant-axis 2D (drop the axis with the
    // largest |normal| component).
    const double ax = std::fabs(normal.x);
    const double ay = std::fabs(normal.y);
    const double az = std::fabs(normal.z);

    std::vector<Vec2> poly(n);
    if (az >= ax && az >= ay) {
        for (std::size_t i = 0; i < n; ++i) {
            poly[i] = {positions[i].x, positions[i].y};
        }
    } else if (ax >= ay) {
        for (std::size_t i = 0; i < n; ++i) {
            poly[i] = {positions[i].y, positions[i].z};
        }
    } else {
        for (std::size_t i = 0; i < n; ++i) {
            poly[i] = {positions[i].z, positions[i].x};
        }
    }

    double area2 = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2& p = poly[i];
        const Vec2& q = poly[(i + 1) % n];
        area2 += p.x * q.y - q.x * p.y;
    }
    if (std::fabs(area2) < kEps) {
        return {};  // degenerate (collinear/zero-area) projection
    }

    // ids is Newell-consistent with face->normal; flip both to force CCW for
    // ear-clipping, then swap each emitted triangle's last two ids back to
    // restore that winding.
    bool flipped = false;
    if (area2 < 0.0) {
        std::reverse(poly.begin(), poly.end());
        std::reverse(ids.begin(), ids.end());
        flipped = true;
    }

    std::vector<std::size_t> indices(n);
    std::iota(indices.begin(), indices.end(), std::size_t{0});

    std::vector<Id> triangles;
    const std::size_t maxIterations = n * n + 64;  // defensive cap against infinite loop
    std::size_t iterations = 0;

    const auto emit = [&](std::size_t a, std::size_t b, std::size_t c) {
        triangles.push_back(ids[a]);
        if (flipped) {
            triangles.push_back(ids[c]);
            triangles.push_back(ids[b]);
        } else {
            triangles.push_back(ids[b]);
            triangles.push_back(ids[c]);
        }
    };

    while (indices.size() > 3) {
        bool earFound = false;
        const std::size_t m = indices.size();
        for (std::size_t i = 0; i < m; ++i) {
            if (++iterations > maxIterations) {
                return {};
            }

            const std::size_t iPrev = (i + m - 1) % m;
            const std::size_t iNext = (i + 1) % m;
            const std::size_t a = indices[iPrev];
            const std::size_t b = indices[i];
            const std::size_t c = indices[iNext];

            if (cross2(poly[a], poly[b], poly[c]) <= kEps) {
                continue;  // reflex or degenerate corner
            }

            bool anyInside = false;
            for (std::size_t j = 0; j < m; ++j) {
                if (j == iPrev || j == i || j == iNext) {
                    continue;
                }
                if (pointInTriangle(poly[indices[j]], poly[a], poly[b], poly[c])) {
                    anyInside = true;
                    break;
                }
            }
            if (anyInside) {
                continue;
            }

            emit(a, b, c);
            indices.erase(indices.begin() + static_cast<std::ptrdiff_t>(i));
            earFound = true;
            break;
        }
        if (!earFound) {
            return {};  // numerical dead end -- bail rather than loop forever
        }
    }

    if (indices.size() == 3) {
        emit(indices[0], indices[1], indices[2]);
    }

    return triangles;
}

}  // namespace plnr::geo
