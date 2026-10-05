#include <geo/follow.h>

#include <algorithm>
#include <cmath>

#include <geo/scene.h>  // geo::Transform -- used internally only (applyVector, .composed), never exposed

namespace plnr::geo {

namespace {

// Newell's method: robust normal for a (possibly non-convex) planar polygon;
// degenerate/collinear input yields the zero vector. Local copy of Model's
// own private newellNormal (model.cpp) since follow.h stays Model-free.
Vec3 newellNormal(const std::vector<Vec3>& positions) {
    Vec3 sum{0.0, 0.0, 0.0};
    const std::size_t n = positions.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Vec3& p1 = positions[i];
        const Vec3& p2 = positions[(i + 1) % n];
        sum.x += (p1.y - p2.y) * (p1.z + p2.z);
        sum.y += (p1.z - p2.z) * (p1.x + p2.x);
        sum.z += (p1.x - p2.x) * (p1.y + p2.y);
    }
    return sum;
}

}  // namespace

FollowResult sweep(const std::vector<Vec3>& profile, const std::vector<Vec3>& path, bool closedPath) {
    if (profile.size() < 3 || path.size() < 2) {
        return {};
    }

    const std::size_t n = path.size();
    const std::size_t segCount = closedPath ? n : n - 1;

    // Segment directions dir[k] = normalized(path[k+1] - path[k]), wrapping
    // for the last (closedPath-only) segment. Guards a zero-length segment
    // (including the wrap) before any transport math runs.
    std::vector<Vec3> dirs(segCount);
    for (std::size_t k = 0; k < segCount; ++k) {
        const Vec3 seg = path[(k + 1) % n] - path[k];
        if (length(seg) < kEps) {
            return {};  // degenerate/duplicate-point path segment
        }
        dirs[k] = normalized(seg);
    }

    if (length(newellNormal(profile)) < kEps) {
        return {};  // degenerate profile: collinear or zero-area
    }

    // R[k]: the accumulated rotation (pure linear map, translation unused --
    // only ever consumed via applyVector below) anchoring section k. See
    // follow.h's own header comment for the exact joint/composition rule.
    std::vector<Transform> rotations(n);
    rotations[0] = Transform::identity();
    for (std::size_t k = 1; k < n; ++k) {
        if (k >= segCount) {
            // Open path's last vertex only: no outgoing segment, no further
            // increment -- inherits the previous section's rotation as-is.
            rotations[k] = rotations[k - 1];
            continue;
        }
        const Vec3& inDir = dirs[k - 1];
        const Vec3& outDir = dirs[k];
        const Vec3 axis = cross(inDir, outDir);
        if (length(axis) < kEps) {
            // Near-parallel joint (collinear or reversed) -- no increment.
            rotations[k] = rotations[k - 1];
            continue;
        }
        const double cosAngle = std::clamp(dot(inDir, outDir), -1.0, 1.0);
        const double angle = std::acos(cosAngle);
        const Transform increment = Transform::rotation(Vec3{}, axis, angle);
        rotations[k] = increment.composed(rotations[k - 1]);
    }

    FollowResult result;
    result.sections.resize(n);
    for (std::size_t k = 0; k < n; ++k) {
        result.sections[k].resize(profile.size());
        for (std::size_t i = 0; i < profile.size(); ++i) {
            const Vec3 offset = profile[i] - path[0];
            result.sections[k][i] = path[k] + rotations[k].applyVector(offset);
        }
    }

    const std::size_t profileSize = profile.size();
    const std::size_t stripCount = closedPath ? n : n - 1;  // number of (k, k+1) section pairs, wrap included
    result.sideQuads.reserve(stripCount * profileSize);
    for (std::size_t k = 0; k < stripCount; ++k) {
        const std::size_t k2 = (k + 1) % n;
        for (std::size_t i = 0; i < profileSize; ++i) {
            const std::size_t i2 = (i + 1) % profileSize;
            result.sideQuads.push_back({
                static_cast<int>(k * profileSize + i),
                static_cast<int>(k * profileSize + i2),
                static_cast<int>(k2 * profileSize + i2),
                static_cast<int>(k2 * profileSize + i),
            });
        }
    }

    result.ok = true;
    return result;
}

}  // namespace plnr::geo
