#pragma once

// Pure Follow Me sweep kernel: per-path-vertex profile sections plus the side-wall quads between them.

#include <array>
#include <vector>

#include <geo/vec3.h>

namespace plnr::geo {

// sections[k] = the profile loop (profile order) transported to path[k]. sideQuads hold flat
// indices section * profile.size() + vertexIndex. ok = false leaves both empty.
struct FollowResult {
    std::vector<std::vector<Vec3>> sections;
    std::vector<std::array<int, 4>> sideQuads;
    bool ok{};
};

// Sweeps the closed profile along the path with rotation-minimizing frames. No miter, so sharp
// corners pinch; section[0] == profile, so a closed lap whose rotation doesn't return to identity
// leaves a seam. ok = false on a degenerate profile or path segment (wrap included).
FollowResult sweep(const std::vector<Vec3>& profile, const std::vector<Vec3>& path, bool closedPath);

}  // namespace plnr::geo
