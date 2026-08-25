#pragma once

// Pure offset-polyline algorithm: a closed face loop or coplanar edge chain pushed sideways within
// its own plane by a signed distance, with mitered joins and self-overlap cleanup. Qt/Model-free.

#include <vector>

#include <geo/vec3.h>

namespace plnr::geo {

// points is the offset polyline, implicitly closed with the last point NOT repeated (same as
// LoopCandidate::vertexLoop). Empty with ok=false on any guard rejection or degenerate result.
struct OffsetResult {
    std::vector<Vec3> points;
    bool ok{};
};

// Offsets loop (closed face boundary, or open/closed coplanar edge chain) by distance within its own
// plane (normal planeNormal). Sign: for a CCW loop against planeNormal, positive moves INWARD.
// Corners are true miter joins, near-parallel pairs share a translated point, open ends never join.
// keepOverlaps=false (default) drops any segment whose direction inverted past collapse, re-mitering
// its neighbours. {.ok=false}: loop too short, |distance| <= kMergeTol, degenerate normal or result.
OffsetResult offsetLoop(const std::vector<Vec3>& loop, const Vec3& planeNormal, double distance, bool closed,
                         bool keepOverlaps);

}  // namespace plnr::geo
