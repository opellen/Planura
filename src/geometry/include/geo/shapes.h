#pragma once

#include <vector>

#include <geo/vec3.h>

namespace plnr::geo {

// Vertex 0 lies along startDir (any perpendicular if it projects to nothing). circumscribed scales
// by 1/cos(pi/n) so edges, not vertices, sit at radius. Empty if segments < 3 or degenerate.
std::vector<Vec3> regularPolygonPoints(Vec3 center, double radius, Vec3 normal, int segments, Vec3 startDir,
                                        bool circumscribed);

// Segment count for every arc tool, whatever the sweep, unless overridden in the VCB.
inline constexpr int kArcDefaultSegments = 12;

// Arc a -> b with signed sagitta `bulge`; positive bows toward cross(normal, b - a), and major arcs
// (bulge > half-chord) work. segments+1 points, first == a, last == b. Empty if degenerate,
// including normal parallel to b - a.
std::vector<Vec3> arcPoints2Point(Vec3 a, Vec3 b, double bulge, Vec3 normal, int segments);

// Rotates (startPoint - center) about normal by signed sweepRad (right-hand rule): segments+1
// points, except a full 2*pi sweep gives `segments` (no duplicate of the first). Empty if
// degenerate or |sweepRad| > 2*pi.
std::vector<Vec3> arcPointsCenter(Vec3 center, Vec3 startPoint, double sweepRad, Vec3 normal, int segments);

// Arc on the circle through p1, p2, p3, swept the way that passes p2: segments+1 points, p1 first,
// p3 last. Empty if points coincide or are collinear.
std::vector<Vec3> arcPoints3Point(Vec3 p1, Vec3 p2, Vec3 p3, int segments);

}  // namespace plnr::geo
