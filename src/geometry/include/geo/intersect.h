#pragma once

// Pure segment x segment / segment x plane intersection math, Model/Qt-free
// (same "pure kernel, no Model mutation" shape as loop_finder.h/offset.h) --
// GeometryApi's splitting pass turns a hit here into a Model::splitEdge call.

#include <optional>

#include <geo/vec3.h>

namespace plnr::geo {

// Coplanar proper-crossing point of segments a1-a2 and b1-b2, or nullopt: either
// segment degenerate, lines parallel/collinear, or the lines' closest points
// farther apart than tol (non-coplanar) -- else returns their midpoint. Also
// nullopt if the crossing isn't interior to BOTH segments by more than tol,
// which rejects a shared-endpoint "T" touch the same as an out-of-span crossing.
std::optional<Vec3> segmentIntersect(Vec3 a1, Vec3 a2, Vec3 b1, Vec3 b2, double tol);

// Crossing point of segment a-b against the infinite plane through planePoint
// with normal planeNormal (need not be unit), or nullopt if the segment is
// degenerate/(near-)parallel to the plane. Unlike segmentIntersect, a crossing
// within tol of either endpoint IS still returned -- there's no second segment
// for it to "T" against, so an endpoint-tip split is valid here.
std::optional<Vec3> segmentFacePlaneIntersect(Vec3 a, Vec3 b, Vec3 planePoint, Vec3 planeNormal, double tol);

}  // namespace plnr::geo
