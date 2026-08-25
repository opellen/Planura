#pragma once

#include <array>
#include <functional>
#include <vector>

#include <geo/entity.h>
#include <geo/model.h>
#include <geo/pick.h>

namespace plnr::geo {

// Edge ids around faceId's half-edge cycle, in loop order. Empty if faceId is unknown or the cycle
// is malformed and never closes.
std::vector<Id> faceBoundaryEdges(const Model& model, Id faceId);

// Every vertex/edge/face reachable from a seed through the half-edge adjacency graph. Each vector is
// sorted ascending by id.
struct ConnectedSet {
    std::vector<Id> vertices;
    std::vector<Id> edges;
    std::vector<Id> faces;
};

// BFS over the connectivity graph from (kind, seedId): vertex<->edge via outgoing half-edges and an
// edge's endpoints, edge<->face via each half-edge's face link and a face's boundary loop. Empty set
// if seedId is not a known entity of kind.
ConnectedSet connectedComponent(const Model& model, EntityKind kind, Id seedId);

// One side of a Frustum: p is inside when dot(normal, p) + d >= 0. normal points INWARD, unlike the
// usual outward-facing plane convention.
struct Plane {
    Vec3 normal;
    double d{};
};

// Convex region swept by a screen-rect drag: 4 side planes sharing one eye point, no near/far bound.
// A point is inside the frustum when it is inside all 4 planes.
struct Frustum {
    std::array<Plane, 4> planes;
};

// Builds a Frustum from 4 screen-rect corner pick rays sharing one eye point, ordered TL, TR, BR, BL.
// Each side plane comes from the eye plus two adjacent corners; its normal is oriented inward by
// testing against the opposite pair's average direction and flipping if needed.
Frustum frustumFromCornerRays(const std::array<Ray, 4>& corners);

// Window (fully contained) vs Crossing (merely touching) region-select semantics.
enum class RegionMode { Window, Crossing };

// Region-selection result; each list sorted ascending by id.
struct RegionPick {
    std::vector<Id> vertices, edges, faces;
};

// Selects every vertex/edge/face touching (Crossing) or fully inside (Window) frustum f. Vertex:
// inside all 4 planes either way. Edge: Window needs both endpoints inside, Crossing either endpoint
// inside or a non-empty clipped interval. Face: Window needs every boundary vertex inside, Crossing
// any qualifying boundary edge -- interior-only overlap is not selected. Results sort ascending by id.
RegionPick pickInFrustum(const Model& model, const Frustum& f, RegionMode mode,
                         const std::function<bool(EntityKind, Id)>& filter = {});

}  // namespace plnr::geo
