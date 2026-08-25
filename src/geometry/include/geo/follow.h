#pragma once

// Pure Follow Me sweep kernel: a profile point loop swept along a path polyline, producing
// per-path-vertex profile "sections" plus the side-wall quads between consecutive sections.

#include <array>
#include <vector>

#include <geo/vec3.h>

namespace plnr::geo {

// sections[k] is profile's vertex loop, in profile order, transported to path[k]. A sideQuads entry
// is 4 flat indices numbered section * profileSize + vertexIndex, profileSize == profile.size().
// ok=false (both vectors empty) on any guard rejection below.
struct FollowResult {
    std::vector<std::vector<Vec3>> sections;
    std::vector<std::array<int, 4>> sideQuads;
    bool ok{};
};

// Sweeps profile (implicitly closed loop) along path (open or closed polyline) via discrete
// rotation-minimizing frames; no miter, so sharp path corners pinch the mesh. section[0] == profile
// unchanged and is never re-derived at a closed path's wrap joint, so a lap whose rotation does not
// return to identity leaves a seam. {.ok=false}: profile.size() < 3, path.size() < 2, a degenerate
// consecutive segment (wrap included), or profile's Newell normal < kEps.
FollowResult sweep(const std::vector<Vec3>& profile, const std::vector<Vec3>& path, bool closedPath);

}  // namespace plnr::geo
