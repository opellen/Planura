#include <geo/csg.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include <geo/triangulate.h>

namespace plnr::geo::csg {

namespace {

// Runaway guard: caps BSP depth and total fragments across one apply() call so a splinter storm
// fails fast instead of hanging. kMaxPolygons is a whole-run budget, not per-pass.
constexpr std::size_t kMaxBspDepth = 512;
constexpr std::size_t kMaxPolygons = 250000;

// Oriented plane: dot(normal, p) == w lies on it, dot > w is in front. normal is unit.
struct Plane {
    Vec3 normal;
    double w{};
};

// One convex fragment in world coordinates; `plane` is carried verbatim through every split rather
// than recomputed from the (shrinking) fragment -- keeps sliver orientation exact.
struct Polygon {
    std::vector<Vec3> loop;
    Plane plane;
    int operandIndex{};
    Id sourceFaceId{};
};

void flipPolygon(Polygon& poly) {
    std::reverse(poly.loop.begin(), poly.loop.end());
    poly.plane.normal = -poly.plane.normal;
    poly.plane.w = -poly.plane.w;
}

// Per-vertex plane side; OR-ing over a loop classifies the polygon
// (COPLANAR|FRONT == FRONT, FRONT|BACK == SPANNING).
enum : int {
    kCoplanar = 0,
    kFront = 1,
    kBack = 2,
    kSpanning = 3,
};

// Splits poly by plane into the four output lists. Coplanar routes by normal agreement (dot > 0 ->
// coplanarFront), which keeps exactly one copy of a face shared by both operands. Split vertices are
// exact; only the SIDE classification is tolerant (kPlaneTol).
void splitPolygon(const Plane& plane, const Polygon& poly,
                  std::vector<Polygon>& coplanarFront, std::vector<Polygon>& coplanarBack,
                  std::vector<Polygon>& front, std::vector<Polygon>& back) {
    int polygonType = kCoplanar;
    std::vector<int> types;
    types.reserve(poly.loop.size());
    for (const Vec3& v : poly.loop) {
        const double t = dot(plane.normal, v) - plane.w;
        const int type = (t < -kPlaneTol) ? kBack : ((t > kPlaneTol) ? kFront : kCoplanar);
        polygonType |= type;
        types.push_back(type);
    }

    switch (polygonType) {
        case kCoplanar:
            (dot(plane.normal, poly.plane.normal) > 0.0 ? coplanarFront : coplanarBack).push_back(poly);
            return;
        case kFront:
            front.push_back(poly);
            return;
        case kBack:
            back.push_back(poly);
            return;
        default:
            break;  // kSpanning
    }

    std::vector<Vec3> frontLoop;
    std::vector<Vec3> backLoop;
    const std::size_t n = poly.loop.size();
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j = (i + 1) % n;
        const int ti = types[i];
        const int tj = types[j];
        const Vec3& vi = poly.loop[i];
        const Vec3& vj = poly.loop[j];
        if (ti != kBack) frontLoop.push_back(vi);
        if (ti != kFront) backLoop.push_back(vi);
        if ((ti | tj) == kSpanning) {
            const Vec3 edge = vj - vi;
            const double denom = dot(plane.normal, edge);
            if (std::fabs(denom) > kEps) {
                const double t = (plane.w - dot(plane.normal, vi)) / denom;
                const Vec3 crossing = vi + edge * t;
                frontLoop.push_back(crossing);
                backLoop.push_back(crossing);
            }
        }
    }

    if (frontLoop.size() >= 3) {
        Polygon piece = poly;
        piece.loop = std::move(frontLoop);
        front.push_back(std::move(piece));
    }
    if (backLoop.size() >= 3) {
        Polygon piece = poly;
        piece.loop = std::move(backLoop);
        back.push_back(std::move(piece));
    }
}

// Shared abort state for one apply() call: once `failed` is set every further step short-circuits.
struct Budget {
    std::size_t polygons{};
    bool failed{};

    // Charges `count` new fragments and reports whether the run may continue.
    bool charge(std::size_t count) {
        polygons += count;
        if (polygons > kMaxPolygons) failed = true;
        return !failed;
    }
};

// BSP node: `plane_` splits space, `polygons_` holds the fragments coplanar with it, the subtrees
// everything in front of / behind it. A node with no plane is the empty solid and clips nothing.
class Node {
public:
    // Inserts polygons, splitting against existing planes and adopting the first polygon's plane if
    // this node has none yet. Never drops a polygon.
    void build(std::vector<Polygon> polygons, Budget& budget, std::size_t depth);

    // Turns this tree into its complement: planes and polygons flipped, subtrees swapped.
    void invert();

    // Parts of `polygons` OUTSIDE this solid; reaching a leaf plane's back side means inside.
    std::vector<Polygon> clipPolygons(std::vector<Polygon> polygons, Budget& budget) const;

    // Removes everything of this tree's own polygons that lies inside `other`.
    void clipTo(const Node& other, Budget& budget);

    void allPolygons(std::vector<Polygon>& out) const;

private:
    bool hasPlane_{};
    Plane plane_;
    std::vector<Polygon> polygons_;
    std::unique_ptr<Node> front_;
    std::unique_ptr<Node> back_;
};

void Node::build(std::vector<Polygon> polygons, Budget& budget, std::size_t depth) {
    if (budget.failed || polygons.empty()) return;
    if (depth > kMaxBspDepth) {
        budget.failed = true;
        return;
    }
    if (!hasPlane_) {
        plane_ = polygons.front().plane;
        hasPlane_ = true;
    }

    std::vector<Polygon> frontList;
    std::vector<Polygon> backList;
    for (const Polygon& poly : polygons) {
        // A node owns every fragment lying in its plane, whichever way it faces.
        splitPolygon(plane_, poly, polygons_, polygons_, frontList, backList);
    }
    if (!budget.charge(frontList.size() + backList.size())) return;

    if (!frontList.empty()) {
        if (!front_) front_ = std::make_unique<Node>();
        front_->build(std::move(frontList), budget, depth + 1);
    }
    if (!backList.empty()) {
        if (!back_) back_ = std::make_unique<Node>();
        back_->build(std::move(backList), budget, depth + 1);
    }
}

void Node::invert() {
    for (Polygon& poly : polygons_) flipPolygon(poly);
    plane_.normal = -plane_.normal;
    plane_.w = -plane_.w;
    if (front_) front_->invert();
    if (back_) back_->invert();
    front_.swap(back_);
}

std::vector<Polygon> Node::clipPolygons(std::vector<Polygon> polygons, Budget& budget) const {
    if (budget.failed) return {};
    if (!hasPlane_) return polygons;  // empty solid clips nothing

    std::vector<Polygon> frontList;
    std::vector<Polygon> backList;
    for (const Polygon& poly : polygons) {
        splitPolygon(plane_, poly, frontList, backList, frontList, backList);
    }
    if (!budget.charge(frontList.size() + backList.size())) return {};

    if (front_) frontList = front_->clipPolygons(std::move(frontList), budget);
    if (back_) {
        backList = back_->clipPolygons(std::move(backList), budget);
    } else {
        backList.clear();  // behind a leaf plane == inside the solid
    }

    frontList.insert(frontList.end(), std::make_move_iterator(backList.begin()),
                     std::make_move_iterator(backList.end()));
    return frontList;
}

void Node::clipTo(const Node& other, Budget& budget) {
    if (budget.failed) return;
    polygons_ = other.clipPolygons(std::move(polygons_), budget);
    if (front_) front_->clipTo(other, budget);
    if (back_) back_->clipTo(other, budget);
}

void Node::allPolygons(std::vector<Polygon>& out) const {
    out.insert(out.end(), polygons_.begin(), polygons_.end());
    if (front_) front_->allPolygons(out);
    if (back_) back_->allPolygons(out);
}

// Collects one operand's world-space source triangles, faces visited in ascending id order for
// deterministic output. False when the operand contributes nothing usable.
bool gatherOperandPolygons(const Operand& operand, int operandIndex, std::vector<Polygon>& out) {
    if (operand.model == nullptr) return false;
    const Model& model = *operand.model;
    if (model.faces().empty()) return false;

    std::vector<Id> faceIds;
    faceIds.reserve(model.faces().size());
    for (const auto& [faceId, face] : model.faces()) {
        (void)face;
        faceIds.push_back(faceId);
    }
    std::sort(faceIds.begin(), faceIds.end());

    // A mirroring toWorld (negative determinant) reverses orientation; reverse the loop to stay outward.
    const double det = dot(operand.toWorld.col0, cross(operand.toWorld.col1, operand.toWorld.col2));
    const bool mirrored = det < 0.0;

    std::size_t emitted = 0;
    for (Id faceId : faceIds) {
        const std::vector<Id> tris = triangulate(model, faceId);
        for (std::size_t i = 0; i + 2 < tris.size(); i += 3) {
            const Vertex* a = model.vertex(tris[i]);
            const Vertex* b = model.vertex(tris[i + 1]);
            const Vertex* c = model.vertex(tris[i + 2]);
            if (a == nullptr || b == nullptr || c == nullptr) continue;

            Vec3 p0 = operand.toWorld.apply(a->pos);
            Vec3 p1 = operand.toWorld.apply(b->pos);
            Vec3 p2 = operand.toWorld.apply(c->pos);
            if (mirrored) std::swap(p1, p2);

            const Vec3 area = cross(p1 - p0, p2 - p0);
            if (length(area) < kEps) continue;  // degenerate after transform

            Polygon poly;
            poly.loop = {p0, p1, p2};
            poly.plane.normal = normalized(area);
            poly.plane.w = dot(poly.plane.normal, p0);
            poly.operandIndex = operandIndex;
            poly.sourceFaceId = faceId;
            out.push_back(std::move(poly));
            ++emitted;
        }
    }
    return emitted > 0;
}

// Vertex dedup within kMergeTol over a uniform grid of kMergeTol cells: a coincident partner can
// only be in the 3x3x3 neighborhood, probed in fixed order so the emitted index is deterministic.
class VertexWelder {
public:
    int add(const Vec3& p, std::vector<Vec3>& vertices) {
        const Cell home = cellOf(p);
        for (long long dx = -1; dx <= 1; ++dx) {
            for (long long dy = -1; dy <= 1; ++dy) {
                for (long long dz = -1; dz <= 1; ++dz) {
                    const auto it = cells_.find(Cell{home.x + dx, home.y + dy, home.z + dz});
                    if (it == cells_.end()) continue;
                    for (int index : it->second) {
                        if (distance(vertices[static_cast<std::size_t>(index)], p) <= kMergeTol) {
                            return index;
                        }
                    }
                }
            }
        }

        const int index = static_cast<int>(vertices.size());
        vertices.push_back(p);
        cells_[home].push_back(index);
        return index;
    }

private:
    struct Cell {
        long long x{};
        long long y{};
        long long z{};

        bool operator==(const Cell& other) const {
            return x == other.x && y == other.y && z == other.z;
        }
    };

    struct CellHash {
        std::size_t operator()(const Cell& c) const noexcept {
            // Usual spatial-hash mix; a collision only costs an extra distance check.
            const std::uint64_t h = static_cast<std::uint64_t>(c.x) * 73856093ULL ^
                                    static_cast<std::uint64_t>(c.y) * 19349663ULL ^
                                    static_cast<std::uint64_t>(c.z) * 83492791ULL;
            return static_cast<std::size_t>(h);
        }
    };

    static Cell cellOf(const Vec3& p) {
        return Cell{static_cast<long long>(std::floor(p.x / kMergeTol)),
                    static_cast<long long>(std::floor(p.y / kMergeTol)),
                    static_cast<long long>(std::floor(p.z / kMergeTol))};
    }

    std::unordered_map<Cell, std::vector<int>, CellHash> cells_;
};

// Turns the surviving fragments into the public MeshSpec: weld, collapse repeats, drop degenerates.
MeshSpec assembleMesh(const std::vector<Polygon>& polygons) {
    MeshSpec mesh;
    VertexWelder welder;

    for (const Polygon& poly : polygons) {
        std::vector<int> loop;
        loop.reserve(poly.loop.size());
        for (const Vec3& p : poly.loop) {
            const int index = welder.add(p, mesh.vertices);
            if (loop.empty() || loop.back() != index) loop.push_back(index);
        }
        while (loop.size() >= 2 && loop.front() == loop.back()) loop.pop_back();
        if (loop.size() < 3) continue;

        // Newell area of the WELDED loop; agreement with the carried plane normal doubles as the
        // degeneracy test -- a sub-kMergeTol fragment collapses or reverses, and both must be dropped.
        Vec3 area{};
        for (std::size_t i = 0; i < loop.size(); ++i) {
            const Vec3& p = mesh.vertices[static_cast<std::size_t>(loop[i])];
            const Vec3& q = mesh.vertices[static_cast<std::size_t>(loop[(i + 1) % loop.size()])];
            area = area + cross(p, q);
        }
        if (dot(area, poly.plane.normal) < kEps) continue;

        PolyFace face;
        face.loop = std::move(loop);
        face.normal = poly.plane.normal;
        face.operandIndex = poly.operandIndex;
        face.sourceFaceId = poly.sourceFaceId;
        mesh.faces.push_back(std::move(face));
    }

    return mesh;
}

// T-vertex healing, coplanar re-merge, Outer Shell, and Split helpers follow.

// Hole-splitting recursion cap: a region with more nested holes falls back to unmerged fragments.
// Each level at most doubles the piece count.
constexpr int kMaxHoleSplitDepth = 12;

// Cut placements inside a hole's axis span, tried in order until one clears every existing vertex.
// Any line strictly between the loop's extremes separates it; they differ only in clearance.
constexpr double kCutFractions[] = {0.5, 0.375, 0.625, 0.25, 0.75, 0.4375, 0.5625};

// Deterministic ray-direction jitters for Outer Shell classification -- NEVER random. Small enough
// to preserve parity, large enough to walk a graze off a triangle edge or fan diagonal.
constexpr Vec3 kRayJitters[] = {
    Vec3{0.0, 0.0, 0.0},          Vec3{1.7e-3, -9.1e-4, 4.3e-4},
    Vec3{-8.3e-4, 2.1e-3, -1.3e-3}, Vec3{5.9e-4, 1.1e-3, 2.7e-3},
    Vec3{-2.3e-3, -1.9e-3, 8.7e-4}, Vec3{3.1e-3, 7.3e-4, -2.1e-3},
    Vec3{-1.1e-3, 3.3e-3, 1.9e-3},  Vec3{2.9e-3, -3.7e-3, -7.9e-4},
};

// Barycentric band around a triangle's boundary counted as a graze, forcing a jittered re-cast.
constexpr double kRayBaryTol = 1e-7;

// How many of a shell's largest fragments to try as ray origins before giving up.
constexpr std::size_t kMaxShellRayOrigins = 3;

double component(const Vec3& v, int axis) {
    return (axis == 0) ? v.x : ((axis == 1) ? v.y : v.z);
}

const Vec3& vertexAt(const std::vector<Vec3>& vertices, int index) {
    return vertices[static_cast<std::size_t>(index)];
}

// The two world axes a plane with this normal projects onto, ordered so a loop wound CCW about
// `normal` has POSITIVE shoelace area. The dropped axis is the normal's dominant one.
struct PlaneFrame {
    int uAxis{};
    int vAxis{};
};

PlaneFrame frameFor(const Vec3& normal) {
    const double ax = std::fabs(normal.x);
    const double ay = std::fabs(normal.y);
    const double az = std::fabs(normal.z);
    int dominant = 0;
    double best = ax;
    if (ay > best) {
        dominant = 1;
        best = ay;
    }
    if (az > best) {
        dominant = 2;
    }
    int u = (dominant + 1) % 3;
    int v = (dominant + 2) % 3;
    if (component(normal, dominant) < 0.0) std::swap(u, v);
    return PlaneFrame{u, v};
}

double signedArea2D(const std::vector<int>& loop, const PlaneFrame& frame,
                    const std::vector<Vec3>& vertices) {
    double twice = 0.0;
    const std::size_t n = loop.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Vec3& p = vertexAt(vertices, loop[i]);
        const Vec3& q = vertexAt(vertices, loop[(i + 1) % n]);
        twice += component(p, frame.uAxis) * component(q, frame.vAxis) -
                 component(q, frame.uAxis) * component(p, frame.vAxis);
    }
    return twice * 0.5;
}

// Newell area vector of a planar loop: length = the area, direction = the winding's normal.
Vec3 loopAreaVector(const std::vector<int>& loop, const std::vector<Vec3>& vertices) {
    Vec3 area{};
    const std::size_t n = loop.size();
    for (std::size_t i = 0; i < n; ++i) {
        area = area + cross(vertexAt(vertices, loop[i]), vertexAt(vertices, loop[(i + 1) % n]));
    }
    return area * 0.5;
}

bool isSimpleLoop(const std::vector<int>& loop) {
    if (loop.size() < 3) return false;
    std::set<int> seen;
    for (int index : loop) {
        if (!seen.insert(index).second) return false;
    }
    return true;
}

// Rotates a loop to a canonical start: the smallest vertex index whose leading triple is CONVEX
// w.r.t. `normal`, so PolyFace's cross(v1-v0, v2-v0) contract survives the collinear T-junction
// vertices merging keeps. Falls back to the smallest index overall.
void normalizeLoopStart(std::vector<int>& loop, const std::vector<Vec3>& vertices,
                        const Vec3& normal) {
    const std::size_t n = loop.size();
    if (n < 3) return;

    std::size_t fallback = 0;
    for (std::size_t i = 1; i < n; ++i) {
        if (loop[i] < loop[fallback]) fallback = i;
    }

    std::size_t best = n;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec3& a = vertexAt(vertices, loop[i]);
        const Vec3& b = vertexAt(vertices, loop[(i + 1) % n]);
        const Vec3& c = vertexAt(vertices, loop[(i + 2) % n]);
        if (dot(cross(b - a, c - a), normal) <= kEps) continue;  // collinear or reflex
        if (best == n || loop[i] < loop[best]) best = i;
    }

    const std::size_t start = (best == n) ? fallback : best;
    std::rotate(loop.begin(), loop.begin() + static_cast<std::ptrdiff_t>(start), loop.end());
}

// Re-welds a MeshSpec's vertices within kMergeTol, leaving `welder` primed so later cut points
// dedupe against the same set. Faces welded down to fewer than 3 indices are dropped.
MeshSpec weldMesh(const MeshSpec& mesh, VertexWelder& welder) {
    MeshSpec out;
    std::vector<int> remap(mesh.vertices.size(), -1);
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        remap[i] = welder.add(mesh.vertices[i], out.vertices);
    }

    for (const PolyFace& face : mesh.faces) {
        PolyFace welded = face;
        welded.loop.clear();
        bool valid = true;
        for (int index : face.loop) {
            if (index < 0 || static_cast<std::size_t>(index) >= mesh.vertices.size()) {
                valid = false;
                break;
            }
            const int mapped = remap[static_cast<std::size_t>(index)];
            if (welded.loop.empty() || welded.loop.back() != mapped) welded.loop.push_back(mapped);
        }
        if (!valid) continue;
        while (welded.loop.size() >= 2 && welded.loop.front() == welded.loop.back()) {
            welded.loop.pop_back();
        }
        if (welded.loop.size() < 3) continue;
        out.faces.push_back(std::move(welded));
    }
    return out;
}

// Drops unreferenced vertices, renumbering in first-use order to stay deterministic.
MeshSpec compactMesh(const MeshSpec& mesh) {
    MeshSpec out;
    std::vector<int> remap(mesh.vertices.size(), -1);
    out.faces.reserve(mesh.faces.size());
    for (const PolyFace& face : mesh.faces) {
        PolyFace compacted = face;
        for (int& index : compacted.loop) {
            const std::size_t old = static_cast<std::size_t>(index);
            if (remap[old] < 0) {
                remap[old] = static_cast<int>(out.vertices.size());
                out.vertices.push_back(mesh.vertices[old]);
            }
            index = remap[old];
        }
        out.faces.push_back(std::move(compacted));
    }
    return out;
}

// T-vertex healing: every mesh vertex within kMergeTol of a face-loop edge's interior is inserted
// into that loop in edge order. Runs over the whole mesh, so shared edges stay index-for-index.
// Candidates are pruned by a 1D x-sorted index.
void healTVertices(MeshSpec& mesh) {
    const std::size_t vertexCount = mesh.vertices.size();
    if (vertexCount == 0 || mesh.faces.empty()) return;

    std::vector<int> order(vertexCount);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&mesh](int lhs, int rhs) {
        const double lx = mesh.vertices[static_cast<std::size_t>(lhs)].x;
        const double rx = mesh.vertices[static_cast<std::size_t>(rhs)].x;
        if (lx != rx) return lx < rx;
        return lhs < rhs;
    });
    std::vector<double> sortedX(vertexCount);
    for (std::size_t i = 0; i < vertexCount; ++i) {
        sortedX[i] = mesh.vertices[static_cast<std::size_t>(order[i])].x;
    }

    std::vector<std::pair<double, int>> hits;
    for (PolyFace& face : mesh.faces) {
        const std::vector<int> original = face.loop;
        const std::set<int> originalSet(original.begin(), original.end());
        std::vector<int> healed;
        healed.reserve(original.size());

        const std::size_t n = original.size();
        for (std::size_t i = 0; i < n; ++i) {
            const int startIndex = original[i];
            const int endIndex = original[(i + 1) % n];
            const Vec3& start = vertexAt(mesh.vertices, startIndex);
            const Vec3& end = vertexAt(mesh.vertices, endIndex);
            if (healed.empty() || healed.back() != startIndex) healed.push_back(startIndex);

            const Vec3 edge = end - start;
            const double lengthSquared = lengthSq(edge);
            if (lengthSquared <= kEps) continue;
            const double edgeLength = std::sqrt(lengthSquared);

            const auto lo = std::lower_bound(sortedX.begin(), sortedX.end(),
                                             std::min(start.x, end.x) - kMergeTol);
            const auto hi = std::upper_bound(sortedX.begin(), sortedX.end(),
                                             std::max(start.x, end.x) + kMergeTol);
            hits.clear();
            for (auto it = lo; it != hi; ++it) {
                const int candidate = order[static_cast<std::size_t>(it - sortedX.begin())];
                if (candidate == startIndex || candidate == endIndex) continue;
                // A vertex already a corner of THIS loop is never spliced in again -- a repeated
                // index would break simplicity. Self-touching loops are left alone.
                if (originalSet.count(candidate) != 0) continue;

                const Vec3& point = vertexAt(mesh.vertices, candidate);
                const double t = dot(point - start, edge) / lengthSquared;
                if (t * edgeLength <= kMergeTol) continue;
                if ((1.0 - t) * edgeLength <= kMergeTol) continue;
                if (length((point - start) - edge * t) > kMergeTol) continue;
                hits.emplace_back(t, candidate);
            }
            std::sort(hits.begin(), hits.end());
            for (const auto& [t, candidate] : hits) {
                (void)t;
                if (healed.back() != candidate) healed.push_back(candidate);
            }
        }

        while (healed.size() >= 2 && healed.front() == healed.back()) healed.pop_back();
        if (healed.size() >= 3) face.loop = std::move(healed);
    }
}

// Boundary loops of the region tiled by `fragments`: directed edges are counted, exact opposite
// pairs cancel (healing must run first so fragments share edges index-for-index), survivors chain
// into cycles. False on any degeneracy: pinch vertex, degree mismatch, or a cycle shorter than 3.
bool traceBoundaryLoops(const std::vector<std::vector<int>>& fragments,
                        std::vector<std::vector<int>>& loops) {
    std::map<std::pair<int, int>, int> counts;
    std::size_t directedEdges = 0;
    for (const std::vector<int>& loop : fragments) {
        const std::size_t n = loop.size();
        if (n < 3) return false;
        for (std::size_t i = 0; i < n; ++i) {
            const int from = loop[i];
            const int to = loop[(i + 1) % n];
            if (from == to) return false;
            ++counts[std::make_pair(from, to)];
            ++directedEdges;
        }
    }

    std::map<int, std::vector<int>> outgoing;
    std::map<int, int> inDegree;
    std::set<std::pair<int, int>> handled;
    for (const auto& [edge, count] : counts) {
        (void)count;
        // Visit each UNDIRECTED pair once, keyed low-to-high -- keying off `edge` and skipping
        // first > second would silently drop a one-way edge (the outer boundary of a merged region).
        const std::pair<int, int> key = (edge.first < edge.second)
                                            ? edge
                                            : std::make_pair(edge.second, edge.first);
        if (!handled.insert(key).second) continue;

        const auto forward = counts.find(key);
        const auto reverse = counts.find(std::make_pair(key.second, key.first));
        const int forwardCount = (forward == counts.end()) ? 0 : forward->second;
        const int reverseCount = (reverse == counts.end()) ? 0 : reverse->second;
        const int cancelled = std::min(forwardCount, reverseCount);
        for (int i = cancelled; i < forwardCount; ++i) {
            outgoing[key.first].push_back(key.second);
            ++inDegree[key.second];
        }
        for (int i = cancelled; i < reverseCount; ++i) {
            outgoing[key.second].push_back(key.first);
            ++inDegree[key.first];
        }
    }

    std::map<int, int> next;
    for (const auto& [vertex, targets] : outgoing) {
        if (targets.size() != 1) return false;  // pinch vertex
        next.emplace(vertex, targets.front());
    }
    if (next.size() != inDegree.size()) return false;
    for (const auto& [vertex, degree] : inDegree) {
        if (degree != 1) return false;
        if (next.find(vertex) == next.end()) return false;
    }

    std::set<int> pending;
    for (const auto& [vertex, target] : next) {
        (void)target;
        pending.insert(vertex);
    }
    while (!pending.empty()) {
        const int start = *pending.begin();
        std::vector<int> loop;
        int current = start;
        do {
            if (pending.erase(current) == 0) return false;
            loop.push_back(current);
            current = next.at(current);
            if (loop.size() > directedEdges + 1) return false;
        } while (current != start);
        if (loop.size() < 3) return false;
        loops.push_back(std::move(loop));
    }
    return true;
}

// Sutherland-Hodgman clip of one fragment loop against the axis plane {component(p, axis) == cut},
// keeping the `keepBelow` side. Crossing points are welded into `vertices`, so both halves share
// indices and the cut edge still cancels.
void clipLoopByAxisPlane(const std::vector<int>& loop, int axis, double cut, bool keepBelow,
                         std::vector<Vec3>& vertices, VertexWelder& welder,
                         std::vector<int>& out) {
    out.clear();
    const std::size_t n = loop.size();
    for (std::size_t i = 0; i < n; ++i) {
        const int fromIndex = loop[i];
        const int toIndex = loop[(i + 1) % n];
        const Vec3 from = vertexAt(vertices, fromIndex);
        const Vec3 to = vertexAt(vertices, toIndex);
        const double dFrom = component(from, axis) - cut;
        const double dTo = component(to, axis) - cut;
        const bool insideFrom = keepBelow ? (dFrom <= 0.0) : (dFrom >= 0.0);
        const bool insideTo = keepBelow ? (dTo <= 0.0) : (dTo >= 0.0);

        if (insideFrom && (out.empty() || out.back() != fromIndex)) out.push_back(fromIndex);
        if (insideFrom != insideTo) {
            const double denom = dFrom - dTo;
            if (std::fabs(denom) <= kEps) continue;
            const double t = dFrom / denom;
            const Vec3 crossing = from + (to - from) * t;
            const int index = welder.add(crossing, vertices);
            if (out.empty() || out.back() != index) out.push_back(index);
        }
    }
    while (out.size() >= 2 && out.front() == out.back()) out.pop_back();
    if (out.size() < 3) out.clear();
}

// Merges one coplanar group's fragments into simple CCW loops, splitting on a cut line while the
// traced boundary still has holes. False on any degeneracy; the caller falls back to the fragments.
bool mergeRegion(const std::vector<std::vector<int>>& fragments, const PlaneFrame& frame,
                 std::vector<Vec3>& vertices, VertexWelder& welder, int depth,
                 std::vector<std::vector<int>>& out) {
    if (fragments.empty()) return true;
    if (depth > kMaxHoleSplitDepth) return false;

    std::vector<std::vector<int>> loops;
    if (!traceBoundaryLoops(fragments, loops)) return false;
    if (loops.empty()) return false;

    std::vector<std::size_t> outerLoops;
    std::vector<std::size_t> holeLoops;
    for (std::size_t i = 0; i < loops.size(); ++i) {
        const double area = signedArea2D(loops[i], frame, vertices);
        if (std::fabs(area) < kEps) return false;  // sliver loop
        (area > 0.0 ? outerLoops : holeLoops).push_back(i);
    }
    if (outerLoops.empty()) return false;
    if (holeLoops.empty()) {
        for (std::size_t i : outerLoops) out.push_back(loops[i]);
        return true;
    }

    // Deterministic hole choice: the hole loop holding the smallest vertex index.
    std::size_t hole = holeLoops.front();
    int holeKey = *std::min_element(loops[hole].begin(), loops[hole].end());
    for (std::size_t i : holeLoops) {
        const int key = *std::min_element(loops[i].begin(), loops[i].end());
        if (key < holeKey) {
            holeKey = key;
            hole = i;
        }
    }

    const int axes[2] = {frame.uAxis, frame.vAxis};
    for (int axis : axes) {
        double low = std::numeric_limits<double>::max();
        double high = std::numeric_limits<double>::lowest();
        for (int index : loops[hole]) {
            const double value = component(vertexAt(vertices, index), axis);
            low = std::min(low, value);
            high = std::max(high, value);
        }
        const double span = high - low;
        if (!(span > 0.0)) continue;
        // Clearance from every existing vertex; a zero-width sliver would break edge cancellation.
        const double clearance = std::max(8.0 * kMergeTol, 1e-6 * span);
        if (span <= 4.0 * clearance) continue;

        for (double fraction : kCutFractions) {
            const double cut = low + fraction * span;
            if (cut - low <= clearance || high - cut <= clearance) continue;

            bool clear = true;
            for (const std::vector<int>& fragment : fragments) {
                for (int index : fragment) {
                    if (std::fabs(component(vertexAt(vertices, index), axis) - cut) < clearance) {
                        clear = false;
                        break;
                    }
                }
                if (!clear) break;
            }
            if (!clear) continue;

            // Committed: a clearing cut either works or the whole group falls back; no retry.
            std::vector<std::vector<int>> below;
            std::vector<std::vector<int>> above;
            std::vector<int> piece;
            for (const std::vector<int>& fragment : fragments) {
                clipLoopByAxisPlane(fragment, axis, cut, true, vertices, welder, piece);
                if (piece.size() >= 3) below.push_back(piece);
                clipLoopByAxisPlane(fragment, axis, cut, false, vertices, welder, piece);
                if (piece.size() >= 3) above.push_back(piece);
            }
            if (below.empty() || above.empty()) return false;

            std::vector<std::vector<int>> split;
            if (!mergeRegion(below, frame, vertices, welder, depth + 1, split)) return false;
            if (!mergeRegion(above, frame, vertices, welder, depth + 1, split)) return false;
            out.insert(out.end(), std::make_move_iterator(split.begin()),
                       std::make_move_iterator(split.end()));
            return true;
        }
    }
    return false;
}

// Counts ray crossings of the fan-triangulated faces in `faceIndices`. Sets `ambiguous` on a
// too-close-to-call hit (barycentric graze, ray nearly in a triangle's plane, hit on the origin) so
// the caller can re-cast with a jitter instead of trusting the parity.
int countRayCrossings(const MeshSpec& mesh, const std::vector<std::size_t>& faceIndices,
                      const Vec3& origin, const Vec3& direction, bool& ambiguous) {
    int crossings = 0;
    for (std::size_t faceIndex : faceIndices) {
        const PolyFace& face = mesh.faces[faceIndex];
        if (face.loop.size() < 3) continue;
        const Vec3& p0 = vertexAt(mesh.vertices, face.loop[0]);
        for (std::size_t i = 1; i + 1 < face.loop.size(); ++i) {
            const Vec3& p1 = vertexAt(mesh.vertices, face.loop[i]);
            const Vec3& p2 = vertexAt(mesh.vertices, face.loop[i + 1]);
            const Vec3 areaVector = cross(p1 - p0, p2 - p0);
            const double areaLength = length(areaVector);
            if (areaLength < kEps) continue;  // degenerate (collinear) triangle

            const Vec3 unitNormal = areaVector * (1.0 / areaLength);
            const double denom = dot(unitNormal, direction);
            const double originHeight = dot(unitNormal, origin - p0);
            if (std::fabs(denom) < 1e-9) {
                // Ray parallel to the plane: no crossing, but one lying IN it can't be trusted.
                if (std::fabs(originHeight) < kMergeTol) ambiguous = true;
                continue;
            }

            const double t = -originHeight / denom;
            if (t < kMergeTol) {
                if (t > -kMergeTol) ambiguous = true;  // hit sitting on the origin
                continue;
            }

            const Vec3 hit = origin + direction * t;
            const double inverse = 1.0 / dot(areaVector, areaVector);
            const double b0 = dot(cross(p1 - hit, p2 - hit), areaVector) * inverse;
            const double b1 = dot(cross(p2 - hit, p0 - hit), areaVector) * inverse;
            const double b2 = dot(cross(p0 - hit, p1 - hit), areaVector) * inverse;
            const double smallest = std::min(b0, std::min(b1, b2));
            if (smallest < -kRayBaryTol) continue;  // clean miss
            if (smallest < kRayBaryTol) {
                ambiguous = true;  // graze on an edge, a vertex or a fan diagonal
                continue;
            }
            ++crossings;
        }
    }
    return crossings;
}

// Groups faces into connected shells (joined transitively by a shared vertex), returned in ascending
// order of smallest member index. VERTEX connectivity needs no healing pass; its only over-merge is
// two shells meeting at a point, which Outer Shell treats as one piece anyway.
std::vector<std::vector<std::size_t>> connectedShells(const MeshSpec& mesh) {
    std::vector<int> parent(mesh.faces.size());
    std::iota(parent.begin(), parent.end(), 0);
    const auto find = [&parent](int node) {
        while (parent[static_cast<std::size_t>(node)] != node) {
            parent[static_cast<std::size_t>(node)] =
                parent[static_cast<std::size_t>(parent[static_cast<std::size_t>(node)])];
            node = parent[static_cast<std::size_t>(node)];
        }
        return node;
    };

    std::vector<int> firstFaceAtVertex(mesh.vertices.size(), -1);
    for (std::size_t faceIndex = 0; faceIndex < mesh.faces.size(); ++faceIndex) {
        for (int vertexIndex : mesh.faces[faceIndex].loop) {
            const std::size_t v = static_cast<std::size_t>(vertexIndex);
            if (firstFaceAtVertex[v] < 0) {
                firstFaceAtVertex[v] = static_cast<int>(faceIndex);
                continue;
            }
            const int a = find(static_cast<int>(faceIndex));
            const int b = find(firstFaceAtVertex[v]);
            if (a != b) parent[static_cast<std::size_t>(a)] = b;
        }
    }

    std::map<int, std::vector<std::size_t>> byRoot;
    for (std::size_t faceIndex = 0; faceIndex < mesh.faces.size(); ++faceIndex) {
        byRoot[find(static_cast<int>(faceIndex))].push_back(faceIndex);
    }

    std::vector<std::vector<std::size_t>> shells;
    shells.reserve(byRoot.size());
    for (auto& [root, faces] : byRoot) {
        (void)root;
        shells.push_back(std::move(faces));
    }
    std::sort(shells.begin(), shells.end(),
              [](const std::vector<std::size_t>& lhs, const std::vector<std::size_t>& rhs) {
                  return lhs.front() < rhs.front();
              });
    return shells;
}

}  // namespace

Result apply(const Operand& a, const Operand& b, Op op) {
    Result result;

    std::vector<Polygon> aPolygons;
    std::vector<Polygon> bPolygons;
    if (!gatherOperandPolygons(a, 0, aPolygons)) return result;  // ok stays false
    if (!gatherOperandPolygons(b, 1, bPolygons)) return result;

    Budget budget;
    Node aTree;
    Node bTree;
    aTree.build(std::move(aPolygons), budget, 0);
    bTree.build(std::move(bPolygons), budget, 0);

    // Concatenating both trees' survivors is surface-for-surface equivalent to csg.js's final
    // a.build(b.allPolygons()) + invert (build only splits, never drops) and produces far fewer
    // fragments. flipResult stands in for that trailing invert, applied to both sets.
    bool flipResult = false;
    switch (op) {
        case Op::Union:
            aTree.clipTo(bTree, budget);
            bTree.clipTo(aTree, budget);
            bTree.invert();
            bTree.clipTo(aTree, budget);
            bTree.invert();
            break;
        case Op::Subtract:
            aTree.invert();
            aTree.clipTo(bTree, budget);
            bTree.clipTo(aTree, budget);
            bTree.invert();
            bTree.clipTo(aTree, budget);
            bTree.invert();
            flipResult = true;
            break;
        case Op::Intersect:
            aTree.invert();
            bTree.clipTo(aTree, budget);
            bTree.invert();
            aTree.clipTo(bTree, budget);
            bTree.clipTo(aTree, budget);
            flipResult = true;
            break;
    }
    if (budget.failed) return result;  // guard breach -> ok stays false

    std::vector<Polygon> survivors;
    aTree.allPolygons(survivors);
    bTree.allPolygons(survivors);
    if (flipResult) {
        for (Polygon& poly : survivors) flipPolygon(poly);
    }

    result.mesh = assembleMesh(survivors);
    result.ok = true;
    return result;
}

double meshVolume(const MeshSpec& mesh) {
    double volume = 0.0;
    for (const PolyFace& face : mesh.faces) {
        if (face.loop.size() < 3) continue;

        bool valid = true;
        for (int index : face.loop) {
            if (index < 0 || static_cast<std::size_t>(index) >= mesh.vertices.size()) {
                valid = false;
                break;
            }
        }
        if (!valid) continue;

        const Vec3& v0 = mesh.vertices[static_cast<std::size_t>(face.loop[0])];
        for (std::size_t i = 1; i + 1 < face.loop.size(); ++i) {
            const Vec3& v1 = mesh.vertices[static_cast<std::size_t>(face.loop[i])];
            const Vec3& v2 = mesh.vertices[static_cast<std::size_t>(face.loop[i + 1])];
            volume += dot(v0, cross(v1, v2)) / 6.0;
        }
    }
    return volume;
}

MeshSpec mergeCoplanarFaces(const MeshSpec& mesh) {
    VertexWelder welder;
    MeshSpec work = weldMesh(mesh, welder);
    if (work.faces.empty()) return MeshSpec{};
    healTVertices(work);

    // Key on (operandIndex, sourceFaceId), then cluster linearly on the ORIENTED plane inside the
    // bucket: planes of one source face agree only to rounding, so they cannot be hashed.
    std::map<std::pair<int, Id>, std::vector<std::size_t>> buckets;
    for (std::size_t i = 0; i < work.faces.size(); ++i) {
        const PolyFace& face = work.faces[i];
        buckets[std::make_pair(face.operandIndex, face.sourceFaceId)].push_back(i);
    }

    struct Cluster {
        Vec3 normal;
        double offset{};
        std::vector<std::size_t> faces;
    };

    std::vector<PolyFace> mergedFaces;
    for (const auto& [key, faceIndices] : buckets) {
        (void)key;
        std::vector<Cluster> clusters;
        for (std::size_t faceIndex : faceIndices) {
            const PolyFace& face = work.faces[faceIndex];
            Vec3 normal = normalized(face.normal);
            if (lengthSq(normal) < 0.5) {
                normal = normalized(loopAreaVector(face.loop, work.vertices));
            }
            if (lengthSq(normal) < 0.5) {  // hopeless face: its own cluster
                clusters.push_back(Cluster{Vec3{}, 0.0, {faceIndex}});
                continue;
            }
            const double offset = dot(normal, vertexAt(work.vertices, face.loop[0]));

            bool placed = false;
            for (Cluster& cluster : clusters) {
                if (dot(cluster.normal, normal) > 1.0 - 1e-6 &&
                    std::fabs(cluster.offset - offset) <= kPlaneTol) {
                    cluster.faces.push_back(faceIndex);
                    placed = true;
                    break;
                }
            }
            if (!placed) clusters.push_back(Cluster{normal, offset, {faceIndex}});
        }

        for (const Cluster& cluster : clusters) {
            const PolyFace& prototype = work.faces[cluster.faces.front()];
            std::vector<std::vector<int>> fragments;
            fragments.reserve(cluster.faces.size());
            for (std::size_t faceIndex : cluster.faces) fragments.push_back(work.faces[faceIndex].loop);

            std::vector<std::vector<int>> loops;
            bool merged = lengthSq(cluster.normal) > 0.5 &&
                          mergeRegion(fragments, frameFor(cluster.normal), work.vertices, welder, 0,
                                      loops);
            if (merged) {
                for (const std::vector<int>& loop : loops) {
                    if (!isSimpleLoop(loop)) {
                        merged = false;
                        break;
                    }
                }
            }

            if (!merged) {
                // Fallback: keep the group's healed fragments; volume and watertightness survive.
                for (std::size_t faceIndex : cluster.faces) mergedFaces.push_back(work.faces[faceIndex]);
                continue;
            }

            for (std::vector<int>& loop : loops) {
                normalizeLoopStart(loop, work.vertices, cluster.normal);
            }
            std::sort(loops.begin(), loops.end());
            for (std::vector<int>& loop : loops) {
                PolyFace face;
                face.loop = std::move(loop);
                face.normal = prototype.normal;
                face.operandIndex = prototype.operandIndex;
                face.sourceFaceId = prototype.sourceFaceId;
                mergedFaces.push_back(std::move(face));
            }
        }
    }

    MeshSpec result;
    result.vertices = std::move(work.vertices);
    result.faces = std::move(mergedFaces);
    // Compact BEFORE the second healing pass, or dead interior vertices get healed back in. That
    // pass exists for the hole-split cut points, which sit on neighbouring faces' boundary edges.
    result = compactMesh(result);
    healTVertices(result);
    return result;
}

bool meshIsWatertight(const MeshSpec& mesh) {
    std::map<std::pair<int, int>, int> counts;
    for (const PolyFace& face : mesh.faces) {
        if (face.loop.size() < 3) return false;
        const std::size_t n = face.loop.size();
        for (std::size_t i = 0; i < n; ++i) {
            const int from = face.loop[i];
            const int to = face.loop[(i + 1) % n];
            if (from < 0 || static_cast<std::size_t>(from) >= mesh.vertices.size()) return false;
            if (to < 0 || static_cast<std::size_t>(to) >= mesh.vertices.size()) return false;
            if (from == to) return false;
            ++counts[std::make_pair(from, to)];
        }
    }

    for (const auto& [edge, count] : counts) {
        if (count != 1) return false;
        const auto reverse = counts.find(std::make_pair(edge.second, edge.first));
        if (reverse == counts.end() || reverse->second != 1) return false;
    }
    return true;
}

Result outerShell(const Operand& a, const Operand& b) {
    Result result;
    const Result unioned = apply(a, b, Op::Union);
    if (!unioned.ok) return result;  // ok stays false

    const MeshSpec& mesh = unioned.mesh;
    if (mesh.faces.empty()) {
        result.ok = true;
        return result;
    }

    const std::vector<std::vector<std::size_t>> shells = connectedShells(mesh);

    // Ray origin offset along the face normal, scaled to the model so it clears its own face.
    Vec3 low{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
             std::numeric_limits<double>::max()};
    Vec3 high{std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(),
              std::numeric_limits<double>::lowest()};
    for (const Vec3& p : mesh.vertices) {
        low = Vec3{std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
        high = Vec3{std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
    }
    const double offset = 1e-6 * std::max(1.0, length(high - low));

    std::vector<bool> keep(shells.size(), true);
    for (std::size_t s = 0; s < shells.size() && shells.size() > 1; ++s) {
        // Ray origins: the shell's largest fragments; a convex fragment's vertex average is inside it.
        std::vector<std::size_t> origins = shells[s];
        std::stable_sort(origins.begin(), origins.end(),
                         [&mesh](std::size_t lhs, std::size_t rhs) {
                             return length(loopAreaVector(mesh.faces[lhs].loop, mesh.vertices)) >
                                    length(loopAreaVector(mesh.faces[rhs].loop, mesh.vertices));
                         });
        if (origins.size() > kMaxShellRayOrigins) origins.resize(kMaxShellRayOrigins);

        bool resolved = false;
        int depth = 0;
        for (std::size_t faceIndex : origins) {
            const PolyFace& face = mesh.faces[faceIndex];
            Vec3 centroid{};
            for (int index : face.loop) centroid = centroid + vertexAt(mesh.vertices, index);
            centroid = centroid * (1.0 / static_cast<double>(face.loop.size()));
            const Vec3 normal = normalized(face.normal);
            if (lengthSq(normal) < 0.5) continue;
            const Vec3 origin = centroid + normal * offset;

            for (const Vec3& jitter : kRayJitters) {
                const Vec3 direction = normalized(normal + jitter);
                if (lengthSq(direction) < 0.5) continue;

                bool ambiguous = false;
                int contained = 0;
                for (std::size_t other = 0; other < shells.size(); ++other) {
                    if (other == s) continue;
                    const int crossings =
                        countRayCrossings(mesh, shells[other], origin, direction, ambiguous);
                    if (ambiguous) break;
                    if ((crossings % 2) != 0) ++contained;
                }
                if (ambiguous) continue;  // re-cast with the next fixed jitter
                depth = contained;
                resolved = true;
                break;
            }
            if (resolved) break;
        }

        // Unresolvable shells are KEPT: one face too many is recoverable, one missing is not (the
        // result would stop being watertight).
        keep[s] = !resolved || depth == 0;
    }

    MeshSpec external;
    external.vertices = mesh.vertices;
    for (std::size_t s = 0; s < shells.size(); ++s) {
        if (!keep[s]) continue;
        for (std::size_t faceIndex : shells[s]) external.faces.push_back(mesh.faces[faceIndex]);
    }

    result.mesh = mergeCoplanarFaces(external);
    result.ok = true;
    return result;
}

SplitResult split(const Operand& a, const Operand& b) {
    SplitResult result;

    // Three independent apply() runs: the op sequences mutate their trees in place (invert/clipTo).
    const Result aMinusB = apply(a, b, Op::Subtract);
    if (!aMinusB.ok) return result;
    const Result bMinusA = apply(b, a, Op::Subtract);
    if (!bMinusA.ok) return result;
    const Result intersection = apply(a, b, Op::Intersect);
    if (!intersection.ok) return result;

    result.aMinusB = mergeCoplanarFaces(aMinusB.mesh);
    result.bMinusA = mergeCoplanarFaces(bMinusA.mesh);
    result.aIntersectB = mergeCoplanarFaces(intersection.mesh);
    result.ok = true;
    return result;
}

}  // namespace plnr::geo::csg
