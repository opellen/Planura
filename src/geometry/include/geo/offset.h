#pragma once

// Pure offset-polyline algorithm: a planar loop or edge chain pushed sideways by a signed distance.

#include <vector>

#include <geo/vec3.h>

namespace plnr::geo {

// points is implicitly closed (last point not repeated); empty when ok = false.
struct OffsetResult {
    std::vector<Vec3> points;
    bool ok{};
};

// Offsets loop within its plane. Sign: for a loop CCW about planeNormal, positive moves inward.
// Corners are mitered; open ends never join. keepOverlaps=false drops segments inverted past
// collapse and re-miters their neighbours. ok = false on |distance| <= kMergeTol or degeneracy.
OffsetResult offsetLoop(const std::vector<Vec3>& loop, const Vec3& planeNormal, double distance, bool closed,
                         bool keepOverlaps);

}  // namespace plnr::geo
