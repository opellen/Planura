#pragma once

#include <array>
#include <functional>
#include <vector>

#include <geo/entity.h>
#include <geo/model.h>
#include <geo/pick.h>

namespace plnr::geo {

// Edge ids in loop order; empty if the face is unknown or its cycle never closes.
std::vector<Id> faceBoundaryEdges(const Model& model, Id faceId);

// Everything reachable from a seed via half-edge adjacency; each vector sorted by id.
struct ConnectedSet {
    std::vector<Id> vertices;
    std::vector<Id> edges;
    std::vector<Id> faces;
};

// Empty if seedId is not a known entity of kind.
ConnectedSet connectedComponent(const Model& model, EntityKind kind, Id seedId);

// p is inside when dot(normal, p) + d >= 0, so normal points INWARD (not the usual convention).
struct Plane {
    Vec3 normal;
    double d{};
};

// Screen-rect drag region: 4 side planes through one eye point, no near/far bound.
struct Frustum {
    std::array<Plane, 4> planes;
};

// corners: pick rays from one eye point, ordered TL, TR, BR, BL.
Frustum frustumFromCornerRays(const std::array<Ray, 4>& corners);

// Window (fully contained) vs Crossing (merely touching) region-select semantics.
enum class RegionMode { Window, Crossing };

// Region-selection result; each list sorted ascending by id.
struct RegionPick {
    std::vector<Id> vertices, edges, faces;
};

// Edge: Window needs both endpoints inside, Crossing an endpoint inside or a non-empty clipped
// interval. Face: Window needs every boundary vertex inside, Crossing any qualifying boundary edge
// (interior-only overlap is not selected).
RegionPick pickInFrustum(const Model& model, const Frustum& f, RegionMode mode,
                         const std::function<bool(EntityKind, Id)>& filter = {});

}  // namespace plnr::geo
