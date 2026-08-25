#pragma once

#include <vector>

#include <geo/vec3.h>

namespace plnr::geo {

// Regular polygon in the plane through center with normal; vertex 0 sits along startDir (an
// arbitrary perpendicular if that projects to nothing). circumscribed=true scales by 1/cos(pi/n) so
// EDGES sit at radius instead of vertices. Empty if segments < 3, radius <= kMergeTol, normal ~0.
std::vector<Vec3> regularPolygonPoints(Vec3 center, double radius, Vec3 normal, int segments, Vec3 startDir,
                                        bool circumscribed);

// Default segment count for every arc tool regardless of sweep angle, independent of Circle's 24.
// Callers should pass this unless the user overrode the count via the VCB.
inline constexpr int kArcDefaultSegments = 12;

// Arc from chord endpoint a to b with signed sagitta `bulge` (chord midpoint to arc midpoint);
// positive bows toward normalized(cross(normal, b - a)). Major arcs (bulge > half-chord) work.
// Returns segments+1 points evenly spaced by angle, first == a, last == b. Empty if |b-a| or |bulge|
// <= kMergeTol, segments < 1, or normal is near-zero or parallel to (b - a).
std::vector<Vec3> arcPoints2Point(Vec3 a, Vec3 b, double bulge, Vec3 normal, int segments);

// Arc from rotating (startPoint - center) about `normal` by signed sweepRad (right-hand rule):
// segments+1 points starting at startPoint -- EXCEPT a full 2*pi sweep (within kEps), which returns
// only `segments` points, dropping the duplicate of the first. Empty if |startPoint - center| <=
// kMergeTol, normal near-zero, |sweepRad| < kEps or > 2*pi + kEps, or segments < 1.
std::vector<Vec3> arcPointsCenter(Vec3 center, Vec3 startPoint, double sweepRad, Vec3 normal, int segments);

// Arc along the circle through p1, p2, p3, swept whichever way (minor or major) passes through p2:
// segments+1 points, p1 first, p3 last. Empty if any two points are within kMergeTol, the three are
// collinear, or segments < 1.
std::vector<Vec3> arcPoints3Point(Vec3 p1, Vec3 p2, Vec3 p3, int segments);

}  // namespace plnr::geo
