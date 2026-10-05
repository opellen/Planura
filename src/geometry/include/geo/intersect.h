#pragma once

// Pure segment/segment and segment/plane intersection math; no Model access.

#include <optional>

#include <geo/vec3.h>

namespace plnr::geo {

// Proper crossing of a1-a2 and b1-b2 (midpoint of the lines' closest points, which must be within
// tol). The crossing must be interior to BOTH segments by more than tol, so a T touch is nullopt.
std::optional<Vec3> segmentIntersect(Vec3 a1, Vec3 a2, Vec3 b1, Vec3 b2, double tol);

// Crossing of segment a-b with an infinite plane (planeNormal need not be unit); nullopt if
// near-parallel. Unlike segmentIntersect, a crossing within tol of an endpoint is returned.
std::optional<Vec3> segmentFacePlaneIntersect(Vec3 a, Vec3 b, Vec3 planePoint, Vec3 planeNormal, double tol);

}  // namespace plnr::geo
