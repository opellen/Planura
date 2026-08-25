#include <geo/loop_finder.h>

#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace plnr::geo {

namespace {

// 2D point in the loop's own plane basis, used for the simple-polygon check.
struct Vec2 {
    double x{};
    double y{};
};

double cross2(const Vec2& o, const Vec2& a, const Vec2& b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

int sign(double v) {
    if (v > kEps) {
        return 1;
    }
    if (v < -kEps) {
        return -1;
    }
    return 0;
}

// True if r lies within p-q's bounding box (only meaningful when p, q, r are
// already known to be collinear -- caller guarantees this via sign() == 0).
bool onSegment(const Vec2& p, const Vec2& q, const Vec2& r) {
    return std::min(p.x, q.x) - kEps <= r.x && r.x <= std::max(p.x, q.x) + kEps &&
           std::min(p.y, q.y) - kEps <= r.y && r.y <= std::max(p.y, q.y) + kEps;
}

// Standard orientation-based 2D segment intersection test (proper crossings
// plus collinear/touching cases), eps-guarded via sign()/onSegment().
bool segmentsIntersect(const Vec2& p1, const Vec2& p2, const Vec2& p3, const Vec2& p4) {
    const int d1 = sign(cross2(p3, p4, p1));
    const int d2 = sign(cross2(p3, p4, p2));
    const int d3 = sign(cross2(p1, p2, p3));
    const int d4 = sign(cross2(p1, p2, p4));

    if (d1 != d2 && d3 != d4 && d1 != 0 && d2 != 0 && d3 != 0 && d4 != 0) {
        return true;
    }
    if (d1 == 0 && onSegment(p3, p4, p1)) {
        return true;
    }
    if (d2 == 0 && onSegment(p3, p4, p2)) {
        return true;
    }
    if (d3 == 0 && onSegment(p1, p2, p3)) {
        return true;
    }
    if (d4 == 0 && onSegment(p1, p2, p4)) {
        return true;
    }
    return false;
}

struct PathResult {
    std::vector<Id> vertices;  // from ... to, inclusive
    std::vector<Id> edges;     // edges[i] connects vertices[i] -> vertices[i + 1]
};

// Shortest path from -> to over the vertex graph, excluding excludedEdge.
// BFS with deterministic tie-break: each vertex's outgoing half-edges are
// expanded in ascending edge-id order.
std::optional<PathResult> shortestPath(const Model& model, Id from, Id to, Id excludedEdge) {
    if (from == to) {
        return std::nullopt;
    }

    std::unordered_map<Id, Id> parent;
    std::unordered_map<Id, Id> parentEdge;
    std::unordered_set<Id> visited;
    std::queue<Id> frontier;

    visited.insert(from);
    frontier.push(from);

    while (!frontier.empty()) {
        const Id current = frontier.front();
        frontier.pop();

        if (current == to) {
            break;
        }

        const Vertex* v = model.vertex(current);
        if (v == nullptr) {
            continue;
        }

        std::vector<Id> outgoing = v->outgoing;
        std::sort(outgoing.begin(), outgoing.end(), [&model](Id a, Id b) {
            return model.halfEdge(a)->edge < model.halfEdge(b)->edge;
        });

        for (Id heId : outgoing) {
            const HalfEdge* he = model.halfEdge(heId);
            if (he == nullptr || he->edge == excludedEdge) {
                continue;
            }
            const HalfEdge* twin = model.halfEdge(he->twin);
            if (twin == nullptr) {
                continue;
            }
            const Id neighbor = twin->origin;
            if (visited.count(neighbor) != 0) {
                continue;
            }
            visited.insert(neighbor);
            parent[neighbor] = current;
            parentEdge[neighbor] = he->edge;
            frontier.push(neighbor);
        }
    }

    if (visited.count(to) == 0) {
        return std::nullopt;
    }

    PathResult result;
    Id cur = to;
    while (cur != from) {
        result.vertices.push_back(cur);
        result.edges.push_back(parentEdge.at(cur));
        cur = parent.at(cur);
    }
    result.vertices.push_back(from);
    std::reverse(result.vertices.begin(), result.vertices.end());
    std::reverse(result.edges.begin(), result.edges.end());
    return result;
}

// True if some existing face's edge set (as a sorted set) exactly matches
// the candidate loop's edge set.
bool matchesExistingFace(const Model& model, const std::vector<Id>& edgeLoop) {
    std::vector<Id> candidate = edgeLoop;
    std::sort(candidate.begin(), candidate.end());

    for (const auto& [faceId, face] : model.faces()) {
        std::vector<Id> faceEdges;
        std::unordered_set<Id> seen;
        Id cur = face.halfEdge;
        while (cur != kInvalidId && seen.count(cur) == 0) {
            const HalfEdge* he = model.halfEdge(cur);
            if (he == nullptr) {
                break;
            }
            seen.insert(cur);
            faceEdges.push_back(he->edge);
            cur = he->next;
            if (cur == face.halfEdge) {
                break;
            }
        }

        std::sort(faceEdges.begin(), faceEdges.end());
        if (faceEdges == candidate) {
            return true;
        }
    }
    return false;
}

}  // namespace

std::optional<LoopCandidate> findLoopForNewEdge(const Model& model, Id newEdgeId) {
    const Edge* newEdge = model.edge(newEdgeId);
    if (newEdge == nullptr) {
        return std::nullopt;
    }
    const HalfEdge* heAB = model.halfEdge(newEdge->halfEdges[0]);
    const HalfEdge* heBA = model.halfEdge(newEdge->halfEdges[1]);
    if (heAB == nullptr || heBA == nullptr) {
        return std::nullopt;
    }

    const Id v1 = heAB->origin;
    const Id v2 = heBA->origin;

    const auto path = shortestPath(model, v2, v1, newEdgeId);
    if (!path) {
        return std::nullopt;
    }

    std::vector<Id> vertexLoop = path->vertices;  // v2 ... v1
    if (vertexLoop.size() < 3) {
        return std::nullopt;
    }

    std::vector<Id> edgeLoop = path->edges;
    edgeLoop.push_back(newEdgeId);  // closes v1 -> v2

    const std::size_t n = vertexLoop.size();
    std::vector<Vec3> positions;
    positions.reserve(n);
    for (Id vId : vertexLoop) {
        const Vertex* v = model.vertex(vId);
        if (v == nullptr) {
            return std::nullopt;
        }
        positions.push_back(v->pos);
    }

    // Newell's method: robust normal for a (possibly non-convex) planar
    // polygon, degenerate/collinear when its length is below kEps.
    Vec3 normalSum{0.0, 0.0, 0.0};
    for (std::size_t i = 0; i < n; ++i) {
        const Vec3& p1 = positions[i];
        const Vec3& p2 = positions[(i + 1) % n];
        normalSum.x += (p1.y - p2.y) * (p1.z + p2.z);
        normalSum.y += (p1.z - p2.z) * (p1.x + p2.x);
        normalSum.z += (p1.x - p2.x) * (p1.y + p2.y);
    }
    if (length(normalSum) < kEps) {
        return std::nullopt;
    }
    const Vec3 normal = normalized(normalSum);

    Vec3 centroid{0.0, 0.0, 0.0};
    for (const Vec3& p : positions) {
        centroid = centroid + p;
    }
    centroid = centroid * (1.0 / static_cast<double>(n));

    for (const Vec3& p : positions) {
        if (std::fabs(dot(normal, p - centroid)) > kPlaneTol) {
            return std::nullopt;  // not coplanar
        }
    }

    // Orthonormal in-plane basis (u, v) for projecting to 2D.
    const Vec3 arbitrary = (std::fabs(normal.x) < 0.9) ? Vec3{1.0, 0.0, 0.0} : Vec3{0.0, 1.0, 0.0};
    const Vec3 u = normalized(cross(normal, arbitrary));
    const Vec3 vAxis = cross(normal, u);

    std::vector<Vec2> projected;
    projected.reserve(n);
    for (const Vec3& p : positions) {
        const Vec3 rel = p - centroid;
        projected.push_back({dot(rel, u), dot(rel, vAxis)});
    }

    // Simple-polygon check: no two non-adjacent edges may intersect.
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            const bool adjacent = (j == i + 1) || (i == 0 && j == n - 1);
            if (adjacent) {
                continue;
            }
            if (segmentsIntersect(projected[i], projected[(i + 1) % n], projected[j], projected[(j + 1) % n])) {
                return std::nullopt;
            }
        }
    }

    if (matchesExistingFace(model, edgeLoop)) {
        return std::nullopt;
    }

    return LoopCandidate{std::move(vertexLoop), std::move(edgeLoop), normal};
}

}  // namespace plnr::geo
